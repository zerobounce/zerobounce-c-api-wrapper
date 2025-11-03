#include <curl/curl.h>
#include "unity.h"
#include "fff.h"

#include "ZeroBounce/ZeroBounce.h"

DEFINE_FFF_GLOBALS;

static ZeroBounce* zb;
static ZBErrorResponse expected_error_response;
static ZBCreditsResponse expected_credits_response;
static ZBGetApiUsageResponse expected_api_usage_response;
static ZBValidateResponse expected_validate_response;
static ZBValidateBatchResponse expected_validate_batch_response;
static ZBSendFileResponse expected_send_file_response;
static ZBFileStatusResponse expected_file_status_response;
static ZBGetFileResponse expected_get_file_response;
static ZBDeleteFileResponse expected_delete_file_response;
static ZBActivityDataResponse expected_activity_data_response;
static ZBFindEmailResponse expected_find_email_response;
static ZBDomainSearchResponse expected_domain_search_response;

static char* response_json = "";

void on_error_valid(ZBErrorResponse error_response) {
    TEST_FAIL_MESSAGE(zb_error_response_to_string(&error_response));
}

void on_error_invalid(ZBErrorResponse error_response) {
    TEST_ASSERT_EQUAL_INT(1, compare_zb_error_response(&expected_error_response, &error_response));
}

void on_success_credits_valid(ZBCreditsResponse response) {
    TEST_ASSERT_EQUAL_INT(1, compare_zb_credits_response(&expected_credits_response, &response));
}

void on_success_credits_invalid(ZBCreditsResponse response) {
    TEST_FAIL_MESSAGE(zb_credits_response_to_string(&response));
}

void on_success_api_usage_valid(ZBGetApiUsageResponse response) {
    TEST_ASSERT_EQUAL_INT(1, compare_zb_get_api_usage_response(&expected_api_usage_response, &response));
}

void on_success_api_usage_invalid(ZBGetApiUsageResponse response) {
    TEST_FAIL_MESSAGE(zb_get_api_usage_response_to_string(&response));
}

void on_success_validate_valid(ZBValidateResponse response) {
    TEST_ASSERT_EQUAL_INT(1,compare_zb_validate_response(&expected_validate_response, &response));
}

void on_success_validate_invalid(ZBValidateResponse response) {
    TEST_FAIL_MESSAGE(zb_validate_response_to_string(&response));
}

void on_success_validate_batch_valid(ZBValidateBatchResponse response) {
    TEST_ASSERT_EQUAL_INT(1, compare_zb_validate_batch_response(&expected_validate_batch_response, &response));
}

void on_success_validate_batch_invalid(ZBValidateBatchResponse response) {
    TEST_FAIL_MESSAGE(zb_validate_batch_response_to_string(&response));
}

void on_success_send_file_valid(ZBSendFileResponse response) {
    TEST_ASSERT_EQUAL_INT(1, compare_zb_send_file_response(&expected_send_file_response, &response));
}

void on_success_send_file_invalid(ZBSendFileResponse response) {
    TEST_FAIL_MESSAGE(zb_send_file_response_to_string(&response));
}

void on_success_file_status_valid(ZBFileStatusResponse response) {
    TEST_ASSERT_EQUAL_INT(1, compare_zb_file_status_response(&expected_file_status_response, &response));
}

void on_success_file_status_invalid(ZBFileStatusResponse response) {
    TEST_FAIL_MESSAGE(zb_file_status_response_to_string(&response));
}

void on_success_get_file_valid(ZBGetFileResponse response) {
    TEST_ASSERT_EQUAL_INT(1, compare_zb_get_file_response(&expected_get_file_response, &response));
}

void on_success_get_file_invalid(ZBGetFileResponse response) {
    TEST_FAIL_MESSAGE(zb_get_file_response_to_string(&response));
}

void on_success_delete_file_valid(ZBDeleteFileResponse response) {
    TEST_ASSERT_EQUAL_INT(1, compare_zb_delete_file_response(&expected_delete_file_response, &response));
}

