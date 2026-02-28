#!/usr/bin/env bash
# Round 8 benchmark: local models generating compose manifests
# Tests esc compose pipeline end-to-end via Ollama structured output
set -euo pipefail

OLLAMA="http://192.168.1.138:11434"
ESC="./compiler/target/debug/esc"
BENCH_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="$BENCH_DIR/results"
mkdir -p "$RESULTS_DIR"

# ── Models to test (ascending size) ──────────────────────────
MODELS=(
  "qwen2.5-coder:1.5b"
  "qwen2.5-coder:3b"
  "qwen3:4b"
  "qwen2.5-coder:7b"
  "qwen3:8b"
  "qwen2.5:14b-instruct"
  "qwen2.5-coder:14b"
  "qwen3:30b-a3b"
)

# ── System prompt (shared across all tasks) ──────────────────
read -r -d '' SYSTEM_PROMPT << 'SYSPROMPT' || true
You are a code generator for the esc compose system. You output JSON manifests.

RULES:
- Output a JSON object with keys: "app" (string), "capabilities" (array of strings), "nodes" (array of node objects).
- Each node has: "id" (unique string), "use" (primitive name), optionally "params" (object) and "bind" (object).
- "bind" values are string IDs referencing EARLIER nodes. A node can only bind to nodes defined before it.
- "params" are literal values (numbers or strings).
- Types: each primitive provides one of: num, str, bool, sink. Binds must match the type the target node provides.
- Capabilities must list all effects used: io_read, io_write, fs_read, fs_write, net_read, env_read.

AVAILABLE PRIMITIVES:
- const_num: params {value: number}. provides: num.
- const_str: params {value: string}. provides: str.
- add/sub/mul/div/mod_num: bind {lhs: num, rhs: num}. provides: num.
- gt/lt/eq_num: bind {lhs: num, rhs: num}. provides: bool.
- and_bool/or_bool: bind {lhs: bool, rhs: bool}. provides: bool.
- not_bool: bind {value: bool}. provides: bool.
- select_num: bind {cond: bool, then: num, else: num}. provides: num.
- select_str: bind {cond: bool, then: str, else: str}. provides: str.
- to_string: bind {value: num}. provides: str.
- concat: bind {left: str, right: str}. provides: str.
- len_str: bind {text: str}. provides: num.
- upper_str/lower_str/trim_str: bind {text: str}. provides: str.
- parse_num: bind {text: str}. provides: num.
- format_str: params {template: string}, bind {v1: str, v2?: str}. provides: str. Template uses {1} and {2}.
- print_num: bind {value: num}. provides: sink. effects: io_write.
- print_str: bind {value: str}. provides: sink. effects: io_write.
- arg_num: params {index: number}. provides: num. (CLI arg as number, 1-indexed)
- arg_str: params {index: number}. provides: str. (CLI arg as string, 1-indexed)
- arg_count: provides: num.
- floor/abs: bind {value: num}. provides: num.
- substr: bind {text: str, start: num, len: num}. provides: str.
- contains_str: bind {text: str, needle: str}. provides: bool.
- replace_str: bind {text: str, pattern: str, replacement: str}. provides: str.
- split_count: bind {text: str, delim: str}. provides: num.
- split_nth: bind {text: str, delim: str, index: num}. provides: str.
- repeat_str: bind {text: str, times: num}. provides: str.
- read_stdin: params {prompt?: string}. provides: str. effects: io_read, io_write.
- exit_code: bind {code: num}. provides: sink.

EXAMPLE — add 13 + 29 and print:
{"app":"sum","capabilities":["io_write"],"nodes":[{"id":"a","use":"const_num","params":{"value":13}},{"id":"b","use":"const_num","params":{"value":29}},{"id":"s","use":"add","bind":{"lhs":"a","rhs":"b"}},{"id":"out","use":"print_num","bind":{"value":"s"}}]}
SYSPROMPT

