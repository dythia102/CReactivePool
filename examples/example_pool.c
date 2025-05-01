#include "object_pool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h> // For PRIu64

// Custom object: Message struct
typedef struct {
    uint32_t magic; // 0xDEADBEEF for validation
    char text[32];
    int id;
} Message;

// Allocator for Message
static void* message_alloc(void* user_data) {
    (void)user_data; // Unused
    // Allocate space for metadata + Message
    void* block = malloc(sizeof(pool_object_metadata_t) + sizeof(Message));
    if (!block) {
        return NULL;
    }
    // Return pointer to Message (after metadata)
    void* msg = (char*)block + sizeof(pool_object_metadata_t);
    Message* message = (Message*)msg;
    message->magic = 0xDEADBEEF;
    message->text[0] = '\0';
    message->id = 0;
    return msg;
}

static void message_free(void* obj, void* user_data) {
    (void)user_data; // Unused
    if (obj) {
        // Free the entire block (metadata + Message)
        free((char*)obj - sizeof(pool_object_metadata_t));
    }
}

static void message_reset(void* obj, void* user_data) {
    (void)user_data; // Unused
    Message* msg = (Message*)obj;
    if (msg) {
        msg->magic = 0xDEADBEEF;
        msg->text[0] = '\0';
        msg->id = 0;
    }
}

static bool message_validate(void* obj, void* user_data) {
    (void)user_data; // Unused
    return obj && ((Message*)obj)->magic == 0xDEADBEEF;
}

static void message_on_create(void* obj, void* user_data) {
    (void)user_data;
    printf("Created message: %p\n", obj);
}

static void message_on_destroy(void* obj, void* user_data) {
    (void)user_data;
    printf("Destroyed message: %p\n", obj);
}

static void message_on_reuse(void* obj, void* user_data) {
    (void)user_data;
    printf("Reusing message: %p\n", obj);
}

// Error callback
static void error_callback(object_pool_error_t error, const char* message, void* context) {
    printf("Error [%d]: %s (context: %p)\n", error, message, context);
}

// Acquire callback for backpressure
static void acquire_callback(void* object, void* context) {
    Message* msg = (Message*)object;
    printf("Acquired via callback: %p (context: %p)\n", object, context);
    if (msg && context) {
        strcpy(msg->text, "Backpressure");
        msg->id = *(int*)context;
        printf("Modified via callback: text=%s, id=%d\n", msg->text, msg->id);
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

    // Create a pool with 4 Message objects, 2 sub-pools, and error callback
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, NULL);
    if (!pool) {
        printf("Failed to create pool\n");
        return 1;
    }

    printf("Pool created: %zu objects, %zu used\n", pool_capacity(pool), pool_used_count(pool));

    // Acquire and use objects
    Message* msg1 = pool_acquire(pool, NULL, NULL);
    if (msg1) {
        strcpy(msg1->text, "Hello");
        msg1->id = 1;
        printf("Acquired msg1: text=%s, id=%d\n", msg1->text, msg1->id);
    }

    Message* msg2 = pool_acquire(pool, NULL, NULL);
    if (msg2) {
        strcpy(msg2->text, "World");
        msg2->id = 2;
        printf("Acquired msg2: text=%s, id=%d\n", msg2->text, msg2->id);
    }

    printf("Pool status: %zu objects, %zu used\n", pool_capacity(pool), pool_used_count(pool));

    // Release objects
    if (pool_release(pool, msg1)) {
        printf("Released msg1\n");
    }
    if (pool_release(pool, msg2)) {
        printf("Released msg2\n");
    }

    printf("Pool status: %zu objects, %zu used\n", pool_capacity(pool), pool_used_count(pool));

    // Grow the pool
    if (pool_grow(pool, 2)) {
        printf("Grew pool by 2: new capacity %zu\n", pool_capacity(pool));
    }

    // Shrink the pool
    if (pool_shrink(pool, 2)) {
        printf("Shrunk pool by 2: new capacity %zu\n", pool_capacity(pool));
    }

    // Test backpressure
    int callback_id = 3;
    Message* held_objects[6] = { NULL };
    size_t acquired_count = 0;
    for (size_t i = 0; i < 6; i++) {
        held_objects[i] = pool_acquire(pool, acquire_callback, &callback_id);
        if (held_objects[i]) {
            acquired_count++;
        }
    }

    // Release acquired objects to trigger backpressure callbacks
    for (size_t i = 0; i < acquired_count; i++) {
        if (held_objects[i]) {
            pool_release(pool, held_objects[i]);
            held_objects[i] = NULL;
        }
    }

    // Acquire and release one more object
    Message* msg3 = pool_acquire(pool, NULL, NULL);
    if (msg3) {
        printf("Acquired msg3: text=%s, id=%d\n", msg3->text, msg3->id);
        pool_release(pool, msg3);
    }

    // Check statistics
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    printf("Stats: max_used=%zu, acquires=%zu, releases=%zu, contention_attempts=%zu, "
           "contention_time_ns=%" PRIu64 ", total_objects=%zu, grows=%zu, shrinks=%zu, "
           "queue_max=%zu, queue_grows=%zu\n",
           stats.max_used, stats.acquire_count, stats.release_count,
           stats.contention_attempts, stats.total_contention_time_ns,
           stats.total_objects_allocated, stats.grow_count, stats.shrink_count,
           stats.queue_max_size, stats.queue_grow_count);

    // Clean up
    pool_destroy(pool);
    printf("Pool destroyed\n");
    return 0;
}