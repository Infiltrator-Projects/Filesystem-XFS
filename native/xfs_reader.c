/* SPDX-License-Identifier: GPL-3.0-or-later
 * Allocation-group inode addressing, data fork extents and native shortform
 * and data directories over the Common V2 partition-relative read service.
 */
#include "xfs_reader.h"
#include "xfs_disk.h"
#include <infiltratr/endian.h>
#include <stdbool.h>
#include <string.h>

enum {
    LOCAL = 1,
    EXTENTS = 2,
    BTREE = 3,
    MODE_DIR = 0040000,
    MODE_REG = 0100000,
    MODE_LINK = 0120000,
    MODE_MASK = 0170000
};
typedef struct {
    uint8_t raw[2048];
    uint64_t ino, size, extents;
    uint16_t mode, fork_start, fork_length;
    uint8_t format;
} Inode;

/* XFS encodes an AG number above a rounded-up power-of-two address space.
 * The last AG can be short and an encoded number can land in an AG gap. */
static int physical(const XfsReader *r, uint64_t encoded, uint64_t length, uint64_t *block)
{
    uint64_t ag = encoded >> r->ag_log;
    uint64_t within = encoded & ((UINT64_C(1) << r->ag_log) - 1);
    if (ag > (r->blocks - 1) / r->ag_blocks || within >= r->ag_blocks || !length ||
        length > r->ag_blocks - within)
        return IFS_FS_CORRUPT;
    uint64_t base = ag * r->ag_blocks;
    if (within >= r->blocks - base || length > r->blocks - base - within)
        return IFS_FS_CORRUPT;
    *block = base + within;
    return IFS_FS_OK;
}

int xfs_reader_configure(XfsReader *r, const IfsFsHostRuntimeV2 *host, const uint8_t *sb)
{
    *r = (XfsReader){0};
    memcpy(&r->host, host,
           host->structure_bytes < sizeof(r->host) ? host->structure_bytes : sizeof(r->host));
    r->blocks = xfs_be64(sb + 8);
    r->block_bytes = xfs_be32(sb + 4);
    r->root = xfs_be64(sb + 56);
    r->log_start = xfs_be64(sb + 48);
    r->log_blocks = xfs_be32(sb + 96);
    r->ag_blocks = xfs_be32(sb + 84);
    r->ag_log = sb[124];
    r->inop_log = sb[123];
    r->dir_log = sb[192];
    r->inode_bytes = xfs_be16(sb + 104);
    uint16_t version = xfs_be16(sb + 100);
    r->v5 = (version & 15u) == 5;
    uint32_t incompat = r->v5 ? xfs_be32(sb + 216) : 0;
    r->sector_bytes = xfs_be16(sb + 102);
    r->sparse = !!(incompat & 2u);
    r->ftype = r->v5 ? !!(incompat & 1u) : !!(xfs_be32(sb + 200) & 0x200u);
    memcpy(r->uuid, sb + 32, 16);
    /* sb_fname is exactly 12 bytes, not necessarily NUL terminated. */
    memcpy(r->label,sb+108u,sizeof(r->label));
    memcpy(r->metadata_uuid, incompat & 4u ? sb + 248 : sb + 32, 16);
    if (!r->root || !r->log_blocks)
        return IFS_FS_CORRUPT;
    /* External logs, realtime devices, ASCII case-insensitive names, log
     * replay and pre-dir2 namespace layouts require separate implementations. */
    if (!r->log_start || xfs_be64(sb + 16) || (version & 0x4000u) ||
        (!r->v5 && !(version & 0x2000u)) || (r->v5 && xfs_be32(sb + 220)))
        return IFS_FS_UNSUPPORTED;
    return IFS_FS_OK;
}

/* An old dinode may retain a valid inode CRC after unlink. The inobt free
 * mask and sparse-inode hole mask are authoritative for directory references. */
