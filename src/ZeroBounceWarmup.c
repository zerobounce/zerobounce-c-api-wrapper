#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <json.h>

#include "ZeroBounce/ZeroBounceWarmup.h"
#include "ZeroBounce/zb_internal.h"

#define ZB_WARMUP_DEFAULT_BASE_URL "https://warmup-api.zerobounce.net"
#define ZB_WARMUP_BASE_PATH "/api/v1/warmup"
/* team_id is required by the schema but always replaced with the key's team. */
#define ZB_WARMUP_ANY_TEAM_ID 1

ZBWarmupEmailContent new_warmup_email_content(void) {
    ZBWarmupEmailContent content = {NULL, NULL, NULL, NULL, NULL};
    return content;
}

ZBWarmupManagedSending new_warmup_managed_sending(void) {
    ZBWarmupManagedSending sending = {-1, NULL, -99, NULL};
    return sending;
}

ZBWarmupAddSendingAccountOptions new_warmup_add_sending_account_options(void) {
    ZBWarmupAddSendingAccountOptions options = {-1, -1, NULL, NULL};
    return options;
}

ZBWarmupListFromAddressesOptions new_warmup_list_from_addresses_options(void) {
    ZBWarmupListFromAddressesOptions options = {NULL, -1, -1};
    return options;
}

ZeroBounceWarmup *new_zero_bounce_warmup_instance(void) {
    ZeroBounceWarmup *zbw = malloc(sizeof(ZeroBounceWarmup));
    if (!zbw) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    zbw->api_key = NULL;
    zbw->base_url = ZB_WARMUP_DEFAULT_BASE_URL;
    return zbw;
}

void zero_bounce_warmup_initialize(ZeroBounceWarmup *zbw, const char *api_key) {
    if (!zbw) {
        return;
    }
    if (!zb_api_key_is_clean(api_key)) {
        fprintf(stderr,
            "ZeroBounce: the Warmup api_key has surrounding whitespace or control "
            "characters; it was not accepted.\n");
        return;
    }
    free(zbw->api_key);
    zbw->api_key = api_key ? strdup(api_key) : NULL;
}

void zero_bounce_warmup_set_base_url(ZeroBounceWarmup *zbw, const char *url) {
    if (zbw && url && url[0]) {
        zbw->base_url = url;
    }
}

void zero_bounce_warmup_free(ZeroBounceWarmup *zbw) {
    if (!zbw) {
        return;
    }
    free(zbw->api_key);
    free(zbw);
}

/* -- argument checks ------------------------------------------------------ */

static bool warmup_is_blank(const char *value) {
    const char *p;
    if (!value) {
        return true;
    }
    for (p = value; *p; p++) {
        if (!isspace((unsigned char)*p)) {
            return false;
        }
    }
    return true;
}

static bool warmup_ready(ZeroBounceWarmup *zbw, OnErrorCallback error_callback) {
    if (zbw && zbw->api_key && zbw->api_key[0]) {
        return true;
    }
    if (error_callback) {
        error_callback(parse_error(
            "The Warmup API is not initialized. Please call "
            "zero_bounce_warmup_initialize(zbw_instance, api_key); first"
        ));
    }
    return false;
}

static bool warmup_required(const char *value, const char *name, OnErrorCallback error_callback) {
    char message[128];
    if (!warmup_is_blank(value)) {
        return true;
    }
    snprintf(message, sizeof(message), "Empty parameter: %s", name);
    if (error_callback) {
        error_callback(parse_error(message));
    }
    return false;
}

static bool warmup_in_range(int value, int low, int high, const char *name, OnErrorCallback error_callback) {
    char message[160];
    if (value >= low && value <= high) {
        return true;
    }
    snprintf(message, sizeof(message), "Invalid parameter: %s (must be %d to %d)", name, low, high);
    if (error_callback) {
        error_callback(parse_error(message));
    }
    return false;
}

