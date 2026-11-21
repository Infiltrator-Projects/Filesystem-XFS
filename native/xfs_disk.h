/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INFILTRATOR_XFS_DISK_H
#define INFILTRATOR_XFS_DISK_H
#include <stddef.h>
#include <stdint.h>

uint16_t xfs_be16(const uint8_t *p);
uint32_t xfs_be32(const uint8_t *p);
uint64_t xfs_be64(const uint8_t *p);
void xfs_put_be16(uint8_t *p, uint16_t value);
void xfs_put_be32(uint8_t *p, uint32_t value);
void xfs_put_be64(uint8_t *p, uint64_t value);
uint32_t xfs_crc32c_intermediate(const uint8_t *data, size_t length, uint32_t seed);
uint32_t xfs_crc_field(const uint8_t *data, size_t length, size_t field);
void xfs_write_crc_le(uint8_t *data, size_t length, size_t field);
#endif
