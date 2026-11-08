// SPDX-License-Identifier: GPL-3.0-or-later
#include "xfs_native.h"
#include "ld_runtime.h"

#include "infiltratr/endian.h"
#include "infiltratr/arithmetic.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int range_compare(const void *left0, const void *right0) {
    const XfsRange *left = left0;
    const XfsRange *right = right0;
    if (left->start < right->start) return -1;
    if (left->start > right->start) return 1;
    if (left->end < right->end) return -1;
    if (left->end > right->end) return 1;
    return 0;
}

static int extent_compare(const void *left0, const void *right0) {
    const XfsExtent *left = left0;
    const XfsExtent *right = right0;
    if (left->logical < right->logical) return -1;
    if (left->logical > right->logical) return 1;
    if (left->physical < right->physical) return -1;
    if (left->physical > right->physical) return 1;
    return 0;
}

uint16_t xfs_be16(const uint8_t *p) {
    return infiltratr_load_be16(p);
}

uint32_t xfs_be32(const uint8_t *p) {
    return infiltratr_load_be32(p);
}

uint64_t xfs_be64(const uint8_t *p) {
    return infiltratr_load_be64(p);
}

void xfs_put_be16(uint8_t *p, uint16_t value) {
    infiltratr_store_be16(p, value);
}

void xfs_put_be32(uint8_t *p, uint32_t value) {
    infiltratr_store_be32(p, value);
}

void xfs_put_be64(uint8_t *p, uint64_t value) {
    infiltratr_store_be64(p, value);
}

/*
 * Immutable reflected CRC32C lookup table.  Building this lazily under an
 * unsynchronised static "ready" flag races when XFS metadata is inspected
 * concurrently by multiple callers of the same shared object.
 */
