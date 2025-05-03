# Object Pool Test Suite File List

Below is the full list of test files for the object pool library, indicating which are already implemented and which are still pending implementation.

## Implemented Test Files
1. **test_creation.c**
   - Verifies pool creation, initial capacity, and used count.
2. **test_acquire_release.c**
   - Tests object acquisition, modification, release, invalid releases, and statistics.
3. **test_stats_metadata.c**
   - Combines `test_stats.c` and `test_metadata.c`:
     - Ensures pool statistics (acquire/release counts, contention metrics) are accurately tracked.
     - Verifies object metadata correctness (sub-pool and index integrity).
4. **test_max_used.c**
   - Verifies the accuracy of the `max_used` statistic under peak usage scenarios.
5. **test_validation.c**
   - Checks object validation, ensuring invalid objects are handled appropriately.

## Test Files to Be Implemented
6. **test_exhaustion.c**
   - Ensures the pool handles exhaustion correctly and reports errors when no objects are available.
7. **test_invalid_ops.c**
   - Verifies behavior for invalid operations (e.g., acquiring/releasing from a null pool).
8. **test_default_pool.c**
   - Tests default pool creation with `pool_create_default_with_size` and object initialization.
9. **test_thread_safety.c**
   - Checks thread safety with multiple threads acquiring and releasing objects concurrently.
10. **test_reset_reuse.c**
    - Ensures objects are properly reset when reused after release.
11. **test_grow.c**
    - Tests dynamic pool growth, ensuring new objects are correctly added and usable.
12. **test_shrink.c**
    - Verifies pool shrinkage removes unused objects without affecting in-use objects.
13. **test_backpressure.c**
    - Tests backpressure handling when the pool is exhausted, including callback invocation.
14. **test_concurrent_backpressure.c**
    - Verifies backpressure handling in a multi-threaded, high-contention environment.
15. **test_fast_lookup.c**
    - Tests the efficiency of object release using metadata for O(1) lookup.
16. **test_load_balancing.c**
    - Ensures load balancing across sub-pools by checking acquire counts in a multi-threaded scenario.

## Notes
- The combined `test_stats_metadata.c` covers both statistics and metadata testing.
- New test files should follow the `test_*.c` naming convention for automatic Makefile integration.