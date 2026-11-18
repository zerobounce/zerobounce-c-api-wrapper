#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define ZB_STRICMP _stricmp
#define zb_strncasecmp _strnicmp
#else
#include <pthread.h>
#include <strings.h>
#define ZB_STRICMP strcasecmp
#define zb_strncasecmp strncasecmp
#endif

#include <json.h>

#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/zb_internal.h"

static ZBHttpPerformFn s_http_perform = NULL;
/* Timeouts for every request, and whether http to 127.0.0.1 is allowed. Only
   zero_bounce_set_transport_for_test changes them. */
#define ZB_DEFAULT_TIMEOUT_MS 120000L
#define ZB_DEFAULT_CONNECT_TIMEOUT_MS 30000L
static long s_timeout_ms = ZB_DEFAULT_TIMEOUT_MS;
static long s_connect_timeout_ms = ZB_DEFAULT_CONNECT_TIMEOUT_MS;
static bool s_allow_loopback_http = false;
static char s_last_request_url_for_test[1024] = {0};
static char s_last_post_body_for_test[4096] = {0};

void zero_bounce_set_http_perform_for_test(ZBHttpPerformFn fn) {
    s_http_perform = fn;
}

void zero_bounce_set_transport_for_test(long timeout_ms, long connect_timeout_ms, bool allow_loopback_http) {
    s_timeout_ms = timeout_ms > 0 ? timeout_ms : ZB_DEFAULT_TIMEOUT_MS;
    s_connect_timeout_ms = connect_timeout_ms > 0 ? connect_timeout_ms : ZB_DEFAULT_CONNECT_TIMEOUT_MS;
    s_allow_loopback_http = allow_loopback_http;
}

/* The test hook in place of libcurl. The content type is always asked for, so
   it is noted for zb_read_success_json as a real response's is. */
static int zb_mock_perform(memory *response_data, long *http_code, char **content_type_out) {
    char *content_type = NULL;
    int ok = s_http_perform(response_data, http_code, &content_type);
    zb_note_response_content_type(ok ? content_type : NULL);
    zb_note_response_bytes(ok ? response_data->response : NULL, ok ? response_data->size : 0, true);
    if (content_type_out) {
        *content_type_out = content_type;
    } else {
        free(content_type);
    }
    return ok;
}

int zb_http_perform_if_mock(memory *response_data, long *http_code, char **content_type_out) {
    if (!s_http_perform) {
        return 0;
    }
    return zb_mock_perform(response_data, http_code, content_type_out);
}

const char* zero_bounce_get_last_request_url_for_test(void) {
    return s_last_request_url_for_test[0] ? s_last_request_url_for_test : NULL;
}

const char* zero_bounce_get_last_post_body_for_test(void) {
    return s_last_post_body_for_test[0] ? s_last_post_body_for_test : NULL;
}

void zero_bounce_record_request_url_for_test(const char* url) {
    if (!url) {
        s_last_request_url_for_test[0] = '\0';
        return;
    }
    strncpy(s_last_request_url_for_test, url, sizeof(s_last_request_url_for_test) - 1);
    s_last_request_url_for_test[sizeof(s_last_request_url_for_test) - 1] = '\0';
}

#define ZB_TEST_FIELD_MAX 48

typedef struct {
    char method[16];
    char url[8192];
    char body[32768];
    struct {
        char name[64];
        char value[1024];
    } fields[ZB_TEST_FIELD_MAX];
    int field_count;
    /* The one extra header the request carried, "Name: value", or "". */
    char extra_header[512];
} ZBTlsRequest;

static _Thread_local ZBTlsRequest zb_tls_request;

/*
 * The region of the client making the current call, for the one-time bulk-host
 * warning. Recorded at the guard every public call already runs, so the
 * transports do not have to carry the client around.
 */
static _Thread_local const char *zb_tls_region = NULL;


static void zb_copy_cap(char *dest, size_t dest_len, const char *src) {
    if (!dest || dest_len == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    snprintf(dest, dest_len, "%s", src);
}

void zb_note_http_request(const char *method, const char *url, const char *body) {
    memset(&zb_tls_request, 0, sizeof(zb_tls_request));
    zb_copy_cap(zb_tls_request.method, sizeof(zb_tls_request.method), method ? method : "");
    zb_copy_cap(zb_tls_request.url, sizeof(zb_tls_request.url), url ? url : "");
    zb_copy_cap(zb_tls_request.body, sizeof(zb_tls_request.body), body ? body : "");
    zero_bounce_record_request_url_for_test(url);
    if (body) {
        strncpy(s_last_post_body_for_test, body, sizeof(s_last_post_body_for_test) - 1);
        s_last_post_body_for_test[sizeof(s_last_post_body_for_test) - 1] = '\0';
    }
}

void zb_note_http_header(const char *header) {
    zb_copy_cap(zb_tls_request.extra_header, sizeof(zb_tls_request.extra_header), header ? header : "");
}

void zb_note_http_field(const char *name, const char *value) {
    int index;
    if (zb_tls_request.field_count >= ZB_TEST_FIELD_MAX) {
        return;
    }
    index = zb_tls_request.field_count++;
    zb_copy_cap(zb_tls_request.fields[index].name, sizeof(zb_tls_request.fields[index].name), name);
    zb_copy_cap(zb_tls_request.fields[index].value, sizeof(zb_tls_request.fields[index].value), value);
}

const char *zero_bounce_test_method(void) { return zb_tls_request.method; }
const char *zero_bounce_test_url(void) { return zb_tls_request.url; }
const char *zero_bounce_test_body(void) { return zb_tls_request.body; }
const char *zero_bounce_test_extra_header(void) { return zb_tls_request.extra_header; }
int zero_bounce_test_field_count(void) { return zb_tls_request.field_count; }
const char *zero_bounce_test_field_name(int index) {
    if (index < 0 || index >= zb_tls_request.field_count) return "";
    return zb_tls_request.fields[index].name;
}
const char *zero_bounce_test_field_value(int index) {
    if (index < 0 || index >= zb_tls_request.field_count) return "";
    return zb_tls_request.fields[index].value;
}

char* zb_url_escape(const char* value) {
    char* escaped = curl_easy_escape(NULL, value ? value : "", 0);
    char* copy = escaped ? strdup(escaped) : NULL;
    zb_secure_curl_free(escaped);
    return copy;
}

static bool zb_append_query_param(StringVector* vector, const char* name, const char* value) {
    if (!value || !strlen(value)) {
        return true;
    }
    size_t prefix_len = strlen(name) + 3;
    char* prefix = (char*)malloc(prefix_len);
    if (!prefix) {
        return false;
    }
    snprintf(prefix, prefix_len, "&%s=", name);
    string_vector_append(vector, prefix);
    free(prefix);
    char* escaped = zb_url_escape(value);
    if (!escaped) {
        return false;
    }
    string_vector_append(vector, escaped);
    free(escaped);
    return true;
}

static bool zb_url_append_suffix(char **url_path, const char *suffix) {
    if (!url_path || !*url_path || !suffix) {
        return false;
    }
    size_t old_len = strlen(*url_path);
    size_t suffix_len = strlen(suffix);
    char *extended = zb_secure_realloc(*url_path, old_len + suffix_len + 1);
    if (!extended) {
        return false;
    }
    memcpy(extended + old_len, suffix, suffix_len + 1);
    *url_path = extended;
    return true;
}

static size_t s_response_cap_for_test = 0;

/* Whether the transfer on this thread was stopped because its body went over
   the size cap. */
static _Thread_local bool zb_tls_over_cap = false;

void zero_bounce_set_response_cap_for_test(size_t max_bytes) {
    s_response_cap_for_test = max_bytes;
}

static size_t zb_response_max_bytes(const memory *mem) {
    if (s_response_cap_for_test != 0) {
        return s_response_cap_for_test;
    }
    if (mem->max_bytes != 0) {
        return mem->max_bytes;
    }
    return ZB_MAX_JSON_RESPONSE_BYTES;
}

static size_t write_callback(void *data, size_t size, size_t nmemb, void *clientp) {
  size_t real_size = size * nmemb;
  memory *mem = (memory*)clientp;
  size_t cap = zb_response_max_bytes(mem);

  if (mem->size + real_size > cap) {
    zb_tls_over_cap = true;
    return 0;
  }

  char *ptr = realloc(mem->response, mem->size + real_size + 1);
  if(ptr == NULL)
    return 0;  /* out of memory! */

  mem->response = ptr;
  memcpy(&(mem->response[mem->size]), data, real_size);
  mem->size += real_size;
  mem->response[mem->size] = 0;
  zb_note_response_bytes(data, real_size, false);

  return real_size;
}

/*
 * The options every request gets, buffered or streamed: a total and a connect
 * timeout (120 s and 30 s; the connect one covers the TLS handshake), no
 * redirects, and https only.
 */
void zb_transport_timeouts(long *timeout_ms, long *connect_timeout_ms) {
    *timeout_ms = s_timeout_ms;
    *connect_timeout_ms = s_connect_timeout_ms;
}

static long s_nosignal_applied = 0;

long zb_nosignal_applied_for_test(void) {
    return s_nosignal_applied;
}

void zb_set_transport_options(CURL* curl) {
    long timeout_ms;
    long connect_timeout_ms;
    zb_transport_timeouts(&timeout_ms, &connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    /* A redirect could send the api_key to another host, so one is reported
       rather than followed, and only https is ever dialled. */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    /* No SIGALRM-based timeouts: a signal handler is not thread safe, and a
       client may be used from several threads. */
    if (curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK) {
        s_nosignal_applied++;
    }
#if LIBCURL_VERSION_NUM >= 0x075500 /* 7.85.0 */
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, s_allow_loopback_http ? "http,https" : "https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
        s_allow_loopback_http ? (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS) : (long)CURLPROTO_HTTPS);
#endif
}

void set_write_callback(CURL* curl, memory* response_data) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)response_data);
    zb_set_transport_options(curl);
    zb_note_response_content_type(NULL);
    zb_note_response_bytes(NULL, 0, true);
    zb_tls_over_cap = false;
}

void zb_report_perform_error(CURL *curl, CURLcode res, OnErrorCallback error_callback) {
    char error_message[300];
    if (!error_callback) {
        return;
    }
    if (res == CURLE_WRITE_ERROR && zb_tls_over_cap) {
        long status_code = get_http_code(curl);
        snprintf(error_message, sizeof(error_message),
                 "The response body was larger than this client accepts (HTTP %ld).", status_code);
        error_callback(parse_error_with_status(error_message, status_code));
        return;
    }
    snprintf(
        error_message, sizeof(error_message),
        "Failed to perform request. CURLcode: %d. Error: %s",
        res, curl_easy_strerror(res)
    );
    if (error_callback) {
        error_callback(parse_error(error_message));
    }
}

/* Every request reads its status here, so this is also where the response's
   content type is noted for zb_read_success_json. */
long get_http_code(CURL* curl) {
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    zb_note_response_content_type(get_content_type_value(curl));

    return http_code;
}

char* get_content_type_value(CURL* curl) {
    struct curl_header *content_type_header = NULL;
    if (curl_easy_header(curl, "Content-Type", 0, CURLH_HEADER, -1, &content_type_header) != CURLHE_OK ||
        !content_type_header) {
        return NULL;
    }

    return content_type_header->value;
}


/*
 * A name next to path for a file being written, unique among concurrent
 * downloads to the same path: the process id and an address that belongs to
 * this call. The caller frees it.
 */
char *zb_temp_path_for(const char *path, const void *owner) {
    size_t length = strlen(path) + 64;
    char *temp_path = malloc(length);
    if (!temp_path) {
        return NULL;
    }
#ifdef _WIN32
    snprintf(temp_path, length, "%s.%ld-%lx.part", path,
             (long)_getpid(), (unsigned long)(uintptr_t)owner);
#else
    snprintf(temp_path, length, "%s.%ld-%lx.part", path,
             (long)getpid(), (unsigned long)(uintptr_t)owner);
#endif
    return temp_path;
}

/* Moves temp_path over path, replacing whatever was there. One call: deleting
   the old file first would lose it if the rename then failed. */
