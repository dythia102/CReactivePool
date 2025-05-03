#include "common.h"
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

// Configuration
#define NUM_THREADS 5
#define POOL_SIZE 2

// Per-thread data
typedef struct {
    Message* callback_object;
    pthread_mutex_t* mutex;
    pthread_cond_t* cond;
    bool object_received;
    object_pool_t* pool;
    int* shared_callback_count;
    pthread_mutex_t* shared_mutex;
    pthread_cond_t* shared_cond;
    int thread_index;
    pthread_barrier_t* start_barrier;
    int* acquired_count;
    acquire_test_data_t* acquire_data;  // Added to per_thread_data_t
} per_thread_data_t;

// Shared data
typedef struct {
    object_pool_t* pool;
    acquire_test_data_t* acquire_data;
    pthread_mutex_t* mutex;
    pthread_cond_t* cond;
    int* acquired_count;
    int* callback_count;
    pthread_barrier_t* start_barrier;
    per_thread_data_t* thread_data_array;
} thread_data_t;

// Callback function
void concurrent_acquire_callback(void* object, void* context) {
    per_thread_data_t* data = (per_thread_data_t*)context;
    if (!object || !context) return;

    pthread_mutex_lock(data->mutex);
    data->callback_object = (Message*)object;
    data->object_received = true;
    pthread_mutex_unlock(data->mutex);
    pthread_cond_signal(data->cond);

    // Update shared callback count
    pthread_mutex_lock(data->shared_mutex);
    (*data->shared_callback_count)++;
    printf("DEBUG: Callback invoked by thread %d, count=%d, object=%p\n", 
           data->thread_index, *data->shared_callback_count, object);
    if (*data->shared_callback_count == (NUM_THREADS - POOL_SIZE)) {
        pthread_cond_signal(data->shared_cond);
    }
    pthread_mutex_unlock(data->shared_mutex);

    // Update acquire_data
    data->acquire_data->last_object = (Message*)object;
    data->acquire_data->object_received = true;  // Set flag to true
}

// Thread function
void* acquire_thread(void* arg) {
    per_thread_data_t* ptd = (per_thread_data_t*)arg;

    pthread_barrier_wait(ptd->start_barrier);

    printf("DEBUG: Thread %d attempting to acquire\n", ptd->thread_index);
    Message* obj = pool_acquire(ptd->pool, concurrent_acquire_callback, ptd);
    if (obj) {
        pthread_mutex_lock(ptd->shared_mutex);
        (*ptd->acquired_count)++;
        printf("DEBUG: Thread %d acquired object %p directly\n", ptd->thread_index, obj);
        pthread_mutex_unlock(ptd->shared_mutex);
        sleep(1); // Simulate work
        printf("DEBUG: Thread %d releasing object %p\n", ptd->thread_index, obj);
        pool_release(ptd->pool, obj);
    } else {
        printf("DEBUG: Thread %d enqueued for callback\n", ptd->thread_index);
        pthread_mutex_lock(ptd->mutex);
        while (!ptd->object_received) {
            pthread_cond_wait(ptd->cond, ptd->mutex);
        }
        obj = ptd->callback_object;
        printf("DEBUG: Thread %d received object %p via callback\n", ptd->thread_index, obj);
        pthread_mutex_unlock(ptd->mutex);
        sleep(1); // Simulate work
        printf("DEBUG: Thread %d releasing object %p\n", ptd->thread_index, obj);
        pool_release(ptd->pool, obj);
    }

    return NULL;
}

// Assert function
void assert_true(const char* test_name, bool condition) {
    if (condition) {
        printf("PASS: %s\n", test_name);
    } else {
        printf("FAIL: %s\n", test_name);
    }
}

int main() {
    error_test_data_t error_data;
    acquire_test_data_t acquire_data = {0};
    int callback_id = 100;
    acquire_data.context_id = &callback_id;
    acquire_data.object_received = false;  // Initialize flag

    // Setup pool
    reset_error_data(&error_data);
    object_pool_t* pool = pool_create(POOL_SIZE, 1, allocator, error_callback, &error_data);
    assert_true("Pool creation", pool != NULL);
    assert_true("Initial capacity", pool_capacity(pool) == POOL_SIZE);

    // Shared synchronization
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    pthread_barrier_t start_barrier;
    pthread_barrier_init(&start_barrier, NULL, NUM_THREADS + 1);
    int acquired_count = 0;
    int callback_count = 0;

    // Per-thread data array
    per_thread_data_t thread_data_array[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data_array[i].mutex = malloc(sizeof(pthread_mutex_t));
        thread_data_array[i].cond = malloc(sizeof(pthread_cond_t));
        pthread_mutex_init(thread_data_array[i].mutex, NULL);
        pthread_cond_init(thread_data_array[i].cond, NULL);
        thread_data_array[i].callback_object = NULL;
        thread_data_array[i].object_received = false;
        thread_data_array[i].pool = pool;
        thread_data_array[i].shared_callback_count = &callback_count;
        thread_data_array[i].shared_mutex = &mutex;
        thread_data_array[i].shared_cond = &cond;
        thread_data_array[i].thread_index = i;
        thread_data_array[i].start_barrier = &start_barrier;
        thread_data_array[i].acquired_count = &acquired_count;
        thread_data_array[i].acquire_data = &acquire_data;  // Initialize pointer
    }

    // Create threads
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, acquire_thread, &thread_data_array[i]);
    }

    pthread_barrier_wait(&start_barrier);

    // Wait for all callbacks to be invoked
    pthread_mutex_lock(&mutex);
    while (callback_count < (NUM_THREADS - POOL_SIZE)) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);

    // Join threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Verify results
    pthread_mutex_lock(&mutex);
    assert_true("Direct acquisitions", acquired_count == POOL_SIZE);
    assert_true("Callbacks invoked", callback_count == (NUM_THREADS - POOL_SIZE));
    assert_true("Callback objects received", acquire_data.object_received);  // Updated assertion
    printf("DEBUG: Final acquired_count=%d, callback_count=%d\n", acquired_count, callback_count);
    pthread_mutex_unlock(&mutex);

    // Cleanup
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_mutex_destroy(thread_data_array[i].mutex);
        pthread_cond_destroy(thread_data_array[i].cond);
        free(thread_data_array[i].mutex);
        free(thread_data_array[i].cond);
    }
    pool_destroy(pool);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    pthread_barrier_destroy(&start_barrier);

    return 0;
}