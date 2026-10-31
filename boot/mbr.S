/* boot/mbr.S — 512-byte MBR. BIOS loads us at 0x7C00 (real mode). We load
   stage2 from disk (LBA 1..) to 0x8000 via INT 13h/AH=42h and jump to it.
   Boot params (payload/kernel LBAs + sizes) are patched by build.sh at 0x1A0
   and read by stage2 while the MBR is still resident at 0x7C00. */
.code16
.intel_syntax noprefix
.global _start
_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl        /* BIOS passes boot drive in DL */
    sti

    mov si, offset dap
    mov ah, 0x42                /* extended read (LBA) */
    mov dl, [boot_drive]
    int 0x13
    jc  disk_err
    /* jump to stage2 at 0x8000; pass boot drive in dl */
    mov dl, [boot_drive]
    jmp 0x0000:0x8000

disk_err:
    mov ah, 0x0e
    mov al, 0x45               /* 'E' */
    int 0x10
1:  hlt
    jmp 1b

boot_drive: .byte 0
.align 4
dap:
    .byte 0x10                 /* DAP size */
    .byte 0
    .word 64                   /* sectors to read (stage2 <= 32KiB) */
    .word 0x8000               /* dest offset */
    .word 0x0000               /* dest segment */
    .quad 1                    /* start LBA */

/* boot params patched by build_disk.sh at offset 0x1A0 (read by stage2) */
.org 0x1A0
bp_payload_lba:     .long 0   /* 0x1A0 */
bp_payload_sectors: .word 0   /* 0x1A4 */
                    .word 0   /* 0x1A6 pad */
bp_kernel_lba:      .long 0   /* 0x1A8 */
bp_kernel_sectors:  .word 0   /* 0x1AC */
                    .word 0   /* 0x1AE pad */
bp_kernel_size:     .long 0   /* 0x1B0 */

.org 510
.word 0xAA55
