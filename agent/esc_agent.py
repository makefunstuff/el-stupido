#!/usr/bin/env python3
"""
esc-agent: autonomous tool-forging agent POC.

Full loop: goal → check cache → generate manifest → compile → cache → invoke → result.
No human in the loop. The LLM forges native binaries on demand.

Usage:
    python3 agent/esc_agent.py "add 17 and 25"
    python3 agent/esc_agent.py "read file data.txt and count characters"

Requires: ANTHROPIC_API_KEY or OPENAI_API_KEY in environment.
Falls back to a simple template engine if no API key is set.
"""

import json
import os
import subprocess
import sys
import hashlib
import tempfile

ESC = os.path.join(os.path.dirname(__file__), "..", "compiler", "target", "debug", "esc")
MAX_RETRIES = 3


def get_primitives():
    """Get the primitive registry as JSON."""
    result = subprocess.run([ESC, "primitives", "--machine"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"esc primitives failed: {result.stderr}")
    return json.loads(result.stdout)


def get_cached_tools():
    """Get list of cached tools from registry."""
    tools_json = os.path.expanduser("~/.esc/tools.json")
    if os.path.exists(tools_json):
        with open(tools_json) as f:
            return json.loads(f.read())
    return []


def compile_manifest(manifest_json, store=True):
    """Compile a manifest, return machine-readable result."""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        f.write(manifest_json)
        tmp_path = f.name

    try:
        args = [ESC, "compose", tmp_path, "--machine"]
        if store:
            args.append("--store")
        result = subprocess.run(args, capture_output=True, text=True)
        return json.loads(result.stdout) if result.stdout.strip() else {
            "status": "error",
            "error": {"kind": "unknown", "message": result.stderr, "hint": "unknown error"}
        }
    finally:
        os.unlink(tmp_path)


def invoke_tool(binary_path, args=None):
    """Invoke a compiled tool and capture output."""
    cmd = [binary_path] + (args or [])
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
    return {
        "stdout": result.stdout.strip(),
        "stderr": result.stderr.strip(),
        "exit_code": result.returncode,
    }


def generate_manifest_with_llm(goal, primitives, error_context=None):
    """
    Ask an LLM to generate a manifest for the given goal.
    Falls back to template matching if no API key is available.
    """
    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if api_key:
        return _generate_anthropic(goal, primitives, error_context, api_key)

    api_key = os.environ.get("OPENAI_API_KEY")
    if api_key:
        return _generate_openai(goal, primitives, error_context, api_key)

    # Fallback: simple template matching for demo purposes
    return _generate_template(goal)


def _build_system_prompt(primitives):
    """Build the system prompt with primitive registry."""
    prim_desc = "\n".join(
        f"  {p['id']}: {p['description']} | params: {json.dumps(p['params'])} | binds: {json.dumps(p['binds'])} | provides: {p['provides']} | effects: {p['effects']}"
        for p in primitives
    )
    return f"""You are an el-stupido manifest generator. Given a goal, emit a JSON manifest that composes primitives into a program.

AVAILABLE PRIMITIVES:
{prim_desc}

RULES:
- Output ONLY valid JSON, no explanation.
- The manifest has: "app" (short name), "capabilities" (effects used), "nodes" (ordered list).
- Each node: {{"id": "<unique>", "use": "<primitive>", "params": {{}}, "bind": {{}}}}
- Binds reference earlier node IDs. Nodes are evaluated in order.
- For reusable tools, prefer arg_num/arg_str over const_num/const_str when the goal implies variable inputs.
- Declare all required capabilities (io_write, io_read, fs_read, fs_write).
- Keep it minimal — fewest nodes possible.

EXAMPLE — "add two numbers from CLI args":
{{"app":"add-tool","capabilities":["io_write"],"nodes":[{{"id":"a","use":"arg_num","params":{{"index":1}}}},{{"id":"b","use":"arg_num","params":{{"index":2}}}},{{"id":"sum","use":"add","bind":{{"lhs":"a","rhs":"b"}}}},{{"id":"out","use":"print_num","bind":{{"value":"sum"}}}}]}}"""


def _generate_anthropic(goal, primitives, error_context, api_key):
    """Generate manifest using Anthropic API."""
    import urllib.request
    system = _build_system_prompt(primitives)
    user_msg = f"Goal: {goal}"
    if error_context:
        user_msg += f"\n\nPrevious attempt failed with: {json.dumps(error_context)}\nFix the error and try again."

    body = json.dumps({
        "model": "claude-sonnet-4-20250514",
        "max_tokens": 1024,
        "system": system,
        "messages": [{"role": "user", "content": user_msg}],
    }).encode()

    req = urllib.request.Request(
        "https://api.anthropic.com/v1/messages",
        data=body,
        headers={
            "Content-Type": "application/json",
            "x-api-key": api_key,
            "anthropic-version": "2023-06-01",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read())

    text = data["content"][0]["text"]
    # Extract JSON from response (handle markdown code blocks)
    if "```" in text:
        text = text.split("```")[1]
        if text.startswith("json"):
            text = text[4:]
    return text.strip()


def _generate_openai(goal, primitives, error_context, api_key):
    """Generate manifest using OpenAI API."""
    import urllib.request
    system = _build_system_prompt(primitives)
    user_msg = f"Goal: {goal}"
    if error_context:
        user_msg += f"\n\nPrevious attempt failed with: {json.dumps(error_context)}\nFix the error and try again."

    body = json.dumps({
        "model": "gpt-4o-mini",
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user_msg},
        ],
        "max_tokens": 1024,
    }).encode()

    req = urllib.request.Request(
        "https://api.openai.com/v1/chat/completions",
        data=body,
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read())

    text = data["choices"][0]["message"]["content"]
    if "```" in text:
        text = text.split("```")[1]
        if text.startswith("json"):
            text = text[4:]
    return text.strip()


