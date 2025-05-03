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

int main() {
    error_test_data_t error_data;
    reset_error_data(&error_data);
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, &error_data);
    assert_true("Pool creation", pool != NULL);
    assert_true("Initial used count", pool_used_count(pool) == 0);

    // Acquire first object
    Message* msg1 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire first object", msg1 != NULL);
    assert_true("Used count after first acquire", pool_used_count(pool) == 1);
    assert_true("First object reset", msg1->text[0] == '\0' && msg1->id == 0);

    // Modify object content
    strcpy(msg1->text, "Test");
    msg1->id = 1;
    assert_true("Object content modified", strcmp(msg1->text, "Test") == 0 && msg1->id == 1);

    // Acquire second object
    Message* msg2 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire second object", msg2 != NULL);
    assert_true("Used count after second acquire", pool_used_count(pool) == 2);

    // Release first object
    assert_true("Release first object", pool_release(pool, msg1));
    assert_true("Used count after first release", pool_used_count(pool) == 1);

    // Re-acquire object and check reset
    Message* msg3 = pool_acquire(pool, NULL, NULL);
    assert_true("Re-acquire object", msg3 != NULL);
    assert_true("Used count after re-acquire", pool_used_count(pool) == 2);
    assert_true("Object reset on reuse", msg3->text[0] == '\0' && msg3->id == 0);

    // Release second object
    assert_true("Release second object", pool_release(pool, msg2));
    assert_true("Used count after second release", pool_used_count(pool) == 1);

    // Attempt to release invalid object
    reset_error_data(&error_data);
    assert_true("Release invalid object", !pool_release(pool, (void*)0xDEADBEEF));
    assert_true("Invalid object error", error_data.error_count > 0 && error_data.last_error == POOL_ERROR_INVALID_OBJECT);

    // Attempt to release unused object
    reset_error_data(&error_data);
    assert_true("Release unused object", !pool_release(pool, msg3)); // msg3 is still acquired
    assert_true("Unused object error", error_data.error_count > 0 && error_data.last_error == POOL_ERROR_INVALID_OBJECT);

    // Release re-acquired object
    assert_true("Release re-acquired object", pool_release(pool, msg3));
    assert_true("Used count after all releases", pool_used_count(pool) == 0);

    // Check pool statistics
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    assert_true("Acquire count", stats.acquire_count == 3);
    assert_true("Release count", stats.release_count == 3);
    assert_true("Max used", stats.max_used == 2);

    pool_destroy(pool);
    return 0;
}