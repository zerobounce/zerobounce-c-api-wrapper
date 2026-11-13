#include <stdio.h>

#include "ZeroBounce/ZBCreditsResponse.h"
#include "ZeroBounce/utils.h"
#include "ZeroBounce/zb_internal.h"

#include <errno.h>
#include <limits.h>

ZBCreditsResponse new_zb_credits_response() {
    ZBCreditsResponse response;

    response.credits = -1;

    return response;
}

char* zb_credits_response_to_string(ZBCreditsResponse* response) {
    const char *serialization = "ZBCreditsResponse{"
        "credits='%d'"
        "}";

    int size = snprintf(
        NULL, 0, serialization,
        response->credits
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
        response->credits
    );
    return buffer;
}

ZBCreditsResponse zb_credits_response_from_json(const json_object* j) {
    ZBCreditsResponse r = new_zb_credits_response();

    /* E7: Credits is an integer, sent as a JSON string ("-1" for a dead key,
       "12345") or as a JSON integer ({"Credits":2375323}, zerobounce.net's
       own example). Missing, null, another JSON type (a fraction, a
       boolean), text that is not an integer, or a value outside int is not
       an answer, so it is noted for the caller as a shape error. */
    json_object* value = NULL;
    const char* text;
    char* end = NULL;
    long long credits;
    if (!json_object_object_get_ex(j, "Credits", &value) || value == NULL) {
        zb_shape_mismatch("Credits");
        return r;
    }
    if (json_object_is_type(value, json_type_int)) {
        /* json-c holds integers as int64 (or uint64 above INT64_MAX, which
           json_object_get_int64 clamps to INT64_MAX): both out of range. */
        credits = (long long)json_object_get_int64(value);
        if (credits < INT_MIN || credits > INT_MAX) {
            zb_shape_mismatch("Credits");
            return r;
        }
        r.credits = (int)credits;
        return r;
    }
    if (!json_object_is_type(value, json_type_string)) {
        zb_shape_mismatch("Credits");
        return r;
    }
    text = json_object_get_string(value);
    errno = 0;
    credits = strtoll(text, &end, 10);
    if (text[0] == '\0' || !(text[0] == '-' || (text[0] >= '0' && text[0] <= '9')) ||
        *end != '\0' || errno == ERANGE || credits < INT_MIN || credits > INT_MAX) {
        zb_shape_mismatch("Credits");
        return r;
    }
    r.credits = (int)credits;

    return r;
}

int compare_zb_credits_response(const ZBCreditsResponse* response1, const ZBCreditsResponse* response2) {
    return response1->credits == response2->credits;
}
