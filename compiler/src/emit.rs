use crate::compose::{ValidComposition, MAX_REPEAT_TIMES};
use std::collections::HashMap;
use std::fmt::Write;

fn rust_string(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '\\' => out.push_str("\\\\"),
            '"' => out.push_str("\\\""),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            '\0' => out.push_str("\\0"),
            c if c.is_control() => {
                // Escape all other control characters as \x{nn}
                for b in c.to_string().bytes() {
                    out.push_str(&format!("\\x{:02x}", b));
                }
            }
            c => out.push(c),
        }
    }
    out
}

fn f64_literal(n: f64) -> String {
    let mut s = n.to_string();
    if !s.contains('.') && !s.contains('e') && !s.contains('E') {
        s.push_str(".0");
    }
    s
}

fn emit_http_helper(out: &mut String) {
    use std::fmt::Write;
    writeln!(out, "fn esc_http_get(url: &str) -> String {{").unwrap();
    writeln!(out, "    use std::io::{{Read, Write}};").unwrap();
    writeln!(out, "    use std::net::TcpStream;").unwrap();
    writeln!(out, "").unwrap();
    writeln!(out, "    // Parse URL: support http:// and https:// (https via external openssl s_client or plain TCP)").unwrap();
    writeln!(out, "    let url = url.trim();").unwrap();
    writeln!(out, "    let is_https = url.starts_with(\"https://\");").unwrap();
    writeln!(out, "    let without_scheme = url.strip_prefix(\"https://\").or_else(|| url.strip_prefix(\"http://\")).unwrap_or(url);").unwrap();
    writeln!(
        out,
        "    let (host_port, path) = match without_scheme.find('/') {{"
    )
    .unwrap();
    writeln!(
        out,
        "        Some(i) => (&without_scheme[..i], &without_scheme[i..]),"
    )
    .unwrap();
    writeln!(out, "        None => (without_scheme, \"/\"),").unwrap();
    writeln!(out, "    }};").unwrap();
    writeln!(out, "    let (host, port) = match host_port.find(':') {{").unwrap();
    writeln!(out, "        Some(i) => (&host_port[..i], host_port[i+1..].parse::<u16>().unwrap_or(if is_https {{ 443 }} else {{ 80 }})),").unwrap();
    writeln!(
        out,
        "        None => (host_port, if is_https {{ 443 }} else {{ 80 }}),"
    )
    .unwrap();
    writeln!(out, "    }};").unwrap();
    writeln!(out, "").unwrap();
    writeln!(out, "    let request = format!(\"GET {{}} HTTP/1.1\\r\\nHost: {{}}\\r\\nConnection: close\\r\\nUser-Agent: esc/0.1\\r\\n\\r\\n\", path, host);").unwrap();
    writeln!(out, "").unwrap();
    writeln!(out, "    if is_https {{").unwrap();
    writeln!(
        out,
        "        // For HTTPS, shell out to openssl s_client or curl as fallback"
    )
    .unwrap();
    writeln!(
        out,
        "        let output = std::process::Command::new(\"curl\")"
    )
    .unwrap();
    writeln!(
        out,
        "            .args(&[\"-s\", \"-L\", \"--max-time\", \"10\", \"--proto\", \"=https,http\", \"--proto-redir\", \"=https,http\", url])"
    )
    .unwrap();
    writeln!(out, "            .output();").unwrap();
    writeln!(out, "        match output {{").unwrap();
    writeln!(out, "            Ok(o) if o.status.success() => return String::from_utf8_lossy(&o.stdout).to_string(),").unwrap();
    writeln!(out, "            _ => return String::new(),").unwrap();
    writeln!(out, "        }}").unwrap();
    writeln!(out, "    }}").unwrap();
    writeln!(out, "").unwrap();
    writeln!(out, "    // Plain HTTP via raw TCP").unwrap();
    writeln!(out, "    let addr = format!(\"{{}}:{{}}\", host, port);").unwrap();
    writeln!(
        out,
        "    let mut stream = match TcpStream::connect(&addr) {{"
    )
    .unwrap();
    writeln!(out, "        Ok(s) => s,").unwrap();
    writeln!(out, "        Err(_) => return String::new(),").unwrap();
    writeln!(out, "    }};").unwrap();
    writeln!(
        out,
        "    let _ = stream.set_read_timeout(Some(std::time::Duration::from_secs(10)));"
    )
    .unwrap();
    writeln!(out, "    let _ = stream.write_all(request.as_bytes());").unwrap();
    writeln!(out, "    let mut response = String::new();").unwrap();
    writeln!(out, "    let _ = stream.read_to_string(&mut response);").unwrap();
    writeln!(out, "").unwrap();
    writeln!(out, "    // Strip HTTP headers (find \\r\\n\\r\\n)").unwrap();
    writeln!(
        out,
        "    if let Some(pos) = response.find(\"\\r\\n\\r\\n\") {{"
    )
    .unwrap();
    writeln!(out, "        response[pos + 4..].to_string()").unwrap();
    writeln!(out, "    }} else {{").unwrap();
    writeln!(out, "        response").unwrap();
    writeln!(out, "    }}").unwrap();
    writeln!(out, "}}").unwrap();
    writeln!(out, "").unwrap();
}