static const uint32_t xfs_crc32c_table[256] = {
    UINT32_C(0x00000000), UINT32_C(0xf26b8303), UINT32_C(0xe13b70f7), UINT32_C(0x1350f3f4), UINT32_C(0xc79a971f), UINT32_C(0x35f1141c), UINT32_C(0x26a1e7e8), UINT32_C(0xd4ca64eb),
    UINT32_C(0x8ad958cf), UINT32_C(0x78b2dbcc), UINT32_C(0x6be22838), UINT32_C(0x9989ab3b), UINT32_C(0x4d43cfd0), UINT32_C(0xbf284cd3), UINT32_C(0xac78bf27), UINT32_C(0x5e133c24),
    UINT32_C(0x105ec76f), UINT32_C(0xe235446c), UINT32_C(0xf165b798), UINT32_C(0x030e349b), UINT32_C(0xd7c45070), UINT32_C(0x25afd373), UINT32_C(0x36ff2087), UINT32_C(0xc494a384),
    UINT32_C(0x9a879fa0), UINT32_C(0x68ec1ca3), UINT32_C(0x7bbcef57), UINT32_C(0x89d76c54), UINT32_C(0x5d1d08bf), UINT32_C(0xaf768bbc), UINT32_C(0xbc267848), UINT32_C(0x4e4dfb4b),
    UINT32_C(0x20bd8ede), UINT32_C(0xd2d60ddd), UINT32_C(0xc186fe29), UINT32_C(0x33ed7d2a), UINT32_C(0xe72719c1), UINT32_C(0x154c9ac2), UINT32_C(0x061c6936), UINT32_C(0xf477ea35),
    UINT32_C(0xaa64d611), UINT32_C(0x580f5512), UINT32_C(0x4b5fa6e6), UINT32_C(0xb93425e5), UINT32_C(0x6dfe410e), UINT32_C(0x9f95c20d), UINT32_C(0x8cc531f9), UINT32_C(0x7eaeb2fa),
    UINT32_C(0x30e349b1), UINT32_C(0xc288cab2), UINT32_C(0xd1d83946), UINT32_C(0x23b3ba45), UINT32_C(0xf779deae), UINT32_C(0x05125dad), UINT32_C(0x1642ae59), UINT32_C(0xe4292d5a),
    UINT32_C(0xba3a117e), UINT32_C(0x4851927d), UINT32_C(0x5b016189), UINT32_C(0xa96ae28a), UINT32_C(0x7da08661), UINT32_C(0x8fcb0562), UINT32_C(0x9c9bf696), UINT32_C(0x6ef07595),
    UINT32_C(0x417b1dbc), UINT32_C(0xb3109ebf), UINT32_C(0xa0406d4b), UINT32_C(0x522bee48), UINT32_C(0x86e18aa3), UINT32_C(0x748a09a0), UINT32_C(0x67dafa54), UINT32_C(0x95b17957),
    UINT32_C(0xcba24573), UINT32_C(0x39c9c670), UINT32_C(0x2a993584), UINT32_C(0xd8f2b687), UINT32_C(0x0c38d26c), UINT32_C(0xfe53516f), UINT32_C(0xed03a29b), UINT32_C(0x1f682198),
    UINT32_C(0x5125dad3), UINT32_C(0xa34e59d0), UINT32_C(0xb01eaa24), UINT32_C(0x42752927), UINT32_C(0x96bf4dcc), UINT32_C(0x64d4cecf), UINT32_C(0x77843d3b), UINT32_C(0x85efbe38),
    UINT32_C(0xdbfc821c), UINT32_C(0x2997011f), UINT32_C(0x3ac7f2eb), UINT32_C(0xc8ac71e8), UINT32_C(0x1c661503), UINT32_C(0xee0d9600), UINT32_C(0xfd5d65f4), UINT32_C(0x0f36e6f7),
    UINT32_C(0x61c69362), UINT32_C(0x93ad1061), UINT32_C(0x80fde395), UINT32_C(0x72966096), UINT32_C(0xa65c047d), UINT32_C(0x5437877e), UINT32_C(0x4767748a), UINT32_C(0xb50cf789),
    UINT32_C(0xeb1fcbad), UINT32_C(0x197448ae), UINT32_C(0x0a24bb5a), UINT32_C(0xf84f3859), UINT32_C(0x2c855cb2), UINT32_C(0xdeeedfb1), UINT32_C(0xcdbe2c45), UINT32_C(0x3fd5af46),
    UINT32_C(0x7198540d), UINT32_C(0x83f3d70e), UINT32_C(0x90a324fa), UINT32_C(0x62c8a7f9), UINT32_C(0xb602c312), UINT32_C(0x44694011), UINT32_C(0x5739b3e5), UINT32_C(0xa55230e6),
    UINT32_C(0xfb410cc2), UINT32_C(0x092a8fc1), UINT32_C(0x1a7a7c35), UINT32_C(0xe811ff36), UINT32_C(0x3cdb9bdd), UINT32_C(0xceb018de), UINT32_C(0xdde0eb2a), UINT32_C(0x2f8b6829),
    UINT32_C(0x82f63b78), UINT32_C(0x709db87b), UINT32_C(0x63cd4b8f), UINT32_C(0x91a6c88c), UINT32_C(0x456cac67), UINT32_C(0xb7072f64), UINT32_C(0xa457dc90), UINT32_C(0x563c5f93),
    UINT32_C(0x082f63b7), UINT32_C(0xfa44e0b4), UINT32_C(0xe9141340), UINT32_C(0x1b7f9043), UINT32_C(0xcfb5f4a8), UINT32_C(0x3dde77ab), UINT32_C(0x2e8e845f), UINT32_C(0xdce5075c),
    UINT32_C(0x92a8fc17), UINT32_C(0x60c37f14), UINT32_C(0x73938ce0), UINT32_C(0x81f80fe3), UINT32_C(0x55326b08), UINT32_C(0xa759e80b), UINT32_C(0xb4091bff), UINT32_C(0x466298fc),
    UINT32_C(0x1871a4d8), UINT32_C(0xea1a27db), UINT32_C(0xf94ad42f), UINT32_C(0x0b21572c), UINT32_C(0xdfeb33c7), UINT32_C(0x2d80b0c4), UINT32_C(0x3ed04330), UINT32_C(0xccbbc033),
    UINT32_C(0xa24bb5a6), UINT32_C(0x502036a5), UINT32_C(0x4370c551), UINT32_C(0xb11b4652), UINT32_C(0x65d122b9), UINT32_C(0x97baa1ba), UINT32_C(0x84ea524e), UINT32_C(0x7681d14d),
    UINT32_C(0x2892ed69), UINT32_C(0xdaf96e6a), UINT32_C(0xc9a99d9e), UINT32_C(0x3bc21e9d), UINT32_C(0xef087a76), UINT32_C(0x1d63f975), UINT32_C(0x0e330a81), UINT32_C(0xfc588982),
    UINT32_C(0xb21572c9), UINT32_C(0x407ef1ca), UINT32_C(0x532e023e), UINT32_C(0xa145813d), UINT32_C(0x758fe5d6), UINT32_C(0x87e466d5), UINT32_C(0x94b49521), UINT32_C(0x66df1622),
    UINT32_C(0x38cc2a06), UINT32_C(0xcaa7a905), UINT32_C(0xd9f75af1), UINT32_C(0x2b9cd9f2), UINT32_C(0xff56bd19), UINT32_C(0x0d3d3e1a), UINT32_C(0x1e6dcdee), UINT32_C(0xec064eed),
    UINT32_C(0xc38d26c4), UINT32_C(0x31e6a5c7), UINT32_C(0x22b65633), UINT32_C(0xd0ddd530), UINT32_C(0x0417b1db), UINT32_C(0xf67c32d8), UINT32_C(0xe52cc12c), UINT32_C(0x1747422f),
    UINT32_C(0x49547e0b), UINT32_C(0xbb3ffd08), UINT32_C(0xa86f0efc), UINT32_C(0x5a048dff), UINT32_C(0x8ecee914), UINT32_C(0x7ca56a17), UINT32_C(0x6ff599e3), UINT32_C(0x9d9e1ae0),
    UINT32_C(0xd3d3e1ab), UINT32_C(0x21b862a8), UINT32_C(0x32e8915c), UINT32_C(0xc083125f), UINT32_C(0x144976b4), UINT32_C(0xe622f5b7), UINT32_C(0xf5720643), UINT32_C(0x07198540),
    UINT32_C(0x590ab964), UINT32_C(0xab613a67), UINT32_C(0xb831c993), UINT32_C(0x4a5a4a90), UINT32_C(0x9e902e7b), UINT32_C(0x6cfbad78), UINT32_C(0x7fab5e8c), UINT32_C(0x8dc0dd8f),
    UINT32_C(0xe330a81a), UINT32_C(0x115b2b19), UINT32_C(0x020bd8ed), UINT32_C(0xf0605bee), UINT32_C(0x24aa3f05), UINT32_C(0xd6c1bc06), UINT32_C(0xc5914ff2), UINT32_C(0x37faccf1),
    UINT32_C(0x69e9f0d5), UINT32_C(0x9b8273d6), UINT32_C(0x88d28022), UINT32_C(0x7ab90321), UINT32_C(0xae7367ca), UINT32_C(0x5c18e4c9), UINT32_C(0x4f48173d), UINT32_C(0xbd23943e),
    UINT32_C(0xf36e6f75), UINT32_C(0x0105ec76), UINT32_C(0x12551f82), UINT32_C(0xe03e9c81), UINT32_C(0x34f4f86a), UINT32_C(0xc69f7b69), UINT32_C(0xd5cf889d), UINT32_C(0x27a40b9e),
    UINT32_C(0x79b737ba), UINT32_C(0x8bdcb4b9), UINT32_C(0x988c474d), UINT32_C(0x6ae7c44e), UINT32_C(0xbe2da0a5), UINT32_C(0x4c4623a6), UINT32_C(0x5f16d052), UINT32_C(0xad7d5351)
};

