#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#endif

#include "ZeroBounce/ZeroBounce.h"

static size_t write_callback(void *data, size_t size, size_t nmemb, void *clientp) {
  size_t real_size = size * nmemb;
  memory *mem = (memory*)clientp;

  char *ptr = realloc(mem->response, mem->size + real_size + 1);
  if(ptr == NULL)
    return 0;  /* out of memory! */

  mem->response = ptr;
  memcpy(&(mem->response[mem->size]), data, real_size);
  mem->size += real_size;
  mem->response[mem->size] = 0;

  return real_size;
}

void set_write_callback(CURL* curl, memory* response_data) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)response_data);
}

long get_http_code(CURL* curl) {
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    return http_code;
}

char* get_content_type_value(CURL* curl) {
    struct curl_header *content_type_header;
    curl_easy_header(curl, "Content-Type", 0, CURLH_HEADER, -1, &content_type_header);

    return content_type_header->value;
}


SendFileOptions new_send_file_options() {
    SendFileOptions options;

    options.return_url = "";
    options.first_name_column = 0;
    options.last_name_column = 0;
    options.gender_column = 0;
    options.ip_address_column = 0;
    options.has_header_row = true;
    options.remove_duplicate = true;

    return options;
}


ZeroBounce* new_zero_bounce_instance() {
    ZeroBounce* zb = (ZeroBounce*) malloc(sizeof(ZeroBounce));

    zb->api_key = NULL;
    zb->api_base_url = "https://api.zerobounce.net/v2";
    zb->bulk_api_base_url = "https://bulkapi.zerobounce.net/v2";
    zb->bulk_api_scoring_base_url = "https://bulkapi.zerobounce.net/v2/scoring";

    curl_global_init(CURL_GLOBAL_ALL);
    return zb;
}


ZeroBounce* zero_bounce_get_instance() {
    if (zero_bounce_instance == NULL) {
        zero_bounce_instance = new_zero_bounce_instance();
    }
    return zero_bounce_instance;
}


void zero_bounce_initialize(ZeroBounce* zb, const char* api_key) {
    if (zb->api_key) {
        free(zb->api_key);
    }
    zb->api_key = strdup(api_key);
}


void zero_bounce_initialize_with_base_url(ZeroBounce* zb, const char* api_key, ZBApiURL api_base_url) {
    if (zb->api_key) {
        free(zb->api_key);
    }
    zb->api_key = strdup(api_key);
    zb->api_base_url = base_url_string_from_zb_api_url(api_base_url);
}


static bool zero_bounce_invalid_api_key(ZeroBounce *zb, OnErrorCallback error_callback) {
    if (zb->api_key == NULL || strlen(zb->api_key) == 0) {
        ZBErrorResponse error_response = parse_error(
            "ZeroBounce is not initialized. Please call zero_bounce_initialize(zb_instance, api_key); first"
        );
        error_callback(error_response);
        return true;
    }
    return false;
}


static int make_request(
    char* url_path,
    char* request_type,
    char* header,
    memory* response_data,
    long* http_code,
    OnErrorCallback error_callback
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error_callback(parse_error("Failed to initialize libcurl"));
        curl_easy_cleanup(curl);
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request_type);
    curl_easy_setopt(curl, CURLOPT_URL, url_path);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, header);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    set_write_callback(curl, response_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        char error_message[300];
        sprintf(
            error_message,
            "Failed to perform request. CURLcode: %d. Error: %s",
            res,
            curl_easy_strerror(res)
        );
        error_callback(parse_error(error_message));

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        return 0;
    }

    *http_code = get_http_code(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return 1;
}

