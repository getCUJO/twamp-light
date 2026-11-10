# Version Bump Guide

## What to Change

Only one file: **`VERSION.txt`** (contains just the version string, e.g. `1.4.1`).

CMake reads this at build time via `cmake/getversion.cmake` — no other files need updating.

## Making a Release

Features and fixes are merged to `main` via PRs as normal. When ready to release:

1. Go to **Actions → release → Run workflow**, keep the branch at `main`, and
   enter the version (e.g. `1.4.4`). Tick **dry run** first to only run the
   checks and see the PR description in the run summary.
2. The workflow runs `release.sh`, which:
   - stops if a release PR (`release/*`) is still open, or if the tag or branch exists
   - creates `release/v<version>` from `main` and commits the `VERSION.txt` bump
   - tags it `v<version>` and pushes the branch and tag
   - creates the GitHub release with generated notes
   - opens the PR back to `main`
3. It then starts the `release-debs` workflow, which attaches the `.deb`
   package to the release.
4. Review the PR and merge it into `main` using a **merge commit** (not squash
   or rebase). The release tag points at the commit on the release branch, and
   squashing or rebasing would leave that commit, and so the tag, outside
   `main`'s history.

```
main: ── feature PRs ── fix PRs ──────────────── merge PR ───▶
                                  \                         /
release/v<VERSION>:                └── bump VERSION.txt ──┘
                                        (tag: v<VERSION>)
```

The workflow needs **Settings → Actions → General → Allow GitHub Actions to
create and approve pull requests** to be enabled. CI does not run on the PR it
opens, since the PR only changes `VERSION.txt`.

### From a terminal

`./release.sh <version>` does step 2 from your machine, asking for
confirmation before creating the branch, the tag and pushing. Use
`./release.sh --dry-run <version>` to only run the checks. Publishing the
release from your machine triggers `release-debs` on its own.

## Conventions

- **Branch**: `release/v<version>`
- **Tag**: `v<version>`
- **Commit message**: `chore: bump version to <version>`
- **Versioning**: Semantic Versioning (`MAJOR.MINOR.PATCH`)
