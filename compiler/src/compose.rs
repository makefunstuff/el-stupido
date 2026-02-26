use crate::primitive::{ParamValue, Registry};
use serde::Deserialize;
use std::collections::{HashMap, HashSet};
use thiserror::Error;

pub const MAX_APP_BYTES: usize = 64;
pub const MAX_NODE_ID_BYTES: usize = 64;
pub const MAX_NODES: usize = 256;
pub const MAX_CAPABILITIES: usize = 16;
pub const MAX_STRING_PARAM_BYTES: usize = 4096;
pub const MAX_REPEAT_TIMES: f64 = 10_000.0;

#[derive(Debug, Error)]
pub enum ComposeError {
    #[error("unknown primitive '{0}'")]
    UnknownPrimitive(String),

    #[error("manifest declares unknown capability '{0}'")]
    UnknownCapability(String),

    #[error("app id too long: {len} bytes (max {max})")]
    AppTooLong { len: usize, max: usize },

    #[error("node id too long: '{id}' is {len} bytes (max {max})")]
    NodeIdTooLong { id: String, len: usize, max: usize },

    #[error("composition has {found} nodes (max {max})")]
    TooManyNodes { found: usize, max: usize },

    #[error("manifest declares {found} capabilities (max {max})")]
    TooManyCapabilities { found: usize, max: usize },

    #[error(
        "node '{node}' primitive '{prim}': param '{param}' string length {len} exceeds max {max}"
    )]
    StringParamTooLong {
        node: String,
        prim: String,
        param: String,
        len: usize,
        max: usize,
    },

    #[error("node '{node}' primitive '{prim}' requires capability '{capability}'")]
    MissingCapability {
        node: String,
        prim: String,
        capability: String,
    },

    #[error("node '{node}' uses unknown param '{param}' for primitive '{prim}'")]
    UnknownParam {
        node: String,
        prim: String,
        param: String,
    },

    #[error("node '{node}' uses unknown bind '{bind}' for primitive '{prim}'")]
    UnknownBind {
        node: String,
        prim: String,
        bind: String,
    },

    #[error("node '{node}' primitive '{prim}': missing required param '{param}'")]
    MissingParam {
        node: String,
        prim: String,
        param: String,
    },

    #[error("node '{node}' primitive '{prim}': missing required bind '{bind}'")]
    MissingBind {
        node: String,
        prim: String,
        bind: String,
    },

    #[error(
        "node '{node}' primitive '{prim}': param '{param}' has wrong type (expected {expected}, got {got})"
    )]
    WrongType {
        node: String,
        prim: String,
        param: String,
        expected: String,
        got: String,
    },

    #[error("node '{node}' primitive '{prim}': bind '{bind}' points to unknown node '{target}'")]
    UnknownBindTarget {
        node: String,
        prim: String,
        bind: String,
        target: String,
    },

    #[error(
        "node '{node}' primitive '{prim}': bind '{bind}' points forward to '{target}', only prior nodes are allowed"
    )]
    ForwardBind {
        node: String,
        prim: String,
        bind: String,
        target: String,
    },

    #[error(
        "node '{node}' primitive '{prim}': bind '{bind}' expects target with capability '{expected}' but node '{target}' does not provide it"
    )]
    BindCapabilityMismatch {
        node: String,
        prim: String,
        bind: String,
        target: String,
        expected: String,
    },

    #[error(
        "node '{node}' primitive 'repeat_str': constant repeat count {value} out of range [0, {max}]"
    )]
    RepeatLiteralOutOfRange { node: String, value: f64, max: f64 },

    #[error("duplicate node id '{0}'")]
    DuplicateNodeId(String),

    #[error("composition has no nodes")]
    Empty,

    #[error("json: {0}")]
    Json(#[from] serde_json::Error),

    #[error("esc parse error at line {line}: {msg}")]
    EscParse { line: usize, msg: String },

    #[error("tape parse error at line {line}: {msg}")]
    TapeParse { line: usize, msg: String },
}

