#!/usr/bin/env bash
# RayOS VM setup: download Alpine virt ISO and create qcow2 disks.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ALPINE_URL="https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-virt-3.20.0-x86_64.iso"
ALPINE_ISO="alpine.iso"
LINUX_DISK="linux.qcow2"
WINDOWS_DISK="windows.qcow2"
WINDOWS_ISO="windows.iso"

LINUX_SIZE="4G"
WINDOWS_SIZE="8G"

download_alpine() {
    if [[ -f "$ALPINE_ISO" ]]; then
        echo "[skip] $ALPINE_ISO already exists"
        return 0
    fi

    echo "[fetch] Downloading Alpine virt ISO (~60MB)..."
    if command -v curl >/dev/null 2>&1; then
        curl -fL --progress-bar -o "$ALPINE_ISO" "$ALPINE_URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$ALPINE_ISO" "$ALPINE_URL"
    else
        echo "error: curl or wget required to download $ALPINE_URL" >&2
        exit 1
    fi
    echo "[ok] Saved $ALPINE_ISO"
}

create_disk() {
    local name="$1"
    local size="$2"

    if [[ -f "$name" ]]; then
        echo "[skip] $name already exists"
        return 0
    fi

    if ! command -v qemu-img >/dev/null 2>&1; then
        echo "error: qemu-img not found; install qemu-utils" >&2
        exit 1
    fi

    echo "[create] $name ($size qcow2)"
    qemu-img create -f qcow2 "$name" "$size"
    echo "[ok] Created $name"
}

main() {
    echo "RayOS VM setup (directory: $SCRIPT_DIR)"
    echo

    download_alpine
    create_disk "$LINUX_DISK" "$LINUX_SIZE"
    create_disk "$WINDOWS_DISK" "$WINDOWS_SIZE"

    echo
    echo "Setup complete."
    echo
    echo "Linux guest:"
    echo "  ISO:  $SCRIPT_DIR/$ALPINE_ISO"
    echo "  Disk: $SCRIPT_DIR/$LINUX_DISK"
    echo "  Test: $SCRIPT_DIR/launch_linux.sh"
    echo
    echo "Windows guest (optional):"
    echo "  Place a bootable Windows installer ISO at:"
    echo "    $SCRIPT_DIR/$WINDOWS_ISO"
    echo "  Disk image ready at:"
    echo "    $SCRIPT_DIR/$WINDOWS_DISK"
    echo "  Boot with QEMU manually or add a launch_windows.sh later."
    echo
    echo "VNC for Linux test VM: localhost:5901 (display :1)"
}

main "$@"