# esc — tool-forging agent prompt

You have access to `esc`, a composable primitives compiler. Instead of writing bash scripts or one-off code, you can forge native binaries from small JSON manifests. Compiled tools are cached by content hash and reused automatically.

## When to use esc

Use esc when you need a small, reusable tool that:
- Transforms text (upper/lower/trim/replace/split)
- Does arithmetic or number formatting
- Reads/writes files
- Fetches URLs (HTTP/HTTPS GET)
- Reads CLI arguments or environment variables
- Pipes data between steps

Do NOT use esc for:
- Complex logic with loops or recursion (esc has no loops)
- Anything requiring external libraries
- One-shot commands better served by existing unix tools

## Quick reference

```bash
# Check what tools already exist in cache
esc tools list

# See details of a cached tool (IO contract, manifest)
esc inspect <binary-path>

# List all available primitives
esc primitives
esc primitives --machine   # JSON output

# Forge a tool from a manifest
esc compose manifest.json -o ./my-tool --store   # compile + cache
esc compose manifest.json --machine --store      # JSON output (for self-correction)
esc compose manifest.json --machine --store --goal "what it does" --tags "tag1,tag2"  # compile + cache + auto-record to memory

# See generated Rust code without compiling
esc expand manifest.json
```

## How to forge a tool

### Step 1 — Search memory

Always search first. Never forge what already exists.

```bash
esc memory search "<keywords from the goal>"
```

Returns compact matches: hash, app name, goal, and how found (direct/edge/shared_tags). If a match exists and binary exists, skip to step 5 (run it).

To inspect a near-match and get its full manifest:

```bash
esc memory show <hash-prefix>
```

### Step 2 — Write a manifest

A manifest is a JSON object with three fields:

```json
{
  "app": "short-name",
  "capabilities": ["io_write", "net_read"],
  "nodes": [
    {"id": "x", "use": "arg_str", "params": {"index": 1}},
    {"id": "out", "use": "print_str", "bind": {"value": "x"}}
  ]
}
```

Rules:
- **app**: short kebab-case name (max 64 bytes)
- **capabilities**: declare every effect used — `io_read`, `io_write`, `fs_read`, `fs_write`, `net_read`, `env_read`
- **nodes**: ordered list. Each node has `id`, `use` (primitive name), optional `params` and `bind`
- **bind**: references earlier node IDs only. Data flows forward.
- Keep it minimal — fewest nodes possible

### Step 3 — Compile + record in one command

```bash
esc compose /tmp/esc_<name>.json --machine --store --goal "<natural language goal>" --tags "<comma,separated,tags>"
```

**IMPORTANT**: `--goal` and `--tags` are REQUIRED with `--store`. Always provide both. They auto-record the tool to memory on both fresh compiles and cache hits.

If it succeeds:
```json
{"status":"ok","binary":"/home/.../.esc/bin/abc123...","hash":"abc123...","app":"my-tool","cached":false}
```

If it fails, you get structured error + hint:
```json
{"status":"error","error":{"kind":"missing_capability","hint":"add \"net_read\" to the capabilities array"}}
```

Fix the manifest according to the hint and retry. Max 3 attempts.

### Step 4 — Run the tool

```bash
~/.esc/bin/<hash> arg1 arg2
```

Or if you used `-o ./name`:
```bash
./name arg1 arg2
```

### Step 5 — Record edges (optional)

If the new tool is a variant of an existing one, add an edge:

```bash
esc memory relate <existing-hash> <new-hash> variant_of
```

## Primitives cheat sheet

### Data sources
| Primitive | What it does | Key params/binds |
|-----------|-------------|-----------------|
| `const_num` | Numeric literal | `params: {value: 42}` |
| `const_str` | String literal | `params: {value: "hello"}` |
| `arg_num` | CLI arg as number | `params: {index: 1}` |
| `arg_str` | CLI arg as string | `params: {index: 1}` |
| `arg_count` | Count of CLI args | — |
| `env_str` | Env var by name | `params: {name: "HOME"}` |
| `env_str_dyn` | Env var by bound name | `bind: {name: ...}` |
| `read_stdin` | Read one line | `params: {prompt: "? "}` |
| `read_stdin_all` | Read all stdin | — |
| `read_file` | Read file | `params: {path: "f.txt"}` |
| `read_file_dyn` | Read file (dynamic path) | `bind: {path: ...}` |
| `http_get` | HTTP GET static URL | `params: {url: "https://..."}` |
| `http_get_dyn` | HTTP GET dynamic URL | `bind: {url: ...}` |

### Arithmetic (all bind lhs/rhs:num → provide num)
`add`, `sub`, `mul`, `div`, `mod_num`, `floor`, `abs`

### Comparison (bind lhs/rhs:num → provide bool)
`gt`, `lt`, `eq_num`

### Boolean logic (bind bool → provide bool)
`and_bool`, `or_bool`, `not_bool`

### Branching (bind cond:bool → provide num or str)
`select_num` (cond/then/else), `select_str` (cond/then/else)

### String ops
| Primitive | Binds | Provides |
|-----------|-------|----------|
| `concat` | left:str, right:str | str |
| `len_str` | text:str | num |
| `upper_str` | text:str | str |
| `lower_str` | text:str | str |
| `trim_str` | text:str | str |
| `substr` | text:str, start:num, len:num | str |
| `contains_str` | text:str, needle:str | bool |
| `replace_str` | text:str, pattern:str, replacement:str | str |
| `split_count` | text:str, delim:str | num |
| `split_nth` | text:str, delim:str, index:num | str |
| `repeat_str` | text:str, times:num | str |
| `to_string` | value:num | str |
| `parse_num` | text:str | num |
| `format_str` | v1:str, v2?:str + `params: {template: "...{1}...{2}..."}` | str |

### Path ops
`cwd`, `path_join` (left:str, right:str)

### Output sinks
| Primitive | Binds | Effect |
|-----------|-------|--------|
| `print_num` | value:num | io_write |
| `print_str` | value:str | io_write |
| `write_file` | content:str + `params: {path: "..."}` | fs_write |
| `write_file_dyn` | path:str, content:str | fs_write |
| `append_file` | content:str + `params: {path: "..."}` | fs_write |
| `exit_code` | code:num | — |

## Capabilities

Every primitive with side effects requires the corresponding capability declared in the manifest:

| Capability | Required by |
|-----------|------------|
| `io_read` | read_stdin, read_stdin_all |
| `io_write` | print_num, print_str, read_stdin (prompt) |
| `fs_read` | read_file, read_file_dyn |
| `fs_write` | write_file, write_file_dyn, append_file |
| `net_read` | http_get, http_get_dyn |
| `env_read` | env_str, env_str_dyn |

## Patterns

Common tool patterns you'll forge often:

**CLI filter** (arg in → transform → stdout):
```
arg_str(1) → upper_str → print_str
```

**Pipe filter** (stdin → transform → stdout):
```
read_stdin_all → trim_str → upper_str → print_str
```

**URL fetcher** (build URL from arg → fetch → print):
```
arg_str(1) → concat(base_url, arg) → http_get_dyn → trim_str → print_str
```

**File processor** (read → transform → write):
```
read_file("in.txt") → upper_str → write_file("out.txt")
```

**Numeric tool** (args → compute → print):
```
arg_num(1) → arg_num(2) → add → print_num
```

**Conditional** (check → branch → output):
```
arg_num(1) → const_num(0) → gt → select_str(cond, "positive", "non-positive") → print_str
```
