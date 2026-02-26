use serde::Deserialize;
use std::collections::{HashMap, HashSet};

/// A typed parameter that a primitive accepts.
#[derive(Debug, Clone)]
pub struct ParamDef {
    pub name: &'static str,
    pub ty: ParamType,
    pub required: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub enum ParamType {
    F64,
    Str,
}

impl ParamType {
    pub fn label(&self) -> &'static str {
        match self {
            Self::F64 => "f64",
            Self::Str => "string",
        }
    }
}

/// Runtime parameter value parsed from JSON.
#[derive(Debug, Clone, Deserialize)]
#[serde(untagged)]
pub enum ParamValue {
    Number(f64),
    Str(String),
}

impl ParamValue {
    pub fn as_f64(&self) -> Option<f64> {
        if let Self::Number(n) = self {
            Some(*n)
        } else {
            None
        }
    }

    pub fn as_str(&self) -> Option<&str> {
        if let Self::Str(s) = self {
            Some(s)
        } else {
            None
        }
    }

    pub fn matches_type(&self, ty: &ParamType) -> bool {
        match (self, ty) {
            (Self::Number(_), ParamType::F64) => true,
            (Self::Str(_), ParamType::Str) => true,
            _ => false,
        }
    }
}

/// Typed node binding from one primitive instance to another.
#[derive(Debug, Clone)]
pub struct BindDef {
    pub name: &'static str,
    pub capability: &'static str,
    pub required: bool,
}

/// A reusable building block for program composition.
#[derive(Debug, Clone)]
pub struct Primitive {
    pub id: &'static str,
    pub description: &'static str,
    pub params: Vec<ParamDef>,
    pub binds: Vec<BindDef>,
    pub provides: Vec<&'static str>,
    pub requires: Vec<&'static str>,
    pub effects: Vec<&'static str>,
}

/// Registry of all available primitives.
pub struct Registry {
    primitives: HashMap<&'static str, Primitive>,
}

impl Registry {
    pub fn new() -> Self {
        let mut r = Self {
            primitives: HashMap::new(),
        };
        r.register_builtins();
        r
    }

    pub fn get(&self, id: &str) -> Option<&Primitive> {
        self.primitives.get(id)
    }

    pub fn all(&self) -> impl Iterator<Item = &Primitive> {
        self.primitives.values()
    }

