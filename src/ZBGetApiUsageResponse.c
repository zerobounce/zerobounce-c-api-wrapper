#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBGetApiUsageResponse.h"
#include "ZeroBounce/utils.h"
#include "ZeroBounce/zb_internal.h"

ZBGetApiUsageResponse new_zb_get_api_usage_response(void) {
    ZBGetApiUsageResponse response;

    response.total = 0;
    response.status_valid = 0;
    response.status_invalid = 0;
    response.status_catch_all = 0;
    response.status_do_not_mail = 0;
    response.status_spamtrap = 0;
    response.status_unknown = 0;
    response.sub_status_toxic = 0;
    response.sub_status_disposable = 0;
    response.sub_status_role_based = 0;
    response.sub_status_possible_trap = 0;
    response.sub_status_global_suppression = 0;
    response.sub_status_timeout_exceeded = 0;
    response.sub_status_mail_server_temporary_error = 0;
    response.sub_status_mail_server_did_not_respond = 0;
    response.sub_status_grey_listed = 0;
    response.sub_status_anti_spam_system = 0;
    response.sub_status_does_not_accept_mail = 0;
    response.sub_status_exception_occurred = 0;
    response.sub_status_failed_syntax_check = 0;
    response.sub_status_mailbox_not_found = 0;
    response.sub_status_unroutable_ip_address = 0;
    response.sub_status_possible_typo = 0;
    response.sub_status_no_dns_entries = 0;
    response.sub_status_role_based_catch_all = 0;
    response.sub_status_mailbox_quota_exceeded = 0;
    response.sub_status_forcible_disconnect = 0;
    response.sub_status_failed_smtp_connection = 0;
    response.sub_status_mx_forward = 0;
    response.sub_status_alternate = 0;
    response.sub_status_blocked = 0;
    response.sub_status_allowed = 0;
    response.sub_status_accept_all = 0;
    response.sub_status_gold = 0;
    response.sub_status_role_based_accept_all = 0;
    response.sub_status_leading_period_removed = 0;
    response.sub_status_alias_address = 0;
    response.start_date = zb_dup_empty();
    response.end_date = zb_dup_empty();
    response.error = zb_dup_empty();

    return response;
}

char* zb_get_api_usage_response_to_string(ZBGetApiUsageResponse* response) {
    const char *serialization = "ZBGetApiUsageResponse{"
        "total=%d"
        ", status_valid=%d"
        ", status_invalid=%d"
        ", status_catch_all=%d"
        ", status_do_not_mail=%d"
        ", status_spamtrap=%d"
        ", status_unknown=%d"
        ", sub_status_toxic=%d"
        ", sub_status_disposable=%d"
        ", sub_status_role_based=%d"
        ", sub_status_possible_trap=%d"
        ", sub_status_global_suppression=%d"
        ", sub_status_timeout_exceeded=%d"
        ", sub_status_mail_server_temporary_error=%d"
        ", sub_status_mail_server_did_not_respond=%d"
        ", sub_status_grey_listed=%d"
        ", sub_status_anti_spam_system=%d"
        ", sub_status_does_not_accept_mail=%d"
        ", sub_status_exception_occurred=%d"
        ", sub_status_failed_syntax_check=%d"
        ", sub_status_mailbox_not_found=%d"
        ", sub_status_unroutable_ip_address=%d"
        ", sub_status_possible_typo=%d"
        ", sub_status_no_dns_entries=%d"
        ", sub_status_role_based_catch_all=%d"
        ", sub_status_mailbox_quota_exceeded=%d"
        ", sub_status_forcible_disconnect=%d"
        ", sub_status_failed_smtp_connection=%d"
        ", sub_status_mx_forward=%d"
        ", sub_status_alternate=%d"
        ", sub_status_blocked=%d"
        ", sub_status_allowed=%d"
        ", sub_status_accept_all=%d"
        ", sub_status_gold=%d"
        ", sub_status_role_based_accept_all=%d"
        ", sub_status_leading_period_removed=%d"
        ", sub_status_alias_address=%d"
        ", start_date='%s'"
        ", end_date='%s'"
        ", error='%s'"
        "}";

    int size = snprintf(
        NULL, 0, serialization,
        response->total,
        response->status_valid,
        response->status_invalid,
        response->status_catch_all,
        response->status_do_not_mail,
        response->status_spamtrap,
        response->status_unknown,
        response->sub_status_toxic,
        response->sub_status_disposable,
        response->sub_status_role_based,
        response->sub_status_possible_trap,
        response->sub_status_global_suppression,
        response->sub_status_timeout_exceeded,
        response->sub_status_mail_server_temporary_error,
        response->sub_status_mail_server_did_not_respond,
        response->sub_status_grey_listed,
        response->sub_status_anti_spam_system,
        response->sub_status_does_not_accept_mail,
        response->sub_status_exception_occurred,
        response->sub_status_failed_syntax_check,
        response->sub_status_mailbox_not_found,
        response->sub_status_unroutable_ip_address,
        response->sub_status_possible_typo,
        response->sub_status_no_dns_entries,
        response->sub_status_role_based_catch_all,
        response->sub_status_mailbox_quota_exceeded,
        response->sub_status_forcible_disconnect,
        response->sub_status_failed_smtp_connection,
        response->sub_status_mx_forward,
        response->sub_status_alternate,
        response->sub_status_blocked,
        response->sub_status_allowed,
        response->sub_status_accept_all,
        response->sub_status_gold,
        response->sub_status_role_based_accept_all,
        response->sub_status_leading_period_removed,
        response->sub_status_alias_address,
        response->start_date,
        response->end_date,
        response->error
    );

    if (size < 0) {
        return NULL;
    }

    char* buffer = (char*) malloc(size + 1);
    if (!buffer) {
        return NULL;
    }

    snprintf(
        buffer, size + 1, serialization,
        response->total,
        response->status_valid,
        response->status_invalid,
        response->status_catch_all,
        response->status_do_not_mail,
        response->status_spamtrap,
        response->status_unknown,
        response->sub_status_toxic,
        response->sub_status_disposable,
        response->sub_status_role_based,
        response->sub_status_possible_trap,
        response->sub_status_global_suppression,
        response->sub_status_timeout_exceeded,
        response->sub_status_mail_server_temporary_error,
        response->sub_status_mail_server_did_not_respond,
        response->sub_status_grey_listed,
        response->sub_status_anti_spam_system,
        response->sub_status_does_not_accept_mail,
        response->sub_status_exception_occurred,
        response->sub_status_failed_syntax_check,
        response->sub_status_mailbox_not_found,
        response->sub_status_unroutable_ip_address,
        response->sub_status_possible_typo,
        response->sub_status_no_dns_entries,
        response->sub_status_role_based_catch_all,
        response->sub_status_mailbox_quota_exceeded,
        response->sub_status_forcible_disconnect,
        response->sub_status_failed_smtp_connection,
        response->sub_status_mx_forward,
        response->sub_status_alternate,
        response->sub_status_blocked,
        response->sub_status_allowed,
        response->sub_status_accept_all,
        response->sub_status_gold,
        response->sub_status_role_based_accept_all,
        response->sub_status_leading_period_removed,
        response->sub_status_alias_address,
        response->start_date,
        response->end_date,
        response->error
    );
    return buffer;
}

