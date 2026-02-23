#!/usr/bin/env python3
"""esbc.py — el-stupido bytecode toolkit.

Full pipeline: JSON (from LLM) → bytecodes → el-stupido source → esc compiler → native binary

Usage:
    # JSON → bytecodes (hex)
    python3 tools/esbc.py encode '{"functions":[{"name":"fact","params":"n","body":"product(1..=n)"}],"main_body":"for i := 1..=12 { print(fact(i)) }"}'

    # Hex → el-stupido source
    python3 tools/esbc.py decode 02000140700172000301ff00107001700c30750001740303

    # JSON → compile+run (full pipeline)
    python3 tools/esbc.py run '{"functions":[...],"main_body":"..."}'

    # Hex → compile+run
    python3 tools/esbc.py run --hex 02000140700172000301ff00...
"""

import sys, json, struct, subprocess, tempfile, os

# ═══════════════════════════════════════════════════════════════
# BYTECODE ISA
# ═══════════════════════════════════════════════════════════════

# Opcodes (1 byte each)
OP_DEF = 0x01  # func_id:u8 nparams:u8 (multi-line, stmts until END)
OP_DEFX = 0x02  # func_id:u8 nparams:u8 (one-liner, expr until END)
OP_END = 0x03
OP_FOR = 0x10  # start:operand end:operand body... END
OP_WHILE = 0x11  # cond body... END
OP_IF = 0x12  # cond body... END
OP_ELSE = 0x13
OP_RET = 0x20  # operand
OP_BRK = 0x21
OP_CONT = 0x22
OP_PRINT = 0x30  # operand
OP_PRINTF = 0x31  # fmt_id:u8 argc:u8 operands...
OP_PROD = 0x40  # start:operand end:operand → value
OP_SUM = 0x41
OP_COUNT = 0x42
OP_ADD = 0x50  # binary ops: pop 2 operands
OP_SUB = 0x51
OP_MUL = 0x52
OP_DIV = 0x53
OP_MOD = 0x54
OP_EQ = 0x60
OP_NE = 0x61
OP_LT = 0x62
OP_LE = 0x63
OP_GT = 0x64
OP_GE = 0x65
OP_INT = 0x70  # val:u8 (0-255)
OP_INT16 = 0x71  # val:i16 big-endian (for larger constants)
OP_PARAM = 0x72  # idx:u8
OP_LOCAL = 0x73  # idx:u8
OP_LVAR = 0x74  # loop variable (implicit)
OP_CALL = 0x75  # func_id:u8 argc:u8 (followed by argc operands)
OP_DECL = 0x76  # local_id:u8 (followed by init operand)
OP_ASGN = 0x77  # local_id:u8 (followed by value operand)
OP_AADD = 0x78  # local_id:u8 (followed by value operand) — add-assign
OP_TERN = 0x79  # cond then else
OP_STR = 0x7A  # len:u8 bytes... (inline string literal)
OP_SREF = 0x7B  # id:u8 (string table reference)
OP_STRTAB = 0x00  # count:u8 (len:u8 bytes...)... — string table header

FUNC_MAIN = 0xFF

# Built-in printf format IDs
FMT_STR_NL = 0  # "%s\n"
FMT_INT_NL = 1  # "%d\n"
FMT_FLT_NL = 2  # "%f\n"

OP_NAMES = {
    0x01: "DEF",
    0x02: "DEFX",
    0x03: "END",
    0x10: "FOR",
    0x11: "WHILE",
    0x12: "IF",
    0x13: "ELSE",
    0x20: "RET",
    0x21: "BRK",
    0x22: "CONT",
    0x30: "PRINT",
    0x31: "PRINTF",
    0x40: "PROD",
    0x41: "SUM",
    0x42: "COUNT",
    0x50: "ADD",
    0x51: "SUB",
    0x52: "MUL",
    0x53: "DIV",
    0x54: "MOD",
    0x60: "EQ",
    0x61: "NE",
    0x62: "LT",
    0x63: "LE",
    0x64: "GT",
    0x65: "GE",
    0x70: "INT",
    0x71: "INT16",
    0x72: "PARAM",
    0x73: "LOCAL",
    0x74: "LVAR",
    0x75: "CALL",
    0x76: "DECL",
    0x77: "ASGN",
    0x78: "AADD",
    0x79: "TERN",
    0x7A: "STR",
    0x7B: "SREF",
    0x00: "STRTAB",
}

