/* boot/irq.S — hardware IRQ entry stubs (timer/keyboard/mouse). Unlike the
   exception stubs these RETURN to the interrupted guest, so they save/restore
   every GP register and iretq. They run on the TSS IST1 stack (set via the IDT
   gate) so they never touch the guest's stack/red-zone. */
.code64
.section .text
.extern handle_timer
.extern handle_keyboard
.extern handle_mouse

.macro IRQSTUB name, handler
.global \name
\name:
    push %rax; push %rcx; push %rdx; push %rbx
    push %rbp; push %rsi; push %rdi; push %r8
    push %r9; push %r10; push %r11; push %r12
    push %r13; push %r14; push %r15
    cld
    call \handler
    pop %r15; pop %r14; pop %r13; pop %r12
    pop %r11; pop %r10; pop %r9; pop %r8
    pop %rdi; pop %rsi; pop %rbp; pop %rbx
    pop %rdx; pop %rcx; pop %rax
    iretq
.endm

IRQSTUB irq_timer,    handle_timer
IRQSTUB irq_keyboard, handle_keyboard
IRQSTUB irq_mouse,    handle_mouse
