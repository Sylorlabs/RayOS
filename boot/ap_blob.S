.section .rodata
.balign 16
.global ap_tramp_blob
ap_tramp_blob: .incbin "ap_tramp.bin"
.global ap_tramp_blob_end
ap_tramp_blob_end:
