# XFS Common V2 source and qualification — 22 November 2026

The independent XFS implementation has moved beyond the metadata-only
probe described by early migration snapshots. The production `XFS.so`
now compiles `native/xfs_disk.c`, `native/xfs_reader.c` and
`module/xfs_v2_probe.c` into the Common V2 hosted reader. The same three
files compile into the no-import freestanding ELF from `module/firmware`,
embedded verbatim in the hosted SO's `.infiltrator_boot` section.

## Implemented Common V2 operations

- CORE PROBE, read-only MOUNT/UNMOUNT, STAT, directory LIST and READ.
- VOLUME QUERY with validated on-disk geometry.
- Native XFS v4/v5 allocation-group, inode, extent/BMBT, shortform and
  directory block handling; unsupported layout features fail closed.
- XFS v5 superblock and metadata CRC checks and rejection of volumes
  requiring log replay, unsupported incompatible formats or incomplete
  formatting.
- Partition-relative, sector-aligned host I/O; bounded, independent
  read-only mount sessions. No writable operations are advertised.

The historical XFS worker, formatter fixtures and mutation engine are
retained as test/source material only. They are not linked into
`/System/Filesystems/XFS.so` and do not constitute a V1 fallback, Linux
VFS reader, filesystem-specific kernel driver, or substitute API.

## Source and package qualification

Current tests compile and exercise both native and embedded firmware V2
implementations using independently generated XFS images. They cover
fragmented extents, nested directories, kernel/initramfs byte comparisons,
sector-size variation, deliberately corrupt metadata, dirty journals,
source-media nonmutation, export filtering and portable/native carrier
contents. The release workflow explicitly requires XFS.so and its PDF,
and rejects a published `.ko` asset.

**BOOT execution remains unqualified.** Current BOOT has not implemented
the Common V2 loader and production validator contract, so this
repository cannot claim that BOOT currently accepts or runs XFS.so.
The actual packaged SO still needs future BOOT V2 validation and
disposable OVMF kernel/initramfs handoff, without changing that validator
or bypassing a checksum. Green XFS-specific source and package checks are
a separate milestone, not proof that the system boots from XFS.
