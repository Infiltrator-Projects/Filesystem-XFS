# XFS Common V2 migration status

The XFS production shared object contains only the native endian/CRC32C
superblock decoder and Common V2 probe, read-only metadata mount sessions
and volume information. The native checksum and geometry logic are unchanged.
Historical XFS AG btree/parser/relayout, worker and fixture source is built
in **noninstalled regression tests**; it is no longer linked into XFS.so.
The installed module exports only `infiltrator_filesystem_api_v2`. The .deb
and source-embedded .run carry only XFS.so and its PDF, without .ko.

**Not a BOOT-ready reader**. Actual supported-feature checks of the XFS
allocation groups, inode btrees, directory formats and extent-based file
reading are not exposed through the Common V2 namespace; no freestanding
payload, production BOOT validator acceptance or OVMF kernel/initramfs
handoff is qualified. No new production release is published.

## 21 November 2026 — malformed and unsupported superblocks

The Common V2 metadata probe now rejects an XFS volume flagged as still
being formatted. For CRC-valid v5 media it also rejects unsupported
incompatible features, including NEEDSREPAIR, rather than claiming a usable
read-only mount. Regression tests write correct CRCs after deliberately
setting unsupported feature bits, then exercise the actual Common V2 CORE
entry point. This is a metadata-only safety fix, not namespace, UEFI or BOOT
qualification. The allocation-group, inode and extent readers are still
required before a BOOT-compatible XFS.so can be released.
