/* SPDX-License-Identifier: GPL-3.0-or-later
 * Common V2 raw-device probe using the canonical native XFS geometry and
 * checksum helpers. No path, fd, or staging operations are exposed; native hosted format uses Common V2.
 * Historical analysis and mutation code is compiled only for regression tests.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <infiltratr/filesystem_ro_sessions_v2.h>
/* Strict Common V2 identity is retained even with native LTO and GC. */
__attribute__((used, retain, section(".ifsmeta"))) static const char ifs_xfs_v2_identity[] =
    "IFS-MODULE:xfs:common2";

#include "xfs_disk.h"
#include "xfs_reader.h"
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int inspect(const IfsFsHostRuntimeV2 *h, uint32_t *variant, IfsFsVolumeInfoV2 *volume,
                   XfsReader *reader)
{
    uint8_t *sb;
    uint32_t span, sector, block, agblocks, agcount, off, version;
    uint64_t dblocks, free_blocks;
    int result = IFS_FS_CORRUPT;
    if (!variant)
        return IFS_FS_INVALID;
    *variant = 0u;
    if (!ifs_fs_host_v2_valid(h))
        return IFS_FS_INVALID;
    sector = h->logical_sector_bytes;
    if (h->volume_bytes < 512u || sector > h->volume_bytes)
        return IFS_FS_NOT_FOUND;
    if (h->maximum_transfer_bytes < sector)
        return IFS_FS_INVALID;
    span = sector > 512u ? sector : 512u;
    if (span > h->volume_bytes)
        return IFS_FS_NOT_FOUND;
    sb = h->allocate(h->context, span);
    if (!sb)
        return IFS_FS_NO_MEMORY;
    for (off = 0u; off < span; off += sector)
        if (h->read_at(h->context, off, sb + off, sector)) {
            result = IFS_FS_IO_ERROR;
            goto done;
        }
    if (memcmp(sb, "XFSB", 4)) {
        result = IFS_FS_NOT_FOUND;
        goto done;
    }
    block = xfs_be32(sb + 4);
    dblocks = xfs_be64(sb + 8);
    free_blocks = xfs_be64(sb + 144);
    agblocks = xfs_be32(sb + 84);
    agcount = xfs_be32(sb + 88);
    version = (uint32_t)(xfs_be16(sb + 100) & 0x000fu);
    const uint16_t disk_sector = xfs_be16(sb + 102);
    const uint16_t inode_size = xfs_be16(sb + 104);
    unsigned ag_shift = 0;
    if (agblocks)
        for (uint32_t rest = agblocks - 1; rest; rest >>= 1)
            ++ag_shift;
    if (block < 512u || block > 65536u || (block & (block - 1u)) || !dblocks ||
        free_blocks > dblocks || dblocks > h->volume_bytes / block || !agblocks || !agcount ||
        dblocks <= (uint64_t)agblocks * (agcount - 1u) || dblocks > (uint64_t)agblocks * agcount ||
        (version != 4u && version != 5u) || sb[121] < 9u || sb[121] > 15u ||
        disk_sector != (1u << sb[121]) || disk_sector > block || sb[122] < 8u || sb[122] > 11u ||
        inode_size != (1u << sb[122]) || inode_size > block || sb[124] != ag_shift ||
        sb[127] > 100u || sb[120] + sb[192] > 16u ||
        xfs_be16(sb + 106) != block / xfs_be16(sb + 104) || sb[120] >= 63u ||
        (UINT64_C(1) << sb[120]) != block || sb[123] >= 63u ||
        (UINT64_C(1) << sb[123]) != xfs_be16(sb + 106))
        goto done;
    /* XFS v5 CRC covers the entire on-disk superblock sector, which
     * can be 4096 bytes rather than the first 512-byte identity record. */
    if (version == 5u) {
        if (disk_sector > span) {
            uint8_t *expanded;
            if (disk_sector > h->volume_bytes)
                goto done;
            expanded = h->allocate(h->context, disk_sector);
            if (!expanded) {
                result = IFS_FS_NO_MEMORY;
                goto done;
            }
            memcpy(expanded, sb, span);
            for (off = span; off < disk_sector; off += sector)
                if (h->read_at(h->context, off, expanded + off, sector)) {
                    h->release(h->context, expanded);
                    result = IFS_FS_IO_ERROR;
                    goto done;
                }
            h->release(h->context, sb);
            sb = expanded;
        }
        if (le32(sb + 224) != xfs_crc_field(sb, disk_sector, 224u))
            goto done;
        /* Do not accept a checksum-valid filesystem requiring repair or
         * using incompatible metadata formats the current reader does not
         * interpret. Retain known v5 FTYPE/SPINODES/META_UUID/BIGTIME/
         * NREXT64 superblocks for read-only metadata recognition only. */
        const uint32_t incompat = xfs_be32(sb + 216u);
        /* A log-incompatible feature must not be reported as a fully
         * supported volume by PROBE when the namespace reader cannot
         * replay it or interpret the associated metadata. */
        if ((incompat & ~UINT32_C(0x2f)) || (incompat & UINT32_C(0x10)) ||
            xfs_be32(sb + 220u)) {
            result = IFS_FS_UNSUPPORTED;
            goto done;
        }
    }
    /* The on-disk INPROGRESS bit is set while formatting and must never
     * identify an incomplete volume as mountable. */
    if (sb[126u]) {
        result = IFS_FS_UNSUPPORTED;
        goto done;
    }
    if (reader) {
        result = xfs_reader_configure(reader, h, sb);
        if (result != IFS_FS_OK)
            goto done;
    }
    *variant = version;
    if (volume) {
        *volume = (IfsFsVolumeInfoV2){.structure_bytes = sizeof(*volume),
                                      .variant = version,
                                      .max_name_bytes = 255u,
                                      .block_bytes = block,
                                      .total_bytes = dblocks * block,
                                      .free_bytes = free_blocks * block};
    }
    result = IFS_FS_OK;
done:
    h->release(h->context, sb);
    return result;
}
static int probe(const IfsFsHostRuntimeV2 *h, uint32_t *variant)
{
    return inspect(h, variant, NULL, NULL);
}

