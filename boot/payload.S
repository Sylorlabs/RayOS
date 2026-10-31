/* Embed the 64-bit payload blob so it lands at load address 0x200000. */
.section .kernel64, "a"
.incbin "kmain64.bin"
