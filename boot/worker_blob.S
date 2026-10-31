.section .rodata
.balign 16
.global worker_blob
worker_blob: .incbin "worker.elf"
.global worker_blob_end
worker_blob_end:
