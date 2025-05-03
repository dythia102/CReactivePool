#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

void assert_true(const char* test_name, bool condition) {
    if (condition) {
        printf("PASS: %s\n", test_name);
    } else {
        printf("FAIL: %s\n", test_name);
    }
}

void test_pool_grow(void) {
    // Initialize error data
    error_test_data_t error_data;
    reset_error_data(&error_data);

    // Create pool with 4 objects, 2 sub-pools
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, &error_data);
    assert_true("Pool creation", pool != NULL);
    assert_true("Initial capacity", pool_capacity(pool) == 4);
    assert_true("Initial used count", pool_used_count(pool) == 0);

    // Acquire 2 objects and modify them
    Message* obj1 = (Message*)pool_acquire(pool, NULL, NULL);
    assert_true("Acquire obj1", obj1 != NULL);
    strcpy(obj1->text, "Test1");
    obj1->id = 1;

    Message* obj2 = (Message*)pool_acquire(pool, NULL, NULL);
    assert_true("Acquire obj2", obj2 != NULL);
    strcpy(obj2->text, "Test2");
    obj2->id = 2;
    assert_true("Used count after 2 acquires", pool_used_count(pool) == 2);

    // Grow pool by 2 objects
    bool grew = pool_grow(pool, 2);
    assert_true("Pool grow by 2", grew);
    assert_true("Capacity after grow", pool_capacity(pool) == 6);

    // Check that original objects are unaffected
    assert_true("obj1 unchanged after grow", strcmp(obj1->text, "Test1") == 0 && obj1->id == 1);
    assert_true("obj2 unchanged after grow", strcmp(obj2->text, "Test2") == 0 && obj2->id == 2);

    // Acquire 2 more objects
    Message* obj3 = (Message*)pool_acquire(pool, NULL, NULL);
    assert_true("Acquire obj3 after grow", obj3 != NULL);
    assert_true("obj3 initialized", obj3->text[0] == '\0' && obj3->id == 0);

    Message* obj4 = (Message*)pool_acquire(pool, NULL, NULL);
    assert_true("Acquire obj4 after grow", obj4 != NULL);
    assert_true("obj4 initialized", obj4->text[0] == '\0' && obj4->id == 0);
    assert_true("Used count after 4 acquires", pool_used_count(pool) == 4);

    // Verify new objects are distinct
    assert_true("obj3 distinct", obj3 != obj1 && obj3 != obj2);
    assert_true("obj4 distinct", obj4 != obj1 && obj4 != obj2 && obj4 != obj3);

    // Release all objects
    assert_true("Release obj1", pool_release(pool, obj1));
    assert_true("Release obj2", pool_release(pool, obj2));
    assert_true("Release obj3", pool_release(pool, obj3));
    assert_true("Release obj4", pool_release(pool, obj4));
    assert_true("Used count after releases", pool_used_count(pool) == 0);

    // Check statistics
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    assert_true("Grow count incremented", stats.grow_count == 1);
    assert_true("Total objects allocated", stats.total_objects_allocated == 6);

    // Test growing by zero objects
    reset_error_data(&error_data);
    bool grew_zero = pool_grow(pool, 0);
    assert_true("Grow by zero fails", !grew_zero);
    assert_true("Capacity unchanged after zero grow", pool_capacity(pool) == 6);
    assert_true("Error reported for zero grow", error_data.error_count > 0 && error_data.last_error == POOL_ERROR_INVALID_SIZE);

    // Test growing with invalid pool
    reset_error_data(&error_data);
    bool grew_invalid = pool_grow(NULL, 1);
    assert_true("Grow with NULL pool fails", !grew_invalid);

    // Clean up
    pool_destroy(pool);
}

int main(void) {
    test_pool_grow();
    return 0;
}