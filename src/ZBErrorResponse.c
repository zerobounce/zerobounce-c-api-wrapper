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

void zb_report_body_error(const char *body, long status_code, OnErrorCallback error_callback) {
    char *message;
    if (!error_callback) {
        return;
    }
    message = zb_message_from_body(body, status_code);
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
    if (status_code > 299 || zb_body_looks_like_html(text) ||
        zb_skip_ws(text)[0] == '\0' || zb_success_means_failure(text)) {
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
