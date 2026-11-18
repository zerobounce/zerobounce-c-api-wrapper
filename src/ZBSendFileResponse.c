#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBSendFileResponse.h"
#include "ZeroBounce/zb_internal.h"

ZBSendFileResponse new_zb_send_file_response(void) {
    ZBSendFileResponse response;

    response.success = false;
    response.message = string_vector_init();
    response.file_name = zb_dup_empty();
    response.file_id = zb_dup_empty();

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

    r.success = zb_success_field(j);

    if (json_object_is_type(json_object_object_get(j, "message"), json_type_array)) {
        string_vector_free(&r.message);
        r.message = zb_json_get_string_vector(j, "message");
    } else {
        char* message = get_json_string_copy(j, "message", "");
        string_vector_append(&r.message, message);
        free(message);
    }

    free(r.file_name);
    free(r.file_id);
    r.file_name = get_json_string_copy(j, "file_name", "");
    r.file_id = get_json_string_copy(j, "file_id", "");

    return r;
}

void zb_send_file_response_free(ZBSendFileResponse* instance) {
    if (!instance) {
        return;
    }
    string_vector_free(&instance->message);
    free(instance->file_name);
    instance->file_name = NULL;
    free(instance->file_id);
    instance->file_id = NULL;
}

int compare_zb_send_file_response(const ZBSendFileResponse* response1, const ZBSendFileResponse* response2) {
    return response1->success == response2->success &&
        compare_string_vector(&response1->message, &response2->message) &&
        strcmp(response1->file_name, response2->file_name) == 0 &&
        strcmp(response1->file_id, response2->file_id) == 0;
}