static bool warmup_iso_date(const char *value, const char *name, OnErrorCallback error_callback) {
    char message[160];
    int i;
    bool ok = value && strlen(value) == 10;
    for (i = 0; ok && i < 10; i++) {
        if (i == 4 || i == 7) {
            ok = value[i] == '-';
        } else {
            ok = isdigit((unsigned char)value[i]) != 0;
        }
    }
    if (ok) {
        return true;
    }
    snprintf(message, sizeof(message), "Invalid parameter: %s (expected YYYY-MM-DD)", name);
    if (error_callback) {
        error_callback(parse_error(message));
    }
    return false;
}

/* -- request plumbing ----------------------------------------------------- */

/* Appends "name=value" to a query string, with the right separator. */
static void warmup_add_query(char *query, size_t query_len, const char *name, const char *value) {
    char *escaped = zb_url_escape(value ? value : "");
    size_t used = strlen(query);
    snprintf(query + used, query_len - used, "%s%s=%s",
             used ? "&" : "?", name, escaped ? escaped : "");
    free(escaped);
}

/*
 * The Warmup key never travels in a URL, but a transport error may quote the
 * header, and test-smtp-configuration carries SMTP credentials in its body.
 */
static char *warmup_redact(const char *text, const char *const *secrets, int secret_count) {
    char *out = strdup(text ? text : "");
    int i;
    if (!out) {
        return NULL;
    }
    for (i = 0; i < secret_count; i++) {
        const char *secret = secrets[i];
        char *at;
        size_t len;
        if (!secret || !secret[0]) {
            continue;
        }
        len = strlen(secret);
        while ((at = strstr(out, secret)) != NULL) {
            memmove(at + 3, at + len, strlen(at + len) + 1);
            memcpy(at, "***", 3);
        }
    }
    return out;
}

/* Reports an error with the secrets taken out of its message. */
static void warmup_report(
    ZBErrorResponse error, const char *const *secrets, int secret_count,
    OnErrorCallback error_callback
) {
    size_t i;
    if (!error_callback) {
        return;
    }
    for (i = 0; i < error.errors.size; i++) {
        char *safe = warmup_redact(error.errors.data[i], secrets, secret_count);
        if (safe) {
            free(error.errors.data[i]);
            error.errors.data[i] = safe;
        }
    }
    error_callback(error);
}

typedef struct {
    OnErrorCallback error_callback;
    const char *const *secrets;
    int secret_count;
} WarmupErrorCtx;

static _Thread_local WarmupErrorCtx warmup_error_ctx;

static void warmup_on_error(ZBErrorResponse error) {
    warmup_report(error, warmup_error_ctx.secrets, warmup_error_ctx.secret_count,
                  warmup_error_ctx.error_callback);
}

/*
 * One Warmup request. The body is handed to success_callback raw; HTTP 204
 * and an empty 200 are the documented empty successes and come back as "".
 */