static int allocated(const XfsReader *r, uint64_t ino)
{
    unsigned shift = r->ag_log + r->inop_log;
    uint64_t ag = ino >> shift, index = ino & ((UINT64_C(1) << shift) - 1);
    if (ag > (r->blocks - 1) / r->ag_blocks || index > UINT32_MAX)
        return IFS_FS_CORRUPT;
    uint64_t base = ag * r->ag_blocks;
    uint32_t length = r->blocks - base < r->ag_blocks ? (uint32_t)(r->blocks - base) : r->ag_blocks;
    uint8_t *p = r->host.allocate(r->host.context, r->block_bytes);
    if (!p)
        return IFS_FS_NO_MEMORY;
    int rc = ifs_fs_host_read_bytes_v2(&r->host, base * r->block_bytes + 2u * r->sector_bytes, p,
                                       r->sector_bytes);
    if (rc)
        goto done;
    rc = IFS_FS_CORRUPT;
    if (xfs_be32(p) != 0x58414749 || xfs_be32(p + 4) != 1 || xfs_be32(p + 8) != ag ||
        xfs_be32(p + 12) != length || xfs_be32(p + 28) > xfs_be32(p + 16))
        goto done;
    if (r->v5 && (memcmp(p + 296, r->metadata_uuid, 16) ||
                  infiltratr_load_le32(p + 312) != xfs_crc_field(p, r->sector_bytes, 312)))
        goto done;
    uint32_t node = xfs_be32(p + 20), levels = xfs_be32(p + 24);
    if (!levels || levels > 16)
        goto done;
    while (levels) {
        if (!node || node >= length)
            goto done;
        uint64_t at = (base + node) * r->block_bytes;
        rc = ifs_fs_host_read_bytes_v2(&r->host, at, p, r->block_bytes);
        if (rc)
            goto done;
        rc = IFS_FS_CORRUPT;
        if (xfs_be32(p) != (r->v5 ? 0x49414233u : 0x49414254u) || xfs_be16(p + 4) != --levels)
            goto done;
        if (r->v5 && (xfs_be64(p + 16) != at / 512 || memcmp(p + 32, r->metadata_uuid, 16) ||
                      xfs_be32(p + 48) != ag ||
                      infiltratr_load_le32(p + 52) != xfs_crc_field(p, r->block_bytes, 52)))
            goto done;
        unsigned header = r->v5 ? 56 : 16, count = xfs_be16(p + 6);
        unsigned capacity = (r->block_bytes - header) / (levels ? 8 : 16);
        if (!count || count > capacity)
            goto done;
        uint32_t prev = 0, selected = UINT32_MAX;
        for (unsigned i = 0; i < count; ++i) {
            uint32_t start = xfs_be32(p + header + i * (levels ? 4 : 16));
            if (i && (start <= prev || (!levels && start - prev < 64)))
                goto done;
            if (start <= index)
                selected = i;
            prev = start;
        }
        if (selected == UINT32_MAX)
            goto done;
        if (levels) {
            node = xfs_be32(p + header + capacity * 4 + selected * 4);
        } else {
            const uint8_t *record = p + header + selected * 16;
            uint64_t bit = index - xfs_be32(record);
            if (bit >= 64 || (xfs_be64(record + 8) & (UINT64_C(1) << bit)) ||
                (r->sparse && (xfs_be16(record + 4) & (1u << (bit / 4)))))
                goto done;
            rc = IFS_FS_OK;
        }
    }
done:
    r->host.release(r->host.context, p);
    return rc;
}

static int load_inode(const XfsReader *r, uint64_t ino, Inode *out)
{
    uint64_t block;
    if (!ino || physical(r, ino >> r->inop_log, 1, &block))
        return IFS_FS_CORRUPT;
    int allocated_rc = allocated(r, ino);
    if (allocated_rc)
        return allocated_rc;
    uint64_t slot = ino & ((UINT64_C(1) << r->inop_log) - 1);
    uint64_t where = block * r->block_bytes + slot * r->inode_bytes;
    uint8_t *p = out->raw;
    int rc = ifs_fs_host_read_bytes_v2(&r->host, where, p, r->inode_bytes);
    if (rc)
        return rc;
    if (xfs_be16(p) != 0x494e || (r->v5 ? p[4] != 3 : (p[4] != 1 && p[4] != 2)))
        return IFS_FS_CORRUPT;
    if (r->v5 && (infiltratr_load_le32(p + 100) != xfs_crc_field(p, r->inode_bytes, 100) ||
                  xfs_be64(p + 152) != ino || memcmp(p + 160, r->metadata_uuid, 16)))
        return IFS_FS_CORRUPT;
    if ((xfs_be16(p + 90) & 1u) || (r->v5 && (xfs_be64(p + 120) & (UINT64_C(1) << 5))))
        return IFS_FS_UNSUPPORTED;
    out->ino = ino;
    out->mode = xfs_be16(p + 2) & MODE_MASK;
    out->format = p[5];
    out->size = xfs_be64(p + 56);
    out->fork_start = r->v5 ? 176 : 100;
    uint16_t available = r->inode_bytes - out->fork_start;
    out->fork_length = p[82] ? (uint16_t)p[82] * 8u : available;
    if (out->fork_length > available || out->size > INT64_MAX || (p[4] != 1 && !xfs_be32(p + 16)))
        return IFS_FS_CORRUPT;
    out->extents = r->v5 && (xfs_be64(p + 120) & 16u) ? xfs_be64(p + 24) : xfs_be32(p + 76);
    if (out->format == EXTENTS && out->extents > out->fork_length / 16u)
        return IFS_FS_CORRUPT;
    if (out->format == LOCAL &&
        (out->mode == MODE_REG || out->extents || out->size > out->fork_length))
        return IFS_FS_CORRUPT;
    return IFS_FS_OK;
}

