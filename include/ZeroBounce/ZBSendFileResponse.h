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

#endif