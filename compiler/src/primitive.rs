use serde::Deserialize;
use std::collections::HashMap;

/// A typed parameter that a primitive accepts.
#[derive(Debug, Clone)]
pub struct ParamDef {
    pub name: &'static str,
    pub ty: ParamType,
    pub required: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub enum ParamType {
    U16,
    U64,
    F64,
    Str,
    StrList,
}

impl ParamType {
    pub fn label(&self) -> &'static str {
        match self {
            Self::U16 => "u16",
            Self::U64 => "u64",
            Self::F64 => "f64",
            Self::Str => "string",
            Self::StrList => "string[]",
        }
    }
}

/// Runtime parameter value parsed from JSON.
#[derive(Debug, Clone, Deserialize)]
#[serde(untagged)]
pub enum ParamValue {
    Number(f64),
    Str(String),
    StrList(Vec<String>),
}

impl ParamValue {
    pub fn as_u16(&self) -> Option<u16> {
        if let Self::Number(n) = self {
            let v = *n as u16;
            if (*n - v as f64).abs() < f64::EPSILON {
                return Some(v);
            }
        }
        None
    }

    pub fn as_u64(&self) -> Option<u64> {
        if let Self::Number(n) = self {
            let v = *n as u64;
            if (*n - v as f64).abs() < f64::EPSILON {
                return Some(v);
            }
        }
        None
    }

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

    pub fn as_str_list(&self) -> Option<&[String]> {
        if let Self::StrList(v) = self {
            Some(v)
        } else {
            None
        }
    }

    pub fn matches_type(&self, ty: &ParamType) -> bool {
        match (self, ty) {
            (Self::Number(_), ParamType::U16) => self.as_u16().is_some(),
            (Self::Number(_), ParamType::U64) => self.as_u64().is_some(),
            (Self::Number(_), ParamType::F64) => true,
            (Self::Str(_), ParamType::Str) => true,
            (Self::StrList(_), ParamType::StrList) => true,
            _ => false,
        }
    }
}

/// A reusable building block for program composition.
#[derive(Debug, Clone)]
pub struct Primitive {
    pub id: &'static str,
    pub description: &'static str,
    pub params: Vec<ParamDef>,
    pub provides: Vec<&'static str>,
    pub requires: Vec<&'static str>,
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

    fn add(&mut self, p: Primitive) {
        self.primitives.insert(p.id, p);
    }

    fn register_builtins(&mut self) {
        self.add(Primitive {
            id: "http_listen",
            description: "HTTP server with fork-per-connection",
            params: vec![ParamDef {
                name: "port",
                ty: ParamType::U16,
                required: true,
            }],
            provides: vec!["server"],
            requires: vec![],
        });

        self.add(Primitive {
            id: "route",
            description: "HTTP route handler",
            params: vec![
                ParamDef {
                    name: "method",
                    ty: ParamType::Str,
                    required: true,
                },
                ParamDef {
                    name: "path",
                    ty: ParamType::Str,
                    required: true,
                },
                ParamDef {
                    name: "handler",
                    ty: ParamType::Str,
                    required: true,
                },
            ],
            provides: vec!["route"],
            requires: vec!["server"],
        });

        self.add(Primitive {
            id: "grug_store",
            description: "Persistent key-value storage (.grug file)",
            params: vec![
                ParamDef {
                    name: "name",
                    ty: ParamType::Str,
                    required: true,
                },
                ParamDef {
                    name: "fields",
                    ty: ParamType::StrList,
                    required: true,
                },
            ],
            provides: vec!["persistence"],
            requires: vec![],
        });

        self.add(Primitive {
            id: "html_list",
            description: "HTML table listing model entries",
            params: vec![ParamDef {
                name: "model",
                ty: ParamType::Str,
                required: true,
            }],
            provides: vec!["ui"],
            requires: vec!["persistence", "server"],
        });

        self.add(Primitive {
            id: "html_form",
            description: "HTML form for creating entries",
            params: vec![ParamDef {
                name: "fields",
                ty: ParamType::StrList,
                required: true,
            }],
            provides: vec!["ui"],
            requires: vec!["server"],
        });

        self.add(Primitive {
            id: "json_respond",
            description: "JSON API response handler",
            params: vec![ParamDef {
                name: "model",
                ty: ParamType::Str,
                required: true,
            }],
            provides: vec!["api"],
            requires: vec!["persistence", "server"],
        });

        self.add(Primitive {
            id: "gpio_read",
            description: "Read value from GPIO pin",
            params: vec![ParamDef {
                name: "pin",
                ty: ParamType::U16,
                required: true,
            }],
            provides: vec!["sensor"],
            requires: vec![],
        });

        self.add(Primitive {
            id: "gpio_write",
            description: "Write value to GPIO pin",
            params: vec![ParamDef {
                name: "pin",
                ty: ParamType::U16,
                required: true,
            }],
            provides: vec!["actuator"],
            requires: vec![],
        });

        self.add(Primitive {
            id: "threshold",
            description: "Compare sensor value and trigger action",
            params: vec![
                ParamDef {
                    name: "source",
                    ty: ParamType::Str,
                    required: true,
                },
                ParamDef {
                    name: "above",
                    ty: ParamType::F64,
                    required: true,
                },
                ParamDef {
                    name: "action",
                    ty: ParamType::Str,
                    required: true,
                },
            ],
            provides: vec!["logic"],
            requires: vec!["sensor"],
        });

        self.add(Primitive {
            id: "timer_loop",
            description: "Periodic execution loop",
            params: vec![ParamDef {
                name: "interval_ms",
                ty: ParamType::U64,
                required: true,
            }],
            provides: vec!["loop"],
            requires: vec![],
        });
    }
}