typedef struct {
    uint64_t logical, length, disk;
    bool unwritten;
} Run;

/* Each packed 128-bit bmbt record stores flag:1, file offset:54, startblock:52,
 * length:21. Non-overlap and physical bounds are checked even for later rows. */
static int find_extent(const XfsReader *r, const uint8_t *fork, unsigned count, uint64_t target,
                       uint64_t minimum, uint64_t limit, Run *match)
{
    uint64_t previous = minimum;
    bool mapped = false;
    *match = (Run){.logical = target, .length = limit - target, .unwritten = true};
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *p = fork + i * 16;
        uint64_t hi = xfs_be64(p), lo = xfs_be64(p + 8);
        uint64_t logical = (hi >> 9) & ((UINT64_C(1) << 54) - 1);
        uint64_t encoded = ((hi & 511u) << 43) | (lo >> 21);
        uint64_t length = lo & 0x1fffffu, disk;
        if (logical < previous || logical + length > limit || physical(r, encoded, length, &disk))
            return IFS_FS_CORRUPT;
        previous = logical + length;
        if (target >= logical && target < previous) {
            mapped = true;
            *match = (Run){logical, length, disk, !!(hi >> 63)};
        } else if (logical > target && !mapped && logical - target < match->length)
            match->length = logical - target;
    }
    return IFS_FS_OK;
}

/* The data-fork B+tree uses long filesystem-block pointers and separate
 * arrays of keys and pointers. The pointer array begins after the maximum
 * key count, even when only a few entries are populated. */
static int extent_at(const XfsReader *r, const Inode *inode, uint64_t target, Run *match)
{
    uint64_t lower = 0, upper = UINT64_C(1) << 54;
    if (target >= upper)
        return IFS_FS_CORRUPT;
    const uint8_t *node = inode->raw + inode->fork_start;
    if (inode->format == EXTENTS)
        return find_extent(r, node, (unsigned)inode->extents, target, lower, upper, match);
    if (inode->format != BTREE)
        return IFS_FS_UNSUPPORTED;
    if (inode->fork_length < 20 || !inode->extents)
        return IFS_FS_CORRUPT;
    unsigned level = xfs_be16(node), count = xfs_be16(node + 2);
    if (!level || level > 16)
        return IFS_FS_CORRUPT;
    uint8_t *block = r->host.allocate(r->host.context, r->block_bytes);
    if (!block)
        return IFS_FS_NO_MEMORY;
    int rc = IFS_FS_CORRUPT;
    unsigned header = 4, length = inode->fork_length;
    while (level) {
        unsigned capacity = (length - header) / 16;
        if (!count || count > capacity)
            goto done;
        const uint8_t *key = node + header, *ptr = key + capacity * 8;
        uint64_t previous = 0;
        unsigned selected = count;
        for (unsigned i = 0; i < count; ++i) {
            uint64_t k = xfs_be64(key + i * 8);
            if ((i && k <= previous) || k < lower || k >= upper)
                goto done;
            if (k <= target)
                selected = i;
            previous = k;
        }
        if (selected == count) {
            *match = (Run){.logical = target, .length = xfs_be64(key) - target, .unwritten = true};
            rc = IFS_FS_OK;
            goto done;
        }
        lower = xfs_be64(key + selected * 8);
        if (selected + 1 < count)
            upper = xfs_be64(key + (selected + 1) * 8);
        uint64_t disk;
        if (physical(r, xfs_be64(ptr + selected * 8), 1, &disk))
            goto done;
        uint64_t at = disk * r->block_bytes;
        rc = ifs_fs_host_read_bytes_v2(&r->host, at, block, r->block_bytes);
        if (rc)
            goto done;
        rc = IFS_FS_CORRUPT;
        if (xfs_be32(block) != (r->v5 ? 0x424d4133u : 0x424d4150u) ||
            xfs_be16(block + 4) != --level)
            goto done;
        if (r->v5 &&
            (xfs_be64(block + 24) != at / 512 || memcmp(block + 40, r->metadata_uuid, 16) ||
             xfs_be64(block + 56) != inode->ino ||
             infiltratr_load_le32(block + 64) != xfs_crc_field(block, r->block_bytes, 64)))
            goto done;
        count = xfs_be16(block + 6);
        header = r->v5 ? 72 : 24;
        length = r->block_bytes;
        node = block;
        if (!count || count > (length - header) / 16)
            goto done;
        uint64_t first = xfs_be64(node + header);
        if (!level)
            first = (first >> 9) & ((UINT64_C(1) << 54) - 1);
        if (first != lower)
            goto done;
    }
    rc = find_extent(r, node + header, count, target, lower, upper, match);
done:
    r->host.release(r->host.context, block);
    return rc;
}

