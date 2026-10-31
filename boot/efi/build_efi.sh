#!/usr/bin/env bash
# Build a self-contained UEFI PE32+ app embedding the RayOS kernel. No gnu-efi.
set -e
cd "$(dirname "$0")"
( cd .. && ./build.sh >/dev/null )          # produce ../kmain64.bin
CF="-ffreestanding -fno-stack-protector -fno-stack-check -fpic -fshort-wchar -mno-red-zone -Wall -O2 -c"
gcc $CF efi_main.c -o efi_main.o
gcc -c blobs.S -o blobs.o
ld -nostdlib -znocombreloc -shared -Bsymbolic -e efi_main -T efi.lds -o efi_main.so efi_main.o blobs.o
if readelf -r efi_main.so 2>/dev/null | grep -qE 'R_X86_64'; then
  echo "ERROR: dynamic relocations present"; readelf -r efi_main.so; exit 1
fi
objcopy -O binary efi_main.so efi_flat.bin
ENTRY=$(readelf -s efi_main.so | awk '/ efi_main$/{print $2}' | head -1)
python3 - "$ENTRY" <<'PY'
import sys,struct
entry=int(sys.argv[1],16)
img=open("efi_flat.bin","rb").read()
FA=0x200; SA=0x1000; SEC_RVA=0x1000; IMAGEBASE=0x08000000   # 128MiB: clear of kernel addrs
def al(v,a): return (v+a-1)//a*a
vsize=len(img); raw=al(len(img),FA)
hdr_end=0x80+4+20+0xF0+40; SizeOfHeaders=al(hdr_end,FA)
SizeOfImage=al(SEC_RVA+vsize,SA)
dos=bytearray(0x80); dos[0:2]=b'MZ'; struct.pack_into("<I",dos,0x3c,0x80)
coff=struct.pack("<HHIIIHH",0x8664,1,0,0,0,0xF0,0x022E)
opt=struct.pack("<HBBIIIII",0x20b,0,0,al(len(img),FA),0,0,SEC_RVA+entry,SEC_RVA)
opt+=struct.pack("<Q",IMAGEBASE)+struct.pack("<II",SA,FA)
opt+=struct.pack("<HHHHHH",0,0,0,0,0,0)+struct.pack("<I",0)
opt+=struct.pack("<II",SizeOfImage,SizeOfHeaders)+struct.pack("<I",0)
opt+=struct.pack("<HH",10,0)+struct.pack("<QQQQ",0x100000,0x1000,0x100000,0x1000)
opt+=struct.pack("<II",0,16)+b'\x00'*(16*8)
sec=struct.pack("<8sIIIIIIHHI",b".text",vsize,SEC_RVA,raw,SizeOfHeaders,0,0,0,0,0xE0000020)
out=bytearray(dos+b'PE\x00\x00'+coff+opt+sec); out+=b'\x00'*(SizeOfHeaders-len(out))
out+=img+b'\x00'*(raw-len(img))
open("BOOTX64.EFI","wb").write(out)
print("PE ImageBase 0x%x, entry RVA 0x%x, %d bytes"%(IMAGEBASE,SEC_RVA+entry,len(out)))
PY
mkdir -p esp/EFI/BOOT; cp BOOTX64.EFI esp/EFI/BOOT/BOOTX64.EFI
echo "built self-contained BOOTX64.EFI ($(stat -c%s BOOTX64.EFI) bytes)"
