#!/usr/bin/env bash
# build.sh — compile and smoke-test RayOS (Flash-only source).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
FLASHC="${FLASHC:-$ROOT/../flash/flashc}"
FLASHZ="${FLASHZ:-$ROOT/../flash/flash/toolchain/flashz}"
BIN_DIR="$ROOT/.test_bin"

mkdir -p "$BIN_DIR" "$ROOT/vm"

if [[ ! -x "$FLASHC" ]]; then
    echo "error: flashc not found at $FLASHC" >&2
    exit 1
fi
if [[ ! -x "$FLASHZ" ]]; then
    echo "error: Flash standalone backend not found at $FLASHZ (set FLASHZ=...)" >&2
    exit 1
fi

shim_for() {
    local src="$1"
    local dir base stem
    dir="$(dirname "$src")"
    base="$(basename "$src" .flash)"
    # flashc drops the last '_' segment: rayos_boot -> rayos__shim.zag
    IFS=_ read -r -a parts <<< "$base"
    if ((${#parts[@]} > 1)); then
        unset 'parts[-1]'
        stem="${parts[*]}"
        stem="${stem// /_}"
    else
        stem="$base"
    fi
    echo "$dir/${stem}__shim.zag"
}

flash_build() {
    local src="$1"
    local out="$2"
    local shim flashc_out
    echo "== flashc: $(basename "$src") =="
    flashc_out="$(cd "$ROOT" && "$FLASHC" "$src" --emit-zag --znc "$FLASHZ" 2>&1)" || {
        echo "$flashc_out" >&2
        return 1
    }
    echo "$flashc_out"
    shim="$(printf '%s\n' "$flashc_out" | sed -n 's/^flashc1: wrote //p' | tail -1)"
    if [[ -z "$shim" ]]; then
        shim="$(shim_for "$src")"
    fi
    sed -i '/^@realtime$/d' "$ROOT/$shim"
    # flashc emits toolchain-relative @imports; rewrite for this repo layout.
    sed -i \
        -e 's|@import("../flash/rt/rt.zag")|@import("../../flash/flash/rt/rt.zag")|' \
        -e 's|@import("../flash/toolchain/zag-poc/std/list.zag")|@import("../../flash/flash/toolchain/zag-poc/std/list.zag")|' \
        -e 's|@import("../flash/stdlib/flash_slim.zag")|@import("../../flash/flash/stdlib/flash_slim.zag")|' \
        "$ROOT/$shim"
    "$FLASHZ" "$ROOT/$shim" -o "$out"
    chmod +x "$out"
}

echo "== rayos: compile foundation =="
"$ROOT/tests/pure_flash_tree.sh"
flash_build "core/rayos_boot.flash" "$BIN_DIR/rayos_boot"
flash_build "tests/scheduler_test.flash" "$BIN_DIR/scheduler_test"
flash_build "tests/trifs_test.flash" "$BIN_DIR/trifs_test"
flash_build "tests/html_parser_test.flash" "$BIN_DIR/html_parser_test"
flash_build "tests/rayos_foundation_test.flash" "$BIN_DIR/rayos_foundation_test"
flash_build "tests/purge_ok.flash" "$BIN_DIR/purge_ok"
flash_build "tests/chamber_selftest.flash" "$BIN_DIR/chamber_selftest"
flash_build "tests/scanout_selftest.flash" "$BIN_DIR/scanout_selftest"
flash_build "tests/x86_decode_test.flash" "$BIN_DIR/x86_decode_test"
flash_build "tests/btm_synthetic_test.flash" "$BIN_DIR/btm_synthetic_test"
flash_build "tests/btm_jmp_test.flash" "$BIN_DIR/btm_jmp_test"
flash_build "tests/btm_intrinsics_test.flash" "$BIN_DIR/btm_intrinsics_test"
flash_build "tests/btm_memory_test.flash" "$BIN_DIR/btm_memory_test"
flash_build "tests/btm_hlt_test.flash" "$BIN_DIR/btm_hlt_test"
flash_build "tests/soh_block_test.flash" "$BIN_DIR/soh_block_test"
flash_build "tests/virtio_block_test.flash" "$BIN_DIR/virtio_block_test"
flash_build "tests/virtio_net_test.flash" "$BIN_DIR/virtio_net_test"
flash_build "tests/virtio_gpu_fb_test.flash" "$BIN_DIR/virtio_gpu_fb_test"
flash_build "tests/guest_boot_sector_test.flash" "$BIN_DIR/guest_boot_sector_test"
flash_build "tests/linux_boot_protocol_test.flash" "$BIN_DIR/linux_boot_protocol_test"
flash_build "tests/linux_bzimage_real_test.flash" "$BIN_DIR/linux_bzimage_real_test"
flash_build "tests/guest_linux_integration_test.flash" "$BIN_DIR/guest_linux_integration_test"
flash_build "tests/desktop_integration_test.flash" "$BIN_DIR/desktop_integration_test"
flash_build "tests/desktop_preview_test.flash" "$BIN_DIR/desktop_preview_test"
flash_build "tests/rayplay_desktop_test.flash" "$BIN_DIR/rayplay_desktop_test"
flash_build "tests/scanout_live_test.flash" "$BIN_DIR/scanout_live_test"
flash_build "tests/device_bus_test.flash" "$BIN_DIR/device_bus_test"
flash_build "tests/device_chamber_test.flash" "$BIN_DIR/device_chamber_test"
flash_build "tests/net_http_test.flash" "$BIN_DIR/net_http_test"
flash_build "tests/crypto_test.flash" "$BIN_DIR/crypto_test"
flash_build "tests/tls_test.flash" "$BIN_DIR/tls_test"
flash_build "desktop/rayos_desktop_main.flash" "$BIN_DIR/rayos_desktop"
flash_build "desktop/rayos_desktop_live.flash" "$BIN_DIR/rayos_desktop_live"

echo "== rayos: run tests =="
"$BIN_DIR/rayos_foundation_test"
"$BIN_DIR/scheduler_test"
"$BIN_DIR/trifs_test"
"$BIN_DIR/html_parser_test"
"$BIN_DIR/x86_decode_test"
"$BIN_DIR/btm_synthetic_test"
"$BIN_DIR/btm_jmp_test"
"$BIN_DIR/btm_intrinsics_test"
"$BIN_DIR/btm_memory_test"
"$BIN_DIR/btm_hlt_test"
"$BIN_DIR/soh_block_test"
"$BIN_DIR/virtio_block_test"
"$BIN_DIR/virtio_net_test"
"$BIN_DIR/virtio_gpu_fb_test"
"$BIN_DIR/guest_boot_sector_test"
"$BIN_DIR/linux_boot_protocol_test"
"$BIN_DIR/linux_bzimage_real_test"
"$BIN_DIR/guest_linux_integration_test"
"$BIN_DIR/chamber_selftest"
"$BIN_DIR/scanout_selftest"
"$BIN_DIR/scanout_live_test"
"$BIN_DIR/device_bus_test"
"$BIN_DIR/device_chamber_test"
"$BIN_DIR/net_http_test"
"$BIN_DIR/crypto_test"
"$BIN_DIR/tls_test"
"$BIN_DIR/desktop_integration_test"
"$BIN_DIR/desktop_preview_test"
"$BIN_DIR/rayplay_desktop_test"
"$BIN_DIR/purge_ok"
"$BIN_DIR/rayos_boot"

echo "rayos: all Flash foundation checks passed"
