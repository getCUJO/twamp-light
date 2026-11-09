#!/bin/bash
# Creates a release following VERSION_BUMP_GUIDE.md, up to and including opening the PR

USAGE="$0 [--dry-run] VERSION

Creates a release branch, tag, GitHub release and a pull request for the version bump.

e.g.
$0 1.4.4

This script will:
 * refuse to run if release PRs are open, the tree is dirty, or the tag/branch exists
 * show the PR title and description (changes since the last tag) and ask for confirmation, before anything is created
 * create release/vVERSION from origin/main and commit the bump of VERSION.txt
 * ask for confirmation, then create the tag vVERSION locally
 * ask for confirmation, then push the branch and the tag
 * create the GitHub release and open a pull request to main
Merging the PR into main with a merge commit is left to a person after review.
With --dry-run only the checks run and the PR preview is shown (fetching is the
only side effect); no branch, commit, tag, push, release or PR is created.
Set VERBOSE=1 to trace the commands.
Set ALLOW_OPEN_RELEASE_PRS=1 to proceed even though release PRs are open.
"
set -eEuo pipefail

dry_run=""
args=()
for arg in "$@"; do
    case "$arg" in
        --help) echo "$USAGE"; exit 101 ;;
        --dry-run) dry_run=1 ;;
        *) args+=("$arg") ;;
    esac