/* Each handle retains its immutable reader until the last in-flight operation
 * completes. A concurrent close returns BUSY rather than freeing live I/O. */
typedef struct {
    uintptr_t token;
    unsigned active;
    XfsReader *reader;
    IfsFsVolumeInfoV2 info;
} Mount;
static Mount mounts[IFS_FS_RO_MAX_SESSIONS_V2];
static atomic_flag registry_lock = ATOMIC_FLAG_INIT;
static uintptr_t next_token;
#ifndef IFS_XFS_FIRMWARE
static int formatting;
#endif
static void lock_mounts(void)
{
    while (atomic_flag_test_and_set_explicit(&registry_lock, memory_order_acquire)) {
    }
}
static void unlock_mounts(void)
{
    atomic_flag_clear_explicit(&registry_lock, memory_order_release);
}
static Mount *find_mount(void *handle)
{
    for (unsigned i = 0; i < IFS_FS_RO_MAX_SESSIONS_V2; ++i)
        if (handle && mounts[i].token == (uintptr_t)handle)
            return &mounts[i];
    return NULL;
}
static int mount_volume(const IfsFsHostRuntimeV2 *h, void **handle, IfsFsVolumeInfoV2 *info)
{
    if (!handle || !info || info->structure_bytes < sizeof(*info))
        return IFS_FS_INVALID;
    *handle = NULL;
    if (!ifs_fs_host_v2_valid(h))
        return IFS_FS_INVALID;
    if (!(h->capabilities & IFS_FS_HOST_READ_ONLY))
        return IFS_FS_ACCESS;
    IfsFsVolumeInfoV2 measured;
    uint32_t variant;
    XfsReader *reader = h->allocate(h->context, sizeof(*reader));
    if (!reader)
        return IFS_FS_NO_MEMORY;
    int rc = inspect(h, &variant, &measured, reader);
    if (!rc)
        rc = xfs_reader_check_root(reader);
#ifndef IFS_XFS_FIRMWARE
    if (!rc)
        rc = xfs_reader_allocation_summary(reader,measured.free_bytes,&measured.free_bytes);
#endif
    if (rc) {
        h->release(h->context, reader);
        return rc;
    }
    lock_mounts();
#ifndef IFS_XFS_FIRMWARE
    if(formatting){unlock_mounts();h->release(h->context,reader);return IFS_FS_BUSY;}
#endif
    for (unsigned i = 0; i < IFS_FS_RO_MAX_SESSIONS_V2 && next_token != UINTPTR_MAX; ++i) {
        if (mounts[i].reader)
            continue;
        uintptr_t token = ++next_token ^ (uintptr_t)(void *)mounts;
        if (!token) {
            if (next_token == UINTPTR_MAX)
                break;
            token = ++next_token ^ (uintptr_t)(void *)mounts;
        }
        mounts[i] = (Mount){token, 0, reader, measured};
        *handle = (void *)mounts[i].token;
        *info = measured;
        break;
    }
    unlock_mounts();
    if (*handle)
        return IFS_FS_OK;
    reader->host.release(reader->host.context, reader);
    return IFS_FS_BUSY;
}
static int unmount_volume(void *handle)
{
    lock_mounts();
    Mount *m = find_mount(handle);
    if (!m || m->active) {
        int rc = m ? IFS_FS_BUSY : IFS_FS_INVALID;
        unlock_mounts();
        return rc;
    }
    XfsReader *reader = m->reader;
    *m = (Mount){0};
    unlock_mounts();
    reader->host.release(reader->host.context, reader);
    return IFS_FS_OK;
}
static int query_volume(void *handle, IfsFsVolumeInfoV2 *info)
{
    if (!info || info->structure_bytes < sizeof(*info))
        return IFS_FS_INVALID;
    lock_mounts();
    Mount *m = find_mount(handle);
    if (m)
        *info = m->info;
    unlock_mounts();
    return m ? IFS_FS_OK : IFS_FS_INVALID;
}
static Mount *acquire(void *handle)
{
    lock_mounts();
    Mount *m = find_mount(handle);
    if (m && m->active != UINT_MAX)
        ++m->active;
    else
        m = NULL;
    unlock_mounts();
    return m;
}
static int finished(Mount *m, int rc)
{
    lock_mounts();
    --m->active;
    unlock_mounts();
    return rc;
}
static int stat_file(void *handle, const char *path, IfsFsEntryV2 *entry)
{
    Mount *m = acquire(handle);
    return m ? finished(m, xfs_reader_stat(m->reader, path, entry)) : IFS_FS_INVALID;
}
static int list_directory(void *handle, const char *path, uint32_t index, IfsFsEntryV2 *entry)
{
    Mount *m = acquire(handle);
    return m ? finished(m, xfs_reader_list(m->reader, path, index, entry)) : IFS_FS_INVALID;
}
static int read_file(void *handle, const char *path, uint64_t offset, void *out, uint32_t bytes)
{
    Mount *m = acquire(handle);
    return m ? finished(m, xfs_reader_read(m->reader, path, offset, out, bytes)) : IFS_FS_INVALID;
}
#ifndef IFS_XFS_FIRMWARE
/* Native XFS v5 empty-volume creation. Initial deliberately bounded
 * profile: 4 KiB blocks, 512-byte disk sectors, four equal power-of-two
 * allocation groups, 512-byte v3 inodes, internal 64 MiB log, FTYPE.
 * No sparse inode chunks, reflink, reverse mapping or realtime device.
 * Independent xfs_repair checks the actual output before release.
 * Layout follows the retained canonical XFS disk definitions. */
