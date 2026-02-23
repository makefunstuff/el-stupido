#include "parser.h"
#include "preproc.h"

int parser_no_std = 0;

/* ---- helpers ---- */
static void next(Parser *p) { p->tok = lexer_next(&p->lex); }

static void skip_nl(Parser *p) {
    while (p->tok.kind == TOK_NEWLINE || p->tok.kind == TOK_SEMI) next(p);
}

static void perror_at(Parser *p, const char *msg) {
    es_error_at(p->file, p->tok.line, p->tok.col, "%s (got '%s')",
                msg, tok_str(p->tok.kind));
}

static Token expect(Parser *p, TokenKind k) {
    if (p->tok.kind != k) {
        char buf[128];
        snprintf(buf, sizeof(buf), "expected '%s'", tok_str(k));
        perror_at(p, buf);
    }
    Token t = p->tok;
    next(p);
    return t;
}

static bool check(Parser *p, TokenKind k) { return p->tok.kind == k; }

/* check if current token is a specific identifier (by name) */
static bool tok_is(Parser *p, const char *name) {
    return p->tok.kind == TOK_IDENT && p->tok.len == (int)strlen(name) &&
           memcmp(p->tok.start, name, p->tok.len) == 0;
}

static bool match(Parser *p, TokenKind k) {
    if (p->tok.kind == k) { next(p); return true; }
    return false;
}

static void expect_nl_or_end(Parser *p) {
    if (p->tok.kind == TOK_NEWLINE || p->tok.kind == TOK_SEMI) { next(p); skip_nl(p); }
    else if (p->tok.kind != TOK_RBRACE && p->tok.kind != TOK_EOF) {
        perror_at(p, "expected newline or ';'");
    }
}

static char *tok_name(Token *t) {
    return es_strndup(t->start, t->len);
}

/* ---- forward declarations ---- */
static EsType *parse_type(Parser *p);
static Node   *parse_expr(Parser *p);
static Node   *parse_block(Parser *p);
static Param  *parse_params(Parser *p, int *count, bool *vararg, bool allow_anon);
Node *parser_parse_prelude(Parser *p);

/* ---- type parsing ---- */
static EsType *parse_type(Parser *p) {
    /* function pointer: *fn(types) -> ret */
    if (check(p, TOK_STAR)) {
        next(p); /* consume * */
        if (check(p, TOK_FN)) {
            next(p); /* consume fn/🔧 */
            expect(p, TOK_LPAREN);
            int pc = 0; bool va = false;
            Param *params = parse_params(p, &pc, &va, true);
            expect(p, TOK_RPAREN);
            EsType *ret = type_basic(TY_VOID);
            if (match(p, TOK_ARROW)) ret = parse_type(p);
            EsType **ptypes = NULL;
            if (pc > 0) {
                ptypes = (EsType **)malloc(pc * sizeof(EsType *));
                for (int i = 0; i < pc; i++) ptypes[i] = params[i].type;
            }
            return type_ptr(type_fn(ret, ptypes, pc, va));
        }
        /* regular pointer: *type */
        return type_ptr(parse_type(p));
    }

    /* array: [ N ] type */
    if (match(p, TOK_LBRACKET)) {
        Token sz = expect(p, TOK_INT_LIT);
        expect(p, TOK_RBRACKET);
        return type_array((int)sz.int_val, parse_type(p));
    }

    /* basic types */
    TokenKind k = p->tok.kind;
    switch (k) {
    case TOK_I8:  next(p); return type_basic(TY_I8);
    case TOK_I16: next(p); return type_basic(TY_I16);
    case TOK_I32: next(p); return type_basic(TY_I32);
    case TOK_I64: next(p); return type_basic(TY_I64);
    case TOK_U8:  next(p); return type_basic(TY_U8);
    case TOK_U16: next(p); return type_basic(TY_U16);
    case TOK_U32: next(p); return type_basic(TY_U32);
    case TOK_U64: next(p); return type_basic(TY_U64);
    case TOK_F32: next(p); return type_basic(TY_F32);
    case TOK_F64: next(p); return type_basic(TY_F64);
    case TOK_VOID: next(p); return type_basic(TY_VOID);
    /* canonical lowering: bool is represented as i32 in core IR */
    case TOK_BOOL: next(p); return type_basic(TY_I32);
    case TOK_IDENT: {
        /* named struct type */
        Token t = p->tok; next(p);
        EsType *ty = (EsType *)calloc(1, sizeof(EsType));
        ty->kind = TY_STRUCT;
        ty->strct.name = tok_name(&t);
        return ty;
    }
    default:
        perror_at(p, "expected type");
        return NULL; /* unreachable */
    }
}

/* ---- is current token start of a type? ---- */
static bool is_type_start(Parser *p) {
    switch (p->tok.kind) {
    case TOK_I8: case TOK_I16: case TOK_I32: case TOK_I64:
    case TOK_U8: case TOK_U16: case TOK_U32: case TOK_U64:
    case TOK_F32: case TOK_F64: case TOK_VOID: case TOK_BOOL:
    case TOK_STAR: case TOK_LBRACKET:
        return true;
    default:
        return false;
    }
}

