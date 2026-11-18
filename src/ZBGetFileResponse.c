#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBGetFileResponse.h"
#include "ZeroBounce/zb_internal.h"
#include "ZeroBounce/utils.h"

ZBGetFileResponse new_zb_get_file_response(void) {
    ZBGetFileResponse response;

    response.success = false;
    response.message = zb_dup_empty();
    response.local_file_path = zb_dup_empty();

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
    /* Every string field is assigned below; drop the initial copies. */
    zb_get_file_response_free(&r);

    r.success = zb_success_field(j);
    r.message = zb_file_job_message(j);
    r.local_file_path = zb_dup_empty();

    return r;
}

void zb_get_file_response_free(ZBGetFileResponse* instance) {
    if (!instance) {
        return;
    }
    free(instance->message);
    instance->message = NULL;
    free(instance->local_file_path);
    instance->local_file_path = NULL;
}

int compare_zb_get_file_response(const ZBGetFileResponse* response1, const ZBGetFileResponse* response2) {
    return response1->success == response2->success &&
        strcmp(response1->message, response2->message) == 0 &&
        strcmp(response1->local_file_path, response2->local_file_path) == 0;
}
