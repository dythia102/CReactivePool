/**
 * @file object_pool.c
 * @brief Implementation of a thread-safe object pool with dynamic resizing and loadbalancing.
 *
 * This file implements the object pool library defined in object_pool.h. The pool manages
 * reusable objects across multiple sub-pools for load balancing, using POSIX mutexes for
 * thread safety. Key features include:
 * - O(1) object release via compact metadata.
 * - Random sub-pool selection in pool_acquire for reduced contention.
 * - Dynamic pool and queue resizing.
 * - Backpressure handling with callbacks.
 * - Custom allocators for flexible object management.
 * - Detailed statistics (e.g., contention time, acquire counts).
 *
 * The implementation is optimized for high-performance, low-memory applications, with
 * robust error handling and thread-safe operations.
 *
 * Debug output can be enabled by defining DEBUG (e.g., -DDEBUG in compiler flags).
 */

 #include "object_pool.h"
 #include <stdio.h>
 #include <string.h>   // For memset
 #include <stdint.h>   // For uint64_t, uint32_t
 #include <pthread.h>
 #include <time.h>     // For clock_gettime
 
 /**
  * @brief Sub-pool structure for managing a subset of objects.
  *
  * Each sub-pool contains an array of objects and tracks usage, contention, and statistics.
  * Thread-safe using a mutex.
  */
 struct sub_pool {
     void** objects;               // Array of user object pointers (point to user data, not metadata)
     bool* used;                   // Track object usage
     size_t pool_size;             // Number of objects in sub-pool
     size_t used_count;            // Number of used objects
     size_t max_used;              // Max concurrent objects in this sub-pool
     size_t acquire_count;         // Total acquire operations
     size_t release_count;         // Total release operations
     size_t contention_attempts;   // Total mutex contention attempts
     uint64_t total_contention_time_ns; // Total mutex wait time
     pthread_mutex_t mutex;        // Mutex for thread safety
 };
 
 /**
  * @brief Acquire request for backpressure queue.
  */
 typedef struct {
     object_pool_acquire_callback_t callback; // Callback to invoke when object is available
     void* context;                           // User-provided context for callback
 } acquire_request_t;
 
 /**
  * @brief Main pool structure managing sub-pools and backpressure queue.
  *
  * Thread-safe using a queue mutex and per-sub-pool mutexes.
  */
 struct object_pool {
     sub_pool_t* sub_pools;        // Array of sub-pools
     size_t sub_pool_count;        // Number of sub-pools
     size_t total_objects_allocated; // Total objects allocated
     size_t grow_count;            // Number of grow operations
     size_t shrink_count;          // Number of shrink operations
     acquire_request_t* request_queue; // Backpressure queue
     size_t queue_size;            // Current queue size
     size_t queue_capacity;        // Max queue size
     size_t queue_max_size;        // Max observed queue size
     size_t queue_grow_count;      // Number of queue growth operations
     size_t max_used;              // Max concurrent objects across all sub-pools
     object_pool_allocator_t allocator; // Allocator for objects
     object_pool_error_callback_t error_callback; // Error callback
     void* error_context;          // Error callback context
     pthread_mutex_t queue_mutex;  // Mutex for request_queue
 };
 
 /**
  * @brief Thread-local random number generator state for sub-pool selection.
  */
 typedef struct {
     uint64_t state; // Current state of the LCG
 } thread_rng_t;
 
 static __thread thread_rng_t rng_state = {0};
 
 /**
  * @brief Gets high-resolution time in nanoseconds.
  *
  * Uses clock_gettime for monotonic time.
  * @return Nanoseconds since an unspecified epoch.
  */
 static uint64_t get_hrtime(void) {
     struct timespec ts;
     if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
         return 0; // Fallback in case of error
     }
     return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
 }
 
 /**
  * @brief Initializes the thread-local random number generator.
  *
  * Seeds the generator with a combination of high-resolution time and thread ID.
  */
 static void init_rng(void) {
     if (rng_state.state == 0) {
         rng_state.state = get_hrtime() ^ (uint64_t)pthread_self();
     }
 }
 
 /**
  * @brief Generates the next random number using a linear congruential generator (LCG).
  *
  * @return A 32-bit random number.
  * @threadsafe
  */
 static uint32_t next_random(void) {
     init_rng();
     rng_state.state = rng_state.state * 6364136223846793005ULL + 1442695040888963407ULL;
     return (uint32_t)(rng_state.state >> 32);
 }
 
 /**
  * @brief Retrieves metadata from a user object pointer.
  *
  * @param pool The pool containing sub-pools.
  * @param user_obj The user object pointer.
  * @param sub_pool Output pointer to the sub-pool.
  * @param index Output index in sub-pool's objects array.
  */
 static inline void get_metadata(object_pool_t* pool, void* user_obj, sub_pool_t** sub_pool, size_t* index) {
     if (!user_obj || !pool) {
         *sub_pool = NULL;
         *index = 0;
         return;
     }
     pool_object_metadata_t* metadata = (pool_object_metadata_t*)((char*)user_obj - sizeof(pool_object_metadata_t));
     *index = metadata->packed & 0xFFFFFFFFFFFFULL; // Lower 48 bits
     size_t sub_pool_id = metadata->packed >> 48; // Upper 16 bits
     if (sub_pool_id >= pool->sub_pool_count) {
         *sub_pool = NULL;
         return;
     }
     *sub_pool = &pool->sub_pools[sub_pool_id];
 }
 
 /**
  * @brief Default allocator for generic memory blocks.
  *
  * Allocates memory for metadata and a user object of specified size, initializing
  * the object to zero.
  *
  * @param user_data Pointer to the object size (size_t).
  * @return Pointer to the user object, or NULL on failure.
  */
 static void* default_alloc(void* user_data) {
     size_t object_size = user_data ? *(size_t*)user_data : DEFAULT_OBJECT_SIZE;
     // Allocate space for metadata + user object
     void* block = malloc(sizeof(pool_object_metadata_t) + object_size);
     if (!block) {
         return NULL;
     }
     // Initialize metadata to safe defaults
     pool_object_metadata_t* metadata = (pool_object_metadata_t*)block;
     metadata->packed = 0;
     // Initialize user object to zero
     void* user_obj = (char*)block + sizeof(pool_object_metadata_t);
     memset(user_obj, 0, object_size);
     return user_obj;
 }
 
 /**
  * @brief Default deallocator for generic memory blocks.
  *
  * Frees the entire block (metadata + user object).
  *
  * @param user_obj The user object to free.
  * @param user_data Unused.
  */
 static void default_free(void* user_obj, void* user_data) {
     (void)user_data; // Unused
     if (user_obj) {
         // Free the entire block (metadata + user object)
         free((char*)user_obj - sizeof(pool_object_metadata_t));
     }
 }
 
 /**
  * @brief Default reset function for generic objects.
  *
  * Resets the object to zero.
  *
  * @param user_obj The user object to reset.
  * @param user_data Pointer to the object size (size_t).
  */
 static void default_reset(void* user_obj, void* user_data) {
     if (user_obj) {
         size_t object_size = user_data ? *(size_t*)user_data : DEFAULT_OBJECT_SIZE;
         memset(user_obj, 0, object_size); // Reset user object to zero
     }
 }
 
 /**
  * @brief Default validation function for generic objects.
  *
  * Checks if the object pointer is non-NULL.
  *
  * @param user_obj The user object to validate.
  * @param user_data Unused.
  * @return true if valid, false otherwise.
  */
 static bool default_validate(void* user_obj, void* user_data) {
     (void)user_data; // Unused
     return user_obj != NULL; // Basic validation: non-NULL pointer
 }
 
 /**
  * @brief Default on-create callback (no-op).
  *
  * @param user_obj The created object.
  * @param user_data Unused.
  */
 static void default_on_create(void* user_obj, void* user_data) {
     (void)user_obj;
     (void)user_data;
 }
 
 /**
  * @brief Default on-destroy callback (no-op).
  *
  * @param user_obj The object to destroy.
  * @param user_data Unused.
  */
 static void default_on_destroy(void* user_obj, void* user_data) {
     (void)user_obj;
     (void)user_data;
 }
 
 /**
  * @brief Default on-reuse callback (no-op).
  *
  * @param user_obj The object to reuse.
  * @param user_data Unused.
  */
 static void default_on_reuse(void* user_obj, void* user_data) {
     (void)user_obj;
     (void)user_data;
 }
 
 /**
  * @brief Reports an error via callback or stderr.
  *
  * @param pool The pool (may be NULL).
  * @param error The error type.
  * @param message The error message.
  */
 static void report_error(object_pool_t* pool, object_pool_error_t error, const char* message) {
     if (pool && pool->error_callback) {
         pool->error_callback(error, message, pool->error_context);
     } else {
         fprintf(stderr, "%s\n", message);
     }
 }
 
 /**
  * @brief Creates a thread-safe object pool with specified parameters.
  *
  * @param pool_size Total number of objects (must be > 0).
  * @param sub_pool_count Number of sub-pools (must be > 0).
  * @param allocator Custom allocator for object management.
  * @param error_callback Optional callback for error reporting.
  * @param error_context User context for error callback.
  * @return Pointer to the created pool, or NULL on failure.
  * @threadsafe
  */
 object_pool_t* pool_create(size_t pool_size, size_t sub_pool_count, object_pool_allocator_t allocator,
                            object_pool_error_callback_t error_callback, void* error_context) {
     if (pool_size == 0 || sub_pool_count == 0 || !allocator.alloc || !allocator.free) {
         if (error_callback) {
             error_callback(POOL_ERROR_INVALID_SIZE, "Invalid pool size, sub-pool count, or allocator", error_context);
         } else {
             fprintf(stderr, "Invalid pool size, sub-pool count, or allocator\n");
         }
         return NULL;
     }
     if (sub_pool_count > 0xFFFF) {
         report_error(NULL, POOL_ERROR_INVALID_SIZE, "Sub-pool count exceeds 2^16");
         return NULL;
     }
 
     object_pool_t* pool = malloc(sizeof(object_pool_t));
     if (!pool) {
         report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate pool");
         return NULL;
     }
 
     pool->sub_pools = malloc(sub_pool_count * sizeof(sub_pool_t));
     if (!pool->sub_pools) {
         report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate sub-pools");
         free(pool);
         return NULL;
     }
 
     pool->request_queue = malloc(DEFAULT_QUEUE_CAPACITY * sizeof(acquire_request_t));
     if (!pool->request_queue) {
         report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate request queue");
         free(pool->sub_pools);
         free(pool);
         return NULL;
     }
     memset(pool->request_queue, 0, DEFAULT_QUEUE_CAPACITY * sizeof(acquire_request_t)); // Initialize queue
 
     pool->sub_pool_count = sub_pool_count;
     pool->total_objects_allocated = pool_size;
     pool->grow_count = 0;
     pool->shrink_count = 0;
     pool->queue_size = 0;
     pool->queue_capacity = DEFAULT_QUEUE_CAPACITY;
     pool->queue_max_size = 0;
     pool->queue_grow_count = 0;
     pool->max_used = 0; // Initialize global max_used
     pool->allocator = allocator;
     pool->error_callback = error_callback;
     pool->error_context = error_context;
     if (!pool->allocator.reset) pool->allocator.reset = default_reset;
     if (!pool->allocator.validate) pool->allocator.validate = default_validate;
     if (!pool->allocator.on_create) pool->allocator.on_create = default_on_create;
     if (!pool->allocator.on_destroy) pool->allocator.on_destroy = default_on_destroy;
     if (!pool->allocator.on_reuse) pool->allocator.on_reuse = default_on_reuse;
 
     if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0) {
         report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to initialize queue mutex");
         free(pool->request_queue);
         free(pool->sub_pools);
         free(pool);
         return NULL;
     }
 
     size_t base_size = pool_size / sub_pool_count;
     size_t remainder = pool_size % sub_pool_count;
     for (size_t i = 0; i < sub_pool_count; i++) {
         sub_pool_t* sub = &pool->sub_pools[i];
         sub->pool_size = base_size + (i < remainder ? 1 : 0);
         if (sub->pool_size == 0 && pool_size > 0) {
             sub->pool_size = 1; // Ensure at least one object per sub-pool if needed
         }
         if (sub->pool_size > 0xFFFFFFFFFFFFULL) {
             report_error(NULL, POOL_ERROR_INVALID_SIZE, "Sub-pool size exceeds 2^48");
             for (size_t j = 0; j < i; j++) {
                 for (size_t k = 0; k < pool->sub_pools[j].pool_size; k++) {
                     if (pool->sub_pools[j].objects[k]) {
                         pool->allocator.free(pool->sub_pools[j].objects[k], pool->allocator.user_data);
                     }
                 }
                 free(pool->sub_pools[j].objects);
                 free(pool->sub_pools[j].used);
                 pthread_mutex_destroy(&pool->sub_pools[j].mutex);
             }
             free(pool->sub_pools);
             free(pool->request_queue);
             pthread_mutex_destroy(&pool->queue_mutex);
             free(pool);
             return NULL;
         }
         sub->objects = malloc(sub->pool_size * sizeof(void*));
         sub->used = malloc(sub->pool_size * sizeof(bool));
         if (!sub->objects || !sub->used) {
             report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate sub-pool arrays");
             for (size_t j = 0; j < i; j++) {
                 for (size_t k = 0; k < pool->sub_pools[j].pool_size; k++) {
                     if (pool->sub_pools[j].objects[k]) {
                         pool->allocator.free(pool->sub_pools[j].objects[k], pool->allocator.user_data);
                     }
                 }
                 free(pool->sub_pools[j].objects);
                 free(pool->sub_pools[j].used);
                 pthread_mutex_destroy(&pool->sub_pools[j].mutex);
             }
             free(pool->sub_pools);
             free(pool->request_queue);
             pthread_mutex_destroy(&pool->queue_mutex);
             free(pool);
             return NULL;
         }
 
         if (pthread_mutex_init(&sub->mutex, NULL) != 0) {
             report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to initialize sub-pool mutex");
             for (size_t j = 0; j < i; j++) {
                 for (size_t k = 0; k < pool->sub_pools[j].pool_size; k++) {
                     if (pool->sub_pools[j].objects[k]) {
                         pool->allocator.free(pool->sub_pools[j].objects[k], pool->allocator.user_data);
                     }
                 }
                 free(pool->sub_pools[j].objects);
                 free(pool->sub_pools[j].used);
                 pthread_mutex_destroy(&pool->sub_pools[j].mutex);
             }
             free(pool->sub_pools);
             free(pool->request_queue);
             pthread_mutex_destroy(&pool->queue_mutex);
             free(pool);
             return NULL;
         }
 
         sub->used_count = 0;
         sub->max_used = 0;
         sub->acquire_count = 0;
         sub->release_count = 0;
         sub->contention_attempts = 0;
         sub->total_contention_time_ns = 0;
 
         for (size_t j = 0; j < sub->pool_size; j++) {
             sub->objects[j] = pool->allocator.alloc(pool->allocator.user_data);
             if (!sub->objects[j]) {
                 report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate object");
                 for (size_t k = 0; k < j; k++) {
                     if (sub->objects[k]) {
                         pool->allocator.free(sub->objects[k], pool->allocator.user_data);
                     }
                 }
                 for (size_t m = 0; m < i; m++) {
                     for (size_t n = 0; n < pool->sub_pools[m].pool_size; n++) {
                         if (pool->sub_pools[m].objects[n]) {
                             pool->allocator.free(pool->sub_pools[m].objects[n], pool->allocator.user_data);
                         }
                     }
                     free(pool->sub_pools[m].objects);
                     free(pool->sub_pools[m].used);
                     pthread_mutex_destroy(&pool->sub_pools[m].mutex);
                 }
                 free(sub->objects);
                 free(sub->used);
                 free(pool->sub_pools);
                 free(pool->request_queue);
                 pthread_mutex_destroy(&pool->queue_mutex);
                 free(pool);
                 return NULL;
             }
             // Initialize metadata
             pool_object_metadata_t* metadata = (pool_object_metadata_t*)((char*)sub->objects[j] - sizeof(pool_object_metadata_t));
             if (!metadata) {
                 report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to access object metadata");
                 for (size_t k = 0; k < j; k++) {
                     if (sub->objects[k]) {
                         pool->allocator.free(sub->objects[k], pool->allocator.user_data);
                     }
                 }
                 for (size_t m = 0; m < i; m++) {
                     for (size_t n = 0; n < pool->sub_pools[m].pool_size; n++) {
                         if (pool->sub_pools[m].objects[n]) {
                             pool->allocator.free(pool->sub_pools[m].objects[n], pool->allocator.user_data);
                         }
                     }
                     free(pool->sub_pools[m].objects);
                     free(pool->sub_pools[m].used);
                     pthread_mutex_destroy(&pool->sub_pools[m].mutex);
                 }
                 free(sub->objects);
                 free(sub->used);
                 free(pool->sub_pools);
                 free(pool->request_queue);
                 pthread_mutex_destroy(&pool->queue_mutex);
                 free(pool);
                 return NULL;
             }
             metadata->packed = ((uint64_t)i << 48) | j; // sub_pool_id | index
             sub->used[j] = false;
             pool->allocator.reset(sub->objects[j], pool->allocator.user_data);
             pool->allocator.on_create(sub->objects[j], pool->allocator.user_data);
         }
     }
 
     return pool;
 }
 
 /**
  * @brief Creates a pool with default settings (16 objects, 4 sub-pools, 1-byte objects).
  *
  * @return Pointer to the created pool, or NULL on failure.
  * @threadsafe
  */
 object_pool_t* pool_create_default(void) {
     return pool_create_default_with_size(1); // Maintain compatibility with original 1-byte allocation
 }
 
 /**
  * @brief Creates a pool with default settings and specified object size.
  *
  * @param object_size Size of each object (0 for default 64 bytes).
  * @return Pointer to the created pool, or NULL on failure.
  * @threadsafe
  */
 object_pool_t* pool_create_default_with_size(size_t object_size) {
     if (object_size == 0) {
         object_size = DEFAULT_OBJECT_SIZE;
     }
     size_t* object_size_ptr = malloc(sizeof(size_t));
     if (!object_size_ptr) {
         fprintf(stderr, "Failed to allocate object size for default allocator\n");
         return NULL;
     }
     *object_size_ptr = object_size;
 
     object_pool_allocator_t allocator = {
         .alloc = default_alloc,
         .free = default_free,
         .reset = default_reset,
         .validate = default_validate,
         .on_create = default_on_create,
         .on_destroy = default_on_destroy,
         .on_reuse = default_on_reuse,
         .user_data = object_size_ptr
     };
     object_pool_t* pool = pool_create(DEFAULT_POOL_SIZE, DEFAULT_SUB_POOL_COUNT, allocator, NULL, NULL);
     if (!pool) {
         free(object_size_ptr);
     }
     return pool;
 }
 
 /**
  * @brief Grows the pool by adding more objects.
  *
  * Distributes additional objects across sub-pools, initializing them with the allocator.
  *
  * @param pool The pool to grow.
  * @param additional_size Number of objects to add (must be > 0).
  * @return true on success, false on failure.
  * @threadsafe
  */
 bool pool_grow(object_pool_t* pool, size_t additional_size) {
     if (!pool || additional_size == 0) {
         report_error(pool, POOL_ERROR_INVALID_SIZE, "Invalid pool or size");
         return false;
     }
 
     size_t base_add = additional_size / pool->sub_pool_count;
     size_t remainder = additional_size % pool->sub_pool_count;
     for (size_t i = 0; i < pool->sub_pool_count; i++) {
         sub_pool_t* sub = &pool->sub_pools[i];
         size_t add_size = base_add + (i < remainder ? 1 : 0);
         if (add_size == 0) continue;
 
         pthread_mutex_lock(&sub->mutex);
         sub->contention_attempts++;
         uint64_t start_time = get_hrtime();
 
         if (sub->pool_size + add_size > 0xFFFFFFFFFFFFULL) {
             report_error(pool, POOL_ERROR_INVALID_SIZE, "Sub-pool size exceeds 2^48 after grow");
             pthread_mutex_unlock(&sub->mutex);
             sub->total_contention_time_ns += get_hrtime() - start_time;
             return false;
         }
 
         void** new_objects = realloc(sub->objects, (sub->pool_size + add_size) * sizeof(void*));
         bool* new_used = realloc(sub->used, (sub->pool_size + add_size) * sizeof(bool));
         if (!new_objects || !new_used) {
             report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to reallocate sub-pool arrays");
             pthread_mutex_unlock(&sub->mutex);
             sub->total_contention_time_ns += get_hrtime() - start_time;
             return false;
         }
 
         sub->objects = new_objects;
         sub->used = new_used;
         for (size_t j = sub->pool_size; j < sub->pool_size + add_size; j++) {
             sub->objects[j] = pool->allocator.alloc(pool->allocator.user_data);
             if (!sub->objects[j]) {
                 report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate object");
                 pthread_mutex_unlock(&sub->mutex);
                 sub->total_contention_time_ns += get_hrtime() - start_time;
                 return false;
             }
             // Initialize metadata
             pool_object_metadata_t* metadata = (pool_object_metadata_t*)((char*)sub->objects[j] - sizeof(pool_object_metadata_t));
             if (!metadata) {
                 report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to access object metadata");
                 pool->allocator.free(sub->objects[j], pool->allocator.user_data);
                 pthread_mutex_unlock(&sub->mutex);
                 sub->total_contention_time_ns += get_hrtime() - start_time;
                 return false;
             }
             metadata->packed = ((uint64_t)i << 48) | j; // sub_pool_id | index
             sub->used[j] = false;
             pool->allocator.reset(sub->objects[j], pool->allocator.user_data);
             pool->allocator.on_create(sub->objects[j], pool->allocator.user_data);
         }
         sub->pool_size += add_size;
         pthread_mutex_unlock(&sub->mutex);
         sub->total_contention_time_ns += get_hrtime() - start_time;
     }
 
     pool->total_objects_allocated += additional_size;
     pool->grow_count++;
     return true;
 }
 
 /**
  * @brief Shrinks the pool by removing unused objects.
  *
  * Removes objects from the end of each sub-pool, ensuring no used objects are removed.
  *
  * @param pool The pool to shrink.
  * @param reduce_size Number of objects to remove (must be > 0 and ≤ capacity).
  * @return true on success, false on failure.
  * @threadsafe
  */
 bool pool_shrink(object_pool_t* pool, size_t reduce_size) {
    if (!pool || reduce_size == 0 || reduce_size > pool_capacity(pool)) {
        report_error(pool, POOL_ERROR_INVALID_SIZE, "Invalid pool or size");
        return false;
    }

    size_t base_reduce = reduce_size / pool->sub_pool_count;
    size_t remainder = reduce_size % pool->sub_pool_count;
    for (size_t i = 0; i < pool->sub_pool_count; i++) {
        sub_pool_t* sub = &pool->sub_pools[i];
        size_t red_size = base_reduce + (i < remainder ? 1 : 0);
        if (red_size == 0) continue;

        pthread_mutex_lock(&sub->mutex);
        sub->contention_attempts++;
        uint64_t start_time = get_hrtime();

        size_t unused_count = 0;
        for (size_t j = sub->pool_size; j > 0 && unused_count < red_size; j--) {
            if (!sub->used[j - 1]) {
                unused_count++;
            } else {
                break;
            }
        }
        if (unused_count < red_size) {
            report_error(pool, POOL_ERROR_INSUFFICIENT_UNUSED, "Not enough unused objects to shrink");
            pthread_mutex_unlock(&sub->mutex);
            sub->total_contention_time_ns += get_hrtime() - start_time;
            return false;
        }

        size_t new_size = sub->pool_size - red_size;
        for (size_t j = new_size; j < sub->pool_size; j++) {
            if (sub->objects[j]) {
                pool->allocator.on_destroy(sub->objects[j], pool->allocator.user_data);
                pool->allocator.free(sub->objects[j], pool->allocator.user_data);
                sub->objects[j] = NULL;
            }
        }

        void** temp_objects = realloc(sub->objects, new_size * sizeof(void*));
        bool* temp_used = realloc(sub->used, new_size * sizeof(bool));
        if (!temp_objects || !temp_used) {
            free(temp_objects);
            free(temp_used);
            report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to reallocate sub-pool arrays");
            pthread_mutex_unlock(&sub->mutex);
            sub->total_contention_time_ns += get_hrtime() - start_time;
            return false;
        }

        sub->objects = temp_objects;
        sub->used = temp_used;
        sub->pool_size = new_size;
        if (sub->max_used > sub->pool_size) {
            sub->max_used = sub->pool_size;
        }
        pthread_mutex_unlock(&sub->mutex);
        sub->total_contention_time_ns += get_hrtime() - start_time;
    }

    pool->shrink_count++;
    pool->total_objects_allocated -= reduce_size; // Fix: Update total_objects_allocated
    return true;
}
 
 /**
  * @brief Grows the request queue for backpressure.
  *
  * @param pool The pool to modify.
  * @param additional_capacity Additional queue slots (must be > 0).
  * @return true on success, false on failure.
  * @threadsafe
  */
 bool pool_grow_queue(object_pool_t* pool, size_t additional_capacity) {
     if (!pool || additional_capacity == 0) {
         report_error(pool, POOL_ERROR_INVALID_SIZE, "Invalid pool or additional capacity");
         return false;
     }
 
     pthread_mutex_lock(&pool->queue_mutex);
     size_t new_capacity = pool->queue_capacity + additional_capacity;
     acquire_request_t* new_queue = realloc(pool->request_queue, new_capacity * sizeof(acquire_request_t));
     if (!new_queue) {
         pthread_mutex_unlock(&pool->queue_mutex);
         report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to grow request queue");
         return false;
     }
     // Initialize new portion of queue
     memset(new_queue + pool->queue_capacity, 0, additional_capacity * sizeof(acquire_request_t));
     pool->request_queue = new_queue;
     pool->queue_capacity = new_capacity;
     pool->queue_grow_count++;
     pthread_mutex_unlock(&pool->queue_mutex);
     return true;
 }
 
 /**
  * @brief Acquires an object from the pool.
  *
  * Uses random sub-pool selection to balance load. If no objects are available,
  * enqueues the callback (if provided) for backpressure.
  *
  * @param pool The pool to acquire from.
  * @param callback Optional callback for backpressure.
  * @param context User context for callback.
  * @return Pointer to the acquired object, or NULL on failure.
  * @threadsafe
  */
 void* pool_acquire(object_pool_t* pool, object_pool_acquire_callback_t callback, void* context) {
     if (!pool) {
         report_error(NULL, POOL_ERROR_INVALID_POOL, "Invalid pool");
         return NULL;
     }
 
     // Try all sub-pools in random order to balance load
     size_t start_idx = next_random() % pool->sub_pool_count;
     for (size_t attempt = 0; attempt < pool->sub_pool_count; attempt++) {
         size_t sub_idx = (start_idx + attempt) % pool->sub_pool_count;
         sub_pool_t* sub = &pool->sub_pools[sub_idx];
 
         pthread_mutex_lock(&sub->mutex);
         sub->contention_attempts++;
         uint64_t start_time = get_hrtime();
 
         if (sub->used_count < sub->pool_size) {
             for (size_t i = 0; i < sub->pool_size; i++) {
                 if (!sub->used[i]) {
                     if (!sub->objects[i] || !pool->allocator.validate(sub->objects[i], pool->allocator.user_data)) {
                         report_error(pool, POOL_ERROR_INVALID_OBJECT, "Invalid object at index");
                         continue;
                     }
                     sub->used[i] = true;
                     sub->used_count++;
                     sub->max_used = sub->used_count > sub->max_used ? sub->used_count : sub->max_used;
                     sub->acquire_count++;
                     pool->allocator.reset(sub->objects[i], pool->allocator.user_data);
                     pool->allocator.on_reuse(sub->objects[i], pool->allocator.user_data);
                     void* obj = sub->objects[i];
                     pthread_mutex_unlock(&sub->mutex);
                     sub->total_contention_time_ns += get_hrtime() - start_time;
                     // Update global max_used
                     size_t current_used = pool_used_count(pool);
                     if (current_used > pool->max_used) {
                         pool->max_used = current_used;
                     }
                     return obj;
                 }
             }
         }
 
         pthread_mutex_unlock(&sub->mutex);
         sub->total_contention_time_ns += get_hrtime() - start_time;
     }
 
     // Pool exhausted, try backpressure
     if (callback && pool->queue_size < pool->queue_capacity) {
         pthread_mutex_lock(&pool->queue_mutex);
         if (pool->queue_size < pool->queue_capacity) {
             pool->request_queue[pool->queue_size++] = (acquire_request_t){callback, context};
             if (pool->queue_size > pool->queue_max_size) {
                 pool->queue_max_size = pool->queue_size;
             }
             pthread_mutex_unlock(&pool->queue_mutex);
             return NULL;
         }
         pthread_mutex_unlock(&pool->queue_mutex);
     }
 
     // Try to grow queue
     if (callback && pool_grow_queue(pool, pool->queue_capacity)) { // Double capacity
         pthread_mutex_lock(&pool->queue_mutex);
         pool->request_queue[pool->queue_size++] = (acquire_request_t){callback, context};
         if (pool->queue_size > pool->queue_max_size) {
             pool->queue_max_size = pool->queue_size;
         }
         pthread_mutex_unlock(&pool->queue_mutex);
         return NULL;
     }
 
     // Report appropriate error based on callback presence
     report_error(pool, callback ? POOL_ERROR_QUEUE_FULL : POOL_ERROR_EXHAUSTED,
                  callback ? "Request queue full" : "Pool exhausted");
     return NULL;
 }
 
 /**
  * @brief Releases an object back to the pool.
  *
  * Uses metadata for O(1) lookup and validates the object before release.
  *
  * @param pool The pool to release to.
  * @param object The object to release.
  * @return true on success, false on failure.
  * @threadsafe
  */
 bool pool_release(object_pool_t* pool, void* object) {
     if (!pool || !object) {
         report_error(pool, POOL_ERROR_INVALID_POOL, "Invalid pool or object");
         return false;
     }
 
     // Check if object is a valid pool object by searching sub-pools
     bool is_valid_object = false;
     sub_pool_t* sub = NULL;
     size_t obj_idx = 0;
     for (size_t i = 0; i < pool->sub_pool_count; i++) {
         for (size_t j = 0; j < pool->sub_pools[i].pool_size; j++) {
             if (pool->sub_pools[i].objects[j] == object) {
                 is_valid_object = true;
                 sub = &pool->sub_pools[i];
                 obj_idx = j;
                 break;
             }
         }
         if (is_valid_object) break;
     }
 
     if (!is_valid_object) {
 #ifdef DEBUG
         printf("DEBUG: Invalid object pointer: %p\n", object);
 #endif
         report_error(pool, POOL_ERROR_INVALID_OBJECT, "Object not in pool");
         return false;
     }
 
     // Use metadata for O(1) lookup
     sub_pool_t* metadata_sub = NULL;
     size_t metadata_idx = 0;
     get_metadata(pool, object, &metadata_sub, &metadata_idx);
     if (!metadata_sub || metadata_idx >= metadata_sub->pool_size || metadata_sub->objects[metadata_idx] != object) {
 #ifdef DEBUG
         printf("DEBUG: Invalid metadata for object: %p\n", object);
 #endif
         report_error(pool, POOL_ERROR_INVALID_OBJECT, "Invalid object metadata");
         return false;
     }
     sub = metadata_sub;
     obj_idx = metadata_idx;
 
     // Validate sub-pool and index
     if (obj_idx >= sub->pool_size || sub->objects[obj_idx] != object) {
 #ifdef DEBUG
         printf("DEBUG: Metadata mismatch for object: %p\n", object);
 #endif
         report_error(pool, POOL_ERROR_INVALID_OBJECT, "Object not in pool");
         return false;
     }
 
     pthread_mutex_lock(&sub->mutex);
     sub->contention_attempts++;
     uint64_t start_time = get_hrtime();
 
     if (!pool->allocator.validate(object, pool->allocator.user_data)) {
 #ifdef DEBUG
         printf("DEBUG: Object validation failed: %p\n", object);
 #endif
         report_error(pool, POOL_ERROR_INVALID_OBJECT, "Invalid object");
         pthread_mutex_unlock(&sub->mutex);
         sub->total_contention_time_ns += get_hrtime() - start_time;
         return false;
     }
 
     if (sub->used[obj_idx]) {
 #ifdef DEBUG
         printf("DEBUG: Releasing object %p, sub->used[%zu]=%d, used_count=%zu\n", 
                object, obj_idx, sub->used[obj_idx], sub->used_count);
 #endif
         sub->used[obj_idx] = false;
         sub->used_count--;
         sub->release_count++;
         pool->allocator.reset(object, pool->allocator.user_data);
 #ifdef DEBUG
         printf("DEBUG: After release, sub->used[%zu]=%d, used_count=%zu\n", 
                obj_idx, sub->used[obj_idx], sub->used_count);
 #endif
 
         // Process backpressure queue
         if (pool->queue_size > 0) {
             pthread_mutex_lock(&pool->queue_mutex);
             if (pool->queue_size > 0) {
                 acquire_request_t req = pool->request_queue[0];
                 for (size_t i = 1; i < pool->queue_size; i++) {
                     pool->request_queue[i - 1] = pool->request_queue[i];
                 }
                 pool->queue_size--;
                 pthread_mutex_unlock(&pool->queue_mutex);
                 if (req.callback && pool->allocator.validate(object, pool->allocator.user_data)) {
                     sub->used[obj_idx] = true;
                     sub->used_count++;
                     sub->acquire_count++;
                     pool->allocator.on_reuse(object, pool->allocator.user_data);
                     req.callback(object, req.context);
                     pthread_mutex_unlock(&sub->mutex);
                     sub->total_contention_time_ns += get_hrtime() - start_time;
                     // Update global max_used after callback acquire
                     size_t current_used = pool_used_count(pool);
                     if (current_used > pool->max_used) {
                         pool->max_used = current_used;
                     }
                     return true;
                 }
             } else {
                 pthread_mutex_unlock(&pool->queue_mutex);
             }
         }
 
         pthread_mutex_unlock(&sub->mutex);
         sub->total_contention_time_ns += get_hrtime() - start_time;
         return true;
     }
 
 #ifdef DEBUG
     printf("DEBUG: Object %p already unused, sub->used[%zu]=%d\n", object, obj_idx, sub->used[obj_idx]);
 #endif
     report_error(pool, POOL_ERROR_INVALID_OBJECT, "Invalid or unused object");
     pthread_mutex_unlock(&sub->mutex);
     sub->total_contention_time_ns += get_hrtime() - start_time;
     return false;
 }
 
 /**
  * @brief Gets the number of used objects in the pool.
  *
  * @param pool The pool to query.
  * @return Number of used objects, or 0 if pool is NULL.
  * @threadsafe
  */
 size_t pool_used_count(object_pool_t* pool) {
     if (!pool) {
         return 0;
     }
     size_t total = 0;
     for (size_t i = 0; i < pool->sub_pool_count; i++) {
         sub_pool_t* sub = &pool->sub_pools[i];
         pthread_mutex_lock(&sub->mutex);
         sub->contention_attempts++;
         uint64_t start_time = get_hrtime();
         total += sub->used_count;
         pthread_mutex_unlock(&sub->mutex);
         sub->total_contention_time_ns += get_hrtime() - start_time;
     }
     return total;
 }
 
 /**
  * @brief Gets the total capacity of the pool.
  *
  * @param pool The pool to query.
  * @return Total number of objects, or 0 if pool is NULL.
  * @threadsafe
  */
 size_t pool_capacity(object_pool_t* pool) {
     if (!pool) {
         return 0;
     }
     size_t total = 0;
     for (size_t i = 0; i < pool->sub_pool_count; i++) {
         total += pool->sub_pools[i].pool_size;
     }
     return total;
 }
 
 /**
  * @brief Gets pool usage statistics.
  *
  * Aggregates statistics from all sub-pools, including acquire/release counts and contention metrics.
  *
  * @param pool The pool to query.
  * @param stats Output structure for statistics.
  * @threadsafe
  */
 void pool_stats(object_pool_t* pool, object_pool_stats_t* stats) {
     if (!pool || !stats) {
         return;
     }
     stats->max_used = pool->max_used; // Use global max_used
     stats->acquire_count = 0;
     stats->release_count = 0;
     stats->contention_attempts = 0;
     stats->total_contention_time_ns = 0;
     for (size_t i = 0; i < pool->sub_pool_count; i++) {
         sub_pool_t* sub = &pool->sub_pools[i];
         pthread_mutex_lock(&sub->mutex);
         sub->contention_attempts++;
         uint64_t start_time = get_hrtime();
         stats->acquire_count += sub->acquire_count;
         stats->release_count += sub->release_count;
         stats->contention_attempts += sub->contention_attempts;
         stats->total_contention_time_ns += sub->total_contention_time_ns;
         pthread_mutex_unlock(&sub->mutex);
         sub->total_contention_time_ns += get_hrtime() - start_time;
     }
     stats->total_objects_allocated = pool->total_objects_allocated;
     stats->grow_count = pool->grow_count;
     stats->shrink_count = pool->shrink_count;
     stats->queue_max_size = pool->queue_max_size;
     stats->queue_grow_count = pool->queue_grow_count;
 }
 
 /**
  * @brief Gets acquire counts for each sub-pool.
  *
  * Allocates an array of acquire counts, one per sub-pool. The caller must free the array.
  *
  * @param pool The pool to query.
  * @param count Output for the number of sub-pools.
  * @return Array of acquire counts, or NULL on failure (sets count to 0).
  * @threadsafe
  */
 size_t* pool_get_sub_pool_acquire_counts(object_pool_t* pool, size_t* count) {
     if (!pool || !count) {
         if (count) *count = 0;
         return NULL;
     }
     size_t* acquires = malloc(pool->sub_pool_count * sizeof(size_t));
     if (!acquires) {
         report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate acquire counts array");
         *count = 0;
         return NULL;
     }
     *count = pool->sub_pool_count;
     for (size_t i = 0; i < pool->sub_pool_count; i++) {
         sub_pool_t* sub = &pool->sub_pools[i];
         pthread_mutex_lock(&sub->mutex);
         acquires[i] = sub->acquire_count;
         pthread_mutex_unlock(&sub->mutex);
     }
     return acquires;
 }
 
 /**
  * @brief Destroys the pool and frees all resources.
  *
  * Frees all objects, sub-pools, and the request queue, destroying mutexes.
  *
  * @param pool The pool to destroy.
  * @threadsafe
  */
 void pool_destroy(object_pool_t* pool) {
     if (!pool) {
         return;
     }
     for (size_t i = 0; i < pool->sub_pool_count; i++) {
         sub_pool_t* sub = &pool->sub_pools[i];
         for (size_t j = 0; j < sub->pool_size; j++) {
             if (sub->objects[j]) {
                 pool->allocator.on_destroy(sub->objects[j], pool->allocator.user_data);
                 pool->allocator.free(sub->objects[j], pool->allocator.user_data);
                 sub->objects[j] = NULL; // Prevent double-free
             }
         }
         free(sub->objects);
         free(sub->used);
         pthread_mutex_destroy(&sub->mutex);
     }
     free(pool->sub_pools);
     free(pool->request_queue);
     pthread_mutex_destroy(&pool->queue_mutex);
     free(pool->allocator.user_data); // Free user_data (object_size_ptr)
     free(pool);
 }