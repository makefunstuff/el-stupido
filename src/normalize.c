#include "normalize.h"
#include "es.h"

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} StrBuf;

static void sb_grow(StrBuf *sb, size_t add) {
    if (sb->len + add + 1 <= sb->cap) return;
    size_t ncap = sb->cap ? sb->cap : 128;
    while (sb->len + add + 1 > ncap) ncap *= 2;
    sb->buf = (char *)realloc(sb->buf, ncap);
    sb->cap = ncap;
}

static void sb_putc(StrBuf *sb, char c) {
    sb_grow(sb, 1);
    sb->buf[sb->len++] = c;
}

static void sb_putn(StrBuf *sb, const char *s, size_t n) {
    sb_grow(sb, n);
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
}

static void sb_puts(StrBuf *sb, const char *s) {
    sb_putn(sb, s, strlen(s));
}

static char *trim_copy(const char *s, size_t n) {
    size_t a = 0, b = n;
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return es_strndup(s + a, b - a);
}

/* Line-wise rewrite:
   x = if cond then a else b   ->   x = cond ? a : b
   x := if cond then a else b  ->   x := cond ? a : b */
static char *normalize_if_then_else_lines(const char *src) {
    StrBuf out = {0};
    const char *p = src;

    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        size_t len = (size_t)(p - line);
        char *linebuf = es_strndup(line, len);

        const char *eq_if = NULL;
        size_t op_len = 0;

        /* skip obvious comment lines */
        size_t lead = 0;
        while (lead < len && isspace((unsigned char)linebuf[lead])) lead++;
        if (lead + 1 < len && linebuf[lead] == '/' && linebuf[lead + 1] == '/') {
            sb_putn(&out, linebuf, len);
            free(linebuf);
            goto line_end;
        }

        const char *p_eq = strstr(linebuf, "= if ");
        const char *p_decl = strstr(linebuf, ":= if ");
        if (p_eq && (!p_decl || p_eq < p_decl)) {
            eq_if = p_eq;
            op_len = 1;
        } else if (p_decl) {
            eq_if = p_decl;
            op_len = 2;
        }

        if (eq_if) {
            const char *cond = eq_if + op_len + 4; /* skip op + " if " */
            const char *then_kw = strstr(cond, " then ");
            const char *else_kw = then_kw ? strstr(then_kw + 6, " else ") : NULL;
            const char *line_end_ptr = linebuf + len;
            if (then_kw && else_kw && else_kw < line_end_ptr) {
                /* prefix (left side + operator) */
                sb_putn(&out, linebuf, (size_t)(eq_if - linebuf));
                if (op_len == 1) sb_puts(&out, "= ");
                else sb_puts(&out, ":= ");

                char *cond_s = trim_copy(cond, (size_t)(then_kw - cond));
                char *then_s = trim_copy(then_kw + 6, (size_t)(else_kw - (then_kw + 6)));
                char *else_s = trim_copy(else_kw + 6, (size_t)(line_end_ptr - (else_kw + 6)));

                sb_puts(&out, cond_s);
                sb_puts(&out, " ? ");
                sb_puts(&out, then_s);
                sb_puts(&out, " : ");
                sb_puts(&out, else_s);

                free(cond_s);
                free(then_s);
                free(else_s);
                free(linebuf);
                goto line_end;
            }
        }

        sb_putn(&out, linebuf, len);
        free(linebuf);

line_end:
        if (*p == '\n') {
            sb_putc(&out, '\n');
            p++;
        }
    }

    sb_putc(&out, '\0');
    return out.buf;
}

char *normalize_source(const char *src) {
    size_t n = strlen(src);
    StrBuf out = {0};

    size_t i = 0;
    while (i < n) {
        char c = src[i];

        /* keep strings unchanged */
        if (c == '"') {
            sb_putc(&out, src[i++]);
            while (i < n) {
                char d = src[i++];
                sb_putc(&out, d);
                if (d == '\\' && i < n) {
                    sb_putc(&out, src[i++]);
                    continue;
                }
                if (d == '"') break;
            }
            continue;
        }

        /* keep char literals unchanged */
        if (c == '\'') {
            sb_putc(&out, src[i++]);
            while (i < n) {
                char d = src[i++];
                sb_putc(&out, d);
                if (d == '\\' && i < n) {
                    sb_putc(&out, src[i++]);
                    continue;
                }
                if (d == '\'') break;
            }
            continue;
        }

        /* line comment */
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {
            sb_putc(&out, src[i++]);
            sb_putc(&out, src[i++]);
            while (i < n && src[i] != '\n') sb_putc(&out, src[i++]);
            continue;
        }

        /* block comment */
        if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            sb_putc(&out, src[i++]);
            sb_putc(&out, src[i++]);
            while (i + 1 < n) {
                char d = src[i++];
                sb_putc(&out, d);
                if (d == '*' && src[i] == '/') {
                    sb_putc(&out, src[i++]);
                    break;
                }
            }
            continue;
        }

        /* normalize identifier-like tokens */
        if (isalpha((unsigned char)c) || c == '_') {
            size_t st = i++;
            while (i < n && (isalnum((unsigned char)src[i]) || src[i] == '_')) i++;
            size_t len = i - st;

            /* canonical bool literals */
            if (len == 4 && memcmp(src + st, "true", 4) == 0) {
                sb_putc(&out, '1');
                continue;
            }
            if (len == 5 && memcmp(src + st, "false", 5) == 0) {
                sb_putc(&out, '0');
                continue;
            }

            /* canonical bool type alias */
            if (len == 4 && memcmp(src + st, "bool", 4) == 0) {
                sb_puts(&out, "i32");
                continue;
            }

            sb_putn(&out, src + st, len);
            continue;
        }

        sb_putc(&out, src[i++]);
    }

    sb_putc(&out, '\0');

    /* second pass for lightweight expression-shape rewrites */
    char *pass2 = normalize_if_then_else_lines(out.buf);
    free(out.buf);
    return pass2;
}
