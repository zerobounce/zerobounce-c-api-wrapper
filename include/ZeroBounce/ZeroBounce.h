#ifndef ZEROBOUNCE_H
#define ZEROBOUNCE_H

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>
#include <curl/header.h>

#include "ZeroBounce/ZBErrorResponse.h"
#include "ZeroBounce/ZBCreditsResponse.h"
#include "ZeroBounce/ZBGetApiUsageResponse.h"
#include "ZeroBounce/ZBValidateResponse.h"
#include "ZeroBounce/ZBValidateBatchResponse.h"
#include "ZeroBounce/ZBSendFileResponse.h"
#include "ZeroBounce/ZBFileStatusResponse.h"
#include "ZeroBounce/ZBGetFileResponse.h"
#include "ZeroBounce/ZBDeleteFileResponse.h"
#include "ZeroBounce/ZBActivityDataResponse.h"
#include "ZeroBounce/ZBFindEmailResponse.h"
#include "ZeroBounce/ZBDomainSearchResponse.h"
#include "ZeroBounce/ZBApiURL.h"

typedef void (*OnErrorCallback)(ZBErrorResponse error_response);
typedef void (*OnSuccessCallbackCredits)(ZBCreditsResponse response);
typedef void (*OnSuccessCallbackApiUsage)(ZBGetApiUsageResponse response);
typedef void (*OnSuccessCallbackValidate)(ZBValidateResponse response);
typedef void (*OnSuccessCallbackValidateBatch)(ZBValidateBatchResponse response);
typedef void (*OnSuccessCallbackSendFile)(ZBSendFileResponse response);
typedef void (*OnSuccessCallbackFileStatus)(ZBFileStatusResponse response);
typedef void (*OnSuccessCallbackGetFile)(ZBGetFileResponse response);
typedef void (*OnSuccessCallbackDeleteFile)(ZBDeleteFileResponse response);
typedef void (*OnSuccessCallbackActivityData)(ZBActivityDataResponse response);
typedef void (*OnSuccessCallbackFindEmail)(ZBFindEmailResponse response);
typedef void (*OnSuccessCallbackDomainSearch)(ZBDomainSearchResponse response);


/**
 * Struct representing the options for sending files in requests.
 */
typedef struct {
    char* return_url;
    int first_name_column;
    int last_name_column;
    int gender_column;
    int ip_address_column;
    bool has_header_row;
    bool remove_duplicate;
} SendFileOptions;

/**
 * @brief Function used to initialize a new SendFileOptions.
 *
 * @return SendFileOptions new instance
 */
SendFileOptions new_send_file_options();


/**
 * Struct used to store the response data of a request.
 */
typedef struct {
  char *response;
  size_t size;
} memory;


/**
 * @brief Internal function used to write the response data of a request.
 *
 */
static size_t write_callback(void *data, size_t size, size_t nmemb, void *clientp);

/**
 * @brief Internal function used to set the write callback for a request.
 *
 */
void set_write_callback(CURL* curl, memory* response_data);

/**
 * @brief Internal function used to get the response code of a request.
 *
 */
long get_http_code(CURL* curl);

/**
 * @brief Internal function used to get the content type header of a request.
 *
 */
char* get_content_type_value(CURL* curl);


/**
 * Main struct used to handle the requests of the ZeroBounce API.
 */
typedef struct {
    char* api_key;
    char* api_base_url;
    const char* bulk_api_base_url;
    const char* bulk_api_scoring_base_url;
} ZeroBounce;

static ZeroBounce* zero_bounce_instance = NULL;

/**
 * @brief Initializes new ZeroBounce instance.
 *
 * @return ZeroBounce* pointer to instance
 */
ZeroBounce* new_zero_bounce_instance();

/**
 * @brief Get pointer to ZeroBounce instance.
 *
 * @return ZeroBounce* pointer to instance
 */
ZeroBounce* zero_bounce_get_instance();

/**
 * @brief Initializes the wrapper with the default base URL.
 *
 * @param zb ZeroBounce pointer
 * @param api_key the API key
 */
void zero_bounce_initialize(ZeroBounce* zb, const char* api_key);

/**
 * @brief Initializes the wrapper.
 *
 * @param zb ZeroBounce pointer
 * @param api_key the API key
 */
void zero_bounce_initialize_with_base_url(ZeroBounce* zb, const char* api_key, ZBApiURL api_base_url);

/**
 * @brief Checks if the [api_key] is invalid or not and if it is, then it throws an error through the provided
 * [error_callback].
 *
 * @param zb ZeroBounce pointer
 * @param error_callback error callback
 * @return **true** if the [api_key] is null or **false** otherwise
 */
static bool zero_bounce_invalid_api_key(ZeroBounce *zb, OnErrorCallback error_callback);

/**
 * @brief Internal function used to make libcurl requests.
 *
 * @param url_path url to make request to
 * @param request_type type of the request (e.g. "GET")
 * @param header header for the request
 * @param response_data response of the request
 * @param http_code response code
 * @param error_callback error callback
 * @return int 1 if the request is successful, 0 otherwise
 */
