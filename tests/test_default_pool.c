#include "object_pool.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

void assert_true(const char* test_name, bool condition) {
    if (condition) {
        printf("PASS: %s\n", test_name);
    } else {
        printf("FAIL: %s\n", test_name);
    }
}

void test_default_pool_with_size(size_t object_size, size_t expected_size) {
    object_pool_t* pool = pool_create_default_with_size(object_size);
    assert_true("Pool creation", pool != NULL);
    assert_true("Pool capacity", pool_capacity(pool) == DEFAULT_POOL_SIZE);
    assert_true("Initial used count", pool_used_count(pool) == 0);

    // Acquire an object
    void* obj = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire object", obj != NULL);
    assert_true("Used count after acquire", pool_used_count(pool) == 1);

    // Check that the first expected_size bytes are zero
    bool is_zero = true;
    char* ptr = (char*)obj;
    for (size_t i = 0; i < expected_size; i++) {
        if (ptr[i] != 0) {
            is_zero = false;
            break;
        }
    }
    assert_true("Object initialized to zero", is_zero);

    // Modify the object
    memset(obj, 1, expected_size);

    // Release the object
    assert_true("Release object", pool_release(pool, obj));
    assert_true("Used count after release", pool_used_count(pool) == 0);

    // Acquire another object
    void* obj2 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire another object", obj2 != NULL);
    assert_true("Used count after acquire", pool_used_count(pool) == 1);

    // Check that it's zero again
    is_zero = true;
    ptr = (char*)obj2;
    for (size_t i = 0; i < expected_size; i++) {
        if (ptr[i] != 0) {
            is_zero = false;
            break;
        }
    }
    assert_true("Object reset on reuse", is_zero);

    // Release it
    pool_release(pool, obj2);

    // Acquire all objects
    void* objects[DEFAULT_POOL_SIZE];
    for (size_t i = 0; i < DEFAULT_POOL_SIZE; i++) {
        objects[i] = pool_acquire(pool, NULL, NULL);
        assert_true("Acquire object", objects[i] != NULL);
    }
    assert_true("Used count after acquiring all", pool_used_count(pool) == DEFAULT_POOL_SIZE);

    // Try to acquire one more
    void* extra_obj = pool_acquire(pool, NULL, NULL);
    assert_true("Pool exhausted", extra_obj == NULL);

    // Release all objects
    for (size_t i = 0; i < DEFAULT_POOL_SIZE; i++) {
        pool_release(pool, objects[i]);
    }
    assert_true("Used count after releasing all", pool_used_count(pool) == 0);

    // Acquire one more time
    void* obj3 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire after release", obj3 != NULL);
    pool_release(pool, obj3);

    pool_destroy(pool);
}

int main() {
    // Test with default object size (0 -> 64)
    test_default_pool_with_size(0, DEFAULT_OBJECT_SIZE);

    // Test with specified object size, e.g., 128
    test_default_pool_with_size(128, 128);

    // Test with another size, e.g., 32
    test_default_pool_with_size(32, 32);

    return 0;
}