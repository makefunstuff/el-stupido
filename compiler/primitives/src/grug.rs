use std::fs;
use std::path::Path;

/// A single entry in a .grug file, identified by an ID with key-value fields.
#[derive(Debug, Clone)]
pub struct GrugEntry {
    pub id: String,
    pub fields: Vec<(String, String)>,
}

impl GrugEntry {
    /// Get a field value by key, or empty string if not found.
    pub fn get(&self, key: &str) -> &str {
        self.fields
            .iter()
            .find(|(k, _)| k == key)
            .map(|(_, v)| v.as_str())
            .unwrap_or("")
    }
}

/// Line-based key-value persistence backed by a .grug file.
///
/// File format:
/// ```text
/// [1]
/// name = Alice
/// msg = Hello world
///
/// [2]
/// name = Bob
/// msg = Grug is great
/// ```
pub struct GrugStore {
    path: String,
    pub entries: Vec<GrugEntry>,
}

impl GrugStore {
    /// Open a .grug file, parsing any existing entries.
    pub fn open(path: &str) -> Self {
        let mut store = Self {
            path: path.to_string(),
            entries: Vec::new(),
        };
        if Path::new(path).exists() {
            if let Ok(content) = fs::read_to_string(path) {
                store.parse(&content);
            }
        }
        store
    }

    fn parse(&mut self, content: &str) {
        let mut current: Option<GrugEntry> = None;
        for line in content.lines() {
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            if line.starts_with('[') && line.ends_with(']') {
                if let Some(entry) = current.take() {
                    self.entries.push(entry);
                }
                let id = line[1..line.len() - 1].to_string();
                current = Some(GrugEntry {
                    id,
                    fields: Vec::new(),
                });
            } else if let Some(ref mut entry) = current {
                if let Some(eq) = line.find('=') {
                    let key = line[..eq].trim().to_string();
                    let val = line[eq + 1..].trim().to_string();
                    entry.fields.push((key, val));
                }
            }
        }
        if let Some(entry) = current {
            self.entries.push(entry);
        }
    }

    /// Write all entries back to the .grug file.
    pub fn save(&self) {
        let mut out = String::new();
        for entry in &self.entries {
            out.push_str(&format!("[{}]\n", entry.id));
            for (k, v) in &entry.fields {
                out.push_str(&format!("{} = {}\n", k, v));
            }
            out.push('\n');
        }
        let _ = fs::write(&self.path, out);
    }

    /// Create a new entry from URL-encoded form body, extracting the given field names.
    pub fn create_from_form(&mut self, body: &str, field_names: &[&str]) {
        let id = self.next_id();
        let mut entry = GrugEntry {
            id: id.to_string(),
            fields: Vec::new(),
        };
        for &name in field_names {
            let val = form_value(body, name);
            entry.fields.push((name.to_string(), val));
        }
        self.entries.push(entry);
        self.save();
    }

    /// Delete entry by ID and save.
    pub fn delete(&mut self, id: &str) {
        self.entries.retain(|e| e.id != id);
        self.save();
    }

    /// Serialize all entries to a JSON array string.
    pub fn to_json(&self) -> String {
        let mut json = String::from("[\n");
        for (i, entry) in self.entries.iter().enumerate() {
            json.push_str("  {\n");
            json.push_str(&format!("    \"id\": \"{}\"", json_escape(&entry.id)));
            for (k, v) in &entry.fields {
                json.push_str(&format!(
                    ",\n    \"{}\": \"{}\"",
                    json_escape(k),
                    json_escape(v)
                ));
            }
            json.push_str("\n  }");
            if i < self.entries.len() - 1 {
                json.push(',');
            }
            json.push('\n');
        }
        json.push(']');
        json
    }

    fn next_id(&self) -> u32 {
        self.entries
            .iter()
            .filter_map(|e| e.id.parse::<u32>().ok())
            .max()
            .unwrap_or(0)
            + 1
    }
}

/// Decode a percent-encoded URL string (also handles + as space).
pub fn url_decode(s: &str) -> String {
    let mut result = String::with_capacity(s.len());
    let mut bytes = s.bytes();
    while let Some(b) = bytes.next() {
        match b {
            b'%' => {
                let h1 = bytes.next().unwrap_or(0);
                let h2 = bytes.next().unwrap_or(0);
                let hex = [h1, h2];
                if let Ok(s) = std::str::from_utf8(&hex) {
                    if let Ok(v) = u8::from_str_radix(s, 16) {
                        result.push(v as char);
                    }
                }
            }
            b'+' => result.push(' '),
            _ => result.push(b as char),
        }
    }
    result
}

/// Extract a single form field value from a URL-encoded body.
pub fn form_value(body: &str, key: &str) -> String {
    let search = format!("{}=", key);
    if let Some(start) = body.find(&search) {
        let val_start = start + search.len();
        let val_end = body[val_start..]
            .find('&')
            .map(|i| val_start + i)
            .unwrap_or(body.len());
        url_decode(&body[val_start..val_end])
    } else {
        String::new()
    }
}

fn json_escape(s: &str) -> String {
    s.replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('\n', "\\n")
        .replace('\r', "\\r")
        .replace('\t', "\\t")
}
