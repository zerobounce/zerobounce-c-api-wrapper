#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "unity.h"
#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/zb_internal.h"
#include <json.h>

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

/* The key-echo sweeps: which echoing body the fake answers every request with
   (0 for none), and whether an error has carried the key. LEAK_KEY needs
   escaping in a URL and in JSON, so its raw, URL-encoded and JSON-escaped
   forms (with '/' escaped or not) all differ. */
#define LEAK_KEY "leak-K3y/x+y\"q%z"
#define LEAK_KEY_ESCAPED "leak-K3y%2Fx%2By%22q%25z"
#define LEAK_KEY_JSON "leak-K3y\\/x+y\\\"q%z"
#define LEAK_KEY_JSON_PLAIN "leak-K3y/x+y\\\"q%z"
static int g_leak_mode;
static int g_error_leaked;

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

static void set_body_n(memory *response_data, const char *text, size_t len) {
    response_data->response = malloc(len + 1);
    memcpy(response_data->response, text, len);
    response_data->response[len] = '\0';
    response_data->size = len;
}

/* g_leak_mode 10: the strict-JSON corpus body under test, with an HTTP 200
   and application/json. g_leak_mode 11: the fuzzed echo body, with its
   status. */
static const char *g_mode_body;
static size_t g_mode_body_len;
static long g_mode_status;

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

    usleep((useconds_t)(g_leak_mode ? 0 : rand() % 21) * 1000);

    const char *content = "application/json";
    *http_code = 200;
    if (g_leak_mode) {
        /* Bodies that quote the key: a dead-key error and "success": false on
           a 200, a proxy quoting the request URL, a JSON error and a text one
           quoting the URL-encoded key. */
        switch (g_leak_mode) {
        case 1:
            set_body(response_data, "{\"error\":\"Invalid API key " LEAK_KEY_JSON "\"}");
            break;
        case 2:
            set_body(response_data, "{\"success\":false,\"message\":\"Invalid API key " LEAK_KEY_JSON_PLAIN "\"}");
            break;
        case 3:
            *http_code = 400;
            set_body(response_data,
                "Bad Request: GET https://api.zerobounce.net/v2/validate?api_key=" LEAK_KEY_ESCAPED "&email=a");
            content = "text/plain";
            break;
        case 4:
            *http_code = 400;
            set_body(response_data, "{\"error\":\"Bad key " LEAK_KEY_JSON "\"}");
            break;
        case 6:
            /* A proxy quoting the JSON it was sent, '/' escaped and not. */
            *http_code = 400;
            set_body(response_data, "Bad Request: {\"api_key\":\"" LEAK_KEY_JSON "\",\"k\":\""
                     LEAK_KEY_JSON_PLAIN "\"} raw " LEAK_KEY);
            content = "text/plain";
            break;
        case 7:
            /* A portal's page that is labelled text/html but parses as JSON. */
            set_body(response_data, "{\"a\":1}");
            content = "text/html";
            break;
        case 8:
            /* A file-looking body labelled as JSON. */
            set_body(response_data, "email\nlabelled-json@example.com\n");
            break;
        case 9:
            /* A JSON object with text after it is not JSON. */
            set_body(response_data, "{\"a\":1} <- proxy note");
            break;
        case 10:
            set_body_n(response_data, g_mode_body, g_mode_body_len);
            break;
        case 11:
            *http_code = g_mode_status;
            set_body_n(response_data, g_mode_body, g_mode_body_len);
            content = g_mode_status == 200 ? "application/json" : "text/plain";
            break;
        default:
            *http_code = 400;
            set_body(response_data, "Rejected key " LEAK_KEY_ESCAPED);
            content = "text/plain";
            break;
        }
    } else if (strncmp(key, "empty-404-", 10) == 0) {
        *http_code = 404;
        set_body(response_data, "");
    } else if (strncmp(key, "html-403-", 9) == 0 || strncmp(key, "html-200-", 9) == 0) {
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
    } else if (strncmp(key, "dead-key-", 9) == 0) {
        /* LIVE (2026-11-11): what an invalid or drained key gets back, each
           with an HTTP 200 and no "success" field. */
        if (strstr(request.path, "guessformat")) {
            set_body(response_data, "{\"Message\":\"Invalid API key or your account ran out of credits\"}");
        } else if (strstr(request.path, "filters")) {
            set_body(response_data, "{  \"Error\": \"Invalid API key or your account ran out of credits\"}");
        } else if (strstr(request.path, "getapiusage")) {
            set_body(response_data, "{\"error\":\"Invalid API key\"}");
        } else {
            set_body(response_data, "{\"error\":\"Invalid API key or your account ran out of credits\"}");
        }
    } else if (strncmp(key, "greylist-pending-", 17) == 0 && strstr(request.path, "/validate")) {
        /* The documented answer to a repeated greylist_processing validate:
           a result whose only field is Message, not an error. */
        set_body(response_data, "{\"Message\":\"Greylist validation still in progress. Please try again later.\"}");
    } else if (strncmp(key, "filter-message-", 15) == 0 && strstr(request.path, "filters")) {
        set_body(response_data, "{\"Message\":\"Filter has been added.\"}");
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
    g_error_leaked = 0;
    g_error_status = 0;
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
        if (strstr(part, LEAK_KEY) || strstr(part, LEAK_KEY_ESCAPED) ||
            strstr(part, LEAK_KEY_JSON) || strstr(part, LEAK_KEY_JSON_PLAIN)) {
            g_error_leaked = 1;
        }
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
    if (g_leak_mode != 10) {
        /* The corpus sweep's bodies hold no credits. */
        TEST_ASSERT_EQUAL_INT(7, response.credits);
    }
}

static void on_validate(ZBValidateResponse response) {
    g_success_called = 1;
    snprintf(tls_echo, sizeof(tls_echo), "%s", response.address ? response.address : "");
    snprintf(g_echo, sizeof(g_echo), "%s", tls_echo);
}

static char g_validate_message[256];

static void on_validate_message(ZBValidateResponse response) {
    g_success_called = 1;
    snprintf(g_validate_message, sizeof(g_validate_message), "%s", response.message ? response.message : "");
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
    if (g_leak_mode != 10) {
        TEST_ASSERT_TRUE(response.found);
    }
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
    if (g_leak_mode != 10) {
        TEST_ASSERT_EQUAL_INT(1, response.total);
    }
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
    /* One of first_name_column and full_name_column is required; a 0 column is
       no column at all, so the full name column carries this request. */
    finder.full_name_column = 3;
    send_email_finder_file(zb, "email_file.csv", 1, &finder, on_send, on_error);
    OfflineRequest request = only_request();
    TEST_ASSERT_EQUAL_STRING("bulkapi.zerobounce.net", request.host);
    TEST_ASSERT_EQUAL_STRING("/email-finder/sendfile", request.path);
    TEST_ASSERT_TRUE(has_field("domain_column", "1"));
    TEST_ASSERT_TRUE(has_field("has_header_row", "false"));
    TEST_ASSERT_TRUE(has_field("full_name_column", "3"));
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
    s3.remove_duplicates = 1;
    s3.remove_duplicate = 1;
    s3.allow_phase_2_is_set = true;
    s3.allow_phase_2 = true;
    s3.return_url = "https://example.com/done";
    validate_s3_file(zb, &s3, on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("bulkapi.zerobounce.net", request.host);
    TEST_ASSERT_EQUAL_STRING("/validateS3file", request.path);
    TEST_ASSERT_EQUAL_STRING("p3-key", request.api_key);
    TEST_ASSERT_EQUAL_INT(0, strncmp(request.body, "api_key=", 8));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "file_name=inbox.csv"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "email_address_column=0"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "has_header_row=false"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "remove_duplicates=true"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "return_url=https%3A%2F%2Fexample.com%2Fdone"));
    TEST_ASSERT_NULL(strstr(request.body, "first_name_column"));
    /* validateS3file documents remove_duplicate*s*, and allow_phase_2 belongs
       to the multipart bulk sendfile; neither of the two goes on this wire. */
    TEST_ASSERT_NULL(strstr(request.body, "remove_duplicate=true"));
    TEST_ASSERT_NULL(strstr(request.body, "allow_phase_2"));
    TEST_ASSERT_EQUAL_STRING("p3-key", g_echo);
    reset_offline();
    ScoreS3FileOptions score = new_score_s3_file_options();
    score.file_name = "inbox.csv";
    score.return_url = "https://example.com/done";
    score_s3_file(zb, &score, on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("/scoreS3file", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "file_name=inbox.csv"));
    TEST_ASSERT_NOT_NULL(strstr(request.body, "return_url=https%3A%2F%2Fexample.com%2Fdone"));
    TEST_ASSERT_NULL(strstr(request.body, "email_address_column"));
    reset_offline();
    export_s3_file(zb, "fid", "bucket-out", on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/exportS3file", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "export_bucket=bucket-out"));
    reset_offline();
    /* The export bucket is optional, the same as export_path on the SFTP twin:
       without it the endpoint uses the one from the account settings. */
    export_s3_file(zb, "fid", NULL, on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("/exportS3file", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.body, "file_id=fid"));
    TEST_ASSERT_NULL(strstr(request.body, "export_bucket"));
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

void test_offline_s3_jobs_require_a_return_url(void) {
    reset_offline();
    ZeroBounce *zb = client_with("s3-key");
    ValidateS3FileOptions validate = new_validate_s3_file_options();
    ScoreS3FileOptions score = new_score_s3_file_options();

    /* LIVE: POST /validateS3file without a return_url answers 400 "Provided
       return URL is not permitted: None", so it never leaves the SDK. */
    validate.file_name = "inbox.csv";
    validate_s3_file(zb, &validate, on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "return_url"));

    g_error_called = 0;
    validate.file_name = "   ";
    validate.return_url = "https://example.com/done";
    validate_s3_file(zb, &validate, on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "file_name"));

    /* scoreS3file had no return_url field at all and so could never send one. */
    g_error_called = 0;
    score.file_name = "inbox.csv";
    score_s3_file(zb, &score, on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "return_url"));

    g_error_called = 0;
    score_s3_file(zb, NULL, on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "file_name"));

    g_error_called = 0;
    export_s3_file(zb, "fid", "   ", on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "export_bucket"));

    TEST_ASSERT_EQUAL_INT(0, g_count);
}

void test_offline_changed_calls_report_error_bodies(void) {
    ValidateS3FileOptions validate = new_validate_s3_file_options();
    ScoreS3FileOptions score = new_score_s3_file_options();
    ZBListInboxPlacementTestsOptions list = new_list_inbox_placement_tests_options();

    validate.file_name = "inbox.csv";
    validate.return_url = "https://example.com/done";
    score.file_name = "inbox.csv";
    score.return_url = "https://example.com/done";

    /* An HTTP 200 with "success": false is a failure, and the status stays on
       the error. */
    reset_offline();
    validate_s3_file(client_with("success-false"), &validate, on_send, on_error);
    TEST_ASSERT_FALSE(g_success_called);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "File cannot be found"));
    TEST_ASSERT_EQUAL_INT(200, (int)g_error_status);

    /* A blocked request answers with an HTML page; it must not reach the
       caller as a result, or whole, as an error. */
    reset_offline();
    score_s3_file(client_with("html-403-user"), &score, on_send, on_error);
    TEST_ASSERT_FALSE(g_success_called);
    expect_short_error();
    TEST_ASSERT_EQUAL_INT(403, (int)g_error_status);

    reset_offline();
    export_s3_file(client_with("empty-user"), "fid", NULL, on_send, on_error);
    TEST_ASSERT_FALSE(g_success_called);
    expect_short_error();

    /* The Tester is a Django REST framework app and reports errors under
       "detail". */
    reset_offline();
    create_inbox_placement_test(client_with("ipt-detail"), "Launch", NULL, on_raw, on_error);
    TEST_ASSERT_FALSE(g_success_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "Inbox placement test not found."));
    TEST_ASSERT_EQUAL_STRING("Authorization: Bearer ipt-detail", only_request().extra_header);
    TEST_ASSERT_NULL(strstr(only_request().query, "api_key="));

    reset_offline();
    list_inbox_placement_tests(client_with("malformed-ipt"), &list, on_raw, on_error);
    TEST_ASSERT_FALSE(g_success_called);
    expect_short_error();

    reset_offline();
    send_list_evaluator_file(client_with("html-200-eval"), "email_file.csv", 1, on_send, on_error);
    TEST_ASSERT_FALSE(g_success_called);
    expect_short_error();
}

void test_offline_email_finder_file_needs_a_name_column(void) {
    reset_offline();
    ZeroBounce *zb = client_with("finder-key");
    EmailFinderSendFileOptions options = new_email_finder_send_file_options();

    /* The endpoint has no name to search for without one of the two columns. */
    send_email_finder_file(zb, "email_file.csv", 1, &options, on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "first_name_column"));
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "full_name_column"));

    g_error_called = 0;
    send_email_finder_file(zb, "email_file.csv", 1, NULL, on_send, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "full_name_column"));
    TEST_ASSERT_EQUAL_INT(0, g_count);

    g_error_called = 0;
    options.first_name_column = 2;
    send_email_finder_file(zb, "email_file.csv", 1, &options, on_send, on_error);
    TEST_ASSERT_FALSE(g_error_called);
    TEST_ASSERT_EQUAL_STRING("/email-finder/sendfile", only_request().path);
    TEST_ASSERT_TRUE(has_field("first_name_column", "2"));
}

void test_offline_send_file_without_a_return_url(void) {
    reset_offline();
    ZeroBounce *zb = client_with("send-key");
    /* A caller that zeroes SendFileOptions rather than calling
       new_send_file_options leaves return_url NULL. That used to reach an
       unguarded strlen on the live request path. */
    SendFileOptions options;
    memset(&options, 0, sizeof(options));

    send_file(zb, "email_file.csv", 1, options, on_send, on_error);

    TEST_ASSERT_FALSE(g_error_called);
    TEST_ASSERT_EQUAL_STRING("/v2/sendfile", only_request().path);
    TEST_ASSERT_FALSE(has_field("return_url", NULL));
}

void test_offline_bulk_base_url_overrides(void) {
    reset_offline();
    ZeroBounce *zb = client_with("base-key");
    SendFileOptions options = new_send_file_options();
    ZBBulkFileJobOptions job = new_bulk_file_job_options();

    /* The bulk hosts are not built from api_base_url, so each one has its own
       setter. The region warning in zb_check_request_url points at these. */
    zero_bounce_set_bulk_api_base_url(zb, "https://bulk.example.test/v2");
    zero_bounce_set_bulk_api_scoring_base_url(zb, "https://bulk.example.test/v2/scoring");
    zero_bounce_set_bulk_api_root_url(zb, "https://bulk.example.test");

    send_file(zb, "email_file.csv", 1, options, on_send, on_error);
    TEST_ASSERT_EQUAL_STRING("bulk.example.test", only_request().host);
    TEST_ASSERT_EQUAL_STRING("/v2/sendfile", only_request().path);

    reset_offline();
    scoring_send_file(zb, "email_file.csv", 1, options, on_send, on_error);
    TEST_ASSERT_EQUAL_STRING("/v2/scoring/sendfile", only_request().path);

    reset_offline();
    job.file_name = "leads.csv";
    job.return_url = "https://example.com/done";
    domain_search_s3_file(zb, &job, on_send, on_error);
    TEST_ASSERT_EQUAL_STRING("bulk.example.test", only_request().host);
    TEST_ASSERT_EQUAL_STRING("/domainSearchS3file", only_request().path);
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

    /* Both halves of the list evaluator follow the override. The upload used
       to be built from bulk_api_base_url, which split the two across hosts. */
    reset_offline();
    send_list_evaluator_file(zb, "email_file.csv", 1, on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("bulk.example.test", request.host);
    TEST_ASSERT_EQUAL_STRING("/v2/listevaluator/", request.path);
    TEST_ASSERT_TRUE(has_field("email_address_column", "1"));
    TEST_ASSERT_TRUE(has_field("api_key", "eval-key"));

    zero_bounce_set_list_evaluator_base_url(zb, "https://bulkapi.zerobounce.net/v2/listevaluator");
    reset_offline();
    send_list_evaluator_file(zb, "email_file.csv", 1, on_send, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("bulkapi.zerobounce.net", request.host);
    TEST_ASSERT_EQUAL_STRING("/v2/listevaluator/", request.path);
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
    static const int provider_ids[] = {3, 7};
    static const int seed_ids[] = {11};
    static const ZBInboxPlacementProvider providers[] = {{3, 10}, {7, -1}};
    create.test_type = 1;
    create.sender_email = "news@example.com";
    create.webhook_url = "https://example.com/ipt";
    create.provider_ids = provider_ids;
    create.provider_ids_count = 2;
    create.seed_ids = seed_ids;
    create.seed_ids_count = 1;
    create.providers = providers;
    create.providers_count = 2;
    create_inbox_placement_test(zb, "Launch", &create, on_raw, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("POST", request.method);
    TEST_ASSERT_EQUAL_STRING("/api/ipt/test", request.path);
    /* Bodies are camelCase JSON, and unset fields are left out. A provider
       without a maxSeeds carries only its id. */
    TEST_ASSERT_EQUAL_STRING(
        "{\"name\":\"Launch\",\"testType\":1,\"senderEmail\":\"news@example.com\","
        "\"webhookUrl\":\"https://example.com/ipt\",\"providerIds\":[3,7],"
        "\"seedIds\":[11],\"providers\":[{\"id\":3,\"maxSeeds\":10},{\"id\":7}]}",
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
    list.created_after = "2026-01-01";
    list.created_before = "2026-02-01";
    list_inbox_placement_tests(zb, &list, on_raw, on_error);
    request = only_request();
    /* LIVE: the listing is GET /tests (plural). /test is the single-test path
       and answers 'Method "GET" not allowed'. */
    TEST_ASSERT_EQUAL_STRING("/api/ipt/tests", request.path);
    TEST_ASSERT_NOT_NULL(strstr(request.query, "status=finished"));
    TEST_ASSERT_NOT_NULL(strstr(request.query, "page=1"));
    TEST_ASSERT_NOT_NULL(strstr(request.query, "createdAfter=2026-01-01"));
    TEST_ASSERT_NOT_NULL(strstr(request.query, "createdBefore=2026-02-01"));
    /* LIVE: the page size filter is page_size. pageSize is accepted and then
       silently ignored, so the caller quietly got the default page. */
    TEST_ASSERT_NOT_NULL(strstr(request.query, "page_size=25"));
    TEST_ASSERT_NULL(strstr(request.query, "pageSize"));

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

void test_offline_inbox_placement_limits(void) {
    reset_offline();
    ZeroBounce *zb = client_with("ipt-key");
    ZBCreateInboxPlacementTestOptions create = new_create_inbox_placement_test_options();
    ZBListInboxPlacementTestsOptions list = new_list_inbox_placement_tests_options();
    char long_name[80];

    /* The name is capped at 64 characters. */
    memset(long_name, 'n', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    create_inbox_placement_test(zb, long_name, NULL, on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "name"));

    g_error_called = 0;
    long_name[64] = '\0';
    create_inbox_placement_test(zb, long_name, NULL, on_raw, on_error);
    TEST_ASSERT_FALSE(g_error_called);
    TEST_ASSERT_EQUAL_INT(1, g_count);

    /* testType is 1, 2 or 3. */
    reset_offline();
    create.test_type = 4;
    create_inbox_placement_test(zb, "Launch", &create, on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "test_type"));

    /* A type 1 test has nowhere to send from without a sender address. */
    g_error_called = 0;
    create.test_type = 1;
    create.sender_email = NULL;
    create_inbox_placement_test(zb, "Launch", &create, on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "sender_email"));

    /* Types 2 and 3 do not need one. */
    g_error_called = 0;
    create.test_type = 2;
    create_inbox_placement_test(zb, "Launch", &create, on_raw, on_error);
    TEST_ASSERT_FALSE(g_error_called);
    TEST_ASSERT_EQUAL_STRING("{\"name\":\"Launch\",\"testType\":2}", only_request().body);

    /* page_size is between 1 and 100. */
    reset_offline();
    list.page_size = 0;
    list_inbox_placement_tests(zb, &list, on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "page_size"));

    g_error_called = 0;
    list.page_size = 101;
    list_inbox_placement_tests(zb, &list, on_raw, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "page_size"));
    TEST_ASSERT_EQUAL_INT(0, g_count);

    g_error_called = 0;
    list.page_size = 100;
    list_inbox_placement_tests(zb, &list, on_raw, on_error);
    TEST_ASSERT_FALSE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(only_request().query, "page_size=100"));
}

