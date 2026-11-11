/*
 * The real libcurl transport against a local socket server: redirects are not
 * followed, and the total and connect timeouts are set on every request path.
 * These fail if CURLOPT_FOLLOWLOCATION, CURLOPT_TIMEOUT_MS or
 * CURLOPT_CONNECTTIMEOUT_MS is dropped from any path.
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
    /* 1: answer 302 to /landed. 2: answer 200 with a CSV body.
       0: accept and never answer. */
    int redirect;
    volatile int stop;
    pthread_t thread;
    pthread_mutex_t mu;
    int requests;
    int landed;
} TServer;

static long t_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void t_answer_redirect(TServer *server, int fd) {
    char request[16384];
    size_t used = 0;
    long deadline = t_now_ms() + 5000;
    char *header_end = NULL;
    size_t content_length = 0;
    const char *response = server->redirect == 2
        ? "HTTP/1.1 200 OK\r\nContent-Type: text/csv\r\nContent-Length: 24\r\nConnection: close\r\n\r\n"
          "email\nsaved@example.com\n"
        : "HTTP/1.1 302 Found\r\nLocation: /landed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

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
    if (strstr(request, " /landed")) {
        server->landed++;
    }
    pthread_mutex_unlock(&server->mu);
    send(fd, response, strlen(response), 0);
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

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_transport_a_downloaded_file_is_saved);
    RUN_TEST(test_transport_redirects_are_not_followed);
    RUN_TEST(test_transport_every_path_has_a_total_timeout);
    RUN_TEST(test_transport_every_path_has_a_connect_timeout);
    return UNITY_END();
}
