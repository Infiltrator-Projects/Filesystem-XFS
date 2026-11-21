/* SPDX-License-Identifier: GPL-3.0-or-later
 * Common V2 raw-device probe using the canonical native XFS geometry and
 * checksum helpers. No path, fd, write, or staging operations are exposed.
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
        if ((incompat & ~UINT32_C(0x2f)) || (incompat & UINT32_C(0x10))) {
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
    if (rc) {
        h->release(h->context, reader);
        return rc;
    }
    lock_mounts();
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
static const IfsFsVolumeOpsV2 volume_ops = {
    .header = {IFS_FS_GROUP_ABI_V2, sizeof(IfsFsVolumeOpsV2), IFS_FS_GROUP_VOLUME, 0u,
               IFS_FS_VOLUME_QUERY},
    .query = query_volume};
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
    .group_mask = IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE) | IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),
    .query_group = query};
__attribute__((visibility("default"))) const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void)
{
    return &api;
}
