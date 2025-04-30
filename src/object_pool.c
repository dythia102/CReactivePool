#include "object_pool.h"
#include <stdio.h>
#include <string.h> // For memset
#include <uv.h>

// Sub-pool structure
typedef struct {
    void** objects;               // Array of object pointers
    bool* used;                   // Track object usage
    size_t pool_size;             // Number of objects in sub-pool
    size_t used_count;            // Number of used objects
    size_t max_used;              // Max concurrent objects
    size_t acquire_count;         // Total acquire operations
    size_t release_count;         // Total release operations
    size_t contention_attempts;   // Total mutex contention attempts
    uint64_t total_contention_time_ns; // Total mutex wait time
    uv_mutex_t mutex;             // Mutex for thread safety
} sub_pool_t;

// Acquire request for backpressure
typedef struct {
    object_pool_acquire_callback_t callback;
    void* context;
} acquire_request_t;

// Main pool structure
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
    object_pool_allocator_t allocator; // Allocator for objects
    object_pool_error_callback_t error_callback; // Error callback
    void* error_context;          // Error callback context
    uv_mutex_t queue_mutex;       // Mutex for request_queue
};

// Default allocator for plain void* (uses malloc/free)
static void* default_alloc(void) {
    return malloc(1); // Minimal allocation for default case
}

static void default_free(void* obj) {
    free(obj);
}

static void default_reset(void* obj) {
    (void)obj; // Suppress unused parameter warning
}

static bool default_validate(void* obj) {
    (void)obj; // Suppress unused parameter warning
    return true; // Always valid for default
}

static void default_on_create(void* obj) {
    (void)obj;
}

static void default_on_destroy(void* obj) {
    (void)obj;
}

static void default_on_reuse(void* obj) {
    (void)obj;
}

// Helper to invoke error callback
static void report_error(object_pool_t* pool, object_pool_error_t error, const char* message) {
    if (pool && pool->error_callback) {
        pool->error_callback(error, message, pool->error_context);
    } else {
        fprintf(stderr, "%s\n", message);
    }
}

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
    pool->allocator = allocator;
    pool->error_callback = error_callback;
    pool->error_context = error_context;
    if (!pool->allocator.reset) pool->allocator.reset = default_reset;
    if (!pool->allocator.validate) pool->allocator.validate = default_validate;
    if (!pool->allocator.on_create) pool->allocator.on_create = default_on_create;
    if (!pool->allocator.on_destroy) pool->allocator.on_destroy = default_on_destroy;
    if (!pool->allocator.on_reuse) pool->allocator.on_reuse = default_on_reuse;

    if (uv_mutex_init(&pool->queue_mutex) != 0) {
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
        sub->objects = malloc(sub->pool_size * sizeof(void*));
        sub->used = malloc(sub->pool_size * sizeof(bool));
        if (!sub->objects || !sub->used) {
            report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate sub-pool arrays");
            for (size_t j = 0; j < i; j++) {
                for (size_t k = 0; k < pool->sub_pools[j].pool_size; k++) {
                    pool->allocator.free(pool->sub_pools[j].objects[k]);
                }
                free(pool->sub_pools[j].objects);
                free(pool->sub_pools[j].used);
            }
            free(pool->sub_pools);
            free(pool->request_queue);
            uv_mutex_destroy(&pool->queue_mutex);
            free(pool);
            return NULL;
        }

        if (uv_mutex_init(&sub->mutex) != 0) {
            report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to initialize sub-pool mutex");
            for (size_t j = 0; j < i; j++) {
                for (size_t k = 0; k < pool->sub_pools[j].pool_size; k++) {
                    pool->allocator.free(pool->sub_pools[j].objects[k]);
                }
                free(pool->sub_pools[j].objects);
                free(pool->sub_pools[j].used);
                uv_mutex_destroy(&pool->sub_pools[j].mutex);
            }
            free(pool->sub_pools);
            free(pool->request_queue);
            uv_mutex_destroy(&pool->queue_mutex);
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
            sub->objects[j] = pool->allocator.alloc();
            if (!sub->objects[j]) {
                report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate object");
                for (size_t k = 0; k < j; k++) {
                    pool->allocator.free(sub->objects[k]);
                }
                for (size_t m = 0; m < i; m++) {
                    for (size_t n = 0; n < pool->sub_pools[m].pool_size; n++) {
                        pool->allocator.free(pool->sub_pools[m].objects[n]);
                    }
                    free(pool->sub_pools[m].objects);
                    free(pool->sub_pools[m].used);
                    uv_mutex_destroy(&pool->sub_pools[m].mutex);
                }
                free(sub->objects);
                free(sub->used);
                free(pool->sub_pools);
                free(pool->request_queue);
                uv_mutex_destroy(&pool->queue_mutex);
                free(pool);
                return NULL;
            }
            sub->used[j] = false;
            pool->allocator.reset(sub->objects[j]);
            pool->allocator.on_create(sub->objects[j]);
        }
    }

    return pool;
}

