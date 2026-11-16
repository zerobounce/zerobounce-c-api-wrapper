#ifndef ZBCREDITSRESPONSE_H
#define ZBCREDITSRESPONSE_H

#include <json.h>

/**
 * @brief The struct associated with the GET /getcredits request.
 *
 */
typedef struct {
    int credits;
} ZBCreditsResponse;

/**
 * @brief Function used to initialize a new ZBCreditsResponse.
 *
 * @return ZBCreditsResponse new instance
 */
ZBCreditsResponse new_zb_credits_response(void);

/**
 * @brief Function used to serialize a ZBCreditsResponse.
 *
 * @param response ZBCreditsResponse pointer
 * @return char* serialization
 */
char* zb_credits_response_to_string(ZBCreditsResponse* response);

/**
 * @brief Function used to create ZBCreditsResponse from a json object.
 *
 * @param j json pointer
 * @return ZBCreditsResponse new instance
 */
ZBCreditsResponse zb_credits_response_from_json(const json_object* j);

/**
 * @brief Function used to compare ZBCreditsResponse instances.
 *
 * @param response1 ZBCreditsResponse pointer
 * @param response2 ZBCreditsResponse pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_zb_credits_response(const ZBCreditsResponse* response1, const ZBCreditsResponse* response2);

#endif