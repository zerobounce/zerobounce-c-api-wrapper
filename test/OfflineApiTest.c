#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"
#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/zb_internal.h"

#define OFFLINE_MAX 64

typedef struct {
    char method[16];
    char host[128];
    char path[256];
    char api_key[256];
    char body[8192];
    char query[4096];
    /* The one extra header the request carried, "Name: value", or "". */
    char extra_header[512];
} OfflineRequest;

static OfflineRequest g_requests[OFFLINE_MAX];
static int g_count;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static char g_error_text[1024];
static int g_error_called;
static long g_error_status;
static int g_success_called;
static char g_echo[256];
static _Thread_local char tls_echo[256];

static const char *html_page(void) {
    static char *page = NULL;
    if (!page) {
        size_t i;
        page = malloc(256 + 20000 * 16);
        strcpy(page, "<!DOCTYPE html><html><head><title>Access denied | zerobounce.net | Zero Bounce</title></head><body>");
        for (i = 0; i < 20000; i++) {
            strcat(page, "<p>blocked</p>");
        }
        strcat(page, "</body></html>");
    }
    return page;
}

static void url_decode(const char *in, char *out, size_t out_len) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < out_len; i++) {
        if (in[i] == '%' && in[i + 1] && in[i + 2]) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            out[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (in[i] == '+') {
            out[o++] = ' ';
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

static void split_url(const char *url, char *host, char *path, char *query) {
    const char *start = strstr(url, "://");
    const char *host_start = start ? start + 3 : url;
    const char *path_start = strchr(host_start, '/');
    const char *query_start = strchr(host_start, '?');
    host[0] = path[0] = query[0] = '\0';
    if (!path_start) {
        snprintf(host, 128, "%s", host_start);
        snprintf(path, 256, "/");
        return;
    }
    snprintf(host, 128, "%.*s", (int)(path_start - host_start), host_start);
    if (!query_start) {
        snprintf(path, 256, "%s", path_start);
        return;
    }
    snprintf(path, 256, "%.*s", (int)(query_start - path_start), path_start);
    snprintf(query, 4096, "%s", query_start + 1);
}

static int query_value(const char *query, const char *key, char *out, size_t out_len) {
    size_t key_len = strlen(key);
    const char *p = query;
    while (p && *p) {
        if ((p == query || p[-1] == '&') && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            char raw[1024];
            const char *end = strchr(p + key_len + 1, '&');
            size_t n = end ? (size_t)(end - (p + key_len + 1)) : strlen(p + key_len + 1);
            if (n >= sizeof(raw)) n = sizeof(raw) - 1;
            memcpy(raw, p + key_len + 1, n);
            raw[n] = '\0';
            url_decode(raw, out, out_len);
            return 1;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return 0;
}

static int field_value(const char *name, char *out, size_t out_len) {
    int count = zero_bounce_test_field_count();
    for (int i = 0; i < count; i++) {
        if (strcmp(zero_bounce_test_field_name(i), name) == 0) {
            snprintf(out, out_len, "%s", zero_bounce_test_field_value(i));
            return 1;
        }
    }
    return 0;
}

static void remember(const OfflineRequest *request) {
    pthread_mutex_lock(&g_mu);
    if (g_count < OFFLINE_MAX) {
        g_requests[g_count++] = *request;
    }
    pthread_mutex_unlock(&g_mu);
}

static void set_body(memory *response_data, const char *text) {
    size_t len = strlen(text);
    response_data->response = malloc(len + 1);
    memcpy(response_data->response, text, len + 1);
    response_data->size = len;
}

static int offline_perform(memory *response_data, long *http_code, char **content_type_out) {
    OfflineRequest request;
    char key[256] = "";
    const char *url = zero_bounce_test_url();
    const char *body = zero_bounce_test_body();
    memset(&request, 0, sizeof(request));
    snprintf(request.method, sizeof(request.method), "%s", zero_bounce_test_method());
    split_url(url ? url : "", request.host, request.path, request.query);
    snprintf(request.body, sizeof(request.body), "%s", body ? body : "");
    if (!query_value(request.query, "api_key", key, sizeof(key))) {
        query_value(request.body, "api_key", key, sizeof(key));
    }
    if (!key[0]) {
        const char *marker = strstr(request.body, "\"api_key\"");
        if (marker) {
            marker += 9;
            while (*marker == ' ' || *marker == ':' || *marker == '\t') marker++;
            if (*marker == '"') {
                const char *end;
                marker++;
                end = strchr(marker, '"');
                if (end && (size_t)(end - marker) < sizeof(key)) {
                    memcpy(key, marker, (size_t)(end - marker));
                    key[end - marker] = '\0';
                }
            }
        }
    }
    if (!key[0]) {
        field_value("api_key", key, sizeof(key));
    }
    snprintf(request.extra_header, sizeof(request.extra_header), "%s",
             zero_bounce_test_extra_header() ? zero_bounce_test_extra_header() : "");
    if (!key[0]) {
        /* The Inbox Placement Tester and the Warmup API carry the key in a
           header instead of a parameter. */
        const char *header = request.extra_header;
        const char *value = NULL;
        if (strncmp(header, "Authorization: Bearer ", 22) == 0) {
            value = header + 22;
        } else if (strncmp(header, "API-Key: ", 9) == 0) {
            value = header + 9;
        }
        if (value) {
            snprintf(key, sizeof(key), "%s", value);
        }
    }
    snprintf(request.api_key, sizeof(request.api_key), "%s", key);
    remember(&request);

    usleep((useconds_t)(rand() % 21) * 1000);

    const char *content = "application/json";
    *http_code = 200;
    if (strncmp(key, "html-403-", 9) == 0 || strncmp(key, "html-200-", 9) == 0) {
        *http_code = strncmp(key, "html-403-", 9) == 0 ? 403 : 200;
        set_body(response_data, html_page());
        content = "text/html; charset=UTF-8";
    } else if (strncmp(key, "empty-", 6) == 0) {
        set_body(response_data, "");
    } else if (strncmp(key, "malformed-", 10) == 0) {
        char *junk = malloc(6000);
        strcpy(junk, "<<<not-json>>>");
        memset(junk + 14, 'x', 5000);
        junk[5014] = '\0';
        set_body(response_data, junk);
        free(junk);
    } else if (strncmp(key, "success-false-str", 17) == 0) {
        /* The API also sends "success" as the string "false". */
        set_body(response_data, "{\"success\":\"False\",\"message\":\"File cannot be found.\"}");
    } else if (strncmp(key, "success-false", 13) == 0) {
        set_body(response_data, "{\"success\":false,\"message\":\"File cannot be found.\"}");
    } else if (strncmp(key, "redirect-302", 12) == 0) {
        *http_code = 302;
        set_body(response_data, "");
    } else if (strncmp(key, "cf-json-", 8) == 0) {
        *http_code = 403;
        set_body(response_data,
            "{\"type\":\"https://developers.cloudflare.com/support/troubleshooting/http-status-codes/cloudflare-1xxx-errors/error-1020/\","
            "\"title\":\"Error 1020: Access denied\",\"status\":403,\"error_code\":1020,\"cloudflare_error\":true}");
    } else if (strncmp(key, "ipt-detail", 10) == 0) {
        /* The Inbox Placement Tester is a Django REST framework app and
           reports errors under "detail". */
        *http_code = 404;
        set_body(response_data, "{\"detail\":\"Inbox placement test not found.\"}");
    } else if (strncmp(key, "double-error-", 13) == 0) {
        *http_code = 400;
        set_body(response_data, "Error: Error: api_key is invalid");
        content = "text/plain";
    } else if (strstr(request.path, "validatebatch") && strncmp(key, "batch-account-", 14) == 0) {
        set_body(response_data,
            "{\"email_batch\":[],\"errors\":[{\"email_address\":\"all\",\"error\":\"Invalid API Key or your account ran out of credits\"}]}");
    } else if (strstr(request.path, "validatebatch") && strncmp(key, "batch-partial-", 14) == 0) {
        set_body(response_data,
            "{\"email_batch\":[{\"address\":\"good@example.com\",\"status\":\"valid\"}],\"errors\":[{\"email_address\":\"bad@\",\"error\":\"Invalid email address\"}]}");
    } else if (strstr(request.path, "getfile")) {
        char csv[512];
        snprintf(csv, sizeof(csv), "email\n%s@example.com\n", key);
        set_body(response_data, csv);
        content = "application/octet-stream";
    } else if (strstr(request.path, "listS3files")) {
        char json[512];
        snprintf(json, sizeof(json), "[{\"name\":\"%s\"}]", key);
        set_body(response_data, json);
    } else if (strstr(request.path, "validatebatch")) {
        char json[512];
        snprintf(json, sizeof(json), "{\"email_batch\":[{\"address\":\"%s\",\"status\":\"valid\"}],\"errors\":[]}", key);
        set_body(response_data, json);
    } else if (strcmp(request.path + strlen(request.path) - 9, "/validate") == 0 ||
               (strlen(request.path) >= 9 && strstr(request.path, "/validate") && !strstr(request.path, "validatebatch") && !strstr(request.path, "validateS3"))) {
        char json[256];
        snprintf(json, sizeof(json), "{\"address\":\"%s\",\"status\":\"valid\"}", key);
        set_body(response_data, json);
    } else if (strstr(request.path, "/scoring") && !strstr(request.path, "sendfile") && !strstr(request.path, "filestatus") && !strstr(request.path, "getfile") && !strstr(request.path, "deletefile")) {
        char json[256];
        snprintf(json, sizeof(json), "{\"email\":\"%s\",\"score\":10}", key);
        set_body(response_data, json);
    } else if (strstr(request.path, "getcredits")) {
        set_body(response_data, "{\"Credits\":\"7\"}");
    } else if (strstr(request.path, "getapiusage")) {
        set_body(response_data, "{\"total\":1}");
    } else if (strstr(request.path, "/activity")) {
        set_body(response_data, "{\"found\":true,\"active_in_days\":\"3\"}");
    } else if (strstr(request.path, "guessformat")) {
        char json[256];
        snprintf(json, sizeof(json), "{\"email\":\"%s\",\"domain\":\"%s\",\"other_domain_formats\":[]}", key, key);
        set_body(response_data, json);
    } else if (strstr(request.path, "filters") || strstr(request.path, "listevaluator")) {
        char json[256];
        snprintf(json, sizeof(json), "{\"echo_api_key\":\"%s\"}", key);
        set_body(response_data, json);
    } else {
        char json[512];
        snprintf(json, sizeof(json),
            "{\"success\":true,\"message\":\"ok\",\"file_id\":\"%s\",\"file_name\":\"f.csv\",\"email\":\"%s\",\"domain\":\"%s\",\"other_domain_formats\":[]}",
            key, key, key);
        set_body(response_data, json);
    }
    if (content_type_out) {
        *content_type_out = strdup(content);
    }
    return 1;
}

static void reset_offline(void) {
    pthread_mutex_lock(&g_mu);
    g_count = 0;
    pthread_mutex_unlock(&g_mu);
    g_error_called = 0;
    g_success_called = 0;
    g_error_text[0] = '\0';
    g_echo[0] = '\0';
    zero_bounce_set_http_perform_for_test(offline_perform);
}

static ZeroBounce *client_with(const char *key) {
    ZeroBounce *zb = new_zero_bounce_instance();
    zero_bounce_initialize(zb, key);
    return zb;
}

static void on_error(ZBErrorResponse error) {
    size_t used = 0;
    g_error_called = 1;
    g_error_status = error.status_code;
    g_error_text[0] = '\0';
    for (size_t i = 0; i < error.errors.size; i++) {
        const char *part = error.errors.data[i] ? error.errors.data[i] : "";
        size_t n = strlen(part);
        if (used && used + 1 < sizeof(g_error_text)) {
            g_error_text[used++] = ' ';
        }
        if (used + n >= sizeof(g_error_text)) {
            n = sizeof(g_error_text) - used - 1;
        }
        memcpy(g_error_text + used, part, n);
        used += n;
        g_error_text[used] = '\0';
    }
}

static void expect_short_error(void) {
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_TRUE(strlen(g_error_text) < 500);
    TEST_ASSERT_NULL(strstr(g_error_text, "<html"));
    TEST_ASSERT_NULL(strstr(g_error_text, "<!DOCTYPE"));
    TEST_ASSERT_NULL(strstr(g_error_text, "<p>blocked</p>"));
    TEST_ASSERT_NULL(strstr(g_error_text, "Error: Error:"));
    TEST_ASSERT_NULL(strstr(g_error_text, "[json.exception"));
}

static OfflineRequest only_request(void) {
    OfflineRequest empty;
    memset(&empty, 0, sizeof(empty));
    TEST_ASSERT_EQUAL_INT(1, g_count);
    if (g_count == 0) return empty;
    return g_requests[0];
}

static int has_field(const char *name, const char *value) {
    for (int i = 0; i < zero_bounce_test_field_count(); i++) {
        if (strcmp(zero_bounce_test_field_name(i), name) == 0) {
            if (!value || strcmp(zero_bounce_test_field_value(i), value) == 0) return 1;
        }
    }
    return 0;
}

static void on_credits(ZBCreditsResponse response) {
    g_success_called = 1;
    TEST_ASSERT_EQUAL_INT(7, response.credits);
}

static void on_validate(ZBValidateResponse response) {
    g_success_called = 1;
    snprintf(tls_echo, sizeof(tls_echo), "%s", response.address ? response.address : "");
    snprintf(g_echo, sizeof(g_echo), "%s", tls_echo);
}

static void on_batch(ZBValidateBatchResponse response) {
    g_success_called = 1;
    if (response.email_batch.size > 0) {
        snprintf(g_echo, sizeof(g_echo), "%s", response.email_batch.data[0].address);
    }
    g_requests[0].body[0] = g_requests[0].body[0];
    if (response.errors.size > 0) {
        snprintf(g_error_text, sizeof(g_error_text), "%s", response.errors.data[0].email_address);
    }
}

static void on_send(ZBSendFileResponse response) {
    g_success_called = 1;
    snprintf(g_echo, sizeof(g_echo), "%s", response.file_id ? response.file_id : "");
}

static void on_status(ZBFileStatusResponse response) {
    g_success_called = 1;
    snprintf(g_echo, sizeof(g_echo), "%s", response.file_id ? response.file_id : "");
}

static void on_get(ZBGetFileResponse response) {
    g_success_called = 1;
    (void)response;
}

static void on_delete(ZBDeleteFileResponse response) {
    g_success_called = 1;
    (void)response;
}

static void on_activity(ZBActivityDataResponse response) {
    g_success_called = 1;
    TEST_ASSERT_TRUE(response.found);
}

static void on_score(ZBScoreEmailResponse response) {
    g_success_called = 1;
    snprintf(g_echo, sizeof(g_echo), "%s", response.email ? response.email : "");
}

static void on_find(ZBFindEmailResponse response) {
    g_success_called = 1;
    snprintf(g_echo, sizeof(g_echo), "%s", response.email ? response.email : "");
}

static void on_domain(ZBDomainSearchResponse response) {
    g_success_called = 1;
    snprintf(g_echo, sizeof(g_echo), "%s", response.domain ? response.domain : "");
}

static void on_usage(ZBGetApiUsageResponse response) {
    g_success_called = 1;
    TEST_ASSERT_EQUAL_INT(1, response.total);
}

static char g_raw[1024];
static void on_raw(const char *json) {
    g_success_called = 1;
    snprintf(g_raw, sizeof(g_raw), "%s", json ? json : "");
}

void test_offline_get_credits(void) {
    reset_offline();
    ZeroBounce *zb = client_with("credits-key");
    get_credits(zb, on_credits, on_error);
    TEST_ASSERT_TRUE(g_success_called);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("GET", request.method);
    TEST_ASSERT_EQUAL_STRING("api.zerobounce.net", request.host);
    TEST_ASSERT_EQUAL_STRING("/v2/getcredits", request.path);
    TEST_ASSERT_EQUAL_STRING("credits-key", request.api_key);
}

void test_offline_validate_options(void) {
    reset_offline();
    ZeroBounce *zb = client_with("validate-key");
    ZBValidateOptions options = new_validate_options();
    options.activity_data = 0;
    options.verify_plus = 1;
    validate_email_with_options(zb, "person@example.com", "", &options, on_validate, on_error);
    TEST_ASSERT_EQUAL_STRING("validate-key", g_echo);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("GET", request.method);
    TEST_ASSERT_EQUAL_STRING("/v2/validate", request.path);
    char value[64];
    TEST_ASSERT_TRUE(query_value(request.query, "email", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("person@example.com", value);
    TEST_ASSERT_TRUE(query_value(request.query, "activity_data", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("false", value);
    TEST_ASSERT_TRUE(query_value(request.query, "verify_plus", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("true", value);
    TEST_ASSERT_FALSE(query_value(request.query, "timeout", value, sizeof(value)));
}

void test_offline_validate_post(void) {
    reset_offline();
    ZeroBounce *zb = client_with("post-key");
    ZBValidateOptions options = new_validate_options();
    options.use_post = 1;
    options.timeout = 15;
    validate_email_with_options(zb, "person@example.com", "203.0.113.5", &options, on_validate, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("/v2/validate", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "api_key=post-key"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "email=person%40example.com"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "timeout=15"));
    TEST_ASSERT_NULL(strstr(request.body, "activity_data"));
}

void test_offline_batch_omits_and_keeps_zero_ip(void) {
    reset_offline();
    ZeroBounce *zb = client_with("batch-key");
    EmailToValidateVector emails = email_to_validate_vector_init();
    ZBEmailToValidate a = {"a@example.com", ""};
    ZBEmailToValidate b = {"b@example.com", "203.0.113.9"};
    email_to_validate_vector_append(&emails, a);
    email_to_validate_vector_append(&emails, b);
    validate_email_batch(zb, emails, on_batch, on_error);
    TEST_ASSERT_EQUAL_STRING("batch-key", g_echo);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("api.zerobounce.net", request.host);
    TEST_ASSERT_EQUAL_STRING("/v2/validatebatch", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "null"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "203.0.113.9"));
    TEST_ASSERT_NULL(strstr(request.body, "\"timeout\""));
    email_to_validate_vector_free(&emails);
}

void test_offline_batch_explicit_false(void) {
    reset_offline();
    ZeroBounce *zb = client_with("batch-opt");
    EmailToValidateVector emails = email_to_validate_vector_init();
    ZBEmailToValidate a = {"a@example.com", "0.0.0.0"};
    email_to_validate_vector_append(&emails, a);
    ZBValidateOptions options = new_validate_options();
    options.timeout = 30;
    options.activity_data = 0;
    options.verify_plus = 1;
    validate_email_batch_with_options(zb, emails, &options, on_batch, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_NOT_NULL(strstr(request.body, "\"timeout\""));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "30"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "\"activity_data\""));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "false"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "\"verify_plus\""));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "true"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "0.0.0.0"));
    email_to_validate_vector_free(&emails);
}

void test_offline_send_file_bools_and_zero_column(void) {
    reset_offline();
    ZeroBounce *zb = client_with("file-key");
    SendFileOptions options = new_send_file_options();
    options.return_url = "https://example.com/hook";
    options.allow_phase_2_is_set = true;
    options.allow_phase_2 = false;
    options.remove_duplicate = false;
    options.has_header_row = true;
    send_file(zb, "email_file.csv", 0, options, on_send, on_error);
    TEST_ASSERT_EQUAL_STRING("file-key", g_echo);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("bulkapi.zerobounce.net", request.host);
    TEST_ASSERT_EQUAL_STRING("/v2/sendfile", request.path);
    TEST_ASSERT_TRUE(has_field("email_address_column", "0"));
    TEST_ASSERT_TRUE(has_field("file", "<file>"));
    TEST_ASSERT_TRUE(has_field("has_header_row", "true"));
    TEST_ASSERT_TRUE(has_field("remove_duplicate", "false"));
    TEST_ASSERT_TRUE(has_field("allow_phase_2", "false"));
    TEST_ASSERT_FALSE(has_field("first_name_column", NULL));
}

void test_offline_scoring_send_omits_validation_fields(void) {
    reset_offline();
    ZeroBounce *zb = client_with("score-file");
    SendFileOptions options = new_send_file_options();
    options.first_name_column = 2;
    options.allow_phase_2_is_set = true;
    options.allow_phase_2 = true;
    scoring_send_file(zb, "email_file.csv", 1, options, on_send, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("/v2/scoring/sendfile", request.path);
    TEST_ASSERT_FALSE(has_field("first_name_column", NULL));
    TEST_ASSERT_FALSE(has_field("allow_phase_2", NULL));
    TEST_ASSERT_TRUE(has_field("has_header_row", "true"));
}

void test_offline_file_status_and_delete(void) {
    reset_offline();
    ZeroBounce *zb = client_with("status-key");
    file_status(zb, "file-1", on_status, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("/v2/filestatus", request.path);
    TEST_ASSERT_EQUAL_STRING("bulkapi.zerobounce.net", request.host);
    char value[64];
    TEST_ASSERT_TRUE(query_value(request.query, "file_id", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("file-1", value);
    reset_offline();
    scoring_file_status(zb, "file-2", on_status, on_error);
    TEST_ASSERT_EQUAL_STRING("/v2/scoring/filestatus", only_request().path);
    reset_offline();
    delete_file(zb, "file-1", on_delete, on_error);
    TEST_ASSERT_EQUAL_STRING("/v2/deletefile", only_request().path);
    reset_offline();
    scoring_delete_file(zb, "file-2", on_delete, on_error);
    TEST_ASSERT_EQUAL_STRING("/v2/scoring/deletefile", only_request().path);
}

void test_offline_get_file_options(void) {
    reset_offline();
    ZeroBounce *zb = client_with("getfile-key");
    GetFileOptions options = new_get_file_options();
    options.download_type = ZB_DOWNLOAD_TYPE_PHASE_2;
    options.activity_data = 1;
    get_file_with_options(zb, "file-9", "test/downloads/offline-get.csv", &options, on_get, on_error);
    TEST_ASSERT_TRUE(g_success_called);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("/v2/getfile", request.path);
    char value[64];
    TEST_ASSERT_TRUE(query_value(request.query, "download_type", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("phase_2", value);
    TEST_ASSERT_TRUE(query_value(request.query, "activity_data", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("true", value);
    reset_offline();
    GetFileOptions scoring = new_get_file_options();
    scoring.download_type = ZB_DOWNLOAD_TYPE_PHASE_1;
    scoring.activity_data = 1;
    scoring_get_file_with_options(zb, "file-8", "test/downloads/offline-score.csv", &scoring, on_get, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/v2/scoring/getfile", request.path);
    TEST_ASSERT_TRUE(query_value(request.query, "download_type", value, sizeof(value)));
    TEST_ASSERT_FALSE(query_value(request.query, "activity_data", value, sizeof(value)));
}

void test_offline_activity_usage_score(void) {
    reset_offline();
    ZeroBounce *zb = client_with("usage-key");
    struct tm start = {0};
    struct tm end = {0};
    start.tm_year = 118; start.tm_mon = 0; start.tm_mday = 1;
    end.tm_year = 118; end.tm_mon = 0; end.tm_mday = 31;
    get_api_usage(zb, start, end, on_usage, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("/v2/getapiusage", request.path);
    char value[32];
    TEST_ASSERT_TRUE(query_value(request.query, "start_date", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("2018-01-01", value);
    reset_offline();
    get_activity_data(zb, "person@example.com", on_activity, on_error);
    TEST_ASSERT_EQUAL_STRING("/v2/activity", only_request().path);
    reset_offline();
    score_email(zb, "person@example.com", on_score, on_error);
    TEST_ASSERT_EQUAL_STRING("usage-key", g_echo);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("GET", request.method);
    TEST_ASSERT_EQUAL_STRING("/v2/scoring", request.path);
    reset_offline();
    score_email_post(zb, "person@example.com", on_score, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "email=person%40example.com"));
}

void test_offline_finder_and_domain(void) {
    reset_offline();
    ZeroBounce *zb = client_with("finder-key");
    find_email_by_domain_first_middle_last_name(zb, "example.com", "Ada", "M", "Lovelace", on_find, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("/v2/guessformat", request.path);
    char value[64];
    TEST_ASSERT_TRUE(query_value(request.query, "domain", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("example.com", value);
    TEST_ASSERT_FALSE(query_value(request.query, "company_name", value, sizeof(value)));
    reset_offline();
    find_email_by_company_name_first_name(zb, "ZeroBounce", "Ada", on_find, on_error);
    request = only_request();
    TEST_ASSERT_TRUE(query_value(request.query, "company_name", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("ZeroBounce", value);
    TEST_ASSERT_FALSE(query_value(request.query, "domain", value, sizeof(value)));
    TEST_ASSERT_FALSE(query_value(request.query, "last_name", value, sizeof(value)));
    reset_offline();
    search_domain_by_domain(zb, "example.com", on_domain, on_error);
    TEST_ASSERT_FALSE(query_value(only_request().query, "company_name", value, sizeof(value)));
    reset_offline();
    search_domain_by_company_name(zb, "ZeroBounce", on_domain, on_error);
    request = only_request();
    TEST_ASSERT_TRUE(query_value(request.query, "company_name", value, sizeof(value)));
    TEST_ASSERT_FALSE(query_value(request.query, "domain", value, sizeof(value)));
}

void test_offline_priority3(void) {
    reset_offline();
    ZeroBounce *zb = client_with("p3-key");
    EmailFinderSendFileOptions finder = new_email_finder_send_file_options();
    finder.has_header_row = 0;
    finder.first_name_column = 0;
    send_email_finder_file(zb, "email_file.csv", 1, &finder, on_send, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("bulkapi.zerobounce.net", request.host);
    TEST_ASSERT_EQUAL_STRING("/email-finder/sendfile", request.path);
    TEST_ASSERT_TRUE(has_field("domain_column", "1"));
    TEST_ASSERT_TRUE(has_field("has_header_row", "false"));
    TEST_ASSERT_FALSE(has_field("first_name_column", NULL));
    reset_offline();
    email_finder_file_status(zb, "fid", on_status, on_error);
    TEST_ASSERT_EQUAL_STRING("/email-finder/filestatus", only_request().path);
    reset_offline();
    email_finder_get_file(zb, "fid", "test/downloads/finder.csv", on_get, on_error);
    TEST_ASSERT_EQUAL_STRING("/email-finder/getfile", only_request().path);
    reset_offline();
    email_finder_delete_file(zb, "fid", on_delete, on_error);
    TEST_ASSERT_EQUAL_STRING("/email-finder/deletefile", only_request().path);
    reset_offline();
    DomainSearchSendFileOptions domain_opts = new_domain_search_send_file_options();
    domain_opts.has_header_row = 0;
    send_domain_search_file(zb, "email_file.csv", 2, &domain_opts, on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/domain-search/sendfile", request.path);
    TEST_ASSERT_TRUE(has_field("has_header_row", "false"));
    TEST_ASSERT_TRUE(has_field("domain_column", "2"));
    reset_offline();
    domain_search_file_status(zb, "fid", on_status, on_error);
    TEST_ASSERT_EQUAL_STRING("/domain-search/filestatus", only_request().path);
    reset_offline();
    domain_search_get_file(zb, "fid", "test/downloads/domain.csv", on_get, on_error);
    TEST_ASSERT_EQUAL_STRING("/domain-search/getfile", only_request().path);
    reset_offline();
    domain_search_delete_file(zb, "fid", on_delete, on_error);
    TEST_ASSERT_EQUAL_STRING("/domain-search/deletefile", only_request().path);
    reset_offline();
    send_list_evaluator_file(zb, "email_file.csv", 1, on_send, on_error);
    TEST_ASSERT_EQUAL_STRING("/v2/listevaluator/", only_request().path);
    TEST_ASSERT_TRUE(has_field("email_address_column", "1"));
    reset_offline();
    get_list_evaluator_result(zb, "fid", on_raw, on_error);
    TEST_ASSERT_NOT_NULL(strstr(g_raw, "p3-key"));
    TEST_ASSERT_EQUAL_STRING("/v2/listevaluator/fid/", only_request().path);
    reset_offline();
    /* LIVE: send-evaluated-file takes the file id as a form field, not an
       upload, and the trailing slash is part of the endpoint. */
    send_evaluated_file(zb, "fid-1", on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("/v2/send-evaluated-file/", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "file_id=fid-1"));
    reset_offline();
    add_filter(zb, "exclude", "domain", "example.com", on_raw, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("/v2/filters/add", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "rule=exclude"));
    reset_offline();
    list_filters(zb, false, on_raw, on_error);
    TEST_ASSERT_EQUAL_STRING("GET", only_request().method);
    TEST_ASSERT_EQUAL_STRING("/v2/filters/list", only_request().path);
    reset_offline();
    list_filters(zb, true, on_raw, on_error);
    TEST_ASSERT_EQUAL_STRING("POST", only_request().method);
    reset_offline();
    delete_filter(zb, "exclude", "domain", "example.com", on_raw, on_error);
    TEST_ASSERT_EQUAL_STRING("/v2/filters/delete", only_request().path);
    reset_offline();
    ValidateS3FileOptions s3 = new_validate_s3_file_options();
    s3.file_name = "inbox.csv";
    s3.email_address_column = 0;
    s3.has_header_row = 0;
    s3.remove_duplicate = 1;
    validate_s3_file(zb, &s3, on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/validateS3file", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "email_address_column=0"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "has_header_row=false"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "remove_duplicate=true"));
    TEST_ASSERT_NULL(strstr(request.body, "first_name_column"));
    reset_offline();
    ScoreS3FileOptions score = new_score_s3_file_options();
    score.file_name = "inbox.csv";
    score_s3_file(zb, &score, on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/scoreS3file", request.path);
    TEST_ASSERT_NULL(strstr(request.body, "email_address_column"));
    reset_offline();
    export_s3_file(zb, "fid", "bucket-out", on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/exportS3file", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "export_bucket=bucket-out"));
    reset_offline();
    list_s3_files(zb, NULL, NULL, on_raw, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/listS3files", request.path);
    TEST_ASSERT_NULL(strstr(request.query, "bucket="));
    TEST_ASSERT_NOT_NULL(strstr(g_raw, "p3-key"));
    reset_offline();
    list_s3_files(zb, "inbox", "csv", on_raw, on_error);
    request = only_request();
    TEST_ASSERT_NOT_NULL(strstr(request.query, "bucket=inbox"));
    TEST_ASSERT_NOT_NULL(strstr(request.query, "search=csv"));
}

void test_offline_validate_new_options(void) {
    reset_offline();
    ZeroBounce *zb = client_with("validate-key");
    ZBValidateOptions options = new_validate_options();

    validate_email_with_options(zb, "a@example.com", "", &options, on_validate, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_NULL(strstr(request.query, "domain_info"));
    TEST_ASSERT_NULL(strstr(request.query, "greylist_processing"));
    TEST_ASSERT_NULL(strstr(request.query, "callback_url"));

    reset_offline();
    options.domain_info = 1;
    options.greylist_processing = 1;
    options.callback_url = "https://example.com/greylist";
    validate_email_with_options(zb, "a@example.com", "", &options, on_validate, on_error);
    request = only_request();
    /* Booleans go on the wire as the strings "true"/"false". */
    TEST_ASSERT_NOT_NULL(strstr(request.query, "domain_info=true"));
    TEST_ASSERT_NOT_NULL(strstr(request.query, "greylist_processing=true"));
    TEST_ASSERT_NOT_NULL(strstr(request.query, "callback_url="));

    reset_offline();
    options.use_post = 1;
    options.domain_info = 0;
    validate_email_with_options(zb, "a@example.com", "", &options, on_validate, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "domain_info=false"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "callback_url="));
}

void test_offline_callback_url_requires_greylist(void) {
    reset_offline();
    ZeroBounce *zb = client_with("validate-key");
    ZBValidateOptions options = new_validate_options();

    options.callback_url = "https://example.com/greylist";
    validate_email_with_options(zb, "a@example.com", "", &options, on_validate, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "greylist_processing"));

    reset_offline();
    options.greylist_processing = 0;
    validate_email_with_options(zb, "a@example.com", "", &options, on_validate, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "greylist_processing"));

    reset_offline();
    options.greylist_processing = 1;
    options.callback_url = "   ";
    validate_email_with_options(zb, "a@example.com", "", &options, on_validate, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "callback_url"));

    TEST_ASSERT_EQUAL_INT(0, g_count);
}

void test_offline_s3_and_sftp_jobs(void) {
    reset_offline();
    ZeroBounce *zb = client_with("bulk-key");
    ZBBulkFileJobOptions options = new_bulk_file_job_options();

    options.file_name = "lists/leads.csv";
    options.return_url = "https://example.com/done";
    email_finder_s3_file(zb, &options, on_send, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("bulkapi.zerobounce.net", request.host);
    /* The S3 and SFTP jobs live on the bulkapi root, WITHOUT /v2. */
    TEST_ASSERT_EQUAL_STRING("/emailFinderS3file", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "file_name=lists%2Fleads.csv"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "return_url="));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "has_header_row=true"));
    /* Options that were never set stay off the request entirely. */
    TEST_ASSERT_NULL(strstr(request.body, "remove_duplicates"));

    reset_offline();
    options.has_header_row = false;
    options.remove_duplicates = 1;
    domain_search_s3_file(zb, &options, on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/domainSearchS3file", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "has_header_row=false"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "remove_duplicates=true"));

    /* LIVE: the docs call file_name optional for validateSFTPfile, but the
       API answers "Empty parameter: file_name" without it, and every job
       needs a return_url as well. Both are checked before sending. */
    reset_offline();
    options = new_bulk_file_job_options();
    options.return_url = "https://example.com/done";
    validate_sftp_file(zb, &options, on_send, on_error);
    TEST_ASSERT_EQUAL_INT(0, g_count);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "file_name"));

    reset_offline();
    options = new_bulk_file_job_options();
    options.file_name = "my_file.txt";
    validate_sftp_file(zb, &options, on_send, on_error);
    TEST_ASSERT_EQUAL_INT(0, g_count);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "return_url"));

    reset_offline();
    options.return_url = "https://example.com/done";
    validate_sftp_file(zb, &options, on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/validateSFTPfile", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "file_name=my_file.txt"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "return_url="));

    reset_offline();
    score_sftp_file(zb, &options, on_send, on_error);
    TEST_ASSERT_EQUAL_STRING("/scoreSFTPfile", only_request().path);
    reset_offline();
    email_finder_sftp_file(zb, &options, on_send, on_error);
    TEST_ASSERT_EQUAL_STRING("/emailFinderSFTPfile", only_request().path);
    reset_offline();
    domain_search_sftp_file(zb, &options, on_send, on_error);
    TEST_ASSERT_EQUAL_STRING("/domainSearchSFTPfile", only_request().path);

    /* exportSFTPfile takes export_path, not the export_bucket exportS3file uses. */
    reset_offline();
    export_sftp_file(zb, "fid", "/exports", on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/exportSFTPfile", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "file_id=fid"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "export_path=%2Fexports"));
    TEST_ASSERT_NULL(strstr(request.body, "export_bucket"));

    reset_offline();
    export_sftp_file(zb, "fid", NULL, on_send, on_error);
    TEST_ASSERT_NULL(strstr(only_request().body, "export_path"));
}

void test_offline_bulk_jobs_reject_before_request(void) {
    reset_offline();
    ZeroBounce *zb = client_with("bulk-key");
    ZBBulkFileJobOptions options = new_bulk_file_job_options();

    email_finder_s3_file(zb, &options, on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "file_name"));

    /* A blank name is rejected just like a missing one. */
    g_error_called = 0;
    options.file_name = "   ";
    validate_sftp_file(zb, &options, on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "file_name"));

    g_error_called = 0;
    send_evaluated_file(zb, "  ", on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "file_id"));

    g_error_called = 0;
    export_sftp_file(zb, "  ", NULL, on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "file_id"));

    g_error_called = 0;
    export_sftp_file(zb, "fid", " ", on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "export_path"));

    TEST_ASSERT_EQUAL_INT(0, g_count);
}

void test_offline_list_evaluator_base_url(void) {
    reset_offline();
    ZeroBounce *zb = client_with("eval-key");

    get_list_evaluator_result(zb, "a b/c", on_raw, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("bulkapi.zerobounce.net", request.host);
    /* The file id is one path segment, so a slash in it must stay encoded and
       a space must not become a plus. */
    TEST_ASSERT_EQUAL_STRING("/v2/listevaluator/a%20b%2Fc/", request.path);

    reset_offline();
    get_list_evaluator_result(zb, "   ", on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "file_id"));
    TEST_ASSERT_EQUAL_INT(0, g_count);

    /* It is a separate host, not a path built from the bulk base. */
    reset_offline();
    zero_bounce_set_list_evaluator_base_url(zb, "https://bulk.example.test/v2/listevaluator");
    get_list_evaluator_result(zb, "fid", on_raw, on_error);
    TEST_ASSERT_EQUAL_STRING("bulk.example.test", only_request().host);
    zero_bounce_set_list_evaluator_base_url(zb, "https://bulkapi.zerobounce.net/v2/listevaluator");
}

void test_offline_inbox_placement(void) {
    reset_offline();
    ZeroBounce *zb = client_with("ipt-key");

    get_inbox_placement_providers(zb, on_raw, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("/api/ipt/providers", request.path);
    /* LIVE: the key travels in an Authorization: Bearer header. Sent as an
       api_key query parameter the request is rejected as unauthorized, and
       the key would end up in whatever logs the query string. */
    TEST_ASSERT_EQUAL_STRING("Authorization: Bearer ipt-key", request.extra_header);
    TEST_ASSERT_NULL(strstr(request.query, "api_key="));
    TEST_ASSERT_NULL(strstr(request.body, "api_key="));

    reset_offline();
    get_inbox_placement_test_types(zb, on_raw, on_error);
    TEST_ASSERT_EQUAL_STRING("/api/ipt/test-types", only_request().path);

    reset_offline();
    ZBCreateInboxPlacementTestOptions create = new_create_inbox_placement_test_options();
    create.test_type = 1;
    create.sender_email = "news@example.com";
    create.webhook_url = "https://example.com/ipt";
    create_inbox_placement_test(zb, "Launch", &create, on_raw, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("/api/ipt/test", request.path);
    /* Bodies are camelCase JSON, and unset fields are left out. */
    TEST_ASSERT_EQUAL_STRING(
        "{\"name\":\"Launch\",\"testType\":1,\"senderEmail\":\"news@example.com\","
        "\"webhookUrl\":\"https://example.com/ipt\"}",
        request.body
    );

    reset_offline();
    create_inbox_placement_test(zb, "Launch", NULL, on_raw, on_error);
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"Launch\"}", only_request().body);

    reset_offline();
    ZBListInboxPlacementTestsOptions list = new_list_inbox_placement_tests_options();
    list.status = "finished";
    list.page = 1;
    list.page_size = 25;
    list_inbox_placement_tests(zb, &list, on_raw, on_error);
    request = only_request();
    /* LIVE: the listing is GET /tests (plural). /test is the single-test path
       and answers 'Method "GET" not allowed'. */
    TEST_ASSERT_EQUAL_STRING("/api/ipt/tests", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.query, "status=finished"));
    TEST_ASSERT_NOT_NULL(strstr(request.query, "page=1"));
    TEST_ASSERT_NOT_NULL(strstr(request.query, "pageSize=25"));

    reset_offline();
    list_inbox_placement_tests(zb, NULL, on_raw, on_error);
    request = only_request();
    TEST_ASSERT_NULL(strstr(request.query, "status="));
    TEST_ASSERT_NULL(strstr(request.query, "page="));

    reset_offline();
    delete_inbox_placement_test(zb, "a b/c", on_raw, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("DELETE", request.method);
    TEST_ASSERT_EQUAL_STRING("/api/ipt/test/a%20b%2Fc", request.path);

    reset_offline();
    set_inbox_placement_webhook(zb, "https://example.com/ipt", on_raw, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("PUT", request.method);
    TEST_ASSERT_EQUAL_STRING("/api/ipt/webhook", request.path);
    TEST_ASSERT_EQUAL_STRING("{\"webhookUrl\":\"https://example.com/ipt\"}", request.body);

    reset_offline();
    get_inbox_placement_webhook(zb, on_raw, on_error);
    TEST_ASSERT_EQUAL_STRING("/api/ipt/webhook", only_request().path);

    reset_offline();
    get_inbox_placement_test(zb, "   ", on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "test_id"));
    g_error_called = 0;
    create_inbox_placement_test(zb, " ", NULL, on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "name"));
    g_error_called = 0;
    set_inbox_placement_webhook(zb, "", on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "webhook_url"));
    TEST_ASSERT_EQUAL_INT(0, g_count);
}

void test_offline_inbox_placement_detail_error(void) {
    reset_offline();
    ZeroBounce *zb = client_with("ipt-detail");

    get_inbox_placement_test(zb, "123", on_raw, on_error);

    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "Inbox placement test not found."));
}

void test_offline_batch_timeout_is_a_string(void) {
    reset_offline();
    ZeroBounce *zb = client_with("batch-timeout");
    EmailToValidateVector emails = email_to_validate_vector_init();
    ZBEmailToValidate a = {"a@example.com", ""};
    ZBValidateOptions options = new_validate_options();

    email_to_validate_vector_append(&emails, a);
    options.timeout = 30;
    validate_email_batch_with_options(zb, emails, &options, on_batch, on_error);

    /* LIVE: validatebatch takes timeout as a JSON *string*. Sent as a number
       the API rejects the whole batch with the account-level "Invalid API Key
       or your account ran out of credits". */
    TEST_ASSERT_NOT_NULL(strstr(only_request().body, "\"timeout\": \"30\""));
    TEST_ASSERT_NULL(strstr(only_request().body, "\"timeout\": 30"));
    email_to_validate_vector_free(&emails);
}

void test_offline_success_false_on_200_is_an_error(void) {
    reset_offline();
    /* LIVE: getfile and deletefile report an unknown or deleted file id with
       HTTP 200 and a "success": false field. That is a failure, not a result,
       and the status is kept on the error. */
    ZeroBounce *zb = client_with("success-false");

    delete_file(zb, "fid", on_delete, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "File cannot be found"));
    TEST_ASSERT_EQUAL_INT(200, (int)g_error_status);

    g_error_called = 0;
    reset_offline();
    zb = client_with("success-false-str");
    delete_file(zb, "fid", on_delete, on_error);
    TEST_ASSERT_TRUE(g_error_called);
}

void test_offline_safety_rails(void) {
    reset_offline();
    ZeroBounce *zb = client_with("rails-key");

    /* Only https is ever dialled: a plain-http base URL would put the key on
       the wire in the clear. */
    zb->api_base_url = "http://api.zerobounce.net/v2";
    get_credits(zb, on_credits, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "https"));
    TEST_ASSERT_EQUAL_INT(0, g_count);

    /* A path segment of "." or ".." would walk the request out of the
       endpoint it was meant for. */
    reset_offline();
    zb = client_with("rails-key");
    get_inbox_placement_test(zb, "..", on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "test_id"));
    TEST_ASSERT_EQUAL_INT(0, g_count);

    reset_offline();
    get_list_evaluator_result(zb, ".", on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "file_id"));
    TEST_ASSERT_EQUAL_INT(0, g_count);

    /* A key with surrounding whitespace or control characters would be
       quoted, unredacted, in a transport error. */
    TEST_ASSERT_TRUE(zb_api_key_is_clean("abc123"));
    TEST_ASSERT_FALSE(zb_api_key_is_clean(" abc123"));
    TEST_ASSERT_FALSE(zb_api_key_is_clean("abc123\n"));
    TEST_ASSERT_FALSE(zb_api_key_is_clean("abc\t123"));

    /* An api_key never reaches an error message. */
    {
        char *safe = zb_redact_api_key("GET https://api.zerobounce.net/v2/validate?api_key=sekrit&email=a");
        TEST_ASSERT_NOT_NULL(safe);
        TEST_ASSERT_NULL(strstr(safe, "sekrit"));
        TEST_ASSERT_NOT_NULL(strstr(safe, "api_key=REDACTED"));
        TEST_ASSERT_NOT_NULL(strstr(safe, "&email=a"));
        free(safe);
    }
}

void test_offline_redirect_is_not_followed(void) {
    reset_offline();
    /* FOLLOWLOCATION is off: a redirect could send the api_key to another
       host, so it is reported rather than followed. */
    ZeroBounce *zb = client_with("redirect-302");

    get_credits(zb, on_credits, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "redirect"));
    TEST_ASSERT_EQUAL_INT(302, (int)g_error_status);
}

void test_offline_streaming_download(void) {
    const char *path = "test/downloads/offline-stream.csv";
    reset_offline();
    /* The body goes straight to disk instead of being buffered first. */
    ZeroBounce *zb = client_with("stream-key");

    remove(path);
    get_file_streaming(zb, "fid", (char *)path, NULL, on_get, on_error);
    TEST_ASSERT_TRUE(g_success_called);
    TEST_ASSERT_FALSE(g_error_called);
    {
        FILE *handle = fopen(path, "rb");
        char first[32] = {0};
        TEST_ASSERT_NOT_NULL(handle);
        TEST_ASSERT_TRUE(fread(first, 1, sizeof(first) - 1, handle) > 0);
        fclose(handle);
        TEST_ASSERT_EQUAL_INT(0, strncmp(first, "email", 5));
    }
    remove(path);

    /* getfile answers an unknown or deleted id with a JSON error rather than
       a file, so the probe must catch it and leave nothing behind. */
    reset_offline();
    zb = client_with("success-false");
    get_file_streaming(zb, "fid", (char *)path, NULL, on_get, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "File cannot be found"));
    TEST_ASSERT_NULL(fopen(path, "rb"));

    /* An HTML error page is caught the same way. */
    reset_offline();
    zb = client_with("html-200-stream");
    get_file_streaming(zb, "fid", (char *)path, NULL, on_get, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NULL(strstr(g_error_text, "<html"));
    TEST_ASSERT_NULL(fopen(path, "rb"));

    reset_offline();
    zb = client_with("stream-key");
    get_file_streaming(zb, "fid", "", NULL, on_get, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "local_download_path"));
}

void test_offline_rejects_before_request(void) {
    reset_offline();
    ZeroBounce *zb = client_with("");
    get_credits(zb, on_credits, on_error);
    find_email_by_domain_first_name(zb, "example.com", "Ada", on_find, on_error);
    search_domain_by_company_name(zb, "ZeroBounce", on_domain, on_error);
    list_filters(zb, false, on_raw, on_error);
    email_finder_delete_file(zb, "fid", on_delete, on_error);
    send_domain_search_file(zb, "email_file.csv", 1, NULL, on_send, on_error);
    ValidateS3FileOptions missing = new_validate_s3_file_options();
    missing.file_name = "inbox.csv";
    validate_s3_file(zb, &missing, on_send, on_error);
    score_s3_file(zb, NULL, on_send, on_error);
    export_s3_file(zb, "fid", "out", on_send, on_error);
    TEST_ASSERT_EQUAL_INT(0, g_count);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "initialize"));

    reset_offline();
    ZeroBounce *keyed = client_with("keyed");
    ZBValidateOptions bad = new_validate_options();
    bad.timeout = 2;
    validate_email_with_options(keyed, "a@example.com", "", &bad, on_validate, on_error);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "timeout"));
    ZBValidateOptions batch_bad = new_validate_options();
    batch_bad.timeout = 9;
    EmailToValidateVector emails = email_to_validate_vector_init();
    ZBEmailToValidate a = {"a@example.com", ""};
    email_to_validate_vector_append(&emails, a);
    validate_email_batch_with_options(keyed, emails, &batch_bad, on_batch, on_error);
    send_email_finder_file(keyed, "email_file.csv", 0, NULL, on_send, on_error);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "domain_column"));
    ValidateS3FileOptions no_name = new_validate_s3_file_options();
    validate_s3_file(keyed, &no_name, on_send, on_error);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "file_name"));
    TEST_ASSERT_EQUAL_INT(0, g_count);
    email_to_validate_vector_free(&emails);
}

