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

    // Test acquiring a valid object
    Message* msg1 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire valid object", msg1 != NULL);
    assert_true("Object is valid", allocator.validate(msg1, allocator.user_data));

    // Test releasing an invalid object
    Message dummy;
    dummy.magic = 0xBADBAD;  // Invalid magic
    reset_error_data(&error_data);
    assert_true("Release invalid object", !pool_release(pool, &dummy));
    assert_true("Invalid object error", error_data.error_count > 0 && error_data.last_error == POOL_ERROR_INVALID_OBJECT);

    // Test releasing a corrupted object
    Message* msg2 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire second object", msg2 != NULL);
    msg2->magic = 0xBADBAD;  // Corrupt the magic number
    reset_error_data(&error_data);
    assert_true("Release corrupted object", !pool_release(pool, msg2));
    assert_true("Corrupted object error", error_data.error_count > 0 && error_data.last_error == POOL_ERROR_INVALID_OBJECT);
    // Restore magic number and release properly
    msg2->magic = 0xDEADBEEF;
    assert_true("Release after fixing", pool_release(pool, msg2));

    // Test that all acquired objects are valid
    for (size_t i = 0; i < 4; i++) {
        Message* msg = pool_acquire(pool, NULL, NULL);
        assert_true("Acquire valid object", msg != NULL);
        assert_true("Object is valid", allocator.validate(msg, allocator.user_data));
        pool_release(pool, msg);
    }

    // Release the first object
    assert_true("Release first object", pool_release(pool, msg1));

    pool_destroy(pool);
    return 0;
}