impl ComposeError {
    /// Produce machine-readable JSON error with structured hints for LLM self-correction.
    pub fn to_machine_json(&self) -> serde_json::Value {
        let (kind, fields, hint) = match self {
            ComposeError::UnknownPrimitive(name) => (
                "unknown_primitive",
                serde_json::json!({"primitive": name}),
                format!("primitive '{}' does not exist — run `esc primitives` to see available primitives", name),
            ),
            ComposeError::UnknownCapability(cap) => (
                "unknown_capability",
                serde_json::json!({"capability": cap}),
                format!("capability '{}' is not recognized — valid capabilities: io_read, io_write, fs_read, fs_write, net_read, env_read", cap),
            ),
            ComposeError::AppTooLong { len, max } => (
                "app_too_long",
                serde_json::json!({"len": len, "max": max}),
                format!("shorten app name to {} bytes or less", max),
            ),
            ComposeError::NodeIdTooLong { id, len, max } => (
                "node_id_too_long",
                serde_json::json!({"id": id, "len": len, "max": max}),
                format!("shorten node id '{}' to {} bytes or less", id, max),
            ),
            ComposeError::TooManyNodes { found, max } => (
                "too_many_nodes",
                serde_json::json!({"found": found, "max": max}),
                format!("reduce to {} nodes or fewer", max),
            ),
            ComposeError::TooManyCapabilities { found, max } => (
                "too_many_capabilities",
                serde_json::json!({"found": found, "max": max}),
                format!("reduce to {} capabilities or fewer", max),
            ),
            ComposeError::StringParamTooLong { node, prim, param, len, max } => (
                "string_param_too_long",
                serde_json::json!({"node": node, "primitive": prim, "param": param, "len": len, "max": max}),
                format!("shorten param '{}' on node '{}' to {} bytes or less", param, node, max),
            ),
            ComposeError::MissingCapability { node, prim, capability } => (
                "missing_capability",
                serde_json::json!({"node": node, "primitive": prim, "capability": capability}),
                format!("primitive '{}' requires effect '{}' — add \"{}\" to the capabilities array", prim, capability, capability),
            ),
            ComposeError::UnknownParam { node, prim, param } => (
                "unknown_param",
                serde_json::json!({"node": node, "primitive": prim, "param": param}),
                format!("primitive '{}' does not accept param '{}' — remove it", prim, param),
            ),
            ComposeError::UnknownBind { node, prim, bind } => (
                "unknown_bind",
                serde_json::json!({"node": node, "primitive": prim, "bind": bind}),
                format!("primitive '{}' does not accept bind '{}' — remove it", prim, bind),
            ),
            ComposeError::MissingParam { node, prim, param } => (
                "missing_param",
                serde_json::json!({"node": node, "primitive": prim, "param": param}),
                format!("add required param '{}' to node '{}'", param, node),
            ),
            ComposeError::MissingBind { node, prim, bind } => (
                "missing_bind",
                serde_json::json!({"node": node, "primitive": prim, "bind": bind}),
                format!("add required bind '{}' to node '{}' pointing to a prior node", bind, node),
            ),
            ComposeError::WrongType { node, prim, param, expected, got } => (
                "wrong_type",
                serde_json::json!({"node": node, "primitive": prim, "param": param, "expected": expected, "got": got}),
                format!("param '{}' on node '{}' expects {} but got {} — fix the value type", param, node, expected, got),
            ),
            ComposeError::UnknownBindTarget { node, prim, bind, target } => (
                "unknown_bind_target",
                serde_json::json!({"node": node, "primitive": prim, "bind": bind, "target": target}),
                format!("bind '{}' on node '{}' points to '{}' which doesn't exist — use an existing node id", bind, node, target),
            ),
            ComposeError::ForwardBind { node, prim, bind, target } => (
                "forward_bind",
                serde_json::json!({"node": node, "primitive": prim, "bind": bind, "target": target}),
                format!("node '{}' binds to '{}' which appears later — move '{}' before '{}'", node, target, target, node),
            ),
            ComposeError::BindCapabilityMismatch { node, prim, bind, target, expected } => (
                "bind_type_mismatch",
                serde_json::json!({"node": node, "primitive": prim, "bind": bind, "target": target, "expected": expected}),
                format!("bind '{}' on node '{}' expects type '{}' but node '{}' provides a different type — use to_string or parse_num to convert", bind, node, expected, target),
            ),
            ComposeError::RepeatLiteralOutOfRange { node, value, max } => (
                "repeat_out_of_range",
                serde_json::json!({"node": node, "value": value, "max": max}),
                format!("repeat count {} on node '{}' must be between 0 and {}", value, node, max),
            ),
            ComposeError::DuplicateNodeId(id) => (
                "duplicate_node_id",
                serde_json::json!({"id": id}),
                format!("node id '{}' is used more than once — each node needs a unique id", id),
            ),
            ComposeError::Empty => (
                "empty",
                serde_json::json!({}),
                "manifest has no nodes — add at least one node".to_string(),
            ),
            ComposeError::Json(e) => (
                "json_parse",
                serde_json::json!({"detail": e.to_string()}),
                "fix JSON syntax error".to_string(),
            ),
            ComposeError::EscParse { line, msg } => (
                "esc_parse",
                serde_json::json!({"line": line, "detail": msg}),
                format!("fix .esc syntax at line {}: {}", line, msg),
            ),
            ComposeError::TapeParse { line, msg } => (
                "tape_parse",
                serde_json::json!({"line": line, "detail": msg}),
                format!("fix tape syntax at line {}: {}", line, msg),
            ),
        };

        serde_json::json!({
            "status": "error",
            "error": {
                "kind": kind,
                "message": self.to_string(),
                "fields": fields,
                "hint": hint,
            }
        })
    }
}

/// Extract input/output contract from a validated composition.
pub fn extract_io_contract(comp: &ValidComposition) -> (Vec<serde_json::Value>, Vec<serde_json::Value>) {
    let mut inputs = Vec::new();
    let mut outputs = Vec::new();

    for node in &comp.nodes {
        match node.primitive_id.as_str() {
            "arg_num" => {
                let idx = node.params.get("index").and_then(|v| v.as_f64()).unwrap_or(0.0) as usize;
                inputs.push(serde_json::json!({"kind": "arg", "index": idx, "type": "num", "node": node.id}));
            }
            "arg_str" => {
                let idx = node.params.get("index").and_then(|v| v.as_f64()).unwrap_or(0.0) as usize;
                inputs.push(serde_json::json!({"kind": "arg", "index": idx, "type": "str", "node": node.id}));
            }
            "read_stdin" | "read_stdin_all" => {
                let prompt = node.params.get("prompt").and_then(|v| v.as_str()).unwrap_or("");
                inputs.push(serde_json::json!({"kind": "stdin", "type": "str", "prompt": prompt, "node": node.id}));
            }
            "print_num" => {
                outputs.push(serde_json::json!({"kind": "stdout", "type": "num", "node": node.id}));
            }
            "print_str" => {
                outputs.push(serde_json::json!({"kind": "stdout", "type": "str", "node": node.id}));
            }
            "write_file" | "write_file_dyn" => {
                outputs.push(serde_json::json!({"kind": "file", "type": "str", "node": node.id}));
            }
            "http_get" => {
                let url = node.params.get("url").and_then(|v| v.as_str()).unwrap_or("");
                inputs.push(serde_json::json!({"kind": "http", "type": "str", "url": url, "node": node.id}));
            }
            "http_get_dyn" => {
                inputs.push(serde_json::json!({"kind": "http", "type": "str", "dynamic": true, "node": node.id}));
            }
            _ => {}
        }
    }

    (inputs, outputs)
}

