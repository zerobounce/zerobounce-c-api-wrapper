#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <json.h>

#include "ZeroBounce/ZeroBounce.h"
#include "ZeroBounce/zb_internal.h"

static bool p3_is_blank(const char *value);
static char *p3_escape_path_segment(const char *value);

typedef void (*p3_mime_fields_fn)(curl_mime *multipart, void *ctx);
/* The job-specific form fields an S3 or SFTP file job sends on top of the four
   shared ones. */
typedef void (*p3_extra_form_fields_fn)(char **body, size_t *cap, const void *ctx);

static void p3_bulk_root_file_job_ex(
    ZeroBounce *zb,
    const char *path,
    const char *file_name,
    int has_header_row,
    int remove_duplicates,
    const char *return_url,
    p3_extra_form_fields_fn add_extra_fields,
    const void *extra_ctx,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

static void p3_export_bulk_file(
    ZeroBounce *zb,
    const char *path,
    const char *destination_field,
    const char *file_id,
    const char *destination,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
);

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
    o.return_url = NULL;
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
        bool was_empty = (*body == NULL);
        char *n;
        while (new_cap < need) {
            new_cap *= 2;
        }
        n = realloc(*body, new_cap);
        if (!n) {
            free(ek);
            free(ev);
            return false;
        }
        /* realloc(NULL, n) is malloc: the block is uninitialized, so the
           strlen and strcat below would read and append past garbage, and
           that garbage would go out on the wire. */
        if (was_empty) {
            n[0] = '\0';
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
    const char *body = response_data->response ? response_data->response : "";
    if (http_code > 299 || zb_body_looks_like_html(body) || body[0] == '\0') {
        zb_report_body_error(body, http_code, error_callback);
        return;
    }
    if (body[0] == '{' || body[0] == '[') {
        json_object *parsed = json_tokener_parse(body);
        if (!parsed) {
            zb_report_body_error(body, http_code, error_callback);
            return;
        }
        json_object_put(parsed);
    } else {
        zb_report_body_error(body, http_code, error_callback);
        return;
    }
    if (success_callback) {
        success_callback(body);
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
    json_object *j_obj = NULL;
    if (!zb_read_success_json(response_data->response, http_code, &j_obj, error_callback)) {
        return;
    }
    if (success_callback) {
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
    zb_note_http_field(name, buf);
    curl_mimepart *part = curl_mime_addpart(multipart);
    curl_mime_name(part, name);
    curl_mime_data(part, buf, CURL_ZERO_TERMINATED);
}

static void p3_mime_add_optional_bool(curl_mime *multipart, const char *name, int tri_state) {
    const char *text;
    curl_mimepart *part;
    if (tri_state < 0) {
        return;
    }
    text = tri_state ? "true" : "false";
    zb_note_http_field(name, text);
    part = curl_mime_addpart(multipart);
    curl_mime_name(part, name);
    curl_mime_data(part, text, CURL_ZERO_TERMINATED);
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
    zb_note_http_request("POST", url_path, NULL);
    if (!zb_check_request_url(url_path, error_callback)) {
        goto cleanup;
    }
    zb_note_http_field("api_key", zb->api_key ? zb->api_key : "");
    zb_note_http_field("file", "<file>");
    if (add_fields) {
        CURL *note_curl = curl_easy_init();
        if (note_curl) {
            curl_mime *note_mime = curl_mime_init(note_curl);
            if (note_mime) {
                add_fields(note_mime, fields_ctx);
                curl_mime_free(note_mime);
            }
            curl_easy_cleanup(note_curl);
        }
    }
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

    zb_note_http_request("GET", url_path, NULL);
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

    const char *body = response_data.response ? response_data.response : "";
    if (http_code > 299 || zb_body_looks_like_html(body) || body[0] == '\0' ||
        zb_get_file_json_indicates_error(body)) {
        zb_report_body_error(body, http_code, error_callback);
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
    {
        json_object *j_obj = NULL;
        if (zb_read_success_json(response_data.response, http_code, &j_obj, error_callback) && success_callback) {
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
    {
        json_object *j_obj = NULL;
        if (zb_read_success_json(response_data.response, http_code, &j_obj, error_callback) && success_callback) {
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
    /* The endpoint has no name to search for without one of the two. */
    if (!options || (options->first_name_column < 1 && options->full_name_column < 1)) {
        if (error_callback) {
            error_callback(parse_error(
                "Empty parameter: first_name_column or full_name_column (one is required)"
            ));
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
    /* The list evaluator is a base URL of its own, the same one
       get_list_evaluator_result reads, so the override set with
       zero_bounce_set_list_evaluator_base_url moves both halves together. */
    snprintf(url, sizeof(url), "%s/", zb->list_evaluator_base_url);
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
    char *api_key_esc;
    char *file_id_esc;
    const char *url_pattern = "%s/%s/?api_key=%s";
    int url_path_len;
    char *url_path;

    if (p3_is_blank(file_id)) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: file_id"));
        }
        return;
    }

    /* The file id is one path segment, so a slash in it must stay encoded. */
    file_id_esc = p3_escape_path_segment(file_id);
    if (!file_id_esc) {
        if (error_callback) {
            error_callback(parse_error("Invalid parameter: file_id"));
        }
        return;
    }
    api_key_esc = zb_url_escape(zb->api_key);
    url_path_len = snprintf(NULL, 0, url_pattern, zb->list_evaluator_base_url, file_id_esc, api_key_esc);
    url_path = malloc((size_t)url_path_len + 1);
    snprintf(url_path, (size_t)url_path_len + 1, url_pattern, zb->list_evaluator_base_url, file_id_esc, api_key_esc);
    free(api_key_esc);
    free(file_id_esc);
    p3_raw_json_get(zb, url_path, success_callback, error_callback);
}

void send_evaluated_file(
    ZeroBounce *zb,
    char *file_id,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    char *body = NULL;
    size_t cap = 0;
    char url[512];

    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    if (p3_is_blank(file_id)) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: file_id"));
        }
        return;
    }

    /*
     * LIVE: send-evaluated-file takes the id of a finished evaluation as a
     * form field, not an upload. The trailing slash is part of the endpoint.
     */
    p3_append_form_field(&body, &cap, "api_key", zb->api_key);
    p3_append_form_field(&body, &cap, "file_id", file_id);
    snprintf(url, sizeof(url), "%s/send-evaluated-file/", zb->bulk_api_base_url);
    p3_post_send_file_form(zb, url, body, success_callback, error_callback);
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

static void p3_append_column_form(char **body, size_t *cap, const char *key, int column) {
    char col[16];
    if (column <= 0) {
        return;
    }
    snprintf(col, sizeof(col), "%d", column);
    p3_append_form_field(body, cap, key, col);
}

/* The columns validateS3file takes on top of the four fields every S3 and SFTP
   job sends. email_address_column is 1-based, -1 omits it, and 0 is sent. */
static void p3_validate_s3_extra_fields(char **body, size_t *cap, const void *ctx) {
    const ValidateS3FileOptions *options = (const ValidateS3FileOptions *)ctx;
    if (options->email_address_column >= 0) {
        char col[16];
        snprintf(col, sizeof(col), "%d", options->email_address_column);
        p3_append_form_field(body, cap, "email_address_column", col);
    }
    p3_append_column_form(body, cap, "first_name_column", options->first_name_column);
    p3_append_column_form(body, cap, "last_name_column", options->last_name_column);
    p3_append_column_form(body, cap, "gender_column", options->gender_column);
    p3_append_column_form(body, cap, "ip_address_column", options->ip_address_column);
}

static void p3_score_s3_extra_fields(char **body, size_t *cap, const void *ctx) {
    const ScoreS3FileOptions *options = (const ScoreS3FileOptions *)ctx;
    if (options->email_address_column >= 0) {
        char col[16];
        snprintf(col, sizeof(col), "%d", options->email_address_column);
        p3_append_form_field(body, cap, "email_address_column", col);
    }
}

void validate_s3_file(
    ZeroBounce *zb,
    const ValidateS3FileOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    ValidateS3FileOptions empty = new_validate_s3_file_options();
    const ValidateS3FileOptions *o = options ? options : &empty;
    /*
     * The same required-parameter rules as the other five jobs, so it goes
     * through the same helper. remove_duplicate and allow_phase_2 are not sent:
     * validateS3file documents remove_duplicate**s**, and allow_phase_2 is a
     * field of the multipart bulk sendfile.
     */
    p3_bulk_root_file_job_ex(
        zb, "/validateS3file", o->file_name, o->has_header_row, o->remove_duplicates,
        o->return_url, p3_validate_s3_extra_fields, o, success_callback, error_callback
    );
}

void score_s3_file(
    ZeroBounce *zb,
    const ScoreS3FileOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    ScoreS3FileOptions empty = new_score_s3_file_options();
    const ScoreS3FileOptions *o = options ? options : &empty;
    p3_bulk_root_file_job_ex(
        zb, "/scoreS3file", o->file_name, o->has_header_row, o->remove_duplicates,
        o->return_url, p3_score_s3_extra_fields, o, success_callback, error_callback
    );
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
    /* export_bucket is optional, as it is on the SFTP twin: left out, the
       endpoint uses the export bucket from your ZeroBounce settings. */
    p3_export_bulk_file(
        zb, "/exportS3file", "export_bucket", file_id, export_bucket,
        success_callback, error_callback
    );
}

/* ------------------------------------------------------------------ */
/* S3 email finder / domain search, SFTP/FTP bulk, Inbox Placement     */
/* Ported from the Python SDK (b0444f8 and 407f0a8).                    */
/* ------------------------------------------------------------------ */

void zero_bounce_set_list_evaluator_base_url(ZeroBounce *zb, const char *url) {
    if (zb && url) {
        zb->list_evaluator_base_url = url;
    }
}

void zero_bounce_set_inbox_placement_base_url(ZeroBounce *zb, const char *url) {
    if (zb && url) {
        zb->inbox_placement_base_url = url;
    }
}

ZBBulkFileJobOptions new_bulk_file_job_options(void) {
    ZBBulkFileJobOptions o;
    o.file_name = NULL;
    o.has_header_row = true;
    o.remove_duplicates = -1;
    o.return_url = NULL;
    return o;
}

ZBCreateInboxPlacementTestOptions new_create_inbox_placement_test_options(void) {
    ZBCreateInboxPlacementTestOptions o;
    o.test_type = -1;
    o.sender_email = NULL;
    o.webhook_url = NULL;
    o.provider_ids = NULL;
    o.provider_ids_count = 0;
    o.seed_ids = NULL;
    o.seed_ids_count = 0;
    o.providers = NULL;
    o.providers_count = 0;
    return o;
}

ZBListInboxPlacementTestsOptions new_list_inbox_placement_tests_options(void) {
    ZBListInboxPlacementTestsOptions o;
    o.status = NULL;
    o.page = -1;
    o.page_size = -1;
    o.created_after = NULL;
    o.created_before = NULL;
    return o;
}

static bool p3_is_blank(const char *value) {
    const char *p;
    if (!value) {
        return true;
    }
    for (p = value; *p; p++) {
        if (!isspace((unsigned char)*p)) {
            return false;
        }
    }
    return true;
}

/*
 * POSTs one of the eight documented S3 / SFTP file jobs on the bulkapi root.
 *
 * Every one of them sends the same four fields, so they all come through here.
 * Booleans go on the wire as the strings "true"/"false", an option the caller
 * left unset is not sent at all, and add_extra_fields adds whatever one job
 * takes on top of the four (the validateS3file and scoreS3file columns).
 *
 * LIVE: file_name and return_url are required on every one of these jobs,
 * validateSFTPfile included, although the docs call its file_name optional.
 * Without either the API answers "Empty parameter: file_name" /
 * "Provided return URL is not permitted: None", so both are checked before
 * sending.
 */
static void p3_bulk_root_file_job_ex(
    ZeroBounce *zb,
    const char *path,
    const char *file_name,
    int has_header_row,
    int remove_duplicates,
    const char *return_url,
    p3_extra_form_fields_fn add_extra_fields,
    const void *extra_ctx,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    char *body = NULL;
    size_t cap = 0;
    char url[512];

    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    if (p3_is_blank(file_name)) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: file_name"));
        }
        return;
    }
    if (p3_is_blank(return_url)) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: return_url"));
        }
        return;
    }

    p3_append_form_field(&body, &cap, "api_key", zb->api_key);
    p3_append_optional_bool_form(&body, &cap, "has_header_row", has_header_row);
    p3_append_form_field(&body, &cap, "file_name", file_name);
    p3_append_optional_bool_form(&body, &cap, "remove_duplicates", remove_duplicates);
    p3_append_form_field(&body, &cap, "return_url", return_url);
    if (add_extra_fields) {
        add_extra_fields(&body, &cap, extra_ctx);
    }

    snprintf(url, sizeof(url), "%s%s", zb->bulk_api_root_url, path);
    p3_post_send_file_form(zb, url, body, success_callback, error_callback);
}

static void p3_bulk_root_file_job(
    ZeroBounce *zb,
    const char *path,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    ZBBulkFileJobOptions empty = new_bulk_file_job_options();
    const ZBBulkFileJobOptions *o = options ? options : &empty;
    /* has_header_row is a plain bool here, so it is always sent. */
    p3_bulk_root_file_job_ex(
        zb, path, o->file_name, o->has_header_row ? 1 : 0, o->remove_duplicates,
        o->return_url, NULL, NULL, success_callback, error_callback
    );
}

static void p3_export_bulk_file(
    ZeroBounce *zb,
    const char *path,
    const char *destination_field,
    const char *file_id,
    const char *destination,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    char *body = NULL;
    size_t cap = 0;
    char url[512];

    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }
    if (p3_is_blank(file_id)) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: file_id"));
        }
        return;
    }
    if (destination && p3_is_blank(destination)) {
        if (error_callback) {
            char message[64];
            snprintf(message, sizeof(message), "Empty parameter: %s", destination_field);
            error_callback(parse_error(message));
        }
        return;
    }

    p3_append_form_field(&body, &cap, "api_key", zb->api_key);
    p3_append_form_field(&body, &cap, "file_id", file_id);
    if (destination) {
        p3_append_form_field(&body, &cap, destination_field, destination);
    }

    snprintf(url, sizeof(url), "%s%s", zb->bulk_api_root_url, path);
    p3_post_send_file_form(zb, url, body, success_callback, error_callback);
}