bool zb_replace_file(const char *temp_path, const char *path) {
#ifdef _WIN32
    return MoveFileExA(temp_path, path, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(temp_path, path) == 0;
#endif
}

int zb_write_file_replacing(const char *path, const char *data, size_t size) {
    char *temp_path = zb_temp_path_for(path, &temp_path);
    FILE *handle;
    size_t written;
    int closed;

    if (!temp_path) {
        return ZB_WRITE_OPEN_FAILED;
    }
    handle = fopen(temp_path, "wb");
    if (!handle) {
        free(temp_path);
        return ZB_WRITE_OPEN_FAILED;
    }
    written = fwrite(data, 1, size, handle);
    closed = fclose(handle);
    if (closed != 0 || written != size || !zb_replace_file(temp_path, path)) {
        /* Only this call's own partial file goes; a file at path is kept. */
        remove(temp_path);
        free(temp_path);
        return ZB_WRITE_FAILED;
    }
    free(temp_path);
    return 0;
}


SendFileOptions new_send_file_options(void) {
    SendFileOptions options;

    options.return_url = "";
    options.first_name_column = 0;
    options.last_name_column = 0;
    options.gender_column = 0;
    options.ip_address_column = 0;
    options.has_header_row = true;
    options.remove_duplicate = true;
    options.allow_phase_2_is_set = false;
    options.allow_phase_2 = false;
    options.remove_duplicate_set = false;

    return options;
}


GetFileOptions new_get_file_options(void) {
    GetFileOptions o = { NULL, -1 };
    return o;
}

ZBValidateOptions new_validate_options(void) {
    ZBValidateOptions o = { -1, -1, -1, -1, -1, NULL, -1 };
    return o;
}

/*
 * The callback is where the greylist retry posts its result, so it is
 * meaningless - and rejected by the API - without the retry itself. Checked
 * before any request is sent.
 */
static bool zb_check_callback_url(
    const ZBValidateOptions *options, OnErrorCallback error_callback
) {
    const char *url;
    const char *p;
    if (!options || !options->callback_url) {
        return true;
    }
    url = options->callback_url;
    for (p = url; *p; p++) {
        if (!isspace((unsigned char)*p)) {
            break;
        }
    }
    if (*p == '\0') {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: callback_url"));
        }
        return false;
    }
    if (options->greylist_processing != 1) {
        if (error_callback) {
            error_callback(parse_error(
                "Invalid parameter: callback_url requires greylist_processing to be true"
            ));
        }
        return false;
    }
    return true;
}

/* Appends the three options this port adds to a query string or a form body. */
static bool zb_append_validate_extras(char **target, const ZBValidateOptions *options) {
    if (options->domain_info >= 0) {
        if (!zb_url_append_suffix(
                target, options->domain_info ? "&domain_info=true" : "&domain_info=false")) {
            return false;
        }
    }
    if (options->greylist_processing >= 0) {
        if (!zb_url_append_suffix(
                target,
                options->greylist_processing
                    ? "&greylist_processing=true"
                    : "&greylist_processing=false")) {
            return false;
        }
    }
    if (options->callback_url) {
        char *escaped = zb_url_escape(options->callback_url);
        bool ok = zb_url_append_suffix(target, "&callback_url=")
            && zb_url_append_suffix(target, escaped);
        free(escaped);
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool zb_check_validate_timeout(int timeout, OnErrorCallback error_callback) {
    if (timeout < 0 || (timeout >= 3 && timeout <= 60)) {
        return true;
    }
    if (error_callback) {
        error_callback(parse_error("Invalid parameter: timeout (must be between 3 and 60 seconds)"));
    }
    return false;
}

static bool zb_check_validate_batch_timeout(int timeout, OnErrorCallback error_callback) {
    if (timeout < 0 || (timeout >= 10 && timeout <= 120)) {
        return true;
    }
    if (error_callback) {
        error_callback(parse_error("Invalid parameter: timeout (must be between 10 and 120 seconds)"));
    }
    return false;
}


/* Whether the content type names JSON: application/json, or a structured
   suffix such as application/problem+json. */
static bool zb_ct_includes_application_json(const char *content_type) {
    if (!content_type) {
        return false;
    }
    for (const char *p = content_type; *p; p++) {
        if (zb_strncasecmp(p, "application/json", 16) == 0 || zb_strncasecmp(p, "+json", 5) == 0) {
            return true;
        }
    }
    return false;
}

/* The start of body past every leading ASCII whitespace byte, UTF-8 byte order
   mark (EF BB BF) and no-break space (C2 A0), in any order. */
const char *zb_download_skip_blank(const char *body) {
    while (body) {
        unsigned char c = (unsigned char)body[0];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
            body++;
        } else if (c == 0xEF && (unsigned char)body[1] == 0xBB && (unsigned char)body[2] == 0xBF) {
            body += 3;
        } else if (c == 0xC2 && (unsigned char)body[1] == 0xA0) {
            body += 2;
        } else {
            break;
        }
    }
    return body ? body : "";
}

/* Whether a download body is nothing but whitespace, byte order marks and
   no-break spaces: not a file, whatever its content type. */
bool zb_download_body_is_blank(const char *body) {
    return zb_download_skip_blank(body)[0] == '\0';
}

bool zb_get_file_json_indicates_error(const char *body) {
    /* The same padding the downloads skip, including a byte order mark. */
    body = zb_download_skip_blank(body);
    if (body[0] != '{') {
        return false;
    }
    json_object *o = json_tokener_parse(body);
    if (!o || json_object_get_type(o) != json_type_object) {
        if (o) {
            json_object_put(o);
        }
        return false;
    }

    json_object *s = json_object_object_get(o, "success");
    if (s) {
        if (json_object_is_type(s, json_type_boolean) && !json_object_get_boolean(s)) {
            json_object_put(o);
            return true;
        }
        if (json_object_is_type(s, json_type_string)) {
            const char *sv = json_object_get_string(s);
            if (sv && (ZB_STRICMP(sv, "false") == 0 || ZB_STRICMP(sv, "False") == 0)) {
                json_object_put(o);
                return true;
            }
        }
    }

    const char *keys[] = {"message", "error", "error_message", "Message", "Error"};
    for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        json_object *v = json_object_object_get(o, keys[k]);
        if (!v || json_object_is_type(v, json_type_null)) {
            continue;
        }
        if (json_object_is_type(v, json_type_string)) {
            const char *t = json_object_get_string(v);
            if (t && t[0]) {
                json_object_put(o);
                return true;
            }
        }
        if (json_object_is_type(v, json_type_array) && json_object_array_length(v) > 0) {
            json_object_put(o);
            return true;
        }
    }

    bool has_success_key = json_object_object_get(o, "success") != NULL;
    json_object_put(o);
    return has_success_key;
}


bool zb_download_body_is_json(const char *body, const char *content_type) {
    if (zb_ct_includes_application_json(content_type)) {
        return true;
    }
    if (!body) {
        return false;
    }
    body = zb_download_skip_blank(body);
    return *body == '{' || *body == '[';
}


char *zb_format_get_file_error_message(const char *body) {
    if (!body) {
        return strdup("Invalid getfile response");
    }
    json_object *o = json_tokener_parse(body);
    if (!o || json_object_get_type(o) != json_type_object) {
        if (o) {
            json_object_put(o);
        }
        return strdup(body[0] ? body : "Invalid getfile response");
    }

    const char *keys[] = {"message", "error", "error_message", "Message", "Error"};
    for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        json_object *v = json_object_object_get(o, keys[k]);
        if (!v || json_object_is_type(v, json_type_null)) {
            continue;
        }
        if (json_object_is_type(v, json_type_string)) {
            const char *t = json_object_get_string(v);
            if (t && t[0]) {
                char *r = strdup(t);
                json_object_put(o);
                return r ? r : strdup("");
            }
        }
        if (json_object_is_type(v, json_type_array)) {
            /* Every non-empty line of a list, joined with "\n" as the file-job
               message is. */
            StringVector lines = string_vector_init();
            for (size_t n = 0; n < json_object_array_length(v); n++) {
                json_object *item = json_object_array_get_idx(v, n);
                if (json_object_is_type(item, json_type_string) && json_object_get_string(item)[0]) {
                    string_vector_append(&lines, json_object_get_string(item));
                }
            }
            if (lines.size > 0) {
                char *r = concatenate_strings(&lines, "\n");
                string_vector_free(&lines);
                json_object_put(o);
                return r ? r : strdup("");
            }
            string_vector_free(&lines);
        }
    }

    json_object_put(o);
    return strdup(body);
}

/* A JSON download error (not a file) after any padding, reported as the API's
   own message, as get_file does, not as the raw body. */
void zb_report_download_json_error(const char *body, long status_code, OnErrorCallback error_callback) {
    char *msg;
    if (!error_callback) {
        return;
    }
    msg = zb_format_get_file_error_message(zb_download_skip_blank(body));
    /* The API did answer: the status goes with the error. */
    error_callback(parse_error_with_status(msg ? msg : "", status_code));
    free(msg);
}


static char *zb_build_getfile_url(
    ZeroBounce *zb,
    bool scoring,
    const char *file_id,
    const GetFileOptions *opts
) {
    const char *base_url = scoring ? zb->bulk_api_scoring_base_url : zb->bulk_api_base_url;
    CURL *curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }

    char *ek = curl_easy_escape(curl, zb->api_key, 0);
    char *fid = curl_easy_escape(curl, file_id, 0);
    if (!ek || !fid) {
        zb_secure_curl_free(ek);
        curl_free(fid);
        curl_easy_cleanup(curl);
        return NULL;
    }

    int base_len = snprintf(NULL, 0, "%s/getfile?api_key=%s&file_id=%s", base_url, ek, fid);
    if (base_len < 0) {
        zb_secure_curl_free(ek);
        curl_free(fid);
        curl_easy_cleanup(curl);
        return NULL;
    }

    char *dt_esc = NULL;
    int dt_len = 0;
    if (opts && opts->download_type && opts->download_type[0]) {
        dt_esc = curl_easy_escape(curl, opts->download_type, 0);
        if (!dt_esc) {
            zb_secure_curl_free(ek);
            curl_free(fid);
            curl_easy_cleanup(curl);
            return NULL;
        }
        dt_len = snprintf(NULL, 0, "&download_type=%s", dt_esc);
        if (dt_len < 0) {
            curl_free(dt_esc);
            zb_secure_curl_free(ek);
            curl_free(fid);
            curl_easy_cleanup(curl);
            return NULL;
        }
    }

    int ad_len = 0;
    if (!scoring && opts && opts->activity_data >= 0) {
        ad_len = snprintf(NULL, 0, "&activity_data=%s", opts->activity_data ? "true" : "false");
        if (ad_len < 0) {
            curl_free(dt_esc);
            zb_secure_curl_free(ek);
            curl_free(fid);
            curl_easy_cleanup(curl);
            return NULL;
        }
    }

    size_t total = (size_t)base_len + (size_t)dt_len + (size_t)ad_len + 1;
    char *url = malloc(total);
    if (!url) {
        curl_free(dt_esc);
        zb_secure_curl_free(ek);
        curl_free(fid);
        curl_easy_cleanup(curl);
        return NULL;
    }

    snprintf(url, total, "%s/getfile?api_key=%s&file_id=%s", base_url, ek, fid);
    if (dt_esc) {
        snprintf(url + strlen(url), total - strlen(url), "&download_type=%s", dt_esc);
    }
    if (!scoring && opts && opts->activity_data >= 0) {
        snprintf(url + strlen(url), total - strlen(url), "&activity_data=%s", opts->activity_data ? "true" : "false");
    }

    curl_free(dt_esc);
    zb_secure_curl_free(ek);
    curl_free(fid);
    curl_easy_cleanup(curl);
    return url;
}


/* The base URLs a client starts with, as arrays: a setter tells one of these
   from a copy it made (and must free) by address. */
static const char zb_default_bulk_api_root_url[] = "https://bulkapi.zerobounce.net";
static const char zb_default_list_evaluator_base_url[] = "https://bulkapi.zerobounce.net/v2/listevaluator";
static const char zb_default_inbox_placement_base_url[] = "https://bulkapi.zerobounce.net/api/ipt";
static const char zb_default_bulk_api_base_url[] = "https://bulkapi.zerobounce.net/v2";
static const char zb_default_bulk_api_scoring_base_url[] = "https://bulkapi.zerobounce.net/v2/scoring";

static ZeroBounce *zero_bounce_instance = NULL;

ZeroBounce* new_zero_bounce_instance(void) {
    ZeroBounce* zb = (ZeroBounce*) malloc(sizeof(ZeroBounce));

    zb->api_key = NULL;
    zb->api_key_rejected = false;
    zb->api_base_url = "https://api.zerobounce.net/v2";
    zb->bulk_api_root_url = zb_default_bulk_api_root_url;
    zb->list_evaluator_base_url = zb_default_list_evaluator_base_url;
    zb->inbox_placement_base_url = zb_default_inbox_placement_base_url;
    zb->bulk_api_base_url = zb_default_bulk_api_base_url;
    zb->bulk_api_scoring_base_url = zb_default_bulk_api_scoring_base_url;

    curl_global_init(CURL_GLOBAL_ALL);
    return zb;
}


