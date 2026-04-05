#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <direct.h>
#define ZB_STRICMP _stricmp
#define zb_strncasecmp _strnicmp
#else
#include <strings.h>
#define ZB_STRICMP strcasecmp
#define zb_strncasecmp strncasecmp
#endif

#include <json.h>

#include "ZeroBounce/ZeroBounce.h"

static ZBHttpPerformFn s_http_perform = NULL;

void zero_bounce_set_http_perform_for_test(ZBHttpPerformFn fn) {
    s_http_perform = fn;
}

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
    options.allow_phase_2_is_set = false;
    options.allow_phase_2 = false;

    return options;
}


GetFileOptions new_get_file_options(void) {
    GetFileOptions o = { NULL, -1 };
    return o;
}


static bool zb_ct_includes_application_json(const char *content_type) {
    if (!content_type) {
        return false;
    }
    for (const char *p = content_type; *p; p++) {
        if (zb_strncasecmp(p, "application/json", 16) == 0) {
            return true;
        }
    }
    return false;
}


bool zb_get_file_json_indicates_error(const char *body) {
    if (!body) {
        return false;
    }
    while (*body == ' ' || *body == '\t' || *body == '\n' || *body == '\r') {
        body++;
    }
    if (*body != '{') {
        return false;
    }
    json_object *o = json_tokener_parse(body);
    if (!o || json_object_get_type(o) != json_type_object) {
        if (o) {
            json_object_put(o);
        }
        return false;
    }

    json_object *s = json_object_object_get(o, "success");
    if (s) {
        if (json_object_is_type(s, json_type_boolean) && !json_object_get_boolean(s)) {
            json_object_put(o);
            return true;
        }
        if (json_object_is_type(s, json_type_string)) {
            const char *sv = json_object_get_string(s);
            if (sv && (ZB_STRICMP(sv, "false") == 0 || ZB_STRICMP(sv, "False") == 0)) {
                json_object_put(o);
                return true;
            }
        }
    }

    const char *keys[] = {"message", "error", "error_message"};
    for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        json_object *v = json_object_object_get(o, keys[k]);
        if (!v || json_object_is_type(v, json_type_null)) {
            continue;
        }
        if (json_object_is_type(v, json_type_string)) {
            const char *t = json_object_get_string(v);
            if (t && t[0]) {
                json_object_put(o);
                return true;
            }
        }
        if (json_object_is_type(v, json_type_array) && json_object_array_length(v) > 0) {
            json_object_put(o);
            return true;
        }
    }

    bool has_success_key = json_object_object_get(o, "success") != NULL;
    json_object_put(o);
    return has_success_key;
}


static bool zb_should_treat_getfile_body_as_error(const char *body, const char *content_type) {
    if (zb_ct_includes_application_json(content_type)) {
        return true;
    }
    return zb_get_file_json_indicates_error(body);
}


char *zb_format_get_file_error_message(const char *body) {
    if (!body) {
        return strdup("Invalid getfile response");
    }
    json_object *o = json_tokener_parse(body);
    if (!o || json_object_get_type(o) != json_type_object) {
        if (o) {
            json_object_put(o);
        }
        return strdup(body[0] ? body : "Invalid getfile response");
    }

    const char *keys[] = {"message", "error", "error_message"};
    for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        json_object *v = json_object_object_get(o, keys[k]);
        if (!v || json_object_is_type(v, json_type_null)) {
            continue;
        }
        if (json_object_is_type(v, json_type_string)) {
            const char *t = json_object_get_string(v);
            if (t && t[0]) {
                char *r = strdup(t);
                json_object_put(o);
                return r ? r : strdup("");
            }
        }
        if (json_object_is_type(v, json_type_array) && json_object_array_length(v) > 0) {
            json_object *item = json_object_array_get_idx(v, 0);
            if (json_object_is_type(item, json_type_string)) {
                const char *t = json_object_get_string(item);
                if (t && t[0]) {
                    char *r = strdup(t);
                    json_object_put(o);
                    return r ? r : strdup("");
                }
            }
        }
    }

    json_object_put(o);
    return strdup(body);
}


