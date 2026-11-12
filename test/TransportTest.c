/*
 * The real libcurl transport against a local socket server: redirects are not
 * followed, and the total and connect timeouts are set on every request path.
 * These fail if CURLOPT_FOLLOWLOCATION, CURLOPT_TIMEOUT_MS or
 * CURLOPT_CONNECTTIMEOUT_MS is dropped from any path. A download answered
 * with a JSON object, a non-2xx status or cut short is an error and leaves no
 * file (and a file already at the path as it was), a JSON response cut short
 * is an error on every path, and a column number of any int is sent whole.
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "unity.h"
#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/ZeroBounceWarmup.h"
#include "ZeroBounce/zb_internal.h"

/* A stalled connection is closed by the server itself after this long, so a
   request without a timeout still comes back (late) instead of hanging. */
#define T_STALL_MS 4000
#define T_MAX_HELD 32

typedef struct {
    int listen_fd;
    int port;
    /* 1: answer 302 to /landed. 2: answer 200 with a CSV body. 3: answer 200
       with {"Message": ...} as application/json; 4: the same as text/csv.
       5: a 200 header with Content-Length 1000, 26 bytes, then close.
       6: answer 200 with T_JSON, a JSON object every JSON call accepts.
       7: a 200 header with Content-Length 1000, T_JSON, then close.
       8: answer 500 with a CSV body.
       9: a 200 CSV header with Content-Length 100000, 14006 bytes (more
          than the streaming download holds back), then close.
       10: answer 500 with a 200-byte text body. 11: answer 200 with a
           200-byte JSON object.
       0: accept and never answer. */
    int redirect;
    volatile int stop;
    pthread_t thread;
    pthread_mutex_t mu;
    int requests;
    int landed;
    /* The last request received, headers and body. */
    char last_request[16384];
} TServer;

static long t_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

#define T_JSON "{\"Credits\":\"5\",\"success\":true,\"message\":\"File Accepted\"," \
    "\"file_id\":\"fid\",\"email_batch\":[],\"errors\":[]}"

/* Sends all of it; the accepted socket can be non-blocking (BSD, macOS). */
static void t_send_all(int fd, const char *data, size_t length) {
    long deadline = t_now_ms() + 5000;
    while (length > 0 && t_now_ms() < deadline) {
        struct pollfd pfd = {fd, POLLOUT, 0};
        ssize_t n;
        if (poll(&pfd, 1, 50) <= 0) {
            continue;
        }
        n = send(fd, data, length, 0);
        if (n <= 0) {
            break;
        }
        data += n;
        length -= (size_t)n;
    }
}

