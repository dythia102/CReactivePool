#include "object_pool.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Custom object: Message struct
typedef struct {
    char text[32];
    int id;
} Message;

// Allocator for Message
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

int main() {
    // Create allocator for Message
    object_pool_allocator_t allocator = {
        .alloc = message_alloc,
        .free = message_free,
        .reset = message_reset,
        .user_data = NULL
    };

    // Create a pool with 4 Message objects
    object_pool_t* pool = pool_create(4, allocator);
    if (!pool) {
        fprintf(stderr, "Failed to create pool\n");
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

    // Acquire again to demonstrate reuse and reset
    Message* msg3 = pool_acquire(pool);
    if (msg3) {
        printf("Acquired msg3: text=%s, id=%d (should be empty)\n", msg3->text, msg3->id);
        strcpy(msg3->text, "Reused");
        msg3->id = 3;
        printf("Modified msg3: text=%s, id=%d\n", msg3->text, msg3->id);
    }

    // Try to acquire when pool is exhausted
    for (int i = 0; i < 5; i++) {
        Message* msg = pool_acquire(pool);
        if (!msg) {
            printf("Failed to acquire: pool exhausted\n");
            break;
        }
    }

    // Clean up
    pool_destroy(pool);
    printf("Pool destroyed\n");
    return 0;
}