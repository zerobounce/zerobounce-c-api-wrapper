#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBValidateResponse.h"
#include "ZeroBounce/utils.h"
#include "ZeroBounce/zb_internal.h"

ZBValidateStatus status_from_string(const char* string) {
    if (strcmp(string, "valid") == 0) {
        return Valid;
    } else if (strcmp(string, "invalid") == 0) {
        return Invalid;
    } else if (strcmp(string, "catch-all") == 0) {
        return CatchAll;
    } else if (strcmp(string, "spamtrap") == 0) {
        return Spamtrap;
    } else if (strcmp(string, "abuse") == 0) {
        return Abuse;
    } else if (strcmp(string, "do_not_mail") == 0) {
        return DoNotMail;
    }

    return UnknownValue;
}

ZBValidateSubStatus sub_status_from_string(const char* string) {
    if (strcmp(string, "antispam_system") == 0) {
        return AntispamSystem;
    } else if (strcmp(string, "greylisted") == 0) {
        return Greylisted;
    } else if (strcmp(string, "mail_server_temporary_error") == 0) {
        return MailServerTemporaryError;
    } else if (strcmp(string, "forcible_disconnect") == 0) {
        return ForcibleDisconnect;
    } else if (strcmp(string, "mail_server_did_not_respond") == 0) {
        return MailServerDidNotRespond;
    } else if (strcmp(string, "timeout_exceeded") == 0) {
        return TimeoutExceeded;
    } else if (strcmp(string, "failed_smtp_connection") == 0) {
        return FailedSmtpConnection;
    } else if (strcmp(string, "mailbox_quota_exceeded") == 0) {
        return MailboxQuotaExceeded;
    } else if (strcmp(string, "exception_occurred") == 0) {
        return ExceptionOccurred;
    } else if (strcmp(string, "possible_trap") == 0) {
        return PossibleTrap;
    } else if (strcmp(string, "role_based") == 0) {
        return RoleBased;
    } else if (strcmp(string, "global_suppression") == 0) {
        return GlobalSuppression;
    } else if (strcmp(string, "mailbox_not_found") == 0) {
        return MailboxNotFound;
    } else if (strcmp(string, "no_dns_entries") == 0) {
        return NoDnsEntries;
    } else if (strcmp(string, "failed_syntax_check") == 0) {
        return FailedSyntaxCheck;
    } else if (strcmp(string, "possible_typo") == 0) {
        return PossibleTypo;
    } else if (strcmp(string, "unroutable_ip_address") == 0) {
        return UnroutableIpAddress;
    } else if (strcmp(string, "leading_period_removed") == 0) {
        return LeadingPeriodRemoved;
    } else if (strcmp(string, "does_not_accept_mail") == 0) {
        return DoesNotAcceptMail;
    } else if (strcmp(string, "alias_address") == 0) {
        return AliasAddress;
    } else if (strcmp(string, "role_based_catch_all") == 0) {
        return RoleBasedCatchAll;
    } else if (strcmp(string, "disposable") == 0) {
        return Disposable;
    } else if (strcmp(string, "toxic") == 0) {
        return Toxic;
    } else if (strcmp(string, "accept_all") == 0) {
        return AcceptAll;
    } else if (strcmp(string, "ai_agent_mailbox") == 0) {
        return AiAgentMailbox;
    }

    return None;
}

ZBValidateResponse new_zb_validate_response(void) {
    ZBValidateResponse response;

    response.address = zb_dup_empty();
    response.status = UnknownValue;
    response.sub_status = None;
    response.account = zb_dup_empty();
    response.domain = zb_dup_empty();
    response.did_you_mean = zb_dup_empty();
    response.domain_age_days = zb_dup_empty();
    response.active_in_days = zb_dup_empty();
    response.active_first_seen = zb_dup_empty();
    response.free_email = false;
    response.mx_found = false;
    response.mx_record = zb_dup_empty();
    response.smtp_provider = zb_dup_empty();
    response.catchall_domain = false;
    response.catchall_domain_set = false;
    response.first_name = zb_dup_empty();
    response.last_name = zb_dup_empty();
    response.gender = zb_dup_empty();
    response.city = zb_dup_empty();
    response.region = zb_dup_empty();
    response.zip_code = zb_dup_empty();
    response.country = zb_dup_empty();
    response.processed_at = zb_dup_empty();
    response.error = zb_dup_empty();
    response.domain_website_exists = zb_dup_empty();
    response.domain_registrant_company_name = zb_dup_empty();
    response.message = zb_dup_empty();

    return response;
}

