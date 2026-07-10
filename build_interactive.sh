#!/usr/bin/env bash
# build_interactive.sh — Quick rebuild of rayos_interactive tool
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
FLASHC="${FLASHC:-$ROOT/../flash/flashc}"
FLASHZ="${FLASHZ:-$ROOT/../flash/flash/toolchain/flashz}"
BIN_DIR="$ROOT/.test_bin"

mkdir -p "$BIN_DIR"

echo "== Building rayos_interactive =="
"$FLASHC" "tools/rayos_interactive.flash" --emit-zag --znc "$FLASHZ" 2>&1

shim="tools/rayos_interactive_shim.zag"
sed -i '/^@realtime$/d' "$ROOT/$shim"
sed -i \
    -e 's|@import("../flash/rt/rt.zag")|@import("../../flash/flash/rt/rt.zag")|' \
    -e 's|@import("../flash/toolchain/zag-poc/std/list.zag")|@import("../../flash/flash/toolchain/zag-poc/std/list.zag")|' \
    -e 's|@import("../flash/stdlib/flash_slim.zag")|@import("../../flash/flash/stdlib/flash_slim.zag")|' \
    "$ROOT/$shim"
"$FLASHZ" "$ROOT/$shim" -o "$BIN_DIR/rayos_interactive"
chmod +x "$BIN_DIR/rayos_interactive"

echo "== rayos_interactive built successfully =="
