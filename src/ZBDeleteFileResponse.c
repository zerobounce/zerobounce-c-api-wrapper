#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBDeleteFileResponse.h"
#include "ZeroBounce/zb_internal.h"
#include "ZeroBounce/utils.h"

ZBDeleteFileResponse new_zb_delete_file_response(void) {
    ZBDeleteFileResponse response;

    response.success = false;
    response.message = zb_dup_empty();
    response.file_name = zb_dup_empty();
    response.file_id = zb_dup_empty();

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
    /* Every string field is assigned below; drop the initial copies. */
    zb_delete_file_response_free(&r);

    r.success = zb_success_field(j);
    r.message = zb_file_job_message(j);
    r.file_name = get_json_string_copy(j, "file_name", "");
    r.file_id = get_json_string_copy(j, "file_id", "");

    return r;
}

void zb_delete_file_response_free(ZBDeleteFileResponse* instance) {
    if (!instance) {
        return;
    }
    free(instance->message);
    instance->message = NULL;
    free(instance->file_name);
    instance->file_name = NULL;
    free(instance->file_id);
    instance->file_id = NULL;
}

int compare_zb_delete_file_response(const ZBDeleteFileResponse* response1, const ZBDeleteFileResponse* response2) {
    return response1->success == response2->success &&
        strcmp(response1->message, response2->message) == 0 &&
        strcmp(response1->file_name, response2->file_name) == 0 &&
        strcmp(response1->file_id, response2->file_id) == 0;
}