static void warmup_send(
    ZeroBounceWarmup *zbw,
    const char *method,
    const char *path,
    const char *query,
    const char *body,
    const char *const *secrets,
    int secret_count,
    bool expect_csv,
    const char *download_path,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    char *url_path;
    char *auth_header;
    int url_len;
    int auth_len;
    memory response_data = {0};
    long http_code = 0;
    const char *text;

    if (!warmup_ready(zbw, error_callback)) {
        return;
    }

    url_len = snprintf(NULL, 0, "%s%s%s%s", zbw->base_url, ZB_WARMUP_BASE_PATH, path, query ? query : "");
    url_path = malloc((size_t)url_len + 1);
    auth_len = snprintf(NULL, 0, "API-Key: %s", zbw->api_key);
    auth_header = malloc((size_t)auth_len + 1);
    if (!url_path || !auth_header) {
        free(url_path);
        free(auth_header);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    snprintf(url_path, (size_t)url_len + 1, "%s%s%s%s",
             zbw->base_url, ZB_WARMUP_BASE_PATH, path, query ? query : "");
    snprintf(auth_header, (size_t)auth_len + 1, "API-Key: %s", zbw->api_key);

    warmup_error_ctx.error_callback = error_callback;
    warmup_error_ctx.secrets = secrets;
    warmup_error_ctx.secret_count = secret_count;

    if (!zb_make_request_with_extra_header(
            url_path, method, body, "Content-Type: application/json", auth_header,
            &response_data, &http_code, warmup_on_error)) {
        free(auth_header);
        free(url_path);
        free(response_data.response);
        return;
    }

    text = response_data.response ? response_data.response : "";
    if (http_code > 299 || zb_body_looks_like_html(text)) {
        zb_report_body_error(text, http_code, warmup_on_error);
    } else if (expect_csv) {
        /* An error comes back labelled as JSON, so it is not a file. */
        if (!text[0] || text[0] == '{' || text[0] == '[') {
            zb_report_body_error(text, http_code, warmup_on_error);
        } else {
            if (download_path && download_path[0]) {
                FILE *handle = fopen(download_path, "wb");
                if (handle) {
                    fwrite(text, 1, strlen(text), handle);
                    fclose(handle);
                } else if (error_callback) {
                    error_callback(parse_error("Could not open download_path for writing"));
                }
            }
            if (success_callback) {
                success_callback(text);
            }
        }
    } else if (success_callback) {
        /* 204 and an empty 200 are the documented empty successes. */
        success_callback(text);
    }

    free(auth_header);
    free(url_path);
    free(response_data.response);
}

static void warmup_json(
    ZeroBounceWarmup *zbw, const char *method, const char *path, const char *query,
    const char *body, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    warmup_send(zbw, method, path, query, body, NULL, 0, false, NULL,
                success_callback, error_callback);
}

/* -- sending accounts ----------------------------------------------------- */

void warmup_list_sending_accounts(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    warmup_json(zbw, "GET", "/sending-accounts", NULL, NULL, success_callback, error_callback);
}

void warmup_get_sending_account(
    ZeroBounceWarmup *zbw, const char *from_address,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char query[1024] = "";
    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(from_address, "from_address", error_callback)) {
        return;
    }
    warmup_add_query(query, sizeof(query), "from_address", from_address);
    warmup_json(zbw, "GET", "/sending-account", query, NULL, success_callback, error_callback);
}

