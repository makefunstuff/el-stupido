/*  Decision manifest compiler — the new core of el-stupido.
 *
 *  Implements:
 *    1. Minimal JSON parser (no dependencies)
 *    2. Manifest struct population
 *    3. GBNF grammar for constrained LLM generation
 *    4. Deterministic expansion to el-stupido source
 *
 *  Design principle: LLMs make decisions (30-80 tokens of JSON).
 *  This code does everything else deterministically.
 */

#include "es.h"
#include "manifest.h"

/* ---- Minimal JSON parser ---- */
/* Just enough to parse manifest JSON. Not a general JSON library.
   Handles: objects, arrays, strings, integers, booleans. */

typedef struct {
    const char *src;
    int pos;
} JP;

static void jp_ws(JP *j) {
    while (j->src[j->pos] == ' ' || j->src[j->pos] == '\t' ||
           j->src[j->pos] == '\n' || j->src[j->pos] == '\r')
        j->pos++;
}

static int jp_ch(JP *j, char c) {
    jp_ws(j);
    if (j->src[j->pos] == c) { j->pos++; return 1; }
    return 0;
}

static int jp_str(JP *j, char *dst, int dsz) {
    jp_ws(j);
    if (j->src[j->pos] != '"') return -1;
    j->pos++;
    int i = 0;
    while (j->src[j->pos] && j->src[j->pos] != '"') {
        if (j->src[j->pos] == '\\' && j->src[j->pos + 1]) {
            j->pos++;
            if (i < dsz - 1) dst[i++] = j->src[j->pos];
            j->pos++;
        } else {
            if (i < dsz - 1) dst[i++] = j->src[j->pos];
            j->pos++;
        }
    }
    dst[i] = '\0';
    if (j->src[j->pos] == '"') j->pos++;
    return i;
}

static int jp_int(JP *j) {
    jp_ws(j);
    int neg = 0;
    if (j->src[j->pos] == '-') { neg = 1; j->pos++; }
    int v = 0;
    while (j->src[j->pos] >= '0' && j->src[j->pos] <= '9') {
        v = v * 10 + (j->src[j->pos] - '0');
        j->pos++;
    }
    return neg ? -v : v;
}

static int jp_bool(JP *j) {
    jp_ws(j);
    if (strncmp(j->src + j->pos, "true", 4) == 0) { j->pos += 4; return 1; }
    if (strncmp(j->src + j->pos, "false", 5) == 0) { j->pos += 5; return 0; }
    return 0;
}

/* Skip any JSON value (string, number, object, array, bool, null) */
static void jp_skip(JP *j) {
    jp_ws(j);
    char c = j->src[j->pos];
    if (c == '"') { char tmp[1024]; jp_str(j, tmp, sizeof(tmp)); }
    else if (c == '{') { j->pos++; jp_ws(j); if (j->src[j->pos] != '}') { for (;;) { char k[256]; jp_str(j, k, sizeof(k)); jp_ch(j, ':'); jp_skip(j); jp_ws(j); if (!jp_ch(j, ',')) break; } } jp_ch(j, '}'); }
    else if (c == '[') { j->pos++; jp_ws(j); if (j->src[j->pos] != ']') { for (;;) { jp_skip(j); jp_ws(j); if (!jp_ch(j, ',')) break; } } jp_ch(j, ']'); }
    else if (c == 't' || c == 'f') jp_bool(j);
    else if (c == 'n') j->pos += 4; /* null */
    else jp_int(j); /* number */
}

/* ---- Parse field type string ---- */
static MfFieldType parse_field_type(const char *s) {
    if (strcmp(s, "int") == 0) return MF_INT;
    if (strcmp(s, "bool") == 0) return MF_BOOL;
    if (strcmp(s, "text") == 0) return MF_TEXT;
    return MF_STRING;
}

/* ---- Parse a single field object ---- */
static int parse_field(JP *j, MfField *f) {
    if (!jp_ch(j, '{')) return -1;
    f->required = 0;
    jp_ws(j);
    while (j->src[j->pos] != '}' && j->src[j->pos]) {
        char key[64];
        jp_str(j, key, sizeof(key));
        jp_ch(j, ':');
        if (strcmp(key, "name") == 0) jp_str(j, f->name, sizeof(f->name));
        else if (strcmp(key, "type") == 0) { char t[32]; jp_str(j, t, sizeof(t)); f->type = parse_field_type(t); }
        else if (strcmp(key, "required") == 0) f->required = jp_bool(j);
        else jp_skip(j);
        jp_ch(j, ',');
        jp_ws(j);
    }
    jp_ch(j, '}');
    return 0;
}

/* ---- Parse a model object ---- */
static int parse_model(JP *j, MfModel *m) {
    if (!jp_ch(j, '{')) return -1;
    m->field_count = 0;
    jp_ws(j);
    while (j->src[j->pos] != '}' && j->src[j->pos]) {
        char key[64];
        jp_str(j, key, sizeof(key));
        jp_ch(j, ':');
        if (strcmp(key, "name") == 0) jp_str(j, m->name, sizeof(m->name));
        else if (strcmp(key, "fields") == 0) {
            jp_ch(j, '[');
            jp_ws(j);
            while (j->src[j->pos] != ']' && j->src[j->pos]) {
                if (m->field_count < MF_MAX_FIELDS)
                    parse_field(j, &m->fields[m->field_count++]);
                else jp_skip(j);
                jp_ch(j, ',');
                jp_ws(j);
            }
            jp_ch(j, ']');
        }
        else jp_skip(j);
        jp_ch(j, ',');
        jp_ws(j);
    }
    jp_ch(j, '}');
    return 0;
}

