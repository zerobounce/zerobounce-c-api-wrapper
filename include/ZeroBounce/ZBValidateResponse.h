#ifndef ZBVALIDATERESPONSE_H
#define ZBVALIDATERESPONSE_H

#include <stdbool.h>

#include <json.h>

/**
 * Enum that lists all the possible statuses of the email validation result.
 */
typedef enum {
    UnknownValue,
    Valid,
    Invalid,
    CatchAll,
    Spamtrap,
    Abuse,
    DoNotMail
} ZBValidateStatus;

/**
 * @brief Function used to return ZBValidateStatus corresponding to a string.
 *
 * @param string status string
 * @return ZBValidateStatus corresponding status
 */
ZBValidateStatus status_from_string(const char* string);


/**
 * Enum that lists all the possible sub-statuses of the email validation result.
 */
typedef enum {
    None,
    AntispamSystem,
    Greylisted,
    MailServerTemporaryError,
    ForcibleDisconnect,
    MailServerDidNotRespond,
    TimeoutExceeded,
    FailedSmtpConnection,
    MailboxQuotaExceeded,
    ExceptionOccurred,
    PossibleTrap,
    RoleBased,
    GlobalSuppression,
    MailboxNotFound,
    NoDnsEntries,
    FailedSyntaxCheck,
    PossibleTypo,
    UnroutableIpAddress,
    LeadingPeriodRemoved,
    DoesNotAcceptMail,
    AliasAddress,
    RoleBasedCatchAll,
    Disposable,
    Toxic,
    AcceptAll
} ZBValidateSubStatus;

/**
 * @brief Function used to return ZBValidateSubStatus corresponding to a string.
 *
 * @param string status string
 * @return ZBValidateSubStatus corresponding substatus
 */
ZBValidateSubStatus sub_status_from_string(const char* string);


/**
 * @brief The struct associated with the GET /validate request.
 *
 */
typedef struct {
    char* address;
    ZBValidateStatus status;
    ZBValidateSubStatus sub_status;
    char* account;
    char* domain;
    char* did_you_mean;
    char* domain_age_days;
    bool free_email;
    bool mx_found;
    char* mx_record;
    char* smtp_provider;
    char* first_name;
    char* last_name;
    char* gender;
    char* city;
    char* region;
    char* zip_code;
    char* country;
    char* processed_at;
    char* error;
} ZBValidateResponse;

/**
 * @brief Function used to initialize a new ZBValidateResponse.
 *
 * @return ZBValidateResponse new instance
 */
ZBValidateResponse new_zb_validate_response();

/**
 * @brief Function used to serialize a ZBValidateResponse.
 *
 * @param response ZBValidateResponse pointer
 * @return char* serialization
 */
char* zb_validate_response_to_string(ZBValidateResponse* response);

/**
 * @brief Function used to create ZBValidateResponse from a json object.
 *
 * @param j json pointer
 * @return ZBValidateResponse new instance
 */
ZBValidateResponse zb_validate_response_from_json(const json_object* j);

/**
 * @brief Function used to compare ZBValidateResponse instances.
 *
 * @param response1 ZBValidateResponse pointer
 * @param response2 ZBValidateResponse pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_zb_validate_response(const ZBValidateResponse* response1, const ZBValidateResponse* response2);

#endif