# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project

`zerobounce-c-api-wrapper` is the ZeroBounce C SDK. Callers allocate a `ZeroBounce` with `new_zero_bounce_instance`, call `zero_bounce_initialize`, and receive results through success and error callbacks. HTTP goes through libcurl in `src/ZeroBounce.c` and `src/Priority3.c`. There is no package registry: releases are GitHub tags. This is not the C++ repository `zerobounce-cpp-api-wrapper`.

## Layout

- `include/ZeroBounce/`: public headers. `ZeroBounce.h` is the client.
- `src/ZeroBounce.c`: validation, bulk file, credits, activity, scoring, and the libcurl write callback.
- `src/Priority3.c`: bulk email finder, domain search, list evaluator, filters, the S3 / SFTP bulk jobs, and the Inbox Placement Tester. The six file jobs share `ZBBulkFileJobOptions` and `p3_bulk_root_file_job`.
- `src/ZeroBounceWarmup.c` / `include/ZeroBounce/ZeroBounceWarmup.h`: the Warmup API. A separate product on its own host with its own team-scoped key and an `API-Key` header, so it has its own `ZeroBounceWarmup` and never touches the validation key.
- `src/ZBErrorResponse.c`: turns HTTP failures, HTML pages, and empty or non-JSON bodies into a short `ZBErrorResponse`, with the HTTP status on `status_code`.
- `test/ZeroBounceTest.c`: existing Unity tests.
- `test/OfflineApiTest.c`, `test/OfflineWarmupTest.c`: offline suites. `zero_bounce_set_http_perform_for_test` replaces libcurl and records every request.
- `live/live_api.c`: the opt-in live suite. Built only with `-DZEROBOUNCE_BUILD_LIVE_TESTS=ON`, so the default build and CI never compile it.
- `CMakeLists.txt`: the only version field, `project(ZeroBounce VERSION x.y.z)`.

## Commands

```bash
cmake -S . -B build -G "Unix Makefiles"
cmake --build build          # this is the typecheck; there is no separate lint step
./build/bin/ZeroBounceTests
```

CI (`.github/workflows/sdk_ci.yml`) configures, builds, and runs `./build/bin/ZeroBounceTests`. That build is the typecheck. `build/` is generated; do not commit it.

The live suite is opt-in and is never part of that build:

```bash
cmake -S . -B build-live -DZEROBOUNCE_BUILD_LIVE_TESTS=ON
cmake --build build-live --target ZeroBounceLiveApi
export ZEROBOUNCE_API_KEY=... ZEROBOUNCE_WARMUP_API_KEY=...
ZEROBOUNCE_LIVE_SPEND=1 ZEROBOUNCE_LIVE_CHANGES=1 ./build-live/bin/ZeroBounceLiveApi
```

Keys come from the environment only. Never write one into a file, a commit, a log, a fixture or a message.

## Conventions

