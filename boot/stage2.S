/* boot/stage2.S — real-mode -> long-mode boot stage for real hardware / BIOS.
   Loaded at 0x8000 by the MBR. Sets a VESA VBE linear framebuffer (universal,
   works on real GPUs + QEMU SeaBIOS), loads the payload + flashz kernel off
   disk via INT 13h (unreal-mode high copies), synthesizes a multiboot info so
   the 64-bit kmain64 runs UNMODIFIED, then enters long mode and jumps to it. */
.code16
.intel_syntax noprefix
.text
.global stage2_start
stage2_start:
    mov [drive], dl
    call serial_init
    lea si, [msg_hi]
    call sputs

    call vbe_setup
    /* fast A20 */
    in al, 0x92
    or al, 2
    out 0x92, al
    call enter_unreal
    call load_all
    call synth_mb
    lea si, [msg_lm]
    call sputs
    call go_long
1:  hlt
    jmp 1b

/* ── serial (COM1 0x3F8) ─────────────────────────────────────────────── */
serial_init:
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 0x03
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    ret
sputc:                       /* al = char */
    push ax
    push dx
2:  mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz 2b
    pop dx
    pop ax
    mov dx, 0x3F8
    out dx, al
    ret
sputs:                       /* si -> asciiz */
3:  lodsb
    test al, al
    jz 4f
    call sputc
    jmp 3b
4:  ret

/* ── VBE: find + set a 32bpp linear mode, publish vinfo at 0x5000 ─────── */
vbe_setup:
    /* get controller info -> 0x0600 */
    mov ax, 0x4F00
    mov di, 0x0600
    mov dword ptr [0x0600], 0x32454256   /* 'VBE2' */
    int 0x10
    cmp ax, 0x004F
    jne vbe_fail
    /* mode list far ptr at [0x0600+14] (off), [0x0600+16] (seg) */
    mov si, [0x060E]
    mov ax, [0x0610]
    mov es, ax
    xor bx, bx               /* best mode = none */
    mov [best_score], word ptr 0
vbe_iter:
    mov cx, es:[si]
    cmp cx, 0xFFFF
    je vbe_pick
    add si, 2
    push si
    push es
    mov ax, 0x4F01
    mov di, 0x0700
    int 0x10
    pop es
    pop si
    cmp ax, 0x004F
    jne vbe_iter
    mov ax, [0x0700]         /* attributes */
    test ax, 0x80            /* LFB */
    jz vbe_iter
    test ax, 0x10            /* graphics */
    jz vbe_iter
    cmp byte ptr [0x0719], 32
    jne vbe_iter
    /* score = width (prefer widest up to 1280) */
    mov ax, [0x0712]         /* XRes */
    cmp ax, 1280
    ja vbe_iter              /* skip >1280 */
    cmp ax, [best_score]
    jbe vbe_iter
    mov [best_score], ax
    mov [best_mode], cx
    jmp vbe_iter
vbe_pick:
    mov cx, [best_mode]
    test cx, cx
    jz vbe_fail
    /* query chosen mode again to fill 0x0700 */
    mov ax, 0x4F01
    mov di, 0x0700
    int 0x10
    /* set it, LFB bit 0x4000 */
    mov bx, [best_mode]
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne vbe_fail
    /* publish vinfo @0x5000: lfb,w,h,pitch */
    mov eax, [0x0728]        /* PhysBasePtr */
    mov [0x5000], eax
    movzx eax, word ptr [0x0712]
    mov [0x5004], eax
    movzx eax, word ptr [0x0714]
    mov [0x5008], eax
    movzx eax, word ptr [0x0710]  /* BytesPerScanLine */
    mov [0x500C], eax
    lea si, [msg_vbe]
    call sputs
    ret
vbe_fail:
    lea si, [msg_vbefail]
    call sputs
5:  hlt
    jmp 5b

