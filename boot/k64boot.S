/* 64-bit payload entry. Linked first at 0x200000; the 32-bit stub far-jumps
   into long mode then CALLs 0x200000. Reads video info from the shared page
   at 0x5000 (written by the 32-bit stage) and hands off to kmain64. */
.code64
.section .text
.global k64_start
k64_start:
    call kmain64
1:  hlt
    jmp 1b
