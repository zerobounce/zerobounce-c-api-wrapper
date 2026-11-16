/*
 * Offline tests for the Warmup API client.
 *
 * Every request is answered by the stub installed with
 * zero_bounce_set_http_perform_for_test; nothing here reaches the live API.
 * The assertions are on the url, the method, the body and the one header that
 * actually went out.
 */

#include <dirent.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include "unity.h"
#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/ZeroBounceWarmup.h"
#include "ZeroBounce/zb_internal.h"

/* ZeroBounceTest.c: an absolute path in the downloads directory. */
char *zb_test_dl(const char *name);

#define WARMUP_ROOT "https://warmup-api.zerobounce.net/api/v1/warmup"

static char w_url[2048];
static char w_method[16];
static char w_body[4096];
static char w_header[512];
static char w_raw[4096];
static char w_error[1024];
static int w_requests;
static int w_error_called;
static int w_success_called;
static long w_status;
static long w_next_status = 200;
static const char *w_next_body = "{\"ok\":true}";
static const char *w_next_type = "application/json";
/* The length of w_next_body when it holds a NUL byte; (size_t)-1 for strlen. */
static size_t w_next_len = (size_t)-1;

static void w_on_raw(const char *json_response) {
    w_success_called = 1;
    snprintf(w_raw, sizeof(w_raw), "%s", json_response ? json_response : "");
}

static void w_on_error(ZBErrorResponse error) {
    size_t i;
    size_t used = 0;
    w_error_called = 1;
    w_status = error.status_code;
    w_error[0] = '\0';
    for (i = 0; i < error.errors.size; i++) {
        const char *part = error.errors.data[i] ? error.errors.data[i] : "";
        int written = snprintf(w_error + used, sizeof(w_error) - used, "%s%s", used ? " " : "", part);
        if (written < 0 || (size_t)written >= sizeof(w_error) - used) {
            break;
        }
        used += (size_t)written;
    }
}

static int warmup_perform(memory *response_data, long *http_code, char **content_type_out) {
    size_t len;
    const char *url = zero_bounce_test_url();
    const char *body = zero_bounce_test_body();
    const char *header = zero_bounce_test_extra_header();

    w_requests++;
    snprintf(w_url, sizeof(w_url), "%s", url ? url : "");
    snprintf(w_method, sizeof(w_method), "%s", zero_bounce_test_method());
    snprintf(w_body, sizeof(w_body), "%s", body ? body : "");
    snprintf(w_header, sizeof(w_header), "%s", header ? header : "");

    len = w_next_len == (size_t)-1 ? strlen(w_next_body) : w_next_len;
    response_data->response = malloc(len + 1);
    memcpy(response_data->response, w_next_body, len);
    response_data->response[len] = '\0';
    response_data->size = len;
    *http_code = w_next_status;
    if (content_type_out) {
        *content_type_out = w_next_type ? strdup(w_next_type) : NULL;
    }
    return 1;
}

static void w_reset(void) {
    zero_bounce_set_http_perform_for_test(warmup_perform);
    w_url[0] = w_method[0] = w_body[0] = w_header[0] = w_raw[0] = w_error[0] = '\0';
    w_requests = 0;
    w_error_called = 0;
    w_success_called = 0;
    w_status = 0;
    w_next_status = 200;
    w_next_body = "{\"ok\":true}";
    w_next_type = "application/json";
    w_next_len = (size_t)-1;
}

static ZeroBounceWarmup *w_client(const char *key) {
    ZeroBounceWarmup *zbw = new_zero_bounce_warmup_instance();
    zero_bounce_warmup_initialize(zbw, key);
    return zbw;
}

void test_warmup_api_key_header_and_own_host(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");

    warmup_list_sending_accounts(zbw, w_on_raw, w_on_error);

    TEST_ASSERT_EQUAL_STRING("GET", w_method);
    /* Its own host and base path, not the validation API's. */
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/sending-accounts", w_url);
    /* The Warmup API authenticates with an API-Key header; the validation
       api_key never appears. */
    TEST_ASSERT_EQUAL_STRING("API-Key: warm-key", w_header);
    TEST_ASSERT_NULL(strstr(w_url, "api_key"));
    TEST_ASSERT_TRUE(w_success_called);

    zero_bounce_warmup_free(zbw);
}

void test_warmup_uninitialized_sends_nothing(void) {
    w_reset();
    ZeroBounceWarmup *zbw = new_zero_bounce_warmup_instance();

    warmup_get_seeds(zbw, w_on_raw, w_on_error);

    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "zero_bounce_warmup_initialize"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    /* A key with surrounding whitespace or control characters would be
       quoted, unredacted, in a transport error. */
    zero_bounce_warmup_initialize(zbw, " warm-key");
    TEST_ASSERT_NULL(zbw->api_key);
    zero_bounce_warmup_initialize(zbw, "warm-key");
    TEST_ASSERT_EQUAL_STRING("warm-key", zbw->api_key);

    zero_bounce_warmup_free(zbw);
}

void test_warmup_reads_use_the_documented_paths(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");

    warmup_get_sending_account(zbw, "sender@example.com", w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/sending-account?from_address=sender%40example.com", w_url);

    warmup_get_engagement_rules(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/engagement-rules", w_url);

    warmup_get_seeds(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/seeds", w_url);

    warmup_get_seed_count(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/seed-count", w_url);

    warmup_get_progress_today(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/progress-today", w_url);

    warmup_get_sent_today(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/sent-today", w_url);

    warmup_get_emails_per_day(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/emails-per-day", w_url);

    warmup_get_statistics(zbw, "sender@example.com", w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/statistics?from_address=sender%40example.com", w_url);

    warmup_get_statistics_detailed(zbw, "sender@example.com", w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/statistics-detailed?c_from=sender%40example.com", w_url);

    warmup_get_from_address_statistics(zbw, "sender@example.com", w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(
        WARMUP_ROOT "/from-address-statistics?from_address=sender%40example.com", w_url);

    /* c_from is one path segment here, so a slash must stay encoded. */
    warmup_get_custom_engagement(zbw, "a b/c", w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/custom-engagement/a%20b%2Fc", w_url);

    TEST_ASSERT_FALSE(w_error_called);
    zero_bounce_warmup_free(zbw);
}

void test_warmup_empty_successes(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");

    /* 204 and an empty 200 are the documented empty successes. */
    w_next_status = 204;
    w_next_body = "";
    warmup_get_sent_today(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_success_called);
    TEST_ASSERT_EQUAL_STRING("", w_raw);
    TEST_ASSERT_FALSE(w_error_called);

    w_next_status = 200;
    w_next_body = "";
    warmup_get_sent_today(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("", w_raw);
    TEST_ASSERT_FALSE(w_error_called);

    zero_bounce_warmup_free(zbw);
}

void test_warmup_add_sending_account(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");
    ZBWarmupAddSendingAccountOptions options = new_warmup_add_sending_account_options();
    ZBWarmupEmailContent content = new_warmup_email_content();
    ZBWarmupManagedSending sending = new_warmup_managed_sending();

    options.engagement_rule_id = 2;
    options.ignore_conflict = 1;
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_SELF_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("POST", w_method);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/sending-accounts", w_url);
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"onboarding_model\": \"self_managed\""));
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"engagement_rule_id\": 2"));
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"ignore_conflict\": true"));

    /* A self-managed account needs an engagement rule, and the ZB managed
       extras belong to the other model. */
    w_reset();
    options = new_warmup_add_sending_account_options();
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_SELF_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "engagement_rule_id"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    w_reset();
    options.engagement_rule_id = 0;
    options.email_content = &content;
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_SELF_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "zb_managed"));

    w_reset();
    warmup_add_sending_account(zbw, "sender@example.com", "whatever", NULL, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "onboarding_model"));

    /* ZB managed takes the content and sending blocks. */
    w_reset();
    content.company_name = "Example";
    content.types_of_emails = "newsletter, marketing";
    sending.weekdays_only = 1;
    sending.timezone_offset = 3;
    sending.warmup_start_date = "2026-11-02";
    options = new_warmup_add_sending_account_options();
    options.email_content = &content;
    options.managed_sending = &sending;
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_FALSE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"company_name\": \"Example\""));
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"types_of_emails\": [ \"newsletter\", \"marketing\" ]"));
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"timezone_offset\": 3"));
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"warmup_start_date\": \"2026-11-02\""));
    TEST_ASSERT_NULL(strstr(w_body, "engagement_rule_id"));

    w_reset();
    content.types_of_emails = "postcards";
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "types_of_emails"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    w_reset();
    content.types_of_emails = "marketing";
    sending.timezone_offset = 15;
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "timezone_offset"));

    w_reset();
    sending.timezone_offset = 3;
    sending.warmup_start_date = "02/11/2026";
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "warmup_start_date"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    zero_bounce_warmup_free(zbw);
}

void test_warmup_update_and_delete_sending_account(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");

    warmup_update_sending_account(zbw, "sender@example.com", 1, 4, true, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("PUT", w_method);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/sending-accounts", w_url);
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"team_id\": 1"));
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"status_id\": 4"));
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"auto_enable\": true"));

    warmup_delete_sending_account(zbw, "sender@example.com", w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("DELETE", w_method);
    TEST_ASSERT_EQUAL_STRING(
        WARMUP_ROOT "/sending-accounts?from_address=sender%40example.com", w_url);

    /* Despite the path, nothing about SMTP credentials is stored. */
    warmup_change_from_address(zbw, "old@example.com", "new@example.com", w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("PUT", w_method);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/smtp-credentials", w_url);
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"old_from_address\": \"old@example.com\""));

    w_reset();
    warmup_update_sending_account(zbw, "sender@example.com", 9, 4, false, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "engagement_rule_id"));

    w_reset();
    warmup_update_sending_account(zbw, "sender@example.com", 1, 9, false, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "status_id"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    zero_bounce_warmup_free(zbw);
}

