CReactivePool
A lightweight, high-performance, thread-safe object pool library in C, designed for reactive programming with libuv. Inspired by Quarkus's efficient resource management, CReactivePool provides a simple API for managing reusable objects (buffers, structs, or custom types), minimizing memory allocation overhead in event-driven, non-blocking systems.
Features

O(1) acquire and release operations for objects.
Thread-safe using libuv mutexes, suitable for multi-threaded applications.
Configurable pool size and custom object types via allocator interface.
Object reset/initialization to ensure default state on acquire/release.
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
Define a custom object and allocator with reset:
#include "object_pool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    char text[32];
    int id;
} Message;

void* message_alloc(void) {
    Message* msg = malloc(sizeof(Message));
    if (msg) {
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
    msg->text[0] = '\0';
    msg->id = 0;
}

int main() {
    object_pool_allocator_t allocator = { message_alloc, message_free, message_reset, NULL };
    object_pool_t* pool = pool_create(4, allocator);
    Message* msg = pool_acquire(pool);
    if (msg) {
        strcpy(msg->text, "Hello");
        msg->id = 1;
        printf("Message: text=%s, id=%d\n", msg->text, msg->id);
        pool_release(pool, msg);
    }
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

Dynamic pool resizing for flexibility.
Integration with libuv-based reactive streams (e.g., map, filter).
Object validation for enhanced robustness.
Pool usage statistics for monitoring.