void on_success_delete_file_invalid(ZBDeleteFileResponse response) {
    TEST_FAIL_MESSAGE(zb_delete_file_response_to_string(&response));
}

void on_success_activity_data_valid(ZBActivityDataResponse response) {
    TEST_ASSERT_EQUAL_INT(1, compare_zb_activity_data_response(&expected_activity_data_response, &response));
}

void on_success_activity_data_invalid(ZBActivityDataResponse response) {
    TEST_FAIL_MESSAGE(zb_activity_data_response_to_string(&response));
}

void on_success_find_email_valid(ZBFindEmailResponse response) {
    TEST_ASSERT_TRUE(
        zb_find_email_response_compare(&expected_find_email_response, &response)
    );
}

void on_success_search_domain_valid(ZBDomainSearchResponse response) {
    TEST_ASSERT_TRUE(
        zb_domain_search_response_compare(&expected_domain_search_response, &response)
    );
}

// Handle mocks

void mock_set_write_callback(CURL* curl, memory* response_data) {
    size_t real_size = strlen(response_json);
    response_data->response = (char*)malloc(real_size + 1);
    strcpy(response_data->response, response_json);
    response_data->size = real_size;
}

FAKE_VALUE_FUNC(CURLcode, curl_easy_perform, CURL*);
FAKE_VOID_FUNC(set_write_callback, CURL*, memory*);
FAKE_VALUE_FUNC(long, get_http_code, CURL*);
FAKE_VALUE_FUNC(char*, get_content_type_value, CURL*);


// Tests

void setUp(void) {
    zb = zero_bounce_get_instance();
    zero_bounce_initialize(zb, "api_key");

    RESET_FAKE(curl_easy_perform);
    RESET_FAKE(set_write_callback);
    RESET_FAKE(get_http_code);
    RESET_FAKE(get_content_type_value);

    curl_easy_perform_fake.return_val = CURLE_OK;
    set_write_callback_fake.custom_fake = mock_set_write_callback;
}

void tearDown(void) {
}

void test_get_credits_invalid(void)
{
    response_json = "{\"Credits\":\"-1\"}";

    get_http_code_fake.return_val = 400;

    expected_error_response = parse_error(response_json);
    get_credits(zb, on_success_credits_invalid, on_error_invalid);
}

void test_get_credits_valid(void)
{
    response_json = "{\"Credits\":\"100\"}";

    get_http_code_fake.return_val = 200;

    expected_credits_response = zb_credits_response_from_json(json_tokener_parse(response_json));
    get_credits(zb, on_success_credits_valid, on_error_valid);
}

void test_get_api_usage_invalid(void)
{
    response_json = "{\"error\":\"Invalid API key\"}";

    get_http_code_fake.return_val = 400;

    expected_error_response = parse_error(response_json);

    struct tm start_date;
    start_date.tm_year = 118;
    start_date.tm_mon = 0;
    start_date.tm_mday = 1;

    struct tm end_date;
    end_date.tm_year = 123;
    end_date.tm_mon = 11;
    end_date.tm_mday = 12;

    get_api_usage(zb, start_date, end_date, on_success_api_usage_invalid, on_error_invalid);
}