ZeroBounce* zero_bounce_get_instance(void) {
    if (zero_bounce_instance == NULL) {
        zero_bounce_instance = new_zero_bounce_instance();
    }
    return zero_bounce_instance;
}


/*
 * A key with surrounding whitespace or control characters would otherwise be
 * quoted, unredacted, in a transport error, so it is refused here. An empty
 * key stays deferred to zero_bounce_invalid_api_key, which names the call
 * that initializes the client.
 */
/* Unicode space separators and the invisible format characters: none of them
   belongs in an API key, and a reader cannot see them in a quoted one. */
static bool zb_codepoint_is_blank_or_invisible(unsigned long cp) {
    return (cp >= 0x80 && cp <= 0xA0) ||                 /* C1 controls, no-break space */
        cp == 0x00AD ||                                   /* soft hyphen */
        cp == 0x034F ||                                   /* combining grapheme joiner */
        (cp >= 0x0600 && cp <= 0x0605) || cp == 0x061C || cp == 0x06DD || cp == 0x070F ||
        (cp >= 0x0890 && cp <= 0x0891) || cp == 0x08E2 ||
        cp == 0x115F || cp == 0x1160 ||                   /* Hangul choseong/jungseong fillers */
        cp == 0x17B4 || cp == 0x17B5 ||                   /* Khmer inherent vowels */
        cp == 0x1680 ||                                   /* ogham space mark */
        (cp >= 0x180B && cp <= 0x180F) ||                 /* Mongolian selectors, vowel separator */
        (cp >= 0x2000 && cp <= 0x200F) ||                 /* en/em spaces ... zero width, marks */
        (cp >= 0x2028 && cp <= 0x202F) ||                 /* separators, bidi embeddings, nnbsp */
        (cp >= 0x205F && cp <= 0x206F) ||                 /* math space, word joiner, isolates */
        cp == 0x2800 ||                                   /* braille pattern blank */
        cp == 0x3000 || cp == 0x3164 ||                   /* ideographic space, Hangul filler */
        (cp >= 0xFE00 && cp <= 0xFE0F) ||                 /* variation selectors */
        cp == 0xFEFF ||                                   /* zero width no-break space, BOM */
        cp == 0xFFA0 ||                                   /* halfwidth Hangul filler */
        (cp >= 0xFFF0 && cp <= 0xFFFB) ||                 /* specials, interlinear annotation */
        cp == 0x110BD || cp == 0x110CD ||                 /* Kaithi number signs */
        (cp >= 0x13430 && cp <= 0x1343F) ||               /* Egyptian hieroglyph format controls */
        (cp >= 0x1BCA0 && cp <= 0x1BCA3) ||               /* shorthand format controls */
        (cp >= 0x1D173 && cp <= 0x1D17A) ||               /* musical formatting */
        (cp >= 0xE0000 && cp <= 0xE0FFF);                 /* tags, variation selectors supplement */
}

