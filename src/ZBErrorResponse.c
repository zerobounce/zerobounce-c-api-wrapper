#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ZeroBounce/zb_internal.h"

ZBErrorResponse new_zb_error_response() {
    ZBErrorResponse response;

    response.success = false;
    response.errors = string_vector_init();
    response.status_code = 0;

    return response;
}

char* zb_error_response_to_string(ZBErrorResponse* response) {
    const char* serialization = "ZBErrorResponse{success=%d, errors=[%s]}";
    char* errors = concatenate_strings(&response->errors, ", ");
    int size = snprintf(NULL, 0, serialization, response->success, errors);

    if (size < 0) {
        return NULL;
    }

    char* buffer = (char*) malloc(size + 1);
    if (!buffer) {
        return NULL;
    }

    snprintf(buffer, size + 1, serialization, response->success, errors);
    free(errors);
    return buffer;
}

ZBErrorResponse parse_error(const char* error) {
    return parse_error_with_status(error, 0);
}

/*
 * The API key of the client making the current call on this thread, recorded
 * by the key check every public call runs first (zb_set_redaction_key), so that
 * every error built below has it taken out without each path carrying the
 * client around. Kept until the next call on the thread replaces it.
 */
static _Thread_local char *zb_tls_api_key = NULL;

void zb_set_redaction_key(const char *api_key) {
    if (zb_tls_api_key && api_key && strcmp(zb_tls_api_key, api_key) == 0) {
        return;
    }
    free(zb_tls_api_key);
    zb_tls_api_key = (api_key && api_key[0]) ? strdup(api_key) : NULL;
}

/* Replaces every occurrence of secret in text, in place, with REDACTED. */
static char *zb_replace_secret(char *text, const char *secret) {
    static const char mask[] = "REDACTED";
    size_t secret_len;
    size_t count = 0;
    const char *at;
    char *out;
    char *dst;
    const char *src;

    if (!text || !secret || !secret[0]) {
        return text;
    }
    secret_len = strlen(secret);
    for (at = strstr(text, secret); at; at = strstr(at + secret_len, secret)) {
        count++;
    }
    if (count == 0) {
        return text;
    }
    out = malloc(strlen(text) + count * (sizeof(mask) - 1) + 1);
    if (!out) {
        return text;
    }
    dst = out;
    src = text;
    while ((at = strstr(src, secret)) != NULL) {
        memcpy(dst, src, (size_t)(at - src));
        dst += at - src;
        memcpy(dst, mask, sizeof(mask) - 1);
        dst += sizeof(mask) - 1;
        src = at + secret_len;
    }
    strcpy(dst, src);
    free(text);
    return out;
}

/*
 * A copy of text with any api_key parameter's value and the current call's
 * key, raw and URL-encoded, replaced. The caller owns it.
 */
static char *zb_redact_error_text(const char *text) {
    char *out = zb_redact_api_key(text);
    if (out && zb_tls_api_key) {
        char *escaped = zb_url_escape(zb_tls_api_key);
        out = zb_replace_secret(out, zb_tls_api_key);
        if (escaped && strcmp(escaped, zb_tls_api_key) != 0) {
            out = zb_replace_secret(out, escaped);
        }
        free(escaped);
    }
    return out;
}

ZBErrorResponse parse_error_with_status(const char* error, long status_code) {
    ZBErrorResponse response = new_zb_error_response();
    response.status_code = status_code;

    if (!error || strlen(error) == 0) {
        return response;
    }

    StringVector errors = string_vector_init();
    StringVector other_messages = string_vector_init();

    json_object* hash_map = json_tokener_parse(error);

    if (hash_map != NULL && json_object_is_type(hash_map, json_type_object)) {
        json_object_object_foreach(hash_map, key, val) {
            if (strstr(key, "error") != NULL || strstr(key, "message") != NULL) {
                if (json_object_is_type(val, json_type_array)) {
                    size_t array_length = json_object_array_length(val);
                    for (size_t i = 0; i < array_length; i++) {
                        json_object* item = json_object_array_get_idx(val, i);
                        if (json_object_is_type(item, json_type_string)) {
                            string_vector_append(&errors, json_object_get_string(item));
                        }
                    }
                } else if (!json_object_is_type(val, json_type_null)) {
                    string_vector_append(&errors, json_object_get_string(val));
                }
            } else {
                if (strcmp(key, "success") == 0 && json_object_is_type(val, json_type_boolean)) {
                    response.success = json_object_get_boolean(val);
                } else if (json_object_is_type(val, json_type_array)) {
                    size_t array_length = json_object_array_length(val);
                    for (size_t i = 0; i < array_length; i++) {
                        json_object* item = json_object_array_get_idx(val, i);
                        if (json_object_is_type(item, json_type_string)) {
                            string_vector_append(&other_messages, json_object_get_string(item));
                        }
                    }
                } else if (!json_object_is_type(val, json_type_null)) {
                    string_vector_append(&other_messages, json_object_get_string(val));
                }
            }
        }

        for (size_t i = 0; i < other_messages.size; i++) {
            string_vector_append(&errors, other_messages.data[i]);
        }
    } else {
        string_vector_append(&errors, error);
    }

    /* Every error a caller sees is built here, so this is where the key is
       taken out: a body may quote the request URL or the key itself. */
    for (size_t i = 0; i < errors.size; i++) {
        char *safe = zb_redact_error_text(errors.data[i]);
        if (safe) {
            free(errors.data[i]);
            errors.data[i] = safe;
        }
    }

    response.errors = errors;
    if (hash_map != NULL) {
        json_object_put(hash_map);
    }

    return response;
}

