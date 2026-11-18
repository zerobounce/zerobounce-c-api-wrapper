#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBFileStatusResponse.h"
#include "ZeroBounce/utils.h"
#include "ZeroBounce/zb_internal.h"

ZBFileStatusResponse new_zb_file_status_response(void) {
    ZBFileStatusResponse response;

    response.success = false;
    response.message = zb_dup_empty();
    response.error_reason = zb_dup_empty();
    response.file_id = zb_dup_empty();
    response.file_name = zb_dup_empty();
    response.upload_date = zb_dup_empty();
    response.file_status = zb_dup_empty();
    response.complete_percentage = zb_dup_empty();
    response.file_phase_2_status = zb_dup_empty();
    response.return_url = zb_dup_empty();

    return response;
}

char* zb_file_status_response_to_string(ZBFileStatusResponse* response) {
    const char *serialization = "ZBFileStatusResponse{"
        "success=%d"
        ", message='%s'"
        ", file_id='%s'"
        ", file_name='%s'"
        ", upload_date='%s'"
        ", file_status='%s'"
        ", complete_percentage='%s'"
        ", file_phase_2_status='%s'"
        ", return_url='%s'"
        ", error_reason='%s'"
        "}";

    int size = snprintf(
        NULL, 0, serialization,
        response->success,
        response->message,
        response->file_id,
        response->file_name,
        response->upload_date,
        response->file_status,
        response->complete_percentage,
        response->file_phase_2_status,
        response->return_url,
        response->error_reason
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
        response->file_id,
        response->file_name,
        response->upload_date,
        response->file_status,
        response->complete_percentage,
        response->file_phase_2_status,
        response->return_url,
        response->error_reason
    );
    return buffer;
}

/* Whether the object holds at least one non-null field a file status has. */
static bool zb_file_status_has_known_field(const json_object *j) {
    static const char *const keys[] = {
        "success", "file_id", "file_name", "file_status", "error_reason",
        "file_phase_2_status", "return_url", "upload_date", "complete_percentage"
    };
    size_t i;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        json_object *value = NULL;
        if (json_object_object_get_ex(j, keys[i], &value) && value &&
            !json_object_is_type(value, json_type_null)) {
            return true;
        }
    }
    return false;
}

ZBFileStatusResponse zb_file_status_response_from_json(const json_object* j) {
    ZBFileStatusResponse r = new_zb_file_status_response();
    /* Every string field is assigned below; drop the initial copies. */
    zb_file_status_response_free(&r);

    /* An object with none of the fields a status has ({"foo":1}, {"":""},
       a message beside a null file_id) is not the API's answer. */
    if (!zb_file_status_has_known_field(j)) {
        zb_shape_mismatch("file_status");
    }

    r.success = zb_success_field(j);
    r.message = zb_file_job_message(j);
    r.error_reason = get_json_string_copy(j, "error_reason", "");
    r.file_id = get_json_string_copy(j, "file_id", "");
    r.file_name = get_json_string_copy(j, "file_name", "");
    r.upload_date = get_json_string_copy(j, "upload_date", "");
    r.file_status = get_json_string_copy(j, "file_status", "");
    r.complete_percentage = get_json_string_copy(j, "complete_percentage", "");
    r.file_phase_2_status = get_json_string_copy(j, "file_phase_2_status", "");
    r.return_url = get_json_string_copy(j, "return_url", "");

    return r;
}

void zb_file_status_response_free(ZBFileStatusResponse* instance) {
    if (!instance) {
        return;
    }
    free(instance->message);
    instance->message = NULL;
    free(instance->error_reason);
    instance->error_reason = NULL;
    free(instance->file_id);
    instance->file_id = NULL;
    free(instance->file_name);
    instance->file_name = NULL;
    free(instance->upload_date);
    instance->upload_date = NULL;
    free(instance->file_status);
    instance->file_status = NULL;
    free(instance->complete_percentage);
    instance->complete_percentage = NULL;
    free(instance->file_phase_2_status);
    instance->file_phase_2_status = NULL;
    free(instance->return_url);
    instance->return_url = NULL;
}

int compare_zb_file_status_response(const ZBFileStatusResponse* response1, const ZBFileStatusResponse* response2) {
    return response1->success == response2->success &&
        strcmp(response1->message, response2->message) == 0 &&
        strcmp(response1->error_reason, response2->error_reason) == 0 &&
        strcmp(response1->file_id, response2->file_id) == 0 &&
        strcmp(response1->file_name, response2->file_name) == 0 &&
        strcmp(response1->upload_date, response2->upload_date) == 0 &&
        strcmp(response1->file_status, response2->file_status) == 0 &&
        strcmp(response1->complete_percentage, response2->complete_percentage) == 0 &&
        strcmp(response1->file_phase_2_status, response2->file_phase_2_status) == 0 &&
        strcmp(response1->return_url, response2->return_url) == 0;
}