static void t_answer_redirect(TServer *server, int fd) {
    static char long_cut[16384];
    char whole_json[512];
    char request[16384];
    size_t used = 0;
    long deadline = t_now_ms() + 5000;
    char *header_end = NULL;
    size_t content_length = 0;
    const char *response;
    switch (server->redirect) {
    case 2:
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/csv\r\nContent-Length: 24\r\nConnection: close\r\n\r\n"
                   "email\nsaved@example.com\n";
        break;
    case 3:
        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 28\r\n"
                   "Connection: close\r\n\r\n{\"Message\":\"File not found\"}";
        break;
    case 4:
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/csv\r\nContent-Length: 28\r\n"
                   "Connection: close\r\n\r\n{\"Message\":\"File not found\"}";
        break;
    case 5:
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/csv\r\nContent-Length: 1000\r\n"
                   "Connection: close\r\n\r\nemail\npartial@example.com\n";
        break;
    case 6:
        snprintf(whole_json, sizeof(whole_json),
                 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n"
                 "Connection: close\r\n\r\n%s", strlen(T_JSON), T_JSON);
        response = whole_json;
        break;
    case 7:
        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 1000\r\n"
                   "Connection: close\r\n\r\n" T_JSON;
        break;
    case 8:
        response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/csv\r\nContent-Length: 24\r\n"
                   "Connection: close\r\n\r\nemail\nfailed@example.com\n";
        break;
    case 9:
        if (!long_cut[0]) {
            size_t used = (size_t)snprintf(long_cut, sizeof(long_cut),
                "HTTP/1.1 200 OK\r\nContent-Type: text/csv\r\nContent-Length: 100000\r\n"
                "Connection: close\r\n\r\nemail\n");
            for (int i = 0; i < 1000; i++) {
                memcpy(long_cut + used, "x@example.com\n", 14);
                used += 14;
            }
            long_cut[used] = '\0';
        }
        response = long_cut;
        break;
    case 10:
    case 11: {
        /* 10: a 500 text body, 11: a 200 JSON object, each 200 bytes: larger
           than the cap the test sets. */
        static char over_cap[512];
        char pad[193];
        memset(pad, 'x', 192);
        pad[192] = '\0';
        snprintf(over_cap, sizeof(over_cap),
                 server->redirect == 10
                     ? "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nContent-Length: 200\r\n"
                       "Connection: close\r\n\r\nerror:  %s"
                     : "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 200\r\n"
                       "Connection: close\r\n\r\n{\"p\":\"%s\"}",
                 pad);
        response = over_cap;
        break;
    }
    default:
        response = "HTTP/1.1 302 Found\r\nLocation: /landed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        break;
    }

    request[0] = '\0';
    while (t_now_ms() < deadline && used + 1 < sizeof(request)) {
        struct pollfd pfd = {fd, POLLIN, 0};
        ssize_t n;
        if (header_end && used >= (size_t)(header_end - request) + 4 + content_length) {
            break;
        }
        if (poll(&pfd, 1, 50) <= 0) {
            continue;
        }
        n = recv(fd, request + used, sizeof(request) - 1 - used, 0);
        if (n <= 0) {
            break;
        }
        used += (size_t)n;
        request[used] = '\0';
        if (!header_end && (header_end = strstr(request, "\r\n\r\n")) != NULL) {
            char lower[16384];
            size_t i;
            const char *at;
            for (i = 0; i < (size_t)(header_end - request); i++) {
                lower[i] = (char)tolower((unsigned char)request[i]);
            }
            lower[i] = '\0';
            at = strstr(lower, "content-length:");
            if (at) {
                content_length = (size_t)strtoul(at + 15, NULL, 10);
            }
        }
    }
    pthread_mutex_lock(&server->mu);
    server->requests++;
    memcpy(server->last_request, request, used + 1);
    if (strstr(request, " /landed")) {
        server->landed++;
    }
    pthread_mutex_unlock(&server->mu);
    t_send_all(fd, response, strlen(response));
    close(fd);
}

static void *t_run(void *arg) {
    TServer *server = (TServer *)arg;
    int held[T_MAX_HELD];
    long until[T_MAX_HELD];
    int held_count = 0;
    int i;

    /* poll with a short timeout rather than blocking in accept(), so stopping
       never depends on a socket waking the thread. */
    while (!server->stop) {
        struct pollfd pfd = {server->listen_fd, POLLIN, 0};
        if (poll(&pfd, 1, 20) > 0 && (pfd.revents & POLLIN)) {
            int fd = accept(server->listen_fd, NULL, NULL);
            if (fd >= 0) {
                if (server->redirect) {
                    t_answer_redirect(server, fd);
                } else if (held_count < T_MAX_HELD) {
                    held[held_count] = fd;
                    until[held_count] = t_now_ms() + T_STALL_MS;
                    held_count++;
                } else {
                    close(fd);
                }
            }
        }
        for (i = 0; i < held_count;) {
            if (t_now_ms() >= until[i]) {
                close(held[i]);
                held[i] = held[held_count - 1];
                until[i] = until[held_count - 1];
                held_count--;
            } else {
                i++;
            }
        }
    }
    for (i = 0; i < held_count; i++) {
        close(held[i]);
    }
    return NULL;
}

static void t_start(TServer *server, int redirect) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int yes = 1;

    memset(server, 0, sizeof(*server));
    server->redirect = redirect;
    pthread_mutex_init(&server->mu, NULL);
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    TEST_ASSERT_EQUAL_INT(0, bind(server->listen_fd, (struct sockaddr *)&addr, sizeof(addr)));
    TEST_ASSERT_EQUAL_INT(0, listen(server->listen_fd, 16));
    /* Non-blocking, so a client that gives up between poll() and accept()
       cannot leave the server thread stuck in accept(). */
    fcntl(server->listen_fd, F_SETFL, fcntl(server->listen_fd, F_GETFL, 0) | O_NONBLOCK);
    getsockname(server->listen_fd, (struct sockaddr *)&addr, &len);
    server->port = ntohs(addr.sin_port);
    pthread_create(&server->thread, NULL, t_run, server);
}

static void t_stop(TServer *server) {
    server->stop = 1;
    pthread_join(server->thread, NULL);
    close(server->listen_fd);
    pthread_mutex_destroy(&server->mu);
}

