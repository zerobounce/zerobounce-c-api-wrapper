#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBSendFileResponse.h"

ZBSendFileResponse new_zb_send_file_response(void) {
    ZBSendFileResponse response;

    response.success = false;
    response.message = string_vector_init();
    response.file_name = "";
    response.file_id = "";

    return response;
}

char* zb_send_file_response_to_string(ZBSendFileResponse* response) {
    const char *serialization = "ZBSendFileResponse{"
        "success=%d"
        ", message='%s'"
        ", file_name='%s'"
        ", file_id='%s'"
        "}";
    char* messages = concatenate_strings(&response->message, ", ");

    int size = snprintf(
        NULL, 0, serialization,
        response->success,
        messages,
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
        messages,
        response->file_name,
        response->file_id
    );
    return buffer;
}

ZBSendFileResponse zb_send_file_response_from_json(const json_object* j) {
    ZBSendFileResponse r = new_zb_send_file_response();

    r.success = *(bool*)get_json_value(j, json_type_boolean, "success", &(bool){false});

    if (json_object_is_type(json_object_object_get(j, "message"), json_type_array)) {
        string_vector_free(&r.message);
        StringVector default_value = string_vector_init();
        r.message = *(StringVector*)get_json_value(j, json_type_array, "message", &default_value);
        string_vector_free(&default_value);
    } else {
        string_vector_append(&r.message, *(char**)get_json_value(j, json_type_string, "message", &(char*){""}));
    }

    r.file_name = *(char**)get_json_value(j, json_type_string, "file_name", &(char*){""});
    r.file_id = *(char**)get_json_value(j, json_type_string, "file_id", &(char*){""});

    return r;
}

int compare_zb_send_file_response(const ZBSendFileResponse* response1, const ZBSendFileResponse* response2) {
    return response1->success == response2->success &&
        compare_string_vector(&response1->message, &response2->message) &&
        strcmp(response1->file_name, response2->file_name) == 0 &&
        strcmp(response1->file_id, response2->file_id) == 0;
}
