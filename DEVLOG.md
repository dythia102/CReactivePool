# Object Pool Test Suite File List

Below is the full list of test files for the object pool library, indicating which are already implemented and which are still pending implementation.

## Implemented Test Files
1. **test_creation.c**  
   - Verifies pool creation, initial capacity, and used count.
2. **test_acquire_release.c**  
   - Tests object acquisition, modification, release, invalid releases, and statistics.
3. **test_stats_metadata.c**  
   - Ensures pool statistics (acquire/release counts, contention metrics) are accurately tracked.  
   - Verifies object metadata correctness (sub-pool and index integrity).
4. **test_max_used.c**  
   - Verifies the accuracy of the `max_used` statistic under peak usage scenarios.
5. **test_validation.c**  
   - Checks object validation, ensuring invalid objects are handled appropriately.
6. **test_default_pool.c**  
   - Tests default pool creation with `pool_create_default_with_size` and object initialization.
7. **test_exhaustion.c**  
   - Ensures the pool handles exhaustion correctly and reports errors when no objects are available.
8. **test_invalid_ops.c**  
   - Verifies behavior for invalid operations (e.g., acquiring/releasing from a null pool).
9. **test_thread_safety.c**  
   - Checks thread safety with multiple threads acquiring and releasing objects concurrently.
10. **test_fast_lookup.c**  
    - Tests the efficiency of object release using metadata for O(1) lookup.
11. **test_reset_reuse.c**  
    - Ensures objects are properly reset when reused after release, with no memory leaks (verified with Valgrind).
12. **test_grow.c**  
    - Tests dynamic pool growth, ensuring new objects are correctly added and usable.
13. **test_shrink.c**  
    - Verifies pool shrinkage removes unused objects without affecting in-use objects.
14. **test_load_balancing.c**  
    - Ensures load balancing across sub-pools by checking acquire counts in a multi-threaded scenario.
15. **test_backpressure.c**  
   - Should test backpressure handling when the pool is exhausted, including callback invocation.

## Test Files to Be Implemented
1. **test_concurrent_backpressure.c**  
   - Should verify backpressure handling in a multi-threaded, high-contention environment.

**Note**: The pending test files (`test_backpressure.c` and `test_concurrent_backpressure.c`) will be addressed in future development to ensure comprehensive coverage of the object pool's features.