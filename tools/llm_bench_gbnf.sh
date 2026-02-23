#!/usr/bin/env bash
# tools/llm_bench_gbnf.sh — Constrained (JSON schema) vs Free benchmark
# Tests all sub-4B ollama models on factorial task: JSON schema vs free text.
set -uo pipefail

ESC="./esc"
OLLAMA_URL="${OLLAMA_URL:-http://localhost:11434}"
RESULTS_DIR="tests/llm_gbnf"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
TIMEOUT="${TIMEOUT:-120}"

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[0;33m'
CYN='\033[0;36m'; BLD='\033[1m'; RST='\033[0m'

mkdir -p "$RESULTS_DIR"

# ─── JSON Schema: forces model to output structured program ──────────
SCHEMA='{
  "type": "object",
  "properties": {
    "functions": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "name": {"type": "string"},
          "params": {"type": "string"},
          "body": {"type": "string"}
        },
        "required": ["name", "params", "body"]
      }
    },
    "main_body": {"type": "string"}
  },
  "required": ["functions", "main_body"]
}'

# ─── Prompts ─────────────────────────────────────────────────────────
CONSTRAINED_PROMPT='el-stupido: compiled language. Write a JSON program spec.
Available: product(1..=n) computes 1*2*...*n. print(expr) prints value.
for i := 1..=n { body }. name(args) = expr defines one-liner function.
Task: factorial using product(1..=n), print factorials 1 through 12.
The "body" field must contain el-stupido code like: product(1..=n)
The "main_body" must contain: for i := 1..=12 { print(fact(i)) }'

FREE_PROMPT='el-stupido: compiled declarative lang.
name(args) = expr (one-liner fn, types inferred as i32)
Built-ins: product(1..=n) sum(1..n) count(a..b)
print(expr) auto-formats output. x := expr. for i := 1..=n { }. if/else/while/return.
fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)
fact(n) = product(1..=n)
fn main() { for i := 1..=10 { print(fib(i)) } }

Write: factorial using product(), print factorials 1 through 12.
Output ONLY code, no explanation, no markdown.'

EXPECTED="479001600"

