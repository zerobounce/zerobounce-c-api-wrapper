#ifndef ZB_INTERNAL_H
#define ZB_INTERNAL_H

#include "ZeroBounce/ZeroBounce.h"

bool zero_bounce_invalid_api_key(ZeroBounce *zb, OnErrorCallback error_callback);
/* Whether a key is free of surrounding whitespace and control characters. */
bool zb_api_key_is_clean(const char *api_key);
/*
 * Refuses a URL that is not https, and warns once when a regional base URL is
 * paired with the single global bulk host.
 */
bool zb_check_request_url(const char *url, OnErrorCallback error_callback);
/* Replaces the value of any api_key parameter in a message with REDACTED. */
char *zb_redact_api_key(const char *text);
char *zb_url_escape(const char *value);
/* A copy of a base URL without its trailing slashes. The caller owns it. */
char *zb_normalized_base_url(const char *url);
/* The timeouts, no-redirect and https-only options every request gets. */
void zb_set_transport_options(CURL *curl);
/*
 * Test hook: request timeouts in milliseconds (0 restores 120 s and 30 s), and
 * whether http://127.0.0.1:<port> and http://localhost:<port> are allowed, so
 * a local test server can stand in for the API.
 */
void zero_bounce_set_transport_for_test(long timeout_ms, long connect_timeout_ms, bool allow_loopback_http);
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

/*
 * As zb_make_request_with_method_and_body, plus one extra request header.
 * The Inbox Placement Tester needs Authorization: Bearer, and the Warmup API
 * an API-Key header.
 */
int zb_make_request_with_extra_header(
    char* url_path,
    const char* method,
    const char* body,
    const char* content_type_header,
    const char* extra_header,
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
void zb_note_http_header(const char *header);
int zb_http_perform_if_mock(memory *response_data, long *http_code, char **content_type_out);

char *zb_message_from_body(const char *body, long status_code);
bool zb_body_looks_like_html(const char *body);
void zb_report_body_error(const char *body, long status_code, OnErrorCallback error_callback);
/** Parses a 2xx non-HTML JSON body. On failure, reports a short error and returns false. */
bool zb_read_success_json(const char *body, long status_code, json_object **out, OnErrorCallback error_callback);
/**
 * Whether a 2xx JSON body is an error with no "success" field to gate on: a
 * non-empty "error", "error_message" or "Error" string. LIVE: an invalid or
 * drained key answers validate, activity and getapiusage with {"error": ...},
 * and the filters with {"Error": ...}, on an HTTP 200.
 */
bool zb_error_only_body(const char *body);
/**
 * zb_read_success_json for a call whose result is never a bare message: the
 * email finder and domain search. A body made only of message fields is an
 * error. Not for validate, which answers a repeated greylist_processing request
 * with {"Message": "Greylist validation still in progress..."}.
 */
bool zb_read_non_message_json(const char *body, long status_code, json_object **out, OnErrorCallback error_callback);
bool zb_batch_has_account_error(const json_object *batch, char **message_out);

#endif
