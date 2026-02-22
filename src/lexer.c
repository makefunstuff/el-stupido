#include "lexer.h"

void lexer_init(Lexer *l, const char *src, const char *filename) {
    l->src = src;
    l->cur = src;
    l->filename = filename;
    l->line = 1;
    l->col = 1;
}

static char peek(Lexer *l) { return *l->cur; }
static char peek2(Lexer *l) { return l->cur[0] ? l->cur[1] : '\0'; }

static char advance(Lexer *l) {
    char c = *l->cur++;
    if (c == '\n') { l->line++; l->col = 1; }
    else { l->col++; }
    return c;
}

static Token make(Lexer *l, TokenKind k, const char *start, int sline, int scol) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = k;
    t.start = start;
    t.len = (int)(l->cur - start);
    t.line = sline;
    t.col = scol;
    return t;
}

static Token error_tok(Lexer *l, const char *msg) {
    Token t;
    memset(&t, 0, sizeof(t));
    t.kind = TOK_ERROR;
    t.start = msg;
    t.len = (int)strlen(msg);
    t.line = l->line;
    t.col = l->col;
    return t;
}

/* ---- keyword lookup ---- */
static TokenKind check_keyword(const char *s, int len) {
    switch (len) {
    case 1:
        if (s[0] == 'v') return TOK_VOID;
        break;
    case 2:
        if (s[0]=='f' && s[1]=='n') return TOK_FN;
        if (s[0]=='i' && s[1]=='f') return TOK_IF;
        if (s[0]=='e' && s[1]=='l') return TOK_EL;
        if (s[0]=='w' && s[1]=='h') return TOK_WH;
        if (s[0]=='s' && s[1]=='t') return TOK_ST;
        if (s[0]=='a' && s[1]=='s') return TOK_AS;
        if (s[0]=='s' && s[1]=='z') return TOK_SZ;
        if (s[0]=='n' && s[1]=='w') return TOK_NW;
        if (s[0]=='c' && s[1]=='t') return TOK_CT;
        if (s[0]=='i' && s[1]=='8') return TOK_I8;
        if (s[0]=='u' && s[1]=='8') return TOK_U8;
        break;
    case 3:
        if (memcmp(s,"ext",3)==0) return TOK_EXT;
        if (memcmp(s,"ret",3)==0) return TOK_RET;
        if (memcmp(s,"use",3)==0) return TOK_USE;
        if (memcmp(s,"brk",3)==0) return TOK_BRK;
        if (memcmp(s,"del",3)==0) return TOK_DEL;
        if (memcmp(s,"asm",3)==0) return TOK_ASM;
        if (memcmp(s,"i16",3)==0) return TOK_I16;
        if (memcmp(s,"i32",3)==0) return TOK_I32;
        if (memcmp(s,"i64",3)==0) return TOK_I64;
        if (memcmp(s,"u16",3)==0) return TOK_U16;
        if (memcmp(s,"u32",3)==0) return TOK_U32;
        if (memcmp(s,"u64",3)==0) return TOK_U64;
        if (memcmp(s,"f32",3)==0) return TOK_F32;
        if (memcmp(s,"f64",3)==0) return TOK_F64;
        break;
    case 4:
        if (memcmp(s,"null",4)==0) return TOK_NULL_KW;
        if (memcmp(s,"cont",4)==0) return TOK_CONT;
        break;
    }
    return TOK_IDENT;
}

