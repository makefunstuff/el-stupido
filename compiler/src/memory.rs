//! Memory graph — persistent contextual knowledge across sessions.
//!
//! The memory is stored at `~/.esc/memory.json`:
//! - **Notes**: contextual knowledge (discoveries, decisions, patterns, issues)
//!
//! Queried through `esc memory search/show/note` subcommands.
//! Dual-writes to atomic-server when configured (proper /query endpoint for structured lookups).

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

/// Top-level memory state — notes only.
/// Serde will silently ignore unknown fields (entries, edges) from old files.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MemoryState {
    pub version: u32,
    #[serde(default)]
    pub notes: HashMap<String, MemoryNote>,
}

/// Contextual knowledge entry — discoveries, decisions, patterns, issues.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MemoryNote {
    /// Kind: discovery, decision, pattern, issue
    pub kind: String,
    /// One-line summary (indexed for search)
    pub summary: String,
    /// Longer explanation
    #[serde(default)]
    pub detail: String,
    /// Project or area context (e.g., "homelab", "work")
    #[serde(default)]
    pub context: String,
    #[serde(default)]
    pub tags: Vec<String>,
    pub created: String,
    #[serde(default = "default_note_status")]
    pub status: String,
}

fn default_note_status() -> String {
    "active".to_string()
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
            notes: HashMap::new(),
        }),
        Err(_) => MemoryState {
            version: 1,
            notes: HashMap::new(),
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

// --- Notes ---

/// Compute content-addressed hash for a note (deduplicates same kind+summary).
pub fn note_hash(kind: &str, summary: &str) -> String {
    use sha2::{Digest, Sha256};
    let mut hasher = Sha256::new();
    hasher.update(kind.as_bytes());
    hasher.update(b":");
    hasher.update(summary.as_bytes());
    format!("{:x}", hasher.finalize())
}

/// Record a contextual note. Dual-writes to atomic-server when configured.
pub fn record_note(kind: &str, summary: &str, detail: &str, context: &str, tags: &[String]) {
    let hash = note_hash(kind, summary);
    let mut state = load();
    let now = now_rfc3339();

    if let Some(existing) = state.notes.get_mut(&hash) {
        if !detail.is_empty() {
            existing.detail = detail.to_string();
        }
        if !context.is_empty() {
            existing.context = context.to_string();
        }
        for tag in tags {
            if !existing.tags.contains(tag) {
                existing.tags.push(tag.clone());
            }
        }
    } else {
        state.notes.insert(
            hash.clone(),
            MemoryNote {
                kind: kind.to_string(),
                summary: summary.to_string(),
                detail: detail.to_string(),
                context: context.to_string(),
                tags: tags.to_vec(),
                created: now,
                status: "active".to_string(),
            },
        );
    }

    save(&state);

    // Dual-write to atomic-server
    if let Some(client) = crate::atomic::AtomicClient::from_env() {
        let note = state.notes.get(&hash).unwrap();
        let _ = atomic_record_note(&client, &hash, note);
    }
}

/// Update a note's status (active → resolved/superseded).
pub fn update_note_status(hash: &str, status: &str) {
    let mut state = load();
    let full_hash = state.notes.keys().find(|k| k.starts_with(hash)).cloned();
    if let Some(full_hash) = full_hash {
        if let Some(note) = state.notes.get_mut(&full_hash) {
            note.status = status.to_string();
            let snapshot = note.clone();
            save(&state);
            if let Some(client) = crate::atomic::AtomicClient::from_env() {
                let _ = atomic_record_note(&client, &full_hash, &snapshot);
            }
        }
    }
}

/// List notes with optional kind/context filters.
/// Local file is the source of truth.
pub fn list_notes(
    kind: Option<&str>,
    context: Option<&str>,
    limit: usize,
) -> Vec<(String, MemoryNote)> {
    local_list_notes(kind, context, limit)
}

fn local_list_notes(
    kind: Option<&str>,
    context: Option<&str>,
    limit: usize,
) -> Vec<(String, MemoryNote)> {
    let state = load();
    let mut notes: Vec<(String, MemoryNote)> = state
        .notes
        .into_iter()
        .filter(|(_, n)| {
            (kind.is_none() || kind == Some(n.kind.as_str()))
                && (context.is_none() || context == Some(n.context.as_str()))
        })
        .collect();
    notes.sort_by(|a, b| b.1.created.cmp(&a.1.created));
    notes.truncate(limit);
    notes
}

// --- Log ---

/// Activity log: notes by time, newest first.
/// Local file is the source of truth.
pub fn log(limit: usize) -> Vec<serde_json::Value> {
    local_log(limit)
}

fn local_log(limit: usize) -> Vec<serde_json::Value> {
    let state = load();
    let mut items: Vec<serde_json::Value> = Vec::new();

    for (hash, note) in &state.notes {
        if note.status == "resolved" || note.status == "superseded" {
            continue;
        }
        items.push(serde_json::json!({
            "type": "note",
            "hash": &hash[..12.min(hash.len())],
            "kind": note.kind,
            "summary": note.summary,
            "context": note.context,
            "time": &note.created,
        }));
    }

    items.sort_by(|a, b| {
        let ta = a["time"].as_str().unwrap_or("");
        let tb = b["time"].as_str().unwrap_or("");
        tb.cmp(ta)
    });
    items.truncate(limit);
    items
}

// --- Recall (search) ---

/// Recall: search notes. Compact output — LLM drills in with `show`.
pub fn recall(query: &str, limit: usize) -> Vec<serde_json::Value> {
    // Always search local first — it's the source of truth.
    // Atomic-server is a sync target, not the primary store.
    let mut results = local_recall(query, limit);

    // Supplement with atomic-server results (may find notes not yet in local file)
    if let Some(client) = crate::atomic::AtomicClient::from_env() {
        match atomic_recall(&client, query, limit) {
            Ok(remote) => {
                let seen: std::collections::HashSet<String> = results
                    .iter()
                    .filter_map(|r| r["hash"].as_str().map(|s| s.to_string()))
                    .collect();
                for r in remote {
                    if let Some(h) = r["hash"].as_str() {
                        if !seen.contains(h) {
                            results.push(r);
                        }
                    }
                }
            }
            Err(e) => eprintln!("warning: atomic-server: {e}"),
        }
    }

    results.truncate(limit);
    results
}

fn local_recall(query: &str, limit: usize) -> Vec<serde_json::Value> {
    use std::collections::HashSet;

    let mut results: Vec<serde_json::Value> = Vec::new();
    let mut seen: HashSet<String> = HashSet::new();
    let mut all_tags: HashSet<String> = HashSet::new();

    // Phase 1: Direct search
    for (hash, note, score) in local_search_notes(query) {
        let short = hash[..12.min(hash.len())].to_string();
        for tag in &note.tags {
            all_tags.insert(tag.clone());
        }
        seen.insert(hash);
        let mut r = compact_note(&short, &note, "direct");
        r["score"] = serde_json::json!(score);
        results.push(r);
    }

    // Phase 2: Tag expansion (2+ shared tags, capped)
    if !all_tags.is_empty() {
        let state = load();
        let mut tag_expanded = 0usize;
        const MAX_TAG_EXPANSION: usize = 5;

        for (hash, note) in &state.notes {
            if tag_expanded >= MAX_TAG_EXPANSION {
                break;
            }
            if seen.contains(hash) {
                continue;
            }
            let shared: Vec<&str> = note
                .tags
                .iter()
                .filter(|t| all_tags.contains(*t))
                .map(|s| s.as_str())
                .collect();
            if shared.len() >= 2 {
                seen.insert(hash.clone());
                let short = &hash[..12.min(hash.len())];
                results.push(compact_note(
                    short,
                    note,
                    &format!("shared_tags:{}", shared.join(",")),
                ));
                tag_expanded += 1;
            }
        }
    }

    results.sort_by(|a, b| {
        let sa = a["score"].as_u64().unwrap_or(0);
        let sb = b["score"].as_u64().unwrap_or(0);
        sb.cmp(&sa)
    });

    results.truncate(limit);
    results
}

fn local_search_notes(query: &str) -> Vec<(String, MemoryNote, usize)> {
    let state = load();
    let query_lower = query.to_lowercase();
    let query_words: Vec<&str> = query_lower.split_whitespace().collect();

    let mut results: Vec<(String, MemoryNote, usize)> = state
        .notes
        .into_iter()
        .filter_map(|(hash, note)| {
            let score = score_note(&note, &query_words);
            if score > 0 {
                Some((hash, note, score))
            } else {
                None
            }
        })
        .collect();

    results.sort_by(|a, b| b.2.cmp(&a.2));
    results
}

fn score_note(note: &MemoryNote, query_words: &[&str]) -> usize {
    let mut score = 0;
    let summary_lower = note.summary.to_lowercase();
    let detail_lower = note.detail.to_lowercase();
    let kind_lower = note.kind.to_lowercase();
    let context_lower = note.context.to_lowercase();
    let tags_lower: Vec<String> = note.tags.iter().map(|t| t.to_lowercase()).collect();

    for word in query_words {
        if summary_lower.contains(word) {
            score += 3;
        }
        if detail_lower.contains(word) {
            score += 2;
        }
        if kind_lower == *word {
            score += 2;
        }
        if context_lower.contains(word) {
            score += 2;
        }
        if tags_lower.iter().any(|t| t == word) {
            score += 3;
        }
        if tags_lower.iter().any(|t| t.contains(word)) {
            score += 1;
        }
    }
    score
}

/// Compact note representation.
fn compact_note(hash: &str, note: &MemoryNote, via: &str) -> serde_json::Value {
    serde_json::json!({
        "type": "note",
        "hash": hash,
        "kind": note.kind,
        "summary": note.summary,
        "context": note.context,
        "status": note.status,
        "via": via,
    })
}

// --- Sync: bulk write all notes to atomic-server ---

/// Sync all notes from flat file to atomic-server. Returns count synced.
pub fn sync_to_atomic() -> Result<usize, String> {
    let client = crate::atomic::AtomicClient::from_env()
        .ok_or_else(|| "set ESC_ATOMIC_URL and ESC_ATOMIC_KEY".to_string())?;
    let state = load();
    let mut count = 0;
    for (hash, note) in &state.notes {
        if let Err(e) = atomic_record_note(&client, hash, note) {
            eprintln!("  warning: {} — {}", &hash[..12.min(hash.len())], e);
        } else {
            count += 1;
        }
    }
    Ok(count)
}

// --- Atomic-server backend ---

/// Recall via atomic-server: full-text search for notes.
fn atomic_recall(
    client: &crate::atomic::AtomicClient,
    query: &str,
    limit: usize,
) -> Result<Vec<serde_json::Value>, String> {
    use std::collections::HashSet;

    let mut results: Vec<serde_json::Value> = Vec::new();
    let mut seen: HashSet<String> = HashSet::new();

    let notes = client.search(query, limit)?;

    for r in notes {
        if let Some((hash, note)) = atomic_resource_to_note(client, &r) {
            let short = hash[..12.min(hash.len())].to_string();
            if seen.insert(format!("n:{short}")) {
                results.push(compact_note(&short, &note, "direct"));
            }
        }
    }

    results.truncate(limit);
    Ok(results)
}

/// Write a MemoryNote to atomic-server.
pub fn atomic_record_note(
    client: &crate::atomic::AtomicClient,
    hash: &str,
    note: &MemoryNote,
) -> Result<(), String> {
    let subject = client.note_url(hash);
    // name+description indexed by tantivy
    let search_name = format!(
        "esc-note {} {} {}",
        note.kind,
        note.context,
        note.tags.join(" ")
    );
    let search_desc = format!("{} | {}", note.summary, note.detail);
    let set = serde_json::json!({
        "https://atomicdata.dev/properties/name": search_name.trim(),
        "https://atomicdata.dev/properties/description": search_desc,
        client.prop_url("note-kind"): note.kind,
        client.prop_url("note-summary"): note.summary,
        client.prop_url("note-detail"): note.detail,
        client.prop_url("note-context"): note.context,
        client.prop_url("tags"): note.tags.join(","),
        client.prop_url("created"): note.created,
        client.prop_url("status"): note.status,
        "https://atomicdata.dev/properties/isA": [client.class_url("note")],
        "https://atomicdata.dev/properties/parent": client.server_url,
    });
    client.upsert(&subject, &set)
}

fn atomic_resource_to_note(
    client: &crate::atomic::AtomicClient,
    r: &serde_json::Value,
) -> Option<(String, MemoryNote)> {
    let p = |name| client.prop_url(name);
    let kind = r
        .get(&p("note-kind"))
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string();
    let summary = r
        .get(&p("note-summary"))
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string();
    if kind.is_empty() || summary.is_empty() {
        return None;
    }
    let detail = r
        .get(&p("note-detail"))
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string();
    let context = r
        .get(&p("note-context"))
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string();
    let tags_str = r.get(&p("tags")).and_then(|v| v.as_str()).unwrap_or("");
    let tags: Vec<String> = if tags_str.is_empty() {
        vec![]
    } else {
        tags_str.split(',').map(|s| s.trim().to_string()).collect()
    };
    let created = r
        .get(&p("created"))
        .and_then(|v| v.as_str())
        .unwrap_or("")
        .to_string();
    let status = r
        .get(&p("status"))
        .and_then(|v| v.as_str())
        .unwrap_or("active")
        .to_string();

    let full_hash = note_hash(&kind, &summary);
    Some((
        full_hash,
        MemoryNote {
            kind,
            summary,
            detail,
            context,
            tags,
            created,
            status,
        },
    ))
}

// --- Time helpers ---

fn now_rfc3339() -> String {
    let duration = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default();
    let secs = duration.as_secs();

    let days = secs / 86400;
    let time_secs = secs % 86400;
    let hours = time_secs / 3600;
    let minutes = (time_secs % 3600) / 60;
    let seconds = time_secs % 60;

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
