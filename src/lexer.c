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
        break;
    case 2:
        if (s[0]=='f' && s[1]=='n') return TOK_FN;
        if (s[0]=='i' && s[1]=='f') return TOK_IF;
        if (s[0]=='e' && s[1]=='l') return TOK_EL;
        if (s[0]=='w' && s[1]=='h') return TOK_WH;
        if (s[0]=='a' && s[1]=='s') return TOK_AS;
        if (s[0]=='n' && s[1]=='w') return TOK_NW;
        if (s[0]=='c' && s[1]=='t') return TOK_CT;
        if (s[0]=='f' && s[1]=='o') return TOK_FOR;
        if (s[0]=='m' && s[1]=='a') return TOK_MATCH;
        if (s[0]=='e' && s[1]=='n') return TOK_ENUM;
        if (s[0]=='d' && s[1]=='f') return TOK_DEFER;
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
        if (memcmp(s,"var",3)==0) return TOK_VAR;
        if (memcmp(s,"for",3)==0) return TOK_FOR;
        if (memcmp(s,"let",3)==0) return TOK_VAR;
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
        if (memcmp(s,"void",4)==0) return TOK_VOID;
        if (memcmp(s,"else",4)==0) return TOK_EL;
        if (memcmp(s,"bool",4)==0) return TOK_BOOL;
        break;
    case 5:
        if (memcmp(s,"while",5)==0) return TOK_WH;
        if (memcmp(s,"break",5)==0) return TOK_BRK;
        if (memcmp(s,"match",5)==0) return TOK_MATCH;
        if (memcmp(s,"defer",5)==0) return TOK_DEFER;
        break;
    case 6:
        if (memcmp(s,"return",6)==0) return TOK_RET;
        if (memcmp(s,"struct",6)==0) return TOK_ST;
        if (memcmp(s,"extern",6)==0) return TOK_EXT;
        if (memcmp(s,"delete",6)==0) return TOK_DEL;
        if (memcmp(s,"sizeof",6)==0) return TOK_SZ;
        break;
    case 8:
        if (memcmp(s,"continue",8)==0) return TOK_CONT;
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
    case 0x27B0:  return TOK_FOR;     /* ➰ */
    case 0x1F3AF: return TOK_MATCH;   /* 🎯 */
    case 0x1F3F7: return TOK_ENUM;    /* 🏷  */
    case 0x1F51C: return TOK_DEFER;   /* 🔜 */

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

/* ---- emoji → C function name aliases ---- */
/*  🖨  printf    📣 fprintf   📝 sprintf   📢 puts
    🔔 putchar   👂 getchar   📂 open      📕 close
    📖 read      ✏  write     🔖 lseek
    🧠 malloc    🧩 calloc    ♻  realloc   🆓 free
    🧹 memset    📋 memcpy    🔀 memmove   ⚖  memcmp
    🧵 strlen    ⚔  strcmp    🗡  strncmp   ✂  strcpy
    🪡 strncpy   🔗 strcat    🔍 strchr    🔎 strstr
    🅰  atoi     🅱  atol
    🌐 socket    📌 bind      📡 listen    🤝 accept
    🧲 connect   📤 send      📩 recv      🎛  setsockopt
    🔃 htons     🔂 htonl     🔙 ntohs     🔚 ntohl
    🏠 inet_addr
    📐 sqrt      🎵 sin       🎶 cos       💪 pow
    🧊 fabs      ⬇  floor     ⬆  ceil      📓 log
    💀 exit      🍴 fork      🏃 execvp    ⏳ waitpid
    🆔 getpid    😴 sleep     ⏰ usleep
    🗺  mmap     🚫 munmap                              */