void test_offline_html_403(void) {
    reset_offline();
    ZeroBounce *zb = client_with("html-403-user");
    get_credits(zb, on_credits, on_error);
    expect_short_error();
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "Access denied"));
    TEST_ASSERT_TRUE(strlen(html_page()) > 100000);
}

void test_offline_html_200_validate(void) {
    reset_offline();
    ZeroBounce *zb = client_with("html-200-user");
    validate_email(zb, "a@example.com", "", on_validate, on_error);
    TEST_ASSERT_FALSE(g_success_called);
    expect_short_error();
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "Access denied"));
}

void test_offline_html_200_get_file(void) {
    reset_offline();
    ZeroBounce *zb = client_with("html-200-file");
    const char *path = "test/downloads/offline-html.csv";
    remove(path);
    get_file(zb, "fid", (char *)path, on_get, on_error);
    TEST_ASSERT_FALSE(g_success_called);
    expect_short_error();
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "Access denied"));
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NULL(f);
}

void test_offline_cloudflare_json(void) {
    reset_offline();
    ZeroBounce *zb = client_with("cf-json-user");
    get_credits(zb, on_credits, on_error);
    expect_short_error();
    TEST_ASSERT_NULL(strstr(g_error_text, "developers.cloudflare.com"));
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "Access denied"));
}

