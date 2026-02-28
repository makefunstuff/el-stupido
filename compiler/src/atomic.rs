//! Atomic-server client — signed commits + authenticated reads.
//!
//! Configured via environment:
//!   ESC_ATOMIC_URL  — server base URL (e.g. http://192.168.1.128:9884)
//!   ESC_ATOMIC_KEY  — agent private key (base64 Ed25519)
//!
//! Uses two query mechanisms:
//!   /search — full-text search (tantivy, indexes name+description)
//!   /query  — structured property+value filtering (sled indexes, server-side)

use base64::prelude::*;
use ed25519_dalek::{Signer, SigningKey};
use serde_json::Value;
use std::process::Command;

// Well-known Atomic Data property URLs
const PROP_IS_A: &str = "https://atomicdata.dev/properties/isA";
const PROP_SHORTNAME: &str = "https://atomicdata.dev/properties/shortname";
const PROP_DESCRIPTION: &str = "https://atomicdata.dev/properties/description";
const PROP_DATATYPE: &str = "https://atomicdata.dev/properties/datatype";
const PROP_PARENT: &str = "https://atomicdata.dev/properties/parent";
const PROP_REQUIRES: &str = "https://atomicdata.dev/properties/requires";
const PROP_RECOMMENDS: &str = "https://atomicdata.dev/properties/recommends";

const PROP_SUBJECT: &str = "https://atomicdata.dev/properties/subject";
const PROP_SIGNER: &str = "https://atomicdata.dev/properties/signer";
const PROP_SIGNATURE: &str = "https://atomicdata.dev/properties/signature";
const PROP_CREATED_AT: &str = "https://atomicdata.dev/properties/createdAt";
const PROP_SET: &str = "https://atomicdata.dev/properties/set";
const PROP_PREVIOUS_COMMIT: &str = "https://atomicdata.dev/properties/previousCommit";
const PROP_LAST_COMMIT: &str = "https://atomicdata.dev/properties/lastCommit";

const CLASS_COMMIT: &str = "https://atomicdata.dev/classes/Commit";
const CLASS_PROPERTY: &str = "https://atomicdata.dev/classes/Property";
const CLASS_CLASS: &str = "https://atomicdata.dev/classes/Class";

const DT_STRING: &str = "https://atomicdata.dev/datatypes/string";

pub struct AtomicClient {
    pub server_url: String,
    agent_url: String,
    public_key_b64: String,
    signing_key: SigningKey,
}

impl AtomicClient {
    /// Create client from ESC_ATOMIC_URL + ESC_ATOMIC_KEY env vars.
    /// Returns None if either is missing.
    pub fn from_env() -> Option<Self> {
        let server_url = std::env::var("ESC_ATOMIC_URL").ok()?;
        let private_key_b64 = std::env::var("ESC_ATOMIC_KEY").ok()?;

        let key_bytes = BASE64_STANDARD.decode(private_key_b64.as_bytes()).ok()?;
        let key_array: [u8; 32] = key_bytes.try_into().ok()?;
        let signing_key = SigningKey::from_bytes(&key_array);
        let verifying_key = signing_key.verifying_key();
        let public_key_b64 = BASE64_STANDARD.encode(verifying_key.as_bytes());

        let server = server_url.trim_end_matches('/').to_string();
        let agent_url = format!("{server}/agents/{public_key_b64}");

        Some(AtomicClient {
            server_url: server,
            agent_url,
            public_key_b64,
            signing_key,
        })
    }

    fn sign(&self, message: &str) -> String {
        let sig = self.signing_key.sign(message.as_bytes());
        BASE64_STANDARD.encode(sig.to_bytes())
    }

    fn auth_headers(&self, url: &str) -> Vec<(String, String)> {
        let ts = now_millis();
        let sig = self.sign(&format!("{url} {ts}"));
        vec![
            ("x-atomic-public-key".into(), self.public_key_b64.clone()),
            ("x-atomic-signature".into(), sig),
            ("x-atomic-timestamp".into(), ts.to_string()),
            ("x-atomic-agent".into(), self.agent_url.clone()),
        ]
    }

    /// GET a resource as JSON-AD.
    pub fn get(&self, url: &str) -> Result<Value, String> {
        let headers = self.auth_headers(url);
        let (code, body) = curl_get(url, &headers)?;
        if code >= 200 && code < 300 {
            serde_json::from_str(&body).map_err(|e| format!("json: {e}"))
        } else {
            Err(format!("HTTP {code}: {body}"))
        }
    }

    /// Check if a resource exists.
    pub fn exists(&self, url: &str) -> bool {
        self.get(url).is_ok()
    }

    // --- Querying ---