# ═══════════════════════════════════════════════════════════════
# ENCODER: JSON → bytecodes
# ═══════════════════════════════════════════════════════════════

BUILTINS = {"printf", "print", "malloc", "free", "exit", "strlen", "strcmp", "sprintf"}


def _is_prose(body):
    words = body.split()
    if len(words) < 3:
        return False
    prose = {
        "the",
        "of",
        "using",
        "that",
        "this",
        "which",
        "output",
        "formatted",
        "simple",
        "value",
        "computes",
        "returns",
        "calculates",
        "function",
        "a",
        "an",
    }
    return sum(1 for w in words if w.lower().rstrip(".,;:") in prose) >= 2


class Encoder:
    """Compiles structured JSON program spec → bytecodes."""

    def __init__(self):
        self.out = bytearray()
        self.func_names = {}  # name → id
        self.next_id = 0
        self.strings = []  # string table
        self._local_map = {}  # name → local_id
        self._loop_vars = []  # stack of loop variable names
        self._param_names = []  # current function's param names

    def _emit(self, *args):
        for a in args:
            if isinstance(a, int):
                self.out.append(a & 0xFF)
            elif isinstance(a, (bytes, bytearray)):
                self.out.extend(a)
            elif isinstance(a, list):
                self.out.extend(a)

    def _func_id(self, name):
        if name == "main":
            return FUNC_MAIN
        if name not in self.func_names:
            self.func_names[name] = self.next_id
            self.next_id += 1
        return self.func_names[name]

    def _add_string(self, s):
        if s in self.strings:
            return self.strings.index(s)
        self.strings.append(s)
        return len(self.strings) - 1

    def _is_oneliner(self, body):
        b = body.strip()
        return (
            "{" not in b
            and "\n" not in b
            and ";" not in b
            and "for " not in b
            and "while " not in b
            and "if " not in b
            and len(b) < 120
        )

    def _emit_int(self, val):
        if 0 <= val <= 255:
            self._emit(OP_INT, val)
        else:
            self._emit(OP_INT16)
            self._emit((val >> 8) & 0xFF, val & 0xFF)

    def _emit_expr(self, expr):
        """Compile a simple expression to bytecodes."""
        expr = expr.strip()

        # ternary: cond ? then : else
        # Find top-level ? and :
        q = self._find_ternary(expr)
        if q:
            cond, then, els = q
            self._emit(OP_TERN)
            self._emit_expr(cond)
            self._emit_expr(then)
            self._emit_expr(els)
            return

        # binary ops (lowest precedence first)
        for ops in [
            [
                ("==", OP_EQ),
                ("!=", OP_NE),
                ("<=", OP_LE),
                (">=", OP_GE),
                ("<", OP_LT),
                (">", OP_GT),
            ],
            [("+", OP_ADD), ("-", OP_SUB)],
            [("*", OP_MUL), ("/", OP_DIV), ("%", OP_MOD)],
        ]:
            for sym, opcode in ops:
                pos = self._find_binop(expr, sym)
                if pos >= 0:
                    left = expr[:pos].strip()
                    right = expr[pos + len(sym) :].strip()
                    self._emit(opcode)
                    self._emit_expr(left)
                    self._emit_expr(right)
                    return

        # function call: name(args)
        m = self._parse_call(expr)
        if m:
            name, args = m
            if name == "product":
                self._emit(OP_PROD)
                self._emit_range(args[0] if args else "1..=$0")
                return
            elif name == "sum":
                self._emit(OP_SUM)
                self._emit_range(args[0] if args else "1..=$0")
                return
            elif name == "print":
                self._emit(OP_PRINT)
                self._emit_expr(args[0])
                return
            elif name == "printf":
                fmt = args[0].strip().strip('"')
                sid = self._add_string(fmt) if fmt not in ("%s\\n", "%d\\n") else None
                if fmt == "%s\\n" or fmt == "%s\n":
                    self._emit(OP_PRINTF, FMT_STR_NL, len(args) - 1)
                elif fmt == "%d\\n" or fmt == "%d\n":
                    self._emit(OP_PRINTF, FMT_INT_NL, len(args) - 1)
                else:
                    self._emit(OP_PRINTF, FMT_STR_NL, len(args) - 1)
                for a in args[1:]:
                    a = a.strip()
                    if a.startswith('"') and a.endswith('"'):
                        sid = self._add_string(a[1:-1])
                        self._emit(OP_SREF, sid)
                    else:
                        self._emit_expr(a)
                return
            else:
                fid = self._func_id(name)
                self._emit(OP_CALL, fid, len(args))
                for a in args:
                    self._emit_expr(a.strip())
                return

        # param reference: $N
        if expr.startswith("$") and expr[1:].isdigit():
            self._emit(OP_PARAM, int(expr[1:]))
            return

        # integer literal
        try:
            val = int(expr)
            self._emit_int(val)
            return
        except ValueError:
            pass

        # string literal
        if expr.startswith('"') and expr.endswith('"'):
            s = expr[1:-1]
            sid = self._add_string(s)
            self._emit(OP_SREF, sid)
            return

        # resolve named variable: loop var > local > param
        if expr.isidentifier():
            # check loop variable stack (innermost first)
            for lv in reversed(self._loop_vars):
                if expr == lv:
                    self._emit(OP_LVAR)
                    return
            # check locals
            if expr in self._local_map:
                self._emit(OP_LOCAL, self._local_map[expr])
                return
            # check params
            if expr in self._param_names:
                self._emit(OP_PARAM, self._param_names.index(expr))
                return
            # unknown identifier — assume param 0 as fallback
            self._emit(OP_PARAM, 0)
            return

        # fallback
        self._emit(OP_PARAM, 0)

    def _emit_range(self, expr):
        """Parse range like 1..=n or 1..=$0"""
        if "..=" in expr:
            parts = expr.split("..=")
            self._emit_expr(parts[0].strip())
            self._emit_expr(parts[1].strip())
        elif ".." in expr:
            parts = expr.split("..")
            self._emit_expr(parts[0].strip())
            self._emit_expr(parts[1].strip())
        else:
            self._emit_int(1)
            self._emit_expr(expr)

    def _find_ternary(self, expr):
        """Find top-level ternary ? : respecting parens."""
        depth = 0
        qpos = -1
        for i, c in enumerate(expr):
            if c in "(":
                depth += 1
            elif c in ")":
                depth -= 1
            elif c == "?" and depth == 0:
                qpos = i
            elif c == ":" and depth == 0 and qpos >= 0:
                return (
                    expr[:qpos].strip(),
                    expr[qpos + 1 : i].strip(),
                    expr[i + 1 :].strip(),
                )
        return None

    def _find_binop(self, expr, op):
        """Find rightmost top-level binary operator (left-associative)."""
        depth = 0
        best = -1
        i = len(expr) - 1
        while i >= 0:
            c = expr[i]
            if c == ")":
                depth += 1
            elif c == "(":
                depth -= 1
            elif depth == 0:
                if expr[i : i + len(op)] == op:
                    # avoid matching <= as < when looking for <
                    if op in ("<", ">") and i + 1 < len(expr) and expr[i + 1] == "=":
                        pass
                    # avoid matching == as = or != as !
                    elif op == "=" and i > 0 and expr[i - 1] in ("!", "<", ">", "="):
                        pass
                    else:
                        if i > 0:  # don't match unary minus at start
                            return i
            i -= 1
        return -1

    def _parse_call(self, expr):
        """Parse func(arg1, arg2, ...) → (name, [args])"""
        if "(" not in expr or not expr.endswith(")"):
            return None
        paren = expr.index("(")
        name = expr[:paren].strip()
        if not name or not (name.isidentifier() or name.startswith("@")):
            return None
        inner = expr[paren + 1 : -1]
        # split on top-level commas
        args = []
        depth = 0
        start = 0
        for i, c in enumerate(inner):
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == "," and depth == 0:
                args.append(inner[start:i])
                start = i + 1
        args.append(inner[start:])
        args = [a.strip() for a in args if a.strip()]
        return (name, args)

    def _emit_stmt(self, stmt):
        """Compile a statement to bytecodes."""
        stmt = stmt.strip()
        if not stmt:
            return

        # for loop: for VAR := START..=END { body }
        if stmt.startswith("for "):
            rest = stmt[4:].strip()
            # parse: var := start..=end { body }
            assign_pos = rest.find(":=")
            if assign_pos >= 0:
                var = rest[:assign_pos].strip()
                rest = rest[assign_pos + 2 :].strip()
                # find range
                if "..=" in rest:
                    range_part, body = self._split_range_body(rest)
                    parts = range_part.split("..=")
                    self._emit(OP_FOR)
                    self._emit_expr(parts[0].strip())
                    self._emit_expr(parts[1].strip())
                    self._loop_vars.append(var)
                    self._emit_block(body)
                    self._loop_vars.pop()
                    self._emit(OP_END)
                    return

        # while: while COND { body }
        if stmt.startswith("while "):
            rest = stmt[6:].strip()
            cond, body = self._split_cond_body(rest)
            self._emit(OP_WHILE)
            self._emit_expr(cond)
            self._emit_block(body)
            self._emit(OP_END)
            return

        # if/el if/el chain
        if stmt.startswith("if "):
            self._emit_if_chain(stmt)
            return

        # el if / el should have been merged into preceding if by _emit_block
        # if we still get them standalone, skip gracefully
        if (
            stmt.startswith("el if ")
            or stmt.startswith("el {")
            or stmt.startswith("el\n")
        ):
            return

        # el { ... } (else)
        if stmt.startswith("el {") or stmt.startswith("el\n"):
            self._emit(OP_ELSE)
            body = stmt[2:].strip()
            if body.startswith("{") and body.endswith("}"):
                body = body[1:-1].strip()
            self._emit_block(body)
            self._emit(OP_END)
            return

        # return
        if stmt.startswith("return "):
            self._emit(OP_RET)
            self._emit_expr(stmt[7:].strip())
            return

        # break/continue
        if stmt == "break":
            self._emit(OP_BRK)
            return
        if stmt == "continue":
            self._emit(OP_CONT)
            return

        # variable declaration: name := expr
        if ":=" in stmt:
            parts = stmt.split(":=", 1)
            name = parts[0].strip()
            val = parts[1].strip()
            if name not in self._local_map:
                self._local_map[name] = len(self._local_map)
            lid = self._local_map[name]
            self._emit(OP_DECL, lid)
            self._emit_expr(val)
            return

        # compound assignment: name += expr
        for op, opcode in [("+=", OP_AADD)]:
            if op in stmt:
                parts = stmt.split(op, 1)
                name = parts[0].strip()
                val = parts[1].strip()
                if name not in self._local_map:
                    self._local_map[name] = len(self._local_map)
                lid = self._local_map[name]
                self._emit(opcode, lid)
                self._emit_expr(val)
                return

        # assignment: name = expr (not ==)
        eq_pos = stmt.find("=")
        if (
            eq_pos > 0
            and stmt[eq_pos - 1] not in ("!", "<", ">", "=")
            and stmt[eq_pos + 1 : eq_pos + 2] != "="
        ):
            name = stmt[:eq_pos].strip()
            val = stmt[eq_pos + 1 :].strip()
            if name.isidentifier() and name in self._local_map:
                lid = self._local_map[name]
                self._emit(OP_ASGN, lid)
                self._emit_expr(val)
                return

        # compound assignment: name += expr
        for op, opcode in [("+=", OP_AADD)]:
            if op in stmt:
                parts = stmt.split(op, 1)
                name = parts[0].strip()
                val = parts[1].strip()
                if not hasattr(self, "_local_map"):
                    self._local_map = {}
                if name not in self._local_map:
                    self._local_map[name] = len(self._local_map)
                lid = self._local_map[name]
                self._emit(opcode, lid)
                self._emit_expr(val)
                return

        # assignment: name = expr (not ==)
        eq_pos = stmt.find("=")
        if (
            eq_pos > 0
            and stmt[eq_pos - 1] not in ("!", "<", ">", "=")
            and stmt[eq_pos + 1 : eq_pos + 2] != "="
        ):
            name = stmt[:eq_pos].strip()
            val = stmt[eq_pos + 1 :].strip()
            if name.isidentifier():
                if not hasattr(self, "_local_map"):
                    self._local_map = {}
                if name in self._local_map:
                    lid = self._local_map[name]
                    self._emit(OP_ASGN, lid)
                    self._emit_expr(val)
                    return

        # expression statement (function calls like print(...))
        self._emit_expr(stmt)

    def _emit_block(self, body):
        """Compile a block of statements."""
        stmts = self._split_stmts(body)
        # merge el-if / el chains into the preceding if
        merged = []
        for s in stmts:
            if (
                s.startswith("el if ") or s.startswith("el {") or s.startswith("el\n")
            ) and merged:
                merged[-1] = merged[-1] + "\n" + s
            else:
                merged.append(s)
        for s in merged:
            self._emit_stmt(s)

    def _emit_if_chain(self, stmt):
        """Compile full if / el if / el chain."""
        # Parse: if COND { BODY } [el if COND { BODY }]* [el { BODY }]
        rest = stmt
        if rest.startswith("if "):
            rest = rest[3:].strip()
        cond, body = self._split_cond_body(rest)
        self._emit(OP_IF)
        self._emit_expr(cond)
        self._emit_block(body)

        # find remainder after the closing } of the if body
        brace = rest.find("{")
        depth = 0
        end_pos = len(rest)
        for i in range(brace, len(rest)):
            if rest[i] == "{":
                depth += 1
            elif rest[i] == "}":
                depth -= 1
            if depth == 0:
                end_pos = i + 1
                break
        remainder = rest[end_pos:].strip()

        # handle el if ...
        if remainder.startswith("el if "):
            self._emit(OP_ELSE)
            self._emit_if_chain(remainder[3:].strip())  # recurse with "if ..."
            self._emit(OP_END)
        # handle el { ... }
        elif remainder.startswith("el {") or remainder.startswith("el\n"):
            self._emit(OP_ELSE)
            el_body = remainder[2:].strip()
            if el_body.startswith("{") and el_body.endswith("}"):
                el_body = el_body[1:-1].strip()
            self._emit_block(el_body)
            self._emit(OP_END)
        else:
            self._emit(OP_END)

    def _split_range_body(self, s):
        """Split 'start..=end { body }' → (range_part, body)"""
        brace = s.find("{")
        if brace < 0:
            return s, ""
        range_part = s[:brace].strip()
        # find matching closing brace
        depth = 0
        for i in range(brace, len(s)):
            if s[i] == "{":
                depth += 1
            elif s[i] == "}":
                depth -= 1
            if depth == 0:
                return range_part, s[brace + 1 : i].strip()
        return range_part, s[brace + 1 :].strip()

    def _split_cond_body(self, s):
        """Split 'cond { body }' → (cond, body)"""
        brace = s.find("{")
        if brace < 0:
            return s, ""
        cond = s[:brace].strip()
        depth = 0
        for i in range(brace, len(s)):
            if s[i] == "{":
                depth += 1
            elif s[i] == "}":
                depth -= 1
            if depth == 0:
                return cond, s[brace + 1 : i].strip()
        return cond, s[brace + 1 :].strip()

    def _split_stmts(self, body):
        """Split body into statements (by ; or newline, respecting braces)."""
        stmts = []
        current = []
        depth = 0
        i = 0
        chars = body.strip()
        while i < len(chars):
            c = chars[i]
            if c == "{":
                depth += 1
                current.append(c)
            elif c == "}":
                depth -= 1
                current.append(c)
                if depth == 0:
                    stmts.append("".join(current).strip())
                    current = []
            elif (c == ";" or c == "\n") and depth == 0:
                s = "".join(current).strip()
                if s:
                    stmts.append(s)
                current = []
            else:
                current.append(c)
            i += 1
        s = "".join(current).strip()
        if s:
            stmts.append(s)

        # merge "el if" and "el" with previous if
        merged = []
        for s in stmts:
            if s.startswith("el if ") or s.startswith("el {") or s == "el":
                # this is part of a previous if chain — emit as else + if
                merged.append(s)
            else:
                merged.append(s)
        return merged

    def encode_json(self, data):
        """Encode a JSON program spec to bytecodes."""
        self.out = bytearray()
        self.func_names = {}
        self.next_id = 0
        self.strings = []

        # First pass: register all function names
        for fn in data.get("functions", []):
            name = fn["name"]
            if name not in BUILTINS:
                self._func_id(name)

        # Encode functions
        for fn in data.get("functions", []):
            name = fn["name"]
            if name in BUILTINS:
                continue
            if _is_prose(fn.get("body", "")):
                continue

            self._local_map = {}
            self._loop_vars = []
            fid = self._func_id(name)
            params = fn.get("params", "").strip()
            param_list = (
                [p.strip() for p in params.split(",") if p.strip()] if params else []
            )
            nparams = len(param_list)
            self._param_names = param_list
            body = fn.get("body", "").strip()

            if self._is_oneliner(body) and name != "main":
                self._emit(OP_DEFX, fid, nparams)
                self._emit_expr(body)
                self._emit(OP_END)
            else:
                self._emit(OP_DEF, fid, nparams)
                b = body
                if b.startswith("{") and b.endswith("}"):
                    b = b[1:-1].strip()
                b = b.replace("else if ", "el if ").replace("else {", "el {")
                b = b.replace("elif ", "el if ")
                self._emit_block(b)
                self._emit(OP_END)

        # Encode main_body
        main_body = data.get("main_body", "").strip()
        has_main = any(fn["name"] == "main" for fn in data.get("functions", []))
        if main_body and not has_main:
            self._local_map = {}
            self._loop_vars = []
            self._param_names = []
            self._emit(OP_DEF, FUNC_MAIN, 0)
            mb = main_body
            if mb.startswith("{") and mb.endswith("}"):
                mb = mb[1:-1].strip()
            mb = mb.replace("else if ", "el if ").replace("else {", "el {")
            self._emit_block(mb)
            self._emit(OP_END)

        # Prepend string table if we have strings
        if self.strings:
            header = bytearray()
            header.append(OP_STRTAB)
            header.append(len(self.strings))
            for s in self.strings:
                b = s.encode("utf-8")
                header.append(len(b))
                header.extend(b)
            self.out = header + self.out

        return bytes(self.out)