bool zb_api_key_is_clean(const char *api_key) {
    const char *p;
    if (!api_key || !api_key[0]) {
        return true;
    }
    if (isspace((unsigned char)api_key[0]) ||
        isspace((unsigned char)api_key[strlen(api_key) - 1])) {
        return false;
    }
    for (p = api_key; *p;) {
        unsigned char c = (unsigned char)*p;
        unsigned long cp;
        int extra;
        int i;
        if (c < 32 || c == 127) {
            return false;
        }
        if (c < 0x80) {
            p++;
            continue;
        }
        /* A multi-byte character: decoded, so a Unicode space or an invisible
           character in the middle of a key is refused as an ASCII control
           character is. Bytes that are not well-formed UTF-8 (a stray
           continuation byte, an overlong form, a surrogate, a value past
           U+10FFFF, a truncated sequence) are refused too: such a key cannot
           be told from a valid one by eye. */
        if (c >= 0xF0 && c <= 0xF4) {
            cp = c & 0x07;
            extra = 3;
        } else if (c >= 0xE0 && c <= 0xEF) {
            cp = c & 0x0F;
            extra = 2;
        } else if (c >= 0xC2 && c <= 0xDF) {
            cp = c & 0x1F;
            extra = 1;
        } else {
            return false;
        }
        for (i = 1; i <= extra; i++) {
            if (((unsigned char)p[i] & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | ((unsigned char)p[i] & 0x3F);
        }
        if ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
            (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
            return false;
        }
        p += extra + 1;
        if (zb_codepoint_is_blank_or_invisible(cp)) {
            return false;
        }
    }
    return true;
}


/*
 * Whether a URL holds none of what would send a request somewhere else once
 * the SDK adds its own path and query: a '#', a backslash, whitespace, a
 * control or an invisible character (decoded, so a Unicode space counts), and
 * a '?' when allow_query is false (a base URL) .
 */
bool zb_url_text_is_clean(const char *url, bool allow_query) {
    const char *p;
    if (!url) {
        return true;
    }
    for (p = url; *p;) {
        unsigned char c = (unsigned char)*p;
        unsigned long cp;
        int extra;
        int i;
        if (c <= 32 || c == 127 || c == '#' || c == '\\' || (c == '?' && !allow_query)) {
            return false;
        }
        if (c < 0x80) {
            p++;
            continue;
        }
        if (c >= 0xF0 && c < 0xF8) {
            cp = c & 0x07;
            extra = 3;
        } else if (c >= 0xE0 && c < 0xF0) {
            cp = c & 0x0F;
            extra = 2;
        } else if (c >= 0xC2 && c < 0xE0) {
            cp = c & 0x1F;
            extra = 1;
        } else {
            p++;
            continue;
        }
        for (i = 1; i <= extra; i++) {
            if (((unsigned char)p[i] & 0xC0) != 0x80) {
                break;
            }
            cp = (cp << 6) | ((unsigned char)p[i] & 0x3F);
        }
        if (i <= extra) {
            p++;
            continue;
        }
        p += extra + 1;
        if (zb_codepoint_is_blank_or_invisible(cp)) {
            return false;
        }
    }
    return true;
}

/* The SDK escapes every value it puts after the '?', so a second '?' or a '/'
   after the first one belongs to a base URL that held a query of its own. */
bool zb_url_query_is_the_sdks_own(const char *url) {
    const char *q = url ? strchr(url, '?') : NULL;
    return !q || (!strchr(q + 1, '?') && !strchr(q + 1, '/'));
}


/*
 * A rejected key also drops the one set before it: carrying on with the old
 * key would send requests the caller believes go out under the new one. Every
 * call then reports the rejection to its error callback.
 */
static bool zb_accept_api_key(ZeroBounce *zb, const char *api_key) {
    if (!zb) {
        return false;
    }
    if (zb->api_key) {
        zb_secure_free(zb->api_key);
        zb->api_key = NULL;
    }
    if (!zb_api_key_is_clean(api_key)) {
        fprintf(stderr,
            "ZeroBounce: api_key has surrounding whitespace or control characters; "
            "it was not accepted.\n");
        zb->api_key_rejected = true;
        return false;
    }
    zb->api_key_rejected = false;
    zb->api_key = api_key ? strdup(api_key) : NULL;
    return true;
}

void zero_bounce_initialize(ZeroBounce* zb, const char* api_key) {
    zb_accept_api_key(zb, api_key);
}


void zero_bounce_initialize_with_base_url(ZeroBounce* zb, const char* api_key, ZBApiURL api_base_url) {
    if (!zb_accept_api_key(zb, api_key)) {
        return;
    }
    zb->api_base_url = base_url_string_from_zb_api_url(api_base_url);
}

/*
 * The bulk hosts are not built from api_base_url, so initializing with a
 * regional base URL leaves them on the global bulk host. These three let a
 * caller point them somewhere else, as zb_check_request_url's warning says.
 */
char *zb_normalized_base_url(const char *url) {
    size_t len = strlen(url);
    char *copy;
    while (len > 0 && url[len - 1] == '/') {
        len--;
    }
    copy = malloc(len + 1);
    if (!copy) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    memcpy(copy, url, len);
    copy[len] = '\0';
    return copy;
}

/*
 * The base URLs this library copied. A URL field is a plain const char *, so
 * a caller can store a literal or its own buffer there; only a pointer that is
 * in this table was allocated here and may be freed. The table is shared by
 * every client in the process, so every register and every free of it holds
 * one lock: two clients on two threads do not race on it. (The clients
 * themselves are still not thread-safe; one client belongs to one thread.)
 */
static const char **zb_owned_urls = NULL;
static size_t zb_owned_url_count = 0;
static size_t zb_owned_url_capacity = 0;

#ifdef _WIN32
static CRITICAL_SECTION zb_owned_urls_lock;
static INIT_ONCE zb_owned_urls_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK zb_owned_urls_init(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once;
    (void)param;
    (void)context;
    InitializeCriticalSection(&zb_owned_urls_lock);
    return TRUE;
}

static void zb_owned_urls_acquire(void) {
    InitOnceExecuteOnce(&zb_owned_urls_once, zb_owned_urls_init, NULL, NULL);
    EnterCriticalSection(&zb_owned_urls_lock);
}

static void zb_owned_urls_release(void) {
    LeaveCriticalSection(&zb_owned_urls_lock);
}
#else
static pthread_mutex_t zb_owned_urls_lock = PTHREAD_MUTEX_INITIALIZER;

static void zb_owned_urls_acquire(void) {
    pthread_mutex_lock(&zb_owned_urls_lock);
}

static void zb_owned_urls_release(void) {
    pthread_mutex_unlock(&zb_owned_urls_lock);
}
#endif

/* Records url (a pointer this library allocated) as owned. When the table
   cannot grow the pointer is not recorded (so it is never freed); nothing
   exits. */
void zb_register_owned_url(const char *url) {
    if (!url) {
        return;
    }
    zb_owned_urls_acquire();
    if (zb_owned_url_count == zb_owned_url_capacity) {
        size_t capacity = zb_owned_url_capacity ? zb_owned_url_capacity * 2 : 8;
        const char **grown = realloc((void *)zb_owned_urls, capacity * sizeof(*grown));
        if (!grown) {
            zb_owned_urls_release();
            return;
        }
        zb_owned_urls = grown;
        zb_owned_url_capacity = capacity;
    }
    zb_owned_urls[zb_owned_url_count++] = url;
    zb_owned_urls_release();
}

/* How many copies the table holds. The offline tests use it to see that a
   replaced or freed URL was released. */
size_t zb_owned_url_count_for_test(void) {
    size_t count;
    zb_owned_urls_acquire();
    count = zb_owned_url_count;
    zb_owned_urls_release();
    return count;
}

/* Frees url when this library allocated it; any other pointer (a literal, a
   caller's buffer, a starting default) is left alone. */
void zb_free_owned_url(const char *url) {
    size_t i;
    if (!url) {
        return;
    }
    zb_owned_urls_acquire();
    for (i = 0; i < zb_owned_url_count; i++) {
        if (zb_owned_urls[i] == url) {
            zb_owned_urls[i] = zb_owned_urls[--zb_owned_url_count];
            zb_owned_urls_release();
            free((char *)url);
            return;
        }
    }
    zb_owned_urls_release();
}

/* Stores a normalized copy of url in *slot and frees the copy it replaces, if
   it was one; anything else the slot held is left alone. */
void zb_replace_base_url(const char **slot, const char *url) {
    char *copy = zb_normalized_base_url(url);
    zb_register_owned_url(copy);
    zb_free_owned_url(*slot);
    *slot = copy;
}

void zero_bounce_set_bulk_api_base_url(ZeroBounce *zb, const char *url) {
    if (zb && url) {
        zb_replace_base_url(&zb->bulk_api_base_url, url);
    }
}

void zero_bounce_set_bulk_api_root_url(ZeroBounce *zb, const char *url) {
    if (zb && url) {
        zb_replace_base_url(&zb->bulk_api_root_url, url);
    }
}

void zero_bounce_set_bulk_api_scoring_base_url(ZeroBounce *zb, const char *url) {
    if (zb && url) {
        zb_replace_base_url(&zb->bulk_api_scoring_base_url, url);
    }
}

/* The host part of a URL, lowercased, or "" when there is none. */
static void zb_host_of(const char *url, char *out, size_t out_len) {
    const char *start = url ? strstr(url, "://") : NULL;
    const char *host = start ? start + 3 : (url ? url : "");
    const char *end = host;
    size_t i = 0;
    while (*end && *end != '/' && *end != '?' && *end != '#') {
        end++;
    }
    while (host + i < end && i + 1 < out_len) {
        out[i] = (char)tolower((unsigned char)host[i]);
        i++;
    }
    out[i] = '\0';
}

/* Whether the authority of url (between "://" and the first '/', '?' or '#')
   holds an '@': a username or password, even an empty one (https://@host,
   https://:@host). The part before the '@' is what the request would send as
   credentials while the part after it, not the one a reader sees first, is
   the host that is contacted. A backslash is not a separator here, so
   https://host\@evil is refused as well. */
static bool zb_url_has_userinfo(const char *url) {
    const char *p = url ? strstr(url, "://") : NULL;
    if (!p) {
        return false;
    }
    for (p += 3; *p && *p != '/' && *p != '?' && *p != '#'; p++) {
        if (*p == '@') {
            return true;
        }
    }
    return false;
}

bool zb_check_request_url(const char *url, OnErrorCallback error_callback) {
    static int warned_about_region = 0;
    char host[256];

    /* Before the scheme check and the loopback exception: neither makes a
       URL with credentials in it safe to send to. The text does not quote the
       URL. */
    if (zb_url_has_userinfo(url)) {
        if (error_callback) {
            error_callback(parse_error("url must not contain a username or password"));
        }
        return false;
    }

    bool loopback = s_allow_loopback_http && url &&
        (strncmp(url, "http://127.0.0.1:", 17) == 0 || strncmp(url, "http://localhost:", 17) == 0);
    if (!loopback && (!url || strncmp(url, "https://", 8) != 0)) {
        if (error_callback) {
            error_callback(parse_error("url must be an https:// URL"));
        }
        return false;
    }
    if (!zb_url_text_is_clean(url, true) || !zb_url_query_is_the_sdks_own(url)) {
        if (error_callback) {
            error_callback(parse_error(ZB_URL_TEXT_MESSAGE));
        }
        return false;
    }
    zb_host_of(url, host, sizeof(host));
    if (!warned_about_region && zb_tls_region &&
        strcmp(host, "bulkapi.zerobounce.net") == 0) {
        warned_about_region = 1;
        fprintf(stderr,
            "ZeroBounce warning: the base URL is the %s API, but this request goes to "
            "bulkapi.zerobounce.net. ZeroBounce documents no regional host for bulk files, "
            "scoring files, email finder and domain search files, the list evaluator, "
            "S3/SFTP or the Inbox Placement Tester. Set the matching base URLs with "
            "zero_bounce_set_bulk_api_base_url and friends if ZeroBounce gave you "
            "regional ones.\n",
            zb_tls_region);
    }
    return true;
}


bool zero_bounce_invalid_api_key(ZeroBounce *zb, OnErrorCallback error_callback) {
    zb_tls_region = NULL;
    if (zb && zb->api_base_url) {
        if (strstr(zb->api_base_url, "api-eu.zerobounce.net")) {
            zb_tls_region = "EU";
        } else if (strstr(zb->api_base_url, "api-us.zerobounce.net")) {
            zb_tls_region = "US";
        }
    }
    if (!zb) {
        zb_set_redaction_key(NULL);
        if (error_callback) {
            error_callback(parse_error(
                "ZeroBounce is not initialized. Please call zero_bounce_initialize(zb_instance, api_key); first"
            ));
        }
        return true;
    }
    /* Every error this call reports has the key taken out. */
    zb_set_redaction_key(zb->api_key_rejected ? NULL : zb->api_key);
    if (zb->api_key_rejected) {
        if (error_callback) {
            error_callback(parse_error("api_key has surrounding whitespace or control characters"));
        }
        return true;
    }
    if (zb->api_key == NULL || strlen(zb->api_key) == 0) {
        ZBErrorResponse error_response = parse_error(
            "ZeroBounce is not initialized. Please call zero_bounce_initialize(zb_instance, api_key); first"
        );
        if (error_callback) {
            error_callback(error_response);
        }
        return true;
    }
    return false;
}


int zb_make_request_with_method_and_body(
    char* url_path,
    const char* method,
    const char* body,
    const char* content_type_header,
    memory* response_data,
    long* http_code,
    OnErrorCallback error_callback
) {
    return zb_make_request_with_extra_header(
        url_path, method, body, content_type_header, NULL,
        response_data, http_code, error_callback
    );
}

int zb_make_request_with_extra_header(
    char* url_path,
    const char* method,
    const char* body,
    const char* content_type_header,
    const char* extra_header,
    memory* response_data,
    long* http_code,
    OnErrorCallback error_callback
) {
    return zb_make_request_with_extra_header_ct(
        url_path, method, body, content_type_header, extra_header, response_data, http_code, NULL,
        error_callback);
}

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
) {
    if (content_type_out) {
        *content_type_out = NULL;
    }
    zb_note_http_request(method, url_path, body);
    zb_note_http_header(extra_header);

    if (!zb_check_request_url(url_path, error_callback)) {
        return 0;
    }

    if (s_http_perform) {
        return zb_mock_perform(response_data, http_code, content_type_out);
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (error_callback) {
            error_callback(parse_error("Failed to initialize libcurl"));
        }
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_URL, url_path);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    if (body && content_type_header) {
        headers = curl_slist_append(headers, content_type_header);
    }
    if (extra_header) {
        headers = curl_slist_append(headers, extra_header);
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (body) {
        curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, body);
    }

    set_write_callback(curl, response_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        zb_report_perform_error(curl, res, error_callback);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        return 0;
    }

    *http_code = get_http_code(curl);
    if (content_type_out) {
        const char *content_type = get_content_type_value(curl);
        *content_type_out = content_type ? strdup(content_type) : NULL;
    }
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    return 1;
}

int zb_make_request_with_post_body(
    char* url_path,
    char* body,
    memory* response_data,
    long* http_code,
    OnErrorCallback error_callback
) {
    zb_note_http_request("POST", url_path, body ? body : "");

    if (!zb_check_request_url(url_path, error_callback)) {
        return 0;
    }

    if (s_http_perform) {
        return zb_mock_perform(response_data, http_code, NULL);
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (error_callback) {
            error_callback(parse_error("Failed to initialize libcurl"));
        }
        curl_easy_cleanup(curl);
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_URL, url_path);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, body);

    set_write_callback(curl, response_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        zb_report_perform_error(curl, res, error_callback);

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        return 0;
    }

    *http_code = get_http_code(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return 1;
}

int zb_make_request(
    char* url_path,
    char* request_type,
    char* header,
    memory* response_data,
    long* http_code,
    OnErrorCallback error_callback
) {
    zb_note_http_request(request_type ? request_type : "GET", url_path, NULL);

    if (!zb_check_request_url(url_path, error_callback)) {
        return 0;
    }
    if (s_http_perform) {
        return zb_mock_perform(response_data, http_code, NULL);
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (error_callback) {
            error_callback(parse_error("Failed to initialize libcurl"));
        }
        curl_easy_cleanup(curl);
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request_type);
    curl_easy_setopt(curl, CURLOPT_URL, url_path);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    set_write_callback(curl, response_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        zb_report_perform_error(curl, res, error_callback);

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        return 0;
    }

    *http_code = get_http_code(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return 1;
}

static void note_send_file_fields(
    ZeroBounce *zb,
    bool scoring,
    int email_address_column_index,
    SendFileOptions options
) {
    char int_str_option[16];
    zb_note_http_field("api_key", zb->api_key ? zb->api_key : "");
    zb_note_http_field("file", "<file>");
    snprintf(int_str_option, sizeof(int_str_option), "%d", email_address_column_index);
    zb_note_http_field("email_address_column", int_str_option);
    if (!scoring) {
        if (options.first_name_column > 0) {
            snprintf(int_str_option, sizeof(int_str_option), "%d", options.first_name_column);
            zb_note_http_field("first_name_column", int_str_option);
        }
        if (options.last_name_column > 0) {
            snprintf(int_str_option, sizeof(int_str_option), "%d", options.last_name_column);
            zb_note_http_field("last_name_column", int_str_option);
        }
        if (options.gender_column > 0) {
            snprintf(int_str_option, sizeof(int_str_option), "%d", options.gender_column);
            zb_note_http_field("gender_column", int_str_option);
        }
        if (options.ip_address_column > 0) {
            snprintf(int_str_option, sizeof(int_str_option), "%d", options.ip_address_column);
            zb_note_http_field("ip_address_column", int_str_option);
        }
    }
    if (options.return_url && strlen(options.return_url) != 0) {
        zb_note_http_field("return_url", options.return_url);
    }
    zb_note_http_field("has_header_row", options.has_header_row ? "true" : "false");
    zb_note_http_field("remove_duplicate", options.remove_duplicate ? "true" : "false");
    if (!scoring && options.allow_phase_2_is_set) {
        zb_note_http_field("allow_phase_2", options.allow_phase_2 ? "true" : "false");
    }
}

static void send_file_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_path,
    int email_address_column_index,
    SendFileOptions options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/sendfile";
    const char* base_url = scoring ? zb->bulk_api_scoring_base_url : zb->bulk_api_base_url;

    int url_path_len = snprintf(
        NULL, 0, url_pattern, base_url
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, base_url
    );

    memory response_data = {0};

    zb_note_http_request("POST", url_path, NULL);
    if (!zb_check_request_url(url_path, error_callback)) {
        goto cleanup;
    }
    note_send_file_fields(zb, scoring, email_address_column_index, options);

    if (s_http_perform) {
        long http_code = 0;
        if (!zb_mock_perform(&response_data, &http_code, NULL)) {
            if (error_callback) error_callback(parse_error("Mock request failed"));
            goto cleanup;
        }
        if (http_code > 299) {
            zb_report_body_error(response_data.response, http_code, error_callback);
        } else if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            } else {
                ZBSendFileResponse typed_response = zb_send_file_response_from_json(j_obj);
                if (!zb_shape_error_reported(http_code, error_callback)) {
                    success_callback(typed_response);
                } else {
                    /* Never handed to the caller. */
                    zb_send_file_response_free(&typed_response);
                }
                json_object_put(j_obj);
            }
        }
        goto cleanup;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        if (error_callback) {
            error_callback(parse_error("Failed to initialize libcurl"));
        }
        curl_easy_cleanup(curl);
        goto cleanup;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_URL, url_path);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: multipart/form-data");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_mime *multipart = curl_mime_init(curl);

    curl_mimepart *part = curl_mime_addpart(multipart);
    curl_mime_name(part, "api_key");
    curl_mime_data(part, zb->api_key, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, file_path);

    /* Room for any int: INT_MIN is 11 characters and a NUL. */
    char int_str_option[16];

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "email_address_column");
    snprintf(int_str_option, sizeof(int_str_option), "%d", email_address_column_index);
    curl_mime_data(part, int_str_option, CURL_ZERO_TERMINATED);

    if (!scoring) {
        if (options.first_name_column > 0) {
            part = curl_mime_addpart(multipart);
            curl_mime_name(part, "first_name_column");
            snprintf(int_str_option, sizeof(int_str_option), "%d", options.first_name_column);
            curl_mime_data(part, int_str_option, CURL_ZERO_TERMINATED);
        }
        if (options.last_name_column > 0) {
            part = curl_mime_addpart(multipart);
            curl_mime_name(part, "last_name_column");
            snprintf(int_str_option, sizeof(int_str_option), "%d", options.last_name_column);
            curl_mime_data(part, int_str_option, CURL_ZERO_TERMINATED);
        }
        if (options.gender_column > 0) {
            part = curl_mime_addpart(multipart);
            curl_mime_name(part, "gender_column");
            snprintf(int_str_option, sizeof(int_str_option), "%d", options.gender_column);
            curl_mime_data(part, int_str_option, CURL_ZERO_TERMINATED);
        }
        if (options.ip_address_column > 0) {
            part = curl_mime_addpart(multipart);
            curl_mime_name(part, "ip_address_column");
            snprintf(int_str_option, sizeof(int_str_option), "%d", options.ip_address_column);
            curl_mime_data(part, int_str_option, CURL_ZERO_TERMINATED);
        }
    }

    /* A caller that zeroed SendFileOptions instead of calling
       new_send_file_options leaves return_url NULL, so it is checked here the
       same way the field notes above check it. */
    if (options.return_url && options.return_url[0] != '\0') {
        part = curl_mime_addpart(multipart);
        curl_mime_name(part, "return_url");
        curl_mime_data(part, options.return_url, CURL_ZERO_TERMINATED);
    }

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "has_header_row");
    curl_mime_data(part, options.has_header_row ? "true" : "false", CURL_ZERO_TERMINATED);

    /* Always sent, with the caller's value (true from new_send_file_options),
       as in 3.0.x. */
    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "remove_duplicate");
    curl_mime_data(part, options.remove_duplicate ? "true" : "false", CURL_ZERO_TERMINATED);

    if (!scoring && options.allow_phase_2_is_set) {
        part = curl_mime_addpart(multipart);
        curl_mime_name(part, "allow_phase_2");
        curl_mime_data(part, options.allow_phase_2 ? "true" : "false", CURL_ZERO_TERMINATED);
    }

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, multipart);

    set_write_callback(curl, &response_data);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        zb_report_perform_error(curl, res, error_callback);

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_mime_free(multipart);
        goto cleanup;
    }

    long http_code = get_http_code(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_mime_free(multipart);

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }

            ZBSendFileResponse typed_response = zb_send_file_response_from_json(j_obj);
            if (!zb_shape_error_reported(http_code, error_callback)) {
                success_callback(typed_response);
            } else {
                /* Never handed to the caller. */
                zb_send_file_response_free(&typed_response);
            }
            json_object_put(j_obj);
        }
    }

    cleanup:
    zb_secure_free(url_path);
    free(response_data.response);
}