/* ---- parameter list ---- */
static Param *parse_params(Parser *p, int *count, bool *vararg, bool allow_anon) {
    struct { Param *items; int count; int cap; } da = {0};
    *vararg = false;

    if (check(p, TOK_RPAREN)) { *count = 0; return NULL; }

    /* check for lone ... (no params before) */
    if (check(p, TOK_ELLIPSIS)) {
        next(p);
        *vararg = true;
        *count = 0;
        return NULL;
    }

    int anon_idx = 0;
    for (;;) {
        if (check(p, TOK_ELLIPSIS)) {
            next(p);
            *vararg = true;
            break;
        }
        /* anonymous param: type only (for ext declarations) */
        if (allow_anon && is_type_start(p)) {
            EsType *ty = parse_type(p);
            char anon_name[16];
            snprintf(anon_name, sizeof(anon_name), "_p%d", anon_idx++);
            Param pm = { .name = es_strdup(anon_name), .type = ty };
            da_push(da, pm);
            if (!match(p, TOK_COMMA)) break;
            continue;
        }
        Token name = expect(p, TOK_IDENT);
        /* check if this is name:type or anonymous struct-type param */
        if (check(p, TOK_COLON)) {
            next(p);
            EsType *ty = parse_type(p);
            Param pm = { .name = tok_name(&name), .type = ty };
            da_push(da, pm);
        } else if (allow_anon) {
            /* identifier used as struct type name (anonymous param) */
            EsType *ty = (EsType *)calloc(1, sizeof(EsType));
            ty->kind = TY_STRUCT;
            ty->strct.name = tok_name(&name);
            char anon_name[16];
            snprintf(anon_name, sizeof(anon_name), "_p%d", anon_idx++);
            Param pm = { .name = es_strdup(anon_name), .type = ty };
            da_push(da, pm);
        } else {
            /* default to i32 when no type is specified */
            Param pm = { .name = tok_name(&name), .type = type_basic(TY_I32) };
            da_push(da, pm);
        }
        if (!match(p, TOK_COMMA)) break;
    }

    *count = da.count;
    return da.items;
}

/* Parse struct init payload: Type { field: expr, ... } */
static Node *parse_struct_init_literal(Parser *p, EsType *ty, int line, int col) {
    struct { char **items; int count; int cap; } fnames = {0};
    struct { Node **items; int count; int cap; } fvals = {0};

    expect(p, TOK_LBRACE);
    skip_nl(p);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Token fname = expect(p, TOK_IDENT);
        expect(p, TOK_COLON);
        da_push(fnames, tok_name(&fname));
        da_push(fvals, parse_expr(p));
        if (!match(p, TOK_COMMA)) {
            skip_nl(p);
            if (!check(p, TOK_RBRACE)) skip_nl(p);
        } else {
            skip_nl(p);
        }
    }
    expect(p, TOK_RBRACE);

    Node *n = node_new(ND_STRUCT_INIT, line, col);
    n->struct_init.stype = ty;
    n->struct_init.fields = fnames.items;
    n->struct_init.vals = fvals.items;
    n->struct_init.field_count = fnames.count;
    return n;
}

/* Disambiguate IDENT { ... } between struct-init and statement block opener.
   Struct init must start with `}` (empty) or `ident:` field syntax. */
static bool looks_like_struct_init(Parser *p) {
    if (!check(p, TOK_LBRACE)) return false;
    Token saved = p->tok;
    Lexer saved_lex = p->lex;

    next(p); /* consume '{' */
    skip_nl(p);

    bool ok = false;
    if (check(p, TOK_RBRACE)) {
        ok = true;
    } else if (check(p, TOK_IDENT)) {
        next(p);
        ok = check(p, TOK_COLON);
    }

    p->tok = saved;
    p->lex = saved_lex;
    return ok;
}

/* ---- expression parsing (precedence climbing) ---- */
static Node *parse_primary(Parser *p) {
    int line = p->tok.line, col = p->tok.col;

    /* integer literal */
    if (check(p, TOK_INT_LIT)) {
        Token t = p->tok; next(p);
        Node *n = node_new(ND_INT_LIT, line, col);
        n->int_lit.value = t.int_val;
        return n;
    }

    /* float literal */
    if (check(p, TOK_FLOAT_LIT)) {
        Token t = p->tok; next(p);
        Node *n = node_new(ND_FLOAT_LIT, line, col);
        n->float_lit.value = t.float_val;
        return n;
    }

    /* string literal */
    if (check(p, TOK_STR_LIT)) {
        Token t = p->tok; next(p);
        Node *n = node_new(ND_STR_LIT, line, col);
        n->str_lit.value = t.str_val.data;
        n->str_lit.len = t.str_val.len;
        return n;
    }

    /* null */
    if (check(p, TOK_NULL_KW)) {
        next(p);
        return node_new(ND_NULL_LIT, line, col);
    }

    /* identifier or struct init sugar: T { ... } */
    if (check(p, TOK_IDENT)) {
        Token t = p->tok;
        next(p);

        if (check(p, TOK_LBRACE) && looks_like_struct_init(p)) {
            /* intent lowering: allow `Point { x: 1 }` in addition to `✨ Point { ... }` */
            EsType *ty = (EsType *)calloc(1, sizeof(EsType));
            ty->kind = TY_STRUCT;
            ty->strct.name = tok_name(&t);
            return parse_struct_init_literal(p, ty, line, col);
        }

        Node *n = node_new(ND_IDENT, line, col);
        n->ident.name = tok_name(&t);
        return n;
    }

    /* grouped expression */
    if (match(p, TOK_LPAREN)) {
        Node *e = parse_expr(p);
        expect(p, TOK_RPAREN);
        return e;
    }

    /* sizeof */
    if (check(p, TOK_SZ)) {
        next(p);
        Node *n = node_new(ND_SIZEOF, line, col);
        n->size_of.target = parse_type(p);
        return n;
    }

    /* nw T  =>  malloc(sz T) as *T
       nw T { f: v, ... }  =>  struct init literal */
    if (check(p, TOK_NW)) {
        next(p);
        EsType *ty = parse_type(p);
        /* struct init literal: nw T { f: v, ... } */
        if (check(p, TOK_LBRACE)) {
            return parse_struct_init_literal(p, ty, line, col);
        }
        /* plain nw T: malloc(sz T) as *T */
        Node *callee = node_new(ND_IDENT, line, col);
        callee->ident.name = es_strdup("malloc");
        Node *arg = node_new(ND_SIZEOF, line, col);
        arg->size_of.target = ty;
        Node *call = node_new(ND_CALL, line, col);
        call->call.callee = callee;
        call->call.args = (Node **)malloc(sizeof(Node *));
        call->call.args[0] = arg;
        call->call.arg_count = 1;
        /* cast to *T */
        Node *cast = node_new(ND_CAST, line, col);
        cast->cast.expr = call;
        cast->cast.target = type_ptr(ty);
        return cast;
    }

    perror_at(p, "expected expression");
    return NULL;
}

