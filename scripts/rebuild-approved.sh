#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'Usage: sudo --preserve-env=NTFS_REPARSE_REBUILD_APPROVAL %s recovery.env\n' "$0" >&2
    exit 2
fi
[[ $(id -u) -eq 0 ]] || { printf 'Root is required.\n' >&2; exit 3; }
[[ ${NTFS_REPARSE_REBUILD_APPROVAL:-} == 'DATA_LOSS_IS_POSSIBLE' ]] || {
    printf 'Missing exact approval token.\n' >&2
    exit 4
}

config=$(realpath "$1")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$repo_root/scripts/load-config.sh"
load_recovery_config "$config"
"$repo_root/scripts/validate-target.sh" "$config"

required=(EXPECTED_KEYS BACKUP_IMAGE BACKUP_SHA256)
for name in "${required[@]}"; do
    [[ -n ${!name:-} ]] || { printf 'Missing %s\n' "$name" >&2; exit 5; }
done
is_positive_decimal "$EXPECTED_KEYS"
[[ $BACKUP_SHA256 =~ ^[[:xdigit:]]{64}$ ]]

disk=$(realpath "$DISK_BY_ID")
partition=$(realpath "$PARTITION_BY_ID")
audit_bin=$(realpath "${AUDIT_BIN:-$repo_root/build/ntfs-r-audit}")
rebuild_bin=$(realpath "${REBUILD_BIN:-$repo_root/build/ntfs-r-rebuild}")
artifact_dir=$(realpath -m "${ARTIFACT_DIR:-$repo_root/artifacts}")
mkdir -p "$artifact_dir"
timestamp=$(date +%Y%m%d-%H%M%S)
pre="$artifact_dir/pre-$timestamp.log"
operation="$artifact_dir/rebuild-$timestamp.log"
post="$artifact_dir/post-$timestamp.log"

lock_read_only() {
    sync || true
    blockdev --setro "$partition" 2>/dev/null || true
    blockdev --setro "$disk" 2>/dev/null || true
}
trap lock_read_only EXIT

blockdev --setro "$disk"
blockdev --setro "$partition"
[[ $(sha256sum "$BACKUP_IMAGE" | awk '{print $1}') == "${BACKUP_SHA256,,}" ]]

"$audit_bin" "$partition" | tee "$pre"
grep -q '^AUDIT_SAFE_TO_REBUILD=YES$' "$pre"
actual_keys=$(awk -F= '/^SUMMARY_TOTAL=/{print $2}' "$pre")
is_positive_decimal "$actual_keys"
[[ $actual_keys == "$EXPECTED_KEYS" ]]

printf '%s\n' '=== APPROVED METADATA WRITE WINDOW OPEN ==='
blockdev --setrw "$disk"
blockdev --setrw "$partition"
[[ $(blockdev --getro "$disk") == 0 ]]
[[ $(blockdev --getro "$partition") == 0 ]]
# Close the validation-to-write gap as far as a shell wrapper can: re-check
# identity and mount state immediately before invoking the writer.
"$repo_root/scripts/validate-target.sh" "$config"

set +e
"$rebuild_bin" "$PARTITION_BY_ID" --apply-device "$EXPECTED_KEYS" \
    "$EXPECTED_PARTITION_BYTES" DATA_LOSS_IS_POSSIBLE | tee "$operation"
rebuild_exit=${PIPESTATUS[0]}
sync "$partition"
lock_read_only
set -e
[[ $rebuild_exit -eq 0 ]]
printf '%s\n' '=== WRITE WINDOW CLOSED; READ-ONLY RESTORED ==='

"$audit_bin" "$partition" | tee "$post"
grep -q '^AUDIT_SAFE_TO_REBUILD=YES$' "$post"
grep '^ENTRY=' "$pre" >"$artifact_dir/pre-entries-$timestamp.log"
grep '^ENTRY=' "$post" >"$artifact_dir/post-entries-$timestamp.log"
cmp "$artifact_dir/pre-entries-$timestamp.log" "$artifact_dir/post-entries-$timestamp.log"
printf 'KEY_SET_IDENTICAL=YES\n'
printf 'REBUILD_RESULT=SUCCESS\n'
