#!/usr/bin/env python3
"""constrained_gen.py — JSON-schema constrained generation for el-stupido.

Uses ollama's structured output (format=json_schema) to force models
to produce valid program specifications. Then converts JSON → el-stupido source.

The model can NEVER produce invalid structure. It only makes semantic choices.

Usage:
    python3 tools/constrained_gen.py --task factorial --model qwen2.5-coder:3b
    python3 tools/constrained_gen.py --task http --model phi4
    python3 tools/constrained_gen.py --task fizzbuzz --model qwen3:8b
"""

import argparse
import json
import sys
import requests
import subprocess
import tempfile
import os

OLLAMA_URL = os.environ.get("OLLAMA_URL", "http://localhost:11434")

# ─── JSON Schemas ────────────────────────────────────────────────────
# These schemas constrain the model's output structure.
# The model can only produce valid JSON matching these schemas.
# Syntax errors become IMPOSSIBLE.

SCHEMA_MATH = {
    "type": "object",
    "properties": {
        "functions": {
            "type": "array",
            "description": "Helper functions",
            "items": {
                "type": "object",
                "properties": {
                    "name": {"type": "string"},
                    "params": {"type": "string", "description": "e.g. 'n' or 'a, b'"},
                    "body": {
                        "type": "string",
                        "description": "expression or statements",
                    },
                },
                "required": ["name", "params", "body"],
            },
        },
        "main_body": {
            "type": "string",
            "description": "Code for main function body (for loops, print calls, etc.)",
        },
    },
    "required": ["functions", "main_body"],
}

SCHEMA_HTTP = {
    "type": "object",
    "properties": {
        "port": {"type": "integer", "description": "Port number to listen on"},
        "routes": {
            "type": "array",
            "items": {
                "type": "object",
                "properties": {
                    "method": {"type": "string", "enum": ["GET", "POST"]},
                    "path": {
                        "type": "string",
                        "description": "URL path like / or /hello",
                    },
                    "type": {"type": "string", "enum": ["static", "handler"]},
                    "content": {
                        "type": "string",
                        "description": "For static: the text. For handler: function name",
                    },
                },
                "required": ["path", "type", "content"],
            },
        },
        "handlers": {
            "type": "array",
            "description": "Handler function definitions",
            "items": {
                "type": "object",
                "properties": {
                    "name": {"type": "string"},
                    "response_code": {"type": "integer"},
                    "content_type": {"type": "string"},
                    "body_text": {"type": "string"},
                },
                "required": ["name", "response_code", "content_type", "body_text"],
            },
        },
    },
    "required": ["port", "routes"],
}

# ─── Task Definitions ────────────────────────────────────────────────

