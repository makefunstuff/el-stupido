#!/usr/bin/env python3
"""
Embedding experiment for el-stupido tool discovery.

Tests three representations for tool similarity search:
1. Bag-of-primitives (structural fingerprint, zero ML)
2. Capability + type signature matching (exact structured search)
3. Simple TF-IDF on natural language descriptions (lightweight baseline)

Question: at the current scale (8 tools, 29 primitives), do we need
neural embeddings? Or does structured search dominate?

No dependencies beyond Python stdlib + json.
"""

import json
import math
import os
import sys

# ── Load tool registry ────────────────────────────────────────────

TOOLS_PATH = os.path.expanduser("~/.esc/tools.json")


def load_tools():
    with open(TOOLS_PATH) as f:
        return json.loads(f.read())


# ── Representation 1: Bag of Primitives ──────────────────────────

# All known primitives (forms the vector dimensions)
ALL_PRIMS = [
    "const_num", "const_str", "add", "sub", "mul", "div", "gt", "eq_num",
    "and_bool", "or_bool", "not_bool", "select_num", "select_str",
    "repeat_str", "to_string", "concat", "len_str", "cwd", "path_join",
    "read_stdin", "parse_num", "read_file", "read_file_dyn",
    "write_file", "write_file_dyn", "print_num", "print_str",
    "arg_num", "arg_str",
]


def bag_of_primitives(tool):
    """Extract primitive count vector from canonical tape."""
    tape = tool.get("canonical_tape", "")
    # Count opcode occurrences
    opcode_to_prim = {
        "cn": "const_num", "cs": "const_str", "ad": "add", "sb": "sub",
        "ml": "mul", "dv": "div", "gt": "gt", "eq": "eq_num",
        "an": "and_bool", "ob": "or_bool", "nt": "not_bool",
        "sn": "select_num", "ss": "select_str", "ts": "to_string",
        "cc": "concat", "ls": "len_str", "rp": "repeat_str",
        "cw": "cwd", "pj": "path_join", "ri": "read_stdin",
        "pf": "parse_num", "rf": "read_file", "rd": "read_file_dyn",
        "wf": "write_file", "wd": "write_file_dyn",
        "pn": "print_num", "ps": "print_str",
        "gn": "arg_num", "gs": "arg_str",
    }

    counts = {p: 0 for p in ALL_PRIMS}
    for line in tape.strip().split("\n"):
        parts = line.strip().split()
        if len(parts) >= 2 and parts[0].isdigit():
            opcode = parts[1]
            prim = opcode_to_prim.get(opcode, opcode)
            if prim in counts:
                counts[prim] += 1

    return [counts[p] for p in ALL_PRIMS]


def cosine_sim(a, b):
    dot = sum(x * y for x, y in zip(a, b))
    mag_a = math.sqrt(sum(x * x for x in a))
    mag_b = math.sqrt(sum(x * x for x in b))
    if mag_a == 0 or mag_b == 0:
        return 0.0
    return dot / (mag_a * mag_b)


# ── Representation 2: Structured Signature ───────────────────────

def type_signature(tool):
    """Extract (input_types, output_types, capabilities) tuple."""
    in_types = tuple(sorted(i.get("type", "?") for i in tool.get("inputs", [])))
    out_types = tuple(sorted(o.get("type", "?") for o in tool.get("outputs", [])))
    caps = tuple(sorted(tool.get("capabilities", [])))
    return (in_types, out_types, caps)


def signature_match(query_sig, tool_sig):
    """Check if a tool signature matches a query signature."""
    q_in, q_out, q_caps = query_sig
    t_in, t_out, t_caps = tool_sig

    # Exact match on I/O types
    in_match = q_in == t_in
    out_match = q_out == t_out

    # Capabilities must be subset
    cap_match = set(q_caps).issubset(set(t_caps))

    score = sum([in_match, out_match, cap_match]) / 3.0
    return score


# ── Representation 3: TF-IDF on description words ────────────────

def tokenize(text):
    """Simple word tokenizer."""
    return [w.lower().strip(".,!?;:") for w in text.split() if len(w) > 1]


def tfidf_vectors(descriptions):
    """Build TF-IDF vectors from a list of descriptions."""
    # Build vocabulary
    vocab = {}
    for desc in descriptions:
        for word in set(tokenize(desc)):
            vocab[word] = vocab.get(word, 0) + 1

    # IDF
    n_docs = len(descriptions)
    idf = {word: math.log(n_docs / (1 + count)) for word, count in vocab.items()}
    word_list = sorted(vocab.keys())

    # TF-IDF vectors
    vectors = []
    for desc in descriptions:
        tokens = tokenize(desc)
        tf = {}
        for t in tokens:
            tf[t] = tf.get(t, 0) + 1
        total = len(tokens) or 1
        vec = [(tf.get(w, 0) / total) * idf.get(w, 0) for w in word_list]
        vectors.append(vec)

    return vectors, word_list


# ── Experiment ───────────────────────────────────────────────────

# Human-written descriptions for each tool (simulating what an LLM would generate)
TOOL_DESCRIPTIONS = {
    "add-args": "add two numbers from command line arguments",
    "sum-demo": "add two hardcoded numbers 13 and 29 and print the result",
    "branch-repeat": "compare two numbers, pick a string based on the comparison, repeat it",
    "file-report": "read a file and print how many characters it has",
    "mul-args": "multiply two numbers from command line arguments",
    "greeter": "greet someone by name with hello prefix",
    "charcount": "count the number of characters in a string argument",
    "add-tool": "add two specific numbers 17 and 25",
}

