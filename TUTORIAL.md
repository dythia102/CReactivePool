# Object Pool Library Tutorial

This tutorial walks you through using the Object Pool Library, a high-performance, thread-safe C library for managing reusable objects. You’ll learn how to create a pool, acquire and release objects, use custom allocators, handle backpressure, resize the pool, and monitor performance metrics. Each section includes compile-ready code snippets to get you started quickly.

For a broader overview, see the [User Guide](user_guide.md). For API details, generate Doxygen docs (`doxygen Doxyfile`).

## Prerequisites
- **Compiler**: GCC or Clang with C11 support.
- **Library**: `libuv` (`sudo apt-get install libuv1-dev` on Ubuntu).
- **Files**: Ensure you have `include/object_pool.h`, `src/object_pool.c`, and a `Makefile`.

## Step 1: Basic Pool Operations
Let’s create a default pool, acquire an object, use it, and release it back to the pool.

### Code
```c
#include "object_pool.h"
#include <stdio.h>

int main() {
    // Create a default pool (16 objects, 4 sub-pools, 1-byte objects)
    object_pool_t* pool = pool_create_default();
    if (!pool) {
        fprintf(stderr, "Failed to create pool\n");
        return 1;
    }

    // Acquire an object
    void* obj = pool_acquire(pool, NULL, NULL);
    if (obj) {
        printf("Acquired object: %p\n", obj);
        // Use the object (e.g., write data)
        *(char*)obj = 'A';
        printf("Object data: %c\n", *(char*)obj);
        // Release the object
        pool_release(pool, obj);
    } else {
        fprintf(stderr, "Failed to acquire object\n");
    }

    // Destroy the pool
    pool_destroy(pool);
    return 0;
}
```

### Explanation
- `pool_create_default()` creates a pool with 16 objects across 4 sub-pools, using a default allocator for 1-byte objects.
- `pool_acquire(pool, NULL, NULL)` gets an available object or returns `NULL` if none are free.
- `pool_release(pool, obj)` returns the object to the pool, using O(1) metadata lookup.
- `pool_destroy(pool)` frees all resources.

### Build and Run
Save as `basic.c`, then:
```bash
gcc -Iinclude basic.c src/object_pool.c -luv -o basic
./basic
```
**Expected Output**:
```
Acquired object: 0x...
Object data: A
```

## Step 2: Custom Allocators
Let’s define a custom object type (`Message`) and allocator to manage it.

### Code
```c
#include "object_pool.h"
#include <stdio.h>
#include <string.h>

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

void message_reset(void* obj, void* user_data) {
    Message* msg = (Message*)obj;
    if (msg) {
        msg->id = 0;
        memset(msg->text, 0, sizeof(msg->text));
    }
}

int main() {
    object_pool_allocator_t allocator = {
        .alloc = message_alloc,
        .free = message_free,
        .reset = message_reset
    };
    object_pool_t* pool = pool_create(4, 4, allocator, NULL, NULL);
    if (!pool) {
        fprintf(stderr, "Failed to create pool\n");
        return 1;
    }

    Message* msg = pool_acquire(pool, NULL, NULL);
    if (msg) {
        snprintf(msg->text, sizeof(msg->text), "Hello, World!");
        msg->id = 1;
        printf("Message: %s, ID: %d\n", msg->text, msg->id);
        pool_release(pool, msg);
    }

    pool_destroy(pool);
    return 0;
}
```

### Explanation
- `Message` is a custom struct with a text buffer and ID.
- The allocator (`message_alloc`, `message_free`, `message_reset`) manages `Message` objects, initializing and resetting them.
- `pool_create(4, 4, allocator, NULL, NULL)` creates a pool with 4 objects across 4 sub-pools.
- The acquired object is cast to `Message*` and used directly.

### Build and Run
Save as `custom.c`, then:
```bash
gcc -Iinclude custom.c src/object_pool.c -luv -o custom
./custom
```
**Expected Output**:
```
Message: Hello, World!, ID: 1
```

## Step 3: Backpressure Handling
When the pool is exhausted, use callbacks to handle backpressure.

### Code
```c
#include "object_pool.h"
#include <stdio.h>

void on_acquire(void* obj, void* context) {
    printf("Backpressure: Acquired object %p (context: %p)\n", obj, context);
    // Use the object
    *(char*)obj = 'B';
    printf("Object data: %c\n", *(char*)obj);
    // Release (in a real app, this might be delayed)
    object_pool_t* pool = (object_pool_t*)context;
    pool_release(pool, obj);
}

int main() {
    object_pool_t* pool = pool_create_default();
    if (!pool) {
        fprintf(stderr, "Failed to create pool\n");
        return 1;
    }

    // Exhaust the pool (16 objects)
    void* objects[16];
    for (int i = 0; i < 16; i++) {
        objects[i] = pool_acquire(pool, NULL, NULL);
        if (!objects[i]) {
            fprintf(stderr, "Failed to acquire object %d\n", i);
            break;
        }
    }

    // Try to acquire another object (triggers backpressure)
    void* obj = pool_acquire(pool, on_acquire, pool);
    if (!obj) {
        printf("Pool exhausted, callback enqueued\n");
    }

    // Release one object to trigger the callback
    pool_release(pool, objects[0]);

    // Clean up
    for (int i = 1; i < 16; i++) {
        if (objects[i]) {
            pool_release(pool, objects[i]);
        }
    }
    pool_destroy(pool);
    return 0;
}
```

