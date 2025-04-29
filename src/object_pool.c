#include "object_pool.h"
#include <stdio.h>

struct object_pool {
    void** objects;               // Array of object pointers
    bool* used;                   // Track object usage
    size_t pool_size;             // Total number of objects
    size_t used_count;            // Number of used objects
    size_t max_used;              // Max concurrent objects
    size_t acquire_count;         // Total acquire operations
    size_t release_count;         // Total release operations
    size_t contention_attempts;   // Total mutex contention attempts
    uint64_t total_contention_time_ns; // Total mutex wait time
    size_t total_objects_allocated; // Total objects allocated
    size_t grow_count;            // Number of grow operations
    size_t shrink_count;          // Number of shrink operations
    object_pool_allocator_t allocator; // Allocator for objects
    object_pool_error_callback_t error_callback; // Error callback
    void* error_context;          // Error callback context
    uv_mutex_t mutex;             // Mutex for thread safety
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

// Helper to invoke error callback
static void report_error(object_pool_t* pool, object_pool_error_t error, const char* message) {
    if (pool && pool->error_callback) {
        pool->error_callback(error, message, pool->error_context);
    } else {
        fprintf(stderr, "%s\n", message);
    }
}

object_pool_t* pool_create(size_t pool_size, object_pool_allocator_t allocator,
                           object_pool_error_callback_t error_callback, void* error_context) {
    if (pool_size == 0 || !allocator.alloc || !allocator.free) {
        if (error_callback) {
            error_callback(POOL_ERROR_INVALID_SIZE, "Invalid pool size or allocator", error_context);
        } else {
            fprintf(stderr, "Invalid pool size or allocator\n");
        }
        return NULL;
    }

    object_pool_t* pool = malloc(sizeof(object_pool_t));
    if (!pool) {
        report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate pool");
        return NULL;
    }

    pool->objects = malloc(pool_size * sizeof(void*));
    pool->used = malloc(pool_size * sizeof(bool));
    if (!pool->objects || !pool->used) {
        report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate pool arrays");
        free(pool->objects);
        free(pool->used);
        free(pool);
        return NULL;
    }

    if (uv_mutex_init(&pool->mutex) != 0) {
        report_error(NULL, POOL_ERROR_ALLOCATION_FAILED, "Failed to initialize mutex");
        free(pool->objects);
        free(pool->used);
        free(pool);
        return NULL;
    }

    pool->pool_size = pool_size;
    pool->used_count = 0;
    pool->max_used = 0;
    pool->acquire_count = 0;
    pool->release_count = 0;
    pool->contention_attempts = 0;
    pool->total_contention_time_ns = 0;
    pool->total_objects_allocated = pool_size;
    pool->grow_count = 0;
    pool->shrink_count = 0;
    pool->allocator = allocator;
    pool->error_callback = error_callback;
    pool->error_context = error_context;
    if (!pool->allocator.reset) {
        pool->allocator.reset = default_reset;
    }
    if (!pool->allocator.validate) {
        pool->allocator.validate = default_validate;
    }

    for (size_t i = 0; i < pool_size; i++) {
        pool->objects[i] = pool->allocator.alloc();
        if (!pool->objects[i]) {
            report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate object");
            for (size_t j = 0; j < i; j++) {
                pool->allocator.free(pool->objects[j]);
            }
            uv_mutex_destroy(&pool->mutex);
            free(pool->objects);
            free(pool->used);
            free(pool);
            return NULL;
        }
        pool->used[i] = false;
        pool->allocator.reset(pool->objects[i]);
    }

    return pool;
}

object_pool_t* pool_create_default(void) {
    object_pool_allocator_t allocator = {
        .alloc = default_alloc,
        .free = default_free,
        .reset = default_reset,
        .validate = default_validate,
        .user_data = NULL
    };
    return pool_create(DEFAULT_POOL_SIZE, allocator, NULL, NULL);
}

bool pool_grow(object_pool_t* pool, size_t additional_size) {
    if (!pool || additional_size == 0) {
        report_error(pool, POOL_ERROR_INVALID_SIZE, "Invalid pool or size");
        return false;
    }

    uv_mutex_lock(&pool->mutex);
    pool->contention_attempts++;
    uint64_t start_time = uv_hrtime();

    void** new_objects = realloc(pool->objects, (pool->pool_size + additional_size) * sizeof(void*));
    bool* new_used = realloc(pool->used, (pool->pool_size + additional_size) * sizeof(bool));
    if (!new_objects || !new_used) {
        report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to reallocate pool arrays");
        uv_mutex_unlock(&pool->mutex);
        pool->total_contention_time_ns += uv_hrtime() - start_time;
        return false;
    }

    pool->objects = new_objects;
    pool->used = new_used;
    for (size_t i = pool->pool_size; i < pool->pool_size + additional_size; i++) {
        pool->objects[i] = pool->allocator.alloc();
        if (!pool->objects[i]) {
            report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to allocate object");
            uv_mutex_unlock(&pool->mutex);
            pool->total_contention_time_ns += uv_hrtime() - start_time;
            return false; // Leave pool in consistent state
        }
        pool->used[i] = false;
        pool->allocator.reset(pool->objects[i]);
    }
    pool->pool_size += additional_size;
    pool->total_objects_allocated += additional_size;
    pool->grow_count++;
    uv_mutex_unlock(&pool->mutex);
    pool->total_contention_time_ns += uv_hrtime() - start_time;
    return true;
}

bool pool_shrink(object_pool_t* pool, size_t reduce_size) {
    if (!pool || reduce_size == 0 || reduce_size > pool->pool_size) {
        report_error(pool, POOL_ERROR_INVALID_SIZE, "Invalid pool or size");
        return false;
    }

    uv_mutex_lock(&pool->mutex);
    pool->contention_attempts++;
    uint64_t start_time = uv_hrtime();

    // Count unused objects from the end
    size_t unused_count = 0;
    for (size_t i = pool->pool_size; i > 0 && unused_count < reduce_size; i--) {
        if (!pool->used[i - 1]) {
            unused_count++;
        } else {
            break; // Stop at first used object
        }
    }
    if (unused_count < reduce_size) {
        report_error(pool, POOL_ERROR_INSUFFICIENT_UNUSED, "Not enough unused objects to shrink");
        uv_mutex_unlock(&pool->mutex);
        pool->total_contention_time_ns += uv_hrtime() - start_time;
        return false;
    }

    // Free unused objects from the end
    size_t new_size = pool->pool_size - reduce_size;
    for (size_t i = new_size; i < pool->pool_size; i++) {
        pool->allocator.free(pool->objects[i]);
    }

    // Reallocate arrays
    void** new_objects = realloc(pool->objects, new_size * sizeof(void*));
    bool* new_used = realloc(pool->used, new_size * sizeof(bool));
    if (!new_objects || !new_used) {
        report_error(pool, POOL_ERROR_ALLOCATION_FAILED, "Failed to reallocate pool arrays");
        uv_mutex_unlock(&pool->mutex);
        pool->total_contention_time_ns += uv_hrtime() - start_time;
        return false;
    }

    pool->objects = new_objects;
    pool->used = new_used;
    pool->pool_size = new_size;
    if (pool->max_used > pool->pool_size) {
        pool->max_used = pool->pool_size; // Adjust max_used
    }
    pool->shrink_count++;
    uv_mutex_unlock(&pool->mutex);
    pool->total_contention_time_ns += uv_hrtime() - start_time;
    return true;
}

void* pool_acquire(object_pool_t* pool) {
    if (!pool) {
        report_error(NULL, POOL_ERROR_INVALID_POOL, "Invalid pool");
        return NULL;
    }

    uv_mutex_lock(&pool->mutex);
    pool->contention_attempts++;
    uint64_t start_time = uv_hrtime();

    if (pool->used_count >= pool->pool_size) {
        report_error(pool, POOL_ERROR_EXHAUSTED, "Pool exhausted");
        uv_mutex_unlock(&pool->mutex);
        pool->total_contention_time_ns += uv_hrtime() - start_time;
        return NULL;
    }

    for (size_t i = 0; i < pool->pool_size; i++) {
        if (!pool->used[i]) {
            if (!pool->allocator.validate(pool->objects[i])) {
                report_error(pool, POOL_ERROR_INVALID_OBJECT, "Invalid object at index");
                continue; // Skip invalid objects
            }
            pool->used[i] = true;
            pool->used_count++;
            pool->max_used = pool->used_count > pool->max_used ? pool->used_count : pool->max_used;
            pool->acquire_count++;
            pool->allocator.reset(pool->objects[i]);
            void* obj = pool->objects[i];
            uv_mutex_unlock(&pool->mutex);
            pool->total_contention_time_ns += uv_hrtime() - start_time;
            return obj;
        }
    }

    uv_mutex_unlock(&pool->mutex);
    pool->total_contention_time_ns += uv_hrtime() - start_time;
    return NULL; // Should not reach here
}

bool pool_release(object_pool_t* pool, void* object) {
    if (!pool || !object) {
        report_error(pool, POOL_ERROR_INVALID_POOL, "Invalid pool or object");
        return false;
    }

    uv_mutex_lock(&pool->mutex);
    pool->contention_attempts++;
    uint64_t start_time = uv_hrtime();

    // Check if object is in pool->objects to avoid invalid dereference
    bool is_pool_object = false;
    for (size_t i = 0; i < pool->pool_size; i++) {
        if (pool->objects[i] == object) {
            is_pool_object = true;
            break;
        }
    }
    if (!is_pool_object) {
        report_error(pool, POOL_ERROR_INVALID_OBJECT, "Object not in pool");
        uv_mutex_unlock(&pool->mutex);
        pool->total_contention_time_ns += uv_hrtime() - start_time;
        return false;
    }
    if (!pool->allocator.validate(object)) {
        report_error(pool, POOL_ERROR_INVALID_OBJECT, "Invalid object");
        uv_mutex_unlock(&pool->mutex);
        pool->total_contention_time_ns += uv_hrtime() - start_time;
        return false;
    }

    for (size_t i = 0; i < pool->pool_size; i++) {
        if (pool->objects[i] == object && pool->used[i]) {
            pool->used[i] = false;
            pool->used_count--;
            pool->release_count++;
            pool->allocator.reset(pool->objects[i]);
            uv_mutex_unlock(&pool->mutex);
            pool->total_contention_time_ns += uv_hrtime() - start_time;
            return true;
        }
    }

    report_error(pool, POOL_ERROR_INVALID_OBJECT, "Invalid or unused object");
    uv_mutex_unlock(&pool->mutex);
    pool->total_contention_time_ns += uv_hrtime() - start_time;
    return false;
}

size_t pool_used_count(object_pool_t* pool) {
    if (!pool) {
        return 0;
    }
    uv_mutex_lock(&pool->mutex);
    pool->contention_attempts++;
    uint64_t start_time = uv_hrtime();
    size_t count = pool->used_count;
    uv_mutex_unlock(&pool->mutex);
    pool->total_contention_time_ns += uv_hrtime() - start_time;
    return count;
}

size_t pool_capacity(object_pool_t* pool) {
    return pool ? pool->pool_size : 0; // No mutex needed, pool_size is immutable
}

void pool_stats(object_pool_t* pool, object_pool_stats_t* stats) {
    if (!pool || !stats) {
        return;
    }
    uv_mutex_lock(&pool->mutex);
    pool->contention_attempts++;
    uint64_t start_time = uv_hrtime();
    stats->max_used = pool->max_used;
    stats->acquire_count = pool->acquire_count;
    stats->release_count = pool->release_count;
    stats->contention_attempts = pool->contention_attempts;
    stats->total_contention_time_ns = pool->total_contention_time_ns;
    stats->total_objects_allocated = pool->total_objects_allocated;
    stats->grow_count = pool->grow_count;
    stats->shrink_count = pool->shrink_count;
    uv_mutex_unlock(&pool->mutex);
    pool->total_contention_time_ns += uv_hrtime() - start_time;
}

void pool_destroy(object_pool_t* pool) {
    if (!pool) {
        return;
    }

    for (size_t i = 0; i < pool->pool_size; i++) {
        pool->allocator.free(pool->objects[i]);
    }
    uv_mutex_destroy(&pool->mutex);
    free(pool->objects);
    free(pool->used);
    free(pool);
}