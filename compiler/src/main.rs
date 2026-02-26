mod compose;
mod emit;
mod grammar;
mod primitive;
mod cache;

use clap::{Parser, Subcommand};
use std::fs;
use std::io::Write;
use std::path::Path;
use std::process::Command;

#[derive(Parser)]
#[command(
    name = "esc",
    about = "el-stupido compiler — composable primitives for program generation"
)]
struct Cli {
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Compose primitives from a manifest and compile to native binary
    Compose {
        /// Path to composition manifest (JSON, .esc, or tape)
        manifest: String,
        /// Output binary path
        #[arg(short, long, default_value = "a.out")]
        output: String,
        /// Machine-readable JSON output (for LLM integration)
        #[arg(long)]
        machine: bool,
        /// Store compiled binary in tool cache (~/.esc/)
        #[arg(long)]
        store: bool,
    },
    /// Print generated Rust source without compiling
    Expand {
        /// Path to composition manifest (JSON, .esc, or tape)
        manifest: String,
    },
    /// Print GBNF grammar for constrained LLM output
    Grammar,
    /// List all available primitives
    Primitives {
        /// Machine-readable JSON output
        #[arg(long)]
        machine: bool,
    },
    /// Manage the tool cache
    Tools {
        #[command(subcommand)]
        action: ToolsAction,
    },
    /// Inspect a compiled binary's metadata trailer
    Inspect {
        /// Path to a compiled binary with metadata trailer
        binary: String,
    },
}

#[derive(Subcommand)]
enum ToolsAction {
    /// List all cached tools
    List,
    /// Show details of a cached tool
    Info {
        /// Tool hash or name
        query: String,
    },
    /// Remove a tool from the cache
    Forget {
        /// Tool hash or name
        query: String,
    },
    /// Remove all cached tools
    Clear,
}