def _generate_template(goal):
    """Simple template fallback when no LLM API key is available."""
    goal_lower = goal.lower()

    if "add" in goal_lower or "sum" in goal_lower:
        # Extract numbers if present
        nums = [w for w in goal.split() if w.replace(".", "").replace("-", "").isdigit()]
        if len(nums) >= 2:
            return json.dumps({
                "app": "add-tool",
                "capabilities": ["io_write"],
                "nodes": [
                    {"id": "a", "use": "const_num", "params": {"value": float(nums[0])}},
                    {"id": "b", "use": "const_num", "params": {"value": float(nums[1])}},
                    {"id": "sum", "use": "add", "bind": {"lhs": "a", "rhs": "b"}},
                    {"id": "out", "use": "print_num", "bind": {"value": "sum"}},
                ],
            })
        else:
            return json.dumps({
                "app": "add-args",
                "capabilities": ["io_write"],
                "nodes": [
                    {"id": "a", "use": "arg_num", "params": {"index": 1}},
                    {"id": "b", "use": "arg_num", "params": {"index": 2}},
                    {"id": "sum", "use": "add", "bind": {"lhs": "a", "rhs": "b"}},
                    {"id": "out", "use": "print_num", "bind": {"value": "sum"}},
                ],
            })

    if "multiply" in goal_lower or "double" in goal_lower:
        return json.dumps({
            "app": "mul-args",
            "capabilities": ["io_write"],
            "nodes": [
                {"id": "a", "use": "arg_num", "params": {"index": 1}},
                {"id": "b", "use": "arg_num", "params": {"index": 2}},
                {"id": "product", "use": "mul", "bind": {"lhs": "a", "rhs": "b"}},
                {"id": "out", "use": "print_num", "bind": {"value": "product"}},
            ],
        })

    if "count" in goal_lower and "char" in goal_lower or "length" in goal_lower:
        return json.dumps({
            "app": "charcount",
            "capabilities": ["io_write"],
            "nodes": [
                {"id": "text", "use": "arg_str", "params": {"index": 1}},
                {"id": "len", "use": "len_str", "bind": {"text": "text"}},
                {"id": "out", "use": "print_num", "bind": {"value": "len"}},
            ],
        })

    if "concat" in goal_lower or "join" in goal_lower:
        return json.dumps({
            "app": "concat-args",
            "capabilities": ["io_write"],
            "nodes": [
                {"id": "a", "use": "arg_str", "params": {"index": 1}},
                {"id": "b", "use": "arg_str", "params": {"index": 2}},
                {"id": "joined", "use": "concat", "bind": {"left": "a", "right": "b"}},
                {"id": "out", "use": "print_str", "bind": {"value": "joined"}},
            ],
        })

    if "greet" in goal_lower or "hello" in goal_lower:
        return json.dumps({
            "app": "greeter",
            "capabilities": ["io_write"],
            "nodes": [
                {"id": "prefix", "use": "const_str", "params": {"value": "Hello, "}},
                {"id": "name", "use": "arg_str", "params": {"index": 1}},
                {"id": "msg", "use": "concat", "bind": {"left": "prefix", "right": "name"}},
                {"id": "out", "use": "print_str", "bind": {"value": "msg"}},
            ],
        })

    if "subtract" in goal_lower or "minus" in goal_lower:
        return json.dumps({
            "app": "sub-args",
            "capabilities": ["io_write"],
            "nodes": [
                {"id": "a", "use": "arg_num", "params": {"index": 1}},
                {"id": "b", "use": "arg_num", "params": {"index": 2}},
                {"id": "diff", "use": "sub", "bind": {"lhs": "a", "rhs": "b"}},
                {"id": "out", "use": "print_num", "bind": {"value": "diff"}},
            ],
        })

    if "divide" in goal_lower:
        return json.dumps({
            "app": "div-args",
            "capabilities": ["io_write"],
            "nodes": [
                {"id": "a", "use": "arg_num", "params": {"index": 1}},
                {"id": "b", "use": "arg_num", "params": {"index": 2}},
                {"id": "quot", "use": "div", "bind": {"lhs": "a", "rhs": "b"}},
                {"id": "out", "use": "print_num", "bind": {"value": "quot"}},
            ],
        })

    # Generic fallback: echo arg
    return json.dumps({
        "app": "echo-arg",
        "capabilities": ["io_write"],
        "nodes": [
            {"id": "input", "use": "arg_str", "params": {"index": 1}},
            {"id": "out", "use": "print_str", "bind": {"value": "input"}},
        ],
    })


