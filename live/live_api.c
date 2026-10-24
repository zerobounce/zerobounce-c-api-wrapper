/*
 * Opt-in live API suite: calls the SDK against the real API to check payloads
 * and response shapes.
 *
 * It is its own executable, built only when ZEROBOUNCE_BUILD_LIVE_TESTS=ON, so
 * `cmake --build build` and CI never compile it and `ctest` never runs it:
 *
 *   cmake -S . -B build-live -DZEROBOUNCE_BUILD_LIVE_TESTS=ON
 *   cmake --build build-live --target ZeroBounceLiveApi
 *   export ZEROBOUNCE_API_KEY=...          # validation API (skipped without it)
 *   export ZEROBOUNCE_WARMUP_API_KEY=...   # Warmup API (skipped without it)
 *   ./build-live/bin/ZeroBounceLiveApi
 *
 * By default only free calls run: sandbox addresses (no credits), reads, and
 * requests expected to fail (unknown file ids, S3/SFTP without an integration)
 * that still prove the API accepts the payload. The free tier checks that the
 * credit balance did not change. Two flags opt in to more:
 *
 *   ZEROBOUNCE_LIVE_SPEND=1     spends credits: scoring, activity, email
 *                               finder, and a bulk round trip whose job is
 *                               deleted again
 *   ZEROBOUNCE_LIVE_CHANGES=1   reversible account changes, each undone in the
 *                               same run even when a check fails: a test
 *                               filter, and a self-managed Warmup sending
 *                               account
 *
 * Never called, because the API cannot undo them: creating an Inbox Placement
 * test (it cannot be deleted while running), setting the Inbox Placement
 * webhook (it cannot be cleared), and ZB managed Warmup onboarding (it starts
 * mailbox provisioning).
 *
 * No key is ever written to a file or printed. Keys come from the environment.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/ZeroBounceWarmup.h"

/* Well formed but matching no file: a malformed id can be Cloudflare blocked
   instead of answered. */
#define UNKNOWN_ID "0d1b7a4e-1111-2222-3333-444455556666"
#define CALLBACK "https://example.com/zerobounce-sdk-live-test"
#define SANDBOX "valid@example.com"

static int passed;
static int failed;
static int skipped;
static char failures[64][512];
static int failure_count;

static const char *api_key;
static const char *warmup_key;
static bool spend_flag;
static bool changes_flag;

static ZeroBounce *zb;
static ZeroBounceWarmup *zbw;

/* What the last callback saw. */
static bool last_ok;
static char last_error[1024];
static long last_status;
static char last_raw[65536];

static const char *env_or_empty(const char *name) {
    const char *value = getenv(name);
    return value ? value : "";
}

static void redact(char *text) {
    const char *secrets[2];
    int i;
    secrets[0] = api_key;
    secrets[1] = warmup_key;
    for (i = 0; i < 2; i++) {
        const char *secret = secrets[i];
        char *at;
        size_t len;
        if (!secret || !secret[0]) {
            continue;
        }
        len = strlen(secret);
        while ((at = strstr(text, secret)) != NULL) {
            memmove(at + 3, at + len, strlen(at + len) + 1);
            memcpy(at, "***", 3);
        }
    }
}

static void reset_result(void) {
    last_ok = false;
    last_status = 0;
    last_error[0] = '\0';
    last_raw[0] = '\0';
}

static void on_error(ZBErrorResponse error) {
    size_t i;
    size_t used = 0;
    last_ok = false;
    last_status = error.status_code;
    last_error[0] = '\0';
    for (i = 0; i < error.errors.size; i++) {
        const char *part = error.errors.data[i] ? error.errors.data[i] : "";
        int written = snprintf(last_error + used, sizeof(last_error) - used,
                               "%s%s", used ? " " : "", part);
        if (written < 0 || (size_t)written >= sizeof(last_error) - used) {
            break;
        }
        used += (size_t)written;
    }
    redact(last_error);
}

static void on_raw(const char *json_response) {
    last_ok = true;
    snprintf(last_raw, sizeof(last_raw), "%s", json_response ? json_response : "");
}