static int data_read(const XfsReader *r, const Inode *inode, uint64_t pos, uint8_t *out,
                     uint32_t bytes, bool directory, uint64_t *first_disk)
{
    while (bytes) {
        uint64_t logical = pos / r->block_bytes;
        Run run;
        int rc = extent_at(r, inode, logical, &run);
        if (rc)
            return rc;
        uint64_t skip = logical - run.logical, inside = pos % r->block_bytes;
        if (skip >= run.length)
            return IFS_FS_CORRUPT;
        uint64_t remaining = run.length - skip;
        uint32_t take = bytes;
        if (remaining <= UINT32_MAX / r->block_bytes + 1ULL) {
            uint64_t left = remaining * r->block_bytes - inside;
            if (left < take)
                take = (uint32_t)left;
        }
        if (!take)
            return IFS_FS_CORRUPT;
        if (run.unwritten) {
            if (directory)
                return IFS_FS_CORRUPT;
            memset(out, 0, take);
        } else {
            uint64_t at = (run.disk + skip) * r->block_bytes + inside;
            if (first_disk) {
                *first_disk = at / 512;
                first_disk = NULL;
            }
            rc = ifs_fs_host_read_bytes_v2(&r->host, at, out, take);
            if (rc)
                return rc;
        }
        pos += take;
        out += take;
        bytes -= take;
    }
    return IFS_FS_OK;
}

typedef struct {
    const char *wanted;
    uint32_t wanted_bytes, target, count;
    uint64_t ino;
    char name[256];
    uint8_t length, ftype;
} Search;

static int consider(Search *s, const uint8_t *name, unsigned length, uint64_t ino, uint8_t type)
{
    if (!length || length > 255 || !ino || type > 8 || memchr(name, 0, length) ||
        memchr(name, '/', length))
        return IFS_FS_CORRUPT;
    if ((length == 1 && name[0] == '.') || (length == 2 && !memcmp(name, "..", 2)))
        return IFS_FS_OK;
    if (s->wanted ? (s->wanted_bytes == length && !memcmp(s->wanted, name, length))
                  : s->count == s->target) {
        if (s->ino)
            return IFS_FS_CORRUPT;
        s->ino = ino;
        s->length = (uint8_t)length;
        s->ftype = type;
        memcpy(s->name, name, length);
        s->name[length] = 0;
    }
    if (s->count == UINT32_MAX)
        return IFS_FS_UNSUPPORTED;
    ++s->count;
    return IFS_FS_OK;
}

