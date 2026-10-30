# XFS migration — 2026-10-30

Source import only. Existing Defragmenter XFS native analysis/relayout code copied unchanged from Defragmenter commit 30af892d8897d3aeaeefeac1bfcdb9620a0a5c72. Format design is from Filesystems commit ca2fa1210c3c1f2b6f52cd15b3da6dfa83d453ad.

The much larger XFS Linux VFS implementation remains in Filesystems/native/filesystems/xfs/linux. These Defragmenter source files depend on Defragmenter's host services and do NOT yet build a standalone XFS.so. The module needs canonical integration, Common ABI adaptation, independent media tests and qualified BOOT/kernel adapters before original sources or consumers can be changed.