/// Produce canonical tape representation for hashing. Strips app name, sorts caps, renumbers nodes.
pub fn canonical_tape(comp: &ValidComposition) -> String {
    use std::fmt::Write;
    let mut out = String::new();

    let mut caps: Vec<&str> = comp.capabilities.iter().map(|s| s.as_str()).collect();
    caps.sort();
    if !caps.is_empty() {
        writeln!(out, "C {}", caps.join(" ")).unwrap();
    }

    // Build node-index map once (outside the loop)
    let node_index: HashMap<&str, usize> = comp.nodes.iter().enumerate()
        .map(|(i, n)| (n.id.as_str(), i))
        .collect();

    for (idx, node) in comp.nodes.iter().enumerate() {
        let op = match node.primitive_id.as_str() {
            "const_num" => "cn",
            "const_str" => "cs",
            "add" => "ad",
            "sub" => "sb",
            "mul" => "ml",
            "div" => "dv",
            "gt" => "gt",
            "eq_num" => "eq",
            "and_bool" => "an",
            "or_bool" => "ob",
            "not_bool" => "nt",
            "select_num" => "sn",
            "select_str" => "ss",
            "to_string" => "ts",
            "concat" => "cc",
            "len_str" => "ls",
            "repeat_str" => "rp",
            "cwd" => "cw",
            "path_join" => "pj",
            "read_stdin" => "ri",
            "parse_num" => "pf",
            "read_file" => "rf",
            "read_file_dyn" => "rd",
            "write_file" => "wf",
            "write_file_dyn" => "wd",
            "print_num" => "pn",
            "print_str" => "ps",
            "arg_num" => "gn",
            "arg_str" => "gs",
            "env_str" => "ev",
            "env_str_dyn" => "ed",
            "arg_count" => "gc",
            "format_str" => "fm",
            "exit_code" => "ex",
            "substr" => "su",
            "upper_str" => "up",
            "lower_str" => "lo",
            "trim_str" => "tr",
            "contains_str" => "ct",
            "replace_str" => "re",
            "split_count" => "sc",
            "split_nth" => "sn2",
            "mod_num" => "mo",
            "floor" => "fl",
            "abs" => "ab",
            "lt" => "lt",
            "read_stdin_all" => "ra",
            "append_file" => "af",
            "http_get" => "hg",
            "http_get_dyn" => "hd",
            other => other,
        };

        let mut args = String::new();

        // Params
        match node.primitive_id.as_str() {
            "const_num" | "arg_num" | "arg_str" => {
                if let Some(v) = node.params.get("value").or(node.params.get("index")) {
                    match v {
                        ParamValue::Number(n) => write!(args, " {}", n).unwrap(),
                        ParamValue::Str(s) => write!(args, " \"{}\"", s).unwrap(),
                    }
                }
            }
            "const_str" | "read_file" | "env_str" | "append_file" => {
                if let Some(ParamValue::Str(s)) = node.params.get("value").or(node.params.get("path")).or(node.params.get("name")) {
                    write!(args, " \"{}\"", s).unwrap();
                }
            }
            "http_get" => {
                if let Some(ParamValue::Str(s)) = node.params.get("url") {
                    write!(args, " \"{}\"", s).unwrap();
                }
            }
            "format_str" => {
                if let Some(ParamValue::Str(s)) = node.params.get("template") {
                    write!(args, " \"{}\"", s).unwrap();
                }
            }
            "read_stdin" => {
                if let Some(ParamValue::Str(s)) = node.params.get("prompt") {
                    write!(args, " \"{}\"", s).unwrap();
                }
            }
            "write_file" => {
                if let Some(ParamValue::Str(s)) = node.params.get("path") {
                    write!(args, " \"{}\"", s).unwrap();
                }
                if let Some(target) = node.bind.get("content") {
                    if let Some(&tidx) = node_index.get(target.as_str()) {
                        write!(args, " {}", tidx).unwrap();
                    }
                }
            }
            _ => {}
        }

        // Binds (sorted for determinism)
        if node.primitive_id != "write_file" {
            let prim_bind_order: Vec<&str> = match node.primitive_id.as_str() {
                "add" | "sub" | "mul" | "div" | "gt" | "eq_num" | "and_bool" | "or_bool" | "mod_num" | "lt" => vec!["lhs", "rhs"],
                "not_bool" | "to_string" | "parse_num" | "print_num" | "print_str" | "len_str" | "upper_str" | "lower_str" | "trim_str" | "floor" | "abs" => vec!["value", "text"],
                "concat" | "path_join" => vec!["left", "right"],
                "select_num" | "select_str" => vec!["cond", "then", "else"],
                "repeat_str" => vec!["text", "times"],
                "read_file_dyn" | "env_str_dyn" => vec!["path", "name"],
                "http_get_dyn" => vec!["url"],
                "write_file_dyn" => vec!["path", "content"],
                "exit_code" => vec!["code"],
                "format_str" => vec!["v1", "v2"],
                "substr" => vec!["text", "start", "len"],
                "contains_str" => vec!["text", "needle"],
                "replace_str" => vec!["text", "pattern", "replacement"],
                "split_count" => vec!["text", "delim"],
                "split_nth" => vec!["text", "delim", "index"],
                _ => vec![],
            };
            for bname in prim_bind_order {
                if let Some(target) = node.bind.get(bname) {
                    if let Some(&tidx) = node_index.get(target.as_str()) {
                        write!(args, " {}", tidx).unwrap();
                    }
                }
            }
        }

        writeln!(out, "{} {}{}", idx, op, args).unwrap();
    }

    out
}

