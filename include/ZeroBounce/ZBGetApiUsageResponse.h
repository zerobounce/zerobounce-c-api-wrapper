#ifndef ZBGETAPIUSAGERESPONSE_H
#define ZBGETAPIUSAGERESPONSE_H

#include <json.h>

/**
 * @brief The struct associated with the GET /getapiusage request.
 *
 */
typedef struct {
    int total;
    int status_valid;
    int status_invalid;
    int status_catch_all;
    int status_do_not_mail;
    int status_spamtrap;
    int status_unknown;
    int sub_status_toxic;
    int sub_status_disposable;
    int sub_status_role_based;
    int sub_status_possible_trap;
    int sub_status_global_suppression;
    int sub_status_timeout_exceeded;
    int sub_status_mail_server_temporary_error;
    int sub_status_mail_server_did_not_respond;
    int sub_status_grey_listed;
    int sub_status_anti_spam_system;
    int sub_status_does_not_accept_mail;
    int sub_status_exception_occurred;
    int sub_status_failed_syntax_check;
    int sub_status_mailbox_not_found;
    int sub_status_unroutable_ip_address;
    int sub_status_possible_typo;
    int sub_status_no_dns_entries;
    int sub_status_role_based_catch_all;
    int sub_status_mailbox_quota_exceeded;
    int sub_status_forcible_disconnect;
    int sub_status_failed_smtp_connection;
    int sub_status_mx_forward;
    int sub_status_alternate;
    int sub_status_blocked;
    int sub_status_allowed;
    int sub_status_accept_all;
    char* start_date;
    char* end_date;
    char* error;

} ZBGetApiUsageResponse;

/**
 * @brief Function used to initialize a new ZBGetApiUsageResponse.
 *
 * @return ZBGetApiUsageResponse new instance
 */
ZBGetApiUsageResponse new_zb_get_api_usage_response();

/**
 * @brief Function used to serialize a ZBGetApiUsageResponse.
 *
 * @param response ZBGetApiUsageResponse pointer
 * @return char* serialization
 */
char* zb_get_api_usage_response_to_string(ZBGetApiUsageResponse* response);

/**
 * @brief Function used to create ZBGetApiUsageResponse from a json object.
 *
 * @param j json pointer
 * @return ZBGetApiUsageResponse new instance
 */
ZBGetApiUsageResponse zb_get_api_usage_response_from_json(const json_object* j);

/**
 * @brief Function used to compare ZBGetApiUsageResponse instances.
 *
 * @param response1 ZBGetApiUsageResponse pointer
 * @param response2 ZBGetApiUsageResponse pointer
 * @return int 1 if the instances are equal, 0 otherwise
 */
int compare_zb_get_api_usage_response(const ZBGetApiUsageResponse* response1, const ZBGetApiUsageResponse* response2);

#endif