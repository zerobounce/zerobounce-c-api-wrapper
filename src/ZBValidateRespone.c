#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBValidateResponse.h"
#include "ZeroBounce/utils.h"

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
    }

    return None;
}

ZBValidateResponse new_zb_validate_response() {
    ZBValidateResponse response;

    response.address = "";
    response.status = UnknownValue;
    response.sub_status = None;
    response.account = "";
    response.domain = "";
    response.did_you_mean = "";
    response.domain_age_days = "";
    response.free_email = false;
    response.mx_found = false;
    response.mx_record = "";
    response.smtp_provider = "";
    response.first_name = "";
    response.last_name = "";
    response.gender = "";
    response.city = "";
    response.region = "";
    response.zip_code = "";
    response.country = "";
    response.processed_at = "";
    response.error = "";

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
        ", free_email=%d"
        ", mx_found=%d"
        ", mx_record='%s'"
        ", smtp_provider='%s'"
        ", first_name='%s'"
        ", last_name='%s'"
        ", gender='%s'"
        ", city='%s'"
        ", region='%s'"
        ", zip_code='%s'"
        ", country='%s'"
        ", processed_at='%s'"
        ", error='%s'"
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
        response->free_email,
        response->mx_found,
        response->mx_record,
        response->smtp_provider,
        response->first_name,
        response->last_name,
        response->gender,
        response->city,
        response->region,
        response->zip_code,
        response->country,
        response->processed_at,
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
        response->address,
        response->status,
        response->sub_status,
        response->account,
        response->domain,
        response->did_you_mean,
        response->domain_age_days,
        response->free_email,
        response->mx_found,
        response->mx_record,
        response->smtp_provider,
        response->first_name,
        response->last_name,
        response->gender,
        response->city,
        response->region,
        response->zip_code,
        response->country,
        response->processed_at,
        response->error
    );
    return buffer;
}

ZBValidateResponse zb_validate_response_from_json(const json_object* j) {
    ZBValidateResponse r = new_zb_validate_response();

    r.address = *(char**)get_json_value(j, json_type_string, "address", &(char*){""});
    r.status = status_from_string(*(char**)get_json_value(j, json_type_string, "status", &(char*){""}));
    r.sub_status = sub_status_from_string(*(char**)get_json_value(j, json_type_string, "sub_status", &(char*){""}));
    r.account = *(char**)get_json_value(j, json_type_string, "account", &(char*){""});
    r.domain = *(char**)get_json_value(j, json_type_string, "domain", &(char*){""});
    r.did_you_mean = *(char**)get_json_value(j, json_type_string, "did_you_mean", &(char*){""});
    r.domain_age_days = *(char**)get_json_value(j, json_type_string, "domain_age_days", &(char*){""});
    r.free_email = *(bool*)get_json_value(j, json_type_boolean, "free_email", &(bool){false});

    if (strcasecmp(*(char**)get_json_value(j, json_type_string, "mx_found", &(char*){"false"}), "true") == 0) {
        r.mx_found = true;
    }

    r.mx_record = *(char**)get_json_value(j, json_type_string, "mx_record", &(char*){""});
    r.smtp_provider = *(char**)get_json_value(j, json_type_string, "smtp_provider", &(char*){""});
    r.first_name = *(char**)get_json_value(j, json_type_string, "firstname", &(char*){""});
    r.last_name = *(char**)get_json_value(j, json_type_string, "lastname", &(char*){""});
    r.gender = *(char**)get_json_value(j, json_type_string, "gender", &(char*){""});
    r.city = *(char**)get_json_value(j, json_type_string, "city", &(char*){""});
    r.region = *(char**)get_json_value(j, json_type_string, "region", &(char*){""});
    r.zip_code = *(char**)get_json_value(j, json_type_string, "zipcode", &(char*){""});
    r.country = *(char**)get_json_value(j, json_type_string, "country", &(char*){""});
    r.processed_at = *(char**)get_json_value(j, json_type_string, "processed_at", &(char*){""});
    r.error = *(char**)get_json_value(j, json_type_string, "error", &(char*){""});

    return r;
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
        response1->free_email == response2->free_email &&
        response1->mx_found == response2->mx_found &&
        strcmp(response1->mx_record, response2->mx_record) == 0 &&
        strcmp(response1->smtp_provider, response2->smtp_provider) == 0 &&
        strcmp(response1->first_name, response2->first_name) == 0 &&
        strcmp(response1->last_name, response2->last_name) == 0 &&
        strcmp(response1->gender, response2->gender) == 0 &&
        strcmp(response1->city, response2->city) == 0 &&
        strcmp(response1->region, response2->region) == 0 &&
        strcmp(response1->zip_code, response2->zip_code) == 0 &&
        strcmp(response1->country, response2->country) == 0 &&
        strcmp(response1->processed_at, response2->processed_at) == 0 &&
        strcmp(response1->error, response2->error) == 0;
}