enum { FMT_BLOCK=4096u,FMT_SECTOR=512u,FMT_AGS=4u,
       FMT_LOG_AG=2u,FMT_LOG_START=8u,FMT_LOG_BLOCKS=16384u };
static int fmt_write(const IfsFsHostRuntimeV2 *h,uint64_t at,const void *p,uint32_t n)
{
    if(h->cancelled && h->cancelled(h->context))return IFS_FS_BUSY;
    return ifs_fs_host_write_bytes_v2(h,at,p,n);
}
static void fmt_tree(uint8_t *p,const char *magic,uint32_t ag,
                     uint64_t disk_block,const uint8_t *uuid,uint16_t count)
{
    memset(p,0,FMT_BLOCK);memcpy(p,magic,4u);
    xfs_put_be16(p+6,count);xfs_put_be32(p+8,UINT32_MAX);
    xfs_put_be32(p+12,UINT32_MAX);xfs_put_be64(p+16,disk_block*8u);
    memcpy(p+32,uuid,16u);xfs_put_be32(p+48,ag);
}
static int format_impl(const IfsFsHostRuntimeV2 *h,const char *label)
{
    const uint64_t blocks=h->volume_bytes/FMT_BLOCK;
    const uint32_t agblocks=(uint32_t)(blocks/FMT_AGS);
    unsigned aglog=0u;
    for(uint32_t n=agblocks;n>1u;n>>=1)++aglog;
    uint8_t uuid[16];
    int rc=ifs_fs_host_random_bytes_v2(h,uuid,sizeof(uuid));
    if(rc!=IFS_FS_OK)return rc;
    uuid[6]=(uint8_t)((uuid[6]&15u)|64u);uuid[8]=(uint8_t)((uuid[8]&63u)|128u);
    uint32_t now=0u;
    if(h->realtime_ns) {
        uint64_t t=h->realtime_ns(h->context)/1000000000ull;
        if(t<=INT32_MAX)now=(uint32_t)t;
    }
    const uint64_t logbase=(uint64_t)FMT_LOG_AG*agblocks+FMT_LOG_START;
    uint8_t sb[FMT_SECTOR]={0},block[FMT_BLOCK]={0};
    /* Count AGFL blocks as free in the global summary but not in AGF's
     * ordinary free-space B-tree count. Four freelist blocks per AG. */
    const uint64_t free=blocks-4u*FMT_AGS-8u-FMT_LOG_BLOCKS;
    memcpy(sb,"XFSB",4u);xfs_put_be32(sb+4,FMT_BLOCK);
    xfs_put_be64(sb+8,blocks);memcpy(sb+32,uuid,16u);
    xfs_put_be64(sb+48,logbase);xfs_put_be64(sb+56,64u);
    xfs_put_be64(sb+64,65u);xfs_put_be64(sb+72,66u);
    xfs_put_be32(sb+80,1u);xfs_put_be32(sb+84,agblocks);
    xfs_put_be32(sb+88,FMT_AGS);xfs_put_be32(sb+96,FMT_LOG_BLOCKS);
    xfs_put_be16(sb+100,0xb4a5u);xfs_put_be16(sb+102,FMT_SECTOR);
    xfs_put_be16(sb+104,512u);xfs_put_be16(sb+106,8u);
    memcpy(sb+108,label,strlen(label));
    sb[120]=12u;sb[121]=9u;sb[122]=9u;sb[123]=3u;
    sb[124]=(uint8_t)aglog;sb[127]=25u;
    xfs_put_be64(sb+128,64u);xfs_put_be64(sb+136,61u);
    xfs_put_be64(sb+144,free);
    xfs_put_be64(sb+160,UINT64_MAX);xfs_put_be64(sb+168,UINT64_MAX);
    xfs_put_be32(sb+180,4u);xfs_put_be32(sb+196,1u);
    xfs_put_be32(sb+200,0x18au);xfs_put_be32(sb+204,0x18au);
    xfs_put_be32(sb+216,1u);xfs_put_be64(sb+232,UINT64_MAX);
    /* Erase all intended superblock identities first, then write all
     * headers with inprogress set. No valid primary before durable data. */
    for(uint32_t ag=0;ag<FMT_AGS;++ag) {
        rc=fmt_write(h,(uint64_t)ag*agblocks*FMT_BLOCK,block,sizeof(block));
        if(rc!=IFS_FS_OK)return rc;
    }
    if(h->flush(h->context)!=0)return IFS_FS_IO_ERROR;
    sb[126]=1u;xfs_write_crc_le(sb,sizeof(sb),224u);
    for(uint32_t ag=0;ag<FMT_AGS;++ag) {
        const uint64_t base=(uint64_t)ag*agblocks;
        const uint32_t start=ag==0?16u:ag==FMT_LOG_AG?FMT_LOG_START+FMT_LOG_BLOCKS:8u;
        const uint32_t count=agblocks-start;
        memset(block,0,sizeof(block));memcpy(block,sb,sizeof(sb));
        uint8_t *agf=block+512u,*agi=block+1024u,*agfl=block+1536u;
        memcpy(agf,"XAGF",4u);xfs_put_be32(agf+4,1u);xfs_put_be32(agf+8,ag);
        xfs_put_be32(agf+12,agblocks);xfs_put_be32(agf+16,1u);
        xfs_put_be32(agf+20,2u);xfs_put_be32(agf+28,1u);
        xfs_put_be32(agf+32,1u);xfs_put_be32(agf+44,3u);
        xfs_put_be32(agf+48,4u);xfs_put_be32(agf+52,count);
        xfs_put_be32(agf+56,count);memcpy(agf+64,uuid,16u);
        xfs_write_crc_le(agf,512u,216u);
        memcpy(agi,"XAGI",4u);xfs_put_be32(agi+4,1u);xfs_put_be32(agi+8,ag);
        xfs_put_be32(agi+12,agblocks);xfs_put_be32(agi+16,ag?0u:64u);
        xfs_put_be32(agi+20,3u);xfs_put_be32(agi+24,1u);
        xfs_put_be32(agi+28,ag?0u:61u);xfs_put_be32(agi+32,ag?UINT32_MAX:64u);
        xfs_put_be32(agi+36,UINT32_MAX);
        for(unsigned i=0;i<64u;++i)xfs_put_be32(agi+40u+4u*i,UINT32_MAX);
        memcpy(agi+296,uuid,16u);xfs_write_crc_le(agi,512u,312u);
        memcpy(agfl,"XAFL",4u);xfs_put_be32(agfl+4,ag);memcpy(agfl+8,uuid,16u);
        for(unsigned i=36u;i<512u;i+=4u)xfs_put_be32(agfl+i,UINT32_MAX);
        for(unsigned i=0;i<4u;++i)xfs_put_be32(agfl+36u+4u*i,4u+i);
        xfs_write_crc_le(agfl,512u,32u);
        rc=fmt_write(h,base*FMT_BLOCK,block,sizeof(block));if(rc!=IFS_FS_OK)return rc;
        for(unsigned i=1;i<=2u;++i) {
            fmt_tree(block,i==1?"AB3B":"AB3C",ag,base+i,uuid,1u);
            xfs_put_be32(block+56,start);xfs_put_be32(block+60,count);
            xfs_write_crc_le(block,sizeof(block),52u);
            rc=fmt_write(h,(base+i)*FMT_BLOCK,block,sizeof(block));
            if(rc!=IFS_FS_OK)return rc;
        }
        fmt_tree(block,"IAB3",ag,base+3u,uuid,ag?0u:1u);
        if(!ag) {
            xfs_put_be32(block+56,64u);xfs_put_be32(block+60,61u);
            xfs_put_be64(block+64,UINT64_MAX<<3u);
        }
        xfs_write_crc_le(block,sizeof(block),52u);
        rc=fmt_write(h,(base+3u)*FMT_BLOCK,block,sizeof(block));
        if(rc!=IFS_FS_OK)return rc;
        memset(block,0,sizeof(block));
        for(unsigned i=4u;i<8u;++i) {
            rc=fmt_write(h,(base+i)*FMT_BLOCK,block,sizeof(block));
            if(rc!=IFS_FS_OK)return rc;
        }
    }
    /* Initialize an entire 64-inode chunk. Root is an empty shortform
     * directory; the two mandatory realtime metadata inodes have no data. */
    for(unsigned b=0;b<8u;++b) {
        memset(block,0,sizeof(block));
        for(unsigned i=0;i<8u;++i) {
            const unsigned index=b*8u+i;
            uint8_t *ino=block+512u*i;
            xfs_put_be16(ino,0x494eu);ino[4]=3u;ino[5]=2u;
            /* An absent attribute fork still uses the extents format. */
            ino[83]=2u;
            xfs_put_be32(ino+92,index+1u);xfs_put_be32(ino+96,UINT32_MAX);
            xfs_put_be64(ino+152,64u+index);memcpy(ino+160,uuid,16u);
            if(index<3u) {
                xfs_put_be16(ino+2,index?0100600u:0040755u);
                xfs_put_be32(ino+16,index?1u:2u);
                xfs_put_be32(ino+32,now);xfs_put_be32(ino+40,now);
                xfs_put_be32(ino+48,now);xfs_put_be32(ino+144,now);
                if(!index) {
                    ino[5]=1u;xfs_put_be64(ino+56,6u);
                    xfs_put_be32(ino+178,64u);
                } else if(index==1u)xfs_put_be16(ino+90,4u);
            }
            xfs_write_crc_le(ino,512u,100u);
        }
        rc=fmt_write(h,(uint64_t)(8u+b)*FMT_BLOCK,block,sizeof(block));
        if(rc!=IFS_FS_OK)return rc;
    }
    /* Clear the full log and create the canonical initial unmount record.
     * The initial formatting log has cycle one and no record CRC. */
    uint8_t *zero=h->allocate(h->context,65536u);
    if(!zero)return IFS_FS_NO_MEMORY;
    memset(zero,0,65536u);
    for(uint32_t off=0;off<FMT_LOG_BLOCKS;off+=16u) {
        rc=fmt_write(h,(logbase+off)*FMT_BLOCK,zero,65536u);
        if(rc!=IFS_FS_OK)break;
    }
    h->release(h->context,zero);if(rc!=IFS_FS_OK)return rc;
    memset(block,0,sizeof(block));xfs_put_be32(block,0xfeedbabeu);
    xfs_put_be32(block+4,1u);xfs_put_be32(block+8,2u);
    xfs_put_be32(block+12,512u);xfs_put_be64(block+16,UINT64_C(1)<<32);
    xfs_put_be64(block+24,UINT64_C(1)<<32);xfs_put_be32(block+36,UINT32_MAX);
    xfs_put_be32(block+40,1u);xfs_put_be32(block+44,0xb0c0d0d0u);
    xfs_put_be32(block+300,1u);memcpy(block+304,uuid,16u);
    xfs_put_be32(block+320,32768u);
    xfs_put_be32(block+512,1u);xfs_put_be32(block+516,8u);
    block[520]=0xaau;block[521]=0x20u;block[524]=0x6eu;block[525]=0x55u;
    rc=fmt_write(h,logbase*FMT_BLOCK,block,sizeof(block));if(rc!=IFS_FS_OK)return rc;
    if(h->flush(h->context)!=0)return IFS_FS_IO_ERROR;
    sb[126]=0u;xfs_write_crc_le(sb,sizeof(sb),224u);
    for(uint32_t ag=1u;ag<FMT_AGS;++ag) {
        rc=fmt_write(h,(uint64_t)ag*agblocks*FMT_BLOCK,sb,sizeof(sb));
        if(rc!=IFS_FS_OK)return rc;
    }
    if(h->flush(h->context)!=0)return IFS_FS_IO_ERROR;
    rc=fmt_write(h,0u,sb,sizeof(sb));if(rc!=IFS_FS_OK)return rc;
    if(h->flush(h->context)!=0) {
        memset(sb,0,sizeof(sb));(void)ifs_fs_host_write_bytes_v2(h,0u,sb,sizeof(sb));
        (void)h->flush(h->context);return IFS_FS_IO_ERROR;
    }
    XfsReader reader;uint32_t observed=0u;
    rc=inspect(h,&observed,NULL,&reader);
    return rc?rc:observed!=5u?IFS_FS_CORRUPT:xfs_reader_check_root(&reader);
}
static int format_volume(const IfsFsHostRuntimeV2 *h,uint32_t variant,
                          const char *label,uint32_t flags)
{
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if((h->capabilities&IFS_FS_HOST_READ_ONLY) ||
       !(h->capabilities&IFS_FS_HOST_EXCLUSIVE) ||
       !(h->capabilities&IFS_FS_HOST_DURABLE))return IFS_FS_ACCESS;
    if(variant!=5u || flags || h->logical_sector_bytes>FMT_BLOCK ||
       h->volume_bytes<512ull*1024u*1024u ||
       h->volume_bytes>16ull*1024u*1024u*1024u ||
       (h->volume_bytes&(h->volume_bytes-1u)))return IFS_FS_UNSUPPORTED;
    if(!label)label="";
    if(strlen(label)>12u)return IFS_FS_INVALID;
    for(const char *p=label;*p;++p)
        if((unsigned char)*p<33u || (unsigned char)*p>126u || *p=='/')return IFS_FS_INVALID;
    lock_mounts();
    if(formatting){unlock_mounts();return IFS_FS_BUSY;}
    for(unsigned i=0;i<IFS_FS_RO_MAX_SESSIONS_V2;++i)
        if(mounts[i].reader){unlock_mounts();return IFS_FS_BUSY;}
    formatting=1;unlock_mounts();
    const int rc=format_impl(h,label);
    lock_mounts();formatting=0;unlock_mounts();return rc;
}
#endif