ZBGetApiUsageResponse zb_get_api_usage_response_from_json(const json_object* j) {
    ZBGetApiUsageResponse r = new_zb_get_api_usage_response();
    /* Every string field is assigned below; drop the initial copies. */
    zb_get_api_usage_response_free(&r);

    r.total = zb_json_get_int(j, "total", 0);
    r.status_valid = zb_json_get_int(j, "status_valid", 0);
    r.status_invalid = zb_json_get_int(j, "status_invalid", 0);
    r.status_catch_all = zb_json_get_int(j, "status_catch_all", 0);
    r.status_do_not_mail = zb_json_get_int(j, "status_do_not_mail", 0);
    r.status_spamtrap = zb_json_get_int(j, "status_spamtrap", 0);
    r.status_unknown = zb_json_get_int(j, "status_unknown", 0);
    r.sub_status_toxic = zb_json_get_int(j, "sub_status_toxic", 0);
    r.sub_status_disposable = zb_json_get_int(j, "sub_status_disposable", 0);
    r.sub_status_role_based = zb_json_get_int(j, "sub_status_role_based", 0);
    r.sub_status_possible_trap = zb_json_get_int(j, "sub_status_possible_trap", 0);
    r.sub_status_global_suppression = zb_json_get_int(j, "sub_status_global_suppression", 0);
    r.sub_status_timeout_exceeded = zb_json_get_int(j, "sub_status_timeout_exceeded", 0);
    r.sub_status_mail_server_temporary_error = zb_json_get_int(j, "sub_status_mail_server_temporary_error", 0);
    r.sub_status_mail_server_did_not_respond = zb_json_get_int(j, "sub_status_mail_server_did_not_respond", 0);
    r.sub_status_grey_listed = zb_json_get_int(j, "sub_status_greylisted", 0);
    r.sub_status_anti_spam_system = zb_json_get_int(j, "sub_status_antispam_system", 0);
    r.sub_status_does_not_accept_mail = zb_json_get_int(j, "sub_status_does_not_accept_mail", 0);
    r.sub_status_exception_occurred = zb_json_get_int(j, "sub_status_exception_occurred", 0);
    r.sub_status_failed_syntax_check = zb_json_get_int(j, "sub_status_failed_syntax_check", 0);
    r.sub_status_mailbox_not_found = zb_json_get_int(j, "sub_status_mailbox_not_found", 0);
    r.sub_status_unroutable_ip_address = zb_json_get_int(j, "sub_status_unroutable_ip_address", 0);
    r.sub_status_possible_typo = zb_json_get_int(j, "sub_status_possible_typo", 0);
    r.sub_status_no_dns_entries = zb_json_get_int(j, "sub_status_no_dns_entries", 0);
    r.sub_status_role_based_catch_all = zb_json_get_int(j, "sub_status_role_based_catch_all", 0);
    r.sub_status_mailbox_quota_exceeded = zb_json_get_int(j, "sub_status_mailbox_quota_exceeded", 0);
    r.sub_status_forcible_disconnect = zb_json_get_int(j, "sub_status_forcible_disconnect", 0);
    r.sub_status_failed_smtp_connection = zb_json_get_int(j, "sub_status_failed_smtp_connection", 0);
    r.sub_status_mx_forward = zb_json_get_int(j, "sub_status_mx_forward", 0);
    r.sub_status_alternate = zb_json_get_int(j, "sub_status_alternate", 0);
    r.sub_status_blocked = zb_json_get_int(j, "sub_status_blocked", 0);
    r.sub_status_allowed = zb_json_get_int(j, "sub_status_allowed", 0);
    r.sub_status_accept_all = zb_json_get_int(j, "sub_status_accept_all", 0);
    r.sub_status_gold = zb_json_get_int(j, "sub_status_gold", 0);
    r.sub_status_role_based_accept_all = zb_json_get_int(j, "sub_status_role_based_accept_all", 0);
    r.sub_status_leading_period_removed = zb_json_get_int(j, "sub_status_leading_period_removed", 0);
    r.sub_status_alias_address = zb_json_get_int(j, "sub_status_alias_address", 0);
    r.start_date = get_json_string_copy(j, "start_date", "");
    r.end_date = get_json_string_copy(j, "end_date", "");
    r.error = get_json_string_copy(j, "error", "");

    return r;
}