/* ---- Parse a route object ---- */
static int parse_route(JP *j, MfRoute *r) {
    if (!jp_ch(j, '{')) return -1;
    r->method = MF_GET;
    r->action = MF_LIST;
    r->model[0] = '\0';
    r->body[0] = '\0';
    jp_ws(j);
    while (j->src[j->pos] != '}' && j->src[j->pos]) {
        char key[64];
        jp_str(j, key, sizeof(key));
        jp_ch(j, ':');
        if (strcmp(key, "method") == 0) {
            char m[16]; jp_str(j, m, sizeof(m));
            if (strcmp(m, "POST") == 0) r->method = MF_POST;
            else if (strcmp(m, "DELETE") == 0) r->method = MF_DELETE;
        }
        else if (strcmp(key, "path") == 0) jp_str(j, r->path, sizeof(r->path));
        else if (strcmp(key, "action") == 0) {
            char a[16]; jp_str(j, a, sizeof(a));
            if (strcmp(a, "create") == 0) r->action = MF_CREATE;
            else if (strcmp(a, "delete") == 0) r->action = MF_DEL;
            else if (strcmp(a, "static") == 0) r->action = MF_STATIC;
            else if (strcmp(a, "health") == 0) r->action = MF_HEALTH;
        }
        else if (strcmp(key, "model") == 0) jp_str(j, r->model, sizeof(r->model));
        else if (strcmp(key, "body") == 0) jp_str(j, r->body, sizeof(r->body));
        else jp_skip(j);
        jp_ch(j, ',');
        jp_ws(j);
    }
    jp_ch(j, '}');
    return 0;
}

