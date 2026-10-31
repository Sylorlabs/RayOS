# Flash RayOS Target Contract

The required compiler target is `rayos-ternary`. It is distinct from the host
`x86_64-emul` target and from the existing path-oriented `ppu32` emitter.

## Required pipeline

```text
.flash source
  -> checked Flash IR
  -> RayOS ternary IR
  -> RayOS object
  -> signed RayOS image
```

## Required compiler behavior

- Preserve `t1` through `t40` as native ternary values.
- Lower ordinary functions, structs, control flow, pointers, calls, loads,
  stores, traps and atomics; not only photonic path manifests.
- Emit the RayOS ISA and calling convention without Zag, ELF or a host ABI.
- Reject `_zag_*`, raw host syscalls and unclassified foreign imports for the
  native target.
- Treat `u8`/byte operations as explicitly attributed boundary operations.
- Emit domain metadata and separate compatibility entries without embedding
  binary code into the native object.
- Produce reproducible objects and signed image manifests.
- Eventually run the compiler inside RayOS and reach a byte-for-trit identical
  bootstrap fixed point.

## Current blocker

The sibling Flash compiler is outside this workspace's write permissions. Its
current `--target ppu32` preserves `t9` names, but an ordinary RayOS policy
program emits a `ppu32` file with an empty `[insns]` section. That target is
therefore emission groundwork, not a native RayOS backend.

`tests/flash_rayos_target_probe.sh` records this exact boundary. It must remain
a failing readiness probe until ordinary Flash control flow produces native
instructions. It must not be weakened to accept an empty file.
