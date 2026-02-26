use crate::primitive::{ParamType, Registry};
use std::fmt::Write;

struct ObjectEntry {
    key: String,
    value_rule: String,
    required: bool,
}

fn render_object(entries: &[ObjectEntry]) -> String {
    let optional_count = entries.iter().filter(|e| !e.required).count();
    let form_count = 1usize << optional_count;
    let mut forms = Vec::new();

    for mask in 0..form_count {
        let mut fields = Vec::new();
        let mut optional_bit = 0usize;

        for entry in entries {
            let include = if entry.required {
                true
            } else {
                let enabled = (mask & (1 << optional_bit)) != 0;
                optional_bit += 1;
                enabled
            };

            if include {
                fields.push(format!(
                    r#""\"{}\"" ws ":" ws {}"#,
                    entry.key, entry.value_rule
                ));
            }
        }

        let form = if fields.is_empty() {
            r#""{" ws "}""#.to_string()
        } else {
            format!(r#""{{" ws {} ws "}}""#, fields.join(r#" ws "," ws "#))
        };
        forms.push(form);
    }

    forms.join(" | ")
}

fn value_rule(ty: &ParamType) -> &'static str {
    match ty {
        ParamType::F64 => "number",
        ParamType::Str => "string",
    }
}

/// Generate a GBNF grammar from the primitive registry.
/// This constrains LLM output to valid composition graph manifests.
pub fn generate_gbnf(registry: &Registry) -> String {
    let mut g = String::with_capacity(4096);

    writeln!(g, "# GBNF grammar for el-stupido composition manifests").unwrap();
    writeln!(g, "# Auto-generated from primitive registry").unwrap();
    writeln!(g).unwrap();

    writeln!(
        g,
        r#"root ::= "{{" ws "\"app\"" ws ":" ws id-string ws "," ws "\"capabilities\"" ws ":" ws capability-array ws "," ws "\"nodes\"" ws ":" ws "[" ws node (ws "," ws node)* ws "]" ws "}}""#
    )
    .unwrap();
    writeln!(g).unwrap();

    let mut ids: Vec<&str> = registry.all().map(|p| p.id).collect();
    ids.sort();

    let node_alts: Vec<String> = ids
        .iter()
        .map(|id| format!("{}-node", id.replace('_', "-")))
        .collect();
    writeln!(g, "node ::= {}", node_alts.join(" | ")).unwrap();
    writeln!(g).unwrap();

    for id in &ids {
        let prim = registry.get(id).unwrap();
        let rule_id = id.replace('_', "-");
        let params_rule = format!("{}-params", rule_id);
        let bind_rule = format!("{}-bind", rule_id);

        if !prim.params.is_empty() {
            let entries: Vec<ObjectEntry> = prim
                .params
                .iter()
                .map(|p| ObjectEntry {
                    key: p.name.to_string(),
                    value_rule: value_rule(&p.ty).to_string(),
                    required: p.required,
                })
                .collect();
            writeln!(g, "{} ::= {}", params_rule, render_object(&entries)).unwrap();
        }

        if !prim.binds.is_empty() {
            let entries: Vec<ObjectEntry> = prim
                .binds
                .iter()
                .map(|b| ObjectEntry {
                    key: b.name.to_string(),
                    value_rule: "id-string".to_string(),
                    required: b.required,
                })
                .collect();
            writeln!(g, "{} ::= {}", bind_rule, render_object(&entries)).unwrap();
        }

        let mut node_entries = vec![
            ObjectEntry {
                key: "id".to_string(),
                value_rule: "id-string".to_string(),
                required: true,
            },
            ObjectEntry {
                key: "use".to_string(),
                value_rule: format!(r#""\"{}\"""#, id),
                required: true,
            },
        ];

        if !prim.params.is_empty() {
            node_entries.push(ObjectEntry {
                key: "params".to_string(),
                value_rule: params_rule,
                required: true,
            });
        }

        if !prim.binds.is_empty() {
            node_entries.push(ObjectEntry {
                key: "bind".to_string(),
                value_rule: bind_rule,
                required: true,
            });
        }

        writeln!(g, "{}-node ::= {}", rule_id, render_object(&node_entries)).unwrap();
    }

    let capabilities = registry.known_capabilities();
    if capabilities.is_empty() {
        writeln!(g, r#"capability ::= "\"__none__\"""#).unwrap();
    } else {
        let capability_alts: Vec<String> = capabilities
            .iter()
            .map(|cap| format!(r#""\"{}\"""#, cap))
            .collect();
        writeln!(g, "capability ::= {}", capability_alts.join(" | ")).unwrap();
    }
    writeln!(
        g,
        r#"capability-array ::= "[" ws "]" | "[" ws capability (ws "," ws capability)* ws "]""#
    )
    .unwrap();

    writeln!(g).unwrap();
    writeln!(g, "# Shared rules").unwrap();
    writeln!(g, r#"ws ::= [ \t\n]*"#).unwrap();
    writeln!(g, r#"id-string ::= "\"" [a-zA-Z0-9_-]+ "\"""#).unwrap();
    writeln!(
        g,
        r#"string ::= "\"" [a-zA-Z0-9_ /\-.!@#$%^&*()?=:+]* "\"""#
    )
    .unwrap();
    writeln!(g, r#"number ::= [0-9]+ ("." [0-9]+)?"#).unwrap();

    g
}