TASKS = {
    "factorial": {
        "schema": SCHEMA_MATH,
        "prompt": (
            "el-stupido: compiled language. Write a JSON program spec.\n"
            "Available built-in: product(1..=n) computes n! (1*2*...*n)\n"
            "Available built-in: sum(1..=n) computes 1+2+...+n\n"
            "print(expr) prints a value. print(f(1..=n)) prints f(1),f(2),...,f(n).\n"
            "For loops: for i := 1..=n { body }\n\n"
            "Task: Define a factorial function using product(), then print factorials 1 through 12.\n"
            "In 'body' field, use el-stupido expressions: product(1..=n), print(fact(i)), for i := 1..=12 { print(fact(i)) }"
        ),
        "validate_cmd": 'python3 -c "import sys; lines=sys.stdin.read().strip().split(chr(10)); f=1\nfor i in range(1,13):\n f*=i\n if lines[i-1].strip()!=str(f): exit(1)"',
        "expected_last": "479001600",
    },
    "fizzbuzz": {
        "schema": SCHEMA_MATH,
        "prompt": (
            "el-stupido: compiled language. Write a JSON program spec.\n"
            "printf(fmt, args) for formatted output. print(expr) for simple values.\n"
            "For loops: for i := 1..=n { body }. Modulo: n % 3.\n"
            'if expr { } else { }. Strings: "Fizz". Printf: printf("%s\\n", "Fizz")\n\n'
            'Task: FizzBuzz 1 to 30. For each i: if divisible by 15 print "FizzBuzz",\n'
            'by 3 print "Fizz", by 5 print "Buzz", else print the number.\n'
            'Put the full logic in main_body. Use printf for strings, printf("%d\\n",i) for numbers.'
        ),
        "validate_cmd": 'python3 -c "import sys; lines=sys.stdin.read().strip().split(chr(10))\nfor i in range(1,31):\n exp="FizzBuzz" if i%15==0 else "Fizz" if i%3==0 else "Buzz" if i%5==0 else str(i)\n if lines[i-1].strip()!=exp: exit(1)"',
        "expected_last": "FizzBuzz",
    },
    "sumsq": {
        "schema": SCHEMA_MATH,
        "prompt": (
            "el-stupido: compiled language. Write a JSON program spec.\n"
            "sum(1..=n) computes 1+2+...+n. But sum(f(range)) is NOT supported.\n"
            "Use a for loop to accumulate: acc := 0; for i := 1..=100 { acc += i * i }\n"
            "print(expr) prints a value.\n\n"
            "Task: Compute and print sum of squares from 1 to 100.\n"
            "Result should be 338350. Use a loop in main_body."
        ),
        "validate_cmd": 'python3 -c "import sys; exit(0 if sys.stdin.read().strip()=="338350" else 1)"',
        "expected_last": "338350",
    },
    "fibonacci": {
        "schema": SCHEMA_MATH,
        "prompt": (
            "el-stupido: compiled language. Write a JSON program spec.\n"
            "Recursive functions: fib(n) with body: n < 2 ? n : fib(n-1) + fib(n-2)\n"
            "Ternary: cond ? a : b. print(f(a..=b)) prints f(a)...f(b).\n"
            "For loops: for i := 1..=n { print(fib(i)) }\n\n"
            "Task: Define fibonacci recursively, print first 20 fibonacci numbers."
        ),
        "validate_cmd": 'python3 -c "import sys; exit(0 if "6765" in sys.stdin.read() else 1)"',
        "expected_last": "6765",
    },
    "primes": {
        "schema": SCHEMA_MATH,
        "prompt": (
            "el-stupido: compiled language. Write a JSON program spec.\n"
            "Loops: for i := a..=b { }. while cond { }. if/else.\n"
            "Modulo: n % d. print(expr). break. Boolean: 0=false, nonzero=true.\n"
            "Variables: x := val. Assignment: x = val.\n\n"
            "Task: Print all prime numbers up to 50, one per line.\n"
            "Use trial division in main_body. No helper functions needed."
        ),
        "validate_cmd": 'python3 -c "import sys,re; nums=set(int(x) for x in re.findall(r"\\d+",sys.stdin.read())); exit(0 if {2,3,5,7,11,13,17,19,23,29,31,37,41,43,47}.issubset(nums) else 1)"',
        "expected_last": "47",
    },
    "http": {
        "schema": SCHEMA_HTTP,
        "prompt": (
            "el-stupido HTTP server. Write a JSON spec for routes.\n"
            "Static routes serve text. Handler routes call a function.\n"
            "Handlers use: http_reply(fd, code, content_type, body_text)\n\n"
            "Task: HTTP server on port 9090 with 3 routes:\n"
            'GET / returns "Welcome"\n'
            'GET /hello returns "Hello World"\n'
            'GET /status calls handler status_handler which returns "OK"'
        ),
        "validate_cmd": None,  # needs server start/curl
        "expected_last": None,
    },
}

# ─── JSON → el-stupido source converter ──────────────────────────────


def _is_prose(body):
    """Detect English prose in a code body (not actual code)."""
    words = body.split()
    if len(words) < 3:
        return False
    # prose indicators: common English words that don't appear in code
    prose_words = {
        "the",
        "of",
        "using",
        "that",
        "this",
        "which",
        "output",
        "formatted",
        "simple",
        "value",
        "computes",
        "returns",
        "calculates",
        "function",
        "defined",
        "a",
        "an",
    }
    hits = sum(1 for w in words if w.lower().rstrip(".,;:") in prose_words)
    return hits >= 2


# builtins that should never be redefined
_BUILTINS = {
    "printf",
    "print",
    "malloc",
    "free",
    "exit",
    "strlen",
    "strcmp",
    "strstr",
    "memcpy",
    "memset",
    "sprintf",
    "read",
    "write",
    "open",
    "close",
    "fork",
    "accept",
    "socket",
    "bind",
    "listen",
    "getpid",
}


def _normalize_body(body):
    """Normalize code body: fix else if -> el if, handle semicolons, etc."""
    b = body.strip()
    # strip outer braces
    if b.startswith("{") and b.endswith("}"):
        b = b[1:-1].strip()
    # else if -> el if (el-stupido syntax)
    b = b.replace("} else if ", "}\n  el if ")
    b = b.replace("else if ", "el if ")
    b = b.replace("} else {", "}\n  el {")
    b = b.replace("else {", "el {")
    # handle Python-ish: elif -> el if
    b = b.replace("elif ", "el if ")
    return b


