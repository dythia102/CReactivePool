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
    acquire_test_data_t acquire_data = {0};
    int callback_id = 5;
    acquire_data.context_id = &callback_id;

    // Create pool with 4 objects across 2 sub-pools
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, &error_data);
    assert_true("Pool creation", pool != NULL);

    // Exhaust the pool
    Message* held_objects[4];
    for (size_t i = 0; i < 4; i++) {
        held_objects[i] = pool_acquire(pool, NULL, NULL);
        assert_true("Exhaust pool for backpressure", held_objects[i] != NULL);
    }

    // Enqueue backpressure requests
    for (size_t i = 0; i < 2; i++) {
        pool_acquire(pool, acquire_callback, &acquire_data);
    }
    assert_true("Backpressure queue", acquire_data.callback_count == 0);

    // Release one object to trigger callback
    if (held_objects[0]) {
        pool_release(pool, held_objects[0]);
        held_objects[0] = NULL;
    }
    assert_true("Backpressure callback", acquire_data.callback_count == 1 && acquire_data.last_object != NULL);
    assert_true("Backpressure object", acquire_data.last_object->id == callback_id);

    // Clean up
    if (acquire_data.last_object) {
        pool_release(pool, acquire_data.last_object);
        acquire_data.last_object = NULL;
    }
    for (size_t i = 1; i < 4; i++) {
        if (held_objects[i]) {
            pool_release(pool, held_objects[i]);
            held_objects[i] = NULL;
        }
    }
    pool_destroy(pool);
    return 0;
}