static int short_directory(const XfsReader *r, const Inode *dir, Search *s)
{
    const uint8_t *p = dir->raw + dir->fork_start;
    if (dir->size < 6)
        return IFS_FS_CORRUPT;
    unsigned width = p[1] ? 8 : 4, at = 2 + width, previous = 0;
    if (dir->size < at)
        return IFS_FS_CORRUPT;
    uint64_t parent = width == 8 ? xfs_be64(p + 2) : xfs_be32(p + 2);
    if (!parent || (dir->ino == r->root && parent != r->root))
        return IFS_FS_CORRUPT;
    unsigned large = parent > UINT32_MAX;
    for (unsigned i = 0; i < p[0]; ++i) {
        if (dir->size - at < 3)
            return IFS_FS_CORRUPT;
        unsigned n = p[at], off = xfs_be16(p + at + 1), len = 3 + n + r->ftype + width;
        if (off % 8 || off <= previous || len > dir->size - at)
            return IFS_FS_CORRUPT;
        previous = off;
        const uint8_t *name = p + at + 3, *num = name + n + r->ftype;
        uint64_t ino = width == 8 ? xfs_be64(num) : xfs_be32(num);
        large += ino > UINT32_MAX;
        int rc = consider(s, name, n, ino, r->ftype ? name[n] : 0);
        if (rc)
            return rc;
        at += len;
    }
    return at == dir->size && large == p[1] ? IFS_FS_OK : IFS_FS_CORRUPT;
}

static int directory_block(const XfsReader *r, const Inode *dir, Search *s, const uint8_t *p,
                           uint32_t bytes, uint64_t addr)
{
    uint32_t magic = xfs_be32(p), end = bytes;
    bool block = magic == (r->v5 ? 0x58444233u : 0x58443242u);
    if (!block && magic != (r->v5 ? 0x58444433u : 0x58443244u))
        return IFS_FS_CORRUPT;
    if (r->v5 &&
        (infiltratr_load_le32(p + 4) != xfs_crc_field(p, bytes, 4) || xfs_be64(p + 8) != addr ||
         memcmp(p + 24, r->metadata_uuid, 16) || xfs_be64(p + 40) != dir->ino))
        return IFS_FS_CORRUPT;
    uint32_t at = r->v5 ? 64 : 16;
    if (block) {
        uint32_t leaf = xfs_be32(p + bytes - 8), stale = xfs_be32(p + bytes - 4);
        if (stale > leaf || leaf > (bytes - at - 8) / 8)
            return IFS_FS_CORRUPT;
        end -= 8 + leaf * 8;
    }
    while (at < end) {
        if (end - at < 8)
            return IFS_FS_CORRUPT;
        unsigned len;
        if (xfs_be16(p + at) == UINT16_MAX) {
            len = xfs_be16(p + at + 2);
            if (len < 8 || len % 8 || len > end - at)
                return IFS_FS_CORRUPT;
        } else {
            if (end - at < 11)
                return IFS_FS_CORRUPT;
            uint8_t n = p[at + 8];
            len = (9u + n + r->ftype + 2u + 7u) & ~7u;
            if (len > end - at)
                return IFS_FS_CORRUPT;
            int rc = consider(s, p + at + 9, n, xfs_be64(p + at), r->ftype ? p[at + 9 + n] : 0);
            if (rc)
                return rc;
        }
        if (xfs_be16(p + at + len - 2) != at)
            return IFS_FS_CORRUPT;
        at += len;
    }
    return IFS_FS_OK;
}

static int lookup(const XfsReader *r, const Inode *dir, Search *s)
{
    if (dir->mode != MODE_DIR)
        return IFS_FS_NOT_FOUND;
    int rc = IFS_FS_OK;
    if (dir->format == LOCAL) {
        rc = short_directory(r, dir, s);
    } else {
        const uint32_t bytes = r->block_bytes << r->dir_log;
        if (dir->format != EXTENTS && dir->format != BTREE)
            return IFS_FS_UNSUPPORTED;
        if (!dir->size || dir->size % bytes || dir->size > (UINT64_C(1) << 35))
            return IFS_FS_CORRUPT;
        /* Directory data is in the first 32 GiB of logical space. Leaf and
         * free-space indexes begin beyond it. Iterate recorded data extents;
         * holes left by directory shrinkage contain no directory block. */
        uint8_t *p = r->host.allocate(r->host.context, bytes);
        if (!p)
            return IFS_FS_NO_MEMORY;
        for (uint64_t at = 0; at < dir->size && !rc;) {
            Run ext;
            rc = extent_at(r, dir, at / r->block_bytes, &ext);
            if (rc)
                break;
            if (ext.unwritten) {
                if (ext.disk) {
                    rc = IFS_FS_CORRUPT;
                    break;
                }
                uint64_t step = ext.length > (dir->size - at) / r->block_bytes
                                    ? dir->size - at
                                    : ext.length * r->block_bytes;
                if (!step || step % bytes) {
                    rc = IFS_FS_CORRUPT;
                    break;
                }
                at += step;
                continue;
            }
            uint64_t disk = 0;
            rc = data_read(r, dir, at, p, bytes, true, &disk);
            if (!rc)
                rc = directory_block(r, dir, s, p, bytes, disk);
            at += bytes;
        }
        r->host.release(r->host.context, p);
    }
    if (rc)
        return rc;
    return s->ino ? IFS_FS_OK : (s->wanted ? IFS_FS_NOT_FOUND : IFS_FS_END);
}

