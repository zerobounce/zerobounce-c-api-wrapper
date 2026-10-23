# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project

`zerobounce-c-api-wrapper` is the ZeroBounce C SDK. Callers allocate a `ZeroBounce` with `new_zero_bounce_instance`, call `zero_bounce_initialize`, and receive results through success and error callbacks. HTTP goes through libcurl in `src/ZeroBounce.c` and `src/Priority3.c`. There is no package registry: releases are GitHub tags. This is not the C++ repository `zerobounce-cpp-api-wrapper`.

## Layout

- `include/ZeroBounce/`: public headers. `ZeroBounce.h` is the client.
- `src/ZeroBounce.c`: validation, bulk file, credits, activity, scoring, and the libcurl write callback.
- `src/Priority3.c`: bulk email finder, domain search, list evaluator, filters, the S3 / SFTP bulk jobs, and the Inbox Placement Tester. The six file jobs share `ZBBulkFileJobOptions` and `p3_bulk_root_file_job`.
- `src/ZBErrorResponse.c`: turns HTTP failures, HTML pages, and empty or non-JSON bodies into a short `ZBErrorResponse`.
- `test/ZeroBounceTest.c`: existing Unity tests.
- `test/OfflineApiTest.c`: offline suite. `zero_bounce_set_http_perform_for_test` replaces libcurl and records every request.
- `CMakeLists.txt`: the only version field, `project(ZeroBounce VERSION x.y.z)`.

## Commands

```bash
cmake -S . -B build -G "Unix Makefiles"
cmake --build build          # this is the typecheck; there is no separate lint step
./build/bin/ZeroBounceTests
```

CI (`.github/workflows/sdk_ci.yml`) configures, builds, and runs `./build/bin/ZeroBounceTests`. That build is the typecheck. `build/` is generated; do not commit it.

## Conventions

- **One `ZeroBounce` per API key.** `zero_bounce_get_instance()` is a process-wide singleton. Construct a separate object with `new_zero_bounce_instance` per key. The offline concurrency test does this.
- **Only endpoints documented at https://www.zerobounce.net/docs/.** Do not add a function for an endpoint the public docs do not describe. Check before concluding something is undocumented: the S3 and SFTP bulk endpoints live under https://www.zerobounce.net/docs/api-integrations, not the main API reference.
- **Four base URLs on `ZeroBounce`.** `api_base_url`, `bulk_api_base_url` (ends in `/v2`), `bulk_api_root_url` (the S3/SFTP jobs, **no** `/v2`), and `list_evaluator_base_url` / `inbox_placement_base_url`. The last two are separate hosts rather than paths built from the bulk base, so `zero_bounce_initialize_with_base_url` alone leaves them on the live API — use the two setters in tests.
- **`zb_url_escape` is a query encoder.** It turns a space into `+`, which is correct in a query string but a literal plus in a path. Use `p3_escape_path_segment` for anything that goes into a path, such as the list evaluator file id and the Inbox Placement test id.
- **Two bulk shapes are easy to get wrong.** `validate_sftp_file`'s `file_name` is optional — leaving it `NULL` uses the input bucket from the account's settings, and every other job requires it. `export_sftp_file` sends `export_path`, not the `export_bucket` that `export_s3_file` sends.
- **`p3_append_form_field` does not call `zb_note_http_field`.** The offline tests assert on the recorded request body (`OfflineRequest.body`), not on `has_field`, for anything built with it.
- **Column indexes are 1-based** on bulk send-file name, gender, and IP columns. `0` means the field is left out. `send_file`'s email column is required and is sent as given, including `0`. `ValidateS3FileOptions.email_address_column` uses `-1` for omit, and `0` is sent.
- **Tri-state ints are `-1` omit, `0` false, `1` true.** `timeout`, `activity_data`, `verify_plus`, `has_header_row`, and `allow_phase_2` follow that. Explicit `0` is sent as `false`.
- **Booleans on multipart and form bodies are the strings `true` and `false`.**
- **`allow_phase_2` is validation `send_file` only**, and only when `allow_phase_2_is_set` is true. Scoring `send_file` ignores it. `activity_data` on get-file is validation only.
- **`validate_email_batch` uses `api.zerobounce.net`**, not `bulkapi.zerobounce.net`. An empty batch IP is JSON `null`.
- **Failures use the error callback**, not a process exit. The message must be short: no raw HTML, no megabyte body, and no `Error: Error:` prefix. An account-level `validate_email_batch` error (`email_address` `all`) is an error callback. A per-address error stays on the success result.
- **Tests replace HTTP** with `zero_bounce_set_http_perform_for_test`. Read `zero_bounce_test_method`, `zero_bounce_test_url`, `zero_bounce_test_body`, and `zero_bounce_test_field_*` from that callback. They are thread-local. Do not call the live API from the default test run.
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
4. Tag `vx.y.z` on that commit and run the Publish workflow with that tag. It creates the GitHub release. There is no package registry.
