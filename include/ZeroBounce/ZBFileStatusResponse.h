#ifndef ZBFILESTATUSRESPONSE_H
#define ZBFILESTATUSRESPONSE_H

#include <stdbool.h>

#include <json.h>

/**
 * @brief The struct associated with the GET /scoring/filestatus and GET /filestatus requests.
 *
 */
typedef struct {
    bool success;
    char* message;
    char* error_reason;
    char* file_id;
    char* file_name;
    char* upload_date;
    char* file_status;
    char* complete_percentage;
    char* file_phase_2_status;
    char* return_url;
} ZBFileStatusResponse;

/**
 * @brief Function used to initialize a new ZBFileStatusResponse.
 *
 * @return ZBFileStatusResponse new instance
 */
ZBFileStatusResponse new_zb_file_status_response();

/**
 * @brief Function used to serialize a ZBFileStatusResponse.
 *
 * @param response ZBFileStatusResponse pointer
 * @return char* serialization
 */
char* zb_file_status_response_to_string(ZBFileStatusResponse* response);

/**
 * @brief Function used to create ZBFileStatusResponse from a json object.
 *
 * @param j json pointer
 * @return ZBFileStatusResponse new instance
 */
ZBFileStatusResponse zb_file_status_response_from_json(const json_object* j);

/**
 * @brief Function used to compare ZBFileStatusResponse instances.
 *
 * @param response1 ZBFileStatusResponse pointer
 * @param response2 ZBFileStatusResponse pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_zb_file_status_response(const ZBFileStatusResponse* response1, const ZBFileStatusResponse* response2);

#endif