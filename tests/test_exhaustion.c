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
    assert_true("Pool creation", pool != NULL);
    assert_true("Initial capacity", pool_capacity(pool) == 4);
    assert_true("Initial used count", pool_used_count(pool) == 0);
    assert_true("No initial errors", error_data.error_count == 0);

    // Acquire 4 objects
    Message* objects[4] = {NULL};
    for (size_t i = 0; i < 4; i++) {
        objects[i] = pool_acquire(pool, NULL, NULL);
        assert_true("Acquire object", objects[i] != NULL);
    }
    assert_true("Used count after acquiring all", pool_used_count(pool) == 4);
    assert_true("No errors during acquisitions", error_data.error_count == 0);

    // Attempt to acquire one more object
    reset_error_data(&error_data);  // Reset to check for exhaustion error
    Message* extra_obj = pool_acquire(pool, NULL, NULL);
    assert_true("Pool exhaustion", extra_obj == NULL);
    assert_true("Exhaustion error", error_data.error_count == 1 && error_data.last_error == POOL_ERROR_EXHAUSTED);

    // Check queue statistics
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    assert_true("Queue max size is 0", stats.queue_max_size == 0);

    // Release all objects
    for (size_t i = 0; i < 4; i++) {
        assert_true("Release object", pool_release(pool, objects[i]));
        objects[i] = NULL;  // Prevent double-release
    }
    assert_true("Used count after releasing all", pool_used_count(pool) == 0);

    // Verify pool is functional after release
    Message* test_obj = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire after release", test_obj != NULL);
    pool_release(pool, test_obj);

    pool_destroy(pool);
    return 0;
}