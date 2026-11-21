# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(XfsFirmware LANGUAGES C)
add_library(xfs-boot MODULE ../xfs_v2_probe.c ../../native/xfs_disk.c ../../native/xfs_reader.c runtime.c)
set_target_properties(xfs-boot PROPERTIES PREFIX "" OUTPUT_NAME XFS C_VISIBILITY_PRESET hidden)
target_compile_features(xfs-boot PRIVATE c_std_11)
target_include_directories(xfs-boot PRIVATE "${COMMON_INCLUDE_DIR}" ../../native)
target_compile_definitions(xfs-boot PRIVATE IFS_XFS_FIRMWARE=1)
target_compile_options(xfs-boot PRIVATE -Wall -Wextra -Wpedantic -Werror
    -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone
    -fno-tree-loop-distribute-patterns -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
target_link_options(xfs-boot PRIVATE -nostdlib -Wl,-Bsymbolic,-z,defs,-z,noexecstack,-z,relro,-z,now)
