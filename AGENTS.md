Build: `cd compiler && cargo build`
Run: `./compiler/target/debug/esc <subcommand>`
Test: `./compiler/target/debug/esc compose examples/compose_sum.json -o ./sum_demo && ./sum_demo`

Subcommands: compose, expand, grammar, primitives, tools, inspect, memory, context

Do NOT modify README.md — it's human-written.

## Context — your permanent memory

You have a persistent brain via `esc context` and `esc memory`. Knowledge accumulates across sessions, models, and providers. The memory graph on this machine is yours — use it.

### At session start

```bash
esc context assemble          # see what you know from prior sessions
esc context recall "project"  # pull relevant knowledge from memory graph
```

### During work — feed your observations

After significant tool calls (builds, file reads, discoveries, errors), feed the output so the observer can manage it:

```bash
esc context feed --source "cargo build" "error[E0433]: failed to resolve serde"
esc context feed --source "architecture" "context.rs implements slot lifecycle with auto-wisdom"
esc context feed --source "decision" "chose atomic-server for typed graph with tantivy search"
```

Feed is silent (zero output) and auto-creates a session if none exists. Do this naturally as you work — it costs nothing.

### Observer — context lifecycle management

The observer runs as a subtask agent (not a background process). It triggers on session idle via the context plugin, or can be invoked manually:

```bash
opencode run --agent observer "Run all 3 jobs"
```

The observer manages:
1. **Context slots** — archive cold, drop stale, compact verbose, touch relevant
2. **Conversation history** — reads `~/.esc/context/history.jsonl`, writes semantic compactions
3. **Memory graph** — deduplicates and supersedes redundant entries

Config lives globally at `~/.config/opencode/agents/observer.md`.

### When you need knowledge

```bash
esc context recall "HTTP patterns"     # search memory graph
esc context assemble                   # see curated context with recommendations
```

### What to feed

Feed things worth remembering across sessions:
- Build errors and their fixes
- Architecture discoveries
- Design decisions with rationale
- Tool forge results
- User preferences and corrections

Do NOT feed: raw file contents, verbose logs, transient scratch work.

## Tool forging

Use `@forge` to invoke the tool-forging agent. It searches memory, forges native binaries from JSON manifests, and records them for reuse. See `.opencode/agents/forge.md`.

Quick manual test:
```bash
esc memory search "add numbers"
esc memory log
esc compose examples/compose_http_test.json --machine --store
```