static char *zb_build_getfile_url(
    ZeroBounce *zb,
    bool scoring,
    const char *file_id,
    const GetFileOptions *opts
) {
    const char *base_url = scoring ? zb->bulk_api_scoring_base_url : zb->bulk_api_base_url;
    CURL *curl = curl_easy_init();
    if (!curl) {
        return NULL;
    }

    char *ek = curl_easy_escape(curl, zb->api_key, 0);
    char *fid = curl_easy_escape(curl, file_id, 0);
    if (!ek || !fid) {
        curl_free(ek);
        curl_free(fid);
        curl_easy_cleanup(curl);
        return NULL;
    }

    int base_len = snprintf(NULL, 0, "%s/getfile?api_key=%s&file_id=%s", base_url, ek, fid);
    if (base_len < 0) {
        curl_free(ek);
        curl_free(fid);
        curl_easy_cleanup(curl);
        return NULL;
    }

    char *dt_esc = NULL;
    int dt_len = 0;
    if (opts && opts->download_type && opts->download_type[0]) {
        dt_esc = curl_easy_escape(curl, opts->download_type, 0);
        if (!dt_esc) {
            curl_free(ek);
            curl_free(fid);
            curl_easy_cleanup(curl);
            return NULL;
        }
        dt_len = snprintf(NULL, 0, "&download_type=%s", dt_esc);
        if (dt_len < 0) {
            curl_free(dt_esc);
            curl_free(ek);
            curl_free(fid);
            curl_easy_cleanup(curl);
            return NULL;
        }
    }

    int ad_len = 0;
    if (!scoring && opts && opts->activity_data >= 0) {
        ad_len = snprintf(NULL, 0, "&activity_data=%s", opts->activity_data ? "true" : "false");
        if (ad_len < 0) {
            curl_free(dt_esc);
            curl_free(ek);
            curl_free(fid);
            curl_easy_cleanup(curl);
            return NULL;
        }
    }

    size_t total = (size_t)base_len + (size_t)dt_len + (size_t)ad_len + 1;
    char *url = malloc(total);
    if (!url) {
        curl_free(dt_esc);
        curl_free(ek);
        curl_free(fid);
        curl_easy_cleanup(curl);
        return NULL;
    }

    snprintf(url, total, "%s/getfile?api_key=%s&file_id=%s", base_url, ek, fid);
    if (dt_esc) {
        snprintf(url + strlen(url), total - strlen(url), "&download_type=%s", dt_esc);
    }
    if (!scoring && opts && opts->activity_data >= 0) {
        snprintf(url + strlen(url), total - strlen(url), "&activity_data=%s", opts->activity_data ? "true" : "false");
    }

    curl_free(dt_esc);
    curl_free(ek);
    curl_free(fid);
    curl_easy_cleanup(curl);
    return url;
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
    if (s_http_perform) {
        return s_http_perform(response_data, http_code, NULL);
    }

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

    if (s_http_perform) {
        long http_code = 0;
        if (!s_http_perform(&response_data, &http_code, NULL)) {
            if (error_callback) error_callback(parse_error("Mock request failed"));
            goto cleanup;
        }
        if (http_code > 299) {
            if (error_callback) error_callback(parse_error(response_data.response));
        } else if (success_callback) {
            json_object *j_obj = json_tokener_parse(response_data.response);
            if (j_obj == NULL) {
                error_callback(parse_error("Failed to parse json string"));
            } else {
                success_callback(zb_send_file_response_from_json(j_obj));
                json_object_put(j_obj);
            }
        }
        goto cleanup;
    }

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

    if (strlen(options.return_url) != 0) {
        part = curl_mime_addpart(multipart);
        curl_mime_name(part, "return_url");
        curl_mime_data(part, options.return_url, CURL_ZERO_TERMINATED);
    }

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "has_header_row");
    curl_mime_data(part, options.has_header_row ? "true" : "false", CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "remove_duplicate");
    curl_mime_data(part, options.remove_duplicate ? "true" : "false", CURL_ZERO_TERMINATED);

    if (!scoring && options.allow_phase_2_is_set) {
        part = curl_mime_addpart(multipart);
        curl_mime_name(part, "allow_phase_2");
        curl_mime_data(part, options.allow_phase_2 ? "true" : "false", CURL_ZERO_TERMINATED);
    }

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
    const GetFileOptions *get_file_options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) return;

    GetFileOptions default_opts = new_get_file_options();
    if (!get_file_options) {
        get_file_options = &default_opts;
    }

    char *url_path = zb_build_getfile_url(zb, scoring, file_id, get_file_options);
    if (!url_path) {
        if (error_callback) {
            error_callback(parse_error("Failed to build getfile URL"));
        }
        return;
    }

    memory response_data = {0};
    long http_code = 0;
    char* content_type = NULL;

    if (s_http_perform) {
        if (!s_http_perform(&response_data, &http_code, &content_type)) {
            if (error_callback) error_callback(parse_error("Mock request failed"));
            goto cleanup;
        }
        if (!content_type) {
            content_type = strdup("application/octet-stream");
        }
    } else {
        CURL* curl = curl_easy_init();
        if (!curl) {
            error_callback(parse_error("Failed to initialize libcurl"));
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

        http_code = get_http_code(curl);
        struct curl_header *hdr = NULL;
        CURLHcode hch = curl_easy_header(curl, "Content-Type", 0, CURLH_HEADER, -1, &hdr);
        if (hch == CURLHE_OK && hdr && hdr->value) {
            content_type = strdup(hdr->value);
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    const char *body = response_data.response ? response_data.response : "";

    if (http_code > 299) {
        if (error_callback) {
            char *fmt = NULL;
            if (body[0] == '{') {
                fmt = zb_format_get_file_error_message(body);
            }
            error_callback(parse_error(fmt && fmt[0] ? fmt : body));
            free(fmt);
        }
    } else if (success_callback) {
        if (zb_should_treat_getfile_body_as_error(body, content_type)) {
            if (error_callback) {
                char *msg = zb_format_get_file_error_message(body);
                error_callback(parse_error(msg ? msg : ""));
                free(msg);
            }
        } else {
            struct stat sb;

            #ifdef _WIN32
            const char path_separator = '\\';
            #define mkdir(path, mode) _mkdir(path)
            #else
            const char path_separator = '/';
            #endif

            if (stat(local_download_path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
                if (error_callback) {
                    error_callback(parse_error("Invalid file path"));
                }
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
            fwrite(response_data.response, 1, response_data.size, file_stream);
            fclose(file_stream);

            ZBGetFileResponse response = new_zb_get_file_response();
            response.success = true;
            response.local_file_path = local_download_path;
            success_callback(response);
        }
    }

    cleanup:
    free(url_path);
    free(response_data.response);
    free(content_type);
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
    long http_code = 0;

    if (s_http_perform) {
        json_object_put(payload);
        if (!s_http_perform(&response_data, &http_code, NULL)) {
            if (error_callback) error_callback(parse_error("Mock request failed"));
            return;
        }
    } else {
        CURL* curl = curl_easy_init();
        if (!curl) {
            error_callback(parse_error("Failed to initialize libcurl"));
            curl_easy_cleanup(curl);
            json_object_put(payload);
            return;
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
            return;
        }

        http_code = get_http_code(curl);

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
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
    get_file_internal(zb, false, file_id, local_download_path, NULL, success_callback, error_callback);
}


void get_file_with_options(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    get_file_internal(zb, false, file_id, local_download_path, options, success_callback, error_callback);
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
    get_file_internal(zb, true, file_id, local_download_path, NULL, success_callback, error_callback);
}


void scoring_get_file_with_options(
    ZeroBounce *zb,
    char* file_id,
    char* local_download_path,
    const GetFileOptions *options,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    get_file_internal(zb, true, file_id, local_download_path, options, success_callback, error_callback);
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