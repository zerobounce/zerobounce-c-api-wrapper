#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBDeleteFileResponse.h"
#include "ZeroBounce/utils.h"

ZBDeleteFileResponse new_zb_delete_file_response() {
    ZBDeleteFileResponse response;

    response.success = false;
    response.message = "";
    response.file_name = "";
    response.file_id = "";

    return response;
}

char* zb_delete_file_response_to_string(ZBDeleteFileResponse* response) {
    const char *serialization = "ZBDeleteFileResponse{"
        "success=%d"
        ", message='%s'"
        ", file_name='%s'"
        ", file_id='%s'"
        "}";

    int size = snprintf(
        NULL, 0, serialization,
        response->success,
        response->message,
        response->file_name,
        response->file_id
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
        response->file_name,
        response->file_id
    );
    return buffer;
}

ZBDeleteFileResponse zb_delete_file_response_from_json(const json_object* j) {
    ZBDeleteFileResponse r = new_zb_delete_file_response();

    r.success = *(bool*)get_json_value(j, json_type_boolean, "success", &(bool){false});
    r.message = zb_file_job_message(j);
    r.file_name = *(char**)get_json_value(j, json_type_string, "file_name", &(char*){""});
    r.file_id = *(char**)get_json_value(j, json_type_string, "file_id", &(char*){""});

    return r;
}

int compare_zb_delete_file_response(const ZBDeleteFileResponse* response1, const ZBDeleteFileResponse* response2) {
    return response1->success == response2->success &&
        strcmp(response1->message, response2->message) == 0 &&
        strcmp(response1->file_name, response2->file_name) == 0 &&
        strcmp(response1->file_id, response2->file_id) == 0;
}
