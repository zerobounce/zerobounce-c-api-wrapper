#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <json.h>

#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/zb_internal.h"

typedef void (*p3_mime_fields_fn)(curl_mime *multipart, void *ctx);

EmailFinderSendFileOptions new_email_finder_send_file_options(void) {
    EmailFinderSendFileOptions o = {0, 0, 0, 0, -1};
    return o;
}

DomainSearchSendFileOptions new_domain_search_send_file_options(void) {
    DomainSearchSendFileOptions o = {-1};
    return o;
}

ValidateS3FileOptions new_validate_s3_file_options(void) {
    ValidateS3FileOptions o = {0};
    o.has_header_row = -1;
    o.remove_duplicates = -1;
    o.remove_duplicate = -1;
    o.email_address_column = -1;
    return o;
}

ScoreS3FileOptions new_score_s3_file_options(void) {
    ScoreS3FileOptions o = {0};
    o.has_header_row = -1;
    o.remove_duplicates = -1;
    o.remove_duplicate = -1;
    o.email_address_column = -1;
    return o;
}

static bool p3_append_form_field(char **body, size_t *cap, const char *key, const char *value) {
    if (!key || !value) {
        return false;
    }
    char *ek = zb_url_escape(key);
    char *ev = zb_url_escape(value);
    if (!ek || !ev) {
        free(ek);
        free(ev);
        return false;
    }
    size_t need = strlen(ek) + strlen(ev) + 2 + (*body ? strlen(*body) + 1 : 0);
    if (*body && strchr(*body, '=')) {
        need += 1;
    }
    if (need > *cap) {
        size_t new_cap = (*cap == 0) ? 256 : *cap * 2;
        while (new_cap < need) {
            new_cap *= 2;
        }
        char *n = realloc(*body, new_cap);
        if (!n) {
            free(ek);
            free(ev);
            return false;
        }
        *body = n;
        *cap = new_cap;
    }
    if (!*body) {
        *body = calloc(1, *cap);
        if (!*body) {
            free(ek);
            free(ev);
            return false;
        }
    }
    size_t len = strlen(*body);
    if (len > 0) {
        strcat(*body, "&");
    }
    strcat(*body, ek);
    strcat(*body, "=");
    strcat(*body, ev);
    free(ek);
    free(ev);
    return true;
}

static void p3_append_optional_bool_form(char **body, size_t *cap, const char *key, int tri_state) {
    if (tri_state < 0) {
        return;
    }
    p3_append_form_field(body, cap, key, tri_state ? "true" : "false");
}

static void p3_send_json_success(
    memory *response_data,
    long http_code,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data->response));
        }
        return;
    }
    if (success_callback) {
        success_callback(response_data->response ? response_data->response : "");
    }
}

static void p3_raw_json_get(
    ZeroBounce *zb,
    char *url_path,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    memory response_data = {0};
    long http_code = 0;
    if (!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)) {
        free(response_data.response);
        free(url_path);
        return;
    }
    p3_send_json_success(&response_data, http_code, success_callback, error_callback);
    free(response_data.response);
    free(url_path);
}

static void p3_raw_json_form_post(
    ZeroBounce *zb,
    char *url_path,
    char *body,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    memory response_data = {0};
    long http_code = 0;
    if (!zb_make_request_with_post_body(url_path, body, &response_data, &http_code, error_callback)) {
        free(response_data.response);
        free(url_path);
        free(body);
        return;
    }
    p3_send_json_success(&response_data, http_code, success_callback, error_callback);
    free(response_data.response);
    free(url_path);
    free(body);
}

static void p3_send_file_json_success(
    memory *response_data,
    long http_code,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data->response));
        }
        return;
    }
    if (success_callback) {
        json_object *j_obj = json_tokener_parse(response_data->response);
        if (j_obj == NULL) {
            if (error_callback) {
                error_callback(parse_error("Failed to parse json string"));
            }
            return;
        }
        success_callback(zb_send_file_response_from_json(j_obj));
        json_object_put(j_obj);
    }
}