void email_finder_s3_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    p3_bulk_root_file_job(zb, "/emailFinderS3file", options, success_callback, error_callback);
}

void domain_search_s3_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    p3_bulk_root_file_job(zb, "/domainSearchS3file", options, success_callback, error_callback);
}

void validate_sftp_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    /* The one job whose file_name the docs make optional. */
    p3_bulk_root_file_job(zb, "/validateSFTPfile", options, success_callback, error_callback);
}

void score_sftp_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    p3_bulk_root_file_job(zb, "/scoreSFTPfile", options, success_callback, error_callback);
}

void email_finder_sftp_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    p3_bulk_root_file_job(zb, "/emailFinderSFTPfile", options, success_callback, error_callback);
}

void domain_search_sftp_file(
    ZeroBounce *zb,
    const ZBBulkFileJobOptions *options,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    p3_bulk_root_file_job(zb, "/domainSearchSFTPfile", options, success_callback, error_callback);
}

void export_sftp_file(
    ZeroBounce *zb,
    const char *file_id,
    const char *export_path,
    OnSuccessCallbackSendFile success_callback,
    OnErrorCallback error_callback
) {
    p3_export_bulk_file(
        zb, "/exportSFTPfile", "export_path", file_id, export_path,
        success_callback, error_callback
    );
}