static int entry_type(uint16_t mode)
{
    switch (mode) {
    case MODE_REG:
        return IFS_FS_ENTRY_FILE;
    case MODE_DIR:
        return IFS_FS_ENTRY_DIRECTORY;
    case MODE_LINK:
        return IFS_FS_ENTRY_LINK;
    default:
        return 0;
    }
}
static int match_inode(const XfsReader *r, const Search *s, Inode *ino)
{
    int rc = load_inode(r, s->ino, ino);
    if (rc)
        return rc;
    static const uint16_t modes[] = {0,       MODE_REG, MODE_DIR,  0020000, 0060000,
                                     0010000, 0140000,  MODE_LINK, 0};
    return s->ftype && modes[s->ftype] != ino->mode ? IFS_FS_CORRUPT : IFS_FS_OK;
}

static int resolve(const XfsReader *r, const char *path, Inode *inode, Search *last)
{
    if (!path || *path != '/')
        return IFS_FS_INVALID;
    int rc = load_inode(r, r->root, inode);
    if (rc)
        return rc;
    *last = (Search){.ino = r->root, .length = 1, .name = "/"};
    unsigned total = 0;
    for (const char *p = path + 1; *p;) {
        const char *start = p;
        unsigned n = 0;
        while (*p && *p != '/') {
            if (++n > 255)
                return IFS_FS_INVALID;
            ++p;
        }
        if (!n || (n == 1 && start[0] == '.') || (n == 2 && !memcmp(start, "..", 2)) ||
            (total += n + 1) > 4096)
            return IFS_FS_INVALID;
        *last = (Search){.wanted = start, .wanted_bytes = n};
        rc = lookup(r, inode, last);
        if (rc)
            return rc;
        rc = match_inode(r, last, inode);
        if (rc)
            return rc;
        if (*p) {
            ++p;
            if (inode->mode != MODE_DIR)
                return inode->mode == MODE_LINK ? IFS_FS_UNSUPPORTED : IFS_FS_NOT_FOUND;
        }
    }
    return IFS_FS_OK;
}

static int fill(const Inode *inode, const Search *where, IfsFsEntryV2 *out)
{
    if (!out || out->structure_bytes < sizeof(*out))
        return IFS_FS_INVALID;
    int type = entry_type(inode->mode);
    if (!type)
        return IFS_FS_UNSUPPORTED;
    out->type = (uint32_t)type;
    out->size_bytes = inode->size;
    out->attributes = 0;
    out->name_bytes = where->length;
    if (!out->name)
        return out->name_capacity ? IFS_FS_INVALID : IFS_FS_OK;
    if (out->name_capacity <= where->length)
        return IFS_FS_NO_SPACE;
    memcpy(out->name, where->name, where->length + 1u);
    return IFS_FS_OK;
}
/* Return native inode ownership and permissions from the same verified
 * namespace traversal used by stat. No synthetic modes, no mutation. */
