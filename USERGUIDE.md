# Object Pool Library User Guide

## Overview

The Object Pool Library is a high-performance, thread-safe C library for managing reusable objects. It is designed for low-memory, multi-threaded applications, offering features like O(1) object release, dynamic resizing, backpressure handling, and load balancing across sub-pools. The library uses `libuv` for thread synchronization and supports custom allocators for flexible object management.

### Key Features
- **Thread-Safe Operations**: All functions are safe for concurrent use across multiple threads.
- **O(1) Object Release**: Uses metadata for constant-time object release.
- **Random Sub-Pool Selection**: Balances load in `pool_acquire` to minimize contention.
- **Dynamic Resizing**: Supports growing/shrinking the pool and backpressure queue.
- **Backpressure Handling**: Enqueues callbacks when objects are unavailable.
- **Custom Allocators**: Allows user-defined object allocation and management.
- **Statistics**: Tracks usage, contention, and operational metrics.

## Setup

### Prerequisites
- **Compiler**: GCC or Clang with C11 support.
- **Library**: `libuv` (install via `sudo apt-get install libuv1-dev` on Ubuntu).
- **Build Tool**: Make.

### Building
1. Clone the repository or copy the source files:
   ```
   include/object_pool.h
   src/object_pool.c
   examples/example_pool.c
   tests/test_object_pool.c
   Makefile
   ```
2. Build the library and examples:
   ```bash
   make all
   ```
3. Run the example:
   ```bash
   ./bin/example_pool
   ```
4. Run the tests:
   ```bash
   ./bin/test_object_pool
   ```

## Basic Usage

### Creating a Pool
Use `pool_create_default` for a default pool (16 objects, 4 sub-pools, 1-byte objects):
```c
#include "object_pool.h"

object_pool_t* pool = pool_create_default();
if (!pool) {
    fprintf(stderr, "Failed to create pool\n");
    return 1;
}
```

For custom settings, use `pool_create`:
```c
object_pool_allocator_t allocator = {
    .alloc = my_alloc,
    .free = my_free,
    .user_data = NULL
};
object_pool_t* pool = pool_create(32, 8, allocator, NULL, NULL);
```

### Acquiring and Releasing Objects
Acquire an object with `pool_acquire`:
```c
void* obj = pool_acquire(pool, NULL, NULL);
if (obj) {
    // Use the object
    pool_release(pool, obj);
}
```

### Destroying the Pool
Free all resources with `pool_destroy`:
```c
pool_destroy(pool);
```

## Advanced Features

### Custom Allocators
Define a custom allocator for specific object types:
```c
typedef struct {
    int id;
    char data[64];
} my_object_t;

void* my_alloc(void* user_data) {
    my_object_t* obj = malloc(sizeof(my_object_t));
    if (obj) {
        obj->id = 0;
        memset(obj->data, 0, sizeof(obj->data));
    }
    return obj;
}

void my_free(void* obj, void* user_data) {
    free(obj);
}

object_pool_allocator_t allocator = {
    .alloc = my_alloc,
    .free = my_free
};
object_pool_t* pool = pool_create(16, 4, allocator, NULL, NULL);
```

### Backpressure Handling
Use callbacks to handle pool exhaustion:
```c
void on_acquire(void* obj, void* context) {
    printf("Acquired object: %p\n", obj);
    // Use obj and release later
}

void* obj = pool_acquire(pool, on_acquire, NULL);
if (!obj) {
    printf("Object queued for backpressure\n");
}
```

### Dynamic Resizing
Grow or shrink the pool as needed:
```c
if (!pool_grow(pool, 16)) {
    fprintf(stderr, "Failed to grow pool\n");
}

if (!pool_shrink(pool, 8)) {
    fprintf(stderr, "Failed to shrink pool\n");
}
```

### Statistics
Monitor pool usage and performance:
```c
object_pool_stats_t stats;
pool_stats(pool, &stats);
printf("Max used: %zu, Contention time: %llu ns\n",
       stats.max_used, stats.total_contention_time_ns);
```

### Load Balancing
Check sub-pool acquire counts to verify load balancing:
```c
size_t count;
size_t* acquires = pool_get_sub_pool_acquire_counts(pool, &count);
if (acquires) {
    for (size_t i = 0; i < count; i++) {
        printf("Sub-pool %zu: %zu acquires\n", i, acquires[i]);
    }
    free(acquires);
}
```

## Thread Safety
All functions are thread-safe, using `libuv` mutexes. Ensure:
- Objects are not used after release.
- Callbacks handle objects in a thread-safe manner.
- Custom allocators are thread-safe if used concurrently.

## Error Handling
Register an error callback to handle issues:
```c
void my_error_callback(object_pool_error_t error, const char* message, void* context) {
    fprintf(stderr, "Error %d: %s\n", error, message);
}

object_pool_t* pool = pool_create(16, 4, allocator, my_error_callback, NULL);
```

## Performance Tips
- **Sub-Pool Count**: Use 4–8 sub-pools for optimal load balancing.
- **Pool Size**: Match the expected number of concurrent objects to minimize resizing.
- **Backpressure**: Use callbacks to handle high contention gracefully.
- **Custom Allocators**: Optimize allocation for specific object types to reduce overhead.
- **Statistics**: Monitor `contention_attempts` and `total_contention_time_ns` to identify bottlenecks.

## Example
See `examples/example_pool.c` for a complete example using a custom `Message` type:
```c
#include "object_pool.h"

typedef struct {
    char text[256];
    int id;
} Message;

void* message_alloc(void* user_data) {
    Message* msg = malloc(sizeof(Message));
    if (msg) {
        msg->id = 0;
        memset(msg->text, 0, sizeof(msg->text));
    }
    return msg;
}

void message_free(void* obj, void* user_data) {
    free(obj);
}

int main() {
    object_pool_allocator_t allocator = { .alloc = message_alloc, .free = message_free };
    object_pool_t* pool = pool_create(4, 4, allocator, NULL, NULL);
    Message* msg = pool_acquire(pool, NULL, NULL);
    if (msg) {
        snprintf(msg->text, sizeof(msg->text), "Hello");
        msg->id = 1;
        pool_release(pool, msg);
    }
    pool_destroy(pool);
    return 0;
}
```

## Limitations
- **Dependency**: Requires `libuv` for threading (see #10 for portability).
- **Memory**: Objects are pre-allocated, which may be memory-intensive for large pools.
- **Callbacks**: Backpressure callbacks must be thread-safe and prompt.

## Further Reading
- Generate Doxygen documentation:
  ```bash
  doxygen -g - > Doxyfile
  doxygen Doxyfile
  ```
- Review `include/object_pool.h` and `src/object_pool.c` for detailed comments.
- Run `tests/test_object_pool.c` to explore edge cases and thread safety.