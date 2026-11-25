# Filesystem-XFS

## 1. Purpose and format scope

XFS v4 and v5, with native allocation-group structures. This repository owns the format-specific implementation of `XFS.so` for InfiltratorOS.

## 2. On-disk architecture and invariants

Validate superblock geometry, v5 CRC32C, internal log state and inode allocation through the inode B+tree. Shortform and data directories, direct extents and bmap trees govern mapping. Sparse and unwritten ranges must not expose stale media contents. Reject unsupported log formats; ordinary XFS file data has no native checksum.

## 3. Common V2 implementation and capabilities

Common V2 declares probe, mount, stat, list and read, with VOLUME format in hosted scope. The embedded reader uses the same namespace implementation. Qualification includes 4Kn transfers and contradictory geometry with recomputed metadata checksums. The canonical entry point is `infiltrator_filesystem_api_v2`. Consumers must negotiate the actual profile and operation masks and use bounded, partition-relative host I/O. A source-level callback does not by itself establish that every format variant or host profile is qualified.

## 4. Integrity, mutation, allocation and recovery

External logs, realtime volumes, ASCII case-insensitive lookup, symlink traversal and journal replay are outside the documented reader scope. General writable namespace and relocation remain separate qualifications. For destructive operations, require the applicable exclusive writable host, durability contract and format-specific validation. Never silently substitute a kernel or FUSE driver or treat a rejected or unavailable operation as success. Preserve original disk-format semantics; relocation and recovery are separate from ordinary file I/O.

## 5. Build, installation and release

The target installation is `/System/Filesystems/XFS.so` and `/System/Resources/Documentation/Filesystem-XFS.pdf`. The project release policy is a portable amd64 DEB and a CPU-native source-first RUN, with GitHub's automatic source archive; do not treat this policy statement as verification of the current installer payload. The release workflow is the authoritative executable build and package specification. Historical adapters and test artifacts are not additional production filesystem authorities.

## 6. Qualification and limitations

[Recorded qualification workflow](https://github.com/Infiltrator-Projects/Filesystem-XFS/actions/runs/45520161633) · [Current workflows](https://github.com/Infiltrator-Projects/Filesystem-XFS/actions?query=branch%3Amain). A successful workflow establishes the tested scope only, not complete original-format parity, physical-media crash safety, or production BOOT/kernel integration. Use disposable images for destructive qualification.

## 7. Technical references and provenance

- [Provider implementation](module/xfs_v2_probe.c)
- [Native design or source provenance](native/filesystems/xfs/DESIGN.md)
- [Migration and source history](MIGRATION_STATUS.md)
- [Original-format reference source](docs/XFS.pages) and [PDF](docs/XFS.pdf)
- [Project philosophy](docs/PHILOSOPHY.md)
- [Common ABI](https://github.com/Infiltrator-Projects/Libraries)

The original-format reference and historical migration notes describe different evidence from the current production provider. When descriptions disagree, the current exported descriptor, implementation, executable tests and exact-revision release evidence take precedence.