fn main() {
    let cli = Cli::parse();
    let registry = primitive::Registry::new();

    match cli.cmd {
        Cmd::Compose { manifest, output, machine, store } => {
            let json = match fs::read_to_string(&manifest) {
                Ok(s) => s,
                Err(e) => {
                    if machine {
                        let err = serde_json::json!({
                            "status": "error",
                            "error": {
                                "kind": "io",
                                "message": format!("cannot read {manifest}: {e}"),
                                "hint": "check that the file path exists and is readable"
                            }
                        });
                        println!("{}", serde_json::to_string_pretty(&err).unwrap());
                    } else {
                        eprintln!("error: cannot read {manifest}: {e}");
                    }
                    std::process::exit(1);
                }
            };

            let comp = match compose::validate(&json, &registry) {
                Ok(c) => c,
                Err(e) => {
                    if machine {
                        println!("{}", serde_json::to_string_pretty(&e.to_machine_json()).unwrap());
                    } else {
                        eprintln!("error: {e}");
                    }
                    std::process::exit(1);
                }
            };

            // Check cache before compiling
            let canonical = compose::canonical_tape(&comp);
            let hash = cache::sha256_hex(&canonical);

            if store {
                if let Some(cached) = cache::lookup(&hash) {
                    if machine {
                        let (inputs, outputs) = compose::extract_io_contract(&comp);
                        let result = serde_json::json!({
                            "status": "ok",
                            "app": comp.app,
                            "binary": cached.binary_path,
                            "hash": hash,
                            "cached": true,
                            "capabilities": comp.capabilities,
                            "inputs": inputs,
                            "outputs": outputs,
                        });
                        println!("{}", serde_json::to_string_pretty(&result).unwrap());
                    } else {
                        eprintln!("ok: {} -> {} (cached, hash: {})", manifest, cached.binary_path, &hash[..12]);
                    }
                    return;
                }
            }

            let rust_source = emit::emit_rust(&comp);

            // Determine output path
            let final_output = if store {
                cache::ensure_dirs();
                cache::bin_path(&hash)
            } else {
                output.clone()
            };

            let output_path = Path::new(&final_output);
            let tmp_parent = output_path.parent().unwrap_or_else(|| Path::new("."));
            let tmp = tmp_parent.join(format!("esc_{}_tmp.rs", std::process::id()));
            {
                let mut f = fs::File::create(&tmp).expect("cannot create temp file");
                f.write_all(rust_source.as_bytes())
                    .expect("cannot write temp file");
            }

            let status = Command::new("rustc")
                .arg("--edition")
                .arg("2021")
                .arg("-C")
                .arg("opt-level=2")
                .arg("-C")
                .arg("strip=symbols")
                .arg("-o")
                .arg(&final_output)
                .arg(&tmp)
                .status();

            let _ = fs::remove_file(&tmp);

            match status {
                Ok(s) if s.success() => {
                    let meta = fs::metadata(&final_output).ok();
                    let binary_size = meta.map(|m| m.len()).unwrap_or(0);
                    let rs_len = rust_source.len();

                    // Append metadata trailer to binary
                    let (inputs, outputs) = compose::extract_io_contract(&comp);
                    let trailer = serde_json::json!({
                        "app": comp.app,
                        "hash": hash,
                        "capabilities": comp.capabilities,
                        "inputs": inputs,
                        "outputs": outputs,
                        "manifest": json.trim(),
                    });
                    let trailer_bytes = serde_json::to_vec(&trailer).unwrap();
                    {
                        let mut f = fs::OpenOptions::new().append(true).open(&final_output).expect("cannot append trailer");
                        f.write_all(&trailer_bytes).expect("cannot write trailer");
                        f.write_all(&(trailer_bytes.len() as u64).to_le_bytes()).expect("cannot write trailer len");
                        f.write_all(b"ESCMETA\0").expect("cannot write trailer magic");
                    }

                    // Store in cache if requested
                    if store {
                        cache::register(&cache::CachedTool {
                            hash: hash.clone(),
                            app: comp.app.clone(),
                            binary_path: final_output.clone(),
                            manifest_source: json.trim().to_string(),
                            canonical_tape: canonical.clone(),
                            capabilities: comp.capabilities.clone(),
                            inputs: inputs.clone(),
                            outputs: outputs.clone(),
                            binary_size,
                            rust_size: rs_len as u64,
                        });
                    }

                    if machine {
                        let result = serde_json::json!({
                            "status": "ok",
                            "app": comp.app,
                            "binary": final_output,
                            "binary_size": binary_size,
                            "rust_size": rs_len,
                            "hash": hash,
                            "cached": false,
                            "capabilities": comp.capabilities,
                            "inputs": inputs,
                            "outputs": outputs,
                        });
                        println!("{}", serde_json::to_string_pretty(&result).unwrap());
                    } else {
                        eprintln!(
                            "ok: {} -> {} ({rs_len} bytes Rust -> {binary_size} bytes binary, hash: {})",
                            manifest, final_output, &hash[..12]
                        );
                    }
                }
                Ok(s) => {
                    if machine {
                        let err = serde_json::json!({
                            "status": "error",
                            "error": {
                                "kind": "rustc",
                                "message": format!("rustc exited with {s}"),
                                "hint": "internal compiler error — this is a bug in esc"
                            }
                        });
                        println!("{}", serde_json::to_string_pretty(&err).unwrap());
                    } else {
                        eprintln!("error: rustc exited with {s}");
                    }
                    std::process::exit(1);
                }
                Err(e) => {
                    if machine {
                        let err = serde_json::json!({
                            "status": "error",
                            "error": {
                                "kind": "rustc_missing",
                                "message": format!("cannot run rustc: {e}"),
                                "hint": "ensure rustc is installed and in PATH"
                            }
                        });
                        println!("{}", serde_json::to_string_pretty(&err).unwrap());
                    } else {
                        eprintln!("error: cannot run rustc: {e}");
                    }
                    std::process::exit(1);
                }
            }
        }

        Cmd::Expand { manifest } => {
            let json = match fs::read_to_string(&manifest) {
                Ok(s) => s,
                Err(e) => {
                    eprintln!("error: cannot read {manifest}: {e}");
                    std::process::exit(1);
                }
            };

            let comp = match compose::validate(&json, &registry) {
                Ok(c) => c,
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            };

            print!("{}", emit::emit_rust(&comp));
        }

        Cmd::Grammar => {
            print!("{}", grammar::generate_gbnf(&registry));
        }

        Cmd::Primitives { machine } => {
            let mut prims: Vec<_> = registry.all().collect();
            prims.sort_by_key(|p| p.id);

            if machine {
                let list: Vec<serde_json::Value> = prims.iter().map(|p| {
                    serde_json::json!({
                        "id": p.id,
                        "description": p.description,
                        "params": p.params.iter().map(|param| serde_json::json!({
                            "name": param.name,
                            "type": param.ty.label(),
                            "required": param.required,
                        })).collect::<Vec<_>>(),
                        "binds": p.binds.iter().map(|b| serde_json::json!({
                            "name": b.name,
                            "type": b.capability,
                            "required": b.required,
                        })).collect::<Vec<_>>(),
                        "provides": p.provides,
                        "effects": p.effects,
                    })
                }).collect();
                println!("{}", serde_json::to_string_pretty(&list).unwrap());
            } else {
                for p in prims {
                    println!("  {} — {}", p.id, p.description);
                    for param in &p.params {
                        let req = if param.required { "required" } else { "optional" };
                        println!("    {}: {} ({})", param.name, param.ty.label(), req);
                    }
                    for bind in &p.binds {
                        let req = if bind.required { "required" } else { "optional" };
                        println!("    bind {} -> {} ({})", bind.name, bind.capability, req);
                    }
                    if !p.provides.is_empty() {
                        println!("    provides: [{}]", p.provides.join(", "));
                    }
                    if !p.requires.is_empty() {
                        println!("    requires: [{}]", p.requires.join(", "));
                    }
                    if !p.effects.is_empty() {
                        println!("    effects: [{}]", p.effects.join(", "));
                    }
                    println!();
                }
            }
        }

        Cmd::Tools { action } => {
            match action {
                ToolsAction::List => {
                    let tools = cache::list_tools();
                    if tools.is_empty() {
                        eprintln!("no cached tools");
                    } else {
                        for t in &tools {
                            println!("  {} [{}]  caps:[{}]  in:{} out:{}  {}",
                                t.app,
                                &t.hash[..12],
                                t.capabilities.join(","),
                                t.inputs.len(),
                                t.outputs.len(),
                                t.binary_path,
                            );
                        }
                        eprintln!("{} tools cached", tools.len());
                    }
                }
                ToolsAction::Info { query } => {
                    let tools = cache::list_tools();
                    let found = tools.iter().find(|t| t.hash.starts_with(&query) || t.app == query);
                    match found {
                        Some(t) => {
                            println!("{}", serde_json::to_string_pretty(&serde_json::json!({
                                "app": t.app,
                                "hash": t.hash,
                                "binary": t.binary_path,
                                "capabilities": t.capabilities,
                                "inputs": t.inputs,
                                "outputs": t.outputs,
                                "binary_size": t.binary_size,
                                "rust_size": t.rust_size,
                                "canonical_tape": t.canonical_tape,
                            })).unwrap());
                        }
                        None => {
                            eprintln!("tool not found: {query}");
                            std::process::exit(1);
                        }
                    }
                }
                ToolsAction::Forget { query } => {
                    if cache::forget(&query) {
                        eprintln!("removed: {query}");
                    } else {
                        eprintln!("tool not found: {query}");
                        std::process::exit(1);
                    }
                }
                ToolsAction::Clear => {
                    let count = cache::clear();
                    eprintln!("cleared {count} tools");
                }
            }
        }

        Cmd::Inspect { binary } => {
            match cache::read_trailer(&binary) {
                Ok(meta) => {
                    println!("{}", serde_json::to_string_pretty(&meta).unwrap());
                }
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            }
        }
    }
}