static const char *zb_skip_ws(const char *value) {
    if (!value) {
        return "";
    }
    while (*value && isspace((unsigned char)*value)) {
        value++;
    }
    return value;
}

static char *zb_dup(const char *value) {
    char *copy = strdup(value ? value : "");
    return copy ? copy : strdup("");
}

static int zb_starts_with(const char *value, const char *prefix) {
    size_t n = strlen(prefix);
    return strncmp(value, prefix, n) == 0;
}

static char *zb_strip_error_prefix(const char *value) {
    char *message = zb_dup(zb_skip_ws(value));
    if (!message) {
        return strdup("");
    }
    for (;;) {
        char *start = message;
        while (*start && isspace((unsigned char)*start)) {
            start++;
        }
        if (start != message) {
            memmove(message, start, strlen(start) + 1);
        }
        if (zb_starts_with(message, "Error:")) {
            memmove(message, message + 6, strlen(message + 6) + 1);
            continue;
        }
        if (zb_starts_with(message, "[Error]:")) {
            memmove(message, message + 8, strlen(message + 8) + 1);
            continue;
        }
        return message;
    }
}

static void zb_lower_prefix(const char *value, char *out, size_t out_len) {
    size_t i = 0;
    if (!value) {
        out[0] = '\0';
        return;
    }
    for (; value[i] && i + 1 < out_len && i < 2000; i++) {
        out[i] = (char)tolower((unsigned char)value[i]);
    }
    out[i] = '\0';
}

bool zb_body_looks_like_html(const char *body) {
    char lower[2001];
    zb_lower_prefix(body, lower, sizeof(lower));
    return strstr(lower, "<!doctype html") != NULL ||
        strstr(lower, "<html") != NULL ||
        strstr(lower, "<head") != NULL ||
        strstr(lower, "<body") != NULL;
}

static char *zb_html_title(const char *body) {
    char lower[8192];
    const char *start;
    const char *open_end;
    const char *close;
    size_t len;
    char *title;
    zb_lower_prefix(body, lower, sizeof(lower));
    start = strstr(lower, "<title");
    if (!start) {
        return NULL;
    }
    open_end = strchr(start, '>');
    if (!open_end) {
        return NULL;
    }
    close = strstr(open_end, "</title>");
    if (!close) {
        return NULL;
    }
    len = (size_t)(close - open_end - 1);
    title = (char *)malloc(len + 1);
    if (!title) {
        return NULL;
    }
    memcpy(title, body + (open_end - lower) + 1, len);
    title[len] = '\0';
    return title;
}

static bool zb_json_key_has_error_or_message(json_object *object) {
    json_object_object_foreach(object, key, val) {
        (void)val;
        if (strstr(key, "error") || strstr(key, "message")) {
            return true;
        }
    }
    return false;
}