static int make_request(
    char* url_path,
    char* request_type,
    char* header,
    memory* response_data,
    long* http_code,
    OnErrorCallback error_callback
);

/**
 * @brief Internal function to send a file for bulk email validation. This function implements the actual
 * request logic.
 *
 * @param zb                            ZeroBounce pointer
 * @param scoring                       *true* if the AI scoring should be used, or *false* otherwise
 * @param file_path                     the path of the file to send
 * @param email_address_column_index    the column index of the email address in the file. Index starts from 1
 * @param options                       the send file options
 * @param success_callback              success callback
 * @param error_callback                error callback
 */
static void send_file_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_path,
    int email_address_column_index,
    SendFileOptions options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Internal function to get the status of a file submitted for email validation. This method implements
 * the actual request logic.
 *
 * @param zb                ZeroBounce pointer
 * @param scoring           *true* if the AI scoring should be used, or *false* otherwise
 * @param file_id           the returned file ID when calling either the send file or scoring send file APIs
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
static void file_status_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Internal function to get the validation results file for the file that has been submitted.
 * This method implements the actual request logic.
 *
 * @param zb                    ZeroBounce pointer
 * @param scoring               *true* if the AI scoring should be used, or *false* otherwise
 * @param file_id               the returned file ID when calling either the send file or scoring send file APIs
 * @param local_download_path   the path to which to download the file
 * @param success_callback      success callback
 * @param error_callback        error callback
 */
