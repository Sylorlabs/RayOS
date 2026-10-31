# Execution Domains

## Values

RayOS stores the execution domain as a native `t9` value:

- `+1`: balanced-ternary native
- `0`: unspecified or rejected
- `-1`: binary compatibility chamber

The zero state is deliberately not “automatic.” Resolving zero always tries the
ternary entry only. If no ternary entry exists, resolution fails and the user
must explicitly choose Binary.

The executable policy is implemented in `core/execution_domain.flash`.

## Package contract

An application manifest may expose a ternary entry, a binary entry, or both:

```text
name = "example"
ternary_entry = "app/example.ray"
binary_entry = "compat/example.elf"
```

A dual package is one product with two independently declared implementations.
It is not permission to translate a native failure into a binary launch.

## Capability boundary

Native applications may receive native memory and fabric-control capabilities.
Binary applications receive proxies for display, input, files and networking.
They never receive native-memory or fabric-control authority.

The compatibility monitor must validate every request, copy or transcode data
across the boundary, bound memory and execution time, and terminate the chamber
on malformed requests. A binary guest must be unable to forge a ternary task ID
or native capability.

## User experience

The launcher defaults to Balanced Ternary. The user can choose Balanced Ternary
or Binary Compat before starting an app. Each app displays a `T`, `B`, or `T+B`
badge. Unsupported combinations remain in the launcher with an explanation.
Tab toggles the selected domain for keyboard users.

Task Manager and `ps` display the actual scheduled domain. This is execution
truth, not package metadata.