def json_to_es_math(data):
    """Convert SCHEMA_MATH JSON to el-stupido source."""
    lines = []
    has_main_fn = False

    for fn in data.get("functions", []):
        name = fn["name"]
        params = fn.get("params", "").strip()
        body = fn["body"]

        # skip builtins the model tried to redefine
        if name in _BUILTINS:
            continue
        # skip functions with English prose bodies
        if _is_prose(body):
            continue

        # track if model defined main() as a function
        if name == "main":
            has_main_fn = True

        # one-liner if body is a single expression (no {, no ;, no newline, no control flow)
        b = body.strip()
        is_oneliner = (
            "{" not in b
            and "\n" not in b
            and ";" not in b
            and "for " not in b
            and "while " not in b
            and "if " not in b
            and len(b) < 120
            and name != "main"
        )

        if is_oneliner:
            if params:
                lines.append(f"{name}({params}) = {b}")
            else:
                lines.append(f"{name}() = {b}")
        else:
            b = _normalize_body(b)
            if params:
                lines.append(f"fn {name}({params}) {{")
            else:
                lines.append(f"fn {name}() {{")
            # split on ; or newlines
            for stmt in b.replace(";", "\n").split("\n"):
                s = stmt.strip()
                if s:
                    lines.append(f"  {s}")
            lines.append("}")
    if lines:
        lines.append("")

    # only generate fn main() from main_body if model didn't already define main
    main_body = data.get("main_body", "").strip()
    if main_body and not has_main_fn:
        # skip if main_body just calls a function that IS main
        if main_body in ("main()", "main_body()", ""):
            # model defined logic in a named function and calls it — that's fine
            # but we still need a main
            lines.append(f"fn main() {{ {main_body} }}")
        else:
            mb = _normalize_body(main_body)
            lines.append("fn main() {")
            for stmt in mb.replace(";", "\n").split("\n"):
                s = stmt.strip()
                if s:
                    lines.append(f"  {s}")
            lines.append("}")

    return "\n".join(lines) + "\n"


def json_to_es_http(data):
    """Convert SCHEMA_HTTP JSON to el-stupido codebook source."""
    lines = ["use web"]
    lines.append(f"listen {data.get('port', 8080)}")

    for route in data.get("routes", []):
        path = route["path"]
        rtype = route.get("type", "static")
        content = route.get("content", "")
        method = route.get("method", "GET")

        prefix = ""
        if method == "POST":
            prefix = "POST "

        if rtype == "static":
            lines.append(f'{prefix}{path} "{content}"')
        elif rtype == "handler":
            lines.append(f"{prefix}{path} fn {content}")

    for handler in data.get("handlers", []):
        name = handler["name"]
        code = handler.get("response_code", 200)
        ctype = handler.get("content_type", "text/plain")
        body = handler.get("body_text", "OK")
        lines.append(
            f'fn {name}(fd: i32, body: *u8) {{ http_reply(fd, {code}, "{ctype}", "{body}") }}'
        )

    return "\n".join(lines) + "\n"


def json_to_es(task_name, data):
    if task_name == "http":
        return json_to_es_http(data)
    else:
        return json_to_es_math(data)


# ─── Query ollama with schema constraint ─────────────────────────────


def query_constrained(model, prompt, schema, timeout=120):
    """Query ollama with JSON schema constraint."""
    payload = {
        "model": model,
        "prompt": prompt,
        "format": schema,
        "stream": False,
        "options": {"temperature": 0.3, "num_predict": 1024},
    }
    try:
        r = requests.post(f"{OLLAMA_URL}/api/generate", json=payload, timeout=timeout)
        data = r.json()
        return data.get("response", ""), data.get("eval_count", 0)
    except Exception as e:
        return "", 0


def query_free(model, prompt, timeout=120):
    """Query ollama without constraints (baseline)."""
    # add /no_think for qwen3
    if model.startswith("qwen3"):
        prompt += " /no_think"
    payload = {
        "model": model,
        "prompt": prompt,
        "stream": False,
        "options": {"temperature": 0.3, "num_predict": 1024},
    }
    try:
        r = requests.post(f"{OLLAMA_URL}/api/generate", json=payload, timeout=timeout)
        data = r.json()
        raw = data.get("response", "")
        # strip markdown fences and think blocks
        import re

        raw = re.sub(r"<think>.*?</think>", "", raw, flags=re.DOTALL)
        raw = re.sub(r"^```\w*\n?", "", raw, flags=re.MULTILINE)
        raw = re.sub(r"^```\s*$", "", raw, flags=re.MULTILINE)
        return raw.strip(), data.get("eval_count", 0)
    except Exception as e:
        return "", 0


