#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'Usage: %s /path/to/configured-and-built/ntfsprogs-plus\n' "$0" >&2
    exit 2
fi

source_root=$(realpath "$1")
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="$repo_root/build"

[[ -f $source_root/config.h ]]
[[ -d $source_root/include ]]
[[ -f $source_root/libntfs/.libs/libntfs.so || -f $source_root/libntfs/.libs/libntfs.a ]]
mkdir -p "$build_dir"

common=(
    -DHAVE_CONFIG_H
    -I"$source_root"
    -I"$source_root/include"
    -O2 -Wall -Wextra -Werror -Wno-address-of-packed-member
    -L"$source_root/libntfs/.libs"
    -Wl,-rpath,"$source_root/libntfs/.libs"
    -lntfs
)

gcc "$repo_root/src/ntfs-r-audit.c" "${common[@]}" -o "$build_dir/ntfs-r-audit"
gcc "$repo_root/src/ntfs-r-rebuild.c" "${common[@]}" -o "$build_dir/ntfs-r-rebuild"
sha256sum "$build_dir/ntfs-r-audit" "$build_dir/ntfs-r-rebuild"