object_pool_t* pool_create_default(void) {
    object_pool_allocator_t allocator = {
        .alloc = default_alloc,
        .free = default_free,
        .reset = default_reset,
        .validate = default_validate,
        .on_create = default_on_create,
        .on_destroy = default_on_destroy,
        .on_reuse = default_on_reuse,
        .user_data = NULL
    };
    return pool_create(DEFAULT_POOL_SIZE, DEFAULT_SUB_POOL_COUNT, allocator, NULL, NULL);
}

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

        uv_mutex_lock(&sub->mutex);
        sub->contention_attempts++;
        uint64_t start_time = uv_hrtime();

        void** new_objects = realloc(sub->objects, (sub->pool_size + add_size) * sizeof(void*));
        bool* new_used = realloc(sub->used, (sub->pool_size + add_size) * sizeof(bool));
        if (!new_objects || !new_used) {
            report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to reallocate sub-pool arrays");
            uv_mutex_unlock(&sub->mutex);
            sub->total_contention_time_ns += uv_hrtime() - start_time;
            return false;
        }

        sub->objects = new_objects;
        sub->used = new_used;
        for (size_t j = sub->pool_size; j < sub->pool_size + add_size; j++) {
            sub->objects[j] = pool->allocator.alloc();
            if (!sub->objects[j]) {
                report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate object");
                uv_mutex_unlock(&sub->mutex);
                sub->total_contention_time_ns += uv_hrtime() - start_time;
                return false;
            }
            sub->used[j] = false;
            pool->allocator.reset(sub->objects[j]);
            pool->allocator.on_create(sub->objects[j]);
        }
        sub->pool_size += add_size;
        uv_mutex_unlock(&sub->mutex);
        sub->total_contention_time_ns += uv_hrtime() - start_time;
    }

    pool->total_objects_allocated += additional_size;
    pool->grow_count++;
    return true;
}

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

        uv_mutex_lock(&sub->mutex);
        sub->contention_attempts++;
        uint64_t start_time = uv_hrtime();

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
            uv_mutex_unlock(&sub->mutex);
            sub->total_contention_time_ns += uv_hrtime() - start_time;
            return false;
        }

        size_t new_size = sub->pool_size - red_size;
        for (size_t j = new_size; j < sub->pool_size; j++) {
            pool->allocator.on_destroy(sub->objects[j]);
            pool->allocator.free(sub->objects[j]);
        }

        void** temp_objects = realloc(sub->objects, new_size * sizeof(void*));
        bool* temp_used = realloc(sub->used, new_size * sizeof(bool));
        if (!temp_objects || !temp_used) {
            free(temp_objects);
            free(temp_used);
            report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to reallocate sub-pool arrays");
            uv_mutex_unlock(&sub->mutex);
            sub->total_contention_time_ns += uv_hrtime() - start_time;
            return false;
        }

        sub->objects = temp_objects;
        sub->used = temp_used;
        sub->pool_size = new_size;
        if (sub->max_used > sub->pool_size) {
            sub->max_used = sub->pool_size;
        }
        uv_mutex_unlock(&sub->mutex);
        sub->total_contention_time_ns += uv_hrtime() - start_time;
    }

    pool->shrink_count++;
    return true;
}