static Node *parse_postfix(Parser *p, Node *left) {
    for (;;) {
        int line = p->tok.line, col = p->tok.col;

        /* function call */
        if (check(p, TOK_LPAREN)) {
            next(p);
            struct { Node **items; int count; int cap; } args = {0};
            if (!check(p, TOK_RPAREN)) {
                for (;;) {
                    skip_nl(p);
                    da_push(args, parse_expr(p));
                    skip_nl(p);
                    if (!match(p, TOK_COMMA)) break;
                }
            }
            expect(p, TOK_RPAREN);
            Node *n = node_new(ND_CALL, line, col);
            n->call.callee = left;
            n->call.args = args.items;
            n->call.arg_count = args.count;
            left = n;
            continue;
        }

        /* field access / UFCS */
        if (check(p, TOK_DOT)) {
            next(p);
            Token name = expect(p, TOK_IDENT);
            Node *n = node_new(ND_FIELD, line, col);
            n->field.object = left;
            n->field.field = tok_name(&name);
            left = n;
            continue;
        }

        /* index */
        if (check(p, TOK_LBRACKET)) {
            next(p);
            Node *idx = parse_expr(p);
            expect(p, TOK_RBRACKET);
            Node *n = node_new(ND_INDEX, line, col);
            n->idx.object = left;
            n->idx.index = idx;
            left = n;
            continue;
        }

        break;
    }
    return left;
}

static Node *parse_unary(Parser *p) {
    int line = p->tok.line, col = p->tok.col;

    if (check(p, TOK_AMP) || check(p, TOK_STAR) ||
        check(p, TOK_BANG) || check(p, TOK_MINUS)) {
        TokenKind op = p->tok.kind;
        next(p);
        Node *n = node_new(ND_UNARY, line, col);
        n->unary.op = op;
        n->unary.operand = parse_unary(p);
        return n;
    }

    /* ⚡ / ct — compile-time eval as expression prefix */
    if (check(p, TOK_CT)) {
        next(p);
        Node *n = node_new(ND_COMPTIME, line, col);
        n->comptime.expr = parse_unary(p);
        return n;
    }

    return parse_postfix(p, parse_primary(p));
}

/* 'as' cast: binds between unary and binary ops
 * so &buf as *v  =>  (&buf) as *v */
static Node *parse_cast(Parser *p) {
    Node *expr = parse_unary(p);
    while (check(p, TOK_AS)) {
        int line = p->tok.line, col = p->tok.col;
        next(p);
        Node *n = node_new(ND_CAST, line, col);
        n->cast.expr = expr;
        n->cast.target = parse_type(p);
        expr = n;
    }
    return expr;
}

/* binary operator precedence helpers */
static int binop_prec(TokenKind k) {
    switch (k) {
    case TOK_RANGE: case TOK_RANGE_INC: return 1;
    case TOK_LOR:     return 2;
    case TOK_LAND:    return 3;
    case TOK_PIPE:    return 4;
    case TOK_CARET:   return 5;
    case TOK_AMP:     return 6;
    case TOK_EQ: case TOK_NEQ: return 7;
    case TOK_LT: case TOK_GT: case TOK_LEQ: case TOK_GEQ: return 8;
    case TOK_SHL: case TOK_SHR: return 9;
    case TOK_PLUS: case TOK_MINUS: return 10;
    case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 11;
    default: return -1;
    }
}

static Node *parse_binop(Parser *p, int min_prec) {
    Node *left = parse_cast(p);
    for (;;) {
        int prec = binop_prec(p->tok.kind);
        if (prec < min_prec) break;
        TokenKind op = p->tok.kind;
        int line = p->tok.line, col = p->tok.col;
        next(p);
        int next_min = (op == TOK_RANGE || op == TOK_RANGE_INC) ? prec : (prec + 1);
        Node *right = parse_binop(p, next_min);
        Node *n = node_new(ND_BINARY, line, col);
        n->binary.op = op;
        n->binary.left = left;
        n->binary.right = right;
        left = n;
    }
    return left;
}

static Node *parse_expr(Parser *p) {
    Node *expr = parse_binop(p, 1);
    /* ternary: expr ? then : else */
    if (check(p, TOK_QUESTION)) {
        int line = p->tok.line, col = p->tok.col;
        next(p);
        Node *then_expr = parse_expr(p);
        expect(p, TOK_COLON);
        Node *else_expr = parse_expr(p);
        Node *n = node_new(ND_TERNARY, line, col);
        n->ternary.cond = expr;
        n->ternary.then_expr = then_expr;
        n->ternary.else_expr = else_expr;
        expr = n;
    }
    while (check(p, TOK_PIPE_OP)) {
        next(p); /* consume |> */
        /* parse the RHS — must be ident or call */
        Node *rhs = parse_binop(p, 1);
        if (rhs->kind == ND_CALL) {
            /* insert expr as first argument */
            int new_argc = rhs->call.arg_count + 1;
            Node **new_args = malloc(new_argc * sizeof(Node *));
            new_args[0] = expr;
            for (int i = 0; i < rhs->call.arg_count; i++)
                new_args[i + 1] = rhs->call.args[i];
            rhs->call.args = new_args;
            rhs->call.arg_count = new_argc;
            expr = rhs;
        } else if (rhs->kind == ND_IDENT) {
            /* wrap as call: f(expr) */
            Node *call = node_new(ND_CALL, rhs->line, rhs->col);
            call->call.callee = rhs;
            call->call.args = malloc(sizeof(Node *));
            call->call.args[0] = expr;
            call->call.arg_count = 1;
            expr = call;
        } else {
            perror_at(p, "pipe RHS must be function or call");
        }
    }
    return expr;
}

