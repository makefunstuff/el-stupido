use crate::compose;
use crate::emit;
use crate::primitive::Registry;

use std::fs;
use std::io::{self, BufRead, Write};
use std::process::Command;

/// Build the system prompt with primitive catalog (compact, no grammar).
fn system_prompt(registry: &Registry) -> String {
    let mut prims_text = String::new();
    let mut prims: Vec<_> = registry.all().collect();
    prims.sort_by_key(|p| p.id);

    for p in &prims {
        // Compact one-liner per primitive
        let mut sig = String::new();
        for param in &p.params {
            sig.push_str(&format!(" param.{}:{}", param.name, param.ty.label()));
        }
        for bind in &p.binds {
            sig.push_str(&format!(" bind.{}->{}", bind.name, bind.capability));
        }
        if !p.provides.is_empty() {
            sig.push_str(&format!(" -> {}", p.provides.join(",")));
        }
        prims_text.push_str(&format!(
            "  {} —{}{}\n",
            p.id,
            if sig.is_empty() { "" } else { "" },
            sig
        ));
    }

    format!(
        r#"You output JSON only. No explanation. No markdown.

Given a request, produce a JSON program manifest.

Example — add two numbers and print:
{{"app":"sum","nodes":[
{{"id":"a","use":"const_num","params":{{"value":17}}}},
{{"id":"b","use":"const_num","params":{{"value":25}}}},
{{"id":"s","use":"add","bind":{{"lhs":"a","rhs":"b"}}}},
{{"id":"o","use":"print_num","bind":{{"value":"s"}}}}
]}}

Example — run a shell command and print:
{{"app":"demo","nodes":[
{{"id":"c","use":"shell","params":{{"cmd":"hostname"}}}},
{{"id":"o","use":"print_str","bind":{{"value":"c"}}}}
]}}

Example — read a file and print:
{{"app":"reader","nodes":[
{{"id":"f","use":"read_file","params":{{"path":"/etc/hostname"}}}},
{{"id":"o","use":"print_str","bind":{{"value":"f"}}}}
]}}

Rules:
- "id": unique name per node
- "use": primitive name from list below
- "params": static values (numbers or strings)
- "bind": connect to a prior node's id
- Nodes execute in order. Binds must point to earlier nodes.
- ONLY use primitives from the list below. Never invent new ones.
- For system info (processes, memory, disk, network, etc), use "shell" with the right command.
- When in doubt, use "shell" with a Linux command.

Primitives:
{prims_text}"#
    )
}

/// Call an OpenAI-compatible chat completion API via curl.
fn call_llm(
    api_base: &str,
    api_key: &str,
    model: &str,
    system: &str,
    user_msg: &str,
) -> Result<String, String> {
    let request = serde_json::json!({
        "model": model,
        "messages": [
            { "role": "system", "content": system },
            { "role": "user", "content": user_msg }
        ],
        "temperature": 0.2,
        "max_tokens": 2048,
    });

    let url = format!("{}/chat/completions", api_base.trim_end_matches('/'));
    let request_json = serde_json::to_string(&request).map_err(|e| format!("json error: {e}"))?;

    let mut cmd = Command::new("curl");
    cmd.arg("-s")
        .arg("-X")
        .arg("POST")
        .arg(&url)
        .arg("-H")
        .arg("Content-Type: application/json")
        .arg("-d")
        .arg(&request_json);

    if !api_key.is_empty() {
        cmd.arg("-H")
            .arg(format!("Authorization: Bearer {api_key}"));
    }

    let output = cmd.output().map_err(|e| format!("cannot run curl: {e}"))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(format!("curl failed: {stderr}"));
    }

    let body = String::from_utf8_lossy(&output.stdout).to_string();
    let parsed: serde_json::Value = serde_json::from_str(&body)
        .map_err(|e| format!("cannot parse LLM response: {e}\nbody: {body}"))?;

    // Extract content from OpenAI-format response
    let content = parsed["choices"][0]["message"]["content"]
        .as_str()
        .ok_or_else(|| {
            // Check for error message
            if let Some(err) = parsed["error"]["message"].as_str() {
                format!("LLM API error: {err}")
            } else {
                format!(
                    "unexpected response format: {}",
                    serde_json::to_string_pretty(&parsed).unwrap_or_default()
                )
            }
        })?;

    Ok(content.to_string())
}