static int t_errors;
static long t_status;
static int t_successes;

static void t_on_error(ZBErrorResponse error) {
    t_errors++;
    t_status = error.status_code;
}
static void t_on_credits(ZBCreditsResponse r) { (void)r; t_successes++; }
static void t_on_score(ZBScoreEmailResponse r) { (void)r; t_successes++; }
static void t_on_send(ZBSendFileResponse r) { (void)r; t_successes++; }
static void t_on_get(ZBGetFileResponse r) { (void)r; t_successes++; }
static void t_on_batch(ZBValidateBatchResponse r) { (void)r; t_successes++; }
static void t_on_raw(const char *json) { (void)json; t_successes++; }

static char t_base[96];
static char t_bulk[96];
static char t_scoring[96];
static char t_ipt[96];

/* A client pointed at the local server, every base URL included. */
static ZeroBounce *t_client(const char *scheme, int port) {
    ZeroBounce *zb = new_zero_bounce_instance();
    zero_bounce_initialize(zb, "transport-key");
    snprintf(t_base, sizeof(t_base), "%s://127.0.0.1:%d/v2", scheme, port);
    snprintf(t_bulk, sizeof(t_bulk), "%s://127.0.0.1:%d", scheme, port);
    snprintf(t_scoring, sizeof(t_scoring), "%s://127.0.0.1:%d/v2/scoring", scheme, port);
    snprintf(t_ipt, sizeof(t_ipt), "%s://127.0.0.1:%d/api/ipt", scheme, port);
    zb->api_base_url = t_base;
    zero_bounce_set_bulk_api_base_url(zb, t_base);
    zero_bounce_set_bulk_api_root_url(zb, t_bulk);
    zero_bounce_set_bulk_api_scoring_base_url(zb, t_scoring);
    zero_bounce_set_inbox_placement_base_url(zb, t_ipt);
    return zb;
}

#define T_PATHS 11

/* One call down each request path the SDK has: the shared GET, form POST and
   extra-header transports (GET, PUT, DELETE), the sendfile and batch POSTs,
   the buffered and streamed getfile, the bulk-root multipart and getfile, and
   the Warmup client. */
static void t_call_path(int index, ZeroBounce *zb, ZeroBounceWarmup *zbw) {
    SendFileOptions send_options = new_send_file_options();
    EmailFinderSendFileOptions finder = new_email_finder_send_file_options();
    EmailToValidateVector emails;
    ZBEmailToValidate email = {"a@example.com", ""};
    const char *path = "test/downloads/transport.csv";

    finder.first_name_column = 2;
    switch (index) {
    case 0: get_credits(zb, t_on_credits, t_on_error); break;
    case 1: score_email_post(zb, "a@example.com", t_on_score, t_on_error); break;
    case 2: get_inbox_placement_providers(zb, t_on_raw, t_on_error); break;
    case 3: set_inbox_placement_webhook(zb, "https://example.com/hook", t_on_raw, t_on_error); break;
    case 4: delete_inbox_placement_test(zb, "t1", t_on_raw, t_on_error); break;
    case 5: send_file(zb, "email_file.csv", 1, send_options, t_on_send, t_on_error); break;
    case 6:
        emails = email_to_validate_vector_init();
        email_to_validate_vector_append(&emails, email);
        validate_email_batch(zb, emails, t_on_batch, t_on_error);
        email_to_validate_vector_free(&emails);
        break;
    case 7: get_file(zb, "fid", (char *)path, t_on_get, t_on_error); break;
    case 8: get_file_streaming(zb, "fid", (char *)path, NULL, t_on_get, t_on_error); break;
    case 9: send_email_finder_file(zb, "email_file.csv", 1, &finder, t_on_send, t_on_error); break;
    case 10: warmup_get_seeds(zbw, t_on_raw, t_on_error); break;
    default: break;
    }
    remove(path);
}

static ZeroBounceWarmup *t_warmup(const char *scheme, int port) {
    static char url[96];
    ZeroBounceWarmup *zbw = new_zero_bounce_warmup_instance();
    zero_bounce_warmup_initialize(zbw, "transport-warmup-key");
    snprintf(url, sizeof(url), "%s://127.0.0.1:%d", scheme, port);
    zero_bounce_warmup_set_base_url(zbw, url);
    return zbw;
}