void test_get_api_usage_valid(void)
{
    response_json =  "{\n"
        "    \"total\": 3,\n"
        "    \"status_valid\": 1,\n"
        "    \"status_invalid\": 2,\n"
        "    \"status_catch_all\": 0,\n"
        "    \"status_do_not_mail\": 0,\n"
        "    \"status_spamtrap\": 0,\n"
        "    \"status_unknown\": 0,\n"
        "    \"sub_status_toxic\": 0,\n"
        "    \"sub_status_disposable\": 0,\n"
        "    \"sub_status_role_based\": 0,\n"
        "    \"sub_status_possible_trap\": 0,\n"
        "    \"sub_status_global_suppression\": 0,\n"
        "    \"sub_status_timeout_exceeded\": 0,\n"
        "    \"sub_status_mail_server_temporary_error\": 0,\n"
        "    \"sub_status_mail_server_did_not_respond\": 0,\n"
        "    \"sub_status_greylisted\": 0,\n"
        "    \"sub_status_antispam_system\": 0,\n"
        "    \"sub_status_does_not_accept_mail\": 0,\n"
        "    \"sub_status_exception_occurred\": 0,\n"
        "    \"sub_status_failed_syntax_check\": 0,\n"
        "    \"sub_status_mailbox_not_found\": 2,\n"
        "    \"sub_status_unroutable_ip_address\": 0,\n"
        "    \"sub_status_possible_typo\": 0,\n"
        "    \"sub_status_no_dns_entries\": 0,\n"
        "    \"sub_status_role_based_catch_all\": 0,\n"
        "    \"sub_status_mailbox_quota_exceeded\": 0,\n"
        "    \"sub_status_forcible_disconnect\": 0,\n"
        "    \"sub_status_failed_smtp_connection\": 0,\n"
        "    \"sub_status_mx_forward\": 0,\n"
        "    \"sub_status_alternate\": 0,\n"
        "    \"sub_status_blocked\": 0,\n"
        "    \"sub_status_allowed\": 0,\n"
        "    \"sub_status_accept_all\": 0,\n"
        "    \"start_date\": \"1/1/2018\",\n"
        "    \"end_date\": \"12/12/2019\"\n"
        "}";

    get_http_code_fake.return_val = 200;

    expected_api_usage_response = zb_get_api_usage_response_from_json(json_tokener_parse(response_json));

    struct tm start_date;
    start_date.tm_year = 118;
    start_date.tm_mon = 0;
    start_date.tm_mday = 1;

    struct tm end_date;
    end_date.tm_year = 123;
    end_date.tm_mon = 11;
    end_date.tm_mday = 12;

    get_api_usage(zb, start_date, end_date, on_success_api_usage_valid, on_error_valid);
}

void test_validate_email_invalid(void)
{
    response_json = "{\"error\":\"Invalid API key or your account ran out of credits\"}";

    get_http_code_fake.return_val = 400;

    expected_error_response = parse_error(response_json);
    validate_email(zb, "valid@example.com", "127.0.0.1", on_success_validate_invalid, on_error_invalid);
}

void test_validate_email_valid(void)
{
    response_json = "{\n"
        "    \"address\": \"valid@example.com\",\n"
        "    \"status\": \"valid\",\n"
        "    \"sub_status\": \"\",\n"
        "    \"free_email\": false,\n"
        "    \"did_you_mean\": null,\n"
        "    \"account\": null,\n"
        "    \"domain\": null,\n"
        "    \"domain_age_days\": \"9692\",\n"
        "    \"smtp_provider\": \"example\",\n"
        "    \"mx_found\": \"true\",\n"
        "    \"mx_record\": \"mx.example.com\",\n"
        "    \"firstname\": \"zero\",\n"
        "    \"lastname\": \"bounce\",\n"
        "    \"gender\": \"male\",\n"
        "    \"country\": null,\n"
        "    \"region\": null,\n"
        "    \"city\": null,\n"
        "    \"zipcode\": null,\n"
        "    \"processed_at\": \"2023-04-05 13:29:47.553\"\n"
        "}";

    get_http_code_fake.return_val = 200;

    expected_validate_response = zb_validate_response_from_json(json_tokener_parse(response_json));
    validate_email(zb, "valid@example.com", "127.0.0.1", on_success_validate_valid, on_error_valid);
}

void test_validate_email_batch_invalid(void)
{
    response_json = "{\"Message\":\"Missing parameter: email_address.\"}";

    get_http_code_fake.return_val = 400;

    expected_error_response = parse_error(response_json);

    EmailToValidateVector emails = email_to_validate_vector_init();

    validate_email_batch(zb, emails, on_success_validate_batch_invalid, on_error_invalid);
}

