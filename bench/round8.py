#!/usr/bin/env python3
"""Round 8: Compose Manifest Generation Benchmark

Tests local LLMs generating esc compose manifests via Ollama.
Three modes per task: schema-constrained, json-constrained, free-form.
"""

import json
import os
import subprocess
import sys
import time
import urllib.request
from collections import defaultdict
from pathlib import Path

OLLAMA = os.environ.get("OLLAMA_HOST", "http://192.168.1.138:11434")
ESC = "./compiler/target/debug/esc"
RESULTS_DIR = Path("bench/results")
RESULTS_DIR.mkdir(parents=True, exist_ok=True)

# ── Models (ascending size) ──────────────────────────────────
MODELS = [
    "qwen2.5-coder:1.5b",
    "qwen2.5-coder:3b",
    "qwen3:4b",
    "qwen2.5-coder:7b",
    "qwen3:8b",
    "qwen2.5:14b-instruct",
    "qwen2.5-coder:14b",
    "qwen3:30b-a3b",
]

# ── JSON Schema for structured output ────────────────────────
SCHEMA = {
    "type": "object",
    "required": ["app", "capabilities", "nodes"],
    "properties": {
        "app": {"type": "string"},
        "capabilities": {
            "type": "array",
            "items": {
                "type": "string",
                "enum": ["io_read", "io_write", "fs_read", "fs_write", "net_read", "env_read"],
            },
        },
        "nodes": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["id", "use"],
                "properties": {
                    "id": {"type": "string"},
                    "use": {"type": "string"},
                    "params": {"type": "object"},
                    "bind": {"type": "object"},
                },
                "additionalProperties": False,
            },
        },
    },
    "additionalProperties": False,
}

# ── System prompt ────────────────────────────────────────────
SYSTEM_PROMPT = """You generate JSON manifests for the esc compose system.

RULES:
- Output a JSON object: {"app": string, "capabilities": [strings], "nodes": [node objects]}
- Each node: {"id": unique_string, "use": primitive_name, "params": {}, "bind": {}}
- "bind" values reference IDs of EARLIER nodes only. Forward references are forbidden.
- Types must match: primitives providing "num" can only bind to inputs expecting "num", etc.
- "capabilities" must list all effects: io_write for printing, io_read for stdin, fs_read/fs_write for files, net_read for HTTP, env_read for env vars.
- Pure primitives (math, string ops) need no capabilities.

PRIMITIVES:
- const_num: params {value: number}. provides: num.
- const_str: params {value: string}. provides: str.
- add: bind {lhs: num, rhs: num}. provides: num. (addition)
- sub: bind {lhs: num, rhs: num}. provides: num. (subtraction)
- mul: bind {lhs: num, rhs: num}. provides: num. (multiplication)
- div: bind {lhs: num, rhs: num}. provides: num. (division)
- mod_num: bind {lhs: num, rhs: num}. provides: num. (modulo)
- floor: bind {value: num}. provides: num.
- abs: bind {value: num}. provides: num.
- gt: bind {lhs: num, rhs: num}. provides: bool. (greater than)
- lt: bind {lhs: num, rhs: num}. provides: bool. (less than)
- eq_num: bind {lhs: num, rhs: num}. provides: bool. (equals)
- and_bool: bind {lhs: bool, rhs: bool}. provides: bool.
- or_bool: bind {lhs: bool, rhs: bool}. provides: bool.
- not_bool: bind {value: bool}. provides: bool.
- select_num: bind {cond: bool, then: num, else: num}. provides: num.
- select_str: bind {cond: bool, then: str, else: str}. provides: str.
- to_string: bind {value: num}. provides: str.
- concat: bind {left: str, right: str}. provides: str.
- len_str: bind {text: str}. provides: num.
- upper_str: bind {text: str}. provides: str.
- lower_str: bind {text: str}. provides: str.
- trim_str: bind {text: str}. provides: str.
- parse_num: bind {text: str}. provides: num.
- format_str: params {template: string}, bind {v1: str, v2?: str}. provides: str. Use {1} and {2} as placeholders.
- print_num: bind {value: num}. provides: sink. effects: [io_write].
- print_str: bind {value: str}. provides: sink. effects: [io_write].
- arg_num: params {index: number}. provides: num. (CLI arg as number, 1-indexed)
- arg_str: params {index: number}. provides: str. (CLI arg as string, 1-indexed)
- arg_count: provides: num.
- read_stdin: params {prompt?: string}. provides: str. effects: [io_read, io_write].
- contains_str: bind {text: str, needle: str}. provides: bool.
- replace_str: bind {text: str, pattern: str, replacement: str}. provides: str.
- substr: bind {text: str, start: num, len: num}. provides: str.
- split_nth: bind {text: str, delim: str, index: num}. provides: str.
- split_count: bind {text: str, delim: str}. provides: num.
- repeat_str: bind {text: str, times: num}. provides: str.
- exit_code: bind {code: num}. provides: sink.

EXAMPLE — add 13+29, print result:
{"app":"sum","capabilities":["io_write"],"nodes":[{"id":"a","use":"const_num","params":{"value":13}},{"id":"b","use":"const_num","params":{"value":29}},{"id":"s","use":"add","bind":{"lhs":"a","rhs":"b"}},{"id":"out","use":"print_num","bind":{"value":"s"}}]}"""

