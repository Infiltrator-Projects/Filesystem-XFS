# Filesystem-XFS
Independent filesystem implementation for InfiltratorOS, using the shared Common ABI.

## Installer files

- `/System/Filesystems/XFS.so`
- `/System/Resources/Documentation/Filesystem-XFS.pdf`
- `/System/Kernel/Filesystems/xfs.ko`

The published Debian package installs these three regular files. A manual native RUN compiles the shared object and Linux adapter locally, generates the PDF, and replaces all three files. It uses the running kernel headers by default. Use `--kernel-build DIRECTORY` for a prepared kernel build with `Module.symvers`. The kernel module is stored for manual use.

InfiltratorOS calls `--system-package-mode` and decides which outputs need updating. Its worker builds a missing or older SO, repairs the PDF independently, and stores the matching released kernel module when available. Use `--build-only --output DIRECTORY` to build a Debian package.

## Common V2 migration (1.0.8)

XFS.so now additionally exports canonical Common V2 CORE probing with
bounded partition-relative host I/O, native XFS geometry decoding, and
v5 superblock CRC validation. The existing native V1 operations remain
until complete V2 namespace, analysis and mutation parity is qualified;
V2 does **not** advertise those operations yet. Both interfaces are
required during the transition to avoid losing existing functionality.

## 1.0.9 qualification correction

The Common V2 XFS v5 superblock checksum now covers its complete on-disk
sector (including 4096-byte sectors), with damaged-media regression tests.
The immutable 1.0.9 release is source-identified, and native release jobs
refuse a tag pointing to a different commit. Existing V1 consumer operations
remain available while their V2 equivalents are qualified.