uint32_t xfs_crc32c_intermediate(const uint8_t *data, size_t length, uint32_t seed) {
    uint32_t value = seed;
    for (size_t index = 0; index < length; ++index)
        value = xfs_crc32c_table[(value ^ data[index]) & UINT32_C(0xff)] ^ (value >> 8);
    return value;
}

uint32_t xfs_crc_field(const uint8_t *data, size_t length, size_t field) {
    if (data == NULL || field > length || length - field < 4U) return 0;
    uint32_t crc = UINT32_C(0xffffffff);
    crc = xfs_crc32c_intermediate(data, field, crc);
    static const uint8_t zero[4] = {0, 0, 0, 0};
    crc = xfs_crc32c_intermediate(zero, sizeof(zero), crc);
    crc = xfs_crc32c_intermediate(data + field + 4, length - field - 4, crc);
    return ~crc;
}

void xfs_write_crc_le(uint8_t *data, size_t length, size_t field) {
    /* An invalid checksum offset must never write outside its buffer. */
    if (data == NULL || field > length || length - field < 4U)
        return;
    uint32_t crc = xfs_crc_field(data, length, field);
    infiltratr_store_le32(data + field, crc);
}

void xfs_set_error(char **error, const char *format, ...) {
    if (error == NULL) return;
    free(*error);
    *error = NULL;
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return;
    }
    char *message = ld_xmalloc((size_t)needed + 1U);
    (void)vsnprintf(message, (size_t)needed + 1U, format, args);
    va_end(args);
    *error = message;
}