static void p3_post_send_file_form(
    ZeroBounce *zb,
    const char *url,
    char *body,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    char *url_path = strdup(url);
    if (!url_path) {
        free(body);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    memory response_data = {0};
    long http_code = 0;
    if (!zb_make_request_with_post_body(url_path, body, &response_data, &http_code, error_callback)) {
        free(response_data.response);
        free(url_path);
        return;
    }
    p3_send_file_json_success(&response_data, http_code, success_callback, error_callback);
    free(response_data.response);
    free(url_path);
}

static void p3_mime_add_int(curl_mime *multipart, const char *name, int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    curl_mimepart *part = curl_mime_addpart(multipart);
    curl_mime_name(part, name);
    curl_mime_data(part, buf, CURL_ZERO_TERMINATED);
}

static void p3_mime_add_optional_bool(curl_mime *multipart, const char *name, int tri_state) {
    if (tri_state == 1) {
        curl_mimepart *part = curl_mime_addpart(multipart);
        curl_mime_name(part, name);
        curl_mime_data(part, "true", CURL_ZERO_TERMINATED);
    }
}

static void p3_bulk_root_multipart(
    ZeroBounce *zb,
    const char *url,
    char *file_path,
    p3_mime_fields_fn add_fields,
    void *fields_ctx,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }

    memory response_data = {0};
    char *url_path = strdup(url);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    long http_code = 0;
    zero_bounce_record_request_url_for_test(url_path);
    if (zb_http_perform_if_mock(&response_data, &http_code, NULL)) {
        p3_send_file_json_success(&response_data, http_code, success_callback, error_callback);
        goto cleanup;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (error_callback) {
            error_callback(parse_error("Failed to initialize libcurl"));
        }
        goto cleanup;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_URL, url_path);
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: multipart/form-data");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_mime *multipart = curl_mime_init(curl);
    curl_mimepart *part = curl_mime_addpart(multipart);
    curl_mime_name(part, "api_key");
    curl_mime_data(part, zb->api_key, CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, file_path);

    if (add_fields) {
        add_fields(multipart, fields_ctx);
    }

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, multipart);
    set_write_callback(curl, &response_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        char error_message[300];
        snprintf(
            error_message,
            sizeof(error_message),
            "Failed to perform request. CURLcode: %d. Error: %s",
            res,
            curl_easy_strerror(res)
        );
        if (error_callback) {
            error_callback(parse_error(error_message));
        }
        curl_mime_free(multipart);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        goto cleanup;
    }

    http_code = get_http_code(curl);
    curl_mime_free(multipart);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    p3_send_file_json_success(&response_data, http_code, success_callback, error_callback);

cleanup:
    free(url_path);
    free(response_data.response);
}

static void p3_bulk_root_get_file(
    ZeroBounce *zb,
    const char *base_url,
    char *file_id,
    char *local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }

    char *api_key_esc = zb_url_escape(zb->api_key);
    char *file_id_esc = zb_url_escape(file_id);
    const char *url_pattern = "%s/getfile?api_key=%s&file_id=%s";
    int url_path_len = snprintf(NULL, 0, url_pattern, base_url, api_key_esc, file_id_esc);
    char *url_path = malloc((size_t)url_path_len + 1);
    if (!url_path) {
        free(api_key_esc);
        free(file_id_esc);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    snprintf(url_path, (size_t)url_path_len + 1, url_pattern, base_url, api_key_esc, file_id_esc);
    free(api_key_esc);
    free(file_id_esc);

    memory response_data = {0};
    response_data.max_bytes = ZB_MAX_GETFILE_BYTES;
    long http_code = 0;
    char *content_type = NULL;

    zero_bounce_record_request_url_for_test(url_path);
    if (!zb_http_perform_if_mock(&response_data, &http_code, &content_type)) {
        CURL *curl = curl_easy_init();
        if (!curl) {
            if (error_callback) {
                error_callback(parse_error("Failed to initialize libcurl"));
            }
            goto cleanup;
        }
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
        curl_easy_setopt(curl, CURLOPT_URL, url_path);
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        set_write_callback(curl, &response_data);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            if (error_callback) {
                error_callback(parse_error(curl_easy_strerror(res)));
            }
            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            goto cleanup;
        }
        http_code = get_http_code(curl);
        content_type = get_content_type_value(curl);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else if (zb_get_file_json_indicates_error(response_data.response)) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else if (success_callback) {
        FILE *file_stream = fopen(local_download_path, "wb");
        if (file_stream == NULL) {
            if (error_callback) {
                error_callback(parse_error("Invalid file path"));
            }
        } else {
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

static void p3_bulk_root_file_status(
    ZeroBounce *zb,
    const char *base_url,
    char *file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    char *api_key_esc = zb_url_escape(zb->api_key);
    char *file_id_esc = zb_url_escape(file_id);
    const char *url_pattern = "%s/filestatus?api_key=%s&file_id=%s";
    int url_path_len = snprintf(NULL, 0, url_pattern, base_url, api_key_esc, file_id_esc);
    char *url_path = malloc((size_t)url_path_len + 1);
    snprintf(url_path, (size_t)url_path_len + 1, url_pattern, base_url, api_key_esc, file_id_esc);
    free(api_key_esc);
    free(file_id_esc);

    memory response_data = {0};
    long http_code = 0;
    if (!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)) {
        free(response_data.response);
        free(url_path);
        return;
    }
    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else if (success_callback) {
        json_object *j_obj = json_tokener_parse(response_data.response);
        if (j_obj == NULL) {
            if (error_callback) {
                error_callback(parse_error("Failed to parse json string"));
            }
        } else {
            success_callback(zb_file_status_response_from_json(j_obj));
            json_object_put(j_obj);
        }
    }
    free(response_data.response);
    free(url_path);
}

static void p3_bulk_root_delete_file(
    ZeroBounce *zb,
    const char *base_url,
    char *file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    char *api_key_esc = zb_url_escape(zb->api_key);
    char *file_id_esc = zb_url_escape(file_id);
    const char *url_pattern = "%s/deletefile?api_key=%s&file_id=%s";
    int url_path_len = snprintf(NULL, 0, url_pattern, base_url, api_key_esc, file_id_esc);
    char *url_path = malloc((size_t)url_path_len + 1);
    snprintf(url_path, (size_t)url_path_len + 1, url_pattern, base_url, api_key_esc, file_id_esc);
    free(api_key_esc);
    free(file_id_esc);

    memory response_data = {0};
    long http_code = 0;
    if (!zb_make_request(url_path, "GET", "Accept: application/json", &response_data, &http_code, error_callback)) {
        free(response_data.response);
        free(url_path);
        return;
    }
    if (http_code > 299) {
        if (error_callback) {
            error_callback(parse_error(response_data.response));
        }
    } else if (success_callback) {
        json_object *j_obj = json_tokener_parse(response_data.response);
        if (j_obj == NULL) {
            if (error_callback) {
                error_callback(parse_error("Failed to parse json string"));
            }
        } else {
            success_callback(zb_delete_file_response_from_json(j_obj));
            json_object_put(j_obj);
        }
    }
    free(response_data.response);
    free(url_path);
}

typedef struct {
    int domain_column;
    const EmailFinderSendFileOptions *options;
} EmailFinderMimeCtx;

static void p3_email_finder_mime_fields(curl_mime *multipart, void *ctx) {
    EmailFinderMimeCtx *c = (EmailFinderMimeCtx *)ctx;
    p3_mime_add_int(multipart, "domain_column", c->domain_column);
    if (c->options) {
        p3_mime_add_optional_bool(multipart, "has_header_row", c->options->has_header_row);
        if (c->options->first_name_column > 0) {
            p3_mime_add_int(multipart, "first_name_column", c->options->first_name_column);
        }
        if (c->options->last_name_column > 0) {
            p3_mime_add_int(multipart, "last_name_column", c->options->last_name_column);
        }
        if (c->options->middle_name_column > 0) {
            p3_mime_add_int(multipart, "middle_name_column", c->options->middle_name_column);
        }
        if (c->options->full_name_column > 0) {
            p3_mime_add_int(multipart, "full_name_column", c->options->full_name_column);
        }
    }
}

typedef struct {
    int domain_column;
    const DomainSearchSendFileOptions *options;
} DomainSearchMimeCtx;

static void p3_domain_search_mime_fields(curl_mime *multipart, void *ctx) {
    DomainSearchMimeCtx *c = (DomainSearchMimeCtx *)ctx;
    p3_mime_add_int(multipart, "domain_column", c->domain_column);
    if (c->options) {
        p3_mime_add_optional_bool(multipart, "has_header_row", c->options->has_header_row);
    }
}

typedef struct {
    int email_address_column;
} ListEvaluatorMimeCtx;

static void p3_list_evaluator_mime_fields(curl_mime *multipart, void *ctx) {
    ListEvaluatorMimeCtx *c = (ListEvaluatorMimeCtx *)ctx;
    p3_mime_add_int(multipart, "email_address_column", c->email_address_column);
}

void send_email_finder_file(
    ZeroBounce *zb,
    char *file_path,
    int domain_column,
    const EmailFinderSendFileOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    if (domain_column < 1) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: domain_column"));
        }
        return;
    }
    char url[512];
    snprintf(url, sizeof(url), "%s/email-finder/sendfile", zb->bulk_api_root_url);
    EmailFinderMimeCtx ctx = {domain_column, options};
    p3_bulk_root_multipart(
        zb, url, file_path, p3_email_finder_mime_fields, &ctx, success_callback, error_callback
    );
}

void email_finder_file_status(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
) {
    char base[256];
    snprintf(base, sizeof(base), "%s/email-finder", zb->bulk_api_root_url);
    p3_bulk_root_file_status(zb, base, file_id, success_callback, error_callback);
}

void email_finder_get_file(
    ZeroBounce *zb,
    char *file_id,
    char *local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    char base[256];
    snprintf(base, sizeof(base), "%s/email-finder", zb->bulk_api_root_url);
    p3_bulk_root_get_file(zb, base, file_id, local_download_path, success_callback, error_callback);
}

void email_finder_delete_file(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
) {
    char base[256];
    snprintf(base, sizeof(base), "%s/email-finder", zb->bulk_api_root_url);
    p3_bulk_root_delete_file(zb, base, file_id, success_callback, error_callback);
}

void send_domain_search_file(
    ZeroBounce *zb,
    char *file_path,
    int domain_column,
    const DomainSearchSendFileOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    if (domain_column < 1) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: domain_column"));
        }
        return;
    }
    char url[512];
    snprintf(url, sizeof(url), "%s/domain-search/sendfile", zb->bulk_api_root_url);
    DomainSearchMimeCtx ctx = {domain_column, options};
    p3_bulk_root_multipart(
        zb, url, file_path, p3_domain_search_mime_fields, &ctx, success_callback, error_callback
    );
}

void domain_search_file_status(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackFileStatus success_callback,
    OnErrorCallback error_callback
) {
    char base[256];
    snprintf(base, sizeof(base), "%s/domain-search", zb->bulk_api_root_url);
    p3_bulk_root_file_status(zb, base, file_id, success_callback, error_callback);
}

void domain_search_get_file(
    ZeroBounce *zb,
    char *file_id,
    char *local_download_path,
    OnSuccessCallbackGetFile success_callback,
    OnErrorCallback error_callback
) {
    char base[256];
    snprintf(base, sizeof(base), "%s/domain-search", zb->bulk_api_root_url);
    p3_bulk_root_get_file(zb, base, file_id, local_download_path, success_callback, error_callback);
}

void domain_search_delete_file(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackDeleteFile success_callback,
    OnErrorCallback error_callback
) {
    char base[256];
    snprintf(base, sizeof(base), "%s/domain-search", zb->bulk_api_root_url);
    p3_bulk_root_delete_file(zb, base, file_id, success_callback, error_callback);
}

void send_list_evaluator_file(
    ZeroBounce *zb,
    char *file_path,
    int email_address_column,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    if (email_address_column < 1) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: email_address_column"));
        }
        return;
    }
    char url[512];
    snprintf(url, sizeof(url), "%s/listevaluator/", zb->bulk_api_base_url);
    ListEvaluatorMimeCtx ctx = {email_address_column};
    p3_bulk_root_multipart(
        zb, url, file_path, p3_list_evaluator_mime_fields, &ctx, success_callback, error_callback
    );
}

void get_list_evaluator_result(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    char *api_key_esc = zb_url_escape(zb->api_key);
    char *file_id_esc = zb_url_escape(file_id);
    const char *url_pattern = "%s/listevaluator/%s/?api_key=%s";
    int url_path_len = snprintf(NULL, 0, url_pattern, zb->bulk_api_base_url, file_id_esc, api_key_esc);
    char *url_path = malloc((size_t)url_path_len + 1);
    snprintf(url_path, (size_t)url_path_len + 1, url_pattern, zb->bulk_api_base_url, file_id_esc, api_key_esc);
    free(api_key_esc);
    free(file_id_esc);
    p3_raw_json_get(zb, url_path, success_callback, error_callback);
}

