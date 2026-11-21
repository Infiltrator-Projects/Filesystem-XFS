#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# A separate Linux guest creates two independent XFS test fixtures. The
# provider subsequently opens each raw file read-only, outside that guest.
set -euo pipefail
kernel=$(realpath "${1:?guest kernel}")
modules=$(realpath "${2:?kernel module tree}")
out=$(realpath -m "${3:?fixture output directory}")
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
mkdir -p ram/{bin,dev,proc,sys,mnt,mods}
cp "$(command -v busybox)" ram/bin/busybox
if readelf -lW ram/bin/busybox | grep INTERP >/dev/null; then exit 1; fi
python3 - "$modules" <<'PY'
from pathlib import Path
import re, shutil, subprocess, sys
tree=Path(sys.argv[1])
known={p.name.split('.ko')[0].replace('-','_'):p for p in tree.rglob('*.ko*') if p.is_file()}
order=[]
def collect(name):
    name=name.replace('-','_')
    if name not in known or name in order:
        return
    path=known[name]
    output=Path('ram/mods')/(name+'.ko')
    commands={'.xz':'xz', '.zst':'zstd', '.gz':'gzip'}
    if path.suffix in commands:
        with output.open('wb') as stream:
            subprocess.run([commands[path.suffix],'-cd',str(path)],stdout=stream,check=True)
    else:
        shutil.copyfile(path,output)
    info=subprocess.check_output(['readelf','-p','.modinfo',str(output)],text=True)
    dependencies=re.search(r'\bdepends=([^\n]*)', info)
    if dependencies:
        for item in dependencies[1].split(','):
            if item.strip(): collect(item.strip())
    order.append(name)
for mod in ('virtio_pci','virtio_blk','crc32c_generic','crc32c_intel','xfs'):
    collect(mod)
Path('ram/load').write_text(''.join('/bin/busybox insmod /mods/'+n+'.ko\n' for n in order))
PY
cat > ram/init <<'GUEST'
#!/bin/busybox sh
set -e
b=/bin/busybox
$b mount -t devtmpfs dev /dev
exec </dev/ttyS0 >/dev/ttyS0 2>&1
$b mount -t proc proc /proc
$b mount -t sysfs sys /sys
. /load
$b mount -t xfs /dev/vda /mnt
$b mkdir -p /mnt/fragmented
i=0
while [ "$i" -lt 64 ]; do
    $b printf 'written segment %d' "$i" | $b dd of=/mnt/fragmented/extent bs=1 seek=$((i*8192)) conv=notrunc 2>/dev/null
    i=$((i+1))
done
$b sync
if $b grep -qw dirty_xfs /proc/cmdline; then
    $b touch /mnt/pending
    $b sync
    echo XFS_IMAGE_ACTIVE
    $b poweroff -f
fi
$b umount /mnt
echo XFS_IMAGE_CLEAN
$b poweroff -f
GUEST
chmod 0755 ram/init
(cd ram && find . -print0 | cpio --null -o -H newc --quiet | gzip -1) > guest.cpio.gz
truncate -s 320M "$out/fragmented.img"
mkfs.xfs -q -f "$out/fragmented.img"
qemu=(qemu-system-x86_64 -machine q35,accel=tcg -cpu max -m 768 -display none
      -monitor none -net none -no-reboot -kernel "$kernel" -initrd "$PWD/guest.cpio.gz")
if [[ -n ${QEMU_DATA_DIR:-} ]]; then qemu+=(-L "$QEMU_DATA_DIR" -bios "$QEMU_DATA_DIR/bios-256k.bin"); fi
for stage in fragmented dirty-log; do
    extra='' marker=XFS_IMAGE_CLEAN
    if [[ $stage == dirty-log ]]; then
        extra=dirty_xfs
        marker=XFS_IMAGE_ACTIVE
        cp --reflink=auto --sparse=always "$out/fragmented.img" "$out/dirty-log.img"
    fi
    timeout 90 "${qemu[@]}" -append "console=ttyS0 rdinit=/init panic=1 quiet $extra" \
        -serial "file:$out/$stage.log" -drive "file=$out/$stage.img,format=raw,if=virtio"
    grep -F "$marker" "$out/$stage.log"
done
format=$(xfs_db -r -c 'path /fragmented/extent' -c 'p core.format' "$out/fragmented.img")
[[ $format == *'(btree)'* ]]
xfs_logprint -t "$out/fragmented.img" > "$out/clean-journal.txt"
xfs_logprint -t "$out/dirty-log.img" > "$out/dirty-journal.txt"
grep 'state: <CLEAN>' "$out/clean-journal.txt"
grep 'state: <DIRTY>' "$out/dirty-journal.txt"

cp --reflink=auto --sparse=always "$out/fragmented.img" "$out/bad-bmap.img"
xfs_db -x -c 'path /fragmented/extent' -c 'addr u3.bmbt.ptrs[1]' \
    -c 'write -d owner 99999' "$out/bad-bmap.img"
changed=$(xfs_db -r -c 'path /fragmented/extent' -c 'addr u3.bmbt.ptrs[1]' -c 'p owner crc' "$out/bad-bmap.img")
[[ $changed == *'owner = 99999'* && $changed == *'(correct)'* ]]
