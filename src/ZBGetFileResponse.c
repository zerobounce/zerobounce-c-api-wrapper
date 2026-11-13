#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBGetFileResponse.h"
#include "ZeroBounce/utils.h"

ZBGetFileResponse new_zb_get_file_response() {
    ZBGetFileResponse response;

    response.success = false;
    response.message = "";
    response.local_file_path = "";

    return response;
}

char* zb_get_file_response_to_string(ZBGetFileResponse* response) {
    const char *serialization = "ZBGetFileResponse{"
        "success=%d"
        ", message='%s'"
        ", local_file_path='%s'"
        "}";

    int size = snprintf(
        NULL, 0, serialization,
        response->success,
        response->message,
        response->local_file_path
    );

    if (size < 0) {
        return NULL;
    }

    char* buffer = (char*) malloc(size + 1);
    if (!buffer) {
        return NULL;
    }

    snprintf(
        buffer, size + 1, serialization,
        response->success,
        response->message,
        response->local_file_path
    );
    return buffer;
}

ZBGetFileResponse zb_get_file_response_from_json(const json_object* j) {
    ZBGetFileResponse r = new_zb_get_file_response();

    r.success = *(bool*)get_json_value(j, json_type_boolean, "success", &(bool){false});
    r.message = zb_file_job_message(j);

    return r;
}

int compare_zb_get_file_response(const ZBGetFileResponse* response1, const ZBGetFileResponse* response2) {
    return response1->success == response2->success &&
        strcmp(response1->message, response2->message) == 0 &&
        strcmp(response1->local_file_path, response2->local_file_path) == 0;
}
