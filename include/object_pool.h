#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

#include <stdlib.h>
#include <stdbool.h>
#include <uv.h> // For uv_mutex_t

#define DEFAULT_POOL_SIZE 16

// Allocator interface for custom objects
typedef struct {
    void* (*alloc)(void);          // Allocate a single object
    void (*free)(void*);           // Free a single object
    void (*reset)(void*);          // Reset object to default state (optional)
    bool (*validate)(void*);       // Validate object integrity (optional)
    void* user_data;               // Optional user data for allocator
} object_pool_allocator_t;

// Pool statistics
typedef struct {
    size_t max_used;               // Max concurrent objects used
    size_t acquire_count;          // Total acquire operations
    size_t release_count;          // Total release operations
} object_pool_stats_t;

// Opaque pool type
typedef struct object_pool object_pool_t;

// Create a thread-safe pool with specified size and allocator
object_pool_t* pool_create(size_t pool_size, object_pool_allocator_t allocator);

// Create a thread-safe pool with default size (16) and default allocator
object_pool_t* pool_create_default(void);

// Grow the pool by adding more objects
bool pool_grow(object_pool_t* pool, size_t additional_size);

// Shrink the pool by removing unused objects
bool pool_shrink(object_pool_t* pool, size_t reduce_size);

// Acquire an object from the pool; returns NULL if pool is exhausted
void* pool_acquire(object_pool_t* pool);

// Release an object back to the pool; returns false if object is invalid
bool pool_release(object_pool_t* pool, void* object);

// Get the number of used objects in the pool
size_t pool_used_count(object_pool_t* pool);

// Get the total capacity of the pool
size_t pool_capacity(object_pool_t* pool);

// Get pool usage statistics
void pool_stats(object_pool_t* pool, object_pool_stats_t* stats);

// Destroy the pool and free all resources
void pool_destroy(object_pool_t* pool);

#endif // OBJECT_POOL_H