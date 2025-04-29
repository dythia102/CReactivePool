#include "object_pool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Custom object: Message struct
typedef struct {
    char text[32];
    int id;
} Message;

static void* message_alloc(void) {
    Message* msg = malloc(sizeof(Message));
    if (msg) {
        msg->text[0] = '\0';
        msg->id = 0;
    }
    return msg;
}

static void message_free(void* obj) {
    free(obj);
}

int test_count = 0;
int test_passed = 0;

void assert_true(const char* test_name, bool condition) {
    test_count++;
    if (condition) {
        test_passed++;
        printf("PASS: %s\n", test_name);
    } else {
        printf("FAIL: %s\n", test_name);
    }
}

int main() {
    // Create allocator for Message
    object_pool_allocator_t allocator = {
        .alloc = message_alloc,
        .free = message_free,
        .user_data = NULL
    };

    // Test 1: Create and destroy pool
    object_pool_t* pool = pool_create(4, allocator);
    assert_true("Pool creation", pool != NULL);
    assert_true("Pool capacity", pool_capacity(pool) == 4);
    assert_true("Pool used count", pool_used_count(pool) == 0);
    pool_destroy(pool);

    // Test 2: Acquire and release objects
    pool = pool_create(4, allocator);
    Message* msg1 = pool_acquire(pool);
    assert_true("Acquire first object", msg1 != NULL);
    assert_true("Used count after acquire", pool_used_count(pool) == 1);
    strcpy(msg1->text, "Test");
    msg1->id = 1;
    assert_true("Object content", strcmp(msg1->text, "Test") == 0 && msg1->id == 1);

    Message* msg2 = pool_acquire(pool);
    assert_true("Acquire second object", msg2 != NULL);
    assert_true("Used count after two acquires", pool_used_count(pool) == 2);

    assert_true("Release first object", pool_release(pool, msg1));
    assert_true("Used count after release", pool_used_count(pool) == 1);

    assert_true("Release second object", pool_release(pool, msg2));
    assert_true("Used count after all releases", pool_used_count(pool) == 0);

    // Test 3: Pool exhaustion
    Message* messages[5];
    int acquired = 0;
    for (size_t i = 0; i < 5; i++) {
        messages[i] = pool_acquire(pool);
        if (messages[i]) {
            acquired++;
        }
    }
    assert_true("Acquire all objects", acquired == 4);
    assert_true("Pool exhaustion", messages[4] == NULL);

    // Release all objects
    for (size_t i = 0; i < 4; i++) {
        if (messages[i]) {
            pool_release(pool, messages[i]);
        }
    }
    assert_true("Used count after releasing all", pool_used_count(pool) == 0);

    // Test 4: Invalid operations
    assert_true("Release invalid object", !pool_release(pool, (void*)0xDEADBEEF));
    assert_true("Acquire from null pool", pool_acquire(NULL) == NULL);
    assert_true("Release from null pool", !pool_release(NULL, msg1));
    pool_destroy(NULL); // Simply call without expecting return value
    assert_true("Destroy null pool", true); // Test passes if no crash
    pool_destroy(pool);

    // Test 5: Default pool
    pool = pool_create_default();
    assert_true("Default pool creation", pool != NULL);
    assert_true("Default pool capacity", pool_capacity(pool) == DEFAULT_POOL_SIZE);
    void* obj = pool_acquire(pool);
    assert_true("Acquire from default pool", obj != NULL);
    assert_true("Release to default pool", pool_release(pool, obj));
    pool_destroy(pool);

    // Summary
    printf("\nTests run: %d, Passed: %d, Failed: %d\n", test_count, test_passed, test_count - test_passed);
    return test_count == test_passed ? 0 : 1;
}