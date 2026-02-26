mod compose;
mod emit;
mod grammar;
mod memory;
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
    /// Query and manage tool memory graph
    Memory {
        #[command(subcommand)]
        action: MemoryAction,
    },
}

#[derive(Subcommand)]
enum MemoryAction {
    /// Search memory for tools matching a query (fuzzy match on goal/app/tags)
    Search {
        /// Search query (natural language or keywords)
        query: String,
        /// Max results to return
        #[arg(short = 'n', long, default_value = "10")]
        limit: usize,
    },
    /// Show full details of a remembered tool by hash prefix
    Show {
        /// Tool hash or hash prefix
        hash: String,
    },
    /// Record a tool forge into memory with semantic context
    Record {
        /// Tool hash (must exist in tool cache)
        hash: String,
        /// Natural language goal description
        #[arg(long)]
        goal: String,
        /// Comma-separated tags for search
        #[arg(long, default_value = "")]
        tags: String,
        /// Optional notes
        #[arg(long, default_value = "")]
        notes: String,
    },
    /// Add a relationship edge between two tools
    Relate {
        /// Source tool hash
        from: String,
        /// Target tool hash
        to: String,
        /// Relationship: variant_of, pipes_to, supersedes
        rel: String,
        /// Optional note about the relationship
        #[arg(long, default_value = "")]
        note: String,
    },
    /// Show recent tool forges (newest first)
    Log {
        /// Max entries to show
        #[arg(short = 'n', long, default_value = "10")]
        limit: usize,
    },
    /// Find tools related to a given tool (by edges or shared tags)
    Related {
        /// Tool hash or hash prefix
        hash: String,
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

        Cmd::Memory { action } => {
            match action {
                MemoryAction::Search { query, limit } => {
                    let results = memory::search(&query);
                    if results.is_empty() {
                        eprintln!("no matches for: {query}");
                        // Output empty JSON array for machine consumption
                        println!("[]");
                    } else {
                        let items: Vec<serde_json::Value> = results
                            .into_iter()
                            .take(limit)
                            .map(|(hash, entry, score)| {
                                let binary = cache::bin_path(&hash);
                                let exists = std::path::Path::new(&binary).exists();
                                serde_json::json!({
                                    "hash": &hash[..12.min(hash.len())],
                                    "hash_full": hash,
                                    "app": entry.app,
                                    "goal": entry.goal,
                                    "io": entry.io,
                                    "tags": entry.tags,
                                    "pattern": entry.pattern,
                                    "use_count": entry.use_count,
                                    "last_used": entry.last_used,
                                    "status": entry.status,
                                    "binary_exists": exists,
                                    "score": score,
                                })
                            })
                            .collect();
                        println!("{}", serde_json::to_string_pretty(&items).unwrap());
                    }
                }

                MemoryAction::Show { hash } => {
                    let state = memory::load();
                    let found = state
                        .entries
                        .iter()
                        .find(|(h, _)| h.starts_with(&hash));

                    match found {
                        Some((full_hash, entry)) => {
                            let binary = cache::bin_path(full_hash);
                            let manifest = if std::path::Path::new(&binary).exists() {
                                cache::read_trailer(&binary)
                                    .ok()
                                    .and_then(|v| v["manifest"].as_str().map(|s| s.to_string()))
                            } else {
                                None
                            };

                            // Find edges involving this tool
                            let edges: Vec<&memory::MemoryEdge> = state
                                .edges
                                .iter()
                                .filter(|e| e.from == *full_hash || e.to == *full_hash)
                                .collect();

                            let mut result = serde_json::json!({
                                "hash": full_hash,
                                "app": entry.app,
                                "goal": entry.goal,
                                "tags": entry.tags,
                                "pattern": entry.pattern,
                                "io": entry.io,
                                "caps": entry.caps,
                                "created": entry.created,
                                "last_used": entry.last_used,
                                "use_count": entry.use_count,
                                "status": entry.status,
                                "notes": entry.notes,
                                "binary": binary,
                                "binary_exists": std::path::Path::new(&binary).exists(),
                            });

                            if let Some(m) = manifest {
                                result["manifest"] = serde_json::Value::String(m);
                            }

                            if !edges.is_empty() {
                                result["edges"] = serde_json::json!(edges
                                    .iter()
                                    .map(|e| serde_json::json!({
                                        "from": &e.from[..12.min(e.from.len())],
                                        "to": &e.to[..12.min(e.to.len())],
                                        "rel": e.rel,
                                        "note": e.note,
                                    }))
                                    .collect::<Vec<_>>());
                            }

                            println!("{}", serde_json::to_string_pretty(&result).unwrap());
                        }
                        None => {
                            eprintln!("not in memory: {hash}");
                            eprintln!("hint: record it with: esc memory record <hash> --goal \"...\"");
                            std::process::exit(1);
                        }
                    }
                }

                MemoryAction::Record { hash, goal, tags, notes } => {
                    // Resolve hash — must exist in tool cache
                    let tools = cache::list_tools();
                    let found = tools.iter().find(|t| t.hash.starts_with(&hash));

                    match found {
                        Some(tool) => {
                            let tag_list: Vec<String> = if tags.is_empty() {
                                Vec::new()
                            } else {
                                tags.split(',').map(|s| s.trim().to_string()).filter(|s| !s.is_empty()).collect()
                            };

                            // Auto-generate pattern from manifest
                            let pattern = match compose::validate(&tool.manifest_source, &registry) {
                                Ok(comp) => memory::extract_pattern(&comp.nodes),
                                Err(_) => String::new(),
                            };

                            let io = memory::io_signature(&tool.inputs, &tool.outputs);

                            memory::record(
                                &tool.hash,
                                &tool.app,
                                &goal,
                                &tag_list,
                                &pattern,
                                &io,
                                &tool.capabilities,
                                &notes,
                            );

                            eprintln!("recorded: {} [{}] — {}", tool.app, &tool.hash[..12], goal);
                        }
                        None => {
                            eprintln!("tool not found in cache: {hash}");
                            eprintln!("hint: compile with --store first, then record");
                            std::process::exit(1);
                        }
                    }
                }

                MemoryAction::Relate { from, to, rel, note } => {
                    let valid_rels = ["variant_of", "pipes_to", "supersedes"];
                    if !valid_rels.contains(&rel.as_str()) {
                        eprintln!("unknown relationship: {rel}");
                        eprintln!("valid: {}", valid_rels.join(", "));
                        std::process::exit(1);
                    }

                    // Resolve hash prefixes
                    let state = memory::load();
                    let from_full = state.entries.keys().find(|k| k.starts_with(&from));
                    let to_full = state.entries.keys().find(|k| k.starts_with(&to));

                    match (from_full, to_full) {
                        (Some(f), Some(t)) => {
                            let f = f.clone();
                            let t = t.clone();
                            memory::relate(&f, &t, &rel, &note);
                            eprintln!("edge: {} --[{}]--> {}", &f[..12], rel, &t[..12]);
                        }
                        (None, _) => {
                            eprintln!("source not in memory: {from}");
                            std::process::exit(1);
                        }
                        (_, None) => {
                            eprintln!("target not in memory: {to}");
                            std::process::exit(1);
                        }
                    }
                }

                MemoryAction::Log { limit } => {
                    let entries = memory::log(limit);
                    if entries.is_empty() {
                        eprintln!("memory is empty");
                        println!("[]");
                    } else {
                        let items: Vec<serde_json::Value> = entries
                            .into_iter()
                            .map(|(hash, entry)| {
                                serde_json::json!({
                                    "hash": &hash[..12.min(hash.len())],
                                    "app": entry.app,
                                    "goal": entry.goal,
                                    "io": entry.io,
                                    "use_count": entry.use_count,
                                    "last_used": entry.last_used,
                                })
                            })
                            .collect();
                        println!("{}", serde_json::to_string_pretty(&items).unwrap());
                    }
                }

                MemoryAction::Related { hash } => {
                    let results = memory::related(&hash);
                    if results.is_empty() {
                        eprintln!("no related tools for: {hash}");
                        println!("[]");
                    } else {
                        let items: Vec<serde_json::Value> = results
                            .into_iter()
                            .map(|(h, entry, rel)| {
                                serde_json::json!({
                                    "hash": &h[..12.min(h.len())],
                                    "app": entry.app,
                                    "goal": entry.goal,
                                    "io": entry.io,
                                    "relationship": rel,
                                })
                            })
                            .collect();
                        println!("{}", serde_json::to_string_pretty(&items).unwrap());
                    }
                }
            }
        }
    }
}
