#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

void test_pool_shrink(void) {
    // Initialize error data
    error_test_data_t error_data;
    reset_error_data(&error_data);

    // Create pool with 6 objects, 2 sub-pools
    object_pool_t* pool = pool_create(6, 2, allocator, error_callback, &error_data);
    assert_true("Pool creation", pool != NULL);
    assert_true("Initial capacity", pool_capacity(pool) == 6);
    assert_true("Initial used count", pool_used_count(pool) == 0);

    // Acquire 2 objects
    Message* obj1 = (Message*)pool_acquire(pool, NULL, NULL);
    assert_true("Acquire obj1", obj1 != NULL);
    strcpy(obj1->text, "Test1");
    obj1->id = 1;

    Message* obj2 = (Message*)pool_acquire(pool, NULL, NULL);
    assert_true("Acquire obj2", obj2 != NULL);
    strcpy(obj2->text, "Test2");
    obj2->id = 2;
    assert_true("Used count after 2 acquires", pool_used_count(pool) == 2);

    // Shrink pool by 2 objects (should succeed since there are 4 unused)
    bool shrank = pool_shrink(pool, 2);
    assert_true("Pool shrink by 2", shrank);
    assert_true("Capacity after shrink", pool_capacity(pool) == 4);

    // Check that in-use objects are unaffected
    assert_true("obj1 unchanged after shrink", strcmp(obj1->text, "Test1") == 0 && obj1->id == 1);
    assert_true("obj2 unchanged after shrink", strcmp(obj2->text, "Test2") == 0 && obj2->id == 2);

    // Acquire 2 more objects (should succeed, as capacity is now 4 with 2 in use)
    Message* obj3 = (Message*)pool_acquire(pool, NULL, NULL);
    assert_true("Acquire obj3 after shrink", obj3 != NULL);
    assert_true("obj3 initialized", obj3->text[0] == '\0' && obj3->id == 0);

    Message* obj4 = (Message*)pool_acquire(pool, NULL, NULL);
    assert_true("Acquire obj4 after shrink", obj4 != NULL);
    assert_true("obj4 initialized", obj4->text[0] == '\0' && obj4->id == 0);
    assert_true("Used count after 4 acquires", pool_used_count(pool) == 4);

    // Attempt to acquire one more object (should fail)
    Message* obj5 = (Message*)pool_acquire(pool, NULL, NULL);
    assert_true("Pool exhausted after shrink", obj5 == NULL);

    // Release all objects
    assert_true("Release obj1", pool_release(pool, obj1));
    assert_true("Release obj2", pool_release(pool, obj2));
    assert_true("Release obj3", pool_release(pool, obj3));
    assert_true("Release obj4", pool_release(pool, obj4));
    assert_true("Used count after releases", pool_used_count(pool) == 0);

    // Check statistics
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    assert_true("Shrink count incremented", stats.shrink_count == 1);
    assert_true("Total objects allocated", stats.total_objects_allocated == 4);

    // Test shrinking by more than available unused objects
    obj1 = (Message*)pool_acquire(pool, NULL, NULL);
    obj2 = (Message*)pool_acquire(pool, NULL, NULL);
    assert_true("Used count before shrink attempt", pool_used_count(pool) == 2);

    // Attempt to shrink by 3 objects (only 2 unused available)
    reset_error_data(&error_data);
    bool shrank_too_much = pool_shrink(pool, 3);
    assert_true("Shrink by more than unused fails", !shrank_too_much);
    assert_true("Capacity unchanged after failed shrink", pool_capacity(pool) == 4);
    assert_true("Error reported for insufficient unused", error_data.error_count > 0 && error_data.last_error == POOL_ERROR_INSUFFICIENT_UNUSED);

    // Release objects
    pool_release(pool, obj1);
    pool_release(pool, obj2);

    // Test shrinking by zero objects
    reset_error_data(&error_data);
    bool shrank_zero = pool_shrink(pool, 0);
    assert_true("Shrink by zero fails", !shrank_zero);
    assert_true("Capacity unchanged after zero shrink", pool_capacity(pool) == 4);
    assert_true("Error reported for zero shrink", error_data.error_count > 0 && error_data.last_error == POOL_ERROR_INVALID_SIZE);

    // Test shrinking with invalid pool
    reset_error_data(&error_data);
    bool shrank_invalid = pool_shrink(NULL, 1);
    assert_true("Shrink with NULL pool fails", !shrank_invalid);

    // Clean up
    pool_destroy(pool);
}

int main(void) {
    test_pool_shrink();
    return 0;
}