# ─── Compile and run ─────────────────────────────────────────────────


def compile_and_run(source, task_name):
    """Compile el-stupido source and run it. Returns (success, output, error)."""
    with tempfile.NamedTemporaryFile(suffix=".es", mode="w", delete=False) as f:
        f.write(source)
        src_path = f.name
    bin_path = src_path.replace(".es", "")

    try:
        result = subprocess.run(
            ["./esc", src_path, "-o", bin_path],
            capture_output=True,
            text=True,
            timeout=30,
        )
        if result.returncode != 0:
            err = (
                result.stderr.strip().split("\n")[0]
                if result.stderr
                else "unknown error"
            )
            return "COMPILE_FAIL", "", err

        result = subprocess.run([bin_path], capture_output=True, text=True, timeout=10)
        output = result.stdout.strip()
        return "OK", output, ""
    except subprocess.TimeoutExpired:
        return "TIMEOUT", "", "execution timeout"
    except Exception as e:
        return "ERROR", "", str(e)
    finally:
        for p in [src_path, bin_path]:
            try:
                os.unlink(p)
            except:
                pass


def validate_output(output, task):
    """Check if output is correct using Python-native validators."""
    task_name = task.get("_name", "")
    lines = output.strip().split("\n") if output.strip() else []

    if task_name == "factorial":
        if len(lines) != 12:
            return False
        f = 1
        for i in range(1, 13):
            f *= i
            if lines[i - 1].strip() != str(f):
                return False
        return True

    if task_name == "fizzbuzz":
        if len(lines) != 30:
            return False
        for i in range(1, 31):
            exp = (
                "FizzBuzz"
                if i % 15 == 0
                else "Fizz"
                if i % 3 == 0
                else "Buzz"
                if i % 5 == 0
                else str(i)
            )
            if lines[i - 1].strip() != exp:
                return False
        return True

    if task_name == "sumsq":
        return output.strip() == "338350"

    if task_name == "fibonacci":
        return "6765" in output

    if task_name == "primes":
        import re

        nums = set(int(x) for x in re.findall(r"\d+", output))
        expected = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47}
        return expected.issubset(nums)

    # fallback
    if task.get("expected_last"):
        return task["expected_last"] in output
    return len(output) > 0


# ─── Main ────────────────────────────────────────────────────────────


