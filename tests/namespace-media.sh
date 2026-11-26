#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
reader=$(realpath "${1:?Common V2 media test}")
carrier=$(realpath "${2:?XFS carrier}")
firmware=${3:-}
if [[ -n "$firmware" ]]; then firmware=$(realpath "$firmware"); fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
mkdir -p source/{many,boot}
if [[ -n ${TEST_KERNEL:-} ]]; then cp -- "$TEST_KERNEL" source/boot/vmlinuz
else dd if=/dev/urandom of=source/boot/vmlinuz bs=1M count=3 status=none; fi
if [[ -n ${TEST_INITRAMFS:-} ]]; then cp -- "$TEST_INITRAMFS" source/boot/initramfs
else dd if=/dev/urandom of=source/boot/initramfs bs=128K count=1 status=none; fi
printf 'XFS sample file\n' > source/boot/inline
{
    printf 'boot\n0 0\nd--755 0 0\nboot d--755 0 0\n'
    for name in vmlinuz initramfs inline; do
        printf '%s ---644 0 0 %s/source/boot/%s\n' "$name" "$work" "$name"
    done
    printf '$\nmany d--755 0 0\n'
    for ((i=0;i<512;i++)); do
        printf '%d\n' "$i" > "source/many/file-$i"
        printf 'file-%d ---644 0 0 %s/source/many/file-%d\n' "$i" "$work" "$i"
    done
    printf '$\n$\n'
} > proto
objcopy --dump-section .infiltrator_boot=reader.so "$carrier" carrier-copy.so
if [[ -n "$firmware" ]]; then cmp "$firmware" reader.so; fi
truncate -s 320M media.img
mkfs.xfs -q -f -m rmapbt=1 -p proto media.img
xfs_repair -n -f media.img >/dev/null
sha256sum media.img > media.sha256
for module in "$carrier" "$work/reader.so"; do
    for sector in 512 4096; do
        if ! "$reader" "$module" media.img "$work/source" "$sector" clean; then
            # Independent allocation-accounting evidence on a real-media
            # failure; never hide the provider's failed return code.
            xfs_db -r -c 'sb 0' -c 'p fdblocks agcount blocksize features2' media.img
            for ag in 0 1 2 3; do
                xfs_db -r -c "agf $ag" -c 'p' media.img
                xfs_db -r -c "agfl $ag" -c 'p' media.img
            done
            exit 1
        fi
    done
done
# Locate metadata independently using xfs_db and the allocation group layout.
inode=$(xfs_db -r -c 'path /boot/vmlinuz' -c inode media.img | awk '/current inode number/{print $5}')
test "$inode" -gt 0
python3 - "$inode" <<'PY'
from pathlib import Path
import sys, subprocess
with open('media.img','rb') as src:
    sb=src.read(512)
def be(at,n): return int.from_bytes(sb[at:at+n], 'big')
bs,agblocks=be(4,4),be(84,4)
def fsb(addr): return (addr>>sb[124])*agblocks+(addr&((1<<sb[124])-1))
ino=int(sys.argv[1])
inodeat=fsb(ino>>sb[123])*bs+(ino&((1<<sb[123])-1))*be(104,2)
log=fsb(be(48,8))*bs
# Locate the actual multi-block reverse map tree using the native AGF,
# independently of the provider. Refuse a fixture that cannot reproduce
# the original reverse-map accounting defect.
with open('media.img','rb') as src:
    rmap_root=None
    for ag in range(be(88,4)):
        src.seek(ag*agblocks*bs+be(102,2))
        agf=src.read(be(102,2))
        if int.from_bytes(agf[80:84],'big')>1:
            rmap_root=(ag*agblocks+int.from_bytes(agf[24:28],'big'))*bs
            break
    assert rmap_root is not None
for name,pos in [('bad-inode',inodeat+100),('bad-tree',2*be(102,2)+312),('dirty-log',log+512+9),('bad-rmap',rmap_root+52)]:
    path=Path(name+'.img')
    subprocess.run(['cp','--sparse=always','--reflink=auto','media.img',path],check=True)
    with path.open('r+b') as f:
        f.seek(pos); original=f.read(1); assert len(original)==1
        f.seek(pos); f.write(bytes([original[0]^1]))
PY
for name in bad-inode bad-tree dirty-log; do
    sha256sum "$name.img" > "$name.sha256"
    for module in "$carrier" "$work/reader.so"; do
        "$reader" "$module" "$name.img" "$work/source" 512 "$name"
    done
    sha256sum -c "$name.sha256"
done
# RMAP allocation inventory is a hosted-only capability; reject the
# damaged CRC through the same mount-corruption assertion as bad-tree.
sha256sum bad-rmap.img > bad-rmap.sha256
"$reader" "$carrier" bad-rmap.img "$work/source" 512 bad-tree
sha256sum -c bad-rmap.sha256
sha256sum -c media.sha256
truncate -s 320M v4.img
mkfs.xfs -q -f -m crc=0 -p proto v4.img
sha256sum v4.img > v4.sha256
for module in "$carrier" "$work/reader.so"; do
    "$reader" "$module" v4.img "$work/source" 512 clean
done
sha256sum -c v4.sha256
