#include "sexpr.h"
#include "parser.h"

/* ---- S-expression tokenizer ---- */
typedef enum { ST_LPAREN, ST_RPAREN, ST_INT, ST_FLOAT, ST_STR, ST_SYM, ST_EOF } STKind;

typedef struct {
    STKind kind;
    const char *start;
    int len;
    int line, col;
    union { int64_t ival; double fval; char *sval; };
} STok;

typedef struct {
    const char *cur;
    const char *file;
    int line, col;
} SLex;

static char speek(SLex *l) { return *l->cur; }
static char sadv(SLex *l) {
    char c = *l->cur++;
    if (c == '\n') { l->line++; l->col = 1; } else l->col++;
    return c;
}

static void sskip(SLex *l) {
    for (;;) {
        while (speek(l) == ' ' || speek(l) == '\t' || speek(l) == '\n' || speek(l) == '\r') sadv(l);
        if (speek(l) == ';') { while (speek(l) && speek(l) != '\n') sadv(l); continue; }
        break;
    }
}

static STok stok(SLex *l) {
    sskip(l);
    STok t = {0}; t.line = l->line; t.col = l->col; t.start = l->cur;
    char c = speek(l);
    if (!c) { t.kind = ST_EOF; return t; }
    if (c == '(') { sadv(l); t.kind = ST_LPAREN; t.len = 1; return t; }
    if (c == ')') { sadv(l); t.kind = ST_RPAREN; t.len = 1; return t; }

    /* string */
    if (c == '"') {
        sadv(l);
        int slen = 0;
        const char *scan = l->cur;
        while (*scan && *scan != '"') { if (*scan == '\\') scan++; scan++; slen++; }
        char *buf = (char *)malloc(slen + 1);
        int i = 0;
        while (speek(l) && speek(l) != '"') {
            if (speek(l) == '\\') {
                sadv(l);
                switch (speek(l)) {
                case 'n': buf[i++] = '\n'; break; case 't': buf[i++] = '\t'; break;
                case '\\': buf[i++] = '\\'; break; case '"': buf[i++] = '"'; break;
                case '0': buf[i++] = '\0'; break; case 'r': buf[i++] = '\r'; break;
                default: buf[i++] = speek(l); break;
                }
                sadv(l);
            } else buf[i++] = sadv(l);
        }
        buf[i] = '\0';
        if (speek(l) == '"') sadv(l);
        t.kind = ST_STR; t.sval = buf; t.len = (int)(l->cur - t.start);
        return t;
    }

    /* number */
    if (isdigit(c) || (c == '-' && isdigit(l->cur[1]))) {
        const char *s = l->cur;
        if (c == '-') sadv(l);
        while (isdigit(speek(l))) sadv(l);
        if (speek(l) == '.' && isdigit(l->cur[1])) {
            sadv(l); while (isdigit(speek(l))) sadv(l);
            t.kind = ST_FLOAT; t.fval = strtod(s, NULL);
        } else {
            t.kind = ST_INT; t.ival = strtoll(s, NULL, 0);
        }
        t.len = (int)(l->cur - t.start);
        return t;
    }

    /* symbol: anything not whitespace/parens/quotes */
    while (speek(l) && speek(l) != ' ' && speek(l) != '\t' && speek(l) != '\n' &&
           speek(l) != '\r' && speek(l) != '(' && speek(l) != ')' && speek(l) != '"')
        sadv(l);
    t.kind = ST_SYM; t.len = (int)(l->cur - t.start);
    return t;
}

/* ---- S-expression tree ---- */
typedef struct SExpr {
    enum { SE_ATOM, SE_LIST } tag;
    int line, col;
    union {
        STok atom;
        struct { struct SExpr **items; int count; int cap; } list;
    };
} SExpr;

typedef struct { SLex lex; STok tok; const char *file; } SP;

static void snext(SP *p) { p->tok = stok(&p->lex); }
static bool sym_eq(STok *t, const char *s) { return t->kind == ST_SYM && t->len == (int)strlen(s) && memcmp(t->start, s, t->len) == 0; }
static char *sym_str(STok *t) { return es_strndup(t->start, t->len); }