/// Extract JSON from LLM response (strip markdown fences, leading text, etc.)
fn extract_json(response: &str) -> Option<String> {
    let trimmed = response.trim();

    // Direct JSON
    if trimmed.starts_with('{') {
        return Some(trimmed.to_string());
    }

    // Markdown code fence: ```json ... ``` or ``` ... ```
    if let Some(start) = trimmed.find("```") {
        let after_fence = &trimmed[start + 3..];
        // Skip language tag (json, JSON, etc.)
        let content_start = if after_fence.starts_with("json") || after_fence.starts_with("JSON") {
            after_fence.find('\n').map(|i| i + 1).unwrap_or(0)
        } else if after_fence.starts_with('\n') {
            1
        } else {
            after_fence.find('\n').map(|i| i + 1).unwrap_or(0)
        };

        let content = &after_fence[content_start..];
        if let Some(end) = content.find("```") {
            let json = content[..end].trim();
            if json.starts_with('{') {
                return Some(json.to_string());
            }
        }
    }

    // Last resort: find first { and last }
    if let (Some(start), Some(end)) = (trimmed.find('{'), trimmed.rfind('}')) {
        if start < end {
            return Some(trimmed[start..=end].to_string());
        }
    }

    None
}

/// Compose and run a manifest, returning (success, output).
fn compose_and_run(
    manifest_json: &str,
    registry: &Registry,
    verbose: bool,
) -> Result<String, String> {
    // Validate (includes repair pass)
    let comp =
        compose::validate(manifest_json, registry).map_err(|e| format!("compose error: {e}"))?;

    // Generate Rust source
    let rust_source = emit::emit_rust(&comp);

    // Write temp files
    let tmp_rs = format!("/tmp/esc_assist_{}.rs", std::process::id());
    let tmp_bin = format!("/tmp/esc_assist_{}", std::process::id());

    fs::write(&tmp_rs, &rust_source).map_err(|e| format!("cannot write temp: {e}"))?;

    if verbose {
        eprintln!("  compiling {} nodes...", comp.nodes.len());
    }

    // Compile
    let compile = Command::new("rustc")
        .arg("--edition")
        .arg("2021")
        .arg("-C")
        .arg("opt-level=2")
        .arg("-C")
        .arg("strip=symbols")
        .arg("-o")
        .arg(&tmp_bin)
        .arg(&tmp_rs)
        .output()
        .map_err(|e| format!("cannot run rustc: {e}"))?;

    let _ = fs::remove_file(&tmp_rs);

    if !compile.status.success() {
        let stderr = String::from_utf8_lossy(&compile.stderr);
        return Err(format!("rustc failed:\n{stderr}"));
    }

    if verbose {
        let size = fs::metadata(&tmp_bin).map(|m| m.len()).unwrap_or(0);
        eprintln!("  compiled ({} bytes), running...", size);
    }

    // Run
    let run = Command::new(&tmp_bin)
        .output()
        .map_err(|e| format!("cannot run binary: {e}"))?;

    let _ = fs::remove_file(&tmp_bin);

    let stdout = String::from_utf8_lossy(&run.stdout).to_string();
    let stderr = String::from_utf8_lossy(&run.stderr).to_string();

    if !run.status.success() {
        return Err(format!(
            "program exited with {}:\nstdout: {stdout}\nstderr: {stderr}",
            run.status
        ));
    }

    if !stderr.is_empty() {
        Ok(format!("{stdout}\n(stderr: {stderr})"))
    } else {
        Ok(stdout)
    }
}