/*
 * One Inbox Placement Tester call.
 *
 * Unlike the rest of the SDK this one authenticates with an Authorization:
 * Bearer header, sends camelCase JSON bodies, and uses PUT and DELETE as well
 * as GET and POST, so it builds the request itself.
 *
 * LIVE: sent as an api_key query parameter the request is rejected as
 * unauthorized, and the key would end up in whatever logs the query string.
 */
static void p3_inbox_placement_request(
    ZeroBounce *zb,
    const char *method,
    const char *path,
    const char *extra_query,
    const char *body,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    char *url_path;
    char *auth_header;
    int url_len;
    int auth_len;
    const char *query = extra_query ? extra_query : "";
    memory response_data = {0};
    long http_code = 0;

    if (zero_bounce_invalid_api_key(zb, error_callback)) {
        return;
    }

    /* The caller builds the query with a leading '&'; without an api_key in
       front of it, the first separator has to become a '?'. */
    if (query[0] == '&') {
        query++;
    }
    url_len = snprintf(
        NULL, 0, "%s%s%s%s",
        zb->inbox_placement_base_url, path, query[0] ? "?" : "", query
    );
    url_path = malloc((size_t)url_len + 1);
    if (!url_path) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    snprintf(
        url_path, (size_t)url_len + 1, "%s%s%s%s",
        zb->inbox_placement_base_url, path, query[0] ? "?" : "", query
    );

    auth_len = snprintf(NULL, 0, "Authorization: Bearer %s", zb->api_key);
    auth_header = malloc((size_t)auth_len + 1);
    if (!auth_header) {
        free(url_path);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    snprintf(auth_header, (size_t)auth_len + 1, "Authorization: Bearer %s", zb->api_key);

    if (!zb_make_request_with_extra_header(
            url_path, method, body, "Content-Type: application/json", auth_header,
            &response_data, &http_code, error_callback)) {
        free(auth_header);
        free(response_data.response);
        free(url_path);
        return;
    }

    /* DELETE answers with an empty body. */
    if (http_code <= 299 && (!response_data.response || response_data.response[0] == '\0')) {
        if (success_callback) {
            success_callback("{}");
        }
    } else {
        p3_send_json_success(&response_data, http_code, success_callback, error_callback);
    }
    free(auth_header);
    free(response_data.response);
    free(url_path);
}

/*
 * URL-encodes a value as a single path segment.
 *
 * zb_url_escape is a query encoder and turns a space into '+', which is a
 * literal plus inside a path, so each one becomes %20 here.
 */
static char *p3_escape_path_segment(const char *value) {
    char *escaped;
    /* Everything else, slashes included, is percent-encoded and stays inside
       the segment; "." and ".." would walk the request elsewhere. */
    if (value && (strcmp(value, ".") == 0 || strcmp(value, "..") == 0)) {
        return NULL;
    }
    escaped = zb_url_escape(value);
    size_t len = strlen(escaped);
    char *out = malloc(len * 3 + 1);
    size_t i;
    size_t j = 0;

    if (!out) {
        free(escaped);
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < len; i++) {
        if (escaped[i] == '+') {
            out[j++] = '%';
            out[j++] = '2';
            out[j++] = '0';
        } else {
            out[j++] = escaped[i];
        }
    }
    out[j] = '\0';
    free(escaped);
    return out;
}

static char *p3_inbox_placement_test_segment(const char *test_id, OnErrorCallback error_callback) {
    char *segment;
    if (p3_is_blank(test_id)) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: test_id"));
        }
        return NULL;
    }
    segment = p3_escape_path_segment(test_id);
    if (!segment && error_callback) {
        error_callback(parse_error("Invalid parameter: test_id"));
    }
    return segment;
}

