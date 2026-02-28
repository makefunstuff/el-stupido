Build: `cd compiler && cargo build`
Run: `./compiler/target/debug/esc <subcommand>`
Test: `./compiler/target/debug/esc memory log && ./compiler/target/debug/esc memory search "test"`

Subcommands: memory

Do NOT modify README.md — it's human-written.

## Memory — persistent knowledge graph

You have a persistent brain via `esc memory`. Knowledge accumulates across sessions.

### Quick reference

```bash
esc memory log                    # recent activity timeline
esc memory search "keywords"      # find notes by topic
esc memory show <hash>            # full details of any entry
esc memory notes                  # list notes (--kind, --context filters)
esc memory note --kind <kind> --context "<project>" --tags "<tags>" "<summary>" "<detail>"
```

Note kinds: discovery, decision, pattern, issue

### What to record

Record after completing tasks:
- Errors and their fixes
- Architecture discoveries
- Design decisions with rationale
- User preferences and corrections

Do NOT record: raw file contents, verbose logs, transient scratch work.
