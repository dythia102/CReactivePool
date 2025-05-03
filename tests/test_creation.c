#include "common.h"
#include <stdio.h>
#include <stdbool.h>

int main() {
    error_test_data_t error_data;
    reset_error_data(&error_data);
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, &error_data);
    assert_true("Pool creation", pool != NULL);
    assert_true("Pool capacity", pool_capacity(pool) == 4);
    assert_true("Pool used count", pool_used_count(pool) == 0);
    pool_destroy(pool);
    return 0;
}