    pub fn known_capabilities(&self) -> Vec<&'static str> {
        let mut caps: HashSet<&'static str> = HashSet::new();
        for prim in self.primitives.values() {
            for effect in &prim.effects {
                if *effect != "pure" {
                    caps.insert(*effect);
                }
            }
        }
        let mut out: Vec<&'static str> = caps.into_iter().collect();
        out.sort();
        out
    }

    fn add(&mut self, p: Primitive) {
        self.primitives.insert(p.id, p);
    }

    fn register_builtins(&mut self) {
        self.add(Primitive {
            id: "const_num",
            description: "Numeric constant",
            params: vec![ParamDef {
                name: "value",
                ty: ParamType::F64,
                required: true,
            }],
            binds: vec![],
            provides: vec!["num"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "const_str",
            description: "String constant",
            params: vec![ParamDef {
                name: "value",
                ty: ParamType::Str,
                required: true,
            }],
            binds: vec![],
            provides: vec!["str"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "add",
            description: "Add two numbers",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "lhs",
                    capability: "num",
                    required: true,
                },
                BindDef {
                    name: "rhs",
                    capability: "num",
                    required: true,
                },
            ],
            provides: vec!["num"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "sub",
            description: "Subtract two numbers",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "lhs",
                    capability: "num",
                    required: true,
                },
                BindDef {
                    name: "rhs",
                    capability: "num",
                    required: true,
                },
            ],
            provides: vec!["num"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "mul",
            description: "Multiply two numbers",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "lhs",
                    capability: "num",
                    required: true,
                },
                BindDef {
                    name: "rhs",
                    capability: "num",
                    required: true,
                },
            ],
            provides: vec!["num"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "div",
            description: "Divide two numbers",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "lhs",
                    capability: "num",
                    required: true,
                },
                BindDef {
                    name: "rhs",
                    capability: "num",
                    required: true,
                },
            ],
            provides: vec!["num"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "gt",
            description: "Greater-than comparison",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "lhs",
                    capability: "num",
                    required: true,
                },
                BindDef {
                    name: "rhs",
                    capability: "num",
                    required: true,
                },
            ],
            provides: vec!["bool"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "eq_num",
            description: "Numeric equality comparison",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "lhs",
                    capability: "num",
                    required: true,
                },
                BindDef {
                    name: "rhs",
                    capability: "num",
                    required: true,
                },
            ],
            provides: vec!["bool"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "and_bool",
            description: "Boolean AND",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "lhs",
                    capability: "bool",
                    required: true,
                },
                BindDef {
                    name: "rhs",
                    capability: "bool",
                    required: true,
                },
            ],
            provides: vec!["bool"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "or_bool",
            description: "Boolean OR",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "lhs",
                    capability: "bool",
                    required: true,
                },
                BindDef {
                    name: "rhs",
                    capability: "bool",
                    required: true,
                },
            ],
            provides: vec!["bool"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "not_bool",
            description: "Boolean NOT",
            params: vec![],
            binds: vec![BindDef {
                name: "value",
                capability: "bool",
                required: true,
            }],
            provides: vec!["bool"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "select_num",
            description: "Conditional number select",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "cond",
                    capability: "bool",
                    required: true,
                },
                BindDef {
                    name: "then",
                    capability: "num",
                    required: true,
                },
                BindDef {
                    name: "else",
                    capability: "num",
                    required: true,
                },
            ],
            provides: vec!["num"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "select_str",
            description: "Conditional string select",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "cond",
                    capability: "bool",
                    required: true,
                },
                BindDef {
                    name: "then",
                    capability: "str",
                    required: true,
                },
                BindDef {
                    name: "else",
                    capability: "str",
                    required: true,
                },
            ],
            provides: vec!["str"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "repeat_str",
            description: "Repeat a string by bounded count",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "text",
                    capability: "str",
                    required: true,
                },
                BindDef {
                    name: "times",
                    capability: "num",
                    required: true,
                },
            ],
            provides: vec!["str"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "to_string",
            description: "Convert number to string",
            params: vec![],
            binds: vec![BindDef {
                name: "value",
                capability: "num",
                required: true,
            }],
            provides: vec!["str"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "concat",
            description: "Concatenate two strings",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "left",
                    capability: "str",
                    required: true,
                },
                BindDef {
                    name: "right",
                    capability: "str",
                    required: true,
                },
            ],
            provides: vec!["str"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "len_str",
            description: "Count characters in a string",
            params: vec![],
            binds: vec![BindDef {
                name: "text",
                capability: "str",
                required: true,
            }],
            provides: vec!["num"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "cwd",
            description: "Current working directory",
            params: vec![],
            binds: vec![],
            provides: vec!["str"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "path_join",
            description: "Join two path segments",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "left",
                    capability: "str",
                    required: true,
                },
                BindDef {
                    name: "right",
                    capability: "str",
                    required: true,
                },
            ],
            provides: vec!["str"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "read_stdin",
            description: "Read one line from stdin",
            params: vec![ParamDef {
                name: "prompt",
                ty: ParamType::Str,
                required: false,
            }],
            binds: vec![],
            provides: vec!["str"],
            requires: vec![],
            effects: vec!["io_read", "io_write"],
        });

        self.add(Primitive {
            id: "parse_num",
            description: "Parse number from string",
            params: vec![],
            binds: vec![BindDef {
                name: "text",
                capability: "str",
                required: true,
            }],
            provides: vec!["num"],
            requires: vec![],
            effects: vec!["pure"],
        });

        self.add(Primitive {
            id: "read_file",
            description: "Read file as UTF-8 string",
            params: vec![ParamDef {
                name: "path",
                ty: ParamType::Str,
                required: true,
            }],
            binds: vec![],
            provides: vec!["str"],
            requires: vec![],
            effects: vec!["fs_read"],
        });

        self.add(Primitive {
            id: "read_file_dyn",
            description: "Read file using bound path string",
            params: vec![],
            binds: vec![BindDef {
                name: "path",
                capability: "str",
                required: true,
            }],
            provides: vec!["str"],
            requires: vec![],
            effects: vec!["fs_read"],
        });

        self.add(Primitive {
            id: "write_file",
            description: "Write UTF-8 string to file",
            params: vec![ParamDef {
                name: "path",
                ty: ParamType::Str,
                required: true,
            }],
            binds: vec![BindDef {
                name: "content",
                capability: "str",
                required: true,
            }],
            provides: vec!["sink"],
            requires: vec![],
            effects: vec!["fs_write"],
        });

        self.add(Primitive {
            id: "write_file_dyn",
            description: "Write UTF-8 string to bound path",
            params: vec![],
            binds: vec![
                BindDef {
                    name: "path",
                    capability: "str",
                    required: true,
                },
                BindDef {
                    name: "content",
                    capability: "str",
                    required: true,
                },
            ],
            provides: vec!["sink"],
            requires: vec![],
            effects: vec!["fs_write"],
        });

        self.add(Primitive {
            id: "print_num",
            description: "Print number to stdout",
            params: vec![],
            binds: vec![BindDef {
                name: "value",
                capability: "num",
                required: true,
            }],
            provides: vec!["sink"],
            requires: vec![],
            effects: vec!["io_write"],
        });

        self.add(Primitive {
            id: "print_str",
            description: "Print string to stdout",
            params: vec![],
            binds: vec![BindDef {
                name: "value",
                capability: "str",
                required: true,
            }],
            provides: vec!["sink"],
            requires: vec![],
            effects: vec!["io_write"],
        });
    }
}
