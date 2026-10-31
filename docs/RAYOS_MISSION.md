# RayOS Mission Contract

RayOS is a balanced-ternary-first operating system and computing ecosystem.
Balanced ternary is the native architecture, not a presentation layer over a
binary kernel. Photonic compute is the intended physical execution plane.

RayOS also supports binary software through explicit compatibility chambers.
This is dual compatibility, not a dual foundation: ternary remains the default,
the trusted control plane, and the format used by native applications.

## Non-negotiable boundaries

1. An unspecified execution request means balanced ternary.
2. Binary-only software never starts through implicit fallback.
3. A user may select Binary for a package only when that package declares a
   binary entry point.
4. A user may select Ternary only when a native balanced-ternary entry exists.
5. Binary code cannot directly address native ternary memory, configure the
   photonic fabric, or enter the native protection domain.
6. Display, input, files and networking are mediated proxy capabilities for a
   binary chamber and may be independently denied.
7. Bytes are valid at protocol, media, text, device and compatibility
   boundaries. They are not the native RayOS machine model.
8. x86, ARM, ELF, PE, BIOS, UEFI, POSIX and Linux are compatibility inputs, not
   architectural authorities.

## Repository worlds

- Native: `core/`, `math/`, `memory/`, `fabric/`, `storage/`, native drivers,
  Flash/Flashy applications and the future RayOS ISA backend.
- Bridge/laboratory: host MMIO, emulators, FPGA bring-up, photonic simulation,
  calibration and fabrication tools.
- Compatibility: BTM, foreign guests, byte protocols and imported executable
  formats.

The worlds may communicate only through reviewed boundary contracts. A bridge
or compatibility success cannot be reported as native photonic execution.

## Proof levels

1. Source contract: native types and boundaries are expressed in Flash.
2. Host model: behavior passes under the Flash-owned host backend.
3. ISA reference: behavior passes on the exact RayOS machine emulator.
4. Hardware bridge: behavior passes on FPGA/electronic ternary validation
   hardware without a host OS underneath it.
5. Photonic tile: measured physical devices execute and retain ternary state.
6. Native system: RayOS cold-boots and runs isolated applications on that
   hardware.
7. Self-host: RayOS builds, signs, installs and debugs its own native software.

Only level 6 is a photonic-computed operating system. The current repository is
at levels 1 and 2, with partial target-emission groundwork toward level 3.