static void on_credits(ZBCreditsResponse response) { last_ok = true; (void)response; }
static void on_api_usage(ZBGetApiUsageResponse response) { last_ok = true; (void)response; }
static void on_validate(ZBValidateResponse response) { last_ok = true; (void)response; }
static void on_score(ZBScoreEmailResponse response) { last_ok = true; (void)response; }
static void on_activity(ZBActivityDataResponse response) { last_ok = true; (void)response; }
static void on_find_email(ZBFindEmailResponse response) { last_ok = true; (void)response; }
static void on_domain(ZBDomainSearchResponse response) { last_ok = true; (void)response; }
static void on_delete(ZBDeleteFileResponse response) { last_ok = true; (void)response; }
static void on_get_file(ZBGetFileResponse response) { last_ok = true; (void)response; }

static long long credits_before = -2;
static long long credits_after = -2;
static void on_credits_before(ZBCreditsResponse response) {
    last_ok = true;
    credits_before = response.credits;
}
static void on_credits_after(ZBCreditsResponse response) {
    last_ok = true;
    credits_after = response.credits;
}

static char batch_account_error[512];
static void on_batch(ZBValidateBatchResponse response) {
    size_t i;
    last_ok = true;
    batch_account_error[0] = '\0';
    for (i = 0; i < response.errors.size; i++) {
        const char *address = response.errors.data[i].email_address;
        if (address && strcasecmp(address, "all") == 0) {
            snprintf(batch_account_error, sizeof(batch_account_error), "%s",
                     response.errors.data[i].error ? response.errors.data[i].error : "unknown");
        }
    }
}

static char send_file_id[128];
static void on_send_file(ZBSendFileResponse response) {
    last_ok = true;
    snprintf(send_file_id, sizeof(send_file_id), "%s", response.file_id ? response.file_id : "");
}

static char status_file_id[128];
static void on_file_status(ZBFileStatusResponse response) {
    last_ok = true;
    snprintf(status_file_id, sizeof(status_file_id), "%s", response.file_id ? response.file_id : "");
}

static void pass(const char *name) {
    passed++;
    printf("  + %s\n", name);
    fflush(stdout);
}

static void fail(const char *name, const char *problem) {
    char safe[512];
    failed++;
    snprintf(safe, sizeof(safe), "%s", problem);
    redact(safe);
    if (failure_count < (int)(sizeof(failures) / sizeof(failures[0]))) {
        snprintf(failures[failure_count++], sizeof(failures[0]), "%s: %s", name, safe);
    }
    printf("  X %s\n      %s\n", name, safe);
    fflush(stdout);
}

static void skip(const char *name, const char *reason) {
    skipped++;
    printf("  - SKIP %s (%s)\n", name, reason);
    fflush(stdout);
}

/* The common shape: the call ran, and either it succeeded or it did not. */
static void check_ok(const char *name) {
    if (last_ok) {
        pass(name);
    } else {
        fail(name, last_error[0] ? last_error : "no response");
    }
}

static void iso_days_ago(int days, char *out, size_t out_len) {
    time_t now = time(NULL) - (time_t)days * 24 * 3600;
    struct tm value;
    localtime_r(&now, &value);
    strftime(out, out_len, "%Y-%m-%d", &value);
}

static struct tm days_ago(int days) {
    time_t now = time(NULL) - (time_t)days * 24 * 3600;
    struct tm value;
    localtime_r(&now, &value);
    return value;
}

/* -- validation API, free -------------------------------------------------- */

