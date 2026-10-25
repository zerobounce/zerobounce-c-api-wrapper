#### INSTALLATION
Before installing the wrapper, you have to make sure that `CMake` is installed on your system. It can be downloaded from [here](https://cmake.org/download/).

You can install by using CMake with `FetchContent`. Add the following to your `CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(
    ZeroBounce
    GIT_REPOSITORY https://github.com/zerobounce/zerobounce-c-api-wrapper.git
    GIT_TAG <DESIRED_TAG>
)
FetchContent_MakeAvailable(ZeroBounce)
```

This will produce the target `ZeroBounce` which you can link against the typical way:

```cmake
target_link_libraries(your_target_name PRIVATE ZeroBounce)
```

If using Visual Studio Code, you can build the library using `CMake` and `Cmake Tools` extensions. After the extensions have been installed, you simply open the commands with `CTRL + SHIFT + P` and use `CMake: Configure` followed by `CMake: Build`.

Alternatively, you can also use the CLI commands in the root directory of your project.

###### LINUX

```bash
mkdir build
cmake -S . -B build -G "Unix Makefiles"
cmake --build build
```

###### WINDOWS

You have to make sure that `MinGW` is installed on your system. It can be installed following the steps [here](https://code.visualstudio.com/docs/cpp/config-mingw).

```bash
mkdir build
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

Be aware that the library may require some .dll files (found in the `build` directory) in order to work properly in your project.

#### USAGE
Include the library in your file:
```c
###include <ZeroBounce/ZeroBounce.h>
```

Initialize the wrapper with your api key and preferred api:
```c
ZeroBounce* zb = zero_bounce_get_instance();
zero_bounce_initialize(zb, "<YOUR_API_KEY>", ZB_Api_URL_Default);
```

#### Examples
Then you can use any of the wrapper functions, for example:
* ####### Check how many credits you have left on your account
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_credits(ZBCreditsResponse response) {
    printf("%s\n\n", zb_credits_response_to_string(&response));
}

get_credits(zb, on_success_credits, on_error);
```

* ####### Check your API usage for a given period of time
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_api_usage(ZBGetApiUsageResponse response) {
    printf("%s\n\n", zb_get_api_usage_response_to_string(&response));
}

struct tm start_date;
start_date.tm_year = 118;
start_date.tm_mon = 0;
start_date.tm_mday = 1;

struct tm end_date;
end_date.tm_year = 123;
end_date.tm_mon = 11;
end_date.tm_mday = 12;

get_api_usage(zb, start_date, end_date, on_success_api_usage, on_error);
```

* ####### Validate an email address
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_validate(ZBValidateResponse response) {
    printf("%s\n\n", zb_validate_response_to_string(&response));
}

char* email = "valid@example.com";   // The email address you want to validate
char* ip_address = "127.0.0.1";     // The IP Address the email signed up from (Optional)

validate_email(zb, email, ip_address, on_success_validate, on_error);
```

* ####### Validate email batch
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_validate_batch(ZBValidateBatchResponse response) {
    printf("%s\n\n", zb_validate_batch_response_to_string(&response));
}

EmailToValidateVector vector = email_to_validate_vector_init();

ZBEmailToValidate email_1 = {"valid@example.com", "1.1.1.1"};
email_to_validate_vector_append(&vector, email_1);

ZBEmailToValidate email_2 = {"invalid@example.com", ""};
email_to_validate_vector_append(&vector, email_2);

validate_email_batch(zb, vector, on_success_validate_batch, on_error);

email_to_validate_vector_free(&vector);
```

* ####### Use the Activity API endpoint to gather insights into your subscribers'overall email engagement
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_activity_data(ZBActivityDataResponse response) {
    printf("%s\n\n", zb_activity_data_response_to_string(&response));
}

char* email = "valid@example.com";    // Subscriber email address

get_activity_data(zb, email, on_success_activity_data, on_error);
```

* ####### The sendfile API allows user to send a file for bulk email validation
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_send_file(ZBSendFileResponse response) {
    printf("%s\n\n", zb_send_file_response_to_string(&response));
}

char* file_path = "<FILE_PATH>"; 			// The path of the csv or txt file
int email_address_column = 3; 			// The index of "email" column in the file. Index starts at 1
SendFileOptions options = new_send_file_options();	// Additional options

options.returnUrl = "https://domain.com/called/after/processing/request";
options.firstNameColumn = 4;		// The index of "first name" column in the file
options.lastNameColumn = 5;		// The index of "last name" column in the file
options.genderColumn = 6;		// The index of "gender" column in the file
options.ipAddressColumn = 7;		// The index of "IP address" column in the file
options.hasHeaderRow = true;		// If this is `true` the first row is considered as table headers
options.removeDuplicate = true;	// If this is `true` the duplicate emails will be removed

send_file(zb, file_path, email_address_column, options, on_success_send_file, on_error);
```

* ####### The getfile API allows users to get the validation results file for the file been submitted using sendfile API
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_get_file(ZBGetFileResponse response) {
    printf("%s\n\n", zb_get_file_response_to_string(&response));
}

char* file_id = "<FILE_ID>";				// The returned file ID when calling sendfile API
char* local_download_path = "<FILE_DOWNLOAD_PATH>"; 	// The location where the downloaded file will be saved

get_file(zb, file_id, local_download_path, on_success_get_file, on_error);
```

Bulk validation uses the [v2 bulk API](https://www.zerobounce.net/docs/email-validation-api-quickstart/v2-send-file). Optional [v2 get file](https://www.zerobounce.net/docs/email-validation-api-quickstart/v2-get-file) query parameters are passed with `get_file_with_options` / `scoring_get_file_with_options` and a `GetFileOptions` value from `new_get_file_options()`. Set `download_type` to `ZB_DOWNLOAD_TYPE_PHASE_1`, `ZB_DOWNLOAD_TYPE_PHASE_2`, or `ZB_DOWNLOAD_TYPE_COMBINED`; set `activity_data` to `0` or `1` when you want `activity_data=false` or `true` (validation `get_file_with_options` only; scoring omits this parameter). On `SendFileOptions`, set `allow_phase_2_is_set` to `true` and `allow_phase_2` to the desired value to send `allow_phase_2` for validation `send_file` only (not `scoring_send_file`).

A result file is downloaded into memory first, which is fine for an ordinary
list. For one too large to buffer, `get_file_streaming` (and its AI scoring
counterpart `scoring_get_file_streaming`) writes the body straight to disk as
it arrives. It is opt-in because a streamed body cannot be inspected as a
whole, and getfile answers an unknown or deleted file id with a JSON error
rather than a file: the first 4 KB are held back and checked, so an error still
reaches the error callback and no partial file is left behind.

```c
get_file_streaming(zb, file_id, local_download_path, NULL, on_success_get_file, on_error);
```

If the HTTP status is an error, or the body is a JSON error (including some HTTP 200 responses with `"success": false`), the error callback runs with a parsed message—not the get-file success callback. To inspect a raw body string yourself, use `zb_get_file_json_indicates_error(body)`; use `zb_format_get_file_error_message(body)` for a human-readable line (caller must `free()` the returned pointer).

`ZBFileStatusResponse` includes `file_phase_2_status` when the API returns it.

* ####### Check the status of a file uploaded via "sendFile" method
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_file_status(ZBFileStatusResponse response) {
    printf("%s\n\n", zb_file_status_response_to_string(&response));
}

char* file_id = "<FILE_ID>";		// The returned file ID when calling sendfile API

file_status(zb, file_id, on_success_file_status, on_error);
```

* ####### Deletes the file that was submitted using sendfile API. File can be deleted only when its status is _`Complete`_
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_delete_file(ZBDeleteFileResponse response) {
    printf("%s\n\n", zb_delete_file_response_to_string(&response));
}

char* file_id = "<FILE_ID>";		// The returned file ID when calling sendfile API

delete_file(zb, file_id, on_success_delete_file, on_error);
```

* ####### The scoringSendfile API allows user to send a file for bulk email validation
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_send_file(ZBSendFileResponse response) {
    printf("%s\n\n", zb_send_file_response_to_string(&response));
}

char* file_path = "<FILE_PATH>";			// The path of the csv or txt file
int email_address_column = 3; 			// The index of "email" column in the file. Index starts at 1
SendFileOptions options = new_send_file_options();	// Additional options

options.returnUrl = "https://domain.com/called/after/processing/request";
options.hasHeaderRow = true;            // If this is `true` the first row is considered as table headers

scoring_send_file(zb, file_path, email_address_column, options, on_success_send_file, on_error);
```

* ####### The scoringGetFile API allows users to get the validation results file for the file been submitted using scoringSendfile API
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_get_file(ZBGetFileResponse response) {
    printf("%s\n\n", zb_get_file_response_to_string(&response));
}

char* file_id = "<FILE_ID>";                       	// The returned file ID when calling scoringSendfile API
char* local_download_path = "<FILE_DOWNLOAD_PATH>";  	// The location where the downloaded file will be saved

scoring_get_file(zb, file_id, local_download_path, on_success_get_file, on_error);
```

* ####### Check the status of a file uploaded via "scoringSendFile" method
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_file_status(ZBFileStatusResponse response) {
    printf("%s\n\n", zb_file_status_response_to_string(&response));
}

char* file_id = "<FILE_ID>";                    // The returned file ID when calling scoringSendfile API

scoring_file_status(zb, file_id, on_success_file_status, on_error);
```

* ####### Deletes the file that was submitted using scoring scoringSendfile API. File can be deleted only when its status is _`Complete`_
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_delete_file(ZBDeleteFileResponse response) {
    printf("%s\n\n", zb_delete_file_response_to_string(&response));
}

char* file_id = "<FILE_ID>";                       // The returned file ID when calling scoringSendfile API

scoring_delete_file(zb, file_id, on_success_delete_file, on_error);
```

* ##### Email finder allows you to identify the correct email format when you provide a name and email domain or company name
```c
void error_callback(ZBErrorResponse error_response) {
    char* error_string = concatenate_strings(&(error_response.errors), "");
    printf("ERROR: %s\n", error_string);
    free(error_string);
}

void on_success_callback(ZBFindEmailResponse response) {
    char* result = zb_find_email_response_to_string(&response);
    printf("%s\n", result);
    free(result);
}

// actual method
find_email_by_domain(
    zb, "example.com", "John", "", "Doe", on_success_callback, error_callback
);

find_email_by_company_name(
    zb, "company", "John", "", "Doe", on_success_callback, error_callback
);
```

* ##### Domain search allows you to identify the email domain when you provide a domain or company name
```c
void error_callback(ZBErrorResponse error_response) {
    char* error_string = concatenate_strings(&(error_response.errors), "");
    printf("ERROR: %s\n", error_string);
    free(error_string);
}

void on_success_callback(ZBDomainSearchResponse response) {
    char* result = zb_domain_search_response_to_string(&response);
    printf("%s\n", result);
    free(result);
}

// actual method
search_domain_by_domain(
    zb, "example.com", on_success_callback, error_callback
);

search_domain_by_company_name(
    zb, "company", on_success_callback, error_callback
);
```

#### Validate options

`validate_email_with_options` takes a `ZBValidateOptions`. A tri-state field is
`-1` to omit, `0` false and `1` true; booleans go on the wire as the strings
`"true"`/`"false"`.

```c
ZBValidateOptions options = new_validate_options();
options.timeout = 30;                 /* 3-60 seconds */
options.activity_data = 1;            /* fills active_in_days / active_first_seen */
options.domain_info = 1;              /* fills domain_website_exists / domain_registrant_company_name */
options.greylist_processing = 1;
options.callback_url = "https://example.com/greylist";
options.use_post = 1;                 /* form-encoded POST /v2/validate instead of GET */

validate_email_with_options(zb, "<EMAIL>", "", &options, on_validate, on_error);
```

`callback_url` is where the greylist retry posts its result, so it is rejected
without `greylist_processing = 1` — before any request is sent. While that retry
is still running the API answers with a `Message` instead of a result, which
lands on `response.message`.

## S3 and SFTP/FTP bulk

Queue a job on a file that already sits in your Amazon S3 bucket, or on your
SFTP/FTP server. See the [S3](https://www.zerobounce.net/docs/api-integrations/amazon-s3-bulk-api)
and [SFTP](https://www.zerobounce.net/docs/api-integrations/sftp_bulk_api) docs.
These live on the bulkapi host *without* the `/v2` path.

Every one of these jobs needs both `file_name` and `return_url`, `validate_s3_file`
and `score_s3_file` included. The docs call several of them optional, but the live
API answers `Empty parameter: file_name` without the one and
`Provided return URL is not permitted: None` without the other, so the SDK checks
both before sending.

```c
ZBBulkFileJobOptions options = new_bulk_file_job_options();

/* Amazon S3. file_name is the full path, as returned by list_s3_files. */
options.file_name = "lists/leads.csv";
options.return_url = "https://example.com/zerobounce-done";
email_finder_s3_file(zb, &options, on_send, on_error);

options.remove_duplicates = 1;
domain_search_s3_file(zb, &options, on_send, on_error);

/* SFTP/FTP. The server, path and protocol come from your ZeroBounce settings. */
options = new_bulk_file_job_options();
options.file_name = "my_file.txt";
options.return_url = "https://example.com/zerobounce-done";
validate_sftp_file(zb, &options, on_send, on_error);
score_sftp_file(zb, &options, on_send, on_error);
email_finder_sftp_file(zb, &options, on_send, on_error);
domain_search_sftp_file(zb, &options, on_send, on_error);

/* Note the field: this one takes export_path, not export_bucket.
   NULL uses the export path from your settings. */
export_sftp_file(zb, "<FILE_ID>", "/exports", on_send, on_error);

/* validate_s3_file and score_s3_file take the same two required fields plus
   their own columns. */
ValidateS3FileOptions validate = new_validate_s3_file_options();
validate.file_name = "lists/leads.csv";
validate.return_url = "https://example.com/zerobounce-done";
validate.email_address_column = 1;
validate_s3_file(zb, &validate, on_send, on_error);

ScoreS3FileOptions score = new_score_s3_file_options();
score.file_name = "lists/leads.csv";
score.return_url = "https://example.com/zerobounce-done";
score_s3_file(zb, &score, on_send, on_error);

/* export_bucket is optional: NULL uses the one from your settings. */
export_s3_file(zb, "<FILE_ID>", NULL, on_send, on_error);
```

## Inbox Placement Tester

Runs on its own host and sends camelCase JSON bodies. It authenticates with an
`Authorization: Bearer` header rather than the `api_key` parameter the rest of
the API takes, and it spends the account's inbox-test credits.

```c
get_inbox_placement_providers(zb, on_raw, on_error);
get_inbox_placement_test_types(zb, on_raw, on_error);

/* name is at most 64 characters, test_type is 1, 2 or 3, and a test_type of 1
   needs a sender_email. */
static const int provider_ids[] = {3, 7};
static const ZBInboxPlacementProvider providers[] = {{3, 10}};

ZBCreateInboxPlacementTestOptions create = new_create_inbox_placement_test_options();
create.test_type = 1;
create.sender_email = "news@example.com";
create.webhook_url = "https://example.com/ipt-webhook";
/* Optional: pick the providers, the seeds, or a per-provider seed cap. */
create.provider_ids = provider_ids;
create.provider_ids_count = 2;
create.providers = providers;
create.providers_count = 1;
create_inbox_placement_test(zb, "Launch", &create, on_raw, on_error);

/* Send your campaign to the seeds you got back, then read the results. */
get_inbox_placement_test(zb, "<TEST_ID>", on_raw, on_error);

ZBListInboxPlacementTestsOptions list = new_list_inbox_placement_tests_options();
list.status = "finished";
list.page = 1;
list.page_size = 25;            /* 1 to 100 */
list.created_after = "2026-01-01";
list.created_before = "2026-02-01";
list_inbox_placement_tests(zb, &list, on_raw, on_error);

set_inbox_placement_webhook(zb, "https://example.com/ipt-webhook", on_raw, on_error);
get_inbox_placement_webhook(zb, on_raw, on_error);
delete_inbox_placement_test(zb, "<TEST_ID>", on_raw, on_error);
```

## List evaluator

Upload a list for evaluation, read the result, and hand a finished evaluation
back for bulk validation. The evaluator has its own base URL, and the file id
is one path segment, so a slash in it stays encoded.

```c
send_list_evaluator_file(zb, "emails.csv", 1, on_send, on_error);
get_list_evaluator_result(zb, "<FILE_ID>", on_raw, on_error);

/* The id of a finished evaluation, not a path: the live endpoint takes it as
   a form field. */
send_evaluated_file(zb, "<FILE_ID>", on_send, on_error);
```

## Warmup API

The [Warmup API](https://www.zerobounce.net/docs/warmup-api) is a separate
product: its own host, its own team-scoped key, and an `API-Key` header instead
of an `api_key` parameter. Use `ZeroBounceWarmup` with a Warmup API key;
`ZeroBounce` stays for the validation key.

Every call hands the raw JSON body to its callback, or an empty string for the
documented empty successes (HTTP 204 and an empty 200). The two download
endpoints hand over CSV text and write it to a path when one is given.

```c
#include "ZeroBounce/ZeroBounceWarmup.h"

ZeroBounceWarmup *zbw = new_zero_bounce_warmup_instance();
zero_bounce_warmup_initialize(zbw, "<YOUR_WARMUP_API_KEY>");

warmup_list_sending_accounts(zbw, on_raw, on_error);
warmup_get_engagement_rules(zbw, on_raw, on_error);
warmup_get_progress_today(zbw, on_raw, on_error);

/* Add a from-address you run yourself. A standard rule is 0 to 4. */
ZBWarmupAddSendingAccountOptions add = new_warmup_add_sending_account_options();
add.engagement_rule_id = 0;
add.ignore_conflict = 1;
warmup_add_sending_account(zbw, "sender@example.com", ZB_WARMUP_SELF_MANAGED, &add,
                           on_raw, on_error);

/* A custom rule: reply_pct 5-45, important_pct 5-40, click_pct 0-70 and at
   most important_pct x 1.75. All of it is checked before anything is sent. */
warmup_create_custom_engagement(zbw, "sender@example.com", 10, 20, 30, on_raw, on_error);

/* CSV, and saved to the path as well. */
warmup_download_seeds(zbw, "seeds.csv", on_raw, on_error);

/* Pass an email address, not a bare domain: that is what the live API wants. */
warmup_check_domain_dmarc(zbw, "sender@example.com", NULL, on_raw, on_error);

zero_bounce_warmup_free(zbw);
```

`warmup_test_smtp_configuration` stores nothing and sends its test message to a
ZeroBounce test recipient, not to `from_address`. The live API answers a failed
SMTP login with HTTP 500, which arrives as an error with
`ZBErrorResponse.status_code` 500; the password never appears in that message.

## Per-API base URLs

The bulk hosts, the list evaluator and the Inbox Placement Tester are not built
from `api_base_url`, so `zero_bounce_initialize_with_base_url` alone leaves them
pointing at the live API. Each one has a setter. They keep the pointer they are
given rather than a copy, so the string has to outlive the client.

```c
zero_bounce_set_bulk_api_base_url(zb, "https://bulkapi.zerobounce.net/v2");
zero_bounce_set_bulk_api_scoring_base_url(zb, "https://bulkapi.zerobounce.net/v2/scoring");
/* The S3 and SFTP jobs use the bulk host root, without /v2. */
zero_bounce_set_bulk_api_root_url(zb, "https://bulkapi.zerobounce.net");
zero_bounce_set_list_evaluator_base_url(zb, "https://bulkapi.zerobounce.net/v2/listevaluator");
zero_bounce_set_inbox_placement_base_url(zb, "https://bulkapi.zerobounce.net/api/ipt");
```

## DEVELOPMENT

It is recommended that the development should be done on Linux.

After checking out the repo, build the library then run tests.

```bash
./build/bin/ZeroBounceTests
```

You should see an output like this

```bash
24 Tests 0 Failures 0 Ignored
OK
```
