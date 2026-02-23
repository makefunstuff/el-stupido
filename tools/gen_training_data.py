#!/usr/bin/env python3
"""Generate synthetic training data for el-stupido codebook LLM.

Produces CSV with (instruction, input, output) columns.
TinyLLM fine-tuning expects this format.

Usage:
    python3 tools/gen_training_data.py --count 2000 -o data/codebook_train.csv
    python3 tools/gen_training_data.py --count 200 -o data/codebook_val.csv
"""

import csv
import random
import argparse
import os

INSTRUCTION = (
    "Convert the request into el-stupido codebook syntax. "
    "Output ONLY the codebook code, nothing else."
)

# ---- vocabulary pools ----

PORTS = [80, 443, 3000, 4000, 5000, 8000, 8080, 8888, 9000, 9090, 9999]

WEB_TEXTS = {
    "/": [
        "hello from el-stupido!",
        "welcome!",
        "home page",
        "hello world",
        "it works!",
        "server is running",
        "main page",
        "index",
        "hi there",
        "greetings",
    ],
    "/about": [
        "about us",
        "about this server",
        "el-stupido web server",
        "a tiny web server",
        "built with el-stupido",
        "about page",
    ],
    "/status": [
        "ok",
        "running",
        "healthy",
        "alive",
        "up",
        "all systems go",
    ],
    "/health": [
        "ok",
        "healthy",
        "alive",
        "up",
    ],
    "/api": [
        "api v1",
        "json api",
        "rest endpoint",
    ],
    "/hello": [
        "hello!",
        "hi!",
        "hey there",
        "hello world",
    ],
    "/version": [
        "v1.0",
        "1.0.0",
        "v0.1",
        "0.0.1",
    ],
}

CRUD_NAMES = [
    "guestbook",
    "todo",
    "note",
    "message",
    "post",
    "comment",
    "user",
    "task",
    "item",
    "entry",
    "bookmark",
    "link",
]

CRUD_FIELDS = {
    "guestbook": [("name", "msg+"), ("author", "text+")],
    "todo": [("title", "done"), ("task", "status")],
    "note": [("title", "body+"), ("name", "content+")],
    "message": [("from", "text+"), ("sender", "body+")],
    "post": [("title", "body+"), ("author", "content+")],
    "comment": [("user", "text+"), ("author", "body+")],
    "user": [("name", "email"), ("username", "role")],
    "task": [("title", "status"), ("name", "priority")],
    "item": [("name", "price"), ("title", "desc")],
    "entry": [("title", "body+"), ("key", "value")],
    "bookmark": [("title", "url"), ("name", "link")],
    "link": [("title", "url"), ("name", "href")],
}

CLI_TOOLS = [
    (
        "grep",
        "search files for patterns",
        [
            ("verbose", "v", "enable verbose output"),
            ("count", "c", "show count only"),
            ("ignore_case", "i", "case insensitive"),
        ],
        [
            ("pattern", "regex pattern"),
            ("file", "file to search"),
        ],
    ),
    (
        "wc",
        "count words in files",
        [
            ("lines", "l", "count lines only"),
            ("words", "w", "count words only"),
            ("bytes", "b", "count bytes"),
        ],
        [
            ("file", "input file"),
        ],
    ),
    (
        "cat",
        "concatenate files",
        [
            ("number", "n", "number lines"),
            ("blank", "b", "skip blank lines"),
        ],
        [
            ("file", "file to read"),
        ],
    ),
    (
        "head",
        "show first lines",
        [
            ("quiet", "q", "suppress headers"),
        ],
        [
            ("count", "number of lines"),
            ("file", "input file"),
        ],
    ),
    (
        "find",
        "find files by name",
        [
            ("recursive", "r", "search recursively"),
            ("hidden", "h", "include hidden files"),
        ],
        [
            ("pattern", "file pattern"),
            ("dir", "directory to search"),
        ],
    ),
    (
        "sort",
        "sort lines of text",
        [
            ("reverse", "r", "reverse sort"),
            ("numeric", "n", "numeric sort"),
            ("unique", "u", "unique lines only"),
        ],
        [
            ("file", "input file"),
        ],
    ),
    (
        "calc",
        "simple calculator",
        [
            ("verbose", "v", "show steps"),
        ],
        [
            ("expr", "math expression"),
        ],
    ),
    (
        "hash",
        "compute file hash",
        [
            ("md5", "m", "use md5"),
            ("sha256", "s", "use sha256"),
        ],
        [
            ("file", "file to hash"),
        ],
    ),
    (
        "serve",
        "serve static files",
        [
            ("verbose", "v", "enable logging"),
        ],
        [
            ("port", "port number"),
            ("dir", "directory to serve"),
        ],
    ),
    (
        "ping",
        "check host availability",
        [
            ("verbose", "v", "show details"),
            ("count", "c", "ping count"),
        ],
        [
            ("host", "hostname or IP"),
        ],
    ),
]

