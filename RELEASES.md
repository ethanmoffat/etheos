# Releases

This document describes how etheos is branched, versioned and released. It's for maintainers (and the AI agents working for them); users only need the [changelog](CHANGELOG.md) and the published releases.

## Versioning

Versions follow [Semantic Versioning](https://semver.org): `MAJOR.MINOR.PATCH`, optionally followed by `-rc.N` for a release candidate. Release tags are the bare version, without a `v` prefix (e.g. `0.8.0-rc.1`, `0.8.0`).

The version is tracked in `CMakeLists.txt` only: `project(etheos VERSION ...)` holds the numeric version and `ETHEOS_VERSION_SUFFIX` holds the `rc.N` suffix (empty for a stable release). CMake generates `version.h` from them. Don't edit them by hand; use `scripts/prepare-release.sh`.

Legacy `build/0.7.1.NNNN` git tags and the matching `darthchungis/etheos` Docker images come from the previous CI system. They're kept, but no new ones are created.

## Branches

- `master` is always releasable. The maintainer may push to it directly; everyone else opens a pull request, which is squash merged.
- Larger work happens on a `feat/*` branch. Each piece of work is a pull request into the feature branch, squash merged so that it becomes one commit. When the feature is done, the feature branch is rebased onto `master` and pushed as a fast-forward, so `master` gets one commit per pull request.
- Starting a feature branch that will be released as a new version begins with a version bump commit, e.g. `scripts/prepare-release.sh 0.8.0-rc.1 --version-only --commit`, so that its builds report the upcoming version.

## Workflows

- **Build** (`.github/workflows/build.yml`) runs on every push and pull request to `master` and `feat/*`. It builds and tests on Linux and Windows, builds the Docker image, and runs the EOBot integration tests against that image. It uploads the install directories and the image as artifacts, and publishes nothing.
- **Release** (`.github/workflows/release.yml`) runs when a version tag is pushed. It:
  1. Runs `scripts/validate-release.sh`: the tag matches the `CMakeLists.txt` version, `CHANGELOG.md` has a dated section with entries and comparison links, and the tagged commit is on `master`.
  2. Finds the successful Build run on `master` for the tagged commit. Nothing is rebuilt; the release publishes exactly what was tested.
  3. Pushes `darthchungis/etheos:<version>` to Docker Hub, and fails if that tag already exists. Only stable releases also move `latest`.
  4. Creates a GitHub release with the changelog section as its notes, `etheos-<version>-linux-x64.zip`, `etheos-<version>-windows-x86.zip` and a SHA-256 checksum file. Release candidates are marked as pre-releases.

## Changelog

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Add user-facing changes to the `[Unreleased]` section as they land (features and behavior changes; not refactoring, CI or build internals). etheos is a fork of EOSERV, so only changes made since the fork are listed.

## Releasing

1. On an up-to-date `master`, make sure `[Unreleased]` in `CHANGELOG.md` has entries for everything in the release.
2. Run `scripts/prepare-release.sh <version> --tag`. It sets the version in `CMakeLists.txt`, moves the `[Unreleased]` entries into a `[<version>] - <today>` section, updates the comparison links, validates the result, commits "Release <version>" and creates the annotated tag. Use `--date YYYY-MM-DD` to set a different date, or leave out `--tag` to review the changes before committing.
3. Push `master` (`git push origin master`) and wait for its Build run to pass.
4. Push the tag (`git push origin <version>`) and check that the Release run passes.

If the Release run fails before publishing, fix the problem on `master`, delete the tag locally and remotely, and tag the fixed commit once its build passes. Once a Docker image or GitHub release is published, don't reuse the version; release the next `rc.N` or patch version instead.

To promote a release candidate, release the same numeric version without the suffix (e.g. `0.8.0` after `0.8.0-rc.1`). Its changelog section should list everything since the previous stable release: add a `## [0.8.0] - <date>` section by hand that combines the release candidates' entries (plus any `[Unreleased]` ones) before running `prepare-release.sh`, which keeps an existing section's entries and only updates the version and links.