int xfs_reader_metadata(const XfsReader *r,const char *path,IfsFsMetadataV2 *out)
{
    if(!out || out->structure_bytes<sizeof(*out)) return IFS_FS_INVALID;
    Inode inode;
    Search search;
    int rc=resolve(r,path,&inode,&search);
    if(rc) return rc;
    *out=(IfsFsMetadataV2){.structure_bytes=sizeof(*out),.mask=IFS_FS_META_GET_MODE,
        .mode=xfs_be16(inode.raw+2),.owner=xfs_be32(inode.raw+8),.group=xfs_be32(inode.raw+12)};
    return IFS_FS_OK;
}
int xfs_reader_stat(const XfsReader *r, const char *path, IfsFsEntryV2 *out)
{
    Inode inode;
    Search s;
    int rc = resolve(r, path, &inode, &s);
    return rc ? rc : fill(&inode, &s, out);
}
int xfs_reader_list(const XfsReader *r, const char *path, uint32_t index, IfsFsEntryV2 *out)
{
    Inode inode;
    Search s;
    int rc = resolve(r, path, &inode, &s);
    if (rc)
        return rc;
    s = (Search){.target = index};
    rc = lookup(r, &inode, &s);
    if (!rc)
        rc = match_inode(r, &s, &inode);
    return rc ? rc : fill(&inode, &s, out);
}
int xfs_reader_read(const XfsReader *r, const char *path, uint64_t offset, void *out,
                    uint32_t bytes)
{
    if (bytes && !out)
        return IFS_FS_INVALID;
    Inode inode;
    Search s;
    int rc = resolve(r, path, &inode, &s);
    if (rc)
        return rc;
    if (inode.mode != MODE_REG)
        return IFS_FS_UNSUPPORTED;
    if (offset > inode.size || bytes > inode.size - offset)
        return IFS_FS_INVALID;
    return data_read(r, &inode, offset, out, bytes, false, NULL);
}

/* A read-only boot provider cannot replay the XFS journal. Locate the newest
 * log header and the end of its cycle-stamped data, including wrapped records.
 * Refuse an incomplete record or a last transaction other than clean unmount. */
