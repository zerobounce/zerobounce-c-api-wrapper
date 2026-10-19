# AGENTS.md

Guidance for AI coding agents working in this repository.

## Project

`zerobounce-c-api-wrapper` is the ZeroBounce C SDK. Callers allocate a `ZeroBounce` with `new_zero_bounce_instance`, call `zero_bounce_initialize`, and receive results through success and error callbacks. HTTP goes through libcurl in `src/ZeroBounce.c` and `src/Priority3.c`. There is no package registry: releases are GitHub tags. This is not the C++ repository `zerobounce-cpp-api-wrapper`.

## Layout

- `include/ZeroBounce/`: public headers. `ZeroBounce.h` is the client.
- `src/ZeroBounce.c`: validation, bulk file, credits, activity, scoring, and the libcurl write callback.
- `src/Priority3.c`: bulk email finder, domain search, list evaluator, filters, S3.
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