### Explanation
- `on_acquire` is called when an object becomes available after pool exhaustion.
- The pool is exhausted by acquiring all 16 objects.
- `pool_acquire(pool, on_acquire, pool)` enqueues the callback, which runs when an object is released.
- Releasing one object triggers the callback, demonstrating backpressure handling.

### Build and Run
Save as `backpressure.c`, then:
```bash
gcc -Iinclude backpressure.c src/object_pool.c -luv -o backpressure
./backpressure
```
**Expected Output**:
```
Pool exhausted, callback enqueued
Backpressure: Acquired object 0x... (context: 0x...)
Object data: B
```

## Step 4: Dynamic Resizing
Grow and shrink the pool to adjust capacity.

### Code
```c
#include "object_pool.h"
#include <stdio.h>

int main() {
    object_pool_t* pool = pool_create_default();
    if (!pool) {
        fprintf(stderr, "Failed to create pool\n");
        return 1;
    }

    // Check initial capacity
    printf("Initial capacity: %zu\n", pool_capacity(pool));

    // Grow the pool by 8 objects
    if (pool_grow(pool, 8)) {
        printf("After growing: %zu\n", pool_capacity(pool));
    } else {
        fprintf(stderr, "Failed to grow pool\n");
    }

    // Acquire an object from the expanded pool
    void* obj = pool_acquire(pool, NULL, NULL);
    if (obj) {
        printf("Acquired object: %p\n", obj);
        pool_release(pool, obj);
    }

    // Shrink the pool by 4 objects
    if (pool_shrink(pool, 4)) {
        printf("After shrinking: %zu\n", pool_capacity(pool));
    } else {
        fprintf(stderr, "Failed to shrink pool\n");
    }

    pool_destroy(pool);
    return 0;
}
```

### Explanation
- `pool_capacity(pool)` returns the total number of objects (initially 16).
- `pool_grow(pool, 8)` adds 8 objects, increasing capacity to 24.
- `pool_shrink(pool, 4)` removes 4 unused objects, reducing capacity to 20.
- The acquired object confirms the pool remains functional after resizing.

### Build and Run
Save as `resize.c`, then:
```bash
gcc -Iinclude resize.c src/object_pool.c -luv -o resize
./resize
```
**Expected Output**:
```
Initial capacity: 16
After growing: 24
Acquired object: 0x...
After shrinking: 20
```

## Step 5: Monitoring Statistics and Load Balancing
Query pool statistics and verify sub-pool load balancing.

### Code
```c
#include "object_pool.h"
#include <stdio.h>

int main() {
    object_pool_t* pool = pool_create(8, 4, (object_pool_allocator_t){
        .alloc = malloc, .free = free
    }, NULL, NULL);
    if (!pool) {
        fprintf(stderr, "Failed to create pool\n");
        return 1;
    }

    // Simulate usage
    void* objects[8];
    for (int i = 0; i < 8; i++) {
        objects[i] = pool_acquire(pool, NULL, NULL);
    }
    for (int i = 0; i < 8; i++) {
        if (objects[i]) {
            pool_release(pool, objects[i]);
        }
    }

    // Get statistics
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    printf("Max used: %zu\n", stats.max_used);
    printf("Acquires: %zu\n", stats.acquire_count);
    printf("Contention time: %llu ns\n", stats.total_contention_time_ns);

    // Check sub-pool acquire counts
    size_t count;
    size_t* acquires = pool_get_sub_pool_acquire_counts(pool, &count);
    if (acquires) {
        for (size_t i = 0; i < count; i++) {
            printf("Sub-pool %zu: %zu acquires\n", i, acquires[i]);
        }
        free(acquires);
    }

    pool_destroy(pool);
    return 0;
}
```

### Explanation
- `pool_create(8, 4, ...)` creates a pool with 8 objects across 4 sub-pools, using `malloc`/`free` for simplicity.
- Simulating usage (acquire/release) generates statistics.
- `pool_stats(pool, &stats)` retrieves metrics like `max_used` and `contention_time_ns`.
- `pool_get_sub_pool_acquire_counts` shows how acquires are distributed, verifying random sub-pool selection.

### Build and Run
Save as `stats.c`, then:
```bash
gcc -Iinclude stats.c src/object_pool.c -luv -o stats
./stats
```
**Expected Output**:
```
Max used: 8
Acquires: 8
Contention time: ... ns
Sub-pool 0: ... acquires
Sub-pool 1: ... acquires
Sub-pool 2: ... acquires
Sub-pool 3: ... acquires
```

## Next Steps
- Explore advanced features in the [User Guide](user_guide.md).
- Run `examples/example_pool.c` for a real-world example.
- Generate Doxygen docs for API details:
  ```bash
  doxygen -g - > Doxyfile
  doxygen Doxyfile
  ```