static void send_file_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_path,
    int email_address_column_index,
    SendFileOptions options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/sendfile";
    const char* base_url = scoring ? zb->bulk_api_scoring_base_url : zb->bulk_api_base_url;

    int url_path_len = snprintf(
        NULL, 0, url_pattern, base_url
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, base_url
    );

    memory response_data = {0};

    CURL* curl = curl_easy_init();
    if (!curl) {
        error_callback(parse_error("Failed to initialize libcurl"));
        curl_easy_cleanup(curl);
        goto cleanup;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_URL, url_path);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: multipart/form-data");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_mime *multipart = curl_mime_init(curl);

    curl_mimepart *part = curl_mime_addpart(multipart);
    curl_mime_name(part, "api_key");
    curl_mime_data(part, zb->api_key, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, file_path);

    char int_str_option[10];

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "email_address_column");
    sprintf(int_str_option, "%d", email_address_column_index);
    curl_mime_data(part, int_str_option, CURL_ZERO_TERMINATED);

    if (!scoring) {
        if (options.first_name_column > 0) {
            part = curl_mime_addpart(multipart);
            curl_mime_name(part, "first_name_column");
            sprintf(int_str_option, "%d", options.first_name_column);
            curl_mime_data(part, int_str_option, CURL_ZERO_TERMINATED);
        }
        if (options.last_name_column > 0) {
            part = curl_mime_addpart(multipart);
            curl_mime_name(part, "last_name_column");
            sprintf(int_str_option, "%d", options.last_name_column);
            curl_mime_data(part, int_str_option, CURL_ZERO_TERMINATED);
        }
        if (options.gender_column > 0) {
            part = curl_mime_addpart(multipart);
            curl_mime_name(part, "gender_column");
            sprintf(int_str_option, "%d", options.gender_column);
            curl_mime_data(part, int_str_option, CURL_ZERO_TERMINATED);
        }
        if (options.ip_address_column > 0) {
            part = curl_mime_addpart(multipart);
            curl_mime_name(part, "ip_address_column");
            sprintf(int_str_option, "%d", options.ip_address_column);
            curl_mime_data(part, int_str_option, CURL_ZERO_TERMINATED);
        }
    }

    if (!strlen(options.return_url) == 0) {
        part = curl_mime_addpart(multipart);
        curl_mime_name(part, "return_url");
        curl_mime_data(part, options.return_url, CURL_ZERO_TERMINATED);
    }

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "has_header_row");
    curl_mime_data(part, options.has_header_row ? "true" : "false", CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "remove_duplicate");
    curl_mime_data(part, options.has_header_row ? "true" : "false", CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, multipart);

    set_write_callback(curl, &response_data);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        char error_message[300];
        sprintf(
            error_message,
            "Failed to perform request. CURLcode: %d. Error: %s",
            res,
            curl_easy_strerror(res)
        );
        error_callback(parse_error(error_message));

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        curl_mime_free(multipart);
        goto cleanup;
    }

    long http_code = get_http_code(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_mime_free(multipart);

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }

            success_callback(zb_send_file_response_from_json(j_obj));
            json_object_put(j_obj);
        }
    }

    cleanup:
    free(url_path);
    free(response_data.response);
}


static void file_status_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/filestatus?api_key=%s&file_id=%s";
    const char* base_url = scoring ? zb->bulk_api_scoring_base_url : zb->bulk_api_base_url;

    int url_path_len = snprintf(
        NULL, 0, url_pattern, base_url, zb->api_key, file_id
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, base_url, zb->api_key, file_id
    );

    memory response_data = {0};
    long http_code;

    if(!make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }

            success_callback(zb_file_status_response_from_json(j_obj));
            json_object_put(j_obj);
        }
    }

    cleanup:
    free(url_path);
    free(response_data.response);
}


static void get_file_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_id,
    char* local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/getfile?api_key=%s&file_id=%s";
    const char* base_url = scoring ? zb->bulk_api_scoring_base_url : zb->bulk_api_base_url;

    int url_path_len = snprintf(
        NULL, 0, url_pattern, base_url, zb->api_key, file_id
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, base_url, zb->api_key, file_id
    );

    memory response_data = {0};

    CURL* curl = curl_easy_init();
    if (!curl) {
        error_callback(parse_error("Failed to initialize libcurl"));
        curl_easy_cleanup(curl);
        goto cleanup;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(curl, CURLOPT_URL, url_path);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    set_write_callback(curl, &response_data);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        char error_message[300];
        sprintf(
            error_message,
            "Failed to perform request. CURLcode: %d. Error: %s",
            res,
            curl_easy_strerror(res)
        );
        error_callback(parse_error(error_message));

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        goto cleanup;
    }

    long http_code = get_http_code(curl);

    char* content_type = get_content_type_value(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            if (strcmp(content_type, "application/json") != 0) {
                struct stat sb;

                #ifdef _WIN32
                const char path_separator = '\\';
                #define mkdir(path, mode) _mkdir(path)
                #else
                const char path_separator = '/';
                #endif

                if (stat(local_download_path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
                    error_callback(parse_error("Invalid file path"));
                    goto cleanup;
                }

                char* dir_path = strdup(local_download_path);
                char* end = strrchr(dir_path, path_separator);
                if (end != NULL) {
                    *end = '\0';
                    mkdir(dir_path, 0777);
                }
                free(dir_path);

                FILE *file_stream = fopen(local_download_path, "wb");
                if (file_stream == NULL) {
                    perror("fopen");
                    goto cleanup;
                }
                fwrite(response_data.response, sizeof(char), strlen(response_data.response), file_stream);
                fclose(file_stream);

                ZBGetFileResponse response = new_zb_get_file_response();
                response.success = true;
                response.local_file_path = local_download_path;
                success_callback(response);
            } else {
                json_object *j_obj = json_tokener_parse(response_data.response);
                if (j_obj == NULL) {
                    error_callback(parse_error("Failed to parse json string"));
                    goto cleanup;
                }

                success_callback(zb_get_file_response_from_json(j_obj));
                json_object_put(j_obj);
            }
        }
    }

    cleanup:
    free(url_path);
    free(response_data.response);
}


