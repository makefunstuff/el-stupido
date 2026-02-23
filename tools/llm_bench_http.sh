#!/usr/bin/env bash
# tools/llm_bench_http.sh — HTTP server LLM benchmark
# Tests whether models can generate compilable web servers using codebook
set -uo pipefail

ESC="./esc"
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"
RESULTS_DIR="tests/llm_results_http"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[0;33m'; CYN='\033[0;36m'; RST='\033[0m'

SPEC='el-stupido web codebook. "use web" activates HTTP server.
listen PORT. /path "text". /path fn handler_name.
Handler: fn name(fd: i32, body: *u8) { http_reply(fd, 200, "text/plain", "content") }

use web
listen 8080
/ "Hello"
/about fn about_page
fn about_page(fd: i32, body: *u8) { http_reply(fd, 200, "text/html", "<h1>About</h1>") }'

TASK="${PROMPT:-Write an HTTP server on port 9090 with 3 routes:
/ returns \"Welcome\"
/hello returns \"Hello World\"
/status fn status_handler that returns \"OK\"
Output ONLY code, no explanation, no markdown.}"

mkdir -p "$RESULTS_DIR"

if [ $# -gt 0 ]; then
    MODELS=("$@")
else
    mapfile -t MODELS < <(
        curl -s "$OLLAMA_URL/api/tags" | \
        python3 -c "
import sys, json
tags = json.load(sys.stdin)
for m in tags.get('models', []):
    name = m['name']
    if 'embed' in name: continue
    print(name)
" 2>/dev/null | sort
    )
fi

[ ${#MODELS[@]} -eq 0 ] && { echo "No models found"; exit 1; }

echo -e "${CYN}el-stupido HTTP benchmark${RST} — $TIMESTAMP"
echo -e "Models: ${#MODELS[@]}  Compiler: $ESC"
echo "Task: 3-route HTTP server on port 9090"
echo "---"

PASS=0; WRONG=0; COMPILE_FAIL=0; PARSE_FAIL=0; TOTAL=0

set +eo pipefail
for model in "${MODELS[@]}"; do
    TOTAL=$((TOTAL + 1))
    slug=$(echo "$model" | tr '/:' '__')
    src="$RESULTS_DIR/${slug}.es"
    bin="/tmp/es_http_${slug}"

    printf "%-40s " "$model"

    FULL_PROMPT="$SPEC

$TASK"
    [[ "$model" == qwen3* ]] && FULL_PROMPT="$FULL_PROMPT /no_think"

    tmpprompt=$(mktemp); tmpjson=$(mktemp)
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

    code=$(echo "$raw" | sed '/^```/d' | sed '/<think>/,/<\/think>/d' | sed '/^;/d')
    echo "$code" > "$src"

    # compile
    compile_out=$($ESC "$src" -o "$bin" 2>&1)
    if [ $? -ne 0 ]; then
        err=$(echo "$compile_out" | head -1)
        if echo "$err" | grep -q "LLVM verify"; then
            echo -e "${YEL}CODEGEN${RST}"
            COMPILE_FAIL=$((COMPILE_FAIL + 1))
        else
            short=$(echo "$err" | sed "s|$src:||")
            echo -e "${RED}FAIL${RST}    $short"
            PARSE_FAIL=$((PARSE_FAIL + 1))
        fi
        continue
    fi

    # compiled! now test: start server, curl routes, check responses
    "$bin" &
    SERVER_PID=$!
    sleep 1

    r1=$(curl -s --max-time 2 http://localhost:9090/ 2>/dev/null || true)
    r2=$(curl -s --max-time 2 http://localhost:9090/hello 2>/dev/null || true)
    r3=$(curl -s --max-time 2 http://localhost:9090/status 2>/dev/null || true)

    kill $SERVER_PID 2>/dev/null; wait $SERVER_PID 2>/dev/null

    # check: at least 2 routes respond with non-empty content
    ok=0
    [ -n "$r1" ] && ok=$((ok + 1))
    [ -n "$r2" ] && ok=$((ok + 1))
    [ -n "$r3" ] && ok=$((ok + 1))

    if [ $ok -ge 2 ]; then
        echo -e "${GRN}PASS${RST}    ($ok/3 routes respond)"
        PASS=$((PASS + 1))
    elif [ $ok -ge 1 ]; then
        echo -e "${YEL}PARTIAL${RST} ($ok/3 routes)"
        WRONG=$((WRONG + 1))
    else
        echo -e "${YEL}WRONG${RST}   compiles but no routes respond"
        WRONG=$((WRONG + 1))
    fi
done

echo "---"
echo -e "Results: ${GRN}${PASS} pass${RST}, ${YEL}${WRONG} wrong/partial${RST}, ${YEL}${COMPILE_FAIL} codegen fail${RST}, ${RED}${PARSE_FAIL} parse fail${RST} / ${TOTAL} total"
echo "Source files saved in $RESULTS_DIR/"