void test_offline_inbox_placement_json_is_escaped(void) {
    reset_offline();
    ZeroBounce *zb = client_with("ipt-key");
    ZBCreateInboxPlacementTestOptions create = new_create_inbox_placement_test_options();
    char long_url[2048];
    OfflineRequest request;

    /*
     * The bodies used to be concatenated into a fixed 1024-byte buffer with no
     * escaping, so a quote closed the JSON string early and injected whatever
     * followed, and anything longer than the buffer was cut off mid-field.
     */
    create.sender_email = "a\"b\\c@example.com";
    create.webhook_url = "https://example.com/a\tb";
    create_inbox_placement_test(zb, "He said \"go\"", &create, on_raw, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING(
        "{\"name\":\"He said \\\"go\\\"\","
        "\"senderEmail\":\"a\\\"b\\\\c@example.com\","
        "\"webhookUrl\":\"https://example.com/a\\tb\"}",
        request.body
    );

    /* A long value grows the body instead of being truncated. */
    reset_offline();
    memset(long_url, 'u', sizeof(long_url) - 1);
    long_url[sizeof(long_url) - 1] = '\0';
    memcpy(long_url, "https://example.com/", 20);
    set_inbox_placement_webhook(zb, long_url, on_raw, on_error);
    request = only_request();
    TEST_ASSERT_EQUAL_STRING("PUT", request.method);
    TEST_ASSERT_EQUAL_STRING("/api/ipt/webhook", request.path);
    /* {"webhookUrl":"…"} is the url plus 17 bytes of wrapper. */
    TEST_ASSERT_EQUAL_INT((int)(strlen(long_url) + 17), (int)strlen(request.body));
    TEST_ASSERT_NOT_NULL(strstr(request.body, long_url));
    TEST_ASSERT_EQUAL_STRING("}", request.body + strlen(request.body) - 1);
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

void test_offline_form_body_is_not_prefixed_with_garbage(void) {
    reset_offline();
    /* The form body starts out as a NULL pointer with a zero capacity, and
       realloc(NULL, n) hands back an uninitialized block. Appending to it
       without terminating it first put stray bytes in front of the first
       field, which the API saw as a malformed request. */
    ZeroBounce *zb = client_with("form-body-key");

    add_filter(zb, "allow", "domain", "example.com", on_raw, on_error);
    {
        OfflineRequest request = only_request();
        TEST_ASSERT_EQUAL_INT(0, strncmp(request.body, "api_key=", 8));
        TEST_ASSERT_NOT_NULL(strstr(request.body, "&rule=allow"));
        TEST_ASSERT_NOT_NULL(strstr(request.body, "&target=domain"));
        TEST_ASSERT_NOT_NULL(strstr(request.body, "&value=example.com"));
    }

    reset_offline();
    send_evaluated_file(zb, "fid-1", on_send, on_error);
    TEST_ASSERT_EQUAL_INT(0, strncmp(only_request().body, "api_key=", 8));

    reset_offline();
    {
        ZBBulkFileJobOptions options = new_bulk_file_job_options();
        options.file_name = "f.csv";
        options.return_url = "https://example.com/done";
        email_finder_s3_file(zb, &options, on_send, on_error);
        TEST_ASSERT_EQUAL_INT(0, strncmp(only_request().body, "api_key=", 8));
    }
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

static void expect_dead_key(const char *name, const char *message) {
    TEST_ASSERT_FALSE_MESSAGE(g_success_called, name);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(message, g_error_text, name);
    reset_offline();
}

/* An invalid or drained key answers with an HTTP 200 and an error-only body.
   Every call must reach the error callback with the API's own message. */
void test_offline_dead_key_is_an_error(void) {
    const char *drained = "Invalid API key or your account ran out of credits";
    reset_offline();
    ZeroBounce *zb = client_with("dead-key-user");
    validate_email(zb, "a@example.com", "", on_validate, on_error);
    expect_dead_key("validate", drained);
    get_activity_data(zb, "a@example.com", on_activity, on_error);
    expect_dead_key("activity", drained);
    struct tm start = {0};
    struct tm end = {0};
    start.tm_year = 126; start.tm_mon = 9; start.tm_mday = 1;
    end.tm_year = 126; end.tm_mon = 9; end.tm_mday = 2;
    get_api_usage(zb, start, end, on_usage, on_error);
    expect_dead_key("api usage", "Invalid API key");
    find_email_by_domain_first_name(zb, "example.com", "Ada", on_find, on_error);
    expect_dead_key("find email", drained);
    search_domain_by_domain(zb, "example.com", on_domain, on_error);
    expect_dead_key("domain search", drained);
    find_email(zb, "example.com", "Ada", "", "Lovelace", on_find, on_error);
    expect_dead_key("find_email (deprecated)", drained);
    add_filter(zb, "allow", "email", "x@example.com", on_raw, on_error);
    expect_dead_key("add_filter", drained);
    delete_filter(zb, "allow", "email", "x@example.com", on_raw, on_error);
    expect_dead_key("delete_filter", drained);
    list_filters(zb, false, on_raw, on_error);
    expect_dead_key("list_filters GET", drained);
    list_filters(zb, true, on_raw, on_error);
    expect_dead_key("list_filters POST", drained);
}

/* The error-body checks must not turn a real result into a failure. */
void test_offline_bare_message_is_a_result_where_the_api_means_one(void) {
    reset_offline();
    ZeroBounce *zb = client_with("greylist-pending-user");
    g_validate_message[0] = '\0';
    validate_email(zb, "a@example.com", "", on_validate_message, on_error);
    TEST_ASSERT_FALSE_MESSAGE(g_error_called, g_error_text);
    TEST_ASSERT_TRUE(g_success_called);
    /* The pending answer is a result, and its text lands on response.message. */
    TEST_ASSERT_EQUAL_STRING("Greylist validation still in progress. Please try again later.", g_validate_message);
    reset_offline();
    zb = client_with("filter-message-user");
    add_filter(zb, "allow", "email", "x@example.com", on_raw, on_error);
    TEST_ASSERT_FALSE_MESSAGE(g_error_called, g_error_text);
    TEST_ASSERT_TRUE(g_success_called);
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

/* -- downloads, base URLs, HTTPS and keys -------------------------------- */

static int file_has(const char *path, const char *expected) {
    char content[256] = {0};
    FILE *handle = fopen(path, "rb");
    if (!handle) {
        return 0;
    }
    size_t n = fread(content, 1, sizeof(content) - 1, handle);
    fclose(handle);
    content[n] = '\0';
    return strcmp(content, expected) == 0;
}

void test_offline_get_file_writes_without_a_success_callback(void) {
    const char *paths[] = {
        "test/downloads/nocb-get.csv", "test/downloads/nocb-score.csv",
        "test/downloads/nocb-finder.csv", "test/downloads/nocb-domain.csv",
    };
    ZeroBounce *zb = client_with("nocb");
    for (int i = 0; i < 4; i++) {
        remove(paths[i]);
    }
    reset_offline();
    get_file(zb, "fid", (char *)paths[0], NULL, on_error);
    scoring_get_file(zb, "fid", (char *)paths[1], NULL, on_error);
    email_finder_get_file(zb, "fid", (char *)paths[2], NULL, on_error);
    domain_search_get_file(zb, "fid", (char *)paths[3], NULL, on_error);
    TEST_ASSERT_FALSE(g_error_called);
    TEST_ASSERT_EQUAL_INT(4, g_count);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE_MESSAGE(file_has(paths[i], "email\nnocb@example.com\n"), paths[i]);
        remove(paths[i]);
    }
}

void test_offline_get_file_that_cannot_be_written_is_an_error(void) {
    /* A regular file used as a directory: nothing can be created under it,
       whoever runs the test (root included). */
    const char *blocker = "test/downloads/not-a-directory";
    char path[128];
    FILE *handle = fopen(blocker, "wb");
    TEST_ASSERT_NOT_NULL(handle);
    fputs("x", handle);
    fclose(handle);
    snprintf(path, sizeof(path), "%s/out.csv", blocker);

    ZeroBounce *zb = client_with("unwritable");
    reset_offline();
    get_file(zb, "fid", path, on_get, on_error);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, "get_file");
    TEST_ASSERT_FALSE(g_success_called);
    reset_offline();
    scoring_get_file(zb, "fid", path, on_get, on_error);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, "scoring_get_file");
    reset_offline();
    email_finder_get_file(zb, "fid", path, NULL, on_error);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, "email_finder_get_file");
    reset_offline();
    domain_search_get_file(zb, "fid", path, NULL, on_error);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, "domain_search_get_file");
    reset_offline();
    get_file_streaming(zb, "fid", path, NULL, NULL, on_error);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, "get_file_streaming");
    remove(blocker);
}

void test_offline_get_file_json_error_keeps_the_http_status(void) {
    /* The API answers an unknown file id with an HTTP 200 JSON error. */
    ZeroBounce *zb = client_with("success-false");
    reset_offline();
    g_error_status = -1;
    get_file(zb, "fid", "test/downloads/json-error.csv", on_get, on_error);
    TEST_ASSERT_TRUE(g_error_called);
    TEST_ASSERT_NOT_NULL(strstr(g_error_text, "File cannot be found"));
    TEST_ASSERT_EQUAL_INT(200, (int)g_error_status);
    TEST_ASSERT_NULL(fopen("test/downloads/json-error.csv", "rb"));
}

/* One scripted HTTP 200 answer for every request, for the download tests. */
static const char *g_scripted_body;
static const char *g_scripted_type;
static int g_scripted_errors;
static int g_scripted_successes;
static long g_scripted_status;
static char g_scripted_text[256];

static int scripted_perform(memory *response_data, long *http_code, char **content_type_out) {
    size_t len = strlen(g_scripted_body);
    response_data->response = malloc(len + 1);
    memcpy(response_data->response, g_scripted_body, len + 1);
    response_data->size = len;
    *http_code = 200;
    if (content_type_out) {
        *content_type_out = g_scripted_type ? strdup(g_scripted_type) : NULL;
    }
    return 1;
}

static void scripted_on_error(ZBErrorResponse error) {
    g_scripted_errors++;
    g_scripted_status = error.status_code;
    snprintf(g_scripted_text, sizeof(g_scripted_text), "%s", error.errors.size ? error.errors.data[0] : "");
}

static void scripted_on_get(ZBGetFileResponse response) {
    (void)response;
    g_scripted_successes++;
}

#define DOWNLOAD_KINDS 6
static const char *download_name(int kind) {
    static const char *names[DOWNLOAD_KINDS] = {
        "get_file", "scoring_get_file", "get_file_streaming", "scoring_get_file_streaming",
        "email_finder_get_file", "domain_search_get_file"};
    return names[kind];
}

static void download(int kind, ZeroBounce *zb, const char *path) {
    switch (kind) {
    case 0: get_file(zb, "fid", (char *)path, scripted_on_get, scripted_on_error); break;
    case 1: scoring_get_file(zb, "fid", (char *)path, scripted_on_get, scripted_on_error); break;
    case 2: get_file_streaming(zb, "fid", (char *)path, NULL, scripted_on_get, scripted_on_error); break;
    case 3: scoring_get_file_streaming(zb, "fid", (char *)path, NULL, scripted_on_get, scripted_on_error); break;
    case 4: email_finder_get_file(zb, "fid", (char *)path, scripted_on_get, scripted_on_error); break;
    default: domain_search_get_file(zb, "fid", (char *)path, scripted_on_get, scripted_on_error); break;
    }
}

void test_offline_a_json_object_in_place_of_a_file_is_an_error(void) {
    /* LIVE: getfile answers with a JSON object to say why there is no file:
       {"error": ...}, {"message": ...}, and for an invalid key {"Message": ...}
       or {"Error": ...}. Any JSON object, with a JSON content type or not, is
       an error carrying the HTTP status, and nothing is written. The streaming
       download used to save all of these; the others saved "Message" and
       "Error" bodies without a JSON content type. */
    const char *bodies[] = {"{\"error\":\"File not found\"}", "{\"message\":\"File not ready\"}",
                            "{\"Message\":\"File not found\"}", " {\"Error\":\"Invalid API key\"}",
                            "{\"file_id\":\"x\"}"};
    const char *types[] = {"application/json", "text/csv", NULL};
    const char *path = "test/downloads/offline-json-object.csv";
    char message[160];
    ZeroBounce *zb = client_with("json-object-key");

    zero_bounce_set_http_perform_for_test(scripted_perform);
    for (size_t b = 0; b < sizeof(bodies) / sizeof(bodies[0]); b++) {
        for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
            for (int kind = 0; kind < DOWNLOAD_KINDS; kind++) {
                FILE *handle;
                g_scripted_body = bodies[b];
                g_scripted_type = types[t];
                g_scripted_errors = g_scripted_successes = 0;
                g_scripted_status = -1;
                remove(path);
                download(kind, zb, path);
                handle = fopen(path, "rb");
                if (handle) {
                    fclose(handle);
                    remove(path);
                }
                snprintf(message, sizeof(message), "%s %s %s", download_name(kind), bodies[b],
                         types[t] ? types[t] : "(no content type)");
                TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_scripted_errors, message);
                TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_scripted_successes, message);
                TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)g_scripted_status, message);
                TEST_ASSERT_NULL_MESSAGE(handle, message);
            }
        }
    }
    /* The error carries the API's message. */
    g_scripted_body = "{\"Message\":\"File not found\"}";
    g_scripted_type = "text/csv";
    for (int kind = 0; kind < DOWNLOAD_KINDS; kind++) {
        g_scripted_text[0] = '\0';
        download(kind, zb, path);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("File not found", g_scripted_text, download_name(kind));
    }
    TEST_ASSERT_TRUE(zb_get_file_json_indicates_error("{\"Error\":\"Invalid API key\"}"));
    zero_bounce_set_http_perform_for_test(offline_perform);
}

void test_offline_an_html_page_in_place_of_a_file_is_an_error(void) {
    /* A proxy or captive portal answers 200 with an HTML page. Whether the
       content type or the body says HTML, it is an error with the status and
       nothing is written. A short page without <html> used to be saved. */
    const char *bodies[] = {"<p>Access denied by proxy</p>", "Access denied", "<html><body>x</body></html>"};
    const char *types[] = {"text/html", "TEXT/HTML; charset=UTF-8", "text/csv"};
    const char *path = "test/downloads/offline-html.csv";
    char message[200];
    ZeroBounce *zb = client_with("html-ct-key");

    zero_bounce_set_http_perform_for_test(scripted_perform);
    for (size_t b = 0; b < sizeof(bodies) / sizeof(bodies[0]); b++) {
        for (size_t t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
            /* A body that is not HTML under text/csv is a file. */
            int is_file = t == 2 && b < 2;
            for (int kind = 0; kind < DOWNLOAD_KINDS; kind++) {
                FILE *handle;
                g_scripted_body = bodies[b];
                g_scripted_type = types[t];
                g_scripted_errors = g_scripted_successes = 0;
                g_scripted_status = -1;
                remove(path);
                download(kind, zb, path);
                handle = fopen(path, "rb");
                if (handle) {
                    fclose(handle);
                    remove(path);
                }
                snprintf(message, sizeof(message), "%s %s %s", download_name(kind), bodies[b], types[t]);
                TEST_ASSERT_EQUAL_INT_MESSAGE(is_file ? 0 : 1, g_scripted_errors, message);
                TEST_ASSERT_EQUAL_INT_MESSAGE(is_file ? 1 : 0, g_scripted_successes, message);
                if (is_file) {
                    TEST_ASSERT_NOT_NULL_MESSAGE(handle, message);
                } else {
                    TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)g_scripted_status, message);
                    TEST_ASSERT_NULL_MESSAGE(handle, message);
                }
            }
        }
    }
    zero_bounce_set_http_perform_for_test(offline_perform);
}

