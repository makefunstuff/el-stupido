# TAPE.md

Compact model-facing manifest format for `esc`.

## Goals

- Minimal token footprint.
- Deterministic parse.
- Fixed decision surface for small models.

## Structure

- `A <app-id>`: app name (required, once).
- `C <cap> [cap ...]`: declared capabilities (optional, repeatable).
- `<idx> <op> <args...>`: instruction tape lines.

Indices must start at `0` and be contiguous (`0,1,2,...`).
References are instruction indices (`0`, `1`, ...), optionally `@0`, `@1`.

## Example

```txt
A sum-demo
C io_write

0 cn 13
1 cn 29
2 ad 0 1
3 pn 2
```

## Opcodes

- `cn` -> `const_num value=<num>`
- `cs` -> `const_str value=<str>`
- `ad|sb|ml|dv` -> `add|sub|mul|div lhs=<ref> rhs=<ref>`
- `gt|eq` -> `gt|eq_num lhs=<ref> rhs=<ref>`
- `an|ob` -> `and_bool|or_bool lhs=<ref> rhs=<ref>`
- `nt` -> `not_bool value=<ref>`
- `sn|ss` -> `select_num|select_str cond=<ref> then=<ref> else=<ref>`
- `ts` -> `to_string value=<ref>`
- `cc` -> `concat left=<ref> right=<ref>`
- `ls` -> `len_str text=<ref>`
- `rp` -> `repeat_str text=<ref> times=<ref>`
- `cw` -> `cwd`
- `pj` -> `path_join left=<ref> right=<ref>`
- `ri` -> `read_stdin [prompt]`
- `pf` -> `parse_num text=<ref>`
- `rf` -> `read_file path=<str>`
- `rd` -> `read_file_dyn path=<ref>`
- `wf` -> `write_file path=<str> content=<ref>`
- `wd` -> `write_file_dyn path=<ref> content=<ref>`
- `pn|ps` -> `print_num|print_str value=<ref>`

## Notes

- Strings with spaces must be quoted.
- `#` starts a comment outside quotes.
- Validation and capability checks are shared with JSON and `.esc` inputs.
