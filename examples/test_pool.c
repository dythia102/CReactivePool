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
    msg->magic = 0xDEADBEEF; // Ensure valid after reset
    msg->text[0] = '\0';
    msg->id = 0;
}

static bool message_validate(void* obj) {
    return obj && ((Message*)obj)->magic == 0xDEADBEEF;
}

// Error callback
static void error_callback(object_pool_error_t error, const char* message, void* context) {
    printf("Error [%d]: %s (context: %p)\n", error, message, context);
}

int main() {
    // Create allocator for Message
    object_pool_allocator_t allocator = {
        .alloc = message_alloc,
        .free = message_free,
        .reset = message_reset,
        .validate = message_validate,
        .user_data = NULL
    };

    // Create a pool with 4 Message objects and error callback
    object_pool_t* pool = pool_create(4, allocator, error_callback, NULL);
    if (!pool) {
        printf("Failed to create pool\n");
        return 1;
    }

    printf("Pool created: %zu objects, %zu used\n", pool_capacity(pool), pool_used_count(pool));

    // Acquire and use objects
    Message* msg1 = pool_acquire(pool);
    if (msg1) {
        strcpy(msg1->text, "Hello");
        msg1->id = 1;
        printf("Acquired msg1: text=%s, id=%d\n", msg1->text, msg1->id);
    }

    Message* msg2 = pool_acquire(pool);
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

    // Acquire again to demonstrate reuse and reset
    Message* msg3 = pool_acquire(pool);
    if (msg3) {
        printf("Acquired msg3: text=%s, id=%d (should be empty)\n", msg3->text, msg3->id);
        strcpy(msg3->text, "Reused");
        msg3->id = 3;
        printf("Modified msg3: text=%s, id=%d\n", msg3->text, msg3->id);
    }

    // Check statistics
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    printf("Stats: max_used=%zu, acquires=%zu, releases=%zu, contention_attempts=%zu, "
           "contention_time_ns=%" PRIu64 ", total_objects=%zu, grows=%zu, shrinks=%zu\n",
           stats.max_used, stats.acquire_count, stats.release_count,
           stats.contention_attempts, stats.total_contention_time_ns,
           stats.total_objects_allocated, stats.grow_count, stats.shrink_count);

    // Try to acquire when pool is exhausted
    Message* messages[7];
    size_t acquired = 0;
    for (size_t i = 0; i < 7; i++) {
        messages[i] = pool_acquire(pool);
        if (messages[i]) {
            acquired++;
        } else {
            break;
        }
    }

    // Release all
    for (size_t i = 0; i < acquired; i++) {
        pool_release(pool, messages[i]);
    }

    // Clean up
    pool_destroy(pool);
    printf("Pool destroyed\n");
    return 0;
}