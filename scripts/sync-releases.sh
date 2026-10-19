#!/usr/bin/env bash
set -euo pipefail

readonly source_repo="${CHROMIUM_SOURCE_REPO:-https://github.com/chromium/chromium.git}"
readonly bcd_url="${CHROME_RELEASES_URL:-https://raw.githubusercontent.com/mdn/browser-compat-data/main/browsers/chrome.json}"
readonly releases_dir="${RELEASES_DIR:-releases}"
start_arg="${1:-}"
dry_run=false

filter_snapshot() {
  local snapshot=$1
  find "$snapshot" -type f \( \
    -name '*.apk' -o -name '*.bin' -o -name '*.dat' -o -name '*.data' \
    -o -name '*.db' -o -name '*.bdic' -o -name '*.crx' -o -name '*.def' \
    -o -name '*.dic' -o -name '*.dll' -o -name '*.dmg' -o -name '*.exe' \
    -o -name '*.gpg' -o -name '*.icc' -o -name '*.ivf' -o -name '*.jar' \
    -o -name '*.jpg' -o -name '*.keystore' -o -name '*.lib' -o -name '*.mp3' \
    -o -name '*.mp4' -o -name '*.ogg' -o -name '*.pdb' -o -name '*.png' \
    -o -name '*.psd' -o -name '*.raw' -o -name '*.res' -o -name '*.sqlite' \
    -o -name '*.so' -o -name '*.svg' -o -name '*.svgz' -o -name '*.tar.xz' \
    -o -name '*.tflite' -o -name '*.ttf' -o -name '*.webm' -o -name '*.webp' \
    -o -name '*.usdz' -o -name '*.yuv' -o -name '*.wasm' -o -name '*.zip' \
  \) -delete
  find "$snapshot" -type d \( -name testdata -o -path '*/test/data' \) -prune -exec rm -rf {} +
  find "$snapshot" -type f \( -name huge_trace.json -o -name 'WPT_BASE_MANIFEST*.json' \) -delete
  rm -rf \
    "$snapshot/chrome/browser/resources/tracing/tests/huge_trace.json" \
    "$snapshot/chrome/tools/test/reference_build/chrome_linux/chrome" \
    "$snapshot/ios/chrome/browser/crash_report/model/seed_corpuses/process_snapshot_dump_3" \
    "$snapshot/net/http/transport_security_state_static.json" \
    "$snapshot/testing/buildbot" \
    "$snapshot/third_party/boringssl/crypto_test_data.cc" \
    "$snapshot/third_party/sqlite/amalgamation/sqlite3.c" \
    "$snapshot/third_party/blink/web_tests/external/wpt/dom/xslt/resources/large_CDATA.xml" \
    "$snapshot/third_party/blink/web_tests/external/WPT_BASE_MANIFEST_8.json" \
    "$snapshot/third_party/webxr_test_pages/webxr-samples/proposals/opencv/opencv.js" \
    "$snapshot/tools/perf/contrib/leak_detection/data/leak_detection_003.wprgo" \
    "$snapshot/tools/metrics/histograms/enums.xml" \
    "$snapshot/tools/metrics/histograms/histograms.xml"
}

if [[ "$start_arg" == "--dry-run" ]]; then
  start_arg=""
  dry_run=true
elif [[ "${2:-}" == "--dry-run" ]]; then
  dry_run=true
elif [[ -n "${2:-}" ]]; then
  echo "Usage: $0 [START_MILESTONE] [--dry-run]" >&2
  exit 2
fi

if [[ ! -d "$releases_dir/.git" ]]; then
  echo "Expected a checkout of the releases branch at '$releases_dir'." >&2
  exit 1
fi

latest_milestone=$(git -C "$releases_dir" log -1 --format=%s | sed -nE 's/^release: Chromium ([0-9]+)\..*/\1/p')
if [[ -z "$start_arg" ]]; then
  if [[ -z "$latest_milestone" ]]; then
    echo "Could not determine the latest release from the releases branch." >&2
    exit 1
  fi
  start_milestone=$((latest_milestone + 1))
else
  if [[ ! "$start_arg" =~ ^[0-9]+$ ]]; then
    echo "START_MILESTONE must be an integer." >&2
    exit 2
  fi
  start_milestone=$start_arg
fi

versions=$(curl -fsSL "$bcd_url" | jq -r '
  .browsers.chrome.releases
  | to_entries[]
  | select(.value.status == "current" or .value.status == "retired")
  | .key
' | awk -v start="$start_milestone" '$1 ~ /^[0-9]+$/ && $1 >= start' | sort -n)

if [[ -z "$versions" ]]; then
  echo "No stable Chrome milestones at or after $start_milestone."
  exit 0
fi

tag_listing=$(git ls-remote --tags --refs "$source_repo")
git -C "$releases_dir" config user.name "github-actions[bot]"
git -C "$releases_dir" config user.email "41898282+github-actions[bot]@users.noreply.github.com"

for milestone in $versions; do
  release_tag=$(printf '%s\n' "$tag_listing" | awk -v milestone="$milestone" '
    {
      tag = $2
      sub("^refs/tags/", "", tag)
      if (tag ~ ("^" milestone "\\.0\\.[0-9]+\\.[0-9]+$")) {
        split(tag, version, /[.]/)
        printf "%06d %06d %06d %s\n", version[1], version[3], version[4], tag
      }
    }
  ' | sort -n -k1,1 -k2,2 -k3,3 | tail -n 1 | awk '{ print $4 }')

  if [[ -z "$release_tag" ]]; then
    echo "No Chromium source tag found for stable milestone $milestone." >&2
    exit 1
  fi

  if git -C "$releases_dir" show-ref --verify --quiet "refs/tags/v$milestone"; then
    echo "v$milestone already exists, skipping."
    continue
  fi

  echo "Importing Chromium $release_tag"
  temp_dir=$(mktemp -d)
  trap 'rm -rf "$temp_dir"' EXIT
  git clone --quiet --depth=1 --filter=blob:none --no-checkout --branch "$release_tag" "$source_repo" "$temp_dir/source"
  source_commit=$(git -C "$temp_dir/source" rev-parse 'HEAD^{commit}')
  git -C "$releases_dir" rm -r --quiet --ignore-unmatch .
  git -C "$releases_dir" clean -fdx --quiet
  git -C "$temp_dir/source" archive "$source_commit" | tar -xf - -C "$releases_dir"

  filter_snapshot "$releases_dir"
  git -C "$releases_dir" add --all
  git -C "$releases_dir" commit -m "release: Chromium $milestone (${release_tag})"
  git -C "$releases_dir" tag "v$milestone"

  if [[ "$dry_run" == true ]]; then
    git -C "$releases_dir" log -1 --format='%h %s'
  else
    git -C "$releases_dir" push --atomic origin HEAD:refs/heads/releases "refs/tags/v$milestone"
  fi

  rm -rf "$temp_dir"
  trap - EXIT
done

