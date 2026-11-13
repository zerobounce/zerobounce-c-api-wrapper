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

/*
 * Whether the response the current call on this thread just got was labelled
 * text/html, noted where every request reads its status (and by the test
 * hook), so zb_read_success_json can refuse an HTML page whatever its body.
 */
static _Thread_local bool zb_tls_html_response = false;

void zb_note_response_content_type(const char *content_type) {
    zb_tls_html_response = zb_content_type_is_html(content_type);
}

bool zb_response_was_html(void) {
    return zb_tls_html_response;
}

/* Replaces every occurrence of secret in text with mask; frees text when it
   returns a new string. */
static char *zb_replace_secret(char *text, const char *secret, const char *mask) {
    size_t mask_len = strlen(mask);
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
    out = malloc(strlen(text) + count * mask_len + 1);
    if (!out) {
        return text;
    }
    dst = out;
    src = text;
    while ((at = strstr(src, secret)) != NULL) {
        memcpy(dst, src, (size_t)(at - src));
        dst += at - src;
        memcpy(dst, mask, mask_len);
        dst += mask_len;
        src = at + secret_len;
    }
    strcpy(dst, src);
    free(text);
    return out;
}

/*
 * secret as it appears inside a JSON string: '"' and '\\' escaped, and '/'
 * escaped too when escape_slash is set (both are valid JSON). The caller
 * owns it.
 */
static char *zb_json_escape(const char *secret, bool escape_slash) {
    char *out = malloc(strlen(secret) * 2 + 1);
    char *dst = out;
    if (!out) {
        return NULL;
    }
    for (; *secret; secret++) {
        if (*secret == '"' || *secret == '\\' || (escape_slash && *secret == '/')) {
            *dst++ = '\\';
        }
        *dst++ = *secret;
    }
    *dst = '\0';
    return out;
}

/* A URL-encoded string with every %20 written as '+', as a form body has it.
   NULL when there is no space to rewrite. The caller owns it. */
static char *zb_plus_for_spaces(const char *escaped) {
    char *out;
    char *dst;
    if (!escaped || !strstr(escaped, "%20")) {
        return NULL;
    }
    out = malloc(strlen(escaped) + 1);
    if (!out) {
        return NULL;
    }
    for (dst = out; *escaped;) {
        if (strncmp(escaped, "%20", 3) == 0) {
            *dst++ = '+';
            escaped += 3;
        } else {
            *dst++ = *escaped++;
        }
    }
    *dst = '\0';
    return out;
}

static int zb_hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* text (length bytes) URL-decoded: each %XX as its byte, and '+' as a space
   when plus_as_space is set (a form body). A %00 decodes to a NUL byte, so
   the view is a byte string with its length in *out_length, not a C string:
   a NUL must not hide what follows it from the check. The caller owns it. */
static char *zb_url_decode_view(const char *text, size_t length, bool plus_as_space, size_t *out_length) {
    char *out = malloc(length + 1);
    char *dst = out;
    const char *end = text + length;
    if (!out) {
        return NULL;
    }
    while (text < end) {
        int hi;
        int lo;
        if (text[0] == '%' && end - text >= 3 && (hi = zb_hex_value(text[1])) >= 0 &&
            (lo = zb_hex_value(text[2])) >= 0) {
            *dst++ = (char)(hi * 16 + lo);
            text += 3;
        } else if (plus_as_space && *text == '+') {
            *dst++ = ' ';
            text++;
        } else {
            *dst++ = *text++;
        }
    }
    *dst = '\0';
    *out_length = (size_t)(dst - out);
    return out;
}

static char *zb_put_utf8(char *dst, unsigned long cp) {
    if (cp < 0x80) {
        *dst++ = (char)cp;
    } else if (cp < 0x800) {
        *dst++ = (char)(0xC0 | (cp >> 6));
        *dst++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *dst++ = (char)(0xE0 | (cp >> 12));
        *dst++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *dst++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *dst++ = (char)(0xF0 | (cp >> 18));
        *dst++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *dst++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *dst++ = (char)(0x80 | (cp & 0x3F));
    }
    return dst;
}

