#ifndef ZB_DOMAIN_SEARCH_RESPONSE_H
#define ZB_DOMAIN_SEARCH_RESPONSE_H

#include <json.h>

/**
 * @brief highlights how a domain can format its emails and how confident is
 * the API about it
 */
typedef struct {
    char* format;
    char* confidence;
} ZBDomainFormat;

/**
 * @brief array of data format objects
 *
 */
typedef struct {
    ZBDomainFormat* data;
    size_t size;
} ZBDomainFormatVector;

/**
 * @brief The struct associated with the GET /guessformat request.
 *
 */
typedef struct {
    char* domain;
    char* company_name;
    char* format;
    char* confidence;
    char* did_you_mean;
    char* failure_reason;
    ZBDomainFormatVector other_domain_formats;
} ZBDomainSearchResponse;


// ZBDomainFormat methods

/**
 * @brief Function used to initialize a new ZBDomainFormat.
 *
 * @return ZBDomainFormat new instance
 */
ZBDomainFormat zb_domain_format_init();

/**
 * @brief Function used to serialize a ZBDomainFormat.
 *
 * @param response ZBDomainFormat pointer
 * @return char* serialization
 */
char* zb_domain_format_to_string(ZBDomainFormat* domain_format);

/**
 * @brief Function used to create ZBDomainFormat from a json object.
 *
 * @param j json pointer
 * @return ZBDomainFormat new instance
 */
ZBDomainFormat zb_domain_format_from_json(const json_object* j);

/**
 * @brief Function used to free the memory of a ZBDomainFormat instance.
 *
 * @param vector ZBDomainFormat pointer
 */
void zb_domain_format_free(ZBDomainFormat* instance);

/**
 * @brief Ensure two ZBDomainFormat instances are the same
 *
 * @param instance1 first ZBDomainFormat instance to compare
 * @param instance2 second ZBDomainFormat instance to compare
 * @return int whether the objects are the same (value 1) or not (value 0)
 */
int zb_domain_format_compare(ZBDomainFormat* instance1, ZBDomainFormat* instance2);

// ZBDomainFormatVector methods

/**
 * @brief Function used to initialize a new ZBDomainFormatVector.
 *
 * @return ZBDomainFormatVector new instance
 */
ZBDomainFormatVector zb_domain_format_vector_init();

/**
 * @brief Function used to add a ZBDomainFormat instance to the vector.
 *
 * @param vector ZBDomainFormatVector vector pointer
 * @param instance ZBDomainFormat to be added
 */
void zb_domain_format_vector_append(ZBDomainFormatVector* vector, const ZBDomainFormat instance);

/**
 * @brief Function used to free the memory of a ZBDomainFormatVector instance.
 *
 * @param vector ZBDomainFormatVector pointer
 */
void zb_domain_format_vector_free(ZBDomainFormatVector* vector);

/**
 * @brief Function used to serialize a ZBDomainFormatVector.
 *
 * @param response ZBDomainFormatVector pointer
 * @return char* serialization
 */
char* zb_domain_format_vector_to_string(ZBDomainFormatVector* vector);

/**
 * @brief Function used to create ZBDomainFormatVector from a json object.
 *
 * @param j json pointer
 * @return ZBDomainFormatVector new instance
 */
ZBDomainFormatVector zb_domain_format_vector_from_json(const json_object* j);

/**
 * @brief Ensure two ZBDomainFormatVector instances are the same
 *
 * @param instance1 first ZBDomainFormatVector instance to compare
 * @param instance2 second ZBDomainFormatVector instance to compare
 * @return int whether the objects are the same (value 1) or not (value 0)
 */
int zb_domain_format_vector_compare(ZBDomainFormatVector* instance1, ZBDomainFormatVector* instance2);

// ZBDomainSearchResponse methods

/**
 * @brief Function used to initialize a new ZBDomainSearchResponse.
 *
 * @return ZBDomainSearchResponse new instance
 */
ZBDomainSearchResponse zb_domain_search_response_init();

/**
 * @brief Function used to serialize a ZBDomainSearchResponse.
 *
 * @param response ZBDomainSearchResponse pointer
 * @return char* serialization
 */
char* zb_domain_search_response_to_string(ZBDomainSearchResponse* response);

/**
 * @brief Function used to create ZBDomainSearchResponse from a json object.
 *
 * @param j json pointer
 * @return ZBDomainSearchResponse new instance
 */
ZBDomainSearchResponse zb_domain_search_response_from_json(const json_object* j);

/**
 * @brief Function used to free the memory of a ZBDomainSearchResponse instance.
 *
 * @param vector ZBDomainSearchResponse pointer
 */
void zb_domain_search_response_free(ZBDomainSearchResponse* instance);

/**
 * @brief Ensure two ZBDomainSearchResponse instances are the same
 *
 * @param instance1 first ZBDomainSearchResponse instance to compare
 * @param instance2 second ZBDomainSearchResponse instance to compare
 * @return int whether the objects are the same (value 1) or not (value 0)
 */
int zb_domain_search_response_compare(ZBDomainSearchResponse* instance1, ZBDomainSearchResponse* instance2);

#endif