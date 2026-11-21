# Filesystem-XFS

The XFS provider exports `infiltrator_filesystem_api_v2`. It detects XFS v4 and v5, checks superblock geometry, verifies the complete v5 superblock CRC32C and provides read-only mounting, file status, path traversal, directory enumeration and file reads. The installed carrier embeds the same reader in an import-free firmware ELF.

The reader checks the internal journal for a clean unmount, verifies allocation through the inode B+tree and reads shortform and data directories. It follows direct extents and native bmap trees, including sparse and unwritten ranges. V5 metadata checksums cover the superblock, allocation metadata, inodes, directory blocks, bmap blocks and journal records used by the reader. Regular XFS file data has no native checksum. External logs, realtime volumes, ASCII case-insensitive lookup, symlink traversal and journal replay are unsupported.

The portable Debian package and CPU-native installer built from this revision install `/System/Filesystems/XFS.so` and `/System/Resources/Documentation/Filesystem-XFS.pdf`. Historical metadata and mutation engines remain in the source tree and its regression tests.

CI formats disposable images with xfsprogs, probes and mounts them through the packaged V2 interface, and verifies their hashes. A disposable Linux guest creates a fragmented sparse file and a separate volume requiring replay. CI compares the kernel and initramfs through the hosted and extracted firmware ELF for both package profiles. Production BOOT acceptance is pending. It tests both damaged checksums and independently re-checksummed contradictory geometry.