char* zb_validate_response_to_string(ZBValidateResponse* response) {
    const char *serialization = "ZBValidateResponse{"
        "address='%s'"
        ", status=%d"
        ", sub_status=%d"
        ", account='%s'"
        ", domain='%s'"
        ", did_you_mean='%s'"
        ", domain_age_days='%s'"
        ", active_in_days='%s'"
        ", active_first_seen='%s'"
        ", free_email=%d"
        ", mx_found=%d"
        ", mx_record='%s'"
        ", smtp_provider='%s'"
        ", catchall_domain=%d"
        ", catchall_domain_set=%d"
        ", first_name='%s'"
        ", last_name='%s'"
        ", gender='%s'"
        ", city='%s'"
        ", region='%s'"
        ", zip_code='%s'"
        ", country='%s'"
        ", processed_at='%s'"
        ", error='%s'"
        ", domain_website_exists='%s'"
        ", domain_registrant_company_name='%s'"
        ", message='%s'"
        "}";

    int size = snprintf(
        NULL, 0, serialization,
        response->address,
        response->status,
        response->sub_status,
        response->account,
        response->domain,
        response->did_you_mean,
        response->domain_age_days,
        response->active_in_days,
        response->active_first_seen,
        response->free_email,
        response->mx_found,
        response->mx_record,
        response->smtp_provider,
        response->catchall_domain,
        response->catchall_domain_set,
        response->first_name,
        response->last_name,
        response->gender,
        response->city,
        response->region,
        response->zip_code,
        response->country,
        response->processed_at,
        response->error,
        response->domain_website_exists,
        response->domain_registrant_company_name,
        response->message
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
        response->address,
        response->status,
        response->sub_status,
        response->account,
        response->domain,
        response->did_you_mean,
        response->domain_age_days,
        response->active_in_days,
        response->active_first_seen,
        response->free_email,
        response->mx_found,
        response->mx_record,
        response->smtp_provider,
        response->catchall_domain,
        response->catchall_domain_set,
        response->first_name,
        response->last_name,
        response->gender,
        response->city,
        response->region,
        response->zip_code,
        response->country,
        response->processed_at,
        response->error,
        response->domain_website_exists,
        response->domain_registrant_company_name,
        response->message
    );
    return buffer;
}

ZBValidateResponse zb_validate_response_from_json(const json_object* j) {
    ZBValidateResponse r = new_zb_validate_response();
    /* Every string field is assigned below; drop the initial copies. */
    zb_validate_response_free(&r);

    /* E7: a result names its address and status, as JSON strings. The one
       answer without them is a greylist_processing validate still in
       progress, which carries its text in Message. */
    json_object* message_obj = NULL;
    bool greylist_answer = json_object_object_get_ex(j, "Message", &message_obj) &&
        json_object_is_type(message_obj, json_type_string) &&
        json_object_get_string_len(message_obj) > 0;
    if (!greylist_answer) {
        zb_json_require_string(j, "address");
        zb_json_require_string(j, "status");
    }

    r.address = get_json_string_copy(j, "address", "");
    char* status_text = get_json_string_copy(j, "status", "");
    r.status = status_from_string(status_text);
    free(status_text);
    char* sub_status_text = get_json_string_copy(j, "sub_status", "");
    r.sub_status = sub_status_from_string(sub_status_text);
    free(sub_status_text);
    r.account = get_json_string_copy(j, "account", "");
    r.domain = get_json_string_copy(j, "domain", "");
    r.did_you_mean = get_json_string_copy(j, "did_you_mean", "");
    r.domain_age_days = get_json_string_copy(j, "domain_age_days", "");
    r.active_in_days = get_json_string_copy(j, "active_in_days", "");
    r.active_first_seen = get_json_string_copy(j, "active_first_seen", "");
    r.free_email = zb_json_get_bool(j, "free_email", false);

    char* mx_found = get_json_string_copy(j, "mx_found", "false");
    if (strcasecmp(mx_found, "true") == 0) {
        r.mx_found = true;
    }
    free(mx_found);

    r.mx_record = get_json_string_copy(j, "mx_record", "");
    r.smtp_provider = get_json_string_copy(j, "smtp_provider", "");

    json_object* catchall_domain_obj = NULL;
    if (json_object_object_get_ex(j, "catchall_domain", &catchall_domain_obj)
        && json_object_is_type(catchall_domain_obj, json_type_boolean)) {
        r.catchall_domain_set = true;
        r.catchall_domain = json_object_get_boolean(catchall_domain_obj);
    } else if (catchall_domain_obj != NULL) {
        zb_shape_mismatch("catchall_domain");
    }

    r.first_name = get_json_string_copy(j, "firstname", "");
    r.last_name = get_json_string_copy(j, "lastname", "");
    r.gender = get_json_string_copy(j, "gender", "");
    r.city = get_json_string_copy(j, "city", "");
    r.region = get_json_string_copy(j, "region", "");
    r.zip_code = get_json_string_copy(j, "zipcode", "");
    r.country = get_json_string_copy(j, "country", "");
    r.processed_at = get_json_string_copy(j, "processed_at", "");
    r.error = get_json_string_copy(j, "error", "");
    r.domain_website_exists = get_json_string_copy(j, "domain_website_exists", "");
    r.domain_registrant_company_name = get_json_string_copy(j, "domain_registrant_company_name", "");
    /* A repeated greylist_processing request answers with nothing but this. */
    r.message = get_json_string_copy(j, "Message", "");

    return r;
}

