# Contributing to Zero Bounce C API Wrapper

Thank you for your interest in contributing. This document explains how to get set up and submit changes.

## Code of Conduct

By participating in this project, you agree to uphold our [Code of Conduct](CODE_OF_CONDUCT.md).

## Getting Started

See the [README](README.md) for prerequisites, setup, and how to run tests.

## How to Contribute

### Reporting Bugs

Open an [issue](https://github.com/zerobounce/zerobounce-c-api-wrapper/issues) and include:

* Environment details (compiler, OS)
* Steps to reproduce
* Expected vs actual behavior
* Relevant code or error messages

### Suggesting Changes

* Check existing issues and pull requests first.
* Open an issue to discuss larger changes or API design before coding.

### Submitting Changes

1. **Fork** the repository and create a branch from `main`:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes** and add or update tests where relevant.

3. **Run the test suite** (see README) before submitting. The offline suites need no key and no network: build, then run `./build/bin/ZeroBounceTests` and `./build/bin/ZeroBounceTransportTests` from the repository root (add `-DZB_SANITIZE=ON` in a second build directory to run them under ASan and UBSan). The live suite (`live/`) calls the real API: it is opt-in (`-DZEROBOUNCE_BUILD_LIVE_TESTS=ON`), reads `ZEROBOUNCE_API_KEY` (and `ZEROBOUNCE_WARMUP_API_KEY`) from the environment, and a key is never committed, written to a file or put in an issue or pull request. Pull requests are checked by the offline suites only.

4. **Commit** with a clear message (e.g. `Add X`, `Fix Y`).

5. **Push** your branch and open a **Pull Request** against `main`.

6. In the PR description, briefly explain what changed and why. Link any related issues.

Maintainers will review and may request changes. Once approved, your PR can be merged.

## Releases (maintainers)

There is no package registry: a release is a GitHub release for a tag. Bump `project(ZeroBounce VERSION ...)` in `CMakeLists.txt`, move the `[Unreleased]` entries of [CHANGELOG.md](CHANGELOG.md) under the new version, commit, and tag `vX.Y.Z`. Then run **Actions, Publish, Run workflow** with that tag: the workflow tests the tag and creates the release. Do not create the release by hand. See the **Publish** section in [README.md](README.md).

## Questions

* [Zero Bounce API docs](https://www.zerobounce.net/docs/)
* [Project homepage](https://zerobounce.net)
* Contact: **integrations@zerobounce.net**

Thanks for contributing.
