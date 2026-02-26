Read `context.grug` for project context.

Build: `cd compiler && cargo build`
Run: `./compiler/target/debug/esc <subcommand>`
Test: `./compiler/target/debug/esc compose examples/compose_sum.json -o ./sum_demo && ./sum_demo`

Subcommands: compose, expand, grammar, primitives, tools, inspect, memory

Do NOT modify README.md — it's human-written.

## Tool forging

Use `@forge` to invoke the tool-forging agent. It searches memory, forges native binaries from JSON manifests, and records them for reuse. See `.opencode/agents/forge.md`.

Quick manual test:
```bash
esc memory search "add numbers"
esc memory log
esc compose examples/compose_http_test.json --machine --store
```