fn bind_var(
    node: &crate::compose::ValidNode,
    bind_name: &str,
    vars: &HashMap<String, String>,
) -> String {
    let target = node.bind.get(bind_name).unwrap();
    vars.get(target).unwrap().clone()
}

/// Emit a self-contained Rust source file from a validated composition graph.
pub fn emit_rust(comp: &ValidComposition) -> String {
    let mut out = String::with_capacity(32768);

    writeln!(out, "// Generated by esc (el-stupido compiler)").unwrap();
    writeln!(out, "// app: {}", rust_string(&comp.app)).unwrap();
    if comp.capabilities.is_empty() {
        writeln!(out, "// capabilities: []").unwrap();
    } else {
        writeln!(out, "// capabilities: [{}]", comp.capabilities.join(", ")).unwrap();
    }
    writeln!(
        out,
        "#![allow(dead_code, unused_imports, unused_variables)]"
    )
    .unwrap();
    writeln!(out).unwrap();

    let mut needs_http_helper = false;

    // First pass: check if we need the HTTP helper
    for node in &comp.nodes {
        if node.primitive_id == "http_get" || node.primitive_id == "http_get_dyn" {
            needs_http_helper = true;
            break;
        }
    }

    if needs_http_helper {
        emit_http_helper(&mut out);
    }

    writeln!(out, "fn main() {{").unwrap();

    let mut vars: HashMap<String, String> = HashMap::new();

    for (idx, node) in comp.nodes.iter().enumerate() {
        let var = format!("v{idx}");
        writeln!(
            out,
            "    // node {} ({})",
            rust_string(&node.id),
            node.primitive_id
        )
        .unwrap();

        match node.primitive_id.as_str() {
            "const_num" => {
                let value = node.params["value"].as_f64().unwrap();
                writeln!(out, "    let {var}: f64 = {};", f64_literal(value)).unwrap();
            }
            "const_str" => {
                let value = node.params["value"].as_str().unwrap();
                writeln!(
                    out,
                    "    let {var}: String = \"{}\".to_string();",
                    rust_string(value)
                )
                .unwrap();
            }
            "add" => {
                let lhs = bind_var(node, "lhs", &vars);
                let rhs = bind_var(node, "rhs", &vars);
                writeln!(out, "    let {var}: f64 = {lhs} + {rhs};").unwrap();
            }
            "sub" => {
                let lhs = bind_var(node, "lhs", &vars);
                let rhs = bind_var(node, "rhs", &vars);
                writeln!(out, "    let {var}: f64 = {lhs} - {rhs};").unwrap();
            }
            "mul" => {
                let lhs = bind_var(node, "lhs", &vars);
                let rhs = bind_var(node, "rhs", &vars);
                writeln!(out, "    let {var}: f64 = {lhs} * {rhs};").unwrap();
            }
            "div" => {
                let lhs = bind_var(node, "lhs", &vars);
                let rhs = bind_var(node, "rhs", &vars);
                writeln!(out, "    let {var}: f64 = {lhs} / {rhs};").unwrap();
            }
            "gt" => {
                let lhs = bind_var(node, "lhs", &vars);
                let rhs = bind_var(node, "rhs", &vars);
                writeln!(out, "    let {var}: bool = {lhs} > {rhs};").unwrap();
            }
            "eq_num" => {
                let lhs = bind_var(node, "lhs", &vars);
                let rhs = bind_var(node, "rhs", &vars);
                writeln!(out, "    let {var}: bool = {lhs} == {rhs};").unwrap();
            }
            "and_bool" => {
                let lhs = bind_var(node, "lhs", &vars);
                let rhs = bind_var(node, "rhs", &vars);
                writeln!(out, "    let {var}: bool = {lhs} && {rhs};").unwrap();
            }
            "or_bool" => {
                let lhs = bind_var(node, "lhs", &vars);
                let rhs = bind_var(node, "rhs", &vars);
                writeln!(out, "    let {var}: bool = {lhs} || {rhs};").unwrap();
            }
            "not_bool" => {
                let value = bind_var(node, "value", &vars);
                writeln!(out, "    let {var}: bool = !{value};").unwrap();
            }
            "select_num" => {
                let cond = bind_var(node, "cond", &vars);
                let then_v = bind_var(node, "then", &vars);
                let else_v = bind_var(node, "else", &vars);
                writeln!(
                    out,
                    "    let {var}: f64 = if {cond} {{ {then_v} }} else {{ {else_v} }};"
                )
                .unwrap();
            }
            "select_str" => {
                let cond = bind_var(node, "cond", &vars);
                let then_v = bind_var(node, "then", &vars);
                let else_v = bind_var(node, "else", &vars);
                writeln!(
                    out,
                    "    let {var}: String = if {cond} {{ {then_v}.clone() }} else {{ {else_v}.clone() }};"
                )
                .unwrap();
            }
            "to_string" => {
                let value = bind_var(node, "value", &vars);
                writeln!(out, "    let {var}: String = format!(\"{{}}\", {value});").unwrap();
            }
            "concat" => {
                let left = bind_var(node, "left", &vars);
                let right = bind_var(node, "right", &vars);
                writeln!(
                    out,
                    "    let {var}: String = format!(\"{{}}{{}}\", {left}, {right});"
                )
                .unwrap();
            }
            "len_str" => {
                let text = bind_var(node, "text", &vars);
                writeln!(out, "    let {var}: f64 = {text}.chars().count() as f64;").unwrap();
            }
            "repeat_str" => {
                let text = bind_var(node, "text", &vars);
                let times = bind_var(node, "times", &vars);
                let repeat_cap = MAX_REPEAT_TIMES as usize;
                writeln!(out, "    let {var}: String = {{").unwrap();
                writeln!(
                    out,
                    "        let count = if {times}.is_finite() && {times} > 0.0 {{ {times}.floor() as usize }} else {{ 0 }};"
                )
                .unwrap();
                writeln!(out, "        let count = count.min({repeat_cap});").unwrap();
                writeln!(
                    out,
                    "        let result_len = {text}.len().saturating_mul(count);"
                )
                .unwrap();
                writeln!(out, "        if result_len > 10_000_000 {{ eprintln!(\"error: repeat_str would produce {{}} bytes (max 10MB)\", result_len); std::process::exit(1); }}").unwrap();
                writeln!(out, "        {text}.repeat(count)").unwrap();
                writeln!(out, "    }};").unwrap();
            }
            "cwd" => {
                writeln!(out, "    let {var}: String = std::env::current_dir().map(|p| p.to_string_lossy().into_owned()).unwrap_or_else(|_| \".\".to_string());").unwrap();
            }
            "path_join" => {
                let left = bind_var(node, "left", &vars);
                let right = bind_var(node, "right", &vars);
                writeln!(out, "    let {var}: String = {{").unwrap();
                writeln!(
                    out,
                    "        let mut p = std::path::PathBuf::from(&{left});"
                )
                .unwrap();
                writeln!(out, "        p.push(&{right});").unwrap();
                writeln!(out, "        p.to_string_lossy().into_owned()").unwrap();
                writeln!(out, "    }};").unwrap();
            }
            "read_stdin" => {
                let prompt = node
                    .params
                    .get("prompt")
                    .and_then(|v| v.as_str())
                    .unwrap_or("");
                writeln!(out, "    let {var}: String = {{").unwrap();
                writeln!(out, "        use std::io::Write as _;").unwrap();
                writeln!(out, "        let mut line = String::new();").unwrap();
                writeln!(out, "        print!(\"{}\");", rust_string(prompt)).unwrap();
                writeln!(out, "        let _ = std::io::stdout().flush();").unwrap();
                writeln!(
                    out,
                    "        std::io::stdin().read_line(&mut line).expect(\"stdin read failed\");"
                )
                .unwrap();
                writeln!(
                    out,
                    "        line.trim_end_matches(&['\\r', '\\n'][..]).to_string()"
                )
                .unwrap();
                writeln!(out, "    }};").unwrap();
            }
            "parse_num" => {
                let text = bind_var(node, "text", &vars);
                writeln!(
                    out,
                    "    let {var}: f64 = {text}.trim().parse::<f64>().unwrap_or(0.0);"
                )
                .unwrap();
            }
            "read_file" => {
                let path = node.params["path"].as_str().unwrap();
                writeln!(
                    out,
                    "    let {var}: String = std::fs::read_to_string(\"{}\").unwrap_or_default();",
                    rust_string(path)
                )
                .unwrap();
            }
            "read_file_dyn" => {
                let path = bind_var(node, "path", &vars);
                writeln!(
                    out,
                    "    let {var}: String = std::fs::read_to_string(&{path}).unwrap_or_default();"
                )
                .unwrap();
            }
            "write_file" => {
                let path = node.params["path"].as_str().unwrap();
                let content = bind_var(node, "content", &vars);
                writeln!(out, "    let {var}: () = {{").unwrap();
                writeln!(
                    out,
                    "        let _ = std::fs::write(\"{}\", {content});",
                    rust_string(path)
                )
                .unwrap();
                writeln!(out, "    }};").unwrap();
            }
            "write_file_dyn" => {
                let path = bind_var(node, "path", &vars);
                let content = bind_var(node, "content", &vars);
                writeln!(out, "    let {var}: () = {{").unwrap();
                writeln!(out, "        let _ = std::fs::write(&{path}, &{content});").unwrap();
                writeln!(out, "    }};").unwrap();
            }
            "print_num" => {
                let value = bind_var(node, "value", &vars);
                writeln!(
                    out,
                    "    let {var}: () = {{ println!(\"{{}}\", {value}); }};"
                )
                .unwrap();
            }
            "print_str" => {
                let value = bind_var(node, "value", &vars);
                writeln!(
                    out,
                    "    let {var}: () = {{ println!(\"{{}}\", {value}); }};"
                )
                .unwrap();
            }
            "arg_num" => {
                let index = node.params["index"].as_f64().unwrap() as usize;
                writeln!(out, "    let {var}: f64 = std::env::args().nth({index})").unwrap();
                writeln!(out, "        .and_then(|s| s.trim().parse::<f64>().ok())").unwrap();
                writeln!(out, "        .unwrap_or_else(|| {{ eprintln!(\"error: missing argument {index}\"); std::process::exit(1); }});").unwrap();
            }
            "arg_str" => {
                let index = node.params["index"].as_f64().unwrap() as usize;
                writeln!(out, "    let {var}: String = std::env::args().nth({index})").unwrap();
                writeln!(out, "        .unwrap_or_else(|| {{ eprintln!(\"error: missing argument {index}\"); std::process::exit(1); }});").unwrap();
            }
            "env_str" => {
                let name = node.params["name"].as_str().unwrap();
                writeln!(
                    out,
                    "    let {var}: String = std::env::var(\"{}\").unwrap_or_default();",
                    rust_string(name)
                )
                .unwrap();
            }
            "env_str_dyn" => {
                let name_var = bind_var(node, "name", &vars);
                writeln!(
                    out,
                    "    let {var}: String = std::env::var(&{name_var}).unwrap_or_default();"
                )
                .unwrap();
            }
            "arg_count" => {
                writeln!(
                    out,
                    "    let {var}: f64 = (std::env::args().count() - 1) as f64;"
                )
                .unwrap();
            }
            "format_str" => {
                let template = node.params["template"].as_str().unwrap();
                let v1 = bind_var(node, "v1", &vars);
                let has_v2 = node.bind.contains_key("v2");
                if has_v2 {
                    let v2 = bind_var(node, "v2", &vars);
                    writeln!(out, "    let {var}: String = \"{}\".replace(\"{{1}}\", &{v1}).replace(\"{{2}}\", &{v2});", rust_string(template)).unwrap();
                } else {
                    writeln!(
                        out,
                        "    let {var}: String = \"{}\".replace(\"{{1}}\", &{v1});",
                        rust_string(template)
                    )
                    .unwrap();
                }
            }
            "exit_code" => {
                let code = bind_var(node, "code", &vars);
                writeln!(
                    out,
                    "    let {var}: () = std::process::exit({code} as i32);"
                )
                .unwrap();
            }
            "substr" => {
                let text = bind_var(node, "text", &vars);
                let start = bind_var(node, "start", &vars);
                let len = bind_var(node, "len", &vars);
                writeln!(out, "    let {var}: String = {{").unwrap();
                writeln!(out, "        let s = {start}.max(0.0).floor() as usize;").unwrap();
                writeln!(out, "        let l = {len}.max(0.0).floor() as usize;").unwrap();
                writeln!(out, "        {text}.chars().skip(s).take(l).collect()").unwrap();
                writeln!(out, "    }};").unwrap();
            }
            "upper_str" => {
                let text = bind_var(node, "text", &vars);
                writeln!(out, "    let {var}: String = {text}.to_uppercase();").unwrap();
            }
            "lower_str" => {
                let text = bind_var(node, "text", &vars);
                writeln!(out, "    let {var}: String = {text}.to_lowercase();").unwrap();
            }
            "trim_str" => {
                let text = bind_var(node, "text", &vars);
                writeln!(out, "    let {var}: String = {text}.trim().to_string();").unwrap();
            }
            "contains_str" => {
                let text = bind_var(node, "text", &vars);
                let needle = bind_var(node, "needle", &vars);
                writeln!(out, "    let {var}: bool = {text}.contains(&*{needle});").unwrap();
            }
            "replace_str" => {
                let text = bind_var(node, "text", &vars);
                let pattern = bind_var(node, "pattern", &vars);
                let replacement = bind_var(node, "replacement", &vars);
                writeln!(
                    out,
                    "    let {var}: String = {text}.replace(&*{pattern}, &{replacement});"
                )
                .unwrap();
            }
            "split_count" => {
                let text = bind_var(node, "text", &vars);
                let delim = bind_var(node, "delim", &vars);
                writeln!(
                    out,
                    "    let {var}: f64 = {text}.split(&*{delim}).count() as f64;"
                )
                .unwrap();
            }
            "split_nth" => {
                let text = bind_var(node, "text", &vars);
                let delim = bind_var(node, "delim", &vars);
                let index = bind_var(node, "index", &vars);
                writeln!(out, "    let {var}: String = {text}.split(&*{delim}).nth({index} as usize).unwrap_or(\"\").to_string();").unwrap();
            }
            "mod_num" => {
                let lhs = bind_var(node, "lhs", &vars);
                let rhs = bind_var(node, "rhs", &vars);
                writeln!(out, "    let {var}: f64 = {lhs} % {rhs};").unwrap();
            }
            "floor" => {
                let value = bind_var(node, "value", &vars);
                writeln!(out, "    let {var}: f64 = {value}.floor();").unwrap();
            }
            "abs" => {
                let value = bind_var(node, "value", &vars);
                writeln!(out, "    let {var}: f64 = {value}.abs();").unwrap();
            }
            "lt" => {
                let lhs = bind_var(node, "lhs", &vars);
                let rhs = bind_var(node, "rhs", &vars);
                writeln!(out, "    let {var}: bool = {lhs} < {rhs};").unwrap();
            }
            "read_stdin_all" => {
                writeln!(out, "    let {var}: String = {{").unwrap();
                writeln!(out, "        let mut buf = String::new();").unwrap();
                writeln!(out, "        std::io::Read::read_to_string(&mut std::io::stdin(), &mut buf).unwrap_or(0);").unwrap();
                writeln!(out, "        buf").unwrap();
                writeln!(out, "    }};").unwrap();
            }
            "append_file" => {
                let path = node.params["path"].as_str().unwrap();
                let content = bind_var(node, "content", &vars);
                writeln!(out, "    let {var}: () = {{").unwrap();
                writeln!(out, "        use std::io::Write as _;").unwrap();
                writeln!(out, "        let mut f = std::fs::OpenOptions::new().create(true).append(true).open(\"{}\").expect(\"cannot open file\");", rust_string(path)).unwrap();
                writeln!(out, "        let _ = f.write_all({content}.as_bytes());").unwrap();
                writeln!(out, "    }};").unwrap();
            }
            "http_get" => {
                let url = node.params["url"].as_str().unwrap();
                writeln!(
                    out,
                    "    let {var}: String = esc_http_get(\"{}\");",
                    rust_string(url)
                )
                .unwrap();
            }
            "http_get_dyn" => {
                let url_var = bind_var(node, "url", &vars);
                writeln!(out, "    let {var}: String = esc_http_get(&{url_var});").unwrap();
            }
            other => {
                writeln!(
                    out,
                    "    compile_error!(\"unsupported primitive in emitter: {}\");",
                    rust_string(other)
                )
                .unwrap();
            }
        }

        vars.insert(node.id.clone(), var);
        writeln!(out).unwrap();
    }

    writeln!(out, "}}").unwrap();

    out
}
