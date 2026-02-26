//! Memory graph — persistent tool knowledge across agent sessions.
//!
//! The memory is a graph stored at `~/.esc/memory.json`:
//! - **Entries** (nodes): one per tool hash, carries goal/tags/pattern/IO summary
//! - **Edges**: relationships between tools (variant_of, pipes_to, supersedes)
//!
//! The agent queries this through `esc memory search/show/record` subcommands.
//! It never loads the full graph into context — only the relevant subset.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MemoryState {
    pub version: u32,
    pub entries: HashMap<String, MemoryEntry>,
    #[serde(default)]
    pub edges: Vec<MemoryEdge>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MemoryEntry {
    pub app: String,
    pub goal: String,
    #[serde(default)]
    pub tags: Vec<String>,
    /// Primitive chain summary, e.g. "arg_str > concat > http_get_dyn > trim_str > print_str"
    pub pattern: String,
    /// Compact IO signature, e.g. "arg:str:1 -> stdout:str"
    pub io: String,
    #[serde(default)]
    pub caps: Vec<String>,
    pub created: String,
    pub last_used: String,
    #[serde(default)]
    pub use_count: u64,
    #[serde(default = "default_status")]
    pub status: String,
    #[serde(default)]
    pub notes: String,
}

fn default_status() -> String {
    "ok".to_string()
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MemoryEdge {
    pub from: String,
    pub to: String,
    /// Relationship type: variant_of, pipes_to, supersedes
    pub rel: String,
    #[serde(default)]
    pub note: String,
}

fn memory_path() -> PathBuf {
    let home = std::env::var("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("."));
    home.join(".esc").join("memory.json")
}

pub fn load() -> MemoryState {
    let path = memory_path();
    match fs::read_to_string(&path) {
        Ok(s) => serde_json::from_str(&s).unwrap_or_else(|_| MemoryState {
            version: 1,
            entries: HashMap::new(),
            edges: Vec::new(),
        }),
        Err(_) => MemoryState {
            version: 1,
            entries: HashMap::new(),
            edges: Vec::new(),
        },
    }
}

fn save(state: &MemoryState) {
    let path = memory_path();
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    let json = serde_json::to_string_pretty(state).unwrap_or_default();
    let tmp = path.with_extension("tmp");
    if fs::write(&tmp, &json).is_ok() {
        let _ = fs::rename(&tmp, &path);
    }
}

/// Produce a compact IO signature string from IO contract arrays.
/// e.g. "arg:str:1, arg:num:2 -> stdout:num"
pub fn io_signature(inputs: &[serde_json::Value], outputs: &[serde_json::Value]) -> String {
    let ins: Vec<String> = inputs
        .iter()
        .map(|v| {
            let kind = v["kind"].as_str().unwrap_or("?");
            let ty = v["type"].as_str().unwrap_or("?");
            match kind {
                "arg" => {
                    let idx = v["index"].as_u64().unwrap_or(0);
                    format!("arg:{}:{}", ty, idx)
                }
                "stdin" => format!("stdin:{}", ty),
                "http" => {
                    if v["dynamic"].as_bool().unwrap_or(false) {
                        format!("http:{}:dyn", ty)
                    } else {
                        let url = v["url"].as_str().unwrap_or("?");
                        // Truncate long URLs
                        if url.len() > 40 {
                            format!("http:{}:{:.40}…", ty, url)
                        } else {
                            format!("http:{}:{}", ty, url)
                        }
                    }
                }
                _ => format!("{}:{}", kind, ty),
            }
        })
        .collect();

    let outs: Vec<String> = outputs
        .iter()
        .map(|v| {
            let kind = v["kind"].as_str().unwrap_or("?");
            let ty = v["type"].as_str().unwrap_or("?");
            format!("{}:{}", kind, ty)
        })
        .collect();

    if ins.is_empty() && outs.is_empty() {
        "void -> void".to_string()
    } else if ins.is_empty() {
        format!("void -> {}", outs.join(", "))
    } else if outs.is_empty() {
        format!("{} -> void", ins.join(", "))
    } else {
        format!("{} -> {}", ins.join(", "), outs.join(", "))
    }
}

/// Produce a primitive chain pattern from a node list.
/// e.g. "arg_str > concat > http_get_dyn > trim_str > print_str"
pub fn extract_pattern(nodes: &[crate::compose::ValidNode]) -> String {
    nodes
        .iter()
        .map(|n| n.primitive_id.as_str())
        .collect::<Vec<_>>()
        .join(" > ")
}

/// Record a tool forge into memory.
pub fn record(
    hash: &str,
    app: &str,
    goal: &str,
    tags: &[String],
    pattern: &str,
    io: &str,
    caps: &[String],
    notes: &str,
) {
    let mut state = load();
    let now = now_rfc3339();

    if let Some(existing) = state.entries.get_mut(hash) {
        // Update existing entry — bump use_count, refresh last_used
        existing.last_used = now;
        existing.use_count += 1;
        if !goal.is_empty() && existing.goal.is_empty() {
            existing.goal = goal.to_string();
        }
        if !tags.is_empty() {
            // Merge tags
            for tag in tags {
                if !existing.tags.contains(tag) {
                    existing.tags.push(tag.clone());
                }
            }
        }
        if !notes.is_empty() {
            existing.notes = notes.to_string();
        }
    } else {
        state.entries.insert(
            hash.to_string(),
            MemoryEntry {
                app: app.to_string(),
                goal: goal.to_string(),
                tags: tags.to_vec(),
                pattern: pattern.to_string(),
                io: io.to_string(),
                caps: caps.to_vec(),
                created: now.clone(),
                last_used: now,
                use_count: 1,
                status: "ok".to_string(),
                notes: notes.to_string(),
            },
        );
    }

    save(&state);
}

/// Add an edge between two tools.
pub fn relate(from: &str, to: &str, rel: &str, note: &str) {
    let mut state = load();

    // Don't duplicate identical edges
    let exists = state
        .edges
        .iter()
        .any(|e| e.from == from && e.to == to && e.rel == rel);
    if !exists {
        state.edges.push(MemoryEdge {
            from: from.to_string(),
            to: to.to_string(),
            rel: rel.to_string(),
            note: note.to_string(),
        });
    }

    save(&state);
}

/// Search memory by fuzzy matching on goal + app + tags.
/// Returns entries sorted by relevance score descending.
pub fn search(query: &str) -> Vec<(String, MemoryEntry, usize)> {
    let state = load();
    let query_lower = query.to_lowercase();
    let query_words: Vec<&str> = query_lower.split_whitespace().collect();

    let mut results: Vec<(String, MemoryEntry, usize)> = state
        .entries
        .into_iter()
        .filter_map(|(hash, entry)| {
            let score = score_entry(&entry, &query_words);
            if score > 0 {
                Some((hash, entry, score))
            } else {
                None
            }
        })
        .collect();

    results.sort_by(|a, b| b.2.cmp(&a.2));
    results
}

/// Score an entry against query words. Higher = better match.
fn score_entry(entry: &MemoryEntry, query_words: &[&str]) -> usize {
    let mut score = 0;
    let goal_lower = entry.goal.to_lowercase();
    let app_lower = entry.app.to_lowercase();
    let pattern_lower = entry.pattern.to_lowercase();
    let tags_lower: Vec<String> = entry.tags.iter().map(|t| t.to_lowercase()).collect();
    let io_lower = entry.io.to_lowercase();

    for word in query_words {
        // Goal match (highest weight)
        if goal_lower.contains(word) {
            score += 3;
        }
        // App name match
        if app_lower.contains(word) {
            score += 2;
        }
        // Tag exact match
        if tags_lower.iter().any(|t| t == word) {
            score += 3;
        }
        // Tag substring match
        if tags_lower.iter().any(|t| t.contains(word)) {
            score += 1;
        }
        // Pattern match (primitive names)
        if pattern_lower.contains(word) {
            score += 1;
        }
        // IO signature match
        if io_lower.contains(word) {
            score += 1;
        }
    }

    score
}

/// Find tools related to a given hash (via edges or shared tags/patterns).
pub fn related(hash: &str) -> Vec<(String, MemoryEntry, String)> {
    let state = load();

    let mut results: Vec<(String, MemoryEntry, String)> = Vec::new();

    // Direct edges
    for edge in &state.edges {
        if edge.from == hash || hash_matches(&edge.from, hash) {
            if let Some(entry) = state.entries.get(&edge.to) {
                let rel = if edge.note.is_empty() {
                    edge.rel.clone()
                } else {
                    format!("{} ({})", edge.rel, edge.note)
                };
                results.push((edge.to.clone(), entry.clone(), rel));
            }
        }
        if edge.to == hash || hash_matches(&edge.to, hash) {
            if let Some(entry) = state.entries.get(&edge.from) {
                let rel = if edge.note.is_empty() {
                    format!("{}←", edge.rel)
                } else {
                    format!("{}← ({})", edge.rel, edge.note)
                };
                results.push((edge.from.clone(), entry.clone(), rel));
            }
        }
    }

    // Find tools with overlapping tags (if we have the source entry)
    if let Some(source) = state.entries.get(hash) {
        if !source.tags.is_empty() {
            for (h, entry) in &state.entries {
                if h == hash {
                    continue;
                }
                // Already in results via edge?
                if results.iter().any(|(rh, _, _)| rh == h) {
                    continue;
                }
                let shared: Vec<&String> = entry
                    .tags
                    .iter()
                    .filter(|t| source.tags.contains(t))
                    .collect();
                if shared.len() >= 2 {
                    let rel = format!(
                        "shared_tags:{}",
                        shared
                            .iter()
                            .map(|s| s.as_str())
                            .collect::<Vec<_>>()
                            .join(",")
                    );
                    results.push((h.clone(), entry.clone(), rel));
                }
            }
        }
    }

    results
}

/// Log: return entries sorted by last_used descending, limited to N.
pub fn log(limit: usize) -> Vec<(String, MemoryEntry)> {
    let state = load();
    let mut entries: Vec<(String, MemoryEntry)> = state.entries.into_iter().collect();
    entries.sort_by(|a, b| b.1.last_used.cmp(&a.1.last_used));
    entries.truncate(limit);
    entries
}

/// Bump use_count and last_used for a tool.
pub fn touch(hash: &str) {
    let mut state = load();
    // Support prefix matching
    let full_hash = state.entries.keys().find(|k| k.starts_with(hash)).cloned();

    if let Some(full_hash) = full_hash {
        if let Some(entry) = state.entries.get_mut(&full_hash) {
            entry.last_used = now_rfc3339();
            entry.use_count += 1;
            save(&state);
        }
    }
}

fn hash_matches(full: &str, query: &str) -> bool {
    full.starts_with(query)
}

fn now_rfc3339() -> String {
    // Simple UTC timestamp without chrono dependency
    let duration = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default();
    let secs = duration.as_secs();

    // Manual UTC breakdown (good enough, no chrono needed)
    let days = secs / 86400;
    let time_secs = secs % 86400;
    let hours = time_secs / 3600;
    let minutes = (time_secs % 3600) / 60;
    let seconds = time_secs % 60;

    // Compute year/month/day from days since epoch (1970-01-01)
    let (year, month, day) = days_to_ymd(days);

    format!(
        "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",
        year, month, day, hours, minutes, seconds
    )
}

fn days_to_ymd(mut days: u64) -> (u64, u64, u64) {
    let mut year = 1970;
    loop {
        let days_in_year = if is_leap(year) { 366 } else { 365 };
        if days < days_in_year {
            break;
        }
        days -= days_in_year;
        year += 1;
    }

    let month_days = if is_leap(year) {
        [31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    } else {
        [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    };

    let mut month = 1u64;
    for &md in &month_days {
        if days < md {
            break;
        }
        days -= md;
        month += 1;
    }

    (year, month, days + 1)
}

fn is_leap(year: u64) -> bool {
    (year % 4 == 0 && year % 100 != 0) || year % 400 == 0
}
