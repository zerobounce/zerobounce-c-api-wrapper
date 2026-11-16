#ifndef ZBGETFILERESPONSE_H
#define ZBGETFILERESPONSE_H

#include <stdbool.h>

#include <json.h>

/**
 * @brief The struct associated with the GET /scoring/getfile and GET /getfile requests.
 *
 */
typedef struct {
    bool success;
    char* message;
    char* local_file_path;
} ZBGetFileResponse;

/**
 * @brief Function used to initialize a new ZBGetFileResponse.
 *
 * @return ZBGetFileResponse new instance
 */
ZBGetFileResponse new_zb_get_file_response(void);

/**
 * @brief Function used to serialize a ZBGetFileResponse.
 *
 * @param response ZBGetFileResponse pointer
 * @return char* serialization
 */
char* zb_get_file_response_to_string(ZBGetFileResponse* response);

/**
 * @brief Function used to create ZBGetFileResponse from a json object.
 *
 * @param j json pointer
 * @return ZBGetFileResponse new instance
 */
ZBGetFileResponse zb_get_file_response_from_json(const json_object* j);

/**
 * @brief Function used to compare ZBGetFileResponse instances.
 *
 * @param response1 ZBGetFileResponse pointer
 * @param response2 ZBGetFileResponse pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_zb_get_file_response(const ZBGetFileResponse* response1, const ZBGetFileResponse* response2);

#endif