#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>

// Assuming this is part of your original code based on pthread usage
typedef struct {
    object_pool_t* pool;
    int iterations;
} thread_data_t;

void* thread_func(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    for (int i = 0; i < data->iterations; i++) {
        Message* msg = pool_acquire(data->pool, NULL, NULL);
        if (msg) {
            pool_release(data->pool, msg);
        }
    }
    return NULL;
}

void test_load_balancing() {
    error_test_data_t error_data;
    reset_error_data(&error_data);
    object_pool_t* pool = pool_create(4, 2, allocator, error_callback, &error_data);
    assert_true("Pool creation", pool != NULL);

    const int num_threads = 4;
    const int iterations = 100;
    pthread_t threads[num_threads];
    thread_data_t thread_data[num_threads];

    for (int i = 0; i < num_threads; i++) {
        thread_data[i].pool = pool;
        thread_data[i].iterations = iterations;
        if (pthread_create(&threads[i], NULL, thread_func, &thread_data[i]) != 0) {
            fprintf(stderr, "Thread creation failed\n");
            exit(1);
        }
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    assert_true("Used count after threads", pool_used_count(pool) == 0);
    // Add more assertions as needed based on your test logic
    pool_destroy(pool);
}

int main() {
    test_load_balancing();
    return 0;
}