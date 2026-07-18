#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

# Parse recovery.env as data.  Never execute it as shell code, because the
# write wrapper runs with root privileges.
load_recovery_config() {
    if [[ $# -ne 1 || ! -f $1 ]]; then
        printf 'Config must be an existing regular file.\n' >&2
        return 2
    fi

    local config_file=$1
    local line key raw value
    local line_number=0
    local -A seen=()

    while IFS= read -r line || [[ -n $line ]]; do
        ((line_number += 1))
        line=${line%$'\r'}
        [[ $line =~ ^[[:space:]]*$ || $line =~ ^[[:space:]]*# ]] && continue

        if [[ ! $line =~ ^([A-Z][A-Z0-9_]*)=(.*)$ ]]; then
            printf 'Invalid config syntax at line %d.\n' "$line_number" >&2
            return 3
        fi
        key=${BASH_REMATCH[1]}
        raw=${BASH_REMATCH[2]}

        case $key in
            DISK_BY_ID|PARTITION_BY_ID|EXPECTED_MODEL|EXPECTED_SERIAL|\
            EXPECTED_DISK_BYTES|EXPECTED_PARTITION_BYTES|EXPECTED_PARTUUID|\
            EXPECTED_KEYS|BACKUP_IMAGE|BACKUP_SHA256|AUDIT_BIN|REBUILD_BIN|\
            ARTIFACT_DIR)
                ;;
            *)
                printf 'Unknown config key at line %d: %s\n' \
                    "$line_number" "$key" >&2
                return 3
                ;;
        esac
        if [[ -v seen[$key] ]]; then
            printf 'Duplicate config key at line %d: %s\n' \
                "$line_number" "$key" >&2
            return 3
        fi
        seen[$key]=1

        if [[ $raw =~ ^\'(.*)\'$ ]]; then
            value=${BASH_REMATCH[1]}
            [[ $value != *"'"* ]] || {
                printf 'Single quotes inside a value are unsupported at line %d.\n' \
                    "$line_number" >&2
                return 3
            }
        elif [[ $raw =~ ^[A-Za-z0-9_./:@%+,=-]*$ ]]; then
            value=$raw
        else
            printf 'Unsafe config value at line %d; use one pair of single quotes.\n' \
                "$line_number" >&2
            return 3
        fi

        printf -v "$key" '%s' "$value"
    done <"$config_file"
}

# Keep values out of Bash arithmetic evaluation.  Arithmetic operands are
# expressions, not plain strings, and may execute command substitutions hidden
# in array subscripts when they come from an untrusted configuration value.
is_positive_decimal() {
    [[ $# -eq 1 && $1 =~ ^[1-9][0-9]*$ ]]
}
