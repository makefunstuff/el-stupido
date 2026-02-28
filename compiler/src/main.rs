mod atomic;
mod context;
mod memory;

use clap::{Parser, Subcommand};
use std::io::Read;

#[derive(Parser)]
#[command(
    name = "esc",
    about = "esc — persistent memory graph and session context"
)]
struct Cli {
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Query and manage the memory graph
    Memory {
        #[command(subcommand)]
        action: MemoryAction,
    },
    /// Manage session context lifecycle
    Context {
        #[command(subcommand)]
        action: ContextAction,
    },
}

#[derive(Subcommand)]
enum MemoryAction {
    /// Search memory: keyword match + tag expansion
    Search {
        /// Search query (natural language or keywords)
        query: String,
        /// Max results to return
        #[arg(short = 'n', long, default_value = "20")]
        limit: usize,
    },
    /// Show full details of a note by hash prefix
    Show {
        /// Note hash (prefix match)
        hash: String,
    },
    /// Recent activity: notes by time, newest first
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
        /// Context/project area (e.g., "homelab", "work")
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
        /// New status: resolved, superseded, active
        #[arg(long, default_value = "resolved")]
        status: String,
    },
    /// Sync all notes to atomic-server (requires ESC_ATOMIC_URL + ESC_ATOMIC_KEY)
    Sync,
    /// Create esc schema on atomic-server (requires ESC_ATOMIC_URL + ESC_ATOMIC_KEY)
    Setup,
}

#[derive(Subcommand)]
enum ContextAction {
    /// Initialize a new context session
    Init {
        /// Token budget for the session
        #[arg(long, default_value = "100000")]
        budget: u32,
    },
    /// Add a context slot
    Add {
        /// Slot kind: task, result, error, knowledge, scratch
        #[arg(long)]
        kind: String,
        /// Content (reads from stdin if omitted)
        content: Option<String>,
    },
    /// Mark a slot as still relevant (promotes back to hot)
    Touch {
        /// Slot ID (e.g. s001)
        id: String,
    },
    /// Advance turn and apply policies (JSON output)
    Observe,
    /// Replace slot content with a compact summary
    Compact {
        /// Slot ID
        id: String,
        /// Summary text
        summary: String,
    },
    /// Persist slot to memory graph, remove from session
    Archive {
        /// Slot ID
        id: String,
    },
    /// Remove slot without persisting
    Drop {
        /// Slot ID
        id: String,
    },
    /// Output curated context with reasoning (auto-observes each call)
    Assemble,
    /// Show session status
    Status,
    /// Update an existing slot's content in place
    Update {
        /// Slot ID
        id: String,
        /// New content (reads from stdin if omitted)
        content: Option<String>,
        /// Where this content came from
        #[arg(long)]
        source: Option<String>,
    },
    /// Ingest raw text: auto-classify, match existing slots, create or update
    Ingest {
        /// Content (reads from stdin if omitted)
        content: Option<String>,
        /// Override auto-classification: task, result, error, knowledge, scratch
        #[arg(long)]
        kind: Option<String>,
        /// Where this content came from (e.g. "cargo build")
        #[arg(long)]
        source: Option<String>,
    },
    /// Write an event to the feed (active session pipes tool output here)
    Feed {
        /// Source of the content (e.g. "cargo build", "file read")
        #[arg(long)]
        source: String,
        /// Override auto-classification
        #[arg(long)]
        kind: Option<String>,
        /// Content (reads from stdin if omitted)
        content: Option<String>,
    },
    /// Process new feed entries and run observe cycle
    Watch,
    /// Search memory graph for knowledge augmentation
    Recall {
        /// Search query
        query: String,
        /// Max results
        #[arg(short = 'n', long, default_value = "10")]
        limit: usize,
    },
}

