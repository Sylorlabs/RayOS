#!/usr/bin/env bash
# Build a raw bootable disk image (BIOS/VESA) that runs RayOS on real hardware.
# Layout: [MBR][stage2 @LBA1..64][payload @LBA65..][flashz kernel ..]
set -e
cd "$(dirname "$0")"
./build.sh >/dev/null              # produces kmain64.bin (the 64-bit payload)
gcc -m32 -c mbr.S -o mbr.o
ld -m elf_i386 -Ttext=0x7c00 --oformat=binary -o mbr.bin mbr.o
gcc -m32 -c stage2.S -o stage2.o
ld -m elf_i386 -Ttext=0x8000 --oformat=binary -o stage2.bin stage2.o

KERNEL="${1:-../.test_bin/rayos_desktop_live}"
PAYLOAD=kmain64.bin
ceil(){ echo $(( ($1 + 511) / 512 )); }
psize=$(stat -c%s $PAYLOAD);  psec=$(ceil $psize)
ksize=$(stat -c%s $KERNEL);   ksec=$(ceil $ksize)
STAGE2_LBA=1; STAGE2_SEC=64
PAYLOAD_LBA=$((STAGE2_LBA+STAGE2_SEC))
KERNEL_LBA=$((PAYLOAD_LBA+psec))
TOTAL=$((KERNEL_LBA+ksec+16))

python3 - "$PAYLOAD_LBA" "$psec" "$KERNEL_LBA" "$ksec" "$ksize" <<'PY'
import sys,struct
pl,ps,kl,ks,ksz=map(int,sys.argv[1:6])
b=bytearray(open("mbr.bin","rb").read())
struct.pack_into("<IHxxIHxxI", b, 0x1A0, pl,ps, kl,ks, ksz)
open("mbr.bin","wb").write(b)
PY

dd if=/dev/zero of=disk.img bs=512 count=$TOTAL 2>/dev/null
dd if=mbr.bin    of=disk.img conv=notrunc                     2>/dev/null
dd if=stage2.bin of=disk.img bs=512 seek=$STAGE2_LBA conv=notrunc 2>/dev/null
dd if=$PAYLOAD   of=disk.img bs=512 seek=$PAYLOAD_LBA conv=notrunc 2>/dev/null
dd if=$KERNEL    of=disk.img bs=512 seek=$KERNEL_LBA  conv=notrunc 2>/dev/null
echo "disk.img ($((TOTAL/2)) KiB): payload LBA $PAYLOAD_LBA ($psec sec), kernel LBA $KERNEL_LBA ($ksec sec, $ksize bytes)"