static void file_status_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/filestatus?api_key=%s&file_id=%s";
    const char* base_url = scoring ? zb->bulk_api_scoring_base_url : zb->bulk_api_base_url;

    char* api_key_esc = zb_url_escape(zb->api_key);
    char* file_id_esc = zb_url_escape(file_id);
    int url_path_len = snprintf(
        NULL, 0, url_pattern, base_url, api_key_esc, file_id_esc
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        zb_secure_free(api_key_esc);
        free(file_id_esc);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, base_url, api_key_esc, file_id_esc
    );
    zb_secure_free(api_key_esc);
    free(file_id_esc);

    memory response_data = {0};
    long http_code;

    if(!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }

            ZBFileStatusResponse typed_response = zb_file_status_response_from_json(j_obj);
            if (!zb_shape_error_reported(http_code, error_callback)) {
                success_callback(typed_response);
            } else {
                /* Never handed to the caller. */
                zb_file_status_response_free(&typed_response);
            }
            json_object_put(j_obj);
        }
    }

    cleanup:
    zb_secure_free(url_path);
    free(response_data.response);
}


static void get_file_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *get_file_options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    GetFileOptions default_opts = new_get_file_options();
    if (!get_file_options) {
        get_file_options = &default_opts;
    }

    char *url_path = zb_build_getfile_url(zb, scoring, file_id, get_file_options);
    if (!url_path) {
        if (error_callback) {
            error_callback(parse_error("Failed to build getfile URL"));
        }
        return;
    }

    memory response_data = {0};
    response_data.max_bytes = ZB_MAX_GETFILE_BYTES;
    long http_code = 0;
    char* content_type = NULL;

    zb_note_http_request("GET", url_path, NULL);

    if (!zb_check_request_url(url_path, error_callback)) {
        goto cleanup;
    }

    if (s_http_perform) {
        if (!zb_mock_perform(&response_data, &http_code, &content_type)) {
            if (error_callback) error_callback(parse_error("Mock request failed"));
            goto cleanup;
        }
        if (!content_type) {
            content_type = strdup("application/octet-stream");
        }
    } else {
        CURL* curl = curl_easy_init();
        if (!curl) {
            if (error_callback) {
                error_callback(parse_error("Failed to initialize libcurl"));
            }
            goto cleanup;
        }

        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
        curl_easy_setopt(curl, CURLOPT_URL, url_path);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        set_write_callback(curl, &response_data);

        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            zb_report_perform_error(curl, res, error_callback);

            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            goto cleanup;
        }

        http_code = get_http_code(curl);
        struct curl_header *hdr = NULL;
        CURLHcode hch = curl_easy_header(curl, "Content-Type", 0, CURLH_HEADER, -1, &hdr);
        if (hch == CURLHE_OK && hdr && hdr->value) {
            content_type = strdup(hdr->value);
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    const char *body = response_data.response ? response_data.response : "";

    /* An HTML page, by its body or its content type, is a proxy or portal
       answering, not the file. */
    if (http_code > 299 || zb_body_looks_like_html(body) || zb_content_type_is_html(content_type) ||
        zb_download_body_is_blank(body)) {
        /* A body of nothing but whitespace or byte order marks is an empty
           one: nothing is written. */
        zb_report_body_error(zb_download_body_is_blank(body) ? "" : body, http_code, error_callback);
    } else {
        /* The file is written whether or not a success callback was passed. */
        /* A JSON body, whatever its keys or content type, is the API saying
           why there is no file. */
        if (zb_download_body_is_json(body, content_type)) {
            zb_report_download_json_error(body, http_code, error_callback);
        } else {
            struct stat sb;

            #ifdef _WIN32
            const char path_separator = '\\';
            #define mkdir(path, mode) _mkdir(path)
            #else
            const char path_separator = '/';
            #endif

            if (stat(local_download_path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
                if (error_callback) {
                    error_callback(parse_error("Invalid file path"));
                }
                goto cleanup;
            }

            char* dir_path = strdup(local_download_path);
            char* end = strrchr(dir_path, path_separator);
            if (end != NULL) {
                *end = '\0';
                mkdir(dir_path, 0777);
            }
            free(dir_path);

            /* Written next to the path and moved into place when complete, so
               a failed write neither leaves a partial file nor touches a file
               already at the path. */
            int write_result = zb_write_file_replacing(
                local_download_path, response_data.response, response_data.size);
            if (write_result == ZB_WRITE_OPEN_FAILED) {
                if (error_callback) {
                    error_callback(parse_error("Could not open local_download_path for writing"));
                }
                goto cleanup;
            }
            if (write_result != 0) {
                if (error_callback) {
                    error_callback(parse_error("Could not write local_download_path"));
                }
                goto cleanup;
            }

            if (success_callback) {
                ZBGetFileResponse response = new_zb_get_file_response();
                response.success = true;
                free(response.local_file_path);
                response.local_file_path = strdup(local_download_path);
                success_callback(response);
            }
        }
    }

    cleanup:
    zb_secure_free(url_path);
    free(response_data.response);
    free(content_type);
}


static void delete_file_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/deletefile?api_key=%s&file_id=%s";
    const char* base_url = scoring ? zb->bulk_api_scoring_base_url : zb->bulk_api_base_url;

    char* api_key_esc = zb_url_escape(zb->api_key);
    char* file_id_esc = zb_url_escape(file_id);
    int url_path_len = snprintf(
        NULL, 0, url_pattern, base_url, api_key_esc, file_id_esc
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        zb_secure_free(api_key_esc);
        free(file_id_esc);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, base_url, api_key_esc, file_id_esc
    );
    zb_secure_free(api_key_esc);
    free(file_id_esc);

    memory response_data = {0};
    long http_code;

    if(!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }

            ZBDeleteFileResponse typed_response = zb_delete_file_response_from_json(j_obj);
            if (!zb_shape_error_reported(http_code, error_callback)) {
                success_callback(typed_response);
            } else {
                /* Never handed to the caller. */
                zb_delete_file_response_free(&typed_response);
            }
            json_object_put(j_obj);
        }
    }

    cleanup:
    zb_secure_free(url_path);
    free(response_data.response);
}

/*
 * A guessformat answer that carries a (non-null) message key and none of the fields a
 * result has (each missing or null), such as {"message":"Invalid domain",
 * "domain":null}, is the API refusing the call, not a result. Reports the
 * unexpected-shape error with the real status and returns true. A real field
 * beside the message, or no message at all, is a result.
 */
static bool zb_report_message_without_result(
    json_object *j_obj,
    const char *const *fields,
    size_t field_count,
    long http_code,
    OnErrorCallback error_callback
) {
    static const char *const message_keys[] = {
        "error", "message", "error_message", "Message", "Error", "detail"
    };
    bool has_message = false;
    size_t i;
    for (i = 0; i < sizeof(message_keys) / sizeof(message_keys[0]); i++) {
        json_object *message = NULL;
        /* A null message is no message: null is never an error on its own. */
        if (json_object_object_get_ex(j_obj, message_keys[i], &message) && message &&
            !json_object_is_type(message, json_type_null)) {
            has_message = true;
            break;
        }
    }
    if (!has_message) {
        return false;
    }
    for (i = 0; i < field_count; i++) {
        json_object *value = NULL;
        if (json_object_object_get_ex(j_obj, fields[i], &value) && value &&
            !json_object_is_type(value, json_type_null)) {
            return false;
        }
    }
    if (error_callback) {
        error_callback(parse_error_with_status(
            "Unexpected JSON response: a message and none of the result fields.", http_code));
    }
    return true;
}

static const char *const zb_find_email_fields[] = {
    "email", "email_confidence", "domain", "company_name", "did_you_mean", "failure_reason"
};
static const char *const zb_search_domain_fields[] = {
    "domain", "company_name", "format", "confidence", "did_you_mean", "failure_reason",
    "other_domain_formats"
};

static void find_email_internal(
    ZeroBounce* zb,
    char* domain,
    char* company_name,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    StringVector string_vector = string_vector_init();
    string_vector_append(&string_vector, zb->api_base_url);
    string_vector_append(&string_vector, "/guessformat?api_key=");
    char* api_key_esc = zb_url_escape(zb->api_key);
    string_vector_append(&string_vector, api_key_esc);
    zb_secure_free(api_key_esc);
    zb_append_query_param(&string_vector, "domain", domain);
    zb_append_query_param(&string_vector, "company_name", company_name);
    zb_append_query_param(&string_vector, "first_name", first_name);
    zb_append_query_param(&string_vector, "middle_name", middle_name);
    zb_append_query_param(&string_vector, "last_name", last_name);
    char *url_path = concatenate_strings(&string_vector, "");
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    };

    memory response_data = {0};
    long http_code;

    if(!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }
            if (zb_report_message_without_result(
                    j_obj, zb_find_email_fields,
                    sizeof(zb_find_email_fields) / sizeof(zb_find_email_fields[0]),
                    http_code, error_callback)) {
                json_object_put(j_obj);
                goto cleanup;
            }
            ZBFindEmailResponse response_obj = zb_find_email_response_from_json(j_obj);
            if (!zb_shape_error_reported(http_code, error_callback)) {
                success_callback(response_obj);
            }
            zb_find_email_response_free(&response_obj);
            json_object_put(j_obj);
        }
    }

    cleanup:
    string_vector_free(&string_vector);
    zb_secure_free(url_path);
    free(response_data.response);
}

static void search_domain_internal(
    ZeroBounce* zb,
    char* domain,
    char* company_name,
    OnSuccessCallbackDomainSearch success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    StringVector string_vector = string_vector_init();
    string_vector_append(&string_vector, zb->api_base_url);
    string_vector_append(&string_vector, "/guessformat?api_key=");
    char* api_key_esc = zb_url_escape(zb->api_key);
    string_vector_append(&string_vector, api_key_esc);
    zb_secure_free(api_key_esc);
    zb_append_query_param(&string_vector, "domain", domain);
    zb_append_query_param(&string_vector, "company_name", company_name);
    char *url_path = concatenate_strings(&string_vector, "");
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    };

    memory response_data = {0};
    long http_code;

    if(!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }
            if (zb_report_message_without_result(
                    j_obj, zb_search_domain_fields,
                    sizeof(zb_search_domain_fields) / sizeof(zb_search_domain_fields[0]),
                    http_code, error_callback)) {
                json_object_put(j_obj);
                goto cleanup;
            }
            ZBDomainSearchResponse response_obj = zb_domain_search_response_from_json(j_obj);
            if (!zb_shape_error_reported(http_code, error_callback)) {
                success_callback(response_obj);
            }
            zb_domain_search_response_free(&response_obj);
            json_object_put(j_obj);
        }
    }

    cleanup:
    string_vector_free(&string_vector);
    zb_secure_free(url_path);
    free(response_data.response);
}


void get_credits(
    ZeroBounce *zb,
    OnSuccessCallbackCredits success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/getcredits?api_key=%s";

    char* api_key_esc = zb_url_escape(zb->api_key);
    int url_path_len = snprintf(
        NULL, 0, url_pattern, zb->api_base_url, api_key_esc
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        zb_secure_free(api_key_esc);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, zb->api_base_url, api_key_esc
    );
    zb_secure_free(api_key_esc);

    memory response_data = {0};
    long http_code;

    if(!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_success_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }

            ZBCreditsResponse typed_response = zb_credits_response_from_json(j_obj);
            if (!zb_shape_error_reported(http_code, error_callback)) {
                success_callback(typed_response);
            } else {
                /* Never handed to the caller. */
                zb_credits_response_free(&typed_response);
            }
            json_object_put(j_obj);
        }
    }

    cleanup:
    zb_secure_free(url_path);
    free(response_data.response);
}