/* ---- statement parsing ---- */
static Node *parse_stmt(Parser *p);

static Node *parse_block(Parser *p) {
    int line = p->tok.line, col = p->tok.col;
    expect(p, TOK_LBRACE);
    skip_nl(p);

    struct { Node **items; int count; int cap; } stmts = {0};
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        da_push(stmts, parse_stmt(p));
        skip_nl(p);
    }

    expect(p, TOK_RBRACE);
    Node *n = node_new(ND_BLOCK, line, col);
    n->block.stmts = stmts.items;
    n->block.count = stmts.count;
    return n;
}

static Node *parse_stmt(Parser *p) {
    int line = p->tok.line, col = p->tok.col;

    /* ret expr? */
    if (check(p, TOK_RET)) {
        next(p);
        Node *n = node_new(ND_RET, line, col);
        if (!check(p, TOK_NEWLINE) && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            n->ret.value = parse_expr(p);
        }
        expect_nl_or_end(p);
        return n;
    }

    /* if expr block (el block)? */
    if (check(p, TOK_IF)) {
        next(p);
        Node *n = node_new(ND_IF, line, col);
        n->if_stmt.cond = parse_expr(p);
        n->if_stmt.then_blk = parse_block(p);
        skip_nl(p);
        if (match(p, TOK_EL)) {
            if (check(p, TOK_IF)) {
                /* el if ... → else-if chain: wrap nested if in a block */
                Node *elif = parse_stmt(p);
                Node *blk = node_new(ND_BLOCK, elif->line, elif->col);
                blk->block.stmts = (Node **)malloc(sizeof(Node *));
                blk->block.stmts[0] = elif;
                blk->block.count = 1;
                n->if_stmt.else_blk = blk;
            } else {
                n->if_stmt.else_blk = parse_block(p);
            }
        }
        skip_nl(p);
        return n;
    }

    /* del expr  =>  free(expr) */
    if (check(p, TOK_DEL)) {
        next(p);
        Node *expr = parse_expr(p);
        Node *callee = node_new(ND_IDENT, line, col);
        callee->ident.name = es_strdup("free");
        Node *call = node_new(ND_CALL, line, col);
        call->call.callee = callee;
        call->call.args = (Node **)malloc(sizeof(Node *));
        call->call.args[0] = expr;
        call->call.arg_count = 1;
        Node *n = node_new(ND_EXPR_STMT, line, col);
        n->expr_stmt.expr = call;
        expect_nl_or_end(p);
        return n;
    }

    /* brk */
    if (check(p, TOK_BRK)) {
        next(p);
        expect_nl_or_end(p);
        return node_new(ND_BREAK, line, col);
    }

    /* cont */
    if (check(p, TOK_CONT)) {
        next(p);
        expect_nl_or_end(p);
        return node_new(ND_CONTINUE, line, col);
    }

    /* 🔩 / asm — inline assembly
       asm("template")
       asm("template" : "=c"(out),... : "c"(in),... : "clobber",...)   */
    if (check(p, TOK_ASM)) {
        next(p);
        expect(p, TOK_LPAREN);
        Token tmpl = expect(p, TOK_STR_LIT);
        Node *n = node_new(ND_INLINE_ASM, line, col);
        n->iasm.templ = es_strndup(tmpl.str_val.data, tmpl.str_val.len);
        n->iasm.is_volatile = true;
        n->iasm.has_side_effects = true;

        /* optional : outputs : inputs : clobbers */
        if (match(p, TOK_COLON)) {
            /* parse output operands: "constraint"(expr), ... */
            struct { char **items; int count; int cap; } oc = {0};
            struct { Node **items; int count; int cap; } oe = {0};
            while (check(p, TOK_STR_LIT)) {
                Token ct = p->tok; next(p);
                da_push(oc, es_strndup(ct.str_val.data, ct.str_val.len));
                expect(p, TOK_LPAREN);
                da_push(oe, parse_expr(p));
                expect(p, TOK_RPAREN);
                if (!match(p, TOK_COMMA)) break;
            }
            n->iasm.out_constraints = oc.items;
            n->iasm.out_exprs = oe.items;
            n->iasm.out_count = oc.count;

            if (match(p, TOK_COLON)) {
                /* parse input operands */
                struct { char **items; int count; int cap; } ic = {0};
                struct { Node **items; int count; int cap; } ie = {0};
                while (check(p, TOK_STR_LIT)) {
                    Token ct = p->tok; next(p);
                    da_push(ic, es_strndup(ct.str_val.data, ct.str_val.len));
                    expect(p, TOK_LPAREN);
                    da_push(ie, parse_expr(p));
                    expect(p, TOK_RPAREN);
                    if (!match(p, TOK_COMMA)) break;
                }
                n->iasm.in_constraints = ic.items;
                n->iasm.in_exprs = ie.items;
                n->iasm.in_count = ic.count;

                if (match(p, TOK_COLON)) {
                    /* parse clobbers */
                    struct { char **items; int count; int cap; } cl = {0};
                    while (check(p, TOK_STR_LIT)) {
                        Token ct = p->tok; next(p);
                        da_push(cl, es_strndup(ct.str_val.data, ct.str_val.len));
                        if (!match(p, TOK_COMMA)) break;
                    }
                    n->iasm.clobbers = cl.items;
                    n->iasm.clobber_count = cl.count;
                }
            }
        }
        expect(p, TOK_RPAREN);
        expect_nl_or_end(p);
        return n;
    }

    /* ⚡ / ct — compile-time evaluation: ct expr */
    if (check(p, TOK_CT)) {
        next(p);
        Node *n = node_new(ND_COMPTIME, line, col);
        n->comptime.expr = parse_expr(p);
        /* don't consume newline — caller wraps in expr_stmt if needed */
        return n;
    }

    /* wh expr block */
    if (check(p, TOK_WH)) {
        next(p);
        Node *n = node_new(ND_WHILE, line, col);
        n->while_stmt.cond = parse_expr(p);
        n->while_stmt.body = parse_block(p);
        skip_nl(p);
        return n;
    }

    /* fo i := start..end { body } — for loop */
    if (check(p, TOK_FOR)) {
        next(p);
        Token iter = expect(p, TOK_IDENT);
        expect(p, TOK_DECL_ASSIGN);
        Node *range_expr = parse_expr(p);
        int inclusive = 0;
        Node *start_expr = NULL;
        Node *end_expr = NULL;
        if (range_expr->kind == ND_BINARY &&
            (range_expr->binary.op == TOK_RANGE || range_expr->binary.op == TOK_RANGE_INC)) {
            inclusive = (range_expr->binary.op == TOK_RANGE_INC);
            start_expr = range_expr->binary.left;
            end_expr = range_expr->binary.right;
        } else {
            perror_at(p, "expected range in for loop");
        }
        Node *body = parse_block(p);
        Node *n = node_new(ND_FOR, line, col);
        /* init: i := start */
        n->for_stmt.init = node_new(ND_DECL_STMT, line, col);
        n->for_stmt.init->decl.name = tok_name(&iter);
        n->for_stmt.init->decl.decl_type = NULL;
        n->for_stmt.init->decl.init = start_expr;
        /* cond: i < end (exclusive) or i <= end (inclusive) */
        Node *iref = node_new(ND_IDENT, line, col);
        iref->ident.name = tok_name(&iter);
        n->for_stmt.cond = node_new(ND_BINARY, line, col);
        n->for_stmt.cond->binary.op = inclusive ? TOK_LEQ : TOK_LT;
        n->for_stmt.cond->binary.left = iref;
        n->for_stmt.cond->binary.right = end_expr;
        /* incr: i = i + 1 */
        Node *iref2 = node_new(ND_IDENT, line, col);
        iref2->ident.name = tok_name(&iter);
        Node *one = node_new(ND_INT_LIT, line, col);
        one->int_lit.value = 1;
        Node *add = node_new(ND_BINARY, line, col);
        add->binary.op = TOK_PLUS;
        add->binary.left = iref2;
        add->binary.right = one;
        Node *iref3 = node_new(ND_IDENT, line, col);
        iref3->ident.name = tok_name(&iter);
        n->for_stmt.incr = node_new(ND_ASSIGN, line, col);
        n->for_stmt.incr->assign.target = iref3;
        n->for_stmt.incr->assign.value = add;
        n->for_stmt.body = body;
        skip_nl(p);
        return n;
    }

    /* 🎯 expr { val { body } ... _ { body } } — match */
    if (check(p, TOK_MATCH)) {
        next(p);
        Node *n = node_new(ND_MATCH, line, col);
        n->match_stmt.expr = parse_expr(p);
        expect(p, TOK_LBRACE);
        skip_nl(p);
        struct { Node **items; int count; int cap; } vals = {0};
        struct { Node **items; int count; int cap; } bods = {0};
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            /* check for _ (default) */
            if (check(p, TOK_IDENT) && p->tok.len == 1 && p->tok.start[0] == '_') {
                next(p);
                da_push(vals, (Node *)NULL);
            } else {
                da_push(vals, parse_expr(p));
            }
            da_push(bods, parse_block(p));
            skip_nl(p);
        }
        expect(p, TOK_RBRACE);
        n->match_stmt.case_vals = vals.items;
        n->match_stmt.case_bodies = bods.items;
        n->match_stmt.case_count = vals.count;
        skip_nl(p);
        return n;
    }

    /* 🔜 stmt — defer */
    if (check(p, TOK_DEFER)) {
        next(p);
        Node *n = node_new(ND_DEFER, line, col);
        n->defer_stmt.body = parse_stmt(p);
        return n;
    }

    /* var ID := expr | var ID = expr | var ID : type = expr */
    if (check(p, TOK_VAR)) {
        next(p);
        /* eat optional 'mut' after let/var */
        if (check(p, TOK_IDENT) && p->tok.len == 3 && memcmp(p->tok.start, "mut", 3) == 0) {
            next(p); /* skip 'mut' */
        }
        Token vname = p->tok;
        expect(p, TOK_IDENT);
        if (match(p, TOK_DECL_ASSIGN) || match(p, TOK_ASSIGN)) {
            Node *n = node_new(ND_DECL_STMT, line, col);
            n->decl.name = tok_name(&vname);
            n->decl.decl_type = NULL;
            n->decl.init = parse_expr(p);
            expect_nl_or_end(p);
            return n;
        }
        if (match(p, TOK_COLON)) {
            EsType *ty = parse_type(p);
            Node *n = node_new(ND_DECL_STMT, line, col);
            n->decl.name = tok_name(&vname);
            n->decl.decl_type = ty;
            n->decl.init = match(p, TOK_ASSIGN) ? parse_expr(p) : NULL;
            expect_nl_or_end(p);
            return n;
        }
        perror_at(p, "expected ':=' or ':' after 'var'");
    }

    /* print/check as statement keywords (allow without parens):
       print expr   →  print(expr)
       check expr   →  check(expr)  */
    if (check(p, TOK_IDENT) &&
        (tok_is(p, "print") || tok_is(p, "check"))) {
        Token saved = p->tok;
        Lexer saved_lex = p->lex;
        next(p);
        /* only rewrite if NOT followed by := : ( — those are normal decl/call */
        if (!check(p, TOK_DECL_ASSIGN) && !check(p, TOK_COLON) &&
            !check(p, TOK_NEWLINE) && !check(p, TOK_SEMI) &&
            !check(p, TOK_EOF) && !check(p, TOK_RBRACE)) {
            /* synthesize: name(expr) */
            Node *callee = node_new(ND_IDENT, line, col);
            callee->ident.name = tok_name(&saved);
            Node *arg = parse_expr(p);
            Node *call = node_new(ND_CALL, line, col);
            call->call.callee = callee;
            call->call.args = (Node **)malloc(sizeof(Node *));
            call->call.args[0] = arg;
            call->call.arg_count = 1;
            Node *stmt = node_new(ND_EXPR_STMT, line, col);
            stmt->expr_stmt.expr = call;
            expect_nl_or_end(p);
            return stmt;
        }
        /* rewind — it's a normal declaration or bare statement */
        p->tok = saved;
        p->lex = saved_lex;
    }

    /* declaration:  ID : type = expr  OR  ID := expr */
    if (check(p, TOK_IDENT)) {
        /* peek ahead for := or : type = */
        Token saved = p->tok;
        Lexer saved_lex = p->lex;

        next(p); /* consume ID */

        /* ID := expr */
        if (check(p, TOK_DECL_ASSIGN)) {
            next(p);
            Node *n = node_new(ND_DECL_STMT, line, col);
            n->decl.name = tok_name(&saved);
            n->decl.decl_type = NULL;
            n->decl.init = parse_expr(p);
            expect_nl_or_end(p);
            return n;
        }

        /* ID : type (= expr)? */
        if (check(p, TOK_COLON)) {
            next(p);
            EsType *ty = parse_type(p);

            if (match(p, TOK_ASSIGN)) {
                Node *n = node_new(ND_DECL_STMT, line, col);
                n->decl.name = tok_name(&saved);
                n->decl.decl_type = ty;
                n->decl.init = parse_expr(p);
                expect_nl_or_end(p);
                return n;
            }

            /* declaration without initializer */
            Node *n = node_new(ND_DECL_STMT, line, col);
            n->decl.name = tok_name(&saved);
            n->decl.decl_type = ty;
            n->decl.init = NULL;
            expect_nl_or_end(p);
            return n;
        }

        /* not a declaration -- rewind to re-parse as expression */
        p->tok = saved;
        p->lex = saved_lex;
    }

    /* expression or assignment (handles p.x = val, *p = val, etc.) */
    {
        Node *expr = parse_expr(p);

        if (check(p, TOK_ASSIGN)) {
            next(p);
            Node *n = node_new(ND_ASSIGN, line, col);
            n->assign.target = expr;
            n->assign.value = parse_expr(p);
            expect_nl_or_end(p);
            return n;
        }

        /* compound assignment: += -= *= /= %= => desugar to x = x op val */
        TokenKind cop = p->tok.kind;
        if (cop == TOK_PLUS_EQ || cop == TOK_MINUS_EQ || cop == TOK_STAR_EQ ||
            cop == TOK_SLASH_EQ || cop == TOK_PERCENT_EQ) {
            next(p);
            TokenKind binop;
            switch (cop) {
            case TOK_PLUS_EQ:    binop = TOK_PLUS; break;
            case TOK_MINUS_EQ:   binop = TOK_MINUS; break;
            case TOK_STAR_EQ:    binop = TOK_STAR; break;
            case TOK_SLASH_EQ:   binop = TOK_SLASH; break;
            case TOK_PERCENT_EQ: binop = TOK_PERCENT; break;
            default: binop = TOK_PLUS; break;
            }
            Node *rhs = parse_expr(p);
            Node *bin = node_new(ND_BINARY, line, col);
            bin->binary.op = binop;
            bin->binary.left = expr;
            bin->binary.right = rhs;
            Node *n = node_new(ND_ASSIGN, line, col);
            n->assign.target = expr;
            n->assign.value = bin;
            expect_nl_or_end(p);
            return n;
        }

        /* expression statement */
        Node *n = node_new(ND_EXPR_STMT, line, col);
        n->expr_stmt.expr = expr;
        expect_nl_or_end(p);
        return n;
    }
}

