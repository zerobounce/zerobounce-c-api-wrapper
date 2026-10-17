#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBValidateBatchResponse.h"
#include "ZeroBounce/utils.h"

ZBEmailToValidate new_zb_email_to_validate() {
    ZBEmailToValidate response;

    response.email_address = "";
    response.ip_address = "";

    return response;
}

EmailToValidateVector email_to_validate_vector_init() {
    EmailToValidateVector vector;
    vector.size = 0;
    vector.data = NULL;
    return vector;
}

void email_to_validate_vector_append(EmailToValidateVector* vector, const ZBEmailToValidate email) {
    if (!vector) return;

    vector->data = (ZBEmailToValidate*) realloc(vector->data, (vector->size + 1) * sizeof(ZBEmailToValidate));
    if (!vector->data) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    vector->data[vector->size].email_address = strdup(email.email_address);
    vector->data[vector->size].ip_address = strdup(email.ip_address);
    vector->size++;
}

void email_to_validate_vector_free(EmailToValidateVector* vector) {
    if (!vector) return;

    for (size_t i = 0; i < vector->size; i++) {
        free(vector->data[i].email_address);
        free(vector->data[i].ip_address);
    }
    free(vector->data);
    vector->data = NULL;
    vector->size = 0;
}

ValidateErrorVector validate_error_vector_init() {
    ValidateErrorVector vector;
    vector.size = 0;
    vector.data = NULL;
    return vector;
}

void validate_error_vector_append(ValidateErrorVector* vector, const ZBValidateError error) {
    if (!vector) return;

    vector->data = (ZBValidateError*) realloc(vector->data, (vector->size + 1) * sizeof(ZBValidateError));
    if (!vector->data) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    vector->data[vector->size].error = strdup(error.error);
    vector->data[vector->size].email_address = strdup(error.email_address);
    vector->size++;
}

void validate_error_vector_free(ValidateErrorVector* vector) {
    if (!vector) return;

    for (size_t i = 0; i < vector->size; i++) {
        free(vector->data[i].error);
        free(vector->data[i].email_address);
    }
    free(vector->data);
    vector->data = NULL;
    vector->size = 0;
}

int compare_validate_error_vector(const ValidateErrorVector* vec1, const ValidateErrorVector* vec2) {
    if (vec1->size != vec2->size) {
        return 0;
    }

    for (size_t i = 0; i < vec1->size; i++) {
        if (strcmp(vec1->data[i].email_address, vec2->data[i].email_address) != 0 ||
            strcmp(vec1->data[i].error, vec2->data[i].error) != 0) {
            return 0;
        }
    }

    return 1;
}

ValidateResponseVector validate_response_vector_init() {
    ValidateResponseVector vector;
    vector.size = 0;
    vector.data = NULL;
    return vector;
}

void validate_response_vector_append(ValidateResponseVector* vector, const ZBValidateResponse response) {
    if (!vector) return;

    vector->data = (ZBValidateResponse*) realloc(vector->data, (vector->size + 1) * sizeof(ZBValidateResponse));
    if (!vector->data) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    vector->data[vector->size].address = strdup(response.address);
    vector->data[vector->size].status = response.status;
    vector->data[vector->size].sub_status = response.sub_status;
    vector->data[vector->size].account = strdup(response.account);
    vector->data[vector->size].domain = strdup(response.domain);
    vector->data[vector->size].did_you_mean = strdup(response.did_you_mean);
    vector->data[vector->size].domain_age_days = strdup(response.domain_age_days);
    vector->data[vector->size].free_email = response.free_email;
    vector->data[vector->size].mx_found = response.mx_found;
    vector->data[vector->size].catchall_domain = response.catchall_domain;
    vector->data[vector->size].catchall_domain_set = response.catchall_domain_set;
    vector->data[vector->size].mx_record = strdup(response.mx_record);
    vector->data[vector->size].smtp_provider = strdup(response.smtp_provider);
    vector->data[vector->size].first_name = strdup(response.first_name);
    vector->data[vector->size].last_name = strdup(response.last_name);
    vector->data[vector->size].gender = strdup(response.gender);
    vector->data[vector->size].city = strdup(response.city);
    vector->data[vector->size].region = strdup(response.region);
    vector->data[vector->size].zip_code = strdup(response.zip_code);
    vector->data[vector->size].country = strdup(response.country);
    vector->data[vector->size].processed_at = strdup(response.processed_at);
    vector->data[vector->size].error = strdup(response.error);
    vector->size++;
}

void validate_response_vector_free(ValidateResponseVector* vector) {
    if (!vector) return;

    for (size_t i = 0; i < vector->size; i++) {
        free(vector->data[i].address);
        free(vector->data[i].account);
        free(vector->data[i].domain);
        free(vector->data[i].did_you_mean);
        free(vector->data[i].domain_age_days);
        free(vector->data[i].mx_record);
        free(vector->data[i].smtp_provider);
        free(vector->data[i].first_name);
        free(vector->data[i].last_name);
        free(vector->data[i].gender);
        free(vector->data[i].city);
        free(vector->data[i].region);
        free(vector->data[i].zip_code);
        free(vector->data[i].country);
        free(vector->data[i].processed_at);
        free(vector->data[i].error);
    }
    free(vector->data);
    vector->data = NULL;
    vector->size = 0;
}

int compare_validate_response_vector(const ValidateResponseVector* vec1, const ValidateResponseVector* vec2) {
    if (vec1->size != vec2->size) {
        return 0;
    }

    for (size_t i = 0; i < vec1->size; i++) {
        if (compare_zb_validate_response(&vec1->data[i], &vec2->data[i]) != 1) {
            return 0;
        }
    }

    return 1;
}

ZBValidateError new_zb_validate_error() {
    ZBValidateError validate_error;

    validate_error.error = "";
    validate_error.email_address = "";

    return validate_error;
}

char* zb_validate_error_to_string(ZBValidateError* validate_error) {
    const char *serialization = "{error='%s', email_address='%s'}";

    int size = snprintf(
        NULL, 0, serialization,
        validate_error->error,
        validate_error->email_address
    );

    if (size < 0) {
        return NULL;
    }

    char* buffer = (char*) malloc(size + 1);
    if (!buffer) {
        return NULL;
    }

    snprintf(
        buffer, size + 1, serialization,
        validate_error->error,
        validate_error->email_address
    );
    return buffer;
}

ZBValidateError zb_validate_error_from_json(const json_object* j) {
    ZBValidateError validate_error = new_zb_validate_error();

    validate_error.error = *(char**)get_json_value(j, json_type_string, "error", &(char*){""});
    validate_error.email_address = *(char**)get_json_value(j, json_type_string, "email_address", &(char*){""});

    return validate_error;
}

ZBValidateBatchResponse new_zb_validate_batch_response() {
    ZBValidateBatchResponse response;

    response.email_batch = validate_response_vector_init();
    response.errors = validate_error_vector_init();

    return response;
}

char* zb_validate_batch_response_to_string(ZBValidateBatchResponse* response) {
    const char *serialization = "ZBValidateBatchResponse{email_batch=[%s], errors=[%s]}";

    StringVector email_batch_strings = string_vector_init();
    for(size_t i = 0; i < response->email_batch.size; i++) {
        string_vector_append(&email_batch_strings, zb_validate_response_to_string(&response->email_batch.data[i]));
    }

    StringVector errors_strings = string_vector_init();
    for(size_t i = 0; i < response->errors.size; i++) {
        string_vector_append(&errors_strings, zb_validate_error_to_string(&response->errors.data[i]));
    }

    char* concatenated_email_batch = concatenate_strings(&email_batch_strings, ", ");
    char* concatenated_errors = concatenate_strings(&errors_strings, ", ");

    int size = snprintf(
        NULL, 0, serialization,
        concatenated_email_batch,
        concatenated_errors
    );

    if (size < 0) {
        return NULL;
    }

    char* buffer = (char*) malloc(size + 1);
    if (!buffer) {
        return NULL;
    }

    snprintf(
        buffer, size + 1, serialization,
        concatenated_email_batch,
        concatenated_errors
    );

    string_vector_free(&email_batch_strings);
    string_vector_free(&errors_strings);

    return buffer;
}

ZBValidateBatchResponse zb_validate_batch_response_from_json(const json_object* j) {
    ZBValidateBatchResponse r = new_zb_validate_batch_response();

    json_object* j_email_batch = NULL;
    json_object* j_errors = NULL;

    if (json_object_object_get_ex(j, "email_batch", &j_email_batch) == false ||
        json_object_object_get_ex(j, "errors", &j_errors) == false) {
        return r;
    }

    size_t email_batch_size = json_object_is_type(j_email_batch, json_type_array)
        ? json_object_array_length(j_email_batch) : 0;
    size_t errors_size = json_object_is_type(j_errors, json_type_array)
        ? json_object_array_length(j_errors) : 0;

    for (size_t i = 0; i < email_batch_size; i++) {
        json_object* j_response = json_object_array_get_idx(j_email_batch, i);
        validate_response_vector_append(&r.email_batch, zb_validate_response_from_json(j_response));
    }

    for (size_t i = 0; i < errors_size; i++) {
        json_object* j_error = json_object_array_get_idx(j_errors, i);
        validate_error_vector_append(&r.errors, zb_validate_error_from_json(j_error));
    }

    return r;
}

int compare_zb_validate_batch_response(const ZBValidateBatchResponse* response1, const ZBValidateBatchResponse* response2) {
    return
        compare_validate_response_vector(&response1->email_batch, &response2->email_batch) &&
        compare_validate_error_vector(&response1->errors, &response2->errors);
}
