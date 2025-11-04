#include <stdio.h>
#include <string.h>

#include "ZeroBounce/ZBDomainSearchResponse.h"
#include "ZeroBounce/utils.h"

// ZBDomainFormat methods

ZBDomainFormat zb_domain_format_init() {
    ZBDomainFormat domain_format;
    domain_format.format = NULL;
    domain_format.confidence = NULL;
    return domain_format;
}

char* zb_domain_format_to_string(ZBDomainFormat* domain_format) {
    const char* serialization = "ZBDomainFormat{format='%s',confidence='%s'}";
    int size = snprintf(
        NULL, 0, serialization,
        domain_format->format,
        domain_format->confidence
    );
    if (size < 0) {
        return NULL;
    }

    char* buffer = (char*) malloc(size + 1);
    if (!buffer) {
        return NULL;
    }

    snprintf(
        buffer,
        size + 1,
        serialization,
        domain_format->format,
        domain_format->confidence
    );
    return buffer;
}

ZBDomainFormat zb_domain_format_from_json(const json_object* j) {
    ZBDomainFormat domain_format = zb_domain_format_init();
    domain_format.format = *(char**)get_json_value(j, json_type_string, "format", &(char*){""});
    domain_format.confidence = *(char**)get_json_value(j, json_type_string, "confidence", &(char*){""});
    return domain_format;
}

void zb_domain_format_free(ZBDomainFormat* instance) {
    if (instance == NULL) return;
    free(instance->format);
    free(instance->confidence);
    instance->format = NULL;
    instance->confidence = NULL;
}

int zb_domain_format_compare(ZBDomainFormat* instance1, ZBDomainFormat* instance2) {
    return
        strcmp(instance1->format, instance2->format) == 0 &&
        strcmp(instance2->confidence, instance2->confidence) == 0;
}

// ZBDomainFormatVector methods

ZBDomainFormatVector zb_domain_format_vector_init() {
    ZBDomainFormatVector vector;
    vector.size = 0;
    vector.data = NULL;
    return vector;
}

