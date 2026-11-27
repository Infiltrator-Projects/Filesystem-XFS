# XFS: technical reference

## History and version boundary

XFS originated at Silicon Graphics for IRIX in the 1990s and entered Linux mainline in the 2.6 era. It uses independently managed allocation groups, extent mappings, B-trees, dynamic inode allocation and a metadata transaction log. This reference is pinned to Linux v6.12's libxfs definitions, distinguishing legacy v4 superblocks from v5 CRC-enabled metadata. A v5 volume may additionally use reverse mappings, shared extents, sparse inode chunks, bigtime and other features. Those flags must be admitted individually. XFS is not a bitmap-based ext2-style allocator; a block's group-relative address, encoded filesystem-block number and physical linear block number are distinct.

XFS was designed for IRIX workstation/server streaming and parallel large-file workloads. The Linux port followed in the early 2000s; allocation groups improve parallelism but add independent metadata and geometry requirements. Ordinary file payload has no native checksum.

## Superblock fields and geometry

Ordinary superblock integers are big-endian. The first superblock begins at volume byte zero; each allocation group begins with another superblock. Magic XFSB (0x58465342) at 0, block bytes 4, 64-bit data/realtime block and realtime extent counts 8/16/24, UUID 32, internal log start 48, root inode 56, realtime bitmap/summary inode 64/72. Realtime extent size 80, blocks per AG 84, AG count 88, realtime bitmap blocks 92, log blocks 96; 16-bit version, sector size, inode size and inodes per block at 100/102/104/106. Label 108..119; block, sector, inode, inodes-per-block and AG-block exponent bytes 120..124. Validate actual AG count, shorter last AG, power-of-two geometry, feature flags and in-progress marker before using any address.

## Superblock statistics and v5 features

64-bit allocated inode count, free inode count, free data blocks and free realtime extents at 128/136/144/152; user and group quota inode numbers 160/168. Quota flags at 176, inode alignment 180, stripe unit and width 184/188, directory block exponent 192, log sector exponent 193, log sector bytes 194 and log stripe unit 196. Features2 at 200 and historical duplicate at 204; v5 compatible, RO-compatible, incompatible and log-incompatible masks at 208/212/216/220. Little-endian CRC at 224, sparse inode alignment 228, project quota inode 232, LSN 240, metadata UUID 248. The ordinary v5 CRC is CRC32C with the format-specific field and coverage rules; do not confuse its byte order with surrounding fields. Unknown log-incompatible bits prohibit safe recovery.

## Encoded block and inode addressing

Let A=AG-block exponent, G=blocks per group and B=block bytes. An encoded fsblock x has AG number x>>A and relative block x & (2^A-1). Its linear physical block is AG*G+relative, not necessarily x. For G=100000, A=17, AG 2 relative block 300, encoded value is 262444, but physical block is 200300. An inode number packs the AG number above A+inopblog low bits. Decode the low bits into AG-relative block and in-block inode index; inode byte address equals (AG*G+agbno)*B+index*inode_size. Verify group boundaries and that the inode chunk actually exists in the inode allocation tree. Never accept an arbitrary encoded inode simply because its computed byte offset fits the image.

## Allocation group headers and free space

At each AG start, the superblock is followed by AGF, AGI and AGFL at sector-relative offsets one, two and three, not necessarily separate filesystem blocks. AGF magic XAGF, version, group number, length at 0/4/8/12; block-number, count and reverse-mapping roots at 16/20/24, levels 28/32/36; free-list first/last/count 40/44/48, free blocks 52, longest run 56, allocation-tree blocks 60; UUID 64. V5 refcount root/level at 88/92. Free extent records contain big-endian startblock and blockcount. The block-number tree orders by start; the count tree orders by length and start. Both describe the same free ranges. AGFL is a reserve for allocation-tree blocks, not ordinary file free space. Check duplicate, overlapping and out-of-group extents and cross-reconcile both trees.

## Inode allocation and orphan state

AGI magic XAGI, version, group number, length at 0/4/8/12, allocated inode count 16, inode-tree root 20, level 24, free count 28, recent inode/dir hints 32/36. Its 64 unlinked-inode buckets begin at 40. The v5 UUID is at 296, CRC 312 and LSN 320, free inode tree root/level 328/332. The inode allocation tree uses chunks of 64 inode identities, with a 64-bit free mask; sparse chunks can have a hole mask and counts replacing the legacy full-chunk fields. A set free bit does not establish that a sparse missing inode slot has a physical record. Cross-check the optional free-inode tree and never reclaim an unlinked but still referenced inode without native recovery rules.

## Dinode core and forks

Inode magic IN at 0, mode 2, version 4, data format 5, legacy links 6, UID/GID 8/12, link count 16, project ID halves 20/22. Atime/mtime/ctime at 32/40/48, size 56, allocated blocks 64, extent-size hint 72, ordinary data/attr extent counts 76/80, attr-fork offset byte 82 and attr format 83, flags 90, generation 92, next unlinked 96. Legacy core size is 100; v3 adds CRC at 100, change count 104, LSN 112, flags2 120, CoW hint 128, creation time 144, self inode 152 and UUID 160, ending at 176. Large extent counts and bigtime alter specific fields under flags. Fork offset is measured in eight-byte units from the literal area. Local, extent, B-tree and device formats are distinct; validate fork bounds and type.

