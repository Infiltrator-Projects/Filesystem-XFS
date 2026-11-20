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
