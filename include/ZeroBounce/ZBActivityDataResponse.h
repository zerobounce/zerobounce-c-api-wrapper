#ifndef ZBACTIVITYDATARESPONSE_H
#define ZBACTIVITYDATARESPONSE_H

#include <stdbool.h>

#include <json.h>

/**
 * @brief The struct associated with the GET /activity request.
 *
 */
typedef struct {
    bool found;
    int active_in_days;
    char* error;
} ZBActivityDataResponse;

/**
 * @brief Function used to initialize a new ZBActivityDataResponse.
 *
 * @return ZBActivityDataResponse new instance
 */
ZBActivityDataResponse new_zb_activity_data_response(void);

/**
 * @brief Function used to serialize a ZBActivityDataResponse.
 *
 * @param response ZBActivityDataResponse pointer
 * @return char* serialization
 */
char* zb_activity_data_response_to_string(ZBActivityDataResponse* response);

/**
 * @brief Function used to create ZBActivityDataResponse from a json object.
 *
 * @param j json pointer
 * @return ZBActivityDataResponse new instance
 */
ZBActivityDataResponse zb_activity_data_response_from_json(const json_object* j);

/**
 * @brief Function used to compare ZBActivityDataResponse instances.
 *
 * @param response1 ZBActivityDataResponse pointer
 * @param response2 ZBActivityDataResponse pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_zb_activity_data_response(const ZBActivityDataResponse* response1, const ZBActivityDataResponse* response2);

/**
 * @brief Frees the strings of a ZBActivityDataResponse the SDK produced (every string field is a
 * heap copy owned by the caller), sets them to NULL. NULL-safe and idempotent.
 *
 * @param instance ZBActivityDataResponse pointer
 */
void zb_activity_data_response_free(ZBActivityDataResponse* instance);

#endif