void test_warmup_list_from_addresses(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");
    ZBWarmupListFromAddressesOptions options = new_warmup_list_from_addresses_options();

    warmup_list_from_addresses(zbw, NULL, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/from-addresses", w_url);

    options.search_from = "sender@example.com";
    options.offset = 20;
    options.limit = 50;
    warmup_list_from_addresses(zbw, &options, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(
        WARMUP_ROOT "/from-addresses?search_from=sender%40example.com&offset=20&limit=50", w_url);

    w_reset();
    options.search_from = "a b";
    warmup_list_from_addresses(zbw, &options, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "search_from"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    zero_bounce_warmup_free(zbw);
}

void test_warmup_engagement_rules(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");

    warmup_set_engagement_rule(zbw, "sender@example.com", 3, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("PUT", w_method);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/engagement-rule?c_from=sender%40example.com", w_url);
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"rule_id\": 3"));

    warmup_create_custom_engagement(zbw, "sender@example.com", 10, 20, 30, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("POST", w_method);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/custom-engagement", w_url);
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"reply_pct\": 10"));

    warmup_update_custom_engagement(zbw, "sender@example.com", 10, 20, 30, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("PUT", w_method);

    warmup_delete_custom_engagement(zbw, "sender@example.com", 2, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("DELETE", w_method);
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"engagement_rule_id\": 2"));

    /* The ranges are checked before anything is sent. */
    w_reset();
    warmup_create_custom_engagement(zbw, "sender@example.com", 4, 20, 10, w_on_raw, w_on_error);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "reply_pct"));

    w_reset();
    warmup_create_custom_engagement(zbw, "sender@example.com", 10, 41, 10, w_on_raw, w_on_error);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "important_pct"));

    /* click_pct is capped at important_pct x 1.75 as well as at 70. */
    w_reset();
    warmup_create_custom_engagement(zbw, "sender@example.com", 10, 20, 36, w_on_raw, w_on_error);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "click_pct"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    w_reset();
    warmup_create_custom_engagement(zbw, "sender@example.com", 10, 20, 35, w_on_raw, w_on_error);
    TEST_ASSERT_FALSE(w_error_called);
    TEST_ASSERT_EQUAL_INT(1, w_requests);

    /* Falling back to the custom rule itself is not a standard rule. */
    w_reset();
    warmup_delete_custom_engagement(zbw, "sender@example.com", 0, w_on_raw, w_on_error);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "engagement_rule_id"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    zero_bounce_warmup_free(zbw);
}

void test_warmup_statistics_per_day_dates(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");

    warmup_get_statistics_per_day(zbw, "sender@example.com", "2026-10-01", "2026-10-07",
                                  w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(
        WARMUP_ROOT "/statistics-per-day?from_address=sender%40example.com"
        "&start_date=2026-10-01&end_date=2026-10-07", w_url);

    w_reset();
    warmup_get_statistics_per_day(zbw, "sender@example.com", "10/01/2026", "2026-10-07",
                                  w_on_raw, w_on_error);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "start_date"));

    w_reset();
    warmup_get_statistics_per_day(zbw, "sender@example.com", "2026-10-08", "2026-10-07",
                                  w_on_raw, w_on_error);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "later than"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    zero_bounce_warmup_free(zbw);
}

void test_warmup_check_domain_dmarc_needs_an_email(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");

    warmup_check_domain_dmarc(zbw, "sender@example.com", "selector1", w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(
        WARMUP_ROOT "/check-domain-dmarc?from_address=sender%40example.com"
        "&dkim_selector=selector1", w_url);

    warmup_check_domain_dmarc(zbw, "sender@example.com", NULL, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(
        WARMUP_ROOT "/check-domain-dmarc?from_address=sender%40example.com", w_url);

    /* LIVE: the docs describe a domain, but a bare domain is answered with
       "Please provide a valid email address". */
    w_reset();
    warmup_check_domain_dmarc(zbw, "example.com", NULL, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "email address"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    zero_bounce_warmup_free(zbw);
}

/* The SMTP credentials never come back in an error, in any form an echo can
   take: raw, URL-encoded, or JSON-escaped with '/' escaped or not. The body
   is not JSON, so nothing decodes the escaped forms first. */
void test_warmup_smtp_credentials_are_masked_in_every_form(void) {
    static const char *const leaks[] = {
        "Pa/ss\"w%rd+", "Pa%2Fss%22w%25rd%2B", "Pa\\/ss\\\"w%rd+", "Pa/ss\\\"w%rd+",
        "us\"er/n+%", "us%22er%2Fn%2B%25", "us\\\"er\\/n+%", "us\\\"er/n+%",
    };
    char body[1024];
    size_t i;
    ZeroBounceWarmup *zbw;
    w_reset();
    zbw = w_client("warm-key");
    snprintf(body, sizeof(body), "SMTP login failed:");
    for (i = 0; i < sizeof(leaks) / sizeof(leaks[0]); i++) {
        size_t used = strlen(body);
        snprintf(body + used, sizeof(body) - used, " [%s]", leaks[i]);
    }
    w_next_status = 500;
    w_next_type = "text/plain";
    w_next_body = body;
    warmup_test_smtp_configuration(zbw, "sender@example.com", "us\"er/n+%", "Pa/ss\"w%rd+",
                                   "smtp.example.com", 587, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_EQUAL_INT(500, (int)w_status);
    for (i = 0; i < sizeof(leaks) / sizeof(leaks[0]); i++) {
        TEST_ASSERT_NULL_MESSAGE(strstr(w_error, leaks[i]), w_error);
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_error, "SMTP login failed: [***]"), w_error);
    w_next_type = "application/json";
    zero_bounce_warmup_free(zbw);
}

/* The SMTP password with a space and every character JSON or URL encoding
   changes, quoted in every form a server echoing the request could use. */
void test_warmup_smtp_password_is_masked_in_every_encoded_form(void) {
    static const char *const forms[] = {
        "a b\"c/d+e%f&g=h<i>j'k\\l",
        "a b\\\"c\\/d+e%f&g=h<i>j'k\\\\l",
        "a b\\\"c/d+e%f&g=h<i>j'k\\\\l",
        "a%20b%22c%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5Cl",
        "a+b%22c%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5Cl",
        "a%20b%5C%22c%5C%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5C%5Cl",
        "a+b%5C%22c%5C%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5C%5Cl",
        "a%20b%5C%22c%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5C%5Cl",
        "a+b%5C%22c%2Fd%2Be%25f%26g%3Dh%3Ci%3Ej%27k%5C%5Cl",
    };
    char body[2048];
    char expected[512];
    size_t i;
    ZeroBounceWarmup *zbw;
    w_reset();
    zbw = w_client("warm-key");
    snprintf(body, sizeof(body), "SMTP login failed:");
    snprintf(expected, sizeof(expected), "SMTP login failed:");
    for (i = 0; i < sizeof(forms) / sizeof(forms[0]); i++) {
        size_t used = strlen(body);
        snprintf(body + used, sizeof(body) - used, " [%s]", forms[i]);
        used = strlen(expected);
        snprintf(expected + used, sizeof(expected) - used, " [***]");
    }
    w_next_status = 500;
    w_next_type = "text/plain";
    w_next_body = body;
    warmup_test_smtp_configuration(zbw, "sender@example.com", "smtp-user", "a b\"c/d+e%f&g=h<i>j'k\\l",
                                   "smtp.example.com", 587, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_EQUAL_INT(500, (int)w_status);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_error, expected), w_error);
    w_next_type = "application/json";
    zero_bounce_warmup_free(zbw);
}

void test_warmup_test_smtp_configuration(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");

    warmup_test_smtp_configuration(zbw, "sender@example.com", "sender@example.com", "sekrit",
                                   "smtp.example.com", 587, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("POST", w_method);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/test-smtp-configuration", w_url);
    TEST_ASSERT_NOT_NULL(strstr(w_body, "\"port\": 587"));

    /* LIVE: a failed SMTP login is answered with HTTP 500, and the password
       must not travel out in the error. */
    w_reset();
    w_next_status = 500;
    w_next_body = "{\"detail\":\"login failed for sekrit\"}";
    warmup_test_smtp_configuration(zbw, "sender@example.com", "sender@example.com", "sekrit",
                                   "smtp.example.com", 587, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_EQUAL_INT(500, (int)w_status);
    TEST_ASSERT_NULL(strstr(w_error, "sekrit"));
    TEST_ASSERT_NOT_NULL(strstr(w_error, "***"));

    w_reset();
    warmup_test_smtp_configuration(zbw, "sender@example.com", "sender@example.com", "sekrit",
                                   "smtp.example.com", 0, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "port"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    zero_bounce_warmup_free(zbw);
}

void test_warmup_csv_downloads(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");

    w_next_type = "text/csv";
    w_next_body = "seed,provider\na@example.com,gmail\n";
    warmup_download_seeds(zbw, NULL, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/seeds/download", w_url);
    TEST_ASSERT_EQUAL_INT(0, strncmp(w_raw, "seed,provider", 13));

    w_next_body = "date,sent\n2026-10-01,10\n";
    warmup_download_statistics(zbw, "sender@example.com", NULL, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(
        WARMUP_ROOT "/statistics/download?c_from=sender%40example.com", w_url);

    /* An error comes back as JSON, so it is not a file. */
    w_reset();
    w_next_body = "{\"detail\":\"nothing here\"}";
    warmup_download_seeds(zbw, NULL, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "nothing here"));

    zero_bounce_warmup_free(zbw);
}

void test_warmup_blank_arguments_are_rejected(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");

    warmup_get_sending_account(zbw, "  ", w_on_raw, w_on_error);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "from_address"));

    w_reset();
    warmup_get_statistics_detailed(zbw, "  ", w_on_raw, w_on_error);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "c_from"));

    /* A path segment of "." or ".." would walk the request out of the
       endpoint it was meant for. */
    w_reset();
    warmup_get_custom_engagement(zbw, "..", w_on_raw, w_on_error);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "c_from"));

    w_reset();
    warmup_change_from_address(zbw, "old@example.com", " ", w_on_raw, w_on_error);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "new_from_address"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    zero_bounce_warmup_free(zbw);
}