/* ---- Parse manifest JSON ---- */
int manifest_parse(const char *json, Manifest *mf) {
    memset(mf, 0, sizeof(*mf));
    JP j = { json, 0 };

    if (!jp_ch(&j, '{')) { fprintf(stderr, "manifest: expected '{'\n"); return -1; }

    jp_ws(&j);
    while (j.src[j.pos] != '}' && j.src[j.pos]) {
        char key[64];
        jp_str(&j, key, sizeof(key));
        jp_ch(&j, ':');

        if (strcmp(key, "domain") == 0) {
            char d[16]; jp_str(&j, d, sizeof(d));
            if (strcmp(d, "crud") == 0) mf->domain = MF_CRUD;
            else if (strcmp(d, "rest") == 0) mf->domain = MF_REST;
            else if (strcmp(d, "cli") == 0) mf->domain = MF_CLI;
            else if (strcmp(d, "test") == 0) mf->domain = MF_TEST;
            else { fprintf(stderr, "manifest: unknown domain '%s'\n", d); return -1; }
        }
        else if (strcmp(key, "app") == 0) {
            jp_ch(&j, '{');
            jp_ws(&j);
            while (j.src[j.pos] != '}' && j.src[j.pos]) {
                char ak[64]; jp_str(&j, ak, sizeof(ak)); jp_ch(&j, ':');
                if (strcmp(ak, "name") == 0) jp_str(&j, mf->app_name, sizeof(mf->app_name));
                else if (strcmp(ak, "port") == 0) mf->port = jp_int(&j);
                else jp_skip(&j);
                jp_ch(&j, ','); jp_ws(&j);
            }
            jp_ch(&j, '}');
        }
        else if (strcmp(key, "models") == 0) {
            jp_ch(&j, '['); jp_ws(&j);
            while (j.src[j.pos] != ']' && j.src[j.pos]) {
                if (mf->model_count < MF_MAX_MODELS)
                    parse_model(&j, &mf->models[mf->model_count++]);
                else jp_skip(&j);
                jp_ch(&j, ','); jp_ws(&j);
            }
            jp_ch(&j, ']');
        }
        else if (strcmp(key, "routes") == 0) {
            jp_ch(&j, '['); jp_ws(&j);
            while (j.src[j.pos] != ']' && j.src[j.pos]) {
                if (mf->route_count < MF_MAX_ROUTES)
                    parse_route(&j, &mf->routes[mf->route_count++]);
                else jp_skip(&j);
                jp_ch(&j, ','); jp_ws(&j);
            }
            jp_ch(&j, ']');
        }
        else if (strcmp(key, "flags") == 0) {
            jp_ch(&j, '['); jp_ws(&j);
            while (j.src[j.pos] != ']' && j.src[j.pos]) {
                if (mf->flag_count < MF_MAX_FLAGS) {
                    MfFlag *f = &mf->flags[mf->flag_count++];
                    jp_ch(&j, '{'); jp_ws(&j);
                    while (j.src[j.pos] != '}' && j.src[j.pos]) {
                        char fk[64]; jp_str(&j, fk, sizeof(fk)); jp_ch(&j, ':');
                        if (strcmp(fk, "name") == 0) jp_str(&j, f->name, sizeof(f->name));
                        else if (strcmp(fk, "short") == 0) jp_str(&j, f->shortf, sizeof(f->shortf));
                        else if (strcmp(fk, "help") == 0) jp_str(&j, f->help, sizeof(f->help));
                        else jp_skip(&j);
                        jp_ch(&j, ','); jp_ws(&j);
                    }
                    jp_ch(&j, '}');
                } else jp_skip(&j);
                jp_ch(&j, ','); jp_ws(&j);
            }
            jp_ch(&j, ']');
        }
        else if (strcmp(key, "args") == 0) {
            jp_ch(&j, '['); jp_ws(&j);
            while (j.src[j.pos] != ']' && j.src[j.pos]) {
                if (mf->arg_count < MF_MAX_ARGS) {
                    MfArg *a = &mf->args[mf->arg_count++];
                    jp_ch(&j, '{'); jp_ws(&j);
                    while (j.src[j.pos] != '}' && j.src[j.pos]) {
                        char ak[64]; jp_str(&j, ak, sizeof(ak)); jp_ch(&j, ':');
                        if (strcmp(ak, "name") == 0) jp_str(&j, a->name, sizeof(a->name));
                        else if (strcmp(ak, "help") == 0) jp_str(&j, a->help, sizeof(a->help));
                        else jp_skip(&j);
                        jp_ch(&j, ','); jp_ws(&j);
                    }
                    jp_ch(&j, '}');
                } else jp_skip(&j);
                jp_ch(&j, ','); jp_ws(&j);
            }
            jp_ch(&j, ']');
        }
        else if (strcmp(key, "tests") == 0) {
            jp_ch(&j, '['); jp_ws(&j);
            while (j.src[j.pos] != ']' && j.src[j.pos]) {
                if (mf->test_count < MF_MAX_TESTS) {
                    MfTest *t = &mf->tests[mf->test_count++];
                    t->assert_count = 0;
                    jp_ch(&j, '{'); jp_ws(&j);
                    while (j.src[j.pos] != '}' && j.src[j.pos]) {
                        char tk[64]; jp_str(&j, tk, sizeof(tk)); jp_ch(&j, ':');
                        if (strcmp(tk, "name") == 0) jp_str(&j, t->name, sizeof(t->name));
                        else if (strcmp(tk, "assertions") == 0) {
                            jp_ch(&j, '['); jp_ws(&j);
                            while (j.src[j.pos] != ']' && j.src[j.pos]) {
                                if (t->assert_count < MF_MAX_ASSERTS)
                                    jp_str(&j, t->assertions[t->assert_count++], 256);
                                else { char tmp[256]; jp_str(&j, tmp, sizeof(tmp)); }
                                jp_ch(&j, ','); jp_ws(&j);
                            }
                            jp_ch(&j, ']');
                        }
                        else jp_skip(&j);
                        jp_ch(&j, ','); jp_ws(&j);
                    }
                    jp_ch(&j, '}');
                } else jp_skip(&j);
                jp_ch(&j, ','); jp_ws(&j);
            }
            jp_ch(&j, ']');
        }
        else jp_skip(&j);

        jp_ch(&j, ',');
        jp_ws(&j);
    }
    jp_ch(&j, '}');
    return 0;
}

/* ---- Buffer for code generation ---- */
typedef struct { char *d; int n, c; } Buf;
static void bc(Buf *b, char ch) {
    if (b->n >= b->c) { b->c = b->c ? b->c * 2 : 4096; b->d = realloc(b->d, b->c); }
    b->d[b->n++] = ch;
}
static void bw(Buf *b, const char *s) { while (*s) bc(b, *s++); }
static char *bz(Buf *b) { bc(b, '\0'); return b->d; }
static void bf(Buf *b, const char *fmt, ...) {
    char tmp[4096]; va_list ap; va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    bw(b, tmp);
}
/* Write escaped string into a code string literal context */
static void besc(Buf *b, const char *s) {
    for (; *s; s++) {
        if (*s == '"') bw(b, "\\\"");
        else if (*s == '\\') bw(b, "\\\\");
        else if (*s == '\n') bw(b, "\\n");
        else bc(b, *s);
    }
}

/* ---- Default dark theme CSS (shared with old codebook) ---- */
static const char *THEME_CSS =
    "*{margin:0;padding:0;box-sizing:border-box}"
    "body{background:#1a1a2e;color:#e0e0e0;font:16px/1.6 monospace;padding:2em;max-width:720px;margin:auto}"
    "h1{color:#e94560;margin-bottom:.5em}"
    "h2{color:#e94560;margin:1em 0 .5em;font-size:1.1em}"
    ".card{background:#16213e;padding:1em;margin:.5em 0;border-radius:8px;border-left:3px solid #e94560}"
    ".card b{color:#e94560}"
    "form{background:#16213e;padding:1em;border-radius:8px;margin-bottom:1em}"
    "input,textarea,select{width:100%;padding:.5em;margin:.3em 0;background:#0f3460;color:#e0e0e0;border:1px solid #e94560;border-radius:4px;font:inherit}"
    "button{background:#e94560;color:#fff;border:0;padding:.5em 1.5em;border-radius:4px;cursor:pointer;font:inherit;margin-top:.5em}"
    ".del{background:#333;font-size:.8em;margin-left:.5em}"
    "nav{margin-bottom:1em}nav a{color:#e94560;margin-right:1em;text-decoration:none}"
    "nav a:hover{text-decoration:underline}";