static void validation_free(void) {
    ZBValidateOptions options;
    EmailToValidateVector batch;
    ZBEmailToValidate row = {SANDBOX, ""};
    ZBBulkFileJobOptions job;
    ZBListInboxPlacementTestsOptions listing;

    printf("Validation API (free calls)\n");

    reset_result();
    get_credits(zb, on_credits_before, on_error);
    check_ok("getCredits returns a balance");

    reset_result();
    {
        struct tm from = days_ago(7);
        struct tm to = days_ago(0);
        get_api_usage(zb, from, to, on_api_usage, on_error);
    }
    check_ok("getApiUsage parses the documented date formats");

    reset_result();
    validate_email(zb, SANDBOX, "", on_validate, on_error);
    check_ok("validate on a sandbox address spends nothing");

    /* No billable add-on here: activity_data, verify_plus and domain_info are
       charged even on a sandbox address, so they live in the SPEND tier. */
    reset_result();
    options = new_validate_options();
    options.use_post = true;
    options.timeout = 30;
    validate_email_with_options(zb, SANDBOX, "", &options, on_validate, on_error);
    check_ok("validate works over POST as well as GET");

    reset_result();
    options = new_validate_options();
    options.timeout = 30;
    batch = email_to_validate_vector_init();
    email_to_validate_vector_append(&batch, row);
    validate_email_batch_with_options(zb, batch, &options, on_batch, on_error);
    email_to_validate_vector_free(&batch);
    if (!last_ok) {
        fail("LIVE: validateBatch accepts timeout as a string",
             last_error[0] ? last_error : "no response");
    } else if (batch_account_error[0]) {
        fail("LIVE: validateBatch accepts timeout as a string", batch_account_error);
    } else {
        pass("LIVE: validateBatch accepts timeout as a string");
    }

    reset_result();
    file_status(zb, UNKNOWN_ID, on_file_status, on_error);
    if (last_ok) {
        fail("an unknown file id is a short, clean error", "fileStatus unexpectedly succeeded");
    } else if (strlen(last_error) >= 500 || strstr(last_error, "<html")) {
        fail("an unknown file id is a short, clean error", "message not short and clean");
    } else {
        pass("an unknown file id is a short, clean error");
    }

    /* No S3 integration configured is an expected, documented failure. */
    reset_result();
    list_s3_files(zb, NULL, NULL, on_raw, on_error);
    if (!last_ok && strlen(last_error) >= 500) {
        fail("listS3files answers (empty without an S3 integration)", "message too long");
    } else {
        pass("listS3files answers (empty without an S3 integration)");
    }

    reset_result();
    job = new_bulk_file_job_options();
    job.file_name = "f.csv";
    job.has_header_row = true;
    email_finder_s3_file(zb, &job, on_send_file, on_error);
    if (last_ok) {
        fail("LIVE: S3/SFTP jobs reject a payload without return_url before sending",
             "a payload without return_url was accepted");
    } else if (!strstr(last_error, "return_url")) {
        fail("LIVE: S3/SFTP jobs reject a payload without return_url before sending", last_error);
    } else {
        pass("LIVE: S3/SFTP jobs reject a payload without return_url before sending");
    }

    /* Without an S3 integration this fails on the integration, not the payload. */
    reset_result();
    job.return_url = CALLBACK;
    email_finder_s3_file(zb, &job, on_send_file, on_error);
    if (!last_ok && (strstr(last_error, "return URL is not permitted") ||
                     strstr(last_error, "This field is required"))) {
        fail("LIVE: a complete S3 job payload is accepted by the API", last_error);
    } else {
        pass("LIVE: a complete S3 job payload is accepted by the API");
    }

    reset_result();
    list_filters(zb, false, on_raw, on_error);
    check_ok("filters list");

    /* LIVE: an api_key parameter is rejected as unauthorized here. */
    reset_result();
    get_inbox_placement_providers(zb, on_raw, on_error);
    check_ok("LIVE: the Inbox Placement Tester accepts a Bearer key");

    reset_result();
    get_inbox_placement_test_types(zb, on_raw, on_error);
    check_ok("inbox placement test types");

    /* LIVE: the listing is GET /tests (plural); /test is the single-test path
       and answers 'Method "GET" not allowed'. */
    reset_result();
    listing = new_list_inbox_placement_tests_options();
    listing.page = 1;
    listing.page_size = 10;
    list_inbox_placement_tests(zb, &listing, on_raw, on_error);
    if (!last_ok) {
        fail("LIVE: inbox placement tests are listed from /tests, and the webhook reads back",
             last_error[0] ? last_error : "no response");
    } else {
        reset_result();
        get_inbox_placement_webhook(zb, on_raw, on_error);
        check_ok("LIVE: inbox placement tests are listed from /tests, and the webhook reads back");
    }

    reset_result();
    get_list_evaluator_result(zb, UNKNOWN_ID, on_raw, on_error);
    if (last_ok) {
        fail("list evaluator result for an unknown id fails cleanly",
             "an unknown evaluation id unexpectedly succeeded");
    } else if (strlen(last_error) >= 500) {
        fail("list evaluator result for an unknown id fails cleanly", "message too long");
    } else {
        pass("list evaluator result for an unknown id fails cleanly");
    }

    /* LIVE: the file id is a form field. Appended to the path it is a 404. */
    reset_result();
    send_evaluated_file(zb, UNKNOWN_ID, on_send_file, on_error);
    if (last_ok) {
        fail("LIVE: sendEvaluatedFile posts the file id as a form field",
             "an unknown evaluation id unexpectedly succeeded");
    } else if (strstr(last_error, "not allowed")) {
        fail("LIVE: sendEvaluatedFile posts the file id as a form field", last_error);
    } else {
        pass("LIVE: sendEvaluatedFile posts the file id as a form field");
    }

    reset_result();
    get_credits(zb, on_credits_after, on_error);
    if (!last_ok) {
        fail("the free calls did not change the credit balance",
             last_error[0] ? last_error : "no response");
    } else if (credits_after != credits_before) {
        char message[128];
        snprintf(message, sizeof(message), "credits went from %lld to %lld",
                 credits_before, credits_after);
        fail("the free calls did not change the credit balance", message);
    } else {
        pass("the free calls did not change the credit balance");
    }
}

