/* boot/idt.S — minimal exception ISR stubs for bring-up diagnostics. Each
   pushes (dummy) error code + vector and funnels to isr_common, which hands
   vector/errcode/rip/cr2 to exc_handler (C) for a serial dump + halt. Turns a
   silent triple-fault into an actionable message. */
.code64
.section .text
.extern exc_handler

.macro ISR_NOERR n
isr\n:
    pushq $0
    pushq $\n
    jmp isr_common
.endm
.macro ISR_ERR n
isr\n:
    pushq $\n
    jmp isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

isr_common:
    mov  0(%rsp), %rdi        /* vector */
    mov  8(%rsp), %rsi        /* error code */
    mov 16(%rsp), %rdx        /* rip */
    mov %cr2, %rcx            /* faulting linear address */
    call exc_handler
1:  hlt
    jmp 1b

.section .rodata
.align 8
.global isr_table
isr_table:
    .quad isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7
    .quad isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15
    .quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