void send_evaluated_file(
    ZeroBounce *zb,
    char *file_path,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    char url[512];
    snprintf(url, sizeof(url), "%s/send-evaluated-file/", zb->bulk_api_base_url);
    p3_bulk_root_multipart(zb, url, file_path, NULL, NULL, success_callback, error_callback);
}

void add_filter(
    ZeroBounce *zb,
    char *rule,
    char *target,
    char *value,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    char *body = NULL;
    size_t cap = 0;
    p3_append_form_field(&body, &cap, "api_key", zb->api_key);
    p3_append_form_field(&body, &cap, "rule", rule ? rule : "");
    p3_append_form_field(&body, &cap, "target", target ? target : "");
    p3_append_form_field(&body, &cap, "value", value ? value : "");
    char url[512];
    snprintf(url, sizeof(url), "%s/filters/add", zb->api_base_url);
    p3_raw_json_form_post(zb, strdup(url), body, success_callback, error_callback);
}

void list_filters(
    ZeroBounce *zb,
    bool use_post,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    if (use_post) {
        char *body = NULL;
        size_t cap = 0;
        p3_append_form_field(&body, &cap, "api_key", zb->api_key);
        char url[512];
        snprintf(url, sizeof(url), "%s/filters/list", zb->api_base_url);
        p3_raw_json_form_post(zb, strdup(url), body, success_callback, error_callback);
    } else {
        char *api_key_esc = zb_url_escape(zb->api_key);
        const char *url_pattern = "%s/filters/list?api_key=%s";
        int url_path_len = snprintf(NULL, 0, url_pattern, zb->api_base_url, api_key_esc);
        char *url_path = malloc((size_t)url_path_len + 1);
        snprintf(url_path, (size_t)url_path_len + 1, url_pattern, zb->api_base_url, api_key_esc);
        free(api_key_esc);
        p3_raw_json_get(zb, url_path, success_callback, error_callback);
    }
}

void delete_filter(
    ZeroBounce *zb,
    char *rule,
    char *target,
    char *value,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    char *body = NULL;
    size_t cap = 0;
    p3_append_form_field(&body, &cap, "api_key", zb->api_key);
    p3_append_form_field(&body, &cap, "rule", rule ? rule : "");
    p3_append_form_field(&body, &cap, "target", target ? target : "");
    p3_append_form_field(&body, &cap, "value", value ? value : "");
    char url[512];
    snprintf(url, sizeof(url), "%s/filters/delete", zb->api_base_url);
    p3_raw_json_form_post(zb, strdup(url), body, success_callback, error_callback);
}

static char *p3_build_validate_s3_body(ZeroBounce *zb, const ValidateS3FileOptions *options) {
    char *body = NULL;
    size_t cap = 0;
    p3_append_form_field(&body, &cap, "file_name", options->file_name ? options->file_name : "");
    p3_append_optional_bool_form(&body, &cap, "has_header_row", options->has_header_row);
    p3_append_optional_bool_form(&body, &cap, "remove_duplicates", options->remove_duplicates);
    p3_append_optional_bool_form(&body, &cap, "remove_duplicate", options->remove_duplicate);
    if (options->email_address_column > 0) {
        char col[16];
        snprintf(col, sizeof(col), "%d", options->email_address_column);
        p3_append_form_field(&body, &cap, "email_address_column", col);
    }
    if (options->return_url && options->return_url[0]) {
        p3_append_form_field(&body, &cap, "return_url", options->return_url);
    }
    if (options->first_name_column > 0) {
        char col[16];
        snprintf(col, sizeof(col), "%d", options->first_name_column);
        p3_append_form_field(&body, &cap, "first_name_column", col);
    }
    if (options->last_name_column > 0) {
        char col[16];
        snprintf(col, sizeof(col), "%d", options->last_name_column);
        p3_append_form_field(&body, &cap, "last_name_column", col);
    }
    if (options->gender_column > 0) {
        char col[16];
        snprintf(col, sizeof(col), "%d", options->gender_column);
        p3_append_form_field(&body, &cap, "gender_column", col);
    }
    if (options->ip_address_column > 0) {
        char col[16];
        snprintf(col, sizeof(col), "%d", options->ip_address_column);
        p3_append_form_field(&body, &cap, "ip_address_column", col);
    }
    if (options->allow_phase_2_is_set) {
        p3_append_form_field(&body, &cap, "allow_phase_2", options->allow_phase_2 ? "true" : "false");
    }
    p3_append_form_field(&body, &cap, "api_key", zb->api_key);
    return body;
}

