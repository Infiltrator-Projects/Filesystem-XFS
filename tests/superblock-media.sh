#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Format only disposable regular files. xfs_db rewrites native CRCs, providing an
# independent oracle for checksum-valid but contradictory geometry fields.
set -euo pipefail
exercise=$(realpath "${1:?test executable}")
module=$(realpath "${2:?packaged XFS.so}")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
for disk_sector in 512 4096; do
    truncate -s 320M clean.img
    mkfs.xfs -q -f -s "size=$disk_sector" -b size=4096 clean.img
    sha256sum clean.img > clean.sha256
    for host_sector in 512 4096 8192; do
        "$exercise" "$module" clean.img "$host_sector" clean
    done
    for field in 'sectlog 13' 'inodelog 11' 'agblklog 3' 'dirblklog 5' 'imax_pct 101'; do
        cp --sparse=always --reflink=auto clean.img corrupt.img
        xfs_db -x -c 'sb 0' -c "write -d $field" corrupt.img
        sha256sum corrupt.img > corrupt.sha256
        "$exercise" "$module" corrupt.img 4096 geometry "$disk_sector"
        sha256sum -c corrupt.sha256
    done
    cp --sparse=always --reflink=auto clean.img corrupt.img
    printf '\001' | dd of=corrupt.img bs=1 seek=$((disk_sector-1)) conv=notrunc status=none
    sha256sum corrupt.img > corrupt.sha256
    "$exercise" "$module" corrupt.img 512 corrupt
    sha256sum -c clean.sha256 corrupt.sha256
done
