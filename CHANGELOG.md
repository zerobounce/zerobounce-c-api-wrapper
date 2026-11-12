# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Every bulk download (`get_file`, `scoring_get_file`, their `_streaming` forms, `email_finder_get_file` and `domain_search_get_file`) saved an HTML page as the result file when its body did not look like HTML, for example a proxy's `<p>Access denied by proxy</p>` sent as `text/html`. A `text/html` content type (in any letter case) is now an error with the HTTP status, and nothing is written, as an HTML body already was.
- `get_file_streaming` and `scoring_get_file_streaming` deleted a file that was already at `local_download_path` when the download failed: a refused connection, a JSON error body, a non-2xx status or a body cut short removed the earlier file, although nothing had been written to it. `get_file` leaves it alone. The streaming download now writes to a temporary file next to `local_download_path` (`<path>.<pid>-<id>.part`) and moves it into place only when the whole file has arrived; a failed download removes only that temporary file, so a file already at the path is kept as it was, also when the connection closes part-way through the body.
- A Warmup CSV download (`warmup_download_seeds`, `warmup_download_statistics`) that could be written only in part (a full disk, a file size limit) reported `Could not write download_path` but left the truncated file behind. It is now removed, as the bulk downloads already did.
- `"success": false` on an HTTP 200 reached the success callback on every raw-JSON call: `add_filter`, `list_filters`, `delete_filter`, `get_list_evaluator_result`, `list_s3_files` and the Inbox Placement Tester calls. It now goes to the error callback with status 200, as on every other call.
- **Behaviour change:** the Inbox Placement Tester calls treated an empty 2xx body as a success and passed `"{}"` to the success callback, whatever the call. Only `delete_inbox_placement_test` answers with an empty body, so it alone still does; an empty 2xx from any other Inbox Placement call (providers, test types, create, get, list, the webhook calls) is now an error with its status (`Empty response from ZeroBounce (HTTP 200)`).
- `warmup_download_seeds` and `warmup_download_statistics` handed a non-file answer to the success callback (and wrote it to `download_path`) when it did not start with `{` or `[`: a JSON body after leading whitespace, any body labelled `application/json`, or a `text/html` page that does not look like HTML. They now report an error with the status, and write nothing, for an empty body, a JSON or HTML content type, or a JSON object or array.

### Security

