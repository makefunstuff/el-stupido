use crate::primitive::{ParamValue, Registry};
use serde::Deserialize;
use std::collections::{HashMap, HashSet};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ComposeError {
    #[error("unknown primitive '{0}'")]
    UnknownPrimitive(String),

    #[error("primitive '{prim}': missing required param '{param}'")]
    MissingParam { prim: String, param: String },

    #[error("primitive '{prim}': param '{param}' has wrong type (expected {expected}, got {got})")]
    WrongType {
        prim: String,
        param: String,
        expected: String,
        got: String,
    },

    #[error("primitive '{prim}' requires [{requires}] but nothing provides [{missing}]")]
    UnsatisfiedRequires {
        prim: String,
        requires: String,
        missing: String,
    },

    #[error("composition has no primitives")]
    Empty,

    #[error("json: {0}")]
    Json(#[from] serde_json::Error),
}

/// Raw JSON manifest as parsed from input.
#[derive(Debug, Deserialize)]
pub struct Manifest {
    pub app: String,
    pub compose: Vec<PrimitiveCall>,
}

/// A single primitive invocation in the manifest.
#[derive(Debug, Deserialize)]
pub struct PrimitiveCall {
    #[serde(rename = "use")]
    pub primitive: String,
    #[serde(flatten)]
    pub params: HashMap<String, ParamValue>,
}

/// A validated composition ready for code emission.
#[derive(Debug)]
pub struct ValidComposition {
    pub app: String,
    pub calls: Vec<ValidCall>,
}

#[derive(Debug)]
pub struct ValidCall {
    pub primitive_id: String,
    pub params: HashMap<String, ParamValue>,
}

/// Parse and validate a manifest JSON string.
pub fn validate(json: &str, registry: &Registry) -> Result<ValidComposition, ComposeError> {
    let manifest: Manifest = serde_json::from_str(json)?;

    if manifest.compose.is_empty() {
        return Err(ComposeError::Empty);
    }

    // Collect all capabilities provided by the composition.
    let mut provided: HashSet<&str> = HashSet::new();
    let mut calls = Vec::new();

    // First pass: check each primitive exists, params are valid.
    for call in &manifest.compose {
        let prim = registry
            .get(&call.primitive)
            .ok_or_else(|| ComposeError::UnknownPrimitive(call.primitive.clone()))?;

        // Check required params present and typed correctly.
        for param_def in &prim.params {
            if let Some(val) = call.params.get(param_def.name) {
                if !val.matches_type(&param_def.ty) {
                    let got = match val {
                        ParamValue::Number(_) => "number",
                        ParamValue::Str(_) => "string",
                        ParamValue::StrList(_) => "string[]",
                    };
                    return Err(ComposeError::WrongType {
                        prim: call.primitive.clone(),
                        param: param_def.name.to_string(),
                        expected: param_def.ty.label().to_string(),
                        got: got.to_string(),
                    });
                }
            } else if param_def.required {
                return Err(ComposeError::MissingParam {
                    prim: call.primitive.clone(),
                    param: param_def.name.to_string(),
                });
            }
        }

        for cap in &prim.provides {
            provided.insert(cap);
        }

        calls.push(ValidCall {
            primitive_id: call.primitive.clone(),
            params: call.params.clone(),
        });
    }

    // Second pass: check all requires are satisfied.
    for call in &manifest.compose {
        let prim = registry.get(&call.primitive).unwrap();
        let missing: Vec<&str> = prim
            .requires
            .iter()
            .filter(|r| !provided.contains(*r))
            .copied()
            .collect();
        if !missing.is_empty() {
            return Err(ComposeError::UnsatisfiedRequires {
                prim: call.primitive.clone(),
                requires: prim.requires.join(", "),
                missing: missing.join(", "),
            });
        }
    }

    Ok(ValidComposition {
        app: manifest.app,
        calls,
    })
}