void test_transport_redirects_are_not_followed(void) {
    TServer server;
    char message[32];
    zero_bounce_set_http_perform_for_test(NULL);
    zero_bounce_set_transport_for_test(5000, 5000, true);
    t_start(&server, 1);
    ZeroBounce *zb = t_client("http", server.port);
    ZeroBounceWarmup *zbw = t_warmup("http", server.port);

    for (int i = 0; i < T_PATHS; i++) {
        t_errors = t_successes = 0;
        t_status = 0;
        snprintf(message, sizeof(message), "request path %d", i);
        t_call_path(i, zb, zbw);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, t_errors, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, t_successes, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(302, (int)t_status, message);
    }
    t_stop(&server);
    /* A followed redirect would have come back for /landed, key and all. */
    TEST_ASSERT_EQUAL_INT(0, server.landed);
    TEST_ASSERT_EQUAL_INT(T_PATHS, server.requests);
    zero_bounce_warmup_free(zbw);
    zero_bounce_set_transport_for_test(0, 0, false);
}

static void t_expect_timeouts(const char *scheme, long timeout_ms, long connect_timeout_ms) {
    TServer server;
    char message[64];
    zero_bounce_set_http_perform_for_test(NULL);
    zero_bounce_set_transport_for_test(timeout_ms, connect_timeout_ms, true);
    t_start(&server, 0);
    ZeroBounce *zb = t_client(scheme, server.port);
    ZeroBounceWarmup *zbw = t_warmup(scheme, server.port);

    for (int i = 0; i < T_PATHS; i++) {
        long started = t_now_ms();
        long elapsed;
        t_errors = t_successes = 0;
        t_call_path(i, zb, zbw);
        elapsed = t_now_ms() - started;
        snprintf(message, sizeof(message), "%s request path %d took %ld ms", scheme, i, elapsed);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, t_errors, message);
        TEST_ASSERT_TRUE_MESSAGE(elapsed < T_STALL_MS / 2, message);
    }
    t_stop(&server);
    zero_bounce_warmup_free(zbw);
    zero_bounce_set_transport_for_test(0, 0, false);
}

void test_transport_every_path_has_a_total_timeout(void) {
    /* Plain http: the connection is made, then the server never answers. */
    t_expect_timeouts("http", 300, 300);
}

void test_transport_every_path_has_a_connect_timeout(void) {
    /* https to a server that never answers the TLS handshake. The handshake is
       part of connecting, so only the connect timeout (300 ms) can end it
       before the server gives up; the total timeout is longer than that. */
    t_expect_timeouts("https", T_STALL_MS * 3, 300);
}

void test_transport_a_downloaded_file_is_saved(void) {
    /* Each getfile, over the real transport, with a Content-Type header and
       no success callback. */
    const char *path = "test/downloads/transport-saved.csv";
    TServer server;
    char message[32];
    zero_bounce_set_http_perform_for_test(NULL);
    zero_bounce_set_transport_for_test(5000, 5000, true);
    t_start(&server, 2);
    ZeroBounce *zb = t_client("http", server.port);
    for (int i = 0; i < 4; i++) {
        FILE *handle;
        char content[64] = {0};
        remove(path);
        t_errors = 0;
        switch (i) {
        case 0: get_file(zb, "fid", (char *)path, NULL, t_on_error); break;
        case 1: email_finder_get_file(zb, "fid", (char *)path, NULL, t_on_error); break;
        case 2: domain_search_get_file(zb, "fid", (char *)path, NULL, t_on_error); break;
        default: get_file_streaming(zb, "fid", (char *)path, NULL, NULL, t_on_error); break;
        }
        snprintf(message, sizeof(message), "getfile %d", i);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, t_errors, message);
        handle = fopen(path, "rb");
        TEST_ASSERT_NOT_NULL_MESSAGE(handle, message);
        TEST_ASSERT_TRUE_MESSAGE(fread(content, 1, sizeof(content) - 1, handle) > 0, message);
        fclose(handle);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("email\nsaved@example.com\n", content, message);
    }
    remove(path);
    t_stop(&server);
    zero_bounce_set_transport_for_test(0, 0, false);
}

/* Each of the six getfile calls, over the real transport. */
static void t_download(int kind, ZeroBounce *zb, const char *path) {
    switch (kind) {
    case 0: get_file(zb, "fid", (char *)path, t_on_get, t_on_error); break;
    case 1: scoring_get_file(zb, "fid", (char *)path, t_on_get, t_on_error); break;
    case 2: get_file_streaming(zb, "fid", (char *)path, NULL, t_on_get, t_on_error); break;
    case 3: scoring_get_file_streaming(zb, "fid", (char *)path, NULL, t_on_get, t_on_error); break;
    case 4: email_finder_get_file(zb, "fid", (char *)path, t_on_get, t_on_error); break;
    default: domain_search_get_file(zb, "fid", (char *)path, t_on_get, t_on_error); break;
    }
}

