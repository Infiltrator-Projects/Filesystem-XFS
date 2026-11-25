/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INFILTRATOR_XFS_READER_H
#define INFILTRATOR_XFS_READER_H
#include <infiltratr/filesystem_module_v2.h>

typedef struct {
    IfsFsHostRuntimeV2 host;
    uint64_t blocks, root, log_start;
    uint32_t block_bytes, ag_blocks, log_blocks;
    uint16_t inode_bytes, sector_bytes;
    uint8_t ag_log, inop_log, dir_log, v5, ftype, sparse;
    uint8_t uuid[16], metadata_uuid[16];
    uint8_t label[12];
} XfsReader;

int xfs_reader_configure(XfsReader *, const IfsFsHostRuntimeV2 *, const uint8_t *);
int xfs_reader_check_root(const XfsReader *);
int xfs_reader_metadata(const XfsReader *r,const char *path,IfsFsMetadataV2 *out);
int xfs_reader_stat(const XfsReader *, const char *, IfsFsEntryV2 *);
int xfs_reader_list(const XfsReader *, const char *, uint32_t, IfsFsEntryV2 *);
int xfs_reader_read(const XfsReader *, const char *, uint64_t, void *, uint32_t);
int xfs_reader_map(const XfsReader *,const char *,uint64_t,IfsFsExtentV2 *,uint32_t,uint32_t *);
#endif
