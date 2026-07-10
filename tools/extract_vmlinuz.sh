#!/usr/bin/env bash
# extract_vmlinuz.sh — extract boot/vmlinuz-virt from vm/alpine.iso into vm/linux.bin.
# If the ISO is missing, writes a synthetic bzImage matching linux_boot_synth_bzimage().
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="$ROOT/vm/alpine.iso"
OUT="$ROOT/vm/linux.bin"

write_synthetic_bzimage() {
    python3 - "$OUT" <<'PY'
import sys

setup_sects = 4
sector = 512
kernel_paragraphs = 64
kernel_bytes = kernel_paragraphs * 16
file_len = sector + setup_sects * sector + kernel_bytes
img = bytearray(file_len)

img[0] = 0xEB
img[1] = 0xFE
img[497] = setup_sects
img[500] = 64
img[501] = 0
img[508] = 0x55
img[509] = 0xAA
img[510] = 0x55
img[511] = 0xAA
img[514:518] = b"HdrS"
img[518] = 14
img[519] = 2

koff = sector + setup_sects * sector
img[koff + 0] = 0x90
img[koff + 1] = 0x90
img[koff + 2] = 0x83

with open(sys.argv[1], "wb") as f:
    f.write(img)
PY
    echo "extract_vmlinuz: wrote synthetic bzImage to $OUT"
}

extract_from_iso() {
    local tmp
    tmp="$(mktemp -d)"

    if command -v 7z >/dev/null 2>&1; then
        7z e -y -o"$tmp" "$ISO" boot/vmlinuz-virt >/dev/null
    elif command -v bsdtar >/dev/null 2>&1; then
        bsdtar -xf "$ISO" -C "$tmp" boot/vmlinuz-virt
        mv "$tmp/boot/vmlinuz-virt" "$tmp/vmlinuz-virt"
    else
        rm -rf "$tmp"
        echo "error: need 7z or bsdtar to extract vmlinuz from ISO" >&2
        return 1
    fi

    if [[ ! -f "$tmp/vmlinuz-virt" ]]; then
        rm -rf "$tmp"
        echo "error: boot/vmlinuz-virt not found in $ISO" >&2
        return 1
    fi

    cp "$tmp/vmlinuz-virt" "$OUT"
    rm -rf "$tmp"
    echo "extract_vmlinuz: extracted boot/vmlinuz-virt to $OUT"
}

main() {
    mkdir -p "$ROOT/vm"
    if [[ -f "$ISO" ]]; then
        extract_from_iso
        return 0
    fi
    write_synthetic_bzimage
}

main "$@"