void zb_domain_format_vector_append(ZBDomainFormatVector* vector, const ZBDomainFormat instance) {
    if (!vector) return;

    vector->data = (ZBDomainFormat*) realloc(vector->data, (vector->size + 1) * sizeof(ZBDomainFormat));
    if (!vector->data) {
        fprintf(stderr, "Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    vector->data[vector->size].format = strdup(instance.format);
    vector->data[vector->size].confidence = strdup(instance.confidence);
    vector->size++;
}

void zb_domain_format_vector_free(ZBDomainFormatVector* vector) {
    for (int i = 0; i < vector->size; i++) {
        zb_domain_format_free(vector->data+i);
    }
    free(vector->data);
    vector->data = NULL;
    vector->size = 0;
}

char* zb_domain_format_vector_to_string(ZBDomainFormatVector* vector){
    char* final_string;

    if (vector->size == 0) {
        return strdup("[]");
    }

    StringVector string_vector = string_vector_init();
    if (vector->size == 1) {
        char* item_serialization = zb_domain_format_to_string(vector->data + 0);
        if (item_serialization == NULL) {
            return NULL;
        }
        string_vector_append(&string_vector, "[");
        string_vector_append(&string_vector, item_serialization);
        string_vector_append(&string_vector, "]");

        final_string = concatenate_strings(&string_vector, "");
        string_vector_free(&string_vector);
        free(item_serialization);
        return final_string;
    }

    char* item_serialization; // used to free individual serializations
    item_serialization = zb_domain_format_to_string(vector->data + 0);

    string_vector_append(&string_vector, "[");
    string_vector_append(&string_vector, item_serialization);
    free(item_serialization);
    for (int i = 0; i < vector->size; i++) {
        item_serialization = zb_domain_format_to_string(vector->data + i);
        string_vector_append(&string_vector, ",");
        string_vector_append(&string_vector, item_serialization);
        free(item_serialization);
    }
    string_vector_append(&string_vector, "]");

    final_string = concatenate_strings(&string_vector, "");
    string_vector_free(&string_vector);
    return final_string;
}

ZBDomainFormatVector zb_domain_format_vector_from_json(const json_object* j) {
    if (json_object_get_type(j) != json_type_array) {
        fprintf(
            stderr,
            "Invalid json type found for domain format vector.\n"
            "Expected 'json_type_array', found '%s'.\n",
            json_type_to_name(json_object_get_type(j))
        );
        exit(EXIT_FAILURE);
    }

    size_t vector_size = json_object_array_length(j);
    ZBDomainFormatVector vector = zb_domain_format_vector_init();
    vector.data = malloc(vector_size * sizeof(ZBDomainFormat));
    vector.size = vector_size;
    for (int index = 0; index < vector_size; index ++) {
        vector.data[index] = zb_domain_format_from_json(
            json_object_array_get_idx(j, index)
        );
    }
    return vector;
}

int zb_domain_format_vector_compare(ZBDomainFormatVector* instance1, ZBDomainFormatVector* instance2) {
    if (instance1->size != instance2->size) {
        return 0;
    }
    for (int index = 0; index < instance1->size; index ++) {
        if (!zb_domain_format_compare(instance1->data + index, instance2->data + index)) {
            return 0;
        }
    }
    return 1;
}

// ZBDomainSearchResponse methods

ZBDomainSearchResponse zb_domain_search_response_init() {
    ZBDomainSearchResponse response;
    response.domain = NULL;
    response.company_name = NULL;
    response.format = NULL;
    response.confidence = NULL;
    response.did_you_mean = NULL;
    response.failure_reason = NULL;
    response.other_domain_formats = zb_domain_format_vector_init();
    return response;
}

char* zb_domain_search_response_to_string(ZBDomainSearchResponse* response) {
    char* domain_serialization;
    const char* serialization = "ZBDomainSearchResponse{"
        "domain=\"%s\", "
        "company_name=\"%s\", "
        "format=\"%s\", "
        "confidence=\"%s\", "
        "did_you_mean=\"%s\", "
        "failure_reason=\"%s\", "
        "other_domain_formats=%s}";

    domain_serialization = zb_domain_format_vector_to_string(
        &(response->other_domain_formats)
    );
    int size = snprintf(
        NULL,
        0,
        serialization,
        response->domain,
        response->company_name,
        response->format,
        response->confidence,
        response->did_you_mean,
        response->failure_reason,
        domain_serialization
    );
    if (size < 0) {
        return NULL;
    }

    char* buffer = (char*) malloc(size + 1);
    if (!buffer) {
        return NULL;
    }

    snprintf(
        buffer,
        size + 1,
        serialization,
        response->domain,
        response->company_name,
        response->format,
        response->confidence,
        response->did_you_mean,
        response->failure_reason,
        domain_serialization
    );

    free(domain_serialization);
    return buffer;
}

ZBDomainSearchResponse zb_domain_search_response_from_json(const json_object* j) {
    ZBDomainSearchResponse response = zb_domain_search_response_init();
    response.domain = *(char**)get_json_value(j, json_type_string, "domain", &(char*){""});
    response.company_name = *(char**)get_json_value(j, json_type_string, "company_name", &(char*){""});
    response.format = *(char**)get_json_value(j, json_type_string, "format", &(char*){""});
    response.confidence = *(char**)get_json_value(j, json_type_string, "confidence", &(char*){""});
    response.did_you_mean = *(char**)get_json_value(j, json_type_string, "did_you_mean", &(char*){""});
    response.failure_reason = *(char**)get_json_value(j, json_type_string, "failure_reason", &(char*){""});

    json_object * domain_formats_json = json_object_object_get(j, "other_domain_formats");
    response.other_domain_formats = zb_domain_format_vector_from_json(domain_formats_json);

    return response;
}

void zb_domain_search_response_free(ZBDomainSearchResponse* instance) {
    zb_domain_format_vector_free(&(instance->other_domain_formats));
    free(instance->domain);
    free(instance->company_name);
    free(instance->format);
    free(instance->confidence);
    free(instance->did_you_mean);
    free(instance->failure_reason);
    instance->domain = NULL;
    instance->company_name = NULL;
    instance->format = NULL;
    instance->confidence = NULL;
    instance->did_you_mean = NULL;
    instance->failure_reason = NULL;
}

int zb_domain_search_response_compare(ZBDomainSearchResponse* instance1, ZBDomainSearchResponse* instance2) {
    return
        strcmp(instance1->domain, instance2->domain) == 0 &&
        strcmp(instance1->company_name, instance2->company_name) == 0 &&
        strcmp(instance1->format, instance2->format) == 0 &&
        strcmp(instance1->confidence, instance2->confidence) == 0 &&
        strcmp(instance1->did_you_mean, instance2->did_you_mean) == 0 &&
        strcmp(instance1->failure_reason, instance2->failure_reason) == 0 &&
        zb_domain_format_vector_compare(
            &(instance1->other_domain_formats), &(instance2->other_domain_formats)
        );
}
