#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$repo_root/scripts/load-config.sh"

test_dir=$(mktemp -d "${TMPDIR:-/tmp}/ntfs-r-config-test.XXXXXX")
config="$test_dir/recovery.env"
marker="$test_dir/must-not-exist"

cleanup() {
    rm -f -- "$config" "$marker"
    rmdir -- "$test_dir" 2>/dev/null || true
}
trap cleanup EXIT

printf '%s\n' \
    "DISK_BY_ID='/dev/disk/by-id/example'" \
    "EXPECTED_MODEL='literal-\$(touch $marker)'" \
    "EXPECTED_DISK_BYTES='x[\$(touch $marker)]'" >"$config"

load_recovery_config "$config"
[[ $DISK_BY_ID == /dev/disk/by-id/example ]]
[[ $EXPECTED_MODEL == "literal-\$(touch $marker)" ]]
[[ $EXPECTED_DISK_BYTES == "x[\$(touch $marker)]" ]]
[[ ! -e $marker ]]
if is_positive_decimal "$EXPECTED_DISK_BYTES"; then
    printf 'Numeric validator accepted an arithmetic expression.\n' >&2
    exit 1
fi
[[ ! -e $marker ]]
is_positive_decimal 123
! is_positive_decimal 0
! is_positive_decimal 12x

printf '%s\n' "UNSUPPORTED_KEY='value'" >"$config"
if load_recovery_config "$config" 2>/dev/null; then
    printf 'Parser accepted an unknown key.\n' >&2
    exit 1
fi

printf 'CONFIG_PARSER_TEST=SUCCESS\n'
