#include "object_pool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <uv.h>

// Custom object: Message struct
typedef struct {
    uint32_t magic; // 0xDEADBEEF for validation
    char text[32];
    int id;
} Message;

static void* message_alloc(void) {
    Message* msg = malloc(sizeof(Message));
    if (msg) {
        msg->magic = 0xDEADBEEF;
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
    msg->magic = 0xDEADBEEF;
    msg->text[0] = '\0';
    msg->id = 0;
}

static bool message_validate(void* obj) {
    return obj && ((Message*)obj)->magic == 0xDEADBEEF;
}

static void message_on_create(void* obj) {
    (void)obj; // Suppress unused parameter warning
}

static void message_on_destroy(void* obj) {
    (void)obj; // Suppress unused parameter warning
}

static void message_on_reuse(void* obj) {
    (void)obj; // Suppress unused parameter warning
}

// Error callback test data
typedef struct {
    int error_count;
    object_pool_error_t last_error;
    char last_message[256];
} error_test_data_t;

static void error_callback(object_pool_error_t error, const char* message, void* context) {
    error_test_data_t* data = (error_test_data_t*)context;
    data->error_count++;
    data->last_error = error;
    strncpy(data->last_message, message, sizeof(data->last_message) - 1);
    data->last_message[sizeof(data->last_message) - 1] = '\0';
}

// Acquire callback test data
typedef struct {
    int callback_count;
    Message* last_object;
    int* context_id;
    Message** callback_objects; // Array to store callback-acquired objects
    size_t callback_objects_count; // Number of callback objects
    size_t callback_objects_capacity; // Capacity of callback objects array
} acquire_test_data_t;

static void acquire_callback(void* object, void* context) {
    acquire_test_data_t* data = (acquire_test_data_t*)context;
    if (!object || !context) return; // Guard against invalid inputs
    data->callback_count++;
    data->last_object = (Message*)object;
    if (data->context_id) {
        data->last_object->id = *(data->context_id);
    }
    // Store callback object for later release
    if (data->callback_objects_count < data->callback_objects_capacity) {
        data->callback_objects[data->callback_objects_count++] = (Message*)object;
    }
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

// Helper function to reset error data
static void reset_error_data(error_test_data_t* error_data) {
    error_data->error_count = 0;
    error_data->last_error = POOL_ERROR_NONE;
    error_data->last_message[0] = '\0';
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
        void* obj = pool_acquire(data->pool, NULL, NULL);
        if (obj) {
            data->success_count++;
            pool_release(data->pool, obj);
        }
    }
}

// Backpressure stress test data
typedef struct {
    object_pool_t* pool;
    int acquire_attempts;
    acquire_test_data_t acquire_data;
    int context_id;
} backpressure_test_data_t;

void backpressure_acquire_cb(void* arg) {
    backpressure_test_data_t* data = (backpressure_test_data_t*)arg;
    for (int i = 0; i < data->acquire_attempts; i++) {
        pool_acquire(data->pool, acquire_callback, &data->acquire_data);
    }
}

void backpressure_release_cb(void* arg) {
    backpressure_test_data_t* data = (backpressure_test_data_t*)arg;
    for (int i = 0; i < data->acquire_attempts; i++) {
        void* obj = pool_acquire(data->pool, NULL, NULL);
        if (obj) {
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
        .validate = message_validate,
        .on_create = message_on_create,
        .on_destroy = message_on_destroy,
        .on_reuse = message_on_reuse,
        .user_data = NULL
    };

    // Initialize error data
    error_test_data_t error_data;
    reset_error_data(&error_data);

    // Test 1: Create and destroy pool
    reset_error_data(&error_data);
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, &error_data);
    assert_true("Pool creation", pool != NULL);
    assert_true("Pool capacity", pool_capacity(pool) == 4);
    assert_true("Pool used count", pool_used_count(pool) == 0);
    pool_destroy(pool);

    // Test 2: Acquire and release objects
    reset_error_data(&error_data);
    pool = pool_create(4, 2, allocator, error_callback, &error_data);
    Message* msg1 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire first object", msg1 != NULL);
    assert_true("Used count after acquire", pool_used_count(pool) == 1);
    assert_true("First object reset", msg1->text[0] == '\0' && msg1->id == 0);
    strcpy(msg1->text, "Test");
    msg1->id = 1;
    assert_true("Object content", strcmp(msg1->text, "Test") == 0 && msg1->id == 1);

    Message* msg2 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire second object", msg2 != NULL);
    assert_true("Used count after two acquires", pool_used_count(pool) == 2);

    assert_true("Release first object", pool_release(pool, msg1));
    assert_true("Used count after release", pool_used_count(pool) == 1);

    assert_true("Release second object", pool_release(pool, msg2));
    assert_true("Used count after all releases", pool_used_count(pool) == 0);

    // Test 3: Pool exhaustion
    reset_error_data(&error_data);
    Message* messages[5];
    int acquired = 0;
    for (size_t i = 0; i < 5; i++) {
        messages[i] = pool_acquire(pool, NULL, NULL);
        if (messages[i]) {
            acquired++;
        }
    }
    assert_true("Acquire all objects", acquired == 4);
    assert_true("Pool exhaustion", messages[4] == NULL);
    assert_true("Exhaustion error", error_data.error_count > 0 && error_data.last_error == POOL_ERROR_EXHAUSTED);

    // Release all objects
    for (size_t i = 0; i < 4; i++) {
        if (messages[i]) {
            pool_release(pool, messages[i]);
        }
    }
    assert_true("Used count after releasing all", pool_used_count(pool) == 0);

    // Test 4: Invalid operations
    reset_error_data(&error_data);
    assert_true("Release invalid object", !pool_release(pool, (void*)0xDEADBEEF));
    assert_true("Invalid object error", error_data.error_count > 0 && error_data.last_error == POOL_ERROR_INVALID_OBJECT);
    assert_true("Acquire from null pool", pool_acquire(NULL, NULL, NULL) == NULL);
    assert_true("Release from null pool", !pool_release(NULL, NULL));
    pool_destroy(NULL);
    assert_true("Destroy null pool", true);

    // Test 5: Default pool
    reset_error_data(&error_data);
    pool = pool_create_default();
    assert_true("Default pool creation", pool != NULL);
    assert_true("Default pool capacity", pool_capacity(pool) == DEFAULT_POOL_SIZE);
    void* obj = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire from default pool", obj != NULL);
    assert_true("Release to default pool", pool_release(pool, obj));
    pool_destroy(pool);

    // Test 6: Thread safety
    reset_error_data(&error_data);
    pool = pool_create(4, 2, allocator, error_callback, &error_data);
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
    reset_error_data(&error_data);
    Message* msg3 = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire for reset test", msg3 != NULL);
    strcpy(msg3->text, "Temporary");
    msg3->id = 999;
    pool_release(pool, msg3);
    Message* msg4 = pool_acquire(pool, NULL, NULL);
    assert_true("Reset on reuse", msg4->text[0] == '\0' && msg4->id == 0);
    pool_release(pool, msg4);

    // Test 8: Dynamic resizing (grow)
    reset_error_data(&error_data);
    size_t old_capacity = pool_capacity(pool);
    assert_true("Grow pool", pool_grow(pool, 2));
    assert_true("New capacity after grow", pool_capacity(pool) == old_capacity + 2);
    Message* new_msg = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire after grow", new_msg != NULL);
    assert_true("New object reset", new_msg->text[0] == '\0' && new_msg->id == 0);
    pool_release(pool, new_msg);

    // Test 9: Dynamic resizing (shrink)
    reset_error_data(&error_data);
    assert_true("Shrink pool", pool_shrink(pool, 2));
    assert_true("New capacity after shrink", pool_capacity(pool) == old_capacity);
    new_msg = pool_acquire(pool, NULL, NULL);
    assert_true("Acquire after shrink", new_msg != NULL);
    pool_release(pool, new_msg);

    // Test 10: Object validation
    reset_error_data(&error_data);
    Message invalid_msg = { .magic = 0xBADBAD };
    assert_true("Release invalid object (bad magic)", !pool_release(pool, &invalid_msg));
    msg3 = pool_acquire(pool, NULL, NULL);
    msg3->magic = 0xBADBAD; // Corrupt object
    assert_true("Release corrupted object", !pool_release(pool, msg3));
    msg3->magic = 0xDEADBEEF; // Restore for cleanup
    pool_release(pool, msg3);

    // Test 11: Backpressure
    reset_error_data(&error_data);
    acquire_test_data_t acquire_data = {0};
    int callback_id = 5;
    acquire_data.context_id = &callback_id;
    // Explicitly exhaust the pool
    Message* held_objects[4];
    for (size_t i = 0; i < 4; i++) {
        held_objects[i] = pool_acquire(pool, NULL, NULL);
        assert_true("Exhaust pool for backpressure", held_objects[i] != NULL);
    }
    // Enqueue backpressure requests
    for (size_t i = 0; i < 2; i++) {
        pool_acquire(pool, acquire_callback, &acquire_data);
    }
    assert_true("Backpressure queue", acquire_data.callback_count == 0);
    // Release one object to trigger callback
    if (held_objects[0]) {
        pool_release(pool, held_objects[0]);
        held_objects[0] = NULL;
    }
    assert_true("Backpressure callback", acquire_data.callback_count == 1 && acquire_data.last_object != NULL);
    assert_true("Backpressure object", acquire_data.last_object->id == callback_id);
    // Release callback-acquired object
    if (acquire_data.last_object) {
        pool_release(pool, acquire_data.last_object);
        acquire_data.last_object = NULL; // Prevent double-free
    }
    // Release remaining held objects
    for (size_t i = 1; i < 4; i++) {
        if (held_objects[i]) {
            pool_release(pool, held_objects[i]);
        }
    }

    // Test 12: Concurrent backpressure queue
    reset_error_data(&error_data);
    pool = pool_create(2, 2, allocator, error_callback, &error_data);
    // Hold all objects to force backpressure
    Message* backpressure_objects[2];
    for (size_t i = 0; i < 2; i++) {
        backpressure_objects[i] = pool_acquire(pool, NULL, NULL);
        assert_true("Exhaust pool for concurrent backpressure", backpressure_objects[i] != NULL);
    }
    // Spawn threads to enqueue and release
    uv_thread_t backpressure_threads[6];
    backpressure_test_data_t backpressure_data[6];
    for (int i = 0; i < 4; i++) {
        backpressure_data[i].pool = pool;
        backpressure_data[i].acquire_attempts = 5;
        backpressure_data[i].acquire_data.callback_count = 0;
        backpressure_data[i].acquire_data.last_object = NULL;
        backpressure_data[i].acquire_data.context_id = &backpressure_data[i].context_id;
        backpressure_data[i].acquire_data.callback_objects = malloc(5 * sizeof(Message*)); // Capacity for 5 objects
        backpressure_data[i].acquire_data.callback_objects_count = 0;
        backpressure_data[i].acquire_data.callback_objects_capacity = 5;
        backpressure_data[i].context_id = i + 1;
        uv_thread_create(&backpressure_threads[i], backpressure_acquire_cb, &backpressure_data[i]);
    }
    for (int i = 4; i < 6; i++) {
        backpressure_data[i].pool = pool;
        backpressure_data[i].acquire_attempts = 5;
        backpressure_data[i].acquire_data.callback_count = 0;
        backpressure_data[i].acquire_data.last_object = NULL;
        backpressure_data[i].acquire_data.context_id = NULL;
        backpressure_data[i].acquire_data.callback_objects = malloc(5 * sizeof(Message*)); // Capacity for 5 objects
        backpressure_data[i].acquire_data.callback_objects_count = 0;
        backpressure_data[i].acquire_data.callback_objects_capacity = 5;
        backpressure_data[i].context_id = 0;
        uv_thread_create(&backpressure_threads[i], backpressure_release_cb, &backpressure_data[i]);
    }
    for (int i = 0; i < 6; i++) {
        uv_thread_join(&backpressure_threads[i]);
    }
    // Release held objects
    for (size_t i = 0; i < 2; i++) {
        if (backpressure_objects[i]) {
            pool_release(pool, backpressure_objects[i]);
        }
    }
    // Release callback-acquired objects
    for (int i = 0; i < 6; i++) {
        for (size_t j = 0; j < backpressure_data[i].acquire_data.callback_objects_count; j++) {
            if (backpressure_data[i].acquire_data.callback_objects[j]) {
                pool_release(pool, backpressure_data[i].acquire_data.callback_objects[j]);
            }
        }
        free(backpressure_data[i].acquire_data.callback_objects);
    }
    int total_callbacks = 0;
    for (int i = 0; i < 4; i++) {
        total_callbacks += backpressure_data[i].acquire_data.callback_count;
    }
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    assert_true("Concurrent backpressure callbacks", total_callbacks >= 0 && pool_used_count(pool) == 0);
    assert_true("Concurrent backpressure queue size", stats.queue_max_size >= 1);
    pool_destroy(pool);

    // Test 13: Pool statistics
    reset_error_data(&error_data);
    pool = pool_create(4, 2, allocator, error_callback, &error_data);
    pool_stats(pool, &stats);
    assert_true("Stats max_used", stats.max_used == 0);
    assert_true("Stats acquire_count", stats.acquire_count == 0);
    assert_true("Stats release_count", stats.release_count == 0);
    assert_true("Stats contention_attempts", stats.contention_attempts > 0);
    assert_true("Stats total_objects_allocated", stats.total_objects_allocated >= 4);
    assert_true("Stats grow_count", stats.grow_count == 0);
    assert_true("Stats shrink_count", stats.shrink_count == 0);
    assert_true("Stats queue_max_size", stats.queue_max_size == 0);
    pool_destroy(pool);

    // Summary
    printf("\nTests run: %d, Passed: %d, Failed: %d\n", test_count, test_passed, test_count - test_passed);
    return test_count == test_passed ? 0 : 1;
}