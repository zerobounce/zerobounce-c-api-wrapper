#ifndef ZB_INTERNAL_H
#define ZB_INTERNAL_H

#include "ZeroBounce/ZeroBounce.h"

bool zero_bounce_invalid_api_key(ZeroBounce *zb, OnErrorCallback error_callback);
char *zb_url_escape(const char *value);
int zb_make_request(
    char *url_path,
    char *request_type,
    char *header,
    memory *response_data,
    long *http_code,
    OnErrorCallback error_callback
);
int zb_make_request_with_post_body(
    char *url_path,
    char *body,
    memory *response_data,
    long *http_code,
    OnErrorCallback error_callback
);
void zero_bounce_record_request_url_for_test(const char *url);
int zb_http_perform_if_mock(memory *response_data, long *http_code, char **content_type_out);

#endif
