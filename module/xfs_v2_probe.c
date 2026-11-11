/* SPDX-License-Identifier: GPL-3.0-or-later
 * Common V2 raw-device probe using the canonical native XFS geometry and
 * checksum helpers. No path, fd, write, or staging operations are exposed.
 * V1 native workers remain available until their V2 replacement qualifies.
 */
#include <infiltratr/filesystem_module_v2.h>
#include "xfs_native.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
         ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static int probe(const IfsFsHostRuntimeV2 *h, uint32_t *variant)
{
    uint8_t *sb;
    uint32_t span, sector, block, agblocks, agcount, off, version;
    uint64_t dblocks;
    int result=IFS_FS_CORRUPT;
    if (!variant || !ifs_fs_host_v2_valid(h)) return IFS_FS_INVALID;
    sector=h->logical_sector_bytes;
    if (h->volume_bytes<512u || sector>h->volume_bytes)
        return IFS_FS_NOT_FOUND;
    if (h->maximum_transfer_bytes<sector) return IFS_FS_INVALID;
    span=sector>512u?sector:512u;
    if (span>h->volume_bytes) return IFS_FS_NOT_FOUND;
    sb=h->allocate(h->context,span);
    if (!sb) return IFS_FS_NO_MEMORY;
    for(off=0u;off<span;off+=sector)
        if(h->read_at(h->context,off,sb+off,sector)) {
            result=IFS_FS_IO_ERROR;
            goto done;
        }
    if(memcmp(sb,"XFSB",4)) {
        result=IFS_FS_NOT_FOUND;
        goto done;
    }
    block=xfs_be32(sb+4);
    dblocks=xfs_be64(sb+8);
    agblocks=xfs_be32(sb+84);
    agcount=xfs_be32(sb+88);
    version=(uint32_t)(xfs_be16(sb+100)&0x000fu);
    if(block<512u || block>65536u || (block&(block-1u)) ||
       block<sector || !dblocks || dblocks>h->volume_bytes/block ||
       !agblocks || !agcount || dblocks<=(uint64_t)agblocks*(agcount-1u) ||
       dblocks>(uint64_t)agblocks*agcount ||
       (version!=4u && version!=5u) ||
       xfs_be16(sb+102)<512u || xfs_be16(sb+102)>block ||
       xfs_be16(sb+104)<256u || xfs_be16(sb+104)>block ||
       (xfs_be16(sb+104)&(xfs_be16(sb+104)-1u)) ||
       xfs_be16(sb+106)!=block/xfs_be16(sb+104) ||
       sb[120]>=63u || (UINT64_C(1)<<sb[120])!=block ||
       sb[123]>=63u ||
       (UINT64_C(1)<<sb[123])!=xfs_be16(sb+106))
        goto done;
    /* XFS v5 superblocks require the canonical CRC field at byte 224. */
    if(version==5u && le32(sb+224)!=xfs_crc_field(sb,512u,224u))
        goto done;
    *variant=version;
    result=IFS_FS_OK;
done:
    h->release(h->context,sb);
    return result;
}
static const IfsFsCoreOpsV2 core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_PROBE},
    .probe=probe
};
static const IfsFsGroupHeaderV2 *query(uint32_t group,uint32_t lo,uint32_t hi)
{
    return group==IFS_FS_GROUP_CORE && lo<=IFS_FS_GROUP_ABI_V2 &&
           hi>=IFS_FS_GROUP_ABI_V2 ? &core.header:NULL;
}
static const IfsFsModuleApiV2 api={
    .abi_version=IFS_FS_COMMON_ABI_V2,
    .structure_bytes=sizeof(IfsFsModuleApiV2),
    .format_id="xfs",
    .runtime_profiles=IFS_FS_PROFILE_HOSTED,
    .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE),
    .query_group=query
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void)
{ return &api; }
