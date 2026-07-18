#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'Usage: %s recovery.env\n' "$0" >&2
    exit 2
fi
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$repo_root/scripts/load-config.sh"
load_recovery_config "$(realpath "$1")"

required=(DISK_BY_ID PARTITION_BY_ID EXPECTED_MODEL EXPECTED_SERIAL
    EXPECTED_DISK_BYTES EXPECTED_PARTITION_BYTES EXPECTED_PARTUUID)
for name in "${required[@]}"; do
    [[ -n ${!name:-} ]] || { printf 'Missing %s\n' "$name" >&2; exit 3; }
done
is_positive_decimal "$EXPECTED_DISK_BYTES" || {
    printf 'EXPECTED_DISK_BYTES must be a positive decimal integer.\n' >&2
    exit 3
}
is_positive_decimal "$EXPECTED_PARTITION_BYTES" || {
    printf 'EXPECTED_PARTITION_BYTES must be a positive decimal integer.\n' >&2
    exit 3
}

disk=$(realpath "$DISK_BY_ID")
partition=$(realpath "$PARTITION_BY_ID")
[[ -b $disk && -b $partition ]]
disk_name=$(basename "$disk")
partition_name=$(basename "$partition")
[[ $(lsblk -nro PKNAME "$partition") == "$disk_name" ]]
[[ $(tr -d ' \r\n\t' <"/sys/class/block/$disk_name/device/model") == "$EXPECTED_MODEL" ]]
[[ $(lsblk -dn -o SERIAL "$disk" | tr -d ' \r\n\t') == "$EXPECTED_SERIAL" ]]
[[ $(blockdev --getsize64 "$disk") == "$EXPECTED_DISK_BYTES" ]]
[[ $(blockdev --getsize64 "$partition") == "$EXPECTED_PARTITION_BYTES" ]]
actual_partuuid=$(blkid -s PARTUUID -o value "$partition" | tr '[:upper:]' '[:lower:]')
[[ $actual_partuuid == "${EXPECTED_PARTUUID,,}" ]]
if findmnt -rn -S "$disk" >/dev/null || findmnt -rn -S "$partition" >/dev/null; then
    printf 'Refusing mounted target.\n' >&2
    exit 4
fi

printf 'VALIDATED_DISK=%s\n' "$disk"
printf 'VALIDATED_PARTITION=%s\n' "$partition"
printf 'VALIDATED_PARTITION_NAME=%s\n' "$partition_name"
printf 'VALIDATED_MODEL=%s\n' "$EXPECTED_MODEL"
printf 'VALIDATED_SERIAL=%s\n' "$EXPECTED_SERIAL"
printf 'VALIDATED_PARTUUID=%s\n' "$actual_partuuid"