static const char *check_emoji_fn(uint32_t cp) {
    switch (cp) {
    /* I/O */
    case 0x1F5A8: return "printf";
    case 0x1F4E3: return "fprintf";
    case 0x1F4DD: return "sprintf";
    case 0x1F4E2: return "puts";
    case 0x1F514: return "putchar";
    case 0x1F442: return "getchar";

    /* file */
    case 0x1F4C2: return "open";
    case 0x1F4D5: return "close";
    case 0x1F4D6: return "read";
    case 0x270F:  return "write";
    case 0x1F516: return "lseek";

    /* memory */
    case 0x1F9E0: return "malloc";
    case 0x1F9E9: return "calloc";
    case 0x267B:  return "realloc";
    case 0x1F193: return "free";
    case 0x1F9F9: return "memset";
    case 0x1F4CB: return "memcpy";
    case 0x1F500: return "memmove";
    case 0x2696:  return "memcmp";

    /* strings */
    case 0x1F9F5: return "strlen";
    case 0x2694:  return "strcmp";
    case 0x1F5E1: return "strncmp";
    case 0x2702:  return "strcpy";
    case 0x1FAA1: return "strncpy";
    case 0x1F517: return "strcat";
    case 0x1F50D: return "strchr";
    case 0x1F50E: return "strstr";
    case 0x1F170: return "atoi";
    case 0x1F171: return "atol";

    /* network */
    case 0x1F310: return "socket";
    case 0x1F4CC: return "bind";
    case 0x1F4E1: return "listen";
    case 0x1F91D: return "accept";
    case 0x1F9F2: return "connect";
    case 0x1F4E4: return "send";
    case 0x1F4E9: return "recv";
    case 0x1F39B: return "setsockopt";
    case 0x1F503: return "htons";
    case 0x1F502: return "htonl";
    case 0x1F519: return "ntohs";
    case 0x1F51A: return "ntohl";
    case 0x1F3E0: return "inet_addr";

    /* math */
    case 0x1F4D0: return "sqrt";
    case 0x1F3B5: return "sin";
    case 0x1F3B6: return "cos";
    case 0x1F4AA: return "pow";
    case 0x1F9CA: return "fabs";
    case 0x2B07:  return "floor";
    case 0x2B06:  return "ceil";
    case 0x1F4D3: return "log";

    /* process */
    case 0x1F480: return "exit";
    case 0x1F374: return "fork";
    case 0x1F3C3: return "execvp";
    case 0x231B:  return "waitpid";
    case 0x1F194: return "getpid";
    case 0x1F634: return "sleep";
    case 0x23F0:  return "usleep";

    /* mmap */
    case 0x1F5FA: return "mmap";
    case 0x1F6AB: return "munmap";

    /* entry point */
    case 0x1F3C1: return "main";

    default: return NULL;
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

    /* keyword? */
    TokenKind k = check_emoji(cp);
    if (k != TOK_ERROR)
        return make(l, k, start, sline, scol);

    /* C function alias? → return as TOK_IDENT with C name */
    const char *fn = check_emoji_fn(cp);
    if (fn) {
        Token t;
        memset(&t, 0, sizeof(t));
        t.kind = TOK_IDENT;
        t.start = fn;
        t.len = (int)strlen(fn);
        t.line = sline;
        t.col = scol;
        return t;
    }

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
        if (peek(l)=='>') { advance(l); return make(l, TOK_PIPE_OP, start, sline, scol); }
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
        if (peek(l)=='.') {
            advance(l);
            if (peek(l)=='=') {
                advance(l);
                return make(l, TOK_RANGE_INC, start, sline, scol);
            }
            return make(l, TOK_RANGE, start, sline, scol);
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
        [TOK_ST]="struct", [TOK_USE]="use", [TOK_AS]="as",
        [TOK_SZ]="sizeof", [TOK_NULL_KW]="null",
        [TOK_BRK]="brk", [TOK_CONT]="cont",
        [TOK_NW]="nw", [TOK_DEL]="del",
        [TOK_ASM]="asm", [TOK_CT]="ct",
        [TOK_FOR]="fo", [TOK_MATCH]="ma", [TOK_ENUM]="en", [TOK_DEFER]="df",
        [TOK_I8]="i8", [TOK_I16]="i16", [TOK_I32]="i32", [TOK_I64]="i64",
        [TOK_U8]="u8", [TOK_U16]="u16", [TOK_U32]="u32", [TOK_U64]="u64",
        [TOK_F32]="f32", [TOK_F64]="f64", [TOK_VOID]="void",
        [TOK_INT_LIT]="<int>", [TOK_FLOAT_LIT]="<float>", [TOK_STR_LIT]="<str>",
        [TOK_IDENT]="<id>",
        [TOK_PLUS]="+", [TOK_MINUS]="-", [TOK_STAR]="*",
        [TOK_SLASH]="/", [TOK_PERCENT]="%",
        [TOK_AMP]="&", [TOK_PIPE]="|", [TOK_PIPE_OP]="|>", [TOK_CARET]="^",
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
        [TOK_ELLIPSIS]="...", [TOK_RANGE]="..", [TOK_RANGE_INC]="..=", [TOK_COMMA]=",",
        [TOK_LPAREN]="(", [TOK_RPAREN]=")",
        [TOK_LBRACE]="{", [TOK_RBRACE]="}",
        [TOK_LBRACKET]="[", [TOK_RBRACKET]="]",
        [TOK_NEWLINE]="<nl>", [TOK_EOF]="<eof>", [TOK_ERROR]="<err>",
    };
    return names[k];
}