static int clean_log(const XfsReader *r)
{
    uint64_t start;
    if (physical(r, r->log_start, r->log_blocks, &start))
        return IFS_FS_CORRUPT;
    uint64_t base = start * r->block_bytes;
    uint64_t sectors = (uint64_t)r->log_blocks * (r->block_bytes / 512u);
    if (!sectors || sectors > (UINT64_C(1) << 22))
        return IFS_FS_UNSUPPORTED;
    const unsigned chunk = 65536;
    uint8_t *p = r->host.allocate(r->host.context, chunk);
    if (!p)
        return IFS_FS_NO_MEMORY;
    uint8_t header[512];
    uint64_t latest = 0, last_stamp = 0;
    bool formatted = true;
    int rc = IFS_FS_CORRUPT;
    for (uint64_t s = 0; s < sectors;) {
        unsigned n = sectors - s > chunk / 512u ? chunk / 512u : (unsigned)(sectors - s);
        rc = ifs_fs_host_read_bytes_v2(&r->host, base + s * 512u, p, n * 512u);
        if (rc)
            goto done;
        for (unsigned i = 0; i < n; ++i) {
            uint8_t *h = p + i * 512u;
            uint32_t cycle = xfs_be32(h);
            if (cycle == 0)
                continue;
            uint32_t pos = (uint32_t)(s + i);
            if (cycle == UINT32_C(0xfeedbabe)) {
                cycle = xfs_be32(h + 4);
                uint64_t lsn = xfs_be64(h + 16);
                if ((lsn >> 32) != cycle || (uint32_t)lsn != pos) {
                    rc = IFS_FS_CORRUPT;
                    goto done;
                }
                /* Recovery stamps unused sectors with empty records from the
                 * preceding cycle. Their LSN is valid, but they carry no logops. */
                if (!xfs_be32(h + 12) && !xfs_be32(h + 40) && !xfs_be32(h + 320) &&
                    !memcmp(h + 304, r->uuid, 16))
                    continue;
                if (!cycle) {
                    rc = IFS_FS_CORRUPT;
                    goto done;
                }
                if (lsn > latest) {
                    latest = lsn;
                    memcpy(header, h, 512);
                }
                if (pos || cycle != 1)
                    formatted = false;
            }
            uint64_t stamp = ((uint64_t)cycle << 32) | pos;
            if (stamp > last_stamp)
                last_stamp = stamp;
        }
        s += n;
    }
    rc = IFS_FS_CORRUPT;
    if (!latest || memcmp(header + 304, r->uuid, 16))
        goto done;
    uint32_t version = xfs_be32(header + 8), len = xfs_be32(header + 12),
             size = xfs_be32(header + 320);
    if ((version != 1 && version != 2) || len < 20 || len % 8 || len > 262144)
        goto done;
    if (version == 2 && (size < 16384 || size > 262144 || (size & (size - 1)) || len > size))
        goto done;
    unsigned heads = version == 2 ? (size + 32767) / 32768 : 1;
    uint64_t end = (uint32_t)latest + (uint64_t)heads + (len + 511u) / 512u - 1;
    if (end >= sectors * 2 || (latest >> 32) == UINT32_MAX) {
        rc = IFS_FS_UNSUPPORTED;
        goto done;
    }
    uint64_t end_stamp = (((latest >> 32) + end / sectors) << 32) | (end % sectors);
    if (last_stamp != end_stamp)
        goto done;
    if (xfs_be32(header + 40) != 1) {
        rc = IFS_FS_UNSUPPORTED;
        goto done;
    }
    uint64_t first = ((uint32_t)latest + (uint64_t)heads) % sectors;
    /* The unmount operation is the only log item required, with possible
     * native sector padding. The remaining journal transactions need replay. */
    uint8_t op[512];
    rc = ifs_fs_host_read_bytes_v2(&r->host, base + first * 512u, op, sizeof(op));
    if (rc)
        goto done;
    if (op[8] != 0xaa || op[9] != 0x20) {
        rc = IFS_FS_UNSUPPORTED;
        goto done;
    }
    uint32_t format = xfs_be32(header + 300);
    uint16_t umagic = format == 1 ? infiltratr_load_le16(op + 12) : xfs_be16(op + 12);
    if ((format < 1 || format > 3) || (xfs_be32(op + 4) != 0 && xfs_be32(op + 4) != 8) ||
        umagic != 0x556e) {
        rc = IFS_FS_CORRUPT;
        goto done;
    }
    uint32_t wanted = infiltratr_load_le32(header + 32);
    if (!r->v5) {
        rc = IFS_FS_OK;
        goto done;
    }
    /* xfsprogs clears the journal with a SPECIFIC initial, checksum-less
     * formatting record. A normal v5 kernel unmount record carries a CRC. */
    if (!wanted) {
        rc = formatted && latest == (UINT64_C(1) << 32) && xfs_be64(header + 24) == latest &&
                     xfs_be32(header + 36) == UINT32_MAX &&
                     xfs_be32(header + 44) == UINT32_C(0xb0c0d0d0)
                 ? IFS_FS_OK
                 : IFS_FS_CORRUPT;
        goto done;
    }
    memset(header + 32, 0, 4);
    uint32_t crc = xfs_crc32c_intermediate(header, 328, UINT32_MAX);
    uint32_t crc32 = xfs_crc32c_intermediate(header, 324, UINT32_MAX);
    /* Log CRCs include the meaningful extended header structs followed by
     * the in-place, cycle-stamped payload. Disk padding is excluded. */
    for (unsigned i = 1; i < (len + 32767u) / 32768u; ++i) {
        uint64_t at = ((uint32_t)latest + (uint64_t)i) % sectors;
        rc = ifs_fs_host_read_bytes_v2(&r->host, base + at * 512u, p, 260);
        if (rc)
            goto done;
        crc = xfs_crc32c_intermediate(p, 260, crc);
        crc32 = xfs_crc32c_intermediate(p, 260, crc32);
    }
    uint32_t left = len;
    uint64_t offset = first * 512;
    while (left) {
        uint32_t take = left > chunk ? chunk : left;
        if (take > sectors * 512 - offset)
            take = (uint32_t)(sectors * 512 - offset);
        rc = ifs_fs_host_read_bytes_v2(&r->host, base + offset, p, take);
        if (rc)
            goto done;
        crc = xfs_crc32c_intermediate(p, take, crc);
        crc32 = xfs_crc32c_intermediate(p, take, crc32);
        left -= take;
        offset = (offset + take) % (sectors * 512);
    }
    rc = (~crc == wanted || ~crc32 == wanted) ? IFS_FS_OK : IFS_FS_CORRUPT;
done:
    r->host.release(r->host.context, p);
    return rc;
}

int xfs_reader_check_root(const XfsReader *r)
{
    int rc = clean_log(r);
    if (rc)
        return rc;
    Inode root;
    rc = load_inode(r, r->root, &root);
    if (rc)
        return rc;
    if (root.mode != MODE_DIR)
        return IFS_FS_CORRUPT;
    Search first = {0};
    rc = lookup(r, &root, &first);
    return rc == IFS_FS_END ? IFS_FS_OK : rc;
}
