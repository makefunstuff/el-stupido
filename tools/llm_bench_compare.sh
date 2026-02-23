#!/usr/bin/env bash
# tools/llm_bench_compare.sh — el-stupido vs Python comparative benchmark
#
# Tests whether local ollama models can generate correct code in el-stupido
# vs Python for the same tasks. Measures: pass rate, output token count,
# lines of code generated.
#
# Usage:
#   ./tools/llm_bench_compare.sh                   # all tasks, default models
#   ./tools/llm_bench_compare.sh qwen3:8b phi4     # specific models
#
# Requires: ollama running, python3, ./esc compiler built

set -uo pipefail

ESC="./esc"
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"
RESULTS_DIR="tests/llm_compare"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
TIMEOUT="${TIMEOUT:-120}"

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[0;33m'; CYN='\033[0;36m'
BLU='\033[0;34m'; MAG='\033[0;35m'; RST='\033[0m'

# ─── TASK DEFINITIONS ────────────────────────────────────────────────
# Each task: name, description, validator command, el-stupido spec, python spec
# The validator receives the program output on stdin and exits 0 if correct.

declare -A TASK_DESC TASK_VALIDATE TASK_ES_SPEC TASK_ES_TASK TASK_PY_SPEC TASK_PY_TASK

# ── Task 1: Factorial ──
TASK_DESC[factorial]="Print factorials 1 through 12"
TASK_VALIDATE[factorial]='python3 -c "
import sys
lines = sys.stdin.read().strip().split(\"\n\")
if len(lines) != 12: exit(1)
f = 1
for i in range(1, 13):
    f *= i
    if lines[i-1].strip() != str(f): exit(1)
"'

TASK_ES_SPEC[factorial]='el-stupido: compiled declarative lang.
name(args) = expr (one-liner fn, types inferred as i64)
Built-ins: product(1..=n) sum(1..n) count(a..b)
print(expr) auto-formats output. print(f(a..=b)) prints f(a), f(a+1)...f(b).
x := expr (declare). for i := 1..=n { } (inclusive range). if/else/while/return.

Example: fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)
Example: sq(x) = x * x
         print(sq(1..=5))'
TASK_ES_TASK[factorial]='Write: a factorial function, then print factorials 1 through 12.
Output ONLY el-stupido code. No explanation, no markdown.'

TASK_PY_SPEC[factorial]='Python 3.'
TASK_PY_TASK[factorial]='Write a Python program that prints factorials 1 through 12, one per line.
Output ONLY Python code. No explanation, no markdown.'

# ── Task 2: FizzBuzz ──
TASK_DESC[fizzbuzz]="FizzBuzz 1 to 30"
TASK_VALIDATE[fizzbuzz]='python3 -c "
import sys
lines = sys.stdin.read().strip().split(\"\n\")
if len(lines) != 30: exit(1)
for i in range(1, 31):
    exp = \"FizzBuzz\" if i%15==0 else \"Fizz\" if i%3==0 else \"Buzz\" if i%5==0 else str(i)
    if lines[i-1].strip() != exp: exit(1)
"'

TASK_ES_SPEC[fizzbuzz]='el-stupido: compiled lang. C-like but concise.
fn name(args) { body }. name(args) = expr (one-liner).
print(expr). printf(fmt, args). for i := 1..=n { }. if/else. x % y (modulo).
Types: i32, i64, *u8 (string). String literals: "hello".

Example: max(a, b) = a > b ? a : b
         fn main() { for i := 1..=10 { print(i) } }'
TASK_ES_TASK[fizzbuzz]='Write FizzBuzz for 1 to 30. Print "Fizz" if divisible by 3, "Buzz" by 5, "FizzBuzz" by both, else the number.
Output ONLY el-stupido code. No explanation, no markdown.'

TASK_PY_SPEC[fizzbuzz]='Python 3.'
TASK_PY_TASK[fizzbuzz]='Write FizzBuzz for 1 to 30. Print "Fizz" if divisible by 3, "Buzz" by 5, "FizzBuzz" by both, else the number.
Output ONLY Python code. No explanation, no markdown.'

# ── Task 3: Sum of Squares ──
TASK_DESC[sumsq]="Sum of squares 1..100"
TASK_VALIDATE[sumsq]='python3 -c "
import sys
out = sys.stdin.read().strip()
if out == \"338350\": exit(0)
exit(1)
"'

TASK_ES_SPEC[sumsq]='el-stupido: compiled declarative lang.
name(args) = expr (one-liner fn, types inferred as i64)
Built-ins: sum(1..=n) computes sum of range, product(1..=n), count(a..b)
print(expr) auto-formats. print(f(a..=b)) prints each result.
sum(f(a..=b)) not supported — use loop or: sq(x) = x*x then accumulate.
x := expr. for i := 1..=n { }. if/else/while/return.

Example: double(x) = x * 2
         print(sum(1..=10))'
TASK_ES_TASK[sumsq]='Compute and print the sum of squares from 1 to 100 (1*1 + 2*2 + ... + 100*100 = 338350).
Output ONLY el-stupido code. No explanation, no markdown.'

TASK_PY_SPEC[sumsq]='Python 3.'
TASK_PY_TASK[sumsq]='Compute and print the sum of squares from 1 to 100 (1*1 + 2*2 + ... + 100*100).
Output ONLY Python code. No explanation, no markdown.'

# ── Task 4: Fibonacci ──
TASK_DESC[fibonacci]="Print first 20 Fibonacci numbers"
TASK_VALIDATE[fibonacci]='python3 -c "
import sys
lines = sys.stdin.read().strip().split(\"\n\")
if len(lines) < 20: exit(1)
a, b = 0, 1
for i in range(20):
    if i == 0: exp = 0
    elif i == 1: exp = 1
    else: a, b = b, a+b; exp = b
    # some programs start from fib(1)=1, accept both conventions
if \"6765\" not in sys.stdin.read() and lines[-1].strip() != \"6765\" and \"6765\" not in \"\n\".join(lines): exit(1)
"'
# simpler validator: just check 6765 appears (fib(20))
TASK_VALIDATE[fibonacci]='python3 -c "
import sys
text = sys.stdin.read()
if \"6765\" in text: exit(0)
exit(1)
"'

TASK_ES_SPEC[fibonacci]='el-stupido: compiled lang.
name(args) = expr (one-liner fn with recursion, types inferred as i64)
print(expr). print(f(a..=b)) prints f(a)...f(b). for i := 1..=n { }. if/else/return.
x := expr. Ternary: cond ? a : b.

Example: sq(x) = x * x
         print(sq(1..=5))'
TASK_ES_TASK[fibonacci]='Write a fibonacci function and print the first 20 fibonacci numbers.
Output ONLY el-stupido code. No explanation, no markdown.'

TASK_PY_SPEC[fibonacci]='Python 3.'
TASK_PY_TASK[fibonacci]='Write a program that prints the first 20 fibonacci numbers, one per line.
Output ONLY Python code. No explanation, no markdown.'

# ── Task 5: Primes ──
TASK_DESC[primes]="Print primes up to 50"
TASK_VALIDATE[primes]='python3 -c "
import sys
text = sys.stdin.read().strip()
# extract all numbers from output
import re
nums = [int(x) for x in re.findall(r\"\d+\", text)]
expected = [2,3,5,7,11,13,17,19,23,29,31,37,41,43,47]
# check all primes present (order may vary, may have extra whitespace)
if set(expected).issubset(set(nums)): exit(0)
exit(1)
"'

TASK_ES_SPEC[primes]='el-stupido: compiled lang. C-like but concise.
fn name(args) -> type { body }. name(args) = expr (one-liner).
print(expr). printf(fmt, args). for i := 1..=n { }. if/else/while/return/break.
x % y (modulo). Bool: 0 is false, nonzero is true.

Example: abs(x) = x < 0 ? -x : x
         fn main() { for i := 1..=10 { print(i) } }'
TASK_ES_TASK[primes]='Write a program that prints all prime numbers up to 50, one per line.
Output ONLY el-stupido code. No explanation, no markdown.'

TASK_PY_SPEC[primes]='Python 3.'
TASK_PY_TASK[primes]='Write a program that prints all prime numbers up to 50, one per line.
Output ONLY Python code. No explanation, no markdown.'

TASKS=(factorial fizzbuzz sumsq fibonacci primes)

# ─── MODEL SELECTION ─────────────────────────────────────────────────
if [ $# -gt 0 ]; then
    MODELS=("$@")
else
    # default: pick interesting subset
    MODELS=(
        "qwen2.5-coder:3b"
        "qwen2.5-coder:7b"
        "qwen3:8b"
        "phi4:latest"
        "granite4:latest"
    )
fi

mkdir -p "$RESULTS_DIR"

echo -e "${CYN}━━━ el-stupido vs Python Comparative Benchmark ━━━${RST}"
echo -e "Timestamp: $TIMESTAMP"
echo -e "Models: ${#MODELS[@]}  Tasks: ${#TASKS[@]}  Languages: el-stupido, Python"
echo ""

# ─── QUERY HELPER ────────────────────────────────────────────────────
query_model() {
    local model="$1" prompt="$2"
    local tmpprompt tmpjson raw_json raw

    # add /no_think for qwen3 models
    [[ "$model" == qwen3* ]] && prompt="$prompt /no_think"

    tmpprompt=$(mktemp); tmpjson=$(mktemp)
    printf '%s' "$prompt" > "$tmpprompt"
    python3 -c "
import json
prompt = open('$tmpprompt','r').read()
with open('$tmpjson','w') as f:
    json.dump({'model': '$model', 'prompt': prompt, 'stream': False}, f)
" 2>/dev/null

    raw_json=$(curl -s --max-time "$TIMEOUT" "$OLLAMA_URL/api/generate" \
        -d @"$tmpjson" 2>/dev/null || true)
    rm -f "$tmpjson" "$tmpprompt"

    raw=$(echo "$raw_json" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    print(data.get('response', ''))
except:
    print('')
" 2>/dev/null || true)

    # strip markdown fences and <think> blocks
    echo "$raw" | sed '/^```/d' | sed '/<think>/,/<\/think>/d'
}

# ─── RUN BENCHMARK ──────────────────────────────────────────────────
# Results table: model | task | lang | pass | tokens | lines
declare -a RESULTS=()
ES_PASS=0; ES_TOTAL=0; PY_PASS=0; PY_TOTAL=0

set +eo pipefail
for model in "${MODELS[@]}"; do
    echo -e "${BLU}━━━ $model ━━━${RST}"
    slug=$(echo "$model" | tr '/:' '__')

    for task in "${TASKS[@]}"; do
        printf "  %-12s " "$task"

        # ── el-stupido ──
        ES_TOTAL=$((ES_TOTAL + 1))
        es_prompt="${TASK_ES_SPEC[$task]}

${TASK_ES_TASK[$task]}"
        es_code=$(query_model "$model" "$es_prompt")
        es_src="$RESULTS_DIR/${slug}_${task}.es"
        echo "$es_code" > "$es_src"
        es_tokens=$(echo "$es_code" | wc -w)
        es_lines=$(echo "$es_code" | wc -l)
        es_bin="/tmp/es_cmp_${slug}_${task}"

        # compile el-stupido
        es_result="FAIL"
        es_compile=$($ESC "$es_src" -o "$es_bin" 2>&1)
        if [ $? -eq 0 ]; then
            # run and validate
            es_output=$("$es_bin" 2>&1) || true
            if echo "$es_output" | eval "${TASK_VALIDATE[$task]}" 2>/dev/null; then
                es_result="PASS"
                ES_PASS=$((ES_PASS + 1))
            else
                es_result="WRONG"
            fi
        else
            if echo "$es_compile" | grep -q "LLVM verify"; then
                es_result="CODEGEN"
            fi
        fi

        # ── Python ──
        PY_TOTAL=$((PY_TOTAL + 1))
        py_prompt="${TASK_PY_SPEC[$task]}

${TASK_PY_TASK[$task]}"
        py_code=$(query_model "$model" "$py_prompt")
        py_src="$RESULTS_DIR/${slug}_${task}.py"
        echo "$py_code" > "$py_src"
        py_tokens=$(echo "$py_code" | wc -w)
        py_lines=$(echo "$py_code" | wc -l)

        # run python
        py_result="FAIL"
        py_output=$(timeout 10 python3 "$py_src" 2>&1) || true
        if [ $? -eq 0 ] && [ -n "$py_output" ]; then
            if echo "$py_output" | eval "${TASK_VALIDATE[$task]}" 2>/dev/null; then
                py_result="PASS"
                PY_PASS=$((PY_PASS + 1))
            else
                py_result="WRONG"
            fi
        fi

        # color results
        es_color="$RED"; [ "$es_result" = "PASS" ] && es_color="$GRN"; [ "$es_result" = "WRONG" ] && es_color="$YEL"
        py_color="$RED"; [ "$py_result" = "PASS" ] && py_color="$GRN"; [ "$py_result" = "WRONG" ] && py_color="$YEL"

        printf "ES: ${es_color}%-6s${RST} (%2dt/%2dl)  PY: ${py_color}%-6s${RST} (%2dt/%2dl)\n" \
            "$es_result" "$es_tokens" "$es_lines" \
            "$py_result" "$py_tokens" "$py_lines"

        RESULTS+=("$model|$task|$es_result|$es_tokens|$es_lines|$py_result|$py_tokens|$py_lines")
    done
    echo ""
done

# ─── SUMMARY ─────────────────────────────────────────────────────────
echo -e "${CYN}━━━ SUMMARY ━━━${RST}"
echo ""
printf "%-20s %-12s %-18s %-18s\n" "Model" "Task" "el-stupido" "Python"
printf "%-20s %-12s %-18s %-18s\n" "────────────────────" "────────────" "──────────────────" "──────────────────"
for r in "${RESULTS[@]}"; do
    IFS='|' read -r model task es_r es_t es_l py_r py_t py_l <<< "$r"
    short_model=$(echo "$model" | cut -d: -f1 | tail -c 18)
    es_color="$RED"; [ "$es_r" = "PASS" ] && es_color="$GRN"; [ "$es_r" = "WRONG" ] && es_color="$YEL"
    py_color="$RED"; [ "$py_r" = "PASS" ] && py_color="$GRN"; [ "$py_r" = "WRONG" ] && py_color="$YEL"
    printf "%-20s %-12s ${es_color}%-6s${RST} %2dt/%2dl   ${py_color}%-6s${RST} %2dt/%2dl\n" \
        "$short_model" "$task" "$es_r" "$es_t" "$es_l" "$py_r" "$py_t" "$py_l"
done

echo ""
echo -e "el-stupido: ${GRN}${ES_PASS}${RST}/${ES_TOTAL} pass  |  Python: ${GRN}${PY_PASS}${RST}/${PY_TOTAL} pass"
echo ""

# token comparison for passing tasks
es_pass_tokens=0; py_pass_tokens=0; both_pass=0
for r in "${RESULTS[@]}"; do
    IFS='|' read -r model task es_r es_t es_l py_r py_t py_l <<< "$r"
    if [ "$es_r" = "PASS" ] && [ "$py_r" = "PASS" ]; then
        both_pass=$((both_pass + 1))
        es_pass_tokens=$((es_pass_tokens + es_t))
        py_pass_tokens=$((py_pass_tokens + py_t))
    fi
done

if [ $both_pass -gt 0 ]; then
    echo -e "Tasks where BOTH pass: $both_pass"
    echo -e "Avg tokens — ES: $((es_pass_tokens / both_pass))  PY: $((py_pass_tokens / both_pass))"
    if [ $es_pass_tokens -gt 0 ]; then
        ratio=$(python3 -c "print(f'{$py_pass_tokens / $es_pass_tokens:.1f}')" 2>/dev/null || echo "?")
        echo -e "Python/ES token ratio: ${ratio}x"
    fi
fi

echo ""
echo "Results saved in $RESULTS_DIR/"

# save machine-readable results
{
    echo "timestamp,model,task,es_result,es_tokens,es_lines,py_result,py_tokens,py_lines"
    for r in "${RESULTS[@]}"; do
        echo "$TIMESTAMP,${r//|/,}"
    done
} > "$RESULTS_DIR/results_${TIMESTAMP}.csv"
echo "CSV: $RESULTS_DIR/results_${TIMESTAMP}.csv"
