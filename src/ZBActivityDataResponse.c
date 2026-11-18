#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBActivityDataResponse.h"
#include "ZeroBounce/utils.h"
#include "ZeroBounce/zb_internal.h"

ZBActivityDataResponse new_zb_activity_data_response(void) {
    ZBActivityDataResponse response;

    response.found = false;
    response.active_in_days = -1;
    response.error = zb_dup_empty();

    return response;
}

char* zb_activity_data_response_to_string(ZBActivityDataResponse* response) {
    const char *serialization = "ZBActivityDataResponse{"
        "found=%d, "
        "active_in_days=%d, "
        "error='%s'"
        "}";

    int size = snprintf(
        NULL, 0, serialization,
        response->found,
        response->active_in_days,
        response->error
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
        response->found,
        response->active_in_days,
        response->error
    );
    return buffer;
}

ZBActivityDataResponse zb_activity_data_response_from_json(const json_object* j) {
    ZBActivityDataResponse r = new_zb_activity_data_response();
    /* Every string field is assigned below; drop the initial copies. */
    zb_activity_data_response_free(&r);

    r.found = zb_json_get_bool(j, "found", false);
    char* active_in_days = get_json_string_copy(j, "active_in_days", "-1");
    r.active_in_days = atoi(active_in_days);
    free(active_in_days);
    r.error = get_json_string_copy(j, "error", "");

    return r;
}

void zb_activity_data_response_free(ZBActivityDataResponse* instance) {
    if (!instance) {
        return;
    }
    free(instance->error);
    instance->error = NULL;
}

int compare_zb_activity_data_response(const ZBActivityDataResponse* response1, const ZBActivityDataResponse* response2) {
    return response1->found == response2->found &&
        response1->active_in_days == response2->active_in_days &&
        strcmp(response1->error, response2->error) == 0;
}
