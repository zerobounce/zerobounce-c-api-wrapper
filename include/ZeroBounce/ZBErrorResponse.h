#ifndef ZBERRORRESPONSE_H
#define ZBERRORRESPONSE_H

#include <stdbool.h>

#include <json.h>

#include "utils.h"

/**
 * The struct used for when a request throws an error. This struct was introduced in order to provide
 * a standardized way of handling the error responses that a request can return.
 *
 * If the error JSON received from the server includes the words "error" or "message", then the
 * values of those keys will be added to the [errors] StringVector. If the error is not a JSON dictionary,
 * then JSON String will be added to the [errors] StringVector.
 *
 * If any type of messages are received, then they will be added according to the same rule above
 * after the errors found above.
 */
typedef struct {
    bool success;
    StringVector errors;
    /**
     * The HTTP status the request came back with, or 0 when the error was
     * raised before a response arrived (a rejected argument, a transport
     * failure). Lets a caller tell 429 from 500 without parsing the message.
     */
    long status_code;
} ZBErrorResponse;

/**
 * @brief Function used to initialize a new ZBErrorResponse.
 *
 * @return ZBErrorResponse new instance
 */
ZBErrorResponse new_zb_error_response();

/**
 * @brief Function used to serialize a ZBErrorResponse.
 *
 * @param response ZBErrorResponse pointer
 * @return char* serialization
 */
char* zb_error_response_to_string(ZBErrorResponse* response);

/**
 * @brief Function used to create ZBErrorResponse from an error string.
 *
 * @param error error string
 * @return ZBErrorResponse new instance
 */
ZBErrorResponse parse_error(const char* error);

/**
 * @brief As parse_error, but records the HTTP status the response carried.
 *
 * @param error error string
 * @param status_code HTTP status, or 0 when there was no response
 * @return ZBErrorResponse new instance
 */
ZBErrorResponse parse_error_with_status(const char* error, long status_code);

/**
 * @brief Whether a JSON body says the call failed although the HTTP status was
 * a success. The API sends "success" as a JSON false, "false" or "False".
 *
 * @param body response body
 * @return true when the body is a failure
 */
bool zb_success_means_failure(const char* body);

/**
 * @brief Function used to compare ZBErrorResponse instances.
 *
 * @param response1 ZBErrorResponse pointer
 * @param response2 ZBErrorResponse pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_zb_error_response(const ZBErrorResponse* response1, const ZBErrorResponse* response2);

#endif