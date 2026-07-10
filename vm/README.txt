RayOS VM Infrastructure
=======================

Directory: vm/

This folder holds real QEMU guest images for RayOS development and testing.

Quick start
-----------
  ./setup_vms.sh      Download Alpine ISO and create qcow2 disks
  ./launch_linux.sh   Boot Alpine with VNC on localhost:5901

Files created by setup_vms.sh
-----------------------------
  alpine.iso      Alpine Linux 3.20 virt ISO (~60MB)
  linux.qcow2     4GB disk for Linux guest installs
  windows.qcow2   8GB disk for optional Windows guest

Alpine Linux guest setup
------------------------
1. Run setup_vms.sh if alpine.iso or linux.qcow2 are missing.
2. Run launch_linux.sh and connect a VNC client to localhost:5901.
3. At the Alpine installer:
   - Login as root (no password on live ISO).
   - Run setup-alpine and follow prompts.
   - Choose the virtio disk (linux.qcow2) for installation target.
   - Enable networking (DHCP via QEMU user netdev).
4. After install, reboot and remove -cdrom / change boot order to boot from disk.

Windows guest (optional)
------------------------
1. Obtain a licensed Windows installer ISO.
2. Place it at: vm/windows.iso
3. windows.qcow2 is pre-created by setup_vms.sh.
4. Example manual launch (adjust memory/CPU as needed):

   qemu-system-x86_64 \
     -machine type=pc,accel=kvm:tcg \
     -m 2048 -smp 2 \
     -drive file=windows.qcow2,format=qcow2,if=virtio \
     -cdrom windows.iso \
     -boot d \
     -netdev user,id=net0 \
     -device virtio-net-pci,netdev=net0 \
     -vga std \
     -vnc :2

Requirements
------------
  qemu-system-x86, qemu-utils (qemu-img), curl or wget

Notes
-----
  KVM is used when available; otherwise QEMU falls back to TCG.
  VNC display :1 maps to TCP port 5901.