static void t_expect_no_download(int mode, long status) {
    const char *path = "test/downloads/transport-not-saved.csv";
    TServer server;
    char message[48];
    zero_bounce_set_http_perform_for_test(NULL);
    zero_bounce_set_transport_for_test(5000, 5000, true);
    t_start(&server, mode);
    ZeroBounce *zb = t_client("http", server.port);
    for (int kind = 0; kind < 6; kind++) {
        FILE *handle;
        remove(path);
        t_errors = t_successes = 0;
        t_status = -1;
        t_download(kind, zb, path);
        handle = fopen(path, "rb");
        if (handle) {
            fclose(handle);
            remove(path);
        }
        snprintf(message, sizeof(message), "mode %d, getfile %d", mode, kind);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, t_errors, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, t_successes, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)status, (int)t_status, message);
        TEST_ASSERT_NULL_MESSAGE(handle, message);
    }
    t_stop(&server);
    zero_bounce_set_transport_for_test(0, 0, false);
}

void test_transport_a_json_body_is_not_saved(void) {
    /* {"Message": ...} on an HTTP 200, with a JSON content type and without:
       the content type comes from libcurl here, the streaming download
       included. */
    t_expect_no_download(3, 200);
    t_expect_no_download(4, 200);
}

void test_transport_a_download_cut_short_is_not_saved(void) {
    /* 26 of the 1000 bytes the header promised, then the connection closes. */
    t_expect_no_download(5, 0);
}

void test_transport_a_non_2xx_download_is_not_saved(void) {
    /* A 500 whose body looks like a file: the status alone makes it an error. */
    t_expect_no_download(8, 500);
}

/* The request paths of t_call_path that read a JSON body: all but the two
   downloads (7, 8). */
static const int t_json_paths[] = {0, 1, 2, 3, 4, 5, 6, 9, 10};

void test_transport_a_json_response_cut_short_is_an_error(void) {
    /* A 200 header promising 1000 bytes, then a JSON object that parses on
       its own, then the connection closes. libcurl reports a partial
       transfer; a path that ignored it would return the object as a result. */
    char message[48];
    size_t count = sizeof(t_json_paths) / sizeof(t_json_paths[0]);
    for (int mode = 6; mode <= 7; mode++) {
        TServer server;
        zero_bounce_set_http_perform_for_test(NULL);
        zero_bounce_set_transport_for_test(5000, 5000, true);
        t_start(&server, mode);
        ZeroBounce *zb = t_client("http", server.port);
        ZeroBounceWarmup *zbw = t_warmup("http", server.port);
        for (size_t i = 0; i < count; i++) {
            t_errors = t_successes = 0;
            t_status = -1;
            t_call_path(t_json_paths[i], zb, zbw);
            snprintf(message, sizeof(message), "mode %d, request path %d", mode, t_json_paths[i]);
            if (mode == 6) {
                /* The control: the whole body is a result on every path. */
                TEST_ASSERT_EQUAL_INT_MESSAGE(0, t_errors, message);
                TEST_ASSERT_EQUAL_INT_MESSAGE(1, t_successes, message);
            } else {
                TEST_ASSERT_EQUAL_INT_MESSAGE(1, t_errors, message);
                TEST_ASSERT_EQUAL_INT_MESSAGE(0, t_successes, message);
                TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)t_status, message);
            }
        }
        t_stop(&server);
        zero_bounce_warmup_free(zbw);
        zero_bounce_set_transport_for_test(0, 0, false);
    }
}

/* A body over the size cap keeps its HTTP status in the error, on every
   buffered request path (the streamed download has no cap): a 500 stays a
   500, and a 200 is an API error with status 200, not a status-0 transport
   failure. */
