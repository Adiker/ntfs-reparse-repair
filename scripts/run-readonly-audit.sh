#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'Usage: sudo %s recovery.env\n' "$0" >&2
    exit 2
fi
[[ $(id -u) -eq 0 ]] || { printf 'Root is required.\n' >&2; exit 3; }

config=$(realpath "$1")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$repo_root/scripts/load-config.sh"
load_recovery_config "$config"
"$repo_root/scripts/validate-target.sh" "$config"

disk=$(realpath "$DISK_BY_ID")
partition=$(realpath "$PARTITION_BY_ID")
audit_bin=$(realpath "${AUDIT_BIN:-$repo_root/build/ntfs-r-audit}")
artifact_dir=$(realpath -m "${ARTIFACT_DIR:-$repo_root/artifacts}")
mkdir -p "$artifact_dir"

blockdev --setro "$disk"
blockdev --setro "$partition"
[[ $(blockdev --getro "$disk") -eq 1 ]]
[[ $(blockdev --getro "$partition") -eq 1 ]]

output="$artifact_dir/audit-$(date +%Y%m%d-%H%M%S).log"
set +e
"$audit_bin" "$partition" | tee "$output"
audit_exit=${PIPESTATUS[0]}
set -e
printf 'AUDIT_EXIT=%s\n' "$audit_exit" | tee -a "$output"
[[ $audit_exit -eq 0 ]]
grep -q '^AUDIT_SAFE_TO_REBUILD=YES$' "$output"
printf 'READONLY_AUDIT_RESULT=SUCCESS\n'
