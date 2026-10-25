#ifndef ZB_PRIORITY3_OPTIONS_H
#define ZB_PRIORITY3_OPTIONS_H

#include <stdbool.h>

/**
 * Columns for bulk email finder sendfile.
 * Column indexes start at 1. has_header_row: -1 omit, 0 sends "false", 1 sends "true".
 *
 * One of first_name_column and full_name_column is required: the endpoint has
 * no name to search for without it.
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

/**
 * Options for validate_s3_file. file_name and return_url are both required:
 * without a return_url the API answers
 * "Provided return URL is not permitted: None".
 *
 * remove_duplicate, allow_phase_2 and allow_phase_2_is_set are no longer sent.
 * validateS3file documents remove_duplicate**s**, and allow_phase_2 belongs to
 * the multipart bulk sendfile, not to this job. They are kept so existing code
 * still compiles.
 */
typedef struct {
    const char *file_name;
    int has_header_row;
    int remove_duplicates;
    /** Deprecated: not sent. Use remove_duplicates. */
    int remove_duplicate;
    int email_address_column;
    /** Required. */
    const char *return_url;
    int first_name_column;
    int last_name_column;
    int gender_column;
    int ip_address_column;
    /** Deprecated: not sent. */
    bool allow_phase_2_is_set;
    /** Deprecated: not sent. */
    bool allow_phase_2;
} ValidateS3FileOptions;

/**
 * Options for score_s3_file. file_name and return_url are both required, as
 * they are for every other S3 and SFTP file job.
 */
typedef struct {
    const char *file_name;
    int has_header_row;
    int remove_duplicates;
    /** Deprecated: not sent. Use remove_duplicates. */
    int remove_duplicate;
    int email_address_column;
    /** Required. */
    const char *return_url;
} ScoreS3FileOptions;

EmailFinderSendFileOptions new_email_finder_send_file_options(void);
DomainSearchSendFileOptions new_domain_search_send_file_options(void);
ValidateS3FileOptions new_validate_s3_file_options(void);
ScoreS3FileOptions new_score_s3_file_options(void);

#endif