/* -- validation API, spends credits ---------------------------------------- */

static char bulk_file_id[128];

static void validation_spend(void) {
    ZBValidateOptions options;
    SendFileOptions send_options;

    printf("\nValidation API (ZEROBOUNCE_LIVE_SPEND=1)\n");

    /* LIVE: activity_data, verify_plus and domain_info are billable add-ons and
       are charged even for a sandbox address, so this is not free. */
    reset_result();
    options = new_validate_options();
    options.timeout = 30;
    options.activity_data = 1;
    options.verify_plus = 1;
    options.domain_info = 1;
    options.greylist_processing = 1;
    options.callback_url = CALLBACK;
    validate_email_with_options(zb, SANDBOX, "", &options, on_validate, on_error);
    check_ok("validate accepts every documented option");

    reset_result();
    score_email(zb, SANDBOX, on_score, on_error);
    check_ok("scoreEmail");

    reset_result();
    score_email_post(zb, SANDBOX, on_score, on_error);
    check_ok("scoreEmail over POST");

    reset_result();
    get_activity_data(zb, SANDBOX, on_activity, on_error);
    check_ok("getActivityData");

    reset_result();
    find_email_by_domain_first_last_name(zb, "example.com", "John", "Doe", on_find_email, on_error);
    check_ok("email finder");

    reset_result();
    search_domain_by_domain(zb, "example.com", on_domain, on_error);
    check_ok("domain search");

    reset_result();
    bulk_file_id[0] = '\0';
    send_file_id[0] = '\0';
    {
        const char *path = "zb-sdk-live.csv";
        FILE *out = fopen(path, "w");
        if (!out) {
            fail("bulk validation round trip: upload, status, delete",
                 "could not write the upload file");
        } else {
            fputs("email\nvalid@example.com\ninvalid@example.com\n", out);
            fclose(out);
            send_options = new_send_file_options();
            send_options.has_header_row = true;
            send_file(zb, (char *)path, 1, send_options, on_send_file, on_error);
            remove(path);
            if (!last_ok) {
                fail("bulk validation round trip: upload, status, delete",
                     last_error[0] ? last_error : "no response");
            } else if (!send_file_id[0]) {
                fail("bulk validation round trip: upload, status, delete",
                     "no file_id in the upload response");
            } else {
                snprintf(bulk_file_id, sizeof(bulk_file_id), "%s", send_file_id);
                reset_result();
                status_file_id[0] = '\0';
                file_status(zb, bulk_file_id, on_file_status, on_error);
                if (!last_ok) {
                    fail("bulk validation round trip: upload, status, delete",
                         last_error[0] ? last_error : "no response");
                } else if (strcmp(status_file_id, bulk_file_id) != 0) {
                    fail("bulk validation round trip: upload, status, delete",
                         "status did not echo the file id");
                } else {
                    pass("bulk validation round trip: upload, status, delete");
                }
            }
        }
    }

    if (!bulk_file_id[0]) {
        skip("the uploaded validation job is deleted again",
             "upload did not produce a file id");
        return;
    }
    /* LIVE: a just-accepted job answers "File cannot be found" for a short
       while, so the delete is retried rather than leaving it behind. */
    {
        int attempt;
        char last[512] = "";
        for (attempt = 0; attempt < 12; attempt++) {
            reset_result();
            delete_file(zb, bulk_file_id, on_delete, on_error);
            if (last_ok) {
                pass("the uploaded validation job is deleted again");
                return;
            }
            snprintf(last, sizeof(last), "%s", last_error);
            sleep(10);
        }
        {
            char message[640];
            snprintf(message, sizeof(message), "could not delete %s after retrying: %s",
                     bulk_file_id, last);
            fail("the uploaded validation job is deleted again", message);
        }
    }
}