/* remove_duplicate goes out only when the caller sets it: the API's own
   default (true) applies otherwise. */
/* remove_duplicate is always sent with the caller's value, true by default,
   as in 3.0.x: an explicit false is never dropped, whether or not the
   remove_duplicate_set field (no longer needed) is set. */
void test_offline_send_file_always_sends_remove_duplicate(void) {
    ZeroBounce *zb = client_with("file-key");
    for (int scoring = 0; scoring < 2; scoring++) {
        SendFileOptions options = new_send_file_options();
        reset_offline();
        if (scoring) {
            scoring_send_file(zb, "email_file.csv", 1, options, on_send, on_error);
        } else {
            send_file(zb, "email_file.csv", 1, options, on_send, on_error);
        }
        only_request();
        TEST_ASSERT_TRUE(has_field("remove_duplicate", "true"));
        TEST_ASSERT_TRUE(has_field("has_header_row", "true"));
        for (int set = 0; set < 2; set++) {
            for (int value = 0; value < 2; value++) {
                options.remove_duplicate = value;
                options.remove_duplicate_set = set;
                reset_offline();
                if (scoring) {
                    scoring_send_file(zb, "email_file.csv", 1, options, on_send, on_error);
                } else {
                    send_file(zb, "email_file.csv", 1, options, on_send, on_error);
                }
                only_request();
                TEST_ASSERT_TRUE(has_field("remove_duplicate", value ? "true" : "false"));
            }
        }
    }
}

/* The files in dir, so a test can tell that nothing was left beside the target. */
static int count_dir_entries(const char *dir) {
    int count = 0;
    struct dirent *entry;
    DIR *handle = opendir(dir);
    if (!handle) {
        return -1;
    }
    while ((entry = readdir(handle)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    closedir(handle);
    return count;
}

/* Empties dir, so files left by an earlier run do not count. */
static void empty_dir(const char *dir) {
    struct dirent *entry;
    char path[512];
    DIR *handle = opendir(dir);
    if (!handle) {
        return;
    }
    while ((entry = readdir(handle)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
            remove(path);
        }
    }
    closedir(handle);
}

static void write_text_file(const char *path, const char *text) {
    FILE *handle = fopen(path, "wb");
    if (handle) {
        fputs(text, handle);
        fclose(handle);
    }
}

static void read_text_file(const char *path, char *out, size_t size) {
    FILE *handle = fopen(path, "rb");
    size_t n = 0;
    out[0] = '\0';
    if (handle) {
        n = fread(out, 1, size - 1, handle);
        out[n] = '\0';
        fclose(handle);
    } else {
        snprintf(out, size, "<missing>");
    }
}

void test_offline_a_failed_write_keeps_the_file_already_at_the_path(void) {
    /* D4: get_file, scoring_get_file, email_finder_get_file and
       domain_search_get_file opened the target with "wb" and removed it after
       a short write, so a file size limit (or a full disk) deleted a result
       that was already there. Every download now writes beside it and moves
       the file into place only when complete. */
    static char long_body[6001];
    const char *bodies[2] = {"email\nkeep-existing@example.com\n", long_body};
    const char *dir = "test/downloads/keep-existing";
    const char *path = "test/downloads/keep-existing/result.csv";
    int errors[2][DOWNLOAD_KINDS];
    int successes[2][DOWNLOAD_KINDS];
    int entries[2][DOWNLOAD_KINDS];
    char kept[2][DOWNLOAD_KINDS][16];
    struct rlimit saved;
    struct rlimit tight;
    void (*previous)(int);
    char message[96];
    ZeroBounce *zb = client_with("keep-existing-key");

    memset(long_body, 'a', sizeof(long_body) - 1);
    memcpy(long_body, "email\n", 6);
    long_body[sizeof(long_body) - 1] = '\0';
    mkdir("test/downloads", 0777);
    mkdir(dir, 0777);
    empty_dir(dir);
    zero_bounce_set_http_perform_for_test(scripted_perform);
    g_scripted_type = "text/csv";
    write_text_file(path, "OLD");

    TEST_ASSERT_EQUAL_INT(0, getrlimit(RLIMIT_FSIZE, &saved));
    tight = saved;
    tight.rlim_cur = 8;
    previous = signal(SIGXFSZ, SIG_IGN);
    TEST_ASSERT_EQUAL_INT(0, setrlimit(RLIMIT_FSIZE, &tight));
    /* No assertion until the limit is lifted (see the short-write test). */
    for (int b = 0; b < 2; b++) {
        g_scripted_body = bodies[b];
        for (int kind = 0; kind < DOWNLOAD_KINDS; kind++) {
            g_scripted_errors = g_scripted_successes = 0;
            download(kind, zb, path);
            errors[b][kind] = g_scripted_errors;
            successes[b][kind] = g_scripted_successes;
            read_text_file(path, kept[b][kind], sizeof(kept[b][kind]));
            entries[b][kind] = count_dir_entries(dir);
            write_text_file(path, "OLD");
        }
    }
    setrlimit(RLIMIT_FSIZE, &saved);
    signal(SIGXFSZ, previous);

    /* Without the limit the download replaces the old file. */
    g_scripted_body = bodies[0];
    for (int kind = 0; kind < DOWNLOAD_KINDS; kind++) {
        char replaced[64];
        write_text_file(path, "OLD");
        g_scripted_errors = g_scripted_successes = 0;
        download(kind, zb, path);
        read_text_file(path, replaced, sizeof(replaced));
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_scripted_successes, download_name(kind));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(bodies[0], replaced, download_name(kind));
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, count_dir_entries(dir), download_name(kind));
    }
    zero_bounce_set_http_perform_for_test(offline_perform);
    remove(path);
    rmdir(dir);

    for (int b = 0; b < 2; b++) {
        for (int kind = 0; kind < DOWNLOAD_KINDS; kind++) {
            snprintf(message, sizeof(message), "%s, %zu-byte body", download_name(kind), strlen(bodies[b]));
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, errors[b][kind], message);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, successes[b][kind], message);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("OLD", kept[b][kind], message);
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, entries[b][kind], message);
        }
    }
}

/* SendFileOptions keeps the 3.0.1 shape: remove_duplicate is a bool (a
   bool pointer to it compiles), and remove_duplicate_set comes last, so a
   positional initialiser written for 3.0.1 compiles and leaves it unset. */
void test_offline_send_file_options_stay_source_compatible(void) {
    SendFileOptions options = new_send_file_options();
    bool *p = &options.remove_duplicate;
    *p = false;
    bool b = options.remove_duplicate;
    TEST_ASSERT_FALSE(b);
    TEST_ASSERT_FALSE(options.remove_duplicate_set);
    TEST_ASSERT_TRUE(new_send_file_options().remove_duplicate);
    _Static_assert(sizeof(((SendFileOptions *)0)->remove_duplicate) == sizeof(bool),
                   "remove_duplicate must stay a bool");
    /* A 3.0.x positional initialiser and zero initialisation send the
       remove_duplicate they hold, as 3.0.x did. */
    SendFileOptions positional = {"", 0, 0, 0, 0, true, false, false, false};
    TEST_ASSERT_FALSE(positional.remove_duplicate_set);
    SendFileOptions zeroed = {0};
    ZeroBounce *zb = client_with("file-key");
    reset_offline();
    send_file(zb, "email_file.csv", 1, positional, on_send, on_error);
    only_request();
    TEST_ASSERT_TRUE(has_field("remove_duplicate", "false"));
    positional.remove_duplicate = true;
    reset_offline();
    send_file(zb, "email_file.csv", 1, positional, on_send, on_error);
    only_request();
    TEST_ASSERT_TRUE(has_field("remove_duplicate", "true"));
    reset_offline();
    zeroed.return_url = "";
    send_file(zb, "email_file.csv", 1, zeroed, on_send, on_error);
    only_request();
    TEST_ASSERT_TRUE(has_field("remove_duplicate", "false"));
}

void test_offline_a_short_write_is_an_error_and_leaves_no_partial_file(void) {
    /* A file size limit stops the write part-way, as a full disk would. Every
       download reports it and removes the partial file; the streaming one
       used to report success. Bodies shorter and longer than the streaming
       probe (4096 bytes) take its two write paths. */
    static char long_body[6001];
    const char *bodies[2] = {"email\nshort-write@example.com\n", long_body};
    const char *path = "test/downloads/offline-short-write.csv";
    int errors[2][DOWNLOAD_KINDS];
    int successes[2][DOWNLOAD_KINDS];
    int left[2][DOWNLOAD_KINDS];
    char text[2][DOWNLOAD_KINDS][sizeof(g_scripted_text)];
    struct rlimit saved;
    struct rlimit tight;
    void (*previous)(int);
    char message[96];
    ZeroBounce *zb = client_with("short-write-key");

    memset(long_body, 'a', sizeof(long_body) - 1);
    memcpy(long_body, "email\n", 6);
    long_body[sizeof(long_body) - 1] = '\0';
    zero_bounce_set_http_perform_for_test(scripted_perform);
    g_scripted_type = "text/csv";
    remove(path);

    TEST_ASSERT_EQUAL_INT(0, getrlimit(RLIMIT_FSIZE, &saved));
    tight = saved;
    tight.rlim_cur = 8;
    previous = signal(SIGXFSZ, SIG_IGN);
    TEST_ASSERT_EQUAL_INT(0, setrlimit(RLIMIT_FSIZE, &tight));
    /* No assertion until the limit is lifted: a failed one would leave it in
       place, and a test log that is a file is under the same limit. */
    for (int b = 0; b < 2; b++) {
        g_scripted_body = bodies[b];
        for (int kind = 0; kind < DOWNLOAD_KINDS; kind++) {
            FILE *handle;
            g_scripted_errors = g_scripted_successes = 0;
            g_scripted_text[0] = '\0';
            download(kind, zb, path);
            errors[b][kind] = g_scripted_errors;
            successes[b][kind] = g_scripted_successes;
            snprintf(text[b][kind], sizeof(text[b][kind]), "%s", g_scripted_text);
            handle = fopen(path, "rb");
            left[b][kind] = handle != NULL;
            if (handle) {
                fclose(handle);
                remove(path);
            }
        }
    }
    setrlimit(RLIMIT_FSIZE, &saved);
    signal(SIGXFSZ, previous);
    zero_bounce_set_http_perform_for_test(offline_perform);

    for (int b = 0; b < 2; b++) {
        for (int kind = 0; kind < DOWNLOAD_KINDS; kind++) {
            snprintf(message, sizeof(message), "%s, %zu-byte body", download_name(kind), strlen(bodies[b]));
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, errors[b][kind], message);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, successes[b][kind], message);
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(text[b][kind], "Could not write"), message);
            TEST_ASSERT_FALSE_MESSAGE(left[b][kind], message);
        }
    }
}

void test_offline_base_url_setters_strip_trailing_slashes(void) {
    char url[64];
    ZeroBounce *zb = client_with("slash-key");
    strcpy(url, "https://bulk.example.test/v2//");
    zero_bounce_set_bulk_api_base_url(zb, url);
    zero_bounce_set_bulk_api_scoring_base_url(zb, "https://bulk.example.test/v2/scoring/");
    zero_bounce_set_bulk_api_root_url(zb, "https://bulk.example.test/");
    zero_bounce_set_list_evaluator_base_url(zb, "https://bulk.example.test/le/");
    zero_bounce_set_inbox_placement_base_url(zb, "https://bulk.example.test/ipt/");
    /* The setter keeps its own copy, so the caller's buffer can change. */
    strcpy(url, "https://changed.example.test");

    reset_offline();
    file_status(zb, "fid", on_status, on_error);
    TEST_ASSERT_EQUAL_STRING("bulk.example.test", only_request().host);
    TEST_ASSERT_EQUAL_STRING("/v2/filestatus", only_request().path);
    reset_offline();
    scoring_file_status(zb, "fid", on_status, on_error);
    TEST_ASSERT_EQUAL_STRING("/v2/scoring/filestatus", only_request().path);
    reset_offline();
    email_finder_file_status(zb, "fid", on_status, on_error);
    TEST_ASSERT_EQUAL_STRING("/email-finder/filestatus", only_request().path);
    reset_offline();
    get_list_evaluator_result(zb, "fid", on_raw, on_error);
    TEST_ASSERT_EQUAL_STRING("/le/fid/", only_request().path);
    reset_offline();
    get_inbox_placement_providers(zb, on_raw, on_error);
    TEST_ASSERT_EQUAL_STRING("/ipt/providers", only_request().path);
}

/* The public request functions run_every_call has run, by name,
   so test_offline_every_request_function_is_in_the_sweep can compare them
   with the header. */
#define SWEPT_MAX 128
static char g_swept[SWEPT_MAX][96];
static int g_swept_count;

static void record_swept(const char *call) {
    size_t len = strcspn(call, "(");
    while (len > 0 && call[len - 1] == ' ') {
        len--;
    }
    for (int i = 0; i < g_swept_count; i++) {
        if (strlen(g_swept[i]) == len && strncmp(g_swept[i], call, len) == 0) {
            return;
        }
    }
    if (g_swept_count < SWEPT_MAX && len < sizeof(g_swept[0])) {
        memcpy(g_swept[g_swept_count], call, len);
        g_swept[g_swept_count][len] = '\0';
        g_swept_count++;
    }
}

/* What run_every_call checks after each call, given the call's text. */
typedef void (*SweepCheck)(const char *call);
static SweepCheck g_sweep_check;
static const char *g_sweep_expected;

#define SWEEP(call) do { \
        record_swept(#call); \
        reset_offline(); \
        call; \
        g_sweep_check(#call); \
    } while (0)

/* Runs every public request function once, then g_sweep_check. */
static void run_every_call(ZeroBounce *zb) {
    const char *path = "test/downloads/refused.csv";
    struct tm day = {0};
    EmailToValidateVector emails = email_to_validate_vector_init();
    ZBEmailToValidate email = {"a@example.com", ""};
    ZBValidateOptions post = new_validate_options();
    SendFileOptions send_options = new_send_file_options();
    GetFileOptions get_options = new_get_file_options();
    EmailFinderSendFileOptions finder = new_email_finder_send_file_options();
    DomainSearchSendFileOptions domain = new_domain_search_send_file_options();
    ValidateS3FileOptions s3 = new_validate_s3_file_options();
    ScoreS3FileOptions score = new_score_s3_file_options();
    ZBBulkFileJobOptions job = new_bulk_file_job_options();
    ZBCreateInboxPlacementTestOptions create = new_create_inbox_placement_test_options();
    ZBListInboxPlacementTestsOptions list = new_list_inbox_placement_tests_options();

    day.tm_year = 120;
    day.tm_mday = 1;
    email_to_validate_vector_append(&emails, email);
    post.use_post = 1;
    finder.first_name_column = 2;
    s3.file_name = score.file_name = job.file_name = "f.csv";
    s3.return_url = score.return_url = job.return_url = "https://example.com/done";
    remove(path);

    SWEEP(get_credits(zb, on_credits, on_error));
    SWEEP(get_api_usage(zb, day, day, on_usage, on_error));
    SWEEP(validate_email(zb, "a@example.com", "", on_validate, on_error));
    SWEEP(validate_email_with_options(zb, "a@example.com", "", &post, on_validate, on_error));
    SWEEP(validate_email_batch(zb, emails, on_batch, on_error));
    SWEEP(validate_email_batch_with_options(zb, emails, &post, on_batch, on_error));
    SWEEP(send_file(zb, "email_file.csv", 1, send_options, on_send, on_error));
    SWEEP(file_status(zb, "fid", on_status, on_error));
    SWEEP(get_file(zb, "fid", (char *)path, on_get, on_error));
    SWEEP(get_file_streaming(zb, "fid", (char *)path, NULL, on_get, on_error));
    SWEEP(scoring_get_file_streaming(zb, "fid", (char *)path, NULL, on_get, on_error));
    SWEEP(get_file_with_options(zb, "fid", (char *)path, &get_options, on_get, on_error));
    SWEEP(delete_file(zb, "fid", on_delete, on_error));
    SWEEP(scoring_send_file(zb, "email_file.csv", 1, send_options, on_send, on_error));
    SWEEP(scoring_file_status(zb, "fid", on_status, on_error));
    SWEEP(scoring_get_file(zb, "fid", (char *)path, on_get, on_error));
    SWEEP(scoring_get_file_with_options(zb, "fid", (char *)path, &get_options, on_get, on_error));
    SWEEP(scoring_delete_file(zb, "fid", on_delete, on_error));
    SWEEP(get_activity_data(zb, "a@example.com", on_activity, on_error));
    SWEEP(score_email(zb, "a@example.com", on_score, on_error));
    SWEEP(score_email_post(zb, "a@example.com", on_score, on_error));
    SWEEP(find_email_by_domain_first_middle_last_name(zb, "example.com", "A", "B", "C", on_find, on_error));
    SWEEP(find_email_by_domain_first_last_name(zb, "example.com", "A", "C", on_find, on_error));
    SWEEP(find_email_by_domain_first_name(zb, "example.com", "A", on_find, on_error));
    SWEEP(find_email_by_company_name_first_middle_last_name(zb, "Co", "A", "B", "C", on_find, on_error));
    SWEEP(find_email_by_company_name_first_last_name(zb, "Co", "A", "C", on_find, on_error));
    SWEEP(find_email_by_company_name_first_name(zb, "Co", "A", on_find, on_error));
    SWEEP(search_domain_by_domain(zb, "example.com", on_domain, on_error));
    SWEEP(search_domain_by_company_name(zb, "Co", on_domain, on_error));
    SWEEP(send_email_finder_file(zb, "email_file.csv", 1, &finder, on_send, on_error));
    SWEEP(email_finder_file_status(zb, "fid", on_status, on_error));
    SWEEP(email_finder_get_file(zb, "fid", (char *)path, on_get, on_error));
    SWEEP(email_finder_delete_file(zb, "fid", on_delete, on_error));
    SWEEP(send_domain_search_file(zb, "email_file.csv", 1, &domain, on_send, on_error));
    SWEEP(domain_search_file_status(zb, "fid", on_status, on_error));
    SWEEP(domain_search_get_file(zb, "fid", (char *)path, on_get, on_error));
    SWEEP(domain_search_delete_file(zb, "fid", on_delete, on_error));
    SWEEP(send_list_evaluator_file(zb, "email_file.csv", 1, on_send, on_error));
    SWEEP(get_list_evaluator_result(zb, "fid", on_raw, on_error));
    SWEEP(send_evaluated_file(zb, "fid", on_send, on_error));
    SWEEP(add_filter(zb, "allow", "domain", "example.com", on_raw, on_error));
    SWEEP(list_filters(zb, false, on_raw, on_error));
    SWEEP(list_filters(zb, true, on_raw, on_error));
    SWEEP(delete_filter(zb, "allow", "domain", "example.com", on_raw, on_error));
    SWEEP(validate_s3_file(zb, &s3, on_send, on_error));
    SWEEP(score_s3_file(zb, &score, on_send, on_error));
    SWEEP(export_s3_file(zb, "fid", NULL, on_send, on_error));
    SWEEP(list_s3_files(zb, NULL, NULL, on_raw, on_error));
    SWEEP(email_finder_s3_file(zb, &job, on_send, on_error));
    SWEEP(domain_search_s3_file(zb, &job, on_send, on_error));
    SWEEP(validate_sftp_file(zb, &job, on_send, on_error));
    SWEEP(score_sftp_file(zb, &job, on_send, on_error));
    SWEEP(email_finder_sftp_file(zb, &job, on_send, on_error));
    SWEEP(domain_search_sftp_file(zb, &job, on_send, on_error));
    SWEEP(export_sftp_file(zb, "fid", NULL, on_send, on_error));
    SWEEP(get_inbox_placement_providers(zb, on_raw, on_error));
    SWEEP(get_inbox_placement_test_types(zb, on_raw, on_error));
    SWEEP(create_inbox_placement_test(zb, "t", &create, on_raw, on_error));
    SWEEP(get_inbox_placement_test(zb, "t1", on_raw, on_error));
    SWEEP(list_inbox_placement_tests(zb, &list, on_raw, on_error));
    SWEEP(delete_inbox_placement_test(zb, "t1", on_raw, on_error));
    SWEEP(get_inbox_placement_webhook(zb, on_raw, on_error));
    SWEEP(set_inbox_placement_webhook(zb, "https://example.com/hook", on_raw, on_error));
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    SWEEP(find_email(zb, "example.com", "A", "B", "C", on_find, on_error));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    TEST_ASSERT_NULL(fopen(path, "rb"));
    email_to_validate_vector_free(&emails);
}

/* Each call must fail with g_sweep_expected and send nothing. */
static void check_refused(const char *call) {
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, call);
    TEST_ASSERT_FALSE_MESSAGE(g_success_called, call);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(g_sweep_expected, g_error_text, call);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_count, call);
}

