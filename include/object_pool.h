#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

#include <stdlib.h>
#include <stdbool.h>
#include <uv.h> // For uv_mutex_t

#define DEFAULT_POOL_SIZE 16
#define DEFAULT_SUB_POOL_COUNT 4
#define DEFAULT_QUEUE_CAPACITY 32
#define DEFAULT_OBJECT_SIZE 64 // Default size for objects in pool_create_default_with_size

// Metadata stored with each object for efficient lookup
typedef struct {
    struct sub_pool* sub_pool; // Pointer to owning sub-pool (opaque)
    size_t index;              // Index in sub-pool's objects array
} pool_object_metadata_t;

// Allocator interface for custom objects
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

// Error types
typedef enum {
    POOL_ERROR_NONE,
    POOL_ERROR_INVALID_POOL,
    POOL_ERROR_INVALID_OBJECT,
    POOL_ERROR_EXHAUSTED,
    POOL_ERROR_ALLOCATION_FAILED,
    POOL_ERROR_INVALID_SIZE,
    POOL_ERROR_INSUFFICIENT_UNUSED,
    POOL_ERROR_QUEUE_FULL
} object_pool_error_t;

// Error callback
typedef void (*object_pool_error_callback_t)(object_pool_error_t error, const char* message, void* context);

// Acquire callback for backpressure
typedef void (*object_pool_acquire_callback_t)(void* object, void* context);

// Pool statistics
typedef struct {
    size_t max_used;               // Max concurrent objects used across all sub-pools
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

// Create a thread-safe pool with specified size, sub-pool count, allocator, and optional error callback
object_pool_t* pool_create(size_t pool_size, size_t sub_pool_count, object_pool_allocator_t allocator,
                           object_pool_error_callback_t error_callback, void* error_context);

// Create a thread-safe pool with default size (16), sub-pool count (4), and default allocator
object_pool_t* pool_create_default(void);

// Create a thread-safe pool with default size (16), sub-pool count (4), and default allocator with specified object size
object_pool_t* pool_create_default_with_size(size_t object_size);

// Grow the pool by adding more objects
bool pool_grow(object_pool_t* pool, size_t additional_size);

// Grow the request queue by adding more capacity
bool pool_grow_queue(object_pool_t* pool, size_t additional_capacity);

// Shrink the pool by removing unused objects
bool pool_shrink(object_pool_t* pool, size_t reduce_size);

// Acquire an object from the pool; if exhausted, enqueue callback (NULL if queue full)
void* pool_acquire(object_pool_t* pool, object_pool_acquire_callback_t callback, void* context);

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