void test_validate_email_batch_valid(void)
{
    response_json = "{\n"
        "    \"email_batch\": [\n"
        "        {\n"
        "            \"address\": \"valid@example.com\",\n"
        "            \"status\": \"valid\",\n"
        "            \"sub_status\": \"\",\n"
        "            \"free_email\": false,\n"
        "            \"did_you_mean\": null,\n"
        "            \"account\": null,\n"
        "            \"domain\": null,\n"
        "            \"domain_age_days\": \"9692\",\n"
        "            \"smtp_provider\": \"example\",\n"
        "            \"mx_found\": \"true\",\n"
        "            \"mx_record\": \"mx.example.com\",\n"
        "            \"firstname\": \"zero\",\n"
        "            \"lastname\": \"bounce\",\n"
        "            \"gender\": \"male\",\n"
        "            \"country\": null,\n"
        "            \"region\": null,\n"
        "            \"city\": null,\n"
        "            \"zipcode\": null,\n"
        "            \"processed_at\": \"2023-04-05 14:35:24.051\"\n"
        "        },\n"
        "        {\n"
        "            \"address\": \"invalid@example.com\",\n"
        "            \"status\": \"invalid\",\n"
        "            \"sub_status\": \"mailbox_not_found\",\n"
        "            \"free_email\": false,\n"
        "            \"did_you_mean\": null,\n"
        "            \"account\": null,\n"
        "            \"domain\": null,\n"
        "            \"domain_age_days\": \"9692\",\n"
        "            \"smtp_provider\": \"example\",\n"
        "            \"mx_found\": \"true\",\n"
        "            \"mx_record\": \"mx.example.com\",\n"
        "            \"firstname\": \"zero\",\n"
        "            \"lastname\": \"bounce\",\n"
        "            \"gender\": \"male\",\n"
        "            \"country\": null,\n"
        "            \"region\": null,\n"
        "            \"city\": null,\n"
        "            \"zipcode\": null,\n"
        "            \"processed_at\": \"2023-04-05 14:35:24.051\"\n"
        "        }\n"
        "    ],\n"
        "    \"errors\": []\n"
        "}";

    get_http_code_fake.return_val = 200;

    expected_validate_batch_response = zb_validate_batch_response_from_json(json_tokener_parse(response_json));

    EmailToValidateVector emails = email_to_validate_vector_init();

    ZBEmailToValidate email_1 = {"valid@example.com", "1.1.1.1"};
    email_to_validate_vector_append(&emails, email_1);

    ZBEmailToValidate email_2 = {"invalid@example.com", ""};
    email_to_validate_vector_append(&emails, email_2);

    validate_email_batch(zb, emails, on_success_validate_batch_valid, on_error_valid);

    email_to_validate_vector_free(&emails);
}

void test_send_file_invalid(void)
{
    response_json = "{\"success\":\"False\",\"message\":[\"api_key is invalid\"]}";

    get_http_code_fake.return_val = 401;

    expected_error_response = parse_error(response_json);

    SendFileOptions options = new_send_file_options();

    send_file(zb, "../email_file.csv", 1, options, on_success_send_file_invalid, on_error_invalid);
}

void test_send_file_valid(void)
{
    response_json = "{"
        "    \"success\": true,"
        "    \"message\": \"File Accepted\","
        "    \"file_name\": \"email_file.csv\","
        "    \"file_id\": \"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff\""
        "}";

    get_http_code_fake.return_val = 201;

    expected_send_file_response = zb_send_file_response_from_json(json_tokener_parse(response_json));

    SendFileOptions options = new_send_file_options();

    send_file(zb, "../email_file.csv", 1, options, on_success_send_file_valid, on_error_valid);
}