/* ── unreal mode: fs = base0/limit4G so we can copy to >1MB ───────────── */
enter_unreal:
    cli
    push ds
    lgdt [unreal_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    mov bx, 0x08
    mov fs, bx
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    pop ds
    sti
    ret

/* ── disk load: payload -> 0x200000, kernel -> 0x800000 ───────────────── */
load_all:
    /* payload: params at 0x7DA0 (MBR+0x1A0) */
    mov eax, [0x7DA0]        /* payload lba */
    movzx ecx, word ptr [0x7DA4]
    mov edx, 0x200000
    call load_range
    mov eax, [0x7DA8]        /* kernel lba */
    movzx ecx, word ptr [0x7DAC]
    mov edx, 0x800000
    call load_range
    lea si, [msg_load]
    call sputs
    ret

/* load_range: eax=lba(32), ecx=sectors, edx=dest_phys. reads 32KiB chunks to
   0x10000 (real-mode buffer) then fs-copies them up. */
load_range:
    mov [lr_lba], eax
    mov [lr_left], ecx
    mov [lr_dst], edx
lr_loop:
    mov ecx, [lr_left]
    test ecx, ecx
    jz lr_done
    cmp ecx, 64
    jbe 6f
    mov ecx, 64
6:  mov [lr_chunk], cx
    /* INT 13h AH=42 read cx sectors from lr_lba to 0x1000:0x0000 (phys 0x10000) */
    mov word ptr [dap2+2], cx
    mov eax, [lr_lba]
    mov [dap2+8], eax
    mov si, offset dap2
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    jc lr_err
    /* fs-copy chunk*512 bytes from 0x10000 to lr_dst */
    movzx ecx, word ptr [lr_chunk]
    shl ecx, 7               /* sectors*512/4 dwords = sectors*128 */
    mov esi, 0x10000
    mov edi, [lr_dst]
7:  mov eax, fs:[esi]
    mov fs:[edi], eax
    add esi, 4
    add edi, 4
    dec ecx
    jnz 7b
    /* advance */
    movzx eax, word ptr [lr_chunk]
    add [lr_lba], eax
    mov ebx, eax
    shl ebx, 9
    add [lr_dst], ebx
    sub [lr_left], eax
    jmp lr_loop
lr_done:
    ret
lr_err:
    lea si, [msg_diskerr]
    call sputs
8:  hlt
    jmp 8b

/* ── synthesize a multiboot info so kmain64 is reused unmodified ──────── */
synth_mb:
    mov dword ptr [0x5100], 0x8          /* flags: mods present */
    mov dword ptr [0x5114], 1            /* mods_count (off 20) */
    mov dword ptr [0x5118], 0x5200       /* mods_addr  (off 24) */
    mov dword ptr [0x5200], 0x800000     /* mod_start */
    mov eax, [0x7DB0]                    /* kernel size bytes */
    add eax, 0x800000
    mov [0x5204], eax                    /* mod_end */
    mov dword ptr [0x5208], 0
    mov dword ptr [0x520C], 0
    mov dword ptr [0x5014], 0x5100       /* kmain64 reads mb ptr here */
    ret

/* ── protected -> long mode, jump to payload @0x200000 ────────────────── */
go_long:
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm32
.code32
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    /* page tables @0x70000: identity map first 4GiB, 2MiB pages */
    mov edi, 0x70000
    mov ecx, 0x6000/4        /* zero 0x70000..0x76000 */
    xor eax, eax
    rep stosd
    mov dword ptr [0x70000], 0x71003    /* pml4[0]->pdpt */
    mov dword ptr [0x71000], 0x72003    /* pdpt[0..3]->pd0..3 */
    mov dword ptr [0x71008], 0x73003
    mov dword ptr [0x71010], 0x74003
    mov dword ptr [0x71018], 0x75003
    mov edi, 0x72000                    /* fill 2048 PDEs */
    xor ecx, ecx
pt_fill:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83
    mov [edi], eax
    mov dword ptr [edi+4], 0
    mov eax, ecx
    shr eax, 11
    mov [edi+4], eax
    add edi, 8
    inc ecx
    cmp ecx, 2048
    jne pt_fill
    mov eax, 0x70000
    mov cr3, eax
    mov eax, cr4
    or eax, 0x20            /* PAE */
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100          /* LME */
    wrmsr
    mov eax, cr0
    or eax, 0x80000000    /* PG */
    mov cr0, eax
    jmp 0x18:lm64
.code64
lm64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x6F000
    mov rax, 0x200000
    jmp rax

/* ── data ─────────────────────────────────────────────────────────────── */
.code16
drive:      .byte 0
best_mode:  .word 0
best_score: .word 0
lr_lba:     .long 0
lr_left:    .long 0
lr_dst:     .long 0
lr_chunk:   .word 0

.align 4
dap2:
    .byte 0x10
    .byte 0
    .word 0            /* sectors (patched) */
    .word 0x0000       /* offset */
    .word 0x1000       /* segment -> phys 0x10000 */
    .quad 0            /* lba (patched at dap2+8) */

.align 8
unreal_gdt:
    .quad 0
    .quad 0x00CF92000000FFFF
unreal_desc:
    .word 15
    .long unreal_gdt

.align 8
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF   /* 0x08 32-bit code */
    .quad 0x00CF92000000FFFF   /* 0x10 data */
    .quad 0x00AF9A000000FFFF   /* 0x18 64-bit code */
gdt_desc:
    .word 31
    .long gdt

msg_hi:      .asciz "\r\n[RayOS/stage2] real mode entry\r\n"
msg_vbe:     .asciz "[RayOS/stage2] VBE linear framebuffer set\r\n"
msg_vbefail: .asciz "[RayOS/stage2] VBE FAILED\r\n"
msg_load:    .asciz "[RayOS/stage2] payload + kernel loaded from disk\r\n"
msg_lm:      .asciz "[RayOS/stage2] entering long mode\r\n"
msg_diskerr: .asciz "[RayOS/stage2] DISK ERROR\r\n"