fn main() {
    let cli = Cli::parse();

    match cli.cmd {
        Cmd::Memory { action } => match action {
            MemoryAction::Search { query, limit } => {
                let results = memory::recall(&query, limit);
                if results.is_empty() {
                    eprintln!("no matches for: {query}");
                }
                println!("{}", serde_json::to_string_pretty(&results).unwrap());
            }

            MemoryAction::Show { hash } => {
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

            MemoryAction::Log { limit } => {
                let items = memory::log(limit);
                if items.is_empty() {
                    eprintln!("memory is empty");
                }
                println!("{}", serde_json::to_string_pretty(&items).unwrap());
            }

            MemoryAction::Note {
                summary,
                detail,
                kind,
                context,
                tags,
            } => {
                let valid_kinds = ["discovery", "decision", "pattern", "issue"];
                if !valid_kinds.contains(&kind.as_str()) {
                    eprintln!("unknown kind: {kind}");
                    eprintln!("valid: {}", valid_kinds.join(", "));
                    std::process::exit(1);
                }
                let tag_list: Vec<String> = if tags.is_empty() {
                    Vec::new()
                } else {
                    tags.split(',')
                        .map(|s| s.trim().to_string())
                        .filter(|s| !s.is_empty())
                        .collect()
                };
                let hash = memory::note_hash(&kind, &summary);
                memory::record_note(&kind, &summary, &detail, &context, &tag_list);
                eprintln!("noted [{}]: {} ({})", kind, summary, &hash[..12]);
            }

            MemoryAction::Notes {
                kind,
                context,
                limit,
            } => {
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
                eprintln!("note {} -> {status}", &hash[..12.min(hash.len())]);
            }

            MemoryAction::Sync => match memory::sync_to_atomic() {
                Ok(count) => {
                    let state = memory::load();
                    eprintln!(
                        "synced {count}/{} notes to atomic-server",
                        state.notes.len()
                    );
                }
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            },

            MemoryAction::Setup => match atomic::AtomicClient::from_env() {
                Some(client) => match client.ensure_schema() {
                    Ok(()) => eprintln!("schema created on {}", client.server_url),
                    Err(e) => {
                        eprintln!("error: {e}");
                        std::process::exit(1);
                    }
                },
                None => {
                    eprintln!("error: set ESC_ATOMIC_URL and ESC_ATOMIC_KEY");
                    std::process::exit(1);
                }
            },
        },

        Cmd::Context { action } => match action {
            ContextAction::Init { budget } => {
                let session = context::init(budget);
                eprintln!(
                    "context session {} (budget: {} tokens)",
                    session.id, session.budget
                );
            }

            ContextAction::Add { kind, content } => {
                let kind = match context::SlotKind::parse(&kind) {
                    Ok(k) => k,
                    Err(e) => {
                        eprintln!("error: {e}");
                        std::process::exit(1);
                    }
                };
                let text = match content {
                    Some(c) => c,
                    None => {
                        let mut buf = String::new();
                        std::io::stdin()
                            .read_to_string(&mut buf)
                            .unwrap_or_default();
                        buf
                    }
                };
                if text.trim().is_empty() {
                    eprintln!("error: empty content");
                    std::process::exit(1);
                }
                match context::add(kind, text.trim()) {
                    Ok((id, tokens)) => {
                        println!("{}", serde_json::json!({"id": id, "tokens": tokens}));
                    }
                    Err(e) => {
                        eprintln!("error: {e}");
                        std::process::exit(1);
                    }
                }
            }

            ContextAction::Touch { id } => match context::touch(&id) {
                Ok(()) => eprintln!("touched: {id}"),
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            },

            ContextAction::Observe => match context::observe() {
                Ok(result) => {
                    eprintln!(
                        "[context] turn {} | {}K/{}K ({:.0}%) | {} slots | {} transitions, {} recs",
                        result.turn,
                        result.total_tokens / 1000,
                        result.budget / 1000,
                        result.budget_pct * 100.0,
                        result.slot_count,
                        result.transitions.len(),
                        result.recommendations.len(),
                    );
                    println!("{}", serde_json::to_string_pretty(&result).unwrap());
                }
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            },

            ContextAction::Compact { id, summary } => match context::compact(&id, &summary) {
                Ok((old, new)) => {
                    eprintln!(
                        "compacted {id}: {old} -> {new} tokens (saved {})",
                        old.saturating_sub(new)
                    );
                }
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            },

            ContextAction::Archive { id } => match context::archive(&id) {
                Ok((hash, kind)) => {
                    eprintln!("archived {id} -> memory note [{kind}] {hash}");
                }
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            },

            ContextAction::Drop { id } => match context::drop_slot(&id) {
                Ok(()) => eprintln!("dropped: {id}"),
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            },

            ContextAction::Assemble => match context::assemble() {
                Ok(text) => print!("{text}"),
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            },

            ContextAction::Status => match context::status() {
                Ok(s) => println!("{}", serde_json::to_string_pretty(&s).unwrap()),
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            },

            ContextAction::Update {
                id,
                content,
                source,
            } => {
                let text = match content {
                    Some(c) => c,
                    None => {
                        let mut buf = String::new();
                        std::io::stdin()
                            .read_to_string(&mut buf)
                            .unwrap_or_default();
                        buf
                    }
                };
                if text.trim().is_empty() {
                    eprintln!("error: empty content");
                    std::process::exit(1);
                }
                match context::update(&id, text.trim(), source.as_deref()) {
                    Ok((old, new)) => {
                        eprintln!("updated {id}: {old} -> {new} tokens");
                    }
                    Err(e) => {
                        eprintln!("error: {e}");
                        std::process::exit(1);
                    }
                }
            }

            ContextAction::Ingest {
                content,
                kind,
                source,
            } => {
                let text = match content {
                    Some(c) => c,
                    None => {
                        let mut buf = String::new();
                        std::io::stdin()
                            .read_to_string(&mut buf)
                            .unwrap_or_default();
                        buf
                    }
                };
                if text.trim().is_empty() {
                    eprintln!("error: empty content");
                    std::process::exit(1);
                }
                let kind_override = kind.map(|k| match context::SlotKind::parse(&k) {
                    Ok(k) => k,
                    Err(e) => {
                        eprintln!("error: {e}");
                        std::process::exit(1);
                    }
                });
                match context::ingest(text.trim(), kind_override, source.as_deref()) {
                    Ok(result) => {
                        println!("{}", serde_json::to_string(&result).unwrap());
                    }
                    Err(e) => {
                        eprintln!("error: {e}");
                        std::process::exit(1);
                    }
                }
            }

            ContextAction::Feed {
                source: _,
                kind: _,
                content: _,
            } => {
                // Disabled — context feed was burning API tokens via auto-triggers
            }

            ContextAction::Watch => {
                // Disabled — context watch was burning API tokens via auto-triggers
                eprintln!("[watch] disabled");
            }

            ContextAction::Recall { query, limit } => {
                let result = context::recall(&query, limit);
                println!("{}", serde_json::to_string_pretty(&result).unwrap());
            }
        },
    }
}