# ═══════════════════════════════════════════════════════════════
# DECODER: bytecodes → el-stupido source
# ═══════════════════════════════════════════════════════════════


class Decoder:
    """Decompiles bytecodes → el-stupido source code."""

    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.strings = []
        self.func_params = {}  # func_id → param_count

    def _read(self):
        if self.pos >= len(self.data):
            raise ValueError(f"unexpected end of bytecodes at pos {self.pos}")
        b = self.data[self.pos]
        self.pos += 1
        return b

    def _peek(self):
        if self.pos >= len(self.data):
            return None
        return self.data[self.pos]

    def _read_i16(self):
        hi = self._read()
        lo = self._read()
        val = (hi << 8) | lo
        if val >= 0x8000:
            val -= 0x10000
        return val

    def _func_name(self, fid):
        if fid == FUNC_MAIN:
            return "main"
        return f"f{fid}"

    def _param_name(self, fid, idx):
        # generate nice param names: n, a, b, c, ...
        names = ["n", "a", "b", "c", "d", "e"]
        return names[idx] if idx < len(names) else f"p{idx}"

    def _local_name(self, idx):
        names = ["acc", "d", "x", "y", "z", "w"]
        return names[idx] if idx < len(names) else f"v{idx}"

    def _decode_expr(self):
        """Decode an operand/expression from bytecodes."""
        op = self._read()

        if op == OP_INT:
            return str(self._read())
        if op == OP_INT16:
            return str(self._read_i16())
        if op == OP_PARAM:
            idx = self._read()
            return self._param_name(self._cur_func, idx)
        if op == OP_LOCAL:
            idx = self._read()
            return self._local_name(idx)
        if op == OP_LVAR:
            return self._loop_var or "i"
        if op == OP_CALL:
            fid = self._read()
            argc = self._read()
            args = [self._decode_expr() for _ in range(argc)]
            return f"{self._func_name(fid)}({', '.join(args)})"
        if op == OP_SREF:
            sid = self._read()
            if sid < len(self.strings):
                return f'"{self.strings[sid]}"'
            return f'"?str{sid}"'

        # binary ops
        binops = {
            OP_ADD: "+",
            OP_SUB: "-",
            OP_MUL: "*",
            OP_DIV: "/",
            OP_MOD: "%",
            OP_EQ: "==",
            OP_NE: "!=",
            OP_LT: "<",
            OP_LE: "<=",
            OP_GT: ">",
            OP_GE: ">=",
        }
        if op in binops:
            left = self._decode_expr()
            right = self._decode_expr()
            return f"{left} {binops[op]} {right}"

        # built-ins
        if op == OP_PROD:
            lo = self._decode_expr()
            hi = self._decode_expr()
            return f"product({lo}..={hi})"
        if op == OP_SUM:
            lo = self._decode_expr()
            hi = self._decode_expr()
            return f"sum({lo}..={hi})"

        if op == OP_TERN:
            cond = self._decode_expr()
            then = self._decode_expr()
            els = self._decode_expr()
            return f"{cond} ? {then} : {els}"

        if op == OP_PRINT:
            arg = self._decode_expr()
            return f"print({arg})"

        raise ValueError(
            f"unexpected opcode 0x{op:02x} at pos {self.pos - 1} in expression context"
        )

    def _decode_if_chain(self, indent):
        """Decode IF [ELSE IF]* [ELSE] END chain into lines."""
        pad = " " * indent
        lines = []

        cond = self._decode_expr()
        body = self._decode_stmts(indent + 2)

        if self._peek() == OP_ELSE:
            self._read()  # consume ELSE
            # is it el if or el?
            if self._peek() == OP_IF:
                self._read()  # consume IF
                else_lines = self._decode_if_chain(indent)
                lines.append(f"{pad}if {cond} {{")
                lines.extend(body)
                # merge: } el if ...
                if else_lines and else_lines[0].strip().startswith("if "):
                    lines.append(f"{pad}}} el {else_lines[0].strip()}")
                    lines.extend(else_lines[1:])
                else:
                    lines.append(f"{pad}}} el {{")
                    lines.extend(else_lines)
                    lines.append(f"{pad}}}")
            else:
                else_body = self._decode_stmts(indent + 2)
                self._read()  # END
                lines.append(f"{pad}if {cond} {{")
                lines.extend(body)
                lines.append(f"{pad}}} el {{")
                lines.extend(else_body)
                lines.append(f"{pad}}}")
        else:
            self._read()  # END
            lines.append(f"{pad}if {cond} {{")
            lines.extend(body)
            lines.append(f"{pad}}}")

        return lines

    def _decode_stmts(self, indent=2):
        """Decode statements until END."""
        lines = []
        pad = " " * indent
        while self._peek() is not None and self._peek() != OP_END:
            op = self._peek()

            if op == OP_FOR:
                self._read()
                lo = self._decode_expr()
                hi = self._decode_expr()
                old_lv = self._loop_var
                self._loop_var = (
                    "i" if not self._loop_var else chr(ord(self._loop_var) + 1)
                )
                lv = self._loop_var
                body = self._decode_stmts(indent + 2)
                self._read()  # END
                self._loop_var = old_lv
                lines.append(f"{pad}for {lv} := {lo}..={hi} {{")
                lines.extend(body)
                lines.append(f"{pad}}}")

            elif op == OP_WHILE:
                self._read()
                cond = self._decode_expr()
                body = self._decode_stmts(indent + 2)
                self._read()  # END
                lines.append(f"{pad}while {cond} {{")
                lines.extend(body)
                lines.append(f"{pad}}}")

            elif op == OP_IF:
                self._read()
                lines.extend(self._decode_if_chain(indent))

            elif op == OP_RET:
                self._read()
                val = self._decode_expr()
                lines.append(f"{pad}return {val}")

            elif op == OP_BRK:
                self._read()
                lines.append(f"{pad}break")

            elif op == OP_PRINT:
                self._read()
                val = self._decode_expr()
                lines.append(f"{pad}print({val})")

            elif op == OP_PRINTF:
                self._read()
                fmt_id = self._read()
                argc = self._read()
                fmts = ['"%s\\n"', '"%d\\n"', '"%f\\n"']
                fmt = fmts[fmt_id] if fmt_id < len(fmts) else f'"?fmt{fmt_id}"'
                args = [self._decode_expr() for _ in range(argc)]
                lines.append(f"{pad}printf({fmt}, {', '.join(args)})")

            elif op == OP_DECL:
                self._read()
                lid = self._read()
                val = self._decode_expr()
                lines.append(f"{pad}{self._local_name(lid)} := {val}")

            elif op == OP_ASGN:
                self._read()
                lid = self._read()
                val = self._decode_expr()
                lines.append(f"{pad}{self._local_name(lid)} = {val}")

            elif op == OP_AADD:
                self._read()
                lid = self._read()
                val = self._decode_expr()
                lines.append(f"{pad}{self._local_name(lid)} += {val}")

            elif op == OP_ELSE:
                break  # let caller handle

            else:
                # expression statement
                expr = self._decode_expr()
                lines.append(f"{pad}{expr}")

        return lines

    def decode(self):
        """Decode full bytecodes → el-stupido source."""
        self.pos = 0
        self._loop_var = None
        self._cur_func = None
        lines = []

        # string table
        if self._peek() == OP_STRTAB:
            self._read()
            count = self._read()
            for _ in range(count):
                slen = self._read()
                s = bytes(self._read() for _ in range(slen)).decode("utf-8")
                self.strings.append(s)

        # functions
        while self.pos < len(self.data):
            op = self._read()

            if op == OP_DEFX:
                fid = self._read()
                nparams = self._read()
                self._cur_func = fid
                self.func_params[fid] = nparams
                name = self._func_name(fid)
                params = ", ".join(self._param_name(fid, i) for i in range(nparams))
                body = self._decode_expr()
                self._read()  # END
                lines.append(f"{name}({params}) = {body}")

            elif op == OP_DEF:
                fid = self._read()
                nparams = self._read()
                self._cur_func = fid
                self.func_params[fid] = nparams
                self._loop_var = None
                name = self._func_name(fid)
                params = ", ".join(self._param_name(fid, i) for i in range(nparams))
                body_lines = self._decode_stmts(2)
                self._read()  # END
                if name == "main":
                    lines.append(f"fn {name}() {{")
                else:
                    lines.append(f"fn {name}({params}) {{")
                lines.extend(body_lines)
                lines.append("}")

            elif op == OP_END:
                pass  # trailing END

            lines.append("")

        return "\n".join(l for l in lines if l is not None).strip() + "\n"


