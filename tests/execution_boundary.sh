#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

fail() {
    echo "FAIL: execution boundary — $1" >&2
    exit 1
}

hits="$(rg -n '_zag_|extern fn|raw_syscall|softgpu_|virtio|x86|elf|linux' \
    "$ROOT/core/execution_domain.flash" "$ROOT/core/scheduler.flash" || true)"
if [[ -n "$hits" ]]; then
    printf '%s\n' "$hits" >&2
    fail "native execution policy imports a host or compatibility mechanism"
fi

rg -q 'execution_domain_ternary\(\)' "$ROOT/core/execution_domain.flash" \
    || fail "ternary domain is missing"
rg -q 'execution_domain_binary\(\)' "$ROOT/core/execution_domain.flash" \
    || fail "binary compatibility domain is missing"
rg -q 'native_memory = 0' "$ROOT/core/execution_domain.flash" \
    || fail "binary native-memory denial is missing"
rg -q 'fabric_control = 0' "$ROOT/core/execution_domain.flash" \
    || fail "binary fabric-control denial is missing"
rg -q 'execution_denied_capabilities' "$ROOT/core/execution_domain.flash" \
    || fail "invalid-domain capability denial is missing"

echo "PASS: balanced ternary is native; binary is an explicit mediated chamber"
