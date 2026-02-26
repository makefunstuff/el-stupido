#!/usr/bin/env python3
"""
Test the self-correction loop: bad manifest → structured error → fix → success.

Simulates what an LLM would do when the compiler rejects a manifest:
1. Submit a broken manifest
2. Read the structured error + hint
3. Apply the fix
4. Resubmit

No LLM API needed — this tests the compiler's error→hint pipeline directly.
"""

import json
import os
import subprocess
import sys
import tempfile

ESC = os.path.join(os.path.dirname(__file__), "..", "compiler", "target", "debug", "esc")


def compile_machine(manifest_json):
    """Compile with --machine, return parsed result."""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False) as f:
        f.write(manifest_json)
        tmp = f.name
    try:
        r = subprocess.run([ESC, "compose", tmp, "--machine", "--store"],
                          capture_output=True, text=True)
        return json.loads(r.stdout) if r.stdout.strip() else {"status": "error", "error": {"kind": "crash"}}
    finally:
        os.unlink(tmp)


def run_test(name, bad_manifest, fix_fn, invoke_args=None, invoke_input=None):
    """Run a single self-correction test case."""
    print(f"\n{'='*60}")
    print(f"TEST: {name}")
    print(f"{'='*60}")

    # Step 1: Submit broken manifest
    print(f"\n[1] Submitting broken manifest...")
    result = compile_machine(json.dumps(bad_manifest))

    if result.get("status") == "ok":
        print(f"    UNEXPECTED: compiled without error!")
        return False

    error = result.get("error", {})
    print(f"    Error kind: {error.get('kind')}")
    print(f"    Message:    {error.get('message')}")
    print(f"    Hint:       {error.get('hint')}")

    # Step 2: Apply fix based on error
    print(f"\n[2] Applying fix based on hint...")
    fixed = fix_fn(bad_manifest, error)
    print(f"    Fixed manifest: {json.dumps(fixed, indent=2)[:200]}...")

    # Step 3: Resubmit
    print(f"\n[3] Resubmitting fixed manifest...")
    result2 = compile_machine(json.dumps(fixed))

    if result2.get("status") == "ok":
        binary = result2["binary"]
        cached = result2.get("cached", False)
        print(f"    Compiled! binary={binary}")
        print(f"    Cached: {cached}")

        # Step 4: Invoke
        if invoke_args or invoke_input:
            cmd = [binary] + (invoke_args or [])
            r = subprocess.run(cmd, capture_output=True, text=True,
                             input=invoke_input, timeout=5)
            print(f"\n[4] Invoked: {r.stdout.strip()}")
            if r.returncode == 0:
                print(f"    PASS ✓")
                return True
            else:
                print(f"    Runtime error: {r.stderr}")
                return False
        else:
            print(f"    PASS ✓ (no invocation)")
            return True
    else:
        error2 = result2.get("error", {})
        print(f"    STILL FAILING: {error2.get('kind')}: {error2.get('message')}")
        return False


# ── Test Cases ────────────────────────────────────────────────────

def test_missing_capability():
    """LLM forgets to declare io_write."""
    bad = {
        "app": "test1",
        "capabilities": [],  # missing io_write!
        "nodes": [
            {"id": "a", "use": "const_num", "params": {"value": 42}},
            {"id": "out", "use": "print_num", "bind": {"value": "a"}},
        ]
    }

    def fix(manifest, error):
        # Error hint says: add "io_write" to capabilities
        cap = error.get("fields", {}).get("capability", "io_write")
        manifest["capabilities"].append(cap)
        return manifest

    return run_test(
        "Missing capability (io_write)",
        bad, fix, invoke_args=[]
    )


def test_type_mismatch():
    """LLM wires a str node into a num bind."""
    bad = {
        "app": "test2",
        "capabilities": ["io_write"],
        "nodes": [
            {"id": "name", "use": "const_str", "params": {"value": "hello"}},
            {"id": "out", "use": "print_num", "bind": {"value": "name"}},  # str -> num mismatch!
        ]
    }

    def fix(manifest, error):
        # Error says: expected 'num' but got 'str'. Fix: use print_str instead.
        if error.get("fields", {}).get("expected") == "num":
            manifest["nodes"][1]["use"] = "print_str"
        return manifest

    return run_test(
        "Type mismatch (str into num bind)",
        bad, fix, invoke_args=[]
    )


def test_unknown_bind_target():
    """LLM references a node that doesn't exist."""
    bad = {
        "app": "test3",
        "capabilities": ["io_write"],
        "nodes": [
            {"id": "a", "use": "arg_num", "params": {"index": 1}},
            {"id": "b", "use": "arg_num", "params": {"index": 2}},
            {"id": "sum", "use": "add", "bind": {"lhs": "a", "rhs": "c"}},  # 'c' doesn't exist!
            {"id": "out", "use": "print_num", "bind": {"value": "sum"}},
        ]
    }

    def fix(manifest, error):
        # Error says: bind 'rhs' points to 'c' which doesn't exist
        target = error.get("fields", {}).get("target")
        if target == "c":
            manifest["nodes"][2]["bind"]["rhs"] = "b"
        return manifest

    return run_test(
        "Unknown bind target",
        bad, fix, invoke_args=["10", "20"]
    )


