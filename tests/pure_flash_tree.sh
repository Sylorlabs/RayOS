#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

bad="$(find \
    "$ROOT/browser" "$ROOT/core" "$ROOT/desktop" "$ROOT/driver" "$ROOT/fabric" \
    "$ROOT/hypervisor" "$ROOT/math" "$ROOT/memory" "$ROOT/net" "$ROOT/storage" \
    "$ROOT/testing" \
    -type f \( -name '*.c' -o -name '*.h' -o -name '*.cc' -o -name '*.cpp' \
                   -o -name '*.py' -o -name '*.rs' -o -name '*.zig' \) \
    -print -quit)"
if [[ -n "$bad" ]]; then
    echo "FAIL: non-Flash implementation source: $bad" >&2
    exit 1
fi

foreign_import="$(rg -n '@import\([^)]*(zag-poc|\.\./zag)' \
    "$ROOT" -g '*.flash' -g '!**/*_shim.zag' -g '!**/*__shim.zag' | head -1 || true)"
if [[ -n "$foreign_import" ]]; then
    echo "FAIL: Flash source imports a foreign compiler/runtime: $foreign_import" >&2
    exit 1
fi

foreign_build="$(rg -n '(python[0-9]*|gcc|g\+\+|clang|cargo|rustc|zig)[[:space:]]' \
    "$ROOT/build.sh" "$ROOT/tests" -g '*.sh' | head -1 || true)"
if [[ -n "$foreign_build" ]]; then
    echo "FAIL: canonical RayOS build/test invokes a foreign implementation tool: $foreign_build" >&2
    exit 1
fi

echo "PASS: RayOS implementation is Flash-only; host backend is the Flash-owned emulator"
