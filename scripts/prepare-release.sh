#!/usr/bin/env bash
#
# Updates every file that references the version for a new release, then runs validate-release.sh:
#   - CMakeLists.txt: project() VERSION and ETHEOS_VERSION_SUFFIX
#   - CHANGELOG.md: moves the [Unreleased] entries into a new dated section, and updates the comparison links
#
# Optionally commits the changes and creates the annotated release tag. Nothing is pushed.
#
# With --version-only, only CMakeLists.txt is updated (e.g. to start development of the next version).

set -u

SCRIPT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
REPO_ROOT="$(dirname "${SCRIPT_ROOT}")"
REPO_URL="https://github.com/ethanmoffat/etheos"
CHANGELOG="${REPO_ROOT}/CHANGELOG.md"

VERSION=""
RELEASE_DATE="$(date +%Y-%m-%d)"
COMMIT=false
TAG=false
VERSION_ONLY=false

usage() {
    echo "Usage:"
    echo "  prepare-release.sh <version> [options]"
    echo ""
    echo "  <version>            Version to release (e.g. 0.8.0-rc.1, 0.8.0)"
    echo "  --date <YYYY-MM-DD>  Release date for the changelog [default: today]"
    echo "  --commit             Commit the changes"
    echo "  --tag                Commit the changes and create the annotated tag <version>"
    echo "  --version-only       Only update the version in CMakeLists.txt; skip the changelog and validation"
    echo "  -h --help            Display this message"
    echo ""
    echo "Push master first and wait for its build to pass, then push the tag, which starts the release workflow."
}

while [ $# -gt 0 ]; do
    case "$1" in
        --date)
            RELEASE_DATE="${2:-}"
            shift
            ;;
        --commit)
            COMMIT=true
            ;;
        --tag)
            COMMIT=true
            TAG=true
            ;;
        --version-only)
            VERSION_ONLY=true
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

if [[ ! "${VERSION}" =~ ^((0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*))(-(rc\.[1-9][0-9]*))?$ ]]; then
    echo "Version ${VERSION} must be MAJOR.MINOR.PATCH with an optional -rc.N suffix"
    exit 1
fi
NUMERIC_VERSION="${BASH_REMATCH[1]}"
SUFFIX="${BASH_REMATCH[6]}"

if ${VERSION_ONLY} && ${TAG}; then
    echo "--version-only can't be combined with --tag; only releases are tagged."
    exit 1
fi

if [[ ! "${RELEASE_DATE}" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
    echo "Release date ${RELEASE_DATE} must be YYYY-MM-DD"
    exit 1
fi

if ${COMMIT} && [ -n "$(git -C "${REPO_ROOT}" status --porcelain --untracked-files=no)" ]; then
    echo "The working tree has uncommitted changes. Commit or stash them before using --commit or --tag."
    exit 1
fi

if ${TAG} && git -C "${REPO_ROOT}" rev-parse --verify --quiet "refs/tags/${VERSION}" > /dev/null; then
    echo "Tag ${VERSION} already exists."
    exit 1
fi

escaped_version="${VERSION//./\\.}"

# Check the changelog before changing anything
#
if ! ${VERSION_ONLY}; then
    has_section=false
    if grep -qE "^## \[${escaped_version}\]" "${CHANGELOG}"; then
        has_section=true
    fi
    unreleased_entries="$(awk '
        /^## \[Unreleased\]/ { in_section = 1; next }
        in_section && /^## \[/ { exit }
        in_section && /^- / { count++ }
        END { print count + 0 }' "${CHANGELOG}")"
    if ! ${has_section} && [ "${unreleased_entries}" -eq 0 ]; then
        echo "CHANGELOG.md has no [Unreleased] entries to release. Add them first."
        exit 1
    fi

    previous_version="$(sed -n "s#^\[Unreleased\]: ${REPO_URL}/compare/\(.*\)\.\.\.HEAD\$#\1#p" "${CHANGELOG}")"
    if [ -z "${previous_version}" ]; then
        echo "CHANGELOG.md has no '[Unreleased]: ${REPO_URL}/compare/<previous>...HEAD' link."
        exit 1
    fi
fi

# CMakeLists.txt
#
sed -i.bak -E \
    -e "s/^project\(etheos VERSION [0-9]+\.[0-9]+\.[0-9]+ /project(etheos VERSION ${NUMERIC_VERSION} /" \
    -e "s/^set\(ETHEOS_VERSION_SUFFIX \".*\"\)$/set(ETHEOS_VERSION_SUFFIX \"${SUFFIX}\")/" \
    "${REPO_ROOT}/CMakeLists.txt"
rm -f "${REPO_ROOT}/CMakeLists.txt.bak"
echo "Updated CMakeLists.txt to ${NUMERIC_VERSION}${SUFFIX:+ (suffix ${SUFFIX})}"

if ${VERSION_ONLY}; then
    if ${COMMIT}; then
        git -C "${REPO_ROOT}" add -u
        git -C "${REPO_ROOT}" commit -q -m "Set version to ${VERSION}" || exit 1
        echo "Committed \"Set version to ${VERSION}\""
    fi
    exit 0
fi

# CHANGELOG.md
#
if ${has_section}; then
    echo "CHANGELOG.md already has a section for ${VERSION}; leaving its entries as they are"
else
    # Insert the new heading below [Unreleased], so its entries become the new version's entries.
    awk -v heading="## [${VERSION}] - ${RELEASE_DATE}" '
        { print }
        /^## \[Unreleased\]$/ && !done { print ""; print heading; done = 1 }' "${CHANGELOG}" > "${CHANGELOG}.tmp"
    mv "${CHANGELOG}.tmp" "${CHANGELOG}"
    echo "Moved ${unreleased_entries} [Unreleased] entries to ## [${VERSION}] - ${RELEASE_DATE}"
fi

# Point [Unreleased] at the new tag, and add the new version's comparison link below it.
if [ "${previous_version}" != "${VERSION}" ]; then
    awk -v version="${VERSION}" -v previous="${previous_version}" -v url="${REPO_URL}" '
        /^\[Unreleased\]:/ {
            print "[Unreleased]: " url "/compare/" version "...HEAD"
            print "[" version "]: " url "/compare/" previous "..." version
            next
        }
        { print }' "${CHANGELOG}" > "${CHANGELOG}.tmp"
    mv "${CHANGELOG}.tmp" "${CHANGELOG}"
    echo "Updated CHANGELOG.md links"
fi

echo ""
git -C "${REPO_ROOT}" --no-pager diff --stat
echo ""

# The release commit doesn't exist yet, so check against the current branch instead of origin/master.
if ! "${SCRIPT_ROOT}/validate-release.sh" "${VERSION}" --branch HEAD; then
    echo ""
    echo "Fix the problems above, then re-run this script or validate-release.sh."
    exit 1
fi

if ${COMMIT}; then
    git -C "${REPO_ROOT}" add -u
    git -C "${REPO_ROOT}" commit -q -m "Release ${VERSION}" || exit 1
    echo "Committed \"Release ${VERSION}\""
fi

if ${TAG}; then
    git -C "${REPO_ROOT}" tag -a "${VERSION}" -m "etheos ${VERSION}" || exit 1
    echo "Created tag ${VERSION}"
fi

echo ""
echo "Next: push master (git push origin master), wait for its build to pass, then push the tag (git push origin ${VERSION})."