void warmup_add_sending_account(
    ZeroBounceWarmup *zbw, const char *from_address, const char *onboarding_model,
    const ZBWarmupAddSendingAccountOptions *options,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    json_object *payload;
    bool self_managed;

    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(from_address, "from_address", error_callback)) {
        return;
    }
    if (!onboarding_model ||
        (strcmp(onboarding_model, ZB_WARMUP_SELF_MANAGED) != 0 &&
         strcmp(onboarding_model, ZB_WARMUP_ZB_MANAGED) != 0)) {
        if (error_callback) {
            error_callback(parse_error(
                "Invalid parameter: onboarding_model (must be self_managed or zb_managed)"));
        }
        return;
    }
    self_managed = strcmp(onboarding_model, ZB_WARMUP_SELF_MANAGED) == 0;

    if (self_managed) {
        if (!options || options->engagement_rule_id < 0) {
            if (error_callback) {
                error_callback(parse_error(
                    "Empty parameter: engagement_rule_id (required for self_managed)"));
            }
            return;
        }
        if (!warmup_in_range(options->engagement_rule_id, 0, 4, "engagement_rule_id", error_callback)) {
            return;
        }
        if (options->email_content || options->managed_sending) {
            if (error_callback) {
                error_callback(parse_error(
                    "Invalid parameter: email_content and managed_sending are for zb_managed"));
            }
            return;
        }
    }

    payload = json_object_new_object();
    json_object_object_add(payload, "from_address", json_object_new_string(from_address));
    json_object_object_add(payload, "onboarding_model", json_object_new_string(onboarding_model));
    if (self_managed) {
        json_object_object_add(
            payload, "engagement_rule_id", json_object_new_int(options->engagement_rule_id));
    }
    if (options && options->ignore_conflict >= 0) {
        json_object_object_add(
            payload, "ignore_conflict", json_object_new_boolean(options->ignore_conflict ? 1 : 0));
    }
    if (options && options->email_content) {
        const ZBWarmupEmailContent *content = options->email_content;
        json_object *node = json_object_new_object();
        if (content->company_name) {
            json_object_object_add(node, "company_name", json_object_new_string(content->company_name));
        }
        if (content->company_website) {
            json_object_object_add(node, "company_website", json_object_new_string(content->company_website));
        }
        if (content->company_description) {
            json_object_object_add(
                node, "company_description", json_object_new_string(content->company_description));
        }
        if (content->types_of_emails) {
            json_object *types = json_object_new_array();
            const char *p = content->types_of_emails;
            while (*p) {
                char item[32];
                size_t n = 0;
                while (*p == ' ' || *p == ',') {
                    p++;
                }
                while (*p && *p != ',' && n + 1 < sizeof(item)) {
                    item[n++] = (char)tolower((unsigned char)*p++);
                }
                while (n > 0 && item[n - 1] == ' ') {
                    n--;
                }
                item[n] = '\0';
                if (!n) {
                    continue;
                }
                if (strcmp(item, "transactional") != 0 && strcmp(item, "newsletter") != 0 &&
                    strcmp(item, "marketing") != 0) {
                    json_object_put(types);
                    json_object_put(node);
                    json_object_put(payload);
                    if (error_callback) {
                        error_callback(parse_error(
                            "Invalid parameter: types_of_emails "
                            "(transactional, newsletter and/or marketing)"));
                    }
                    return;
                }
                json_object_array_add(types, json_object_new_string(item));
            }
            json_object_object_add(node, "types_of_emails", types);
        }
        if (content->example_email) {
            json_object_object_add(node, "example_email", json_object_new_string(content->example_email));
        }
        json_object_object_add(payload, "email_content", node);
    }
    if (options && options->managed_sending) {
        const ZBWarmupManagedSending *sending = options->managed_sending;
        json_object *node = json_object_new_object();
        if (sending->weekdays_only >= 0) {
            json_object_object_add(
                node, "weekdays_only", json_object_new_boolean(sending->weekdays_only ? 1 : 0));
        }
        if (sending->timezone) {
            json_object_object_add(node, "timezone", json_object_new_string(sending->timezone));
        }
        if (sending->timezone_offset != -99) {
            if (!warmup_in_range(sending->timezone_offset, -11, 14, "timezone_offset", error_callback)) {
                json_object_put(node);
                json_object_put(payload);
                return;
            }
            json_object_object_add(
                node, "timezone_offset", json_object_new_int(sending->timezone_offset));
        }
        if (sending->warmup_start_date) {
            if (!warmup_iso_date(sending->warmup_start_date, "warmup_start_date", error_callback)) {
                json_object_put(node);
                json_object_put(payload);
                return;
            }
            json_object_object_add(
                node, "warmup_start_date", json_object_new_string(sending->warmup_start_date));
        }
        json_object_object_add(payload, "managed_sending", node);
    }

    warmup_json(zbw, "POST", "/sending-accounts", NULL,
                json_object_to_json_string(payload), success_callback, error_callback);
    json_object_put(payload);
}

void warmup_update_sending_account(
    ZeroBounceWarmup *zbw, const char *from_address, int engagement_rule_id, int status_id,
    bool auto_enable, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    json_object *payload;

    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(from_address, "from_address", error_callback) ||
        !warmup_in_range(engagement_rule_id, 0, 4, "engagement_rule_id", error_callback) ||
        !warmup_in_range(status_id, 0, 8, "status_id", error_callback)) {
        return;
    }
    payload = json_object_new_object();
    json_object_object_add(payload, "team_id", json_object_new_int(ZB_WARMUP_ANY_TEAM_ID));
    json_object_object_add(payload, "from_address", json_object_new_string(from_address));
    json_object_object_add(payload, "engagement_rule_id", json_object_new_int(engagement_rule_id));
    json_object_object_add(payload, "status_id", json_object_new_int(status_id));
    json_object_object_add(payload, "auto_enable", json_object_new_boolean(auto_enable ? 1 : 0));
    warmup_json(zbw, "PUT", "/sending-accounts", NULL,
                json_object_to_json_string(payload), success_callback, error_callback);
    json_object_put(payload);
}

void warmup_delete_sending_account(
    ZeroBounceWarmup *zbw, const char *from_address,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char query[1024] = "";
    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(from_address, "from_address", error_callback)) {
        return;
    }
    warmup_add_query(query, sizeof(query), "from_address", from_address);
    warmup_json(zbw, "DELETE", "/sending-accounts", query, NULL, success_callback, error_callback);
}