void test_warmup_a_rejected_key_replaces_the_previous_one(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");
    /* Carrying on with the old key would send requests the caller believes
       go out under the new one. */
    zero_bounce_warmup_initialize(zbw, "warm-key\n");
    TEST_ASSERT_NULL(zbw->api_key);
    warmup_get_seeds(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "surrounding whitespace or control characters"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);

    w_reset();
    zero_bounce_warmup_initialize(zbw, "warm-key");
    warmup_get_seeds(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_FALSE(w_error_called);
    TEST_ASSERT_EQUAL_INT(1, w_requests);
    zero_bounce_warmup_free(zbw);
}

void test_warmup_base_url_trailing_slashes_are_dropped(void) {
    w_reset();
    ZeroBounceWarmup *zbw = w_client("warm-key");
    zero_bounce_warmup_set_base_url(zbw, "https://warmup-api.zerobounce.net//");
    warmup_get_seeds(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING(WARMUP_ROOT "/seeds", w_url);

    /* And http is refused before anything is sent. */
    w_reset();
    zero_bounce_warmup_set_base_url(zbw, "http://warmup-api.zerobounce.net");
    warmup_get_seeds(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_EQUAL_INT(0, w_requests);
    zero_bounce_warmup_free(zbw);
}

void test_warmup_a_download_that_cannot_be_saved_is_not_a_success(void) {
    const char *blocker = zb_test_dl("warmup-not-a-directory");
    FILE *handle = fopen(blocker, "wb");
    TEST_ASSERT_NOT_NULL(handle);
    fputs("x", handle);
    fclose(handle);

    w_reset();
    w_next_type = "text/csv";
    w_next_body = "seed,provider\nseed@example.com,gmail\n";
    ZeroBounceWarmup *zbw = w_client("warm-key");
    warmup_download_seeds(zbw, zb_test_dl("warmup-not-a-directory/seeds.csv"), w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_FALSE(w_success_called);
    zero_bounce_warmup_free(zbw);
    remove(blocker);
}

void test_warmup_a_failed_write_keeps_the_file_already_at_the_path(void) {
    /* D4: the CSV downloads opened download_path with "wb", so a write that
       failed (a file size limit, a full disk) destroyed a file already there.
       They now write beside it and move the file into place when complete. */
    const char *path = zb_test_dl("warmup-keep-existing.csv");
    const char *part_prefix = "warmup-keep-existing.csv.";
    struct rlimit saved;
    struct rlimit tight;
    void (*previous)(int);
    char kept[2][16];
    int parts = 0;
    FILE *handle;

    mkdir(zb_test_dl(""), 0777);
    ZeroBounceWarmup *zbw = w_client("warm-key");
    /* Partial files left by an earlier run do not count. */
    DIR *stale = opendir(zb_test_dl(""));
    TEST_ASSERT_NOT_NULL(stale);
    struct dirent *old_entry;
    while ((old_entry = readdir(stale)) != NULL) {
        if (strncmp(old_entry->d_name, part_prefix, strlen(part_prefix)) == 0) {
            char old_path[512];
            snprintf(old_path, sizeof(old_path), "%s%s", zb_test_dl(""), old_entry->d_name);
            remove(old_path);
        }
    }
    closedir(stale);
    TEST_ASSERT_EQUAL_INT(0, getrlimit(RLIMIT_FSIZE, &saved));
    tight = saved;
    tight.rlim_cur = 8;
    for (int call = 0; call < 2; call++) {
        w_reset();
        w_next_type = "text/csv";
        w_next_body = "seed,provider\nseed@example.com,gmail\n";
        handle = fopen(path, "wb");
        TEST_ASSERT_NOT_NULL(handle);
        fputs("OLD", handle);
        fclose(handle);
        previous = signal(SIGXFSZ, SIG_IGN);
        TEST_ASSERT_EQUAL_INT(0, setrlimit(RLIMIT_FSIZE, &tight));
        if (call == 0) {
            warmup_download_seeds(zbw, (char *)path, w_on_raw, w_on_error);
        } else {
            warmup_download_statistics(zbw, "seed@example.com", path, w_on_raw, w_on_error);
        }
        setrlimit(RLIMIT_FSIZE, &saved);
        signal(SIGXFSZ, previous);
        TEST_ASSERT_TRUE(w_error_called);
        TEST_ASSERT_FALSE(w_success_called);
        handle = fopen(path, "rb");
        kept[call][0] = '\0';
        if (handle) {
            size_t n = fread(kept[call], 1, sizeof(kept[call]) - 1, handle);
            kept[call][n] = '\0';
            fclose(handle);
        }
        TEST_ASSERT_EQUAL_STRING("OLD", kept[call]);
        DIR *dir = opendir(zb_test_dl(""));
        TEST_ASSERT_NOT_NULL(dir);
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, part_prefix, strlen(part_prefix)) == 0) {
                parts++;
            }
        }
        closedir(dir);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, parts, "a partial file was left beside download_path");
    }
    /* Without the limit the download replaces it. */
    w_reset();
    w_next_type = "text/csv";
    w_next_body = "seed,provider\nseed@example.com,gmail\n";
    warmup_download_seeds(zbw, (char *)path, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_success_called);
    handle = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(handle);
    char replaced[64] = {0};
    fread(replaced, 1, sizeof(replaced) - 1, handle);
    fclose(handle);
    TEST_ASSERT_EQUAL_STRING("seed,provider\nseed@example.com,gmail\n", replaced);
    remove(path);
    zero_bounce_warmup_free(zbw);
}

void test_warmup_a_short_write_is_an_error_and_leaves_no_partial_file(void) {
    /* A file size limit stops the write part-way, as a full disk would. The
       error was reported, but the first 8 bytes were left on disk looking
       like a result. */
    const char *path = zb_test_dl("warmup-short-write.csv");
    struct rlimit saved;
    struct rlimit tight;
    void (*previous)(int);
    FILE *handle;
    int left;

    w_reset();
    w_next_type = "text/csv";
    w_next_body = "seed,provider\nseed@example.com,gmail\n";
    ZeroBounceWarmup *zbw = w_client("warm-key");
    mkdir(zb_test_dl(""), 0777);
    remove(path);
    TEST_ASSERT_EQUAL_INT(0, getrlimit(RLIMIT_FSIZE, &saved));
    tight = saved;
    tight.rlim_cur = 8;
    previous = signal(SIGXFSZ, SIG_IGN);
    TEST_ASSERT_EQUAL_INT(0, setrlimit(RLIMIT_FSIZE, &tight));
    /* No assertion until the limit is lifted: a failed one would leave it in
       place, and a test log that is a file is under the same limit. */
    warmup_download_seeds(zbw, (char *)path, w_on_raw, w_on_error);
    handle = fopen(path, "rb");
    left = handle != NULL;
    if (handle) {
        fclose(handle);
        remove(path);
    }
    setrlimit(RLIMIT_FSIZE, &saved);
    signal(SIGXFSZ, previous);

    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_FALSE(w_success_called);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_error, "Could not write"), w_error);
    TEST_ASSERT_FALSE_MESSAGE(left, "a partial file was left at the download path");
    zero_bounce_warmup_free(zbw);
}

/* Every field https://www.zerobounce.net/docs/warmup-api/adding-a-sending-account
   documents, with the exact body. */
void test_warmup_add_sending_account_sends_every_documented_field(void) {
    ZeroBounceWarmup *zbw;
    ZBWarmupAddSendingAccountOptions options = new_warmup_add_sending_account_options();
    ZBWarmupEmailContent content = new_warmup_email_content();
    ZBWarmupManagedSending sending = new_warmup_managed_sending();
    w_reset();
    zbw = w_client("warm-key");
    content.company_name = "Example";
    content.company_website = "https://example.com";
    content.company_description = "We sell \"things\"";
    content.types_of_emails = "transactional,newsletter,marketing";
    content.example_email = "Hello,\nWelcome.";
    sending.weekdays_only = 0;
    sending.timezone = "Asia/Kolkata";
    sending.timezone_offset_hours = 5.5;
    sending.timezone_offset_hours_set = true;
    sending.warmup_start_date = "2026-11-02";
    options.ignore_conflict = 1;
    options.email_content = &content;
    options.managed_sending = &sending;
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_FALSE_MESSAGE(w_error_called, w_error);
    TEST_ASSERT_EQUAL_STRING(
        "{ \"from_address\": \"sender@example.com\", \"onboarding_model\": \"zb_managed\", "
        "\"ignore_conflict\": true, \"email_content\": { \"company_name\": \"Example\", "
        "\"company_website\": \"https:\\/\\/example.com\", "
        "\"company_description\": \"We sell \\\"things\\\"\", "
        "\"types_of_emails\": [ \"transactional\", \"newsletter\", \"marketing\" ], "
        "\"example_email\": \"Hello,\\nWelcome.\" }, "
        "\"managed_sending\": { \"weekdays_only\": false, \"timezone\": \"Asia\\/Kolkata\", "
        "\"timezone_offset\": 5.5, \"warmup_start_date\": \"2026-11-02\" } }",
        w_body);
    zero_bounce_warmup_free(zbw);
}

