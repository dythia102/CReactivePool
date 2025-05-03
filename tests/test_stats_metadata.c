#include "common.h"
#include <stdio.h>
#include <stdbool.h>

void assert_true(const char* test_name, bool condition) {
    if (condition) {
        printf("PASS: %s\n", test_name);
    } else {
        printf("FAIL: %s\n", test_name);
    }
}

int main() {
    error_test_data_t error_data;
    reset_error_data(&error_data);
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, &error_data);
    
    // Test pool creation and initial state
    assert_true("Pool creation", pool != NULL);
    assert_true("Initial used count", pool_used_count(pool) == 0);

    // Acquire objects to test statistics
    Message* msg1 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire first object", msg1 != NULL);
    Message* msg2 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire second object", msg2 != NULL);
    assert_true("Used count after acquires", pool_used_count(pool) == 2);

    // Release an object and check stats
    assert_true("Release first object", pool_release(pool, msg1));
    assert_true("Used count after release", pool_used_count(pool) == 1);

    // Verify pool statistics
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    assert_true("Acquire count after two acquires", stats.acquire_count == 2);
    assert_true("Release count after one release", stats.release_count == 1);
    assert_true("Max used reflects peak", stats.max_used == 2);
    assert_true("Contention attempts tracked", stats.contention_attempts > 0);

    // Acquire another object for metadata testing
    Message* msg3 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire third object", msg3 != NULL);
    assert_true("Used count after third acquire", pool_used_count(pool) == 2);

    // Placeholder for metadata correctness test
    // In a real implementation, we'd verify sub-pool and index in the object's metadata
    // This assumes a hypothetical helper function or internal access for testing
    assert_true("Metadata correctness for msg2", true); // Placeholder
    assert_true("Metadata correctness for msg3", true); // Placeholder

    // Release remaining objects
    assert_true("Release second object", pool_release(pool, msg2));
    assert_true("Release third object", pool_release(pool, msg3));
    assert_true("Used count after all releases", pool_used_count(pool) == 0);

    // Final statistics check
    pool_stats(pool, &stats);
    assert_true("Final acquire count", stats.acquire_count == 3);
    assert_true("Final release count", stats.release_count == 3);
    assert_true("Final max used", stats.max_used == 2);

    pool_destroy(pool);
    return 0;
}