# SUCKLESS.md

el-stupido follows a suckless-style design: small surface area, explicit behavior, and hard limits.

## Core stance

- Keep `esc` a tiny compiler, not a framework.
- Prefer deletion over feature growth.
- Make behavior obvious from the manifest; no hidden heuristics.
- Keep execution bounded and deterministic.
- Keep the default core domain-agnostic (compute/io/fs only).
- Prefer compact tape manifests as canonical model-facing input; keep `.esc` and JSON as compatibility inputs.

## Non-goals

- No agent runtime or orchestration layer.
- No baked-in web framework semantics in core.
- No plugin system or dynamic code loading.
- No unbounded loops in generated behavior.

## Hard limits

These limits are enforced in `compiler/src/compose.rs`:

- `MAX_APP_BYTES = 64`
- `MAX_NODE_ID_BYTES = 64`
- `MAX_NODES = 256`
- `MAX_CAPABILITIES = 16`
- `MAX_STRING_PARAM_BYTES = 4096`
- `MAX_REPEAT_TIMES = 10000`

`repeat_str` is runtime-clamped to `MAX_REPEAT_TIMES` in `compiler/src/emit.rs`.

## Rules for new primitives

- Must declare precise typed inputs/outputs (`params`, `binds`, `provides`).
- Must declare effects explicitly (`pure`, `io_*`, `fs_*`).
- Must be bounded in time/space.
- Must have at least one small manifest example.
- If the primitive adds broad policy/config surface, reject it.

## Maintenance policy

- If a feature does not clearly improve constrained generation quality, remove it.
- If code is not used by core path, delete it.
- Keep error messages concrete and local to the node that failed.