def test_forward_bind():
    """LLM orders nodes wrong — binds to a later node."""
    bad = {
        "app": "test4",
        "capabilities": ["io_write"],
        "nodes": [
            {"id": "sum", "use": "add", "bind": {"lhs": "a", "rhs": "b"}},  # a,b defined AFTER!
            {"id": "a", "use": "const_num", "params": {"value": 10}},
            {"id": "b", "use": "const_num", "params": {"value": 20}},
            {"id": "out", "use": "print_num", "bind": {"value": "sum"}},
        ]
    }

    def fix(manifest, error):
        # Error says: move 'a' before 'sum'. Fix: reorder nodes.
        nodes = manifest["nodes"]
        # Move 'a' and 'b' before 'sum'
        a = next(n for n in nodes if n["id"] == "a")
        b = next(n for n in nodes if n["id"] == "b")
        s = next(n for n in nodes if n["id"] == "sum")
        o = next(n for n in nodes if n["id"] == "out")
        manifest["nodes"] = [a, b, s, o]
        return manifest

    return run_test(
        "Forward bind (wrong node order)",
        bad, fix, invoke_args=[]
    )


def test_missing_bind():
    """LLM forgets a required bind."""
    bad = {
        "app": "test5",
        "capabilities": ["io_write"],
        "nodes": [
            {"id": "a", "use": "arg_num", "params": {"index": 1}},
            {"id": "sum", "use": "add", "bind": {"lhs": "a"}},  # missing 'rhs'!
            {"id": "out", "use": "print_num", "bind": {"value": "sum"}},
        ]
    }

    def fix(manifest, error):
        # Error says: missing required bind 'rhs'. Fix: add a const node and bind to it.
        bind_name = error.get("fields", {}).get("bind", "rhs")
        manifest["nodes"].insert(1, {"id": "b", "use": "const_num", "params": {"value": 0}})
        manifest["nodes"][2]["bind"][bind_name] = "b"
        return manifest

    return run_test(
        "Missing required bind",
        bad, fix, invoke_args=["42"]
    )


def test_unknown_primitive():
    """LLM uses a primitive that doesn't exist."""
    bad = {
        "app": "test6",
        "capabilities": ["io_write"],
        "nodes": [
            {"id": "x", "use": "http_get", "params": {"url": "http://example.com"}},
            {"id": "out", "use": "print_str", "bind": {"value": "x"}},
        ]
    }

    def fix(manifest, error):
        # Error says: primitive 'http_get' doesn't exist. Downgrade to const_str.
        manifest["nodes"][0] = {"id": "x", "use": "const_str", "params": {"value": "http not available"}}
        return manifest

    return run_test(
        "Unknown primitive (http_get doesn't exist)",
        bad, fix, invoke_args=[]
    )


def test_chained_errors():
    """Multiple errors — fix one at a time (simulates multi-round correction)."""
    bad = {
        "app": "test7",
        "capabilities": [],  # missing caps
        "nodes": [
            {"id": "a", "use": "arg_num", "params": {"index": 1}},
            {"id": "b", "use": "arg_num", "params": {"index": 2}},
            {"id": "sum", "use": "add", "bind": {"lhs": "a", "rhs": "b"}},
            {"id": "out", "use": "print_num", "bind": {"value": "sum"}},
        ]
    }

    print(f"\n{'='*60}")
    print(f"TEST: Chained errors (multi-round correction)")
    print(f"{'='*60}")

    manifest = bad
    for attempt in range(1, 4):
        print(f"\n[round {attempt}]")
        result = compile_machine(json.dumps(manifest))
        if result.get("status") == "ok":
            binary = result["binary"]
            r = subprocess.run([binary, "3", "4"], capture_output=True, text=True, timeout=5)
            print(f"  Result: {r.stdout.strip()}")
            print(f"  PASS ✓")
            return True

        error = result.get("error", {})
        kind = error.get("kind")
        print(f"  Error: {kind} — {error.get('hint')}")

        # Auto-fix based on error kind
        if kind == "missing_capability":
            cap = error.get("fields", {}).get("capability")
            manifest["capabilities"].append(cap)
            print(f"  Fix: added capability '{cap}'")
        elif kind == "bind_type_mismatch":
            # Would need to change node wiring
            break
        else:
            break

    print(f"  FAIL ✗")
    return False


# ── Main ──────────────────────────────────────────────────────────

if __name__ == "__main__":
    tests = [
        test_missing_capability,
        test_type_mismatch,
        test_unknown_bind_target,
        test_forward_bind,
        test_missing_bind,
        test_unknown_primitive,
        test_chained_errors,
    ]

    results = []
    for test in tests:
        try:
            results.append(test())
        except Exception as e:
            print(f"  EXCEPTION: {e}")
            results.append(False)

    passed = sum(results)
    total = len(results)
    print(f"\n{'='*60}")
    print(f"RESULTS: {passed}/{total} self-correction tests passed")
    print(f"{'='*60}")

    sys.exit(0 if passed == total else 1)