/* ---- top-level declarations ---- */
static Node *parse_ext_decl(Parser *p) {
    int line = p->tok.line, col = p->tok.col;
    expect(p, TOK_EXT);
    Token name = expect(p, TOK_IDENT);
    expect(p, TOK_LPAREN);
    int pc = 0; bool va = false;
    Param *params = parse_params(p, &pc, &va, true);
    expect(p, TOK_RPAREN);

    EsType *ret = NULL;
    if (match(p, TOK_ARROW)) {
        ret = parse_type(p);
    } else {
        ret = type_basic(TY_VOID);
    }

    expect_nl_or_end(p);

    Node *n = node_new(ND_EXT_DECL, line, col);
    n->ext.name = tok_name(&name);
    n->ext.params = params;
    n->ext.param_count = pc;
    n->ext.ret_type = ret;
    n->ext.is_vararg = va;
    return n;
}

/* Recursively check if a block contains any 'return expr' (non-void return). */
static bool block_has_return_value(Node *n) {
    if (!n) return false;
    if (n->kind == ND_RET) return n->ret.value != NULL;
    if (n->kind == ND_BLOCK) {
        for (int i = 0; i < n->block.count; i++)
            if (block_has_return_value(n->block.stmts[i])) return true;
    }
    if (n->kind == ND_IF) {
        if (block_has_return_value(n->if_stmt.then_blk)) return true;
        if (block_has_return_value(n->if_stmt.else_blk)) return true;
    }
    if (n->kind == ND_WHILE) return block_has_return_value(n->while_stmt.body);
    if (n->kind == ND_FOR) return block_has_return_value(n->for_stmt.body);
    if (n->kind == ND_MATCH) {
        for (int i = 0; i < n->match_stmt.case_count; i++)
            if (block_has_return_value(n->match_stmt.case_bodies[i])) return true;
    }
    return false;
}

