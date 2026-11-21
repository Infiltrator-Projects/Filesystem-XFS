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