void test_offline_empty_and_malformed(void) {
    reset_offline();
    ZeroBounce *zb = client_with("empty-user");
    validate_email(zb, "a@example.com", "", on_validate, on_error);
    expect_short_error();
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "Empty response"));
    reset_offline();
    ZeroBounce *bad = client_with("malformed-user");
    validate_email(bad, "a@example.com", "", on_validate, on_error);
    expect_short_error();
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "not valid JSON"));
    TEST_ASSERT_NULL(strstr(g_error_text, "<<<not-json>>>"));
}

void test_offline_double_error_prefix(void) {
    reset_offline();
    ZeroBounce *zb = client_with("double-error-user");
    get_activity_data(zb, "a@example.com", on_activity, on_error);
    expect_short_error();
    TEST_ASSERT_EQUAL_STRING("api_key is invalid", g_error_text);
}

void test_offline_batch_account_error(void) {
    reset_offline();
    ZeroBounce *zb = client_with("batch-account-user");
    EmailToValidateVector emails = email_to_validate_vector_init();
    ZBEmailToValidate a = {"a@example.com", ""};
    email_to_validate_vector_append(&emails, a);
    validate_email_batch(zb, emails, on_batch, on_error);
    TEST_ASSERT_FALSE(g_success_called);
    expect_short_error();
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "Invalid API Key"));
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "ran out of credits"));
    email_to_validate_vector_free(&emails);
}