/* Pin the mounted reader for metadata I/O just as for file reads. */
static int get_metadata(void *handle,const char *path,IfsFsMetadataV2 *out)
{
    Mount *m=acquire(handle);
    return m ? finished(m,xfs_reader_metadata(m->reader,path,out)) : IFS_FS_INVALID;
}
static const IfsFsMetadataOpsV2 metadata_ops={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(metadata_ops),IFS_FS_GROUP_METADATA,0u,
             IFS_FS_META_GET_MODE},
    .get=get_metadata
};
/* Return the label from the validated mounted superblock, preserving
 * the reader's lifetime and refusing partial output. */
static int volume_label(void *handle,char *out,uint32_t capacity,uint32_t *bytes)
{
    if(!bytes)return IFS_FS_INVALID;
    *bytes=0u;
    if(!out || !capacity)return IFS_FS_INVALID;
    Mount *m=acquire(handle);
    if(!m)return IFS_FS_INVALID;
    uint32_t n=0u;
    while(n<sizeof(m->reader->label) && m->reader->label[n])++n;
    if(n>=capacity)return finished(m,IFS_FS_NO_SPACE);
    memcpy(out,m->reader->label,n);
    out[n]=0;*bytes=n;
    return finished(m,IFS_FS_OK);
}
static const IfsFsVolumeOpsV2 volume_ops = {
    .header = {IFS_FS_GROUP_ABI_V2, sizeof(IfsFsVolumeOpsV2), IFS_FS_GROUP_VOLUME, 0u,
               IFS_FS_VOLUME_QUERY | IFS_FS_VOLUME_LABEL_GET
#ifndef IFS_XFS_FIRMWARE
               | IFS_FS_VOLUME_FORMAT
#endif
    },
    .query = query_volume, .label=volume_label
#ifndef IFS_XFS_FIRMWARE
    ,.format = format_volume
#endif
};
#ifndef IFS_XFS_FIRMWARE
/* Only hosted Common V2 advertises file extent mapping. No writer or
 * relocation capability is inferred from the read-only data fork. */
