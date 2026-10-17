#ifndef ZB_PRIORITY3_OPTIONS_H
#define ZB_PRIORITY3_OPTIONS_H

#include <stdbool.h>

/**
 * Optional columns for bulk email finder sendfile.
 * Column indexes start at 1. has_header_row: -1 omit, 1 sends "true".
 */
typedef struct {
    int first_name_column;
    int last_name_column;
    int middle_name_column;
    int full_name_column;
    int has_header_row;
} EmailFinderSendFileOptions;

typedef struct {
    int has_header_row;
} DomainSearchSendFileOptions;

typedef struct {
    const char *file_name;
    int has_header_row;
    int remove_duplicates;
    int remove_duplicate;
    int email_address_column;
    const char *return_url;
    int first_name_column;
    int last_name_column;
    int gender_column;
    int ip_address_column;
    bool allow_phase_2_is_set;
    bool allow_phase_2;
} ValidateS3FileOptions;

typedef struct {
    const char *file_name;
    int has_header_row;
    int remove_duplicates;
    int remove_duplicate;
    int email_address_column;
} ScoreS3FileOptions;

EmailFinderSendFileOptions new_email_finder_send_file_options(void);
DomainSearchSendFileOptions new_domain_search_send_file_options(void);
ValidateS3FileOptions new_validate_s3_file_options(void);
ScoreS3FileOptions new_score_s3_file_options(void);

#endif
