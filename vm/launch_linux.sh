#!/usr/bin/env bash
# Standalone Alpine Linux QEMU test for RayOS VM infrastructure.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ALPINE_ISO="alpine.iso"
LINUX_DISK="linux.qcow2"
VNC_DISPLAY=":1"   # VNC port 5901

for bin in qemu-system-x86_64; do
    if ! command -v "$bin" >/dev/null 2>&1; then
        echo "error: $bin not found; install qemu-system-x86" >&2
        exit 1
    fi
done

if [[ ! -f "$ALPINE_ISO" || ! -f "$LINUX_DISK" ]]; then
    echo "error: missing $ALPINE_ISO or $LINUX_DISK" >&2
    echo "Run: $SCRIPT_DIR/setup_vms.sh" >&2
    exit 1
fi

echo "Booting Alpine Linux in QEMU"
echo "  ISO:  $SCRIPT_DIR/$ALPINE_ISO"
echo "  Disk: $SCRIPT_DIR/$LINUX_DISK"
echo "  VNC:  localhost:5901 (display $VNC_DISPLAY)"
echo "  Stop: Ctrl+C in this terminal"
echo

exec qemu-system-x86_64 \
    -machine type=pc,accel=kvm:tcg \
    -cpu host \
    -m 512 \
    -smp 2 \
    -drive file="$LINUX_DISK",format=qcow2,if=virtio \
    -cdrom "$ALPINE_ISO" \
    -boot d \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -vga std \
    -vnc "$VNC_DISPLAY" \
    -name "rayos-alpine-test"