void warmup_change_from_address(
    ZeroBounceWarmup *zbw, const char *old_from_address, const char *new_from_address,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    json_object *payload;

    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(old_from_address, "old_from_address", error_callback) ||
        !warmup_required(new_from_address, "new_from_address", error_callback)) {
        return;
    }
    payload = json_object_new_object();
    json_object_object_add(payload, "team_id", json_object_new_int(ZB_WARMUP_ANY_TEAM_ID));
    json_object_object_add(payload, "old_from_address", json_object_new_string(old_from_address));
    json_object_object_add(payload, "new_from_address", json_object_new_string(new_from_address));
    warmup_json(zbw, "PUT", "/smtp-credentials", NULL,
                json_object_to_json_string(payload), success_callback, error_callback);
    json_object_put(payload);
}

void warmup_list_from_addresses(
    ZeroBounceWarmup *zbw, const ZBWarmupListFromAddressesOptions *options,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char query[1024] = "";
    char number[32];

    if (!warmup_ready(zbw, error_callback)) {
        return;
    }
    if (options && options->search_from) {
        const char *p;
        for (p = options->search_from; *p; p++) {
            if (!isalnum((unsigned char)*p) && !strchr(".-_@+", *p)) {
                if (error_callback) {
                    error_callback(parse_error(
                        "Invalid parameter: search_from (letters, digits and . - _ @ + only)"));
                }
                return;
            }
        }
        warmup_add_query(query, sizeof(query), "search_from", options->search_from);
    }
    /* The API defaults these to 0 and 100. */
    if (options && options->offset >= 0) {
        snprintf(number, sizeof(number), "%d", options->offset);
        warmup_add_query(query, sizeof(query), "offset", number);
    }
    if (options && options->limit >= 0) {
        snprintf(number, sizeof(number), "%d", options->limit);
        warmup_add_query(query, sizeof(query), "limit", number);
    }
    warmup_json(zbw, "GET", "/from-addresses", query, NULL, success_callback, error_callback);
}

/* -- engagement rules ----------------------------------------------------- */

void warmup_get_engagement_rules(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    warmup_json(zbw, "GET", "/engagement-rules", NULL, NULL, success_callback, error_callback);
}

void warmup_set_engagement_rule(
    ZeroBounceWarmup *zbw, const char *c_from, int rule_id,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char query[1024] = "";
    json_object *payload;

    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(c_from, "c_from", error_callback) ||
        !warmup_in_range(rule_id, 0, 4, "rule_id", error_callback)) {
        return;
    }
    warmup_add_query(query, sizeof(query), "c_from", c_from);
    payload = json_object_new_object();
    json_object_object_add(payload, "rule_id", json_object_new_int(rule_id));
    warmup_json(zbw, "PUT", "/engagement-rule", query,
                json_object_to_json_string(payload), success_callback, error_callback);
    json_object_put(payload);
}

void warmup_get_custom_engagement(
    ZeroBounceWarmup *zbw, const char *c_from,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char path[640];
    char *segment;

    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(c_from, "c_from", error_callback)) {
        return;
    }
    /* c_from is one path segment here, so a slash must stay encoded, and a
       "." or ".." would walk the request out of the endpoint. */
    if (strcmp(c_from, ".") == 0 || strcmp(c_from, "..") == 0) {
        if (error_callback) {
            error_callback(parse_error("Invalid parameter: c_from"));
        }
        return;
    }
    segment = zb_url_escape(c_from);
    snprintf(path, sizeof(path), "/custom-engagement/%s", segment ? segment : "");
    free(segment);
    warmup_json(zbw, "GET", path, NULL, NULL, success_callback, error_callback);
}

/*
 * reply_pct 5-45, important_pct 5-40, click_pct 0-70 and at most
 * important_pct x 1.75. Returns NULL when an argument was rejected.
 */
