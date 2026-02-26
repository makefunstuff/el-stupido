use crate::primitive::{ParamValue, Registry};
use serde::Deserialize;
use std::collections::{HashMap, HashSet};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ComposeError {
    #[error("unknown primitive '{0}'")]
    UnknownPrimitive(String),

    #[error("manifest declares unknown capability '{0}'")]
    UnknownCapability(String),

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

    #[error(
        "node '{node}' primitive '{prim}' requires [{requires}] but composition provides no [{missing}]"
    )]
    UnsatisfiedRequires {
        node: String,
        prim: String,
        requires: String,
        missing: String,
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

    #[error("duplicate node id '{0}'")]
    DuplicateNodeId(String),

    #[error("composition has no nodes")]
    Empty,

    #[error("json: {0}")]
    Json(#[from] serde_json::Error),
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

/// Parse and validate a manifest JSON string.
pub fn validate(json: &str, registry: &Registry) -> Result<ValidComposition, ComposeError> {
    let manifest: Manifest = serde_json::from_str(json)?;

    if manifest.nodes.is_empty() {
        return Err(ComposeError::Empty);
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

        let missing_caps: Vec<&str> = prim
            .requires
            .iter()
            .filter(|r| !provided.contains(**r))
            .copied()
            .collect();
        if !missing_caps.is_empty() {
            return Err(ComposeError::UnsatisfiedRequires {
                node: node.id.clone(),
                prim: node.primitive_id.clone(),
                requires: prim.requires.join(", "),
                missing: missing_caps.join(", "),
            });
        }

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