void get_api_usage(
    ZeroBounce *zb,
    struct tm start_date,
    struct tm end_date,
    OnSuccessCallbackApiUsage success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    char* date_format = "%Y-%m-%d";
    char start_date_str[11];
    char end_date_str[11];

    strftime(start_date_str, sizeof(start_date_str), date_format, &start_date);
    strftime(end_date_str, sizeof(end_date_str), date_format, &end_date);

    const char *url_pattern = "%s/getapiusage?api_key=%s&start_date=%s&end_date=%s";

    char* api_key_esc = zb_url_escape(zb->api_key);
    char* start_esc = zb_url_escape(start_date_str);
    char* end_esc = zb_url_escape(end_date_str);
    int url_path_len = snprintf(
        NULL, 0, url_pattern, zb->api_base_url, api_key_esc, start_esc, end_esc
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        zb_secure_free(api_key_esc);
        free(start_esc);
        free(end_esc);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, zb->api_base_url, api_key_esc, start_esc, end_esc
    );
    zb_secure_free(api_key_esc);
    free(start_esc);
    free(end_esc);

    memory response_data = {0};
    long http_code;

    if(!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }

            ZBGetApiUsageResponse typed_response = zb_get_api_usage_response_from_json(j_obj);
            if (!zb_shape_error_reported(http_code, error_callback)) {
                success_callback(typed_response);
            } else {
                /* Never handed to the caller. */
                zb_get_api_usage_response_free(&typed_response);
            }
            json_object_put(j_obj);
        }
    }

    cleanup:
    zb_secure_free(url_path);
    free(response_data.response);
}


void validate_email(
    ZeroBounce *zb,
    char* email,
    char* ip_address,
    OnSuccessCallbackValidate success_callback,
    OnErrorCallback error_callback
) {
    validate_email_with_options(zb, email, ip_address, NULL, success_callback, error_callback);
}

void validate_email_with_options(
    ZeroBounce *zb,
    char* email,
    char* ip_address,
    const ZBValidateOptions *options,
    OnSuccessCallbackValidate success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    ZBValidateOptions default_options = new_validate_options();
    if (!options) {
        options = &default_options;
    }
    if (!zb_check_validate_timeout(options->timeout, error_callback)) {
        return;
    }
    if (!zb_check_callback_url(options, error_callback)) {
        return;
    }

    if (options->use_post == 1) {
        char *url_path = malloc(strlen(zb->api_base_url) + 16);
        if (!url_path) {
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
        snprintf(url_path, strlen(zb->api_base_url) + 16, "%s/validate", zb->api_base_url);

        char *api_key_esc = zb_url_escape(zb->api_key);
        char *email_esc = zb_url_escape(email ? email : "");
        char *ip_esc = zb_url_escape(ip_address ? ip_address : "");
        if (!api_key_esc || !email_esc || !ip_esc) {
            zb_secure_free(api_key_esc);
            free(email_esc);
            free(ip_esc);
            zb_secure_free(url_path);
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }

        size_t body_cap = strlen(api_key_esc) + strlen(email_esc) + strlen(ip_esc) + 128;
        if (options->timeout >= 0) {
            body_cap += 32;
        }
        if (options->activity_data >= 0 || options->verify_plus >= 0) {
            body_cap += 64;
        }
        char *post_body = malloc(body_cap);
        if (!post_body) {
            zb_secure_free(api_key_esc);
            free(email_esc);
            free(ip_esc);
            zb_secure_free(url_path);
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
        snprintf(
            post_body, body_cap, "api_key=%s&email=%s&ip_address=%s",
            api_key_esc, email_esc, ip_esc
        );
        zb_secure_free(api_key_esc);
        free(email_esc);
        free(ip_esc);

        if (options->timeout >= 0) {
            char timeout_buf[16];
            snprintf(timeout_buf, sizeof(timeout_buf), "%d", options->timeout);
            char *timeout_esc = zb_url_escape(timeout_buf);
            if (!timeout_esc || !zb_url_append_suffix(&post_body, "&timeout=") ||
                !zb_url_append_suffix(&post_body, timeout_esc)) {
                free(timeout_esc);
                zb_secure_free(post_body);
                zb_secure_free(url_path);
                fprintf(stderr, "Memory allocation failed.\n");
                exit(EXIT_FAILURE);
            }
            free(timeout_esc);
        }
        if (options->activity_data >= 0) {
            const char *suffix = options->activity_data ? "&activity_data=true" : "&activity_data=false";
            if (!zb_url_append_suffix(&post_body, suffix)) {
                zb_secure_free(post_body);
                zb_secure_free(url_path);
                fprintf(stderr, "Memory allocation failed.\n");
                exit(EXIT_FAILURE);
            }
        }
        if (options->verify_plus >= 0) {
            const char *suffix = options->verify_plus ? "&verify_plus=true" : "&verify_plus=false";
            if (!zb_url_append_suffix(&post_body, suffix)) {
                zb_secure_free(post_body);
                zb_secure_free(url_path);
                fprintf(stderr, "Memory allocation failed.\n");
                exit(EXIT_FAILURE);
            }
        }
        if (!zb_append_validate_extras(&post_body, options)) {
            zb_secure_free(post_body);
            zb_secure_free(url_path);
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }

        zero_bounce_record_request_url_for_test(url_path);

        memory response_data = {0};
        long http_code;

        if (!zb_make_request_with_post_body(url_path, post_body, &response_data, &http_code, error_callback)) {
            zb_secure_free(url_path);
            zb_secure_free(post_body);
            return;
        }

        if (http_code > 299) {
            zb_report_body_error(response_data.response, http_code, error_callback);
        } else if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_success_json(response_data.response, http_code, &j_obj, error_callback)) {
                /* reported */
            } else {
                ZBValidateResponse typed_response = zb_validate_response_from_json(j_obj);
                if (!zb_shape_error_reported(http_code, error_callback)) {
                    success_callback(typed_response);
                } else {
                    /* Never handed to the caller. */
                    zb_validate_response_free(&typed_response);
                }
                json_object_put(j_obj);
            }
        }

        zb_secure_free(url_path);
        zb_secure_free(post_body);
        free(response_data.response);
        return;
    }

    char* api_key_esc = zb_url_escape(zb->api_key);
    char* email_esc = zb_url_escape(email ? email : "");
    char* ip_esc = zb_url_escape(ip_address ? ip_address : "");

    size_t url_cap = strlen(zb->api_base_url) + strlen(api_key_esc) + strlen(email_esc) + strlen(ip_esc) + 256;
    if (options->timeout >= 0) {
        url_cap += 32;
    }
    if (options->activity_data >= 0 || options->verify_plus >= 0) {
        url_cap += 64;
    }

    char *url_path = malloc(url_cap);
    if (!url_path) {
        zb_secure_free(api_key_esc);
        free(email_esc);
        free(ip_esc);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_cap, "%s/validate?api_key=%s&email=%s&ip_address=%s",
        zb->api_base_url, api_key_esc, email_esc, ip_esc
    );
    if (options->timeout >= 0) {
        char timeout_buf[16];
        snprintf(timeout_buf, sizeof(timeout_buf), "%d", options->timeout);
        char* timeout_esc = zb_url_escape(timeout_buf);
        char* extended = zb_secure_realloc(url_path, strlen(url_path) + strlen(timeout_esc) + 32);
        if (!extended) {
            free(timeout_esc);
            zb_secure_free(api_key_esc);
            free(email_esc);
            free(ip_esc);
            zb_secure_free(url_path);
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
        url_path = extended;
        strcat(url_path, "&timeout=");
        strcat(url_path, timeout_esc);
        free(timeout_esc);
    }
    if (options->activity_data >= 0) {
        const char *suffix = options->activity_data ? "&activity_data=true" : "&activity_data=false";
        if (!zb_url_append_suffix(&url_path, suffix)) {
            zb_secure_free(url_path);
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
    }
    if (options->verify_plus >= 0) {
        const char *suffix = options->verify_plus ? "&verify_plus=true" : "&verify_plus=false";
        if (!zb_url_append_suffix(&url_path, suffix)) {
            zb_secure_free(url_path);
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
    }
    if (!zb_append_validate_extras(&url_path, options)) {
        zb_secure_free(url_path);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    zb_secure_free(api_key_esc);
    free(email_esc);
    free(ip_esc);

    zero_bounce_record_request_url_for_test(url_path);

    memory response_data = {0};
    long http_code;

    if(!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_success_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }

            ZBValidateResponse typed_response = zb_validate_response_from_json(j_obj);
            if (!zb_shape_error_reported(http_code, error_callback)) {
                success_callback(typed_response);
            } else {
                /* Never handed to the caller. */
                zb_validate_response_free(&typed_response);
            }
            json_object_put(j_obj);
        }
    }

    cleanup:
    zb_secure_free(url_path);
    free(response_data.response);
}

void validate_email_batch(
    ZeroBounce *zb,
    EmailToValidateVector email_batch,
    OnSuccessCallbackValidateBatch success_callback,
    OnErrorCallback error_callback
) {
    validate_email_batch_with_options(zb, email_batch, NULL, success_callback, error_callback);
}

void validate_email_batch_with_options(
    ZeroBounce *zb,
    EmailToValidateVector email_batch,
    const ZBValidateOptions *options,
    OnSuccessCallbackValidateBatch success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    ZBValidateOptions default_options = new_validate_options();
    if (!options) {
        options = &default_options;
    }
    if (!zb_check_validate_batch_timeout(options->timeout, error_callback)) {
        return;
    }

    const char *url_pattern = "%s/validatebatch";

    int url_path_len = snprintf(
        NULL, 0, url_pattern, zb->api_base_url
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, zb->api_base_url
    );

    zero_bounce_record_request_url_for_test(url_path);

    json_object *payload = json_object_new_object();
    json_object *email_batch_json = json_object_new_array();

    for(size_t i = 0; i < email_batch.size; i++) {
        json_object *email_obj = json_object_new_object();
        json_object_object_add(email_obj, "email_address", json_object_new_string(email_batch.data[i].email_address ? email_batch.data[i].email_address : ""));
        if (!email_batch.data[i].ip_address || !email_batch.data[i].ip_address[0]) {
            json_object_object_add(email_obj, "ip_address", json_object_new_null());
        } else {
            json_object_object_add(email_obj, "ip_address", json_object_new_string(email_batch.data[i].ip_address));
        }
        json_object_array_add(email_batch_json, email_obj);
    }

    json_object_object_add(payload, "api_key", json_object_new_string(zb->api_key));
    json_object_object_add(payload, "email_batch", email_batch_json);
    if (options->timeout >= 0) {
        /*
         * LIVE: validatebatch takes timeout as a JSON *string*. Sent as a
         * number the API rejects the whole batch with the account-level
         * "Invalid API Key or your account ran out of credits".
         */
        char timeout_text[16];
        snprintf(timeout_text, sizeof(timeout_text), "%d", options->timeout);
        json_object_object_add(payload, "timeout", json_object_new_string(timeout_text));
    }
    if (options->activity_data >= 0) {
        json_object_object_add(
            payload,
            "activity_data",
            json_object_new_boolean(options->activity_data ? 1 : 0)
        );
    }
    if (options->verify_plus >= 0) {
        json_object_object_add(
            payload,
            "verify_plus",
            json_object_new_boolean(options->verify_plus ? 1 : 0)
        );
    }

    const char* payload_str = json_object_to_json_string(payload);
    zb_note_http_request("POST", url_path, payload_str);

    memory response_data = {0};
    long http_code = 0;

    if (!zb_check_request_url(url_path, error_callback)) {
        json_object_put(payload);
        zb_secure_free(url_path);
        return;
    }

    if (s_http_perform) {
        json_object_put(payload);
        if (!zb_mock_perform(&response_data, &http_code, NULL)) {
            if (error_callback) error_callback(parse_error("Mock request failed"));
            return;
        }
    } else {
        CURL* curl = curl_easy_init();
        if (!curl) {
            if (error_callback) {
                error_callback(parse_error("Failed to initialize libcurl"));
            }
            curl_easy_cleanup(curl);
            json_object_put(payload);
            return;
        }

        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
        curl_easy_setopt(curl, CURLOPT_URL, url_path);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, json_object_to_json_string(payload));

        json_object_put(payload);

        set_write_callback(curl, &response_data);

        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            zb_report_perform_error(curl, res, error_callback);

            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            return;
        }

        http_code = get_http_code(curl);

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }

            char *account_error = NULL;
            if (zb_batch_has_account_error(j_obj, &account_error)) {
                if (error_callback) {
                    error_callback(parse_error_with_status(account_error, http_code));
                }
                free(account_error);
            } else {
                ZBValidateBatchResponse typed_response = zb_validate_batch_response_from_json(j_obj);
                if (!zb_shape_error_reported(http_code, error_callback)) {
                    success_callback(typed_response);
                } else {
                    /* Never handed to the caller. */
                    zb_validate_batch_response_free(&typed_response);
                }
            }
            json_object_put(j_obj);
        }
    }

    cleanup:
    zb_secure_free(url_path);
    free(response_data.response);
}