/// Raw JSON manifest as parsed from input.
#[derive(Debug, Deserialize)]
pub struct Manifest {
    pub app: String,
    pub capabilities: Vec<String>,
    pub nodes: Vec<NodeSpec>,
}

/// A single primitive instance in the manifest graph.
#[derive(Debug, Deserialize)]
pub struct NodeSpec {
    pub id: String,
    #[serde(rename = "use")]
    pub primitive: String,
    #[serde(default)]
    pub params: HashMap<String, ParamValue>,
    #[serde(default)]
    pub bind: HashMap<String, String>,
}

/// A validated composition graph ready for code emission.
#[derive(Debug)]
pub struct ValidComposition {
    pub app: String,
    pub capabilities: Vec<String>,
    pub nodes: Vec<ValidNode>,
}

#[derive(Debug)]
pub struct ValidNode {
    pub id: String,
    pub primitive_id: String,
    pub params: HashMap<String, ParamValue>,
    pub bind: HashMap<String, String>,
    pub provides: Vec<String>,
    pub effects: Vec<String>,
}

fn parse_manifest(input: &str) -> Result<Manifest, ComposeError> {
    let trimmed = input.trim_start();
    if trimmed.starts_with('{') {
        return Ok(serde_json::from_str(trimmed)?);
    }

    if looks_like_tape(input)? {
        return parse_tape_manifest(input);
    }

    parse_esc_manifest(input)
}