    /// Full-text search via /search endpoint (tantivy).
    /// Returns resolved resources matching the note URL prefix.
    pub fn search(&self, query: &str, limit: usize) -> Result<Vec<Value>, String> {
        let url = format!(
            "{}/search?q={}&limit={}",
            self.server_url,
            urlenc(query),
            limit
        );
        let result = self.get(&url)?;

        let members = result
            .get("https://atomicdata.dev/properties/endpoint/results")
            .and_then(|v| v.as_array())
            .cloned()
            .unwrap_or_default();

        let prefix = format!("{}/esc/note/", self.server_url);
        let mut resolved = Vec::new();
        for member in members {
            if let Some(url) = member.as_str() {
                if url.starts_with(&prefix) {
                    if let Ok(r) = self.get(url) {
                        resolved.push(r);
                    }
                }
            }
        }
        Ok(resolved)
    }

    // --- Writes ---

    /// Create a new resource.
    pub fn create(&self, subject: &str, set: &Value) -> Result<(), String> {
        self.do_commit(subject, set, None)
    }

    /// Create or update.
    pub fn upsert(&self, subject: &str, set: &Value) -> Result<(), String> {
        match self.get(subject) {
            Ok(resource) => {
                let prev = resource.get(PROP_LAST_COMMIT).and_then(|v| v.as_str());
                self.do_commit(subject, set, prev)
            }
            Err(_) => self.do_commit(subject, set, None),
        }
    }

    fn do_commit(&self, subject: &str, set: &Value, previous: Option<&str>) -> Result<(), String> {
        let ts = now_millis();

        let mut fields = serde_json::Map::new();
        fields.insert(PROP_CREATED_AT.into(), Value::Number(ts.into()));
        fields.insert(PROP_IS_A.into(), serde_json::json!([CLASS_COMMIT]));
        if let Some(prev) = previous {
            fields.insert(PROP_PREVIOUS_COMMIT.into(), Value::String(prev.into()));
        }
        fields.insert(PROP_SET.into(), set.clone());
        fields.insert(PROP_SIGNER.into(), Value::String(self.agent_url.clone()));
        fields.insert(PROP_SUBJECT.into(), Value::String(subject.into()));

        let sign_str = deterministic_json(&Value::Object(fields.clone()));
        let signature = self.sign(&sign_str);

        fields.insert(PROP_SIGNATURE.into(), Value::String(signature.clone()));
        fields.insert(
            "@id".into(),
            Value::String(format!("{}/commits/{}", self.server_url, signature)),
        );

        let body =
            serde_json::to_string(&Value::Object(fields)).map_err(|e| format!("json: {e}"))?;

        let (code, resp) = curl_post(&format!("{}/commit", self.server_url), &body, &[])?;
        if code >= 200 && code < 300 {
            Ok(())
        } else {
            Err(format!("commit HTTP {code}: {resp}"))
        }
    }

    /// Destroy a resource on atomic-server via signed commit.
    pub fn destroy(&self, subject: &str) -> Result<(), String> {
        let resource = self.get(subject)?;
        let prev = resource
            .get(PROP_LAST_COMMIT)
            .and_then(|v| v.as_str())
            .ok_or_else(|| "no lastCommit on resource".to_string())?;

        let ts = now_millis();
        let mut fields = serde_json::Map::new();
        fields.insert(PROP_CREATED_AT.into(), Value::Number(ts.into()));
        fields.insert(PROP_IS_A.into(), serde_json::json!([CLASS_COMMIT]));
        fields.insert(PROP_PREVIOUS_COMMIT.into(), Value::String(prev.into()));
        fields.insert(PROP_SIGNER.into(), Value::String(self.agent_url.clone()));
        fields.insert(PROP_SUBJECT.into(), Value::String(subject.into()));
        fields.insert(
            "https://atomicdata.dev/properties/destroy".into(),
            Value::Bool(true),
        );

        let sign_str = deterministic_json(&Value::Object(fields.clone()));
        let signature = self.sign(&sign_str);

        fields.insert(PROP_SIGNATURE.into(), Value::String(signature.clone()));
        fields.insert(
            "@id".into(),
            Value::String(format!("{}/commits/{}", self.server_url, signature)),
        );

        let body =
            serde_json::to_string(&Value::Object(fields)).map_err(|e| format!("json: {e}"))?;

        let (code, resp) = curl_post(&format!("{}/commit", self.server_url), &body, &[])?;
        if code >= 200 && code < 300 {
            Ok(())
        } else {
            Err(format!("destroy HTTP {code}: {resp}"))
        }
    }

    // --- URL helpers ---

    pub fn prop_url(&self, name: &str) -> String {
        format!("{}/esc/prop/{name}", self.server_url)
    }

    pub fn class_url(&self, name: &str) -> String {
        format!("{}/esc/class/{name}", self.server_url)
    }

    pub fn note_url(&self, hash: &str) -> String {
        let short = &hash[..12.min(hash.len())];
        format!("{}/esc/note/{short}", self.server_url)
    }

    // --- Schema bootstrap ---