bool pool_grow_queue(object_pool_t* pool, size_t additional_capacity) {
    if (!pool || additional_capacity == 0) {
        report_error(pool, POOL_ERROR_INVALID_SIZE, "Invalid pool or additional capacity");
        return false;
    }

    uv_mutex_lock(&pool->queue_mutex);
    size_t new_capacity = pool->queue_capacity + additional_capacity;
    acquire_request_t* new_queue = realloc(pool->request_queue, new_capacity * sizeof(acquire_request_t));
    if (!new_queue) {
        uv_mutex_unlock(&pool->queue_mutex);
        report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to grow request queue");
        return false;
    }
    // Initialize new portion of queue
    memset(new_queue + pool->queue_capacity, 0, additional_capacity * sizeof(acquire_request_t));
    pool->request_queue = new_queue;
    pool->queue_capacity = new_capacity;
    pool->queue_grow_count++;
    uv_mutex_unlock(&pool->queue_mutex);
    return true;
}

void* pool_acquire(object_pool_t* pool, object_pool_acquire_callback_t callback, void* context) {
    if (!pool) {
        report_error(NULL, POOL_ERROR_INVALID_POOL, "Invalid pool");
        return NULL;
    }

    // Try all sub-pools to maximize object acquisition
    for (size_t attempt = 0; attempt < pool->sub_pool_count; attempt++) {
        size_t sub_idx = (uv_thread_self() + attempt) % pool->sub_pool_count;
        sub_pool_t* sub = &pool->sub_pools[sub_idx];

        uv_mutex_lock(&sub->mutex);
        sub->contention_attempts++;
        uint64_t start_time = uv_hrtime();

        if (sub->used_count < sub->pool_size) {
            for (size_t i = 0; i < sub->pool_size; i++) {
                if (!sub->used[i]) {
                    if (!pool->allocator.validate(sub->objects[i])) {
                        report_error(pool, POOL_ERROR_INVALID_OBJECT, "Invalid object at index");
                        continue;
                    }
                    sub->used[i] = true;
                    sub->used_count++;
                    sub->max_used = sub->used_count > sub->max_used ? sub->used_count : sub->max_used;
                    sub->acquire_count++;
                    pool->allocator.reset(sub->objects[i]);
                    pool->allocator.on_reuse(sub->objects[i]);
                    void* obj = sub->objects[i];
                    uv_mutex_unlock(&sub->mutex);
                    sub->total_contention_time_ns += uv_hrtime() - start_time;
                    return obj;
                }
            }
        }

        uv_mutex_unlock(&sub->mutex);
        sub->total_contention_time_ns += uv_hrtime() - start_time;
    }

    // Pool exhausted, try backpressure
    if (callback && pool->queue_size < pool->queue_capacity) {
        uv_mutex_lock(&pool->queue_mutex);
        if (pool->queue_size < pool->queue_capacity) {
            pool->request_queue[pool->queue_size++] = (acquire_request_t){callback, context};
            if (pool->queue_size > pool->queue_max_size) {
                pool->queue_max_size = pool->queue_size;
            }
            uv_mutex_unlock(&pool->queue_mutex);
            return NULL;
        }
        uv_mutex_unlock(&pool->queue_mutex);
    }

    // Try to grow queue
    if (callback && pool_grow_queue(pool, pool->queue_capacity)) { // Double capacity
        uv_mutex_lock(&pool->queue_mutex);
        pool->request_queue[pool->queue_size++] = (acquire_request_t){callback, context};
        if (pool->queue_size > pool->queue_max_size) {
            pool->queue_max_size = pool->queue_size;
        }
        uv_mutex_unlock(&pool->queue_mutex);
        return NULL;
    }

    // Report appropriate error based on callback presence
    report_error(pool, callback ? POOL_ERROR_QUEUE_FULL : POOL_ERROR_EXHAUSTED,
                 callback ? "Request queue full" : "Pool exhausted");
    return NULL;
}

