#!/usr/bin/env bash
#
# Checks that the repository is ready to be released as the given version. Run it before pushing the release tag;
# the release workflow runs it too and stops before publishing anything if a check fails.
#

set -u

usage() {
    echo "Usage:"
    echo "  validate-release.sh <version> [--ref <git-ref>] [--branch <branch>]"
    echo ""
    echo "  <version>            Version to release (e.g. 0.8.0-rc.1, 0.8.0)"
    echo "  --ref <git-ref>      Commit that will be tagged [default: HEAD]"
    echo "  --branch <branch>    Branch the commit must be on [default: origin/master]"
    echo "  -h --help            Display this message"
}

VERSION=""
REF="HEAD"
BRANCH="origin/master"

while [ $# -gt 0 ]; do
    case "$1" in
        --ref)
            REF="${2:-}"
            shift
            ;;
        --branch)
            BRANCH="${2:-}"
            shift
            ;;
        -h | --help)
            usage
            exit 0
            ;;
        -*)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
        *)
            VERSION="$1"
            ;;
    esac
    shift
done

if [ -z "${VERSION}" ]; then
    usage
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHANGELOG="${REPO_ROOT}/CHANGELOG.md"
REPO_URL="https://github.com/ethanmoffat/etheos"
FAILURES=0

fail() {
    echo "::error::$1"
    FAILURES=$((FAILURES + 1))
}

pass() {
    echo "ok: $1"
}

# Version format: MAJOR.MINOR.PATCH with an optional -rc.N suffix
#
if [[ "${VERSION}" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-rc\.[1-9][0-9]*)?$ ]]; then
    pass "version ${VERSION} is valid"
else
    fail "Version ${VERSION} must be MAJOR.MINOR.PATCH with an optional -rc.N suffix"
fi

# CMake project version
#
project_version="$(sed -n 's/^project(etheos VERSION \([0-9]*\.[0-9]*\.[0-9]*\) .*$/\1/p' "${REPO_ROOT}/CMakeLists.txt" | head -n 1)"
suffix="$(sed -n 's/^set(ETHEOS_VERSION_SUFFIX "\(.*\)")$/\1/p' "${REPO_ROOT}/CMakeLists.txt" | head -n 1)"
cmake_version="${project_version}${suffix:+-${suffix}}"
if [ "${cmake_version}" = "${VERSION}" ]; then
    pass "CMakeLists.txt version is ${cmake_version}"
else
    fail "CMakeLists.txt version is '${cmake_version}', expected ${VERSION} (project VERSION and ETHEOS_VERSION_SUFFIX)"
fi

# CHANGELOG.md: a dated section with at least one entry, and comparison links
#
escaped_version="${VERSION//./\\.}"
heading="$(grep -E "^## \[${escaped_version}\]" "${CHANGELOG}" | head -n 1)"
if [ -z "${heading}" ]; then
    fail "CHANGELOG.md has no '## [${VERSION}] - YYYY-MM-DD' section"
elif [[ ! "${heading}" =~ ^##\ \[${escaped_version}\]\ -\ ([0-9]{4}-[0-9]{2}-[0-9]{2})$ ]]; then
    fail "CHANGELOG.md heading '${heading}' must be '## [${VERSION}] - YYYY-MM-DD'"
elif ! date -d "${BASH_REMATCH[1]}" > /dev/null 2>&1; then
    fail "CHANGELOG.md date ${BASH_REMATCH[1]} is not a valid date"
else
    pass "CHANGELOG.md has a section for ${VERSION} dated ${BASH_REMATCH[1]}"
fi

first_section="$(grep -m 1 -E '^## \[' "${CHANGELOG}")"
if [ "${first_section}" = "## [Unreleased]" ]; then
    pass "CHANGELOG.md starts with an [Unreleased] section"
else
    fail "CHANGELOG.md must start with an '## [Unreleased]' section"
fi

entries="$(awk -v heading="${heading}" '
    $0 == heading { in_section = 1; next }
    in_section && /^## \[/ { exit }
    in_section && /^- / { count++ }
    END { print count + 0 }' "${CHANGELOG}")"
if [ -n "${heading}" ] && [ "${entries}" -gt 0 ]; then
    pass "CHANGELOG.md section for ${VERSION} has ${entries} entries"
elif [ -n "${heading}" ]; then
    fail "CHANGELOG.md section for ${VERSION} has no entries"
fi

if grep -qE "^\[${escaped_version}\]: ${REPO_URL//./\\.}/compare/[^ ]+\.\.\.${escaped_version}$" "${CHANGELOG}"; then
    pass "CHANGELOG.md links [${VERSION}] to its comparison"
else
    fail "CHANGELOG.md is missing the link '[${VERSION}]: ${REPO_URL}/compare/<previous>...${VERSION}'"
fi

if grep -qE "^\[Unreleased\]: ${REPO_URL//./\\.}/compare/${escaped_version}\.\.\.HEAD$" "${CHANGELOG}"; then
    pass "CHANGELOG.md [Unreleased] link compares from ${VERSION}"
else
    fail "CHANGELOG.md [Unreleased] link must be '${REPO_URL}/compare/${VERSION}...HEAD'"
fi

# Git: the commit is on the release branch and the tag isn't already used for another commit
#
if ! commit="$(git -C "${REPO_ROOT}" rev-parse --verify --quiet "${REF}^{commit}")"; then
    fail "${REF} is not a commit"
else
    if ! git -C "${REPO_ROOT}" rev-parse --verify --quiet "${BRANCH}" > /dev/null; then
        fail "Branch ${BRANCH} not found (fetch it first)"
    elif git -C "${REPO_ROOT}" merge-base --is-ancestor "${commit}" "${BRANCH}"; then
        pass "${REF} is on ${BRANCH}"
    else
        fail "${REF} (${commit:0:7}) is not on ${BRANCH}"
    fi

    if tagged="$(git -C "${REPO_ROOT}" rev-parse --verify --quiet "refs/tags/${VERSION}^{commit}")" \
        && [ "${tagged}" != "${commit}" ]; then
        fail "Tag ${VERSION} already exists on a different commit (${tagged:0:7})"
    fi
fi

echo ""
if [ "${FAILURES}" -gt 0 ]; then
    echo "${FAILURES} check(s) failed; not ready to release ${VERSION}."
    exit 1
fi
echo "Ready to release ${VERSION}."
