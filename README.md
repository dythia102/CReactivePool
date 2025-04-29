# CReactivePoolCReactivePool
A lightweight, high-performance object pool library in C, designed for reactive programming with libuv. Inspired by Quarkus's efficient resource management, CReactivePool provides a simple API for managing reusable buffers, minimizing memory allocation overhead in event-driven, non-blocking systems.
Features

O(1) acquire and release operations for fixed-size buffers.
Configurable pool size and buffer size.
Robust error handling for pool exhaustion and invalid operations.
Single-threaded by default, with thread-safe extensions planned.
No external dependencies, pure C implementation.
Suitable for reactive streams, network programming, and high-throughput applications.

Installation

Clone the repository:git clone https://github.com/username/CReactivePool.git
cd CReactivePool


Build the example and tests:make



Usage
Include the header and link against the library:
#include "object_pool.h"
#include <stdio.h>
#include <string.h>

int main() {
    object_pool_t* pool = pool_create(4, 32);
    char* buffer = pool_acquire(pool);
    if (buffer) {
        strcpy(buffer, "Hello");
        printf("Buffer: %s\n", buffer);
        pool_release(pool, buffer);
    }
    pool_destroy(pool);
    return 0;
}

Run the example:
./test_pool

Run the tests:
./test_pool_tests

Building

Requirements: GCC (or compatible C compiler), Make.
Commands:
make: Build the example and tests.
make clean: Remove build artifacts.



License
MIT License. See LICENSE for details.
Contributing
Contributions are welcome! Please submit issues or pull requests on GitHub.
Future Plans

Thread-safe pool variant using libuv mutexes.
Support for custom object types (e.g., structs).
Integration with libuv-based reactive streams (e.g., map, filter).
Dynamic pool resizing for flexibility.

