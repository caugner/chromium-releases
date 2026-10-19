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

The scheduled workflow checks Chrome release metadata, finds the matching version tag in the Chromium source mirror, imports each missing stable milestone, and pushes its commit and `v<major>` tag to `releases`. Commit author and committer timestamps use the stable release date at noon UTC, and the date is included in the commit message. Release tags are annotated and use the same date for their tagger timestamp. The workflow can also be run manually with a starting milestone or in dry-run mode. Dry runs create commits and tags in the temporary runner checkout but do not push them.

For a local import, check out both this branch and `releases`, then run:

```sh
scripts/sync-releases.sh [START_MILESTONE] [--dry-run]
```

The importer requires `git`, `jq`, `curl`, `tar`, and `find`. It replaces the checked-out snapshot tree in place to reduce temporary disk use. Chromium source snapshots are large, so imports need substantial disk space and network bandwidth.
