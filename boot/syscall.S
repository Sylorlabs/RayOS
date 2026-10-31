/* boot/syscall.S — SYSCALL entry trampoline. The flashz runtime executes the
   Linux `syscall` instruction; the CPU (with our MSRs set) vectors here.
   Linux preserves every register across a syscall except rax(result),rcx,r11,
   so we save/restore rdi,rsi,rdx,r10,r8,r9 too, marshal into the SysV C ABI,
   call do_syscall, then return to rcx with r11->rflags. Runs in ring 0 on the
   caller's stack (flashz write/exit don't depend on the red zone across it). */
.code64
.section .text
.global syscall_entry
.extern do_syscall
syscall_entry:
    push %rcx                 /* return rip (CPU put it here) */
    push %r11                 /* saved rflags */
    push %rdi
    push %rsi
    push %rdx
    push %r10
    push %r8
    push %r9
    /* marshal syscall args (rax,rdi,rsi,rdx,r10,r8,r9) -> C(rdi,rsi,rdx,rcx,r8,r9,stack) */
    push %r9                  /* a6 -> 7th C arg (on stack) */
    mov %r8,  %r9             /* a5 */
    mov %r10, %r8             /* a4 */
    mov %rdx, %rcx            /* a3 */
    mov %rsi, %rdx            /* a2 */
    mov %rdi, %rsi            /* a1 */
    mov %rax, %rdi            /* syscall number */
    call do_syscall          /* result -> rax */
    add $8, %rsp             /* pop a6 */
    pop %r9
    pop %r8
    pop %r10
    pop %rdx
    pop %rsi
    pop %rdi
    pop %r11
    pop %rcx
    push %r11
    popfq                     /* restore caller rflags */
    jmp *%rcx                 /* return to caller (rax = result) */
