#ifndef ZEROBOUNCEWARMUP_H
#define ZEROBOUNCEWARMUP_H

/**
 * Client for the ZeroBounce Warmup API (https://www.zerobounce.net/docs/warmup-api).
 *
 * The Warmup API is a separate product from email validation: its own host,
 * its own team-scoped key, and an API-Key header instead of an api_key
 * parameter. Use ZeroBounceWarmup with a Warmup API key; ZeroBounce stays for
 * the validation key, and the two never mix.
 *
 * Every call hands the raw JSON body to its success callback, or an empty
 * string for the documented empty successes (HTTP 204 and an empty 200). The
 * two download endpoints hand over CSV text instead.
 */

#include <stdbool.h>

#include "ZeroBounce/ZeroBounce.h"

/** Onboarding models accepted by warmup_add_sending_account. */
#define ZB_WARMUP_SELF_MANAGED "self_managed"
#define ZB_WARMUP_ZB_MANAGED "zb_managed"

/**
 * The Warmup client. One instance per Warmup API key; the key is bound to one
 * team.
 */
typedef struct {
    char *api_key;
    const char *base_url;
} ZeroBounceWarmup;

/** ZB managed onboarding only. A NULL field is left out of the request. */
typedef struct {
    const char *company_name;
    const char *company_website;
    const char *company_description;
    /** Any of transactional, newsletter, marketing, comma separated. */
    const char *types_of_emails;
    const char *example_email;
} ZBWarmupEmailContent;

/** ZB managed onboarding only, and write-only on the API. */
typedef struct {
    /** -1 to omit, 0 false, 1 true. */
    int weekdays_only;
    const char *timezone;
    /** -99 to omit, otherwise -11 to 14. */
    int timezone_offset;
    /** YYYY-MM-DD, or NULL to omit. */
    const char *warmup_start_date;
} ZBWarmupManagedSending;

/** Options for warmup_add_sending_account. */
typedef struct {
    /** Required for self_managed (0 to 4), ignored for zb_managed; -1 omits. */
    int engagement_rule_id;
    /** Self-managed only: -1 to omit, 0 false, 1 true. */
    int ignore_conflict;
    /** ZB managed only; NULL to omit. */
    const ZBWarmupEmailContent *email_content;
    /** ZB managed only; NULL to omit. */
    const ZBWarmupManagedSending *managed_sending;
} ZBWarmupAddSendingAccountOptions;

/** Options for warmup_list_from_addresses. */
typedef struct {
    /** Letters, digits and . - _ @ + only; NULL to omit. */
    const char *search_from;
    /** -1 to omit. The API defaults to 0. */
    int offset;
    /** -1 to omit. The API defaults to 100. */
    int limit;
} ZBWarmupListFromAddressesOptions;

ZBWarmupEmailContent new_warmup_email_content(void);
ZBWarmupManagedSending new_warmup_managed_sending(void);
ZBWarmupAddSendingAccountOptions new_warmup_add_sending_account_options(void);
ZBWarmupListFromAddressesOptions new_warmup_list_from_addresses_options(void);

/** A new client, with the default base URL and no key yet. */
ZeroBounceWarmup *new_zero_bounce_warmup_instance(void);
/** Sets the Warmup API key. It is sent in an API-Key header. */
void zero_bounce_warmup_initialize(ZeroBounceWarmup *zbw, const char *api_key);
/** Overrides the base URL ZeroBounce gave you. Must be https. */
void zero_bounce_warmup_set_base_url(ZeroBounceWarmup *zbw, const char *url);
/** Frees the client and its key. */
void zero_bounce_warmup_free(ZeroBounceWarmup *zbw);

/* -- sending accounts ---------------------------------------------------- */