REST_MODELS = [
    ("user", ["name", "email"]),
    ("user", ["username", "email", "role"]),
    ("post", ["title", "body", "author"]),
    ("todo", ["title", "done"]),
    ("task", ["title", "status", "priority"]),
    ("note", ["title", "content"]),
    ("product", ["name", "price"]),
    ("item", ["name", "desc", "price"]),
    ("comment", ["user", "text"]),
    ("message", ["from", "to", "body"]),
    ("bookmark", ["title", "url"]),
    ("event", ["name", "date", "location"]),
]

# ---- prompt templates ----

WEB_PROMPTS = [
    # direct/imperative
    "make a web server on port {port} with {routes}",
    "create an HTTP server listening on {port} that serves {routes}",
    "build a simple server on port {port}: {routes}",
    "set up a server at port {port} serving {routes}",
    "start a web server on {port} that has {routes}",
    "spin up an HTTP server on port {port} with {routes}",
    "launch a web server at {port} that responds with {routes}",
    # request/desire
    "I want a web server on port {port} with {routes}",
    "I need a website on port {port} with {routes}",
    "I'd like a simple HTTP server on {port} serving {routes}",
    "can you make a server on port {port} with {routes}",
    "give me a web server at port {port} that has {routes}",
    # terse/shorthand
    "web server, port {port}, {routes}",
    "serve on port {port}: {routes}",
    "HTTP server on port {port} with pages: {routes}",
    "http {port}: {routes}",
    "server {port} with {routes}",
    "web {port} {routes}",
    # descriptive/verbose
    "I'm looking for a simple web server that listens on port {port} and serves {routes}",
    "please create a basic website running on port {port} that displays {routes}",
    "set up a lightweight HTTP server bound to port {port} with the following pages: {routes}",
    "deploy a web server on port {port} that handles requests for {routes}",
    # varied vocabulary
    "run a site on port {port} with {routes}",
    "host pages on port {port}: {routes}",
    "serve content on {port} with {routes}",
    "stand up an HTTP listener on {port} serving {routes}",
    "expose an endpoint on port {port} with {routes}",
]

CRUD_PROMPTS = [
    "make a {name} app with fields {fields} on port {port}",
    "create a CRUD app for {name}s with {fields}, port {port}",
    "I want a {name} web app on port {port} with {fields}",
    "build a {name} manager on port {port}, fields: {fields}",
    "{name} app, port {port}, store {fields}",
    "web app for managing {name}s on port {port} with {fields}",
    "CRUD for {name} on port {port}: {fields}",
    "simple {name} app at port {port} with {fields} fields",
    "I need a {name} tracker on port {port} with {fields}",
    "set up a {name} form on port {port} that saves {fields}",
    "create a {name} database app at port {port} storing {fields}",
    "{name} manager with {fields} on port {port}",
    "build me a {name} CRUD on port {port} ({fields})",
    "I want to manage {name}s with {fields} fields, serve on {port}",
    "full CRUD for {name}s on {port}, fields are {fields}",
    "web form for {name} entries with {fields} on port {port}",
    "data entry app for {name}s ({fields}) on port {port}",
    "persistent {name} app on {port} with {fields}",
]

