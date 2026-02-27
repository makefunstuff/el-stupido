mod atomic;
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
        /// Goal description for memory (used with --store)
        #[arg(long, default_value = "")]
        goal: String,
        /// Comma-separated tags for memory search (used with --store)
        #[arg(long, default_value = "")]
        tags: String,
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
    /// Search memory: graph-aware keyword match + edge walk + tag expansion
    Search {
        /// Search query (natural language or keywords)
        query: String,
        /// Max results to return
        #[arg(short = 'n', long, default_value = "20")]
        limit: usize,
    },
    /// Show full details of a tool or note by hash prefix
    Show {
        /// Tool or note hash (prefix match)
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
    /// Store a contextual note (discovery, decision, pattern, issue)
    Note {
        /// Note summary (one line)
        summary: String,
        /// Longer detail/explanation (optional)
        #[arg(default_value = "")]
        detail: String,
        /// Note kind: discovery, decision, pattern, issue
        #[arg(long, default_value = "discovery")]
        kind: String,
        /// Context/project area (e.g., "el-stupido", "homelab")
        #[arg(long, default_value = "")]
        context: String,
        /// Comma-separated tags
        #[arg(long, default_value = "")]
        tags: String,
    },
    /// List contextual notes with optional filters
    Notes {
        /// Filter by kind (discovery, decision, pattern, issue)
        #[arg(long)]
        kind: Option<String>,
        /// Filter by context
        #[arg(long)]
        context: Option<String>,
        /// Max entries to show
        #[arg(short = 'n', long, default_value = "20")]
        limit: usize,
    },
    /// Mark a note resolved or superseded
    Resolve {
        /// Note hash or hash prefix
        hash: String,
        /// New status: resolved, superseded
        #[arg(long, default_value = "resolved")]
        status: String,
    },
    /// Create esc schema on atomic-server (requires ESC_ATOMIC_URL + ESC_ATOMIC_KEY)
    Setup,
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
        Cmd::Compose { manifest, output, machine, store, goal, tags } => {
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
                    let (inputs, outputs) = compose::extract_io_contract(&comp);

                    // If goal or tags provided, upsert memory entry (not just touch)
                    if !goal.is_empty() || !tags.is_empty() {
                        let pattern = memory::extract_pattern(&comp.nodes);
                        let io = memory::io_signature(&inputs, &outputs);
                        let tag_list: Vec<String> = if tags.is_empty() {
                            Vec::new()
                        } else {
                            tags.split(',').map(|s| s.trim().to_string()).filter(|s| !s.is_empty()).collect()
                        };
                        memory::record(
                            &hash,
                            &comp.app,
                            &goal,
                            &tag_list,
                            &pattern,
                            &io,
                            &comp.capabilities,
                            "",
                        );
                    } else {
                        memory::touch(&hash);
                    }

                    if machine {
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

                    // Store in cache and auto-record to memory
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

                        // Auto-record to memory graph with goal/tags from CLI
                        let pattern = memory::extract_pattern(&comp.nodes);
                        let io = memory::io_signature(&inputs, &outputs);
                        let tag_list: Vec<String> = if tags.is_empty() {
                            Vec::new()
                        } else {
                            tags.split(',').map(|s| s.trim().to_string()).filter(|s| !s.is_empty()).collect()
                        };
                        memory::record(
                            &hash,
                            &comp.app,
                            &goal,
                            &tag_list,
                            &pattern,
                            &io,
                            &comp.capabilities,
                            "",
                        );
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
                    let results = memory::recall(&query, limit);
                    if results.is_empty() {
                        eprintln!("no matches for: {query}");
                    }
                    println!("{}", serde_json::to_string_pretty(&results).unwrap());
                }

                MemoryAction::Show { hash } => {
                    // Try tool first (atomic → flat-file), then note (flat-file)
                    let tool = memory::atomic_show(&hash)
                        .or_else(|| {
                            let state = memory::load();
                            state.entries.into_iter().find(|(h, _)| h.starts_with(&hash))
                        });

                    if let Some((full_hash, entry)) = tool {
                        let binary = cache::bin_path(&full_hash);
                        let manifest = if std::path::Path::new(&binary).exists() {
                            cache::read_trailer(&binary)
                                .ok()
                                .and_then(|v| v["manifest"].as_str().map(|s| s.to_string()))
                        } else {
                            None
                        };

                        let edges = memory::related(&hash);

                        let mut result = serde_json::json!({
                            "type": "tool",
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
                                .map(|(h, _, rel)| serde_json::json!({
                                    "hash": &h[..12.min(h.len())],
                                    "relationship": rel,
                                }))
                                .collect::<Vec<_>>());
                        }

                        println!("{}", serde_json::to_string_pretty(&result).unwrap());
                    } else {
                        // Try notes
                        let state = memory::load();
                        let note = state.notes.into_iter().find(|(h, _)| h.starts_with(&hash));

                        match note {
                            Some((full_hash, note)) => {
                                let result = serde_json::json!({
                                    "type": "note",
                                    "hash": full_hash,
                                    "kind": note.kind,
                                    "summary": note.summary,
                                    "detail": note.detail,
                                    "context": note.context,
                                    "tags": note.tags,
                                    "created": note.created,
                                    "status": note.status,
                                });
                                println!("{}", serde_json::to_string_pretty(&result).unwrap());
                            }
                            None => {
                                eprintln!("not in memory: {hash}");
                                std::process::exit(1);
                            }
                        }
                    }
                }

                MemoryAction::Record { hash, goal, tags } => {
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
                                "",
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

                MemoryAction::Note { summary, detail, kind, context, tags } => {
                    let valid_kinds = ["discovery", "decision", "pattern", "issue"];
                    if !valid_kinds.contains(&kind.as_str()) {
                        eprintln!("unknown kind: {kind}");
                        eprintln!("valid: {}", valid_kinds.join(", "));
                        std::process::exit(1);
                    }
                    let tag_list: Vec<String> = if tags.is_empty() {
                        Vec::new()
                    } else {
                        tags.split(',').map(|s| s.trim().to_string()).filter(|s| !s.is_empty()).collect()
                    };
                    let hash = memory::note_hash(&kind, &summary);
                    memory::record_note(&kind, &summary, &detail, &context, &tag_list);
                    eprintln!("noted [{}]: {} ({})", kind, summary, &hash[..12]);
                }

                MemoryAction::Notes { kind, context, limit } => {
                    let notes = memory::list_notes(kind.as_deref(), context.as_deref(), limit);
                    if notes.is_empty() {
                        eprintln!("no notes");
                        println!("[]");
                    } else {
                        let items: Vec<serde_json::Value> = notes
                            .into_iter()
                            .map(|(hash, note)| {
                                serde_json::json!({
                                    "hash": &hash[..12.min(hash.len())],
                                    "kind": note.kind,
                                    "summary": note.summary,
                                    "detail": note.detail,
                                    "context": note.context,
                                    "tags": note.tags,
                                    "created": note.created,
                                    "status": note.status,
                                })
                            })
                            .collect();
                        println!("{}", serde_json::to_string_pretty(&items).unwrap());
                    }
                }

                MemoryAction::Resolve { hash, status } => {
                    let valid = ["resolved", "superseded", "active"];
                    if !valid.contains(&status.as_str()) {
                        eprintln!("unknown status: {status}");
                        eprintln!("valid: {}", valid.join(", "));
                        std::process::exit(1);
                    }
                    memory::update_note_status(&hash, &status);
                    eprintln!("note {} → {status}", &hash[..12.min(hash.len())]);
                }

                MemoryAction::Setup => {
                    match atomic::AtomicClient::from_env() {
                        Some(client) => {
                            match client.ensure_schema() {
                                Ok(()) => eprintln!("schema created on {}", client.server_url),
                                Err(e) => {
                                    eprintln!("error: {e}");
                                    std::process::exit(1);
                                }
                            }
                        }
                        None => {
                            eprintln!("error: set ESC_ATOMIC_URL and ESC_ATOMIC_KEY");
                            std::process::exit(1);
                        }
                    }
                }


            }
        }
    }
}
