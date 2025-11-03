#ifndef ZB_FIND_EMAIL_RESPONSE_H
#define ZB_FIND_EMAIL_RESPONSE_H

#include <json.h>

/**
 * @brief The struct associated with the GET /guessformat request.
 *
 */
typedef struct {
    char* email;
    char* email_confidence;
    char* domain;
    char* company_name;
    char* did_you_mean;
    char* failure_reason;
} ZBFindEmailResponse;


// ZBFindEmailResponse methods

/**
 * @brief Function used to initialize a new ZBFindEmailResponse.
 *
 * @return ZBFindEmailResponse new instance
 */
ZBFindEmailResponse zb_find_email_response_init();

/**
 * @brief Function used to serialize a ZBFindEmailResponse.
 *
 * @param response ZBFindEmailResponse pointer
 * @return char* serialization
 */
char* zb_find_email_response_to_string(ZBFindEmailResponse* response);

/**
 * @brief Function used to create ZBFindEmailResponse from a json object.
 *
 * @param j json pointer
 * @return ZBFindEmailResponse new instance
 */
ZBFindEmailResponse zb_find_email_response_from_json(const json_object* j);

/**
 * @brief Function used to free the memory of a ZBFindEmailResponse instance.
 *
 * @param vector ZBFindEmailResponse pointer
 */
void zb_find_email_response_free(ZBFindEmailResponse* instance);

/**
 * @brief Ensure two ZBFindEmailResponse instances are the same
 *
 * @param instance1 first ZBFindEmailResponse instance to compare
 * @param instance2 second ZBFindEmailResponse instance to compare
 * @return int whether the objects are the same (value 1) or not (value 0)
 */
int zb_find_email_response_compare(ZBFindEmailResponse* instance1, ZBFindEmailResponse* instance2);

#endif