OUTPUT_FORMAT("elf64-x86-64")
ENTRY(efi_main)
SECTIONS {
  . = 0;
  ImageBase = .;
  .text : { *(.text) *(.text.*) }
  . = ALIGN(0x1000);
  .data : { *(.data) *(.data.*) *(.rodata) *(.rodata.*) *(.sdata) *(.got) *(.got.plt) *(.blobs) *(.bss) *(COMMON) }
  . = ALIGN(0x1000);
  .dynamic : { *(.dynamic) }
  . = ALIGN(0x1000);
  .rela : { *(.rela) *(.rela.*) }
  . = ALIGN(0x1000);
  .dynsym : { *(.dynsym) }
  . = ALIGN(0x1000);
  .reloc : { *(.reloc) }
  /DISCARD/ : { *(.eh_frame) *(.note.*) *(.comment) *(.gnu.hash) *(.hash) *(.dynstr) }
}
