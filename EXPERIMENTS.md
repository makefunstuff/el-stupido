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

## Round 5: JSON Schema Constrained Decoding

Script: `tools/constrained_gen.py`
Date: 2026-02-23

**Approach**: Use ollama's structured output (`format=json_schema`) to force models to emit a JSON program spec instead of raw code. A converter transforms JSON → el-stupido source. This eliminates syntax errors — the model can only make *semantic* mistakes.

**Schema** (math tasks):
```json
{
  "functions": [{"name": "...", "params": "...", "body": "..."}],
  "main_body": "..."
}
```

**5 tasks tested**: factorial, fizzbuzz, sum-of-squares, fibonacci, primes

### Compiler Fix During Round 5

During benchmarking, discovered that sumsq and primes failed across ALL models because the compiler didn't infer return types for multi-line functions with `return expr`. Functions like `fn is_prime(n) { ... return 0 ... return 1 }` were declared as `void` in LLVM IR.

**Fix**: Added `block_has_return_value()` recursive scanner in `src/parser.c` — walks blocks, if/else, while, for, match to find any `return expr` and infers `i32` return type. This immediately bumped top models from 3/5 → 4/5.

### Results (after compiler fix)

| Model | Size | fact | fizz | sumsq | fib | primes | Schema | Free |
|---|---|---|---|---|---|---|---|---|
| deepseek-coder:1.3b | 1B | CF/CF | CF/CF | CF/CF | CF/CF | CF/CF | **0/5** | 0/5 |
| qwen2.5-coder:1.5b | 1.5B | CF/CF | CF/CF | W/CF | CF/CF | CF/CF | **0/5** | 0/5 |
| lfm-opencode:1.2b | 1.2B | CF/CF | CF/CF | CF/CF | CF/CF | CF/CF | **0/5** | 0/5 |
| lfm2.5-thinking:1.2b | 1.2B | P/CF | CF/CF | CF/CF | CF/CF | CF/CF | **1/5** | 0/5 |
| codegemma:2b | 3B | P/CF | W/CF | CF/F | P/CF | CF/CF | **2/5** | 0/5 |
| sam860/LFM2:2.6b | 2.6B | P/CF | CF/CF | CF/CF | CF/CF | JE/CF | **1/5** | 0/5 |
| starcoder2:3b | 3B | P/F | CF/CF | P/F | JE/CF | CF/F | **2/5** | 0/5 |
| qwen2.5-coder:3b | 3.1B | CF/CF | P/CF | CF/CF | P/CF | CF/CF | **2/5** | 0/5 |
| granite4 | 3.4B | CF/P | CF/CF | CF/CF | P/CF | CF/CF | **1/5** | 1/5 |
| **qwen2.5-coder:7b** | 7.6B | P/CF | P/CF | P/CF | P/CF | CF/CF | **4/5** | 0/5 |
| **phi4** | 14.7B | P/CF | P/CF | P/CF | P/CF | CF/CF | **4/5** | 0/5 |
| qwen3:8b | 8.2B | P/P | W/P | CF/CF | P/P | CF/F | **2/5** | 3/5 |

*P=pass, CF=compile fail, W=wrong output, F=fail, JE=JSON error. Format: schema/free.*

### Key Findings

1. **Schema constrained decoding wins decisively for sub-4B models**: Total across sub-4B (9 models): **9 schema pass vs 1 free pass**. Without schema, tiny models are nearly useless.

2. **Free generation is useless under 4B**: Only granite4 (3.4B) managed 1 free pass across all sub-4B models. Schema is the ONLY viable path for tiny models.

3. **Best sub-4B models (schema)**: codegemma:2b, starcoder2:3b, qwen2.5-coder:3b — all at **2/5**.

4. **Best overall (schema)**: qwen2.5-coder:7b and phi4 tied at **4/5**. The remaining failure (primes) uses `sqrt()` as int or `step N` in for-loops — fixable compiler issues.

5. **Token efficiency**: Schema averages ~100 tokens, free averages ~400 tokens. Schema is **4x more compact**.

6. **qwen3:8b is anomalous**: It's the only model where free (3/5) beats schema (2/5). This 8B model has enough capacity to learn el-stupido patterns from the prompt alone, making the JSON intermediate format an unnecessary constraint.

### Remaining Primes Blocker

Every model fails primes (the hardest task). Models generate valid algorithms but use unsupported features:
- `sqrt(n)` as integer loop bound (sqrt returns double)
- `step 2` in for loops (not supported)
- `return true`/`return false` (booleans not yet supported as return values)
- Forward function references (calling before declaration)

These are all fixable compiler improvements.

## Progress Across Rounds

| Round | Approach | Pass Rate | Key Change |
|---|---|---|---|
| 1 | Original (short keywords, imperative) | 2/12 (17%) | baseline |
| 2 | Natural keywords + var/let/..= | 4/12 (33%) | accept what LLMs write |
| 3 | Declarative combinators | **8/12 (67%)** | eliminate loops from LLM output |
| 4 | HTTP codebook (web server) | 6/12 (50%) | 4-line DSL → full server |
| 5 | JSON schema constrained | **4/5 (80%)** best | structured output + compiler fix |

## Reproducing

```bash
# Round 3: factorial free generation
./tools/llm_test.sh

# Round 4: HTTP codebook
./tools/llm_bench_http.sh

# Round 5: constrained vs free, all tasks
python3 tools/constrained_gen.py --all-tasks --model qwen2.5-coder:7b
python3 tools/constrained_gen.py --all-tasks --model phi4

# Round 5: specific model
./tools/llm_bench_gbnf.sh qwen2.5-coder:3b
```

Generated source files saved in `tests/llm_results/`, `tests/llm_results_http/`, `tests/llm_constrained/`.