/* The value of the four hex digits at text, or -1; text has at least
   available bytes. */
static long zb_hex4(const char *text, size_t available) {
    long value = 0;
    int i;
    if (available < 4) {
        return -1;
    }
    for (i = 0; i < 4; i++) {
        int digit = zb_hex_value(text[i]);
        if (digit < 0) {
            return -1;
        }
        value = value * 16 + digit;
    }
    return value;
}

/* text (length bytes) with JSON string escapes undone (\" \\ \/ \b \f \n \r
   \t \uXXXX, surrogate pairs included); anything else is kept. A \u0000
   decodes to a NUL byte, so the view's length is in *out_length. The caller
   owns it. */
static char *zb_json_unescape_view(const char *text, size_t length, size_t *out_length) {
    /* \uXXXX (6 bytes) becomes at most 4, so the text never grows. */
    char *out = malloc(length + 1);
    char *dst = out;
    const char *end = text + length;
    if (!out) {
        return NULL;
    }
    while (text < end) {
        long cp;
        if (text[0] != '\\' || end - text < 2) {
            *dst++ = *text++;
            continue;
        }
        switch (text[1]) {
        case '"': case '\\': case '/':
            *dst++ = text[1];
            text += 2;
            continue;
        case 'b': *dst++ = '\b'; text += 2; continue;
        case 'f': *dst++ = '\f'; text += 2; continue;
        case 'n': *dst++ = '\n'; text += 2; continue;
        case 'r': *dst++ = '\r'; text += 2; continue;
        case 't': *dst++ = '\t'; text += 2; continue;
        case 'u':
            cp = zb_hex4(text + 2, (size_t)(end - text) - 2);
            if (cp < 0) {
                break;
            }
            text += 6;
            if (cp >= 0xD800 && cp <= 0xDBFF && end - text >= 2 && text[0] == '\\' && text[1] == 'u') {
                long low = zb_hex4(text + 2, (size_t)(end - text) - 2);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    text += 6;
                }
            }
            dst = zb_put_utf8(dst, (unsigned long)cp);
            continue;
        default:
            break;
        }
        *dst++ = *text++;
    }
    *dst = '\0';
    *out_length = (size_t)(dst - out);
    return out;
}

/* Whether needle (a C string) occurs in the length bytes at text, NUL bytes
   in text included. */
