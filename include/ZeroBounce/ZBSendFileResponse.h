#ifndef ZBSENDFILERESPONSE_H
#define ZBSENDFILERESPONSE_H

#include <stdbool.h>

#include <json.h>

#include "utils.h"

/**
 * @brief The struct associated with the POST /scoring/sendfile and POST /sendfile requests.
 *
 */
typedef struct {
    bool success;
    StringVector message;
    char* file_name;
    char* file_id;
} ZBSendFileResponse;

/**
 * @brief Function used to initialize a new ZBSendFileResponse.
 *
 * @return ZBSendFileResponse new instance
 */
ZBSendFileResponse new_zb_send_file_response(void);

/**
 * @brief Function used to serialize a ZBSendFileResponse.
 *
 * @param response ZBSendFileResponse pointer
 * @return char* serialization
 */
char* zb_send_file_response_to_string(ZBSendFileResponse* response);

/**
 * @brief Function used to create ZBSendFileResponse from a json object.
 *
 * @param j json pointer
 * @return ZBSendFileResponse new instance
 */
ZBSendFileResponse zb_send_file_response_from_json(const json_object* j);

/**
 * @brief Function used to compare ZBSendFileResponse instances.
 *
 * @param response1 ZBSendFileResponse pointer
 * @param response2 ZBSendFileResponse pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_zb_send_file_response(const ZBSendFileResponse* response1, const ZBSendFileResponse* response2);
/**
 * @brief Frees the strings of a ZBSendFileResponse the SDK produced (message
 * strings, file_name, file_id), sets the pointers to NULL. NULL-safe and idempotent.
 *
 * @param instance ZBSendFileResponse pointer
 */
void zb_send_file_response_free(ZBSendFileResponse* instance);

#endif