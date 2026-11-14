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

static int inspect(const IfsFsHostRuntimeV2 *h, uint32_t *variant,
                   IfsFsVolumeInfoV2 *volume)
{
    uint8_t *sb;
    uint32_t span, sector, block, agblocks, agcount, off, version;
    uint64_t dblocks,free_blocks;
    int result=IFS_FS_CORRUPT;
    if (!variant)return IFS_FS_INVALID;
    *variant=0u;
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
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
    free_blocks=xfs_be64(sb+144);
    agblocks=xfs_be32(sb+84);
    agcount=xfs_be32(sb+88);
    version=(uint32_t)(xfs_be16(sb+100)&0x000fu);
    if(block<512u || block>65536u || (block&(block-1u)) ||
       block<sector || !dblocks || free_blocks>dblocks ||
       dblocks>h->volume_bytes/block ||
       !agblocks || !agcount || dblocks<=(uint64_t)agblocks*(agcount-1u) ||
       dblocks>(uint64_t)agblocks*agcount ||
       (version!=4u && version!=5u) ||
       xfs_be16(sb+102)<512u || xfs_be16(sb+102)>block ||
       (xfs_be16(sb+102)&(xfs_be16(sb+102)-1u)) ||
       xfs_be16(sb+104)<256u || xfs_be16(sb+104)>block ||
       (xfs_be16(sb+104)&(xfs_be16(sb+104)-1u)) ||
       xfs_be16(sb+106)!=block/xfs_be16(sb+104) ||
       sb[120]>=63u || (UINT64_C(1)<<sb[120])!=block ||
       sb[123]>=63u ||
       (UINT64_C(1)<<sb[123])!=xfs_be16(sb+106))
        goto done;
    /* XFS v5 CRC covers the entire on-disk superblock sector, which
     * can be 4096 bytes rather than the first 512-byte identity record. */
    if(version==5u) {
        const uint32_t disk_sector=xfs_be16(sb+102);
        if(disk_sector>span) {
            uint8_t *expanded;
            if(disk_sector>h->volume_bytes) goto done;
            expanded=h->allocate(h->context,disk_sector);
            if(!expanded) {
                result=IFS_FS_NO_MEMORY;
                goto done;
            }
            for(off=0u;off<disk_sector;off+=sector)
                if(h->read_at(h->context,off,expanded+off,sector)) {
                    h->release(h->context,expanded);
                    result=IFS_FS_IO_ERROR;
                    goto done;
                }
            h->release(h->context,sb);
            sb=expanded;
            span=disk_sector;
        }
        if(le32(sb+224)!=xfs_crc_field(sb,disk_sector,224u))
            goto done;
    }
    *variant=version;
    if(volume) {
        *volume=(IfsFsVolumeInfoV2){
            .structure_bytes=sizeof(*volume),.variant=version,
            .max_name_bytes=255u,.block_bytes=block,
            .total_bytes=dblocks*block,
            .free_bytes=free_blocks*block
        };
    }
    result=IFS_FS_OK;
done:
    h->release(h->context,sb);
    return result;
}
static int probe(const IfsFsHostRuntimeV2 *h,uint32_t *variant)
{ return inspect(h,variant,NULL); }

/* Read-only Common V2 session: the canonical XFS metadata decoder validated
 * feature flags and geometry; this reports the superblock's RECORDED free
 * block count, not an independently scanned block bitmap. The host alone
 * owns media I/O and writable access is always rejected. */
static struct {
    uintptr_t generation,active;
    IfsFsVolumeInfoV2 info;
} session;
static int mounted(void *handle)
{ return session.active && (uintptr_t)handle==session.active; }
static int mount_volume(const IfsFsHostRuntimeV2 *h, void **volume,
                        IfsFsVolumeInfoV2 *info)
{
    IfsFsVolumeInfoV2 measured={0};
    uint32_t variant=0u;
    int rc;
    if(!volume || !info || info->structure_bytes<sizeof(*info))
        return IFS_FS_INVALID;
    *volume=NULL;
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if(!(h->capabilities & IFS_FS_HOST_READ_ONLY))return IFS_FS_ACCESS;
    if(session.active || session.generation==UINTPTR_MAX)return IFS_FS_BUSY;
    rc=inspect(h,&variant,&measured);
    if(rc!=IFS_FS_OK)return rc;
    session.info=measured;
    session.active=++session.generation;
    *info=measured;
    *volume=(void *)session.active;
    return IFS_FS_OK;
}
static int unmount_volume(void *volume)
{
    if(!mounted(volume))return IFS_FS_INVALID;
    session.active=0u;
    session.info=(IfsFsVolumeInfoV2){0};
    return IFS_FS_OK;
}
static int query_volume(void *volume,IfsFsVolumeInfoV2 *info)
{
    if(!mounted(volume) || !info ||
       info->structure_bytes<sizeof(*info))return IFS_FS_INVALID;
    *info=session.info;
    return IFS_FS_OK;
}
static const IfsFsVolumeOpsV2 volume_ops={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsVolumeOpsV2),
             IFS_FS_GROUP_VOLUME,0u,IFS_FS_VOLUME_QUERY},
    .query=query_volume
};
static const IfsFsCoreOpsV2 core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT},
    .probe=probe,
    .mount=mount_volume,
    .unmount=unmount_volume
};
static const IfsFsGroupHeaderV2 *query(uint32_t group,uint32_t lo,uint32_t hi)
{
    if(lo>IFS_FS_GROUP_ABI_V2 || hi<IFS_FS_GROUP_ABI_V2)return NULL;
    if(group==IFS_FS_GROUP_CORE)return &core.header;
    if(group==IFS_FS_GROUP_VOLUME)return &volume_ops.header;
    return NULL;
}
static const IfsFsModuleApiV2 api={
    .abi_version=IFS_FS_COMMON_ABI_V2,
    .structure_bytes=sizeof(IfsFsModuleApiV2),
    .format_id="xfs",
    .runtime_profiles=IFS_FS_PROFILE_HOSTED,
    .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
                IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),
    .query_group=query
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void)
{ return &api; }
