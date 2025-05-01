/**
 * @file object_pool.h
 * @brief A thread-safe object pool library with dynamic resizing and backpressure handling.
 *
 * This library provides a generic, thread-safe object pool for managing reusable objects.
 * Features include:
 * - Custom allocators for flexible object management.
 * - Dynamic pool and queue resizing for scalability.
 * - Backpressure handling via callbacks for high-contention scenarios.
 * - Accurate statistics tracking (e.g., max usage, contention time).
 * - O(1) object release using compact metadata.
 * - Random sub-pool selection for load balancing in multi-threaded environments.
 *
 * All operations are thread-safe using POSIX mutexes. The library is designed for high-performance
 * applications, with optimizations for low memory usage and minimal contention.
 */

 #ifndef OBJECT_POOL_H
 #define OBJECT_POOL_H
 
 #include <stdlib.h>
 #include <stdbool.h>
 #include <stdint.h>   // For uint64_t, uint32_t
 #include <pthread.h>  // For pthread_mutex_t
 
 #define DEFAULT_POOL_SIZE 16
 #define DEFAULT_SUB_POOL_COUNT 4
 #define DEFAULT_QUEUE_CAPACITY 32
 #define DEFAULT_OBJECT_SIZE 64 // Default size for objects in pool_create_default_with_size
 
 /**
  * @brief Metadata stored with each object for efficient lookup.
  */
 typedef struct {
     uint64_t packed; // Bits 0-47: index, 48-63: sub_pool_id
 } pool_object_metadata_t;
 
 /**
  * @brief Allocator interface for custom object management.
  */
 typedef struct {
     void* (*alloc)(void* user_data);          // Allocate a single object
     void (*free)(void* obj, void* user_data); // Free a single object
     void (*reset)(void* obj, void* user_data); // Reset object to default state (optional)
     bool (*validate)(void* obj, void* user_data); // Validate object integrity (optional)
     void (*on_create)(void* obj, void* user_data); // Called after object creation (optional)
     void (*on_destroy)(void* obj, void* user_data); // Called before object destruction (optional)
     void (*on_reuse)(void* obj, void* user_data); // Called before object reuse (optional)
     void* user_data;                          // Optional user data for allocator
 } object_pool_allocator_t;
 
 /**
  * @brief Error types for pool operations.
  */
 typedef enum {
     POOL_ERROR_NONE,              // No error
     POOL_ERROR_INVALID_POOL,      // Invalid pool pointer
     POOL_ERROR_INVALID_OBJECT,    // Invalid object pointer
     POOL_ERROR_EXHAUSTED,         // Pool has no available objects
     POOL_ERROR_ALLOCATION_FAILED, // Memory allocation failed
     POOL_ERROR_INVALID_SIZE,      // Invalid size parameter
     POOL_ERROR_INSUFFICIENT_UNUSED, // Not enough unused objects to shrink
     POOL_ERROR_QUEUE_FULL         // Backpressure queue is full
 } object_pool_error_t;
 
 /**
  * @brief Callback for reporting errors.
  *
  * @param error Error type.
  * @param message Descriptive error message.
  * @param context User-provided context.
  */
 typedef void (*object_pool_error_callback_t)(object_pool_error_t error, const char* message, void* context);
 
 /**
  * @brief Callback for backpressure when acquiring objects.
  *
  * @param object Acquired object.
  * @param context User-provided context.
  */
 typedef void (*object_pool_acquire_callback_t)(void* object, void* context);
 
 /**
  * @brief Statistics for pool usage.
  */
 typedef struct {
     size_t max_used;               // Max concurrent objects used
     size_t acquire_count;          // Total acquire operations
     size_t release_count;          // Total release operations
     size_t contention_attempts;    // Total mutex contention attempts
     uint64_t total_contention_time_ns; // Total mutex wait time (nanoseconds)
     size_t total_objects_allocated; // Total objects allocated
     size_t grow_count;             // Number of grow operations
     size_t shrink_count;           // Number of shrink operations
     size_t queue_max_size;         // Max queue size for backpressure
     size_t queue_grow_count;       // Number of queue growth operations
 } object_pool_stats_t;
 
 // Opaque pool and sub-pool types
 typedef struct object_pool object_pool_t;
 typedef struct sub_pool sub_pool_t;
 
 /**
  * @brief Creates a thread-safe object pool with specified parameters.
  *
  * Allocates a pool with the given number of objects distributed across sub-pools for
  * load balancing. The allocator defines object management, and an optional error callback
  * reports issues.
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
                            object_pool_error_callback_t error_callback, void* error_context);
 
 /**
  * @brief Creates a pool with default settings (16 objects, 4 sub-pools, 1-byte objects).
  *
  * @return Pointer to the created pool, or NULL on failure.
  * @threadsafe
  */
 object_pool_t* pool_create_default(void);
 
 /**
  * @brief Creates a pool with default settings and specified object size.
  *
  * @param object_size Size of each object (0 for default 64 bytes).
  * @return Pointer to the created pool, or NULL on failure.
  * @threadsafe
  */
 object_pool_t* pool_create_default_with_size(size_t object_size);
 
 /**
  * @brief Grows the pool by adding more objects.
  *
  * @param pool The pool to grow.
  * @param additional_size Number of objects to add (must be > 0).
  * @return true on success, false on failure.
  * @threadsafe
  */
 bool pool_grow(object_pool_t* pool, size_t additional_size);
 
 /**
  * @brief Grows the request queue for backpressure.
  *
  * @param pool The pool to modify.
  * @param additional_capacity Additional queue slots (must be > 0).
  * @return true on success, false on failure.
  * @threadsafe
  */
 bool pool_grow_queue(object_pool_t* pool, size_t additional_capacity);
 
 /**
  * @brief Shrinks the pool by removing unused objects.
  *
  * @param pool The pool to shrink.
  * @param reduce_size Number of objects to remove (must be > 0 and ≤ capacity).
  * @return true on success, false on failure.
  * @threadsafe
  */
 bool pool_shrink(object_pool_t* pool, size_t reduce_size);
 
 /**
  * @brief Acquires an object from the pool.
  *
  * If no objects are available, enqueues the callback (if provided) for backpressure.
  * Returns NULL if the queue is full or no callback is provided.
  *
  * @param pool The pool to acquire from.
  * @param callback Optional callback for backpressure.
  * @param context User context for callback.
  * @return Pointer to the acquired object, or NULL on failure.
  * @threadsafe
  */
 void* pool_acquire(object_pool_t* pool, object_pool_acquire_callback_t callback, void* context);
 
 /**
  * @brief Releases an object back to the pool.
  *
  * Uses metadata for O(1) lookup. Returns false if the object is invalid or not in the pool.
  *
  * @param pool The pool to release to.
  * @param object The object to release.
  * @return true on success, false on failure.
  * @threadsafe
  */
 bool pool_release(object_pool_t* pool, void* object);
 
 /**
  * @brief Gets the number of used objects in the pool.
  *
  * @param pool The pool to query.
  * @return Number of used objects, or 0 if pool is NULL.
  * @threadsafe
  */
 size_t pool_used_count(object_pool_t* pool);
 
 /**
  * @brief Gets the total capacity of the pool.
  *
  * @param pool The pool to query.
  * @return Total number of objects, or 0 if pool is NULL.
  * @threadsafe
  */
 size_t pool_capacity(object_pool_t* pool);
 
 /**
  * @brief Gets pool usage statistics.
  *
  * @param pool The pool to query.
  * @param stats Output structure for statistics.
  * @threadsafe
  */
 void pool_stats(object_pool_t* pool, object_pool_stats_t* stats);
 
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
 size_t* pool_get_sub_pool_acquire_counts(object_pool_t* pool, size_t* count);
 
 /**
  * @brief Destroys the pool and frees all resources.
  *
  * @param pool The pool to destroy.
  * @threadsafe
  */
 void pool_destroy(object_pool_t* pool);
 
 #endif // OBJECT_POOL_H