# ── JSON Schema for Ollama structured output ─────────────────
read -r -d '' JSON_SCHEMA << 'SCHEMA' || true
{
  "type": "object",
  "required": ["app", "capabilities", "nodes"],
  "properties": {
    "app": {"type": "string"},
    "capabilities": {
      "type": "array",
      "items": {"type": "string", "enum": ["io_read", "io_write", "fs_read", "fs_write", "net_read", "env_read"]}
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
          "bind": {"type": "object"}
        },
        "additionalProperties": false
      }
    }
  },
  "additionalProperties": false
}
SCHEMA

# ── Task definitions ─────────────────────────────────────────
# Each task: name|prompt|cli_args|expected_output
TASKS=(
  "sum_const|Generate a manifest that computes 17 + 25 and prints the result.||42"
  "mul_const|Generate a manifest that computes 6 * 7 and prints the result.||42"
  "temp_conv|Generate a manifest that reads CLI arg 1 as a number, converts Fahrenheit to Celsius using (arg - 32) * 5 / 9, and prints the result.|212|100"
  "greeting|Generate a manifest that reads CLI arg 1 as a string, concatenates \"Hello, \" with it, and prints the result string.|World|Hello, World"
  "str_upper|Generate a manifest that reads CLI arg 1 as a string, converts it to uppercase, and prints it.|hello|HELLO"
)

# ── Helpers ──────────────────────────────────────────────────

safe_name() {
  echo "$1" | tr ':.' '_' | tr '/' '_'
}

