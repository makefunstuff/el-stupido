mod compose;
mod emit;
mod grammar;
mod primitive;

use clap::{Parser, Subcommand};
use std::fs;
use std::io::Write;
use std::process::Command;

#[derive(Parser)]
#[command(
    name = "esc",
    about = "el-stupido compiler — composable primitives for program generation"
)]
struct Cli {
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Compose primitives from a manifest and compile to native binary
    Compose {
        /// Path to composition manifest (JSON)
        manifest: String,
        /// Output binary path
        #[arg(short, long, default_value = "a.out")]
        output: String,
    },
    /// Print generated C source without compiling
    Expand {
        /// Path to composition manifest (JSON)
        manifest: String,
    },
    /// Print GBNF grammar for constrained LLM output
    Grammar,
    /// List all available primitives
    Primitives,
}

fn main() {
    let cli = Cli::parse();
    let registry = primitive::Registry::new();

    match cli.cmd {
        Cmd::Compose { manifest, output } => {
            let json = match fs::read_to_string(&manifest) {
                Ok(s) => s,
                Err(e) => {
                    eprintln!("error: cannot read {manifest}: {e}");
                    std::process::exit(1);
                }
            };

            let comp = match compose::validate(&json, &registry) {
                Ok(c) => c,
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            };

            let c_source = emit::emit_c(&comp);

            // Write to temp file
            let tmp = format!("/tmp/esc_{}.c", std::process::id());
            {
                let mut f = fs::File::create(&tmp).expect("cannot create temp file");
                f.write_all(c_source.as_bytes())
                    .expect("cannot write temp file");
            }

            // Compile with cc
            let status = Command::new("cc")
                .args(["-O2", "-o", &output, &tmp])
                .status();

            // Clean up temp file
            let _ = fs::remove_file(&tmp);

            match status {
                Ok(s) if s.success() => {
                    let meta = fs::metadata(&output).ok();
                    let size = meta.map(|m| m.len()).unwrap_or(0);
                    let c_len = c_source.len();
                    eprintln!(
                        "ok: {} -> {output} ({c_len} bytes C -> {size} bytes binary)",
                        manifest
                    );
                }
                Ok(s) => {
                    eprintln!("error: cc exited with {s}");
                    std::process::exit(1);
                }
                Err(e) => {
                    eprintln!("error: cannot run cc: {e}");
                    std::process::exit(1);
                }
            }
        }

        Cmd::Expand { manifest } => {
            let json = match fs::read_to_string(&manifest) {
                Ok(s) => s,
                Err(e) => {
                    eprintln!("error: cannot read {manifest}: {e}");
                    std::process::exit(1);
                }
            };

            let comp = match compose::validate(&json, &registry) {
                Ok(c) => c,
                Err(e) => {
                    eprintln!("error: {e}");
                    std::process::exit(1);
                }
            };

            print!("{}", emit::emit_c(&comp));
        }

        Cmd::Grammar => {
            print!("{}", grammar::generate_gbnf(&registry));
        }

        Cmd::Primitives => {
            let mut prims: Vec<_> = registry.all().collect();
            prims.sort_by_key(|p| p.id);
            for p in prims {
                println!("  {} — {}", p.id, p.description);
                for param in &p.params {
                    let req = if param.required {
                        "required"
                    } else {
                        "optional"
                    };
                    println!("    {}: {} ({})", param.name, param.ty.label(), req);
                }
                if !p.provides.is_empty() {
                    println!("    provides: [{}]", p.provides.join(", "));
                }
                if !p.requires.is_empty() {
                    println!("    requires: [{}]", p.requires.join(", "));
                }
                println!();
            }
        }
    }
}