# ── Tasks ────────────────────────────────────────────────────
# (name, prompt, cli_args, expected_output, description)
TASKS = [
    (
        "sum_const",
        "Generate a manifest that computes 17 + 25 and prints the result.",
        [],
        "42",
        "basic arithmetic",
    ),
    (
        "mul_const",
        "Generate a manifest that computes 6 * 7 and prints the result.",
        [],
        "42",
        "multiplication",
    ),
    (
        "temp_conv",
        "Generate a manifest that reads CLI argument 1 as a number (Fahrenheit), converts to Celsius using the formula (F - 32) * 5 / 9, and prints the Celsius value.",
        ["212"],
        "100",
        "multi-step arithmetic with args",
    ),
    (
        "greeting",
        "Generate a manifest that reads CLI argument 1 as a string, concatenates the string \"Hello, \" before it, and prints the resulting string.",
        ["World"],
        "Hello, World",
        "string concat with args",
    ),
    (
        "str_upper",
        "Generate a manifest that reads CLI argument 1 as a string, converts it to uppercase, and prints it.",
        ["hello"],
        "HELLO",
        "string transform",
    ),
]


def ollama_generate(model, prompt, mode="schema", timeout=120):
    """Call Ollama API. Returns (response_text, eval_tokens, elapsed_ms)."""
    payload = {
        "model": model,
        "system": SYSTEM_PROMPT,
        "prompt": prompt,
        "stream": False,
        "options": {"num_predict": 512, "temperature": 0.1},
    }

    if mode == "schema":
        payload["format"] = SCHEMA
    elif mode == "json":
        payload["format"] = "json"
    # "free" mode: no format constraint

    data = json.dumps(payload).encode()
    req = urllib.request.Request(
        f"{OLLAMA}/api/generate",
        data=data,
        headers={"Content-Type": "application/json"},
    )

    start = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            result = json.loads(resp.read())
    except Exception as e:
        return None, 0, 0, str(e)

    elapsed_ms = int((time.monotonic() - start) * 1000)
    response_text = result.get("response", "")
    eval_tokens = result.get("eval_count", 0)

    return response_text, eval_tokens, elapsed_ms, None