void xfs_clear_error(char **error) {
    if (error == NULL) return;
    free(*error);
    *error = NULL;
}

static void *grow(void *items, size_t *capacity, size_t count, size_t item_size) {
    if (count == SIZE_MAX ||
        !infiltratr_array_reserve(&items, capacity, item_size, count + 1U, 16U))
        ld_die("XFS native vector is too large");
    return items;
}

void xfs_range_push(XfsRangeVec *vec, uint64_t start, uint64_t end) {
    if (start >= end) return;
    vec->items = grow(vec->items, &vec->capacity, vec->count, sizeof(*vec->items));
    vec->items[vec->count++] = (XfsRange){start, end};
}

void xfs_range_sort_merge(XfsRangeVec *vec) {
    if (vec->count < 2) return;
    qsort(vec->items, vec->count, sizeof(*vec->items), range_compare);
    size_t out = 0;
    for (size_t index = 0; index < vec->count; ++index) {
        XfsRange current = vec->items[index];
        if (out != 0 && current.start <= vec->items[out - 1].end) {
            if (current.end > vec->items[out - 1].end) vec->items[out - 1].end = current.end;
        } else {
            vec->items[out++] = current;
        }
    }
    vec->count = out;
}

void xfs_range_free(XfsRangeVec *vec) {
    if (vec == NULL) return;
    free(vec->items);
    memset(vec, 0, sizeof(*vec));
}

void xfs_u64_push(XfsU64Vec *vec, uint64_t value) {
    vec->items = grow(vec->items, &vec->capacity, vec->count, sizeof(*vec->items));
    vec->items[vec->count++] = value;
}

void xfs_u64_free(XfsU64Vec *vec) {
    if (vec == NULL) return;
    free(vec->items);
    memset(vec, 0, sizeof(*vec));
}

void xfs_extent_push(XfsExtentVec *vec, XfsExtent extent) {
    if (extent.length == 0) return;
    vec->items = grow(vec->items, &vec->capacity, vec->count, sizeof(*vec->items));
    vec->items[vec->count++] = extent;
}

