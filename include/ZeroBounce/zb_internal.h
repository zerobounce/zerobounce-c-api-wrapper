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
/*
 * A request with an explicit method and an optional body. Needed by the Inbox
 * Placement Tester, which uses PUT and DELETE and sends JSON rather than a
 * form body.
 */
int zb_make_request_with_method_and_body(
    char* url_path,
    const char* method,
    const char* body,
    const char* content_type_header,
    memory* response_data,
    long* http_code,
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
void zb_note_http_request(const char *method, const char *url, const char *body);
void zb_note_http_field(const char *name, const char *value);
int zb_http_perform_if_mock(memory *response_data, long *http_code, char **content_type_out);

char *zb_message_from_body(const char *body, long status_code);
bool zb_body_looks_like_html(const char *body);
void zb_report_body_error(const char *body, long status_code, OnErrorCallback error_callback);
/** Parses a 2xx non-HTML JSON body. On failure, reports a short error and returns false. */
bool zb_read_success_json(const char *body, long status_code, json_object **out, OnErrorCallback error_callback);
bool zb_batch_has_account_error(const json_object *batch, char **message_out);

#endif
