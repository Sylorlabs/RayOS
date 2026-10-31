/* boot/ap_tramp.S — Application Processor startup trampoline. SIPI vectors each
   AP here in 16-bit real mode (physical 0x8000). We switch to long mode using
   the shared kernel page tables and jump to ap_entry64. The BSP patches CR3,
   the per-AP stack, and the entry address into the fixed page at 0x9000. */
.code16
.intel_syntax noprefix
.text
.global ap_tramp_start
ap_tramp_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    lgdt [gdtp]
    mov eax, [0x9000]           /* CR3 (shared kernel PML4) */
    mov cr3, eax
    mov eax, cr4
    or  eax, 0x20               /* PAE */
    mov cr4, eax
    mov ecx, 0xC0000080
    rdmsr
    or  eax, 0x100             /* LME */
    wrmsr
    mov eax, cr0
    or  eax, 0x80000001        /* PG|PE */
    mov cr0, eax
    ljmp 0x18, offset lm64
.code64
lm64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, [0x9008]          /* per-AP stack top */
    mov rax, [0x9010]          /* ap_entry64 */
    jmp rax
.align 8
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF   /* 0x08 32-bit code */
    .quad 0x00CF92000000FFFF   /* 0x10 data */
    .quad 0x00AF9A000000FFFF   /* 0x18 64-bit code */
gdtp:
    .word 31
    .long gdt
.global ap_tramp_end
ap_tramp_end:
