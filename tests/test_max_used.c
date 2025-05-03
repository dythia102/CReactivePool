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
    assert_true("Initial used count", pool_used_count(pool) == 0);

    // Check initial max_used
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    assert_true("Initial max_used", stats.max_used == 0);

    // Array to hold acquired objects
    Message* objects[4] = {NULL};
    size_t acquired = 0;

    // Acquire 3 objects
    for (size_t i = 0; i < 3; i++) {
        objects[acquired] = pool_acquire(pool, NULL, NULL);
        assert_true("Acquire object", objects[acquired] != NULL);
        acquired++;
        pool_stats(pool, &stats);
        assert_true("Used count after acquire", pool_used_count(pool) == acquired);
        assert_true("Max used after acquire", stats.max_used == acquired);
    }

    // Release one object
    pool_release(pool, objects[acquired - 1]);
    objects[acquired - 1] = NULL;
    acquired--;
    pool_stats(pool, &stats);
    assert_true("Used count after release", pool_used_count(pool) == acquired);
    assert_true("Max used after release", stats.max_used == 3);  // Peak was 3

    // Acquire one more object
    objects[acquired] = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire object", objects[acquired] != NULL);
    acquired++;
    pool_stats(pool, &stats);
    assert_true("Used count after acquire", pool_used_count(pool) == acquired);
    assert_true("Max used after acquire", stats.max_used == 3);  // Acquired == 3 <= 3

    // Acquire one more to exceed previous max
    objects[acquired] = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire object", objects[acquired] != NULL);
    acquired++;
    pool_stats(pool, &stats);
    assert_true("Used count after acquire", pool_used_count(pool) == acquired);
    assert_true("Max used after acquire", stats.max_used == 4);  // Acquired == 4 > 3

    // Release all objects
    while (acquired > 0) {
        pool_release(pool, objects[acquired - 1]);
        objects[acquired - 1] = NULL;
        acquired--;
    }
    assert_true("Used count after all releases", pool_used_count(pool) == 0);
    pool_stats(pool, &stats);
    assert_true("Max used after all releases", stats.max_used == 4);  // Remains 4

    // Acquire 2 objects again
    for (size_t i = 0; i < 2; i++) {
        objects[acquired] = pool_acquire(pool, NULL, NULL);
        assert_true("Acquire object", objects[acquired] != NULL);
        acquired++;
    }
    pool_stats(pool, &stats);
    assert_true("Used count after acquiring 2", pool_used_count(pool) == 2);
    assert_true("Max used after acquiring 2", stats.max_used == 4);  // Still 4

    // Release all objects
    while (acquired > 0) {
        pool_release(pool, objects[acquired - 1]);
        objects[acquired - 1] = NULL;
        acquired--;
    }

    pool_destroy(pool);
    return 0;
}