/* timezone_offset is hours from UTC, -11.0 to 14.0, fractional zones
   included, sent through timezone_offset_hours; only a real number. */
void test_warmup_timezone_offset_may_be_fractional(void) {
    static const double accepted[] = {5.5, 3.0, -11.0, 14.0, -9.5, 5.75, 0.0};
    static const char *const sent[] = {"5.5", "3", "-11", "14", "-9.5", "5.75", "0"};
    const double rejected[] = {-11.5, 14.5, 15, -12, NAN, INFINITY, -INFINITY};
    ZeroBounceWarmup *zbw;
    ZBWarmupAddSendingAccountOptions options = new_warmup_add_sending_account_options();
    ZBWarmupManagedSending sending = new_warmup_managed_sending();
    char expected[160];
    size_t i;
    w_reset();
    zbw = w_client("warm-key");
    options.managed_sending = &sending;
    for (i = 0; i < sizeof(accepted) / sizeof(accepted[0]); i++) {
        w_reset();
        sending.timezone_offset_hours = accepted[i];
        sending.timezone_offset_hours_set = true;
        warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                                   w_on_raw, w_on_error);
        TEST_ASSERT_FALSE_MESSAGE(w_error_called, w_error);
        snprintf(expected, sizeof expected, "\"managed_sending\": { \"timezone_offset\": %s }", sent[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_body, expected), w_body);
    }
    for (i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
        w_reset();
        sending.timezone_offset_hours = rejected[i];
        sending.timezone_offset_hours_set = true;
        warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                                   w_on_raw, w_on_error);
        TEST_ASSERT_TRUE(w_error_called);
        TEST_ASSERT_NOT_NULL(strstr(w_error, "timezone_offset"));
        TEST_ASSERT_EQUAL_INT(0, w_requests);
    }
    zero_bounce_warmup_free(zbw);
}

/* Code written for 3.0.x keeps compiling and behaving the same: the
   deprecated timezone_offset is still an int (a positional initialiser, an
   int pointer, %d), -99 omits it, and a whole number goes out as before.
   timezone_offset_hours, appended at the end, wins when it is set. */
void test_warmup_timezone_offset_keeps_its_3_0_x_type(void) {
    ZBWarmupManagedSending positional = {1, "Europe/Bucharest", 3, "2026-11-02"};
    ZBWarmupManagedSending sending = new_warmup_managed_sending();
    ZBWarmupAddSendingAccountOptions options = new_warmup_add_sending_account_options();
    int *offset = &positional.timezone_offset;
    char printed[16];
    ZeroBounceWarmup *zbw;
    snprintf(printed, sizeof printed, "%d", *offset);
    TEST_ASSERT_EQUAL_STRING("3", printed);
    TEST_ASSERT_EQUAL_INT(-99, sending.timezone_offset);
    TEST_ASSERT_FALSE(sending.timezone_offset_hours_set);
    TEST_ASSERT_FALSE(positional.timezone_offset_hours_set);

    w_reset();
    zbw = w_client("warm-key");
    options.managed_sending = &positional;
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_FALSE_MESSAGE(w_error_called, w_error);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_body,
        "\"managed_sending\": { \"weekdays_only\": true, \"timezone\": \"Europe\\/Bucharest\", "
        "\"timezone_offset\": 3, \"warmup_start_date\": \"2026-11-02\" }"), w_body);

    /* -99 (the default) leaves it out. */
    w_reset();
    options.managed_sending = &sending;
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_FALSE_MESSAGE(w_error_called, w_error);
    TEST_ASSERT_NULL_MESSAGE(strstr(w_body, "timezone_offset"), w_body);

    /* Both set: the new field wins. */
    w_reset();
    sending.timezone_offset = 3;
    sending.timezone_offset_hours = 5.75;
    sending.timezone_offset_hours_set = true;
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_FALSE_MESSAGE(w_error_called, w_error);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_body, "\"managed_sending\": { \"timezone_offset\": 5.75 }"), w_body);

    /* The deprecated int keeps its range check. */
    w_reset();
    sending.timezone_offset_hours_set = false;
    sending.timezone_offset = -12;
    warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_ZB_MANAGED, &options,
                               w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_NOT_NULL(strstr(w_error, "timezone_offset"));
    TEST_ASSERT_EQUAL_INT(0, w_requests);
    zero_bounce_warmup_free(zbw);
}

/* D1-D3: a JSON body (or content type), an HTML page (by body or content
   type) and an empty body are not a CSV download: an error with the status,
   no success and no file. */
void test_warmup_csv_downloads_reject_every_non_file_answer(void) {
    static const char *const bodies[] = {
        "{\"message\":\"No seeds\"}", "  {\"detail\":\"nope\"}", " \n [{\"error\":\"nope\"}]", "",
        "<!DOCTYPE html><html><title>Blocked</title></html>", "Blocked", "seed,provider\n",
    };
    static const char *const types[] = {
        "text/csv", "text/csv", "text/csv", "text/csv", "text/csv", "text/html; charset=utf-8",
        "application/json",
    };
    const char *path = zb_test_dl("warmup-nonfile.csv");
    ZeroBounceWarmup *zbw;
    size_t i;
    int call;
    char message[200];
    mkdir(zb_test_dl(""), 0777);
    zbw = w_client("warm-key");
    for (i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
        for (call = 0; call < 2; call++) {
            w_reset();
            remove(path);
            w_next_body = bodies[i];
            w_next_type = types[i];
            if (call == 0) {
                warmup_download_seeds(zbw, path, w_on_raw, w_on_error);
            } else {
                warmup_download_statistics(zbw, "sender@example.com", path, w_on_raw, w_on_error);
            }
            snprintf(message, sizeof message, "%d: %s (%s)", call, bodies[i], types[i]);
            TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
            TEST_ASSERT_FALSE_MESSAGE(w_success_called, message);
            TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)w_status, message);
            TEST_ASSERT_NULL_MESSAGE(fopen(path, "rb"), message);
        }
    }
    zero_bounce_warmup_free(zbw);
}

/* An HTML page behind more than 2000 bytes of padding is not a CSV, and a JSON
   error behind a byte order mark or spaces is the API's message. */
void test_warmup_csv_downloads_see_through_padding(void) {
    static const size_t pads[] = {2500, 4095, 5000};
    static const char *const json_bodies[] = {
        "\xEF\xBB\xBF{\"Message\":\"File not found\"}",
        "  \xC2\xA0{\"Message\":\"File not found\"}",
    };
    const char *path = zb_test_dl("warmup-audit3.csv");
    ZeroBounceWarmup *zbw;
    char message[200];
    mkdir(zb_test_dl(""), 0777);
    zbw = w_client("warm-key");
    for (size_t k = 0; k < sizeof(pads) / sizeof(pads[0]); k++) {
        static const char tail[] = "<html></html>";
        size_t len = pads[k] + sizeof(tail) - 1;
        char *body = malloc(len + 1);
        memset(body, ' ', pads[k]);
        memcpy(body + pads[k], tail, sizeof(tail));
        for (int call = 0; call < 2; call++) {
            FILE *handle;
            char content[16] = {0};
            size_t size;
            snprintf(message, sizeof message, "%s: %zu spaces then <html>", call ? "statistics" : "seeds", pads[k]);
            remove(path);
            handle = fopen(path, "wb");
            fwrite("ORIG", 1, 4, handle);
            fclose(handle);
            w_reset();
            w_next_body = body;
            w_next_len = len;
            w_next_type = "text/csv";
            if (call == 0) {
                warmup_download_seeds(zbw, path, w_on_raw, w_on_error);
            } else {
                warmup_download_statistics(zbw, "sender@example.com", path, w_on_raw, w_on_error);
            }
            TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
            TEST_ASSERT_FALSE_MESSAGE(w_success_called, message);
            TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)w_status, message);
            handle = fopen(path, "rb");
            TEST_ASSERT_NOT_NULL_MESSAGE(handle, message);
            size = fread(content, 1, sizeof(content), handle);
            fclose(handle);
            TEST_ASSERT_EQUAL_UINT_MESSAGE(4, size, message);
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE("ORIG", content, 4, message);
        }
        free(body);
    }
    for (size_t k = 0; k < sizeof(json_bodies) / sizeof(json_bodies[0]); k++) {
        for (int call = 0; call < 2; call++) {
            snprintf(message, sizeof message, "%s: json body %zu", call ? "statistics" : "seeds", k);
            remove(path);
            w_reset();
            w_next_body = json_bodies[k];
            w_next_type = "text/csv";
            if (call == 0) {
                warmup_download_seeds(zbw, path, w_on_raw, w_on_error);
            } else {
                warmup_download_statistics(zbw, "sender@example.com", path, w_on_raw, w_on_error);
            }
            TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
            TEST_ASSERT_FALSE_MESSAGE(w_success_called, message);
            TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)w_status, message);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("File not found", w_error, message);
        }
    }
    remove(path);
    zero_bounce_warmup_free(zbw);
}