# ═══════════════════════════════════════════════════════════════
# CLI
# ═══════════════════════════════════════════════════════════════


def compile_and_run(source):
    """Compile el-stupido source and run it."""
    with tempfile.NamedTemporaryFile(suffix=".es", mode="w", delete=False) as f:
        f.write(source)
        src = f.name
    binpath = src.replace(".es", "")
    try:
        r = subprocess.run(
            ["./esc", src, "-o", binpath], capture_output=True, text=True, timeout=30
        )
        if r.returncode != 0:
            return "COMPILE_FAIL", r.stderr.strip().split("\n")[
                0
            ] if r.stderr else "unknown"
        r = subprocess.run([binpath], capture_output=True, text=True, timeout=10)
        return "OK", r.stdout.strip()
    except subprocess.TimeoutExpired:
        return "TIMEOUT", ""
    except Exception as e:
        return "ERROR", str(e)
    finally:
        for p in [src, binpath]:
            try:
                os.unlink(p)
            except:
                pass


def main():
    if len(sys.argv) < 2:
        print("Usage: esbc.py <encode|decode|run|pipeline> [args]")
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "encode":
        data = json.loads(sys.argv[2])
        enc = Encoder()
        bc = enc.encode_json(data)
        print(bc.hex())

    elif cmd == "decode":
        hexstr = sys.argv[2]
        bc = bytes.fromhex(hexstr)
        dec = Decoder(bc)
        print(dec.decode())

    elif cmd == "run":
        if "--hex" in sys.argv:
            hexstr = sys.argv[sys.argv.index("--hex") + 1]
            bc = bytes.fromhex(hexstr)
            dec = Decoder(bc)
            source = dec.decode()
        else:
            data = json.loads(sys.argv[2])
            enc = Encoder()
            bc = enc.encode_json(data)
            dec = Decoder(bc)
            source = dec.decode()

        print(
            f"--- bytecodes: {len(bc)} bytes, hex: {bc.hex()[:80]}{'...' if len(bc.hex()) > 80 else ''}"
        )
        print(f"--- source ---")
        print(source)
        print(f"--- compile+run ---")
        status, output = compile_and_run(source)
        if status == "OK":
            print(output)
        else:
            print(f"{status}: {output}")

    elif cmd == "pipeline":
        # Full pipeline test with JSON input
        data = json.loads(sys.argv[2])
        enc = Encoder()
        bc = enc.encode_json(data)
        dec = Decoder(bc)
        source = dec.decode()
        status, output = compile_and_run(source)
        # Output: hex, source, result
        print(
            json.dumps(
                {
                    "hex": bc.hex(),
                    "bytes": len(bc),
                    "source": source,
                    "status": status,
                    "output": output,
                },
                indent=2,
            )
        )


if __name__ == "__main__":
    main()
