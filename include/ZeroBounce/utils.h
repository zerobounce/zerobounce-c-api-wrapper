#ifndef UTILS_H
#define UTILS_H

#include <json.h>

/**
 * @brief Struct used to simulate a vector of strings.
 *
 */
typedef struct {
    size_t size;
    char** data;
} StringVector;

/**
 * @brief Initializes a new StringVector instance.
 *
 * @return StringVector new instance
 */
StringVector string_vector_init();

/**
 * @brief Function used to add a string to the vector of strings.
 *
 * @param vector StringVector pointer
 * @param str string to be added
 */
void string_vector_append(StringVector *vector, const char* str);

/**
 * @brief Function used to free the memory of a StringVector instance.
 *
 * @param vector StringVector pointer
 */
void string_vector_free(StringVector* vector);

/**
 * @brief Function used to compare StringVector instances.
 *
 * @param vec1 StringVector pointer
 * @param vec2 StringVector pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_string_vector(const StringVector* vec1, const StringVector* vec2);

/**
 * @brief Function used to concatenate the strings of a StringVector.
 *
 * @param vec StringVector pointer
 * @return char* resulting string
 */
char* concatenate_strings(const StringVector* vec, char* separator) ;

/**
 * @brief Function used to get the value of a given key in a json.
 * If the key doesn't exist or it's null, a default value is returned.
 *
 * @param obj json pointer
 * @param type type of the value to be extracteds
 * @param key name of the field
 * @param default_value pointer to default value
 * @return void* pointer to the found value
 */
void* get_json_value(const json_object* obj, json_type type, const char* key, void* default_value);

/**
 * @brief Heap-allocated copy of a JSON string field (or default). Caller must free().
 */
char* get_json_string_copy(const json_object* obj, const char* key, const char* default_value);

#endif