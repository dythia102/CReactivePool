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

    // Create first pool with 4 objects across 2 sub-pools
    object_pool_t* pool1 = pool_create(4, 2, allocator, error_callback, &error_data);
    assert_true("Pool1 creation", pool1 != NULL);
    assert_true("Initial capacity", pool_capacity(pool1) == 4);
    assert_true("Initial used count", pool_used_count(pool1) == 0);

    // Acquire all 4 objects from pool1
    Message* objects[4] = {NULL};
    for (size_t i = 0; i < 4; i++) {
        objects[i] = pool_acquire(pool1, NULL, NULL);
        assert_true("Acquire object from pool1", objects[i] != NULL);
    }
    assert_true("Used count after acquiring all", pool_used_count(pool1) == 4);

    // Release each object one by one
    for (size_t i = 0; i < 4; i++) {
        assert_true("Release object", pool_release(pool1, objects[i]));
        objects[i] = NULL;  // Prevent double-release
        assert_true("Used count decreases", pool_used_count(pool1) == 3 - i);
    }
    assert_true("Used count after releasing all", pool_used_count(pool1) == 0);

    // Attempt to release an object not in the pool
    Message dummy;
    reset_error_data(&error_data);
    assert_true("Release invalid object", !pool_release(pool1, &dummy));
    assert_true("Error for invalid object", error_data.error_count == 1 && error_data.last_error == POOL_ERROR_INVALID_OBJECT);

    // Acquire an object, release it, then try to release it again
    Message* test_obj = pool_acquire(pool1, NULL, NULL);
    assert_true("Acquire test object", test_obj != NULL);
    assert_true("Release test object", pool_release(pool1, test_obj));
    reset_error_data(&error_data);
    assert_true("Double release fails", !pool_release(pool1, test_obj));
    assert_true("Error for double release", error_data.error_count == 1 && error_data.last_error == POOL_ERROR_INVALID_OBJECT);

    // Create a second pool
    object_pool_t* pool2 = pool_create(2, 1, allocator, error_callback, &error_data);
    assert_true("Pool2 creation", pool2 != NULL);
    Message* obj_from_pool2 = pool_acquire(pool2, NULL, NULL);
    assert_true("Acquire from pool2", obj_from_pool2 != NULL);

    // Attempt to release an object from pool2 into pool1
    reset_error_data(&error_data);
    assert_true("Release object from wrong pool", !pool_release(pool1, obj_from_pool2));
    assert_true("Error for wrong pool release", error_data.error_count == 1 && error_data.last_error == POOL_ERROR_INVALID_OBJECT);

    // Clean up
    pool_release(pool2, obj_from_pool2);
    pool_destroy(pool1);
    pool_destroy(pool2);
    return 0;
}