void send_file(
    ZeroBounce *zb,
    char* file_path,
    int email_address_column_index,
    SendFileOptions options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    send_file_internal(zb, false, file_path, email_address_column_index, options, success_callback, error_callback);
}


void file_status(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
) {
    file_status_internal(zb, false, file_id, success_callback, error_callback);
}



/*
 * A streaming getfile download: the body goes straight to disk as it arrives
 * instead of being held in memory first.
 *
 * It is opt-in because a streamed body cannot be inspected as a whole, and
 * getfile answers an unknown or deleted file id with a JSON error rather than
 * a file. The first chunk is held back and checked; only once it is clearly
 * not an error does the file get created and the rest stream through.
 */
#define ZB_STREAM_PROBE_BYTES 4096
/* How far past the padding the HTML check looks. */
#define ZB_HTML_WINDOW 2000

typedef struct {
    FILE *file;
    const char *path;
    /* The file being written, next to path; renamed to path only once the
       whole download has arrived, so a failed one never touches a file
       already at path. NULL until the probe has passed. */
    char *temp_path;
    char probe[ZB_STREAM_PROBE_BYTES + 1];
    size_t probe_size;
    /* NULL for the offline test hook. */
    CURL *curl;
    /* The content type the test hook supplied, when there is no handle. */
    const char *content_type;
    bool decided;
    /* A byte that is not padding has arrived. */
    bool past_padding;
    bool rejected;
    /* The rejection is a JSON answer (not an HTML page): the API's message. */
    bool json_error;
    bool open_failed;
    bool write_failed;
    /* Leading padding moved out of the probe so the HTML check can see past
       it. Written ahead of the file when the body is a file; freed otherwise. */
    char *kept_prefix;
    size_t kept_prefix_size;
} ZbDownloadStream;

/* Whether the held-back start of the body is the API's error, not a file. */
static bool zb_stream_probe_is_error(ZbDownloadStream *stream) {
    const char *content_type = stream->content_type;
    if (stream->curl) {
        char *from_curl = NULL;
        if (curl_easy_getinfo(stream->curl, CURLINFO_CONTENT_TYPE, &from_curl) == CURLE_OK) {
            content_type = from_curl;
        }
    }
    if (zb_body_looks_like_html(stream->probe) || zb_content_type_is_html(content_type)) {
        return true;
    }
    if (zb_success_means_failure(stream->probe) || zb_download_body_is_json(stream->probe, content_type)) {
        stream->json_error = true;
        return true;
    }
    return false;
}

/* Reports a rejected stream: a JSON answer under a success status is the API's
   message, anything else is the body as it came. */
static void zb_stream_report_rejection(
    const ZbDownloadStream *stream, long http_code, OnErrorCallback error_callback
) {
    if (http_code <= 299 && stream->json_error) {
        /* The probe is full and still not one JSON value: the error was cut
           off. The truncated text is not the API's message. */
        if (stream->probe_size == ZB_STREAM_PROBE_BYTES) {
            json_object *parsed = json_tokener_parse(zb_download_skip_blank(stream->probe));
            if (!parsed) {
                if (error_callback) {
                    error_callback(parse_error_with_status(
                        "The download was a JSON error larger than this client reads.",
                        http_code));
                }
                return;
            }
            json_object_put(parsed);
        }
        zb_report_download_json_error(stream->probe, http_code, error_callback);
    } else {
        zb_report_body_error(stream->probe, http_code, error_callback);
    }
}

/* Writes one chunk; a short write stops the transfer (CURLE_WRITE_ERROR). */
static bool zb_stream_put(ZbDownloadStream *stream, const void *data, size_t size) {
    if (size && fwrite(data, 1, size, stream->file) != size) {
        stream->write_failed = true;
        return false;
    }
    return true;
}

static void zb_stream_drop_prefix(ZbDownloadStream *stream) {
    free(stream->kept_prefix);
    stream->kept_prefix = NULL;
    stream->kept_prefix_size = 0;
}

/* Remembers the padding at the front of the probe. A later file write emits
   it first; a rejection frees it. */
static bool zb_stream_stash_padding(ZbDownloadStream *stream, size_t skipped) {
    char *grown = realloc(stream->kept_prefix, stream->kept_prefix_size + skipped);
    if (!grown) {
        return false;
    }
    memcpy(grown + stream->kept_prefix_size, stream->probe, skipped);
    stream->kept_prefix = grown;
    stream->kept_prefix_size += skipped;
    return true;
}

static bool zb_stream_write_kept(ZbDownloadStream *stream) {
    bool ok = true;
    if (stream->kept_prefix_size &&
        !zb_stream_put(stream, stream->kept_prefix, stream->kept_prefix_size)) {
        ok = false;
    }
    zb_stream_drop_prefix(stream);
    return ok;
}

static bool zb_stream_open(ZbDownloadStream *stream) {
    struct stat sb;
    char *dir_path;
    char *end;
#ifdef _WIN32
    const char path_separator = '\\';
#else
    const char path_separator = '/';
#endif

    if (stat(stream->path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        return false;
    }
    dir_path = strdup(stream->path);
    if (dir_path) {
        end = strrchr(dir_path, path_separator);
        if (end) {
            *end = '\0';
            mkdir(dir_path, 0777);
        }
        free(dir_path);
    }
    stream->temp_path = zb_temp_path_for(stream->path, stream);
    if (!stream->temp_path) {
        return false;
    }
    stream->file = fopen(stream->temp_path, "wb");
    if (!stream->file) {
        free(stream->temp_path);
        stream->temp_path = NULL;
        return false;
    }
    return true;
}

/* How many bytes at the start of data (size bytes) are whole padding units:
   ASCII whitespace, a byte order mark (EF BB BF) or a no-break space (C2 A0).
   A unit cut off by the end of data is not counted. */
static size_t zb_stream_padding_length(const char *data, size_t size) {
    size_t i = 0;
    while (i < size) {
        unsigned char c = (unsigned char)data[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
            i++;
        } else if (c == 0xEF && size - i >= 3 && (unsigned char)data[i + 1] == 0xBB &&
                   (unsigned char)data[i + 2] == 0xBF) {
            i += 3;
        } else if (c == 0xC2 && size - i >= 2 && (unsigned char)data[i + 1] == 0xA0) {
            i += 2;
        } else {
            break;
        }
    }
    return i;
}

/* Whether the size bytes at data (at most two) could be the start of a byte
   order mark or a no-break space that the next chunk completes. */
static bool zb_stream_padding_partial(const char *data, size_t size) {
    if (size == 1) {
        return (unsigned char)data[0] == 0xEF || (unsigned char)data[0] == 0xC2;
    }
    return size == 2 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB;
}

static size_t zb_stream_write(void *contents, size_t size, size_t nmemb, void *userp) {
    ZbDownloadStream *stream = (ZbDownloadStream *)userp;
    size_t total = size * nmemb;
    const char *data = (const char *)contents;
    size_t left = total;

    if (stream->rejected || stream->open_failed) {
        /* Keep draining so libcurl reports the HTTP status, not a write error. */
        return total;
    }
    if (!stream->decided) {
        while (left) {
            size_t room = ZB_STREAM_PROBE_BYTES - stream->probe_size;
            size_t take = left < room ? left : room;
            if (take == 0) {
                /* A full probe that took nothing would spin: reject. */
                stream->rejected = true;
                return total;
            }
            memcpy(stream->probe + stream->probe_size, data, take);
            stream->probe_size += take;
            data += take;
            left -= take;
            stream->probe[stream->probe_size] = '\0';
            if (!stream->past_padding) {
                /* Padding decides nothing, however much of it comes. A
                   split byte order mark or no-break space (at most two
                   bytes) is left for the next chunk to finish. Padding is
                   kept while it fits in the probe along with what follows;
                   once the probe is full of it, it is dropped, so the cap
                   never turns padding into a file. */
                size_t skipped = zb_stream_padding_length(stream->probe, stream->probe_size);
                size_t rest = stream->probe_size - skipped;
                if (rest == 0 || zb_stream_padding_partial(stream->probe + skipped, rest)) {
                    if (stream->probe_size == ZB_STREAM_PROBE_BYTES) {
                        memmove(stream->probe, stream->probe + skipped, rest);
                        stream->probe_size = rest;
                        stream->probe[rest] = '\0';
                    }
                    continue;
                }
                stream->past_padding = true;
            }
            if (stream->probe_size == ZB_STREAM_PROBE_BYTES) {
                /* Padding, then too little to judge: set the padding aside so
                   the HTML check can see ZB_HTML_WINDOW bytes after it. A file
                   is written with that padding put back in front. */
                size_t skipped = zb_stream_padding_length(stream->probe, stream->probe_size);
                size_t rest = stream->probe_size - skipped;
                if (skipped > 0 && rest < ZB_HTML_WINDOW) {
                    if (!zb_stream_stash_padding(stream, skipped)) {
                        stream->rejected = true;
                        return total;
                    }
                    memmove(stream->probe, stream->probe + skipped, rest);
                    stream->probe_size = rest;
                    stream->probe[rest] = '\0';
                }
            }
            if (stream->probe_size < ZB_STREAM_PROBE_BYTES) {
                continue;
            }
            break;
        }
        if (!stream->past_padding || stream->probe_size < ZB_STREAM_PROBE_BYTES) {
            return total;
        }
        /* Enough to tell a file from an error page. */
        stream->decided = true;
        if (zb_stream_probe_is_error(stream)) {
            stream->rejected = true;
            return total;
        }
        if (!zb_stream_open(stream)) {
            stream->open_failed = true;
            return total;
        }
        if (!zb_stream_write_kept(stream) ||
            !zb_stream_put(stream, stream->probe, stream->probe_size) ||
            !zb_stream_put(stream, data, left)) {
            return 0;
        }
        return total;
    }
    if (stream->write_failed || !zb_stream_put(stream, contents, total)) {
        return 0;
    }
    return total;
}

/* Flushes whatever the probe still holds when the body ended before it was
   full. */
static void zb_stream_finish(ZbDownloadStream *stream) {
    if (stream->decided || stream->rejected || stream->open_failed) {
        return;
    }
    stream->decided = true;
    if (stream->probe_size == 0 || zb_download_body_is_blank(stream->probe)) {
        /* Nothing, or nothing but whitespace and byte order marks, of any
           length: an empty response, so no file. */
        stream->probe[0] = '\0';
        stream->probe_size = 0;
        stream->rejected = true;
        return;
    }
    if (zb_stream_probe_is_error(stream)) {
        stream->rejected = true;
        return;
    }
    if (!zb_stream_open(stream)) {
        stream->open_failed = true;
        return;
    }
    if (!zb_stream_write_kept(stream)) {
        return;
    }
    zb_stream_put(stream, stream->probe, stream->probe_size);
}

/* Removes the file this call was writing, if any. A file at path is kept. */
static void zb_stream_discard(ZbDownloadStream *stream) {
    zb_stream_drop_prefix(stream);
    if (stream->temp_path) {
        remove(stream->temp_path);
        free(stream->temp_path);
        stream->temp_path = NULL;
    }
}

/*
 * Closes the file. A write that failed, or a close that could not flush the
 * last of it, removes the partial file and returns false.
 */
static bool zb_stream_close(ZbDownloadStream *stream) {
    if (stream->file) {
        if (fclose(stream->file) != 0) {
            stream->write_failed = true;
        }
        stream->file = NULL;
    }
    if (stream->write_failed) {
        zb_stream_discard(stream);
        return false;
    }
    return true;
}

/* Moves the finished file to path, replacing whatever was there. */
static bool zb_stream_commit(ZbDownloadStream *stream) {
    bool moved;
    if (!stream->temp_path) {
        return true;
    }
    moved = zb_replace_file(stream->temp_path, stream->path);
    if (!moved) {
        zb_stream_discard(stream);
        return false;
    }
    free(stream->temp_path);
    stream->temp_path = NULL;
    return true;
}

static void get_file_streaming_internal(
    ZeroBounce *zb,
    bool scoring,
    char *file_id,
    char *local_download_path,
    const GetFileOptions *options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    GetFileOptions default_opts = new_get_file_options();
    ZbDownloadStream stream;
    char *url_path;
    long http_code = 0;
    CURL *curl;
    CURLcode res;
    struct curl_slist *headers = NULL;

    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    if (!local_download_path || !local_download_path[0]) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: local_download_path"));
        }
        return;
    }
    if (!options) {
        options = &default_opts;
    }
    url_path = zb_build_getfile_url(zb, scoring, file_id, options);
    if (!url_path) {
        if (error_callback) {
            error_callback(parse_error("Failed to build getfile URL"));
        }
        return;
    }
    zb_note_http_request("GET", url_path, NULL);
    if (!zb_check_request_url(url_path, error_callback)) {
        zb_secure_free(url_path);
        return;
    }

    memset(&stream, 0, sizeof(stream));
    stream.path = local_download_path;

    if (s_http_perform) {
        /* The offline suite drives the same probe-then-stream logic. */
        memory buffer = {0};
        char *content_type = NULL;
        buffer.max_bytes = ZB_MAX_GETFILE_BYTES;
        if (!zb_mock_perform(&buffer, &http_code, &content_type)) {
            free(buffer.response);
            free(content_type);
            zb_secure_free(url_path);
            if (error_callback) {
                error_callback(parse_error("Mock request failed"));
            }
            return;
        }
        stream.content_type = content_type;
        if (buffer.response && buffer.size) {
            zb_stream_write(buffer.response, 1, buffer.size, &stream);
        }
        free(buffer.response);
        zb_secure_free(url_path);
        zb_stream_finish(&stream);
        free(content_type);
        stream.content_type = NULL;
        if (!zb_stream_close(&stream)) {
            if (error_callback) {
                error_callback(parse_error("Could not write local_download_path"));
            }
            return;
        }
        if (http_code > 299 || stream.rejected) {
            zb_stream_discard(&stream);
            zb_stream_report_rejection(&stream, http_code, error_callback);
            return;
        }
        if (stream.open_failed) {
            zb_stream_drop_prefix(&stream);
            if (error_callback) {
                error_callback(parse_error("Invalid file path"));
            }
            return;
        }
        if (!zb_stream_commit(&stream)) {
            if (error_callback) {
                error_callback(parse_error("Could not write local_download_path"));
            }
            return;
        }
        if (success_callback) {
            ZBGetFileResponse response = new_zb_get_file_response();
            response.success = true;
            free(response.local_file_path);
            response.local_file_path = strdup(local_download_path);
            success_callback(response);
        }
        return;
    }

    curl = curl_easy_init();
    if (!curl) {
        zb_secure_free(url_path);
        if (error_callback) {
            if (error_callback) {
                error_callback(parse_error("Failed to initialize libcurl"));
            }
        }
        return;
    }
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(curl, CURLOPT_URL, url_path);
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, zb_stream_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&stream);
    zb_set_transport_options(curl);
    stream.curl = curl;

    res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        zb_stream_finish(&stream);
    }
    stream.curl = NULL;
    http_code = get_http_code(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    zb_secure_free(url_path);

    if (!zb_stream_close(&stream)) {
        /* Checked before res: a failed write stops the transfer with
           CURLE_WRITE_ERROR, and this says why. */
        if (error_callback) {
            error_callback(parse_error("Could not write local_download_path"));
        }
        return;
    }
    if (res != CURLE_OK) {
        char message[300];
        snprintf(message, sizeof(message),
                 "Failed to perform request. CURLcode: %d. Error: %s",
                 res, curl_easy_strerror(res));
        if (error_callback) {
            error_callback(parse_error(message));
        }
        zb_stream_discard(&stream);
        return;
    }
    if (http_code > 299 || stream.rejected) {
        zb_stream_discard(&stream);
        zb_stream_report_rejection(&stream, http_code, error_callback);
        return;
    }
    if (stream.open_failed) {
        zb_stream_drop_prefix(&stream);
        if (error_callback) {
            error_callback(parse_error("Invalid file path"));
        }
        return;
    }
    if (!zb_stream_commit(&stream)) {
        if (error_callback) {
            error_callback(parse_error("Could not write local_download_path"));
        }
        return;
    }
    if (success_callback) {
        ZBGetFileResponse response = new_zb_get_file_response();
        response.success = true;
        free(response.local_file_path);
        response.local_file_path = strdup(local_download_path);
        success_callback(response);
    }
}