static SExpr *parse_sexpr(SP *p) {
    SExpr *e = (SExpr *)calloc(1, sizeof(SExpr));
    e->line = p->tok.line; e->col = p->tok.col;
    if (p->tok.kind == ST_LPAREN) {
        e->tag = SE_LIST;
        snext(p);
        while (p->tok.kind != ST_RPAREN && p->tok.kind != ST_EOF) {
            SExpr *child = parse_sexpr(p);
            if (e->list.count >= e->list.cap) {
                e->list.cap = e->list.cap ? e->list.cap * 2 : 8;
                e->list.items = realloc(e->list.items, e->list.cap * sizeof(SExpr *));
            }
            e->list.items[e->list.count++] = child;
        }
        if (p->tok.kind == ST_RPAREN) snext(p);
        return e;
    }
    e->tag = SE_ATOM;
    e->atom = p->tok;
    snext(p);
    return e;
}

/* ---- S-expr → AST conversion ---- */
#define L(e) ((e)->list.items)
#define LC(e) ((e)->list.count)
#define IS(e, s) ((e)->tag == SE_ATOM && sym_eq(&(e)->atom, s))

static EsType *se_type(SExpr *e);
static Node *se_expr(SExpr *e);
static Node *se_block(SExpr **items, int count, int line, int col);

static EsType *se_type(SExpr *e) {
    if (e->tag != SE_ATOM) es_fatal("expected type");
    char *s = sym_str(&e->atom);
    EsType *t = NULL;
    if (strcmp(s,"i8")==0) t=type_basic(TY_I8);
    else if (strcmp(s,"i16")==0) t=type_basic(TY_I16);
    else if (strcmp(s,"i32")==0) t=type_basic(TY_I32);
    else if (strcmp(s,"i64")==0) t=type_basic(TY_I64);
    else if (strcmp(s,"u8")==0) t=type_basic(TY_U8);
    else if (strcmp(s,"u16")==0) t=type_basic(TY_U16);
    else if (strcmp(s,"u32")==0) t=type_basic(TY_U32);
    else if (strcmp(s,"u64")==0) t=type_basic(TY_U64);
    else if (strcmp(s,"f32")==0) t=type_basic(TY_F32);
    else if (strcmp(s,"f64")==0) t=type_basic(TY_F64);
    else if (strcmp(s,"v")==0) t=type_basic(TY_VOID);
    else if (s[0]=='*') {
        /* pointer: *i32, *Vec2, *v, etc */
        SExpr inner = *e;
        STok itok = e->atom; itok.start++; itok.len--;
        inner.atom = itok;
        t = type_ptr(se_type(&inner));
    } else {
        /* struct name */
        t = (EsType *)calloc(1, sizeof(EsType));
        t->kind = TY_STRUCT; t->strct.name = s;
        return t;
    }
    free(s);
    return t;
}

/* convert a list form to a statement node */
static Node *se_stmt(SExpr *e);

