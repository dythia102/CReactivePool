#ifndef COMMON_H
#define COMMON_H

#include "object_pool.h"
#include <stdbool.h>
#include <stdint.h>

// Define Message struct
typedef struct {
    uint32_t magic; // 0xDEADBEEF for validation
    char text[32];
    int id;
} Message;

// Declare allocator functions
void* message_alloc(void* user_data);
void message_free(void* obj, void* user_data);
void message_reset(void* obj, void* user_data);
bool message_validate(void* obj, void* user_data);
void message_on_create(void* obj, void* user_data);
void message_on_destroy(void* obj, void* user_data);
void message_on_reuse(void* obj, void* user_data);

// Error callback test data
typedef struct {
    int error_count;
    object_pool_error_t last_error;
    char last_message[256];
} error_test_data_t;

// Declare error_callback
void error_callback(object_pool_error_t error, const char* message, void* context);

// Acquire callback test data
typedef struct {
    int callback_count;
    Message* last_object;
    int* context_id;
    Message** callback_objects; // Array to store callback-acquired objects
    size_t callback_objects_count; // Number of callback objects
    size_t callback_objects_capacity; // Capacity of callback objects array
} acquire_test_data_t;

// Declare acquire_callback
void acquire_callback(void* object, void* context);

// Function to reset error data
void reset_error_data(error_test_data_t* error_data);

// Global allocator
extern object_pool_allocator_t allocator;

#endif // COMMON_H