void test_file_status_invalid(void)
{
    response_json = "{\"success\":\"False\",\"message\":[\"api_key is invalid\"]}";

    get_http_code_fake.return_val = 401;

    expected_error_response = parse_error(response_json);

    file_status(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", on_success_file_status_invalid, on_error_invalid);
}

void test_file_status_valid(void)
{
    response_json = "{\n"
        "    \"success\": true,\n"
        "    \"file_id\": \"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff\",\n"
        "    \"file_name\": \"email_file.csv\",\n"
        "    \"upload_date\": \"10/20/2018 4:35:58 PM\",\n"
        "    \"file_status\": \"Complete\",\n"
        "    \"complete_percentage\": \"100%\",\n"
        "    \"error_reason\": null,\n"
        "    \"return_url\": \"Your return URL if provided when calling sendfile API\"\n"
        "}";

    get_http_code_fake.return_val = 200;

    expected_file_status_response = zb_file_status_response_from_json(json_tokener_parse(response_json));

    file_status(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", on_success_file_status_valid, on_error_valid);
}

void test_get_file_invalid(void)
{
    response_json = "{\"success\":false,\"message\":\"File deleted.\"}";

    get_http_code_fake.return_val = 400;
    get_content_type_value_fake.return_val = "application/json";

    expected_error_response = parse_error(response_json);

    get_file(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", "test/downloads/file.csv", on_success_get_file_invalid, on_error_invalid);
}

void test_get_file_valid(void)
{
    response_json =
        "\"Email Address\",\"First Name\",\"Last Name\",\"Gender\",\"ZB Status\",\"ZB Sub Status\",\"ZB Account\",\"ZB Domain\",\"ZB First Name\",\"ZB Last Name\",\"ZB Gender\",\"ZB Free Email\",\"ZB MX Found\",\"ZB MX Record\",\"ZB SMTP Provider\",\"ZB Did You Mean\"\n"
        "\"valid@example.com\",\"zero\",\"bounce\",\"\",\"valid\",\"\",\"\",\"\",\"zero\",\"bounce\",\"male\",\"False\",\"true\",\"mx.example.com\",\"example\",\"\"\n";

    get_http_code_fake.return_val = 200;
    get_content_type_value_fake.return_val = "application/octet-stream";

    expected_get_file_response = new_zb_get_file_response();
    expected_get_file_response.success = true;
    expected_get_file_response.local_file_path = "test/downloads/file.csv";

    get_file(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", "test/downloads/file.csv", on_success_get_file_valid, on_error_valid);
}

void test_delete_file_invalid(void)
{
    response_json = "{\"success\":false,\"message\":\"File cannot be found.\"}";

    get_http_code_fake.return_val = 400;

    expected_error_response = parse_error(response_json);

    delete_file(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", on_success_delete_file_invalid, on_error_invalid);
}

void test_delete_file_valid(void)
{
    response_json = "{\n"
        "    \"success\": true,\n"
        "    \"message\": \"File Deleted\",\n"
        "    \"file_name\": \"test2\",\n"
        "    \"file_id\": \"b222a0fd-90d5-416c-8f1a-9cc3851fc823\"\n"
        "}";

    get_http_code_fake.return_val = 200;

    expected_delete_file_response = zb_delete_file_response_from_json(json_tokener_parse(response_json));

    delete_file(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", on_success_delete_file_valid, on_error_valid);
}

void test_scoring_send_file_invalid(void)
{
    response_json = "{\"success\":\"False\",\"message\":[\"api_key is invalid\"]}";

    get_http_code_fake.return_val = 401;

    expected_error_response = parse_error(response_json);

    SendFileOptions options = new_send_file_options();

    scoring_send_file(zb, "../email_file.csv", 1, options, on_success_send_file_invalid, on_error_invalid);
}

void test_scoring_send_file_valid(void)
{
    response_json = "{"
        "    \"success\": true,"
        "    \"message\": \"File Accepted\","
        "    \"file_name\": \"email_file.csv\","
        "    \"file_id\": \"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff\""
        "}";

    get_http_code_fake.return_val = 201;

    expected_send_file_response = zb_send_file_response_from_json(json_tokener_parse(response_json));

    SendFileOptions options = new_send_file_options();

    scoring_send_file(zb, "../email_file.csv", 1, options, on_success_send_file_valid, on_error_valid);
}

void test_scoring_file_status_invalid(void)
{
    response_json = "{\"success\":\"False\",\"message\":[\"api_key is invalid\"]}";

    get_http_code_fake.return_val = 401;

    expected_error_response = parse_error(response_json);

    scoring_file_status(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", on_success_file_status_invalid, on_error_invalid);
}

void test_scoring_file_status_valid(void)
{
    response_json = "{\n"
        "    \"success\": true,\n"
        "    \"file_id\": \"aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff\",\n"
        "    \"file_name\": \"email_file.csv\",\n"
        "    \"upload_date\": \"10/20/2018 4:35:58 PM\",\n"
        "    \"file_status\": \"Complete\",\n"
        "    \"complete_percentage\": \"100%\",\n"
        "    \"error_reason\": null,\n"
        "    \"return_url\": \"Your return URL if provided when calling sendfile API\"\n"
        "}";

    get_http_code_fake.return_val = 200;

    expected_file_status_response = zb_file_status_response_from_json(json_tokener_parse(response_json));

    scoring_file_status(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", on_success_file_status_valid, on_error_valid);
}

void test_scoring_get_file_invalid(void)
{
    response_json = "{\"success\":false,\"message\":\"File deleted.\"}";

    get_http_code_fake.return_val = 400;
    get_content_type_value_fake.return_val = "application/json";

    expected_error_response = parse_error(response_json);

    scoring_get_file(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", "test/downloads/scoring_file.csv", on_success_get_file_invalid, on_error_invalid);
}

void test_scoring_get_file_valid(void)
{
    response_json =
        "\"email\",\"firstname\",\"lastname\",\"ZeroBounceQualityScore\"\n"
        "\"valid@example.com\",\"zero\",\"bounce\",\"10\"";

    get_http_code_fake.return_val = 200;
    get_content_type_value_fake.return_val = "application/octet-stream";

    expected_get_file_response = new_zb_get_file_response();
    expected_get_file_response.success = true;
    expected_get_file_response.local_file_path = "test/downloads/scoring_file.csv";

    scoring_get_file(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", "test/downloads/scoring_file.csv", on_success_get_file_valid, on_error_valid);
}

void test_scoring_delete_file_invalid(void)
{
    response_json = "{\"success\":false,\"message\":\"File cannot be found.\"}";

    get_http_code_fake.return_val = 400;

    expected_error_response = parse_error(response_json);

    scoring_delete_file(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", on_success_delete_file_invalid, on_error_invalid);
}

void test_scoring_delete_file_valid(void)
{
    response_json = "{\n"
        "    \"success\": true,\n"
        "    \"message\": \"File Deleted\",\n"
        "    \"file_name\": \"test2\",\n"
        "    \"file_id\": \"b222a0fd-90d5-416c-8f1a-9cc3851fc823\"\n"
        "}";

    get_http_code_fake.return_val = 200;

    expected_delete_file_response = zb_delete_file_response_from_json(json_tokener_parse(response_json));

    scoring_delete_file(zb, "aaaaaaaa-zzzz-xxxx-yyyy-5003727fffff", on_success_delete_file_valid, on_error_valid);
}

void test_find_email_status_invalid_payload(void) {
    response_json = "{\n"
        "    \"email\": \"\",\n"
        "    \"email_confidence\": \"high\",\n"
        "    \"domain\": \"example.com\",\n"
        "    \"company_name\": \"\",\n"
        "    \"did_you_mean\": \"\",\n"
        "    \"failure_reason\": \"\"\n"
        "}";

    // de-serialization testing
    ZBFindEmailResponse response_obj = zb_find_email_response_from_json(
        json_tokener_parse(response_json)
    );
    TEST_ASSERT_EQUAL_STRING("", response_obj.email);
    TEST_ASSERT_EQUAL_STRING("example.com", response_obj.domain);

    // request checking
    get_http_code_fake.return_val = 200;
    expected_find_email_response = response_obj;
    find_email_by_domain_first_middle_last_name(
        zb, "example.com", "John", "", "Doe",
        on_success_find_email_valid,
        on_error_valid
    );
    zb_find_email_response_free(&response_obj);
}

void test_find_email_status_valid_payload(void) {
    response_json = "{\n"
        "    \"email\": \"john.doe@example.com\",\n"
        "    \"email_confidence\": \"high\",\n"
        "    \"domain\": \"example.com\",\n"
        "    \"company_name\": \"\",\n"
        "    \"did_you_mean\": \"\",\n"
        "    \"failure_reason\": \"\"\n"
        "}";

    // de-serialization testing
    ZBFindEmailResponse response_obj = zb_find_email_response_from_json(
        json_tokener_parse(response_json)
    );
    TEST_ASSERT_EQUAL_STRING("john.doe@example.com", response_obj.email);
    TEST_ASSERT_EQUAL_STRING("example.com", response_obj.domain);

    // request checking
    get_http_code_fake.return_val = 200;
    expected_find_email_response = response_obj;
    find_email_by_domain_first_middle_last_name(
        zb, "example.com", "John", "", "Doe",
        on_success_find_email_valid,
        on_error_valid
    );
    zb_find_email_response_free(&response_obj);
}


void test_search_domain_status_invalid_payload(void) {
    response_json = "{\n"
        "    \"domain\": \"example.com\",\n"
        "    \"company_name\": \"\",\n"
        "    \"format\": \"unknown\",\n"
        "    \"confidence\": \"undetermined\",\n"
        "    \"did_you_mean\": \"\",\n"
        "    \"failure_reason\": \"\",\n"
        "    \"other_domain_formats\": []\n"
        "}";

    // de-serialization testing
    ZBDomainSearchResponse response_obj = zb_domain_search_response_from_json(
        json_tokener_parse(response_json)
    );
    TEST_ASSERT_EQUAL_STRING("example.com", response_obj.domain);
    TEST_ASSERT_EQUAL_INT(0, response_obj.other_domain_formats.size);

    // request checking
    get_http_code_fake.return_val = 200;
    expected_domain_search_response = response_obj;
    search_domain_by_domain(
        zb, "example.com",
        on_success_search_domain_valid,
        on_error_valid
    );
    zb_domain_search_response_free(&response_obj);
}

void test_search_domain_status_valid_payload(void) {
    response_json = "{\n"
        "    \"domain\": \"example.com\",\n"
        "    \"company_name\": \"\",\n"
        "    \"format\": \"first.last\",\n"
        "    \"confidence\": \"high\",\n"
        "    \"did_you_mean\": \"\",\n"
        "    \"failure_reason\": \"\",\n"
        "    \"other_domain_formats\": [\n"
        "        {\n"
        "            \"format\": \"first_last\",\n"
        "            \"confidence\": \"high\"\n"
        "        },\n"
        "        {\n"
        "            \"format\": \"first\",\n"
        "            \"confidence\": \"medium\"\n"
        "        }\n"
        "    ]\n"
        "}";

    // de-serialization testing
    ZBDomainSearchResponse response_obj = zb_domain_search_response_from_json(
        json_tokener_parse(response_json)
    );
    TEST_ASSERT_EQUAL_STRING("example.com", response_obj.domain);
    TEST_ASSERT_EQUAL_STRING("", response_obj.company_name);
    TEST_ASSERT_EQUAL_INT(2, response_obj.other_domain_formats.size);
    TEST_ASSERT_EQUAL_STRING("first_last", response_obj.other_domain_formats.data[0].format);
    TEST_ASSERT_EQUAL_STRING("high", response_obj.other_domain_formats.data[0].confidence);
    TEST_ASSERT_EQUAL_STRING("first", response_obj.other_domain_formats.data[1].format);
    TEST_ASSERT_EQUAL_STRING("medium", response_obj.other_domain_formats.data[1].confidence);

    // request checking
    get_http_code_fake.return_val = 200;
    expected_domain_search_response = response_obj;
    search_domain_by_domain(
        zb, "example.com", 
        on_success_search_domain_valid,
        on_error_valid
    );
    zb_domain_search_response_free(&response_obj);
}


void test_search_domain_status_serialize_functions() {
    response_json = "{\n"
        "    \"domain\": \"example.com\",\n"
        "    \"company_name\": \"\",\n"
        "    \"format\": \"first.last\",\n"
        "    \"confidence\": \"high\",\n"
        "    \"did_you_mean\": \"\",\n"
        "    \"failure_reason\": \"\",\n"
        "    \"other_domain_formats\": [\n"
        "        {\n"
        "            \"format\": \"first_last\",\n"
        "            \"confidence\": \"high\"\n"
        "        },\n"
        "        {\n"
        "            \"format\": \"first\",\n"
        "            \"confidence\": \"medium\"\n"
        "        }\n"
        "    ]\n"
        "}";

    ZBDomainSearchResponse response_obj = zb_domain_search_response_from_json(
        json_tokener_parse(response_json)
    );

    char* string1 = zb_domain_format_to_string(&(response_obj.other_domain_formats.data[0]));
    TEST_ASSERT_NOT_NULL_MESSAGE(string1, "domain format serializing failed");

    char* string2 = zb_domain_format_vector_to_string(&(response_obj.other_domain_formats));
    TEST_ASSERT_NOT_NULL_MESSAGE(string2, "domain format vector serializing failed");
    TEST_ASSERT_GREATER_THAN_MESSAGE(
        2,
        strlen(string2),
        "domain format vector serializing is '[]' but is should not"
    );

    char* string3 = zb_domain_search_response_to_string(&response_obj);
    TEST_ASSERT_NOT_NULL_MESSAGE(string3, "find mail response serializing failed");

    free(string1);
    free(string2);
    free(string3);
    zb_domain_search_response_free(&response_obj);
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_get_credits_invalid);
    RUN_TEST(test_get_credits_valid);
    RUN_TEST(test_get_api_usage_invalid);
    RUN_TEST(test_get_api_usage_valid);
    RUN_TEST(test_validate_email_invalid);
    RUN_TEST(test_validate_email_valid);
    RUN_TEST(test_validate_email_batch_invalid);
    RUN_TEST(test_validate_email_batch_valid);
    RUN_TEST(test_send_file_invalid);
    RUN_TEST(test_send_file_valid);
    RUN_TEST(test_file_status_invalid);
    RUN_TEST(test_file_status_valid);
    RUN_TEST(test_get_file_invalid);
    RUN_TEST(test_get_file_valid);
    RUN_TEST(test_delete_file_invalid);
    RUN_TEST(test_delete_file_valid);
    RUN_TEST(test_scoring_send_file_invalid);
    RUN_TEST(test_scoring_send_file_valid);
    RUN_TEST(test_scoring_file_status_invalid);
    RUN_TEST(test_scoring_file_status_valid);
    RUN_TEST(test_scoring_get_file_invalid);
    RUN_TEST(test_scoring_get_file_valid);
    RUN_TEST(test_scoring_delete_file_invalid);
    RUN_TEST(test_scoring_delete_file_valid);
    RUN_TEST(test_find_email_status_invalid_payload);
    RUN_TEST(test_find_email_status_valid_payload);
    RUN_TEST(test_search_domain_status_invalid_payload);
    RUN_TEST(test_search_domain_status_valid_payload);
    RUN_TEST(test_search_domain_status_serialize_functions);

    return UNITY_END();
}