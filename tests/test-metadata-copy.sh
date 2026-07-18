#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

if [[ $# -ne 2 ]]; then
    printf 'Usage: %s /path/to/metadata-restored.img /path/to/ntfsinfo\n' "$0" >&2
    exit 2
fi

source_image=$(realpath "$1")
ntfsinfo=$(realpath "$2")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
audit="$repo_root/build/ntfs-r-audit"
rebuild="$repo_root/build/ntfs-r-rebuild"
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ntfs-r-test.XXXXXX")
test_image="$test_dir/metadata-test.img"
pre="$test_dir/pre.log"
post="$test_dir/post.log"

cleanup() {
    rm -f -- "$test_image" "$pre" "$post" \
        "$test_dir/pre-entries" "$test_dir/post-entries" "$test_dir/ntfsinfo.log"
    rmdir -- "$test_dir" 2>/dev/null || true
}
trap cleanup EXIT

cp --reflink=auto --sparse=always "$source_image" "$test_image"
"$audit" "$test_image" | tee "$pre"
grep -q '^AUDIT_SAFE_TO_REBUILD=YES$' "$pre"
expected_keys=$(awk -F= '/^SUMMARY_TOTAL=/{print $2}' "$pre")
[[ $expected_keys =~ ^[1-9][0-9]*$ ]]

"$rebuild" "$test_image" --test-image "$expected_keys"
"$audit" "$test_image" | tee "$post"
grep -q '^AUDIT_SAFE_TO_REBUILD=YES$' "$post"
grep '^ENTRY=' "$pre" >"$test_dir/pre-entries"
grep '^ENTRY=' "$post" >"$test_dir/post-entries"
cmp "$test_dir/pre-entries" "$test_dir/post-entries"

"$ntfsinfo" -f -i 26 "$test_image" >"$test_dir/ntfsinfo.log" 2>&1
! grep -Eiq 'corrupt|failed to read.*index|input/output error|unknown index attribute type' \
    "$test_dir/ntfsinfo.log"
grep -q 'Indexed Attr Type:.*REPARSE_R' "$test_dir/ntfsinfo.log"
printf 'METADATA_COPY_TEST=SUCCESS KEYS=%s\n' "$expected_keys"
