//! Context lifecycle manager — evolutionary session context.
//!
//! Gives the model structured reasoning about its own context:
//! what it knows, what's aging, what to archive, when to augment from the knowledge graph.
//!
//! Observation is continuous — every `assemble` call advances the turn and applies policies.
//! Graph augmentation is on-demand — the model calls `recall` when it decides it needs knowledge.
//!
//! Auto-wisdom: slots that survive long enough AND get touched repeatedly are automatically
//! persisted to the memory graph as permanent knowledge. The machine accumulates wisdom
//! across sessions, models, and providers.
//!
//! Dual-write: flat-file (offline fallback) + atomic-server (permanent graph, searchable).
//!
//! Slot lifecycle: hot (active) → warm (aging) → cold (summary only) → archived (memory graph)

use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::fs;
use std::io::Write as _;
use std::path::PathBuf;

// --- Hard limits (suckless: bounded, deterministic) ---

pub const MAX_SLOTS: usize = 256;
pub const MAX_CONTENT_BYTES: usize = 16_384;
pub const _DEFAULT_BUDGET: u32 = 100_000;

/// Auto-archive cold slots when budget usage exceeds this fraction.
const ARCHIVE_THRESHOLD: f32 = 0.80;
/// Recommend compacting warm slots above this token count.
const COMPACT_THRESHOLD: u32 = 200;
/// Slots touched this many times are candidates for auto-wisdom extraction.
const WISDOM_TOUCH_THRESHOLD: u32 = 1;
/// Knowledge slots surviving this many turns get auto-extracted as wisdom.
const WISDOM_AGE_THRESHOLD: u32 = 15;
/// Slots surviving this many turns get wisdom'd even with zero engagement.
/// Rationale: if nothing dropped it for this long, it has passive value.
const WISDOM_AGE_ONLY_THRESHOLD: u32 = 30;

// --- Types ---

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum SlotKind {
    Task,
    Result,
    Error,
    Knowledge,
    Scratch,
}

impl SlotKind {
    /// Turns before hot → warm.
    fn hot_ttl(self) -> u32 {
        match self {
            Self::Task => 20,
            Self::Knowledge => 10,
            Self::Result => 5,
            Self::Error => 3,
            Self::Scratch => 2,
        }
    }

    /// Turns before warm → cold.
    fn warm_ttl(self) -> u32 {
        match self {
            Self::Task => 50,
            Self::Knowledge => 30,
            Self::Result => 15,
            Self::Error => 8,
            Self::Scratch => 5,
        }
    }

    /// Map to memory note kind for archival. None = don't archive.
    fn note_kind(self) -> Option<&'static str> {
        match self {
            Self::Task => Some("decision"),
            Self::Knowledge => Some("discovery"),
            Self::Result => Some("discovery"),
            Self::Error => Some("issue"),
            Self::Scratch => None,
        }
    }

    /// Can this kind produce wisdom?
    fn can_be_wisdom(self) -> bool {
        matches!(self, Self::Knowledge | Self::Task)
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Task => "task",
            Self::Result => "result",
            Self::Error => "error",
            Self::Knowledge => "knowledge",
            Self::Scratch => "scratch",
        }
    }

    pub fn parse(s: &str) -> Result<Self, String> {
        match s {
            "task" => Ok(Self::Task),
            "result" => Ok(Self::Result),
            "error" => Ok(Self::Error),
            "knowledge" => Ok(Self::Knowledge),
            "scratch" => Ok(Self::Scratch),
            _ => Err(format!(
                "unknown slot kind: '{s}' (valid: task, result, error, knowledge, scratch)"
            )),
        }
    }
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, PartialOrd)]
#[serde(rename_all = "snake_case")]
pub enum SlotState {
    Hot = 0,
    Warm = 1,
    Cold = 2,
}