/* ---- find model by name ---- */
static const MfModel *find_model(const Manifest *mf, const char *name) {
    for (int i = 0; i < mf->model_count; i++)
        if (strcmp(mf->models[i].name, name) == 0) return &mf->models[i];
    return NULL;
}

/* ---- CRUD domain expansion ---- */
static void expand_crud(const Manifest *mf, Buf *out) {
    bw(out, "\nuse http\nuse grug\nuse str\n\n");

    /* ---- generate per-model handlers ---- */
    for (int mi = 0; mi < mf->model_count; mi++) {
        const MfModel *m = &mf->models[mi];

        /* list page handler: GET /<model>s */
        bf(out, "fn __%s_list(fd: i32, req: *u8) {\n", m->name);
        bw(out, "  http_resp(fd, 200, \"text/html\")\n");
        bf(out, "  http_send(fd, \"<!DOCTYPE html><html><head><meta charset=utf-8><title>%s</title>"
                "<style>%s</style></head><body>\")\n", mf->app_name, THEME_CSS);

        /* nav bar */
        bf(out, "  http_send(fd, \"<nav>");
        for (int ni = 0; ni < mf->model_count; ni++)
            bf(out, "<a href='/%ss'>%ss</a>", mf->models[ni].name, mf->models[ni].name);
        bf(out, "</nav>\")\n");

        bf(out, "  http_send(fd, \"<h1>%ss</h1>\")\n", m->name);

        /* create form */
        bf(out, "  http_send(fd, \"<form method=POST action='/%ss'>\")\n", m->name);
        for (int fi = 0; fi < m->field_count; fi++) {
            const MfField *f = &m->fields[fi];
            if (f->type == MF_TEXT)
                bf(out, "  http_send(fd, \"<textarea name='%s' placeholder='%s'%s></textarea>\")\n",
                   f->name, f->name, f->required ? " required" : "");
            else
                bf(out, "  http_send(fd, \"<input name='%s' placeholder='%s'%s>\")\n",
                   f->name, f->name, f->required ? " required" : "");
        }
        bf(out, "  http_send(fd, \"<button>add %s</button></form>\")\n", m->name);

        /* list existing entries */
        bf(out, "  __g := grug_parse(\"%s.grug\")\n", m->name);
        bw(out, "  if __g as i64 != 0 {\n"
                "    __s := __g.sec\n"
                "    wh __s as i64 != 0 {\n"
                "      http_send(fd, \"<div class='card'>\")\n");
        for (int fi = 0; fi < m->field_count; fi++) {
            const MfField *f = &m->fields[fi];
            bf(out, "      __fv := fval(__s, \"%s\")\n", f->name);
            if (fi == 0)
                bw(out, "      if __fv as i64 != 0 { http_send(fd, \"<b>\"); http_hesc(fd, __fv); http_send(fd, \"</b>\") }\n");
            else
                bw(out, "      if __fv as i64 != 0 { http_send(fd, \" \"); http_hesc(fd, __fv) }\n");
        }
        /* delete button */
        bw(out, "      __sn := __s.nm\n");
        bf(out, "      s := str_new()\n"
                "      str_add(s, \"<form method=POST action='/%ss/delete' style='display:inline'>\")\n", m->name);
        bw(out, "      str_add(s, \"<input type=hidden name=id value='\")\n"
                "      str_add(s, __sn)\n"
                "      str_add(s, \"'><button class='del'>x</button></form>\")\n"
                "      http_send(fd, str_get(s))\n"
                "      str_fr(s)\n");
        bw(out, "      http_send(fd, \"</div>\")\n"
                "      __s = __s.nx\n"
                "    }\n"
                "    grug_fr(__g)\n"
                "  }\n");
        bw(out, "  http_send(fd, \"</body></html>\")\n"
                "}\n\n");

        /* create handler: POST /<model>s */
        bf(out, "fn __%s_create(fd: i32, req: *u8) {\n", m->name);
        bw(out, "  body := http_body(req)\n"
                "  if body as i64 == 0 { http_resp(fd, 400, \"text/plain\"); http_send(fd, \"no body\"); ret }\n");
        bf(out, "  __g := grug_parse(\"%s.grug\")\n", m->name);
        bw(out, "  if __g as i64 == 0 { __g = nw Grug; __g.sec = null as *Sec; __g.buf = null as *u8 }\n"
                "  __sid:[40]u8; sprintf(&__sid, \"e_%d\", getpid())\n"
                "  __sp := &__sid as *u8\n");
        for (int fi = 0; fi < m->field_count; fi++) {
            bf(out, "  __%s_val:[512]u8; http_fval(body, \"%s\", &__%s_val, 512)\n",
               m->fields[fi].name, m->fields[fi].name, m->fields[fi].name);
            bf(out, "  grug_set(__g, __sp, \"%s\", &__%s_val)\n",
               m->fields[fi].name, m->fields[fi].name);
        }
        bf(out, "  grug_write(__g, \"%s.grug\"); grug_fr(__g)\n", m->name);
        bf(out, "  http_resp(fd, 302, \"text/plain\")\n"
                "  http_send(fd, \"HTTP/1.1 302\\r\\nLocation: /%ss\\r\\n\\r\\n\")\n", m->name);
        bw(out, "}\n\n");

        /* delete handler: POST /<model>s/delete */
        /* grug_del(g, sec, key) deletes a key. To delete a whole section,
           we rebuild the grug file without the matching section. */
        bf(out, "fn __%s_delete(fd: i32, req: *u8) {\n", m->name);
        bw(out, "  body := http_body(req)\n"
                "  if body as i64 == 0 { http_resp(fd, 400, \"text/plain\"); http_send(fd, \"no body\"); ret }\n"
                "  __id:[128]u8; http_fval(body, \"id\", &__id, 128)\n");
        bf(out, "  __g := grug_parse(\"%s.grug\")\n", m->name);
        bw(out, "  if __g as i64 != 0 {\n"
                "    __prev: *Sec = null as *Sec\n"
                "    __cur := __g.sec\n"
                "    wh __cur as i64 != 0 {\n"
                "      if strcmp(__cur.nm, &__id) == 0 {\n"
                "        if __prev as i64 == 0 { __g.sec = __cur.nx }\n"
                "        el { __prev.nx = __cur.nx }\n"
                "      }\n"
                "      el { __prev = __cur }\n"
                "      __cur = __cur.nx\n"
                "    }\n");
        bf(out, "    grug_write(__g, \"%s.grug\"); grug_fr(__g)\n", m->name);
        bw(out, "  }\n");
        bf(out, "  http_resp(fd, 302, \"text/plain\")\n"
                "  http_send(fd, \"HTTP/1.1 302\\r\\nLocation: /%ss\\r\\n\\r\\n\")\n", m->name);
        bw(out, "}\n\n");
    }

    /* ---- serve function with route dispatch ---- */
    bw(out, "fn __cb_serve(fd: i32) {\n"
            "  buf:[8192]u8; n := read(fd, &buf, 8191) as i32\n"
            "  if n <= 0 { close(fd); ret }\n"
            "  *((&buf) as *u8 + n) = 0; req := &buf as *u8\n"
            "  path:[256]u8; http_path(req, &path, 256)\n");

    int first = 1;

    /* root redirect to first model */
    if (mf->model_count > 0) {
        bf(out, "  if strcmp(&path, \"/\") == 0 {\n"
                "    http_resp(fd, 302, \"text/plain\")\n"
                "    http_send(fd, \"HTTP/1.1 302\\r\\nLocation: /%ss\\r\\n\\r\\n\")\n"
                "  }\n", mf->models[0].name);
        first = 0;
    }

    /* per-model routes */
    for (int mi = 0; mi < mf->model_count; mi++) {
        const MfModel *m = &mf->models[mi];

        /* GET /<model>s */
        bf(out, "  %s http_isget(req) != 0 && strcmp(&path, \"/%ss\") == 0 {\n",
           first ? "if" : "el if", m->name);
        bf(out, "    __%s_list(fd, req)\n  }\n", m->name);
        first = 0;

        /* POST /<model>s */
        bf(out, "  el if http_ispost(req) != 0 && strcmp(&path, \"/%ss\") == 0 {\n", m->name);
        bf(out, "    __%s_create(fd, req)\n  }\n", m->name);

        /* POST /<model>s/delete */
        bf(out, "  el if http_ispost(req) != 0 && strcmp(&path, \"/%ss/delete\") == 0 {\n", m->name);
        bf(out, "    __%s_delete(fd, req)\n  }\n", m->name);
    }

    /* any explicit routes from manifest */
    for (int ri = 0; ri < mf->route_count; ri++) {
        const MfRoute *r = &mf->routes[ri];
        const char *mcheck = r->method == MF_POST ? "http_ispost(req) != 0 && " : "http_isget(req) != 0 && ";
        bf(out, "  el if %sstrcmp(&path, \"%s\") == 0 {\n", mcheck, r->path);
        if (r->action == MF_HEALTH || r->action == MF_STATIC) {
            bw(out, "    http_resp(fd, 200, \"application/json\")\n");
            bw(out, "    http_send(fd, \"");
            besc(out, r->body[0] ? r->body : "{\"ok\":true}");
            bw(out, "\")\n");
        }
        bw(out, "  }\n");
    }

    /* 404 */
    bw(out, "  el { http_resp(fd, 404, \"text/html\"); http_send(fd, \"not found\") }\n"
            "  close(fd)\n}\n\n");

    /* ---- main ---- */
    bw(out, "ext signal(i32, *void) -> *void\n"
            "fn main() -> i32 {\n"
            "  signal(17, 1 as *void)\n");
    bf(out, "  sfd := http_listen(%d)\n", mf->port > 0 ? mf->port : 8080);
    bw(out, "  if sfd < 0 { printf(\"listen failed\\n\"); ret 1 }\n");
    bf(out, "  printf(\":%d\\n\")\n", mf->port > 0 ? mf->port : 8080);
    bw(out, "  wh 1 { cfd := accept(sfd, null, null); if cfd < 0 { ret 1 }\n"
            "    pid := fork(); if pid == 0 { close(sfd); __cb_serve(cfd); exit(0) }; close(cfd) }\n"
            "}\n");
}

