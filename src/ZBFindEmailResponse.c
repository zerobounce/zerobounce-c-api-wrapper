#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBFindEmailResponse.h"
#include "ZeroBounce/utils.h"

// ZBFindEmailResponse methods

ZBFindEmailResponse zb_find_email_response_init() {
    ZBFindEmailResponse response;
    response.email = NULL;
    response.email_confidence = NULL;
    response.domain = NULL;
    response.company_name = NULL;
    response.did_you_mean = NULL;
    response.failure_reason = NULL;
    return response;
}

char* zb_find_email_response_to_string(ZBFindEmailResponse* response) {
    const char* serialization = "ZBFindEmailResponse{"
        "email=\"%s\", "
        "email_confidence=\"%s\", "
        "domain=\"%s\", "
        "company_name=\"%s\", "
        "did_you_mean=\"%s\", "
        "failure_reason=\"%s\"}";

    int size = snprintf(
        NULL,
        0,
        serialization,
        response->email,
        response->email_confidence,
        response->domain,
        response->company_name,
        response->did_you_mean,
        response->failure_reason
    );
    if (size < 0) {
        return NULL;
    }

    char* buffer = (char*) malloc(size + 1);
    if (!buffer) {
        return NULL;
    }

    snprintf(
        buffer,
        size + 1,
        serialization,
        response->email,
        response->email_confidence,
        response->domain,
        response->company_name,
        response->did_you_mean,
        response->failure_reason
    );

    return buffer;
}

ZBFindEmailResponse zb_find_email_response_from_json(const json_object* j) {
    ZBFindEmailResponse response = zb_find_email_response_init();
    response.email = get_json_string_copy(j, "email", "");
    response.email_confidence = get_json_string_copy(j, "email_confidence", "");
    response.domain = get_json_string_copy(j, "domain", "");
    response.company_name = get_json_string_copy(j, "company_name", "");
    response.did_you_mean = get_json_string_copy(j, "did_you_mean", "");
    response.failure_reason = get_json_string_copy(j, "failure_reason", "");

    return response;
}

void zb_find_email_response_free(ZBFindEmailResponse* instance) {
    free(instance->email);
    free(instance->email_confidence);
    free(instance->domain);
    free(instance->company_name);
    free(instance->did_you_mean);
    free(instance->failure_reason);
    instance->email = NULL;
    instance->email_confidence = NULL;
    instance->domain = NULL;
    instance->company_name = NULL;
    instance->did_you_mean = NULL;
    instance->failure_reason = NULL;
}

int zb_find_email_response_compare(ZBFindEmailResponse* instance1, ZBFindEmailResponse* instance2) {
    return
        strcmp(instance1->email, instance2->email) == 0 &&
        strcmp(instance1->email_confidence, instance2->email_confidence) == 0 &&
        strcmp(instance1->domain, instance2->domain) == 0 &&
        strcmp(instance1->company_name, instance2->company_name) == 0 &&
        strcmp(instance1->did_you_mean, instance2->did_you_mean) == 0 &&
        strcmp(instance1->failure_reason, instance2->failure_reason) == 0;
}