- `warmup_test_smtp_configuration` masked the SMTP username and password only in their raw form, so an error body that quoted them URL-encoded (`Pa%2Fss%22w%25rd`) or JSON-escaped, as a server or proxy echoing the request body would (`Pa\/ss\"w%rd`), reached the error callback with the password readable. Every form is now replaced with `***`, as is the Warmup key.
- An error built from a response body could carry the API key. Only an `api_key=` parameter's value was masked, so a body quoting the key itself reached the error callback whole: `{"error": "Invalid API key <key>"}` or `{"success": false, "message": "... <key>"}` on an HTTP 200, a JSON or text 4xx quoting it, or the key in its URL-encoded form. The Warmup client masked only the SMTP credentials of `warmup_test_smtp_configuration`, so a Warmup error quoting the key came through too. `parse_error_with_status`, which builds every error a callback gets, now replaces the calling client's key, raw, URL-encoded and JSON-escaped (`"` and `\` escaped, `/` escaped or not, as a request body echoed back), with `REDACTED` on every call of both clients, besides any `api_key=` value. `zb_set_redaction_key` (internal) records the key per thread at the check every call runs first.

### Changed

- **Behaviour change:** `send_file` and `scoring_send_file` always sent `remove_duplicate` (`true` by default), although the API defaults it to true itself. `SendFileOptions.remove_duplicate` is now an `int`: `new_send_file_options` sets it to -1, which leaves the field out; 0 sends `false` and 1 sends `true`, so assigning `true` or `false` keeps compiling and is sent as before. Code that takes a `bool *` to the field must change, and a binary built against the old header must be rebuilt (the struct layout changed).
- `ZBWarmupManagedSending.timezone_offset` is now a `double` (it was an `int`), so fractional zones such as 5.5 (India) or 5.75 (Nepal) can be sent; -99 still omits it, and a value outside -11 to 14, or NaN, is rejected before anything is sent. A whole number is sent as before (`"timezone_offset": 3`). Assigning an integer, positional initialisers and `-99` keep compiling unchanged; code that prints the field with `%d` or takes an `int *` to it must change. The struct's size and field offsets are unchanged on 64-bit targets, but a binary built against the old header must be rebuilt (FetchContent always rebuilds).

### Added

- Transport tests: the default timeouts (120 s in total, 30 s to connect) read back from where every request takes them, and the exact multipart parts `send_file` and `scoring_send_file` send through the real libcurl transport (`api_key`, `file`, `email_address_column`, the optional columns, `return_url`, `has_header_row`, `remove_duplicate`, `allow_phase_2`), so renaming a part or sending one the caller did not set fails a test.
- Offline tests: every Warmup call answered with a 200 JSON error body quoting the key (`error`, `success: false`, `message`, `detail`), which the CSV downloads must turn into an error without the key; every documented `add_sending_account` field in one exact request body; fractional `timezone_offset` values (5.5, 3.0 and 5.75 accepted, -11.5 and 14.5 rejected); the CSV downloads rejecting a JSON body or content type, an HTML page by body or content type, and an empty body.

## [3.0.1] - 2026-11-11

### Fixed

- `get_file_streaming` and `scoring_get_file_streaming` saved a JSON error body as the downloaded file and called the success callback: `{"error": ...}`, `{"message": ...}`, `{"Message": ...}` and `{"Error": ...}` alike, with an `application/json` content type or without. Only `{"success": false, ...}` and HTML pages were caught. They now report it to the error callback with the HTTP status, as `get_file` does, and write nothing.
- `get_file_streaming` and `scoring_get_file_streaming` ignored write errors: a full disk or a file size limit left a truncated file and still called the success callback. They now stop the transfer, remove the partial file and report `Could not write local_download_path`.
- `get_file`, `scoring_get_file` and their `_with_options` forms saved a `{"Message": ...}` or `{"Error": ...}` body (as an invalid key answers) as the file when the content type was not `application/json`, and `email_finder_get_file` and `domain_search_get_file` saved it even with that content type. Every download now treats any JSON object body, whatever its keys or content type, as the API's error, carrying the HTTP status, as the Pascal and iOS SDKs do. `zb_get_file_json_indicates_error` and `zb_format_get_file_error_message` also recognise the `Message` and `Error` keys.
- When `get_file` and its siblings, `email_finder_get_file` or `domain_search_get_file` could write a file only in part, the error was reported but the truncated file was left behind. It is now removed.
- `send_file` and `scoring_send_file` formatted the column numbers it sends into a 10-byte buffer, so a value of 10 digits or more, or a negative `email_address_column_index` of 9 digits or more (`INT_MAX` needs 11 bytes, `INT_MIN` 12), wrote one or two bytes past it. The buffer now holds any `int`, and the value is sent whole. gcc 15 warned about it.

## [3.0.0] - 2026-11-12

### Breaking

- Re-initializing with a key that has surrounding whitespace or control characters no longer keeps the key set before it. `zero_bounce_initialize`, `zero_bounce_initialize_with_base_url` and `zero_bounce_warmup_initialize` still print the rejection to stderr; they now also drop the previous key, and every call reports `api_key has surrounding whitespace or control characters` to its error callback and sends nothing. 2.2.0 went on sending requests with the previous key. `ZeroBounce` and `ZeroBounceWarmup` gain a trailing `api_key_rejected` field for this; the fields before it keep their offsets.
- `send_file`, `scoring_send_file`, `email_finder_get_file` and `domain_search_get_file` refuse a base URL that is not `https://`, like every other call: the error callback gets `url must be an https:// URL` and nothing is sent. 2.2.0 sent these four over plain `http://`, API key included, when a setter was given such a URL.

### Changed

- The base URL setters (`zero_bounce_set_bulk_api_base_url`, `zero_bounce_set_bulk_api_root_url`, `zero_bounce_set_bulk_api_scoring_base_url`, `zero_bounce_set_list_evaluator_base_url`, `zero_bounce_set_inbox_placement_base_url`) keep their own copy of the URL, with trailing slashes stripped, instead of the caller's pointer, so the string passed in no longer has to outlive the client. The Warmup client drops trailing slashes from its base URL too. 2.2.0 built URLs such as `https://host/v2//sendfile`.
- `CURL_USE_SCHANNEL` (Windows), `CURL_ENABLE_NTLM` and `CURL_USE_LIBPSL` are cache defaults instead of `CACHE ... FORCE`, so a consumer can override them with `-D`. `CURL_ENABLE_SSL` and `CURL_WINDOWS_SSPI` on Windows are cache defaults as well, no longer plain variables. `BUILD_TESTING` is still forced off.
- The bundled libcurl no longer builds the `curl` command-line tool or the man pages (`BUILD_CURL_EXE`, `BUILD_LIBCURL_DOCS`, `BUILD_MISC_DOCS` and `ENABLE_CURL_MANUAL` default to `OFF`); the SDK only links the library.
- Every request path sets its timeouts in milliseconds (`CURLOPT_TIMEOUT_MS` 120000, `CURLOPT_CONNECTTIMEOUT_MS` 30000) through one shared function; the values are unchanged.

### Fixed

- `get_file`, `get_file_with_options`, `scoring_get_file`, `scoring_get_file_with_options`, `email_finder_get_file` and `domain_search_get_file` wrote no file when no success callback was passed. The file is now written either way, as `get_file_streaming` already did.
- `get_file` and its scoring and `_with_options` variants only printed `fopen: ...` to stderr when the download path could not be opened; the error callback now gets `Could not open local_download_path for writing`. A short write is reported too.
- A JSON error body from `get_file` on an HTTP 200 (an unknown file id) reported `status_code` 0; it now carries the HTTP status.
- `email_finder_get_file` and `domain_search_get_file` passed a pointer owned by libcurl to `free()` after a real download whose response had a `Content-Type` header, freeing memory they did not allocate: the process aborted on macOS and crashed with a segmentation fault on Linux. They keep their own copy now.
- A Warmup CSV download (`warmup_download_seeds`, `warmup_download_statistics`) that could not be written to `download_path` called the error callback and then the success callback; it now calls only the error callback.

## [2.2.0] - 2026-11-11

### Breaking

- Several public structs changed layout: `ZeroBounce`, `ZBValidateResponse` and `ZBGetApiUsageResponse` have fields inserted in the middle, and `ZBErrorResponse` and `memory` have a field appended (details under Changed). Objects built against 2.1.4 are not compatible with the 2.2.0 library, and a positional initializer of these structs puts values in the wrong fields.
- Behaviour a 2.1.4 caller may rely on: an invalid or drained API key, `"success": false` on an HTTP 200, and an account-level `validate_email_batch` error now reach the error callback (see Fixed); an HTML, empty or non-JSON body is an error, also from the getfile calls; requests time out after 120 seconds, redirects are not followed, base URLs must be `https://`, and JSON responses and file downloads are capped at 16 MB and 256 MB (`get_file_streaming` for larger files); both initializers reject a key with surrounding whitespace or control characters; `ai_agent_mailbox` maps to `AiAgentMailbox` instead of `None`.

#### Upgrading from 2.1.4

- Rebuild everything that includes the headers, and replace positional initializers of the structs above with designated ones (`.field = value`).
- CMake 3.21 or newer is required.
- macOS builds need OpenSSL 3 or newer (Homebrew's `openssl` is found automatically; pass `-DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"` for a keg-only formula). Linux builds need the OpenSSL development headers (for example `libssl-dev`). Windows still uses Schannel.
- A project that pulls this one in with `FetchContent` or `add_subdirectory` gets `BUILD_TESTING`, `CURL_USE_LIBPSL` and `CURL_ENABLE_NTLM` forced off in its own cache.

### Added

- `validate_email_with_options` and `validate_email_batch_with_options` take `ZBValidateOptions`: `timeout`, `activity_data`, `verify_plus`, and `use_post` (form-encoded `POST /validate`).
- `ZBValidateResponse` reads `active_in_days` and `active_first_seen`, and `zb_validate_response_to_string` prints them.
- `score_email` (`GET /v2/scoring`) and `score_email_post` (form-encoded `POST /v2/scoring`).
- Bulk email finder (`send_email_finder_file`, `email_finder_file_status`, `email_finder_get_file`, `email_finder_delete_file`). `send_email_finder_file` needs one of `first_name_column` and `full_name_column`, and rejects a call with neither before sending.
- Bulk domain search (`send_domain_search_file`, `domain_search_file_status`, `domain_search_get_file`, `domain_search_delete_file`).
- The bulk email finder and domain search options take `has_header_row` as a tri-state: `-1` omits it, `0` sends `false` and `1` sends `true`.
- List evaluator (`send_list_evaluator_file`, `get_list_evaluator_result`, `send_evaluated_file`). The upload and the result use the evaluator's own base URL (`.../v2/listevaluator`, set with `zero_bounce_set_list_evaluator_base_url`), and the file id is encoded as a single path segment. `send_evaluated_file` takes the id of a finished evaluation, not a path to upload: the live endpoint takes the file id as a form field. It is sent to the bulk base URL.
- Filters (`add_filter`, `list_filters`, `delete_filter`).
- S3 bulk (`validate_s3_file`, `score_s3_file`, `export_s3_file`, `list_s3_files`, `email_finder_s3_file`, `domain_search_s3_file`). `validate_s3_file` and `score_s3_file` require `file_name` and `return_url` like the other jobs, so `ScoreS3FileOptions` has a `return_url` field; `export_s3_file`'s `export_bucket` is optional, as `export_path` is on the SFTP twin. `ValidateS3FileOptions.remove_duplicate`, `allow_phase_2` and `allow_phase_2_is_set` are not sent: the endpoint documents `remove_duplicates`, and `allow_phase_2` belongs to the multipart bulk `send_file`. `ValidateS3FileOptions.email_address_column` is 1-based; `-1` omits it and `0` is sent as given. `validate_s3_file`, `score_s3_file` and `export_s3_file` reject an empty API key before a request is sent.
- SFTP/FTP bulk (`validate_sftp_file`, `score_sftp_file`, `email_finder_sftp_file`, `domain_search_sftp_file`, `export_sftp_file`), sharing the new `ZBBulkFileJobOptions`. The server, path and protocol come from your ZeroBounce settings. `export_sftp_file` takes `export_path` rather than the `export_bucket` `export_s3_file` sends, and both are optional.
- `ZBValidateOptions` accepts `domain_info`, `greylist_processing` and `callback_url`. `callback_url` needs `greylist_processing` set to 1 and is rejected without it, and when blank, before a request is sent.
- `ZBValidateSubStatus` has a new `AiAgentMailbox` value for the `ai_agent_mailbox` sub-status. It is appended after `AcceptAll`, so the existing values keep their numbers. In 2.1.4 `ai_agent_mailbox` mapped to `None`.
- `ZBValidateResponse` reads `domain_website_exists`, `domain_registrant_company_name` and `message`. `message` is the API's `Message` key, returned instead of a result while a greylist retry is still running.
- Inbox Placement Tester on `https://bulkapi.zerobounce.net/api/ipt`: `get_inbox_placement_providers`, `get_inbox_placement_test_types`, `create_inbox_placement_test`, `get_inbox_placement_test`, `list_inbox_placement_tests`, `delete_inbox_placement_test`, `get_inbox_placement_webhook`, `set_inbox_placement_webhook`. It authenticates with an `Authorization: Bearer` header, bodies are camelCase JSON, and unset fields are left out. Every value in a body is JSON-escaped and the body grows to fit, so a quote, a backslash or a long URL is sent intact.
- `ZBCreateInboxPlacementTestOptions` carries `provider_ids`, `seed_ids` and `providers` (a `ZBInboxPlacementProvider` array of `id` plus `max_seeds`), sent as `providerIds`, `seedIds` and `providers`. `create_inbox_placement_test` requires a `name` of at most 64 characters, a `test_type` of 1, 2 or 3, and a `sender_email` when `test_type` is 1.
- `ZBListInboxPlacementTestsOptions` carries the documented `created_after` and `created_before` filters, and `page_size` has to be between 1 and 100. It is sent as `page_size`: the live API also accepts `pageSize`, but ignores it and returns the default page.
- `zero_bounce_set_list_evaluator_base_url`, `zero_bounce_set_inbox_placement_base_url`, `zero_bounce_set_bulk_api_base_url`, `zero_bounce_set_bulk_api_root_url` and `zero_bounce_set_bulk_api_scoring_base_url`. The bulk hosts are not built from `api_base_url`. In 2.1.4 the only way to move them was to assign the public `bulk_api_base_url` and `bulk_api_scoring_base_url` members directly; the setters cover those and the new bulk root, list evaluator and Tester hosts, and they are what the new regional-host warning (see Safety rails) points to.
- `zb_make_request_with_method_and_body` and `zb_make_request_with_extra_header`, for the Tester's PUT and DELETE and its Bearer header.
- `ZeroBounceWarmup`, a client for the [Warmup API](https://www.zerobounce.net/docs/warmup-api): a separate product with its own host, its own team-scoped key (`zero_bounce_warmup_initialize`, sent in an `API-Key` header) and all 26 documented endpoints. Every call hands the raw JSON body to its callback, or an empty string for the documented empty successes (HTTP 204 and an empty 200); `warmup_download_seeds` and `warmup_download_statistics` hand over CSV text and write it to a path when one is given.
- `ZBErrorResponse.status_code` carries the HTTP status the request came back with, so a caller can tell 429 from 500 without parsing the message. It is 0 when the error was raised before a response arrived.
- Safety rails: `zb_api_key_is_clean` rejects a key with surrounding whitespace or control characters (both initializers run it), `zb_check_request_url` refuses anything that is not `https://`, `p3_escape_path_segment` rejects a `.` or `..` id, redirects are never followed and only the https protocol is enabled, and `zb_redact_api_key` strips the key out of any message built from a URL. A regional base URL paired with the single global bulk host warns once.
- `get_file_streaming` and `scoring_get_file_streaming` write a result file straight to disk as it arrives instead of buffering it in memory first, for a download too large to hold. They are opt-in because a streamed body cannot be inspected as a whole: the first 4 KB are held back and checked, so a JSON or HTML error still reaches the error callback and no partial file is left behind.
- `live/live_api.c`, an opt-in live suite built only with `-DZEROBOUNCE_BUILD_LIVE_TESTS=ON`, so the default build and CI never compile it. Keys come from `ZEROBOUNCE_API_KEY` and `ZEROBOUNCE_WARMUP_API_KEY`; `ZEROBOUNCE_LIVE_SPEND=1` and `ZEROBOUNCE_LIVE_CHANGES=1` opt in to calls that spend credits or make reversible account changes.

### Changed

- **Breaking (ABI and positional initializers):** several public structs changed layout, so code built against 2.1.4 has to be rebuilt, and a positional (non-designated) initializer for any of these binds values to the wrong fields:
  - `ZeroBounce`: `bulk_api_root_url`, `list_evaluator_base_url` and `inbox_placement_base_url` are inserted between `api_base_url` and `bulk_api_base_url`.
  - `ZBValidateResponse`: `active_in_days` and `active_first_seen` are inserted after `domain_age_days`; `domain_website_exists`, `domain_registrant_company_name` and `message` are appended.
  - `ZBGetApiUsageResponse`: `sub_status_leading_period_removed` and `sub_status_alias_address` are inserted before `start_date`.
  - `ZBErrorResponse`: `status_code` is appended.
  - `memory`: `max_bytes` is appended.
- The bundled libcurl (FetchContent) moves from `curl-8_0_1` to curl 8.23.0, pinned to the commit the `curl-8_23_0` tag points at (`3163ec065643ba20709f190111f42c3e1fe95273`). curl's [vulnerability table](https://curl.se/docs/vulnerabilities.html) lists 60 advisories that affect 8.0.1 and are fixed by 8.23.0: 1 High, 15 Medium and 44 Low, 3 of the Low ones in the curl command-line tool only. None is listed against 8.23.0.
- TLS backend: Schannel on Windows, as before, and OpenSSL on Linux and macOS. macOS already used OpenSSL in 2.1.4, since curl 8.0.1 defaults to it there; curl removed Secure Transport in 8.15.0, so there is no OS-native option on macOS. A macOS build needs OpenSSL 3 or newer: Homebrew's `openssl` is found on its own, and a keg-only one such as `openssl@3` is passed with `-DOPENSSL_ROOT_DIR` (see the README). On Apple, `USE_APPLE_SECTRUST` defaults to `ON`, so certificates are verified against the macOS trust store; it is a cache default, not forced.
- libcurl is built without optional libraries, so it no longer depends on what the build machine has installed: `CURL_ZLIB`, `CURL_BROTLI`, `CURL_ZSTD`, `USE_NGHTTP2`, `USE_LIBIDN2`, `CURL_USE_LIBSSH2`, `CURL_USE_LIBSSH` and `CURL_USE_GSSAPI` default to `OFF`. It is also built for HTTP and HTTPS only: `CURL_DISABLE_<protocol>` defaults to `ON` for DICT, FILE, FTP, GOPHER, IMAP, IPFS, LDAP, LDAPS, MQTT, POP3, RTSP, SMTP, TELNET, TFTP and WEBSOCKETS (2.1.4 built every protocol). The SDK only makes HTTPS requests and does not ask for compressed responses. 2.1.4's curl 8.0.1 linked zlib, libidn2, libssh2 and an LDAP library when it found them; brotli, zstd and nghttp2 were off by default there, so HTTP/2 was never enabled on purpose. These are cache defaults, not forced, so `-D` or a parent project can turn any of them back on.
- `CMakeLists.txt` forces new cache settings: `CURL_USE_LIBPSL OFF`, `CURL_ENABLE_NTLM OFF` and `BUILD_TESTING OFF`. These are `CACHE ... FORCE`, so they also apply to a parent project that pulls this one in with `FetchContent` or `add_subdirectory`; in particular `BUILD_TESTING` is turned off for that parent and its other dependencies. `CURL_ENABLE_NTLM OFF` replaces 2.1.4's forced `CURL_DISABLE_NTLM ON`, which curl 8.20 stopped reading when NTLM became opt-in.
- Verified against the live API. Where it disagrees with the docs, the API wins and the call site says so:
  - `validate_email_batch_with_options` sends `timeout` as a JSON **string**. Sent as a number the API rejects the whole batch with the account-level "Invalid API Key or your account ran out of credits".
  - The Inbox Placement Tester authenticates with `Authorization: Bearer`, not the `api_key` query parameter the rest of the API takes, and its listing is `GET /tests` (plural) — `/test` answers `Method "GET" not allowed`.
  - Every S3/SFTP job requires both `file_name` and `return_url`, `validate_s3_file`, `score_s3_file` and `validate_sftp_file` included, although the docs call several of them optional. `POST /validateS3file` without a `return_url` answers 400 `Provided return URL is not permitted: None`, so both are checked before the request is sent.
- A `"success": false` field on an HTTP 200 now reaches the error callback instead of being returned as a result. In 2.1.4 `delete_file`, `file_status`, `send_file` and their scoring twins handed such a body to the success callback; `delete_file` is answered that way for an unknown or deleted file id. `get_file` already reported it as an error.

- API requests time out after 120 seconds.
- Query values, including the API key, email, and file id, are percent-encoded.
- JSON API responses are capped at 16 MB and bulk file downloads at 256 MB.
- The build requires CMake 3.21 or newer. `CMakeLists.txt` declared 3.20, but `CMAKE_C_STANDARD 17` needs 3.21: CMake 3.20 stops at configure because it "does not know the compile flags to use" for C17.

### Fixed

- An invalid or drained API key was a silent success. The API answers one with an HTTP 200 and no `success` field: `{"error": "Invalid API key or your account ran out of credits"}` from `validate_email` and `get_activity_data`, `{"error": "Invalid API key"}` from `get_api_usage`, `{"Error": ...}` from the filters, and `{"Message": ...}` from the email finder and domain search. Each was parsed into an empty result, or for the filters handed over as raw JSON, and passed to the success callback. Each now goes to the error callback with the API's own message. `message`, `Message` and `detail` decide failure only for the email finder and domain search, because successful responses use them too: a bulk submit answers `{"success": true, "message": "File Accepted"}`, a filter change answers with a bare `Message`, and a repeated `greylist_processing` validate answers `{"Message": "Greylist validation still in progress. Please try again later."}`, which is a result. `get_credits` still returns `-1` for an invalid key, as documented.
- `get_api_usage` read `sub_status_anti_spam_system`, but the API sends that counter as `sub_status_antispam_system`, so it was always 0. The struct field keeps its name; only the key it reads is corrected.
- `get_api_usage` now reads `sub_status_leading_period_removed` and `sub_status_alias_address`, two counters the API returns that were silently discarded.

- A 403 HTML "Access denied" page is returned as a short error. An HTML page with status 200 is an error as well, including `get_file`, `email_finder_get_file`, and `domain_search_get_file`, instead of a saved file.
- Empty and non-JSON bodies are a short error. A repeated `Error:` / `[Error]:` prefix is removed.
- `validate_email_batch` reports an account-level failure (HTTP 200, error for address `all`, such as an invalid API key or no credits) through the error callback. An error for one address stays on the successful result.
- `validate_email_batch` sends an empty IP address as JSON `null`.
- `validate_email_batch` results carry `catchall_domain` and `catchall_domain_set`. 2.1.4 parsed both but did not copy them into the batch result, so they were left uninitialised.
- `send_file` and `scoring_send_file` no longer dereference a NULL `SendFileOptions.return_url`. A caller that zeroed the struct instead of calling `new_send_file_options` crashed on the request path.

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

[Unreleased]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v3.0.1...HEAD
[3.0.1]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v3.0.0...v3.0.1
[3.0.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.2.0...v3.0.0
[2.2.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.1.4...v2.2.0
[2.1.3]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.1.2...v2.1.3
[2.1.2]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.1.0...v2.1.2
[2.1.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.10...v2.1.0
[2.0.10]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.0...v2.0.10
[2.0.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v1.1.0...v2.0.0
[1.1.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/releases/tag/v1.0.0
