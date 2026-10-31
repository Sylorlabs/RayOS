ENTRY(_start)
SECTIONS {
    . = 1M;
    .boot   : { *(.multiboot) }
    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }
    . = 0x200000;
    .kernel64 : { *(.kernel64) }
}