CLI_PROMPTS = [
    # direct
    "make a CLI tool called {name} that {desc}",
    "create a command-line tool named {name} for {desc_verb}",
    "build a {name} command that {desc_verb}",
    "make me a {name} utility that {desc_verb}",
    # request
    "I want a CLI program called {name} to {desc}",
    "I need a terminal tool called {name} to {desc}",
    "can you create a {name} command for {desc_verb}",
    "give me a CLI {name} that {desc_verb}",
    # terse
    "CLI tool: {name} - {desc}",
    "command-line {name} tool for {desc_verb}",
    "terminal tool called {name} to {desc}",
    "cli {name} for {desc_verb}",
    "{name} tool: {desc}",
    "{name} command that {desc_verb}",
    # verbose
    "write a command-line application named {name} whose purpose is to {desc}",
    "I'd like a terminal utility called {name} that can {desc}",
    "please build a CLI program named {name} for {desc_verb}",
    "create a unix-style tool called {name} to {desc}",
    # varied vocabulary
    "shell tool {name} to {desc}",
    "console app called {name} for {desc_verb}",
    "program {name} that {desc_verb} from the terminal",
    "develop a {name} utility for {desc_verb}",
]

REST_PROMPTS = [
    # direct
    "make a REST API for {model}s on port {port} with fields {fields}",
    "create a JSON API for {model} on port {port}: {fields}",
    "build an API on port {port} for {model}s ({fields})",
    "set up a REST service at {port} for {model}s: {fields}",
    # request
    "I want a REST endpoint for {model}s at port {port} with {fields}",
    "I need an API for {model}s on port {port} with {fields}",
    "give me a JSON API to manage {model}s on port {port}, fields: {fields}",
    "can you make a REST API on port {port} for {model}s with {fields}",
    # terse
    "REST API, port {port}, model {model} with {fields}",
    "JSON API for managing {model}s on port {port}, fields: {fields}",
    "API server on port {port} for {model} with {fields}",
    "api {port} {model} {fields}",
    "rest {model} {fields} on {port}",
    "{model} api on {port}: {fields}",
    # verbose
    "create a RESTful web service on port {port} that manages {model} resources with fields {fields}",
    "I'd like a JSON REST API running on port {port} for CRUD operations on {model}s with {fields}",
    "please set up an HTTP API on port {port} that stores and retrieves {model}s with {fields}",
    "build a backend service on port {port} with a {model} model having {fields} fields",
    # varied vocabulary
    "microservice on port {port} for {model}s ({fields})",
    "backend for {model}s on {port} with {fields}",
    "CRUD API on port {port}: {model} with {fields}",
    "data service for {model}s at port {port}, {fields}",
]

# multi-model REST prompts (2 models in one API)
REST_MULTI_PROMPTS = [
    "REST API on port {port} with {model1}s ({fields1}) and {model2}s ({fields2})",
    "build an API on port {port} managing {model1}s and {model2}s",
    "JSON API on {port} for {model1}s ({fields1}) and {model2}s ({fields2})",
    "I need an API on port {port} with two models: {model1} ({fields1}) and {model2} ({fields2})",
    "create a REST service on port {port} that handles {model1}s with {fields1} and {model2}s with {fields2}",
    "API server on {port}: {model1} ({fields1}), {model2} ({fields2})",
    "backend on port {port} for {model1}s and {model2}s with their fields",
    "multi-model API on port {port}: {model1} ({fields1}) + {model2} ({fields2})",
]


def augment_prompt(prompt):
    """Randomly augment a prompt for diversity: case, filler words, reordering."""
    augmentations = [
        lambda p: p,  # identity
        lambda p: p,  # identity (weighted)
        lambda p: p.lower(),  # all lowercase
        lambda p: p[0].upper() + p[1:] if p else p,  # capitalize first
        lambda p: "please " + p,  # add politeness
        lambda p: "hey, " + p,  # casual prefix
        lambda p: p + " please",  # polite suffix
        lambda p: "quickly " + p,  # add urgency
        lambda p: p.replace("I want", "I'd like"),  # synonym
        lambda p: p.replace("create", "generate"),  # synonym
        lambda p: p.replace("make", "build"),  # synonym
        lambda p: p.replace("build", "set up"),  # synonym
    ]
    return random.choice(augmentations)(prompt)


def describe_routes(route_descs):
    """Join route descriptions in varied ways."""
    if len(route_descs) == 1:
        return route_descs[0]
    joiners = [
        lambda ds: " and ".join(ds[:2])
        if len(ds) <= 2
        else ", ".join(ds[:-1]) + " and " + ds[-1],
        lambda ds: ", ".join(ds),
        lambda ds: "; ".join(ds),
        lambda ds: " plus ".join(ds[:2])
        if len(ds) <= 2
        else ", ".join(ds[:-1]) + ", plus " + ds[-1],
    ]
    return random.choice(joiners)(route_descs)