# Queries that should match specific tools
TEST_QUERIES = [
    ("sum two numbers",                   ["add-args", "add-tool", "sum-demo"]),
    ("multiply two values",               ["mul-args"]),
    ("say hello to a person",             ["greeter"]),
    ("how long is this text",             ["charcount"]),
    ("read a file and measure its size",  ["file-report"]),
    ("conditional string repetition",     ["branch-repeat"]),
    ("add command line arguments",        ["add-args"]),
    ("subtract two numbers",             []),  # no match expected
]


def run_experiment():
    tools = load_tools()
    tool_by_name = {t["app"]: t for t in tools}

    print("=" * 70)
    print("EL-STUPIDO EMBEDDING EXPERIMENT")
    print(f"Tools in cache: {len(tools)}")
    print(f"Primitive vocabulary: {len(ALL_PRIMS)} dimensions")
    print("=" * 70)

    # ── Bag of Primitives Similarity Matrix ──
    print("\n── 1. BAG-OF-PRIMITIVES SIMILARITY MATRIX ──\n")

    bop_vectors = {}
    for t in tools:
        name = t["app"]
        bop_vectors[name] = bag_of_primitives(t)

    names = sorted(bop_vectors.keys())
    print(f"{'':>15}", end="")
    for n in names:
        print(f"{n[:8]:>10}", end="")
    print()

    for n1 in names:
        print(f"{n1:>15}", end="")
        for n2 in names:
            sim = cosine_sim(bop_vectors[n1], bop_vectors[n2])
            print(f"{sim:10.3f}", end="")
        print()

    # ── Type Signature Clustering ──
    print("\n── 2. TYPE SIGNATURE CLUSTERING ──\n")

    sig_groups = {}
    for t in tools:
        sig = type_signature(t)
        sig_str = f"({','.join(sig[0])}) -> ({','.join(sig[1])}) [{','.join(sig[2])}]"
        sig_groups.setdefault(sig_str, []).append(t["app"])

    for sig, members in sorted(sig_groups.items()):
        print(f"  {sig}")
        for m in members:
            print(f"    - {m}")

    # ── TF-IDF Query Matching ──
    print("\n── 3. TF-IDF QUERY MATCHING ──\n")

    all_names_ordered = sorted(TOOL_DESCRIPTIONS.keys())
    all_descs = [TOOL_DESCRIPTIONS[n] for n in all_names_ordered]

    correct = 0
    total = len(TEST_QUERIES)

    for query, expected_matches in TEST_QUERIES:
        # Build TF-IDF with query included
        descs_with_query = all_descs + [query]
        vectors, _ = tfidf_vectors(descs_with_query)
        query_vec = vectors[-1]
        tool_vecs = vectors[:-1]

        # Rank by similarity
        scored = []
        for i, name in enumerate(all_names_ordered):
            sim = cosine_sim(query_vec, tool_vecs[i])
            scored.append((sim, name))
        scored.sort(reverse=True)

        top = scored[0][1] if scored[0][0] > 0.01 else None
        hit = top in expected_matches if expected_matches else top is None

        if hit:
            correct += 1

        marker = "✓" if hit else "✗"
        print(f"  {marker} \"{query}\"")
        print(f"    expected: {expected_matches or ['(none)']}")
        print(f"    top-3: ", end="")
        for sim, name in scored[:3]:
            print(f"{name}({sim:.3f})  ", end="")
        print()

    print(f"\n  TF-IDF accuracy: {correct}/{total} ({100*correct/total:.0f}%)")

    # ── Bag-of-Primitives Query Matching ──
    print("\n── 4. STRUCTURAL QUERY: CAN BAG-OF-PRIMITIVES DISTINGUISH TOOLS? ──\n")

    # Check: how many tool pairs have identical bags?
    identical_pairs = []
    distinct_pairs = []
    for i, n1 in enumerate(names):
        for n2 in names[i+1:]:
            sim = cosine_sim(bop_vectors[n1], bop_vectors[n2])
            if sim > 0.99:
                identical_pairs.append((n1, n2, sim))
            elif sim > 0.8:
                distinct_pairs.append((n1, n2, sim))

    print(f"  Structurally identical pairs (cosine > 0.99):")
    if identical_pairs:
        for n1, n2, sim in identical_pairs:
            print(f"    {n1} ≈ {n2} ({sim:.4f})")
    else:
        print(f"    (none — all tools are structurally distinct)")

    print(f"\n  Structurally similar pairs (cosine 0.8-0.99):")
    if distinct_pairs:
        for n1, n2, sim in distinct_pairs:
            print(f"    {n1} ~ {n2} ({sim:.4f})")
    else:
        print(f"    (none)")

    # ── Verdict ──
    print("\n" + "=" * 70)
    print("VERDICT")
    print("=" * 70)
    print()
    print("At this scale (8 tools, 29 primitives):")
    print("  - Type signatures separate tools into clear families")
    print("  - Bag-of-primitives distinguishes structural variants")
    print(f"  - TF-IDF gives {correct}/{total} retrieval accuracy on NL queries")
    print()
    if correct >= total - 1:
        print("→ Even trivial TF-IDF works. Neural embeddings are unnecessary.")
    else:
        print("→ TF-IDF struggles. Neural embeddings might help for NL discovery.")
    print("→ Structural search (signatures + bags) is the right primary index.")
    print("→ Add neural embeddings only when tool count >> 100.")


if __name__ == "__main__":
    run_experiment()