static Node *parse_fn_decl(Parser *p, bool has_kw) {
    int line = p->tok.line, col = p->tok.col;
    if (has_kw) expect(p, TOK_FN);
    Token name = expect(p, TOK_IDENT);

    /* main { } shorthand — no parens, no return type */
    bool is_main = (strncmp(name.start, "main", name.len) == 0 && name.len == 4);
    int pc = 0; bool va = false;
    Param *params = NULL;

    if (check(p, TOK_LPAREN)) {
        expect(p, TOK_LPAREN);
        params = parse_params(p, &pc, &va, false);
        expect(p, TOK_RPAREN);
    }

    EsType *ret = NULL;
    if (match(p, TOK_ARROW)) {
        ret = parse_type(p);
    } else {
        ret = is_main ? type_basic(TY_I32) : type_basic(TY_VOID);
    }

    Node *body;
    if (match(p, TOK_ASSIGN)) {
        /* one-liner: fn name(args) = expr */
        Node *val = parse_expr(p);
        expect_nl_or_end(p);
        /* wrap in block with return */
        Node *ret_node = node_new(ND_RET, line, col);
        ret_node->ret.value = val;
        body = node_new(ND_BLOCK, line, col);
        body->block.stmts = malloc(sizeof(Node *));
        body->block.stmts[0] = ret_node;
        body->block.count = 1;
        /* infer return type from expression if not specified */
        if (ret->kind == TY_VOID && !is_main) ret = type_basic(TY_I32);
    } else {
        body = parse_block(p);
        /* infer i32 return type for multi-line fns that contain 'return expr' */
        if (ret->kind == TY_VOID && !is_main && block_has_return_value(body))
            ret = type_basic(TY_I32);
    }

    /* implicit return: convert last expr stmt to ret for non-void, non-main fns */
    if (ret->kind != TY_VOID && !is_main && body->block.count > 0) {
        Node *last = body->block.stmts[body->block.count - 1];
        if (last->kind == ND_EXPR_STMT) {
            last->kind = ND_RET;
            /* ND_RET.value and ND_EXPR_STMT.expr share the same union slot */
        }
    }

    Node *n = node_new(ND_FN_DECL, line, col);
    n->fn.name = tok_name(&name);
    n->fn.params = params;
    n->fn.param_count = pc;
    n->fn.ret_type = ret;
    n->fn.body = body;
    return n;
}

