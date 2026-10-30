# XFS shared-object migration — 2026-10-30

XFS filesystem analysis, metadata parsing, planning, mutation, and fixture creation are owned by this canonical repository. `XFS.so` is built from `native/`, `module/`, and `fixtures/`. It exports `infiltrator_filesystem_xfs_v1` with run and fixture callbacks. Defragmenter loads it through a filesystem-neutral host, including the test-media path.

The native regression sources were imported from Defragmenter commit `30af892d8897d3aeaeefeac1bfcdb9620a0a5c72`. The module build now owns `xfs-module-contract`, `xfs-native`, and `xfs-metadata` tests; CI results remain the source of truth for qualification. The native implementation must not also be compiled into Defragmenter.

The separate XFS Linux kernel VFS code remains in the Filesystems repository. Its presence does not imply that BOOT/kernel adapters are fully qualified. Do not claim that live-media writes or crash recovery are qualified without their corresponding independent end-to-end test evidence.
