CReactivePool
A lightweight, high-performance, thread-safe object pool library in C, designed for reactive programming with libuv. Inspired by Quarkus's efficient resource management, CReactivePool provides a simple API for managing reusable objects (buffers, structs, or custom types), minimizing memory allocation overhead in event-driven, non-blocking systems.
Features

O(1) acquire and release operations for objects.
Thread-safe with reduced mutex contention using sub-pool partitioning.
Configurable pool size with dynamic growing and shrinking.
Custom object types via allocator interface with reset, validation, and lifecycle callbacks.
Object reset/initialization to ensure default state on acquire/release.
Backpressure support via request queueing for high-throughput scenarios.
Advanced pool usage statistics (max used, acquire/release counts, contention metrics, allocation history, queue usage).
Error callbacks for fine-grained error handling.
Robust error handling for pool exhaustion, invalid operations, and queue limits.
No external dependencies except libuv for thread safety.
Suitable for reactive streams, network programming, and high-throughput applications.

Installation

Install libuv:# Ubuntu
sudo apt install libuv1-dev
# macOS
brew install libuv


Clone the repository:git clone https://github.com/username/CReactivePool.git
cd CReactivePool


Build the example and tests:make



Usage
Basic Usage (Default Pool)
#include "object_pool.h"
#include <stdio.h>

int main() {
    object_pool_t* pool = pool_create_default();
    void* obj = pool_acquire(pool, NULL, NULL);
    if (obj) {
        printf("Acquired object: %p\n", obj);
        pool_release(pool, obj);
    }
    pool_destroy(pool);
    return 0;
}

Custom Object Usage
Define a custom object with allocator, reset, validation, lifecycle callbacks, and error handling:
#include "object_pool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

typedef struct {
    uint32_t magic; // 0xDEADBEEF
    char text[32];
    int id;
} Message;

void* message_alloc(void) {
    Message* msg = malloc(sizeof(Message));
    if (msg) {
        msg->magic = 0xDEADBEEF;
        msg->text[0] = '\0';
        msg->id = 0;
    }
    return msg;
}

void message_free(void* obj) {
    free(obj);
}

void message_reset(void* obj) {
    Message* msg = (Message*)obj;
    msg->magic = 0xDEADBEEF;
    msg->text[0] = '\0';
    msg->id = 0;
}

bool message_validate(void* obj) {
    return obj && ((Message*)obj)->magic == 0xDEADBEEF;
}

void message_on_create(void* obj) {
    printf("Created: %p\n", obj);
}

void message_on_destroy(void* obj) {
    printf("Destroyed: %p\n", obj);
}

void message_on_reuse(void* obj) {
    printf("Reusing: %p\n", obj);
}

void error_callback(object_pool_error_t error, const char* message, void* context) {
    printf("Error [%d]: %s\n", error, message);
}

void acquire_callback(void* object, void* context) {
    Message* msg = (Message*)object;
    if (msg) {
        strcpy(msg->text, "Backpressure");
        msg->id = *(int*)context;
        printf("Acquired via callback: text=%s, id=%d\n", msg->text, msg->id);
    }
}

int main() {
    object_pool_allocator_t allocator = {
        message_alloc, message_free, message_reset, message_validate,
        message_on_create, message_on_destroy, message_on_reuse, NULL
    };
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, NULL);
    if (pool_grow(pool, 2)) {
        printf("Pool grew to %zu objects\n", pool_capacity(pool));
    }
    if (pool_shrink(pool, 2)) {
        printf("Pool shrunk to %zu objects\n", pool_capacity(pool));
    }
    int callback_id = 3;
    Message* msg = pool_acquire(pool, acquire_callback, &callback_id);
    if (msg) {
        strcpy(msg->text, "Hello");
        msg->id = 1;
        printf("Message: text=%s, id=%d\n", msg->text, msg->id);
        pool_release(pool, msg);
    }
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    printf("Stats: max_used=%zu, acquires=%zu, releases=%zu, contention_attempts=%zu, "
           "contention_time_ns=%" PRIu64 ", total_objects=%zu, grows=%zu, shrinks=%zu, queue_max=%zu\n",
           stats.max_used, stats.acquire_count, stats.release_count,
           stats.contention_attempts, stats.total_contention_time_ns,
           stats.total_objects_allocated, stats.grow_count, stats.shrink_count,
           stats.queue_max_size);
    pool_destroy(pool);
    return 0;
}

Run the example:
./test_pool

Run the tests:
./test_pool_tests

Building

Requirements: GCC (or compatible C compiler), Make, libuv.
Commands:
make: Build the example and tests.
make clean: Remove build artifacts.



License
MIT License. See LICENSE for details.
Contributing
Contributions are welcome! Please submit issues or pull requests on GitHub.
Future Plans

Integration with libuv-based reactive streams (e.g., map, filter).