static void delete_file_internal(
    ZeroBounce *zb,
    bool scoring,
    char* file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/deletefile?api_key=%s&file_id=%s";
    const char* base_url = scoring ? zb->bulk_api_scoring_base_url : zb->bulk_api_base_url;

    int url_path_len = snprintf(
        NULL, 0, url_pattern, base_url, zb->api_key, file_id
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, base_url, zb->api_key, file_id
    );

    memory response_data = {0};
    long http_code;

    if(!make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }

            success_callback(zb_delete_file_response_from_json(j_obj));
            json_object_put(j_obj);
        }
    }

    cleanup:
    free(url_path);
    free(response_data.response);
}

static void find_email_internal(
    ZeroBounce* zb,
    char* domain,
    char* company_name,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    StringVector string_vector = string_vector_init();
    string_vector_append(&string_vector, zb->api_base_url);
    string_vector_append(&string_vector, strdup("/guessformat?api_key="));
    string_vector_append(&string_vector, zb->api_key);

    if (domain != NULL && strlen(domain)) {
        string_vector_append(&string_vector, strdup("&domain="));
        string_vector_append(&string_vector, domain);
    }
    if (company_name != NULL && strlen(company_name)) {
        string_vector_append(&string_vector, strdup("&company_name="));
        string_vector_append(&string_vector, company_name);
    }
    if (first_name != NULL && strlen(first_name)) {
        string_vector_append(&string_vector, strdup("&first_name="));
        string_vector_append(&string_vector, first_name);
    }
    if (middle_name != NULL && strlen(middle_name)) {
        string_vector_append(&string_vector, strdup("&middle_name="));
        string_vector_append(&string_vector, middle_name);
    }
    if (last_name != NULL && strlen(last_name)) {
        string_vector_append(&string_vector, strdup("&last_name="));
        string_vector_append(&string_vector, last_name);
    }
    char *url_path = concatenate_strings(&string_vector, "");
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    };

    memory response_data = {0};
    long http_code;

    if(!make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }
            ZBFindEmailResponse response_obj = zb_find_email_response_from_json(j_obj);
            success_callback(response_obj);
            zb_find_email_response_free(&response_obj);
            json_object_put(j_obj);
        }
    }

    cleanup:
    string_vector_free(&string_vector);
    free(url_path);
    free(response_data.response);
}

static void search_domain_internal(
    ZeroBounce* zb,
    char* domain,
    char* company_name,
    OnSuccessCallbackDomainSearch success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    StringVector string_vector = string_vector_init();
    string_vector_append(&string_vector, zb->api_base_url);
    string_vector_append(&string_vector, strdup("/guessformat?api_key="));
    string_vector_append(&string_vector, zb->api_key);

    if (domain != NULL && strlen(domain)) {
        string_vector_append(&string_vector, strdup("&domain="));
        string_vector_append(&string_vector, domain);
    }
    if (company_name != NULL && strlen(company_name)) {
        string_vector_append(&string_vector, strdup("&company_name="));
        string_vector_append(&string_vector, company_name);
    }
    char *url_path = concatenate_strings(&string_vector, "");
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    };

    memory response_data = {0};
    long http_code;

    if(!make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }
            ZBDomainSearchResponse response_obj = zb_domain_search_response_from_json(j_obj);
            success_callback(response_obj);
            zb_domain_search_response_free(&response_obj);
            json_object_put(j_obj);
        }
    }

    cleanup:
    string_vector_free(&string_vector);
    free(url_path);
    free(response_data.response);
}