def agent_loop(goal, invoke_args=None):
    """
    The full autonomous loop:
    1. Generate manifest from goal (LLM or template)
    2. Compile with --store --machine
    3. If error → self-correct → retry
    4. Invoke compiled binary
    5. Return result
    """
    print(f"[goal] {goal}", file=sys.stderr)

    # Step 1: Get primitive registry
    primitives = get_primitives()

    # Step 2: Generate + compile with retry
    error_context = None
    for attempt in range(1, MAX_RETRIES + 1):
        print(f"[attempt {attempt}/{MAX_RETRIES}] generating manifest...", file=sys.stderr)

        manifest_json = generate_manifest_with_llm(goal, primitives, error_context)

        # Validate it's actual JSON
        try:
            parsed = json.loads(manifest_json)
            manifest_json = json.dumps(parsed)  # normalize
        except json.JSONDecodeError as e:
            error_context = {"kind": "invalid_json", "message": str(e), "hint": "output valid JSON only"}
            print(f"[error] invalid JSON from LLM: {e}", file=sys.stderr)
            continue

        print(f"[manifest] {parsed.get('app', '?')} ({len(parsed.get('nodes', []))} nodes)", file=sys.stderr)

        # Step 3: Compile
        result = compile_manifest(manifest_json, store=True)

        if result.get("status") == "ok":
            binary = result["binary"]
            cached = result.get("cached", False)
            hash_short = result.get("hash", "")[:12]

            if cached:
                print(f"[cached] {hash_short}", file=sys.stderr)
            else:
                print(f"[compiled] {hash_short} ({result.get('rust_size', '?')}B Rust -> {result.get('binary_size', '?')}B binary)", file=sys.stderr)

            # Step 4: Invoke
            run_args = invoke_args or []
            print(f"[invoke] {binary} {' '.join(run_args)}", file=sys.stderr)
            run_result = invoke_tool(binary, run_args)

            if run_result["exit_code"] == 0:
                print(f"[ok] {run_result['stdout']}", file=sys.stderr)
                return {
                    "status": "ok",
                    "output": run_result["stdout"],
                    "binary": binary,
                    "hash": result.get("hash", ""),
                    "cached": cached,
                    "app": result.get("app", ""),
                }
            else:
                print(f"[runtime error] exit {run_result['exit_code']}: {run_result['stderr']}", file=sys.stderr)
                return {
                    "status": "runtime_error",
                    "stderr": run_result["stderr"],
                    "exit_code": run_result["exit_code"],
                }
        else:
            # Compilation failed — extract error for retry
            error = result.get("error", {})
            error_context = error
            print(f"[compile error] {error.get('kind', '?')}: {error.get('hint', error.get('message', '?'))}", file=sys.stderr)
            continue

    return {"status": "failed", "message": f"failed after {MAX_RETRIES} attempts"}


def main():
    if len(sys.argv) < 2:
        print("usage: esc-agent <goal> [args for compiled tool...]", file=sys.stderr)
        print("", file=sys.stderr)
        print("examples:", file=sys.stderr)
        print('  python3 agent/esc_agent.py "add two numbers" 17 25', file=sys.stderr)
        print('  python3 agent/esc_agent.py "multiply two numbers" 6 7', file=sys.stderr)
        print('  python3 agent/esc_agent.py "greet someone" World', file=sys.stderr)
        print('  python3 agent/esc_agent.py "count characters" "hello world"', file=sys.stderr)
        sys.exit(1)

    goal = sys.argv[1]
    invoke_args = sys.argv[2:] if len(sys.argv) > 2 else None

    result = agent_loop(goal, invoke_args)

    if result["status"] == "ok":
        # Print just the output to stdout (clean for piping)
        print(result["output"])
    else:
        print(json.dumps(result, indent=2), file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