void test_transport_a_body_over_the_size_cap_keeps_its_status(void) {
    static const int capped_paths[] = {0, 1, 2, 3, 4, 5, 6, 7, 9, 10};
    char message[48];
    for (int mode = 10; mode <= 11; mode++) {
        TServer server;
        long expected = mode == 10 ? 500 : 200;
        zero_bounce_set_http_perform_for_test(NULL);
        zero_bounce_set_transport_for_test(5000, 5000, true);
        zero_bounce_set_response_cap_for_test(64);
        t_start(&server, mode);
        ZeroBounce *zb = t_client("http", server.port);
        ZeroBounceWarmup *zbw = t_warmup("http", server.port);
        for (size_t i = 0; i < sizeof(capped_paths) / sizeof(capped_paths[0]); i++) {
            t_errors = t_successes = 0;
            t_status = -1;
            t_call_path(capped_paths[i], zb, zbw);
            snprintf(message, sizeof(message), "mode %d, request path %d", mode, capped_paths[i]);
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, t_errors, message);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, t_successes, message);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)expected, (int)t_status, message);
        }
        t_stop(&server);
        zero_bounce_warmup_free(zbw);
        zero_bounce_set_response_cap_for_test(0);
        zero_bounce_set_transport_for_test(0, 0, false);
    }
}

/* The names in dir, other than . and .., joined by spaces. */
static void t_list_dir(const char *dir, char *out, size_t size) {
    DIR *handle = opendir(dir);
    struct dirent *entry;
    size_t used = 0;
    out[0] = '\0';
    if (!handle) {
        return;
    }
    while ((entry = readdir(handle)) != NULL) {
        int written;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        written = snprintf(out + used, size - used, "%s%s", used ? " " : "", entry->d_name);
        if (written < 0 || (size_t)written >= size - used) {
            break;
        }
        used += (size_t)written;
    }
    closedir(handle);
}

/* Empties dir (one level), so a file left by an earlier run cannot count. */
static void t_clear_dir(const char *dir) {
    DIR *handle = opendir(dir);
    struct dirent *entry;
    char path[512];
    if (!handle) {
        return;
    }
    while ((entry = readdir(handle)) != NULL) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
            snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
            remove(path);
        }
    }
    closedir(handle);
}

void test_transport_a_failed_download_keeps_the_file_already_there(void) {
    /* A file from an earlier download is at the target path. A download that
       fails -- refused, a JSON error, a 500, cut short before or after the
       streaming download starts writing -- leaves it as it was and leaves no
       other file behind. The streaming download used to delete it. With no
       earlier file, nothing is left at all. */
    const char *dir = "test/downloads/transport-keep";
    const char *path = "test/downloads/transport-keep/result.csv";
    const char *previous = "email\nprevious@example.com\n";
    const int modes[] = {-1, 3, 5, 8, 9};
    char message[384];
    char listing[256];
    mkdir("test/downloads", 0777);
    mkdir(dir, 0777);
    t_clear_dir(dir);
    for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++) {
        TServer server;
        ZeroBounce *zb;
        zero_bounce_set_http_perform_for_test(NULL);
        zero_bounce_set_transport_for_test(5000, 5000, true);
        if (modes[m] < 0) {
            /* Nothing listens on port 1: the connection is refused. */
            zb = t_client("http", 1);
        } else {
            t_start(&server, modes[m]);
            zb = t_client("http", server.port);
        }
        for (int seeded = 0; seeded < 2; seeded++) {
            for (int kind = 0; kind < 6; kind++) {
                char content[128] = {0};
                FILE *handle;
                remove(path);
                if (seeded) {
                    handle = fopen(path, "wb");
                    TEST_ASSERT_NOT_NULL(handle);
                    fputs(previous, handle);
                    fclose(handle);
                }
                t_errors = t_successes = 0;
                t_download(kind, zb, path);
                handle = fopen(path, "rb");
                if (handle) {
                    TEST_ASSERT_TRUE(fread(content, 1, sizeof(content) - 1, handle) <= sizeof(content) - 1);
                    fclose(handle);
                }
                t_list_dir(dir, listing, sizeof(listing));
                remove(path);
                snprintf(message, sizeof(message), "mode %d, getfile %d, %s; left: [%s]",
                         modes[m], kind, seeded ? "earlier file" : "no earlier file", listing);
                TEST_ASSERT_EQUAL_INT_MESSAGE(1, t_errors, message);
                TEST_ASSERT_EQUAL_INT_MESSAGE(0, t_successes, message);
                if (seeded) {
                    TEST_ASSERT_NOT_NULL_MESSAGE(handle, message);
                    TEST_ASSERT_EQUAL_STRING_MESSAGE(previous, content, message);
                    TEST_ASSERT_EQUAL_STRING_MESSAGE("result.csv", listing, message);
                } else {
                    TEST_ASSERT_NULL_MESSAGE(handle, message);
                    TEST_ASSERT_EQUAL_STRING_MESSAGE("", listing, message);
                }
            }
        }
        if (modes[m] >= 0) {
            t_stop(&server);
        }
        zero_bounce_set_transport_for_test(0, 0, false);
    }
    rmdir(dir);
}

