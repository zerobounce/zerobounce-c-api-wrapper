#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBScoreEmailResponse.h"
#include "ZeroBounce/utils.h"

ZBScoreEmailResponse new_zb_score_email_response() {
    ZBScoreEmailResponse response;
    response.email = "";
    response.score = 0;
    response.score_is_set = false;
    return response;
}

char* zb_score_email_response_to_string(ZBScoreEmailResponse* response) {
    const char *serialization = "ZBScoreEmailResponse{"
        "email='%s', "
        "score=%d, "
        "score_is_set=%d"
        "}";

    int size = snprintf(
        NULL, 0, serialization,
        response->email,
        response->score,
        response->score_is_set ? 1 : 0
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
        response->email,
        response->score,
        response->score_is_set ? 1 : 0
    );
    return buffer;
}

static bool parse_score_field(const json_object* j, int* score_out) {
    json_object* score_obj = NULL;
    if (!json_object_object_get_ex(j, "score", &score_obj) || score_obj == NULL) {
        return false;
    }
    if (json_object_is_type(score_obj, json_type_int)) {
        *score_out = json_object_get_int(score_obj);
        return true;
    }
    if (json_object_is_type(score_obj, json_type_string)) {
        *score_out = atoi(json_object_get_string(score_obj));
        return true;
    }
    return false;
}

ZBScoreEmailResponse zb_score_email_response_from_json(const json_object* j) {
    ZBScoreEmailResponse r = new_zb_score_email_response();

    r.email = *(char**)get_json_value(j, json_type_string, "email", &(char*){""});
    int score = 0;
    if (parse_score_field(j, &score)) {
        r.score = score;
        r.score_is_set = true;
    }

    return r;
}

int compare_zb_score_email_response(
    const ZBScoreEmailResponse* response1,
    const ZBScoreEmailResponse* response2
) {
    return strcmp(response1->email, response2->email) == 0 &&
        response1->score == response2->score &&
        response1->score_is_set == response2->score_is_set;
}
