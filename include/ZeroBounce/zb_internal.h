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
/*
 * Records the key of the client making the current call on this thread (NULL
 * forgets it). parse_error_with_status replaces it, raw and URL-encoded, in
 * every error it builds, as it does any api_key parameter's value.
 */
void zb_set_redaction_key(const char *api_key);
char *zb_url_escape(const char *value);
/* What an error says in place of a body that still held a secret. */
#define ZB_SECRET_OMITTED_MESSAGE \
    "The response body was omitted because it contained a credential."
/*
 * text with every raw, URL-encoded and JSON-escaped occurrence of secret
 * replaced by mask. When a view of the result decoded up to three times
 * (URL-decoding, '+' as a space, JSON-unescaping, in any order) still holds
 * the secret, the whole text is replaced by ZB_SECRET_OMITTED_MESSAGE. Takes
 * ownership of text and returns the string to use.
 */
char *zb_mask_secret(char *text, const char *secret, const char *mask);
/*
 * Notes the content type of the response the current call on this thread just
 * got (NULL before a request); zb_read_success_json refuses a text/html one.
 */
void zb_note_response_content_type(const char *content_type);
/* Whether that response was labelled text/html. */
bool zb_response_was_html(void);
/* zb_write_file_replacing results besides 0 (written). */
#define ZB_WRITE_OPEN_FAILED (-1)
#define ZB_WRITE_FAILED (-2)
/*
 * Writes data to a temporary file next to path and moves it over path only
 * when the whole of it was written. On failure the temporary file is removed
 * and a file already at path is left as it was.
 */
int zb_write_file_replacing(const char *path, const char *data, size_t size);
/* A temporary file name next to path, unique to owner. The caller frees it. */
char *zb_temp_path_for(const char *path, const void *owner);
/* Moves temp_path over path, replacing what was there. */
bool zb_replace_file(const char *temp_path, const char *path);
/* A copy of a base URL without its trailing slashes. The caller owns it. */
char *zb_normalized_base_url(const char *url);
/* The timeouts, no-redirect and https-only options every request gets. */
void zb_set_transport_options(CURL *curl);
/* The total and connect timeouts zb_set_transport_options sets, in ms. */
void zb_transport_timeouts(long *timeout_ms, long *connect_timeout_ms);
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

/*
 * As zb_make_request_with_extra_header, and hands back the response's
 * Content-Type (malloc'd, or NULL when it had none) through content_type_out.
 * The Warmup CSV downloads need it to tell a file from an error.
 */
int zb_make_request_with_extra_header_ct(
    char* url_path,
    const char* method,
    const char* body,
    const char* content_type_header,
    const char* extra_header,
    memory* response_data,
    long* http_code,
    char** content_type_out,
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
/* Whether a Content-Type names an HTML page (text/html), in any letter case. */
bool zb_content_type_is_html(const char *content_type);
void zb_report_body_error(const char *body, long status_code, OnErrorCallback error_callback);
/**
 * Whether text[0..length) is exactly one strict RFC 8259 JSON value, with
 * only space, tab, CR and LF around it.
 */
bool zb_json_is_strict(const char *text, size_t length);
/**
 * Notes bytes of the response the current call on this thread is reading
 * (first starts a new one), so a NUL byte in a body, which would end it early
 * as a C string, makes zb_parse_json_whole refuse it.
 */
void zb_note_response_bytes(const char *data, size_t size, bool first);
/** Whether a response body is one strict JSON value and held no NUL byte. */
bool zb_body_is_strict_json(const char *text);
/** The one strict JSON value text holds (zb_body_is_strict_json), or NULL. */
json_object *zb_parse_json_whole(const char *text);
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
/**
 * Whether a download's body is the API saying why there is no file: a JSON
 * content type, or a body that is a JSON object (a result file never starts
 * with "{"), whatever its keys. content_type may be NULL.
 */
bool zb_download_body_is_json(const char *body, const char *content_type);

#endif
