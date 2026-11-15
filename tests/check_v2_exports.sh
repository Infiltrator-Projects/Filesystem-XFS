#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
module=${1:?XFS.so required}
symbols=$(readelf -W --dyn-syms "$module" | awk '$4 == "FUNC" && $7 != "UND" && $8 ~ /^infiltrator_filesystem_/ {print $8}')
if [ "$symbols" != 'infiltrator_filesystem_api_v2' ]; then
    printf 'Expected only Common V2 export, found: %s\n' "$symbols" >&2
    exit 1
fi
readelf -p .ifsmeta "$module" | grep -Fq 'IFS-MODULE:xfs:common2' ||
    { echo 'Missing Common V2 XFS identity' >&2; exit 1; }