static json_object *warmup_engagement_payload(
    const char *c_from, int reply_pct, int important_pct, int click_pct,
    OnErrorCallback error_callback
) {
    json_object *payload;

    if (!warmup_required(c_from, "c_from", error_callback) ||
        !warmup_in_range(reply_pct, 5, 45, "reply_pct", error_callback) ||
        !warmup_in_range(important_pct, 5, 40, "important_pct", error_callback) ||
        !warmup_in_range(click_pct, 0, 70, "click_pct", error_callback)) {
        return NULL;
    }
    if (click_pct * 100 > important_pct * 175) {
        if (error_callback) {
            error_callback(parse_error("Invalid parameter: click_pct (at most important_pct x 1.75)"));
        }
        return NULL;
    }
    payload = json_object_new_object();
    json_object_object_add(payload, "c_from", json_object_new_string(c_from));
    json_object_object_add(payload, "reply_pct", json_object_new_int(reply_pct));
    json_object_object_add(payload, "important_pct", json_object_new_int(important_pct));
    json_object_object_add(payload, "click_pct", json_object_new_int(click_pct));
    return payload;
}

void warmup_create_custom_engagement(
    ZeroBounceWarmup *zbw, const char *c_from, int reply_pct, int important_pct, int click_pct,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    json_object *payload;
    if (!warmup_ready(zbw, error_callback)) {
        return;
    }
    payload = warmup_engagement_payload(c_from, reply_pct, important_pct, click_pct, error_callback);
    if (!payload) {
        return;
    }
    warmup_json(zbw, "POST", "/custom-engagement", NULL,
                json_object_to_json_string(payload), success_callback, error_callback);
    json_object_put(payload);
}

void warmup_update_custom_engagement(
    ZeroBounceWarmup *zbw, const char *c_from, int reply_pct, int important_pct, int click_pct,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    json_object *payload;
    if (!warmup_ready(zbw, error_callback)) {
        return;
    }
    payload = warmup_engagement_payload(c_from, reply_pct, important_pct, click_pct, error_callback);
    if (!payload) {
        return;
    }
    warmup_json(zbw, "PUT", "/custom-engagement", NULL,
                json_object_to_json_string(payload), success_callback, error_callback);
    json_object_put(payload);
}

void warmup_delete_custom_engagement(
    ZeroBounceWarmup *zbw, const char *c_from, int engagement_rule_id,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    json_object *payload;

    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(c_from, "c_from", error_callback) ||
        !warmup_in_range(engagement_rule_id, 1, 4, "engagement_rule_id", error_callback)) {
        return;
    }
    payload = json_object_new_object();
    json_object_object_add(payload, "c_from", json_object_new_string(c_from));
    json_object_object_add(payload, "engagement_rule_id", json_object_new_int(engagement_rule_id));
    warmup_json(zbw, "DELETE", "/custom-engagement", NULL,
                json_object_to_json_string(payload), success_callback, error_callback);
    json_object_put(payload);
}

/* -- seeds, progress and statistics --------------------------------------- */

void warmup_get_seeds(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    warmup_json(zbw, "GET", "/seeds", NULL, NULL, success_callback, error_callback);
}

void warmup_download_seeds(
    ZeroBounceWarmup *zbw, const char *download_path,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    warmup_send(zbw, "GET", "/seeds/download", NULL, NULL, NULL, 0, true, download_path,
                success_callback, error_callback);
}

void warmup_get_seed_count(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    warmup_json(zbw, "GET", "/seed-count", NULL, NULL, success_callback, error_callback);
}

void warmup_get_progress_today(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    warmup_json(zbw, "GET", "/progress-today", NULL, NULL, success_callback, error_callback);
}

void warmup_get_sent_today(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    warmup_json(zbw, "GET", "/sent-today", NULL, NULL, success_callback, error_callback);
}

void warmup_get_emails_per_day(
    ZeroBounceWarmup *zbw, OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    warmup_json(zbw, "GET", "/emails-per-day", NULL, NULL, success_callback, error_callback);
}

void warmup_get_statistics(
    ZeroBounceWarmup *zbw, const char *from_address,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char query[1024] = "";
    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(from_address, "from_address", error_callback)) {
        return;
    }
    warmup_add_query(query, sizeof(query), "from_address", from_address);
    warmup_json(zbw, "GET", "/statistics", query, NULL, success_callback, error_callback);
}