static void expect_every_call_refused(ZeroBounce *zb, const char *expected) {
    g_sweep_expected = expected;
    g_sweep_check = check_refused;
    run_every_call(zb);
}

void test_offline_every_call_is_https_only(void) {
    ZeroBounce *zb = client_with("https-key");
    /* Every base URL, the API one included, set to plain http. */
    zb->api_base_url = "http://api.example.test/v2";
    zero_bounce_set_bulk_api_base_url(zb, "http://bulk.example.test/v2");
    zero_bounce_set_bulk_api_scoring_base_url(zb, "http://bulk.example.test/v2/scoring");
    zero_bounce_set_bulk_api_root_url(zb, "http://bulk.example.test");
    zero_bounce_set_list_evaluator_base_url(zb, "http://bulk.example.test/v2/listevaluator");
    zero_bounce_set_inbox_placement_base_url(zb, "http://bulk.example.test/api/ipt");
    expect_every_call_refused(zb, "url must be an https:// URL");
}

static int was_swept(const char *name) {
    for (int i = 0; i < g_swept_count; i++) {
        if (strcmp(g_swept[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Reads a public header and fails for any function taking an OnErrorCallback
   (a request) that is_swept does not know. Returns how many it declares. */
int offline_count_swept_request_functions(const char *header_path, int (*is_swept)(const char *name)) {
    FILE *f = fopen(header_path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, header_path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = calloc((size_t)size + 1, 1);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_EQUAL_size_t((size_t)size, fread(text, 1, (size_t)size, f));
    fclose(f);

    /* Blank out comments and preprocessor lines, which hold parentheses. */
    for (char *p = text; *p; p++) {
        if (p[0] == '/' && p[1] == '*') {
            char *close = strstr(p + 2, "*/");
            char *stop = close ? close + 2 : p + strlen(p);
            memset(p, ' ', (size_t)(stop - p));
            p = stop - 1;
        } else if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n') {
                *p++ = ' ';
            }
            if (!*p) break;
        } else if (p[0] == '#' && (p == text || p[-1] == '\n')) {
            while (*p && *p != '\n') {
                *p++ = ' ';
            }
            if (!*p) break;
        }
    }

    /* Each declaration runs from the end of the previous one to its ';'. */
    int declared = 0;
    char message[160];
    for (char *start = text, *end; (end = strchr(start, ';')) != NULL; start = end + 1) {
        *end = '\0';
        while (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t' || *start == '}') {
            start++;
        }
        char *paren = strchr(start, '(');
        if (paren == NULL || strstr(start, "OnErrorCallback") == NULL ||
            strncmp(start, "typedef", 7) == 0 || strncmp(start, "static", 6) == 0) {
            continue;
        }
        char *name_end = paren;
        while (name_end > start && name_end[-1] == ' ') {
            name_end--;
        }
        char *name = name_end;
        while (name > start && (isalnum((unsigned char)name[-1]) || name[-1] == '_')) {
            name--;
        }
        char saved = *name_end;
        *name_end = '\0';
        snprintf(message, sizeof message, "%s is missing from the sweep", name);
        TEST_ASSERT_TRUE_MESSAGE(is_swept(name), message);
        *name_end = saved;
        declared++;
    }
    free(text);
    return declared;
}

/* Fails for any request function in ZeroBounce.h that run_every_call does not
   run, so a new request function cannot escape the HTTPS, key and
   response-body sweeps. */
void test_offline_every_request_function_is_in_the_sweep(void) {
    ZeroBounce *zb = client_with("sweep-key");
    zero_bounce_set_bulk_api_base_url(zb, "http://bulk.example.test/v2");
    zb->api_base_url = "http://api.example.test/v2";
    zero_bounce_set_bulk_api_scoring_base_url(zb, "http://bulk.example.test/v2/scoring");
    zero_bounce_set_bulk_api_root_url(zb, "http://bulk.example.test");
    zero_bounce_set_list_evaluator_base_url(zb, "http://bulk.example.test/v2/listevaluator");
    zero_bounce_set_inbox_placement_base_url(zb, "http://bulk.example.test/api/ipt");
    g_swept_count = 0;
    expect_every_call_refused(zb, "url must be an https:// URL");

    int declared = offline_count_swept_request_functions(ZB_PUBLIC_HEADER, was_swept);
    /* The scan found the request functions, and nothing else was swept. */
    TEST_ASSERT_GREATER_THAN_INT(55, declared);
    TEST_ASSERT_EQUAL_INT(declared, g_swept_count);
}

void test_offline_a_rejected_key_replaces_the_previous_one(void) {
    const char *dirty[] = {" padded", "trailing\n", "in\x01side"};
    for (int i = 0; i < 3; i++) {
        ZeroBounce *zb = client_with("clean-key");
        /* Re-initialising with a rejected key does not quietly keep the old
           one: every call reports the rejection and nothing is sent. */
        zero_bounce_initialize(zb, dirty[i]);
        TEST_ASSERT_NULL(zb->api_key);
        expect_every_call_refused(zb, "api_key has surrounding whitespace or control characters");

        zb = client_with("clean-key");
        zero_bounce_initialize_with_base_url(zb, dirty[i], ZB_Api_URL_EU);
        reset_offline();
        get_credits(zb, on_credits, on_error);
        TEST_ASSERT_TRUE(g_error_called);
        TEST_ASSERT_EQUAL_INT(0, g_count);

        /* A clean key afterwards is accepted again. */
        zero_bounce_initialize(zb, "clean-again");
        reset_offline();
        get_credits(zb, on_credits, on_error);
        TEST_ASSERT_FALSE(g_error_called);
        TEST_ASSERT_TRUE(g_success_called);
    }
}

/* A request went out and came back as an error with `status`, never a
   success. */
static void check_error_with_status(const char *call, long status) {
    char message[200];
    snprintf(message, sizeof message, "%s (body %d)", call, g_leak_mode);
    TEST_ASSERT_TRUE_MESSAGE(g_count > 0, message);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, message);
    TEST_ASSERT_FALSE_MESSAGE(g_success_called, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)status, (int)g_error_status, message);
}

static void check_success_false(const char *call) {
    check_error_with_status(call, 200);
}

/* "success": false on an HTTP 200 is an error with that status on every
   call; the raw-JSON calls (filters, list evaluator, list_s3_files, Inbox
   Placement) used to hand it to the success callback. */
void test_offline_success_false_is_an_error_on_every_call(void) {
    ZeroBounce *zb = client_with("success-false-sweep");
    g_sweep_check = check_success_false;
    g_leak_mode = 2;
    if (TEST_PROTECT()) {
        run_every_call(zb);
    }
    g_leak_mode = 0;
}

static void check_no_key_in_error(const char *call) {
    char message[1400];
    check_error_with_status(call, g_leak_mode <= 2 ? 200 : 400);
    snprintf(message, sizeof message, "%s (body %d): %s", call, g_leak_mode, g_error_text);
    TEST_ASSERT_FALSE_MESSAGE(g_error_leaked, message);
    /* The message is kept, with the key masked, not dropped. */
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_error_text, "REDACTED"), message);
}

/* Every form a secret can take in an echoed body, for one with a space and
   every character JSON or URL encoding changes. Written out by hand, so each
   form zb_mask_secret knows is needed: dropping any one leaves its line
   unmasked. */
#define EVERY_FORM_SECRET "a b\"c/d+e%f&g=h<i>j'k\\l"
static const char *const every_form_of_the_secret[] = {
    /* raw */
    "a b\"c/d+e%f&g=h<i>j'k\\l",
    /* JSON-escaped, '/' escaped (json-c) and not */
    "a b\\\"c\\/d+e%f&g=h<i>j'k\\\\l",
    "a b\\\"c/d+e%f&g=h<i>j'k\\\\l",
    /* URL-encoded, space as %20 and as '+' */
    "a%20b%22c%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5Cl",
    "a+b%22c%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5Cl",
    /* URL-encoded after JSON escaping, each JSON form, %20 and '+' */
    "a%20b%5C%22c%5C%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5C%5Cl",
    "a+b%5C%22c%5C%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5C%5Cl",
    "a%20b%5C%22c%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5C%5Cl",
    "a+b%5C%22c%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5C%5Cl",
};

void test_offline_every_form_of_a_secret_is_masked(void) {
    char message[200];
    for (size_t i = 0; i < sizeof(every_form_of_the_secret) / sizeof(every_form_of_the_secret[0]); i++) {
        char *text = malloc(strlen(every_form_of_the_secret[i]) + 3);
        TEST_ASSERT_NOT_NULL(text);
        sprintf(text, "[%s]", every_form_of_the_secret[i]);
        text = zb_mask_secret(text, EVERY_FORM_SECRET, "***");
        snprintf(message, sizeof(message), "form %zu: %s", i, every_form_of_the_secret[i]);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("[***]", text, message);
        free(text);
    }
}

/*
 * Every spelling a secret can take in an echoed body, composed here from the
 * encoders a server or proxy applies, independently of zb_mask_secret:
 * {raw, JSON-escaped, JSON-escaped with '/' as '\\/'} x {as is, URL-encoded
 * with a space as %20, URL-encoded with '+'}, and JSON-escaped twice (with
 * and without '\\/'). Each form must be masked whole, and since every form
 * differs, dropping any one generator from zb_mask_secret leaves its form
 * readable.
 */
static char *t_json_escape(const char *in, int escape_slash) {
    char *out = malloc(strlen(in) * 2 + 1);
    char *dst = out;
    for (; *in; in++) {
        if (*in == '"' || *in == '\\' || (escape_slash && *in == '/')) {
            *dst++ = '\\';
        }
        *dst++ = *in;
    }
    *dst = '\0';
    return out;
}

static char *t_url_encode(const char *in, int plus_for_space) {
    static const char hex[] = "0123456789ABCDEF";
    char *out = malloc(strlen(in) * 3 + 1);
    char *dst = out;
    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~') {
            *dst++ = (char)c;
        } else if (c == ' ' && plus_for_space) {
            *dst++ = '+';
        } else {
            *dst++ = '%';
            *dst++ = hex[c >> 4];
            *dst++ = hex[c & 15];
        }
    }
    *dst = '\0';
    return out;
}

void test_offline_every_composed_spelling_of_a_secret_is_masked(void) {
    char *bases[3];
    char *forms[11];
    size_t count = 0;
    size_t b;
    size_t i;
    size_t j;
    char message[300];
    bases[0] = strdup(EVERY_FORM_SECRET);
    bases[1] = t_json_escape(EVERY_FORM_SECRET, 0);
    bases[2] = t_json_escape(EVERY_FORM_SECRET, 1);
    for (b = 0; b < 3; b++) {
        forms[count++] = strdup(bases[b]);
        forms[count++] = t_url_encode(bases[b], 0);
        forms[count++] = t_url_encode(bases[b], 1);
    }
    forms[count++] = t_json_escape(bases[1], 0);
    forms[count++] = t_json_escape(bases[2], 1);
    TEST_ASSERT_EQUAL_INT(11, (int)count);
    for (i = 0; i < count; i++) {
        char *text;
        /* Distinct forms, so each generator is needed. */
        for (j = 0; j < i; j++) {
            TEST_ASSERT_FALSE_MESSAGE(strcmp(forms[i], forms[j]) == 0, forms[i]);
        }
        text = malloc(strlen(forms[i]) + 32);
        TEST_ASSERT_NOT_NULL(text);
        sprintf(text, "echo [%s] end", forms[i]);
        text = zb_mask_secret(text, EVERY_FORM_SECRET, "***");
        snprintf(message, sizeof(message), "form %zu: %s -> %s", i, forms[i], text);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("echo [***] end", text, message);
        free(text);
    }
    for (i = 0; i < count; i++) {
        free(forms[i]);
    }
    for (b = 0; b < 3; b++) {
        free(bases[b]);
    }
}

/* Standard section 3: the key is never in an error. Every call gets each
   echoing body; no error text may carry the key, raw, URL-encoded or
   JSON-escaped. */
void test_offline_no_error_carries_the_key(void) {
    ZeroBounce *zb = client_with(LEAK_KEY);
    g_sweep_check = check_no_key_in_error;
    if (TEST_PROTECT()) {
        for (g_leak_mode = 1; g_leak_mode <= 6; g_leak_mode++) {
            run_every_call(zb);
        }
    }
    g_leak_mode = 0;
}

/* A 200 labelled text/html whose body parses as JSON ({"a":1}) is a portal's
   or a proxy's page, not the API's answer: an error with the status on every
   call, and no file. A file-looking body labelled application/json is an
   error too: a JSON call cannot parse it, and a download takes the label as
   the API's error. Either guard's content-type half alone is what catches
   these bodies. */
void test_offline_the_content_type_alone_makes_a_200_an_error(void) {
    ZeroBounce *zb = client_with("content-type-sweep");
    g_sweep_check = check_success_false;
    if (TEST_PROTECT()) {
        for (g_leak_mode = 7; g_leak_mode <= 9; g_leak_mode++) {
            run_every_call(zb);
        }
    }
    g_leak_mode = 0;
}

static long g_empty_status;

static void check_empty_body(const char *call) {
    char message[200];
    snprintf(message, sizeof message, "%s (HTTP %ld)", call, g_empty_status);
    TEST_ASSERT_TRUE_MESSAGE(g_count > 0, message);
    if (g_empty_status < 300 && strncmp(call, "delete_inbox_placement_test(", 28) == 0) {
        /* DELETE answers with an empty body: a success with an empty result. */
        TEST_ASSERT_FALSE_MESSAGE(g_error_called, message);
        TEST_ASSERT_TRUE_MESSAGE(g_success_called, message);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("{}", g_raw, message);
    } else {
        TEST_ASSERT_TRUE_MESSAGE(g_error_called, message);
        TEST_ASSERT_FALSE_MESSAGE(g_success_called, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)g_empty_status, (int)g_error_status, message);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_error_text, "Empty response"), message);
    }
}

/* An empty 2xx is a success only for delete_inbox_placement_test. Every other
   call, the other Inbox Placement calls included, reports it as an error with
   its status, and an empty non-2xx is an error everywhere. */
void test_offline_an_empty_body_is_a_success_only_for_an_inbox_placement_delete(void) {
    g_sweep_check = check_empty_body;
    g_empty_status = 200;
    run_every_call(client_with("empty-sweep"));
    g_empty_status = 404;
    run_every_call(client_with("empty-404-sweep"));
}

/* ---- E7: every JSON call against the strict-JSON corpus ---------------- */