/* -- every call ------------------------------------------------------------ */

int offline_count_swept_request_functions(const char *header_path, int (*is_swept)(const char *name));

#define W_LEAK_KEY "leak-W4rm/x+y\"q%z"
#define W_LEAK_KEY_ESCAPED "leak-W4rm%2Fx%2By%22q%25z"
#define W_LEAK_KEY_JSON "leak-W4rm\\/x+y\\\"q%z"
#define W_LEAK_KEY_JSON_PLAIN "leak-W4rm/x+y\\\"q%z"
/* Whether text carries the Warmup key in any form. */
static int w_has_key(const char *text) {
    return strstr(text, W_LEAK_KEY) || strstr(text, W_LEAK_KEY_ESCAPED) ||
        strstr(text, W_LEAK_KEY_JSON) || strstr(text, W_LEAK_KEY_JSON_PLAIN);
}
#define W_SWEPT_MAX 64
static char w_swept[W_SWEPT_MAX][96];
static int w_swept_count;
static void (*w_sweep_check)(const char *call);

static void w_record_swept(const char *call) {
    size_t len = strcspn(call, "(");
    int i;
    for (i = 0; i < w_swept_count; i++) {
        if (strlen(w_swept[i]) == len && strncmp(w_swept[i], call, len) == 0) {
            return;
        }
    }
    if (w_swept_count < W_SWEPT_MAX && len < sizeof(w_swept[0])) {
        memcpy(w_swept[w_swept_count], call, len);
        w_swept[w_swept_count][len] = '\0';
        w_swept_count++;
    }
}

static int w_was_swept(const char *name) {
    int i;
    for (i = 0; i < w_swept_count; i++) {
        if (strcmp(w_swept[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The answer every request gets stays as set by the test; only the request
   record is cleared. */
#define W_SWEEP(call) do { \
        w_record_swept(#call); \
        w_url[0] = w_error[0] = w_raw[0] = '\0'; \
        w_requests = 0; \
        w_error_called = w_success_called = 0; \
        w_status = 0; \
        call; \
        w_sweep_check(#call); \
    } while (0)

/* Runs every public Warmup request function once, with valid arguments. */
static void w_run_every_call(ZeroBounceWarmup *zbw, const char *path) {
    ZBWarmupAddSendingAccountOptions add = new_warmup_add_sending_account_options();
    add.engagement_rule_id = 2;
    W_SWEEP(warmup_list_sending_accounts(zbw, w_on_raw, w_on_error));
    W_SWEEP(warmup_get_sending_account(zbw, "sender@example.com", w_on_raw, w_on_error));
    W_SWEEP(warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_SELF_MANAGED, &add,
                                       w_on_raw, w_on_error));
    W_SWEEP(warmup_update_sending_account(zbw, "sender@example.com", 1, 4, true, w_on_raw, w_on_error));
    W_SWEEP(warmup_delete_sending_account(zbw, "sender@example.com", w_on_raw, w_on_error));
    W_SWEEP(warmup_change_from_address(zbw, "old@example.com", "new@example.com", w_on_raw, w_on_error));
    W_SWEEP(warmup_list_from_addresses(zbw, NULL, w_on_raw, w_on_error));
    W_SWEEP(warmup_get_engagement_rules(zbw, w_on_raw, w_on_error));
    W_SWEEP(warmup_set_engagement_rule(zbw, "sender@example.com", 3, w_on_raw, w_on_error));
    W_SWEEP(warmup_get_custom_engagement(zbw, "sender@example.com", w_on_raw, w_on_error));
    W_SWEEP(warmup_create_custom_engagement(zbw, "sender@example.com", 10, 20, 30, w_on_raw, w_on_error));
    W_SWEEP(warmup_update_custom_engagement(zbw, "sender@example.com", 10, 20, 30, w_on_raw, w_on_error));
    W_SWEEP(warmup_delete_custom_engagement(zbw, "sender@example.com", 2, w_on_raw, w_on_error));
    W_SWEEP(warmup_get_seeds(zbw, w_on_raw, w_on_error));
    W_SWEEP(warmup_download_seeds(zbw, path, w_on_raw, w_on_error));
    W_SWEEP(warmup_get_seed_count(zbw, w_on_raw, w_on_error));
    W_SWEEP(warmup_get_progress_today(zbw, w_on_raw, w_on_error));
    W_SWEEP(warmup_get_sent_today(zbw, w_on_raw, w_on_error));
    W_SWEEP(warmup_get_emails_per_day(zbw, w_on_raw, w_on_error));
    W_SWEEP(warmup_get_statistics(zbw, "sender@example.com", w_on_raw, w_on_error));
    W_SWEEP(warmup_get_statistics_per_day(zbw, "sender@example.com", "2026-10-01", "2026-10-07",
                                          w_on_raw, w_on_error));
    W_SWEEP(warmup_get_statistics_detailed(zbw, "sender@example.com", w_on_raw, w_on_error));
    W_SWEEP(warmup_download_statistics(zbw, "sender@example.com", path, w_on_raw, w_on_error));
    W_SWEEP(warmup_get_from_address_statistics(zbw, "sender@example.com", w_on_raw, w_on_error));
    W_SWEEP(warmup_check_domain_dmarc(zbw, "sender@example.com", "selector1", w_on_raw, w_on_error));
    W_SWEEP(warmup_test_smtp_configuration(zbw, "sender@example.com", "sender@example.com", "sekrit",
                                           "smtp.example.com", 587, w_on_raw, w_on_error));
}

static void w_check_no_key_in_error(const char *call) {
    char message[1300];
    snprintf(message, sizeof message, "%s: %s", call, w_error);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, w_requests, message);
    TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
    TEST_ASSERT_FALSE_MESSAGE(w_success_called, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, (int)w_status, message);
    TEST_ASSERT_FALSE_MESSAGE(w_has_key(w_error), message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_error, "REDACTED"), message);
}

/* Standard section 3: the key is never in an error, also when the Warmup API
   (or a proxy in front of it) quotes it, raw, URL-encoded or JSON-escaped,
   in an error. */
void test_warmup_no_error_carries_the_key(void) {
    static const char *const bodies[] = {
        "{\"error\":\"Bad key " W_LEAK_KEY_JSON "\"}",
        "{\"success\":false,\"message\":\"Bad key " W_LEAK_KEY_JSON_PLAIN "\"}",
        "Bad Request: GET https://warmup-api.zerobounce.net/api/v1/warmup/seeds?key=" W_LEAK_KEY_ESCAPED,
        "Rejected key " W_LEAK_KEY,
        "Bad Request: {\"key\":\"" W_LEAK_KEY_JSON "\",\"k\":\"" W_LEAK_KEY_JSON_PLAIN "\"}",
    };
    const char *path = zb_test_dl("warmup-leak.csv");
    size_t i;
    ZeroBounceWarmup *zbw;
    w_reset();
    zbw = w_client(W_LEAK_KEY);
    w_sweep_check = w_check_no_key_in_error;
    w_swept_count = 0;
    for (i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
        w_next_status = 400;
        w_next_body = bodies[i];
        w_run_every_call(zbw, path);
    }
    TEST_ASSERT_NULL(fopen(path, "rb"));
    zero_bounce_warmup_free(zbw);
}

/* Every byte of in as %XX with lower-case hex: a spelling the SDK never
   generates, so only the decode check sees the secret in it. */
static void w_percent_every_byte(const char *in, char *out, size_t size) {
    size_t used = 0;
    for (; *in && used + 4 < size; in++) {
        used += (size_t)snprintf(out + used, size - used, "%%%02x", (unsigned char)*in);
    }
    out[used] = '\0';
}

static void w_check_secret_omitted(const char *call) {
    char message[1300];
    snprintf(message, sizeof message, "%s: %s", call, w_error);
    TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(400, (int)w_status, message);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_error, "omitted because it contained a credential"), message);
}

/* S4/S7: a %00 before the Warmup key or the SMTP password, fully
   percent-encoded, does not end the decode check. */
void test_warmup_a_nul_in_the_body_does_not_end_the_redaction_check(void) {
    const char *password = "a b\"c/d+e%f&g=h<i>j'k\\l";
    const char *path = zb_test_dl("warmup-nul.csv");
    char encoded[512];
    char body[700];
    ZeroBounceWarmup *zbw;
    w_reset();
    zbw = w_client(W_LEAK_KEY);
    w_percent_every_byte(W_LEAK_KEY, encoded, sizeof(encoded));
    snprintf(body, sizeof(body), "bad %%00 then %s", encoded);
    w_sweep_check = w_check_secret_omitted;
    w_next_status = 400;
    w_next_type = "text/plain";
    w_next_body = body;
    w_run_every_call(zbw, path);
    TEST_ASSERT_NULL(fopen(path, "rb"));

    w_reset();
    w_percent_every_byte(password, encoded, sizeof(encoded));
    snprintf(body, sizeof(body), "SMTP login failed: bad %%00 then %s", encoded);
    w_next_status = 500;
    w_next_type = "text/plain";
    w_next_body = body;
    warmup_test_smtp_configuration(zbw, "sender@example.com", "smtp-user", password,
                                   "smtp.example.com", 587, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_EQUAL_INT(500, (int)w_status);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_error, "omitted because it contained a credential"), w_error);
    w_next_type = "application/json";
    zero_bounce_warmup_free(zbw);
}