def gen_web_example():
    """Generate a web codebook training pair."""
    port = random.choice(PORTS)

    # pick 1-4 routes
    n_routes = random.randint(1, 4)
    available = list(WEB_TEXTS.keys())
    routes = random.sample(available, min(n_routes, len(available)))

    # build codebook output
    lines = [f"use web", f"listen {port}"]
    route_descs = []
    for r in routes:
        text = random.choice(WEB_TEXTS[r])
        lines.append(f'{r} "{text}"')
        # vary how routes are described
        desc_styles = [
            f'{r} saying "{text}"',
            f'a {r} page with "{text}"',
            f'{r} that shows "{text}"',
            f'{r} returning "{text}"',
            f'"{text}" at {r}',
        ]
        route_descs.append(random.choice(desc_styles))

    output = "\n".join(lines)
    routes_str = describe_routes(route_descs)
    prompt = random.choice(WEB_PROMPTS).format(port=port, routes=routes_str)
    return augment_prompt(prompt), output


def gen_crud_example():
    """Generate a web+crud codebook training pair."""
    port = random.choice(PORTS)
    name = random.choice(CRUD_NAMES)
    fields = random.choice(CRUD_FIELDS.get(name, [("name", "value")]))

    output = f"use web\nlisten {port}\ncrud {name} {' '.join(fields)}"
    # vary field description style
    field_joiners = [
        " and ".join(fields),
        ", ".join(fields),
        "/".join(fields),
        f"{fields[0]} and {fields[1]}" if len(fields) == 2 else ", ".join(fields),
    ]
    fields_str = random.choice(field_joiners)
    prompt = random.choice(CRUD_PROMPTS).format(name=name, fields=fields_str, port=port)
    return augment_prompt(prompt), output


def gen_cli_example():
    """Generate a cli codebook training pair."""
    tool = random.choice(CLI_TOOLS)
    name, desc, flags, args = tool

    # pick subset of flags (0 to all)
    n_flags = random.randint(0, len(flags))
    sel_flags = random.sample(flags, n_flags)

    lines = [f"use cli", f'name "{name}"', f'desc "{desc}"']
    for fname, fshort, fdesc in sel_flags:
        lines.append(f'flag {fname} -{fshort} "{fdesc}"')
    for aname, adesc in args:
        lines.append(f'arg {aname} "{adesc}"')

    output = "\n".join(lines)

    desc_verb = desc.replace("count ", "counting ").replace("search ", "searching ")
    desc_verb = desc_verb.replace("sort ", "sorting ").replace("show ", "showing ")
    desc_verb = desc_verb.replace("find ", "finding ").replace("serve ", "serving ")
    desc_verb = desc_verb.replace("compute ", "computing ").replace(
        "check ", "checking "
    )

    prompt = random.choice(CLI_PROMPTS).format(
        name=name, desc=desc, desc_verb=desc_verb
    )
    return augment_prompt(prompt), output


def gen_rest_example():
    """Generate a rest codebook training pair (single model)."""
    port = random.choice(PORTS)
    model_name, fields = random.choice(REST_MODELS)

    lines = [
        f"use rest",
        f"listen {port}",
        f"model {model_name} {' '.join(fields)}",
        f"GET /{model_name}s list {model_name}",
        f"POST /{model_name}s create {model_name}",
    ]

    # optionally add health endpoint
    if random.random() > 0.5:
        lines.append(f'GET /health "ok"')

    output = "\n".join(lines)
    fields_str = ", ".join(fields)
    prompt = random.choice(REST_PROMPTS).format(
        model=model_name, fields=fields_str, port=port
    )
    return augment_prompt(prompt), output