def main():
    parser = argparse.ArgumentParser(
        description="Constrained vs free generation benchmark"
    )
    parser.add_argument("--task", default="factorial", choices=list(TASKS.keys()))
    parser.add_argument("--model", default="qwen2.5-coder:3b")
    parser.add_argument("--all-tasks", action="store_true", help="Run all tasks")
    parser.add_argument(
        "--all-models", action="store_true", help="Run all available models"
    )
    parser.add_argument("--save-dir", default="tests/llm_constrained")
    parser.add_argument(
        "--free-spec", default=None, help="Custom spec for free-form generation"
    )
    args = parser.parse_args()

    os.makedirs(args.save_dir, exist_ok=True)

    # discover models
    if args.all_models:
        try:
            r = requests.get(f"{OLLAMA_URL}/api/tags")
            models = [
                m["name"]
                for m in r.json().get("models", [])
                if "embed" not in m["name"]
            ]
        except:
            models = [args.model]
    else:
        models = [args.model]

    tasks = list(TASKS.keys()) if args.all_tasks else [args.task]
    # skip http in all-tasks mode (needs server testing)
    if args.all_tasks:
        tasks = [t for t in tasks if t != "http"]

    # free-form specs (from existing benchmark)
    FREE_SPECS = {
        "factorial": (
            "el-stupido: compiled declarative lang.\n"
            "name(args) = expr (one-liner fn, types inferred as i32)\n"
            "Built-ins: product(1..=n) sum(1..n) count(a..b)\n"
            "print(expr) auto-formats. for i := 1..=n { }. if/else/while/return.\n"
            "fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)\n"
            "fact(n) = product(1..=n)\n"
            "fn main() { for i := 1..=10 { print(fib(i)) } }\n\n"
            "Write: factorial using product(), print factorials 1 through 12.\n"
            "Output ONLY code, no explanation, no markdown."
        ),
        "fizzbuzz": TASKS["fizzbuzz"]["prompt"]
        + ("\n\nOutput ONLY el-stupido code. No JSON. No explanation. No markdown."),
        "sumsq": TASKS["sumsq"]["prompt"]
        + ("\n\nOutput ONLY el-stupido code. No JSON. No explanation. No markdown."),
        "fibonacci": TASKS["fibonacci"]["prompt"]
        + ("\n\nOutput ONLY el-stupido code. No JSON. No explanation. No markdown."),
        "primes": TASKS["primes"]["prompt"]
        + ("\n\nOutput ONLY el-stupido code. No JSON. No explanation. No markdown."),
    }

    C = "\033[0;36m"  # cyan
    G = "\033[0;32m"  # green
    R = "\033[0;31m"  # red
    Y = "\033[0;33m"  # yellow
    Z = "\033[0m"  # reset

    print(f"{C}━━━ Constrained vs Free Generation ━━━{Z}")
    print(f"Models: {len(models)}  Tasks: {len(tasks)}")
    print()

    results = []

    for model in sorted(models):
        print(f"{C}━━━ {model} ━━━{Z}")
        slug = model.replace(":", "__").replace("/", "__")

        for task_name in tasks:
            task = {**TASKS[task_name], "_name": task_name}
            print(f"  {task_name:12s} ", end="", flush=True)

            # --- constrained (JSON schema) ---
            raw_json, c_tokens = query_constrained(
                model, task["prompt"], task["schema"]
            )
            c_result = "FAIL"
            c_source = ""
            c_output = ""
            if raw_json:
                try:
                    data = json.loads(raw_json)
                    c_source = json_to_es(task_name, data)
                    # save source
                    src_path = os.path.join(
                        args.save_dir, f"{slug}_{task_name}_constrained.es"
                    )
                    with open(src_path, "w") as f:
                        f.write(c_source)
                    # save json
                    json_path = os.path.join(
                        args.save_dir, f"{slug}_{task_name}_constrained.json"
                    )
                    with open(json_path, "w") as f:
                        json.dump(data, f, indent=2)

                    status, c_output, err = compile_and_run(c_source, task_name)
                    if status == "OK":
                        if validate_output(c_output, task):
                            c_result = "PASS"
                        else:
                            c_result = "WRONG"
                    else:
                        c_result = status
                except json.JSONDecodeError:
                    c_result = "JSON_ERR"
                except Exception as e:
                    c_result = "ERROR"

            # --- free (baseline) ---
            free_spec = FREE_SPECS.get(
                task_name, task["prompt"] + "\nOutput ONLY code."
            )
            raw_free, f_tokens = query_free(model, free_spec)
            f_result = "FAIL"
            f_source = raw_free
            f_output = ""
            if raw_free:
                src_path = os.path.join(args.save_dir, f"{slug}_{task_name}_free.es")
                with open(src_path, "w") as f:
                    f.write(raw_free)

                status, f_output, err = compile_and_run(raw_free, task_name)
                if status == "OK":
                    if validate_output(f_output, task):
                        f_result = "PASS"
                    else:
                        f_result = "WRONG"
                else:
                    f_result = status

            # color results
            cc = G if c_result == "PASS" else (Y if c_result == "WRONG" else R)
            fc = G if f_result == "PASS" else (Y if f_result == "WRONG" else R)

            print(
                f"SCHEMA: {cc}{c_result:12s}{Z} ({c_tokens:3d}t)  "
                f"FREE: {fc}{f_result:12s}{Z} ({f_tokens:3d}t)"
            )

            results.append(
                {
                    "model": model,
                    "task": task_name,
                    "constrained": c_result,
                    "c_tokens": c_tokens,
                    "free": f_result,
                    "f_tokens": f_tokens,
                }
            )

        print()

    # --- summary ---
    c_pass = sum(1 for r in results if r["constrained"] == "PASS")
    f_pass = sum(1 for r in results if r["free"] == "PASS")
    total = len(results)

    print(f"{C}━━━ SUMMARY ━━━{Z}")
    print(f"  Schema-constrained: {G}{c_pass}{Z}/{total} pass")
    print(f"  Free generation:    {G}{f_pass}{Z}/{total} pass")
    if c_pass > f_pass:
        print(f"  {G}Schema wins by {c_pass - f_pass}{Z}")
    elif f_pass > c_pass:
        print(f"  {Y}Free wins by {f_pass - c_pass}{Z}")
    else:
        print(f"  Tied")

    # save CSV
    csv_path = os.path.join(args.save_dir, "results.csv")
    with open(csv_path, "w") as f:
        f.write("model,task,constrained,c_tokens,free,f_tokens\n")
        for r in results:
            f.write(
                f"{r['model']},{r['task']},{r['constrained']},{r['c_tokens']},"
                f"{r['free']},{r['f_tokens']}\n"
            )
    print(f"\n  Results: {csv_path}")
    print(f"  Sources: {args.save_dir}/")


if __name__ == "__main__":
    main()