static bool zb_bytes_contain(const char *text, size_t length, const char *needle) {
    size_t needle_length = strlen(needle);
    size_t i;
    if (needle_length == 0) {
        return true;
    }
    for (i = 0; i + needle_length <= length; i++) {
        if (text[i] == needle[0] && memcmp(text + i, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

/*
 * Whether secret can be read in text (length bytes) or in any view of it
 * decoded up to layers more times: URL-decoded (%XX), form-decoded ('+' as a
 * space too) and JSON-unescaped, in any order. A view that decodes to itself
 * is not followed. Views are byte strings, so a decoded NUL does not end the
 * search.
 */
static bool zb_secret_in_decoded_views(const char *text, size_t length, const char *secret, int layers) {
    char *views[3];
    size_t lengths[3] = {0, 0, 0};
    bool found = false;
    int i;
    if (zb_bytes_contain(text, length, secret)) {
        return true;
    }
    if (layers == 0) {
        return false;
    }
    views[0] = zb_url_decode_view(text, length, false, &lengths[0]);
    views[1] = zb_url_decode_view(text, length, true, &lengths[1]);
    views[2] = zb_json_unescape_view(text, length, &lengths[2]);
    for (i = 0; i < 3; i++) {
        if (!views[i]) {
            /* Out of memory: assume the worst. */
            found = true;
        } else if (!found && (lengths[i] != length || memcmp(views[i], text, length) != 0)) {
            found = zb_secret_in_decoded_views(views[i], lengths[i], secret, layers - 1);
        }
        free(views[i]);
    }
    return found;
}

/*
 * text with every spelling of secret zb_mask_secret generates replaced by
 * mask, then checked by decoding (standard: redaction is closed under
 * encoding). When any view of what is left, decoded up to ZB_SECRET_DECODE_LAYERS
 * layers, still holds the secret, the text is dropped for
 * ZB_SECRET_OMITTED_MESSAGE.
 */
static char *zb_mask_known_spellings(char *text, const char *secret, const char *mask);

char *zb_mask_secret(char *text, const char *secret, const char *mask) {
    if (!text || !secret || !secret[0]) {
        return text;
    }
    text = zb_mask_known_spellings(text, secret, mask);
    if (zb_secret_in_decoded_views(text, strlen(text), secret, ZB_SECRET_DECODE_LAYERS)) {
        char *omitted = strdup(ZB_SECRET_OMITTED_MESSAGE);
        if (omitted) {
            free(text);
            return omitted;
        }
        /* Out of memory: never hand the secret on. */
        text[0] = '\0';
    }
    return text;
}

static char *zb_mask_known_spellings(char *text, const char *secret, const char *mask) {
    enum { FORMS = 10 };
    char *forms[FORMS] = {0};
    char *json_slash;
    char *json_plain;
    size_t i;
    if (!text || !secret || !secret[0]) {
        return text;
    }
    /* A body may quote the secret raw, URL-encoded (a request URL or form,
       spaces as %20 or '+'), JSON-escaped (a request body echoed back; json-c
       escapes '/', other encoders do not), JSON-escaped twice (an echoed body
       quoted inside another JSON string), or URL-encoded after being
       JSON-escaped (an echoed JSON body put into a URL or form). The longer,
       escaped forms are replaced first, as they can contain the raw one. */
    json_slash = zb_json_escape(secret, true);
    json_plain = zb_json_escape(secret, false);
    forms[0] = json_slash ? zb_json_escape(json_slash, true) : NULL;
    forms[1] = json_plain ? zb_json_escape(json_plain, false) : NULL;
    forms[2] = zb_url_escape(json_slash);
    forms[3] = zb_plus_for_spaces(forms[2]);
    forms[4] = zb_url_escape(json_plain);
    forms[5] = zb_plus_for_spaces(forms[4]);
    forms[6] = json_slash;
    forms[7] = json_plain;
    forms[8] = zb_url_escape(secret);
    forms[9] = zb_plus_for_spaces(forms[8]);
    for (i = 0; i < FORMS; i++) {
        if (forms[i]) {
            text = zb_replace_secret(text, forms[i], mask);
            free(forms[i]);
        }
    }
    return zb_replace_secret(text, secret, mask);
}

/*
 * A copy of text with any api_key parameter's value and the current call's
 * key, raw, URL-encoded and JSON-escaped, replaced. The caller owns it.
 */
static char *zb_redact_error_text(const char *text) {
    char *out = zb_redact_api_key(text);
    if (out && zb_tls_api_key) {
        out = zb_mask_secret(out, zb_tls_api_key, "REDACTED");
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

bool zb_content_type_is_html(const char *content_type) {
    static const char html[] = "text/html";
    const char *p;
    size_t i;
    if (!content_type) {
        return false;
    }
    for (p = content_type; *p; p++) {
        for (i = 0; html[i] && p[i] && tolower((unsigned char)p[i]) == html[i]; i++) {
        }
        if (!html[i]) {
            return true;
        }
    }
    return false;
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
        json_object *parsed = zb_parse_json_whole(body);
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

/*
 * A strict RFC 8259 check. json-c's tokener is lenient: it takes single
 * quotes, trailing commas, NaN and Infinity, TRUE, leading zeros, raw control
 * characters in strings and comments, so a proxy's or a portal's almost-JSON
 * would pass as the API's answer. Only space, tab, CR and LF may surround the
 * value; anything else (a BOM, a vertical tab, a no-break space) is not JSON.
 */
typedef struct {
    const char *p;
    const char *end;
} ZBJsonScan;

#define ZB_JSON_MAX_DEPTH 512

static void zb_json_ws(ZBJsonScan *s) {
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' || *s->p == '\n' || *s->p == '\r')) {
        s->p++;
    }
}

static bool zb_json_digit(ZBJsonScan *s) {
    return s->p < s->end && *s->p >= '0' && *s->p <= '9';
}

static bool zb_json_digits(ZBJsonScan *s) {
    if (!zb_json_digit(s)) {
        return false;
    }
    while (zb_json_digit(s)) {
        s->p++;
    }
    return true;
}

static bool zb_json_number(ZBJsonScan *s) {
    if (s->p < s->end && *s->p == '-') {
        s->p++;
    }
    if (s->p < s->end && *s->p == '0') {
        s->p++;
    } else if (!zb_json_digits(s)) {
        return false;
    }
    if (s->p < s->end && *s->p == '.') {
        s->p++;
        if (!zb_json_digits(s)) {
            return false;
        }
    }
    if (s->p < s->end && (*s->p == 'e' || *s->p == 'E')) {
        s->p++;
        if (s->p < s->end && (*s->p == '+' || *s->p == '-')) {
            s->p++;
        }
        if (!zb_json_digits(s)) {
            return false;
        }
    }
    return true;
}

static bool zb_json_string(ZBJsonScan *s) {
    if (s->p >= s->end || *s->p != '"') {
        return false;
    }
    s->p++;
    while (s->p < s->end) {
        unsigned char c = (unsigned char)*s->p++;
        if (c == '"') {
            return true;
        }
        if (c < 0x20) {
            return false;
        }
        if (c == '\\') {
            int i;
            if (s->p >= s->end) {
                return false;
            }
            c = (unsigned char)*s->p++;
            if (c == 'u') {
                for (i = 0; i < 4; i++) {
                    if (s->p >= s->end || !isxdigit((unsigned char)*s->p)) {
                        return false;
                    }
                    s->p++;
                }
            } else if (c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' &&
                       c != 'n' && c != 'r' && c != 't') {
                return false;
            }
        }
    }
    return false;
}

static bool zb_json_literal(ZBJsonScan *s, const char *word) {
    size_t n = strlen(word);
    if ((size_t)(s->end - s->p) < n || memcmp(s->p, word, n) != 0) {
        return false;
    }
    s->p += n;
    return true;
}

static bool zb_json_value(ZBJsonScan *s, int depth) {
    if (s->p >= s->end || depth > ZB_JSON_MAX_DEPTH) {
        return false;
    }
    switch (*s->p) {
    case '{':
        s->p++;
        zb_json_ws(s);
        if (s->p < s->end && *s->p == '}') {
            s->p++;
            return true;
        }
        for (;;) {
            if (!zb_json_string(s)) {
                return false;
            }
            zb_json_ws(s);
            if (s->p >= s->end || *s->p != ':') {
                return false;
            }
            s->p++;
            zb_json_ws(s);
            if (!zb_json_value(s, depth + 1)) {
                return false;
            }
            zb_json_ws(s);
            if (s->p < s->end && *s->p == ',') {
                s->p++;
                zb_json_ws(s);
                continue;
            }
            if (s->p < s->end && *s->p == '}') {
                s->p++;
                return true;
            }
            return false;
        }
    case '[':
        s->p++;
        zb_json_ws(s);
        if (s->p < s->end && *s->p == ']') {
            s->p++;
            return true;
        }
        for (;;) {
            zb_json_ws(s);
            if (!zb_json_value(s, depth + 1)) {
                return false;
            }
            zb_json_ws(s);
            if (s->p < s->end && *s->p == ',') {
                s->p++;
                continue;
            }
            if (s->p < s->end && *s->p == ']') {
                s->p++;
                return true;
            }
            return false;
        }
    case '"':
        return zb_json_string(s);
    case 't':
        return zb_json_literal(s, "true");
    case 'f':
        return zb_json_literal(s, "false");
    case 'n':
        return zb_json_literal(s, "null");
    default:
        return zb_json_number(s);
    }
}

bool zb_json_is_strict(const char *text, size_t length) {
    ZBJsonScan s;
    if (!text) {
        return false;
    }
    s.p = text;
    s.end = text + length;
    zb_json_ws(&s);
    if (!zb_json_value(&s, 0)) {
        return false;
    }
    zb_json_ws(&s);
    return s.p == s.end;
}

/*
 * Whether the body the current call on this thread just got held a NUL byte.
 * Bodies travel as C strings, so one would end the text early and hide
 * whatever came after it from the check above.
 */
static _Thread_local bool zb_tls_body_has_nul = false;

void zb_note_response_bytes(const char *data, size_t size, bool first) {
    if (first) {
        zb_tls_body_has_nul = false;
    }
    if (data && size && memchr(data, '\0', size)) {
        zb_tls_body_has_nul = true;
    }
}

bool zb_body_is_strict_json(const char *text) {
    return text && !zb_tls_body_has_nul && zb_json_is_strict(text, strlen(text));
}

/*
 * The JSON value text holds, or NULL when it is not exactly one strict
 * RFC 8259 JSON value (zb_json_is_strict): json_tokener_parse stops after the
 * first value and ignores what follows, so {"a":1} followed by a proxy's text
 * would pass as JSON, and it takes almost-JSON besides. Space, tab, CR and LF
 * may surround the value.
 */
json_object *zb_parse_json_whole(const char *text) {
    json_tokener *tok;
    json_object *parsed;
    const char *rest;
    size_t length;
    text = text ? text : "";
    if (!zb_body_is_strict_json(text)) {
        return NULL;
    }
    text = zb_skip_ws(text);
    tok = json_tokener_new();
    if (!tok) {
        return NULL;
    }
    /* The terminating NUL is passed too, so a bare number (0) ends there
       rather than waiting for more input. */
    length = strlen(text);
    parsed = json_tokener_parse_ex(tok, text, (int)length + 1);
    if (parsed && json_tokener_get_error(tok) == json_tokener_success) {
        size_t end = json_tokener_get_parse_end(tok);
        rest = zb_skip_ws(end < length ? text + end : "");
        if (rest[0] != '\0') {
            json_object_put(parsed);
            parsed = NULL;
        }
    } else if (parsed) {
        json_object_put(parsed);
        parsed = NULL;
    }
    json_tokener_free(tok);
    return parsed;
}

bool zb_read_success_json(const char *body, long status_code, json_object **out, OnErrorCallback error_callback) {
    const char *text = body ? body : "";
    json_object *parsed;
    if (!out) {
        return false;
    }
    *out = NULL;
    /* A typed call decodes what this returns next; start its field-type
       check afresh. */
    zb_shape_reset();
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
    /* A text/html page is a proxy's or a portal's, not the API's answer, even
       when its body happens to parse as JSON. */
    if (status_code > 299 || zb_body_looks_like_html(text) || zb_response_was_html() ||
        zb_skip_ws(text)[0] == '\0' || zb_success_means_failure(text) || zb_error_only_body(text)) {
        zb_report_body_error(text, status_code, error_callback);
        return false;
    }
    parsed = zb_parse_json_whole(text);
    /* Every typed call reads a JSON object. Valid JSON of another shape (0,
       "x", [], true) is not the API's answer either, so it is an error with
       its status rather than a result filled with defaults. */
    if (!parsed || !json_object_is_type(parsed, json_type_object)) {
        if (parsed) {
            json_object_put(parsed);
        }
        zb_report_body_error(text, status_code, error_callback);
        return false;
    }
    *out = parsed;
    return true;
}

bool zb_shape_error_reported(long status_code, OnErrorCallback error_callback) {
    const char *field = zb_shape_error_field();
    char message[160];
    if (!field) {
        return false;
    }
    /* E7: valid JSON whose field has the wrong type (or a required field is
       missing) is not the API's answer: an error with the status, never a
       result filled with defaults. */
    snprintf(message, sizeof(message),
             "Unexpected JSON response: field \"%s\" is missing or has the wrong type.", field);
    zb_shape_reset();
    if (error_callback) {
        error_callback(parse_error_with_status(message, status_code));
    }
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