/* -- validation API, reversible account changes ---------------------------- */

static void validation_changes(void) {
    char value[128];
    bool added;

    printf("\nValidation API (ZEROBOUNCE_LIVE_CHANGES=1)\n");
    snprintf(value, sizeof(value), "zb-sdk-live-%lld.example.com", (long long)time(NULL));

    reset_result();
    add_filter(zb, "allow", "domain", value, on_raw, on_error);
    added = last_ok;
    check_ok("addFilter creates a reversible allow rule");

    if (!added) {
        skip("the test filter is removed again", "filter was not created");
        return;
    }
    reset_result();
    delete_filter(zb, "allow", "domain", value, on_raw, on_error);
    check_ok("the test filter is removed again");
}

/* -- Warmup API ------------------------------------------------------------ */

/*
 * The from-address of the first sending account this team has, or "": the
 * statistics endpoints answer "Sending account ... was not found" for anything
 * else.
 */
static void first_sending_address(char *out, size_t out_len) {
    const char *at;
    const char *value;
    const char *end;

    out[0] = '\0';
    reset_result();
    warmup_list_sending_accounts(zbw, on_raw, on_error);
    if (!last_ok) {
        return;
    }
    at = strstr(last_raw, "\"from_address\"");
    if (!at) {
        at = strstr(last_raw, "\"c_from\"");
    }
    if (!at) {
        return;
    }
    value = strchr(at + 1, ':');
    if (!value) {
        return;
    }
    value = strchr(value, '"');
    if (!value) {
        return;
    }
    value++;
    end = strchr(value, '"');
    if (!end || end == value || (size_t)(end - value) >= out_len) {
        return;
    }
    memcpy(out, value, (size_t)(end - value));
    out[end - value] = '\0';
}

static void warmup_free_reads(void) {
    char known[256];
    char start[16];
    char end[16];

    printf("\nWarmup API (reads)\n");

    reset_result();
    warmup_list_sending_accounts(zbw, on_raw, on_error);
    check_ok("listSendingAccounts");

    reset_result();
    warmup_list_from_addresses(zbw, NULL, on_raw, on_error);
    check_ok("listFromAddresses");

    reset_result();
    warmup_get_engagement_rules(zbw, on_raw, on_error);
    check_ok("getEngagementRules");

    reset_result();
    warmup_get_seeds(zbw, on_raw, on_error);
    check_ok("getSeeds");

    reset_result();
    warmup_get_seed_count(zbw, on_raw, on_error);
    check_ok("getSeedCount");

    reset_result();
    warmup_get_progress_today(zbw, on_raw, on_error);
    check_ok("getProgressToday");

    reset_result();
    warmup_get_sent_today(zbw, on_raw, on_error);
    check_ok("getSentToday");

    reset_result();
    warmup_get_emails_per_day(zbw, on_raw, on_error);
    check_ok("getEmailsPerDay");

    reset_result();
    warmup_download_seeds(zbw, NULL, on_raw, on_error);
    if (!last_ok) {
        fail("downloadSeeds returns CSV text", last_error[0] ? last_error : "no response");
    } else if (!last_raw[0]) {
        fail("downloadSeeds returns CSV text", "empty CSV body");
    } else if (last_raw[0] == '{') {
        fail("downloadSeeds returns CSV text", "got JSON instead of CSV");
    } else {
        pass("downloadSeeds returns CSV text");
    }

    /* LIVE: a bare domain is answered with "Please provide a valid email
       address", although the docs describe a domain. */
    reset_result();
    warmup_check_domain_dmarc(zbw, "example.com", NULL, on_raw, on_error);
    if (last_ok) {
        fail("LIVE: checkDomainDmarc rejects a bare domain before sending",
             "a bare domain unexpectedly succeeded");
    } else if (!strstr(last_error, "email address")) {
        fail("LIVE: checkDomainDmarc rejects a bare domain before sending", last_error);
    } else {
        pass("LIVE: checkDomainDmarc rejects a bare domain before sending");
    }

    reset_result();
    warmup_check_domain_dmarc(zbw, "sender@example.com", NULL, on_raw, on_error);
    check_ok("LIVE: checkDomainDmarc accepts an email address");

    first_sending_address(known, sizeof(known));
    if (!known[0]) {
        const char *reason = "this team has no sending account";
        skip("statistics for a registered from-address", reason);
        skip("statistics per day over a short range", reason);
        skip("detailed statistics and the CSV download for that address", reason);
        skip("from-address statistics", reason);
    } else {
        reset_result();
        warmup_get_statistics(zbw, known, on_raw, on_error);
        check_ok("statistics for a registered from-address");

        reset_result();
        iso_days_ago(6, start, sizeof(start));
        iso_days_ago(0, end, sizeof(end));
        warmup_get_statistics_per_day(zbw, known, start, end, on_raw, on_error);
        check_ok("statistics per day over a short range");

        reset_result();
        warmup_get_statistics_detailed(zbw, known, on_raw, on_error);
        if (!last_ok) {
            fail("detailed statistics and the CSV download for that address",
                 last_error[0] ? last_error : "no response");
        } else {
            reset_result();
            warmup_download_statistics(zbw, known, NULL, on_raw, on_error);
            check_ok("detailed statistics and the CSV download for that address");
        }

        reset_result();
        warmup_get_from_address_statistics(zbw, known, on_raw, on_error);
        check_ok("from-address statistics");
    }

    reset_result();
    warmup_get_custom_engagement(zbw, "no-such-address@example.com", on_raw, on_error);
    if (last_ok) {
        pass("an unknown Warmup resource fails with a status code");
    } else if (last_status == 0) {
        fail("an unknown Warmup resource fails with a status code", "no status code on the error");
    } else if (warmup_key[0] && strstr(last_error, warmup_key)) {
        fail("an unknown Warmup resource fails with a status code",
             "the key leaked into the message");
    } else {
        pass("an unknown Warmup resource fails with a status code");
    }
}

