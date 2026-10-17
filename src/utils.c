#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "ZeroBounce/utils.h"

StringVector string_vector_init() {
    StringVector vector;
    vector.size = 0;
    vector.data = NULL;
    return vector;
}

void string_vector_append(StringVector* vector, const char* str) {
    if (!vector) return;

    vector->data = realloc(vector->data, (vector->size + 1) * sizeof(char*));
    if (!vector->data) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    vector->data[vector->size] = strdup(str);
    vector->size++;
}

void string_vector_free(StringVector* vector) {
    if (!vector) return;

    for (size_t i = 0; i < vector->size; i++) {
        free(vector->data[i]);
    }
    free(vector->data);
    vector->data = NULL;
    vector->size = 0;
}

int compare_string_vector(const StringVector* vec1, const StringVector* vec2) {
    if (vec1->size != vec2->size) {
        return 0;
    }

    for (size_t i = 0; i < vec1->size; i++) {
        if (strcmp(vec1->data[i], vec2->data[i]) != 0) {
            return 0;
        }
    }

    return 1;
}

char* concatenate_strings(const StringVector* vec, char* separator) {
    size_t total_length = 1;
    if (separator == NULL) {
        separator = "";
    }

    for (size_t i = 0; i < vec->size; i++) {
        total_length += strlen(vec->data[i]) + 2;
        if (i < vec->size - 1) {
            total_length += strlen(separator);
        }
    }

    char* result = (char*) malloc(total_length);
    if (!result) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    result[0] = '\0';

    for (size_t i = 0; i < vec->size; i++) {
        strcat(result, vec->data[i]);
        if (i < vec->size - 1) {
            strcat(result, separator);
        }
    }

    return result;
}

void* get_json_value(const json_object* obj, json_type type, const char* key, void* default_value) {
    json_object* value_obj = NULL;
    int* int_value;
    bool* bool_value;
    char** string_value;
    StringVector* string_vector_value;

    if (json_object_object_get_ex(obj, key, &value_obj)) {
        if (json_object_is_type(value_obj, type)) {
            switch (type) {
                case json_type_boolean:
                    bool_value = (bool*) malloc(sizeof(bool));
                    if (!bool_value) {
                        fprintf(stderr, "Memory allocation failed.\n");
                        exit(EXIT_FAILURE);
                    }
                    *bool_value = json_object_get_boolean(value_obj);
                    return (void*)bool_value;
                case json_type_int:
                    int_value = (int*) malloc(sizeof(int));
                    if (!int_value) {
                        fprintf(stderr, "Memory allocation failed.\n");
                        exit(EXIT_FAILURE);
                    }
                    *int_value = json_object_get_int(value_obj);
                    return (void*)int_value;
                case json_type_string:
                    string_value = (char**) malloc(sizeof(char*));
                    if (!string_value) {
                        fprintf(stderr, "Memory allocation failed.\n");
                        exit(EXIT_FAILURE);
                    }
                    *string_value = strdup(json_object_get_string(value_obj));
                    return (void*) string_value;
                case json_type_array:
                    string_vector_value = (StringVector*) malloc(sizeof(StringVector));
                    if (!string_vector_value) {
                        fprintf(stderr, "Memory allocation failed.\n");
                        exit(EXIT_FAILURE);
                    }

                    *string_vector_value = string_vector_init();

                    size_t array_length = json_object_array_length(value_obj);
                    for (size_t i = 0; i < array_length; i++) {
                        json_object* item = json_object_array_get_idx(value_obj, i);
                        if (json_object_is_type(item, json_type_string)) {
                            string_vector_append(string_vector_value, json_object_get_string(item));
                        }
                    }
                    return (void*) string_vector_value;
                default:
                    return NULL;
            }
        }
    }
    return default_value;
}

char* get_json_string_copy(const json_object* obj, const char* key, const char* default_value) {
    const char* def = default_value ? default_value : "";
    json_object* value_obj = NULL;

    if (obj && json_object_object_get_ex(obj, key, &value_obj)) {
        if (json_object_is_type(value_obj, json_type_string)) {
            return strdup(json_object_get_string(value_obj));
        }
    }
    return strdup(def);
}