void zb_validate_response_free(ZBValidateResponse* instance) {
    if (!instance) {
        return;
    }
    free(instance->address);
    instance->address = NULL;
    free(instance->account);
    instance->account = NULL;
    free(instance->domain);
    instance->domain = NULL;
    free(instance->did_you_mean);
    instance->did_you_mean = NULL;
    free(instance->domain_age_days);
    instance->domain_age_days = NULL;
    free(instance->active_in_days);
    instance->active_in_days = NULL;
    free(instance->active_first_seen);
    instance->active_first_seen = NULL;
    free(instance->mx_record);
    instance->mx_record = NULL;
    free(instance->smtp_provider);
    instance->smtp_provider = NULL;
    free(instance->first_name);
    instance->first_name = NULL;
    free(instance->last_name);
    instance->last_name = NULL;
    free(instance->gender);
    instance->gender = NULL;
    free(instance->city);
    instance->city = NULL;
    free(instance->region);
    instance->region = NULL;
    free(instance->zip_code);
    instance->zip_code = NULL;
    free(instance->country);
    instance->country = NULL;
    free(instance->processed_at);
    instance->processed_at = NULL;
    free(instance->error);
    instance->error = NULL;
    free(instance->domain_website_exists);
    instance->domain_website_exists = NULL;
    free(instance->domain_registrant_company_name);
    instance->domain_registrant_company_name = NULL;
    free(instance->message);
    instance->message = NULL;
}

int compare_zb_validate_response(const ZBValidateResponse* response1, const ZBValidateResponse* response2) {
    return
        strcmp(response1->address, response2->address) == 0 &&
        response1->status == response2->status &&
        response1->sub_status == response2->sub_status &&
        strcmp(response1->account, response2->account) == 0 &&
        strcmp(response1->domain, response2->domain) == 0 &&
        strcmp(response1->did_you_mean, response2->did_you_mean) == 0 &&
        strcmp(response1->domain_age_days, response2->domain_age_days) == 0 &&
        strcmp(response1->active_in_days, response2->active_in_days) == 0 &&
        strcmp(response1->active_first_seen, response2->active_first_seen) == 0 &&
        response1->free_email == response2->free_email &&
        response1->mx_found == response2->mx_found &&
        strcmp(response1->mx_record, response2->mx_record) == 0 &&
        strcmp(response1->smtp_provider, response2->smtp_provider) == 0 &&
        response1->catchall_domain == response2->catchall_domain &&
        response1->catchall_domain_set == response2->catchall_domain_set &&
        strcmp(response1->first_name, response2->first_name) == 0 &&
        strcmp(response1->last_name, response2->last_name) == 0 &&
        strcmp(response1->gender, response2->gender) == 0 &&
        strcmp(response1->city, response2->city) == 0 &&
        strcmp(response1->region, response2->region) == 0 &&
        strcmp(response1->zip_code, response2->zip_code) == 0 &&
        strcmp(response1->country, response2->country) == 0 &&
        strcmp(response1->processed_at, response2->processed_at) == 0 &&
        strcmp(response1->error, response2->error) == 0 &&
        strcmp(response1->domain_website_exists, response2->domain_website_exists) == 0 &&
        strcmp(response1->domain_registrant_company_name, response2->domain_registrant_company_name) == 0 &&
        strcmp(response1->message, response2->message) == 0;
}
