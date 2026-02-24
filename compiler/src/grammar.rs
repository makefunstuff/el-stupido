use crate::primitive::Registry;
use std::fmt::Write;

/// Generate a GBNF grammar from the primitive registry.
/// This constrains LLM output to valid composition manifests only.
pub fn generate_gbnf(registry: &Registry) -> String {
    let mut g = String::with_capacity(2048);

    writeln!(g, "# GBNF grammar for el-stupido composition manifests").unwrap();
    writeln!(g, "# Auto-generated from primitive registry").unwrap();
    writeln!(g).unwrap();

    // Root rule
    writeln!(g, r#"root ::= "{{" ws "\"app\"" ws ":" ws string "," ws "\"compose\"" ws ":" ws "[" ws primitive (ws "," ws primitive)* ws "]" ws "}}""#).unwrap();
    writeln!(g).unwrap();

    // Primitive union
    let mut ids: Vec<&str> = registry.all().map(|p| p.id).collect();
    ids.sort();
    let alts: Vec<String> = ids.iter().map(|id| id.replace('_', "-")).collect();
    writeln!(g, "primitive ::= {}", alts.join(" | ")).unwrap();
    writeln!(g).unwrap();

    // Per-primitive rules
    for id in &ids {
        let prim = registry.get(id).unwrap();
        let rule_name = id.replace('_', "-");
        let mut parts = vec![format!(r#""\"use\"" ws ":" ws "\"{}\"" "#, id)];

        for param in &prim.params {
            let val_rule = match param.ty {
                crate::primitive::ParamType::U16 | crate::primitive::ParamType::U64 => {
                    "number".to_string()
                }
                crate::primitive::ParamType::F64 => "number".to_string(),
                crate::primitive::ParamType::Str => "string".to_string(),
                crate::primitive::ParamType::StrList => "string-array".to_string(),
            };
            parts.push(format!(
                r#"ws "," ws "\"{}\"" ws ":" ws {}"#,
                param.name, val_rule
            ));
        }

        writeln!(g, r#"{rule_name} ::= "{{" ws {} ws "}}""#, parts.join(" ")).unwrap();
    }

    writeln!(g).unwrap();
    writeln!(g, "# Shared rules").unwrap();
    writeln!(g, r#"ws ::= [ \t\n]*"#).unwrap();
    writeln!(g, r#"string ::= "\"" [a-zA-Z0-9_/\-.!@#$%^&*() ]* "\"""#).unwrap();
    writeln!(g, r#"number ::= [0-9]+"#).unwrap();
    writeln!(g, r#"decimal ::= [0-9]+ ("." [0-9]+)?"#).unwrap();
    writeln!(
        g,
        r#"string-array ::= "[" ws string (ws "," ws string)* ws "]""#
    )
    .unwrap();

    g
}