def try_extract_json(text):
    """Try to extract JSON from free-form text."""
    text = text.strip()
    # Direct parse
    try:
        json.loads(text)
        return text
    except json.JSONDecodeError:
        pass
    # Find JSON block in markdown
    for marker in ["```json", "```"]:
        if marker in text:
            start = text.index(marker) + len(marker)
            end = text.find("```", start)
            if end > start:
                candidate = text[start:end].strip()
                try:
                    json.loads(candidate)
                    return candidate
                except json.JSONDecodeError:
                    pass
    # Find first { ... } block
    brace_start = text.find("{")
    if brace_start >= 0:
        depth = 0
        for i in range(brace_start, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    candidate = text[brace_start : i + 1]
                    try:
                        json.loads(candidate)
                        return candidate
                    except json.JSONDecodeError:
                        pass
                    break
    return None


def compile_and_run(manifest_text, cli_args, task_name, model_name, mode):
    """Compile manifest with esc compose, run binary, return (status, output, detail)."""
    safe = f"{model_name}_{task_name}_{mode}".replace(":", "_").replace("/", "_")
    manifest_path = RESULTS_DIR / f"{safe}.json"
    binary_path = f"/tmp/esc_bench_{safe}"

    # Write manifest
    manifest_path.write_text(manifest_text)

    # Compile
    result = subprocess.run(
        [ESC, "compose", str(manifest_path), "-o", binary_path],
        capture_output=True,
        text=True,
        timeout=30,
    )

    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip().split("\n")[0]
        return "CF", "", detail

    if not os.path.exists(binary_path):
        return "CF", "", "no binary produced"

    # Run
    try:
        run = subprocess.run(
            [binary_path] + cli_args, capture_output=True, text=True, timeout=10
        )
        output = run.stdout.strip()
    except subprocess.TimeoutExpired:
        output = ""
        return "W", "", "timeout"
    except Exception as e:
        return "W", "", str(e)
    finally:
        try:
            os.unlink(binary_path)
        except OSError:
            pass

    return "RAN", output, ""


def check_output(actual, expected):
    """Compare output, handling numeric formatting."""
    actual = actual.strip()
    expected = expected.strip()

    if actual == expected:
        return True

    # Numeric comparison (42.0 == 42, 100.0 == 100)
    try:
        return abs(float(actual) - float(expected)) < 0.01
    except (ValueError, TypeError):
        return False


def warm_model(model):
    """Load model into memory."""
    try:
        payload = json.dumps(
            {
                "model": model,
                "prompt": "hi",
                "stream": False,
                "options": {"num_predict": 1},
            }
        ).encode()
        req = urllib.request.Request(
            f"{OLLAMA}/api/generate",
            data=payload,
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(req, timeout=120):
            pass
    except Exception:
        pass


def main():
    # Check connectivity
    try:
        with urllib.request.urlopen(f"{OLLAMA}/api/version", timeout=5) as r:
            version = json.loads(r.read())["version"]
    except Exception as e:
        print(f"ERROR: Cannot reach Ollama at {OLLAMA}: {e}")
        sys.exit(1)

    modes = ["schema", "json", "free"]
    total_runs = len(MODELS) * len(TASKS) * len(modes)

    print("=" * 90)
    print(f" Round 8: Compose Manifest Generation via Local Models")
    print(f" Ollama: {OLLAMA} (v{version})")
    print(f" Models: {len(MODELS)}  Tasks: {len(TASKS)}  Modes: {', '.join(modes)}")
    print(f" Total runs: {total_runs}")
    print("=" * 90)
    print()

    results = []
    run_count = 0

    for model in MODELS:
        print(f"── {model} ──")
        warm_model(model)

        for task_name, prompt, cli_args, expected, desc in TASKS:
            for mode in modes:
                run_count += 1
                sys.stdout.write(
                    f"  [{run_count}/{total_runs}] {task_name:12s} {mode:6s} ... "
                )
                sys.stdout.flush()

                # Generate
                response, tokens, elapsed_ms, error = ollama_generate(
                    model, prompt, mode
                )

                if error:
                    status = "ERR"
                    detail = error
                    output = ""
                    print(f"ERR  ({error})")
                    results.append(
                        {
                            "model": model,
                            "task": task_name,
                            "mode": mode,
                            "status": "ERR",
                            "tokens": 0,
                            "ms": 0,
                            "detail": error,
                        }
                    )
                    continue

                # For free mode, try to extract JSON
                manifest_text = response
                if mode == "free":
                    extracted = try_extract_json(response)
                    if extracted is None:
                        print(f"JE   | {tokens:4d} tok | {elapsed_ms:5d} ms | no JSON found")
                        # Save raw response for inspection
                        safe = f"{model}_{task_name}_{mode}".replace(":", "_").replace("/", "_")
                        (RESULTS_DIR / f"{safe}.raw").write_text(response)
                        results.append(
                            {
                                "model": model,
                                "task": task_name,
                                "mode": mode,
                                "status": "JE",
                                "tokens": tokens,
                                "ms": elapsed_ms,
                                "detail": "no JSON in free-form output",
                            }
                        )
                        continue
                    manifest_text = extracted

                # Compile and run
                status, output, detail = compile_and_run(
                    manifest_text, cli_args, task_name, model, mode
                )

                if status == "CF":
                    print(f"CF   | {tokens:4d} tok | {elapsed_ms:5d} ms | {detail}")
                    results.append(
                        {
                            "model": model,
                            "task": task_name,
                            "mode": mode,
                            "status": "CF",
                            "tokens": tokens,
                            "ms": elapsed_ms,
                            "detail": detail,
                        }
                    )
                    continue

                # Check output
                passed = check_output(output, expected)
                final_status = "P" if passed else "W"
                mark = "P ✓" if passed else "W ✗"
                print(
                    f"{mark:5s}| {tokens:4d} tok | {elapsed_ms:5d} ms | got: {output!r} {'' if passed else f'(expected: {expected!r})'}"
                )
                results.append(
                    {
                        "model": model,
                        "task": task_name,
                        "mode": mode,
                        "status": final_status,
                        "tokens": tokens,
                        "ms": elapsed_ms,
                        "detail": output,
                    }
                )

        print()

    # ── Summary ──────────────────────────────────────────────
    print("=" * 90)
    print(" RESULTS SUMMARY")
    print("=" * 90)

    # Per-model pass rates by mode
    tally = defaultdict(lambda: defaultdict(lambda: {"P": 0, "total": 0}))
    token_sums = defaultdict(lambda: {"sum": 0, "count": 0})

    for r in results:
        tally[r["model"]][r["mode"]]["total"] += 1
        if r["status"] == "P":
            tally[r["model"]][r["mode"]]["P"] += 1
        if r["tokens"] > 0:
            token_sums[r["mode"]]["sum"] += r["tokens"]
            token_sums[r["mode"]]["count"] += 1

    print(f"\n{'Model':<30s} {'Schema':>10s} {'JSON':>10s} {'Free':>10s}")
    print("─" * 65)

    mode_totals = defaultdict(lambda: {"P": 0, "total": 0})
    for model in MODELS:
        parts = []
        for mode in modes:
            d = tally[model][mode]
            parts.append(f"{d['P']}/{d['total']}")
            mode_totals[mode]["P"] += d["P"]
            mode_totals[mode]["total"] += d["total"]
        print(f"{model:<30s} {parts[0]:>10s} {parts[1]:>10s} {parts[2]:>10s}")

    print("─" * 65)
    totals = []
    for mode in modes:
        d = mode_totals[mode]
        pct = (d["P"] / d["total"] * 100) if d["total"] > 0 else 0
        totals.append(f"{d['P']}/{d['total']} ({pct:.0f}%)")
    print(f"{'TOTAL':<30s} {totals[0]:>10s} {totals[1]:>10s} {totals[2]:>10s}")

    # Token averages
    print(f"\nAvg tokens per mode:")
    for mode in modes:
        d = token_sums[mode]
        avg = d["sum"] / d["count"] if d["count"] > 0 else 0
        print(f"  {mode:8s}: {avg:.0f} tokens/response")

    # Sub-4B vs 4B+ breakdown
    print(f"\nSub-4B models (1.5B, 3B):")
    for mode in modes:
        p = sum(
            1
            for r in results
            if r["model"] in ("qwen2.5-coder:1.5b", "qwen2.5-coder:3b")
            and r["mode"] == mode
            and r["status"] == "P"
        )
        t = sum(
            1
            for r in results
            if r["model"] in ("qwen2.5-coder:1.5b", "qwen2.5-coder:3b")
            and r["mode"] == mode
        )
        print(f"  {mode:8s}: {p}/{t}")

    # Per-task breakdown
    print(f"\nPer-task pass rate (all models, schema mode):")
    for task_name, _, _, _, desc in TASKS:
        p = sum(
            1
            for r in results
            if r["task"] == task_name and r["mode"] == "schema" and r["status"] == "P"
        )
        t = sum(
            1
            for r in results
            if r["task"] == task_name and r["mode"] == "schema"
        )
        print(f"  {task_name:12s} ({desc:30s}): {p}/{t}")

    # Failure analysis
    failures = [r for r in results if r["status"] != "P"]
    if failures:
        print(f"\nFailure breakdown:")
        status_counts = defaultdict(int)
        for r in failures:
            status_counts[r["status"]] += 1
        for status, count in sorted(status_counts.items()):
            label = {
                "CF": "Compile failures",
                "W": "Wrong output",
                "JE": "JSON extraction failed",
                "ERR": "API errors",
            }.get(status, status)
            print(f"  {label}: {count}")

    # Save full results
    results_path = RESULTS_DIR / "round8.json"
    with open(results_path, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nFull results: {results_path}")


if __name__ == "__main__":
    main()
