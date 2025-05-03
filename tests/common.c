#include "common.h"
#include <stdlib.h>
#include <string.h>

// Implement allocator functions
void* message_alloc(void* user_data) {
    (void)user_data; // Unused
    void* block = malloc(sizeof(pool_object_metadata_t) + sizeof(Message));
    if (!block) {
        return NULL;
    }
    void* msg = (char*)block + sizeof(pool_object_metadata_t);
    Message* message = (Message*)msg;
    message->magic = 0xDEADBEEF;
    message->text[0] = '\0';
    message->id = 0;
    return msg;
}

void message_free(void* obj, void* user_data) {
    (void)user_data; // Unused
    if (obj) {
        free((char*)obj - sizeof(pool_object_metadata_t));
    }
}

void message_reset(void* obj, void* user_data) {
    (void)user_data; // Unused
    Message* msg = (Message*)obj;
    if (msg) {
        msg->magic = 0xDEADBEEF;
        msg->text[0] = '\0';
        msg->id = 0;
    }
}

bool message_validate(void* obj, void* user_data) {
    (void)user_data; // Unused
    return obj && ((Message*)obj)->magic == 0xDEADBEEF;
}

void message_on_create(void* obj, void* user_data) {
    (void)obj;
    (void)user_data;
}

void message_on_destroy(void* obj, void* user_data) {
    (void)obj;
    (void)user_data;
}

void message_on_reuse(void* obj, void* user_data) {
    (void)obj;
    (void)user_data;
}

// Implement error_callback
void error_callback(object_pool_error_t error, const char* message, void* context) {
    error_test_data_t* data = (error_test_data_t*)context;
    data->error_count++;
    if (error == POOL_ERROR_EXHAUSTED) {
        data->exhaustion_count++;
    }
    data->last_error = error;
    strncpy(data->last_message, message, sizeof(data->last_message) - 1);
    data->last_message[sizeof(data->last_message) - 1] = '\0';
}

// Implement acquire_callback
void acquire_callback(void* object, void* context) {
    acquire_test_data_t* data = (acquire_test_data_t*)context;
    if (!object || !context) return;
    data->callback_count++;
    data->last_object = (Message*)object;
    if (data->context_id) {
        data->last_object->id = *(data->context_id);
    }
    if (data->callback_objects_count < data->callback_objects_capacity) {
        data->callback_objects[data->callback_objects_count++] = (Message*)object;
    }
}

// Implement reset_error_data
void reset_error_data(error_test_data_t* error_data) {
    error_data->error_count = 0;
    error_data->exhaustion_count = 0;
    error_data->last_error = POOL_ERROR_NONE;
    error_data->last_message[0] = '\0';
}

// Define global allocator
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