void test_offline_batch_partial(void) {
    reset_offline();
    ZeroBounce *zb = client_with("batch-partial-user");
    EmailToValidateVector emails = email_to_validate_vector_init();
    ZBEmailToValidate a = {"good@example.com", ""};
    ZBEmailToValidate b = {"bad@", ""};
    email_to_validate_vector_append(&emails, a);
    email_to_validate_vector_append(&emails, b);
    validate_email_batch(zb, emails, on_batch, on_error);
    TEST_ASSERT_TRUE(g_success_called);
    TEST_ASSERT_EQUAL_STRING("good@example.com", g_echo);
    TEST_ASSERT_EQUAL_STRING("bad@", g_error_text);
    TEST_ASSERT_FALSE(g_error_called);
    email_to_validate_vector_free(&emails);
}

typedef struct {
    int index;
    char echo[64];
} ConcArg;

static void *conc_thread(void *arg) {
    ConcArg *item = arg;
    char key[32];
    snprintf(key, sizeof(key), "conc-%d", item->index);
    ZeroBounce *zb = client_with(key);
    tls_echo[0] = '\0';
    validate_email(zb, "person@example.com", "", on_validate, on_error);
    snprintf(item->echo, sizeof(item->echo), "%s", tls_echo);
    return NULL;
}