/* An error a 200 can raise carries no key either: the CSV downloads reject a
   JSON answer after the request itself succeeded. The JSON calls hand a 200
   JSON body over as their result, so for them only an error is checked. */
static void w_check_no_key_if_error(const char *call) {
    char message[1300];
    snprintf(message, sizeof message, "%s: %s", call, w_error);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, w_requests, message);
    if (strstr(call, "warmup_download_")) {
        TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
        TEST_ASSERT_FALSE_MESSAGE(w_success_called, message);
    }
    if (w_error_called) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)w_status, message);
        TEST_ASSERT_FALSE_MESSAGE(w_has_key(w_error), message);
        if (strstr(call, "smtp")) {
            TEST_ASSERT_NULL_MESSAGE(strstr(w_error, "sekrit"), message);
        }
    }
}

void test_warmup_no_error_on_a_200_carries_the_key(void) {
    static const char *const bodies[] = {
        "{\"error\":\"Invalid API key " W_LEAK_KEY_JSON " sekrit\"}",
        "{\"success\":false,\"message\":\"Bad key " W_LEAK_KEY_JSON " " W_LEAK_KEY_ESCAPED " sekrit\"}",
        "{\"message\":\"Invalid API-Key " W_LEAK_KEY_JSON_PLAIN " " W_LEAK_KEY_ESCAPED " sekrit\"}",
        "{\"detail\":\"Invalid API-Key " W_LEAK_KEY_JSON " " W_LEAK_KEY_ESCAPED " sekrit\"}",
    };
    const char *path = zb_test_dl("warmup-leak200.csv");
    size_t i;
    ZeroBounceWarmup *zbw;
    w_reset();
    zbw = w_client(W_LEAK_KEY);
    w_sweep_check = w_check_no_key_if_error;
    w_swept_count = 0;
    for (i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
        w_next_status = 200;
        w_next_body = bodies[i];
        w_run_every_call(zbw, path);
    }
    TEST_ASSERT_NULL(fopen(path, "rb"));
    zero_bounce_warmup_free(zbw);
}

/* Each call got an error with status 200 and wrote nothing. */
static void w_check_error_200(const char *call) {
    char message[1300];
    snprintf(message, sizeof message, "%s: %s", call, w_error);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, w_requests, message);
    TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
    TEST_ASSERT_FALSE_MESSAGE(w_success_called, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)w_status, message);
}

/* A 200 labelled text/html is a proxy's or a portal's page, not the API's
   answer, even when its body parses as JSON: every call reports an error with
   the status, and the CSV downloads write nothing. */
void test_warmup_a_text_html_200_is_an_error_on_every_call(void) {
    const char *path = zb_test_dl("warmup-html-ct.csv");
    ZeroBounceWarmup *zbw;
    w_reset();
    zbw = w_client("warm-key");
    w_sweep_check = w_check_error_200;
    w_swept_count = 0;
    w_next_status = 200;
    w_next_type = "text/html; charset=UTF-8";
    w_next_body = "{\"a\":1}";
    w_run_every_call(zbw, path);
    TEST_ASSERT_NULL(fopen(path, "rb"));
    w_next_type = "application/json";
    zero_bounce_warmup_free(zbw);
}

/* E7: a 2xx JSON call whose body is not JSON is an error with status 200.
   The CSV downloads take a non-JSON body as the file, so for them only an
   application/json label must make it an error. */
static const char *w_e7_type;
static void w_check_non_json_200(const char *call) {
    char message[1300];
    snprintf(message, sizeof message, "%s (%s): %s", call, w_e7_type, w_error);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, w_requests, message);
    if (strstr(call, "warmup_download_") && strcmp(w_e7_type, "application/json") != 0) {
        return;
    }
    TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
    TEST_ASSERT_FALSE_MESSAGE(w_success_called, message);
    TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)w_status, message);
}

void test_warmup_a_non_json_200_is_an_error_on_every_json_call(void) {
    static const char *const bodies[] = {
        "not json", "hello", "email,status\na@example.com,valid\n", "{\"a\":1", "[1,2", "{\"a\":1} trailing",
    };
    static const char *const types[] = {"application/json", "text/plain", "text/csv"};
    const char *path = zb_test_dl("warmup-non-json.csv");
    size_t i;
    size_t t;
    ZeroBounceWarmup *zbw;
    w_reset();
    zbw = w_client("warm-key");
    w_sweep_check = w_check_non_json_200;
    w_swept_count = 0;
    for (t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
        for (i = 0; i < sizeof(bodies) / sizeof(bodies[0]); i++) {
            w_next_status = 200;
            w_next_type = types[t];
            w_e7_type = types[t];
            w_next_body = bodies[i];
            w_run_every_call(zbw, path);
            remove(path);
        }
    }
    /* A JSON body after whitespace and an empty (zero-byte) body stay
       results; a body of only whitespace is not empty, and is an error. */
    w_reset();
    w_next_body = " \n {\"ok\":true} \n";
    warmup_get_seed_count(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_FALSE_MESSAGE(w_error_called, w_error);
    TEST_ASSERT_TRUE(w_success_called);
    w_reset();
    w_next_body = "";
    warmup_get_seed_count(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_FALSE_MESSAGE(w_error_called, w_error);
    TEST_ASSERT_TRUE(w_success_called);
    w_reset();
    w_next_body = "  ";
    warmup_get_seed_count(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_TRUE(w_error_called);
    TEST_ASSERT_FALSE(w_success_called);
    TEST_ASSERT_EQUAL_INT(200, (int)w_status);
    {
        /* Nested deeper than the strict-JSON scanner's limit: an error with
           status 200, not a crash. */
        char *deep = malloc(200001);
        memset(deep, '[', 200000);
        deep[200000] = '\0';
        w_reset();
        w_next_body = deep;
        warmup_get_seed_count(zbw, w_on_raw, w_on_error);
        TEST_ASSERT_TRUE(w_error_called);
        TEST_ASSERT_FALSE(w_success_called);
        TEST_ASSERT_EQUAL_INT(200, (int)w_status);
        free(deep);
    }
    zero_bounce_warmup_free(zbw);
}

/* Each call sent one request. */
static void w_check_sent(const char *call) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, w_requests, call);
}

/* Fails for any request function in ZeroBounceWarmup.h that w_run_every_call
   does not run. */
void test_warmup_every_request_function_is_in_the_sweep(void) {
    char header[1024];
    size_t len = strlen(ZB_PUBLIC_HEADER) - strlen("ZeroBounce.h");
    int declared;
    ZeroBounceWarmup *zbw;
    TEST_ASSERT_TRUE(len + strlen("ZeroBounceWarmup.h") < sizeof(header));
    memcpy(header, ZB_PUBLIC_HEADER, len);
    strcpy(header + len, "ZeroBounceWarmup.h");

    w_reset();
    zbw = w_client("warm-key");
    w_sweep_check = w_check_sent;
    w_swept_count = 0;
    w_run_every_call(zbw, zb_test_dl("warmup-sweep.csv"));
    remove(zb_test_dl("warmup-sweep.csv"));
    zero_bounce_warmup_free(zbw);

    declared = offline_count_swept_request_functions(header, w_was_swept);
    TEST_ASSERT_GREATER_THAN_INT(20, declared);
    TEST_ASSERT_EQUAL_INT(declared, w_swept_count);
}

/* E7: every Warmup call against every strict-JSON corpus body on an HTTP 200
   application/json (test/fixtures/strict-json-corpus.tsv, see
   OfflineApiTest.c). A REJECT body is an error with status 200 and an ACCEPT
   body the result, on every JSON call; the CSV downloads take any
   application/json answer as an error. An empty or blank 2xx stays a Warmup
   JSON call's documented empty success. */
int offline_corpus_count(void);
const char *offline_corpus_body(int index, size_t *len, int *accept, const char **line);
static int w_corpus_accept;
static size_t w_corpus_len;
static const char *w_corpus_line;
static const char *w_corpus_body;

static void w_check_corpus_body(const char *call) {
    char message[1600];
    int download = strstr(call, "warmup_download_") != NULL;
    /* Only a zero-byte body is the documented empty success; whitespace is
       not empty. */
    int empty = w_corpus_len == 0;
    snprintf(message, sizeof message, "%s with %s: %s", call, w_corpus_line, w_error);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, w_requests, message);
    if (download || (!w_corpus_accept && !empty)) {
        TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
        TEST_ASSERT_FALSE_MESSAGE(w_success_called, message);
        TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)w_status, message);
    } else {
        TEST_ASSERT_FALSE_MESSAGE(w_error_called, message);
        TEST_ASSERT_TRUE_MESSAGE(w_success_called, message);
    }
}

void test_warmup_every_json_call_takes_only_strict_json(void) {
    const char *path = zb_test_dl("warmup-corpus.csv");
    int count = offline_corpus_count();
    int i;
    ZeroBounceWarmup *zbw;
    w_reset();
    zbw = w_client("warm-key");
    w_sweep_check = w_check_corpus_body;
    w_swept_count = 0;
    for (i = 0; i < count; i++) {
        w_corpus_body = offline_corpus_body(i, &w_corpus_len, &w_corpus_accept, &w_corpus_line);
        w_next_status = 200;
        w_next_type = "application/json";
        w_next_body = w_corpus_body;
        w_next_len = w_corpus_len;
        w_run_every_call(zbw, path);
    }
    TEST_ASSERT_NULL(fopen(path, "rb"));
    w_reset();
    zero_bounce_warmup_free(zbw);
}

