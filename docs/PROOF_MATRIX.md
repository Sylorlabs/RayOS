# RayOS Proof Matrix

| Capability | Current evidence | Level | Release blocker |
|---|---|---:|---|
| Balanced-ternary execution policy | Focused policy and scheduler tests | 2 | No |
| Explicit binary compatibility choice | Shared launcher policy and task-domain focused test | 2 | No |
| No silent binary fallback | Core and shared launcher-policy tests | 2 | No |
| Binary capability mediation | Policy model denies native memory/fabric control | 1 | Enforcement monitor incomplete |
| Native RayOS ISA | Specification contract only | 0 | Yes |
| Flash native backend | Existing `ppu32` produces empty ordinary instruction stream | 0 | Yes |
| Kernel isolation and IPC | Not implemented | 0 | Yes |
| Crash-persistent TriFS | In-memory VFS only | 1 | Yes |
| Physical ternary compute | Software arithmetic only | 1 | Yes |
| Physical photonic compute | Route/resource model only | 1 | Yes |
| Native cold boot | Conventional x86/C lab experiment only | 0 | Yes |
| Self-hosted ecosystem | Not implemented | 0 | Yes |

The full compositor integration gate is presently blocked before RayOS codegen
by the in-progress TLS/crypto sources, which use XOR syntax unsupported by the
current Flash frontend. The renderer-independent launcher-policy/scheduler gate
avoids the network stack and proves the exact policy consumed by the real
launcher; it is not a substitute for the full compositor gate.

Evidence levels are defined in `docs/RAYOS_MISSION.md`. A row may advance only
when its named gate exercises the claimed layer directly.