static Node *se_expr(SExpr *e) {
    int ln = e->line, co = e->col;

    /* atoms */
    if (e->tag == SE_ATOM) {
        if (e->atom.kind == ST_INT) {
            Node *n = node_new(ND_INT_LIT, ln, co);
            n->int_lit.value = e->atom.ival;
            return n;
        }
        if (e->atom.kind == ST_FLOAT) {
            Node *n = node_new(ND_FLOAT_LIT, ln, co);
            n->float_lit.value = e->atom.fval;
            return n;
        }
        if (e->atom.kind == ST_STR) {
            Node *n = node_new(ND_STR_LIT, ln, co);
            n->str_lit.value = e->atom.sval;
            n->str_lit.len = strlen(e->atom.sval);
            return n;
        }
        /* symbol */
        char *s = sym_str(&e->atom);
        if (strcmp(s, "null") == 0) { free(s); return node_new(ND_NULL_LIT, ln, co); }
        Node *n = node_new(ND_IDENT, ln, co);
        n->ident.name = s;
        return n;
    }

    /* lists */
    if (e->tag != SE_LIST || LC(e) == 0) es_fatal("empty list at %d:%d", ln, co);
    SExpr *head = L(e)[0];
    if (head->tag != SE_ATOM) {
        /* first element is a list: treat as call? error for now */
        es_fatal("unexpected nested list at %d:%d", ln, co);
    }

    char *op = sym_str(&head->atom);

    /* ternary: (? cond then else) */
    if (strcmp(op, "?") == 0 && LC(e) == 4) {
        Node *n = node_new(ND_TERNARY, ln, co);
        n->ternary.cond = se_expr(L(e)[1]);
        n->ternary.then_expr = se_expr(L(e)[2]);
        n->ternary.else_expr = se_expr(L(e)[3]);
        free(op); return n;
    }

    /* binary ops */
    if (LC(e) == 3) {
        TokenKind binop = -1;
        if (strcmp(op,"+")==0) binop=TOK_PLUS;
        else if (strcmp(op,"-")==0) binop=TOK_MINUS;
        else if (strcmp(op,"*")==0) binop=TOK_STAR;
        else if (strcmp(op,"/")==0) binop=TOK_SLASH;
        else if (strcmp(op,"%")==0) binop=TOK_PERCENT;
        else if (strcmp(op,"<")==0) binop=TOK_LT;
        else if (strcmp(op,">")==0) binop=TOK_GT;
        else if (strcmp(op,"<=")==0) binop=TOK_LEQ;
        else if (strcmp(op,">=")==0) binop=TOK_GEQ;
        else if (strcmp(op,"==")==0) binop=TOK_EQ;
        else if (strcmp(op,"!=")==0) binop=TOK_NEQ;
        else if (strcmp(op,"&&")==0) binop=TOK_LAND;
        else if (strcmp(op,"||")==0) binop=TOK_LOR;
        else if (strcmp(op,"&")==0 && LC(e)==3) binop=TOK_AMP;
        else if (strcmp(op,"|")==0 && LC(e)==3) binop=TOK_PIPE;
        else if (strcmp(op,"^")==0 && LC(e)==3) binop=TOK_CARET;
        else if (strcmp(op,"<<")==0) binop=TOK_SHL;
        else if (strcmp(op,">>")==0) binop=TOK_SHR;
        if (binop != (TokenKind)-1) {
            Node *n = node_new(ND_BINARY, ln, co);
            n->binary.op = binop;
            n->binary.left = se_expr(L(e)[1]);
            n->binary.right = se_expr(L(e)[2]);
            free(op); return n;
        }
    }

    /* unary ops (1 arg) */
    if (LC(e) == 2) {
        if (strcmp(op,"&")==0) {
            Node *n = node_new(ND_UNARY, ln, co);
            n->unary.op = TOK_AMP; n->unary.operand = se_expr(L(e)[1]);
            free(op); return n;
        }
        if (strcmp(op,"*")==0) {
            Node *n = node_new(ND_UNARY, ln, co);
            n->unary.op = TOK_STAR; n->unary.operand = se_expr(L(e)[1]);
            free(op); return n;
        }
        if (strcmp(op,"-")==0) {
            Node *n = node_new(ND_UNARY, ln, co);
            n->unary.op = TOK_MINUS; n->unary.operand = se_expr(L(e)[1]);
            free(op); return n;
        }
        if (strcmp(op,"!")==0) {
            Node *n = node_new(ND_UNARY, ln, co);
            n->unary.op = TOK_BANG; n->unary.operand = se_expr(L(e)[1]);
            free(op); return n;
        }
        if (strcmp(op,"~")==0) {
            Node *n = node_new(ND_UNARY, ln, co);
            n->unary.op = TOK_TILDE; n->unary.operand = se_expr(L(e)[1]);
            free(op); return n;
        }
    }

    /* field access: (. obj field) */
    if (strcmp(op, ".") == 0 && LC(e) == 3) {
        Node *n = node_new(ND_FIELD, ln, co);
        n->field.object = se_expr(L(e)[1]);
        n->field.field = sym_str(&L(e)[2]->atom);
        free(op); return n;
    }

    /* index: ([] obj idx) */
    if (strcmp(op, "[]") == 0 && LC(e) == 3) {
        Node *n = node_new(ND_INDEX, ln, co);
        n->idx.object = se_expr(L(e)[1]);
        n->idx.index = se_expr(L(e)[2]);
        free(op); return n;
    }

    /* cast: (as expr type) */
    if (strcmp(op, "as") == 0 && LC(e) == 3) {
        Node *n = node_new(ND_CAST, ln, co);
        n->cast.expr = se_expr(L(e)[1]);
        n->cast.target = se_type(L(e)[2]);
        free(op); return n;
    }

    /* sizeof: (sz type) */
    if (strcmp(op, "sz") == 0 && LC(e) == 2) {
        Node *n = node_new(ND_SIZEOF, ln, co);
        n->size_of.target = se_type(L(e)[1]);
        free(op); return n;
    }

    /* nw: (nw type) → malloc(sz type) as *type */
    if (strcmp(op, "nw") == 0 && LC(e) == 2) {
        EsType *ty = se_type(L(e)[1]);
        Node *callee = node_new(ND_IDENT, ln, co);
        callee->ident.name = es_strdup("malloc");
        Node *arg = node_new(ND_SIZEOF, ln, co);
        arg->size_of.target = ty;
        Node *call = node_new(ND_CALL, ln, co);
        call->call.callee = callee;
        call->call.args = (Node **)malloc(sizeof(Node *));
        call->call.args[0] = arg;
        call->call.arg_count = 1;
        Node *cast = node_new(ND_CAST, ln, co);
        cast->cast.expr = call;
        cast->cast.target = type_ptr(ty);
        free(op); return cast;
    }

    /* function call: (name args...) */
    {
        Node *callee = node_new(ND_IDENT, ln, co);
        callee->ident.name = op;
        int argc = LC(e) - 1;
        Node **args = NULL;
        if (argc > 0) {
            args = (Node **)malloc(argc * sizeof(Node *));
            for (int i = 0; i < argc; i++) args[i] = se_expr(L(e)[i + 1]);
        }
        Node *n = node_new(ND_CALL, ln, co);
        n->call.callee = callee;
        n->call.args = args;
        n->call.arg_count = argc;
        return n;
    }
}