    /// Create esc note properties + class on atomic-server. Idempotent.
    pub fn ensure_schema(&self) -> Result<(), String> {
        let s = &self.server_url;

        // Note schema (sentinel: "note-kind" property)
        if !self.exists(&self.prop_url("note-kind")) {
            let note_props: &[(&str, &str)] = &[
                (
                    "note-kind",
                    "Note kind: discovery, decision, pattern, issue",
                ),
                ("note-summary", "One-line note summary"),
                ("note-detail", "Longer explanation or detail"),
                ("note-context", "Project or area context"),
                ("tags", "Comma-separated search tags"),
                ("created", "Creation timestamp (ISO 8601)"),
                ("status", "Status: active, resolved, superseded"),
            ];

            for (name, desc) in note_props {
                self.create(
                    &self.prop_url(name),
                    &serde_json::json!({
                        PROP_IS_A: [CLASS_PROPERTY],
                        PROP_SHORTNAME: format!("esc-{name}"),
                        PROP_DESCRIPTION: *desc,
                        PROP_DATATYPE: DT_STRING,
                        PROP_PARENT: s,
                    }),
                )?;
            }

            self.create(
                &self.class_url("note"),
                &serde_json::json!({
                    PROP_IS_A: [CLASS_CLASS],
                    PROP_SHORTNAME: "esc-note",
                    PROP_DESCRIPTION: "Contextual knowledge: discoveries, decisions, patterns, issues",
                    PROP_REQUIRES: [self.prop_url("note-kind"), self.prop_url("note-summary")],
                    PROP_RECOMMENDS: [
                        self.prop_url("note-detail"),
                        self.prop_url("note-context"),
                        self.prop_url("tags"),
                        self.prop_url("created"),
                        self.prop_url("status"),
                    ],
                    PROP_PARENT: s,
                }),
            )?;
        }

        Ok(())
    }
}

// --- Helpers ---

fn now_millis() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_millis() as u64
}

fn urlenc(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char)
            }
            _ => {
                out.push_str(&format!("%{:02X}", b));
            }
        }
    }
    out
}

/// Deterministic JSON-AD: sorted keys, minified, no empty containers.
fn deterministic_json(value: &Value) -> String {
    match value {
        Value::Object(map) => {
            let mut sorted: Vec<(&String, &Value)> = map.iter().collect();
            sorted.sort_by_key(|(k, _)| *k);
            let pairs: Vec<String> = sorted
                .into_iter()
                .filter(|(_, v)| !is_empty_container(v))
                .map(|(k, v)| {
                    format!(
                        "{}:{}",
                        serde_json::to_string(k).unwrap(),
                        deterministic_json(v)
                    )
                })
                .collect();
            format!("{{{}}}", pairs.join(","))
        }
        Value::Array(arr) => {
            let items: Vec<String> = arr.iter().map(deterministic_json).collect();
            format!("[{}]", items.join(","))
        }
        _ => serde_json::to_string(value).unwrap_or_default(),
    }
}

fn is_empty_container(v: &Value) -> bool {
    matches!(v, Value::Array(a) if a.is_empty()) || matches!(v, Value::Object(o) if o.is_empty())
}

fn curl_get(url: &str, headers: &[(String, String)]) -> Result<(u16, String), String> {
    let mut cmd = Command::new("curl");
    cmd.arg("-s")
        .arg("--connect-timeout")
        .arg("2")
        .arg("--max-time")
        .arg("5")
        .arg("-w")
        .arg("\n%{http_code}")
        .arg("-H")
        .arg("Accept: application/ad+json");
    for (k, v) in headers {
        cmd.arg("-H").arg(format!("{k}: {v}"));
    }
    cmd.arg(url);
    let output = cmd.output().map_err(|e| format!("curl: {e}"))?;
    parse_curl_output(&output.stdout)
}

fn curl_post(url: &str, body: &str, headers: &[(String, String)]) -> Result<(u16, String), String> {
    let mut cmd = Command::new("curl");
    cmd.arg("-s")
        .arg("--connect-timeout")
        .arg("2")
        .arg("--max-time")
        .arg("5")
        .arg("-w")
        .arg("\n%{http_code}")
        .arg("-X")
        .arg("POST")
        .arg("-H")
        .arg("Content-Type: application/ad+json")
        .arg("-H")
        .arg("Accept: application/ad+json");
    for (k, v) in headers {
        cmd.arg("-H").arg(format!("{k}: {v}"));
    }
    cmd.arg("-d").arg(body);
    cmd.arg(url);
    let output = cmd.output().map_err(|e| format!("curl: {e}"))?;
    parse_curl_output(&output.stdout)
}

fn parse_curl_output(stdout: &[u8]) -> Result<(u16, String), String> {
    let text = String::from_utf8_lossy(stdout).to_string();
    let (body, code_str) = text.rsplit_once('\n').ok_or("empty curl response")?;
    let code: u16 = code_str
        .trim()
        .parse()
        .map_err(|_| format!("bad http status: '{code_str}'"))?;
    Ok((code, body.to_string()))
}
