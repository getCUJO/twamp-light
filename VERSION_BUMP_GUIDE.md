# Version Bump Guide

## What to Change

Only one file: **`VERSION.txt`** (contains just the version string, e.g. `1.4.1`).

CMake reads this at build time via `cmake/getversion.cmake` — no other files need updating.

## Release Workflow

Features and fixes are merged to `main` via PRs as normal. When ready to release:

```bash
version=1.4.2 # set the version first

# 1. Branch off the current tip of main
git checkout main && git pull
git checkout -b release/v$version

# 2. Bump VERSION.txt
echo -n "$version" > VERSION.txt

# 3. Commit and tag
git add VERSION.txt
git commit -m "chore: bump version to $version"
git tag "v$version"

# 4. Push and open a PR to merge back into main
git push origin release/v$version
git push origin v$version

# 5. Create a GitHub release:
gh release create "v$version" --title "v$version" --generate-notes
```

6. Merge the PR into `main`.

```
main: ── feature PRs ── fix PRs ──────────────── merge PR ───▶
                                  \                         /
release/v<VERSION>:                └── bump VERSION.txt ──┘
                                        (tag: v<VERSION>)
```

## Conventions

- **Branch**: `release/v<version>`
- **Tag**: `v<version>`
- **Commit message**: `chore: bump version to <version>`
- **Versioning**: Semantic Versioning (`MAJOR.MINOR.PATCH`)