/* S4/S7: whatever composition of up to three URL and JSON encodings an error
   body uses to quote the Warmup key or the SMTP password, no decoded view of
   the error holds it. Seeded, so a failure repeats. */
char *offline_random_spelling(const char *secret, unsigned *seed, char *desc, size_t desc_len);
char *offline_encode(const char *in, int how);
int offline_secret_readable(const char *text, const char *secret, int layers);
#define W_FUZZ_SECRET "wz a\"b/c\\d+e%f&g=h<i>j'k"
static char w_fuzz_desc[200];
static const char *w_fuzz_secret;

static void w_check_fuzzed_echo(const char *call) {
    char message[1500];
    snprintf(message, sizeof message, "%s, encodings [%s], status %ld: %s",
             call, w_fuzz_desc, w_next_status, w_error);
    TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
    TEST_ASSERT_FALSE_MESSAGE(offline_secret_readable(w_error, w_fuzz_secret, 3), message);
}

static void w_fuzz_body(char *body, size_t size, const char *spelling, int round) {
    char *quoted = offline_encode(spelling, 2);
    /* A JSON error and a text one, each with a 4xx: a Warmup 200 is the
       result, whatever its body says. */
    w_next_status = 400;
    if (round % 2) {
        snprintf(body, size, "{\"error\":\"Invalid API-Key %s\"}", quoted);
        w_next_type = "application/json";
    } else {
        snprintf(body, size, "Bad Request: [%s] end", spelling);
        w_next_type = "text/plain";
    }
    free(quoted);
}

void test_warmup_redaction_is_closed_under_encoding(void) {
    const char *path = zb_test_dl("warmup-fuzz.csv");
    unsigned seed = 20261113u;
    static char body[4096];
    int round;
    ZeroBounceWarmup *zbw;
    w_reset();
    zbw = w_client(W_FUZZ_SECRET);
    w_sweep_check = w_check_fuzzed_echo;
    w_swept_count = 0;
    w_fuzz_secret = W_FUZZ_SECRET;
    for (round = 0; round < 40; round++) {
        char *spelling = offline_random_spelling(W_FUZZ_SECRET, &seed, w_fuzz_desc, sizeof(w_fuzz_desc));
        w_fuzz_body(body, sizeof(body), spelling, round);
        w_next_body = body;
        w_run_every_call(zbw, path);
        free(spelling);
    }
    TEST_ASSERT_NULL(fopen(path, "rb"));
    zero_bounce_warmup_free(zbw);
    /* The SMTP password and username of test_smtp_configuration. */
    zbw = w_client("warm-key");
    for (round = 0; round < 200; round++) {
        char *spelling;
        w_fuzz_secret = round % 3 ? "pw a\"b/c\\d+e%f&g=h<i>j'k" : "us a\"b/c\\d+e%f&g=h<i>j'k";
        spelling = offline_random_spelling(w_fuzz_secret, &seed, w_fuzz_desc, sizeof(w_fuzz_desc));
        w_fuzz_body(body, sizeof(body), spelling, round);
        w_next_body = body;
        W_SWEEP(warmup_test_smtp_configuration(zbw, "sender@example.com", "us a\"b/c\\d+e%f&g=h<i>j'k",
                                               "pw a\"b/c\\d+e%f&g=h<i>j'k", "smtp.example.com", 587,
                                               w_on_raw, w_on_error));
        free(spelling);
    }
    w_reset();
    zero_bounce_warmup_free(zbw);
}

/* ---- depth-first audit 1 (2026-11-16): the behaviour fixes ------------- */

typedef struct {
    const char *name;
    const char *body;
    size_t len;
    const char *type;
    int is_file;
} WAuditCase;

#define W_B(text) (text), sizeof(text) - 1

static const WAuditCase w_audit_cases[] = {
    {"BOM then {", W_B("\xEF\xBB\xBF" "{\"a\":1}"), "text/csv", 0},
    {"BOM then { (not JSON)", W_B("\xEF\xBB\xBF" "{not json"), "text/csv", 0},
    {"NBSP then {", W_B("\xC2\xA0" "{\"a\":1}"), "text/csv", 0},
    {"BOM then [", W_B("\xEF\xBB\xBF" "[1]"), "text/csv", 0},
    {"bare [", W_B("[\"error\"]"), "text/csv", 0},
    {"only BOMs", W_B("\xEF\xBB\xBF\xEF\xBB\xBF"), "text/csv", 0},
    {"only whitespace", W_B(" \t\r\n  "), "text/csv", 0},
    {"+json content type", W_B("email\na@b.c\n"), "application/problem+json", 0},
    {"a csv", W_B("email\na@b.c\n"), "text/csv", 1},
    {"a csv with a BOM", W_B("\xEF\xBB\xBF" "email\na@b.c\n"), "text/csv", 1},
    {"a csv with a NUL", W_B("a,b\0c,d\n"), "text/csv", 1},
};

#define W_OLD_CONTENT "OLD-CONTENT\n"

void test_warmup_a_download_that_is_not_a_file_is_an_error_and_writes_nothing(void) {
    char *path = zb_test_dl("warmup-audit.csv");
    char message[200];
    ZeroBounceWarmup *zbw = w_client("warm-audit-key");
    for (size_t k = 0; k < sizeof(w_audit_cases) / sizeof(w_audit_cases[0]); k++) {
        const WAuditCase *test_case = &w_audit_cases[k];
        for (int which = 0; which < 2; which++) {
            FILE *handle;
            char content[256];
            size_t size;
            snprintf(message, sizeof(message), "%s: %s (%s)", which ? "statistics" : "seeds", test_case->name,
                     test_case->type);
            remove(path);
            handle = fopen(path, "wb");
            fwrite(W_OLD_CONTENT, 1, sizeof(W_OLD_CONTENT) - 1, handle);
            fclose(handle);
            w_reset();
            w_next_body = test_case->body;
            w_next_len = test_case->len;
            w_next_type = test_case->type;
            if (which) {
                warmup_download_statistics(zbw, "sender@example.com", path, w_on_raw, w_on_error);
            } else {
                warmup_download_seeds(zbw, path, w_on_raw, w_on_error);
            }
            handle = fopen(path, "rb");
            TEST_ASSERT_NOT_NULL_MESSAGE(handle, message);
            size = fread(content, 1, sizeof(content), handle);
            fclose(handle);
            if (test_case->is_file) {
                TEST_ASSERT_FALSE_MESSAGE(w_error_called, message);
                TEST_ASSERT_TRUE_MESSAGE(w_success_called, message);
                /* The whole body, a NUL byte included. */
                TEST_ASSERT_EQUAL_UINT_MESSAGE(test_case->len, size, message);
                TEST_ASSERT_EQUAL_MEMORY_MESSAGE(test_case->body, content, size, message);
            } else {
                TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
                TEST_ASSERT_FALSE_MESSAGE(w_success_called, message);
                TEST_ASSERT_EQUAL_INT_MESSAGE(200, (int)w_status, message);
                TEST_ASSERT_EQUAL_UINT_MESSAGE(sizeof(W_OLD_CONTENT) - 1, size, message);
                TEST_ASSERT_EQUAL_MEMORY_MESSAGE(W_OLD_CONTENT, content, size, message);
            }
            remove(path);
        }
    }
    w_reset();
    zero_bounce_warmup_free(zbw);
}

void test_warmup_a_url_with_a_username_or_password_is_refused(void) {
    static const char *const urls[] = {
        "https://warmup-api.zerobounce.net@evil.example", "https://@evil.example", "https://:@evil.example",
        "https://user:pass@evil.example/", "http://localhost:@evil.example/",
    };
    char message[300];
    for (size_t u = 0; u < sizeof(urls) / sizeof(urls[0]); u++) {
        ZeroBounceWarmup *zbw = w_client("warm-userinfo-key");
        /* The loopback exception must not pass a URL that has credentials. */
        zero_bounce_set_transport_for_test(0, 0, true);
        w_reset();
        zero_bounce_warmup_set_base_url(zbw, urls[u]);
        warmup_list_sending_accounts(zbw, w_on_raw, w_on_error);
        zero_bounce_set_transport_for_test(0, 0, false);
        snprintf(message, sizeof(message), "%s: %s", urls[u], w_error);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, w_requests, message);
        TEST_ASSERT_TRUE_MESSAGE(w_error_called, message);
        TEST_ASSERT_FALSE_MESSAGE(w_success_called, message);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_error, "username or password"), message);
        TEST_ASSERT_NULL_MESSAGE(strstr(w_error, "https:// URL"), message);
        TEST_ASSERT_NULL_MESSAGE(strstr(w_error, "evil.example"), message);
        zero_bounce_warmup_free(zbw);
    }
    w_reset();
}