# ─── Model selection ─────────────────────────────────────────────────
if [ $# -gt 0 ]; then
    MODELS=("$@")
else
    # All sub-4B models
    mapfile -t MODELS < <(
        curl -s "$OLLAMA_URL/api/tags" | python3 -c "
import sys, json
tags = json.load(sys.stdin)
for m in sorted(tags.get('models', []), key=lambda x: x.get('size',0)):
    d = m.get('details', {})
    ps = d.get('parameter_size', '0')
    try:
        n = float(ps.replace('B','').replace('M','')) if 'B' in ps else float(ps.replace('M',''))/1000 if 'M' in ps else 0
    except: n = 0
    if 0.3 < n <= 4 and 'embed' not in m['name']:
        print(m['name'])
" 2>/dev/null
    )
fi

[ ${#MODELS[@]} -eq 0 ] && { echo "No sub-4B models found"; exit 1; }

# ─── JSON → el-stupido converter ─────────────────────────────────────
json_to_es() {
    python3 -c "
import json, sys
BUILTINS = {'printf','print','malloc','free','exit','strlen','strcmp','sprintf'}
try:
    data = json.load(sys.stdin)
except: sys.exit(1)

lines = []
has_main = False
for fn in data.get('functions', []):
    name = fn.get('name','')
    if name in BUILTINS: continue
    params = fn.get('params','').strip()
    body = fn.get('body','').strip()
    # skip prose
    prose_words = {'the','of','using','that','output','formatted','simple','computes','returns','function'}
    words = body.lower().split()
    if sum(1 for w in words if w in prose_words) >= 2: continue
    if name == 'main': has_main = True
    # one-liner?
    if '{' not in body and '\n' not in body and ';' not in body and 'for ' not in body and len(body) < 120 and name != 'main':
        lines.append(f'{name}({params}) = {body}')
    else:
        b = body.strip()
        if b.startswith('{') and b.endswith('}'): b = b[1:-1].strip()
        b = b.replace('else if ','el if ').replace('} else {','}\n  el {').replace('else {','el {')
        lines.append(f'fn {name}({params}) {{')
        for s in b.replace(';','\n').split('\n'):
            s = s.strip()
            if s: lines.append(f'  {s}')
        lines.append('}')

main_body = data.get('main_body','').strip()
if main_body and not has_main:
    mb = main_body
    if mb.startswith('{') and mb.endswith('}'): mb = mb[1:-1].strip()
    mb = mb.replace('else if ','el if ').replace('} else {','}\n  el {')
    lines.append('')
    lines.append('fn main() {')
    for s in mb.replace(';','\n').split('\n'):
        s = s.strip()
        if s: lines.append(f'  {s}')
    lines.append('}')

print('\n'.join(lines))
" 2>/dev/null
}

# ─── Query ollama ────────────────────────────────────────────────────
query_constrained() {
    local model="$1"
    tmpprompt=$(mktemp); tmpjson=$(mktemp)
    printf '%s' "$CONSTRAINED_PROMPT" > "$tmpprompt"
    python3 -c "
import json
prompt = open('$tmpprompt','r').read()
schema = json.loads('''$SCHEMA''')
with open('$tmpjson','w') as f:
    json.dump({'model': '$model', 'prompt': prompt, 'format': schema, 'stream': False, 'options': {'temperature': 0.3, 'num_predict': 512}}, f)
" 2>/dev/null
    raw=$(curl -s --max-time "$TIMEOUT" "$OLLAMA_URL/api/generate" -d @"$tmpjson" 2>/dev/null)
    rm -f "$tmpprompt" "$tmpjson"
    echo "$raw" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    r = data.get('response','')
    t = data.get('eval_count',0)
    print(f'{t}')
    print(r)
except: print('0')
" 2>/dev/null
}

query_free() {
    local model="$1"
    local prompt="$FREE_PROMPT"
    [[ "$model" == qwen3* ]] && prompt="$prompt /no_think"
    tmpprompt=$(mktemp); tmpjson=$(mktemp)
    printf '%s' "$prompt" > "$tmpprompt"
    python3 -c "
import json
prompt = open('$tmpprompt','r').read()
with open('$tmpjson','w') as f:
    json.dump({'model': '$model', 'prompt': prompt, 'stream': False, 'options': {'temperature': 0.3, 'num_predict': 512}}, f)
" 2>/dev/null
    raw=$(curl -s --max-time "$TIMEOUT" "$OLLAMA_URL/api/generate" -d @"$tmpjson" 2>/dev/null)
    rm -f "$tmpprompt" "$tmpjson"
    echo "$raw" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    r = data.get('response','')
    t = data.get('eval_count',0)
    # strip markdown/think
    import re
    r = re.sub(r'<think>.*?</think>', '', r, flags=re.DOTALL)
    r = re.sub(r'^\`\`\`\w*\n?', '', r, flags=re.MULTILINE)
    r = re.sub(r'^\`\`\`\s*$', '', r, flags=re.MULTILINE)
    r = r.strip()
    print(f'{t}')
    print(r)
except: print('0')
" 2>/dev/null
}

# ─── Main ────────────────────────────────────────────────────────────
echo -e "${CYN}${BLD}━━━ JSON Schema Constrained vs Free Generation ━━━${RST}"
echo -e "Task: factorial 1..12 (expected last line: $EXPECTED)"
echo -e "Models: ${#MODELS[@]}"
echo ""
printf "${BLD}%-30s  %-20s  %-20s${RST}\n" "Model" "Schema (constrained)" "Free (baseline)"
printf "%-30s  %-20s  %-20s\n" "──────────────────────────────" "────────────────────" "────────────────────"

C_PASS=0; F_PASS=0; TOTAL=0

set +eo pipefail
for model in "${MODELS[@]}"; do
    TOTAL=$((TOTAL + 1))
    slug=$(echo "$model" | tr '/:' '__')
    printf "%-30s  " "$model"

    # ── Constrained (JSON schema) ──
    c_out=$(query_constrained "$model")
    c_tokens=$(echo "$c_out" | head -1)
    c_json=$(echo "$c_out" | tail -n +2)
    c_src="$RESULTS_DIR/${slug}_schema.es"
    c_json_file="$RESULTS_DIR/${slug}_schema.json"
    echo "$c_json" > "$c_json_file"

    c_result="FAIL"
    if [ -n "$c_json" ]; then
        es_code=$(echo "$c_json" | json_to_es)
        if [ -n "$es_code" ]; then
            echo "$es_code" > "$c_src"
            bin="/tmp/es_bench_c_$$"
            if $ESC "$c_src" -o "$bin" 2>/dev/null; then
                run_out=$(timeout 10 "$bin" 2>&1) || true
                rm -f "$bin"
                if echo "$run_out" | grep -q "$EXPECTED"; then
                    c_result="PASS"; C_PASS=$((C_PASS + 1))
                elif [ -n "$run_out" ]; then
                    c_result="WRONG"
                else
                    c_result="NO_OUT"
                fi
            else
                c_result="COMP_FAIL"
            fi
        else
            c_result="CONV_FAIL"
        fi
    fi

    # ── Free (baseline) ──
    f_out=$(query_free "$model")
    f_tokens=$(echo "$f_out" | head -1)
    f_code=$(echo "$f_out" | tail -n +2)
    f_src="$RESULTS_DIR/${slug}_free.es"
    echo "$f_code" > "$f_src"

    f_result="FAIL"
    if [ -n "$f_code" ]; then
        bin="/tmp/es_bench_f_$$"
        if $ESC "$f_src" -o "$bin" 2>/dev/null; then
            run_out=$(timeout 10 "$bin" 2>&1) || true
            rm -f "$bin"
            if echo "$run_out" | grep -q "$EXPECTED"; then
                f_result="PASS"; F_PASS=$((F_PASS + 1))
            elif [ -n "$run_out" ]; then
                f_result="WRONG"
            else
                f_result="NO_OUT"
            fi
        else
            f_result="COMP_FAIL"
        fi
    fi

    # Display
    case "$c_result" in PASS) cc="$GRN";; WRONG) cc="$YEL";; *) cc="$RED";; esac
    case "$f_result" in PASS) fc="$GRN";; WRONG) fc="$YEL";; *) fc="$RED";; esac
    printf "${cc}%-8s${RST} (%3dt)      ${fc}%-8s${RST} (%3dt)\n" \
        "$c_result" "$c_tokens" "$f_result" "$f_tokens"
done

echo ""
echo -e "${BLD}━━━ Results ━━━${RST}"
echo -e "  Schema constrained: ${GRN}${C_PASS}${RST}/${TOTAL} pass"
echo -e "  Free generation:    ${GRN}${F_PASS}${RST}/${TOTAL} pass"
if [ $C_PASS -gt $F_PASS ]; then
    delta=$((C_PASS - F_PASS))
    echo -e "  ${GRN}${BLD}Schema wins by ${delta}${RST}"
elif [ $F_PASS -gt $C_PASS ]; then
    delta=$((F_PASS - C_PASS))
    echo -e "  ${YEL}Free wins by ${delta}${RST}"
else
    echo -e "  Tied"
fi
echo ""
echo "Sources: $RESULTS_DIR/"