def gen_rest_multi_example():
    """Generate a multi-model REST API training pair."""
    port = random.choice(PORTS)
    # pick 2 distinct models
    models = random.sample(REST_MODELS, 2)
    m1_name, m1_fields = models[0]
    m2_name, m2_fields = models[1]
    # ensure distinct names
    if m1_name == m2_name:
        m2_name = m2_name + "s_alt"

    lines = [
        f"use rest",
        f"listen {port}",
        f"model {m1_name} {' '.join(m1_fields)}",
        f"model {m2_name} {' '.join(m2_fields)}",
        f"GET /{m1_name}s list {m1_name}",
        f"POST /{m1_name}s create {m1_name}",
        f"GET /{m2_name}s list {m2_name}",
        f"POST /{m2_name}s create {m2_name}",
    ]
    if random.random() > 0.5:
        lines.append(f'GET /health "ok"')

    output = "\n".join(lines)
    fields1_str = ", ".join(m1_fields)
    fields2_str = ", ".join(m2_fields)
    prompt = random.choice(REST_MULTI_PROMPTS).format(
        port=port,
        model1=m1_name,
        fields1=fields1_str,
        model2=m2_name,
        fields2=fields2_str,
    )
    return augment_prompt(prompt), output


def gen_web_crud_combo():
    """Generate a web codebook with both static routes AND CRUD."""
    port = random.choice(PORTS)
    name = random.choice(CRUD_NAMES)
    fields = random.choice(CRUD_FIELDS.get(name, [("name", "value")]))

    # pick 1-2 static routes
    available = list(WEB_TEXTS.keys())
    routes = random.sample(available, random.randint(1, 2))

    lines = [f"use web", f"listen {port}"]
    route_descs = []
    for r in routes:
        text = random.choice(WEB_TEXTS[r])
        lines.append(f'{r} "{text}"')
        route_descs.append(f'{r} showing "{text}"')
    lines.append(f"crud {name} {' '.join(fields)}")

    output = "\n".join(lines)
    routes_str = describe_routes(route_descs)
    fields_str = " and ".join(fields)

    combo_prompts = [
        f"web server on port {port} with {routes_str} and a {name} CRUD with {fields_str}",
        f"website on {port}: {routes_str}, plus manage {name}s with {fields_str}",
        f"build a server on port {port} that has {routes_str} and a {name} manager ({fields_str})",
        f"HTTP server on {port} with pages {routes_str} and CRUD for {name}s ({fields_str})",
        f"server on port {port} with {routes_str} and {name} tracking ({fields_str})",
    ]
    prompt = random.choice(combo_prompts)
    return augment_prompt(prompt), output


GENERATORS = [
    (gen_web_example, 0.22),
    (gen_crud_example, 0.15),
    (gen_cli_example, 0.22),
    (gen_rest_example, 0.18),
    (gen_rest_multi_example, 0.10),
    (gen_web_crud_combo, 0.13),
]


def generate_dataset(count, seed=42):
    """Generate `count` training examples."""
    random.seed(seed)
    examples = []

    # build weighted generator list
    gens = []
    for gen_fn, weight in GENERATORS:
        gens.extend([gen_fn] * int(weight * 100))

    for _ in range(count):
        gen = random.choice(gens)
        prompt, output = gen()
        examples.append(
            {
                "instruction": INSTRUCTION,
                "input": prompt,
                "output": output,
            }
        )

    return examples


def main():
    parser = argparse.ArgumentParser(description="Generate codebook training data")
    parser.add_argument("--count", type=int, default=2000, help="Number of examples")
    parser.add_argument("--seed", type=int, default=42, help="Random seed")
    parser.add_argument("-o", "--output", default="data/codebook_train.csv")
    parser.add_argument("--preview", type=int, default=0, help="Print N examples")
    args = parser.parse_args()

    examples = generate_dataset(args.count, args.seed)

    if args.preview > 0:
        for ex in examples[: args.preview]:
            print(f"INPUT: {ex['input']}")
            print(f"OUTPUT:\n{ex['output']}")
            print("---")
        return

    os.makedirs(os.path.dirname(args.output), exist_ok=True)

    with open(args.output, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["instruction", "input", "output"])
        writer.writeheader()
        writer.writerows(examples)

    # stats
    types = {"web": 0, "cli": 0, "rest": 0, "crud": 0}
    for ex in examples:
        out = ex["output"]
        if "use rest" in out:
            types["rest"] += 1
        elif "crud " in out:
            types["crud"] += 1
        elif "use cli" in out:
            types["cli"] += 1
        elif "use web" in out:
            types["web"] += 1

    print(f"Generated {len(examples)} examples -> {args.output}")
    print(
        f"  web: {types['web']}, crud: {types['crud']}, cli: {types['cli']}, rest: {types['rest']}"
    )


if __name__ == "__main__":
    main()
