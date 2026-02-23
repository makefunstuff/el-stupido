#!/usr/bin/env bash
# tools/llm_test.sh — LLM smoke test for el-stupido
#
# Tests whether local ollama models can generate compilable el-stupido code
# from a minimal syntax spec (~200 tokens) + one fibonacci example.
#
# Usage:
#   ./tools/llm_test.sh                    # test all available models
#   ./tools/llm_test.sh qwen3:8b phi4      # test specific models
#   PROMPT=custom ./tools/llm_test.sh ...   # override the task prompt
#
# Requires: ollama running locally, jq or python3, ./esc compiler built

set -euo pipefail

ESC="./esc"
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"
RESULTS_DIR="tests/llm_results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# colors
RED='\033[0;31m'
GRN='\033[0;32m'
YEL='\033[0;33m'
CYN='\033[0;36m'
RST='\033[0m'

# the syntax spec — declarative, minimal
SPEC='el-stupido: compiled declarative lang.
name(args) = expr (one-liner fn, types inferred as i32)
name(args) { body } (block fn)
Built-ins: product(1..=n) sum(1..n) count(a..b) min(a..b) max(a..b)
print(expr) (auto-formatted output)
x := expr (declare). for i := 1..=n { } (inclusive range). if/else/while/return.

fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)
fact(n) = product(1..=n)
fn main() { for i := 1..=10 { print(fib(i)) } }'

# the task (can be overridden with PROMPT env var)
TASK="${PROMPT:-Write: factorial using product(), print factorials 1 through 12.
Output ONLY code, no explanation, no markdown.}"

# expected output for factorial (used to check correctness)
EXPECTED_LINES=12
EXPECTED_LAST="479001600"

mkdir -p "$RESULTS_DIR"

# discover models
if [ $# -gt 0 ]; then
    MODELS=("$@")
else
    # all available models (skip embedding models)
    mapfile -t MODELS < <(
        curl -s "$OLLAMA_URL/api/tags" | \
        python3 -c "
import sys, json
tags = json.load(sys.stdin)
for m in tags.get('models', []):
    name = m['name']
    # skip embedding models
    if 'embed' in name: continue
    print(name)
" 2>/dev/null | sort
    )
fi

if [ ${#MODELS[@]} -eq 0 ]; then
    echo "No models found. Is ollama running?"
    exit 1
fi

echo -e "${CYN}el-stupido LLM smoke test${RST} — $TIMESTAMP"
echo -e "Models: ${#MODELS[@]}  Compiler: $ESC"
echo "Task: factorial 1..12"
echo "---"

# result accumulators
PASS=0
WRONG=0
PARSE_FAIL=0
COMPILE_FAIL=0
TOTAL=0

set +eo pipefail
for model in "${MODELS[@]}"; do
    TOTAL=$((TOTAL + 1))
    slug=$(echo "$model" | tr '/:' '__')
    src="$RESULTS_DIR/${slug}.es"
    bin="/tmp/es_llm_${slug}"

    printf "%-40s " "$model"

    # query ollama
    FULL_PROMPT="$SPEC

$TASK"
    # add /no_think for qwen3 models to suppress thinking
    if [[ "$model" == qwen3* ]]; then
        FULL_PROMPT="$FULL_PROMPT /no_think"
    fi

    # build JSON payload via temp files (avoids all shell escaping issues)
    tmpprompt=$(mktemp)
    tmpjson=$(mktemp)
    printf '%s' "$FULL_PROMPT" > "$tmpprompt"
    python3 -c "
import json
prompt = open('$tmpprompt','r').read()
with open('$tmpjson','w') as f:
    json.dump({'model': '$model', 'prompt': prompt, 'stream': False}, f)
" 2>/dev/null

    raw_json=$(curl -s --max-time "${TIMEOUT:-300}" "$OLLAMA_URL/api/generate" \
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

    if [ -z "$raw" ]; then
        echo -e "${RED}SKIP${RST} (no response)"
        continue
    fi

    # strip markdown fences and <think> blocks if present
    code=$(echo "$raw" | sed '/^```/d' | sed '/<think>/,/<\/think>/d' | sed '/^;/d')
    echo "$code" > "$src"

    # try to compile
    compile_out=$($ESC "$src" -o "$bin" 2>&1)
    if [ $? -ne 0 ]; then
        # extract first error line
        err=$(echo "$compile_out" | head -1)
        if echo "$err" | grep -q "LLVM verify"; then
            echo -e "${YEL}CODEGEN${RST} := shadow bug (LLVM domination)"
            COMPILE_FAIL=$((COMPILE_FAIL + 1))
        else
            short=$(echo "$err" | sed "s|$src:||")
            echo -e "${RED}FAIL${RST}    $short"
            PARSE_FAIL=$((PARSE_FAIL + 1))
        fi
        continue
    fi

    # try to run
    run_out=$("$bin" 2>&1) || true
    lines=$(echo "$run_out" | wc -l)

    # check correctness: last line should contain 479001600
    if echo "$run_out" | grep -q "$EXPECTED_LAST"; then
        echo -e "${GRN}PASS${RST}    (compiles + correct output)"
        PASS=$((PASS + 1))
    else
        # compiled but wrong output
        last=$(echo "$run_out" | tail -1)
        echo -e "${YEL}WRONG${RST}   compiles, bad output: \"$last\""
        WRONG=$((WRONG + 1))
    fi
done

echo "---"
echo -e "Results: ${GRN}${PASS} pass${RST}, ${YEL}${WRONG} wrong output${RST}, ${YEL}${COMPILE_FAIL} codegen fail${RST}, ${RED}${PARSE_FAIL} parse fail${RST} / ${TOTAL} total"
echo "Source files saved in $RESULTS_DIR/"