/// Parse and validate a manifest string (JSON, .esc, or tape).
pub fn validate(input: &str, registry: &Registry) -> Result<ValidComposition, ComposeError> {
    let manifest = parse_manifest(input)?;

    if manifest.app.len() > MAX_APP_BYTES {
        return Err(ComposeError::AppTooLong {
            len: manifest.app.len(),
            max: MAX_APP_BYTES,
        });
    }

    if manifest.nodes.is_empty() {
        return Err(ComposeError::Empty);
    }

    if manifest.nodes.len() > MAX_NODES {
        return Err(ComposeError::TooManyNodes {
            found: manifest.nodes.len(),
            max: MAX_NODES,
        });
    }

    if manifest.capabilities.len() > MAX_CAPABILITIES {
        return Err(ComposeError::TooManyCapabilities {
            found: manifest.capabilities.len(),
            max: MAX_CAPABILITIES,
        });
    }

    let known_caps: HashSet<String> = registry
        .known_capabilities()
        .into_iter()
        .map(|c| c.to_string())
        .collect();
    for capability in &manifest.capabilities {
        if !known_caps.contains(capability) {
            return Err(ComposeError::UnknownCapability(capability.clone()));
        }
    }
    let declared_caps: HashSet<String> = manifest.capabilities.iter().cloned().collect();

    let mut provided: HashSet<String> = HashSet::new();
    let mut seen_ids: HashSet<String> = HashSet::new();
    let mut nodes = Vec::with_capacity(manifest.nodes.len());

    for spec in &manifest.nodes {
        if spec.id.len() > MAX_NODE_ID_BYTES {
            return Err(ComposeError::NodeIdTooLong {
                id: spec.id.clone(),
                len: spec.id.len(),
                max: MAX_NODE_ID_BYTES,
            });
        }

        if !seen_ids.insert(spec.id.clone()) {
            return Err(ComposeError::DuplicateNodeId(spec.id.clone()));
        }

        let prim = registry
            .get(&spec.primitive)
            .ok_or_else(|| ComposeError::UnknownPrimitive(spec.primitive.clone()))?;

        for param_name in spec.params.keys() {
            if !prim.params.iter().any(|p| p.name == param_name) {
                return Err(ComposeError::UnknownParam {
                    node: spec.id.clone(),
                    prim: spec.primitive.clone(),
                    param: param_name.clone(),
                });
            }

            if let Some(ParamValue::Str(s)) = spec.params.get(param_name)
                && s.len() > MAX_STRING_PARAM_BYTES
            {
                return Err(ComposeError::StringParamTooLong {
                    node: spec.id.clone(),
                    prim: spec.primitive.clone(),
                    param: param_name.clone(),
                    len: s.len(),
                    max: MAX_STRING_PARAM_BYTES,
                });
            }
        }

        for bind_name in spec.bind.keys() {
            if !prim.binds.iter().any(|b| b.name == bind_name) {
                return Err(ComposeError::UnknownBind {
                    node: spec.id.clone(),
                    prim: spec.primitive.clone(),
                    bind: bind_name.clone(),
                });
            }
        }

        for param_def in &prim.params {
            if let Some(val) = spec.params.get(param_def.name) {
                if !val.matches_type(&param_def.ty) {
                    let got = match val {
                        ParamValue::Number(_) => "number",
                        ParamValue::Str(_) => "string",
                    };
                    return Err(ComposeError::WrongType {
                        node: spec.id.clone(),
                        prim: spec.primitive.clone(),
                        param: param_def.name.to_string(),
                        expected: param_def.ty.label().to_string(),
                        got: got.to_string(),
                    });
                }
            } else if param_def.required {
                return Err(ComposeError::MissingParam {
                    node: spec.id.clone(),
                    prim: spec.primitive.clone(),
                    param: param_def.name.to_string(),
                });
            }
        }

        for bind_def in &prim.binds {
            if bind_def.required && !spec.bind.contains_key(bind_def.name) {
                return Err(ComposeError::MissingBind {
                    node: spec.id.clone(),
                    prim: spec.primitive.clone(),
                    bind: bind_def.name.to_string(),
                });
            }
        }

        for cap in &prim.provides {
            provided.insert((*cap).to_string());
        }

        nodes.push(ValidNode {
            id: spec.id.clone(),
            primitive_id: spec.primitive.clone(),
            params: spec.params.clone(),
            bind: spec.bind.clone(),
            provides: prim.provides.iter().map(|c| (*c).to_string()).collect(),
            effects: prim.effects.iter().map(|e| (*e).to_string()).collect(),
        });
    }

    let node_by_id: HashMap<&str, &ValidNode> = nodes.iter().map(|n| (n.id.as_str(), n)).collect();
    let node_index: HashMap<&str, usize> = nodes
        .iter()
        .enumerate()
        .map(|(idx, n)| (n.id.as_str(), idx))
        .collect();

    for (idx, node) in nodes.iter().enumerate() {
        let prim = registry.get(&node.primitive_id).unwrap();

        for bind_def in &prim.binds {
            if let Some(target_id) = node.bind.get(bind_def.name) {
                let Some(target) = node_by_id.get(target_id.as_str()) else {
                    return Err(ComposeError::UnknownBindTarget {
                        node: node.id.clone(),
                        prim: node.primitive_id.clone(),
                        bind: bind_def.name.to_string(),
                        target: target_id.clone(),
                    });
                };

                let target_idx = node_index[target.id.as_str()];
                if target_idx >= idx {
                    return Err(ComposeError::ForwardBind {
                        node: node.id.clone(),
                        prim: node.primitive_id.clone(),
                        bind: bind_def.name.to_string(),
                        target: target.id.clone(),
                    });
                }

                if !target.provides.iter().any(|cap| cap == bind_def.capability) {
                    return Err(ComposeError::BindCapabilityMismatch {
                        node: node.id.clone(),
                        prim: node.primitive_id.clone(),
                        bind: bind_def.name.to_string(),
                        target: target.id.clone(),
                        expected: bind_def.capability.to_string(),
                    });
                }
            }
        }

        if node.primitive_id == "repeat_str"
            && let Some(times_target_id) = node.bind.get("times")
            && let Some(times_target) = node_by_id.get(times_target_id.as_str())
            && times_target.primitive_id == "const_num"
            && let Some(value) = times_target.params.get("value").and_then(|v| v.as_f64())
            && !(0.0..=MAX_REPEAT_TIMES).contains(&value)
        {
            return Err(ComposeError::RepeatLiteralOutOfRange {
                node: node.id.clone(),
                value,
                max: MAX_REPEAT_TIMES,
            });
        }

        for effect in &node.effects {
            if effect == "pure" {
                continue;
            }
            if !declared_caps.contains(effect) {
                return Err(ComposeError::MissingCapability {
                    node: node.id.clone(),
                    prim: node.primitive_id.clone(),
                    capability: effect.clone(),
                });
            }
        }
    }

    Ok(ValidComposition {
        app: manifest.app,
        capabilities: manifest.capabilities,
        nodes,
    })
}

fn looks_like_tape(input: &str) -> Result<bool, ComposeError> {
    for (idx, raw_line) in input.lines().enumerate() {
        let line_no = idx + 1;
        let line = strip_esc_comment(raw_line).trim().to_string();
        if line.is_empty() {
            continue;
        }

        let tokens = tokenize_esc_line(&line, line_no)?;
        if tokens.is_empty() {
            continue;
        }

        let head = tokens[0].as_str();
        if head == "A" || head == "C" || head.parse::<usize>().is_ok() {
            return Ok(true);
        }

        return Ok(false);
    }

    Ok(false)
}

