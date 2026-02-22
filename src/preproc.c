/*  ⚡ unified comptime preprocessor
 *  Runs BEFORE lexer.  Two roles for ⚡:
 *    ⚡ NAME(p1,p2) 👉 body   →  text macro (stripped, expanded)
 *    ⚡ NAME 👉 body           →  constant macro (stripped, expanded)
 *    ⚡ expr                   →  left for parser (comptime eval)
 *  The 👉 separator distinguishes macro defs from comptime exprs.
 *  Multi-pass expansion handles nested macros.
 *  Skips strings and comments during expansion. */

#include "es.h"

#define PP_MAX_MACROS 128
#define PP_MAX_PARAMS 8

typedef struct { char *name; char *params[PP_MAX_PARAMS]; int pc; char *body; } PMac;
static PMac ms[PP_MAX_MACROS];
static int mn = 0;

/* ---- buffer ---- */
typedef struct { char *d; int n, c; } PBuf;
static void bc(PBuf *b, char ch) {
    if (b->n >= b->c) { b->c = b->c ? b->c*2 : 4096; b->d = realloc(b->d, b->c); }
    b->d[b->n++] = ch;
}
static void bw(PBuf *b, const char *s, int l) { for (int i = 0; i < l; i++) bc(b, s[i]); }
static char *bz(PBuf *b) { bc(b, '\0'); return b->d; }
static int ic(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* ---- UTF-8 decode (handles 1-4 byte sequences) ---- */
static uint32_t u8d(const char *s, int *bytes) {
    uint8_t c = (uint8_t)s[0];
    if (c < 0x80) { *bytes = 1; return c; }
    if ((c&0xE0)==0xC0) { *bytes=2; return ((uint32_t)(c&0x1F)<<6)|(s[1]&0x3F); }
    if ((c&0xF0)==0xE0) { *bytes=3; return ((uint32_t)(c&0x0F)<<12)|((uint32_t)(s[1]&0x3F)<<6)|(s[2]&0x3F); }
    if ((c&0xF8)==0xF0) { *bytes=4; return ((uint32_t)(c&0x07)<<18)|((uint32_t)(s[1]&0x3F)<<12)|((uint32_t)(s[2]&0x3F)<<6)|(s[3]&0x3F); }
    *bytes = 1; return c;
}

/* check emoji codepoint, return bytes consumed (incl optional FE0F), 0 if no match */
static int cem(const char *s, uint32_t cp) {
    int b; if (u8d(s, &b) != cp) return 0;
    int n = b; int vb; if (u8d(s+n, &vb) == 0xFE0F) n += vb;
    return n;
}

/* ---- pass 1: collect ⚡...👉 defs, pass through everything else ---- */
static char *collect(const char *src) {
    PBuf out = {0};
    const char *p = src;
    while (*p) {
        const char *sol = p;
        while (*p == ' ' || *p == '\t') p++;
        int cl = cem(p, 0x26A1); /* ⚡ U+26A1 */
        if (cl) {
            const char *q = p + cl;
            while (*q == ' ' || *q == '\t') q++;
            /* need identifier name */
            const char *ns = q;
            while (ic(*q)) q++;
            int nlen = (int)(q - ns);
            if (nlen > 0) {
                /* optional (params) */
                int tpc = 0; char *tp[PP_MAX_PARAMS];
                if (*q == '(') {
                    q++;
                    while (*q && *q != ')') {
                        while (*q == ' ' || *q == '\t') q++;
                        const char *ps = q;
                        while (ic(*q)) q++;
                        if (q > ps) tp[tpc++] = es_strndup(ps, (size_t)(q - ps));
                        while (*q == ' ' || *q == '\t') q++;
                        if (*q == ',') q++;
                    }
                    if (*q == ')') q++;
                }
                while (*q == ' ' || *q == '\t') q++;
                int pl = cem(q, 0x1F449); /* 👉 U+1F449 */
                if (pl) {
                    /* YES — macro definition. consume and store. */
                    q += pl;
                    while (*q == ' ' || *q == '\t') q++;
                    const char *bs = q;
                    while (*q && *q != '\n') q++;
                    PMac *m = &ms[mn++];
                    m->name = es_strndup(ns, (size_t)nlen);
                    m->pc = tpc;
                    for (int i = 0; i < tpc; i++) m->params[i] = tp[i];
                    m->body = es_strndup(bs, (size_t)(q - bs));
                    p = q; if (*p == '\n') p++;
                    continue;
                }
                /* NO 👉 — not a macro. free temps, fall through to copy line. */
                for (int i = 0; i < tpc; i++) free(tp[i]);
            }
        }
        /* copy line verbatim */
        p = sol;
        while (*p && *p != '\n') { bc(&out, *p); p++; }
        if (*p == '\n') { bc(&out, '\n'); p++; }
    }
    return bz(&out);
}

/* ---- substitute params in macro body ---- */
static void subst(PBuf *out, PMac *m, char **args, int ac) {
    const char *b = m->body;
    while (*b) {
        if (*b == '"') {
            bc(out, *b); b++;
            while (*b && *b != '"') {
                if (*b == '\\' && b[1]) { bc(out, *b); b++; }
                bc(out, *b); b++;
            }
            if (*b == '"') { bc(out, *b); b++; }
            continue;
        }
        if ((isalpha((unsigned char)*b) || *b == '_') && (b == m->body || !ic(b[-1]))) {
            const char *ws = b;
            while (ic(*b)) b++;
            int wl = (int)(b - ws), pi = -1;
            for (int j = 0; j < m->pc; j++)
                if ((int)strlen(m->params[j]) == wl && memcmp(ws, m->params[j], wl) == 0) { pi = j; break; }
            if (pi >= 0 && pi < ac) bw(out, args[pi], (int)strlen(args[pi]));
            else bw(out, ws, wl);
            continue;
        }
        bc(out, *b); b++;
    }
}

/* ---- pass 2+: expand macro invocations ---- */
static char *expand(const char *src) {
    PBuf out = {0};
    const char *r = src;
    int changed = 0;
    while (*r) {
        if (*r == '"') {
            bc(&out, *r); r++;
            while (*r && *r != '"') {
                if (*r == '\\' && r[1]) { bc(&out, *r); r++; }
                bc(&out, *r); r++;
            }
            if (*r == '"') { bc(&out, *r); r++; }
            continue;
        }
        if (*r == '\'' && r[1] && r[2] == '\'') {
            bc(&out, *r); r++; bc(&out, *r); r++; bc(&out, *r); r++;
            continue;
        }
        if (*r == '/' && r[1] == '/') {
            while (*r && *r != '\n') { bc(&out, *r); r++; }
            continue;
        }
        if ((isalpha((unsigned char)*r) || *r == '_') && (r == src || !ic(r[-1]))) {
            const char *ws = r;
            while (ic(*r)) r++;
            int wl = (int)(r - ws), fi = -1;
            for (int i = 0; i < mn; i++)
                if ((int)strlen(ms[i].name) == wl && memcmp(ws, ms[i].name, wl) == 0) { fi = i; break; }
            if (fi >= 0 && ms[fi].pc > 0 && *r == '(') {
                r++;
                char *args[PP_MAX_PARAMS]; int ac = 0, depth = 1;
                const char *as = r;
                while (*r && depth > 0) {
                    if (*r == '"') { r++; while (*r && *r != '"') { if (*r == '\\' && r[1]) r++; r++; } if (*r) r++; continue; }
                    if (*r == '(') depth++;
                    else if (*r == ')') { if (--depth == 0) break; }
                    else if (*r == ',' && depth == 1) { args[ac++] = es_strndup(as, (size_t)(r - as)); r++; as = r; continue; }
                    r++;
                }
                args[ac++] = es_strndup(as, (size_t)(r - as));
                if (*r == ')') r++;
                subst(&out, &ms[fi], args, ac);
                for (int j = 0; j < ac; j++) free(args[j]);
                changed = 1; continue;
            }
            if (fi >= 0 && ms[fi].pc == 0) {
                bw(&out, ms[fi].body, (int)strlen(ms[fi].body));
                changed = 1; continue;
            }
            bw(&out, ws, wl); continue;
        }
        bc(&out, *r); r++;
    }
    if (!changed) { free(out.d); return NULL; }
    return bz(&out);
}

/* ---- public API ---- */
char *preprocess(const char *src) {
    mn = 0;
    char *cur = collect(src);
    if (mn == 0) return cur;
    for (int pass = 0; pass < 16; pass++) {
        char *exp = expand(cur);
        if (!exp) break;
        free(cur); cur = exp;
    }
    return cur;
}