static Node *parse_st_decl(Parser *p, bool has_kw) {
    int line = p->tok.line, col = p->tok.col;
    if (has_kw) expect(p, TOK_ST);
    Token name = expect(p, TOK_IDENT);
    expect(p, TOK_LBRACE);
    skip_nl(p);

    struct { Param *items; int count; int cap; } fields = {0};
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Token fname = expect(p, TOK_IDENT);
        expect(p, TOK_COLON);
        EsType *ftype = parse_type(p);
        Param f = { .name = tok_name(&fname), .type = ftype };
        da_push(fields, f);
        skip_nl(p);
    }
    expect(p, TOK_RBRACE);

    Node *n = node_new(ND_ST_DECL, line, col);
    n->st.name = tok_name(&name);
    n->st.fields = fields.items;
    n->st.field_count = fields.count;
    return n;
}

static Node *parse_enum_decl(Parser *p) {
    int line = p->tok.line, col = p->tok.col;
    expect(p, TOK_ENUM);
    Token name = expect(p, TOK_IDENT);
    expect(p, TOK_LBRACE);
    skip_nl(p);
    struct { char **items; int count; int cap; } members = {0};
    struct { int *items; int count; int cap; } values = {0};
    int val = 0;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Token mname = expect(p, TOK_IDENT);
        if (match(p, TOK_ASSIGN)) {
            Token num = expect(p, TOK_INT_LIT);
            val = (int)num.int_val;
        }
        da_push(members, tok_name(&mname));
        da_push(values, val);
        val++;
        if (match(p, TOK_COMMA) || match(p, TOK_SEMI)) { /* optional separator */ }
        skip_nl(p);
    }
    expect(p, TOK_RBRACE);
    Node *n = node_new(ND_ENUM_DECL, line, col);
    n->enum_decl.name = tok_name(&name);
    n->enum_decl.members = members.items;
    n->enum_decl.values = values.items;
    n->enum_decl.member_count = members.count;
    return n;
}