void xfs_extent_sort_coalesce(XfsExtentVec *vec) {
    if (vec->count < 2) return;
    qsort(vec->items, vec->count, sizeof(*vec->items), extent_compare);
    size_t out = 0;
    uint64_t previous_logical_end = 0;
    bool have_previous = false;
    for (size_t index = 0; index < vec->count; ++index) {
        XfsExtent current = vec->items[index];
        if (have_previous && current.logical < previous_logical_end)
            ld_die("overlapping XFS file extents");
        if (out != 0) {
            XfsExtent *old = &vec->items[out - 1];
            if (old->logical + old->length == current.logical &&
                old->physical + old->length == current.physical &&
                old->unwritten == current.unwritten) {
                old->length += current.length;
            } else {
                vec->items[out++] = current;
            }
        } else {
            vec->items[out++] = current;
        }
        previous_logical_end = current.logical + current.length;
        have_previous = true;
    }
    vec->count = out;
}

void xfs_extent_free(XfsExtentVec *vec) {
    if (vec == NULL) return;
    free(vec->items);
    memset(vec, 0, sizeof(*vec));
}

void xfs_object_free(XfsObject *object) {
    if (object == NULL) return;
    xfs_extent_free(&object->extents);
    xfs_u64_free(&object->bmap_blocks);
    memset(object, 0, sizeof(*object));
}

void xfs_catalogue_free(XfsCatalogue *catalogue) {
    if (catalogue == NULL) return;
    for (size_t index = 0; index < catalogue->objects.count; ++index)
        xfs_object_free(&catalogue->objects.items[index]);
    free(catalogue->objects.items);
    xfs_range_free(&catalogue->free_ranges);
    xfs_range_free(&catalogue->used_ranges);
    xfs_range_free(&catalogue->fragmented_ranges);
    xfs_range_free(&catalogue->directory_ranges);
    free(catalogue->allocation_groups.items);
    memset(catalogue, 0, sizeof(*catalogue));
}

void xfs_plan_free(XfsPlan *plan) {
    if (plan == NULL) return;
    free(plan->items);
    xfs_range_free(&plan->pool_ranges);
    memset(plan, 0, sizeof(*plan));
}

void xfs_rmap_push(XfsRmapVec *vec, XfsRmapRecord record) {
    vec->items = grow(vec->items, &vec->capacity, vec->count, sizeof(*vec->items));
    vec->items[vec->count++] = record;
}

void xfs_rmap_free(XfsRmapVec *vec) {
    if (vec == NULL) return;
    free(vec->items);
    memset(vec, 0, sizeof(*vec));
}

uint64_t xfs_ag_length(const XfsGeometry *g, uint32_t agno) {
    uint64_t start = (uint64_t)agno * g->agblocks;
    if (start >= g->dblocks) return 0;
    uint64_t remaining = g->dblocks - start;
    return remaining < g->agblocks ? remaining : g->agblocks;
}

uint64_t xfs_ag_offset(const XfsGeometry *g, uint32_t agno, uint32_t agbno) {
    return ((uint64_t)agno * g->agblocks + agbno) * g->block_size;
}

uint64_t xfs_object_block_count(const XfsObject *object) {
    uint64_t total = 0;
    for (size_t index = 0; index < object->extents.count; ++index)
        total += object->extents.items[index].length;
    return total;
}

uint64_t xfs_catalogue_movable_blocks(const XfsCatalogue *catalogue) {
    uint64_t total = 0;
    for (size_t index = 0; index < catalogue->objects.count; ++index)
        if (catalogue->objects.items[index].is_file)
            total += xfs_object_block_count(&catalogue->objects.items[index]);
    return total;
}

bool xfs_range_contains(const XfsRangeVec *ranges, uint64_t start, uint64_t end) {
    for (size_t index = 0; index < ranges->count; ++index)
        if (ranges->items[index].start <= start && end <= ranges->items[index].end)
            return true;
    return false;
}
