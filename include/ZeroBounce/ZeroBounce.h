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
#include "ZeroBounce/ZBScoreEmailResponse.h"
#include "ZeroBounce/ZBFindEmailResponse.h"
#include "ZeroBounce/ZBDomainSearchResponse.h"
#include "ZeroBounce/ZBApiURL.h"
#include "ZeroBounce/ZBPriority3Options.h"

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
typedef void (*OnSuccessCallbackScoreEmail)(ZBScoreEmailResponse response);
typedef void (*OnSuccessCallbackFindEmail)(ZBFindEmailResponse response);
typedef void (*OnSuccessCallbackDomainSearch)(ZBDomainSearchResponse response);
/** Raw JSON body; valid only for the duration of the callback. */
typedef void (*OnSuccessCallbackRawJson)(const char* json_response);


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
    /** When set, sent as allow_phase_2 on validation sendfile only (not scoring). */
    bool allow_phase_2_is_set;
    bool allow_phase_2;
} SendFileOptions;

/** Bulk getfile download_type query values (validation and scoring). */
#define ZB_DOWNLOAD_TYPE_PHASE_1 "phase_1"
#define ZB_DOWNLOAD_TYPE_PHASE_2 "phase_2"
#define ZB_DOWNLOAD_TYPE_COMBINED "combined"

/**
 * Optional query parameters for bulk getfile (v2).
 * download_type: NULL to omit. Use ZB_DOWNLOAD_TYPE_* constants.
 * activity_data: -1 to omit, 0 false, 1 true — validation get_file only (ignored for scoring_get_file).
 */
typedef struct {
    const char *download_type;
    int activity_data;
} GetFileOptions;

/**
 * @brief Function used to initialize a new SendFileOptions.
 *
 * @return SendFileOptions new instance
 */
SendFileOptions new_send_file_options();

/**
 * @brief Default GetFileOptions (no download_type, activity_data omitted).
 */
GetFileOptions new_get_file_options(void);

/**
 * Optional query/body parameters for GET /validate and POST /validatebatch.
 * timeout: -1 to omit. activity_data / verify_plus: -1 omit, 0 false, 1 true.
 */
typedef struct {
    int timeout;
    int activity_data;
    int verify_plus;
    /** -1 omit, 0 false, 1 true. Fills domain_website_exists and
     *  domain_registrant_company_name on the response. */
    int domain_info;
    /** -1 omit, 0 false, 1 true. Retries a greylisted address in the background. */
    int greylist_processing;
    /**
     * Where the greylist retry posts its result. NULL to omit. It needs
     * greylist_processing set to 1 and is rejected without it, and when blank,
     * before any request is sent.
     */
    const char *callback_url;
    /** -1 omit, 1 use form-encoded POST for /validate */
    int use_post;
} ZBValidateOptions;

/**
 * @brief Default ZBValidateOptions (all parameters omitted).
 */
ZBValidateOptions new_validate_options(void);

/**
 * @brief Whether a getfile response body looks like a JSON error payload (including HTTP 200).
 */
bool zb_get_file_json_indicates_error(const char *body);

/**
 * @brief Human-readable message from a getfile JSON error body. Caller must free() the returned string.
 */
char *zb_format_get_file_error_message(const char *body);


/** Cap JSON API responses so error pages cannot exhaust memory (F-01). */
#define ZB_MAX_JSON_RESPONSE_BYTES (16u * 1024u * 1024u)
/** Cap bulk getfile downloads (F-01). */
#define ZB_MAX_GETFILE_BYTES (256u * 1024u * 1024u)

/**
 * Struct used to store the response data of a request.
 * max_bytes: 0 uses ZB_MAX_JSON_RESPONSE_BYTES; set ZB_MAX_GETFILE_BYTES for getfile.
 */