static void p3_str_append(char **body, size_t *cap, const char *fragment) {
    size_t needed = (*body ? strlen(*body) : 0) + strlen(fragment) + 1;
    if (needed > *cap) {
        size_t new_cap = needed * 2;
        char *grown = realloc(*body, new_cap);
        if (!grown) {
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
        if (!*body) {
            grown[0] = '\0';
        }
        *body = grown;
        *cap = new_cap;
    }
    strcat(*body, fragment);
}

/*
 * Escapes a value for use inside a JSON string.
 *
 * The Inbox Placement Tester bodies used to be concatenated into a fixed
 * buffer with no escaping at all, so a quote or a backslash in a test name, a
 * sender address or a webhook URL ended the string early: the body was either
 * rejected as malformed or carried fields the caller never asked for, and
 * anything longer than the buffer was silently cut off. Every value goes
 * through here now, and the buffers grow to fit.
 */
static char *p3_json_escape(const char *value) {
    const char *in = value ? value : "";
    size_t len = strlen(in);
    /* Worst case is \u00XX: six bytes out for one byte in. */
    char *out = malloc(len * 6 + 1);
    size_t i;
    size_t j = 0;

    if (!out) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)in[i];
        switch (c) {
            case '"':  out[j++] = '\\'; out[j++] = '"';  break;
            case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
            case '\b': out[j++] = '\\'; out[j++] = 'b';  break;
            case '\f': out[j++] = '\\'; out[j++] = 'f';  break;
            case '\n': out[j++] = '\\'; out[j++] = 'n';  break;
            case '\r': out[j++] = '\\'; out[j++] = 'r';  break;
            case '\t': out[j++] = '\\'; out[j++] = 't';  break;
            default:
                if (c < 0x20) {
                    snprintf(out + j, 7, "\\u%04x", c);
                    j += 6;
                } else {
                    out[j++] = (char)c;
                }
                break;
        }
    }
    out[j] = '\0';
    return out;
}

