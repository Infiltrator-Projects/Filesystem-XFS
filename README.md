# Filesystem-XFS

The XFS provider exports `infiltrator_filesystem_api_v2`. It detects XFS v4 and v5, checks superblock geometry, verifies the complete v5 superblock CRC32C and reports recorded volume capacity through read-only Common V2 sessions. Namespace operations and a freestanding execution profile are pending.

The portable Debian package and CPU-native installer built from this revision install `/System/Filesystems/XFS.so` and `/System/Resources/Documentation/Filesystem-XFS.pdf`. Historical metadata and mutation engines remain in the source tree and its regression tests.

CI formats disposable images with xfsprogs, probes and mounts them through the packaged V2 interface, and verifies their hashes. It tests both damaged checksums and independently re-checksummed contradictory geometry.
