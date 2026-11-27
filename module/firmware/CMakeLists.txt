# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(XfsFirmware LANGUAGES C)
add_library(xfs-boot MODULE ../xfs_v2_probe.c ../../native/xfs_disk.c ../../native/xfs_reader.c runtime.c)
set_target_properties(xfs-boot PROPERTIES PREFIX "" OUTPUT_NAME XFS C_VISIBILITY_PRESET hidden)
target_compile_features(xfs-boot PRIVATE c_std_11)
target_include_directories(xfs-boot PRIVATE "${COMMON_INCLUDE_DIR}" ../../native)
target_compile_definitions(xfs-boot PRIVATE IFS_XFS_FIRMWARE=1)
# Firmware uses AMD64 SysV without CET, SIMD state assumptions,
# packed relative relocations or non-4-KiB load alignment. Hosted builds
# retain their separate native optimization profile.
target_compile_options(xfs-boot PRIVATE -Wall -Wextra -Wpedantic -Werror
    -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fcf-protection=none -mgeneral-regs-only
    -fno-tree-loop-distribute-patterns -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
target_link_options(xfs-boot PRIVATE -nostdlib -Wl,-z,nopack-relative-relocs,-z,max-page-size=4096,-Bsymbolic,-z,defs,-z,noexecstack,-z,relro,-z,now)