/* ---- REST domain expansion ---- */
static void expand_rest(const Manifest *mf, Buf *out) {
    bw(out, "\nuse http\nuse grug\nuse str\n\n");

    /* JSON escape helper */
    bw(out,
        "fn __jesc(s: *Str, v: *u8) {\n"
        "  i := 0\n"
        "  wh *(v+i) != 0 {\n"
        "    c := *(v+i) as i32\n"
        "    if c == 34 { str_add(s, \"\\\\\\\"\" as *u8) }\n"
        "    el if c == 92 { str_add(s, \"\\\\\\\\\" as *u8) }\n"
        "    el if c == 10 { str_add(s, \"\\\\n\" as *u8) }\n"
        "    el { str_addc(s, c) }\n"
        "    i += 1\n"
        "  }\n"
        "}\n\n");

    /* JSON field extractor */
    bw(out,
        "fn __jfind(body: *u8, key: *u8, dst: *u8, dsz: i32) {\n"
        "  *dst = 0\n"
        "  kl := strlen(key) as i32\n"
        "  p := body\n"
        "  wh *p != 0 {\n"
        "    if *p == 34 {\n"
        "      p = p + 1\n"
        "      if strncmp(p, key, kl as u64) == 0 && *(p+kl) == 34 {\n"
        "        p = p + kl + 1\n"
        "        wh *p == 32 || *p == 58 { p = p + 1 }\n"
        "        if *p == 34 {\n"
        "          p = p + 1; o := 0\n"
        "          wh *p != 0 && *p != 34 && o < dsz - 1 {\n"
        "            if *p == 92 && *(p+1) != 0 { p = p + 1 }\n"
        "            *(dst+o) = *p; o += 1; p = p + 1\n"
        "          }\n"
        "          *(dst+o) = 0; ret\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "    p = p + 1\n"
        "  }\n"
        "}\n\n");

    /* list/create handlers for each route */
    for (int ri = 0; ri < mf->route_count; ri++) {
        const MfRoute *r = &mf->routes[ri];
        const MfModel *m = find_model(mf, r->model);

        if (r->action == MF_LIST && m) {
            bf(out, "fn __rest_list_%s(fd: i32, req: *u8) {\n", m->name);
            bw(out, "  http_resp(fd, 200, \"application/json\")\n"
                    "  s := str_new()\n"
                    "  str_add(s, \"[\")\n");
            bf(out, "  __g := grug_parse(\"%s.grug\")\n", m->name);
            bw(out, "  if __g as i64 != 0 {\n"
                    "    __s := __g.sec\n"
                    "    __fi := 0\n"
                    "    wh __s as i64 != 0 {\n"
                    "      if __fi > 0 { str_add(s, \",\") }\n"
                    "      str_add(s, \"{\")\n");
            for (int fi = 0; fi < m->field_count; fi++) {
                bf(out, "      __fv := fval(__s, \"%s\")\n", m->fields[fi].name);
                bf(out, "      str_add(s, \"\\\"%s\\\":\")\n", m->fields[fi].name);
                bw(out, "      if __fv as i64 != 0 { str_add(s, \"\\\"\"); __jesc(s, __fv); str_add(s, \"\\\"\") }\n"
                        "      el { str_add(s, \"null\") }\n");
                if (fi < m->field_count - 1)
                    bw(out, "      str_add(s, \",\")\n");
            }
            bw(out, "      str_add(s, \"}\")\n"
                    "      __fi += 1\n"
                    "      __s = __s.nx\n"
                    "    }\n"
                    "    grug_fr(__g)\n"
                    "  }\n"
                    "  str_add(s, \"]\")\n"
                    "  http_send(fd, str_get(s))\n"
                    "  str_fr(s)\n"
                    "}\n\n");
        }

        if (r->action == MF_CREATE && m) {
            bf(out, "fn __rest_create_%s(fd: i32, req: *u8) {\n", m->name);
            bw(out, "  body := http_body(req)\n"
                    "  if body as i64 == 0 { http_resp(fd, 400, \"application/json\"); http_send(fd, \"{\\\"error\\\":\\\"no body\\\"}\"); ret }\n");
            for (int fi = 0; fi < m->field_count; fi++)
                bf(out, "  __v%d:[512]u8; __jfind(body, \"%s\", &__v%d, 512)\n",
                   fi, m->fields[fi].name, fi);
            bf(out, "  __g := grug_parse(\"%s.grug\")\n", m->name);
            bw(out, "  if __g as i64 == 0 { __g = nw Grug; __g.sec = null as *Sec; __g.buf = null as *u8 }\n"
                    "  __sid:[40]u8; sprintf(&__sid, \"e_%d\", getpid())\n"
                    "  __sp := &__sid as *u8\n");
            for (int fi = 0; fi < m->field_count; fi++)
                bf(out, "  grug_set(__g, __sp, \"%s\", &__v%d)\n", m->fields[fi].name, fi);
            bf(out, "  grug_write(__g, \"%s.grug\"); grug_fr(__g)\n", m->name);
            bw(out, "  http_resp(fd, 201, \"application/json\")\n"
                    "  http_send(fd, \"{\\\"ok\\\":true}\")\n"
                    "}\n\n");
        }
    }

    /* serve function */
    bw(out, "fn __cb_serve(fd: i32) {\n"
            "  buf:[8192]u8; n := read(fd, &buf, 8191) as i32\n"
            "  if n <= 0 { close(fd); ret }\n"
            "  *((&buf) as *u8 + n) = 0; req := &buf as *u8\n"
            "  path:[256]u8; http_path(req, &path, 256)\n");

    int rfirst = 1;
    for (int ri = 0; ri < mf->route_count; ri++) {
        const MfRoute *r = &mf->routes[ri];
        const MfModel *m = find_model(mf, r->model);
        const char *mc = r->method == MF_POST ? "http_ispost(req) != 0 && " : "http_isget(req) != 0 && ";

        bf(out, "  %s %sstrcmp(&path, \"%s\") == 0 {\n",
           rfirst ? "if" : "el if", mc, r->path);
        rfirst = 0;

        switch (r->action) {
        case MF_LIST:
            if (m) bf(out, "    __rest_list_%s(fd, req)\n", m->name);
            break;
        case MF_CREATE:
            if (m) bf(out, "    __rest_create_%s(fd, req)\n", m->name);
            break;
        case MF_STATIC: case MF_HEALTH:
            bw(out, "    http_resp(fd, 200, \"application/json\")\n");
            bw(out, "    http_send(fd, \"");
            besc(out, r->body[0] ? r->body : "{\"ok\":true}");
            bw(out, "\")\n");
            break;
        default: break;
        }
        bw(out, "  }\n");
    }
    if (mf->route_count > 0)
        bw(out, "  el { http_resp(fd, 404, \"application/json\"); http_send(fd, \"{\\\"error\\\":\\\"not found\\\"}\") }\n");
    bw(out, "  close(fd)\n}\n\n");

    /* main */
    bw(out, "ext signal(i32, *void) -> *void\nfn main() -> i32 {\n"
            "  signal(17, 1 as *void)\n");
    bf(out, "  sfd := http_listen(%d)\n", mf->port > 0 ? mf->port : 8080);
    bw(out, "  if sfd < 0 { printf(\"listen failed\\n\"); ret 1 }\n");
    bf(out, "  printf(\":%d\\n\")\n", mf->port > 0 ? mf->port : 8080);
    bw(out, "  wh 1 { cfd := accept(sfd, null, null); if cfd < 0 { ret 1 }\n"
            "    pid := fork(); if pid == 0 { close(sfd); __cb_serve(cfd); exit(0) }; close(cfd) }\n}\n");
}