void get_credits(
    ZeroBounce *zb,
    OnSuccessCallbackCredits success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/getcredits?api_key=%s";

    int url_path_len = snprintf(
        NULL, 0, url_pattern, zb->api_base_url, zb->api_key
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, zb->api_base_url, zb->api_key
    );

    memory response_data = {0};
    long http_code;

    if(!make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }

            success_callback(zb_credits_response_from_json(j_obj));
            json_object_put(j_obj);
        }
    }

    cleanup:
    free(url_path);
    free(response_data.response);
}


void get_api_usage(
    ZeroBounce *zb,
    struct tm start_date,
    struct tm end_date,
    OnSuccessCallbackApiUsage success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    char* date_format = "%Y-%m-%d";
    char start_date_str[11];
    char end_date_str[11];

    strftime(start_date_str, sizeof(start_date_str), date_format, &start_date);
    strftime(end_date_str, sizeof(end_date_str), date_format, &end_date);

    const char *url_pattern = "%s/getapiusage?api_key=%s&start_date=%s&end_date=%s";

    int url_path_len = snprintf(
        NULL, 0, url_pattern, zb->api_base_url, zb->api_key,
        start_date_str, end_date_str
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, zb->api_base_url, zb->api_key,
        start_date_str, end_date_str
    );

    memory response_data = {0};
    long http_code;

    if(!make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }

            success_callback(zb_get_api_usage_response_from_json(j_obj));
            json_object_put(j_obj);
        }
    }

    cleanup:
    free(url_path);
    free(response_data.response);
}


void validate_email(
    ZeroBounce *zb,
    char* email,
    char* ip_address,
    OnSuccessCallbackValidate success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/validate?api_key=%s&email=%s&ip_address=%s";

    int url_path_len = snprintf(
        NULL, 0, url_pattern, zb->api_base_url, zb->api_key, email, ip_address
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, zb->api_base_url, zb->api_key, email, ip_address
    );

    memory response_data = {0};
    long http_code;

    if(!make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }

            success_callback(zb_validate_response_from_json(j_obj));
            json_object_put(j_obj);
        }
    }

    cleanup:
    free(url_path);
    free(response_data.response);
}


void validate_email_batch(
    ZeroBounce *zb,
    EmailToValidateVector email_batch,
    OnSuccessCallbackValidateBatch success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/validatebatch";

    int url_path_len = snprintf(
        NULL, 0, url_pattern, zb->bulk_api_base_url
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, zb->bulk_api_base_url
    );

    json_object *payload = json_object_new_object();
    json_object *email_batch_json = json_object_new_array();

    for(size_t i = 0; i < email_batch.size; i++) {
        json_object *email_obj = json_object_new_object();
        json_object_object_add(email_obj, "email_address", json_object_new_string(email_batch.data[i].email_address));
        json_object_object_add(email_obj, "ip_address", json_object_new_string(email_batch.data[i].ip_address));
        json_object_array_add(email_batch_json, email_obj);
    }

    json_object_object_add(payload, "api_key", json_object_new_string(zb->api_key));
    json_object_object_add(payload, "email_batch", email_batch_json);

    memory response_data = {0};

    CURL* curl = curl_easy_init();
    if (!curl) {
        error_callback(parse_error("Failed to initialize libcurl"));
        curl_easy_cleanup(curl);
        goto cleanup;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_URL, url_path);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, json_object_to_json_string(payload));

    json_object_put(payload);

    set_write_callback(curl, &response_data);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        char error_message[300];
        sprintf(
            error_message,
            "Failed to perform request. CURLcode: %d. Error: %s",
            res,
            curl_easy_strerror(res)
        );
        error_callback(parse_error(error_message));

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        goto cleanup;
    }

    long http_code = get_http_code(curl);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }

            success_callback(zb_validate_batch_response_from_json(j_obj));
            json_object_put(j_obj);
        }
    }

    cleanup:
    free(url_path);
    free(response_data.response);
}


void send_file(
    ZeroBounce *zb,
    char* file_path,
    int email_address_column_index,
    SendFileOptions options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    send_file_internal(zb, false, file_path, email_address_column_index, options, success_callback, error_callback);
}


void file_status(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
) {
    file_status_internal(zb, false, file_id, success_callback, error_callback);
}


void get_file(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    get_file_internal(zb, false, file_id, local_download_path, success_callback, error_callback);
}


void delete_file(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
) {
    delete_file_internal(zb, false, file_id, success_callback, error_callback);
}