static void warmup_changes(void) {
    char address[128];
    ZBWarmupAddSendingAccountOptions options;
    bool added;

    printf("\nWarmup API (ZEROBOUNCE_LIVE_CHANGES=1)\n");
    snprintf(address, sizeof(address), "zb-sdk-live-%lld@example.com", (long long)time(NULL));

    reset_result();
    options = new_warmup_add_sending_account_options();
    options.engagement_rule_id = 0;
    options.ignore_conflict = 1;
    warmup_add_sending_account(zbw, address, ZB_WARMUP_SELF_MANAGED, &options, on_raw, on_error);
    added = last_ok;
    check_ok("a self-managed sending account can be added");

    if (!added) {
        skip("the test sending account is removed again", "account was not created");
        return;
    }
    reset_result();
    warmup_delete_sending_account(zbw, address, on_raw, on_error);
    check_ok("the test sending account is removed again");
}

int main(void) {
    int i;

    api_key = env_or_empty("ZEROBOUNCE_API_KEY");
    warmup_key = env_or_empty("ZEROBOUNCE_WARMUP_API_KEY");
    spend_flag = strcmp(env_or_empty("ZEROBOUNCE_LIVE_SPEND"), "1") == 0;
    changes_flag = strcmp(env_or_empty("ZEROBOUNCE_LIVE_CHANGES"), "1") == 0;

    printf("ZeroBounce live API suite\n");
    printf("  validation key: %s\n", api_key[0] ? "set" : "MISSING");
    printf("  warmup key:     %s\n", warmup_key[0] ? "set" : "MISSING");
    printf("  flags:          SPEND=%d CHANGES=%d\n\n", spend_flag ? 1 : 0, changes_flag ? 1 : 0);
    fflush(stdout);

    if (api_key[0]) {
        zb = new_zero_bounce_instance();
        zero_bounce_initialize(zb, api_key);
        validation_free();
        if (spend_flag) {
            validation_spend();
        }
        if (changes_flag) {
            validation_changes();
        }
    }

    if (warmup_key[0]) {
        zbw = new_zero_bounce_warmup_instance();
        zero_bounce_warmup_initialize(zbw, warmup_key);
        warmup_free_reads();
        if (changes_flag) {
            warmup_changes();
        }
        zero_bounce_warmup_free(zbw);
    }

    printf("\n%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    if (failure_count) {
        printf("\nFailures:\n");
        for (i = 0; i < failure_count; i++) {
            printf("  - %s\n", failures[i]);
        }
    }
    return failed == 0 ? 0 : 1;
}