void get_file_streaming(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    get_file_streaming_internal(zb, false, file_id, local_download_path, options,
                                success_callback, error_callback);
}

void scoring_get_file_streaming(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    get_file_streaming_internal(zb, true, file_id, local_download_path, options,
                                success_callback, error_callback);
}

void get_file(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    get_file_internal(zb, false, file_id, local_download_path, NULL, success_callback, error_callback);
}


void get_file_with_options(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    get_file_internal(zb, false, file_id, local_download_path, options, success_callback, error_callback);
}


void delete_file(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
) {
    delete_file_internal(zb, false, file_id, success_callback, error_callback);
}

void scoring_send_file(
    ZeroBounce *zb,
    char* file_path,
    int email_address_column_index,
    SendFileOptions options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    send_file_internal(zb, true, file_path, email_address_column_index, options, success_callback, error_callback);
}

void scoring_file_status(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
) {
    file_status_internal(zb, true, file_id, success_callback, error_callback);
}

void scoring_get_file(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    get_file_internal(zb, true, file_id, local_download_path, NULL, success_callback, error_callback);
}


void scoring_get_file_with_options(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    get_file_internal(zb, true, file_id, local_download_path, options, success_callback, error_callback);
}

void scoring_delete_file(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
) {
    delete_file_internal(zb, true, file_id, success_callback, error_callback);
}


void get_activity_data(
    ZeroBounce *zb,
    char* email,
    OnSuccessCallbackActivityData success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/activity?api_key=%s&email=%s";

    char* api_key_esc = zb_url_escape(zb->api_key);
    char* email_esc = zb_url_escape(email ? email : "");
    int url_path_len = snprintf(
        NULL, 0, url_pattern, zb->api_base_url, api_key_esc, email_esc
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        zb_secure_free(api_key_esc);
        free(email_esc);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, zb->api_base_url, api_key_esc, email_esc
    );
    zb_secure_free(api_key_esc);
    free(email_esc);

    memory response_data = {0};
    long http_code;

    if(!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }

            ZBActivityDataResponse typed_response = zb_activity_data_response_from_json(j_obj);
            if (!zb_shape_error_reported(http_code, error_callback)) {
                success_callback(typed_response);
            } else {
                /* Never handed to the caller. */
                zb_activity_data_response_free(&typed_response);
            }
            json_object_put(j_obj);
        }
    }

    cleanup:
    zb_secure_free(url_path);
    free(response_data.response);
}

static void score_email_internal(
    ZeroBounce *zb,
    bool use_post,
    char* email,
    OnSuccessCallbackScoreEmail success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    char* api_key_esc = zb_url_escape(zb->api_key);
    char* email_esc = zb_url_escape(email ? email : "");

    char *url_path = NULL;
    char *post_body = NULL;

    if (use_post) {
        const char *url_pattern = "%s/scoring";
        int url_path_len = snprintf(NULL, 0, url_pattern, zb->api_base_url);
        url_path = malloc(url_path_len + 1);
        if (!url_path) {
            goto alloc_fail;
        }
        snprintf(url_path, url_path_len + 1, url_pattern, zb->api_base_url);

        const char *body_pattern = "api_key=%s&email=%s";
        int body_len = snprintf(NULL, 0, body_pattern, api_key_esc, email_esc);
        post_body = malloc(body_len + 1);
        if (!post_body) {
            goto alloc_fail;
        }
        snprintf(post_body, body_len + 1, body_pattern, api_key_esc, email_esc);
    } else {
        const char *url_pattern = "%s/scoring?api_key=%s&email=%s";
        int url_path_len = snprintf(
            NULL, 0, url_pattern, zb->api_base_url, api_key_esc, email_esc
        );
        url_path = malloc(url_path_len + 1);
        if (!url_path) {
            goto alloc_fail;
        }
        snprintf(
            url_path, url_path_len + 1, url_pattern, zb->api_base_url, api_key_esc, email_esc
        );
        zero_bounce_record_request_url_for_test(url_path);
    }

    zb_secure_free(api_key_esc);
    api_key_esc = NULL;
    free(email_esc);
    email_esc = NULL;

    memory response_data = {0};
    long http_code = 0;
    int ok = use_post
        ? zb_make_request_with_post_body(url_path, post_body, &response_data, &http_code, error_callback)
        : zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback);

    if (!ok) {
        goto cleanup;
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else if (success_callback) {
        json_object *j_obj = NULL;
        if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
            goto cleanup;
        }

        ZBScoreEmailResponse typed_response = zb_score_email_response_from_json(j_obj);
        if (!zb_shape_error_reported(http_code, error_callback)) {
            success_callback(typed_response);
        } else {
            /* Never handed to the caller. */
            zb_score_email_response_free(&typed_response);
        }
        json_object_put(j_obj);
    }

    goto cleanup;

alloc_fail:
    zb_secure_free(api_key_esc);
    free(email_esc);
    fprintf(stderr, "Memory allocation failed.\n");
    exit(EXIT_FAILURE);

cleanup:
    zb_secure_free(url_path);
    zb_secure_free(post_body);
    free(response_data.response);
}

void score_email(
    ZeroBounce *zb,
    char* email,
    OnSuccessCallbackScoreEmail success_callback,
    OnErrorCallback error_callback
) {
    score_email_internal(zb, false, email, success_callback, error_callback);
}

void score_email_post(
    ZeroBounce *zb,
    char* email,
    OnSuccessCallbackScoreEmail success_callback,
    OnErrorCallback error_callback
) {
    score_email_internal(zb, true, email, success_callback, error_callback);
}


void find_email_by_domain_first_middle_last_name(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, domain, NULL, first_name, middle_name, last_name, success_callback, error_callback);
}

void find_email_by_domain_first_last_name(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, domain, NULL, first_name, NULL, last_name, success_callback, error_callback);
}

void find_email_by_domain_first_name(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, domain, NULL, first_name, NULL, NULL, success_callback, error_callback);
}

void find_email_by_company_name_first_middle_last_name(
    ZeroBounce* zb,
    char* company_name,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, NULL, company_name, first_name, middle_name, last_name, success_callback, error_callback);
}

void find_email_by_company_name_first_last_name(
    ZeroBounce* zb,
    char* company_name,
    char* first_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, NULL, company_name, first_name, NULL, last_name, success_callback, error_callback);
}

void find_email_by_company_name_first_name(
    ZeroBounce* zb,
    char* company_name,
    char* first_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, NULL, company_name, first_name, NULL, NULL, success_callback, error_callback);
}

void search_domain_by_domain(
    ZeroBounce* zb,
    char* domain,
    OnSuccessCallbackDomainSearch success_callback,
    OnErrorCallback error_callback
) {
    search_domain_internal(zb, domain, NULL, success_callback, error_callback);
}

void search_domain_by_company_name(
    ZeroBounce* zb,
    char* company_name,
    OnSuccessCallbackDomainSearch success_callback,
    OnErrorCallback error_callback
) {
    search_domain_internal(zb, NULL, company_name, success_callback, error_callback);
}


void find_email(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    StringVector string_vector = string_vector_init();
    string_vector_append(&string_vector, zb->api_base_url);
    string_vector_append(&string_vector, "/guessformat?api_key=");
    char* api_key_esc = zb_url_escape(zb->api_key);
    string_vector_append(&string_vector, api_key_esc);
    zb_secure_free(api_key_esc);
    zb_append_query_param(&string_vector, "domain", domain);
    zb_append_query_param(&string_vector, "first_name", first_name);
    zb_append_query_param(&string_vector, "middle_name", middle_name);
    zb_append_query_param(&string_vector, "last_name", last_name);
    char *url_path = concatenate_strings(&string_vector, "");
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    };

    memory response_data = {0};
    long http_code;

    if(!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        zb_report_body_error(response_data.response, http_code, error_callback);
    } else {
        if (success_callback) {
            json_object *j_obj = NULL;
            if (!zb_read_non_message_json(response_data.response, http_code, &j_obj, error_callback)) {
                goto cleanup;
            }
            if (zb_report_message_without_result(
                    j_obj, zb_find_email_fields,
                    sizeof(zb_find_email_fields) / sizeof(zb_find_email_fields[0]),
                    http_code, error_callback)) {
                json_object_put(j_obj);
                goto cleanup;
            }
            ZBFindEmailResponse response_obj = zb_find_email_response_from_json(j_obj);
            if (!zb_shape_error_reported(http_code, error_callback)) {
                success_callback(response_obj);
            }
            zb_find_email_response_free(&response_obj);
            json_object_put(j_obj);
        }
    }

    cleanup:
    string_vector_free(&string_vector);
    zb_secure_free(url_path);
    free(response_data.response);
}