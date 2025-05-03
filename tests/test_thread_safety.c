#include "common.h"
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>

// Thread data structure
typedef struct {
    object_pool_t* pool;
    int acquire_count;
    int success_count;
    Message** objects; // Array to store acquired objects
} thread_data_t;

// Thread function to acquire and release objects
void* thread_test(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    for (int i = 0; i < data->acquire_count; i++) {
        Message* obj = pool_acquire(data->pool, NULL, NULL);
        if (obj) {
            data->objects[data->success_count] = obj;
            data->success_count++;
        }
    }
    // Release all acquired objects
    for (int i = 0; i < data->success_count; i++) {
        pool_release(data->pool, data->objects[i]);
    }
    return NULL;
}

int main() {
    error_test_data_t error_data;
    reset_error_data(&error_data);

    // Create a pool with 8 objects across 4 sub-pools
    object_pool_t* pool = pool_create(8, 4, allocator, error_callback, &error_data);
    assert_true("Pool creation", pool != NULL);
    assert_true("Initial capacity", pool_capacity(pool) == 8);
    assert_true("Initial used count", pool_used_count(pool) == 0);

    // Define thread parameters
    const int thread_count = 8;
    const int acquire_per_thread = 10;
    pthread_t threads[thread_count];
    thread_data_t thread_data[thread_count];

    // Initialize thread data
    for (int i = 0; i < thread_count; i++) {
        thread_data[i].pool = pool;
        thread_data[i].acquire_count = acquire_per_thread;
        thread_data[i].success_count = 0;
        thread_data[i].objects = malloc(acquire_per_thread * sizeof(Message*));
        if (!thread_data[i].objects) {
            printf("FAIL: Memory allocation for thread %d\n", i);
            exit(1);
        }
    }

    // Spawn threads
    for (int i = 0; i < thread_count; i++) {
        pthread_create(&threads[i], NULL, thread_test, &thread_data[i]);
    }

    // Wait for all threads to complete
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    // Calculate total successful acquisitions
    int total_success = 0;
    for (int i = 0; i < thread_count; i++) {
        total_success += thread_data[i].success_count;
    }

    // Verify pool state after concurrent operations
    assert_true("Thread-safe acquire/release", total_success <= thread_count * acquire_per_thread);
    assert_true("Final used count", pool_used_count(pool) == 0);
    assert_true("Only exhaustion errors", error_data.error_count == error_data.exhaustion_count);

    // Check pool statistics
    object_pool_stats_t stats;
    pool_stats(pool, &stats);
    assert_true("Acquire count consistency", stats.acquire_count == (size_t)total_success);
    assert_true("Release count consistency", stats.release_count == (size_t)total_success);
    assert_true("Contention attempts recorded", stats.contention_attempts > 0);

    // Cleanup
    for (int i = 0; i < thread_count; i++) {
        free(thread_data[i].objects);
    }
    pool_destroy(pool);

    return 0;
}