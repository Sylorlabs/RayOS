# RayOS

RayOS is a balanced-ternary operating-system project written in Flash. The
supported source of truth is `.flash`; binary hosts are an emulation and
interop boundary, not the architecture. Build and run the current verified
foundation with:

```bash
./build.sh
```

Balanced ternary is the native and default execution domain. Users may also
explicitly select a binary compatibility chamber for packages that provide a
binary entry point. RayOS never silently falls back from ternary to binary;
binary guests receive mediated display/input/files/network proxies and cannot
directly access native ternary memory or the photonic fabric. See
`docs/EXECUTION_DOMAINS.md` and `docs/RAYOS_MISSION.md`.

The build uses Flash's bundled standalone backend. It no longer requires the
sibling Zag repository. `tests/pure_flash_tree.sh` blocks C, C++, Rust, Python,
Zig, or direct foreign-runtime imports from implementation directories.

## Implemented and gated

- native `t9`/`t27` weighted virtual-deadline scheduler with priority, sleep,
  wake, block, exit, deterministic tie-breaking, and live Task Manager UI;
- TriFS namespace and native `t9` payload core with serialized transactions,
  create/read/write/open/close/rename/unlink, cycle prevention, and busy/nonempty
  safety;
- retained compositor, window manager, launcher, Files UI, taskbar, MMIO input,
  and display scanout;
- Ray Browser's bounded HTML tokenizer/DOM, inert-script policy, flow layout,
  wrapping, and internal `rayos://` page;
- RayPlay, a Flash-native frame-scheduled UI driver that traverses production
  MMIO input and captures/asserts Task Manager, Files, and Browser states;
- x86/Linux guest compatibility emulation, virtio paths, photonic routing, SOH,
  and balanced-ternary arithmetic tests inherited from the foundation.

Screenshots produced by the gate are `desktop/task-manager.ppm`,
`desktop/files.ppm`, and `desktop/browser.ppm`.

## Not complete

RayOS is not yet a complete general-purpose OS or standards-conformant web
browser. TriFS currently provides the VFS/transaction core but is not yet
crash-persistent on ternary media. Ray Browser has HTTP/1.1 and URL parsing.
Authenticated TLS 1.3 and ECDSA P-256/P-384 chain verification are in progress
but are not currently build- or test-verified by the Flash frontend, so they
are not release capabilities. The browser also does not yet implement CSS,
scripting, accessibility trees, RSA cert chains, media, or the web platform
test corpus. Kernel capabilities, users/security domains, IPC,
network sockets, audio, power management, installation/update recovery, and
real ternary hardware boot also remain release blockers.

The compatibility guest artifacts under `vm/` are foreign binary payloads;
they are not RayOS implementation dependencies. The host emulator is binary
because current development machines are binary. Native Flash targets remain
the architectural destination.

## Design baseline

Implementation decisions are checked against the current primary standards and
platform guidance rather than copied from another OS:

- [POSIX.1-2024](https://pubs.opengroup.org/onlinepubs/9799919799/) for OS
  interface and concurrency edge cases;
- [HTML Living Standard](https://html.spec.whatwg.org/) and
  [CSS Snapshot](https://www.w3.org/TR/css-2024/) for browser conformance;
- [WCAG 2.2](https://www.w3.org/TR/WCAG22/) for keyboard, focus, target size,
  reflow, contrast, and semantic accessibility;
- [HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110) for the future network
  layer;
- [Linux filesystem locking](https://www.kernel.org/doc/html/latest/filesystems/directory-locking.html)
  and [EEVDF](https://www.kernel.org/doc/html/latest/scheduler/sched-eevdf.html)
  as edge-case research inputs, not source-code dependencies.