impl SlotState {
    pub fn label(self) -> &'static str {
        match self {
            Self::Hot => "hot",
            Self::Warm => "warm",
            Self::Cold => "cold",
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Slot {
    pub id: String,
    pub kind: SlotKind,
    pub state: SlotState,
    pub content: String,
    pub summary: Option<String>,
    pub tokens: u32,
    pub born: u32,
    pub touched: u32,
    /// How many times this slot was explicitly touched (referenced).
    /// High touch_count + long survival = wisdom candidate.
    #[serde(default)]
    pub touch_count: u32,
    /// Whether this slot has been auto-persisted as wisdom.
    #[serde(default)]
    pub wisdom_persisted: bool,
    /// Where this content came from (e.g. "cargo build", "read main.rs").
    #[serde(default)]
    pub source: Option<String>,
    /// How many times content was updated in place. Evolving knowledge = valuable.
    #[serde(default)]
    pub update_count: u32,
}

impl Slot {
    pub fn effective_content(&self) -> &str {
        self.summary.as_deref().unwrap_or(&self.content)
    }

    /// Age in turns since creation.
    fn age(&self, turn: u32) -> u32 {
        turn.saturating_sub(self.born)
    }

    /// Total engagement: touches + updates. Both indicate value.
    fn engagement(&self) -> u32 {
        self.touch_count + self.update_count
    }

    /// Is this slot a wisdom candidate?
    /// Two paths to wisdom:
    /// 1. Engagement path: touched/updated at least once + survived WISDOM_AGE_THRESHOLD turns.
    /// 2. Age-only path: survived WISDOM_AGE_ONLY_THRESHOLD turns regardless of engagement.
    ///    If nothing dropped it for this long, it has passive value.
    fn is_wisdom_candidate(&self, turn: u32) -> bool {
        if self.wisdom_persisted || !self.kind.can_be_wisdom() {
            return false;
        }
        let age = self.age(turn);
        // Path 1: any engagement + moderate age
        let engaged = self.engagement() >= WISDOM_TOUCH_THRESHOLD && age >= WISDOM_AGE_THRESHOLD;
        // Path 2: long survival alone (passive value)
        let survived = age >= WISDOM_AGE_ONLY_THRESHOLD;
        engaged || survived
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Session {
    pub id: String,
    pub turn: u32,
    pub budget: u32,
    pub next_slot: u32,
    pub slots: Vec<Slot>,
    pub archived_count: u32,
    #[serde(default)]
    pub wisdom_count: u32,
}

// --- Observer output ---

#[derive(Debug, Serialize)]
pub struct Transition {
    pub slot: String,
    pub kind: String,
    pub from: String,
    pub to: String,
    pub age: u32,
}

#[derive(Debug, Serialize)]
pub struct Recommendation {
    pub action: String,
    pub slot: String,
    pub reason: String,
    pub tokens: u32,
}

#[derive(Debug, Serialize)]
pub struct WisdomExtracted {
    pub slot: String,
    pub content: String,
    pub touch_count: u32,
    pub age: u32,
    pub memory_hash: String,
}

#[derive(Debug, Serialize)]
pub struct AutoArchived {
    pub slot: String,
    pub kind: String,
    pub action: String, // "archived", "wisdom-archived", "dropped"
}

#[derive(Debug, Serialize)]
pub struct ObserveResult {
    pub turn: u32,
    pub total_tokens: u32,
    pub budget: u32,
    pub budget_pct: f32,
    pub slot_count: usize,
    pub transitions: Vec<Transition>,
    pub recommendations: Vec<Recommendation>,
    pub wisdom: Vec<WisdomExtracted>,
    pub auto_archived: Vec<AutoArchived>,
}

// --- File management ---

fn session_dir() -> PathBuf {
    let home = std::env::var("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("."));
    home.join(".esc").join("context")
}

fn session_path() -> PathBuf {
    session_dir().join("session.json")
}

pub fn load() -> Option<Session> {
    let path = session_path();
    let s = fs::read_to_string(&path).ok()?;
    serde_json::from_str(&s).ok()
}

fn save(session: &Session) {
    let dir = session_dir();
    let _ = fs::create_dir_all(&dir);
    let path = session_path();
    let json = serde_json::to_string_pretty(session).unwrap_or_default();
    let tmp = path.with_extension("tmp");
    if fs::write(&tmp, &json).is_ok() {
        let _ = fs::rename(&tmp, &path);
    }
}

// --- Utilities ---

fn estimate_tokens(text: &str) -> u32 {
    // ~4 bytes per token for English + code (rough BPE approximation)
    ((text.len() + 3) / 4) as u32
}

fn slot_id(n: u32) -> String {
    format!("s{:03}", n)
}

// --- Atomic dual-write helpers ---

/// Write a context slot to atomic-server.
fn atomic_write_slot(session_id: &str, slot: &Slot) {
    let client = match crate::atomic::AtomicClient::from_env() {
        Some(c) => c,
        None => return,
    };

    let subject = client.ctx_slot_url(session_id, &slot.id);
    // name + description get indexed by tantivy for full-text search
    let search_name = format!(
        "esc-ctx {} {} {}",
        slot.kind.label(),
        slot.state.label(),
        &slot.id
    );
    let content = slot.effective_content();
    let search_desc = if content.len() > 200 {
        &content[..200]
    } else {
        content
    };

    let mut set = serde_json::json!({
        "https://atomicdata.dev/properties/name": search_name,
        "https://atomicdata.dev/properties/description": search_desc,
        client.prop_url("ctx-slot-id"): slot.id,
        client.prop_url("ctx-session"): session_id,
        client.prop_url("ctx-kind"): slot.kind.label(),
        client.prop_url("ctx-state"): slot.state.label(),
        client.prop_url("ctx-content"): slot.content,
        client.prop_url("ctx-tokens"): slot.tokens,
        client.prop_url("ctx-born"): slot.born,
        client.prop_url("ctx-touched"): slot.touched,
        client.prop_url("ctx-touch-count"): slot.touch_count,
        client.prop_url("ctx-wisdom"): if slot.wisdom_persisted { "true" } else { "false" },
        "https://atomicdata.dev/properties/isA": [client.class_url("context-slot")],
        "https://atomicdata.dev/properties/parent": client.server_url,
    });

    if let Some(ref summary) = slot.summary {
        set[client.prop_url("ctx-summary")] = serde_json::Value::String(summary.clone());
    }

    let _ = client.upsert(&subject, &set);
}

/// Write session metadata to atomic-server.
fn atomic_write_session(session: &Session) {
    let client = match crate::atomic::AtomicClient::from_env() {
        Some(c) => c,
        None => return,
    };

    let subject = client.ctx_session_url(&session.id);
    let total_tokens: u32 = session.slots.iter().map(|s| s.tokens).sum();
    let set = serde_json::json!({
        "https://atomicdata.dev/properties/name": format!("esc-session {}", session.id),
        "https://atomicdata.dev/properties/description": format!(
            "turn {} | {} slots | {} archived | {} wisdom",
            session.turn, session.slots.len(), session.archived_count, session.wisdom_count
        ),
        client.prop_url("ctx-session"): session.id,
        client.prop_url("ctx-tokens"): total_tokens,
        client.prop_url("ctx-born"): 0,
        client.prop_url("ctx-touched"): session.turn,
        "https://atomicdata.dev/properties/isA": [client.class_url("context-slot")],
        "https://atomicdata.dev/properties/parent": client.server_url,
    });

    let _ = client.upsert(&subject, &set);
}

// --- Internal: tick (advance turn + apply policies) ---

fn tick(session: &mut Session) -> Vec<Transition> {
    session.turn += 1;
    let turn = session.turn;
    let mut transitions = Vec::new();

    for slot in &mut session.slots {
        let age = turn.saturating_sub(slot.touched);

        match slot.state {
            SlotState::Hot if age >= slot.kind.hot_ttl() => {
                let from = slot.state.label().to_string();
                slot.state = SlotState::Warm;
                transitions.push(Transition {
                    slot: slot.id.clone(),
                    kind: slot.kind.label().to_string(),
                    from,
                    to: "warm".to_string(),
                    age,
                });
            }
            SlotState::Warm if age >= slot.kind.warm_ttl() => {
                let from = slot.state.label().to_string();
                slot.state = SlotState::Cold;
                transitions.push(Transition {
                    slot: slot.id.clone(),
                    kind: slot.kind.label().to_string(),
                    from,
                    to: "cold".to_string(),
                    age,
                });
            }
            _ => {}
        }
    }

    transitions
}

/// Auto-extract wisdom: slots with high touch_count + long survival get persisted
/// to the memory graph as permanent knowledge. Returns what was extracted.
fn extract_wisdom(session: &mut Session) -> Vec<WisdomExtracted> {
    let turn = session.turn;
    let session_id = session.id.clone();
    let mut extracted = Vec::new();

    for slot in &mut session.slots {
        if !slot.is_wisdom_candidate(turn) {
            continue;
        }

        let content = slot.effective_content().to_string();
        let kind_label = slot.kind.label();
        let note_kind = match slot.kind.note_kind() {
            Some(k) => k,
            None => continue,
        };

        // Persist to memory graph as high-value knowledge
        let summary = format!(
            "[wisdom:{}] {}",
            kind_label,
            if content.len() > 120 {
                &content[..120]
            } else {
                &content
            }
        );
        let detail = format!(
            "{}\n\n[survived {} turns, touched {} times, session {}]",
            content,
            slot.age(turn),
            slot.touch_count,
            session_id,
        );
        let hash = crate::memory::note_hash(note_kind, &summary);
        crate::memory::record_note(
            note_kind,
            &summary,
            &detail,
            "auto-wisdom",
            &[
                "wisdom".to_string(),
                "auto-extracted".to_string(),
                kind_label.to_string(),
            ],
        );

        slot.wisdom_persisted = true;

        // Dual-write the slot with wisdom flag
        atomic_write_slot(&session_id, slot);

        extracted.push(WisdomExtracted {
            slot: slot.id.clone(),
            content: if content.len() > 80 {
                format!("{}...", &content[..80])
            } else {
                content
            },
            touch_count: slot.touch_count,
            age: slot.age(turn),
            memory_hash: hash[..12].to_string(),
        });

        session.wisdom_count += 1;
    }

    extracted
}

/// Auto-archive cold slots: persist to memory (or drop scratch), remove from session.
/// Without an observer session, cold slots would die silently. This is the safety net.
/// Wisdom-persisted slots are just removed (already saved). Others get archived.
fn auto_archive_cold(session: &mut Session) -> Vec<AutoArchived> {
    let mut results = Vec::new();

    // Collect indices of Cold slots (reverse order so removal doesn't shift indices)
    let cold_indices: Vec<usize> = session
        .slots
        .iter()
        .enumerate()
        .filter(|(_, s)| s.state == SlotState::Cold)
        .map(|(i, _)| i)
        .rev()
        .collect();

    for idx in cold_indices {
        let slot = &session.slots[idx];
        let slot_id = slot.id.clone();
        let kind_label = slot.kind.label().to_string();

        if slot.wisdom_persisted {
            // Already saved to memory graph via wisdom extraction — just remove
            results.push(AutoArchived {
                slot: slot_id,
                kind: kind_label,
                action: "wisdom-archived".to_string(),
            });
            session.slots.remove(idx);
            session.archived_count += 1;
        } else if let Some(note_kind) = slot.kind.note_kind() {
            // Archivable — persist to memory graph
            let content = slot.effective_content().to_string();
            let summary = format!(
                "[ctx:{}] {}",
                kind_label,
                if content.len() > 120 {
                    &content[..120]
                } else {
                    &content
                }
            );
            crate::memory::record_note(
                note_kind,
                &summary,
                &content,
                "context-archive",
                &["auto-archived".to_string()],
            );
            results.push(AutoArchived {
                slot: slot_id,
                kind: kind_label,
                action: "archived".to_string(),
            });
            session.slots.remove(idx);
            session.archived_count += 1;
        } else {
            // Scratch — just drop
            results.push(AutoArchived {
                slot: slot_id,
                kind: kind_label,
                action: "dropped".to_string(),
            });
            session.slots.remove(idx);
        }
    }

    results
}

/// Generate recommendations based on current state.
fn recommend(session: &Session) -> Vec<Recommendation> {
    let total: u32 = session.slots.iter().map(|s| s.tokens).sum();
    let pct = if session.budget > 0 {
        total as f32 / session.budget as f32
    } else {
        0.0
    };
    let mut recs = Vec::new();

    // Compact warm slots with high token count
    for slot in &session.slots {
        if slot.state == SlotState::Warm
            && slot.tokens > COMPACT_THRESHOLD
            && slot.summary.is_none()
        {
            recs.push(Recommendation {
                action: "compact".to_string(),
                slot: slot.id.clone(),
                reason: format!("{} tokens, warm — summarize to save space", slot.tokens),
                tokens: slot.tokens,
            });
        }
    }

    // Archive cold slots under budget pressure
    if pct > ARCHIVE_THRESHOLD {
        let mut cold: Vec<&Slot> = session
            .slots
            .iter()
            .filter(|s| s.state == SlotState::Cold)
            .collect();
        cold.sort_by_key(|s| s.touched); // oldest first

        for slot in cold {
            if slot.kind.note_kind().is_some() {
                recs.push(Recommendation {
                    action: "archive".to_string(),
                    slot: slot.id.clone(),
                    reason: format!(
                        "cold, budget at {:.0}% — persist to memory graph",
                        pct * 100.0
                    ),
                    tokens: slot.tokens,
                });
            } else {
                recs.push(Recommendation {
                    action: "drop".to_string(),
                    slot: slot.id.clone(),
                    reason: format!("cold scratch, budget at {:.0}%", pct * 100.0),
                    tokens: slot.tokens,
                });
            }
        }
    }

    recs
}

// --- Public operations ---

/// Create a new session. Overwrites any existing session.
pub fn init(budget: u32) -> Session {
    let budget = budget.max(1000); // minimum 1K tokens
    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    let session = Session {
        id: format!("ctx-{ts}"),
        turn: 0,
        budget,
        next_slot: 1,
        slots: Vec::new(),
        archived_count: 0,
        wisdom_count: 0,
    };
    save(&session);
    atomic_write_session(&session);
    session
}

/// Add a context slot. Returns (slot_id, token_count).
pub fn add(kind: SlotKind, content: &str) -> Result<(String, u32), String> {
    let mut session = load().ok_or("no active session — run `esc context init` first")?;

    if session.slots.len() >= MAX_SLOTS {
        return Err(format!("slot limit reached (max {})", MAX_SLOTS));
    }

    let content = if content.len() > MAX_CONTENT_BYTES {
        &content[..MAX_CONTENT_BYTES]
    } else {
        content
    };

    let id = slot_id(session.next_slot);
    let tokens = estimate_tokens(content);

    let slot = Slot {
        id: id.clone(),
        kind,
        state: SlotState::Hot,
        content: content.to_string(),
        summary: None,
        tokens,
        born: session.turn,
        touched: session.turn,
        touch_count: 0,
        wisdom_persisted: false,
        source: None,
        update_count: 0,
    };

    // Dual-write to atomic
    atomic_write_slot(&session.id, &slot);

    session.slots.push(slot);
    session.next_slot += 1;

    save(&session);
    Ok((id, tokens))
}

/// Mark a slot as still relevant. Promotes back to hot. Increments touch_count.
pub fn touch(slot_id: &str) -> Result<(), String> {
    let mut session = load().ok_or("no active session")?;
    let session_id = session.id.clone();
    let slot = session
        .slots
        .iter_mut()
        .find(|s| s.id == slot_id)
        .ok_or(format!("slot not found: {slot_id}"))?;

    slot.touched = session.turn;
    slot.touch_count += 1;
    if slot.state != SlotState::Hot {
        slot.state = SlotState::Hot;
    }

    // Dual-write
    atomic_write_slot(&session_id, slot);

    save(&session);
    Ok(())
}

/// Advance turn, apply policies, extract wisdom, return JSON observation.
pub fn observe() -> Result<ObserveResult, String> {
    let mut session = load().ok_or("no active session")?;
    let transitions = tick(&mut session);
    let wisdom = extract_wisdom(&mut session);
    let auto_archived = auto_archive_cold(&mut session);
    let recommendations = recommend(&session);
    let total_tokens: u32 = session.slots.iter().map(|s| s.tokens).sum();
    let budget_pct = if session.budget > 0 {
        total_tokens as f32 / session.budget as f32
    } else {
        0.0
    };
    let slot_count = session.slots.len();

    save(&session);
    atomic_write_session(&session);

    Ok(ObserveResult {
        turn: session.turn,
        total_tokens,
        budget: session.budget,
        budget_pct,
        slot_count,
        transitions,
        recommendations,
        wisdom,
        auto_archived,
    })
}

/// Replace slot content with a summary. Returns (old_tokens, new_tokens).
pub fn compact(slot_id: &str, summary: &str) -> Result<(u32, u32), String> {
    let mut session = load().ok_or("no active session")?;
    let session_id = session.id.clone();
    let slot = session
        .slots
        .iter_mut()
        .find(|s| s.id == slot_id)
        .ok_or(format!("slot not found: {slot_id}"))?;

    let old_tokens = slot.tokens;
    slot.summary = Some(summary.to_string());
    slot.tokens = estimate_tokens(summary);
    let new_tokens = slot.tokens;

    // Dual-write
    atomic_write_slot(&session_id, slot);

    save(&session);
    Ok((old_tokens, new_tokens))
}

/// Persist slot to memory graph, remove from session. Returns (memory_hash, note_kind).
pub fn archive(slot_id: &str) -> Result<(String, String), String> {
    let mut session = load().ok_or("no active session")?;

    let idx = session
        .slots
        .iter()
        .position(|s| s.id == slot_id)
        .ok_or(format!("slot not found: {slot_id}"))?;

    let slot = &session.slots[idx];
    let note_kind = slot
        .kind
        .note_kind()
        .ok_or("scratch slots cannot be archived — use drop")?;

    let content = slot.effective_content().to_string();
    let kind_label = slot.kind.label().to_string();

    // Persist to memory graph
    let summary = format!(
        "[ctx:{}] {}",
        kind_label,
        if content.len() > 120 {
            &content[..120]
        } else {
            &content
        }
    );
    let hash = crate::memory::note_hash(note_kind, &summary);
    crate::memory::record_note(
        note_kind,
        &summary,
        &content,
        "context-archive",
        &["auto-archived".to_string()],
    );

    session.slots.remove(idx);
    session.archived_count += 1;
    save(&session);
    atomic_write_session(&session);

    Ok((hash[..12].to_string(), note_kind.to_string()))
}

/// Remove slot without persisting to memory.
pub fn drop_slot(slot_id: &str) -> Result<(), String> {
    let mut session = load().ok_or("no active session")?;
    let idx = session
        .slots
        .iter()
        .position(|s| s.id == slot_id)
        .ok_or(format!("slot not found: {slot_id}"))?;
    session.slots.remove(idx);
    save(&session);
    Ok(())
}

/// Continuous observation + contextual reasoning output.
///
/// This is the primary entry point for the model. Each call:
/// 1. Advances the turn (continuous observation)
/// 2. Applies deterministic lifecycle policies
/// 3. Auto-extracts wisdom (high-touch, long-survived knowledge → permanent memory)
/// 4. Outputs curated context with reasoning + recommendations + augmentation hints
///
/// The model reads this to understand what it knows, what's aging, and when to
/// augment from the knowledge graph.
pub fn assemble() -> Result<String, String> {
    let mut session = load().ok_or("no active session")?;

    // Continuous observation: advance turn and apply policies
    let transitions = tick(&mut session);
    let wisdom = extract_wisdom(&mut session);
    let auto_archived = auto_archive_cold(&mut session);
    let recommendations = recommend(&session);
    let total_tokens: u32 = session.slots.iter().map(|s| s.tokens).sum();
    let budget_pct = if session.budget > 0 {
        total_tokens as f32 / session.budget as f32
    } else {
        0.0
    };
    save(&session);
    atomic_write_session(&session);

    // --- Build contextual reasoning output ---

    let mut out = format!(
        "# Context [turn {} | {}K/{}K tokens ({:.0}%) | {} slots | {} archived | {} wisdom]\n\n",
        session.turn,
        total_tokens / 1000,
        session.budget / 1000,
        budget_pct * 100.0,
        session.slots.len(),
        session.archived_count,
        session.wisdom_count,
    );

    // Group slots
    let mut tasks: Vec<&Slot> = Vec::new();
    let mut hot: Vec<&Slot> = Vec::new();
    let mut warm: Vec<&Slot> = Vec::new();
    let mut cold: Vec<&Slot> = Vec::new();

    for slot in &session.slots {
        if slot.kind == SlotKind::Task {
            tasks.push(slot);
        } else {
            match slot.state {
                SlotState::Hot => hot.push(slot),
                SlotState::Warm => warm.push(slot),
                SlotState::Cold => cold.push(slot),
            }
        }
    }

    // Tasks always first
    if !tasks.is_empty() {
        out.push_str("## Active Task\n");
        for s in &tasks {
            out.push_str(&format!(
                "- [{}|{}] {}\n",
                s.id,
                s.state.label(),
                s.effective_content()
            ));
        }
        out.push('\n');
    }

    if !hot.is_empty() {
        out.push_str("## Hot — actively relevant\n");
        for s in &hot {
            let w = if s.touch_count >= WISDOM_TOUCH_THRESHOLD {
                format!(" ({}x touched)", s.touch_count)
            } else {
                String::new()
            };
            out.push_str(&format!(
                "- [{}|{}{}] {}\n",
                s.id,
                s.kind.label(),
                w,
                s.effective_content()
            ));
        }
        out.push('\n');
    }

    if !warm.is_empty() {
        out.push_str("## Warm — aging, evaluate relevance\n");
        for s in &warm {
            let age = session.turn.saturating_sub(s.touched);
            out.push_str(&format!(
                "- [{}|{}|{}t ago] {}\n",
                s.id,
                s.kind.label(),
                age,
                s.effective_content()
            ));
        }
        out.push('\n');
    }

    if !cold.is_empty() {
        out.push_str("## Cold — archive or drop\n");
        for s in &cold {
            let text = s.effective_content();
            let display = if text.len() > 120 { &text[..120] } else { text };
            out.push_str(&format!("- [{}|{}] {}\n", s.id, s.kind.label(), display));
        }
        out.push('\n');
    }

    // Wisdom extracted this turn
    if !wisdom.is_empty() {
        out.push_str("## Wisdom extracted (auto-persisted to memory graph)\n");
        for w in &wisdom {
            out.push_str(&format!(
                "- {} — {}x touched, {} turns old -> memory {}\n",
                w.slot, w.touch_count, w.age, w.memory_hash
            ));
        }
        out.push('\n');
    }

    // Transitions this turn
    if !transitions.is_empty() {
        out.push_str("## Transitions\n");
        for t in &transitions {
            out.push_str(&format!(
                "- {} ({}) {} -> {} ({} turns idle)\n",
                t.slot, t.kind, t.from, t.to, t.age
            ));
        }
        out.push('\n');
    }

    // Auto-archived cold slots
    if !auto_archived.is_empty() {
        out.push_str("## Auto-archived (cold → memory)\n");
        for a in &auto_archived {
            out.push_str(&format!("- {} ({}) — {}\n", a.slot, a.kind, a.action));
        }
        out.push('\n');
    }

    // Recommendations
    if !recommendations.is_empty() {
        out.push_str("## Recommended\n");
        for r in &recommendations {
            out.push_str(&format!(
                "- `esc context {} {}` — {} ({}tok)\n",
                r.action, r.slot, r.reason, r.tokens
            ));
        }
        out.push('\n');
    }

    // Augmentation hint: extract keywords from active task
    if let Some(task) = tasks.first() {
        let words: Vec<&str> = task
            .effective_content()
            .split_whitespace()
            .filter(|w| w.len() > 3)
            .take(5)
            .collect();
        if !words.is_empty() {
            out.push_str(&format!(
                "## Augment\n`esc context recall \"{}\"`\n\n",
                words.join(" ")
            ));
        }
    }

    if session.slots.is_empty() {
        out.push_str("Empty session. Use `esc context add --kind <kind> \"content\"`.\n");
        out.push_str("Kinds: task, result, error, knowledge, scratch\n");
    }

    Ok(out)
}

/// Session status as JSON.
pub fn status() -> Result<serde_json::Value, String> {
    let session = load().ok_or("no active session")?;

    let total_tokens: u32 = session.slots.iter().map(|s| s.tokens).sum();
    let hot = session
        .slots
        .iter()
        .filter(|s| s.state == SlotState::Hot)
        .count();
    let warm = session
        .slots
        .iter()
        .filter(|s| s.state == SlotState::Warm)
        .count();
    let cold = session
        .slots
        .iter()
        .filter(|s| s.state == SlotState::Cold)
        .count();

    let budget_pct = if session.budget > 0 {
        (total_tokens as f32 / session.budget as f32 * 100.0).round()
    } else {
        0.0
    };

    Ok(serde_json::json!({
        "session": session.id,
        "turn": session.turn,
        "budget": session.budget,
        "tokens_used": total_tokens,
        "budget_pct": budget_pct,
        "slots": session.slots.len(),
        "hot": hot,
        "warm": warm,
        "cold": cold,
        "archived": session.archived_count,
        "wisdom": session.wisdom_count,
    }))
}

// --- Auto-classification (deterministic, no LLM) ---

/// Classify raw text into a slot kind based on content patterns.
fn auto_classify(text: &str) -> SlotKind {
    let lower = text.to_lowercase();

    // Error patterns
    if lower.contains("error")
        || lower.contains("failed")
        || lower.contains("cannot")
        || lower.contains("panic")
        || lower.contains("fatal")
        || lower.contains("not found")
        || lower.contains("undefined")
        || lower.contains("exit code")
        || (lower.contains("warning") && lower.contains(":"))
    {
        return SlotKind::Error;
    }

    // Result patterns (success, output, completion)
    if lower.contains("ok:")
        || lower.contains("success")
        || lower.contains("compiled")
        || lower.contains("finished")
        || lower.contains("created")
        || lower.contains("-> ")
        || lower.starts_with('{')
    {
        return SlotKind::Result;
    }

    // Short text is scratch
    if text.len() < 40 {
        return SlotKind::Scratch;
    }

    // Longer text defaults to knowledge
    SlotKind::Knowledge
}

/// Find an existing slot with high content overlap. Returns slot index if found.
/// Uses word-level intersection — deterministic, no embeddings needed.
///
/// Dedup rules (tuned to avoid false matches):
/// - Words > 2 chars (catches "api", "cli", "fix", "bug", "add", etc.)
/// - Minimum 3 absolute word overlaps (prevents short texts clobbering long ones)
/// - >40% overlap ratio using min(new, existing) as denominator
fn find_related_slot(session: &Session, text: &str, kind: SlotKind) -> Option<usize> {
    let new_words: HashSet<&str> = text.split_whitespace().filter(|w| w.len() > 2).collect();

    if new_words.len() < 3 {
        // Too few meaningful words — can't reliably match
        return None;
    }

    let mut best_idx = None;
    let mut best_overlap = 0usize;

    for (i, slot) in session.slots.iter().enumerate() {
        // Only match same kind
        if slot.kind != kind {
            continue;
        }

        let slot_words: HashSet<&str> = slot
            .content
            .split_whitespace()
            .filter(|w| w.len() > 2)
            .collect();

        if slot_words.len() < 3 {
            continue;
        }

        let overlap = new_words.intersection(&slot_words).count();

        // Must have at least 3 words in common (absolute floor)
        if overlap < 3 {
            continue;
        }

        let min_size = new_words.len().min(slot_words.len()).max(1);

        // >40% word overlap = same topic, update instead of create
        if overlap * 100 / min_size > 40 && overlap > best_overlap {
            best_overlap = overlap;
            best_idx = Some(i);
        }
    }

    best_idx
}

// --- Ingest result ---

#[derive(Debug, Serialize)]
pub struct IngestResult {
    pub action: String,
    pub slot_id: String,
    pub kind: String,
    pub tokens: u32,
    pub previous_tokens: Option<u32>,
}

/// Update an existing slot's content in place. Preserves history (born, touch_count).
/// Promotes to hot, clears summary, increments update_count.
pub fn update(slot_id: &str, content: &str, source: Option<&str>) -> Result<(u32, u32), String> {
    let mut session = load().ok_or("no active session")?;
    let session_id = session.id.clone();

    let content = if content.len() > MAX_CONTENT_BYTES {
        &content[..MAX_CONTENT_BYTES]
    } else {
        content
    };

    let slot = session
        .slots
        .iter_mut()
        .find(|s| s.id == slot_id)
        .ok_or(format!("slot not found: {slot_id}"))?;

    let old_tokens = slot.tokens;
    slot.content = content.to_string();
    slot.tokens = estimate_tokens(content);
    slot.summary = None; // content changed, summary is stale
    slot.touched = session.turn;
    slot.update_count += 1;
    slot.state = SlotState::Hot; // updated = actively relevant
    if let Some(src) = source {
        slot.source = Some(src.to_string());
    }
    let new_tokens = slot.tokens;

    atomic_write_slot(&session_id, slot);
    save(&session);

    Ok((old_tokens, new_tokens))
}

/// Conversation interceptor: takes raw text, auto-classifies, finds related
/// existing slot and updates it OR creates a new one.
///
/// This is the bridge between conversation flow and context management.
/// The model (or a wrapper) pipes tool outputs through this — no manual
/// slot management needed.
///
/// ```bash
/// cargo build 2>&1 | esc context ingest --source "cargo build"
/// esc context ingest --kind knowledge "HTTP needs net_read capability"
/// ```
pub fn ingest(
    text: &str,
    kind_override: Option<SlotKind>,
    source: Option<&str>,
) -> Result<IngestResult, String> {
    let mut session = load().ok_or("no active session — run `esc context init` first")?;
    let session_id = session.id.clone();

    let text = text.trim();
    if text.is_empty() {
        return Err("empty content".to_string());
    }

    let text = if text.len() > MAX_CONTENT_BYTES {
        &text[..MAX_CONTENT_BYTES]
    } else {
        text
    };

    let kind = kind_override.unwrap_or_else(|| auto_classify(text));

    // Check for related existing slot — update instead of duplicate
    if let Some(idx) = find_related_slot(&session, text, kind) {
        let slot = &mut session.slots[idx];
        let old_tokens = slot.tokens;
        slot.content = text.to_string();
        slot.tokens = estimate_tokens(text);
        slot.touched = session.turn;
        slot.update_count += 1;
        slot.state = SlotState::Hot;
        slot.summary = None;
        if let Some(src) = source {
            slot.source = Some(src.to_string());
        }

        let result = IngestResult {
            action: "updated".to_string(),
            slot_id: slot.id.clone(),
            kind: kind.label().to_string(),
            tokens: slot.tokens,
            previous_tokens: Some(old_tokens),
        };

        atomic_write_slot(&session_id, &session.slots[idx]);
        save(&session);
        return Ok(result);
    }

    // No match — create new slot
    if session.slots.len() >= MAX_SLOTS {
        return Err(format!("slot limit reached (max {})", MAX_SLOTS));
    }

    let id = slot_id(session.next_slot);
    let tokens = estimate_tokens(text);

    let slot = Slot {
        id: id.clone(),
        kind,
        state: SlotState::Hot,
        content: text.to_string(),
        summary: None,
        tokens,
        born: session.turn,
        touched: session.turn,
        touch_count: 0,
        wisdom_persisted: false,
        source: source.map(|s| s.to_string()),
        update_count: 0,
    };

    atomic_write_slot(&session_id, &slot);
    session.slots.push(slot);
    session.next_slot += 1;
    save(&session);

    Ok(IngestResult {
        action: "created".to_string(),
        slot_id: id,
        kind: kind.label().to_string(),
        tokens,
        previous_tokens: None,
    })
}

// --- Feed / Watch: active session → observer session pipeline ---

fn feed_path() -> PathBuf {
    session_dir().join("feed.jsonl")
}

fn cursor_path() -> PathBuf {
    session_dir().join("feed_cursor")
}

/// Append an event to the feed file. Called by the active session after tool calls.
/// Silent on success — no stdout, no stderr. Minimal friction.
/// Auto-creates session if none exists — zero manual setup.
/// Optional `kind` overrides auto-classification (discovery, decision, pattern, issue, etc.)
pub fn feed(source: &str, content: &str, kind: Option<&str>) -> Result<(), String> {
    // Auto-init: first feed bootstraps the session
    if load().is_none() {
        init(100_000);
    }
    let dir = session_dir();
    let _ = fs::create_dir_all(&dir);

    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();

    let mut entry = serde_json::json!({
        "ts": ts,
        "source": source,
        "content": content,
    });
    if let Some(k) = kind {
        entry["kind"] = serde_json::Value::String(k.to_string());
    }

    let mut line = serde_json::to_string(&entry).map_err(|e| format!("json: {e}"))?;
    line.push('\n');

    let mut f = fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(feed_path())
        .map_err(|e| format!("cannot write feed: {e}"))?;
    f.write_all(line.as_bytes())
        .map_err(|e| format!("feed write: {e}"))?;

    Ok(())
}

#[derive(Debug, Serialize)]
pub struct WatchResult {
    pub processed: usize,
    pub ingested: Vec<IngestResult>,
    pub observe: Option<ObserveResult>,
}

/// Process new feed entries since last watch, ingest them into the session,
/// and run an observe cycle. Called by the observer session.
///
/// The observer (Opus) reads this output, reasons about the context state,
/// and decides what actions to take (compact, archive, recall, etc.).
pub fn watch() -> Result<WatchResult, String> {
    // Session must exist for ingestion
    let _ = load().ok_or("no active session — run `esc context init` first")?;

    let fp = feed_path();
    if !fp.exists() {
        return Ok(WatchResult {
            processed: 0,
            ingested: Vec::new(),
            observe: None,
        });
    }

    // Read cursor (byte offset of last processed position)
    let cursor: usize = fs::read_to_string(cursor_path())
        .ok()
        .and_then(|s| s.trim().parse().ok())
        .unwrap_or(0);

    let feed_content = fs::read_to_string(&fp).map_err(|e| format!("cannot read feed: {e}"))?;

    // If feed was truncated/reset, start from 0
    let start = if cursor > feed_content.len() {
        0
    } else {
        cursor
    };
    let new_content = &feed_content[start..];

    if new_content.trim().is_empty() {
        return Ok(WatchResult {
            processed: 0,
            ingested: Vec::new(),
            observe: None,
        });
    }

    let mut results = Vec::new();

    for line in new_content.lines() {
        if line.trim().is_empty() {
            continue;
        }

        let entry: serde_json::Value = match serde_json::from_str(line) {
            Ok(v) => v,
            Err(_) => continue, // skip malformed
        };

        let content = entry["content"].as_str().unwrap_or("");
        let source = entry["source"].as_str();

        if content.trim().is_empty() {
            continue;
        }

        // Honor explicit kind from feed entry, overriding auto-classification
        let kind_override = entry["kind"].as_str().and_then(|k| SlotKind::parse(k).ok());

        match ingest(content, kind_override, source) {
            Ok(r) => results.push(r),
            Err(_) => continue,
        }
    }

    // Update cursor to end of file
    let _ = fs::write(cursor_path(), feed_content.len().to_string());

    // Run observe cycle if we ingested anything
    let obs = if !results.is_empty() {
        observe().ok()
    } else {
        None
    };

    Ok(WatchResult {
        processed: results.len(),
        ingested: results,
        observe: obs,
    })
}

/// Reset the feed (clear feed file and cursor). For session cleanup.
pub fn feed_clear() {
    let _ = fs::remove_file(feed_path());
    let _ = fs::remove_file(cursor_path());
}

/// Search memory graph for knowledge augmentation.
/// The model decides when to call this and what to incorporate.
/// Searches across tools, notes, AND context history in atomic-server.
pub fn recall(query: &str, limit: usize) -> serde_json::Value {
    let results = crate::memory::recall(query, limit);
    serde_json::json!({
        "query": query,
        "results": results,
        "count": results.len(),
        "hint": "Use `esc context add --kind knowledge \"<insight>\"` to incorporate"
    })
}
