# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Upgrading

- **Source compatible, binary layout changed:** code written for 3.0.x keeps compiling, but `SendFileOptions` and `ZBWarmupManagedSending` each grew by members appended at the end, so a shared library (`.so`, `.dylib`, `.dll`) built against the 3.0.x headers must be rebuilt (a `FetchContent` build always is). Every public function that takes no arguments is now declared `(void)` instead of `()`: a call that passed an argument no longer compiles. The public header no longer declares the library's internal functions or `zero_bounce_instance`.
- **Behaviour change:** the Inbox Placement Tester calls no longer treat an empty 2xx body as a success, except `delete_inbox_placement_test`; see the first "Behaviour change" entry under Fixed below. Typed calls also treat a 200 body of the wrong shape as an error (see Fixed).
- A project that pulls this one in with `FetchContent` no longer fetches unity or fff (see Changed).
- The bundled dependency pins moved: json-c 0.17 to 0.19 (`json-c-0.19-20260627`) and Unity v2.5.2 to v2.7.0 (the test suite passes unchanged on both).

### Added

- `ZBWarmupManagedSending.timezone_offset_hours` (`double`) and `timezone_offset_hours_set` (`bool`), appended at the end of the struct: hours from UTC with fractional zones such as 5.5 (India) or 5.75 (Nepal). Sent as `timezone_offset` when `timezone_offset_hours_set` is `true`, in place of `timezone_offset` if both are set; a whole number is sent as an integer (`"timezone_offset": 3`), and a value outside -11.0 to 14.0, NaN or an infinity is rejected before anything is sent. `new_warmup_managed_sending` and zero initialisation leave it unset. The struct grew by two members, so a binary built against the old header must be rebuilt (FetchContent always rebuilds); source written for 3.0.x keeps compiling.
- Offline tests: code written for 3.0.x keeps compiling with `ZBWarmupManagedSending` (positional initialiser, `int *` to `timezone_offset`, `%d`) and sends the same body; every Warmup JSON call answering 200 with a non-JSON body (`text/plain`, `application/json`, `text/csv`) and every `ZeroBounce` call answering a JSON value followed by text; every spelling of a secret composed from raw, JSON-escaped (with and without `\/`) and URL-encoded (`%20` and `+`) forms, plus JSON-escaped twice, generated in the test rather than listed by hand.
- Transport tests: the default timeouts (120 s in total, 30 s to connect) read back from where every request takes them, and the exact multipart parts `send_file` and `scoring_send_file` send through the real libcurl transport (`api_key`, `file`, `email_address_column`, the optional columns, `return_url`, `has_header_row`, `remove_duplicate`, `allow_phase_2`), so renaming a part or sending one the caller did not set fails a test.
- Offline tests: a pre-existing file at the target survives a download whose write fails (a file size limit), on every download and both Warmup CSV downloads, with no partial file left beside it; `SendFileOptions` keeps compiling with code written for 3.0.1 (`bool *` to `remove_duplicate`, positional initialisers); a sweep over every call answering 200 `text/html` with `{"a":1}`, and 200 `application/json` with a CSV body; every encoded form of a secret containing a space, `"`, `/`, `+`, `%`, `&`, `=`, `<`, `>`, `'` and `\`, each needed by a test.
- Offline tests: every Warmup call answered with a 200 JSON error body quoting the key (`error`, `success: false`, `message`, `detail`), which the CSV downloads must turn into an error without the key; every documented `add_sending_account` field in one exact request body; fractional `timezone_offset_hours` values (5.5, 3.0 and 5.75 accepted; -11.5, 14.5, NaN and the infinities rejected); the CSV downloads rejecting a JSON body or content type, an HTML page by body or content type, and an empty body.
- Offline tests: every call of both clients against every body of the shared strict-JSON corpus (`test/fixtures/strict-json-corpus.tsv`, a copy of sdk-docs' `strict-json-corpus.tsv`); a seeded fuzz of up to three URL (`%20` and `+`) and JSON (with and without `\/`) encodings of the API key, the Warmup key and the SMTP credentials over every call, checking that no decoded view of any error holds the secret.
- Offline tests: every typed call against JSON of the wrong shape (`0`, `1.5`, `true`, `null`, `"x"`, `[]`, and arrays a decoder could read by position, such as `["x"]`, `["a@example.com",10]` and `[true,"3"]`); every value at every path of every documented example (nested objects, array items, `validate_email_batch` rows and their fields, `other_domain_formats` items, a `sendfile` `message` list item) replaced by each other JSON type, which must be an error with status 200 (`null` only for an optional field); 200000 nested `[` on every call and on Warmup (an error with status 200, not a crash); each decode step of the redaction check (lower-case `%xx`, an invalid UTF-8 run, `+`, `\uXXXX` and surrogate pairs, four URL layers) and a `%00` before a fully percent-encoded key, Warmup key and SMTP password, each needed by a test. Transport test: a 500 and a 200 over the size cap keep their status on every buffered request path.

### Changed

- Build and CI: json-c moves from 0.17 to 0.19 and Unity from v2.5.2 to v2.7.0 (tag and full commit SHA pinned), `github/codeql-action` to v4.39.3 and `advanced-security/filter-sarif` to v1.1, and the Dockerfile base `ubuntu:26.04` is pinned by digest (Dependabot's `docker` ecosystem keeps it current). The test image's default command runs `ZeroBounceTransportTests` after `ZeroBounceTests`, a CI job builds it and runs it with `--network=none`, and `.dockerignore` leaves out `build-live/` and `zb-sdk-live.csv`. The publish workflow checks the tag out by name in its test job, then verifies HEAD against the validated commit (CodeQL `actions/cache-poisoning` alert), and runs one publish at a time.
- `send_file` and `scoring_send_file` send `remove_duplicate` with the caller's value on every call, `true` by default (the API's default too), as 3.0.x did, so an explicit `false` always reaches the API. `SendFileOptions` has one more member than in 3.0.1, `remove_duplicate_set`, appended at the end of the struct; it is not needed and is ignored. The struct grew by one member, so a binary built against the 3.0.1 header must be rebuilt (FetchContent always rebuilds).
- A project that pulls this one in with `FetchContent` or `add_subdirectory` no longer fetches unity and fff or builds `ZeroBounceTests` and `ZeroBounceTransportTests`; the test targets exist only when this is the top-level project.
- Each base URL setter (`zero_bounce_warmup_set_base_url` and the `ZeroBounce` setters) copies the URL it is given. `zero_bounce_warmup_free` and the setters free only a copy the library made, and leave a `base_url` or `bulk_api_base_url` that the caller assigned itself alone. The list of those copies is locked, so two clients on two threads setting base URLs do not race.
- Every public function that takes no arguments is declared `(void)`. A call that passed an argument no longer compiles.
- The public header no longer declares the library's internal functions, or the process-wide `zero_bounce_instance`.
- The library links the platform thread library itself (`Threads::Threads`), so a static link does not depend on libcurl to pull in `pthread`.
- **Behaviour change:** the API key check also refuses U+034F, U+115F, U+1160, U+17B4, U+17B5, U+2800, U+3164, U+FFA0, U+FFF0-FFF8, U+110BD, U+110CD, U+13430-1343F, U+1BCA0-1BCA3 and U+E0000-E0FFF (the Rust SDK's list), and a key that is not well-formed UTF-8 (`C0 80`, `E0 80 A0`, `ED A0 80`, `FF`, truncated sequences). Before, these were accepted and sent; now the key is not accepted and each call reports "api_key has surrounding whitespace or control characters".
- New build option `ZB_SANITIZE` (ASan + UBSan on the SDK and its tests only).

### Deprecated

- `ZBWarmupManagedSending.timezone_offset` (`int`, whole hours only): use `timezone_offset_hours` with `timezone_offset_hours_set`. It keeps its type and behaviour (-99 omits it) and is ignored when `timezone_offset_hours_set` is `true`.
- `find_email` is now marked deprecated with a compiler attribute on the function (`__attribute__((deprecated))`, `__declspec(deprecated)` on MSVC) instead of a `#warning` in the header, so including the header does not warn; only a call to `find_email` can.

### Fixed

- A file job's `message` sent as a list of strings (`delete_file`, `scoring_delete_file`, `file_status`, `scoring_file_status`, `email_finder_file_status`, `domain_search_file_status`, `email_finder_delete_file`, `domain_search_delete_file`) was reported as an empty `message` in 3.0.1, the list silently dropped. Its strings are now joined with a newline into the `char *` `message`, as a one-string message is returned. A `message` of any other JSON type is an error with status 200.
- A `getfile` JSON error whose `message` is a list (`get_file`, `scoring_get_file` and their `_with_options` forms) reported only the first line of the list in 3.0.1. Every non-empty line is now included, joined with a newline.
- The email finder and domain search calls (`find_email_by_*`, `search_domain_by_*`) returned an empty result with status 200 when the body had only message keys (`message`, `Message`, `error`, `error_message`, `Error` or `detail`) and their value was `null`, blank or a list, such as `{"message":null}` or `{"message":["a","b"]}`. These are now errors with status 200, whatever the value, as a non-blank string value already was. A body with another key, or an empty object, is still a result.
- Every typed call took an HTTP 200 JSON object whose fields were missing or of the wrong JSON type as a result filled with defaults: `get_credits` reported `credits` `-1` (the dead-key value) for `{}`, `{"Credits":null}`, `{"Credits":true}` or `{"Credits":5}`, and `0` for `{"Credits":"abc"}`; `validate_email` reported a result for `{"address":5,"status":"valid"}` (empty address) or `{"address":"a@b.c","status":7}` (status 0); a wrong-typed field of any other model (`free_email: "maybe"`, a numeric `domain_age_days`, a non-boolean `catchall_domain`, a non-array `other_domain_formats` or `email_batch`, ...) was silently replaced by its default. Field types are now part of the response shape: such a body is an error with status 200 (`Unexpected JSON response: field "<name>" is missing or has the wrong type.`). `Credits` is an integer within the range of `int`, sent as a JSON string (`"-1"`, `"12345"`) or as a JSON integer (`{"Credits":2375323}`, zerobounce.net's own example, which 3.0.1 reported as `-1`); `"5.0"`, `"abc"`, `5.5`, `1e3` or `2147483648` is an error; a `score_email` `score` sent as a string must hold an integer too (3.0.1 read `"score":"abc"` as `0`); `validate_email` (and each `validate_email_batch` row) needs `address` and `status` as strings, except a greylist answer that carries its text in `Message` (`{"Message": "..."}`), which is still a result. `null` is still accepted for every optional field, and the documented example responses still parse. Warmup and the raw-JSON calls are unchanged.
- Offline tests: a `Credits` string one past the range of `int` (`"2147483648"`, `"-2147483649"`) is an error with status 200, a `"success": "false"` string (any letter case) is an error on the file status, delete, send file and raw-JSON filter calls, and `return_url` is left out of both sendfile calls when it is not set. The documented batch (three rows), get-api-usage (with `sub_status_leading_period_removed` and `sub_status_alias_address`) and file status (`file_phase_2_status` `"N/A"`) examples are parsed by their calls.
- Offline tests: the dead-key bodies `{"error": ...}`, `{"error_message": ...}` and `{"Error": ...}` on an HTTP 200 over every request function (the `send_file` family of uploads included); a `\uXXXX` escape for each UTF-8 sequence length (2, 3 and 4 bytes) in the redaction check, whose 2- and 3-byte branches had no test.
- An HTML page after more than 2000 bytes of whitespace (spaces, tabs, line breaks, a byte-order mark or a no-break space) was saved as the result file on every bulk download and both Warmup CSV downloads, and replaced a file already at the path, because only the first 2000 bytes of the body were checked for HTML and those were all padding. The check now starts after the padding, so the page is an error with the HTTP status and the existing file is kept. A body of only padding is still not HTML.
- `get_file_streaming` and `scoring_get_file_streaming` dropped leading whitespace that shared their first 4096-byte block with the file (2096 to 4095 bytes of padding, then the file). Those bytes are written at the front of the file. Padding that fills a whole 4096-byte block before any other byte is still not copied, as before, so a run of padding alone cannot become a file. An HTML page after that padding is still an error and the existing file is kept.
- A streamed JSON error longer than 4096 bytes was reported as the truncated body. It is now `The download was a JSON error larger than this client reads.` with the HTTP status. A JSON error that fits in those 4096 bytes is still the API's message.
- On Windows, a finished download deleted the file already at the path and then renamed the new one into place. If the rename failed, the old file was already gone. The replace is now one call, and a failure leaves the old file where it was.
- `zb_get_file_json_indicates_error` treated a byte-order mark or a no-break space before `{` as not an error. It skips the same padding the downloads skip.
- A JSON download error that came after a byte-order mark or spaces (`{"Message":"File not found"}`) was reported as the raw body on the streaming downloads, `email_finder_get_file`, `domain_search_get_file` and both Warmup CSV downloads. It is now reported as the API's message, as `get_file` already did.
- Every bulk download (`get_file`, `scoring_get_file`, their `_streaming` forms, `email_finder_get_file` and `domain_search_get_file`) saved an HTML page as the result file when its body did not look like HTML, for example a proxy's `<p>Access denied by proxy</p>` sent as `text/html`. A `text/html` content type (in any letter case) is now an error with the HTTP status, and nothing is written, as an HTML body already was.
- `get_file_streaming` and `scoring_get_file_streaming` deleted a file that was already at `local_download_path` when the download failed: a refused connection, a JSON error body, a non-2xx status or a body cut short removed the earlier file, although nothing had been written to it. `get_file` leaves it alone. The streaming download now writes to a temporary file next to `local_download_path` (`<path>.<pid>-<id>.part`) and moves it into place only when the whole file has arrived; a failed download removes only that temporary file, so a file already at the path is kept as it was, also when the connection closes part-way through the body.
- `get_file`, `scoring_get_file` (and their `_with_options` forms), `email_finder_get_file`, `domain_search_get_file`, `warmup_download_seeds` and `warmup_download_statistics` wrote straight into the target path, so a write that failed part-way (a full disk, a file size limit) destroyed a file that was already there: it was truncated by the open and then removed. They now write to a temporary file next to the target (`<path>.<pid>-<id>.part`) and move it into place only when the whole file is written, as the streaming downloads do; a failed write removes only the temporary file and reports `Could not write ...` as before, and the earlier file is kept unchanged.
- A Warmup CSV download (`warmup_download_seeds`, `warmup_download_statistics`) that could be written only in part (a full disk, a file size limit) reported `Could not write download_path` but left the truncated file behind. It is now removed, as the bulk downloads already did.
- An HTTP 200 labelled `text/html` whose body parsed as JSON (a proxy's or a captive portal's page, `{"a":1}`) reached the success callback as a result on every JSON call, the raw-JSON calls (filters, list evaluator, `list_s3_files`, Inbox Placement) and every Warmup JSON call included; only an HTML-looking body was caught. A `text/html` content type (in any letter case) is now an error with the HTTP status on every call, as it already was on the downloads.
- `"success": false` on an HTTP 200 reached the success callback on every raw-JSON call: `add_filter`, `list_filters`, `delete_filter`, `get_list_evaluator_result`, `list_s3_files` and the Inbox Placement Tester calls. It now goes to the error callback with status 200, as on every other call.
- **Behaviour change:** the Inbox Placement Tester calls treated an empty 2xx body as a success and passed `"{}"` to the success callback, whatever the call. Only `delete_inbox_placement_test` answers with an empty body, so it alone still does; an empty 2xx from any other Inbox Placement call (providers, test types, create, get, list, the webhook calls) is now an error with its status (`Empty response from ZeroBounce (HTTP 200)`).
- `warmup_download_seeds` and `warmup_download_statistics` handed a non-file answer to the success callback (and wrote it to `download_path`) when it did not start with `{` or `[`: a JSON body after leading whitespace, any body labelled `application/json`, or a `text/html` page that does not look like HTML. They now report an error with the status, and write nothing, for an empty body, a JSON or HTML content type, or a JSON object or array.
- Every Warmup JSON call (all but the two CSV downloads) handed any HTTP 200 body that was not an HTML page to the success callback, also when it was not JSON: a proxy's `hello` as `text/plain`, `not json` or a CSV labelled `application/json`. Such a body is now an error with status 200, as is a body of only whitespace; an empty body (HTTP 204 or an empty 200, zero bytes) is still the documented empty success, and a JSON object or array still the result.
- A JSON call whose HTTP 200 body was a JSON value followed by other text (`{"a":1} <- proxy note`) took the leading value as the result, because json-c stops reading after the first value. Every JSON call of `ZeroBounce` (the raw-JSON calls included) and of `ZeroBounceWarmup` now requires the body to be one JSON value, whitespace around it allowed, and reports anything else as an error with status 200.
- Every JSON call of `ZeroBounce` (the raw-JSON calls included) and of `ZeroBounceWarmup` took bodies that are not JSON as the result, because json-c's tokener is lenient: single quotes (`{'a':1}`), unquoted keys, trailing commas, `NaN` and `Infinity`, `TRUE`, leading zeros (`01`), `.5`, `+1`, a raw tab or a `\x41` escape in a string, comments, and a NUL byte after the value. A 2xx body must now be one strict RFC 8259 JSON value with only space, tab, CR or LF around it (no BOM or other whitespace); anything else is an error with status 200. A JSON string or number on its own (`"x"`, `0`) is JSON too: a raw-JSON call hands it over as the result, and a typed call reports it as an error (below). Only a zero-byte Warmup 2xx stays the documented empty success, as does an empty `delete_inbox_placement_test`; a body of only whitespace is an error with status 200.
- Every typed call (`get_credits`, `validate_email`, `get_api_usage`, the bulk file calls, the S3 and SFTP jobs, the email finder and domain search calls and the rest) took an HTTP 200 body that was valid JSON of the wrong shape, such as `0`, `"x"`, `[]` or `true`, as a result filled with defaults (`get_credits` reported `credits` `-1`). It is now an error with status 200; the raw-JSON calls still hand any JSON value over as it is.
- A response larger than the size cap (`ZB_MAX_JSON_RESPONSE_BYTES`, `ZB_MAX_GETFILE_BYTES` for `get_file` and its siblings) was reported as a transport failure with status 0 (`Failed to perform request. CURLcode: 23`), whatever the server answered. The error now keeps the HTTP status: a 500 stays a 500, and an oversized 200 is an error with status 200 (`The response body was larger than this client accepts (HTTP 200).`).
- Offline tests: a file job's `message` list holding a non-string item (`[5]`, `["ok",5]`, `[true]`, `[null]`, an object or a nested list) is an error with status 200 on every file-job call, checked by a test that fails when the item check is removed; a missing optional field reads as empty on every documented example, each non-required field left out in turn (required fields excepted), and a missing `validate` field reads as `""` or `false`.
- `file_status`, `delete_file`, `send_file`, `validate_email_batch` and `list_s3_files`, and the same jobs on the bulk root (email-finder and domain-search file status and delete, and the S3 and SFTP sends), treated `{}`, `{ }`, `{\n}`, `{\t}` and an object holding only a message as a result. They are now errors with the response status. Affects 3.0.0 to 3.0.1.
- `find_email` and `search_domain` treated an empty object, and a message beside only-null fields, as an empty result. They are now errors. `{"domain":null}` with no message is still a result. Affects 3.0.0 to 3.0.1.
- A file status returned any JSON object as a result, including `{"foo":1}` and `{"message":"m","file_id":null}`. A status now needs one non-null known field (`success`, `file_id`, `file_name`, `file_status`, `error_reason`, `file_phase_2_status`, `return_url`, `upload_date`, `complete_percentage`). `{"file_status":"Processing"}` is still a result. Affects 3.0.0 to 3.0.1.
- The batch account-level `"all"` error was reported with status 0. It now carries the HTTP status. Affects 3.0.0 to 3.0.1.
- A download whose body started with U+FEFF or U+00A0 before `{` or `[`, a body of only BOMs, a body of only whitespace, or a `+json` content type was saved as the file. Those are now errors, and a file already at the path is left unchanged. Affects 3.0.0 to 3.0.1.
- `send_file`, `delete_file` (and their scoring, email-finder and domain-search forms), `validate_email_batch`, `list_s3_files`, the list-evaluator sends and the S3 and SFTP jobs took `{"message":"m","file_id":null}`, a message beside only null fields, as a result. It is now an error with the HTTP status (200 or 202). `{"domain":null}` with no message is still a result, and so is `{"message":"m","file_id":"x"}`. Affects 3.0.0 to 3.0.1.
- `get_api_usage`, `get_activity_data` and `score_email` took an empty object, or an object holding only a message, as a result filled with defaults. They are now errors with the HTTP status, as `validate_email` already was. Affects 3.0.0 to 3.0.1.
- `get_file_streaming` and `scoring_get_file_streaming` decided on a body after its first 4096 bytes, so a body of more than 4096 bytes of BOMs (U+FEFF) or non-breaking spaces (U+00A0) before `{` or `[` was saved as the file, and padding alone was saved as an empty file. Whitespace, U+FEFF and U+00A0 at the start of the body are now skipped however long they run: the body is rejected if it then begins with `{`, `[` or has a `+json` content type, and a file already at the path is left unchanged. A body that is only padding is an empty response and writes nothing. Affects 3.0.0 to 3.0.1.
- **Behaviour change:** a `"success": null` (or any "success" that is not a boolean or a "true"/"false" string) no longer hides an error. `{"message":"m","success":null}` and `{"error":"Invalid API key","success":null}` answered with HTTP 200/202 used to reach the success callback as an empty result on get_api_usage, validate_email_batch, send_file, delete_file, get_activity_data, score_email, the scoring send/delete calls and the email-finder send/delete calls; they now reach the error callback with the API's message and the HTTP status. Affects every released version before this one.
- An error object with no message text (`{"message":null}`, a blank message, an empty list) used to reach the error callback with an empty error list; it now carries "ZeroBounce returned an error with no message text."
- A NULL client passed to any public call used to crash (a NULL dereference); it now reaches the error callback with "ZeroBounce is not initialized. Please call zero_bounce_initialize(zb_instance, api_key); first". A NULL error callback is no longer called on the uninitialised-key path or when libcurl fails to initialise.
- **Behaviour change:** a base URL that holds a query (or a bare `?`), a fragment, a backslash, whitespace, a control character or an invisible/Unicode-space character (set with `zero_bounce_set_bulk_api_base_url`, `..._root_url`, `..._scoring_base_url`, `zero_bounce_set_list_evaluator_base_url`, `zero_bounce_set_inbox_placement_base_url` or `zero_bounce_warmup_set_base_url`) is now refused when a request is sent, with the error "request URL must not contain a query, a fragment, a backslash, whitespace or control characters". Before, `https://host/v2?x=1` and `https://host/v2?` were sent, the SDK's own path and query ending up inside the caller's query. Affects all released versions.
- Every libcurl handle now sets `CURLOPT_NOSIGNAL`, so a request timeout does not use SIGALRM and is safe in a multi-threaded program.
- **Behaviour change:** a `success` field that is not a flag no longer counts as a result field. `success` decides only as a boolean, a JSON number 0 or 1, or a string "true", "false", "0" or "1" (any case, surrounding whitespace ignored); 0, "0", false and "false" are failures. Any other value (`""`, "maybe", `[]`, `{}`, 2, null) is not a flag. Before, `{"message":"m","success":0}` or `{"message":"m","success":"maybe"}` on HTTP 200 reached the success callback of get_api_usage, get_activity_data, score_email and the batch call as an empty result; now they report the message as an error with the HTTP status. Affects 2.2.0 to 3.0.1.
- `zero_bounce_initialize(NULL, ...)` and `zero_bounce_initialize_with_base_url(NULL, ...)` crashed (NULL dereference); they are now no-ops.

### Security

- `warmup_test_smtp_configuration` masked the SMTP username and password only in their raw form, so an error body that quoted them URL-encoded (`Pa%2Fss%22w%25rd`) or JSON-escaped, as a server or proxy echoing the request body would (`Pa\/ss\"w%rd`), reached the error callback with the password readable. Every form is now replaced with `***`, as is the Warmup key: raw, URL-encoded (a space as `%20` or `+`), JSON-escaped (`/` escaped or not), JSON-escaped twice (an echoed body quoted inside another JSON string, `Pa\\\/ss\\\"w%rd`), and URL-encoded after JSON escaping (`Pa%5C%2Fss%5C%22w%25rd`), as a server putting an echoed JSON body into a URL or form would.
- An error built from a response body could carry the API key. Only an `api_key=` parameter's value was masked, so a body quoting the key itself reached the error callback whole: `{"error": "Invalid API key <key>"}` or `{"success": false, "message": "... <key>"}` on an HTTP 200, a JSON or text 4xx quoting it, or the key in its URL-encoded form. The Warmup client masked only the SMTP credentials of `warmup_test_smtp_configuration`, so a Warmup error quoting the key came through too. `parse_error_with_status`, which builds every error a callback gets, now replaces the calling client's key, raw, URL-encoded (a space as `%20` or `+`), JSON-escaped (`"` and `\` escaped, `/` escaped or not, as a request body echoed back), JSON-escaped twice and URL-encoded after JSON escaping, with `REDACTED` on every call of both clients, besides any `api_key=` value. `zb_set_redaction_key` (internal) records the key per thread at the check every call runs first.
- Key and SMTP-credential redaction covered only the spellings it generated (raw, URL-encoded, JSON-escaped and a few compositions), so a body quoting a secret through another composition, such as URL-encoded three times or URL-encoded after two JSON escapes, reached the error callback decodable. After masking, every error message is now decoded up to four more times (the SDK's own request encoding plus three layers an echo may add: URL-decoding with upper- or lower-case hex, `+` as a space, JSON-unescaping including `\/`, `\uXXXX` and surrogate pairs, in any order), and a `%00` or `\u0000` in the body does not end the search; if any view still holds the API key, the Warmup key or the SMTP username or password, the body-derived text is dropped for `The response body was omitted because it contained a credential.`
- A base URL with a username or password (`https://host@evil.example`) was sent. It is now rejected before the scheme check, and the error does not quote the URL. Affects v2.2.0 to v3.0.1, whose URL check is an `https://` prefix test only (v2.1.4 and earlier have no check at all).
- An API key with a Unicode space or an invisible character was sent. It is now rejected. Affects v2.2.0 to v3.0.1, whose key check is ASCII only (surrounding ASCII whitespace and control characters); v2.1.4 and earlier have no key check.
- An error that echoed a secret with a different letter case, or with a quote written as `\u{27}`, `\047`, `&APOS;`, `&#39;`, `\X27`, `\'` or `''`, still contained the secret. Such an error is now redacted. Affects v2.2.0 to v3.0.1 for the Warmup key and SMTP credentials: their masking (`warmup_redact`, byte-identical in v2.2.0, v3.0.0 and v3.0.1) is a case-sensitive raw match. The API key itself was not masked at all before this release (see the entry above); only `api_key=` parameter values were.
- The redaction of an error also undoes `\U{27}`, `\U0027` and `\U00000027` (a capital `\U`, with 4 or 8 hex digits, or braces), so an error that echoed the API key, the Warmup key or the SMTP password through these spellings no longer holds it. Affects v2.2.0 to v3.0.1 for the Warmup key and SMTP password, as in the entry above.
- Fixed a heap buffer overflow in the redaction of `api_key=` parameters in error messages: every `api_key=` grows by 8 bytes but the buffer only had 16 spare, so a server-controlled text with several (`api_key=&api_key=&...`) wrote past it. The buffer is now sized from the match count. Affects v2.2.0 to v3.0.1 (every error path, Warmup included); v2.1.4 and earlier have no redaction. A new test runs under ASan (`-DZB_SANITIZE=ON`).
- Redaction compares under Unicode full case folding, not ASCII only: `PÄSSWÖRD-SSETA` echoing `Pässwörd-ßeta` is now caught (table generated by `tools/gen_casefold.py`).
- Redaction also decodes `&#00000039;` (any number of digits), `&#39` / `&#x27` without the semicolon, the HTML named references `&quot; &amp; &lt; &gt;` and the Latin-1 set (`&auml;`, `&szlig;`, ...), and `\xHH` / `\ooo` spelled as the UTF-8 bytes of a non-ASCII secret.
- The API key copies are zeroed before they are freed.
- Every buffer that held the API key (the key's URL-escaped copies, request URLs, form and POST bodies, the Warmup api_key, the Warmup and Inbox Placement auth headers, the redaction forms) is zeroed before it is freed, and URL buffers grown with realloc are moved to a new block with the old one zeroed. The SMTP password in warmup_test_smtp_configuration (serialized body and string node) is zeroed too. Memory hygiene only; no behaviour change.

## [3.0.1] - 2026-11-11

### Fixed

- `get_file_streaming` and `scoring_get_file_streaming` saved a JSON error body as the downloaded file and called the success callback: `{"error": ...}`, `{"message": ...}`, `{"Message": ...}` and `{"Error": ...}` alike, with an `application/json` content type or without. Only `{"success": false, ...}` and HTML pages were caught. They now report it to the error callback with the HTTP status, as `get_file` does, and write nothing.
- `get_file_streaming` and `scoring_get_file_streaming` ignored write errors: a full disk or a file size limit left a truncated file and still called the success callback. They now stop the transfer, remove the partial file and report `Could not write local_download_path`.
- `get_file`, `scoring_get_file` and their `_with_options` forms saved a `{"Message": ...}` or `{"Error": ...}` body (as an invalid key answers) as the file when the content type was not `application/json`, and `email_finder_get_file` and `domain_search_get_file` saved it even with that content type. Every download now treats any JSON object body, whatever its keys or content type, as the API's error, carrying the HTTP status, as the Pascal and iOS SDKs do. `zb_get_file_json_indicates_error` and `zb_format_get_file_error_message` also recognise the `Message` and `Error` keys.
- When `get_file` and its siblings, `email_finder_get_file` or `domain_search_get_file` could write a file only in part, the error was reported but the truncated file was left behind. It is now removed.
- `send_file` and `scoring_send_file` formatted the column numbers it sends into a 10-byte buffer, so a value of 10 digits or more, or a negative `email_address_column_index` of 9 digits or more (`INT_MAX` needs 11 bytes, `INT_MIN` 12), wrote one or two bytes past it. The buffer now holds any `int`, and the value is sent whole. gcc 15 warned about it.

## [3.0.0] - 2026-11-11

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

## [2.1.4] - 2026-08-14

### Changed

- CI dependency bumps only: `actions/checkout` 6 to 7 and `github/codeql-action` 4.35.2 to 4.36.3. No library change.

## [2.1.3] - 2026-06-17

### Fixed

- `validate_email_batch` posts to `api.zerobounce.net/v2/validatebatch` instead of `bulkapi.zerobounce.net`.

## [2.1.2] - 2026-06-10

### Added

- `ZBValidateResponse.catchall_domain`, read from `catchall_domain`.

## [2.1.1] - 2026-04-18

### Changed

- CI only: every `github/codeql-action/*@v4` reference in the workflows is pinned to `v4.35.2`. No library change.

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

## [2.0.9] - 2026-03-03

### Changed

- README note on the CI tag check ("verify commit and tag workflow"). No library change.

## [2.0.8] - 2026-03-03

### Changed

- Publish documentation updates and a version bump in `CMakeLists.txt`. No library change.

## [2.0.7] - 2026-03-03

### Changed

- Publish documentation updates and a version bump in `CMakeLists.txt`. No library change.

## [2.0.6] - 2026-03-03

### Added

- README "Publish" section and registry-submission documentation. No library change.

## [2.0.5] - 2026-03-03

### Changed

- Version bump in `CMakeLists.txt` only, after the workflow changes in 2.0.4. No library change.

## [2.0.4] - 2026-03-03

### Changed

- GitHub workflows: explicit permissions, `actions/checkout` v6, `sdk_ci.yml` synced. No library change.

## [2.0.3] - 2026-03-03

### Changed

- Version bump in `CMakeLists.txt` only (GitHub workflow update). No library change.

## [2.0.2] - 2026-03-03

### Changed

- Version bump in `CMakeLists.txt` only (GitHub workflow update). No library change.

## [2.0.1] - 2026-03-03

### Added

- GitHub workflows: auto-assign and `sdk_ci.yml`. No library change.

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
[2.1.4]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.1.3...v2.1.4
[2.1.3]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.1.2...v2.1.3
[2.1.2]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.1.1...v2.1.2
[2.1.1]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.1.0...v2.1.1
[2.1.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.10...v2.1.0
[2.0.10]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.9...v2.0.10
[2.0.9]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.8...v2.0.9
[2.0.8]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.7...v2.0.8
[2.0.7]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.6...v2.0.7
[2.0.6]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.5...v2.0.6
[2.0.5]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.4...v2.0.5
[2.0.4]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.3...v2.0.4
[2.0.3]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.2...v2.0.3
[2.0.2]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.1...v2.0.2
[2.0.1]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v2.0.0...v2.0.1
[2.0.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v1.1.0...v2.0.0
[1.1.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/zerobounce/zerobounce-c-api-wrapper/releases/tag/v1.0.0