void scoring_send_file(
    ZeroBounce *zb,
    char* file_path,
    int email_address_column_index,
    SendFileOptions options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    send_file_internal(zb, true, file_path, email_address_column_index, options, success_callback, error_callback);
}

void scoring_file_status(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
) {
    file_status_internal(zb, true, file_id, success_callback, error_callback);
}

void scoring_get_file(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    get_file_internal(zb, true, file_id, local_download_path, success_callback, error_callback);
}

void scoring_delete_file(
    ZeroBounce *zb,
    char* file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
) {
    delete_file_internal(zb, true, file_id, success_callback, error_callback);
}


void get_activity_data(
    ZeroBounce *zb,
    char* email,
    OnSuccessCallbackActivityData success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    const char *url_pattern = "%s/activity?api_key=%s&email=%s";

    int url_path_len = snprintf(
        NULL, 0, url_pattern, zb->api_base_url, zb->api_key, email
    );

    char *url_path = malloc(url_path_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    snprintf(
        url_path, url_path_len + 1, url_pattern, zb->api_base_url, zb->api_key, email
    );

    memory response_data = {0};
    long http_code;

    if(!make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }

            success_callback(zb_activity_data_response_from_json(j_obj));
            json_object_put(j_obj);
        }
    }

    cleanup:
    free(url_path);
    free(response_data.response);
}


void find_email_by_domain_first_middle_last_name(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, domain, NULL, first_name, middle_name, last_name, success_callback, error_callback);
}

void find_email_by_domain_first_last_name(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, domain, NULL, first_name, NULL, last_name, success_callback, error_callback);
}

void find_email_by_domain_first_name(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, domain, NULL, first_name, NULL, NULL, success_callback, error_callback);
}

void find_email_by_company_name_first_middle_last_name(
    ZeroBounce* zb,
    char* company_name,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, NULL, company_name, first_name, middle_name, last_name, success_callback, error_callback);
}

void find_email_by_company_name_first_last_name(
    ZeroBounce* zb,
    char* company_name,
    char* first_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, NULL, company_name, first_name, NULL, last_name, success_callback, error_callback);
}

void find_email_by_company_name_first_name(
    ZeroBounce* zb,
    char* company_name,
    char* first_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    find_email_internal(zb, NULL, company_name, first_name, NULL, NULL, success_callback, error_callback);
}

void search_domain_by_domain(
    ZeroBounce* zb,
    char* domain,
    OnSuccessCallbackDomainSearch success_callback,
    OnErrorCallback error_callback
) {
    search_domain_internal(zb, domain, NULL, success_callback, error_callback);
}

void search_domain_by_company_name(
    ZeroBounce* zb,
    char* company_name,
    OnSuccessCallbackDomainSearch success_callback,
    OnErrorCallback error_callback
) {
    search_domain_internal(zb, NULL, company_name, success_callback, error_callback);
}


void find_email(
    ZeroBounce* zb,
    char* domain,
    char* first_name,
    char* middle_name,
    char* last_name,
    OnSuccessCallbackFindEmail success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    StringVector string_vector = string_vector_init();
    string_vector_append(&string_vector, zb->api_base_url);
    string_vector_append(&string_vector, strdup("/guessformat?api_key="));
    string_vector_append(&string_vector, zb->api_key);

    if (domain != NULL && strlen(domain)) {
        string_vector_append(&string_vector, strdup("&domain="));
        string_vector_append(&string_vector, domain);
    }
    if (first_name != NULL && strlen(first_name)) {
        string_vector_append(&string_vector, strdup("&first_name="));
        string_vector_append(&string_vector, first_name);
    }
    if (middle_name != NULL && strlen(middle_name)) {
        string_vector_append(&string_vector, strdup("&middle_name="));
        string_vector_append(&string_vector, middle_name);
    }
    if (last_name != NULL && strlen(last_name)) {
        string_vector_append(&string_vector, strdup("&last_name="));
        string_vector_append(&string_vector, last_name);
    }
    char *url_path = concatenate_strings(&string_vector, "");
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    };

    memory response_data = {0};
    long http_code;

    if(!make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)){
        goto cleanup;
    }

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else {
        if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
                goto cleanup;
            }
            ZBFindEmailResponse response_obj = zb_find_email_response_from_json(j_obj);
            success_callback(response_obj);
            zb_find_email_response_free(&response_obj);
            json_object_put(j_obj);
        }
    }

    cleanup:
    string_vector_free(&string_vector);
    free(url_path);
    free(response_data.response);
}