static int map_file(void *handle,const char *path,uint64_t first,
                    IfsFsExtentV2 *out,uint32_t capacity,uint32_t *written)
{
    if(!written)return IFS_FS_INVALID;
    *written=0u;
    Mount *m=acquire(handle);
    return m?finished(m,xfs_reader_map(m->reader,path,first,out,capacity,written)):
             IFS_FS_INVALID;
}
static int allocation_map(void *handle,uint64_t first,IfsFsAllocationRangeV2 *out,
                          uint32_t capacity,uint32_t *written)
{
    if(!written)return IFS_FS_INVALID;
    *written=0u;
    Mount *m=acquire(handle);
    return m?finished(m,xfs_reader_allocation(m->reader,first,out,capacity,written)):
        IFS_FS_INVALID;
}
static const IfsFsAllocationOpsV2 allocation_ops={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(allocation_ops),IFS_FS_GROUP_ALLOCATION,0u,
             IFS_FS_ALLOCATION_MAP},
    .map=allocation_map
};
static const IfsFsDefragOpsV2 defrag_ops={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(defrag_ops),IFS_FS_GROUP_DEFRAG,0u,
             IFS_FS_DEFRAG_MAP},
    .map=map_file
};
#endif
static const IfsFsCoreOpsV2 core = {
    .header = {IFS_FS_GROUP_ABI_V2, sizeof(IfsFsCoreOpsV2), IFS_FS_GROUP_CORE, 0u,
               IFS_FS_CORE_PROBE | IFS_FS_CORE_MOUNT | IFS_FS_CORE_STAT | IFS_FS_CORE_LIST |
                   IFS_FS_CORE_READ},
    .probe = probe,
    .mount = mount_volume,
    .unmount = unmount_volume,
    .stat = stat_file,
    .directory_entry = list_directory,
    .read = read_file};
