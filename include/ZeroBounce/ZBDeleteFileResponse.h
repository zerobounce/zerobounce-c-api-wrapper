#ifndef ZBDELETEFILERESPONSE_H
#define ZBDELETEFILERESPONSE_H

#include <stdbool.h>

#include <json.h>

/**
 * @brief The struct associated with the GET /scoring/deletefile GET /deletefile requests.
 *
 */
typedef struct {
    bool success ;
    char* message;
    char* file_name;
    char* file_id;
} ZBDeleteFileResponse;

/**
 * @brief Function used to initialize a new ZBDeleteFileResponse.
 *
 * @return ZBDeleteFileResponse new instance
 */
ZBDeleteFileResponse new_zb_delete_file_response(void);

/**
 * @brief Function used to serialize a ZBDeleteFileResponse.
 *
 * @param response ZBDeleteFileResponse pointer
 * @return char* serialization
 */
char* zb_delete_file_response_to_string(ZBDeleteFileResponse* response);

/**
 * @brief Function used to create ZBDeleteFileResponse from a json object.
 *
 * @param j json pointer
 * @return ZBDeleteFileResponse new instance
 */
ZBDeleteFileResponse zb_delete_file_response_from_json(const json_object* j);

/**
 * @brief Function used to compare ZBDeleteFileResponse instances.
 *
 * @param response1 ZBDeleteFileResponse pointer
 * @param response2 ZBDeleteFileResponse pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_zb_delete_file_response(const ZBDeleteFileResponse* response1, const ZBDeleteFileResponse* response2);

/**
 * @brief Frees the strings of a ZBDeleteFileResponse the SDK produced (every string field is a
 * heap copy owned by the caller), sets them to NULL. NULL-safe and idempotent.
 *
 * @param instance ZBDeleteFileResponse pointer
 */
void zb_delete_file_response_free(ZBDeleteFileResponse* instance);

#endif