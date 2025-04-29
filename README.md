CReactivePool
A lightweight, high-performance, thread-safe object pool library in C, designed for reactive programming with libuv. Inspired by Quarkus's efficient resource management, CReactivePool provides a simple API for managing reusable objects (buffers, structs, or custom types), minimizing memory allocation overhead in event-driven, non-blocking systems.
Features

O(1) acquire and release operations for objects.
Thread-safe using libuv mutexes, suitable for multi-threaded applications.
Configurable pool size with dynamic resizing.
Custom object types via allocator interface with reset and validation.
Object reset/initialization to ensure default state on acquire/release.
Pool usage statistics (max used, acquire/release counts).
Robust error handling for pool exhaustion and invalid operations.
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
    void* obj = pool_acquire(pool);
    if (obj) {
        printf("Acquired object: %p\n", obj);
        pool_release(pool, obj);
    }
    pool_destroy(pool);
    return 0;
}

Custom Object Usage
Define a custom object with allocator, reset, and validation:
#include "object_pool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

int main() {
    object_pool_allocator_t allocator = { message_alloc, message_free, message_reset, message_validate, NULL };
    object_pool_t* pool = pool_create(4, allocator);
    if (pool_grow(pool, 2)) {
        printf("Pool grew to %zu objects\n", pool_capacity(pool));
    }
    Message* msg = pool_acquire(pool);
    if (msg) {
        strcpy(msg->text, "Hello");
        msg->id = 1;
        printf("Message: text=%s, id=%d\n", msg->text, msg->id);
        pool_release(pool, msg);
    }
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    printf("Stats: max_used=%zu, acquires=%zu, releases=%zu\n",
           stats.max_used, stats.acquire_count, stats.release_count);
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