fn parse_tape_manifest(input: &str) -> Result<Manifest, ComposeError> {
    let mut app: Option<String> = None;
    let mut capabilities: Vec<String> = Vec::new();
    let mut nodes: Vec<NodeSpec> = Vec::new();
    let mut expected_idx: usize = 0;

    for (idx, raw_line) in input.lines().enumerate() {
        let line_no = idx + 1;
        let line = strip_esc_comment(raw_line).trim().to_string();
        if line.is_empty() {
            continue;
        }

        let tokens = tokenize_esc_line(&line, line_no)?;
        if tokens.is_empty() {
            continue;
        }

        match tokens[0].as_str() {
            "A" => {
                if tokens.len() != 2 {
                    return Err(ComposeError::TapeParse {
                        line: line_no,
                        msg: "expected: A <app-id>".to_string(),
                    });
                }
                if app.is_some() {
                    return Err(ComposeError::TapeParse {
                        line: line_no,
                        msg: "duplicate app declaration".to_string(),
                    });
                }
                app = Some(tokens[1].clone());
            }
            "C" => {
                for capability in &tokens[1..] {
                    capabilities.push(capability.clone());
                }
            }
            _ => {
                let Ok(slot_idx) = tokens[0].parse::<usize>() else {
                    return Err(ComposeError::TapeParse {
                        line: line_no,
                        msg: format!("invalid instruction index '{}'", tokens[0]),
                    });
                };

                if slot_idx != expected_idx {
                    return Err(ComposeError::TapeParse {
                        line: line_no,
                        msg: format!("expected instruction index {expected_idx}, got {slot_idx}"),
                    });
                }

                if tokens.len() < 2 {
                    return Err(ComposeError::TapeParse {
                        line: line_no,
                        msg: "expected instruction: <idx> <op> [args...]".to_string(),
                    });
                }

                let node_id = format!("n{slot_idx}");
                let opcode = tokens[1].as_str();
                let args: &[String] = &tokens[2..];
                let node = parse_tape_instruction(line_no, slot_idx, &node_id, opcode, args)?;
                nodes.push(node);

                expected_idx += 1;
            }
        }
    }

    let Some(app) = app else {
        return Err(ComposeError::TapeParse {
            line: 1,
            msg: "missing app declaration (A <app-id>)".to_string(),
        });
    };

    Ok(Manifest {
        app,
        capabilities,
        nodes,
    })
}