/*
 * test/fixtures/strict-json-corpus.tsv is a byte-identical copy of
 * sdk-docs/skills/zerobounce-sdk/references/strict-json-corpus.tsv in
 * zerobounce/sdk-docs (standard section 3, check E7); update it from there.
 * Each line is REJECT or ACCEPT, a tab, and the body as a JSON-encoded string.
 */
#define CORPUS_MAX 128
typedef struct {
    int accept;
    char *body;
    size_t len;
    char line[256];
} CorpusEntry;

static CorpusEntry g_corpus[CORPUS_MAX];
static int g_corpus_count = -1;

int offline_corpus_count(void) {
    FILE *file;
    char line[1024];
    if (g_corpus_count >= 0) {
        return g_corpus_count;
    }
    g_corpus_count = 0;
    file = fopen(ZB_STRICT_JSON_CORPUS, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(file, ZB_STRICT_JSON_CORPUS);
    while (fgets(line, sizeof(line), file)) {
        char *tab;
        json_object *decoded;
        CorpusEntry *entry;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        tab = strchr(line, '\t');
        TEST_ASSERT_NOT_NULL_MESSAGE(tab, line);
        TEST_ASSERT_TRUE_MESSAGE(g_corpus_count < CORPUS_MAX, line);
        entry = &g_corpus[g_corpus_count];
        *tab = '\0';
        TEST_ASSERT_TRUE_MESSAGE(strcmp(line, "ACCEPT") == 0 || strcmp(line, "REJECT") == 0, line);
        entry->accept = strcmp(line, "ACCEPT") == 0;
        decoded = json_tokener_parse(tab + 1);
        TEST_ASSERT_TRUE_MESSAGE(decoded && json_object_is_type(decoded, json_type_string), tab + 1);
        entry->len = (size_t)json_object_get_string_len(decoded);
        entry->body = malloc(entry->len + 1);
        memcpy(entry->body, json_object_get_string(decoded), entry->len + 1);
        snprintf(entry->line, sizeof(entry->line), "%s %s", line, tab + 1);
        json_object_put(decoded);
        g_corpus_count++;
    }
    fclose(file);
    /* 42 REJECT and 9 ACCEPT lines when this was written. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(51, g_corpus_count);
    return g_corpus_count;
}

const char *offline_corpus_body(int index, size_t *len, int *accept, const char **line) {
    *len = g_corpus[index].len;
    *accept = g_corpus[index].accept;
    *line = g_corpus[index].line;
    return g_corpus[index].body;
}

static const CorpusEntry *g_corpus_entry;

static int is_download_call(const char *call) {
    return strstr(call, "get_file") != NULL;
}

/* Whether body is a JSON object (an ACCEPT body that starts with '{' after
   whitespace): the only shape a typed call reads. */
static int corpus_body_is_object(const CorpusEntry *entry) {
    size_t i = 0;
    while (i < entry->len && strchr(" \t\r\n", entry->body[i]) && entry->body[i]) {
        i++;
    }
    return i < entry->len && entry->body[i] == '{';
}

/* A typed call hands the caller a parsed struct; a raw-JSON call (on_raw)
   hands over the body as it is. */
static int is_typed_call(const char *call) {
    return strstr(call, "on_raw") == NULL && !is_download_call(call);
}

/* E7: the typed calls that cannot answer without named fields: getcredits
   (Credits) and validate (address and status). */
static int needs_named_fields(const char *call) {
    return strncmp(call, "get_credits(", 12) == 0 || strncmp(call, "validate_email(", 15) == 0 ||
        strncmp(call, "validate_email_with_options(", 28) == 0;
}

static void check_corpus_body(const char *call) {
    char message[1600];
    snprintf(message, sizeof message, "%s with %s: %s", call, g_corpus_entry->line, g_error_text);
    TEST_ASSERT_TRUE_MESSAGE(g_count > 0, message);
    if ((!g_corpus_entry->accept &&
         !(g_corpus_entry->len == 0 && strncmp(call, "delete_inbox_placement_test(", 28) == 0)) ||
        (g_corpus_entry->accept && is_typed_call(call) && !corpus_body_is_object(g_corpus_entry)) ||
        (g_corpus_entry->accept && needs_named_fields(call))) {
        /* A REJECT body, or (E7) valid JSON of the wrong shape for a typed
           call (0, "x", [], or an object without the fields getcredits and
           validate need): an API error with status 200, never a result
           filled with defaults. */
        TEST_ASSERT_TRUE_MESSAGE(g_error_called, message);
        TEST_ASSERT_FALSE_MESSAGE(g_success_called, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)g_error_status, message);
    } else if (is_download_call(call)) {
        /* A download takes any application/json answer as the API's error. */
        TEST_ASSERT_TRUE_MESSAGE(g_error_called, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)g_error_status, message);
    } else {
        /* The one exception among REJECT: delete_inbox_placement_test's empty
           2xx is its documented empty success (E8). */
        TEST_ASSERT_FALSE_MESSAGE(g_error_called, message);
        TEST_ASSERT_TRUE_MESSAGE(g_success_called, message);
    }
}

/* Every call against every corpus body on an HTTP 200 application/json: a
   REJECT body is an error with status 200, an ACCEPT body is the result. */
void test_offline_every_json_call_takes_only_strict_json(void) {
    int count = offline_corpus_count();
    ZeroBounce *zb = client_with("corpus-sweep");
    g_sweep_check = check_corpus_body;
    if (TEST_PROTECT()) {
        for (int i = 0; i < count; i++) {
            g_corpus_entry = &g_corpus[i];
            g_mode_body = g_corpus[i].body;
            g_mode_body_len = g_corpus[i].len;
            g_leak_mode = 10;
            run_every_call(zb);
        }
    }
    g_leak_mode = 0;
}

/* ---- S4: redaction is closed under encoding ---------------------------- */

/* One encoding a server or proxy may apply: URL-encoding with a space as %20
   (0) or '+' (1), JSON escaping (2), JSON escaping with '/' as '\/' (3). */
char *offline_encode(const char *in, int how) {
    return how < 2 ? t_url_encode(in, how == 1) : t_json_escape(in, how == 3);
}

static int t_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* One decoding: %XX (0), %XX and '+' (1), JSON unescaping (2). */
static char *t_decode(const char *in, int how) {
    char *out = malloc(strlen(in) * 3 + 1);
    char *dst = out;
    while (*in) {
        if (how < 2 && in[0] == '%' && t_hex(in[1]) >= 0 && t_hex(in[2]) >= 0) {
            *dst++ = (char)(t_hex(in[1]) * 16 + t_hex(in[2]));
            in += 3;
        } else if (how == 1 && *in == '+') {
            *dst++ = ' ';
            in++;
        } else if (how == 2 && in[0] == '\\' && in[1] && strchr("\"\\/bfnrt", in[1])) {
            const char *from = "\"\\/bfnrt";
            const char *to = "\"\\/\b\f\n\r\t";
            *dst++ = to[strchr(from, in[1]) - from];
            in += 2;
        } else if (how == 2 && in[0] == '\\' && in[1] == 'u' && t_hex(in[2]) >= 0 && t_hex(in[3]) >= 0 &&
                   t_hex(in[4]) >= 0 && t_hex(in[5]) >= 0) {
            unsigned cp = (unsigned)((t_hex(in[2]) << 12) | (t_hex(in[3]) << 8) | (t_hex(in[4]) << 4) | t_hex(in[5]));
            if (cp < 0x80) {
                *dst++ = (char)cp;
            } else if (cp < 0x800) {
                *dst++ = (char)(0xC0 | (cp >> 6));
                *dst++ = (char)(0x80 | (cp & 0x3F));
            } else {
                *dst++ = (char)(0xE0 | (cp >> 12));
                *dst++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                *dst++ = (char)(0x80 | (cp & 0x3F));
            }
            in += 6;
        } else {
            *dst++ = *in++;
        }
    }
    *dst = '\0';
    return out;
}

/* Whether secret is in text or in any view of it decoded up to layers times,
   in any order. Written apart from the SDK's own check. */
int offline_secret_readable(const char *text, const char *secret, int layers) {
    int found = strstr(text, secret) != NULL;
    for (int how = 0; how < 3 && !found && layers > 0; how++) {
        char *view = t_decode(text, how);
        found = offline_secret_readable(view, secret, layers - 1);
        free(view);
    }
    return found;
}

/* A random composition of up to three encodings of secret; the caller frees
   it. desc gets the sequence, e.g. "2,0". */
char *offline_random_spelling(const char *secret, unsigned *seed, char *desc, size_t desc_len) {
    char *text = strdup(secret);
    int layers = (int)(rand_r(seed) % 4);
    desc[0] = '\0';
    for (int i = 0; i < layers; i++) {
        int how = (int)(rand_r(seed) % 4);
        char *next = offline_encode(text, how);
        size_t used = strlen(desc);
        snprintf(desc + used, desc_len - used, "%s%d", i ? "," : "", how);
        free(text);
        text = next;
    }
    return text;
}

/* Has every character JSON or URL encoding changes, and a space. */
#define FUZZ_SECRET "fz a\"b/c\\d+e%f&g=h<i>j'k"

static char g_fuzz_desc[600];

static void check_fuzzed_echo(const char *call) {
    char message[1800];
    snprintf(message, sizeof message, "%s, encodings [%s], status %ld: %s",
             call, g_fuzz_desc, g_mode_status, g_error_text);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, message);
    TEST_ASSERT_FALSE_MESSAGE(offline_secret_readable(g_error_text, FUZZ_SECRET, 3), message);
}

/* Standard section 3: whatever composition of up to three URL and JSON
   encodings a body uses to quote the key, no decoded view of any error a call
   gets holds it. Seeded, so a failure repeats. */
void test_offline_redaction_is_closed_under_encoding(void) {
    unsigned seed = 20261112u;
    ZeroBounce *zb = client_with(FUZZ_SECRET);
    g_sweep_check = check_fuzzed_echo;
    if (TEST_PROTECT()) {
        for (int round = 0; round < 60; round++) {
            char *spelling = offline_random_spelling(FUZZ_SECRET, &seed, g_fuzz_desc, 200);
            char *quoted = t_json_escape(spelling, 0);
            char *body = malloc(strlen(quoted) + 64);
            if (round % 2) {
                /* A dead-key answer on a 200 quoting it in a JSON string. */
                sprintf(body, "{\"error\":\"Invalid API key %s\"}", quoted);
                g_mode_status = 200;
            } else {
                sprintf(body, "Bad Request: [%s] end", spelling);
                g_mode_status = 400;
            }
            g_mode_body = body;
            g_mode_body_len = strlen(body);
            g_leak_mode = 11;
            run_every_call(zb);
            free(body);
            free(quoted);
            free(spelling);
        }
    }
    g_leak_mode = 0;
}

/* Spellings no list of generated forms covers (three URL layers, URL of
   double JSON) are dropped whole, with a message that says why. */
void test_offline_a_body_still_holding_the_key_after_masking_is_dropped(void) {
    char *once = t_url_encode(FUZZ_SECRET, 0);
    char *twice = t_url_encode(once, 0);
    char *thrice = t_url_encode(twice, 1);
    char *text = malloc(strlen(thrice) + 32);
    sprintf(text, "echo [%s] end", thrice);
    text = zb_mask_secret(text, FUZZ_SECRET, "***");
    TEST_ASSERT_EQUAL_STRING(ZB_SECRET_OMITTED_MESSAGE, text);
    free(text);
    text = strdup("nothing secret here %41");
    text = zb_mask_secret(text, FUZZ_SECRET, "***");
    TEST_ASSERT_EQUAL_STRING("nothing secret here %41", text);
    free(text);
    free(once);
    free(twice);
    free(thrice);
}

/* Every byte of in as %XX, lower- or upper-case hex: a spelling the SDK never
   generates, so only the decode check can see the secret in it. */
static char *t_percent_every_byte(const char *in, int lower) {
    const char *hex = lower ? "0123456789abcdef" : "0123456789ABCDEF";
    char *out = malloc(strlen(in) * 3 + 1);
    char *dst = out;
    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        *dst++ = '%';
        *dst++ = hex[c >> 4];
        *dst++ = hex[c & 15];
    }
    *dst = '\0';
    return out;
}

/* Every byte of an ASCII string as a JSON \u00XX escape. */
static char *t_unicode_every_byte(const char *in) {
    char *out = malloc(strlen(in) * 6 + 1);
    char *dst = out;
    for (; *in; in++) {
        dst += sprintf(dst, "\\u%04x", (unsigned char)*in);
    }
    return out;
}

/* zb_mask_secret on "<before><spelling>]" must drop the whole text. */
static void t_expect_omitted(const char *secret, const char *before, const char *spelling, const char *what) {
    char *text = malloc(strlen(before) + strlen(spelling) + 2);
    sprintf(text, "%s%s]", before, spelling);
    text = zb_mask_secret(text, secret, "***");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(ZB_SECRET_OMITTED_MESSAGE, text, what);
    free(text);
}

/* Each decode step of the redaction check is needed: every spelling below is
   one the SDK does not generate, and one decode step (or layer) alone makes
   the secret readable again. Removing that step lets the text through. */
void test_offline_each_decode_step_of_the_redaction_check_is_needed(void) {
    char *upper = t_percent_every_byte(FUZZ_SECRET, 0);
    char *lower = t_percent_every_byte(FUZZ_SECRET, 1);
    char *unicode = t_unicode_every_byte(FUZZ_SECRET);
    char *plus_once = t_url_encode(FUZZ_SECRET, 1);
    char *plus_twice = t_url_encode(plus_once, 0);
    char *url1 = t_url_encode(FUZZ_SECRET, 0);
    char *url2 = t_url_encode(url1, 0);
    char *url3 = t_url_encode(url2, 0);
    char *url4 = t_url_encode(url3, 0);
    char *text;
    /* %XX, upper- and lower-case hex. */
    t_expect_omitted(FUZZ_SECRET, "echo [", upper, "percent, upper-case hex");
    t_expect_omitted(FUZZ_SECRET, "echo [", lower, "percent, lower-case hex");
    /* An invalid UTF-8 run decoded first does not stop the check. */
    t_expect_omitted(FUZZ_SECRET, "%ff%fe%c3%28 [", lower, "after invalid UTF-8");
    /* '+' as a space, under a second URL layer. */
    t_expect_omitted(FUZZ_SECRET, "echo [", plus_twice, "plus as space");
    /* JSON \uXXXX, and a surrogate pair for a character outside the BMP. */
    t_expect_omitted(FUZZ_SECRET, "echo [", unicode, "\\u escapes");
    t_expect_omitted("k\xF0\x9F\x94\x91y", "echo [", "k\\ud83d\\udd11y", "surrogate pair");
    /* A \uXXXX escape for each UTF-8 sequence length the decoder writes:
       1 byte (above), 2 bytes (U+00E9), 3 bytes (U+20AC) and 4 bytes (the
       surrogate pair above, and an upper-case one here). */
    t_expect_omitted("k\xC3\xA9y", "echo [", "k\\u00e9y", "\\u escape, 2-byte UTF-8");
    t_expect_omitted("k\xE2\x82\xACy", "echo [", "k\\u20ACy", "\\u escape, 3-byte UTF-8");
    t_expect_omitted("k\xF0\x9F\x94\x91y", "echo [", "k\\uD83D\\uDD11y", "\\u escape, 4-byte UTF-8");
    /* Four layers: the SDK's own request encoding plus three echo layers. */
    t_expect_omitted(FUZZ_SECRET, "echo [", url4, "four URL layers");
    /* A decoded NUL (%00, \u0000) does not end the search. */
    t_expect_omitted(FUZZ_SECRET, "bad %00 then [", upper, "after %00");
    t_expect_omitted(FUZZ_SECRET, "bad \\u0000 then [", unicode, "after \\u0000");
    /* The control: a NUL and escapes with no secret behind them stay. */
    text = strdup("nothing secret %00 here \\u0000 %41");
    text = zb_mask_secret(text, FUZZ_SECRET, "***");
    TEST_ASSERT_EQUAL_STRING("nothing secret %00 here \\u0000 %41", text);
    free(text);
    free(upper);
    free(lower);
    free(unicode);
    free(plus_once);
    free(plus_twice);
    free(url1);
    free(url2);
    free(url3);
    free(url4);
}

static void check_nul_echo(const char *call) {
    char message[1800];
    snprintf(message, sizeof message, "%s, status %ld: %s", call, g_mode_status, g_error_text);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)g_mode_status, (int)g_error_status, message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_error_text, "omitted because it contained a credential"), message);
}

/* S4: a body that quotes the key fully percent-encoded after a %00 is dropped
   on every call, at 400 and on a 200 dead-key answer. */
void test_offline_a_nul_in_the_body_does_not_end_the_redaction_check(void) {
    ZeroBounce *zb = client_with(FUZZ_SECRET);
    char *encoded = t_percent_every_byte(FUZZ_SECRET, 0);
    char *body = malloc(strlen(encoded) + 64);
    g_sweep_check = check_nul_echo;
    if (TEST_PROTECT()) {
        sprintf(body, "bad %%00 then %s", encoded);
        g_mode_status = 400;
        g_mode_body = body;
        g_mode_body_len = strlen(body);
        g_leak_mode = 11;
        run_every_call(zb);
        sprintf(body, "{\"error\":\"bad %%00 then %s\"}", encoded);
        g_mode_status = 200;
        g_mode_body_len = strlen(body);
        run_every_call(zb);
    }
    g_leak_mode = 0;
    free(body);
    free(encoded);
}

/* E7: JSON of a shape no typed call reads (true, null, a number, a string, an
   array) is an API error with status 200 on every typed call; a raw-JSON call
   hands it over as it is. */