static void get_file_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_id,
    char* local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Internal function to elete a file. This method implements the actual request logic.
 *
 * @param zb                ZeroBounce pointer
 * @param scoring           *true* if the AI scoring should be used, or *false* otherwise
 * @param file_id           the returned file ID when calling either the send file or scoring send file APIs
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
static void delete_file_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Email Address Search - Identifies and validates a person’s primary email address
 *
 * @param zb                ZeroBounce pointer
 * @param domain            The email domain for which to find the email format
 * @param company_name      The company name for which to find the email format
 * @param first_name        The first name of the person whose email format is being searched
 * @param middle_name       The middle name of the person whose email format is being searched
 * @param last_name         The last name of the person whose email format is being searched
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
static void find_email_internal(
    ZeroBounce* zb,
    char* domain,
    char* company_name,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Domain Search - Identifies an email domain
 *
 * @param zb                ZeroBounce pointer
 * @param domain            The email domain for which to find the email format
 * @param company_name      The company name for which to identify the domain
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
static void search_domain_internal(
    ZeroBounce* zb,
    char* domain,
    char* company_name,
    OnSuccessCallbackDomainSearch success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief This API will tell you how many credits you have left on your account.
 *
 * @param zb                ZeroBounce pointer
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void get_credits(
    ZeroBounce *zb,
    OnSuccessCallbackCredits success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Returns the API usage between the given dates.
 *
 * @param zb                ZeroBounce pointer
 * @param start_date        the start date of when you want to view API usage
 * @param end_date          the end date of when you want to view API usage
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void get_api_usage(
    ZeroBounce *zb,
    struct tm start_date,
    struct tm end_date,
    OnSuccessCallbackApiUsage success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Validates the given email.
 *
 * @param zb                ZeroBounce pointer
 * @param email             the email address you want to validate
 * @param ip_address        the IP Address the email signed up from (Can be blank)
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void validate_email(
    ZeroBounce *zb,
    char* email,
    char* ip_address,
    OnSuccessCallbackValidate success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Validates a batch of emails.
 *
 * @param zb                ZeroBounce pointer
 * @param email_batch       EmailToValidateVector containing ZBEmailToValidate items to validate
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void validate_email_batch(
    ZeroBounce *zb,
    EmailToValidateVector email_batch,
    OnSuccessCallbackValidateBatch success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief The send file API allows user to send a file for bulk email validation.
 *
 * @param zb                            ZeroBounce pointer
 * @param file_path                     path of the file to send
 * @param email_address_column_index    the column index of the email address in the file. Index starts from 1.
 * @param options                       send file options
 * @param success_callback              success callback
 * @param error_callback                error callback
 */
void send_file(
    ZeroBounce *zb,
    char* file_path,
    int email_address_column_index,
    SendFileOptions options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Returns the status of a file submitted for email validation.
 *
 * @param zb                ZeroBounce pointer
 * @param file_id           the returned file ID when calling send file API
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void file_status(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief The get file API allows users to get the validation results file for the file been submitted using send file API.
 *
 * @param zb                    ZeroBounce pointer
 * @param file_id               the returned file ID when calling send file API
 * @param local_download_path   the path to which to download the file
 * @param success_callback      success callback
 * @param error_callback        error callback
 */
void get_file(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Delete a file.
 *
 * @param zb                ZeroBounce pointer
 * @param file_id           the returned file ID when calling send file API
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void delete_file(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief The scoring send file API allows user to send a file for bulk email validation.
 *
 * @param zb                            ZeroBounce pointer
 * @param file_path                     path of the file to send
 * @param email_address_column_index    the column index of the email address in the file. Index starts from 1.
 * @param options                       send file options
 * @param success_callback              success callback
 * @param error_callback                error callback
 */
void scoring_send_file(
    ZeroBounce *zb,
    char* file_path,
    int email_address_column_index,
    SendFileOptions options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Returns the status of a file submitted for email validation using the AI Scoring request.
 *
 * @param zb                ZeroBounce pointer
 * @param file_id           the returned file ID when calling scoring send file API
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void scoring_file_status(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief The scoring get file API allows users to get the validation results file for the file been submitted using
 * scoring send file API.
 *
 * @param zb                    ZeroBounce pointer
 * @param file_id               the returned file ID when calling scoring send file API
 * @param local_download_path   the path to which to download the file
 * @param success_callback      success callback
 * @param error_callback        error callback
 */
void scoring_get_file(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Delete a file submitted using the scoring API.
 *
 * @param zb                ZeroBounce pointer
 * @param file_id           the returned file ID when calling scoring send file API
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void scoring_delete_file(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief The request returns data regarding opens, clicks, forwards and unsubscribes that have taken place in the past
 * 30, 90, 180 or 365 days.
 *
 * @param zb                ZeroBounce pointer
 * @param email             email address
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void get_activity_data(
    ZeroBounce *zb,
    char* email,
    OnSuccessCallbackActivityData success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Email Address Search - Identifies and validates a person’s primary email address
 *
 * @param zb                ZeroBounce pointer
 * @param domain            The email domain for which to find the email format
 * @param first_name        The first name of the person whose email format is being searched
 * @param middle_name       The middle name of the person whose email format is being searched
 * @param last_name         The last name of the person whose email format is being searched
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void find_email_by_domain_first_middle_last_name(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Email Address Search - Identifies and validates a person’s primary email address
 *
 * @param zb                ZeroBounce pointer
 * @param domain            The email domain for which to find the email format
 * @param first_name        The first name of the person whose email format is being searched
 * @param last_name         The last name of the person whose email format is being searched
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void find_email_by_domain_first_last_name(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Email Address Search - Identifies and validates a person’s primary email address
 *
 * @param zb                ZeroBounce pointer
 * @param domain            The email domain for which to find the email format
 * @param first_name        The first name of the person whose email format is being searched
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void find_email_by_domain_first_name(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Email Address Search - Identifies and validates a person’s primary email address
 *
 * @param zb                ZeroBounce pointer
 * @param company_name      The company name for which to find the email format
 * @param first_name        The first name of the person whose email format is being searched
 * @param middle_name       The middle name of the person whose email format is being searched
 * @param last_name         The last name of the person whose email format is being searched
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void find_email_by_company_name_first_middle_last_name(
    ZeroBounce* zb,
    char* company_name,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Email Address Search - Identifies and validates a person’s primary email address
 *
 * @param zb                ZeroBounce pointer
 * @param company_name      The company name for which to find the email format
 * @param first_name        The first name of the person whose email format is being searched
 * @param middle_name       The middle name of the person whose email format is being searched
 * @param last_name         The last name of the person whose email format is being searched
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void find_email_by_company_name_first_last_name(
    ZeroBounce* zb,
    char* company_name,
    char* first_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Email Address Search - Identifies and validates a person’s primary email address
 *
 * @param zb                ZeroBounce pointer
 * @param company_name      The company name for which to find the email format
 * @param first_name        The first name of the person whose email format is being searched
 * @param middle_name       The middle name of the person whose email format is being searched
 * @param last_name         The last name of the person whose email format is being searched
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void find_email_by_company_name_first_name(
    ZeroBounce* zb,
    char* company_name,
    char* first_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Domain Search - Identifies an email domain
 *
 * @param zb                ZeroBounce pointer
 * @param domain            The email domain for which to find the email format
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void search_domain_by_domain(
    ZeroBounce* zb,
    char* domain,
    OnSuccessCallbackDomainSearch success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Domain Search - Identifies an email domain
 *
 * @param zb                ZeroBounce pointer
 * @param company_name     The company name for which to identify the domain
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
void search_domain_by_company_name(
    ZeroBounce* zb,
    char* company_name,
    OnSuccessCallbackDomainSearch success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Email Address Search - Identifies and validates a person’s primary email address
 *
 * @param zb                ZeroBounce pointer
 * @param domain            The email domain for which to find the email format
 * @param first_name        The first name of the person whose email format is being searched
 * @param middle_name       The middle name of the person whose email format is being searched
 * @param last_name         The last name of the person whose email format is being searched
 * @param success_callback  success callback
 * @param error_callback    error callback
 */
#warning "Use find_email... methods for Email Finder API, or search_domain... methods for Domain Search API"
void find_email(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
);

#endif