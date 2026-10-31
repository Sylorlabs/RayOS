#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FLASHC="${FLASHC:-$ROOT/../flash/flashc}"
SRC="$ROOT/tests/execution_domain_test.flash"
OUT="$ROOT/tests/execution_domain_test.ppu32"
FIR="$ROOT/tests/execution_domain_test.fir"

"$FLASHC" "$SRC" --target ppu32 --emit-ir >/tmp/rayos_flash_target_probe.log 2>&1 || {
    cat /tmp/rayos_flash_target_probe.log >&2
    echo "BLOCKED: Flash ppu32 target could not emit the RayOS fixture" >&2
    exit 1
}

insns="$(awk '
    /^\[insns\]$/ { in_insns=1; next }
    in_insns && $0 !~ /^[[:space:]]*($|#)/ { count++ }
    END { print count + 0 }
' "$OUT")"

if [[ "$insns" -lt 1 ]]; then
    echo "BLOCKED: Flash ppu32 emitted zero ordinary RayOS instructions" >&2
    echo "Required next step: implement the rayos-ternary general-code backend in Flash" >&2
    exit 1
fi

echo "PASS: Flash emitted $insns native RayOS instructions"
