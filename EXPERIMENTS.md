# el-stupido LLM Benchmark Results

Script: `tools/llm_test.sh`
Date: 2026-02-23

## Round 3: Declarative (current)

**Approach**: Built-in combinators (`product`/`sum`/`print`), implicit main, one-liner functions, type inference. LLM writes WHAT, compiler generates HOW.

**Spec given to LLM** (~60 tokens):
```
name(args) = expr (one-liner fn, types inferred)
Built-ins: product(1..=n) sum(1..n) print(expr)
x := expr. for i := 1..=n { }. if/else/while/return.
```

**Target output** (what we want LLMs to generate):
```
fact(n) = product(1..=n)
fn main() { for i := 1..=12 { print(fact(i)) } }
```

| Model | Size | Result | Generated Code |
|---|---|---|---|
| deepseek-coder:1.3b | 776MB | FAIL | writes prose, not code |
| qwen2.5-coder:3b | 1.9GB | **PASS** | `print(product(1..=x))` |
| starcoder2:3b | 1.7GB | FAIL | Python import (completion model) |
| granite4 | 2.1GB | **PASS** | `print(product(1..=n))` inline |
| lfm2.5-thinking | 731MB | FAIL | Python syntax |
| qwen2.5-coder:7b | 4.7GB | **PASS** | `fact(n) = product(1..=n)` |
| deepseek-r1 | 5.2GB | **PASS** | `fact(n) = product(1..=n)` |
| qwen3:8b | 5.2GB | **PASS** | `fact(n) = product(1..=n)` |
| phi4 | 9.1GB | **PASS** | `fact(n) = product(1..=n)` |
| qwen2.5-coder:14b | 9.0GB | **PASS** | `fact(n) = product(1..=n)` |
| qwen2.5:7b-instruct | 4.7GB | **PASS** | `fact(n) = product(1..=n)` |
| qwen2.5:14b-instruct | 9.0GB | WRONG | `product(1..n)` (off-by-one, exclusive range) |

**Pass rate: 8/12 (67%)**

## Round 4: HTTP Server (Codebook)

Script: `tools/llm_bench_http.sh`

**Approach**: Codebook DSL — `use web` activates HTTP server framework. LLM writes 4-6 line declarative server spec, compiler expands to 400+ lines of fork-per-connection C server code.

**Spec given to LLM** (~80 tokens):
```
el-stupido web codebook. "use web" activates HTTP server.
listen PORT. /path "text". /path fn handler_name.
Handler: fn name(fd: i32, body: *u8) { http_reply(fd, 200, "text/plain", "content") }
```

**Target output** (what we want LLMs to generate):
```
use web
listen 9090
/ "Welcome"
/hello "Hello World"
/status fn status_handler
fn status_handler(fd: i32, body: *u8) { http_reply(fd, 200, "text/plain", "OK") }
```

| Model | Size | Result | Notes |
|---|---|---|---|
| deepseek-coder:1.3b | 776MB | FAIL | Elixir/Phoenix code with prose |
| qwen2.5-coder:3b | 1.9GB | FAIL | Rust-like syntax (`web::get`, closures) |
| starcoder2:3b | 1.7GB | COMPILE_FAIL | `/fn hello(...)` merges route with fn keyword |
| granite4 | 2.1GB | **PASS** | Perfect codebook syntax |
| lfm2.5-thinking | 731MB | FAIL | Garbled one-liner with semicolons and `fn /` |
| qwen2.5-coder:7b | 4.7GB | COMPILE_FAIL | `use web;` / `listen 9090;` (semicolons on directives) + inline handler |
| deepseek-r1 | 5.2GB | **PASS** | Perfect — exact match to target |
| qwen3:8b | 5.2GB | COMPILE_FAIL | Inline handler on route line: `/status fn name(fd:i32, ...) { ... }` |
| phi4 | 9.1GB | **PASS** | Perfect codebook syntax |
| qwen2.5-coder:14b | 9.0GB | **PASS** | Perfect codebook syntax |
| qwen2.5:7b-instruct | 4.7GB | **PASS** | Perfect — exact match to target |
| qwen2.5:14b-instruct | 9.0GB | **PASS** | Perfect codebook syntax |

**Pass rate: 6/12 (50%)**

### COMPILE_FAIL Analysis (fixable)

These 3 models wrote *almost* correct code. Fixing any of these in the compiler would flip them to PASS:

1. **qwen2.5-coder:7b** — `use web;` and `listen 9090;` with trailing semicolons. Fix: tolerate semicolons after codebook directives.
2. **qwen3:8b** — `/status fn status_handler(fd: i32, body: *u8) { http_reply(...) }` inline on route line. Fix: parse inline handler definitions on route lines.
3. **starcoder2:3b** — `/fn hello(...)` confused route path with fn keyword. Harder to fix — fundamentally misunderstands the DSL.

Fixing (1) and (2) would bring HTTP to **8/12 (67%)**.

## Progress Across Rounds

| Round | Approach | Pass Rate | Key Change |
|---|---|---|---|
| 1 | Original (short keywords, imperative) | 2/12 (17%) | baseline |
| 2 | Natural keywords + var/let/..= | 4/12 (33%) | accept what LLMs write |
| 3 | Declarative combinators | **8/12 (67%)** | eliminate loops from LLM output |
| 4 | HTTP codebook (web server) | 6/12 (50%) | 4-line DSL → full server |

## What The Compiler Now Expands

| LLM writes | Compiler generates |
|---|---|
| `fact(n) = product(1..=n)` | `fn fact(n:i32)->i32 { acc:=1; for i:=1..=n { acc*=i }; return acc }` |
| `print(expr)` | `printf("%d\n", expr)` with type-directed format |
| `sum(1..=100)` | accumulator loop with `+=` |
| top-level statements | wrapped in `fn main() { ... }` |
| `f(x) = expr` | `fn f(x:i32)->i32 { return expr }` |

## Remaining Failures

Only 3 models fail, all unsalvageable:
- **deepseek-coder:1.3b** (776MB) — too small, writes prose not code
- **starcoder2:3b** (1.7GB) — completion model, not instruction-tuned
- **lfm2.5-thinking** (731MB) — thinking model, outputs Python

1 model compiles but wrong output:
- **qwen2.5:14b-instruct** — `product(1..n)` exclusive vs inclusive off-by-one

## New Features Added

1. `product(range)` / `sum(range)` / `count`/`min`/`max` — compiler-expanded combinators
2. `print(expr)` — type-directed printf
3. `|>` pipe operator — `x |> f` desugars to `f(x)`
4. `name(args) = expr` — one-liner function with implicit return
5. Implicit `fn main()` — top-level statements auto-wrapped
6. Type inference — params default to `i32` when omitted
7. `let`/`var`/`mut` accepted as noise words
8. `while`/`for`/`return`/`else`/`struct`/`extern`/`break`/`continue` — long keyword aliases
9. `..=` inclusive range operator

## Reproducing

```bash
# all models
./tools/llm_test.sh

# specific models
./tools/llm_test.sh qwen2.5-coder:3b granite4 phi4

# custom task
PROMPT="Write: sum of squares 1..100 using sum()" ./tools/llm_test.sh qwen2.5-coder:3b
```

Generated source files saved in `tests/llm_results/`.

HTTP server results saved in `tests/llm_results_http/`.
