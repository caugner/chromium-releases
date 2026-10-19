#!/usr/bin/env bash
set -euo pipefail

releases_ref="${1:-releases}"
git rev-parse --verify "$releases_ref^{commit}" >/dev/null

while IFS=$'\t' read -r commit subject; do
  if [[ "$subject" =~ ^release:\ Chromium\ ([0-9]+)\. ]]; then
    milestone=${BASH_REMATCH[1]}
    if git show-ref --verify --quiet "refs/tags/v$milestone"; then
      continue
    fi
    git tag "v$milestone" "$commit"
    echo "Created v$milestone at $commit"
  fi
done < <(git log --reverse --format='%H%x09%s' "$releases_ref")