/*
 * One "&key=value" query filter, url-encoded and appended to a buffer that
 * grows to fit. p3_inbox_placement_request turns the leading '&' into the '?'.
 */
static void p3_query_append_string(char **query, size_t *cap, const char *key, const char *value) {
    char *escaped;
    if (!value || !value[0]) {
        return;
    }
    escaped = zb_url_escape(value);
    if (!escaped) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }
    p3_str_append(query, cap, "&");
    p3_str_append(query, cap, key);
    p3_str_append(query, cap, "=");
    p3_str_append(query, cap, escaped);
    free(escaped);
}

static void p3_query_append_int(char **query, size_t *cap, const char *key, int value) {
    char number[16];
    snprintf(number, sizeof(number), "%d", value);
    p3_str_append(query, cap, "&");
    p3_str_append(query, cap, key);
    p3_str_append(query, cap, "=");
    p3_str_append(query, cap, number);
}

/* A comma between two fields, but not after the opening brace. */
static void p3_json_append_separator(char **body, size_t *cap) {
    size_t len = *body ? strlen(*body) : 0;
    if (len > 0 && (*body)[len - 1] != '{') {
        p3_str_append(body, cap, ",");
    }
}

static void p3_json_append_string_field(char **body, size_t *cap, const char *key, const char *value) {
    char *escaped = p3_json_escape(value);
    p3_json_append_separator(body, cap);
    p3_str_append(body, cap, "\"");
    p3_str_append(body, cap, key);
    p3_str_append(body, cap, "\":\"");
    p3_str_append(body, cap, escaped);
    p3_str_append(body, cap, "\"");
    free(escaped);
}