void test_offline_typed_calls_reject_json_of_the_wrong_shape(void) {
    /* An array where an object is expected, positional ones included (a
       decoder may read an array into a struct by position). */
    static const char *const bodies[] = {"0", "1.5", "true", "false", "null", "\"x\"", "[]", "[{}]", " [ ] ",
                                         "[\"x\"]", "[\"a@example.com\",10]", "[true,\"3\"]",
                                         "[\"7\"]", "[\"a@b.c\",\"valid\"]"};
    ZeroBounce *zb = client_with("shape-sweep");
    CorpusEntry entry;
    g_sweep_check = check_corpus_body;
    if (TEST_PROTECT()) {
        for (size_t i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
            memset(&entry, 0, sizeof(entry));
            entry.accept = 1;
            entry.body = (char *)bodies[i];
            entry.len = strlen(bodies[i]);
            snprintf(entry.line, sizeof(entry.line), "ACCEPT %s", bodies[i]);
            g_corpus_entry = &entry;
            g_mode_body = entry.body;
            g_mode_body_len = entry.len;
            g_leak_mode = 10;
            run_every_call(zb);
        }
    }
    g_leak_mode = 0;
}

/* E7: a body nested deeper than the strict-JSON scanner's limit (200000 '[')
   is an error with status 200 on every call, not a crash. */
void test_offline_a_deeply_nested_body_is_an_error(void) {
    ZeroBounce *zb = client_with("depth-sweep");
    CorpusEntry entry;
    size_t depth = 200000;
    char *body = malloc(depth + 1);
    memset(body, '[', depth);
    body[depth] = '\0';
    memset(&entry, 0, sizeof(entry));
    entry.accept = 0;
    entry.body = body;
    entry.len = depth;
    snprintf(entry.line, sizeof(entry.line), "REJECT 200000 x [");
    g_sweep_check = check_corpus_body;
    if (TEST_PROTECT()) {
        g_corpus_entry = &entry;
        g_mode_body = body;
        g_mode_body_len = depth;
        g_leak_mode = 10;
        run_every_call(zb);
    }
    g_leak_mode = 0;
    free(body);
}


/* ---- E7: field types are part of the shape --------------------------- */

static void expect_shape_error(const char *what, const char *body) {
    char message[512];
    snprintf(message, sizeof message, "%s with %s: %s", what, body, g_error_text);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, message);
    TEST_ASSERT_FALSE_MESSAGE(g_success_called, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)g_error_status, message);
}

static void serve(const char *body) {
    reset_offline();
    g_mode_body = body;
    g_mode_body_len = strlen(body);
    g_leak_mode = 10;
}

/* return_url is sent only when the caller sets one: the default (empty) and an
   unset one are left out of both sendfile calls. */
void test_offline_return_url_is_sent_only_when_set(void) {
    reset_offline();
    ZeroBounce *zb = client_with("return-key");
    send_file(zb, "email_file.csv", 1, new_send_file_options(), on_send, on_error);
    TEST_ASSERT_FALSE(has_field("return_url", NULL));
    reset_offline();
    scoring_send_file(zb, "email_file.csv", 1, new_send_file_options(), on_send, on_error);
    TEST_ASSERT_FALSE(has_field("return_url", NULL));
    reset_offline();
    SendFileOptions options = new_send_file_options();
    options.return_url = "https://example.com/hook";
    send_file(zb, "email_file.csv", 1, options, on_send, on_error);
    TEST_ASSERT_TRUE(has_field("return_url", "https://example.com/hook"));
}

/* standard section 3: "success" as the string "false", in any letter case, is a
   failure with status 200 on every JSON path. No message is sent, so nothing
   else in the body reports the failure. */
void test_offline_success_string_false_is_an_error(void) {
    static const char *const bodies[] = {
        "{\"success\":\"false\"}", "{\"success\":\"False\"}", "{\"success\":\"FALSE\"}",
    };
    ZeroBounce *zb = client_with("success-string");
    if (TEST_PROTECT()) {
        for (size_t i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
            serve(bodies[i]);
            file_status(zb, "file-1", on_status, on_error);
            expect_shape_error("file_status", bodies[i]);
            serve(bodies[i]);
            scoring_file_status(zb, "file-2", on_status, on_error);
            expect_shape_error("scoring_file_status", bodies[i]);
            serve(bodies[i]);
            delete_file(zb, "file-3", on_delete, on_error);
            expect_shape_error("delete_file", bodies[i]);
            serve(bodies[i]);
            send_file(zb, "email_file.csv", 1, new_send_file_options(), on_send, on_error);
            expect_shape_error("send_file", bodies[i]);
            /* The raw-JSON calls hand the body over unparsed: nothing else checks it. */
            serve(bodies[i]);
            add_filter(zb, "exclude", "domain", "example.com", on_raw, on_error);
            expect_shape_error("add_filter", bodies[i]);
            serve(bodies[i]);
            list_filters(zb, false, on_raw, on_error);
            expect_shape_error("list_filters", bodies[i]);
            serve(bodies[i]);
            delete_filter(zb, "exclude", "domain", "example.com", on_raw, on_error);
            expect_shape_error("delete_filter", bodies[i]);
        }
    }
}

/* checks.md E7: getcredits and validate with a missing, null or wrong-typed
   field fail with status 200; nothing is coerced or defaulted. */
void test_offline_typed_calls_reject_fields_of_the_wrong_type(void) {
    static const char *const credits[] = {
        "{}", "{\"Credits\":null}", "{\"Credits\":true}", "{\"Credits\":5.5}",
        "{\"Credits\":\"abc\"}", "{\"Credits\":\"5.0\"}", "{\"Credits\":\"\"}", "{\"Credits\":\" 5\"}",
        "{\"Credits\":\"99999999999999999999\"}", "{\"Credits\":[1]}", "{\"Credits\":{}}",
        /* A string Credits is an int too: one past either end of int is an error. */
        "{\"Credits\":\"2147483648\"}", "{\"Credits\":\"-2147483649\"}",
        /* A JSON integer is an answer only when it is a whole number in the
           range of int: no fraction or exponent, nothing past INT_MAX. */
        "{\"Credits\":5.0}", "{\"Credits\":1e3}", "{\"Credits\":2147483648}", "{\"Credits\":-2147483649}",
        "{\"Credits\":9223372036854775808}", "{\"Credits\":99999999999999999999}",
    };
    static const char *const validate[] = {
        "{}", "{\"address\":true,\"status\":\"valid\"}", "{\"address\":\"a@b.c\",\"status\":5}",
        "{\"status\":\"valid\"}", "{\"address\":\"a@b.c\"}", "{\"address\":null,\"status\":\"valid\"}",
        "{\"address\":\"a@b.c\",\"status\":null}",
        "{\"address\":\"a@b.c\",\"status\":\"valid\",\"free_email\":\"maybe\"}",
        "{\"address\":\"a@b.c\",\"status\":\"valid\",\"domain_age_days\":9692}",
        "{\"address\":\"a@b.c\",\"status\":\"valid\",\"Message\":7}",
    };
    ZeroBounce *zb = client_with("field-types");
    EmailToValidateVector emails = email_to_validate_vector_init();
    ZBEmailToValidate email = {"a@b.c", ""};
    ZBValidateOptions post = new_validate_options();
    email_to_validate_vector_append(&emails, email);
    post.use_post = 1;
    if (TEST_PROTECT()) {
        for (size_t i = 0; i < sizeof(credits) / sizeof(credits[0]); i++) {
            serve(credits[i]);
            get_credits(zb, on_credits, on_error);
            expect_shape_error("get_credits", credits[i]);
        }
        for (size_t i = 0; i < sizeof(validate) / sizeof(validate[0]); i++) {
            serve(validate[i]);
            validate_email(zb, "a@b.c", "", on_validate, on_error);
            expect_shape_error("validate_email", validate[i]);
            serve(validate[i]);
            validate_email_with_options(zb, "a@b.c", "", &post, on_validate, on_error);
            expect_shape_error("validate_email_with_options", validate[i]);
        }
        {
            /* A score sent as a string must hold an integer: atoi read "abc"
               as a score of 0. */
            static const char *const scores[] = {
                "{\"email\":\"a@b.c\",\"score\":\"abc\"}", "{\"email\":\"a@b.c\",\"score\":\"5.5\"}",
                "{\"email\":\"a@b.c\",\"score\":\"\"}", "{\"email\":\"a@b.c\",\"score\":\"9 \"}",
                "{\"email\":\"a@b.c\",\"score\":5.5}",
            };
            for (size_t i = 0; i < sizeof(scores) / sizeof(scores[0]); i++) {
                serve(scores[i]);
                score_email(zb, "a@b.c", on_score, on_error);
                expect_shape_error("score_email", scores[i]);
            }
            serve("{\"email\":\"a@b.c\",\"score\":\"10\"}");
            score_email(zb, "a@b.c", on_score, on_error);
            TEST_ASSERT_TRUE_MESSAGE(g_success_called, g_error_text);
        }
        serve("{\"email_batch\":[{\"address\":\"a@b.c\",\"status\":5}],\"errors\":[]}");
        validate_email_batch(zb, emails, on_batch, on_error);
        expect_shape_error("validate_email_batch", "a row with status 5");
        {
            /* Each batch row needs its address and status too. */
            static const char *const rows[] = {
                "{\"email_batch\":[{}],\"errors\":[]}",
                "{\"email_batch\":[{\"status\":\"valid\"}],\"errors\":[]}",
                "{\"email_batch\":[{\"address\":\"a@b.c\"}],\"errors\":[]}",
                "{\"email_batch\":[{\"address\":null,\"status\":\"valid\"}],\"errors\":[]}",
                "{\"email_batch\":[{\"address\":\"a@b.c\",\"status\":null}],\"errors\":[]}",
            };
            for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
                serve(rows[i]);
                validate_email_batch(zb, emails, on_batch, on_error);
                expect_shape_error("validate_email_batch", rows[i]);
            }
        }
    }
    g_leak_mode = 0;
    email_to_validate_vector_free(&emails);
}

/* Every field name a response model reads, scanned from the model sources
   (src/ZB*.c), so a field added later is in the sweeps below. */
#define SHAPE_FIELDS_MAX 160
static char g_shape_fields[SHAPE_FIELDS_MAX][64];
static int g_shape_field_count;

static void add_shape_field(const char *name, size_t len) {
    if (len == 0 || len >= sizeof(g_shape_fields[0])) return;
    for (int i = 0; i < g_shape_field_count; i++) {
        if (strlen(g_shape_fields[i]) == len && strncmp(g_shape_fields[i], name, len) == 0) return;
    }
    TEST_ASSERT_TRUE(g_shape_field_count < SHAPE_FIELDS_MAX);
    memcpy(g_shape_fields[g_shape_field_count], name, len);
    g_shape_fields[g_shape_field_count][len] = '\0';
    g_shape_field_count++;
}

static int scan_shape_fields(void) {
    static const char *const readers[] = {
        "get_json_value(", "get_json_string_copy(", "zb_json_require_string(",
        "zb_json_array_or_null(", "json_object_object_get_ex(",
    };
    DIR *dir = opendir(ZB_SOURCE_DIR);
    struct dirent *entry;
    g_shape_field_count = 0;
    TEST_ASSERT_NOT_NULL(dir);
    while ((entry = readdir(dir)) != NULL) {
        char path[1024];
        FILE *f;
        long size;
        char *text;
        /* The response models; the error-body checks read error fields
           whatever the call. */
        if (strncmp(entry->d_name, "ZB", 2) != 0 || strstr(entry->d_name, "Error") ||
            strcmp(entry->d_name + strlen(entry->d_name) - 2, ".c") != 0) {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s", ZB_SOURCE_DIR, entry->d_name);
        f = fopen(path, "rb");
        TEST_ASSERT_NOT_NULL(f);
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fseek(f, 0, SEEK_SET);
        text = calloc((size_t)size + 1, 1);
        TEST_ASSERT_EQUAL_size_t((size_t)size, fread(text, 1, (size_t)size, f));
        fclose(f);
        for (size_t r = 0; r < sizeof(readers) / sizeof(readers[0]); r++) {
            for (char *p = strstr(text, readers[r]); p; p = strstr(p + 1, readers[r])) {
                char *close = strchr(p, ')');
                char *quote = strchr(p, '"');
                if (quote && close && quote < close) {
                    char *end = strchr(quote + 1, '"');
                    if (end) add_shape_field(quote + 1, (size_t)(end - quote - 1));
                }
            }
        }
        /* The score and Credits checks read their field by name too. */
        if (strstr(text, "\"score\"")) add_shape_field("score", 5);
        free(text);
    }
    closedir(dir);
    return g_shape_field_count;
}

static void check_every_typed_call_rejects(const char *call) {
    char message[2400];
    if (!is_typed_call(call)) return;
    snprintf(message, sizeof message, "%s with %s: %s", call, g_mode_body, g_error_text);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, message);
    TEST_ASSERT_FALSE_MESSAGE(g_success_called, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)g_error_status, message);
}

/* Which field the sweep below gives the wrong type, and which typed calls
   reported exactly that field. */
static const char *g_shape_field;
static int g_shape_field_rejected;

static void check_field_rejected_somewhere(const char *call) {
    char expected[160];
    if (!is_typed_call(call)) return;
    snprintf(expected, sizeof expected, "field \"%s\"", g_shape_field);
    if (g_error_called && g_error_status == 200 && strstr(g_error_text, expected)) {
        g_shape_field_rejected++;
    } else {
        /* Every other call either takes the body or rejects another field. */
        TEST_ASSERT_TRUE_MESSAGE(g_success_called || g_error_status == 200, call);
    }
}

/* E7 by construction: a body in which every field any model reads is an
   object ({}, a type no scalar or list field takes) fails every typed call
   with 200; and each field alone with the wrong type is reported, by name,
   by at least one typed call. */
void test_offline_every_field_a_model_reads_is_type_checked(void) {
    ZeroBounce *zb = client_with("field-sweep");
    char *all = malloc(16384);
    char *one = malloc(1024);
    int fields = scan_shape_fields();
    size_t used;
    /* 75 fields when this was written. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT(70, fields);
    used = (size_t)snprintf(all, 16384, "{");
    for (int i = 0; i < fields; i++) {
        used += (size_t)snprintf(all + used, 16384 - used, "%s\"%s\":{}", i ? "," : "", g_shape_fields[i]);
    }
    snprintf(all + used, 16384 - used, "}");
    if (TEST_PROTECT()) {
        g_mode_body = all;
        g_mode_body_len = strlen(all);
        g_leak_mode = 10;
        g_sweep_check = check_every_typed_call_rejects;
        run_every_call(zb);
        g_sweep_check = check_field_rejected_somewhere;
        for (int i = 0; i < fields; i++) {
            const char *base[] = {"Credits", "\"7\"", "address", "\"a@b.c\"", "status", "\"valid\""};
            used = (size_t)snprintf(one, 1024, "{\"%s\":{}", g_shape_fields[i]);
            for (int b = 0; b < 6; b += 2) {
                if (strcmp(base[b], g_shape_fields[i]) != 0) {
                    used += (size_t)snprintf(one + used, 1024 - used, ",\"%s\":%s", base[b], base[b + 1]);
                }
            }
            /* The lists' rows carry the field too, for the fields only a row
               reads (email_address, a format's confidence). */
            if (strcmp(g_shape_fields[i], "email_batch") != 0) {
                used += (size_t)snprintf(one + used, 1024 - used,
                    ",\"email_batch\":[{\"%s\":{}%s}]", g_shape_fields[i],
                    strcmp(g_shape_fields[i], "address") && strcmp(g_shape_fields[i], "status")
                        ? ",\"address\":\"a@b.c\",\"status\":\"valid\"" : "");
            }
            if (strcmp(g_shape_fields[i], "errors") != 0) {
                used += (size_t)snprintf(one + used, 1024 - used, ",\"errors\":[{\"%s\":{}}]", g_shape_fields[i]);
            }
            if (strcmp(g_shape_fields[i], "other_domain_formats") != 0) {
                used += (size_t)snprintf(one + used, 1024 - used,
                    ",\"other_domain_formats\":[{\"%s\":{}}]", g_shape_fields[i]);
            }
            snprintf(one + used, 1024 - used, "}");
            g_shape_field = g_shape_fields[i];
            g_shape_field_rejected = 0;
            g_mode_body = one;
            g_mode_body_len = strlen(one);
            run_every_call(zb);
            TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, g_shape_field_rejected, one);
        }
    }
    g_leak_mode = 0;
    free(all);
    free(one);
}

static int g_ok_credits;
static void on_credits_value(ZBCreditsResponse response) {
    g_success_called = 1;
    g_ok_credits = response.credits;
}

/* E7 ACCEPT: the documented example responses (zerobounce.net/docs, as the
   SDKs' fixtures carry them) still parse, nulls included. */