bool pool_release(object_pool_t* pool, void* object) {
    if (!pool || !object) {
        report_error(pool, POOL_ERROR_INVALID_POOL, "Invalid pool or object");
        return false;
    }

    // Find sub-pool containing the object
    sub_pool_t* sub = NULL;
    size_t obj_idx = 0;
    for (size_t i = 0; i < pool->sub_pool_count; i++) {
        for (size_t j = 0; j < pool->sub_pools[i].pool_size; j++) {
            if (pool->sub_pools[i].objects[j] == object) {
                sub = &pool->sub_pools[i];
                obj_idx = j;
                break;
            }
        }
        if (sub) break;
    }
    if (!sub) {
        report_error(pool, POOL_ERROR_INVALID_OBJECT, "Object not in pool");
        return false;
    }

    uv_mutex_lock(&sub->mutex);
    sub->contention_attempts++;
    uint64_t start_time = uv_hrtime();

    if (!pool->allocator.validate(object)) {
        report_error(pool, POOL_ERROR_INVALID_OBJECT, "Invalid object");
        uv_mutex_unlock(&sub->mutex);
        sub->total_contention_time_ns += uv_hrtime() - start_time;
        return false;
    }

    if (sub->used[obj_idx]) {
        sub->used[obj_idx] = false;
        sub->used_count--;
        sub->release_count++;
        pool->allocator.reset(object);

        // Process backpressure queue
        if (pool->queue_size > 0) {
            uv_mutex_lock(&pool->queue_mutex);
            if (pool->queue_size > 0) {
                acquire_request_t req = pool->request_queue[0];
                for (size_t i = 1; i < pool->queue_size; i++) {
                    pool->request_queue[i - 1] = pool->request_queue[i];
                }
                pool->queue_size--;
                uv_mutex_unlock(&pool->queue_mutex);
                if (req.callback && pool->allocator.validate(object)) {
                    sub->used[obj_idx] = true;
                    sub->used_count++;
                    sub->acquire_count++;
                    pool->allocator.on_reuse(object);
                    req.callback(object, req.context);
                    uv_mutex_unlock(&sub->mutex);
                    sub->total_contention_time_ns += uv_hrtime() - start_time;
                    return true;
                }
            } else {
                uv_mutex_unlock(&pool->queue_mutex);
            }
        }

        uv_mutex_unlock(&sub->mutex);
        sub->total_contention_time_ns += uv_hrtime() - start_time;
        return true;
    }

    report_error(pool, POOL_ERROR_INVALID_OBJECT, "Invalid or unused object");
    uv_mutex_unlock(&sub->mutex);
    sub->total_contention_time_ns += uv_hrtime() - start_time;
    return false;
}

size_t pool_used_count(object_pool_t* pool) {
    if (!pool) {
        return 0;
    }
    size_t total = 0;
    for (size_t i = 0; i < pool->sub_pool_count; i++) {
        sub_pool_t* sub = &pool->sub_pools[i];
        uv_mutex_lock(&sub->mutex);
        sub->contention_attempts++;
        uint64_t start_time = uv_hrtime();
        total += sub->used_count;
        uv_mutex_unlock(&sub->mutex);
        sub->total_contention_time_ns += uv_hrtime() - start_time;
    }
    return total;
}

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

void pool_stats(object_pool_t* pool, object_pool_stats_t* stats) {
    if (!pool || !stats) {
        return;
    }
    stats->max_used = 0;
    stats->acquire_count = 0;
    stats->release_count = 0;
    stats->contention_attempts = 0;
    stats->total_contention_time_ns = 0;
    for (size_t i = 0; i < pool->sub_pool_count; i++) {
        sub_pool_t* sub = &pool->sub_pools[i];
        uv_mutex_lock(&sub->mutex);
        sub->contention_attempts++;
        uint64_t start_time = uv_hrtime();
        stats->max_used += sub->max_used;
        stats->acquire_count += sub->acquire_count;
        stats->release_count += sub->release_count;
        stats->contention_attempts += sub->contention_attempts;
        stats->total_contention_time_ns += sub->total_contention_time_ns;
        uv_mutex_unlock(&sub->mutex);
        sub->total_contention_time_ns += uv_hrtime() - start_time;
    }
    stats->total_objects_allocated = pool->total_objects_allocated;
    stats->grow_count = pool->grow_count;
    stats->shrink_count = pool->shrink_count;
    stats->queue_max_size = pool->queue_max_size;
    stats->queue_grow_count = pool->queue_grow_count;
}

void pool_destroy(object_pool_t* pool) {
    if (!pool) {
        return;
    }

    for (size_t i = 0; i < pool->sub_pool_count; i++) {
        sub_pool_t* sub = &pool->sub_pools[i];
        for (size_t j = 0; j < sub->pool_size; j++) {
            pool->allocator.on_destroy(sub->objects[j]);
            pool->allocator.free(sub->objects[j]);
        }
        free(sub->objects);
        free(sub->used);
        uv_mutex_destroy(&sub->mutex);
    }
    free(pool->sub_pools);
    free(pool->request_queue);
    uv_mutex_destroy(&pool->queue_mutex);
    free(pool);
}