/* ---- CLI domain expansion ---- */
static void expand_cli(const Manifest *mf, Buf *out) {
    /* Cli struct */
    bw(out, "struct Cli {\n");
    for (int i = 0; i < mf->flag_count; i++)
        bf(out, "  %s: i32\n", mf->flags[i].name);
    for (int i = 0; i < mf->arg_count; i++)
        bf(out, "  %s: *u8\n", mf->args[i].name);
    bw(out, "}\n\n");

    /* help function */
    bw(out, "fn __cli_help() {\n");
    bf(out, "  printf(\"%s\\n\\n\")\n", mf->app_name);
    bf(out, "  printf(\"usage: %s", mf->app_name);
    if (mf->flag_count) bw(out, " [flags]");
    for (int i = 0; i < mf->arg_count; i++)
        bf(out, " <%s>", mf->args[i].name);
    bw(out, "\\n\\n\")\n");
    if (mf->flag_count) {
        bw(out, "  printf(\"flags:\\n\")\n");
        for (int i = 0; i < mf->flag_count; i++)
            bf(out, "  printf(\"  %s, --%-12s %s\\n\")\n",
               mf->flags[i].shortf, mf->flags[i].name, mf->flags[i].help);
        bw(out, "  printf(\"  -h, --help          show help\\n\")\n");
    }
    if (mf->arg_count) {
        bw(out, "  printf(\"\\nargs:\\n\")\n");
        for (int i = 0; i < mf->arg_count; i++)
            bf(out, "  printf(\"  %-18s %s\\n\")\n", mf->args[i].name, mf->args[i].help);
    }
    bw(out, "}\n\n");

    /* main */
    bw(out, "fn main(argc: i32, argv: **u8) -> i32 {\n"
            "  cli := nw Cli\n");
    for (int i = 0; i < mf->flag_count; i++)
        bf(out, "  cli.%s = 0\n", mf->flags[i].name);
    for (int i = 0; i < mf->arg_count; i++)
        bf(out, "  cli.%s = null as *u8\n", mf->args[i].name);
    bw(out, "  __pi := 0\n"
            "  for __i := 1..argc {\n"
            "    __a := *(argv + __i)\n");
    for (int i = 0; i < mf->flag_count; i++)
        bf(out, "    %s strcmp(__a, \"%s\") == 0 || strcmp(__a, \"--%s\") == 0 { cli.%s = 1 }\n",
           i == 0 ? "if" : "el if", mf->flags[i].shortf, mf->flags[i].name, mf->flags[i].name);
    bf(out, "    %s strcmp(__a, \"-h\") == 0 || strcmp(__a, \"--help\") == 0 { __cli_help(); exit(0) }\n",
       mf->flag_count ? "el if" : "if");
    bw(out, "    el if *(__a) == 45 { printf(\"unknown flag: %s\\n\", __a); __cli_help(); exit(1) }\n"
            "    el {\n");
    for (int i = 0; i < mf->arg_count; i++)
        bf(out, "      %s __pi == %d { cli.%s = __a }\n",
           i == 0 ? "if" : "el if", i, mf->args[i].name);
    bw(out, "      __pi += 1\n"
            "    }\n"
            "  }\n");
    for (int i = 0; i < mf->arg_count; i++)
        bf(out, "  if cli.%s as i64 == 0 { printf(\"error: missing <%s>\\n\"); __cli_help(); exit(1) }\n",
           mf->args[i].name, mf->args[i].name);
    bw(out, "  cli_main(cli)\n"
            "  free(cli as *void)\n"
            "  ret 0\n"
            "}\n");
}