- **One `ZeroBounce` per API key.** `zero_bounce_get_instance()` is a process-wide singleton. Construct a separate object with `new_zero_bounce_instance` per key. The offline concurrency test does this.
- **Only endpoints documented at https://www.zerobounce.net/docs/.** Do not add a function for an endpoint the public docs do not describe. Check before concluding something is undocumented: the S3 and SFTP bulk endpoints live under https://www.zerobounce.net/docs/api-integrations, not the main API reference.
- **Four base URLs on `ZeroBounce`.** `api_base_url`, `bulk_api_base_url` (ends in `/v2`), `bulk_api_root_url` (the S3/SFTP jobs, **no** `/v2`), and `list_evaluator_base_url` / `inbox_placement_base_url`. The last two are separate hosts rather than paths built from the bulk base, so `zero_bounce_initialize_with_base_url` alone leaves them on the live API — use the two setters in tests.
- **`zb_url_escape` is a query encoder.** It turns a space into `+`, which is correct in a query string but a literal plus in a path. Use `p3_escape_path_segment` for anything that goes into a path, such as the list evaluator file id and the Inbox Placement test id.
- **The live API wins over the docs.** Where they disagree, follow the API and say so in a comment at the call site, prefixed `LIVE:`. The ones found so far: `validatebatch` takes `timeout` as a JSON *string*; the Inbox Placement Tester authenticates with `Authorization: Bearer` and lists tests from `GET /tests` (plural); every S3/SFTP job requires both `file_name` and `return_url`, `validate_sftp_file` included, although the docs call its `file_name` optional; `send_evaluated_file` takes a file id as a form field, not an upload; Warmup's `check-domain-dmarc` takes an *email address*, not a domain; and `test-smtp-configuration` answers a failed SMTP login with HTTP 500.
- **A `"success": false` field on an HTTP 200 is a failure.** `getfile` and `deletefile` report an unknown or deleted file id that way. `zb_success_means_failure` catches it in `zb_read_success_json`, and `ZBErrorResponse.status_code` carries the HTTP status.
- **`export_sftp_file` sends `export_path`**, not the `export_bucket` that `export_s3_file` sends.
- **Safety rails.** `zb_api_key_is_clean` rejects a key with surrounding whitespace or control characters, `zb_check_request_url` allows https only, `p3_escape_path_segment` rejects `.` and `..`, `set_write_callback` turns redirects off and restricts the protocol to https, and `zb_redact_api_key` keeps the key out of messages built from a URL. Use them rather than adding a second rail beside them.
- **`p3_append_form_field` does not call `zb_note_http_field`.** The offline tests assert on the recorded request body (`OfflineRequest.body`), not on `has_field`, for anything built with it.
- **Column indexes are 1-based** on bulk send-file name, gender, and IP columns. `0` means the field is left out. `send_file`'s email column is required and is sent as given, including `0`. `ValidateS3FileOptions.email_address_column` uses `-1` for omit, and `0` is sent.
- **Tri-state ints are `-1` omit, `0` false, `1` true.** `timeout`, `activity_data`, `verify_plus`, `has_header_row`, and `allow_phase_2` follow that. Explicit `0` is sent as `false`.
- **Booleans on multipart and form bodies are the strings `true` and `false`.**
- **`allow_phase_2` is validation `send_file` only**, and only when `allow_phase_2_is_set` is true. Scoring `send_file` ignores it. `activity_data` on get-file is validation only.
- **`validate_email_batch` uses `api.zerobounce.net`**, not `bulkapi.zerobounce.net`. An empty batch IP is JSON `null`.
- **Failures use the error callback**, not a process exit. The message must be short: no raw HTML, no megabyte body, and no `Error: Error:` prefix. An account-level `validate_email_batch` error (`email_address` `all`) is an error callback. A per-address error stays on the success result.
- **Tests replace HTTP** with `zero_bounce_set_http_perform_for_test`. Read `zero_bounce_test_method`, `zero_bounce_test_url`, `zero_bounce_test_body`, `zero_bounce_test_extra_header`, and `zero_bounce_test_field_*` from that callback. They are thread-local. Do not call the live API from anything under `test/`.
- **Never from the live suite:** creating an Inbox Placement test (it cannot be deleted while running), setting the Inbox Placement webhook (it cannot be cleared), or ZB managed Warmup onboarding (it provisions a mailbox). Everything the `ZEROBOUNCE_LIVE_CHANGES=1` tier creates is removed again in the same run.
- Match the surrounding style. Do not reformat unrelated code.

## Changelog

`CHANGELOG.md` follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/), and the project adheres to [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).

- Add every user-visible change to `## [Unreleased]` in the same commit as the change.
- Group entries under these headings, in this order, omitting empty ones: `Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`.
- Write for SDK users: name the function, option, or behaviour, and say what changed. Leave out CI-only changes, internal refactors, and fixes to code that was never released.
- At 2.y.z, a breaking change bumps the major version, a new function or option bumps the minor version, and fixes alone bump the patch version.

## Releasing

Publishing and pushing tags need a maintainer's explicit approval. Do not bump the version, tag, or publish unless they asked.

1. Move `[Unreleased]` entries under `## [x.y.z] - YYYY-MM-DD`, leave an empty `[Unreleased]` section, and update the compare links at the bottom of `CHANGELOG.md`.
2. Set the same version in `project(ZeroBounce VERSION …)` in `CMakeLists.txt`. That is the only version field. There is no lockfile and no runtime version constant.
3. `cmake --build build` and `./build/bin/ZeroBounceTests`.
3a. Run the live suite by hand with real keys and both flags set, and paste its summary line into the release notes. The offline suite cannot catch an API that has changed shape underneath the SDK.
4. Tag `vx.y.z` on that commit and run the Publish workflow with that tag. It creates the GitHub release. There is no package registry.