/** Every sending account of the key's team. An empty team answers an empty list. */
void warmup_list_sending_accounts(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

void warmup_get_sending_account(
    ZeroBounceWarmup *zbw, const char *from_address,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/**
 * Adds a from-address to warmup. For a ZB managed account a 200 means
 * accepted; the mailbox is provisioned afterwards.
 */
void warmup_add_sending_account(
    ZeroBounceWarmup *zbw, const char *from_address, const char *onboarding_model,
    const ZBWarmupAddSendingAccountOptions *options,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/**
 * Changes an account's engagement rule, status and auto-enable flag.
 * status_id is 0 to 8 (4 Paused, 5 Disabled); auto_enable is accepted only
 * for an account that is not active.
 */
void warmup_update_sending_account(
    ZeroBounceWarmup *zbw, const char *from_address, int engagement_rule_id, int status_id,
    bool auto_enable, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/** Removes a sending account. Destructive and cannot be undone. */
void warmup_delete_sending_account(
    ZeroBounceWarmup *zbw, const char *from_address,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/**
 * Moves an account and its history to a new from-address. Despite the path it
 * uses, nothing about SMTP credentials is stored.
 */
void warmup_change_from_address(
    ZeroBounceWarmup *zbw, const char *old_from_address, const char *new_from_address,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

void warmup_list_from_addresses(
    ZeroBounceWarmup *zbw, const ZBWarmupListFromAddressesOptions *options,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/* -- engagement rules ---------------------------------------------------- */

/** The rule catalogue: -1 Custom, 0 Default, 1 Slow Pace, 2 Repair, 3 Boost, 4 Protect. */
void warmup_get_engagement_rules(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/** Sets a standard rule (0 to 4) on an address. */
void warmup_set_engagement_rule(
    ZeroBounceWarmup *zbw, const char *c_from, int rule_id,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

void warmup_get_custom_engagement(
    ZeroBounceWarmup *zbw, const char *c_from,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/**
 * Creates a custom rule. reply_pct 5-45, important_pct 5-40, click_pct 0-70
 * and at most important_pct x 1.75. Starts a 7 day cooldown on updates.
 */
void warmup_create_custom_engagement(
    ZeroBounceWarmup *zbw, const char *c_from, int reply_pct, int important_pct, int click_pct,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/** Updates a custom rule. During the 7 day cooldown this fails with HTTP 429. */
void warmup_update_custom_engagement(
    ZeroBounceWarmup *zbw, const char *c_from, int reply_pct, int important_pct, int click_pct,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/** Deletes a custom rule, falling back to standard rule engagement_rule_id (1 to 4). */
void warmup_delete_custom_engagement(
    ZeroBounceWarmup *zbw, const char *c_from, int engagement_rule_id,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/* -- seeds, progress and statistics -------------------------------------- */

void warmup_get_seeds(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/**
 * The seed list as CSV (columns seed,provider). When download_path is not
 * NULL the body is written there as well.
 */
void warmup_download_seeds(
    ZeroBounceWarmup *zbw, const char *download_path,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/** The number of seeds. Answers a bare number, not an object. */
void warmup_get_seed_count(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

void warmup_get_progress_today(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

void warmup_get_sent_today(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

void warmup_get_emails_per_day(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/** Statistics for an address. An address with no history answers zeros. */
void warmup_get_statistics(
    ZeroBounceWarmup *zbw, const char *from_address,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/** Daily statistics between two YYYY-MM-DD dates, inclusive. */
void warmup_get_statistics_per_day(
    ZeroBounceWarmup *zbw, const char *from_address, const char *start_date, const char *end_date,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

void warmup_get_statistics_detailed(
    ZeroBounceWarmup *zbw, const char *c_from,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/** An address's statistics as CSV; also written to download_path when set. */
void warmup_download_statistics(
    ZeroBounceWarmup *zbw, const char *c_from, const char *download_path,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

void warmup_get_from_address_statistics(
    ZeroBounceWarmup *zbw, const char *from_address,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/* -- deliverability checks ----------------------------------------------- */

/**
 * SPF, DKIM and DMARC status of a from-address's domain. Pass an email
 * address: the live API answers "Please provide a valid email address" for a
 * bare domain, although the docs describe a domain. dkim_selector may be NULL.
 */
void warmup_check_domain_dmarc(
    ZeroBounceWarmup *zbw, const char *from_address, const char *dkim_selector,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

/**
 * Checks that SMTP credentials connect and hand off a message. Nothing is
 * stored, and the test message goes to a ZeroBounce test recipient, not
 * from_address. The live API answers a failed SMTP login with HTTP 500.
 */
void warmup_test_smtp_configuration(
    ZeroBounceWarmup *zbw, const char *from_address, const char *username, const char *password,
    const char *server, int port,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback);

#endif