char *zb_message_from_body(const char *body, long status_code) {
    const char *trimmed = zb_skip_ws(body);
    char status_buf[16];
    if (!trimmed[0]) {
        snprintf(status_buf, sizeof(status_buf), "%ld", status_code);
        char *empty = (char *)malloc(80);
        if (!empty) {
            return strdup("Empty response from ZeroBounce");
        }
        snprintf(empty, 80, "Empty response from ZeroBounce (HTTP %s)", status_buf);
        return empty;
    }
    if (zb_body_looks_like_html(body)) {
        char *title = zb_html_title(body);
        char *message;
        if (!title || !title[0]) {
            free(title);
            return strdup("ZeroBounce returned an HTML error page instead of an API response.");
        }
        message = (char *)malloc(strlen(title) + 96);
        if (!message) {
            free(title);
            return strdup("ZeroBounce returned an HTML error page instead of an API response.");
        }
        snprintf(
            message,
            strlen(title) + 96,
            "ZeroBounce returned an HTML error page (\"%s\") instead of an API response.",
            title
        );
        free(title);
        return message;
    }
    if (trimmed[0] == '{' || trimmed[0] == '[') {
        json_object *parsed = json_tokener_parse(trimmed);
        if (!parsed) {
            return strdup("ZeroBounce returned a response that is not valid JSON.");
        }
        if (json_object_is_type(parsed, json_type_object)) {
            json_object *title = json_object_object_get(parsed, "title");
            if (title && json_object_is_type(title, json_type_string)) {
                const char *title_text = json_object_get_string(title);
                json_object *cf = json_object_object_get(parsed, "cloudflare_error");
                bool access_denied = title_text &&
                    (strstr(title_text, "Access denied") || strstr(title_text, "1020"));
                bool cloudflare = cf && json_object_is_type(cf, json_type_boolean) && json_object_get_boolean(cf);
                if (access_denied && (cloudflare || !zb_json_key_has_error_or_message(parsed))) {
                    char *copy = zb_dup(title_text);
                    json_object_put(parsed);
                    return copy;
                }
            }
        }
        json_object_put(parsed);
        return zb_dup(trimmed);
    }
    if (strlen(trimmed) > 1000) {
        return strdup("ZeroBounce returned a response that is not valid JSON.");
    }
    {
        char *message = zb_strip_error_prefix(trimmed);
        if (!message || !message[0]) {
            free(message);
            snprintf(status_buf, sizeof(status_buf), "%ld", status_code);
            message = (char *)malloc(80);
            if (!message) {
                return strdup("Empty response from ZeroBounce");
            }
            snprintf(message, 80, "Empty response from ZeroBounce (HTTP %s)", status_buf);
        }
        return message;
    }
}

char *zb_redact_api_key(const char *text) {
    const char *src = text ? text : "";
    size_t len = strlen(src);
    char *out = malloc(len + 16);
    size_t i = 0;
    size_t j = 0;

    if (!out) {
        return NULL;
    }
    while (i < len) {
        if (strncmp(src + i, "api_key=", 8) == 0) {
            memcpy(out + j, "api_key=REDACTED", 16);
            j += 16;
            i += 8;
            /* Skip the value, up to the next separator. */
            while (i < len && src[i] != '&' && src[i] != ' ' && src[i] != '"') {
                i++;
            }
            continue;
        }
        out[j++] = src[i++];
    }
    out[j] = '\0';
    return out;
}

void zb_report_body_error(const char *body, long status_code, OnErrorCallback error_callback) {
    char *message;
    if (!error_callback) {
        return;
    }
    if (status_code >= 300 && status_code < 400) {
        /* FOLLOWLOCATION is off: a redirect could send the api_key to another
           host, so it is reported rather than followed. */
        error_callback(parse_error_with_status(
            "ZeroBounce returned a redirect, which this client does not follow.", status_code
        ));
        return;
    }
    message = zb_message_from_body(body, status_code);
    /* parse_error_with_status takes the key out. */
    error_callback(parse_error_with_status(message, status_code));
    free(message);
}

bool zb_success_means_failure(const char *body) {
    const char *text = zb_skip_ws(body ? body : "");
    json_object *parsed;
    json_object *success;
    bool failed = false;

    if (text[0] != '{') {
        return false;
    }
    parsed = json_tokener_parse(text);
    if (!parsed) {
        return false;
    }
    if (json_object_object_get_ex(parsed, "success", &success)) {
        if (json_object_is_type(success, json_type_boolean)) {
            failed = !json_object_get_boolean(success);
        } else if (json_object_is_type(success, json_type_string)) {
            const char *value = json_object_get_string(success);
            failed = value && (strcasecmp(value, "false") == 0);
        }
    }
    json_object_put(parsed);
    return failed;
}

static bool zb_nonempty_string_field(json_object *object, const char *key) {
    json_object *value = NULL;
    if (!json_object_object_get_ex(object, key, &value) || !json_object_is_type(value, json_type_string)) {
        return false;
    }
    return zb_skip_ws(json_object_get_string(value))[0] != '\0';
}