fn parse_tape_instruction(
    line_no: usize,
    slot_idx: usize,
    node_id: &str,
    opcode: &str,
    args: &[String],
) -> Result<NodeSpec, ComposeError> {
    let mut params: HashMap<String, ParamValue> = HashMap::new();
    let mut bind: HashMap<String, String> = HashMap::new();

    let expect = |count: usize| -> Result<(), ComposeError> {
        if args.len() != count {
            return Err(ComposeError::TapeParse {
                line: line_no,
                msg: format!("opcode '{opcode}' expects {count} args, got {}", args.len()),
            });
        }
        Ok(())
    };

    let primitive = match opcode {
        "cn" => {
            expect(1)?;
            let value = parse_tape_number(&args[0], line_no)?;
            params.insert("value".to_string(), ParamValue::Number(value));
            "const_num"
        }
        "cs" => {
            expect(1)?;
            params.insert("value".to_string(), ParamValue::Str(args[0].clone()));
            "const_str"
        }
        "ad" => {
            expect(2)?;
            bind.insert(
                "lhs".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "rhs".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "add"
        }
        "sb" => {
            expect(2)?;
            bind.insert(
                "lhs".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "rhs".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "sub"
        }
        "ml" => {
            expect(2)?;
            bind.insert(
                "lhs".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "rhs".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "mul"
        }
        "dv" => {
            expect(2)?;
            bind.insert(
                "lhs".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "rhs".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "div"
        }
        "gt" => {
            expect(2)?;
            bind.insert(
                "lhs".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "rhs".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "gt"
        }
        "eq" => {
            expect(2)?;
            bind.insert(
                "lhs".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "rhs".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "eq_num"
        }
        "an" => {
            expect(2)?;
            bind.insert(
                "lhs".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "rhs".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "and_bool"
        }
        "ob" => {
            expect(2)?;
            bind.insert(
                "lhs".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "rhs".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "or_bool"
        }
        "nt" => {
            expect(1)?;
            bind.insert(
                "value".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "not_bool"
        }
        "sn" => {
            expect(3)?;
            bind.insert(
                "cond".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "then".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            bind.insert(
                "else".to_string(),
                parse_tape_ref(&args[2], line_no, slot_idx)?,
            );
            "select_num"
        }
        "ss" => {
            expect(3)?;
            bind.insert(
                "cond".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "then".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            bind.insert(
                "else".to_string(),
                parse_tape_ref(&args[2], line_no, slot_idx)?,
            );
            "select_str"
        }
        "ts" => {
            expect(1)?;
            bind.insert(
                "value".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "to_string"
        }
        "cc" => {
            expect(2)?;
            bind.insert(
                "left".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "right".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "concat"
        }
        "ls" => {
            expect(1)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "len_str"
        }
        "rp" => {
            expect(2)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "times".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "repeat_str"
        }
        "cw" => {
            expect(0)?;
            "cwd"
        }
        "pj" => {
            expect(2)?;
            bind.insert(
                "left".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "right".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "path_join"
        }
        "ri" => {
            if args.len() > 1 {
                return Err(ComposeError::TapeParse {
                    line: line_no,
                    msg: format!("opcode '{opcode}' expects 0 or 1 args, got {}", args.len()),
                });
            }
            if let Some(prompt) = args.first() {
                params.insert("prompt".to_string(), ParamValue::Str(prompt.clone()));
            }
            "read_stdin"
        }
        "pf" => {
            expect(1)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "parse_num"
        }
        "rf" => {
            expect(1)?;
            params.insert("path".to_string(), ParamValue::Str(args[0].clone()));
            "read_file"
        }
        "rd" => {
            expect(1)?;
            bind.insert(
                "path".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "read_file_dyn"
        }
        "wf" => {
            expect(2)?;
            params.insert("path".to_string(), ParamValue::Str(args[0].clone()));
            bind.insert(
                "content".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "write_file"
        }
        "wd" => {
            expect(2)?;
            bind.insert(
                "path".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "content".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "write_file_dyn"
        }
        "pn" => {
            expect(1)?;
            bind.insert(
                "value".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "print_num"
        }
        "ps" => {
            expect(1)?;
            bind.insert(
                "value".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "print_str"
        }
        "gn" => {
            expect(1)?;
            let value = parse_tape_number(&args[0], line_no)?;
            params.insert("index".to_string(), ParamValue::Number(value));
            "arg_num"
        }
        "gs" => {
            expect(1)?;
            let value = parse_tape_number(&args[0], line_no)?;
            params.insert("index".to_string(), ParamValue::Number(value));
            "arg_str"
        }
        "ev" => {
            expect(1)?;
            params.insert("name".to_string(), ParamValue::Str(args[0].clone()));
            "env_str"
        }
        "ed" => {
            expect(1)?;
            bind.insert(
                "name".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "env_str_dyn"
        }
        "gc" => {
            expect(0)?;
            "arg_count"
        }
        "fm" => {
            // format_str: template bind_v1 [bind_v2]
            if args.len() < 2 || args.len() > 3 {
                return Err(ComposeError::TapeParse {
                    line: line_no,
                    msg: format!("opcode '{opcode}' expects 2 or 3 args, got {}", args.len()),
                });
            }
            params.insert("template".to_string(), ParamValue::Str(args[0].clone()));
            bind.insert(
                "v1".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            if args.len() == 3 {
                bind.insert(
                    "v2".to_string(),
                    parse_tape_ref(&args[2], line_no, slot_idx)?,
                );
            }
            "format_str"
        }
        "ex" => {
            expect(1)?;
            bind.insert(
                "code".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "exit_code"
        }
        "su" => {
            expect(3)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "start".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            bind.insert(
                "len".to_string(),
                parse_tape_ref(&args[2], line_no, slot_idx)?,
            );
            "substr"
        }
        "up" => {
            expect(1)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "upper_str"
        }
        "lo" => {
            expect(1)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "lower_str"
        }
        "tr" => {
            expect(1)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "trim_str"
        }
        "ct" => {
            expect(2)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "needle".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "contains_str"
        }
        "re" => {
            expect(3)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "pattern".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            bind.insert(
                "replacement".to_string(),
                parse_tape_ref(&args[2], line_no, slot_idx)?,
            );
            "replace_str"
        }
        "sc" => {
            expect(2)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "delim".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "split_count"
        }
        "sn2" => {
            expect(3)?;
            bind.insert(
                "text".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "delim".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            bind.insert(
                "index".to_string(),
                parse_tape_ref(&args[2], line_no, slot_idx)?,
            );
            "split_nth"
        }
        "mo" => {
            expect(2)?;
            bind.insert(
                "lhs".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "rhs".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "mod_num"
        }
        "fl" => {
            expect(1)?;
            bind.insert(
                "value".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "floor"
        }
        "ab" => {
            expect(1)?;
            bind.insert(
                "value".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "abs"
        }
        "lt" => {
            expect(2)?;
            bind.insert(
                "lhs".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            bind.insert(
                "rhs".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "lt"
        }
        "ra" => {
            expect(0)?;
            "read_stdin_all"
        }
        "af" => {
            expect(2)?;
            params.insert("path".to_string(), ParamValue::Str(args[0].clone()));
            bind.insert(
                "content".to_string(),
                parse_tape_ref(&args[1], line_no, slot_idx)?,
            );
            "append_file"
        }
        "hg" => {
            expect(1)?;
            params.insert("url".to_string(), ParamValue::Str(args[0].clone()));
            "http_get"
        }
        "hd" => {
            expect(1)?;
            bind.insert(
                "url".to_string(),
                parse_tape_ref(&args[0], line_no, slot_idx)?,
            );
            "http_get_dyn"
        }
        _ => {
            return Err(ComposeError::TapeParse {
                line: line_no,
                msg: format!("unknown opcode '{opcode}'"),
            });
        }
    };

    Ok(NodeSpec {
        id: node_id.to_string(),
        primitive: primitive.to_string(),
        params,
        bind,
    })
}

fn parse_tape_number(token: &str, line_no: usize) -> Result<f64, ComposeError> {
    token.parse::<f64>().map_err(|_| ComposeError::TapeParse {
        line: line_no,
        msg: format!("expected number, got '{token}'"),
    })
}

fn parse_tape_ref(token: &str, line_no: usize, slot_idx: usize) -> Result<String, ComposeError> {
    let raw = token.strip_prefix('@').unwrap_or(token);
    let target_idx: usize = raw.parse::<usize>().map_err(|_| ComposeError::TapeParse {
        line: line_no,
        msg: format!("expected instruction reference, got '{token}'"),
    })?;

    if target_idx >= slot_idx {
        return Err(ComposeError::TapeParse {
            line: line_no,
            msg: format!("reference '{token}' must point to earlier instruction (< {slot_idx})"),
        });
    }

    Ok(format!("n{target_idx}"))
}

fn parse_esc_manifest(input: &str) -> Result<Manifest, ComposeError> {
    let mut app: Option<String> = None;
    let mut capabilities: Vec<String> = Vec::new();
    let mut nodes: Vec<NodeSpec> = Vec::new();

    for (idx, raw_line) in input.lines().enumerate() {
        let line_no = idx + 1;
        let line = strip_esc_comment(raw_line).trim().to_string();
        if line.is_empty() {
            continue;
        }

        let tokens = tokenize_esc_line(&line, line_no)?;
        if tokens.is_empty() {
            continue;
        }

        match tokens[0].as_str() {
            "app" => {
                if tokens.len() != 2 {
                    return Err(ComposeError::EscParse {
                        line: line_no,
                        msg: "expected: app <id>".to_string(),
                    });
                }
                if app.is_some() {
                    return Err(ComposeError::EscParse {
                        line: line_no,
                        msg: "duplicate app declaration".to_string(),
                    });
                }
                app = Some(tokens[1].clone());
            }
            "cap" => {
                if tokens.len() < 2 {
                    return Err(ComposeError::EscParse {
                        line: line_no,
                        msg: "expected: cap <name> [name...]".to_string(),
                    });
                }
                for capability in &tokens[1..] {
                    capabilities.push(capability.clone());
                }
            }
            _ => {
                if tokens.len() < 2 {
                    return Err(ComposeError::EscParse {
                        line: line_no,
                        msg: "expected node: <id> <primitive> [key=value ...]".to_string(),
                    });
                }

                let id = tokens[0].clone();
                let primitive = tokens[1].clone();
                let mut params: HashMap<String, ParamValue> = HashMap::new();
                let mut bind: HashMap<String, String> = HashMap::new();

                for assign in &tokens[2..] {
                    let (key, value) = parse_assignment(assign, line_no)?;

                    if params.contains_key(key) || bind.contains_key(key) {
                        return Err(ComposeError::EscParse {
                            line: line_no,
                            msg: format!("duplicate assignment for '{key}'"),
                        });
                    }

                    if let Some(target) = value.strip_prefix('@') {
                        if target.is_empty() {
                            return Err(ComposeError::EscParse {
                                line: line_no,
                                msg: format!("bind '{key}' has empty target"),
                            });
                        }
                        bind.insert(key.to_string(), target.to_string());
                    } else if let Ok(number) = value.parse::<f64>() {
                        params.insert(key.to_string(), ParamValue::Number(number));
                    } else {
                        params.insert(key.to_string(), ParamValue::Str(value.to_string()));
                    }
                }

                nodes.push(NodeSpec {
                    id,
                    primitive,
                    params,
                    bind,
                });
            }
        }
    }

    let Some(app) = app else {
        return Err(ComposeError::EscParse {
            line: 1,
            msg: "missing app declaration (app <id>)".to_string(),
        });
    };

    Ok(Manifest {
        app,
        capabilities,
        nodes,
    })
}

fn parse_assignment<'a>(
    token: &'a str,
    line_no: usize,
) -> Result<(&'a str, &'a str), ComposeError> {
    let Some(eq) = token.find('=') else {
        return Err(ComposeError::EscParse {
            line: line_no,
            msg: format!("expected key=value, got '{token}'"),
        });
    };

    let key = &token[..eq];
    let value = &token[eq + 1..];

    if key.is_empty() {
        return Err(ComposeError::EscParse {
            line: line_no,
            msg: format!("empty key in assignment '{token}'"),
        });
    }

    Ok((key, value))
}

fn strip_esc_comment(line: &str) -> String {
    let mut out = String::with_capacity(line.len());
    let mut in_quotes = false;
    let mut escaped = false;

    for ch in line.chars() {
        if escaped {
            out.push(ch);
            escaped = false;
            continue;
        }

        if in_quotes && ch == '\\' {
            out.push(ch);
            escaped = true;
            continue;
        }

        if ch == '"' {
            in_quotes = !in_quotes;
            out.push(ch);
            continue;
        }

        if ch == '#' && !in_quotes {
            break;
        }

        out.push(ch);
    }

    out
}

fn tokenize_esc_line(line: &str, line_no: usize) -> Result<Vec<String>, ComposeError> {
    let mut tokens: Vec<String> = Vec::new();
    let mut cur = String::new();
    let mut in_quotes = false;
    let mut escaped = false;

    for ch in line.chars() {
        if escaped {
            let decoded = match ch {
                'n' => '\n',
                'r' => '\r',
                't' => '\t',
                '"' => '"',
                '\\' => '\\',
                other => other,
            };
            cur.push(decoded);
            escaped = false;
            continue;
        }

        if in_quotes {
            match ch {
                '\\' => escaped = true,
                '"' => in_quotes = false,
                other => cur.push(other),
            }
            continue;
        }

        match ch {
            '"' => in_quotes = true,
            c if c.is_whitespace() => {
                if !cur.is_empty() {
                    tokens.push(std::mem::take(&mut cur));
                }
            }
            other => cur.push(other),
        }
    }

    if escaped {
        return Err(ComposeError::EscParse {
            line: line_no,
            msg: "dangling escape in quoted string".to_string(),
        });
    }

    if in_quotes {
        return Err(ComposeError::EscParse {
            line: line_no,
            msg: "unterminated quote".to_string(),
        });
    }

    if !cur.is_empty() {
        tokens.push(cur);
    }

    Ok(tokens)
}