/// Run the interactive REPL.
pub fn run_repl(registry: &Registry, model: &str, api_base: &str, api_key: &str, verbose: bool) {
    let system = system_prompt(registry);

    eprintln!("esc assist — type a request, get a program");
    eprintln!("  model: {model}");
    eprintln!("  api: {api_base}");
    if verbose {
        eprintln!(
            "  system prompt: {} chars, {} primitives",
            system.len(),
            registry.all().count()
        );
    }
    eprintln!("  type 'quit' or Ctrl-D to exit\n");

    let stdin = io::stdin();
    let mut stdout = io::stdout();

    loop {
        // Prompt
        eprint!("\x1b[1;36mesc>\x1b[0m ");
        let _ = io::stderr().flush();

        // Read line
        let mut line = String::new();
        match stdin.lock().read_line(&mut line) {
            Ok(0) => {
                eprintln!("\nbye");
                break;
            }
            Ok(_) => {}
            Err(e) => {
                eprintln!("read error: {e}");
                break;
            }
        }

        let prompt = line.trim();
        if prompt.is_empty() {
            continue;
        }
        if prompt == "quit" || prompt == "exit" || prompt == "q" {
            eprintln!("bye");
            break;
        }

        // Special commands
        if prompt == "primitives" || prompt == "prims" {
            let mut prims: Vec<_> = registry.all().collect();
            prims.sort_by_key(|p| p.id);
            for p in prims {
                println!("  {} — {}", p.id, p.description);
            }
            continue;
        }

        // Call LLM
        eprint!("\x1b[90m  thinking...\x1b[0m");
        let _ = io::stderr().flush();

        let response = match call_llm(api_base, api_key, model, &system, prompt) {
            Ok(r) => r,
            Err(e) => {
                eprintln!("\r\x1b[K\x1b[31m  error: {e}\x1b[0m");
                continue;
            }
        };

        eprint!("\r\x1b[K"); // clear "thinking..."
        let _ = io::stderr().flush();

        if verbose {
            eprintln!(
                "\x1b[90m  LLM response ({} chars):\n{response}\x1b[0m",
                response.len()
            );
        }

        // Extract JSON manifest
        let manifest = match extract_json(&response) {
            Some(j) => j,
            None => {
                eprintln!("\x1b[31m  error: no JSON manifest in response\x1b[0m");
                if verbose {
                    eprintln!("\x1b[90m  raw: {response}\x1b[0m");
                }
                continue;
            }
        };

        if verbose {
            eprintln!("\x1b[90m  manifest:\n{manifest}\x1b[0m");
        }

        // Compose and run
        match compose_and_run(&manifest, registry, verbose) {
            Ok(output) => {
                print!("{output}");
                let _ = stdout.flush();
            }
            Err(e) => {
                eprintln!("\x1b[31m  {e}\x1b[0m");
            }
        }
    }
}

/// Run a single prompt (non-interactive mode).
pub fn run_once(
    registry: &Registry,
    model: &str,
    api_base: &str,
    api_key: &str,
    prompt: &str,
    verbose: bool,
) {
    let system = system_prompt(registry);

    if verbose {
        eprintln!("model: {model}");
        eprintln!("api: {api_base}");
        eprintln!("prompt: {prompt}");
    }

    let response = match call_llm(api_base, api_key, model, &system, prompt) {
        Ok(r) => r,
        Err(e) => {
            eprintln!("error: {e}");
            std::process::exit(1);
        }
    };

    if verbose {
        eprintln!("LLM response ({} chars):\n{response}", response.len());
    }

    let manifest = match extract_json(&response) {
        Some(j) => j,
        None => {
            eprintln!("error: no JSON manifest in response");
            eprintln!("raw: {response}");
            std::process::exit(1);
        }
    };

    if verbose {
        eprintln!("manifest:\n{manifest}");
    }

    match compose_and_run(&manifest, registry, verbose) {
        Ok(output) => {
            print!("{output}");
        }
        Err(e) => {
            eprintln!("error: {e}");
            std::process::exit(1);
        }
    }
}
