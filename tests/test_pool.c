#include "object_pool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <uv.h>

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

static void message_reset(void* obj) {
    Message* msg = (Message*)obj;
    msg->text[0] = '\0';
    msg->id = 0;
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

// Thread test data
typedef struct {
    object_pool_t* pool;
    int acquire_count;
    int success_count;
} thread_test_data_t;

void thread_test_cb(void* arg) {
    thread_test_data_t* data = (thread_test_data_t*)arg;
    for (int i = 0; i < data->acquire_count; i++) {
        void* obj = pool_acquire(data->pool);
        if (obj) {
            data->success_count++;
            pool_release(data->pool, obj);
        }
    }
}

int main() {
    // Create allocator for Message
    object_pool_allocator_t allocator = {
        .alloc = message_alloc,
        .free = message_free,
        .reset = message_reset,
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
    assert_true("First object reset", msg1->text[0] == '\0' && msg1->id == 0);
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
    pool_destroy(NULL);
    assert_true("Destroy null pool", true);

    // Test 5: Default pool
    pool = pool_create_default();
    assert_true("Default pool creation", pool != NULL);
    assert_true("Default pool capacity", pool_capacity(pool) == DEFAULT_POOL_SIZE);
    void* obj = pool_acquire(pool);
    assert_true("Acquire from default pool", obj != NULL);
    assert_true("Release to default pool", pool_release(pool, obj));
    pool_destroy(pool);

    // Test 6: Thread safety
    pool = pool_create(4, allocator);
    uv_thread_t threads[4];
    thread_test_data_t thread_data[4];
    for (int i = 0; i < 4; i++) {
        thread_data[i].pool = pool;
        thread_data[i].acquire_count = 10;
        thread_data[i].success_count = 0;
        uv_thread_create(&threads[i], thread_test_cb, &thread_data[i]);
    }
    for (int i = 0; i < 4; i++) {
        uv_thread_join(&threads[i]);
    }
    int total_success = 0;
    for (int i = 0; i < 4; i++) {
        total_success += thread_data[i].success_count;
    }
    assert_true("Thread-safe acquire/release", total_success <= 40 && pool_used_count(pool) == 0);

    // Test 7: Reset on reuse
    Message* msg3 = pool_acquire(pool);
    assert_true("Acquire for reset test", msg3 != NULL);
    strcpy(msg3->text, "Temporary");
    msg3->id = 999;
    pool_release(pool, msg3);
    Message* msg4 = pool_acquire(pool);
    assert_true("Reset on reuse", msg4->text[0] == '\0' && msg4->id == 0);
    pool_release(pool, msg4);
    pool_destroy(pool);

    // Summary
    printf("\nTests run: %d, Passed: %d, Failed: %d\n", test_count, test_passed, test_count - test_passed);
    return test_count == test_passed ? 0 : 1;
}