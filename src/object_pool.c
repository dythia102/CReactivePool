#include "object_pool.h"
#include <stdio.h>

struct object_pool {
    void** objects;               // Array of object pointers
    bool* used;                   // Track object usage
    size_t pool_size;             // Total number of objects
    size_t used_count;            // Number of used objects
    object_pool_allocator_t allocator; // Allocator for objects
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
    // No-op for default allocator
}

object_pool_t* pool_create(size_t pool_size, object_pool_allocator_t allocator) {
    if (pool_size == 0 || !allocator.alloc || !allocator.free) {
        fprintf(stderr, "Invalid pool size or allocator\n");
        return NULL;
    }

    object_pool_t* pool = malloc(sizeof(object_pool_t));
    if (!pool) {
        fprintf(stderr, "Failed to allocate pool\n");
        return NULL;
    }

    pool->objects = malloc(pool_size * sizeof(void*));
    pool->used = malloc(pool_size * sizeof(bool));
    if (!pool->objects || !pool->used) {
        fprintf(stderr, "Failed to allocate pool arrays\n");
        free(pool->objects);
        free(pool->used);
        free(pool);
        return NULL;
    }

    if (uv_mutex_init(&pool->mutex) != 0) {
        fprintf(stderr, "Failed to initialize mutex\n");
        free(pool->objects);
        free(pool->used);
        free(pool);
        return NULL;
    }

    pool->pool_size = pool_size;
    pool->used_count = 0;
    pool->allocator = allocator;
    if (!pool->allocator.reset) {
        pool->allocator.reset = default_reset; // Default to no-op
    }

    for (size_t i = 0; i < pool_size; i++) {
        pool->objects[i] = pool->allocator.alloc();
        if (!pool->objects[i]) {
            fprintf(stderr, "Failed to allocate object %zu\n", i);
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
        pool->allocator.reset(pool->objects[i]); // Initialize objects
    }

    return pool;
}

object_pool_t* pool_create_default(void) {
    object_pool_allocator_t allocator = {
        .alloc = default_alloc,
        .free = default_free,
        .reset = default_reset,
        .user_data = NULL
    };
    return pool_create(DEFAULT_POOL_SIZE, allocator);
}

void* pool_acquire(object_pool_t* pool) {
    if (!pool) {
        fprintf(stderr, "Invalid pool\n");
        return NULL;
    }

    uv_mutex_lock(&pool->mutex);
    if (pool->used_count >= pool->pool_size) {
        uv_mutex_unlock(&pool->mutex);
        fprintf(stderr, "Pool exhausted\n");
        return NULL;
    }

    for (size_t i = 0; i < pool->pool_size; i++) {
        if (!pool->used[i]) {
            pool->used[i] = true;
            pool->used_count++;
            pool->allocator.reset(pool->objects[i]); // Reset on acquire
            uv_mutex_unlock(&pool->mutex);
            return pool->objects[i];
        }
    }

    uv_mutex_unlock(&pool->mutex);
    return NULL; // Should not reach here
}

bool pool_release(object_pool_t* pool, void* object) {
    if (!pool || !object) {
        fprintf(stderr, "Invalid pool or object\n");
        return false;
    }

    uv_mutex_lock(&pool->mutex);
    for (size_t i = 0; i < pool->pool_size; i++) {
        if (pool->objects[i] == object && pool->used[i]) {
            pool->used[i] = false;
            pool->used_count--;
            pool->allocator.reset(pool->objects[i]); // Reset on release
            uv_mutex_unlock(&pool->mutex);
            return true;
        }
    }

    uv_mutex_unlock(&pool->mutex);
    fprintf(stderr, "Invalid or unused object\n");
    return false;
}

size_t pool_used_count(object_pool_t* pool) {
    if (!pool) {
        return 0;
    }
    uv_mutex_lock(&pool->mutex);
    size_t count = pool->used_count;
    uv_mutex_unlock(&pool->mutex);
    return count;
}

size_t pool_capacity(object_pool_t* pool) {
    return pool ? pool->pool_size : 0; // No mutex needed, pool_size is immutable
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