void warmup_get_statistics_per_day(
    ZeroBounceWarmup *zbw, const char *from_address, const char *start_date, const char *end_date,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char query[1024] = "";

    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(from_address, "from_address", error_callback) ||
        !warmup_iso_date(start_date, "start_date", error_callback) ||
        !warmup_iso_date(end_date, "end_date", error_callback)) {
        return;
    }
    if (strcmp(start_date, end_date) > 0) {
        if (error_callback) {
            error_callback(parse_error("Invalid parameter: start_date is later than end_date"));
        }
        return;
    }
    warmup_add_query(query, sizeof(query), "from_address", from_address);
    warmup_add_query(query, sizeof(query), "start_date", start_date);
    warmup_add_query(query, sizeof(query), "end_date", end_date);
    warmup_json(zbw, "GET", "/statistics-per-day", query, NULL, success_callback, error_callback);
}

void warmup_get_statistics_detailed(
    ZeroBounceWarmup *zbw, const char *c_from,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char query[1024] = "";
    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(c_from, "c_from", error_callback)) {
        return;
    }
    warmup_add_query(query, sizeof(query), "c_from", c_from);
    warmup_json(zbw, "GET", "/statistics-detailed", query, NULL, success_callback, error_callback);
}

void warmup_download_statistics(
    ZeroBounceWarmup *zbw, const char *c_from, const char *download_path,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char query[1024] = "";
    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(c_from, "c_from", error_callback)) {
        return;
    }
    warmup_add_query(query, sizeof(query), "c_from", c_from);
    warmup_send(zbw, "GET", "/statistics/download", query, NULL, NULL, 0, true, download_path,
                success_callback, error_callback);
}

void warmup_get_from_address_statistics(
    ZeroBounceWarmup *zbw, const char *from_address,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char query[1024] = "";
    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(from_address, "from_address", error_callback)) {
        return;
    }
    warmup_add_query(query, sizeof(query), "from_address", from_address);
    warmup_json(zbw, "GET", "/from-address-statistics", query, NULL, success_callback, error_callback);
}

/* -- deliverability checks ------------------------------------------------ */

void warmup_check_domain_dmarc(
    ZeroBounceWarmup *zbw, const char *from_address, const char *dkim_selector,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    char query[1024] = "";

    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(from_address, "from_address", error_callback)) {
        return;
    }
    /* LIVE: this takes an email address. The docs describe a domain, but a
       bare domain is answered with "Please provide a valid email address". */
    if (!strchr(from_address, '@')) {
        if (error_callback) {
            error_callback(parse_error(
                "Invalid parameter: from_address (an email address, e.g. sender@example.com)"));
        }
        return;
    }
    warmup_add_query(query, sizeof(query), "from_address", from_address);
    if (dkim_selector && dkim_selector[0]) {
        warmup_add_query(query, sizeof(query), "dkim_selector", dkim_selector);
    }
    warmup_json(zbw, "GET", "/check-domain-dmarc", query, NULL, success_callback, error_callback);
}

void warmup_test_smtp_configuration(
    ZeroBounceWarmup *zbw, const char *from_address, const char *username, const char *password,
    const char *server, int port,
    OnSuccessCallbackRawJson success_callback, OnErrorCallback error_callback
) {
    json_object *payload;
    const char *secrets[2];

    if (!warmup_ready(zbw, error_callback) ||
        !warmup_required(from_address, "from_address", error_callback) ||
        !warmup_required(username, "username", error_callback) ||
        !warmup_required(password, "password", error_callback) ||
        !warmup_required(server, "server", error_callback) ||
        !warmup_in_range(port, 1, 65535, "port", error_callback)) {
        return;
    }
    secrets[0] = password;
    secrets[1] = username;

    payload = json_object_new_object();
    json_object_object_add(payload, "from_address", json_object_new_string(from_address));
    json_object_object_add(payload, "username", json_object_new_string(username));
    json_object_object_add(payload, "password", json_object_new_string(password));
    json_object_object_add(payload, "server", json_object_new_string(server));
    json_object_object_add(payload, "port", json_object_new_int(port));
    warmup_send(zbw, "POST", "/test-smtp-configuration", NULL,
                json_object_to_json_string(payload), secrets, 2, false, NULL,
                success_callback, error_callback);
    json_object_put(payload);
}
