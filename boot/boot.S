/* boot/boot.S — 32-bit multiboot1 entry + 32->64 long-mode trampoline.
   QEMU -kernel loads this in flat protected mode. We bring up video in 32-bit
   C (video_setup), build our own page tables + GDT, enter long mode, and jump
   into the 64-bit payload at 0x200000. No Linux, no firmware handoff. */
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000006
.set MB_CHECK, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC, MB_FLAGS, MB_CHECK
.long 0,0,0,0,0
.long 0, 1280, 800, 32

.section .bss, "aw", @nobits
.align 4096
pml4: .skip 4096
pdpt: .skip 4096
pd0:  .skip 4096
pd1:  .skip 4096
pd2:  .skip 4096
pd3:  .skip 4096
.align 16
stack_bottom: .skip 65536
stack_top:

.section .rodata
.align 16
gdt64:
    .quad 0
    .quad 0x00AF9A000000FFFF        /* 64-bit code */
    .quad 0x00AF92000000FFFF        /* 64-bit data */
gdt64_ptr:
    .word gdt64_ptr - gdt64 - 1
    .long gdt64

.section .text
.code32
.global _start
_start:
    movl $stack_top, %esp
    movl %ebx, 0x5014          /* stash multiboot info ptr */
    call video_setup                /* PCI+VBE bring-up, publishes vinfo@0x5000 */

    /* PML4[0]->pdpt ; pdpt[0..3]->pd0..pd3 */
    movl $pdpt, %eax
    orl  $0x3, %eax
    movl %eax, pml4
    movl $pd0, %eax ; orl $0x3, %eax ; movl %eax, pdpt+0
    movl $pd1, %eax ; orl $0x3, %eax ; movl %eax, pdpt+8
    movl $pd2, %eax ; orl $0x3, %eax ; movl %eax, pdpt+16
    movl $pd3, %eax ; orl $0x3, %eax ; movl %eax, pdpt+24

    /* identity-map first 4 GiB with 2 MiB pages (covers LFB @ 0xfd000000) */
    movl $pd0, %edi
    xorl %ecx, %ecx
1:
    movl %ecx, %eax
    shll $21, %eax
    orl  $0x83, %eax                 /* present|write|PS */
    movl %eax, (%edi)
    movl %ecx, %eax
    shrl $11, %eax                   /* high dword of i*2MiB */
    movl %eax, 4(%edi)
    addl $8, %edi
    incl %ecx
    cmpl $2048, %ecx
    jne  1b

    movl $pml4, %eax
    movl %eax, %cr3
    movl %cr4, %eax ; orl $0x20, %eax ; movl %eax, %cr4     /* PAE */
    movl $0xC0000080, %ecx ; rdmsr ; orl $0x100, %eax ; wrmsr  /* EFER.LME */
    movl %cr0, %eax ; orl $0x80000001, %eax ; movl %eax, %cr0  /* PG|PE */

    lgdt gdt64_ptr
    ljmp $0x08, $long_entry

.code64
long_entry:
    movw $0x10, %ax
    movw %ax, %ds ; movw %ax, %es ; movw %ax, %ss
    movw %ax, %fs ; movw %ax, %gs
    movl %esp, %esp                  /* zero-extend 32-bit stack into rsp */
    movq $0x200000, %rax             /* 64-bit payload entry (k64_start) */
    call *%rax
hang:
    hlt
    jmp hang