## Packed extent records and example

Each extent mapping is two big-endian 64-bit words w0 and w1. Unwritten flag is w0 bit 63; logical startoff=(w0>>9)&((1<<54)-1). Physical startblock=((w0&0x1ff)<<43)|(w1>>21), length=w1&((1<<21)-1). Startoff and length use filesystem blocks. Example: logical run starts at block 200, length 12, physical start encoded for AG 2 relative block 300; logical block 205 maps to AG 2 relative 305. Translate to physical linear block with the actual AG size, then add in-block offset. Unwritten extents are allocated but read as zero, and absent mappings are holes. Validate ordering, extent length, AG crossing restrictions and EOF; realtime data uses a separate address domain.

## B-tree nodes and inode-root geometry

All ordinary B-tree blocks start with magic at 0, level 4 (16 bits) and record count 6. Legacy short-pointer headers are 16 bytes, long-pointer headers 24; CRC-enabled headers are 56 and 72 respectively, incorporating physical identity, owner, LSN, UUID and checksum. Short pointers are AG-relative; long mapping pointers are encoded fsblocks. Leaf records and internal key/pointer arrays have different geometry; pointer-array position depends on maximum records rather than used count. The data-fork B-tree root is a compact in-inode structure with level and count followed by keys and pointers. Verify exact magic and revision, node bounds, monotone keys, parent ranges, level transitions and cycles. Do not use sizeof the combined in-memory header as a disk-header size.

## Directory and extended-attribute formats

Small directories may store shortform data directly in the inode, with compact parent and inode numbers selected by the shortform header. Larger directories use block, leaf and node forms. Dir2 data headers occupy 16 bytes; dir3 adds CRC, block identity, LSN, UUID and owner for a 64-byte data header. A live data entry contains 64-bit inode, name length byte, name bytes, optional filetype and trailing 16-bit offset tag, aligned to eight bytes. A free record starts with 0xffff and a length and has its own trailing offset tag. Validate tags and bounds, and use the native hashed leaf entries to locate candidates rather than treating hashes as unique names. Extended attributes have local and remote representations and separate fork metadata; preserve names, values, ACLs and security namespaces.

## Reverse maps, reflink and allocation ownership

V5 reverse mapping can associate physical extents with owner inode or metadata identity and logical offset; reference-count trees describe shared extents. A duplicate physical reference is not necessarily corruption when reflink is admitted. The forward mapping, reverse mapping and reference counts must agree before relocation or reclamation. Delayed allocation is a runtime reservation and must not be fabricated as a persistent physical run. Realtime volumes require their own bitmap/summary and device identity. A defrag plan must preserve extent state, sharing, data and attr fork mappings, allocation-group counters, and journal ordering; a simple free-space scan cannot establish safe ownership.

## Implementation and qualification boundary

At historical revision f91c0ee203d8 the hosted and embedded reader validated v4/v5 geometry, internal log state, inode allocation trees, shortform/data directories and direct/bmap mappings; hosted FORMAT was separate. Tests covered 4Kn and contradictory geometry with recomputed metadata CRC. External logs, realtime volumes, ASCII-insensitive lookup, symlink traversal and journal replay were outside that baseline reader scope.

Common V2 is the independent provider interface: https://github.com/Infiltrator-Projects/Libraries/blob/main/docs/FILESYSTEM_COMMON_ABI_V2.md . Negotiate real operation masks and media profiles. Use bounded host I/O, exclusive unmounted ownership and durable flush for mutation; distinguish unsupported, corrupt, I/O and stale-handle results. Test Media and Defragmenter use the same engine. Hosted, freestanding, BOOT, kernel, recovery and package qualification are independent. Current capability and release status belongs to the repository README and exact-source qualification records; historical evidence is not a current status claim.

Historical source or test reference: https://github.com/Infiltrator-Projects/Filesystem-XFS/tree/f91c0ee203d8 .

## Journal, recovery and validation

The internal or external XFS log stores metadata transactions and intent/completion items. Recovery must establish log head/tail, cycle and record sequence, validate committed items, and apply native buffer/inode replay and deferred operations before trusting a dirty namespace. CRC checks alone do not establish transaction consistency. A writer must reserve space and log capacity, copy verified data and publish changed mappings and allocator metadata under native transaction rules; do not retire blocks until recovery can safely establish ownership. Exact log record and item layouts require further dedicated source and crash-fixture validation. Sources: https://github.com/torvalds/linux/tree/v6.12/fs/xfs (libxfs/xfs_format.h, xfs_da_format.h, xfs_bmap_btree.c, xfs_bmap.c and log recovery sources). Test independent xfsprogs images with v4/v5, sparse chunks, non-power-of-two AG size, forks, unwritten extents, reflinks, multiple devices and interrupted relocation.