/* ---- Test domain expansion ---- */
static void expand_test(const Manifest *mf, Buf *out) {
    /* test functions */
    for (int i = 0; i < mf->test_count; i++) {
        bf(out, "fn __test_%d() -> i32 {\n", i);
        for (int a = 0; a < mf->tests[i].assert_count; a++) {
            bf(out, "  if %s { }\n", mf->tests[i].assertions[a]);
            bf(out, "  el { printf(\"    FAIL: %s\\n\"); ret 1 }\n", mf->tests[i].assertions[a]);
        }
        bw(out, "  ret 0\n}\n\n");
    }

    /* main */
    bw(out, "fn main() -> i32 {\n"
            "  __pass := 0\n"
            "  __fail := 0\n");
    bf(out, "  __total := %d\n", mf->test_count);
    bw(out, "  printf(\"running %d tests...\\n\\n\", __total)\n");
    for (int i = 0; i < mf->test_count; i++) {
        bf(out, "  printf(\"  %s ... \")\n", mf->tests[i].name);
        bf(out, "  if __test_%d() == 0 { printf(\"\\x1b[32mPASS\\x1b[0m\\n\"); __pass += 1 }\n", i);
        bw(out, "  el { printf(\"\\x1b[31mFAIL\\x1b[0m\\n\"); __fail += 1 }\n");
    }
    bw(out, "  printf(\"\\n%d/%d passed\\n\", __pass, __total)\n"
            "  if __fail > 0 { ret 1 }\n"
            "  ret 0\n"
            "}\n");
}

