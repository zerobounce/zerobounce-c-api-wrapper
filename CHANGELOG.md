# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `validate_email_with_options` and `validate_email_batch_with_options` take `ZBValidateOptions`: `timeout`, `activity_data`, `verify_plus`, and `use_post` (form-encoded `POST /validate`).
- `ZBValidateResponse` reads `active_in_days` and `active_first_seen`.
- `score_email` (`GET /v2/scoring`) and `score_email_post` (form-encoded `POST /v2/scoring`).
- Bulk email finder (`send_email_finder_file`, `email_finder_file_status`, `email_finder_get_file`, `email_finder_delete_file`).
- Bulk domain search (`send_domain_search_file`, `domain_search_file_status`, `domain_search_get_file`, `domain_search_delete_file`).
- List evaluator (`send_list_evaluator_file`, `get_list_evaluator_result`, `send_evaluated_file`).
- Filters (`add_filter`, `list_filters`, `delete_filter`).
- S3 bulk (`validate_s3_file`, `score_s3_file`, `export_s3_file`, `list_s3_files`, `email_finder_s3_file`, `domain_search_s3_file`).
- SFTP/FTP bulk (`validate_sftp_file`, `score_sftp_file`, `email_finder_sftp_file`, `domain_search_sftp_file`, `export_sftp_file`), sharing the new `ZBBulkFileJobOptions`. The server, path and protocol come from your ZeroBounce settings. `validate_sftp_file`'s `file_name` is optional; `export_sftp_file` takes `export_path` rather than the `export_bucket` `export_s3_file` sends.
- `ZBValidateOptions` accepts `domain_info`, `greylist_processing` and `callback_url`. `callback_url` needs `greylist_processing` set to 1 and is rejected without it, and when blank, before a request is sent.
- `ZBValidateResponse` reads `domain_website_exists`, `domain_registrant_company_name` and `message`. `message` is the API's `Message` key, returned instead of a result while a greylist retry is still running.
- Inbox Placement Tester on `https://bulkapi.zerobounce.net/api/ipt`: `get_inbox_placement_providers`, `get_inbox_placement_test_types`, `create_inbox_placement_test`, `get_inbox_placement_test`, `list_inbox_placement_tests`, `delete_inbox_placement_test`, `get_inbox_placement_webhook`, `set_inbox_placement_webhook`. It authenticates with an `Authorization: Bearer` header, bodies are camelCase JSON, and unset fields are left out.
- `zero_bounce_set_list_evaluator_base_url` and `zero_bounce_set_inbox_placement_base_url`.
- `zb_make_request_with_method_and_body` and `zb_make_request_with_extra_header`, for the Tester's PUT and DELETE and its Bearer header.
- `ZeroBounceWarmup`, a client for the [Warmup API](https://www.zerobounce.net/docs/warmup-api): a separate product with its own host, its own team-scoped key (`zero_bounce_warmup_initialize`, sent in an `API-Key` header) and all 26 documented endpoints. Every call hands the raw JSON body to its callback, or an empty string for the documented empty successes (HTTP 204 and an empty 200); `warmup_download_seeds` and `warmup_download_statistics` hand over CSV text and write it to a path when one is given.
- `ZBErrorResponse.status_code` carries the HTTP status the request came back with, so a caller can tell 429 from 500 without parsing the message. It is 0 when the error was raised before a response arrived.
- Safety rails: `zb_api_key_is_clean` rejects a key with surrounding whitespace or control characters (both initializers run it), `zb_check_request_url` refuses anything that is not `https://`, `p3_escape_path_segment` rejects a `.` or `..` id, redirects are never followed and only the https protocol is enabled, and `zb_redact_api_key` strips the key out of any message built from a URL. A regional base URL paired with the single global bulk host warns once.
- `get_file_streaming` and `scoring_get_file_streaming` write a result file straight to disk as it arrives instead of buffering it in memory first, for a download too large to hold. They are opt-in because a streamed body cannot be inspected as a whole: the first 4 KB are held back and checked, so a JSON or HTML error still reaches the error callback and no partial file is left behind.
- `live/live_api.c`, an opt-in live suite built only with `-DZEROBOUNCE_BUILD_LIVE_TESTS=ON`, so the default build and CI never compile it. Keys come from `ZEROBOUNCE_API_KEY` and `ZEROBOUNCE_WARMUP_API_KEY`; `ZEROBOUNCE_LIVE_SPEND=1` and `ZEROBOUNCE_LIVE_CHANGES=1` opt in to calls that spend credits or make reversible account changes.

### Changed

- Verified against the live API. Where it disagrees with the docs, the API wins and the call site says so:
  - `validate_email_batch_with_options` sends `timeout` as a JSON **string**. Sent as a number the API rejects the whole batch with the account-level "Invalid API Key or your account ran out of credits".
  - The Inbox Placement Tester authenticates with `Authorization: Bearer`, not the `api_key` query parameter the rest of the API takes, and its listing is `GET /tests` (plural) — `/test` answers `Method "GET" not allowed`.
  - Every S3/SFTP job requires both `file_name` and `return_url`, `validate_sftp_file` included, although the docs call its `file_name` optional. Both are checked before the request is sent.
  - **Breaking:** `send_evaluated_file`'s second argument is the id of a finished evaluation, not a path to upload. The live endpoint takes the file id as a form field.
- A `"success": false` field on an HTTP 200 now reaches the error callback instead of being returned as a result. `get_file` and `delete_file` report an unknown or deleted file id that way.
- The list evaluator uses its own base URL (`.../v2/listevaluator`) instead of building the path from the bulk base, and the file id is encoded as a single path segment. `send_evaluated_file` stays on the bulk base.

- API requests time out after 120 seconds.
- Query values, including the API key, email, and file id, are percent-encoded.
- JSON API responses are capped at 16 MB and bulk file downloads at 256 MB.

### Fixed

- Form-encoded request bodies no longer start with stray bytes. The builder grew its buffer with `realloc(NULL, n)`, which hands back uninitialized memory, and then appended to it without terminating it first, so whatever was in that block went out in front of the first field. Depending on what the allocator handed back, `add_filter`, `send_evaluated_file` and the S3/SFTP jobs could be answered with a Cloudflare block instead of a result.

- A 403 HTML "Access denied" page is returned as a short error. An HTML page with status 200 is an error as well, including `get_file`, `email_finder_get_file`, and `domain_search_get_file`, instead of a saved file.
- Empty and non-JSON bodies are a short error. A repeated `Error:` / `[Error]:` prefix is removed.
- `validate_email_batch` reports an account-level failure (HTTP 200, error for address `all`, such as an invalid API key or no credits) through the error callback. An error for one address stays on the successful result.
- Bulk email finder and domain search send `has_header_row` when it is set to false.
- `validate_s3_file` sends `email_address_column` when it is set to 0. `validate_s3_file`, `score_s3_file`, and `export_s3_file` reject an empty API key before a request is sent.
- `validate_email_batch` sends an empty IP address as JSON `null`.

### Security

- Query parameters are percent-encoded, so a value cannot rewrite the request URL.
- Error callbacks no longer include the raw HTML access-denied page or a multi-megabyte body.

## [2.1.3] - 2026-06-18

### Fixed

- `validate_email_batch` posts to `api.zerobounce.net/v2/validatebatch` instead of `bulkapi.zerobounce.net`.

## [2.1.2] - 2026-06-10

### Added

- `ZBValidateResponse.catchall_domain`, read from `catchall_domain`.

## [2.1.0] - 2026-04-05

### Added

- `get_file_with_options` and `scoring_get_file_with_options`, with `GetFileOptions.download_type` (`phase_1`, `phase_2`, `combined`) and `activity_data` (validation `get_file` only).
- `SendFileOptions.allow_phase_2` / `allow_phase_2_is_set`, sent as `allow_phase_2` on validation `send_file` only.
- `ZBFileStatusResponse.file_phase_2_status`.

### Fixed

- `get_file` calls the error callback when the body is a JSON error, including HTTP 200 with `"success": false`, instead of saving that body as the results file.

## [2.0.10] - 2026-03-03

### Changed

- License the project under MIT.

## [2.0.0] - 2026-03-03

### Added

- `find_email_by_domain_*`, `find_email_by_company_name_*`, `search_domain_by_domain`, and `search_domain_by_company_name`.
- `zero_bounce_initialize_with_base_url` to select the API base URL.
- `ZBValidateSubStatus` value `AcceptAll`.

### Changed

- The library builds as C17 and uses libcurl for HTTP.

### Deprecated

- `find_email`. Use the email-finder or domain-search functions instead.

## [1.1.0] - 2023-09-07

### Added

- Email finder response types and `find_email`.

## [1.0.0] - 2023-08-22

### Added

- Initial release: `get_credits`, `get_api_usage`, `validate_email`, `validate_email_batch`, bulk and AI-scoring file functions, and `get_activity_data`.

[Unreleased]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.1.4...HEAD
[2.1.3]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.1.2...v2.1.3
[2.1.2]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.1.0...v2.1.2
[2.1.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.10...v2.1.0
[2.0.10]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.0...v2.0.10
[2.0.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v1.1.0...v2.0.0
[1.1.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/releases/tag/v1.0.0