void test_offline_concurrent_clients(void) {
    reset_offline();
    enum { N = 12 };
    pthread_t threads[N];
    ConcArg args[N];
    for (int i = 0; i < N; i++) {
        args[i].index = i;
        args[i].echo[0] = '\0';
        pthread_create(&threads[i], NULL, conc_thread, &args[i]);
    }
    for (int i = 0; i < N; i++) pthread_join(threads[i], NULL);
    int seen[N] = {0};
    TEST_ASSERT_EQUAL_INT(N, g_count);
    for (int i = 0; i < g_count; i++) {
        int n = -1;
        sscanf(g_requests[i].api_key, "conc-%d", &n);
        TEST_ASSERT_TRUE(n >= 0 && n < N);
        seen[n]++;
    }
    for (int i = 0; i < N; i++) {
        char key[32];
        snprintf(key, sizeof(key), "conc-%d", i);
        TEST_ASSERT_EQUAL_INT(1, seen[i]);
        TEST_ASSERT_EQUAL_STRING(key, args[i].echo);
    }
}

void offline_run_tests(void) {
    RUN_TEST(test_offline_get_credits);
    RUN_TEST(test_offline_validate_options);
    RUN_TEST(test_offline_validate_post);
    RUN_TEST(test_offline_batch_omits_and_keeps_zero_ip);
    RUN_TEST(test_offline_batch_explicit_false);
    RUN_TEST(test_offline_send_file_bools_and_zero_column);
    RUN_TEST(test_offline_scoring_send_omits_validation_fields);
    RUN_TEST(test_offline_file_status_and_delete);
    RUN_TEST(test_offline_get_file_options);
    RUN_TEST(test_offline_activity_usage_score);
    RUN_TEST(test_offline_finder_and_domain);
    RUN_TEST(test_offline_priority3);
    RUN_TEST(test_offline_validate_new_options);
    RUN_TEST(test_offline_callback_url_requires_greylist);
    RUN_TEST(test_offline_s3_and_sftp_jobs);
    RUN_TEST(test_offline_bulk_jobs_reject_before_request);
    RUN_TEST(test_offline_batch_timeout_is_a_string);
    RUN_TEST(test_offline_success_false_on_200_is_an_error);
    RUN_TEST(test_offline_safety_rails);
    RUN_TEST(test_offline_redirect_is_not_followed);
    RUN_TEST(test_offline_streaming_download);
    RUN_TEST(test_offline_list_evaluator_base_url);
    RUN_TEST(test_offline_inbox_placement);
    RUN_TEST(test_offline_inbox_placement_detail_error);
    RUN_TEST(test_offline_rejects_before_request);
    RUN_TEST(test_offline_html_403);
    RUN_TEST(test_offline_html_200_validate);
    RUN_TEST(test_offline_html_200_get_file);
    RUN_TEST(test_offline_cloudflare_json);
    RUN_TEST(test_offline_empty_and_malformed);
    RUN_TEST(test_offline_double_error_prefix);
    RUN_TEST(test_offline_batch_account_error);
    RUN_TEST(test_offline_batch_partial);
    RUN_TEST(test_offline_concurrent_clients);
}
