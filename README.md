## Zero Bounce C API Wrapper
This API wrapper contains function for interacting easily with ZeroBounce API.
More information about ZeroBounce you can find in the [official documentation](https://www.zerobounce.net/docs/).

## INSTALLATION
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

#### LINUX

```bash
mkdir build
cmake -S . -B build -G "Unix Makefiles"
cmake --build build
```

#### WINDOWS

You have to make sure that `MinGW` is installed on your system. It can be installed following the steps [here](https://code.visualstudio.com/docs/cpp/config-mingw).

```bash
mkdir build
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

Be aware that the library may require some .dll files (found in the `build` directory) in order to work properly in your project.

## USAGE
Include the library in your file:
```c
#include <ZeroBounce/ZeroBounce.h>
```

Initialize the wrapper with your api key and preferred api:
```c
ZeroBounce* zb = zero_bounce_get_instance();
zero_bounce_initialize(zb, "<YOUR_API_KEY>", ZB_Api_URL_Default);
```

## Examples
Then you can use any of the wrapper functions, for example:
* ##### Check how many credits you have left on your account
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_credits(ZBCreditsResponse response) {
    printf("%s\n\n", zb_credits_response_to_string(&response));
}

get_credits(zb, on_success_credits, on_error);
```

* ##### Check your API usage for a given period of time
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

* ##### Validate an email address
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

* ##### Validate email batch
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

* ##### Use the Activity API endpoint to gather insights into your subscribers'overall email engagement
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

* ##### The sendfile API allows user to send a file for bulk email validation
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

* ##### The getfile API allows users to get the validation results file for the file been submitted using sendfile API
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

* ##### Check the status of a file uploaded via "sendFile" method
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

* ##### Deletes the file that was submitted using sendfile API. File can be deleted only when its status is _`Complete`_
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

* ##### The scoringSendfile API allows user to send a file for bulk email validation
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

* ##### The scoringGetFile API allows users to get the validation results file for the file been submitted using scoringSendfile API
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

* ##### Check the status of a file uploaded via "scoringSendFile" method
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

* ##### Deletes the file that was submitted using scoring scoringSendfile API. File can be deleted only when its status is _`Complete`_
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