static json_object *zb_parse_object_without_success(const char *body) {
    const char *text = zb_skip_ws(body ? body : "");
    json_object *parsed;
    if (text[0] != '{') {
        return NULL;
    }
    parsed = json_tokener_parse(text);
    if (!parsed) {
        return NULL;
    }
    if (!json_object_is_type(parsed, json_type_object) || json_object_object_get_ex(parsed, "success", NULL)) {
        json_object_put(parsed);
        return NULL;
    }
    return parsed;
}

bool zb_error_only_body(const char *body) {
    static const char *const keys[] = {"error", "error_message", "Error"};
    json_object *parsed = zb_parse_object_without_success(body);
    bool found = false;
    size_t i;
    if (!parsed) {
        return false;
    }
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]) && !found; i++) {
        found = zb_nonempty_string_field(parsed, keys[i]);
    }
    json_object_put(parsed);
    return found;
}

/* An object with no "success" field whose keys are all message keys, at least
   one of them a non-empty string. */
static bool zb_message_only_body(const char *body) {
    static const char *const keys[] = {"error", "message", "error_message", "Message", "Error", "detail"};
    json_object *parsed = zb_parse_object_without_success(body);
    bool has_text = false;
    bool only_messages = true;
    if (!parsed) {
        return false;
    }
    json_object_object_foreach(parsed, key, value) {
        bool known = false;
        size_t i;
        (void)value;
        for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            if (strcmp(key, keys[i]) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            only_messages = false;
            break;
        }
        if (zb_nonempty_string_field(parsed, key)) {
            has_text = true;
        }
    }
    json_object_put(parsed);
    return only_messages && has_text;
}

bool zb_read_success_json(const char *body, long status_code, json_object **out, OnErrorCallback error_callback) {
    const char *text = body ? body : "";
    json_object *parsed;
    if (!out) {
        return false;
    }
    *out = NULL;
    /*
     * LIVE: a "success": false field on an HTTP 200 is a failure, not a
     * result. getfile and deletefile report an unknown or deleted file id
     * that way, and the caller should not have to inspect a flag to notice.
     */
    /*
     * LIVE: so is an error with no "success" field at all. An invalid or
     * drained key answers validate, activity and getapiusage with
     * {"error": ...} on an HTTP 200; parsed, that used to reach the success
     * callback as an empty result.
     */
    if (status_code > 299 || zb_body_looks_like_html(text) ||
        zb_skip_ws(text)[0] == '\0' || zb_success_means_failure(text) || zb_error_only_body(text)) {
        zb_report_body_error(text, status_code, error_callback);
        return false;
    }
    parsed = json_tokener_parse(text);
    if (!parsed) {
        zb_report_body_error(text, status_code, error_callback);
        return false;
    }
    *out = parsed;
    return true;
}

bool zb_batch_has_account_error(const json_object *batch, char **message_out) {
    json_object *errors = NULL;
    size_t i;
    size_t count;
    if (message_out) {
        *message_out = NULL;
    }
    if (!batch || !json_object_object_get_ex(batch, "errors", &errors) ||
        !json_object_is_type(errors, json_type_array)) {
        return false;
    }
    count = json_object_array_length(errors);
    for (i = 0; i < count; i++) {
        json_object *item = json_object_array_get_idx(errors, i);
        json_object *address = item ? json_object_object_get(item, "email_address") : NULL;
        const char *address_text = address && json_object_is_type(address, json_type_string)
            ? json_object_get_string(address) : NULL;
        if (address_text && strcmp(address_text, "all") == 0) {
            json_object *error = json_object_object_get(item, "error");
            const char *error_text = error && json_object_is_type(error, json_type_string)
                ? json_object_get_string(error) : NULL;
            if (message_out) {
                *message_out = zb_dup(
                    error_text && error_text[0]
                        ? error_text
                        : "Invalid API Key or your account ran out of credits"
                );
            }
            return true;
        }
    }
    return false;
}

int compare_zb_error_response(const ZBErrorResponse* response1, const ZBErrorResponse* response2) {
    if (response1->success != response2->success) {
        return 0;
    }

    if (compare_string_vector(&response1->errors, &response2->errors) != 1) {
        return 0;
    }

    return 1;
}

bool zb_read_non_message_json(const char *body, long status_code, json_object **out, OnErrorCallback error_callback) {
    /*
     * LIVE: guessformat answers an invalid or drained key with an HTTP 200 and
     * nothing but {"Message": "Invalid API key or your account ran out of
     * credits"}, which used to come back as an empty result.
     */
    if (status_code <= 299 && zb_message_only_body(body)) {
        if (out) {
            *out = NULL;
        }
        zb_report_body_error(body, status_code, error_callback);
        return false;
    }
    return zb_read_success_json(body, status_code, out, error_callback);
}
