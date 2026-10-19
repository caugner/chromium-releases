# Chromium Releases

This repository keeps a searchable history of Chromium source snapshots for stable Chrome releases.

## Branches

- `main` contains this README, the import scripts, and the workflow that maintains the release history.
- `releases` is an orphan history with one snapshot commit per stable Chrome milestone. Each commit is tagged `v<major>` and contains the Chromium source tree for that release.

Search the snapshots without checking out `releases`:

```sh
git grep 'pattern' releases
git log -S'pattern' releases
git blame releases -- path/to/file
```

## Updating the history

The scheduled workflow checks Chrome release metadata, finds the matching version tag in the Chromium source mirror, imports each missing stable milestone, and pushes its commit and `v<major>` tag to `releases`. The workflow can also be run manually with a starting milestone or in dry-run mode. Dry runs create commits and tags in the temporary runner checkout but do not push them.

For a local import, check out both this branch and `releases`, then run:

```sh
scripts/sync-releases.sh [START_MILESTONE] [--dry-run]
```

The importer requires `git`, `jq`, `curl`, `tar`, and `find`. It replaces the checked-out snapshot tree in place to reduce temporary disk use. Chromium source snapshots are large, so imports need substantial disk space and network bandwidth.

## Migrating the existing history

Before changing the repository's default branch, publish the current snapshot history as `releases` and backfill its missing milestone tags:

```sh
git push origin origin/main:refs/heads/releases
git fetch origin releases:refs/heads/releases
scripts/backfill-release-tags.sh releases
git push origin --tags
```

The current history has release commits through milestone 155, while its existing tags stop at milestone 131. Then publish this control branch as `main`. The new `main` and `releases` histories must remain unrelated.