/* ---- UTF-8 decode ---- */
static uint32_t decode_utf8(const char *s, int *bytes) {
    uint8_t c = (uint8_t)s[0];
    if (c < 0x80) { *bytes = 1; return c; }
    if ((c & 0xE0) == 0xC0) {
        *bytes = 2;
        return ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0) {
        *bytes = 3;
        return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0) {
        *bytes = 4;
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
               ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    *bytes = 1; return c;
}

/* ---- emoji keyword map ---- */
/*  CONTROL FLOW          DECLARATIONS         MEMORY
    ❓ if                 🔧 fn               ✨ nw
    ❗ el                 📦 st               🗑  del
    🔁 wh                🔌 ext
    ↩  ret               📥 use              MISC
    🛑 brk                                    🔄 as
    ⏩ cont              NEW FEATURES         📏 sz
                          🔩 asm              ∅  null
    TYPES                 ⚡ ct
    🔢 i32   💧 i8    📊 i16   🔷 i64
    🔶 u8    📈 u16   🔵 u32   💎 u64
    🌊 f32   🌀 f64   ⬛ void                           */
static TokenKind check_emoji(uint32_t cp) {
    switch (cp) {
    /* control flow */
    case 0x2753:  return TOK_IF;      /* ❓ */
    case 0x2757:  return TOK_EL;      /* ❗ */
    case 0x1F501: return TOK_WH;      /* 🔁 */
    case 0x21A9:  return TOK_RET;     /* ↩  */
    case 0x1F6D1: return TOK_BRK;     /* 🛑 */
    case 0x23E9:  return TOK_CONT;    /* ⏩ */

    /* declarations */
    case 0x1F527: return TOK_FN;      /* 🔧 */
    case 0x1F4E6: return TOK_ST;      /* 📦 */
    case 0x1F50C: return TOK_EXT;     /* 🔌 */
    case 0x1F4E5: return TOK_USE;     /* 📥 */

    /* memory */
    case 0x2728:  return TOK_NW;      /* ✨ */
    case 0x1F5D1: return TOK_DEL;     /* 🗑  */

    /* new features */
    case 0x1F529: return TOK_ASM;     /* 🔩 */
    case 0x26A1:  return TOK_CT;      /* ⚡ */

    /* misc */
    case 0x1F504: return TOK_AS;      /* 🔄 */
    case 0x1F4CF: return TOK_SZ;      /* 📏 */
    case 0x2205:  return TOK_NULL_KW; /* ∅  */

    /* types — signed int */
    case 0x1F4A7: return TOK_I8;      /* 💧 */
    case 0x1F4CA: return TOK_I16;     /* 📊 */
    case 0x1F522: return TOK_I32;     /* 🔢 */
    case 0x1F537: return TOK_I64;     /* 🔷 */

    /* types — unsigned int */
    case 0x1F536: return TOK_U8;      /* 🔶 */
    case 0x1F4C8: return TOK_U16;     /* 📈 */
    case 0x1F535: return TOK_U32;     /* 🔵 */
    case 0x1F48E: return TOK_U64;     /* 💎 */

    /* types — float */
    case 0x1F30A: return TOK_F32;     /* 🌊 */
    case 0x1F300: return TOK_F64;     /* 🌀 */

    /* types — void */
    case 0x2B1B:  return TOK_VOID;    /* ⬛ */

    default:      return TOK_ERROR;
    }
}

static Token scan_emoji(Lexer *l) {
    const char *start = l->cur;
    int sline = l->line, scol = l->col;
    int bytes;
    uint32_t cp = decode_utf8(l->cur, &bytes);
    for (int i = 0; i < bytes; i++) advance(l);
    /* skip optional variation selector U+FE0F */
    int vb;
    if (decode_utf8(l->cur, &vb) == 0xFE0F)
        for (int i = 0; i < vb; i++) advance(l);
    TokenKind k = check_emoji(cp);
    if (k != TOK_ERROR)
        return make(l, k, start, sline, scol);
    return error_tok(l, "unexpected character");
}

/* ---- scan helpers ---- */
static Token scan_ident(Lexer *l) {
    const char *start = l->cur;
    int sline = l->line, scol = l->col;
    while (isalnum(peek(l)) || peek(l) == '_') advance(l);
    int len = (int)(l->cur - start);
    TokenKind k = check_keyword(start, len);
    return make(l, k, start, sline, scol);
}

static Token scan_number(Lexer *l) {
    const char *start = l->cur;
    int sline = l->line, scol = l->col;

    if (peek(l) == '0' && (peek2(l) == 'x' || peek2(l) == 'X')) {
        advance(l); advance(l); /* skip 0x */
        while (isxdigit(peek(l))) advance(l);
        Token t = make(l, TOK_INT_LIT, start, sline, scol);
        t.int_val = strtoll(start, NULL, 0);
        return t;
    }

    while (isdigit(peek(l))) advance(l);

    /* check for float: digit+ '.' digit+ */
    if (peek(l) == '.' && isdigit(peek2(l))) {
        advance(l); /* skip '.' */
        while (isdigit(peek(l))) advance(l);
        Token t = make(l, TOK_FLOAT_LIT, start, sline, scol);
        t.float_val = strtod(start, NULL);
        return t;
    }

    Token t = make(l, TOK_INT_LIT, start, sline, scol);
    t.int_val = strtoll(start, NULL, 0);
    return t;
}

static Token scan_string(Lexer *l) {
    int sline = l->line, scol = l->col;
    const char *start = l->cur;
    advance(l); /* skip opening " */

    /* first pass: compute length */
    const char *scan = l->cur;
    int slen = 0;
    while (*scan && *scan != '"') {
        if (*scan == '\\') { scan++; }
        scan++; slen++;
    }

    char *buf = (char *)malloc(slen + 1);
    int i = 0;
    while (peek(l) && peek(l) != '"') {
        if (peek(l) == '\\') {
            advance(l);
            switch (peek(l)) {
            case 'n':  buf[i++] = '\n'; break;
            case 't':  buf[i++] = '\t'; break;
            case '\\': buf[i++] = '\\'; break;
            case '"':  buf[i++] = '"';  break;
            case '0':  buf[i++] = '\0'; break;
            case 'r':  buf[i++] = '\r'; break;
            default:   buf[i++] = peek(l); break;
            }
            advance(l);
        } else {
            buf[i++] = advance(l);
        }
    }
    buf[i] = '\0';

    if (peek(l) != '"')
        return error_tok(l, "unterminated string");
    advance(l); /* skip closing " */

    Token t = make(l, TOK_STR_LIT, start, sline, scol);
    t.str_val.data = buf;
    t.str_val.len = i;
    return t;
}

/* ---- main lexer entry ---- */
Token lexer_next(Lexer *l) {
    /* skip spaces and tabs (NOT newlines) */
    while (peek(l) == ' ' || peek(l) == '\t') advance(l);

    /* skip line comments */
    if (peek(l) == '/' && peek2(l) == '/') {
        while (peek(l) && peek(l) != '\n') advance(l);
        /* fall through to newline handling or next call */
        if (peek(l) == '\0') return make(l, TOK_EOF, l->cur, l->line, l->col);
    }

    const char *start = l->cur;
    int sline = l->line, scol = l->col;
    char c = peek(l);

    if (c == '\0') return make(l, TOK_EOF, start, sline, scol);

    /* newlines -- collapse consecutive */
    if (c == '\n') {
        advance(l);
        while (peek(l) == '\n' || peek(l) == ' ' || peek(l) == '\t' || peek(l) == '\r') {
            if (peek(l) == '\n') advance(l);
            else advance(l);
        }
        /* skip comments after blank lines */
        if (peek(l) == '/' && peek2(l) == '/') {
            while (peek(l) && peek(l) != '\n') advance(l);
            return lexer_next(l);
        }
        return make(l, TOK_NEWLINE, start, sline, scol);
    }

    if (c == '\r') { advance(l); return lexer_next(l); }

    /* identifiers and keywords */
    if (isalpha(c) || c == '_') return scan_ident(l);

    /* numbers */
    if (isdigit(c)) return scan_number(l);

    /* strings */
    if (c == '"') return scan_string(l);

    /* emoji keywords (UTF-8 multi-byte) */
    if ((uint8_t)c >= 0x80) return scan_emoji(l);

    /* operators and punctuation */
    advance(l);
    switch (c) {
    case '(': return make(l, TOK_LPAREN, start, sline, scol);
    case ')': return make(l, TOK_RPAREN, start, sline, scol);
    case '{': return make(l, TOK_LBRACE, start, sline, scol);
    case '}': return make(l, TOK_RBRACE, start, sline, scol);
    case '[': return make(l, TOK_LBRACKET, start, sline, scol);
    case ']': return make(l, TOK_RBRACKET, start, sline, scol);
    case ',': return make(l, TOK_COMMA, start, sline, scol);
    case '~': return make(l, TOK_TILDE, start, sline, scol);
    case '^': return make(l, TOK_CARET, start, sline, scol);
    case '?': return make(l, TOK_QUESTION, start, sline, scol);
    case ';': return make(l, TOK_SEMI, start, sline, scol);
    case '+':
        if (peek(l)=='=') { advance(l); return make(l, TOK_PLUS_EQ, start, sline, scol); }
        return make(l, TOK_PLUS, start, sline, scol);
    case '%':
        if (peek(l)=='=') { advance(l); return make(l, TOK_PERCENT_EQ, start, sline, scol); }
        return make(l, TOK_PERCENT, start, sline, scol);
    case '/':
        if (peek(l)=='=') { advance(l); return make(l, TOK_SLASH_EQ, start, sline, scol); }
        return make(l, TOK_SLASH, start, sline, scol);

    case '*':
        if (peek(l)=='=') { advance(l); return make(l, TOK_STAR_EQ, start, sline, scol); }
        return make(l, TOK_STAR, start, sline, scol);

    case '&':
        if (peek(l)=='&') { advance(l); return make(l, TOK_LAND, start, sline, scol); }
        return make(l, TOK_AMP, start, sline, scol);

    case '|':
        if (peek(l)=='|') { advance(l); return make(l, TOK_LOR, start, sline, scol); }
        return make(l, TOK_PIPE, start, sline, scol);

    case '!':
        if (peek(l)=='=') { advance(l); return make(l, TOK_NEQ, start, sline, scol); }
        return make(l, TOK_BANG, start, sline, scol);

    case '=':
        if (peek(l)=='=') { advance(l); return make(l, TOK_EQ, start, sline, scol); }
        return make(l, TOK_ASSIGN, start, sline, scol);

    case '<':
        if (peek(l)=='=') { advance(l); return make(l, TOK_LEQ, start, sline, scol); }
        if (peek(l)=='<') { advance(l); return make(l, TOK_SHL, start, sline, scol); }
        return make(l, TOK_LT, start, sline, scol);

    case '>':
        if (peek(l)=='=') { advance(l); return make(l, TOK_GEQ, start, sline, scol); }
        if (peek(l)=='>') { advance(l); return make(l, TOK_SHR, start, sline, scol); }
        return make(l, TOK_GT, start, sline, scol);

    case ':':
        if (peek(l)=='=') { advance(l); return make(l, TOK_DECL_ASSIGN, start, sline, scol); }
        return make(l, TOK_COLON, start, sline, scol);

    case '-':
        if (peek(l)=='>') { advance(l); return make(l, TOK_ARROW, start, sline, scol); }
        if (peek(l)=='=') { advance(l); return make(l, TOK_MINUS_EQ, start, sline, scol); }
        return make(l, TOK_MINUS, start, sline, scol);

    case '.':
        if (peek(l)=='.' && peek2(l)=='.') {
            advance(l); advance(l);
            return make(l, TOK_ELLIPSIS, start, sline, scol);
        }
        return make(l, TOK_DOT, start, sline, scol);
    }

    return error_tok(l, "unexpected character");
}

/* ---- debug names ---- */
const char *tok_str(TokenKind k) {
    static const char *names[] = {
        [TOK_EXT]="ext", [TOK_FN]="fn", [TOK_RET]="ret",
        [TOK_IF]="if", [TOK_EL]="el", [TOK_WH]="wh",
        [TOK_ST]="st", [TOK_USE]="use", [TOK_AS]="as",
        [TOK_SZ]="sz", [TOK_NULL_KW]="null",
        [TOK_BRK]="brk", [TOK_CONT]="cont",
        [TOK_NW]="nw", [TOK_DEL]="del",
        [TOK_ASM]="asm", [TOK_CT]="ct",
        [TOK_I8]="i8", [TOK_I16]="i16", [TOK_I32]="i32", [TOK_I64]="i64",
        [TOK_U8]="u8", [TOK_U16]="u16", [TOK_U32]="u32", [TOK_U64]="u64",
        [TOK_F32]="f32", [TOK_F64]="f64", [TOK_VOID]="v",
        [TOK_INT_LIT]="<int>", [TOK_FLOAT_LIT]="<float>", [TOK_STR_LIT]="<str>",
        [TOK_IDENT]="<id>",
        [TOK_PLUS]="+", [TOK_MINUS]="-", [TOK_STAR]="*",
        [TOK_SLASH]="/", [TOK_PERCENT]="%",
        [TOK_AMP]="&", [TOK_PIPE]="|", [TOK_CARET]="^",
        [TOK_TILDE]="~", [TOK_BANG]="!",
        [TOK_EQ]="==", [TOK_NEQ]="!=",
        [TOK_LT]="<", [TOK_GT]=">", [TOK_LEQ]="<=", [TOK_GEQ]=">=",
        [TOK_LAND]="&&", [TOK_LOR]="||",
        [TOK_SHL]="<<", [TOK_SHR]=">>",
        [TOK_QUESTION]="?",
        [TOK_ASSIGN]="=", [TOK_DECL_ASSIGN]=":=",
        [TOK_PLUS_EQ]="+=", [TOK_MINUS_EQ]="-=",
        [TOK_STAR_EQ]="*=", [TOK_SLASH_EQ]="/=", [TOK_PERCENT_EQ]="%=",
        [TOK_SEMI]=";",
        [TOK_COLON]=":", [TOK_ARROW]="->", [TOK_DOT]=".",
        [TOK_ELLIPSIS]="...", [TOK_COMMA]=",",
        [TOK_LPAREN]="(", [TOK_RPAREN]=")",
        [TOK_LBRACE]="{", [TOK_RBRACE]="}",
        [TOK_LBRACKET]="[", [TOK_RBRACKET]="]",
        [TOK_NEWLINE]="<nl>", [TOK_EOF]="<eof>", [TOK_ERROR]="<err>",
    };
    return names[k];
}
