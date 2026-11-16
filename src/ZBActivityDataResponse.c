#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBActivityDataResponse.h"
#include "ZeroBounce/utils.h"

ZBActivityDataResponse new_zb_activity_data_response(void) {
    ZBActivityDataResponse response;

    response.found = false;
    response.active_in_days = -1;
    response.error = "";

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

    r.found = *(bool*)get_json_value(j, json_type_boolean, "found", &(bool){false});
    r.active_in_days = atoi(*(char**)get_json_value(j, json_type_string, "active_in_days", &(char*){"-1"}));
    r.error = *(char**)get_json_value(j, json_type_string, "error", &(char*){""});

    return r;
}

int compare_zb_activity_data_response(const ZBActivityDataResponse* response1, const ZBActivityDataResponse* response2) {
    return response1->found == response2->found &&
        response1->active_in_days == response2->active_in_days &&
        strcmp(response1->error, response2->error) == 0;
}