void validate_s3_file(
    ZeroBounce *zb,
    const ValidateS3FileOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    if (!options || !options->file_name || !options->file_name[0]) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: file_name"));
        }
        return;
    }
    char *body = p3_build_validate_s3_body(zb, options);
    char url[512];
    snprintf(url, sizeof(url), "%s/validateS3file", zb->bulk_api_root_url);
    p3_post_send_file_form(zb, url, body, success_callback, error_callback);
}

void score_s3_file(
    ZeroBounce *zb,
    const ScoreS3FileOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    if (!options || !options->file_name || !options->file_name[0]) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: file_name"));
        }
        return;
    }
    char *body = NULL;
    size_t cap = 0;
    p3_append_form_field(&body, &cap, "file_name", options->file_name);
    p3_append_form_field(&body, &cap, "api_key", zb->api_key);
    p3_append_optional_bool_form(&body, &cap, "has_header_row", options->has_header_row);
    p3_append_optional_bool_form(&body, &cap, "remove_duplicates", options->remove_duplicates);
    p3_append_optional_bool_form(&body, &cap, "remove_duplicate", options->remove_duplicate);
    if (options->email_address_column > 0) {
        char col[16];
        snprintf(col, sizeof(col), "%d", options->email_address_column);
        p3_append_form_field(&body, &cap, "email_address_column", col);
    }
    char url[512];
    snprintf(url, sizeof(url), "%s/scoreS3file", zb->bulk_api_root_url);
    p3_post_send_file_form(zb, url, body, success_callback, error_callback);
}

void list_s3_files(
    ZeroBounce *zb,
    const char *bucket,
    const char *search,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    char *api_key_esc = zb_url_escape(zb->api_key);
    char *bucket_esc = (bucket && bucket[0]) ? zb_url_escape(bucket) : NULL;
    char *search_esc = (search && search[0]) ? zb_url_escape(search) : NULL;
    if (!api_key_esc) {
        free(bucket_esc);
        free(search_esc);
        if (error_callback) {
            error_callback(parse_error("Memory allocation failed"));
        }
        return;
    }
    const char *url_pattern = "%s/listS3files?api_key=%s%s%s%s%s";
    const char *bucket_part = bucket_esc ? "&bucket=" : "";
    const char *search_part = search_esc ? "&search=" : "";
    int url_path_len = snprintf(
        NULL,
        0,
        url_pattern,
        zb->bulk_api_root_url,
        api_key_esc,
        bucket_part,
        bucket_esc ? bucket_esc : "",
        search_part,
        search_esc ? search_esc : ""
    );
    char *url_path = malloc((size_t)url_path_len + 1);
    if (!url_path) {
        free(api_key_esc);
        free(bucket_esc);
        free(search_esc);
        if (error_callback) {
            error_callback(parse_error("Memory allocation failed"));
        }
        return;
    }
    snprintf(
        url_path,
        (size_t)url_path_len + 1,
        url_pattern,
        zb->bulk_api_root_url,
        api_key_esc,
        bucket_part,
        bucket_esc ? bucket_esc : "",
        search_part,
        search_esc ? search_esc : ""
    );
    free(api_key_esc);
    free(bucket_esc);
    free(search_esc);
    p3_raw_json_get(zb, url_path, success_callback, error_callback);
}

void export_s3_file(
    ZeroBounce *zb,
    char *file_id,
    char *export_bucket,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    if (!file_id || !file_id[0]) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: file_id"));
        }
        return;
    }
    if (!export_bucket || !export_bucket[0]) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: export_bucket"));
        }
        return;
    }
    char *body = NULL;
    size_t cap = 0;
    p3_append_form_field(&body, &cap, "file_id", file_id);
    p3_append_form_field(&body, &cap, "export_bucket", export_bucket);
    p3_append_form_field(&body, &cap, "api_key", zb->api_key);
    char url[512];
    snprintf(url, sizeof(url), "%s/exportS3file", zb->bulk_api_root_url);
    p3_post_send_file_form(zb, url, body, success_callback, error_callback);
}