void test_transport_a_download_replaces_the_file_already_there(void) {
    const char *dir = "test/downloads/transport-replace";
    const char *path = "test/downloads/transport-replace/result.csv";
    char message[320];
    char listing[256];
    TServer server;
    mkdir("test/downloads", 0777);
    mkdir(dir, 0777);
    t_clear_dir(dir);
    zero_bounce_set_http_perform_for_test(NULL);
    zero_bounce_set_transport_for_test(5000, 5000, true);
    t_start(&server, 2);
    ZeroBounce *zb = t_client("http", server.port);
    for (int kind = 0; kind < 6; kind++) {
        char content[128] = {0};
        FILE *handle = fopen(path, "wb");
        TEST_ASSERT_NOT_NULL(handle);
        fputs("email\nprevious@example.com\nand a longer line than the new file has\n", handle);
        fclose(handle);
        t_errors = t_successes = 0;
        t_download(kind, zb, path);
        handle = fopen(path, "rb");
        TEST_ASSERT_NOT_NULL(handle);
        TEST_ASSERT_TRUE(fread(content, 1, sizeof(content) - 1, handle) > 0);
        fclose(handle);
        t_list_dir(dir, listing, sizeof(listing));
        snprintf(message, sizeof(message), "getfile %d; left: [%s]", kind, listing);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, t_errors, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, t_successes, message);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("email\nsaved@example.com\n", content, message);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("result.csv", listing, message);
    }
    remove(path);
    rmdir(dir);
    t_stop(&server);
    zero_bounce_set_transport_for_test(0, 0, false);
}

void test_transport_a_column_number_of_any_int_is_sent_whole(void) {
    /* The multipart fields were formatted into a 10-byte buffer: INT_MAX
       needs 11 bytes and INT_MIN 12. */
    TServer server;
    SendFileOptions options = new_send_file_options();
    zero_bounce_set_http_perform_for_test(NULL);
    zero_bounce_set_transport_for_test(5000, 5000, true);
    t_start(&server, 2);
    ZeroBounce *zb = t_client("http", server.port);

    options.first_name_column = INT_MAX;
    options.ip_address_column = INT_MAX;
    send_file(zb, "email_file.csv", INT_MIN, options, t_on_send, t_on_error);
    pthread_mutex_lock(&server.mu);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(server.last_request,
        "name=\"email_address_column\"\r\n\r\n-2147483648\r\n"), "email_address_column");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(server.last_request,
        "name=\"first_name_column\"\r\n\r\n2147483647\r\n"), "first_name_column");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(server.last_request,
        "name=\"ip_address_column\"\r\n\r\n2147483647\r\n"), "ip_address_column");
    pthread_mutex_unlock(&server.mu);
    t_stop(&server);
    zero_bounce_set_transport_for_test(0, 0, false);
}

/* The multipart parts a request carried, in order: "name=value" joined by
   '&', the file part by its name only. */
static void t_parts(const char *request, char *out, size_t size) {
    const char *at = request;
    size_t used = 0;
    out[0] = '\0';
    while ((at = strstr(at, "Content-Disposition: form-data; name=\"")) != NULL) {
        const char *name = at + 38;
        const char *name_end = strchr(name, '"');
        const char *value;
        const char *value_end;
        if (!name_end) {
            break;
        }
        value = strstr(name_end, "\r\n\r\n");
        value_end = value ? strstr(value + 4, "\r\n--") : NULL;
        if (!value || !value_end) {
            break;
        }
        value += 4;
        if (strncmp(name, "file\"", 5) == 0) {
            used += (size_t)snprintf(out + used, size - used, "%sfile", used ? "&" : "");
        } else {
            used += (size_t)snprintf(out + used, size - used, "%s%.*s=%.*s", used ? "&" : "",
                                     (int)(name_end - name), name, (int)(value_end - value), value);
        }
        if (used >= size) {
            break;
        }
        at = value_end;
    }
}