static const IfsFsGroupHeaderV2 *query(uint32_t group, uint32_t lo, uint32_t hi)
{
    if (lo > IFS_FS_GROUP_ABI_V2 || hi < IFS_FS_GROUP_ABI_V2)
        return NULL;
    if (group == IFS_FS_GROUP_CORE)
        return &core.header;
    if (group == IFS_FS_GROUP_VOLUME)
        return &volume_ops.header;
    if (group == IFS_FS_GROUP_METADATA) return &metadata_ops.header;
#ifndef IFS_XFS_FIRMWARE
    if (group == IFS_FS_GROUP_DEFRAG) return &defrag_ops.header;
    if (group == IFS_FS_GROUP_ALLOCATION) return &allocation_ops.header;
#endif
    return NULL;
}
static const IfsFsModuleApiV2 api = {
    .abi_version = IFS_FS_COMMON_ABI_V2,
    .structure_bytes = sizeof(IfsFsModuleApiV2),
    .format_id = "xfs",
#ifdef IFS_XFS_FIRMWARE
    .runtime_profiles = IFS_FS_PROFILE_FREESTANDING | IFS_FS_PROFILE_FIRMWARE,
#else
    .runtime_profiles = IFS_FS_PROFILE_HOSTED,
#endif
    .group_mask = IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE) | IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME) |
                  IFS_FS_GROUP_BIT(IFS_FS_GROUP_METADATA)
#ifndef IFS_XFS_FIRMWARE
                  | IFS_FS_GROUP_BIT(IFS_FS_GROUP_DEFRAG) | IFS_FS_GROUP_BIT(IFS_FS_GROUP_ALLOCATION)
#endif
                  ,
    .query_group = query};
__attribute__((visibility("default"))) const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void)
{
    return &api;
}
