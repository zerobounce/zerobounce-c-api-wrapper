#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBErrorResponse.h"

ZBErrorResponse new_zb_error_response() {
    ZBErrorResponse response;

    response.success = false;
    response.errors = string_vector_init();

    return response;
}

char* zb_error_response_to_string(ZBErrorResponse* response) {
    const char* serialization = "ZBErrorResponse{success=%d, errors=[%s]}";
    char* errors = concatenate_strings(&response->errors, ", ");
    int size = snprintf(NULL, 0, serialization, response->success, errors);

    if (size < 0) {
        return NULL;
    }

    char* buffer = (char*) malloc(size + 1);
    if (!buffer) {
        return NULL;
    }

    snprintf(buffer, size + 1, serialization, response->success, errors);
    free(errors);
    return buffer;
}

ZBErrorResponse parse_error(const char* error) {
    ZBErrorResponse response = new_zb_error_response();

    if (!error || strlen(error) == 0) {
        return response;
    }

    StringVector errors = string_vector_init();
    StringVector other_messages = string_vector_init();

    json_object* hash_map = json_tokener_parse(error);

    if (hash_map != NULL && json_object_is_type(hash_map, json_type_object)) {
        json_object_object_foreach(hash_map, key, val) {
            if (strstr(key, "error") != NULL || strstr(key, "message") != NULL) {
                if (json_object_is_type(val, json_type_array)) {
                    size_t array_length = json_object_array_length(val);
                    for (size_t i = 0; i < array_length; i++) {
                        json_object* item = json_object_array_get_idx(val, i);
                        if (json_object_is_type(item, json_type_string)) {
                            string_vector_append(&errors, json_object_get_string(item));
                        }
                    }
                } else if (!json_object_is_type(val, json_type_null)) {
                    string_vector_append(&errors, json_object_get_string(val));
                }
            } else {
                if (strcmp(key, "success") == 0 && json_object_is_type(val, json_type_boolean)) {
                    response.success = json_object_get_boolean(val);
                } else if (json_object_is_type(val, json_type_array)) {
                    size_t array_length = json_object_array_length(val);
                    for (size_t i = 0; i < array_length; i++) {
                        json_object* item = json_object_array_get_idx(val, i);
                        if (json_object_is_type(item, json_type_string)) {
                            string_vector_append(&other_messages, json_object_get_string(item));
                        }
                    }
                } else if (!json_object_is_type(val, json_type_null)) {
                    string_vector_append(&other_messages, json_object_get_string(val));
                }
            }
        }

        for (size_t i = 0; i < other_messages.size; i++) {
            string_vector_append(&errors, other_messages.data[i]);
        }
    } else {
        string_vector_append(&errors, error);
    }

    response.errors = errors;
    if (hash_map != NULL) {
        json_object_put(hash_map);
    }

    return response;
}

int compare_zb_error_response(const ZBErrorResponse* response1, const ZBErrorResponse* response2) {
    if (response1->success != response2->success) {
        return 0;
    }

    if (compare_string_vector(&response1->errors, &response2->errors) != 1) {
        return 0;
    }

    return 1;
}