static void t_expect_parts(int scoring, SendFileOptions options, const char *expected) {
    TServer server;
    char parts[2048];
    zero_bounce_set_http_perform_for_test(NULL);
    zero_bounce_set_transport_for_test(5000, 5000, true);
    t_start(&server, 2);
    ZeroBounce *zb = t_client("http", server.port);
    t_errors = t_successes = 0;
    if (scoring) {
        scoring_send_file(zb, "email_file.csv", 3, options, t_on_send, t_on_error);
    } else {
        send_file(zb, "email_file.csv", 3, options, t_on_send, t_on_error);
    }
    pthread_mutex_lock(&server.mu);
    t_parts(server.last_request, parts, sizeof(parts));
    pthread_mutex_unlock(&server.mu);
    t_stop(&server);
    zero_bounce_set_transport_for_test(0, 0, false);
    TEST_ASSERT_EQUAL_STRING(expected, parts);
}

/* The multipart sendfile actually sends, field names and values, through the
   real libcurl transport: renaming a part or sending one the caller did not
   set fails here. */
void test_transport_send_file_sends_exactly_these_parts(void) {
    SendFileOptions options = new_send_file_options();
    t_expect_parts(0, options,
        "api_key=transport-key&file&email_address_column=3&has_header_row=true&remove_duplicate=true");
    t_expect_parts(1, options,
        "api_key=transport-key&file&email_address_column=3&has_header_row=true&remove_duplicate=true");

    options.return_url = "https://example.com/hook";
    options.first_name_column = 4;
    options.last_name_column = 5;
    options.gender_column = 6;
    options.ip_address_column = 7;
    options.has_header_row = false;
    options.remove_duplicate = false;
    options.allow_phase_2_is_set = true;
    options.allow_phase_2 = true;
    t_expect_parts(0, options,
        "api_key=transport-key&file&email_address_column=3&first_name_column=4"
        "&last_name_column=5&gender_column=6&ip_address_column=7"
        "&return_url=https://example.com/hook&has_header_row=false"
        "&remove_duplicate=false&allow_phase_2=true");
    /* Scoring takes no name, gender, IP or phase-2 fields. */
    options.remove_duplicate = true;
    t_expect_parts(1, options,
        "api_key=transport-key&file&email_address_column=3"
        "&return_url=https://example.com/hook&has_header_row=false&remove_duplicate=true");
}

/* The defaults every request gets: 120 s in total and 30 s to connect, read
   from where zb_set_transport_options takes them. Run first, before any test
   changes them, and again after restoring them. */
void test_transport_default_timeouts_are_120_s_and_30_s(void) {
    long timeout_ms = -1;
    long connect_timeout_ms = -1;
    zb_transport_timeouts(&timeout_ms, &connect_timeout_ms);
    TEST_ASSERT_EQUAL_INT(120000, (int)timeout_ms);
    TEST_ASSERT_EQUAL_INT(30000, (int)connect_timeout_ms);
    zero_bounce_set_transport_for_test(300, 400, true);
    zb_transport_timeouts(&timeout_ms, &connect_timeout_ms);
    TEST_ASSERT_EQUAL_INT(300, (int)timeout_ms);
    TEST_ASSERT_EQUAL_INT(400, (int)connect_timeout_ms);
    zero_bounce_set_transport_for_test(0, 0, false);
    zb_transport_timeouts(&timeout_ms, &connect_timeout_ms);
    TEST_ASSERT_EQUAL_INT(120000, (int)timeout_ms);
    TEST_ASSERT_EQUAL_INT(30000, (int)connect_timeout_ms);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_transport_default_timeouts_are_120_s_and_30_s);
    RUN_TEST(test_transport_a_downloaded_file_is_saved);
    RUN_TEST(test_transport_redirects_are_not_followed);
    RUN_TEST(test_transport_every_path_has_a_total_timeout);
    RUN_TEST(test_transport_every_path_has_a_connect_timeout);
    RUN_TEST(test_transport_a_json_body_is_not_saved);
    RUN_TEST(test_transport_a_download_cut_short_is_not_saved);
    RUN_TEST(test_transport_a_non_2xx_download_is_not_saved);
    RUN_TEST(test_transport_a_json_response_cut_short_is_an_error);
    RUN_TEST(test_transport_a_body_over_the_size_cap_keeps_its_status);
    RUN_TEST(test_transport_a_failed_download_keeps_the_file_already_there);
    RUN_TEST(test_transport_a_download_replaces_the_file_already_there);
    RUN_TEST(test_transport_a_column_number_of_any_int_is_sent_whole);
    RUN_TEST(test_transport_send_file_sends_exactly_these_parts);
    return UNITY_END();
}