static void p3_json_append_int_field(char **body, size_t *cap, const char *key, int value) {
    char number[16];
    snprintf(number, sizeof(number), "%d", value);
    p3_json_append_separator(body, cap);
    p3_str_append(body, cap, "\"");
    p3_str_append(body, cap, key);
    p3_str_append(body, cap, "\":");
    p3_str_append(body, cap, number);
}

static void p3_json_append_int_array_field(
    char **body,
    size_t *cap,
    const char *key,
    const int *values,
    size_t count
) {
    size_t i;
    if (!values || count == 0) {
        return;
    }
    p3_json_append_separator(body, cap);
    p3_str_append(body, cap, "\"");
    p3_str_append(body, cap, key);
    p3_str_append(body, cap, "\":[");
    for (i = 0; i < count; i++) {
        char number[16];
        snprintf(number, sizeof(number), "%s%d", i ? "," : "", values[i]);
        p3_str_append(body, cap, number);
    }
    p3_str_append(body, cap, "]");
}

/* providers is an array of objects: [{"id":3,"maxSeeds":10}, ...]. */
static void p3_json_append_providers_field(
    char **body,
    size_t *cap,
    const ZBInboxPlacementProvider *providers,
    size_t count
) {
    size_t i;
    if (!providers || count == 0) {
        return;
    }
    p3_json_append_separator(body, cap);
    p3_str_append(body, cap, "\"providers\":[");
    for (i = 0; i < count; i++) {
        char entry[64];
        if (providers[i].max_seeds >= 0) {
            snprintf(
                entry, sizeof(entry), "%s{\"id\":%d,\"maxSeeds\":%d}",
                i ? "," : "", providers[i].id, providers[i].max_seeds
            );
        } else {
            snprintf(entry, sizeof(entry), "%s{\"id\":%d}", i ? "," : "", providers[i].id);
        }
        p3_str_append(body, cap, entry);
    }
    p3_str_append(body, cap, "]");
}

void get_inbox_placement_providers(
    ZeroBounce *zb,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    p3_inbox_placement_request(zb, "GET", "/providers", NULL, NULL, success_callback, error_callback);
}

void get_inbox_placement_test_types(
    ZeroBounce *zb,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    p3_inbox_placement_request(zb, "GET", "/test-types", NULL, NULL, success_callback, error_callback);
}

#define ZB_INBOX_PLACEMENT_NAME_MAX 64

