ENTRY(k64_start)
SECTIONS {
    . = 0x200000;
    .text   : { k64boot.o(.text) *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    . = ALIGN(16);
    __bss_start = .;
    .bss    : { *(.bss*) *(COMMON) }
    __bss_end = .;
}