void test_offline_documented_examples_still_parse(void) {
    static const char *const usage =
        "{\"total\":10,\"status_valid\":10,\"status_invalid\":0,\"status_catch_all\":0,\"status_do_not_mail\":0,"
        "\"status_spamtrap\":0,\"status_unknown\":0,\"sub_status_toxic\":0,\"sub_status_disposable\":0,"
        "\"sub_status_role_based\":0,\"sub_status_possible_trap\":0,\"sub_status_global_suppression\":0,"
        "\"sub_status_timeout_exceeded\":0,\"sub_status_mail_server_temporary_error\":0,"
        "\"sub_status_mail_server_did_not_respond\":0,\"sub_status_greylisted\":0,\"sub_status_antispam_system\":0,"
        "\"sub_status_does_not_accept_mail\":0,\"sub_status_exception_occurred\":0,\"sub_status_failed_syntax_check\":0,"
        "\"sub_status_mailbox_not_found\":0,\"sub_status_unroutable_ip_address\":0,\"sub_status_possible_typo\":0,"
        "\"sub_status_no_dns_entries\":0,\"sub_status_role_based_catch_all\":0,\"sub_status_accept_all\":0,"
        "\"sub_status_mailbox_quota_exceeded\":0,\"sub_status_forcible_disconnect\":0,"
        "\"sub_status_failed_smtp_connection\":0,\"sub_status_mx_forward\":0,\"sub_status_alternate\":0,"
        "\"sub_status_allowed\":0,\"sub_status_blocked\":0,\"sub_status_gold\":0,\"sub_status_role_based_accept_all\":0,"
        "\"start_date\":\"1/12/2010\",\"end_date\":\"12/1/2030\"}";
#define ZB_DOC_ROW(addr, st, sub) \
    "{\"address\":\"" addr "\",\"status\":\"" st "\",\"sub_status\":\"" sub "\",\"free_email\":false," \
    "\"did_you_mean\":null,\"account\":null,\"domain\":null,\"domain_age_days\":\"9692\",\"smtp_provider\":\"example\"," \
    "\"catchall_domain\":false,\"mx_found\":\"true\",\"mx_record\":\"mx.example.com\",\"firstname\":\"zero\"," \
    "\"lastname\":\"bounce\",\"gender\":\"male\",\"country\":\"United States\",\"region\":\"Florida\"," \
    "\"city\":\"West Palm Beach\",\"zipcode\":\"33401\",\"processed_at\":\"2023-03-23 13:30:28.105\"}"
    static const char *const validate[] = {
        ZB_DOC_ROW("valid@example.com", "valid", ""),
        ZB_DOC_ROW("invalid@example.com", "invalid", "mailbox_not_found"),
        "{\"address\":\"invalid@example.com\",\"status\":\"invalid\",\"sub_status\":\"failed_syntax_check\","
        "\"free_email\":false,\"did_you_mean\":null,\"account\":\"\",\"domain\":null,\"domain_age_days\":\"\","
        "\"smtp_provider\":\"\",\"mx_found\":\"false\",\"mx_record\":null,\"firstname\":\"\",\"lastname\":\"\","
        "\"gender\":\"\",\"country\":null,\"region\":null,\"city\":null,\"zipcode\":null,"
        "\"processed_at\":\"2023-04-05 08:55:40.661\"}",
        "{\"Message\":\"Greylist validation still in progress. Please try again later.\"}",
        "{\"address\":\"a@example.com\",\"Message\":\"Greylisted email, we will retry\"}",
    };
    static const char *const batch[] = {
        "{\"email_batch\":[" ZB_DOC_ROW("valid@example.com", "valid", "") "],"
        "\"errors\":[{\"error\":\"Mock error message\",\"email_address\":\"invalid@example.com\"}]}",
        "{\"email_batch\":[],\"errors\":[{\"error\":\"Mock error message\",\"email_address\":\"invalid@example.com\"}]}",
        "{\"email_batch\":[" ZB_DOC_ROW("valid@example.com", "valid", "") "],\"errors\":[]}",
    };
#undef ZB_DOC_ROW
    static const char *const status[] = {
        "{\"success\":true,\"file_id\":\"e90e9b1d-8dc7-40eb-a7d9-999d52086a56\",\"file_name\":\"emails2.txt\","
        "\"upload_date\":\"2023-04-26T17:52:23Z\",\"file_status\":\"Processing\",\"complete_percentage\":\"100%\","
        "\"error_reason\":null,\"return_url\":\"https://mock.value.com/\"}",
        "{\"success\":true,\"file_id\":\"e90e9b1d-8dc7-40eb-a7d9-999d52086a56\",\"file_name\":\"emails2.txt\","
        "\"upload_date\":\"2023-04-26T17:52:23Z\",\"file_status\":\"Deleted\",\"complete_percentage\":\"0%\","
        "\"error_reason\":\"mock value\",\"return_url\":null}",
    };
    static const char *const finder[] = {
        "{\"email\":\"john.doe@example.com\",\"email_confidence\":\"high\",\"domain\":\"\","
        "\"company_name\":\"Internet Assigned Numbers Authority\",\"did_you_mean\":\"\",\"failure_reason\":\"\"}",
        "{\"email\":\"\",\"email_confidence\":\"undetermined\",\"domain\":\"betheexample.org\","
        "\"company_name\":\"Example Inc\",\"did_you_mean\":\"\",\"failure_reason\":\"\"}",
    };
    static const char *const domain[] = {
        "{\"domain\":\"example.com\",\"company_name\":\"\",\"format\":\"unknown\",\"confidence\":\"undetermined\","
        "\"did_you_mean\":\"\",\"failure_reason\":\"\",\"other_domain_formats\":[]}",
        "{\"domain\":\"example.com\",\"company_name\":\"\",\"format\":\"first.last\",\"confidence\":\"high\","
        "\"did_you_mean\":\"\",\"failure_reason\":\"\",\"other_domain_formats\":[{\"format\":\"first_last\","
        "\"confidence\":\"high\"},{\"format\":\"first\",\"confidence\":\"medium\"}]}",
    };
    struct tm day = {0};
    ZeroBounce *zb = client_with("documented");
    EmailToValidateVector emails = email_to_validate_vector_init();
    ZBEmailToValidate email = {"valid@example.com", ""};
    email_to_validate_vector_append(&emails, email);
    day.tm_year = 126;
    day.tm_mday = 1;
#define ZB_EXPECT_OK(call, body) do { \
        serve(body); call; \
        TEST_ASSERT_FALSE_MESSAGE(g_error_called, g_error_text); \
        TEST_ASSERT_TRUE_MESSAGE(g_success_called, body); \
    } while (0)
    if (TEST_PROTECT()) {
        ZB_EXPECT_OK(get_credits(zb, on_credits_value, on_error), "{\"Credits\":\"123456\"}");
        TEST_ASSERT_EQUAL_INT(123456, g_ok_credits);
        ZB_EXPECT_OK(get_credits(zb, on_credits_value, on_error), "{\"Credits\":\"-1\"}");
        TEST_ASSERT_EQUAL_INT(-1, g_ok_credits);
        /* Credits as a JSON integer: zerobounce.net's own getcredits example,
           verbatim, and the ends of the int range. */
        ZB_EXPECT_OK(get_credits(zb, on_credits_value, on_error), "{\"Credits\":2375323}");
        TEST_ASSERT_EQUAL_INT(2375323, g_ok_credits);
        ZB_EXPECT_OK(get_credits(zb, on_credits_value, on_error), "{\"Credits\":-1}");
        TEST_ASSERT_EQUAL_INT(-1, g_ok_credits);
        ZB_EXPECT_OK(get_credits(zb, on_credits_value, on_error), "{\"Credits\":2147483647}");
        TEST_ASSERT_EQUAL_INT(2147483647, g_ok_credits);
        ZB_EXPECT_OK(get_credits(zb, on_credits_value, on_error), "{\"Credits\":-2147483648}");
        TEST_ASSERT_EQUAL_INT(-2147483647 - 1, g_ok_credits);
        ZB_EXPECT_OK(get_api_usage(zb, day, day, on_usage, on_error), usage);
        ZB_EXPECT_OK(get_activity_data(zb, "a@example.com", on_activity, on_error),
                     "{\"found\":true,\"active_in_days\":\"180\"}");
        ZB_EXPECT_OK(get_activity_data(zb, "a@example.com", on_activity, on_error),
                     "{\"found\":false,\"active_in_days\":null}");
        for (size_t i = 0; i < sizeof(validate) / sizeof(validate[0]); i++) {
            ZB_EXPECT_OK(validate_email(zb, "a@example.com", "", on_validate, on_error), validate[i]);
        }
        for (size_t i = 0; i < sizeof(batch) / sizeof(batch[0]); i++) {
            ZB_EXPECT_OK(validate_email_batch(zb, emails, on_batch, on_error), batch[i]);
        }
        ZB_EXPECT_OK(send_file(zb, "email_file.csv", 1, new_send_file_options(), on_send, on_error),
                     "{\"success\":true,\"message\":\"File Accepted\",\"file_name\":\"emails2.txt\","
                     "\"file_id\":\"e90e9b1d-8dc7-40eb-a7d9-999d52086a56\"}");
        for (size_t i = 0; i < sizeof(status) / sizeof(status[0]); i++) {
            ZB_EXPECT_OK(file_status(zb, "fid", on_status, on_error), status[i]);
        }
        ZB_EXPECT_OK(delete_file(zb, "fid", on_delete, on_error),
                     "{\"success\":true,\"message\":\"File Deleted\",\"file_name\":\"emails2.txt\","
                     "\"file_id\":\"e90e9b1d-8dc7-40eb-a7d9-999d52086a56\"}");
        for (size_t i = 0; i < sizeof(finder) / sizeof(finder[0]); i++) {
            ZB_EXPECT_OK(find_email_by_domain_first_name(zb, "example.com", "A", on_find, on_error), finder[i]);
        }
        for (size_t i = 0; i < sizeof(domain) / sizeof(domain[0]); i++) {
            ZB_EXPECT_OK(search_domain_by_domain(zb, "example.com", on_domain, on_error), domain[i]);
        }
    }
#undef ZB_EXPECT_OK
    g_leak_mode = 0;
    email_to_validate_vector_free(&emails);
}

/* ---- E2: a dead key on an HTTP 200 is an error on every call ---------- */

static void check_dead_key_body(const char *call) {
    char message[1200];
    snprintf(message, sizeof message, "%s with %s: %s", call, g_mode_body, g_error_text);
    TEST_ASSERT_TRUE_MESSAGE(g_count > 0, message);
    TEST_ASSERT_TRUE_MESSAGE(g_error_called, message);
    TEST_ASSERT_FALSE_MESSAGE(g_success_called, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)g_error_status, message);
}

/* The three dead-key spellings the API answers with on an HTTP 200, over
   every request function: the uploads (send_file and its siblings), the
   S3/SFTP jobs, Inbox Placement and the raw-JSON calls included. */
void test_offline_a_dead_key_on_200_is_an_error_on_every_call(void) {
    static const char *const bodies[] = {
        "{\"error\":\"Invalid API key or your account ran out of credits\"}",
        "{\"error_message\":\"Invalid API key or your account ran out of credits\"}",
        "{\"Error\":\"Invalid API key or your account ran out of credits\"}",
    };
    ZeroBounce *zb = client_with("dead-key-sweep");
    g_sweep_check = check_dead_key_body;
    if (TEST_PROTECT()) {
        for (size_t i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
            g_mode_body = bodies[i];
            g_mode_body_len = strlen(bodies[i]);
            g_leak_mode = 10;
            run_every_call(zb);
        }
    }
    g_leak_mode = 0;
}

/* ---- E7 / X1: every nested value of every example is type-checked ------ */

/* The typed calls the examples below are answers to. */
enum {
    EX_CREDITS, EX_USAGE, EX_ACTIVITY, EX_VALIDATE, EX_VALIDATE_POST, EX_BATCH, EX_SEND, EX_STATUS,
    EX_DELETE, EX_SCORING_SEND, EX_SCORING_STATUS, EX_SCORING_DELETE, EX_SCORE, EX_FIND, EX_DOMAIN,
};

typedef struct {
    int call;
    const char *body;
} TypedExample;

/* Every documented example answer a typed call parses: copied verbatim from
   zerobounce.net/docs (the credit balance, validate, file status, send file
   and delete file pages), and the repo's own fixtures for the answers the
   site gives no success example of. */
static const TypedExample g_typed_examples[] = {
    /* zerobounce.net: v2 credit balance. */
    {EX_CREDITS, "{\"Credits\":2375323}"},
    {EX_CREDITS, "{\"Credits\":\"-1\"}"},
    /* zerobounce.net: v2 validate emails. */
    {EX_VALIDATE, "{\n      \"address\": \"john.doe@zerobounce.net\",\n      \"status\": \"valid\",\n      \"sub_status\": \"\",\n"
                  "      \"free_email\": false,\n      \"catchall_domain\": true,\n      \"did_you_mean\": null,\n"
                  "      \"account\": \"johndoe\",\n      \"domain\": \"zerobounce.net\",\n      \"domain_age_days\": \"3431\",\n"
                  "      \"active_in_days\": \"30\",\n      \"active_first_seen\": \"2026-02-25\",\n"
                  "      \"smtp_provider\": \"g-suite\",\n      \"mx_found\": \"true\",\n      \"mx_record\": \"aspmx.l.google.com\",\n"
                  "      \"firstname\": \"dan\",\n      \"lastname\": \"cotet\",\n      \"gender\": \"male\",\n      \"country\": null,\n"
                  "      \"region\": null,\n      \"city\": null,\n      \"zipcode\": null,\n"
                  "      \"processed_at\": \"2026-06-18 07:05:00.033\"\n  }"},
    {EX_VALIDATE, "{\"address\":\"a@example.com\",\"Message\":\"Greylisted email, we will retry\"}"},
    {EX_VALIDATE_POST, "{\"address\":\"a@example.com\",\"status\":\"valid\",\"sub_status\":\"\",\"free_email\":false,"
                       "\"catchall_domain\":false,\"domain_age_days\":\"9692\",\"mx_found\":\"true\",\"firstname\":\"zero\"}"},
    /* The repo's batch fixtures: a row next to a per-address error. */
    {EX_BATCH, "{\"email_batch\":[{\"address\":\"valid@example.com\",\"status\":\"valid\",\"sub_status\":\"\","
               "\"free_email\":false,\"did_you_mean\":null,\"account\":null,\"domain\":null,\"domain_age_days\":\"9692\","
               "\"smtp_provider\":\"example\",\"catchall_domain\":false,\"mx_found\":\"true\",\"mx_record\":\"mx.example.com\","
               "\"firstname\":\"zero\",\"lastname\":\"bounce\",\"gender\":\"male\",\"country\":\"United States\","
               "\"region\":\"Florida\",\"city\":\"West Palm Beach\",\"zipcode\":\"33401\","
               "\"processed_at\":\"2023-03-23 13:30:28.105\"}],"
               "\"errors\":[{\"error\":\"Mock error message\",\"email_address\":\"invalid@example.com\"}]}"},
    /* zerobounce.net: batch validate, three rows and no errors. */
    {EX_BATCH, "{\n"
                  "      \"email_batch\": [\n"
                  "          {\n"
                  "              \"address\": \"john.doe@zerobounce.net\",\n"
                  "              \"status\": \"valid\",\n"
                  "              \"sub_status\": \"\",\n"
                  "              \"free_email\": false,\n"
                  "              \"catchall_domain\": true,\n"
                  "              \"did_you_mean\": null,\n"
                  "              \"account\": \"johndoe\",\n"
                  "              \"domain\": \"zerobounce.net\",\n"
                  "              \"domain_age_days\": \"3431\",\n"
                  "              \"active_in_days\": \"30\",\n"
                  "              \"active_first_seen\": \"2026-02-25\",\n"
                  "              \"smtp_provider\": \"g-suite\",\n"
                  "              \"mx_found\": \"true\",\n"
                  "              \"mx_record\": \"aspmx.l.google.com\",\n"
                  "              \"firstname\": \"dan\",\n"
                  "              \"lastname\": \"cotet\",\n"
                  "              \"gender\": \"male\",\n"
                  "              \"country\": null,\n"
                  "              \"region\": null,\n"
                  "              \"city\": null,\n"
                  "              \"zipcode\": null,\n"
                  "              \"processed_at\": \"2026-06-18 07:05:00.033\"\n"
                  "          },\n"
                  "          {\n"
                  "              \"address\": \"invalid@example.com\",\n"
                  "              \"status\": \"invalid\",\n"
                  "              \"sub_status\": \"mailbox_not_found\",\n"
                  "              \"free_email\": false,\n"
                  "              \"did_you_mean\": null,\n"
                  "              \"account\": null,\n"
                  "              \"domain\": null,\n"
                  "              \"domain_age_days\": \"9692\",\n"
                  "              \"active_in_days\": \"365\",\n"
                  "              \"active_first_seen\": \"2026-02-25\",\n"
                  "              \"smtp_provider\": \"example\",\n"
                  "              \"mx_found\": \"true\",\n"
                  "              \"mx_record\": \"mx.example.com\",\n"
                  "              \"catchall_domain\": false,\n"
                  "              \"firstname\": \"zero\",\n"
                  "              \"lastname\": \"bounce\",\n"
                  "              \"gender\": \"male\",\n"
                  "              \"country\": null,\n"
                  "              \"region\": null,\n"
                  "              \"city\": null,\n"
                  "              \"zipcode\": null,\n"
                  "              \"processed_at\": \"2020-09-17 17:43:11.830\"\n"
                  "          },\n"
                  "          {\n"
                  "              \"address\": \"disposable@example.com\",\n"
                  "              \"status\": \"do_not_mail\",\n"
                  "              \"sub_status\": \"disposable\",\n"
                  "              \"free_email\": false,\n"
                  "              \"did_you_mean\": null,\n"
                  "              \"account\": null,\n"
                  "              \"domain\": null,\n"
                  "              \"domain_age_days\": \"9692\",\n"
                  "              \"active_in_days\": \"365\",\n"
                  "              \"active_first_seen\": \"2026-02-25\",\n"
                  "              \"smtp_provider\": \"example\",\n"
                  "              \"mx_found\": \"true\",\n"
                  "              \"mx_record\": \"mx.example.com\",\n"
                  "              \"catchall_domain\": false,\n"
                  "              \"firstname\": \"zero\",\n"
                  "              \"lastname\": \"bounce\",\n"
                  "              \"gender\": \"male\",\n"
                  "              \"country\": null,\n"
                  "              \"region\": null,\n"
                  "              \"city\": null,\n"
                  "              \"zipcode\": null,\n"
                  "              \"processed_at\": \"2020-09-17 17:43:11.830\"\n"
                  "          }\n"
                  "      ],\n"
                  "      \"errors\": []\n"
                  "  }\n"},
    /* zerobounce.net: v2 send file, file status (both), delete file. */
    {EX_SEND, "{\n      \"success\": true,\n      \"message\": \"File Accepted\",\n      \"file_name\": \"Your file name.csv\",\n"
              "      \"file_id\": \"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff\"\n    }"},
    /* sendfile's message may be a list of strings. */
    {EX_SEND, "{\"success\":true,\"message\":[\"File Accepted\"],\"file_name\":\"f.csv\",\"file_id\":\"fid\"}"},
    {EX_STATUS, "{\n    \"success\": true,\n    \"file_id\": \"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff\",\n"
                "    \"file_name\": \"Your file name.csv\",\n    \"upload_date\": \"2023-04-28T15:25:41Z\",\n"
                "    \"file_status\": \"Complete\",\n    \"file_phase_2_status\": \"Complete\",\n"
                "    \"complete_percentage\": \"100%\",\n"
                "    \"return_url\": \"Your return URL if provided when calling sendfile API\"\n  }"},
    {EX_DELETE, "{\n    \"success\": true,\n    \"message\": \"File Deleted\",\n    \"file_name\": \"test2\",\n"
                "    \"file_id\": \"b222a0fd-90d5-416c-8f1a-9cc3851fc823\"\n  }"},
    /* zerobounce.net: file status with file_phase_2_status "N/A". */
    {EX_STATUS, "{\n"
                  "    \"success\": true,\n"
                  "    \"file_id\": \"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff\",\n"
                  "    \"file_name\": \"Your file name.csv\",\n"
                  "    \"upload_date\": \"2023-04-28T15:25:41Z\",\n"
                  "    \"file_status\": \"Complete\",\n"
                  "    \"file_phase_2_status\": \"N/A\",\n"
                  "    \"complete_percentage\": \"100%\",\n"
                  "    \"return_url\": \"Your return URL if provided when calling sendfile API\"\n"
                  "  }\n"},
    /* zerobounce.net: AI scoring send file, file status, delete file. */
    {EX_SCORING_SEND, "{\n          \"success\": true,\n          \"message\": \"File Accepted\",\n"
                      "          \"file_name\": \"Your file name.csv\",\n"
                      "          \"file_id\": \"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff\"\n      }"},
    {EX_SCORING_STATUS, "{\n          \"success\": true,\n          \"file_id\": \"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff\",\n"
                        "          \"file_name\": \"Your file name.csv\",\n          \"upload_date\": \"2023-04-28T15:25:41Z\",\n"
                        "          \"file_status\": \"Complete\",\n          \"complete_percentage\": \"100%\",\n"
                        "          \"return_url\": \"Your return URL if provided when calling sendfile API\"\n      }"},
    {EX_SCORING_DELETE, "{\n        \"success\": true,\n        \"message\": \"File Deleted\",\n        \"file_name\": \"test2\",\n"
                        "        \"file_id\": \"b222a0fd-90d5-416c-8f1a-9cc3851fc823\"\n    }"},
    /* zerobounce.net: get-api-usage, with the leading-period and alias sub-statuses. */
    {EX_USAGE, "{\n"
                  "      \"total\": 120,\n"
                  "      \"status_valid\": 17,\n"
                  "      \"status_invalid\": 69,\n"
                  "      \"status_catch_all\": 9,\n"
                  "      \"status_do_not_mail\": 16,\n"
                  "      \"status_spamtrap\": 0,\n"
                  "      \"status_unknown\": 6,\n"
                  "      \"sub_status_toxic\": 0,\n"
                  "      \"sub_status_disposable\": 1,\n"
                  "      \"sub_status_role_based\": 1,\n"
                  "      \"sub_status_possible_trap\": 0,\n"
                  "      \"sub_status_global_suppression\": 14,\n"
                  "      \"sub_status_timeout_exceeded\": 0,\n"
                  "      \"sub_status_mail_server_temporary_error\": 0,\n"
                  "      \"sub_status_mail_server_did_not_respond\": 0,\n"
                  "      \"sub_status_greylisted\": 6,\n"
                  "      \"sub_status_antispam_system\": 0,\n"
                  "      \"sub_status_does_not_accept_mail\": 27,\n"
                  "      \"sub_status_exception_occurred\": 0,\n"
                  "      \"sub_status_failed_syntax_check\": 20,\n"
                  "      \"sub_status_mailbox_not_found\": 12,\n"
                  "      \"sub_status_unroutable_ip_address\": 0,\n"
                  "      \"sub_status_possible_typo\": 0,\n"
                  "      \"sub_status_no_dns_entries\": 10,\n"
                  "      \"sub_status_role_based_catch_all\": 0,\n"
                  "      \"sub_status_mailbox_quota_exceeded\": 0,\n"
                  "      \"sub_status_forcible_disconnect\": 0,\n"
                  "      \"sub_status_failed_smtp_connection\": 0,\n"
                  "      \"sub_status_leading_period_removed\": 0,\n"
                  "      \"sub_status_alias_address\": 2,\n"
                  "      \"sub_status_mx_forward\": 0,\n"
                  "      \"sub_status_alternate\": 0,\n"
                  "      \"sub_status_blocked\": 0,\n"
                  "      \"sub_status_allowed\": 0,\n"
                  "      \"start_date\": \"1/1/2023\",\n"
                  "      \"end_date\": \"1/30/2024\"\n"
                  "    }\n"},
    /* The repo's fixtures for the other typed calls. */
    {EX_USAGE, "{\"total\":10,\"status_valid\":10,\"status_invalid\":0,\"sub_status_toxic\":0,"
               "\"start_date\":\"1/12/2010\",\"end_date\":\"12/1/2030\"}"},
    {EX_ACTIVITY, "{\"found\":true,\"active_in_days\":\"180\"}"},
    {EX_SCORE, "{\"email\":\"a@example.com\",\"score\":10}"},
    {EX_FIND, "{\"email\":\"john.doe@example.com\",\"email_confidence\":\"high\",\"domain\":\"\","
              "\"company_name\":\"Internet Assigned Numbers Authority\",\"did_you_mean\":\"\",\"failure_reason\":\"\"}"},
    {EX_DOMAIN, "{\"domain\":\"example.com\",\"company_name\":\"\",\"format\":\"first.last\",\"confidence\":\"high\","
                "\"did_you_mean\":\"\",\"failure_reason\":\"\",\"other_domain_formats\":[{\"format\":\"first_last\","
                "\"confidence\":\"high\"},{\"format\":\"first\",\"confidence\":\"medium\"}]}"},
};

