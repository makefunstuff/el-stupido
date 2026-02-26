use sha2::{Digest, Sha256};
use std::fs;
use std::io::{Read, Seek, SeekFrom};
use std::path::PathBuf;

const TRAILER_MAGIC: &[u8; 8] = b"ESCMETA\0";

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct CachedTool {
    pub hash: String,
    pub app: String,
    pub binary_path: String,
    pub manifest_source: String,
    pub canonical_tape: String,
    pub capabilities: Vec<String>,
    pub inputs: Vec<serde_json::Value>,
    pub outputs: Vec<serde_json::Value>,
    pub binary_size: u64,
    pub rust_size: u64,
}

fn cache_dir() -> PathBuf {
    dirs_or_home().join(".esc")
}

fn dirs_or_home() -> PathBuf {
    std::env::var("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from("."))
}

fn registry_path() -> PathBuf {
    cache_dir().join("tools.json")
}

pub fn bin_path(hash: &str) -> String {
    let dir = cache_dir().join("bin");
    dir.join(hash).to_string_lossy().into_owned()
}

pub fn ensure_dirs() {
    let base = cache_dir();
    let _ = fs::create_dir_all(base.join("bin"));
}

pub fn sha256_hex(data: &str) -> String {
    let mut hasher = Sha256::new();
    hasher.update(data.as_bytes());
    format!("{:x}", hasher.finalize())
}

fn load_registry() -> Vec<CachedTool> {
    let path = registry_path();
    match fs::read_to_string(&path) {
        Ok(s) => serde_json::from_str(&s).unwrap_or_default(),
        Err(_) => Vec::new(),
    }
}

fn save_registry(tools: &[CachedTool]) {
    let path = registry_path();
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    let json = serde_json::to_string_pretty(tools).unwrap_or_default();
    let tmp = path.with_extension("tmp");
    if fs::write(&tmp, &json).is_ok() {
        let _ = fs::rename(&tmp, &path);
    }
}

pub fn lookup(hash: &str) -> Option<CachedTool> {
    let tools = load_registry();
    tools
        .into_iter()
        .find(|t| t.hash == hash && std::path::Path::new(&t.binary_path).exists())
}

pub fn register(tool: &CachedTool) {
    let mut tools = load_registry();
    tools.retain(|t| t.hash != tool.hash);
    tools.push(tool.clone());
    save_registry(&tools);
}

pub fn list_tools() -> Vec<CachedTool> {
    load_registry()
}

pub fn forget(query: &str) -> bool {
    let mut tools = load_registry();
    let before = tools.len();
    let removed: Vec<CachedTool> = tools
        .iter()
        .filter(|t| t.hash.starts_with(query) || t.app == query)
        .cloned()
        .collect();

    for t in &removed {
        let _ = fs::remove_file(&t.binary_path);
    }

    tools.retain(|t| !t.hash.starts_with(query) && t.app != query);
    save_registry(&tools);
    tools.len() < before
}

pub fn clear() -> usize {
    let tools = load_registry();
    let count = tools.len();
    for t in &tools {
        let _ = fs::remove_file(&t.binary_path);
    }
    save_registry(&[]);
    count
}

pub fn read_trailer(binary_path: &str) -> Result<serde_json::Value, String> {
    let mut f =
        fs::File::open(binary_path).map_err(|e| format!("cannot open {binary_path}: {e}"))?;
    let file_len = f.metadata().map_err(|e| format!("cannot stat: {e}"))?.len();

    if file_len < 16 {
        return Err("file too small for trailer".to_string());
    }

    // Read magic (last 8 bytes)
    f.seek(SeekFrom::End(-8))
        .map_err(|e| format!("seek error: {e}"))?;
    let mut magic = [0u8; 8];
    f.read_exact(&mut magic)
        .map_err(|e| format!("read error: {e}"))?;

    if &magic != TRAILER_MAGIC {
        return Err("no ESCMETA trailer found".to_string());
    }

    // Read payload length (8 bytes before magic)
    f.seek(SeekFrom::End(-16))
        .map_err(|e| format!("seek error: {e}"))?;
    let mut len_bytes = [0u8; 8];
    f.read_exact(&mut len_bytes)
        .map_err(|e| format!("read error: {e}"))?;
    let payload_len = u64::from_le_bytes(len_bytes) as usize;

    if payload_len as u64 + 16 > file_len {
        return Err("trailer length exceeds file size".to_string());
    }

    // Read payload
    f.seek(SeekFrom::End(-(payload_len as i64 + 16)))
        .map_err(|e| format!("seek error: {e}"))?;
    let mut payload = vec![0u8; payload_len];
    f.read_exact(&mut payload)
        .map_err(|e| format!("read error: {e}"))?;

    serde_json::from_slice(&payload).map_err(|e| format!("invalid trailer JSON: {e}"))
}