run_one() {
  local model="$1" task_line="$2" mode="$3"
  IFS='|' read -r task_name task_prompt cli_args expected <<< "$task_line"

  local sname
  sname="$(safe_name "$model")"
  local out_prefix="$RESULTS_DIR/${sname}__${task_name}__${mode}"

  # Build request payload
  local format_field
  if [ "$mode" = "schema" ]; then
    format_field="$JSON_SCHEMA"
  elif [ "$mode" = "json" ]; then
    format_field='"json"'
  else
    format_field='null'
  fi

  local payload
  payload=$(python3 -c "
import json, sys
payload = {
    'model': '$model',
    'system': sys.stdin.read(),
    'prompt': $(python3 -c "import json; print(json.dumps('$task_prompt'))"),
    'stream': False,
    'format': $format_field,
    'options': {
        'num_predict': 512,
        'temperature': 0.1
    }
}
print(json.dumps(payload))
" <<< "$SYSTEM_PROMPT")

  # Call Ollama
  local start_ms
  start_ms=$(date +%s%3N)

  local response
  response=$(curl -s --max-time 120 "$OLLAMA/api/generate" \
    -H "Content-Type: application/json" \
    -d "$payload" 2>&1) || { echo "CURL_FAIL"; return; }

  local end_ms
  end_ms=$(date +%s%3N)
  local elapsed=$(( end_ms - start_ms ))

  # Extract response text and token counts
  local generated
  generated=$(echo "$response" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('response',''))" 2>/dev/null) || { echo "PARSE_FAIL"; return; }

  local prompt_tokens eval_tokens
  prompt_tokens=$(echo "$response" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('prompt_eval_count',0))" 2>/dev/null || echo 0)
  eval_tokens=$(echo "$response" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('eval_count',0))" 2>/dev/null || echo 0)

  echo "$generated" > "${out_prefix}.json"

  # Try to compile
  local compile_result binary_path="/tmp/esc_bench_${sname}_${task_name}_${mode}"
  compile_result=$($ESC compose "${out_prefix}.json" -o "$binary_path" 2>&1) || true
  echo "$compile_result" > "${out_prefix}.compile"

  if [ ! -f "$binary_path" ]; then
    printf "%-30s %-12s %-6s | CF  | %4d tok | %5d ms | %s\n" \
      "$model" "$task_name" "$mode" "$eval_tokens" "$elapsed" "$(head -1 "${out_prefix}.compile")"
    echo "${model}|${task_name}|${mode}|CF|${eval_tokens}|${elapsed}|$(head -1 "${out_prefix}.compile")" >> "$RESULTS_DIR/summary.csv"
    return
  fi

  # Try to run
  local actual
  if [ -n "$cli_args" ]; then
    actual=$("$binary_path" $cli_args 2>&1) || true
  else
    actual=$("$binary_path" 2>&1) || true
  fi
  echo "$actual" > "${out_prefix}.output"
  rm -f "$binary_path"

  # Compare output
  # Trim whitespace and handle float formatting
  local actual_trimmed expected_trimmed
  actual_trimmed=$(echo "$actual" | head -1 | sed 's/[[:space:]]*$//')
  expected_trimmed=$(echo "$expected" | sed 's/[[:space:]]*$//')

  local status="W"
  if [ "$actual_trimmed" = "$expected_trimmed" ]; then
    status="P"
  else
    # Try numeric comparison (100.0 == 100, 42.0 == 42)
    if python3 -c "
import sys
try:
    a = float('$actual_trimmed')
    e = float('$expected_trimmed')
    sys.exit(0 if abs(a - e) < 0.01 else 1)
except:
    sys.exit(1)
" 2>/dev/null; then
      status="P"
    fi
  fi

  printf "%-30s %-12s %-6s | %-3s | %4d tok | %5d ms | got: %s\n" \
    "$model" "$task_name" "$mode" "$status" "$eval_tokens" "$elapsed" "$actual_trimmed"
  echo "${model}|${task_name}|${mode}|${status}|${eval_tokens}|${elapsed}|${actual_trimmed}" >> "$RESULTS_DIR/summary.csv"
}

# ── Main ─────────────────────────────────────────────────────
echo "═══════════════════════════════════════════════════════════════════════════════"
echo " Round 8: Compose Manifest Generation Benchmark"
echo " Ollama: $OLLAMA (v$(curl -s "$OLLAMA/api/version" | python3 -c 'import sys,json;print(json.load(sys.stdin)["version"])'))"
echo " Models: ${#MODELS[@]}   Tasks: ${#TASKS[@]}   Modes: schema, json, free"
echo "═══════════════════════════════════════════════════════════════════════════════"
echo ""
echo "model|task|mode|result|tokens|ms|detail" > "$RESULTS_DIR/summary.csv"

for model in "${MODELS[@]}"; do
  echo "── $model ──"
  # Warm model first
  curl -s "$OLLAMA/api/generate" -d "{\"model\":\"$model\",\"prompt\":\"hi\",\"stream\":false,\"options\":{\"num_predict\":1}}" > /dev/null 2>&1 || true

  for task_line in "${TASKS[@]}"; do
    for mode in schema json free; do
      run_one "$model" "$task_line" "$mode"
    done
  done
  echo ""
done

echo "═══════════════════════════════════════════════════════════════════════════════"
echo " Summary"
echo "═══════════════════════════════════════════════════════════════════════════════"

# Tally results
python3 << 'PYEOF'
import csv
from collections import defaultdict

results = defaultdict(lambda: defaultdict(lambda: {"P": 0, "CF": 0, "W": 0, "total": 0}))

with open("bench/results/summary.csv") as f:
    reader = csv.reader(f, delimiter='|')
    next(reader)  # header
    for row in reader:
        if len(row) < 4:
            continue
        model, task, mode, status = row[0], row[1], row[2], row[3]
        results[model][mode]["total"] += 1
        if status in ("P", "CF", "W"):
            results[model][mode][status] += 1

print(f"\n{'Model':<30} {'Schema':>10} {'JSON':>10} {'Free':>10}")
print("─" * 65)
for model in sorted(results.keys()):
    parts = []
    for mode in ("schema", "json", "free"):
        d = results[model][mode]
        parts.append(f"{d['P']}/{d['total']}")
    print(f"{model:<30} {parts[0]:>10} {parts[1]:>10} {parts[2]:>10}")

# Total tokens
total_by_mode = defaultdict(int)
count_by_mode = defaultdict(int)
with open("bench/results/summary.csv") as f:
    reader = csv.reader(f, delimiter='|')
    next(reader)
    for row in reader:
        if len(row) >= 5:
            mode, tokens = row[2], int(row[4])
            total_by_mode[mode] += tokens
            count_by_mode[mode] += 1

print(f"\nAvg tokens — schema: {total_by_mode['schema']/max(count_by_mode['schema'],1):.0f}, json: {total_by_mode['json']/max(count_by_mode['json'],1):.0f}, free: {total_by_mode['free']/max(count_by_mode['free'],1):.0f}")
PYEOF
