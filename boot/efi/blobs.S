/* Embed the 64-bit payload (kmain64.bin) and the flashz kernel into the EFI
   app so BOOTX64.EFI is self-contained (one file to put on the ESP). */
.section .blobs, "a"
.balign 16
.global payload_start
payload_start: .incbin "../kmain64.bin"
.global payload_end
payload_end:
.balign 16
.global kernel_start
kernel_start: .incbin "../../.test_bin/rayos_desktop_live"
.global kernel_end
kernel_end:
