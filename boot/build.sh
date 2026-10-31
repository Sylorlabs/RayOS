#!/usr/bin/env bash
# Build RayOS bare-metal image: 64-bit payload blob embedded in a 32-bit
# multiboot ELF that QEMU -kernel boots. 32-bit stage brings up video and
# switches to long mode; 64-bit payload (@0x200000) runs there.
set -e
cd "$(dirname "$0")"
C32="-m32 -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mno-sse -mno-mmx -Wall -Wextra -O2 -c"
C64="-m64 -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mno-red-zone -mno-sse -mno-mmx -mcmodel=small -Wall -Wextra -O2 -c"

# 64-bit payload -> flat blob linked at 0x200000
gcc $C64 kmain64.c -o kmain64.o
gcc -m64 -c k64boot.S -o k64boot.o
gcc -m64 -c syscall.S -o syscall.o
gcc -m64 -c idt.S -o idt.o
gcc -m64 -c irq.S -o irq.o
gcc $C64 net.c -o net.o
gcc -m32 -c ap_tramp.S -o ap_tramp.o
ld -m elf_i386 -Ttext=0x8000 --oformat=binary -o ap_tramp.bin ap_tramp.o 2>/dev/null || true
gcc -m64 -c ap_blob.S -o ap_blob.o
gcc $C64 smp.c -o smp.o
gcc -m64 -c worker_blob.S -o worker_blob.o
gcc $C64 proc.c -o proc.o
gcc $C64 sched_stub.c -o sched_stub.o
ld -m elf_x86_64 -n -T kernel64.ld -o kernel64.elf k64boot.o kmain64.o syscall.o idt.o irq.o net.o ap_blob.o smp.o sched_stub.o worker_blob.o proc.o
objcopy -O binary kernel64.elf kmain64.bin

# 32-bit stage + embedded payload -> final multiboot ELF
gcc -m32 -c boot.S -o boot.o
gcc $C32 kmain.c -o kmain.o
gcc -m32 -c payload.S -o payload.o
ld -m elf_i386 -n -T linker.ld -o rayos_boot.elf boot.o kmain.o payload.o
echo "built boot/rayos_boot.elf (payload $(stat -c%s kmain64.bin) bytes @0x200000)"
