#include "common.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

int main() {
    error_test_data_t error_data;
    reset_error_data(&error_data);

    // Create a pool with 4 objects across 2 sub-pools
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, &error_data);
    assert_true("Pool creation", pool != NULL);
    assert_true("Initial capacity", pool_capacity(pool) == 4);
    assert_true("Initial used count", pool_used_count(pool) == 0);

    const int cycles = 3;
    const int objects_per_cycle = 4;

    for (int cycle = 0; cycle < cycles; cycle++) {
        Message* objects[objects_per_cycle];

        // Acquire objects
        for (int i = 0; i < objects_per_cycle; i++) {
            objects[i] = pool_acquire(pool, NULL, NULL);
            assert_true("Acquire object", objects[i] != NULL);
            // Check reset state
            assert_true("Object id reset", objects[i]->id == 0);
            assert_true("Object text reset", objects[i]->text[0] == '\0');
        }

        // Modify objects
        for (int i = 0; i < objects_per_cycle; i++) {
            objects[i]->id = cycle * objects_per_cycle + i + 1;
            strcpy(objects[i]->text, "Used");
        }

        // Release objects
        for (int i = 0; i < objects_per_cycle; i++) {
            assert_true("Release object", pool_release(pool, objects[i]));
        }
    }

    // Final checks
    assert_true("Final used count", pool_used_count(pool) == 0);
    assert_true("No unexpected errors", error_data.error_count == 0);

    pool_destroy(pool);
    return 0;
}