void test_warmup_the_base_url_is_copied(void) {
    char url[] = "https://warmup.example.test";
    ZeroBounceWarmup *zbw = w_client("warm-copy-key");
    w_reset();
    zero_bounce_warmup_set_base_url(zbw, url);
    /* The caller's buffer is reused: the client must not read it again. */
    memset(url, 'X', sizeof(url) - 1);
    warmup_list_sending_accounts(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("https://warmup.example.test/api/v1/warmup/sending-accounts", w_url);
    /* Replacing it twice keeps the last one. */
    zero_bounce_warmup_set_base_url(zbw, "https://second.example.test/");
    zero_bounce_warmup_set_base_url(zbw, "https://third.example.test/");
    warmup_list_sending_accounts(zbw, w_on_raw, w_on_error);
    TEST_ASSERT_EQUAL_STRING("https://third.example.test/api/v1/warmup/sending-accounts", w_url);
    zero_bounce_warmup_free(zbw);
}

void test_warmup_a_key_with_a_unicode_space_or_invisible_character_is_refused(void) {
    static const char *const characters[] = {
        "\xC2\xA0", "\xE2\x80\xA8", "\xE3\x80\x80", "\xE2\x80\x8B", "\xEF\xBB\xBF", "\xE2\x80\xAE", "\xE2\x81\xA6",
        "\xD8\x80", "\xE0\xA2\x90", "\xE0\xA2\x91", "\xC2\xAD", "\xEF\xBF\xB9", "\xF0\x9D\x85\xB3", "\xE1\xA0\x8B",
    };
    char key[64];
    for (size_t i = 0; i < sizeof(characters) / sizeof(characters[0]); i++) {
        ZeroBounceWarmup *zbw = new_zero_bounce_warmup_instance();
        snprintf(key, sizeof(key), "ab%scd", characters[i]);
        w_reset();
        zero_bounce_warmup_initialize(zbw, key);
        TEST_ASSERT_NULL_MESSAGE(zbw->api_key, key);
        warmup_list_sending_accounts(zbw, w_on_raw, w_on_error);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, w_requests, key);
        TEST_ASSERT_TRUE_MESSAGE(w_error_called, key);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_error, "surrounding whitespace or control characters"), key);
        zero_bounce_warmup_free(zbw);
    }
    {
        ZeroBounceWarmup *zbw = new_zero_bounce_warmup_instance();
        zero_bounce_warmup_initialize(zbw, "ab cd");
        TEST_ASSERT_NOT_NULL(zbw->api_key);
        zero_bounce_warmup_free(zbw);
    }
}

void test_warmup_redaction_decodes_every_quote_spelling_and_ignores_case(void) {
    static const char *const spellings[] = {
        "\\u{27}", "\\047", "&APOS;", "&apos;", "&#39;", "\\X27", "\\x27", "\\'", "''",
        "\\U{27}", "\\U0027", "\\U00000027",
    };
    char body[400];
    ZeroBounceWarmup *zbw = w_client("Warm-Key-77a");
    for (size_t i = 0; i < sizeof(spellings) / sizeof(spellings[0]); i++) {
        w_reset();
        snprintf(body, sizeof(body), "SMTP login failed: user smtp%suser password pa%sss-w0rd", spellings[i],
                 spellings[i]);
        w_next_status = 500;
        w_next_type = "text/plain";
        w_next_body = body;
        warmup_test_smtp_configuration(zbw, "sender@example.com", "smtp'user", "pa'ss-w0rd",
                                       "smtp.example.com", 587, w_on_raw, w_on_error);
        TEST_ASSERT_TRUE_MESSAGE(w_error_called, body);
        TEST_ASSERT_EQUAL_INT_MESSAGE(500, (int)w_status, body);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_error, "omitted because it contained a credential"), w_error);
    }
    /* An upper-cased echo of the key, the SMTP user name and the password. */
    {
        static const char *const echoes[] = {
            "Rejected key WARM-KEY-77A", "SMTP login failed for SMTP-USER@EXAMPLE.TEST",
            "SMTP login failed: PASS-W0RD-ZZ"};
        for (size_t i = 0; i < 3; i++) {
            w_reset();
            w_next_status = 500;
            w_next_type = "text/plain";
            w_next_body = echoes[i];
            warmup_test_smtp_configuration(zbw, "sender@example.com", "smtp-user@example.test", "pass-w0rd-zz",
                                           "smtp.example.com", 587, w_on_raw, w_on_error);
            TEST_ASSERT_TRUE_MESSAGE(w_error_called, echoes[i]);
            TEST_ASSERT_NOT_NULL_MESSAGE(strstr(w_error, "omitted because it contained a credential"), w_error);
        }
    }
    zero_bounce_warmup_free(zbw);

    /* The Warmup key itself, quoted with a quote spelling. */
    for (size_t i = 0; i < sizeof(spellings) / sizeof(spellings[0]); i++) {
        ZeroBounceWarmup *keyed = w_client("Warm'Key-77a");
        w_reset();
        snprintf(body, sizeof(body), "Rejected key Warm%sKey-77a", spellings[i]);
        w_next_status = 500;
        w_next_type = "text/plain";
        w_next_body = body;
        warmup_list_sending_accounts(keyed, w_on_raw, w_on_error);
        TEST_ASSERT_TRUE_MESSAGE(w_error_called, body);
        TEST_ASSERT_NULL_MESSAGE(strstr(w_error, "Key-77a"), w_error);
        zero_bounce_warmup_free(keyed);
    }
}

/* A base URL the caller stored itself (a literal) is not the library's to
   free: neither zero_bounce_warmup_free nor the setter may free it. */
void test_warmup_a_base_url_the_caller_stored_is_never_freed(void) {
    ZeroBounceWarmup *zbw = w_client("warm-key");
    zbw->base_url = "https://caller-literal.example.test";
    zero_bounce_warmup_free(zbw);

    zbw = w_client("warm-key");
    zbw->base_url = "https://caller-literal.example.test";
    zero_bounce_warmup_set_base_url(zbw, "https://copied.example.test");
    TEST_ASSERT_EQUAL_STRING("https://copied.example.test", zbw->base_url);
    zbw->base_url = "https://caller-literal-2.example.test";
    zero_bounce_warmup_free(zbw);

    /* A copy the library made is still released, and replacing it works. */
    zbw = w_client("warm-key");
    zero_bounce_warmup_set_base_url(zbw, "https://one.example.test");
    zero_bounce_warmup_set_base_url(zbw, "https://two.example.test");
    TEST_ASSERT_EQUAL_STRING("https://two.example.test", zbw->base_url);
    zero_bounce_warmup_free(zbw);
}

void warmup_run_tests(void) {
    RUN_TEST(test_warmup_a_base_url_the_caller_stored_is_never_freed);
    RUN_TEST(test_warmup_csv_downloads_see_through_padding);
    RUN_TEST(test_warmup_a_download_that_is_not_a_file_is_an_error_and_writes_nothing);
    RUN_TEST(test_warmup_a_url_with_a_username_or_password_is_refused);
    RUN_TEST(test_warmup_the_base_url_is_copied);
    RUN_TEST(test_warmup_a_key_with_a_unicode_space_or_invisible_character_is_refused);
    RUN_TEST(test_warmup_redaction_decodes_every_quote_spelling_and_ignores_case);

    RUN_TEST(test_warmup_api_key_header_and_own_host);
    RUN_TEST(test_warmup_uninitialized_sends_nothing);
    RUN_TEST(test_warmup_reads_use_the_documented_paths);
    RUN_TEST(test_warmup_empty_successes);
    RUN_TEST(test_warmup_add_sending_account);
    RUN_TEST(test_warmup_update_and_delete_sending_account);
    RUN_TEST(test_warmup_list_from_addresses);
    RUN_TEST(test_warmup_engagement_rules);
    RUN_TEST(test_warmup_statistics_per_day_dates);
    RUN_TEST(test_warmup_check_domain_dmarc_needs_an_email);
    RUN_TEST(test_warmup_test_smtp_configuration);
    RUN_TEST(test_warmup_smtp_credentials_are_masked_in_every_form);
    RUN_TEST(test_warmup_smtp_password_is_masked_in_every_encoded_form);
    RUN_TEST(test_warmup_csv_downloads);
    RUN_TEST(test_warmup_blank_arguments_are_rejected);
    RUN_TEST(test_warmup_a_rejected_key_replaces_the_previous_one);
    RUN_TEST(test_warmup_base_url_trailing_slashes_are_dropped);
    RUN_TEST(test_warmup_a_download_that_cannot_be_saved_is_not_a_success);
    RUN_TEST(test_warmup_a_short_write_is_an_error_and_leaves_no_partial_file);
    RUN_TEST(test_warmup_a_failed_write_keeps_the_file_already_at_the_path);
    RUN_TEST(test_warmup_no_error_carries_the_key);
    RUN_TEST(test_warmup_every_json_call_takes_only_strict_json);
    RUN_TEST(test_warmup_redaction_is_closed_under_encoding);
    RUN_TEST(test_warmup_no_error_on_a_200_carries_the_key);
    RUN_TEST(test_warmup_a_text_html_200_is_an_error_on_every_call);
    RUN_TEST(test_warmup_add_sending_account_sends_every_documented_field);
    RUN_TEST(test_warmup_timezone_offset_may_be_fractional);
    RUN_TEST(test_warmup_timezone_offset_keeps_its_3_0_x_type);
    RUN_TEST(test_warmup_a_non_json_200_is_an_error_on_every_json_call);
    RUN_TEST(test_warmup_csv_downloads_reject_every_non_file_answer);
    RUN_TEST(test_warmup_every_request_function_is_in_the_sweep);
    RUN_TEST(test_warmup_a_nul_in_the_body_does_not_end_the_redaction_check);
    zero_bounce_set_http_perform_for_test(NULL);
}
