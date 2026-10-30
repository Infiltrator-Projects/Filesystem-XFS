/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_xfs_module_v1.h"
#include <stdio.h>
#include <string.h>
int main(void) {
 const IfsXfsModuleV1 *a=infiltrator_filesystem_xfs_v1();
 if(!a || a->abi_version!=IFS_XFS_MODULE_V1 ||
    a->structure_bytes<sizeof(*a) || !a->format_id ||
    strcmp(a->format_id,"xfs") || a->capabilities!=IFS_XFS_CAP_RUN ||
    !a->run) { fputs("XFS.so ABI invalid\n",stderr);return 1; }
 return 0;
}