static void run_typed_example(ZeroBounce *zb, int call) {
    struct tm day = {0};
    EmailToValidateVector emails = email_to_validate_vector_init();
    ZBEmailToValidate email = {"valid@example.com", ""};
    ZBValidateOptions post = new_validate_options();
    day.tm_year = 126;
    day.tm_mday = 1;
    post.use_post = 1;
    email_to_validate_vector_append(&emails, email);
    switch (call) {
        case EX_CREDITS: get_credits(zb, on_credits, on_error); break;
        case EX_USAGE: get_api_usage(zb, day, day, on_usage, on_error); break;
        case EX_ACTIVITY: get_activity_data(zb, "a@example.com", on_activity, on_error); break;
        case EX_VALIDATE: validate_email(zb, "a@example.com", "", on_validate, on_error); break;
        case EX_VALIDATE_POST: validate_email_with_options(zb, "a@example.com", "", &post, on_validate, on_error); break;
        case EX_BATCH: validate_email_batch(zb, emails, on_batch, on_error); break;
        case EX_SEND: send_file(zb, "email_file.csv", 1, new_send_file_options(), on_send, on_error); break;
        case EX_STATUS: file_status(zb, "fid", on_status, on_error); break;
        case EX_DELETE: delete_file(zb, "fid", on_delete, on_error); break;
        case EX_SCORING_SEND: scoring_send_file(zb, "email_file.csv", 1, new_send_file_options(), on_send, on_error); break;
        case EX_SCORING_STATUS: scoring_file_status(zb, "fid", on_status, on_error); break;
        case EX_SCORING_DELETE: scoring_delete_file(zb, "fid", on_delete, on_error); break;
        case EX_SCORE: score_email(zb, "a@example.com", on_score, on_error); break;
        case EX_FIND: find_email_by_domain_first_name(zb, "example.com", "A", on_find, on_error); break;
        case EX_DOMAIN: search_domain_by_domain(zb, "example.com", on_domain, on_error); break;
        default: TEST_FAIL_MESSAGE("unknown example call");
    }
    email_to_validate_vector_free(&emails);
}

/* One value at one path of an example: found by its position in a pre-order
   walk (the root not counted). */
typedef struct {
    json_object *parent;
    const char *key;
    size_t index;
    json_object *node;
    char path[256];
} NestedSlot;

static int find_nested_slot(json_object *node, const char *path, int *counter, int want, NestedSlot *slot) {
    char child_path[256];
    if (json_object_is_type(node, json_type_object)) {
        json_object_object_foreach(node, key, value) {
            snprintf(child_path, sizeof child_path, "%s.%s", path, key);
            if ((*counter)++ == want) {
                slot->parent = node;
                slot->key = key;
                slot->node = value;
                snprintf(slot->path, sizeof slot->path, "%s", child_path);
                return 1;
            }
            if (value && find_nested_slot(value, child_path, counter, want, slot)) return 1;
        }
    } else if (json_object_is_type(node, json_type_array)) {
        for (size_t i = 0; i < json_object_array_length(node); i++) {
            json_object *value = json_object_array_get_idx(node, i);
            snprintf(child_path, sizeof child_path, "%s[%zu]", path, i);
            if ((*counter)++ == want) {
                slot->parent = node;
                slot->key = NULL;
                slot->index = i;
                slot->node = value;
                snprintf(slot->path, sizeof slot->path, "%s", child_path);
                return 1;
            }
            if (value && find_nested_slot(value, child_path, counter, want, slot)) return 1;
        }
    }
    return 0;
}

/* The fields a call cannot answer without (standard.md: Credits, and the
   address and status of a validate answer and of every batch row). */
static int nested_slot_is_required(const NestedSlot *slot, int greylist) {
    if (greylist) {
        /* A greylist answer is a result by its Message alone. */
        return slot->key && strcmp(slot->key, "Message") == 0;
    }
    return slot->key && (strcmp(slot->key, "Credits") == 0 || strcmp(slot->key, "address") == 0 ||
                         strcmp(slot->key, "status") == 0);
}

/* The values a field takes besides its type in the example: Credits and a
   score are integers sent as a JSON integer or as a string; a sendfile
   message is a string or a list of strings. */
static int nested_slot_also_takes(const NestedSlot *slot, json_object *value, int call) {
    if (!slot->key) return 0;
    if (strcmp(slot->key, "Credits") == 0 || strcmp(slot->key, "score") == 0) {
        return json_object_is_type(value, json_type_int);
    }
    if (strcmp(slot->key, "message") == 0 && (call == EX_SEND || call == EX_SCORING_SEND)) {
        return json_object_is_type(value, json_type_array) || json_object_is_type(value, json_type_string);
    }
    return 0;
}

/* Standard section 3: a value of another JSON type than the documented one,
   at any path of an example (a nested object, an array item, a batch row,
   a row's field), is an API error with status 200. null is taken only for an
   optional object field; an array item that is null is a wrong type too.
   Credits alone also takes a JSON integer. */
void test_offline_every_nested_value_of_an_example_is_type_checked(void) {
    static const char *const replacements[] = {"null", "true", "5", "5.5", "\"x\"", "[]", "{}"};
    ZeroBounce *zb = client_with("nested-sweep");
    char failures[4096] = "";
    int failed = 0;
    int checked = 0;
    if (TEST_PROTECT()) {
        for (size_t e = 0; e < sizeof(g_typed_examples) / sizeof(g_typed_examples[0]); e++) {
            const TypedExample *example = &g_typed_examples[e];
            json_object *root = json_tokener_parse(example->body);
            int slots = 0;
            NestedSlot slot;
            TEST_ASSERT_NOT_NULL_MESSAGE(root, example->body);
            /* The example itself parses (ACCEPT). */
            serve(example->body);
            run_typed_example(zb, example->call);
            TEST_ASSERT_FALSE_MESSAGE(g_error_called, g_error_text);
            TEST_ASSERT_TRUE_MESSAGE(g_success_called, example->body);
            find_nested_slot(root, "$", &slots, -1, &slot);
            for (int k = 0; k < slots; k++) {
                for (size_t r = 0; r < sizeof(replacements) / sizeof(replacements[0]); r++) {
                    json_object *copy = NULL;
                    json_object *value = json_tokener_parse(replacements[r]);
                    int counter = 0;
                    int expect_ok;
                    const char *text;
                    TEST_ASSERT_EQUAL_INT(0, json_object_deep_copy(root, &copy, NULL));
                    TEST_ASSERT_TRUE(find_nested_slot(copy, "$", &counter, k, &slot));
                    /* Same type, or a null in the example (its type is not
                       shown there): nothing to check. */
                    if (slot.node == NULL || json_object_get_type(slot.node) == json_object_get_type(value)) {
                        json_object_put(value);
                        json_object_put(copy);
                        continue;
                    }
                    expect_ok = (value == NULL && slot.key &&
                                 !nested_slot_is_required(&slot, strstr(example->body, "\"Message\"") != NULL)) ||
                                nested_slot_also_takes(&slot, value, example->call);
                    if (slot.key) {
                        json_object_object_add(slot.parent, slot.key, value);
                    } else {
                        json_object_array_put_idx(slot.parent, slot.index, value);
                    }
                    text = json_object_to_json_string_ext(copy, JSON_C_TO_STRING_PLAIN);
                    serve(text);
                    run_typed_example(zb, example->call);
                    checked++;
                    if (expect_ok ? (g_error_called || !g_success_called)
                                  : (!g_error_called || g_success_called || g_error_status != 200)) {
                        size_t used = strlen(failures);
                        failed++;
                        if (used + 200 < sizeof failures) {
                            snprintf(failures + used, sizeof failures - used, "\n  example %zu %s = %s: %s",
                                     e, slot.path, replacements[r], expect_ok ? "rejected" : "accepted");
                        }
                    }
                    json_object_put(copy);
                }
            }
            json_object_put(root);
        }
        /* 17 examples, about 560 substitutions when this was written. */
        TEST_ASSERT_GREATER_OR_EQUAL_INT(500, checked);
        if (failed) fprintf(stderr, "nested sweep:%s\n", failures);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, failed, "see the list on stderr");
    }
    g_leak_mode = 0;
}

void offline_run_tests(void) {
    RUN_TEST(test_offline_get_credits);
    RUN_TEST(test_offline_validate_options);
    RUN_TEST(test_offline_validate_post);
    RUN_TEST(test_offline_batch_omits_and_keeps_zero_ip);
    RUN_TEST(test_offline_batch_explicit_false);
    RUN_TEST(test_offline_send_file_bools_and_zero_column);
    RUN_TEST(test_offline_send_file_always_sends_remove_duplicate);
    RUN_TEST(test_offline_send_file_options_stay_source_compatible);
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
    RUN_TEST(test_offline_form_body_is_not_prefixed_with_garbage);
    RUN_TEST(test_offline_s3_jobs_require_a_return_url);
    RUN_TEST(test_offline_changed_calls_report_error_bodies);
    RUN_TEST(test_offline_email_finder_file_needs_a_name_column);
    RUN_TEST(test_offline_send_file_without_a_return_url);
    RUN_TEST(test_offline_bulk_base_url_overrides);
    RUN_TEST(test_offline_list_evaluator_base_url);
    RUN_TEST(test_offline_inbox_placement);
    RUN_TEST(test_offline_inbox_placement_limits);
    RUN_TEST(test_offline_inbox_placement_json_is_escaped);
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
    RUN_TEST(test_offline_dead_key_is_an_error);
    RUN_TEST(test_offline_bare_message_is_a_result_where_the_api_means_one);
    RUN_TEST(test_offline_get_file_writes_without_a_success_callback);
    RUN_TEST(test_offline_get_file_that_cannot_be_written_is_an_error);
    RUN_TEST(test_offline_get_file_json_error_keeps_the_http_status);
    RUN_TEST(test_offline_base_url_setters_strip_trailing_slashes);
    RUN_TEST(test_offline_every_call_is_https_only);
    RUN_TEST(test_offline_every_request_function_is_in_the_sweep);
    RUN_TEST(test_offline_a_rejected_key_replaces_the_previous_one);
    RUN_TEST(test_offline_success_false_is_an_error_on_every_call);
    RUN_TEST(test_offline_every_form_of_a_secret_is_masked);
    RUN_TEST(test_offline_every_composed_spelling_of_a_secret_is_masked);
    RUN_TEST(test_offline_no_error_carries_the_key);
    RUN_TEST(test_offline_the_content_type_alone_makes_a_200_an_error);
    RUN_TEST(test_offline_an_empty_body_is_a_success_only_for_an_inbox_placement_delete);
    RUN_TEST(test_offline_a_json_object_in_place_of_a_file_is_an_error);
    RUN_TEST(test_offline_an_html_page_in_place_of_a_file_is_an_error);
    RUN_TEST(test_offline_a_short_write_is_an_error_and_leaves_no_partial_file);
    RUN_TEST(test_offline_a_failed_write_keeps_the_file_already_at_the_path);
    RUN_TEST(test_offline_every_json_call_takes_only_strict_json);
    RUN_TEST(test_offline_redaction_is_closed_under_encoding);
    RUN_TEST(test_offline_a_body_still_holding_the_key_after_masking_is_dropped);
    RUN_TEST(test_offline_each_decode_step_of_the_redaction_check_is_needed);
    RUN_TEST(test_offline_a_nul_in_the_body_does_not_end_the_redaction_check);
    RUN_TEST(test_offline_typed_calls_reject_json_of_the_wrong_shape);
    RUN_TEST(test_offline_a_deeply_nested_body_is_an_error);
    RUN_TEST(test_offline_typed_calls_reject_fields_of_the_wrong_type);
    RUN_TEST(test_offline_success_string_false_is_an_error);
    RUN_TEST(test_offline_return_url_is_sent_only_when_set);
    RUN_TEST(test_offline_every_field_a_model_reads_is_type_checked);
    RUN_TEST(test_offline_documented_examples_still_parse);
    RUN_TEST(test_offline_every_nested_value_of_an_example_is_type_checked);
    RUN_TEST(test_offline_a_dead_key_on_200_is_an_error_on_every_call);
}