void create_inbox_placement_test(
    ZeroBounce *zb,
    const char *name,
    const ZBCreateInboxPlacementTestOptions *options,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    char *body = NULL;
    size_t cap = 0;

    if (p3_is_blank(name)) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: name"));
        }
        return;
    }
    if (strlen(name) > ZB_INBOX_PLACEMENT_NAME_MAX) {
        if (error_callback) {
            error_callback(parse_error("Invalid parameter: name (at most 64 characters)"));
        }
        return;
    }
    if (options && options->test_type >= 0 &&
        (options->test_type < 1 || options->test_type > 3)) {
        if (error_callback) {
            error_callback(parse_error("Invalid parameter: test_type (must be 1, 2 or 3)"));
        }
        return;
    }
    /* A seed-list test (type 1) has nowhere to send from without a sender. */
    if (options && options->test_type == 1 && p3_is_blank(options->sender_email)) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: sender_email (required when test_type is 1)"));
        }
        return;
    }

    /* camelCase JSON, with unset fields left out. */
    p3_str_append(&body, &cap, "{");
    p3_json_append_string_field(&body, &cap, "name", name);
    if (options) {
        if (options->test_type >= 0) {
            p3_json_append_int_field(&body, &cap, "testType", options->test_type);
        }
        if (options->sender_email) {
            p3_json_append_string_field(&body, &cap, "senderEmail", options->sender_email);
        }
        if (options->webhook_url) {
            p3_json_append_string_field(&body, &cap, "webhookUrl", options->webhook_url);
        }
        p3_json_append_int_array_field(
            &body, &cap, "providerIds", options->provider_ids, options->provider_ids_count
        );
        p3_json_append_int_array_field(
            &body, &cap, "seedIds", options->seed_ids, options->seed_ids_count
        );
        p3_json_append_providers_field(
            &body, &cap, options->providers, options->providers_count
        );
    }
    p3_str_append(&body, &cap, "}");

    p3_inbox_placement_request(zb, "POST", "/test", NULL, body, success_callback, error_callback);
    free(body);
}

void get_inbox_placement_test(
    ZeroBounce *zb,
    const char *test_id,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    char path[512];
    char *segment = p3_inbox_placement_test_segment(test_id, error_callback);
    if (!segment) {
        return;
    }
    snprintf(path, sizeof(path), "/test/%s", segment);
    free(segment);
    p3_inbox_placement_request(zb, "GET", path, NULL, NULL, success_callback, error_callback);
}

void list_inbox_placement_tests(
    ZeroBounce *zb,
    const ZBListInboxPlacementTestsOptions *options,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    char *query = NULL;
    size_t cap = 0;

    if (options && options->page_size >= 0 &&
        (options->page_size < 1 || options->page_size > 100)) {
        if (error_callback) {
            error_callback(parse_error("Invalid parameter: page_size (must be between 1 and 100)"));
        }
        return;
    }

    if (options) {
        p3_query_append_string(&query, &cap, "status", options->status);
        p3_query_append_string(&query, &cap, "createdAfter", options->created_after);
        p3_query_append_string(&query, &cap, "createdBefore", options->created_before);
        if (options->page >= 0) {
            p3_query_append_int(&query, &cap, "page", options->page);
        }
        if (options->page_size >= 0) {
            /*
             * LIVE: the filter is page_size. pageSize is accepted and then
             * silently ignored, so a caller asking for 10 rows quietly got the
             * default page.
             */
            p3_query_append_int(&query, &cap, "page_size", options->page_size);
        }
    }
    /*
     * LIVE: the listing is GET /tests (plural). /test is the single-test path
     * and answers 'Method "GET" not allowed'.
     */
    p3_inbox_placement_request(zb, "GET", "/tests", query, NULL, success_callback, error_callback);
    free(query);
}

void delete_inbox_placement_test(
    ZeroBounce *zb,
    const char *test_id,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    char path[512];
    char *segment = p3_inbox_placement_test_segment(test_id, error_callback);
    if (!segment) {
        return;
    }
    snprintf(path, sizeof(path), "/test/%s", segment);
    free(segment);
    p3_inbox_placement_request(zb, "DELETE", path, NULL, NULL, success_callback, error_callback);
}

void get_inbox_placement_webhook(
    ZeroBounce *zb,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    p3_inbox_placement_request(zb, "GET", "/webhook", NULL, NULL, success_callback, error_callback);
}

void set_inbox_placement_webhook(
    ZeroBounce *zb,
    const char *webhook_url,
    OnSuccessCallbackRawJson success_callback,
    OnErrorCallback error_callback
) {
    char *body = NULL;
    size_t cap = 0;
    if (p3_is_blank(webhook_url)) {
        if (error_callback) {
            error_callback(parse_error("Empty parameter: webhook_url"));
        }
        return;
    }
    p3_str_append(&body, &cap, "{");
    p3_json_append_string_field(&body, &cap, "webhookUrl", webhook_url);
    p3_str_append(&body, &cap, "}");
    p3_inbox_placement_request(zb, "PUT", "/webhook", NULL, body, success_callback, error_callback);
    free(body);
}
