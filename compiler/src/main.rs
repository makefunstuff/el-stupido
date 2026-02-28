mod atomic;
mod memory;

use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "esc", about = "esc — persistent memory graph")]
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
    }
}