done
if [[ ${#args[@]} -ne 1 ]]; then
    echo "$USAGE"
    exit 101
fi

if [ -n "${VERBOSE:-}" ]; then
    set -x
fi

version="${args[0]}"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "VERSION must be MAJOR.MINOR.PATCH, got: $version" >&2
    exit 1
fi

MARK_OK="✅"
MARK_FAIL="❌"
CURRENT_STEP=""
LAST_OUTPUT=""
BRANCH_CREATED=""
TAG_CREATED=""
PUSHED=""

on_error() {
    trap - ERR
    if [[ -n "$CURRENT_STEP" ]]; then
        printf " %s\n" "$MARK_FAIL" >&2
        [[ -n "$LAST_OUTPUT" ]] && echo "$LAST_OUTPUT" | sed 's/^/      /' >&2
    fi
    echo "---------- Script failed ----------" >&2
    print_cleanup >&2
    exit 1
}
trap on_error ERR

print_cleanup() {
    if [[ -n "$PUSHED" ]]; then
        echo "Already pushed. To clean up the remote as well:"
        echo "  git push origin --delete $branch $tag   # only if no release/PR depends on them"
    fi
    if [[ -n "$BRANCH_CREATED" ]]; then
        echo "To clean up locally and start again:"
        echo "  git checkout -"
        [[ -n "$TAG_CREATED" ]] && echo "  git tag -d $tag"
        echo "  git branch -D $branch"
    fi
}

# Run a named step; command output is shown only on failure.
step() {
    local desc="$1"
    shift
    CURRENT_STEP="$desc"
    printf "  %-55s" "$desc..."
    local out
    out="$(mktemp)"
    if ! "$@" >"$out" 2>&1; then
        LAST_OUTPUT="$(cat "$out")"
        rm -f "$out"
        return 1
    fi
    rm -f "$out"
    printf "%s\n" "$MARK_OK"
    CURRENT_STEP=""
    LAST_OUTPUT=""
}

prompt_yes_no() {
    local prompt="${1:-do you want to continue?}"
    REPLY=
    while [ "$REPLY" != "y" ] && [ "$REPLY" != "n" ]; do
        printf "(release) %s [y/n] " "$prompt"
        read -r
        REPLY="${REPLY/Y/y}"
        REPLY="${REPLY/N/n}"
    done
}

fail() {
    printf "%s\n" "$1"
    return 1
}

cd "$(dirname "$0")"
branch="release/v$version"
tag="v$version"
pr_title="chore: bump version to $version"
pr_body=""

check_tools() {
    local tool
    for tool in git gh; do
        command -v "$tool" >/dev/null || { fail "$tool is required"; return 1; }
    done
}

check_open_release_prs() {
    local open_bumps
    open_bumps="$(gh pr list --state open --json number,title,headRefName \
        --jq '.[] | select(.headRefName | startswith("release/")) | "#\(.number) \(.title) (\(.headRefName))"')"
    if [[ -n "$open_bumps" && "${ALLOW_OPEN_RELEASE_PRS:-}" != "1" ]]; then
        fail "Unmerged release PRs exist, merge them first (or set ALLOW_OPEN_RELEASE_PRS=1):
$open_bumps" || return 1
    fi
}

check_clean_state() {
    [[ -z "$(git status --porcelain --ignore-submodules)" ]] || { fail "Working tree is not clean"; return 1; }
    if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
        fail "Tag $tag already exists" || return 1
    fi
    if git rev-parse -q --verify "refs/heads/$branch" >/dev/null ||
        git rev-parse -q --verify "refs/remotes/origin/$branch" >/dev/null; then
        fail "Branch $branch already exists" || return 1
    fi
}

create_branch() {
    git checkout -b "$branch" origin/main || return 1
    BRANCH_CREATED=1
}

commit_bump() {
    echo -n "$version" > VERSION.txt || return 1
    git add VERSION.txt || return 1
    git commit -m "chore: bump version to $version"
}

create_tag() {
    git tag "$tag" || return 1
    TAG_CREATED=1
}

push_refs() {
    # set -e is inactive inside step(), so chain explicitly
    git push origin "$branch" || return 1
    PUSHED=1
    git push origin "$tag"
}

echo "Releasing $tag from the current tip of origin/main"
step "Checking required tools" check_tools
step "Fetching origin" git fetch --quiet origin --tags
step "Checking for open release PRs" check_open_release_prs
step "Checking working tree, tag and branch" check_clean_state

build_pr_body() {
    local last_tag line subject body num
    last_tag="$(git describe --tags --abbrev=0 origin/main 2>/dev/null || true)"
    pr_body="Release $tag."
    [[ -n "$last_tag" ]] && pr_body+=$'\n\n'"Changes since $last_tag:"
    while IFS=$'\x1f' read -r subject body; do
        subject="${subject# }"
        if [[ "$subject" =~ ^Merge\ pull\ request\ \#([0-9]+) ]]; then
            num="${BASH_REMATCH[1]}"
            body="${body# }"
            [[ -n "$body" ]] || body="$(gh pr view "$num" --json title --jq .title 2>/dev/null || true)"
            line="- ${body:-$subject} (#$num)"
        else
            line="- $subject"
        fi
        pr_body+=$'\n'"$line"
    done < <(git log --first-parent --format='%s%x1f%b%x1e' ${last_tag:+"$last_tag..origin/main"} | tr '\n' ' ' | tr '\036' '\n' | sed 's/ *$//')
}
build_pr_body

echo ""
echo "(release) The pull request to main will look like this:"
echo "  Title: $pr_title"
echo "  Description:"
if [[ -n "$pr_body" ]]; then
    echo "$pr_body" | sed 's/^/    /'
else
    echo "    (empty)"
fi
echo "  Files: VERSION.txt ($(git show origin/main:VERSION.txt) -> $version)"
if [[ -z "$dry_run" ]]; then
    prompt_yes_no "Does the PR look ok? Nothing has been created yet; continuing creates the branch, tag and release."
    if [ "$REPLY" != "y" ]; then
        echo "(release) Aborted, nothing created."
        exit 1
    fi
fi
if [[ -n "$dry_run" ]]; then
    echo ""
    echo "(release) Dry run, nothing created. Would have:"
    echo "  - created branch $branch from origin/main and committed VERSION.txt = $version"
    echo "  - created tag $tag, pushed both, created the GitHub release and the PR to main"
    exit 0
fi
step "Creating branch $branch" create_branch
step "Committing version bump" commit_bump

echo ""
echo "(release) About to create tag $tag on:"
git --no-pager log -1 --stat --format='%h %s' "$branch"
prompt_yes_no "Create tag $tag?"
if [ "$REPLY" != "y" ]; then
    echo "(release) Aborted."
    print_cleanup
    exit 1
fi
step "Creating tag $tag" create_tag

prompt_yes_no "Push $branch and tag $tag to origin, and create the GitHub release?"
if [ "$REPLY" != "y" ]; then
    echo "(release) Nothing pushed."
    print_cleanup
    exit 1
fi
step "Pushing branch and tag" push_refs
step "Creating GitHub release $tag" gh release create "$tag" --title "$tag" --generate-notes
step "Opening pull request to main" gh pr create --base main --head "$branch" \
    --title "$pr_title" --body "$pr_body"

echo ""
echo "Done. Merge the PR into main with a merge commit after review."
