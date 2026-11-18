#ifndef ZBVALIDATEBATCHRESPONSE_H
#define ZBVALIDATEBATCHRESPONSE_H

#include "ZBValidateResponse.h"

/**
 * @brief Struct representing an item that is used in email batch validation.
 *
 */
typedef struct {
    char* email_address;
    char* ip_address;
} ZBEmailToValidate;

/**
 * @brief Function used to initialize a new ZBEmailToValidate.
 *
 * @return ZBEmailToValidate new instance
 */
ZBEmailToValidate new_zb_email_to_validate(void);


/**
 * @brief Struct used to simulate a vector of ZBEmailToValidate instances.
 *
 */
typedef struct {
    ZBEmailToValidate* data;
    size_t size;
} EmailToValidateVector;

/**
 * @brief Initializes a new EmailToValidateVector instance.
 *
 * @return EmailToValidateVector new instance
 */
EmailToValidateVector email_to_validate_vector_init(void);

/**
 * @brief Function used to add a ZBEmailToValidate instance to the vector.
 *
 * @param vector EmailToValidateVector pointer
 * @param email ZBEmailToValidate to be added
 */
void email_to_validate_vector_append(EmailToValidateVector* vector, const ZBEmailToValidate email);

/**
 * @brief Function used to free the memory of a EmailToValidateVector instance.
 *
 * @param vector EmailToValidateVector pointer
 */
void email_to_validate_vector_free(EmailToValidateVector* vector);


/**
 * @brief Struct representing error from email batch validation.
 *
 */
typedef struct {
    char* error;
    char* email_address;
} ZBValidateError;

/**
 * @brief Function used to initialize a new ZBValidateError.
 *
 * @return ZBValidateError new instance
 */
ZBValidateError new_zb_validate_error(void);

/**
 * @brief Function used to serialize a ZBValidateError.
 *
 * @param response ZBValidateError pointer
 * @return char* serialization
 */
char* zb_validate_error_to_string(ZBValidateError* response);

/**
 * @brief Function used to create ZBValidateError from a json object.
 *
 * @param j json pointer
 * @return ZBValidateError new instance
 */
ZBValidateError zb_validate_error_from_json(const json_object* j);


/**
 * @brief Frees the strings of a ZBValidateError the SDK produced, sets them to
 * NULL. NULL-safe and idempotent.
 *
 * @param instance ZBValidateError pointer
 */
void zb_validate_error_free(ZBValidateError* instance);


/**
 * @brief Struct used to simulate a vector of ZBValidateError instances.
 *
 */
typedef struct {
    ZBValidateError* data;
    size_t size;
} ValidateErrorVector;

/**
 * @brief Initializes a new ValidateErrorVector instance.
 *
 * @return ValidateErrorVector new instance
 */
ValidateErrorVector validate_error_vector_init(void);

/**
 * @brief Function used to add a ZBValidateError instance to the vector.
 *
 * @param vector ValidateErrorVector pointer
 * @param error ZBValidateError to be added
 */
void validate_error_vector_append(ValidateErrorVector* vector, const ZBValidateError error);

/**
 * @brief Function used to free the memory of a ValidateErrorVector instance.
 *
 * @param vector ValidateErrorVector pointer
 */
void validate_error_vector_free(ValidateErrorVector* vector);

/**
 * @brief Function used to compare ValidateErrorVector instances.
 *
 * @param vec1 ValidateErrorVector pointer
 * @param vec2 ValidateErrorVector pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_validate_error_vector(const ValidateErrorVector* vec1, const ValidateErrorVector* vec2);


/**
 * @brief Struct used to simulate a vector of ZBValidateResponse instances.
 *
 */
typedef struct {
    ZBValidateResponse* data;
    size_t size;
} ValidateResponseVector;

/**
 * @brief Initializes a new ValidateResponseVector instance.
 *
 * @return ValidateResponseVector new instance
 */
ValidateResponseVector validate_response_vector_init(void);

/**
 * @brief Function used to add a ZBValidateResponse instance to the vector.
 *
 * @param vector ValidateResponseVector pointer
 * @param response ZBValidateResponse to be added
 */
void validate_response_vector_append(ValidateResponseVector* vector, const ZBValidateResponse response);

/**
 * @brief Function used to free the memory of a ValidateResponseVector instance.
 *
 * @param vector ValidateResponseVector pointer
 */
void validate_response_vector_free(ValidateResponseVector* vector);

/**
 * @brief Function used to compare ValidateResponseVector instances.
 *
 * @param vec1 ValidateResponseVector pointer
 * @param vec2 ValidateResponseVector pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_validate_response_vector(const ValidateResponseVector* vec1, const ValidateResponseVector* vec2);


/**
 * @brief The struct associated with the POST /validatebatch request.
 *
 */
typedef struct {
    ValidateResponseVector email_batch;
    ValidateErrorVector errors;
} ZBValidateBatchResponse;

/**
 * @brief Function used to initialize a new ZBValidateBatchResponse.
 *
 * @return ZBValidateBatchResponse new instance
 */
ZBValidateBatchResponse new_zb_validate_batch_response(void);

/**
 * @brief Function used to serialize a ZBValidateBatchResponse.
 *
 * @param response ZBValidateBatchResponse pointer
 * @return char* serialization
 */
char* zb_validate_batch_response_to_string(ZBValidateBatchResponse* response);

/**
 * @brief Function used to create ZBValidateBatchResponse from a json object.
 *
 * @param j json pointer
 * @return ZBValidateBatchResponse new instance
 */
ZBValidateBatchResponse zb_validate_batch_response_from_json(const json_object* j);

/**
 * @brief Function used to compare ZBValidateBatchResponse instances.
 *
 * @param response1 ZBValidateBatchResponse pointer
 * @param response2 ZBValidateBatchResponse pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_zb_validate_batch_response(const ZBValidateBatchResponse* response1, const ZBValidateBatchResponse* response2);

/**
 * @brief Frees both vectors of a ZBValidateBatchResponse the SDK produced and
 * every string in them. NULL-safe and idempotent.
 *
 * @param instance ZBValidateBatchResponse pointer
 */
void zb_validate_batch_response_free(ZBValidateBatchResponse* instance);

#endif