/* ---- Main expansion dispatcher ---- */
char *manifest_expand(const Manifest *mf) {
    Buf out = {0};

    switch (mf->domain) {
    case MF_CRUD: expand_crud(mf, &out); break;
    case MF_REST: expand_rest(mf, &out); break;
    case MF_CLI:  expand_cli(mf, &out); break;
    case MF_TEST: expand_test(mf, &out); break;
    }

    return bz(&out);
}

/* ---- GBNF Grammar for constrained manifest generation ---- */
const char *manifest_grammar(void) {
    return
        "root ::= \"{\" ws domain-kv \",\" ws app-kv \",\" ws models-kv ws \"}\"\n"
        "\n"
        "domain-kv ::= '\"' \"domain\" '\"' ws \":\" ws domain-val\n"
        "domain-val ::= '\"' (\"crud\" | \"rest\" | \"cli\" | \"test\") '\"'\n"
        "\n"
        "app-kv ::= '\"' \"app\" '\"' ws \":\" ws app-obj\n"
        "app-obj ::= \"{\" ws '\"' \"name\" '\"' ws \":\" ws string \",\" ws '\"' \"port\" '\"' ws \":\" ws integer ws \"}\"\n"
        "\n"
        "models-kv ::= '\"' \"models\" '\"' ws \":\" ws \"[\" ws model (\",\" ws model)* ws \"]\"\n"
        "model ::= \"{\" ws '\"' \"name\" '\"' ws \":\" ws string \",\" ws '\"' \"fields\" '\"' ws \":\" ws \"[\" ws field (\",\" ws field)* ws \"]\" ws \"}\"\n"
        "field ::= \"{\" ws '\"' \"name\" '\"' ws \":\" ws string \",\" ws '\"' \"type\" '\"' ws \":\" ws field-type (\",\" ws '\"' \"required\" '\"' ws \":\" ws boolean)? ws \"}\"\n"
        "field-type ::= '\"' (\"string\" | \"int\" | \"bool\" | \"text\") '\"'\n"
        "\n"
        "string ::= '\"' [a-z_][a-z0-9_]* '\"'\n"
        "integer ::= [0-9]+\n"
        "boolean ::= \"true\" | \"false\"\n"
        "ws ::= [ \\t\\n]*\n";
}