typedef struct {
  char *response;
  size_t size;
  size_t max_bytes;
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
    const char* bulk_api_root_url;
    /**
     * The list evaluator and the Inbox Placement Tester have their own base
     * URLs, not paths built from the bulk one. Overriding api_base_url alone
     * leaves them pointing at the live API.
     */
    const char* list_evaluator_base_url;
    const char* inbox_placement_base_url;
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
    const GetFileOptions *get_file_options,
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
 * @brief Validates the given email with optional v2 query parameters.
 */
void validate_email_with_options(
    ZeroBounce *zb,
    char* email,
    char* ip_address,
    const ZBValidateOptions *options,
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
 * @brief Validates a batch of emails with optional v2 body parameters.
 */
void validate_email_batch_with_options(
    ZeroBounce *zb,
    EmailToValidateVector email_batch,
    const ZBValidateOptions *options,
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
 * @brief As get_file, but written straight to disk as it arrives instead of
 * being held in memory first — use it for a download too large to buffer.
 *
 * It is opt-in because a streamed body cannot be inspected as a whole, and
 * getfile answers an unknown or deleted file id with a JSON error rather than
 * a file. The first 4 KB are held back and checked, so an error still reaches
 * the error callback and no partial file is left behind.
 *
 * @param zb                    ZeroBounce pointer
 * @param file_id               the returned file ID when calling send file API
 * @param local_download_path   the path to which to download the file
 * @param options               optional v2 query parameters, or NULL
 * @param success_callback      success callback
 * @param error_callback        error callback
 */
void get_file_streaming(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
);

/** @brief The AI scoring counterpart of get_file_streaming. */
void scoring_get_file_streaming(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Bulk get file with optional v2 query parameters (download_type, activity_data for validation only).
 */
void get_file_with_options(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *options,
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
 * @brief Scoring bulk get file with optional download_type (activity_data is not sent).
 */
void scoring_get_file_with_options(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *options,
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
 * @brief Real-time AI scoring for a single email (GET /scoring).
 */
void score_email(
    ZeroBounce *zb,
    char* email,
    OnSuccessCallbackScoreEmail success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Real-time AI scoring for a single email (POST /scoring, form-encoded).
 */
void score_email_post(
    ZeroBounce *zb,
    char* email,
    OnSuccessCallbackScoreEmail success_callback,
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

/* Priority 3 — bulk email finder, domain search, list evaluator, filters, S3 */

void send_email_finder_file(
    ZeroBounce *zb,
    char *file_path,
    int domain_column,
    const EmailFinderSendFileOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void email_finder_file_status(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
);

void email_finder_get_file(
    ZeroBounce *zb,
    char *file_id,
    char *local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
);

void email_finder_delete_file(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
);

void send_domain_search_file(
    ZeroBounce *zb,
    char *file_path,
    int domain_column,
    const DomainSearchSendFileOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void domain_search_file_status(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
);

void domain_search_get_file(
    ZeroBounce *zb,
    char *file_id,
    char *local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
);

void domain_search_delete_file(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
);

void send_list_evaluator_file(
    ZeroBounce *zb,
    char *file_path,
    int email_address_column,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void get_list_evaluator_result(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

void send_evaluated_file(
    ZeroBounce *zb,
    char *file_path,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void add_filter(
    ZeroBounce *zb,
    char *rule,
    char *target,
    char *value,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

void list_filters(
    ZeroBounce *zb,
    bool use_post,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

void delete_filter(
    ZeroBounce *zb,
    char *rule,
    char *target,
    char *value,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

void validate_s3_file(
    ZeroBounce *zb,
    const ValidateS3FileOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void score_s3_file(
    ZeroBounce *zb,
    const ScoreS3FileOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void export_s3_file(
    ZeroBounce *zb,
    char *file_id,
    char *export_bucket,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void list_s3_files(
    ZeroBounce *zb,
    const char *bucket,
    const char *search,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

/* The base URL overrides. Each one keeps the pointer it is given rather than a
 * copy, so the string has to outlive the client. */

/** @brief Overrides the list evaluator base URL. */
void zero_bounce_set_list_evaluator_base_url(ZeroBounce *zb, const char *url);

/** @brief Overrides the Inbox Placement Tester base URL. */
void zero_bounce_set_inbox_placement_base_url(ZeroBounce *zb, const char *url);

/** @brief Overrides the bulk file base URL. Ends in /v2. */
void zero_bounce_set_bulk_api_base_url(ZeroBounce *zb, const char *url);

/**
 * @brief Overrides the bulk host root, which the S3 and SFTP/FTP jobs use.
 *
 * This one has no /v2 on the end.
 */
void zero_bounce_set_bulk_api_root_url(ZeroBounce *zb, const char *url);

/** @brief Overrides the bulk AI scoring base URL. Ends in /v2/scoring. */
void zero_bounce_set_bulk_api_scoring_base_url(ZeroBounce *zb, const char *url);

/**
 * @brief The documented fields shared by the six S3 and SFTP/FTP file jobs.
 *
 * LIVE: file_name and return_url are required by every one of them,
 * validate_sftp_file included, although the docs call its file_name optional.
 * remove_duplicates is -1 to omit, 0 false, 1 true.
 */
typedef struct {
    const char *file_name;
    bool has_header_row;
    int remove_duplicates;
    /** NULL to omit. */
    const char *return_url;
} ZBBulkFileJobOptions;

/** @brief has_header_row true, remove_duplicates and return_url omitted. */
ZBBulkFileJobOptions new_bulk_file_job_options(void);

/* S3 bulk — https://www.zerobounce.net/docs/api-integrations/amazon-s3-bulk-api */

void email_finder_s3_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void domain_search_s3_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

/* SFTP/FTP bulk — https://www.zerobounce.net/docs/api-integrations/sftp_bulk_api
 * The server, path and protocol are never sent by the SDK; they come from your
 * ZeroBounce settings. */

/**
 * @brief Validates a file from your SFTP/FTP server.
 *
 * LIVE: the docs call this job's file_name optional, but the API answers
 * "Empty parameter: file_name" without it, so it is required here too.
 */
void validate_sftp_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void score_sftp_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void email_finder_sftp_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

void domain_search_sftp_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Exports a validated or scored file back to your SFTP/FTP server.
 *
 * Note the field name: this endpoint takes export_path, not the export_bucket
 * that export_s3_file sends. Pass NULL to use the one from your settings.
 */
void export_sftp_file(
    ZeroBounce *zb,
    const char *file_id,
    const char *export_path,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

/* Inbox Placement Tester (/api/ipt). Its own host, camelCase JSON bodies, and
 * errors under a "detail" key. Spends the account's inbox-test credits.
 *
 * LIVE: the key travels in an Authorization: Bearer header. Sent as an api_key
 * query parameter the request is rejected as unauthorized, and the key would
 * end up in whatever logs the query string. */

/**
 * @brief One entry of a new test's providers[] array.
 *
 * id is the provider id from get_inbox_placement_providers. max_seeds caps how
 * many of that provider's seed addresses the test uses; -1 leaves it out.
 */
typedef struct {
    int id;
    int max_seeds;
} ZBInboxPlacementProvider;

/**
 * @brief The optional fields of a new test. Only what is set is sent.
 *
 * test_type is 1, 2 or 3, and a test_type of 1 requires sender_email.
 * The three arrays are left out when their count is 0.
 */
typedef struct {
    /** -1 to omit. 1, 2 or 3 otherwise. */
    int test_type;
    /** Required when test_type is 1. */
    const char *sender_email;
    const char *webhook_url;
    /** Provider ids from get_inbox_placement_providers. */
    const int *provider_ids;
    size_t provider_ids_count;
    /** Seed ids to use instead of the whole seed list. */
    const int *seed_ids;
    size_t seed_ids_count;
    /** Per-provider seed caps. */
    const ZBInboxPlacementProvider *providers;
    size_t providers_count;
} ZBCreateInboxPlacementTestOptions;

ZBCreateInboxPlacementTestOptions new_create_inbox_placement_test_options(void);

/**
 * @brief The documented filters of the test list.
 *
 * page / page_size: -1 to omit; page_size has to be between 1 and 100.
 * created_after / created_before are the documented date filters; NULL omits
 * them.
 */
typedef struct {
    const char *status;
    int page;
    int page_size;
    const char *created_after;
    const char *created_before;
} ZBListInboxPlacementTestsOptions;

ZBListInboxPlacementTestsOptions new_list_inbox_placement_tests_options(void);

void get_inbox_placement_providers(
    ZeroBounce *zb,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

void get_inbox_placement_test_types(
    ZeroBounce *zb,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

/**
 * @brief Starts a new inbox placement test.
 *
 * name is required and is at most 64 characters.
 */
void create_inbox_placement_test(
    ZeroBounce *zb,
    const char *name,
    const ZBCreateInboxPlacementTestOptions *options,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

void get_inbox_placement_test(
    ZeroBounce *zb,
    const char *test_id,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

void list_inbox_placement_tests(
    ZeroBounce *zb,
    const ZBListInboxPlacementTestsOptions *options,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

void delete_inbox_placement_test(
    ZeroBounce *zb,
    const char *test_id,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

void get_inbox_placement_webhook(
    ZeroBounce *zb,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
);

void set_inbox_placement_webhook(
    ZeroBounce *zb,
    const char *webhook_url,
    OnSuccessCallbackRawJson success_callback,
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

/**
 * @brief Test hook: when set, HTTP perform uses this instead of libcurl.
 * response_data and http_code must be filled by the function.
 * content_type_out may be set when non-NULL (caller frees).
 * @return 1 on success, 0 on failure
 */
typedef int (*ZBHttpPerformFn)(memory* response_data, long* http_code, char** content_type_out);
void zero_bounce_set_http_perform_for_test(ZBHttpPerformFn fn);

/**
 * @brief Test hook: returns the URL of the most recent validatebatch request.
 */
const char* zero_bounce_get_last_request_url_for_test(void);

/**
 * @brief Test hook: returns the JSON body of the most recent validatebatch request.
 */
const char* zero_bounce_get_last_post_body_for_test(void);

/**
 * Thread-local snapshot of the request about to be performed.
 * Safe to read from the HTTP test hook on the same thread.
 */
const char *zero_bounce_test_method(void);
const char *zero_bounce_test_url(void);
const char *zero_bounce_test_body(void);
const char *zero_bounce_test_extra_header(void);
int zero_bounce_test_field_count(void);
const char *zero_bounce_test_field_name(int index);
const char *zero_bounce_test_field_value(int index);

#endif