/* convert form to statement */
static Node *se_stmt(SExpr *e) {
    int ln = e->line, co = e->col;

    /* atoms and non-special lists → expression statement */
    if (e->tag == SE_ATOM) {
        Node *n = node_new(ND_EXPR_STMT, ln, co);
        n->expr_stmt.expr = se_expr(e);
        return n;
    }

    if (LC(e) == 0) es_fatal("empty form at %d:%d", ln, co);
    SExpr *head = L(e)[0];
    if (head->tag != SE_ATOM) goto expr_stmt;

    char *op = sym_str(&head->atom);

    /* (= name val) → decl */
    if (strcmp(op, "=") == 0 && LC(e) == 3) {
        Node *n = node_new(ND_DECL_STMT, ln, co);
        n->decl.name = sym_str(&L(e)[1]->atom);
        n->decl.decl_type = NULL;
        n->decl.init = se_expr(L(e)[2]);
        free(op); return n;
    }

    /* (: name type val) → typed decl */
    if (strcmp(op, ":") == 0 && (LC(e) == 4 || LC(e) == 3)) {
        Node *n = node_new(ND_DECL_STMT, ln, co);
        n->decl.name = sym_str(&L(e)[1]->atom);
        n->decl.decl_type = se_type(L(e)[2]);
        n->decl.init = LC(e) == 4 ? se_expr(L(e)[3]) : NULL;
        free(op); return n;
    }

    /* (! target val) → assign */
    if (strcmp(op, "!") == 0 && LC(e) == 3) {
        Node *n = node_new(ND_ASSIGN, ln, co);
        n->assign.target = se_expr(L(e)[1]);
        n->assign.value = se_expr(L(e)[2]);
        free(op); return n;
    }

    /* compound assign: (+= target val) etc */
    if ((strcmp(op,"+=")==0||strcmp(op,"-=")==0||strcmp(op,"*=")==0||
         strcmp(op,"/=")==0||strcmp(op,"%=")==0) && LC(e) == 3) {
        TokenKind binop = TOK_PLUS;
        if (op[0]=='-') binop=TOK_MINUS;
        else if (op[0]=='*') binop=TOK_STAR;
        else if (op[0]=='/') binop=TOK_SLASH;
        else if (op[0]=='%') binop=TOK_PERCENT;
        Node *target = se_expr(L(e)[1]);
        Node *bin = node_new(ND_BINARY, ln, co);
        bin->binary.op = binop;
        bin->binary.left = target;
        bin->binary.right = se_expr(L(e)[2]);
        Node *n = node_new(ND_ASSIGN, ln, co);
        n->assign.target = target;
        n->assign.value = bin;
        free(op); return n;
    }

    /* (^ val) → return */
    if (strcmp(op, "^") == 0) {
        Node *n = node_new(ND_RET, ln, co);
        n->ret.value = LC(e) > 1 ? se_expr(L(e)[1]) : NULL;
        free(op); return n;
    }

    /* (brk) (cont) */
    if (strcmp(op, "brk") == 0) { free(op); return node_new(ND_BREAK, ln, co); }
    if (strcmp(op, "cont") == 0) { free(op); return node_new(ND_CONTINUE, ln, co); }

    /* (if cond then... [else...]) */
    if (strcmp(op, "if") == 0 && LC(e) >= 3) {
        Node *n = node_new(ND_IF, ln, co);
        n->if_stmt.cond = se_expr(L(e)[1]);
        /* find optional (el ...) at end */
        int then_end = LC(e);
        Node *else_blk = NULL;
        SExpr *last = L(e)[LC(e) - 1];
        if (last->tag == SE_LIST && LC(last) > 0 &&
            last->list.items[0]->tag == SE_ATOM && sym_eq(&last->list.items[0]->atom, "el")) {
            then_end = LC(e) - 1;
            else_blk = se_block(last->list.items + 1, LC(last) - 1, last->line, last->col);
        }
        n->if_stmt.then_blk = se_block(L(e) + 2, then_end - 2, ln, co);
        n->if_stmt.else_blk = else_blk;
        free(op); return n;
    }

    /* (@ cond body...) → while */
    if (strcmp(op, "@") == 0 && LC(e) >= 3) {
        Node *n = node_new(ND_WHILE, ln, co);
        n->while_stmt.cond = se_expr(L(e)[1]);
        n->while_stmt.body = se_block(L(e) + 2, LC(e) - 2, ln, co);
        free(op); return n;
    }

    /* (del expr) → free(expr) */
    if (strcmp(op, "del") == 0 && LC(e) == 2) {
        Node *callee = node_new(ND_IDENT, ln, co);
        callee->ident.name = es_strdup("free");
        Node *call = node_new(ND_CALL, ln, co);
        call->call.callee = callee;
        call->call.args = (Node **)malloc(sizeof(Node *));
        call->call.args[0] = se_expr(L(e)[1]);
        call->call.arg_count = 1;
        Node *n = node_new(ND_EXPR_STMT, ln, co);
        n->expr_stmt.expr = call;
        free(op); return n;
    }

    free(op);

expr_stmt:;
    Node *n = node_new(ND_EXPR_STMT, ln, co);
    n->expr_stmt.expr = se_expr(e);
    return n;
}

