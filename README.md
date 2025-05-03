# Object Pool Library

A high-performance, thread-safe C library for managing reusable objects, optimized for low-memory, multi-threaded applications. It offers dynamic resizing, backpressure handling, and load balancing.

## Features
- **O(1) Object Release**: Constant-time release using metadata.
- **Random Sub-Pool Selection**: Balances load across sub-pools for minimal contention.
- **Thread-Safe**: All operations are safe for concurrent use.
- **Dynamic Resizing**: Grow or shrink the pool and backpressure queue.
- **Custom Allocators**: Support for user-defined object management.
- **Backpressure**: Callback-based handling for pool exhaustion.
- **Statistics**: Detailed metrics (e.g., contention time, acquire counts).

## Getting Started

### Prerequisites
- **Compiler**: GCC or Clang with C11 support.
- **Build Tool**: Make.

### Installation
1. Clone the repository and navigate to the project directory.
2. Build the library and tests:
   ```bash
   make all
   ```

### Running
- Run the example:
  ```bash
  ./bin/example_pool
  ```
- Run the tests:
  ```bash
  make test
  ```

## Basic Usage
```c
#include "object_pool.h"

int main() {
    object_pool_t* pool = pool_create_default();
    if (!pool) {
        fprintf(stderr, "Failed to create pool\n");
        return 1;
    }
    void* obj = pool_acquire(pool, NULL, NULL);
    if (obj) {
        // Use the object
        pool_release(pool, obj);
    }
    pool_destroy(pool);
    return 0;
}
```

## Documentation
- **Tutorial**: See `TUTORIAL.md` for a step-by-step walkthrough of basic and advanced features.
- **User Guide**: See `USERGUIDE.md` for detailed usage, advanced features, and performance tips.

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.