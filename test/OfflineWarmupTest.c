/*
 * Offline tests for the Warmup API client.
 *
 * Every request is answered by the stub installed with
 * zero_bounce_set_http_perform_for_test; nothing here reaches the live API.
 * The assertions are on the url, the method, the body and the one header that
 * actually went out.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/ZeroBounceWarmup.h"
#include "ZeroBounce/zb_internal.h"

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

    len = strlen(w_next_body);
    response_data->response = malloc(len + 1);
    memcpy(response_data->response, w_next_body, len + 1);
    response_data->size = len;
    *http_code = w_next_status;
    if (content_type_out) {
        *content_type_out = strdup("application/json");
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

void warmup_run_tests(void) {
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
    RUN_TEST(test_warmup_csv_downloads);
    RUN_TEST(test_warmup_blank_arguments_are_rejected);
    zero_bounce_set_http_perform_for_test(NULL);
}