void zb_get_api_usage_response_free(ZBGetApiUsageResponse* instance) {
    if (!instance) {
        return;
    }
    free(instance->start_date);
    instance->start_date = NULL;
    free(instance->end_date);
    instance->end_date = NULL;
    free(instance->error);
    instance->error = NULL;
}

int compare_zb_get_api_usage_response(const ZBGetApiUsageResponse* response1, const ZBGetApiUsageResponse* response2) {
    return response1->total == response2->total &&
        response1->status_valid == response2->status_valid &&
        response1->status_invalid == response2->status_invalid &&
        response1->status_catch_all == response2->status_catch_all &&
        response1->status_do_not_mail == response2->status_do_not_mail &&
        response1->status_spamtrap == response2->status_spamtrap &&
        response1->status_unknown == response2->status_unknown &&
        response1->sub_status_toxic == response2->sub_status_toxic &&
        response1->sub_status_disposable == response2->sub_status_disposable &&
        response1->sub_status_role_based == response2->sub_status_role_based &&
        response1->sub_status_possible_trap == response2->sub_status_possible_trap &&
        response1->sub_status_global_suppression == response2->sub_status_global_suppression &&
        response1->sub_status_timeout_exceeded == response2->sub_status_timeout_exceeded &&
        response1->sub_status_mail_server_temporary_error == response2->sub_status_mail_server_temporary_error &&
        response1->sub_status_mail_server_did_not_respond == response2->sub_status_mail_server_did_not_respond &&
        response1->sub_status_grey_listed == response2->sub_status_grey_listed &&
        response1->sub_status_anti_spam_system == response2->sub_status_anti_spam_system &&
        response1->sub_status_does_not_accept_mail == response2->sub_status_does_not_accept_mail &&
        response1->sub_status_exception_occurred == response2->sub_status_exception_occurred &&
        response1->sub_status_failed_syntax_check == response2->sub_status_failed_syntax_check &&
        response1->sub_status_mailbox_not_found == response2->sub_status_mailbox_not_found &&
        response1->sub_status_unroutable_ip_address == response2->sub_status_unroutable_ip_address &&
        response1->sub_status_possible_typo == response2->sub_status_possible_typo &&
        response1->sub_status_no_dns_entries == response2->sub_status_no_dns_entries &&
        response1->sub_status_role_based_catch_all == response2->sub_status_role_based_catch_all &&
        response1->sub_status_mailbox_quota_exceeded == response2->sub_status_mailbox_quota_exceeded &&
        response1->sub_status_forcible_disconnect == response2->sub_status_forcible_disconnect &&
        response1->sub_status_failed_smtp_connection == response2->sub_status_failed_smtp_connection &&
        response1->sub_status_mx_forward == response2->sub_status_mx_forward &&
        response1->sub_status_alternate == response2->sub_status_alternate &&
        response1->sub_status_blocked == response2->sub_status_blocked &&
        response1->sub_status_allowed == response2->sub_status_allowed &&
        response1->sub_status_accept_all == response2->sub_status_accept_all &&
        response1->sub_status_gold == response2->sub_status_gold &&
        response1->sub_status_role_based_accept_all == response2->sub_status_role_based_accept_all &&
        response1->sub_status_leading_period_removed == response2->sub_status_leading_period_removed &&
        response1->sub_status_alias_address == response2->sub_status_alias_address &&
        strcmp(response1->start_date, response2->start_date) == 0 &&
        strcmp(response1->end_date, response2->end_date) == 0 &&
        strcmp(response1->error, response2->error) == 0;
}