static Node *parse_decl(Parser *p) {
    if (check(p, TOK_EXT))  return parse_ext_decl(p);
    if (check(p, TOK_FN))   return parse_fn_decl(p, true);
    if (check(p, TOK_ST))   return parse_st_decl(p, true);
    if (check(p, TOK_ENUM)) return parse_enum_decl(p);

    /* keyword-free: IDENT( → function, IDENT{ → struct */
    if (check(p, TOK_IDENT)) {
        Token saved = p->tok;
        Lexer saved_lex = p->lex;
        next(p);
        bool is_fn = check(p, TOK_LPAREN);
        bool is_st = check(p, TOK_LBRACE);
        p->tok = saved;
        p->lex = saved_lex;
        if (is_fn) return parse_fn_decl(p, false);
        if (is_st) return parse_st_decl(p, false);
    }

    perror_at(p, "expected declaration");
    return NULL;
}

/* ---- public API ---- */
void parser_init(Parser *p, const char *src, const char *file) {
    lexer_init(&p->lex, src, file);
    p->file = file;
    next(p); /* load first token */
}

static Node *load_prelude(const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "lib/%s.es", name);
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(path, sizeof(path), "/home/jurip/Vibes/el-stupido/lib/%s.es", name);
        f = fopen(path, "r");
    }
    if (!f) return NULL;
    fclose(f);
    char *raw = es_read_file(path);
    char *src = preprocess(raw);
    if (src != raw) free(raw);
    Parser sub;
    parser_init(&sub, src, path);
    return parser_parse_prelude(&sub);
}

/* parse prelude file (no auto-load of std to avoid recursion) */
Node *parser_parse_prelude(Parser *p) {
    struct { Node **items; int count; int cap; } decls = {0};
    skip_nl(p);
    while (!check(p, TOK_EOF)) {
        if (check(p, TOK_USE)) {
            /* handle use in prelude files too */
            expect(p, TOK_USE);
            Token name = expect(p, TOK_IDENT);
            expect_nl_or_end(p);
            char *modname = tok_name(&name);
            Node *sub = load_prelude(modname);
            free(modname);
            if (sub) {
                for (int i = 0; i < sub->program.count; i++)
                    da_push(decls, sub->program.decls[i]);
            }
            skip_nl(p);
            continue;
        }
        da_push(decls, parse_decl(p));
        skip_nl(p);
    }
    Node *prog = node_new(ND_PROGRAM, 1, 1);
    prog->program.decls = decls.items;
    prog->program.count = decls.count;
    return prog;
}

Node *parser_parse(Parser *p) {
    struct { Node **items; int count; int cap; } decls = {0};
    struct { Node **items; int count; int cap; } top_stmts = {0};

    /* auto-load std prelude (skipped for --wasm / --no-std) */
    Node *std = parser_no_std ? NULL : load_prelude("std");
    if (std) {
        for (int i = 0; i < std->program.count; i++)
            da_push(decls, std->program.decls[i]);
    }

    skip_nl(p);
    while (!check(p, TOK_EOF)) {
        /* explicit 'use' still works (for non-std modules) */
        if (check(p, TOK_USE)) {
            expect(p, TOK_USE);
            Token name = expect(p, TOK_IDENT);
            expect_nl_or_end(p);
            char *modname = tok_name(&name);
            Node *sub = load_prelude(modname);
            free(modname);
            if (sub) {
                for (int i = 0; i < sub->program.count; i++)
                    da_push(decls, sub->program.decls[i]);
            }
            skip_nl(p);
            continue;
        }
        /* Try as declaration first */
        if (check(p, TOK_EXT) || check(p, TOK_FN) || check(p, TOK_ST) || check(p, TOK_ENUM)) {
            da_push(decls, parse_decl(p));
            skip_nl(p);
            continue;
        }

        /* keyword-free fn/struct: IDENT followed by ( then = / -> / { */
        if (check(p, TOK_IDENT)) {
            Token saved = p->tok;
            Lexer saved_lex = p->lex;
            next(p);
            if (check(p, TOK_LBRACE)) {
                /* IDENT{ → struct decl */
                p->tok = saved; p->lex = saved_lex;
                da_push(decls, parse_decl(p));
                skip_nl(p);
                continue;
            }
            if (check(p, TOK_LPAREN)) {
                /* skip to matching ) then check for = -> { */
                next(p);
                int depth = 1;
                while (depth > 0 && !check(p, TOK_EOF)) {
                    if (check(p, TOK_LPAREN)) depth++;
                    else if (check(p, TOK_RPAREN)) depth--;
                    if (depth > 0) next(p);
                }
                if (check(p, TOK_RPAREN)) next(p);
                bool is_decl = check(p, TOK_ASSIGN) || check(p, TOK_ARROW) || check(p, TOK_LBRACE);
                p->tok = saved; p->lex = saved_lex;
                if (is_decl) {
                    da_push(decls, parse_decl(p));
                    skip_nl(p);
                    continue;
                }
            } else {
                p->tok = saved; p->lex = saved_lex;
            }
        }

        /* Not a declaration — treat as top-level statement */
        da_push(top_stmts, parse_stmt(p));
        skip_nl(p);
    }

    /* If we have top-level statements, wrap in fn main() */
    if (top_stmts.count > 0) {
        Node *body = node_new(ND_BLOCK, 1, 1);
        body->block.stmts = top_stmts.items;
        body->block.count = top_stmts.count;
        Node *main_fn = node_new(ND_FN_DECL, 1, 1);
        main_fn->fn.name = es_strdup("main");
        main_fn->fn.params = NULL;
        main_fn->fn.param_count = 0;
        main_fn->fn.ret_type = type_basic(TY_I32);
        main_fn->fn.body = body;
        da_push(decls, main_fn);
    }
    Node *prog = node_new(ND_PROGRAM, 1, 1);
    prog->program.decls = decls.items;
    prog->program.count = decls.count;
    return prog;
}
