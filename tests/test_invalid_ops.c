#include "common.h"
#include <stdio.h>
#include <stdbool.h>

int main() {
    error_test_data_t error_data;
    reset_error_data(&error_data);

    // Test 1: Acquire from null pool
    reset_error_data(&error_data);
    void* obj = pool_acquire(NULL, NULL, NULL);
    assert_true("Acquire from null pool returns NULL", obj == NULL);
    // Note: Error is printed to stderr

    // Test 2: Release to null pool
    reset_error_data(&error_data);
    bool result = pool_release(NULL, (void*)0xDEADBEEF);
    assert_true("Release to null pool returns false", result == false);
    // Note: Error is printed to stderr

    // Test 3: Release null object to valid pool
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, &error_data);
    assert_true("Pool creation for release test", pool != NULL);
    reset_error_data(&error_data);
    result = pool_release(pool, NULL);
    assert_true("Release null object returns false", result == false);
    assert_true("Error callback called for invalid object", error_data.error_count == 1);
    assert_true("Error type is POOL_ERROR_INVALID_POOL", error_data.last_error == POOL_ERROR_INVALID_POOL);
    pool_destroy(pool);

    // Test 4: Create pool with zero pool size
    reset_error_data(&error_data);
    pool = pool_create(0, 2, allocator, error_callback, &error_data);
    assert_true("Create with zero pool size returns NULL", pool == NULL);
    assert_true("Error callback called for invalid size", error_data.error_count == 1);
    assert_true("Error type is POOL_ERROR_INVALID_SIZE", error_data.last_error == POOL_ERROR_INVALID_SIZE);

    // Test 5: Create pool with zero sub-pool count
    reset_error_data(&error_data);
    pool = pool_create(4, 0, allocator, error_callback, &error_data);
    assert_true("Create with zero sub-pool count returns NULL", pool == NULL);
    assert_true("Error callback called for invalid sub-pool count", error_data.error_count == 1);
    assert_true("Error type is POOL_ERROR_INVALID_SIZE", error_data.last_error == POOL_ERROR_INVALID_SIZE);

    // Test 6: Create pool with invalid allocator (null alloc and free)
    object_pool_allocator_t invalid_allocator = {0};  // alloc and free are NULL
    reset_error_data(&error_data);
    pool = pool_create(4, 2, invalid_allocator, error_callback, &error_data);
    assert_true("Create with invalid allocator returns NULL", pool == NULL);
    assert_true("Error callback called for invalid allocator", error_data.error_count == 1);
    assert_true("Error type is POOL_ERROR_INVALID_SIZE", error_data.last_error == POOL_ERROR_INVALID_SIZE);

    // Test 7: Create pool with sub_pool_count exceeding limit
    reset_error_data(&error_data);
    pool = pool_create(4, 0x10000, allocator, error_callback, &error_data);  // 0x10000 > 0xFFFF
    assert_true("Create with excessive sub-pool count returns NULL", pool == NULL);
    // Note: Error is printed to stderr

    // Test 8: Destroy null pool
    pool_destroy(NULL);
    assert_true("Destroy null pool does not crash", true);

    return 0;
}