static Node *se_block(SExpr **items, int count, int line, int col) {
    Node *blk = node_new(ND_BLOCK, line, col);
    blk->block.stmts = (Node **)malloc(count * sizeof(Node *));
    blk->block.count = count;
    for (int i = 0; i < count; i++)
        blk->block.stmts[i] = se_stmt(items[i]);
    return blk;
}

/* parse top-level: (fn ...) (st ...) (ext ...) */
static Node *se_decl(SExpr *e) {
    int ln = e->line, co = e->col;
    if (e->tag != SE_LIST || LC(e) == 0) es_fatal("expected declaration at %d:%d", ln, co);
    SExpr *head = L(e)[0];
    char *op = sym_str(&head->atom);

    /* (fn name ((p type)...) ret body...) */
    if (strcmp(op, "fn") == 0) {
        char *name = sym_str(&L(e)[1]->atom);
        bool is_main = strcmp(name, "main") == 0;

        /* parse params: ((p type) (p type) ...) */
        SExpr *plist = L(e)[2];
        int pc = 0;
        Param *params = NULL;
        if (plist->tag == SE_LIST && LC(plist) > 0) {
            pc = LC(plist);
            params = (Param *)malloc(pc * sizeof(Param));
            for (int i = 0; i < pc; i++) {
                SExpr *pp = plist->list.items[i];
                if (pp->tag == SE_LIST && LC(pp) == 2) {
                    params[i].name = sym_str(&pp->list.items[0]->atom);
                    params[i].type = se_type(pp->list.items[1]);
                } else {
                    /* anonymous param: just a type */
                    params[i].name = es_strdup("_");
                    params[i].type = se_type(pp);
                }
            }
        }

        /* return type */
        EsType *ret;
        int body_start;
        if (LC(e) > 3 && L(e)[3]->tag == SE_ATOM) {
            /* check if it looks like a type */
            char *maybe_type = sym_str(&L(e)[3]->atom);
            bool is_type = (strcmp(maybe_type,"i8")==0||strcmp(maybe_type,"i16")==0||
                strcmp(maybe_type,"i32")==0||strcmp(maybe_type,"i64")==0||
                strcmp(maybe_type,"u8")==0||strcmp(maybe_type,"u16")==0||
                strcmp(maybe_type,"u32")==0||strcmp(maybe_type,"u64")==0||
                strcmp(maybe_type,"f32")==0||strcmp(maybe_type,"f64")==0||
                strcmp(maybe_type,"v")==0||maybe_type[0]=='*');
            if (is_type) {
                ret = se_type(L(e)[3]);
                body_start = 4;
            } else {
                ret = is_main ? type_basic(TY_I32) : type_basic(TY_VOID);
                body_start = 3;
            }
            free(maybe_type);
        } else {
            ret = is_main ? type_basic(TY_I32) : type_basic(TY_VOID);
            body_start = 3;
        }

        Node *body = se_block(L(e) + body_start, LC(e) - body_start, ln, co);

        /* implicit return for non-void non-main */
        if (ret->kind != TY_VOID && !is_main && body->block.count > 0) {
            Node *last = body->block.stmts[body->block.count - 1];
            if (last->kind == ND_EXPR_STMT)
                last->kind = ND_RET;
        }

        Node *n = node_new(ND_FN_DECL, ln, co);
        n->fn.name = name;
        n->fn.params = params;
        n->fn.param_count = pc;
        n->fn.ret_type = ret;
        n->fn.body = body;
        free(op); return n;
    }

    /* (st name (field type) ...) */
    if (strcmp(op, "st") == 0) {
        char *name = sym_str(&L(e)[1]->atom);
        int fc = LC(e) - 2;
        Param *fields = (Param *)malloc(fc * sizeof(Param));
        for (int i = 0; i < fc; i++) {
            SExpr *f = L(e)[i + 2];
            fields[i].name = sym_str(&f->list.items[0]->atom);
            fields[i].type = se_type(f->list.items[1]);
        }
        Node *n = node_new(ND_ST_DECL, ln, co);
        n->st.name = name;
        n->st.fields = fields;
        n->st.field_count = fc;
        free(op); return n;
    }

    /* (ext name (type...) ret) or (ext name (type... ...) ret) */
    if (strcmp(op, "ext") == 0) {
        char *name = sym_str(&L(e)[1]->atom);
        SExpr *tlist = L(e)[2];
        bool vararg = false;
        int pc = LC(tlist);
        /* check for ... */
        if (pc > 0 && tlist->list.items[pc-1]->tag == SE_ATOM &&
            sym_eq(&tlist->list.items[pc-1]->atom, "...")) {
            vararg = true; pc--;
        }
        Param *params = NULL;
        if (pc > 0) {
            params = (Param *)malloc(pc * sizeof(Param));
            for (int i = 0; i < pc; i++) {
                char pname[16]; snprintf(pname, 16, "_p%d", i);
                params[i].name = es_strdup(pname);
                params[i].type = se_type(tlist->list.items[i]);
            }
        }
        EsType *ret = LC(e) > 3 ? se_type(L(e)[3]) : type_basic(TY_VOID);
        Node *n = node_new(ND_EXT_DECL, ln, co);
        n->ext.name = name;
        n->ext.params = params;
        n->ext.param_count = pc;
        n->ext.ret_type = ret;
        n->ext.is_vararg = vararg;
        free(op); return n;
    }

    es_fatal("unknown declaration '%s' at %d:%d", op, ln, co);
    return NULL;
}

/* ---- load std prelude (reuse existing .es parser) ---- */

static Node *load_std_prelude(void) {
    const char *paths[] = { "lib/std.es", "/home/jurip/Vibes/el-stupido/lib/std.es" };
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "r");
        if (f) {
            fclose(f);
            char *src = es_read_file(paths[i]);
            Parser p;
            parser_init(&p, src, paths[i]);
            return parser_parse_prelude(&p);
        }
    }
    return NULL;
}

/* ---- public API ---- */
Node *sexpr_parse(const char *src, const char *file) {
    SP p = {0};
    p.lex.cur = src;
    p.lex.file = file;
    p.lex.line = 1; p.lex.col = 1;
    p.file = file;
    snext(&p);

    struct { Node **items; int count; int cap; } decls = {0};

    /* auto-load std */
    Node *std = load_std_prelude();
    if (std) {
        for (int i = 0; i < std->program.count; i++)
            da_push(decls, std->program.decls[i]);
    }

    while (p.tok.kind != ST_EOF) {
        SExpr *e = parse_sexpr(&p);
        da_push(decls, se_decl(e));
    }

    Node *prog = node_new(ND_PROGRAM, 1, 1);
    prog->program.decls = decls.items;
    prog->program.count = decls.count;
    return prog;
}
