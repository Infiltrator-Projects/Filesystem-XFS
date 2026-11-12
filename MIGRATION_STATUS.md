# XFS shared-object migration — 2026-10-30

XFS filesystem analysis, metadata parsing, planning, mutation, and fixture creation are owned by this canonical repository. `XFS.so` is built from `native/`, `module/`, and `fixtures/`. It exports `infiltrator_filesystem_xfs_v1` with run and fixture callbacks. Defragmenter loads it through a filesystem-neutral host, including the test-media path.

The native regression sources were imported from Defragmenter commit `30af892d8897d3aeaeefeac1bfcdb9620a0a5c72`. The module build now owns `xfs-module-contract`, `xfs-native`, and `xfs-metadata` tests; CI results remain the source of truth for qualification. The native implementation must not also be compiled into Defragmenter.

The separate XFS Linux kernel VFS code remains in the Filesystems repository. Its presence does not imply that BOOT/kernel adapters are fully qualified. Do not claim that live-media writes or crash recovery are qualified without their corresponding independent end-to-end test evidence.

## Strict Common V2 read-only XFS volume lifecycle (1.0.10)
Independent XFS now provides CORE.PROBE, CORE.MOUNT/UNMOUNT and
VOLUME.QUERY through canonical on-disk superblock decoding over Common V2
read-only host callbacks. V5 CRC verification checks the source-owned
full disk sector, while recorded free blocks are range-checked against
volume size. The free-space number is **recorded metadata**, not a scan
of free-space Btrees or AG ownership. No UFS or XFS parsing was duplicated
inside Defragmenter. The independent native XFS analyser and experimental
writer remain unchanged and no V2 DEFRAG MAP/PLAN/EXECUTE/VERIFY/RECOVER
is advertised until qualified. Bad CRC/geometry, stale sessions and
non-readonly mounts fail closed.
