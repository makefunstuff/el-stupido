/*  Codebook expander — text-level pass between preprocess() and parser.
 *
 *  Codebooks:
 *    use web  — HTTP/WebSocket server DSL
 *    use cli  — CLI argument parser generator
 *    use rest — JSON REST API generator
 *    use test — test runner framework
 *
 *  Web directives (after use web):
 *    listen PORT                           — listen on port
 *    /PATH "text"                          — static html
 *    /PATH fn handler                      — call handler(fd,req)
 *    /PATH file "file" "ctype"             — serve file
 *    /PATH tpl "tpl.grug"                  — serve .grug template
 *    /PATH { body }                        — inline handler (fd/req in scope)
 *    POST /PATH save "store.grug" "f1,f2" /redir — save form
 *    ws /PATH echo                         — websocket echo
 *    ws /PATH { body }                     — websocket handler
 *    crud NAME field1 field2+              — full CRUD app
 *
 *  CLI directives (after use cli):
 *    name "APPNAME"                        — app name for help text
 *    desc "DESCRIPTION"                    — description for help text
 *    flag NAME -X "help"                   — boolean flag (--NAME/-X)
 *    arg NAME "help"                       — positional argument
 *
 *  REST directives (after use rest):
 *    listen PORT                           — listen on port
 *    model NAME field1 field2 ...          — define a data model
 *    GET /PATH list MODEL                  — list all MODEL entries as JSON
 *    POST /PATH create MODEL               — create MODEL from JSON body
 *    GET /PATH "text"                      — static JSON response
 *
 *  Test directives (after use test):
 *    test "NAME" { assert EXPR }           — test case with assertions
 */

#include "es.h"

#define CB_MAX_ROUTES 64

/* ---- UTF-8 helpers ---- */
static uint32_t cb_u8d(const char *s, int *bytes) {
    uint8_t c = (uint8_t)s[0];
    if (c < 0x80) { *bytes = 1; return c; }
    if ((c&0xE0)==0xC0) { *bytes=2; return ((uint32_t)(c&0x1F)<<6)|(s[1]&0x3F); }
    if ((c&0xF0)==0xE0) { *bytes=3; return ((uint32_t)(c&0x0F)<<12)|((uint32_t)(s[1]&0x3F)<<6)|(s[2]&0x3F); }
    if ((c&0xF8)==0xF0) { *bytes=4; return ((uint32_t)(c&0x07)<<18)|((uint32_t)(s[1]&0x3F)<<12)|((uint32_t)(s[2]&0x3F)<<6)|(s[3]&0x3F); }
    *bytes = 1; return c;
}
static int cb_cem(const char *s, uint32_t cp) {
    int b; if (cb_u8d(s, &b) != cp) return 0;
    int n = b; int vb; if (cb_u8d(s+n, &vb) == 0xFE0F) n += vb;
    return n;
}

/* ---- buffer ---- */
typedef struct { char *d; int n, c; } CBuf;
static void cb_bc(CBuf *b, char ch) {
    if (b->n >= b->c) { b->c = b->c ? b->c*2 : 4096; b->d = realloc(b->d, b->c); }
    b->d[b->n++] = ch;
}
static void cb_bw(CBuf *b, const char *s) { while (*s) cb_bc(b, *s++); }
static char *cb_bz(CBuf *b) { cb_bc(b, '\0'); return b->d; }
static void cb_bfmt(CBuf *b, const char *fmt, ...) {
    char tmp[4096]; va_list ap; va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap); va_end(ap);
    cb_bw(b, tmp);
}

/* ---- route types ---- */
typedef enum { RT_HTML, RT_FUNC, RT_INLINE, RT_FILE, RT_TEMPLATE, RT_SAVE, RT_WS_PAGE, RT_CRUD } RouteKind;
typedef enum { RM_GET, RM_POST } RouteMethod;

typedef struct {
    RouteKind kind;
    RouteMethod method;
    char path[128];
    char content[2048];     /* RT_HTML: text, RT_FUNC: func name, RT_FILE: file path */
    char ctype[64];         /* RT_FILE: content-type */
    char body[4096];        /* RT_INLINE: user code */
    char tpl_path[256];     /* RT_TEMPLATE: template .grug path */
    char data_path[256];    /* RT_TEMPLATE: data .grug path (optional) */
    char fields[512];       /* RT_SAVE: comma-sep field names */
    char redirect[128];     /* RT_SAVE: redirect path */
} Route;

typedef struct {
    int active, port, route_count, ws_count, needs_grug;
    Route routes[CB_MAX_ROUTES];
    struct { char path[128]; int is_echo; char body[4096]; } ws[8];
} WebCB;

/* ---- CLI codebook state ---- */
#define CLI_MAX_FLAGS 16
#define CLI_MAX_ARGS  16

typedef struct {
    int active, flag_count, arg_count;
    char app_name[128];
    char app_desc[256];
    struct { char name[64]; char shortf[4]; char help[128]; } flags[CLI_MAX_FLAGS];
    struct { char name[64]; char help[128]; } args[CLI_MAX_ARGS];
} CliCB;

/* ---- REST codebook state ---- */
#define REST_MAX_MODELS 8
#define REST_MAX_ROUTES 32
#define REST_MAX_FIELDS 16

typedef enum { REST_LIST, REST_CREATE, REST_STATIC } RestRouteKind;
typedef enum { REST_GET, REST_POST } RestMethod;

typedef struct {
    int active, port, model_count, route_count;
    struct {
        char name[64];
        char fields[REST_MAX_FIELDS][64];
        int field_count;
    } models[REST_MAX_MODELS];
    struct {
        RestRouteKind kind;
        RestMethod method;
        char path[128];
        char model[64];     /* for list/create */
        char content[256];  /* for static */
    } routes[REST_MAX_ROUTES];
} RestCB;

/* ---- Test codebook state ---- */
#define TEST_MAX_CASES 64

typedef struct {
    int active, count;
    struct { char name[128]; char body[4096]; } cases[TEST_MAX_CASES];
} TestCB;

/* ---- line/string utilities ---- */
static const char *cb_skip_ws(const char *p) { while (*p==' '||*p=='\t') p++; return p; }
static const char *cb_line_end(const char *p) { while (*p&&*p!='\n') p++; return p; }

static int cb_read_word(const char *p, char *dst, int dsz) {
    int i=0; while (i<dsz-1&&(isalnum((unsigned char)p[i])||p[i]=='_')) { dst[i]=p[i]; i++; }
    dst[i]='\0'; return i;
}
static int cb_read_path(const char *p, char *dst, int dsz) {
    int i=0; while (i<dsz-1&&(isalnum((unsigned char)p[i])||p[i]=='_'||p[i]=='/'||p[i]=='.'||p[i]=='-'))
    { dst[i]=p[i]; i++; } dst[i]='\0'; return i;
}
static int cb_read_quoted(const char *p, char *dst, int dsz) {
    if (*p!='"') return 0;
    int i=1,o=0;
    while (p[i]&&p[i]!='"'&&o<dsz-1) {
        if (p[i]=='\\'&&p[i+1]) { dst[o++]=p[i]; i++; dst[o++]=p[i]; i++; }
        else { dst[o++]=p[i]; i++; }
    } dst[o]='\0'; if (p[i]=='"') i++; return i;
}
static int cb_read_block(const char *p, char *dst, int dsz) {
    if (*p!='{') return 0;
    int depth=1,i=1,o=0;
    while (p[i]&&depth>0&&o<dsz-2) {
        if (p[i]=='{') depth++; else if (p[i]=='}') { if (--depth==0) { i++; break; } }
        if (p[i]=='"') { dst[o++]=p[i++]; while (p[i]&&p[i]!='"') { if (p[i]=='\\'&&p[i+1]) dst[o++]=p[i++]; dst[o++]=p[i++]; } if (p[i]=='"') dst[o++]=p[i++]; continue; }
        dst[o++]=p[i++];
    } dst[o]='\0'; return i;
}

/* ---- write string literal with escaping for generated code ---- */
static void cb_str_lit(CBuf *b, const char *s, int len) {
    cb_bw(b, "\"");
    for (int i=0; i<len; i++) {
        if (s[i]=='"') cb_bw(b, "\\\"");
        else if (s[i]=='\\') cb_bw(b, "\\\\");
        else if (s[i]=='\n') cb_bw(b, "\\n");
        else if (s[i]=='\r') cb_bw(b, "\\r");
        else cb_bc(b, s[i]);
    }
    cb_bw(b, "\"");
}

/* ---- compile-time .grug template reader ---- */
/* New format: 📂 section / key = value / # comment */
typedef struct { char name[64]; char html[16384]; } TplSec;
typedef struct { TplSec secs[16]; int count; } GrugTpl;

static int read_grug_tpl(const char *path, GrugTpl *tpl) {
    FILE *f = fopen(path, "r");
    if (!f) {
        /* try relative to project root */
        char alt[512]; snprintf(alt, sizeof(alt), "/home/jurip/Vibes/el-stupido/%s", path);
        f = fopen(alt, "r");
    }
    if (!f) { fprintf(stderr, "codebook: cannot read template '%s'\n", path); return -1; }
    tpl->count = 0; char line[16384]; int cur = -1;
    while (fgets(line, sizeof(line), f)) {
        const char *p = line; while (*p==' '||*p=='\t') p++;
        if (*p=='#'||*p=='\n'||*p=='\r'||*p=='\0') continue;
        /* 📂 section (U+1F4C2) */
        int cl = cb_cem(p, 0x1F4C2);
        if (cl) {
            const char *ns = p+cl; while (*ns==' '||*ns=='\t') ns++;
            cur = tpl->count++; int i=0;
            while (*ns&&*ns!='\n'&&*ns!='\r'&&*ns!=' '&&*ns!='\t'&&i<63) { tpl->secs[cur].name[i++]=*ns++; }
            tpl->secs[cur].name[i]='\0'; tpl->secs[cur].html[0]='\0';
            continue;
        }
        /* key = value — look for html key */
        if (cur >= 0) {
            const char *eq = p; while (*eq&&*eq!='\n'&&*eq!='=') eq++;
            if (*eq == '=') {
                /* extract key (trim) */
                const char *ks=p; const char *ke=eq;
                while (ke>ks&&(*(ke-1)==' '||*(ke-1)=='\t')) ke--;
                int klen = (int)(ke-ks);
                /* accept any key (html, title, js, css) — store value in .html */
                (void)klen;
                {
                    const char *vs = eq+1; while (*vs==' '||*vs=='\t') vs++;
                    int vlen = (int)strlen(vs);
                    while (vlen>0&&(vs[vlen-1]=='\n'||vs[vlen-1]=='\r')) vlen--;
                    if (vlen >= (int)sizeof(tpl->secs[cur].html)) vlen = (int)sizeof(tpl->secs[cur].html)-1;
                    memcpy(tpl->secs[cur].html, vs, vlen);
                    tpl->secs[cur].html[vlen] = '\0';
                }
            }
        }
    }
    fclose(f); return 0;
}
static const char *tpl_get(GrugTpl *t, const char *name) {
    for (int i=0; i<t->count; i++) if (strcmp(t->secs[i].name, name)==0 && t->secs[i].html[0]) return t->secs[i].html;
    return NULL;
}

/* ---- generate code for {{field}} template rendering ---- */
static void gen_tpl_send(CBuf *out, const char *html, const char *indent) {
    /* simple html with no {{}} — just send it */
    cb_bfmt(out, "%shttp_send(fd, ", indent);
    cb_str_lit(out, html, (int)strlen(html));
    cb_bw(out, ")\n");
}
static void gen_each_render(CBuf *out, const char *tmpl) {
    /* walk template, split at {{field}}, emit http_send/http_hesc */
    const char *p = tmpl;
    while (*p) {
        const char *m = strstr(p, "{{");
        if (!m) { if (*p) { cb_bw(out, "        http_send(fd, "); cb_str_lit(out, p, (int)strlen(p)); cb_bw(out, ")\n"); } break; }
        int lit = (int)(m-p);
        if (lit>0) { cb_bw(out, "        http_send(fd, "); cb_str_lit(out, p, lit); cb_bw(out, ")\n"); }
        const char *end = strstr(m+2, "}}");
        if (!end) { cb_bw(out, "        http_send(fd, "); cb_str_lit(out, m, (int)strlen(m)); cb_bw(out, ")\n"); break; }
        char field[128]; int fl=(int)(end-m-2); if (fl>127) fl=127;
        memcpy(field, m+2, fl); field[fl]='\0';
        cb_bfmt(out, "        __tf := fval(__s, \"%s\")\n", field);
        cb_bw(out,    "        if __tf as i64 != 0 { http_hesc(fd, __tf) }\n");
        p = end+2;
    }
}

/* ---- emoji codepoints ---- */
#define CP_USE      0x1F4E5  /* 📥 */
#define CP_LISTEN   0x1F310  /* 🌐 */
#define CP_ROUTE    0x1F4CD  /* 📍 */
#define CP_HTML     0x1F4C4  /* 📄 */
#define CP_HANDLER  0x1F4CA  /* 📊 */
#define CP_WS       0x1F4E1  /* 📡 */
#define CP_ECHO     0x1F501  /* 🔁 */
#define CP_FILE     0x1F4C1  /* 📁 */
#define CP_TEMPLATE 0x1F3A8  /* 🎨 */
#define CP_SAVE     0x1F4BE  /* 💾 */

/* ---- default dark theme CSS ---- */
static const char *CB_THEME =
    "*{margin:0;padding:0;box-sizing:border-box}"
    "body{background:#1a1a2e;color:#e0e0e0;font:16px/1.6 monospace;padding:2em;max-width:640px;margin:auto}"
    "h1{color:#e94560;margin-bottom:.5em}"
    ".card{background:#16213e;padding:1em;margin:.5em 0;border-radius:8px;border-left:3px solid #e94560}"
    ".card b{color:#e94560}"
    "form{background:#16213e;padding:1em;border-radius:8px;margin-bottom:1em}"
    "input,textarea{width:100%;padding:.5em;margin:.3em 0;background:#0f3460;color:#e0e0e0;border:1px solid #e94560;border-radius:4px;font:inherit}"
    "button{background:#e94560;color:#fff;border:0;padding:.5em 1.5em;border-radius:4px;cursor:pointer;font:inherit;margin-top:.5em}"
    "#log{background:#16213e;padding:1em;border-radius:8px;height:300px;overflow-y:auto;margin-bottom:1em}"
    ".s{color:#0f0}.r{color:#e94560}";

/* ---- grugscript: short JS helpers auto-prepended to <script> ---- */
static const char *GS_PREAMBLE =
    "function ge(i){return document.getElementById(i)}"
    "function ce(t){return document.createElement(t)}"
    "function qs(s){return document.querySelector(s)}"
    "function qa(s){return document.querySelectorAll(s)}"
    "function ws(p){return new WebSocket('ws://'+location.host+p)}";

/* ---- built-in WS client page JS ---- */
static const char *CB_WS_JS =
    "let _w,_l=ge('log'),_i=ge('msg');"
    "function _a(c,t){let d=ce('div');d.className=c;d.textContent=t;_l.appendChild(d);_l.scrollTop=_l.scrollHeight}"
    "function _c(p){_w=ws(p);_w.onopen=()=>_a('s','connected');_w.onmessage=e=>_a('r',e.data);"
    "_w.onclose=()=>{_a('s','disconnected');setTimeout(()=>_c(p),2000)}}"
    "function snd(){if(_i.value){_w.send(_i.value);_a('s','> '+_i.value);_i.value=''}}"
    "_i.onkeydown=e=>{if(e.key==='Enter')snd()};";

/* ---- ASCII keyword helpers ---- */
static int cb_kw(const char *p, const char *kw) {
    int n = (int)strlen(kw);
    if (strncmp(p,kw,n)!=0) return 0;
    char c = p[n]; if (c&&c!=' '&&c!='\t'&&c!='\n'&&c!='\r') return 0;
    return n;
}

/* ---- parse route sub-type (shared by emoji and ASCII triggers) ---- */
static const char *parse_route_type(const char *a, Route *r, int *needs_grug, CBuf *passthru) {
    int ecl;
    /* emoji sub-types (backward compat) */
    if ((ecl=cb_cem(a,CP_HTML))>0) { r->kind=RT_HTML; a=cb_skip_ws(a+ecl); cb_read_quoted(a,r->content,sizeof(r->content)); }
    else if ((ecl=cb_cem(a,CP_HANDLER))>0) { r->kind=RT_FUNC; a=cb_skip_ws(a+ecl); cb_read_word(a,r->content,sizeof(r->content)); }
    else if ((ecl=cb_cem(a,CP_FILE))>0) { r->kind=RT_FILE; a=cb_skip_ws(a+ecl); int ql=cb_read_quoted(a,r->content,sizeof(r->content)); a=cb_skip_ws(a+ql); cb_read_quoted(a,r->ctype,sizeof(r->ctype)); }
    else if ((ecl=cb_cem(a,CP_TEMPLATE))>0) {
        r->kind=RT_TEMPLATE; a=cb_skip_ws(a+ecl);
        int ql=cb_read_quoted(a,r->tpl_path,sizeof(r->tpl_path)); a=cb_skip_ws(a+ql);
        if (*a=='"') cb_read_quoted(a,r->data_path,sizeof(r->data_path));
        if (r->data_path[0]) *needs_grug=1;
    }
    else if ((ecl=cb_cem(a,CP_SAVE))>0) {
        r->kind=RT_SAVE; a=cb_skip_ws(a+ecl);
        int ql=cb_read_quoted(a,r->content,sizeof(r->content)); a=cb_skip_ws(a+ql);
        ql=cb_read_quoted(a,r->fields,sizeof(r->fields)); a=cb_skip_ws(a+ql);
        cb_read_path(a,r->redirect,sizeof(r->redirect));
        if (!r->redirect[0]) strcpy(r->redirect,"/");
        *needs_grug=1;
    }
    else if ((ecl=cb_cem(a,CP_WS))>0) {
        r->kind=RT_WS_PAGE; a=cb_skip_ws(a+ecl);
        cb_read_path(a,r->content,sizeof(r->content));
    }
    /* ASCII sub-types */
    else if ((ecl=cb_kw(a,"fn"))>0) {
        r->kind=RT_FUNC; a=cb_skip_ws(a+ecl);
        int nl=cb_read_word(a,r->content,sizeof(r->content)); a=cb_skip_ws(a+nl);
        /* If ( or { follows, LLM wrote inline function def — extract it as passthrough */
        if ((*a=='(' || *a=='{') && passthru) {
            cb_bw(passthru, "fn "); cb_bw(passthru, r->content);
            if (*a=='(') {
                /* copy params: fn name(params) */
                cb_bc(passthru, '(');
                a++; int depth=1;
                while (*a && depth>0) {
                    if (*a=='(') depth++;
                    else if (*a==')') { if (--depth==0) { a++; break; } }
                    cb_bc(passthru, *a++);
                }
                cb_bc(passthru, ')');
                a=cb_skip_ws(a);
            } else {
                /* no params — use default (fd: i32, body: *u8) */
                cb_bw(passthru, "(fd: i32, body: *u8)");
            }
            if (*a=='{') {
                /* copy body block */
                char body[4096];
                int bl=cb_read_block(a, body, sizeof(body));
                cb_bw(passthru, " { "); cb_bw(passthru, body); cb_bw(passthru, " }\n");
                a+=bl;
            }
        }
    }
    else if ((ecl=cb_kw(a,"file"))>0) { r->kind=RT_FILE; a=cb_skip_ws(a+ecl); int ql=cb_read_quoted(a,r->content,sizeof(r->content)); a=cb_skip_ws(a+ql); cb_read_quoted(a,r->ctype,sizeof(r->ctype)); }
    else if ((ecl=cb_kw(a,"tpl"))>0) {
        r->kind=RT_TEMPLATE; a=cb_skip_ws(a+ecl);
        int ql=cb_read_quoted(a,r->tpl_path,sizeof(r->tpl_path)); a=cb_skip_ws(a+ql);
        if (*a=='"') cb_read_quoted(a,r->data_path,sizeof(r->data_path));
        if (r->data_path[0]) *needs_grug=1;
    }
    else if ((ecl=cb_kw(a,"save"))>0) {
        r->kind=RT_SAVE; a=cb_skip_ws(a+ecl);
        int ql=cb_read_quoted(a,r->content,sizeof(r->content)); a=cb_skip_ws(a+ql);
        ql=cb_read_quoted(a,r->fields,sizeof(r->fields)); a=cb_skip_ws(a+ql);
        cb_read_path(a,r->redirect,sizeof(r->redirect));
        if (!r->redirect[0]) strcpy(r->redirect,"/");
        *needs_grug=1;
    }
    else if ((ecl=cb_kw(a,"page"))>0) {
        r->kind=RT_WS_PAGE; a=cb_skip_ws(a+ecl);
        cb_read_path(a,r->content,sizeof(r->content));
    }
    /* { inline } */
    else if (*a=='{') { r->kind=RT_INLINE; int bl=cb_read_block(a,r->body,sizeof(r->body)); a+=bl; }
    /* bare quoted string → RT_HTML (most compact: / "hello") */
    else if (*a=='"') { r->kind=RT_HTML; cb_read_quoted(a,r->content,sizeof(r->content)); }
    /* anything else → RT_HTML (bare text) */
    else { r->kind=RT_HTML; const char *le=cb_line_end(a); int tl=(int)(le-a); if(tl>(int)sizeof(r->content)-1) tl=(int)sizeof(r->content)-1; memcpy(r->content,a,tl); r->content[tl]='\0'; }
    return a;
}

/* ---- CLI codebook: generate expanded source ---- */
static void gen_cli(CliCB *cli, CBuf *passthru, CBuf *out) {
    /* Cli struct with all flags/args as fields */
    cb_bw(out, "struct Cli {\n");
    for (int i = 0; i < cli->flag_count; i++)
        cb_bfmt(out, "  %s: i32\n", cli->flags[i].name);
    for (int i = 0; i < cli->arg_count; i++)
        cb_bfmt(out, "  %s: *u8\n", cli->args[i].name);
    cb_bw(out, "}\n\n");

    /* passthrough (user code — contains cli_main(cli: *Cli) etc.) */
    if (passthru->n > 0) { cb_bc(passthru, '\0'); cb_bw(out, passthru->d); cb_bw(out, "\n"); }

    /* cli_help() */
    cb_bw(out, "fn __cli_help() {\n");
    if (cli->app_desc[0])
        cb_bfmt(out, "  printf(\"%s — %s\\n\\n\")\n", cli->app_name, cli->app_desc);
    else
        cb_bfmt(out, "  printf(\"%s\\n\\n\")\n", cli->app_name);

    /* usage line */
    cb_bfmt(out, "  printf(\"usage: %s", cli->app_name);
    if (cli->flag_count) cb_bw(out, " [flags]");
    for (int i = 0; i < cli->arg_count; i++)
        cb_bfmt(out, " <%s>", cli->args[i].name);
    cb_bw(out, "\\n\\n\")\n");

    /* flags help */
    if (cli->flag_count || 1) {
        cb_bw(out, "  printf(\"flags:\\n\")\n");
        for (int i = 0; i < cli->flag_count; i++)
            cb_bfmt(out, "  printf(\"  %s, --%-12s %s\\n\")\n",
                    cli->flags[i].shortf, cli->flags[i].name, cli->flags[i].help);
        cb_bw(out, "  printf(\"  -h, --help          show this help\\n\")\n");
    }

    /* args help */
    if (cli->arg_count) {
        cb_bw(out, "  printf(\"\\nargs:\\n\")\n");
        for (int i = 0; i < cli->arg_count; i++)
            cb_bfmt(out, "  printf(\"  %-18s %s\\n\")\n", cli->args[i].name, cli->args[i].help);
    }
    cb_bw(out, "}\n\n");

    /* main(argc, argv) */
    cb_bw(out, "fn main(argc: i32, argv: **u8) -> i32 {\n"
               "  cli := nw Cli\n");
    /* zero out flags */
    for (int i = 0; i < cli->flag_count; i++)
        cb_bfmt(out, "  cli.%s = 0\n", cli->flags[i].name);
    for (int i = 0; i < cli->arg_count; i++)
        cb_bfmt(out, "  cli.%s = null as *u8\n", cli->args[i].name);
    cb_bw(out, "  __pi := 0\n"
               "  for __i := 1..argc {\n"
               "    __a := *(argv + __i)\n");

    /* flag checks */
    for (int i = 0; i < cli->flag_count; i++) {
        cb_bfmt(out, "    %s strcmp(__a, \"%s\") == 0 || strcmp(__a, \"--%s\") == 0 { cli.%s = 1 }\n",
                i == 0 ? "if" : "el if", cli->flags[i].shortf, cli->flags[i].name, cli->flags[i].name);
    }

    /* help check */
    cb_bfmt(out, "    %s strcmp(__a, \"-h\") == 0 || strcmp(__a, \"--help\") == 0 { __cli_help(); exit(0) }\n",
            cli->flag_count ? "el if" : "if");

    /* unknown flag check */
    cb_bw(out, "    el if *(__a) == 45 { printf(\"unknown flag: %s\\n\", __a); __cli_help(); exit(1) }\n");

    /* positional args */
    cb_bw(out, "    el {\n");
    for (int i = 0; i < cli->arg_count; i++) {
        cb_bfmt(out, "      %s __pi == %d { cli.%s = __a }\n",
                i == 0 ? "if" : "el if", i, cli->args[i].name);
    }
    cb_bw(out, "      __pi += 1\n"
               "    }\n"
               "  }\n");

    /* required arg checks */
    for (int i = 0; i < cli->arg_count; i++) {
        cb_bfmt(out, "  if cli.%s as i64 == 0 { printf(\"error: missing <%s>\\n\"); __cli_help(); exit(1) }\n",
                cli->args[i].name, cli->args[i].name);
    }

    cb_bw(out, "  cli_main(cli)\n"
               "  free(cli as *void)\n"
               "  ret 0\n"
               "}\n");
}

/* ---- REST codebook: find model by name ---- */
static int rest_find_model(RestCB *r, const char *name) {
    for (int i = 0; i < r->model_count; i++)
        if (strcmp(r->models[i].name, name) == 0) return i;
    return -1;
}

/* ---- REST codebook: generate expanded source ---- */
static void gen_rest(RestCB *rest, CBuf *passthru, CBuf *out) {
    cb_bw(out, "\nuse http\nuse grug\nuse str\n\n");

    /* passthrough (user code) */
    if (passthru->n > 0) { cb_bc(passthru, '\0'); cb_bw(out, passthru->d); cb_bw(out, "\n"); }

    /* JSON string escape helper */
    cb_bw(out,
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

    /* JSON field finder: extracts "key":"value" from JSON body */
    cb_bw(out,
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

    /* strdup helper */
    cb_bw(out,
        "fn __jsd(s: *u8) -> *u8 {\n"
        "  l := strlen(s) as i32\n"
        "  d := malloc((l + 1) as u64) as *u8\n"
        "  memcpy(d as *void, s as *void, (l + 1) as u64)\n"
        "  ret d\n"
        "}\n\n");

    /* list handler for each model */
    for (int ri = 0; ri < rest->route_count; ri++) {
        if (rest->routes[ri].kind != REST_LIST) continue;
        int mi = rest_find_model(rest, rest->routes[ri].model);
        if (mi < 0) { fprintf(stderr, "codebook: unknown model '%s'\n", rest->routes[ri].model); exit(1); }

        char fn_name[128];
        snprintf(fn_name, sizeof(fn_name), "__rest_list_%s", rest->routes[ri].model);
        cb_bfmt(out, "fn %s(fd: i32, req: *u8) {\n", fn_name);
        cb_bw(out,   "  http_resp(fd, 200, \"application/json\")\n"
                     "  s := str_new()\n"
                     "  str_add(s, \"[\")\n");
        cb_bfmt(out, "  __g := grug_parse(\"%s.grug\")\n", rest->routes[ri].model);
        cb_bw(out,   "  if __g as i64 != 0 {\n"
                     "    __s := __g.sec\n"
                     "    __fi := 0\n"
                     "    wh __s as i64 != 0 {\n"
                     "      if __fi > 0 { str_add(s, \",\") }\n"
                     "      str_add(s, \"{\")\n");

        for (int f = 0; f < rest->models[mi].field_count; f++) {
            cb_bfmt(out, "      __fv%d := fval(__s, \"%s\")\n", f, rest->models[mi].fields[f]);
            cb_bfmt(out, "      str_add(s, \"\\\"%s\\\":\")\n", rest->models[mi].fields[f]);
            cb_bfmt(out, "      if __fv%d as i64 != 0 { str_add(s, \"\\\"\"); __jesc(s, __fv%d); str_add(s, \"\\\"\") }\n", f, f);
            cb_bfmt(out, "      el { str_add(s, \"null\") }\n");
            if (f < rest->models[mi].field_count - 1)
                cb_bw(out, "      str_add(s, \",\")\n");
        }

        cb_bw(out,   "      str_add(s, \"}\")\n"
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

    /* create handler for each model */
    for (int ri = 0; ri < rest->route_count; ri++) {
        if (rest->routes[ri].kind != REST_CREATE) continue;
        int mi = rest_find_model(rest, rest->routes[ri].model);
        if (mi < 0) { fprintf(stderr, "codebook: unknown model '%s'\n", rest->routes[ri].model); exit(1); }

        char fn_name[128];
        snprintf(fn_name, sizeof(fn_name), "__rest_create_%s", rest->routes[ri].model);
        cb_bfmt(out, "fn %s(fd: i32, req: *u8) {\n", fn_name);
        cb_bw(out,   "  body := http_body(req)\n"
                     "  if body as i64 == 0 { http_resp(fd, 400, \"application/json\"); http_send(fd, \"{\\\"error\\\":\\\"no body\\\"}\"); ret }\n");

        /* extract each field from JSON */
        for (int f = 0; f < rest->models[mi].field_count; f++)
            cb_bfmt(out, "  __v%d:[512]u8; __jfind(body, \"%s\", &__v%d, 512)\n", f, rest->models[mi].fields[f], f);

        /* store to grug */
        cb_bfmt(out, "  __sg := grug_parse(\"%s.grug\")\n", rest->routes[ri].model);
        cb_bw(out,   "  if __sg as i64 == 0 { __sg = nw Grug; __sg.sec = null as *Sec; __sg.buf = null as *u8 }\n"
                     "  __sid:[40]u8; sprintf(&__sid, \"e_%d\", getpid())\n"
                     "  __sp := &__sid as *u8\n");
        for (int f = 0; f < rest->models[mi].field_count; f++)
            cb_bfmt(out, "  grug_set(__sg, __sp, \"%s\", &__v%d)\n", rest->models[mi].fields[f], f);
        cb_bfmt(out, "  grug_write(__sg, \"%s.grug\"); grug_fr(__sg)\n", rest->routes[ri].model);

        cb_bw(out,   "  http_resp(fd, 201, \"application/json\")\n"
                     "  http_send(fd, \"{\\\"ok\\\":true}\")\n"
                     "}\n\n");
    }

    /* serve function */
    cb_bw(out, "fn __cb_serve(fd: i32) {\n"
               "  buf:[8192]u8; n := read(fd, &buf, 8191) as i32\n"
               "  if n <= 0 { close(fd); ret }\n"
               "  *((&buf) as *u8 + n) = 0; req := &buf as *u8\n"
               "  path:[256]u8; http_path(req, &path, 256)\n");

    /* route dispatch */
    int first = 1;
    for (int ri = 0; ri < rest->route_count; ri++) {
        const char *mcheck;
        if (rest->routes[ri].method == REST_POST)
            mcheck = "http_ispost(req) != 0 && ";
        else
            mcheck = "http_isget(req) != 0 && ";

        cb_bfmt(out, "  %s %sstrcmp(&path, \"%s\") == 0 {\n",
                first ? "if" : "el if", mcheck, rest->routes[ri].path);
        first = 0;

        switch (rest->routes[ri].kind) {
        case REST_LIST:
            cb_bfmt(out, "    __rest_list_%s(fd, req)\n", rest->routes[ri].model);
            break;
        case REST_CREATE:
            cb_bfmt(out, "    __rest_create_%s(fd, req)\n", rest->routes[ri].model);
            break;
        case REST_STATIC:
            cb_bw(out,  "    http_resp(fd, 200, \"application/json\")\n");
            cb_bfmt(out, "    http_send(fd, \"%s\")\n", rest->routes[ri].content);
            break;
        }
        cb_bw(out, "  }\n");
    }

    /* 404 */
    if (rest->route_count > 0)
        cb_bw(out, "  el { http_resp(fd, 404, \"application/json\"); http_send(fd, \"{\\\"error\\\":\\\"not found\\\"}\") }\n");
    cb_bw(out, "  close(fd)\n}\n\n");

    /* main */
    cb_bw(out, "ext signal(i32, *void) -> *void\nfn main() {\n"
               "  signal(17, 1 as *void)\n");
    cb_bfmt(out, "  sfd := http_listen(%d)\n", rest->port);
    cb_bw(out,   "  if sfd < 0 { printf(\"listen failed\\n\"); ret 1 }\n");
    cb_bfmt(out, "  printf(\":%d\\n\")\n", rest->port);
    cb_bw(out,   "  wh 1 { cfd := accept(sfd, null, null); if cfd < 0 { ret 1 }\n"
                 "    pid := fork(); if pid == 0 { close(sfd); __cb_serve(cfd); exit(0) }; close(cfd) }\n}\n");
}

/* ---- Test codebook: generate expanded source ---- */
static void gen_test(TestCB *test, CBuf *passthru, CBuf *out) {
    /* passthrough (user code — functions under test) */
    if (passthru->n > 0) { cb_bc(passthru, '\0'); cb_bw(out, passthru->d); cb_bw(out, "\n"); }

    /* generate test functions: each assert becomes if/el check */
    for (int i = 0; i < test->count; i++) {
        cb_bfmt(out, "fn __test_%d() -> i32 {\n", i);

        /* parse body for assert statements */
        const char *bp = test->cases[i].body;
        while (*bp) {
            bp = cb_skip_ws(bp);
            if (*bp == '\n') { bp++; continue; }
            if (*bp == '\0') break;

            int acl = cb_kw(bp, "assert");
            if (acl) {
                const char *expr_start = cb_skip_ws(bp + acl);
                /* find end of expression: newline, semicolon, or } */
                const char *expr_end = expr_start;
                int depth = 0;
                while (*expr_end && *expr_end != '\n' && *expr_end != ';') {
                    if (*expr_end == '(') depth++;
                    else if (*expr_end == ')') { if (depth > 0) depth--; }
                    else if (*expr_end == '}' && depth == 0) break;
                    expr_end++;
                }
                /* trim trailing whitespace */
                while (expr_end > expr_start && (*(expr_end-1) == ' ' || *(expr_end-1) == '\t'))
                    expr_end--;

                int elen = (int)(expr_end - expr_start);
                char expr_text[2048];
                if (elen > (int)sizeof(expr_text) - 1) elen = (int)sizeof(expr_text) - 1;
                memcpy(expr_text, expr_start, elen);
                expr_text[elen] = '\0';

                /* escape the expression for printf string */
                char expr_esc[2048];
                int ei = 0;
                for (int j = 0; expr_text[j] && ei < (int)sizeof(expr_esc) - 2; j++) {
                    if (expr_text[j] == '"') { expr_esc[ei++] = '\\'; expr_esc[ei++] = '"'; }
                    else if (expr_text[j] == '\\') { expr_esc[ei++] = '\\'; expr_esc[ei++] = '\\'; }
                    else if (expr_text[j] == '%') { expr_esc[ei++] = '%'; expr_esc[ei++] = '%'; }
                    else expr_esc[ei++] = expr_text[j];
                }
                expr_esc[ei] = '\0';

                cb_bfmt(out, "  if %s { }\n", expr_text);
                cb_bfmt(out, "  el { printf(\"    FAIL: assert %s\\n\"); ret 1 }\n", expr_esc);

                bp = expr_end;
                if (*bp == ';') bp++;
                if (*bp == '\n') bp++;
            } else {
                /* skip non-assert line */
                while (*bp && *bp != '\n') bp++;
                if (*bp == '\n') bp++;
            }
        }
        cb_bw(out, "  ret 0\n}\n\n");
    }

    /* main: run all tests */
    cb_bw(out, "fn main() -> i32 {\n"
               "  __pass := 0\n"
               "  __fail := 0\n");
    cb_bfmt(out, "  __total := %d\n", test->count);
    cb_bw(out, "  printf(\"running %d tests...\\n\\n\", __total)\n");

    for (int i = 0; i < test->count; i++) {
        /* escape test name for printf */
        char name_esc[256];
        int ni = 0;
        for (int j = 0; test->cases[i].name[j] && ni < (int)sizeof(name_esc) - 2; j++) {
            if (test->cases[i].name[j] == '"') { name_esc[ni++] = '\\'; name_esc[ni++] = '"'; }
            else if (test->cases[i].name[j] == '\\') { name_esc[ni++] = '\\'; name_esc[ni++] = '\\'; }
            else if (test->cases[i].name[j] == '%') { name_esc[ni++] = '%'; name_esc[ni++] = '%'; }
            else name_esc[ni++] = test->cases[i].name[j];
        }
        name_esc[ni] = '\0';

        cb_bfmt(out, "  printf(\"  %s ... \")\n", name_esc);
        cb_bfmt(out, "  if __test_%d() == 0 { printf(\"\\x1b[32mPASS\\x1b[0m\\n\"); __pass += 1 }\n", i);
        cb_bw(out,   "  el { printf(\"\\x1b[31mFAIL\\x1b[0m\\n\"); __fail += 1 }\n");
    }

    cb_bw(out, "  printf(\"\\n%d/%d passed\\n\", __pass, __total)\n"
               "  if __fail > 0 { ret 1 }\n"
               "  ret 0\n"
               "}\n");
}

/* ---- helper: check if identifier matches at position (word boundary) ---- */
static int cb_match_ident(const char *p, const char *id) {
    int n = (int)strlen(id);
    if (strncmp(p, id, n) != 0) return 0;
    char c = p[n];
    if (c && c != '\0' && c != '\n' && c != ' ' && c != '\t' && c != '\r' && c != ';') return 0;
    return n;
}

/* ---- main codebook expansion ---- */
char *codebook_expand(const char *src) {
    WebCB web = {0};
    CliCB cli = {0};
    RestCB rest = {0};
    TestCB tst = {0};
    CBuf passthru = {0};

    const char *p = src;
    int found_codebook = 0;
    /* which codebook is active: 0=none, 1=web, 2=cli, 3=rest, 4=test */
    int active_cb = 0;

    while (*p) {
        const char *q = cb_skip_ws(p);

        /* skip empty lines and comments */
        if (*q == '\n') { if (!active_cb) cb_bc(&passthru, '\n'); p = q + 1; continue; }
        if (*q == '\0') break;

        /* detect codebook activation: 📥 NAME  OR  use NAME */
        int cl = cb_cem(q, CP_USE);
        if (!cl) cl = cb_kw(q, "use");
        if (cl && !active_cb) {
            const char *after = cb_skip_ws(q + cl);
            if (cb_match_ident(after, "web")) {
                web.active = 1; found_codebook = 1; active_cb = 1;
                p = cb_line_end(after + 3); if (*p == '\n') p++; continue;
            }
            if (cb_match_ident(after, "cli")) {
                cli.active = 1; found_codebook = 1; active_cb = 2;
                p = cb_line_end(after + 3); if (*p == '\n') p++; continue;
            }
            if (cb_match_ident(after, "rest")) {
                rest.active = 1; found_codebook = 1; active_cb = 3;
                p = cb_line_end(after + 4); if (*p == '\n') p++; continue;
            }
            if (cb_match_ident(after, "test")) {
                tst.active = 1; found_codebook = 1; active_cb = 4;
                p = cb_line_end(after + 4); if (*p == '\n') p++; continue;
            }
        }

        /* no codebook active — passthrough */
        if (!active_cb) {
            while (*p && *p != '\n') cb_bc(&passthru, *p++);
            if (*p == '\n') { cb_bc(&passthru, '\n'); p++; }
            continue;
        }

        /* ==== WEB codebook directives ==== */
        if (active_cb == 1) {
            /* 🌐 PORT  OR  listen PORT */
            cl = cb_cem(q, CP_LISTEN);
            if (!cl) cl = cb_kw(q, "listen");
            if (cl) { const char *a = cb_skip_ws(q + cl); web.port = atoi(a); if (web.port <= 0) web.port = 8080; p = cb_line_end(a); if (*p == '\n') p++; continue; }

            /* route: 📍  OR  line starts with /  OR  starts with GET/POST */
            cl = cb_cem(q, CP_ROUTE);
            int is_route = cl > 0;
            int is_ascii_route = 0;
            if (!is_route && (*q == '/' || strncmp(q, "GET ", 4) == 0 || strncmp(q, "POST ", 5) == 0)) { is_ascii_route = 1; is_route = 1; cl = 0; }
            if (is_route) {
                Route *r = &web.routes[web.route_count]; memset(r, 0, sizeof(*r)); r->method = RM_GET;
                const char *a = is_ascii_route ? q : cb_skip_ws(q + cl);
                if (strncmp(a, "POST", 4) == 0 && (a[4] == ' ' || a[4] == '\t')) { r->method = RM_POST; a = cb_skip_ws(a + 4); }
                else if (strncmp(a, "GET", 3) == 0 && (a[3] == ' ' || a[3] == '\t')) { a = cb_skip_ws(a + 3); }
                int pl = cb_read_path(a, r->path, sizeof(r->path)); a = cb_skip_ws(a + pl);
                a = parse_route_type(a, r, &web.needs_grug, &passthru);
                web.route_count++;
                p = cb_line_end(a); while (*p == '\n' || *p == '\r') p++; continue;
            }

            /* websocket: 📡 PATH  OR  ws PATH */
            cl = cb_cem(q, CP_WS);
            if (!cl) cl = cb_kw(q, "ws");
            if (cl) {
                const char *a = cb_skip_ws(q + cl); int wi = web.ws_count;
                int pl = cb_read_path(a, web.ws[wi].path, sizeof(web.ws[wi].path)); a = cb_skip_ws(a + pl);
                int ecl; if ((ecl = cb_cem(a, CP_ECHO)) > 0) { web.ws[wi].is_echo = 1; a += ecl; }
                else if ((ecl = cb_kw(a, "echo")) > 0) { web.ws[wi].is_echo = 1; a += ecl; }
                else if (*a == '{') { web.ws[wi].is_echo = 0; int bl = cb_read_block(a, web.ws[wi].body, sizeof(web.ws[wi].body)); a += bl; }
                web.ws_count++; p = cb_line_end(a); while (*p == '\n' || *p == '\r') p++; continue;
            }

            /* crud NAME field1 field2+ ... */
            cl = cb_kw(q, "crud");
            if (cl) {
                const char *a = cb_skip_ws(q + cl);
                char cname[128]; int nl = cb_read_word(a, cname, sizeof(cname)); a = cb_skip_ws(a + nl);
                char fraw[512]; int fi = 0;
                char fclean[512]; int ci = 0;
                int first_f = 1;
                while (*a && *a != '\n' && *a != '\r') {
                    char fw[64]; int wl = 0;
                    while (wl < 63 && *a && *a != ' ' && *a != '\t' && *a != '\n' && *a != '\r') { fw[wl++] = *a++; }
                    fw[wl] = '\0'; a = cb_skip_ws(a);
                    if (!wl) break;
                    if (!first_f) { fraw[fi++] = ','; fclean[ci++] = ','; }
                    for (int j = 0; fw[j]; j++) { if (fi < (int)sizeof(fraw) - 1) fraw[fi++] = fw[j]; }
                    for (int j = 0; fw[j] && fw[j] != '+'; j++) { if (ci < (int)sizeof(fclean) - 1) fclean[ci++] = fw[j]; }
                    first_f = 0;
                }
                fraw[fi] = '\0'; fclean[ci] = '\0';
                char store[256]; snprintf(store, sizeof(store), "%s.grug", cname);
                Route *rp = &web.routes[web.route_count]; memset(rp, 0, sizeof(*rp));
                rp->kind = RT_SAVE; rp->method = RM_POST; strcpy(rp->path, "/");
                strncpy(rp->content, store, sizeof(rp->content) - 1);
                strncpy(rp->fields, fclean, sizeof(rp->fields) - 1);
                strcpy(rp->redirect, "/");
                web.route_count++;
                Route *rg = &web.routes[web.route_count]; memset(rg, 0, sizeof(*rg));
                rg->kind = RT_CRUD; rg->method = RM_GET; strcpy(rg->path, "/");
                strncpy(rg->content, cname, sizeof(rg->content) - 1);
                strncpy(rg->fields, fraw, sizeof(rg->fields) - 1);
                strncpy(rg->data_path, store, sizeof(rg->data_path) - 1);
                web.route_count++;
                web.needs_grug = 1;
                p = cb_line_end(a); while (*p == '\n' || *p == '\r') p++; continue;
            }

            /* passthrough in web mode */
            while (*p && *p != '\n') cb_bc(&passthru, *p++);
            if (*p == '\n') { cb_bc(&passthru, '\n'); p++; }
            continue;
        }

        /* ==== CLI codebook directives ==== */
        if (active_cb == 2) {
            /* name "APPNAME" */
            cl = cb_kw(q, "name");
            if (cl) {
                const char *a = cb_skip_ws(q + cl);
                cb_read_quoted(a, cli.app_name, sizeof(cli.app_name));
                p = cb_line_end(a); if (*p == '\n') p++; continue;
            }

            /* desc "DESCRIPTION" */
            cl = cb_kw(q, "desc");
            if (cl) {
                const char *a = cb_skip_ws(q + cl);
                cb_read_quoted(a, cli.app_desc, sizeof(cli.app_desc));
                p = cb_line_end(a); if (*p == '\n') p++; continue;
            }

            /* flag NAME -X "help" */
            cl = cb_kw(q, "flag");
            if (cl) {
                const char *a = cb_skip_ws(q + cl);
                int nl = cb_read_word(a, cli.flags[cli.flag_count].name,
                                      sizeof(cli.flags[cli.flag_count].name));
                a = cb_skip_ws(a + nl);
                /* read short flag: -X */
                if (*a == '-') {
                    int si = 0;
                    while (*a && *a != ' ' && *a != '\t' && si < 3) {
                        cli.flags[cli.flag_count].shortf[si++] = *a++;
                    }
                    cli.flags[cli.flag_count].shortf[si] = '\0';
                    a = cb_skip_ws(a);
                }
                cb_read_quoted(a, cli.flags[cli.flag_count].help,
                               sizeof(cli.flags[cli.flag_count].help));
                cli.flag_count++;
                p = cb_line_end(a); if (*p == '\n') p++; continue;
            }

            /* arg NAME "help" */
            cl = cb_kw(q, "arg");
            if (cl) {
                const char *a = cb_skip_ws(q + cl);
                int nl = cb_read_word(a, cli.args[cli.arg_count].name,
                                      sizeof(cli.args[cli.arg_count].name));
                a = cb_skip_ws(a + nl);
                cb_read_quoted(a, cli.args[cli.arg_count].help,
                               sizeof(cli.args[cli.arg_count].help));
                cli.arg_count++;
                p = cb_line_end(a); if (*p == '\n') p++; continue;
            }

            /* passthrough in cli mode (user code like cli_main) */
            while (*p && *p != '\n') cb_bc(&passthru, *p++);
            if (*p == '\n') { cb_bc(&passthru, '\n'); p++; }
            continue;
        }

        /* ==== REST codebook directives ==== */
        if (active_cb == 3) {
            /* listen PORT */
            cl = cb_cem(q, CP_LISTEN);
            if (!cl) cl = cb_kw(q, "listen");
            if (cl) {
                const char *a = cb_skip_ws(q + cl);
                rest.port = atoi(a); if (rest.port <= 0) rest.port = 8080;
                p = cb_line_end(a); if (*p == '\n') p++; continue;
            }

            /* model NAME field1 field2 ... */
            cl = cb_kw(q, "model");
            if (cl) {
                const char *a = cb_skip_ws(q + cl);
                int mi = rest.model_count;
                int nl = cb_read_word(a, rest.models[mi].name, sizeof(rest.models[mi].name));
                a = cb_skip_ws(a + nl);
                rest.models[mi].field_count = 0;
                while (*a && *a != '\n' && *a != '\r') {
                    int fi = rest.models[mi].field_count;
                    int wl = cb_read_word(a, rest.models[mi].fields[fi],
                                          sizeof(rest.models[mi].fields[fi]));
                    if (wl == 0) break;
                    rest.models[mi].field_count++;
                    a = cb_skip_ws(a + wl);
                }
                rest.model_count++;
                p = cb_line_end(a); if (*p == '\n') p++; continue;
            }

            /* GET/POST /PATH verb MODEL  OR  GET /PATH "text" */
            if (strncmp(q, "GET ", 4) == 0 || strncmp(q, "POST ", 5) == 0) {
                int ri = rest.route_count;
                RestMethod meth = REST_GET;
                const char *a = q;
                if (strncmp(a, "POST", 4) == 0) { meth = REST_POST; a = cb_skip_ws(a + 4); }
                else { a = cb_skip_ws(a + 3); }
                rest.routes[ri].method = meth;

                int pl = cb_read_path(a, rest.routes[ri].path, sizeof(rest.routes[ri].path));
                a = cb_skip_ws(a + pl);

                /* check for quoted static text */
                if (*a == '"') {
                    rest.routes[ri].kind = REST_STATIC;
                    cb_read_quoted(a, rest.routes[ri].content, sizeof(rest.routes[ri].content));
                } else {
                    /* verb MODEL: "list user" or "create user" */
                    char verb[32]; int vl = cb_read_word(a, verb, sizeof(verb)); a = cb_skip_ws(a + vl);
                    cb_read_word(a, rest.routes[ri].model, sizeof(rest.routes[ri].model));
                    if (strcmp(verb, "list") == 0) rest.routes[ri].kind = REST_LIST;
                    else if (strcmp(verb, "create") == 0) rest.routes[ri].kind = REST_CREATE;
                    else { fprintf(stderr, "codebook: unknown REST verb '%s'\n", verb); exit(1); }
                }
                rest.route_count++;
                p = cb_line_end(a); if (*p == '\n') p++; continue;
            }

            /* passthrough in rest mode */
            while (*p && *p != '\n') cb_bc(&passthru, *p++);
            if (*p == '\n') { cb_bc(&passthru, '\n'); p++; }
            continue;
        }

        /* ==== TEST codebook directives ==== */
        if (active_cb == 4) {
            /* test "NAME" { body } */
            cl = cb_kw(q, "test");
            if (cl) {
                const char *a = cb_skip_ws(q + cl);
                int ql2 = cb_read_quoted(a, tst.cases[tst.count].name,
                                         sizeof(tst.cases[tst.count].name));
                a = cb_skip_ws(a + ql2);
                if (*a == '{') {
                    int bl = cb_read_block(a, tst.cases[tst.count].body,
                                           sizeof(tst.cases[tst.count].body));
                    a += bl;
                }
                tst.count++;
                p = cb_line_end(a); while (*p == '\n' || *p == '\r') p++; continue;
            }

            /* passthrough in test mode (user code — functions under test) */
            while (*p && *p != '\n') cb_bc(&passthru, *p++);
            if (*p == '\n') { cb_bc(&passthru, '\n'); p++; }
            continue;
        }

        /* fallback passthrough */
        while (*p && *p != '\n') cb_bc(&passthru, *p++);
        if (*p == '\n') { cb_bc(&passthru, '\n'); p++; }
    }

    if (!found_codebook) { free(passthru.d); return (char *)src; }

    /* ==== dispatch to codebook-specific generators ==== */
    if (active_cb == 2) {
        /* CLI codebook */
        if (!cli.app_name[0]) strcpy(cli.app_name, "app");
        CBuf out = {0};
        gen_cli(&cli, &passthru, &out);
        free(passthru.d);
        return cb_bz(&out);
    }

    if (active_cb == 3) {
        /* REST codebook */
        if (rest.port <= 0) rest.port = 8080;
        CBuf out = {0};
        gen_rest(&rest, &passthru, &out);
        free(passthru.d);
        return cb_bz(&out);
    }

    if (active_cb == 4) {
        /* Test codebook */
        CBuf out = {0};
        gen_test(&tst, &passthru, &out);
        free(passthru.d);
        return cb_bz(&out);
    }

    /* ---- generate output ---- */
    CBuf out={0};
    cb_bw(&out, "\n");
    if (web.ws_count>0) cb_bw(&out, "use http\nuse ws\n");
    else cb_bw(&out, "use http\n");
    if (web.needs_grug) cb_bw(&out, "use grug\n");

    /* passthrough (user code) */
    if (passthru.n>0) { cb_bc(&passthru,'\0'); cb_bw(&out, passthru.d); cb_bw(&out,"\n"); }

    /* file-serve helper */
    int has_file=0; for (int i=0;i<web.route_count;i++) if (web.routes[i].kind==RT_FILE) has_file=1;
    if (has_file) cb_bw(&out,
        "fn __cb_sf(fd: i32, fpath: *u8, ctype: *u8) {\n"
        "  ffd := open(fpath,0)\n"
        "  if ffd < 0 { http_resp(fd,404,\"text/plain\"); http_send(fd,\"not found\"); ret }\n"
        "  fsz := lseek(ffd,0,2) as i32; lseek(ffd,0,0)\n"
        "  hdr:[512]u8; sprintf(&hdr,\"HTTP/1.1 200 OK\\r\\nContent-Type: %s\\r\\nContent-Length: %d\\r\\nConnection: close\\r\\n\\r\\n\",ctype,fsz)\n"
        "  http_send(fd,&hdr); fbuf:[4096]u8\n"
        "  wh 1 { nr:=read(ffd,&fbuf,4096) as i32; if nr<=0{close(ffd);ret}; write(fd,&fbuf,nr as u64) }\n"
        "}\n\n");

    /* serve function */
    cb_bw(&out, "fn __cb_serve(fd: i32) {\n"
                "  buf:[8192]u8; n:=read(fd,&buf,8191) as i32\n"
                "  if n<=0{close(fd);ret}\n"
                "  *((&buf) as *u8+n)=0; req:=&buf as *u8\n"
                "  path:[256]u8; http_path(req,&path,256)\n");

    /* websocket upgrade */
    if (web.ws_count>0) {
        cb_bw(&out, "  if strstr(req,\"Upgrade: websocket\") as i64 != 0 {\n");
        for (int i=0; i<web.ws_count; i++) {
            cb_bfmt(&out, "    %s strcmp(&path,\"%s\")==0 {\n", i==0?"if":"el if", web.ws[i].path);
            cb_bw(&out,   "      if ws_handshake(fd,req)<0{close(fd);ret}\n");
            if (web.ws[i].is_echo) {
                cb_bw(&out, "      mbuf:[4096]u8\n      wh 1{ml:=ws_read(fd,&mbuf,4096);if ml<0{close(fd);ret};if ml>0{ws_text(fd,&mbuf)}}\n");
            } else {
                cb_bfmt(&out, "      msg:[4096]u8\n      wh 1{ml:=ws_read(fd,&msg,4096);if ml<0{close(fd);ret};if ml>0{\n        %s\n      }}\n", web.ws[i].body);
            }
            cb_bw(&out, "    }\n");
        }
        cb_bw(&out, "    close(fd);ret\n  }\n");
    }

    /* route dispatch */
    int first=1;
    for (int i=0; i<web.route_count; i++) {
        Route *r=&web.routes[i];
        char mc[64]=""; if (r->method==RM_POST) snprintf(mc,sizeof(mc),"http_ispost(req)!=0 && ");
        cb_bfmt(&out, "  %s %sstrcmp(&path,\"%s\")==0 {\n", first?"if":"el if", mc, r->path);
        first=0;

        switch (r->kind) {
        case RT_HTML:
            cb_bw(&out, "    http_resp(fd,200,\"text/html\")\n");
            cb_bfmt(&out, "    http_send(fd,\"%s\")\n", r->content);
            break;
        case RT_FUNC:
            cb_bfmt(&out, "    %s(fd,req)\n", r->content);
            break;
        case RT_FILE:
            cb_bfmt(&out, "    __cb_sf(fd,\"%s\",\"%s\")\n", r->content, r->ctype[0]?r->ctype:"application/octet-stream");
            break;
        case RT_INLINE:
            cb_bfmt(&out, "    %s\n", r->body);
            break;
        case RT_TEMPLATE: {
            GrugTpl tpl={0};
            if (read_grug_tpl(r->tpl_path, &tpl) < 0) { fprintf(stderr,"codebook: template '%s' not found\n",r->tpl_path); exit(1); }
            cb_bw(&out, "    http_resp(fd,200,\"text/html\")\n");

            /* --- auto-inject HTML boilerplate + default theme --- */
            const char *title = tpl_get(&tpl, "head");
            if (!title) title = "el-stupido";
            const char *extra_css = tpl_get(&tpl, "style");

            /* <!DOCTYPE html><html><head>... */
            cb_bw(&out, "    http_send(fd,\"<!DOCTYPE html><html><head><meta charset='utf-8'><title>");
            cb_bw(&out, title);
            cb_bw(&out, "</title><style>");
            cb_bw(&out, CB_THEME);
            if (extra_css) cb_bw(&out, extra_css);
            cb_bw(&out, "</style></head><body><h1>");
            cb_bw(&out, title);
            cb_bw(&out, "</h1>\")\n");

            /* body section (static html) */
            const char *body_html = tpl_get(&tpl, "body");
            if (body_html) gen_tpl_send(&out, body_html, "    ");

            /* form section */
            const char *form_html = tpl_get(&tpl, "form");
            if (form_html) gen_tpl_send(&out, form_html, "    ");

            /* if data_path: iterate grug sections with 'each' template */
            const char *each = tpl_get(&tpl, "each");
            if (r->data_path[0] && each) {
                cb_bfmt(&out, "    __g := grug_parse(\"%s\")\n", r->data_path);
                cb_bw(&out,   "    if __g as i64 != 0 {\n"
                              "      __s := __g.sec\n"
                              "      wh __s as i64 != 0 {\n");
                gen_each_render(&out, each);
                cb_bw(&out,   "        __s = __s.nx\n"
                              "      }\n"
                              "      grug_fr(__g)\n"
                              "    }\n");
            }

            /* script section (js) → <script>grugscript_preamble + user_js</script> */
            const char *js = tpl_get(&tpl, "script");
            if (js) {
                cb_bw(&out, "    http_send(fd,\"<script>");
                cb_bw(&out, GS_PREAMBLE);
                /* escape the JS for embedding in .es string literal */
                for (const char *jp=js; *jp; jp++) {
                    if (*jp=='"') cb_bw(&out, "\\\"");
                    else if (*jp=='\\') cb_bw(&out, "\\\\");
                    else if (*jp=='\n') cb_bw(&out, "\\n");
                    else cb_bc(&out, *jp);
                }
                cb_bw(&out, "</script>\")\n");
            }

            /* close html */
            cb_bw(&out, "    http_send(fd,\"</body></html>\")\n");
            break;
        }
        case RT_CRUD: {
            cb_bw(&out, "    http_resp(fd,200,\"text/html\")\n");
            /* --- auto-generate full CRUD page: head + form + list --- */
            const char *ctitle = r->content;
            cb_bw(&out, "    http_send(fd,\"<!DOCTYPE html><html><head><meta charset='utf-8'><title>");
            cb_bw(&out, ctitle);
            cb_bw(&out, "</title><style>");
            cb_bw(&out, CB_THEME);
            cb_bw(&out, "</style></head><body><h1>");
            cb_bw(&out, ctitle);
            cb_bw(&out, "</h1><form method='post'>");
            /* emit form fields from fraw (comma-sep, + = textarea) */
            { char ff[512]; strncpy(ff,r->fields,sizeof(ff)-1); ff[sizeof(ff)-1]='\0';
              char *fp=ff;
              while (*fp) {
                  char fname[64]; int fi2=0;
                  int is_area=0;
                  while (*fp && *fp!=',' && fi2<63) {
                      if (*fp=='+') { is_area=1; fp++; continue; }
                      fname[fi2++]=*fp++;
                  }
                  fname[fi2]='\0'; if (*fp==',') fp++;
                  if (!fi2) continue;
                  if (is_area)
                      cb_bfmt(&out, "<textarea name='%s' placeholder='%s' required></textarea>", fname, fname);
                  else
                      cb_bfmt(&out, "<input name='%s' placeholder='%s' required>", fname, fname);
              }
            }
            cb_bw(&out, "<button>post</button></form>\")\n");
            /* iterate stored entries */
            cb_bfmt(&out, "    __g := grug_parse(\"%s\")\n", r->data_path);
            cb_bw(&out,   "    if __g as i64 != 0 {\n"
                          "      __s := __g.sec\n"
                          "      wh __s as i64 != 0 {\n"
                          "        http_send(fd,\"<div class='card'>\")\n");
            /* emit each field: first as <b>, rest as <p> */
            { char ff[512]; strncpy(ff,r->fields,sizeof(ff)-1); ff[sizeof(ff)-1]='\0';
              char *fp=ff; int idx=0;
              while (*fp) {
                  char fname[64]; int fi2=0;
                  while (*fp && *fp!=',' && *fp!='+' && fi2<63) fname[fi2++]=*fp++;
                  if (*fp=='+') fp++;
                  fname[fi2]='\0'; if (*fp==',') fp++;
                  if (!fi2) continue;
                  if (idx==0)
                      cb_bfmt(&out, "        __tf := fval(__s, \"%s\")\n"
                                    "        if __tf as i64 != 0 { http_send(fd,\"<b>\"); http_hesc(fd, __tf); http_send(fd,\"</b>\") }\n", fname);
                  else
                      cb_bfmt(&out, "        __tf := fval(__s, \"%s\")\n"
                                    "        if __tf as i64 != 0 { http_send(fd,\"<p>\"); http_hesc(fd, __tf); http_send(fd,\"</p>\") }\n", fname);
                  idx++;
              }
            }
            cb_bw(&out,   "        http_send(fd,\"</div>\")\n"
                          "        __s = __s.nx\n"
                          "      }\n"
                          "      grug_fr(__g)\n"
                          "    }\n");
            cb_bw(&out, "    http_send(fd,\"</body></html>\")\n");
            break;
        }
        case RT_WS_PAGE: {
            cb_bw(&out, "    http_resp(fd,200,\"text/html\")\n");
            /* full auto-generated WS client page */
            cb_bw(&out, "    http_send(fd,\"<!DOCTYPE html><html><head><meta charset='utf-8'><title>ws</title><style>");
            cb_bw(&out, CB_THEME);
            cb_bw(&out, "</style></head><body><h1>ws</h1>"
                        "<div id='log'></div>"
                        "<input id='msg' placeholder='type...' autofocus>"
                        "<button onclick='snd()'>send</button>"
                        "<script>");
            cb_bw(&out, GS_PREAMBLE);
            cb_bw(&out, CB_WS_JS);
            cb_bfmt(&out, "_c('%s')", r->content); /* connect to ws endpoint */
            cb_bw(&out, "</script></body></html>\")\n");
            break;
        }
        case RT_SAVE: {
            cb_bfmt(&out, "    __bl := http_body(req)\n");
            cb_bw(&out,   "    if __bl as i64 != 0 {\n");
            /* declare vars for each field */
            char flds[512]; strncpy(flds, r->fields, sizeof(flds)-1); flds[sizeof(flds)-1]='\0';
            int fc=0; char *fnames[32]; char *fp=flds;
            while (*fp && fc<32) {
                while (*fp==' ') fp++;
                fnames[fc]=fp; while (*fp&&*fp!=',') fp++;
                if (*fp==',') { *fp='\0'; fp++; }
                if (*fnames[fc]) fc++;
            }
            for (int j=0;j<fc;j++) cb_bfmt(&out, "      __v%d:[512]u8; http_fval(__bl,\"%s\",&__v%d,512)\n", j, fnames[j], j);
            /* check all non-empty */
            cb_bw(&out, "      if ");
            for (int j=0;j<fc;j++) { if(j) cb_bw(&out," && "); cb_bfmt(&out,"*((&__v%d) as *u8)!=0",j); }
            cb_bw(&out, " {\n");
            cb_bfmt(&out, "        __sg := grug_parse(\"%s\")\n", r->content);
            cb_bw(&out,   "        if __sg as i64 == 0 { __sg=nw Grug; __sg.sec=null as *Sec; __sg.buf=null as *u8 }\n");
            cb_bw(&out,   "        __sid:[40]u8; sprintf(&__sid,\"e_%d\",getpid())\n"
                          "        __sp := &__sid as *u8\n");
            for (int j=0;j<fc;j++) cb_bfmt(&out, "        grug_set(__sg,__sp,\"%s\",&__v%d)\n", fnames[j], j);
            cb_bfmt(&out, "        grug_write(__sg,\"%s\"); grug_fr(__sg)\n", r->content);
            cb_bw(&out,   "      }\n    }\n");
            cb_bfmt(&out, "    http_redirect(fd,\"%s\")\n", r->redirect);
            break;
        }
        }
        cb_bw(&out, "  }\n");
    }

    /* 404 */
    if (web.route_count>0) cb_bw(&out, "  el { http_resp(fd,404,\"text/plain\"); http_send(fd,\"not found\") }\n");
    cb_bw(&out, "  close(fd)\n}\n\n");

    /* main */
    cb_bw(&out, "ext signal(i32, *void) -> *void\nfn main() {\n"
                "  signal(17,1 as *void)\n");
    cb_bfmt(&out, "  sfd:=http_listen(%d)\n", web.port);
    cb_bw(&out,   "  if sfd<0{printf(\"listen failed\\n\");ret 1}\n");
    cb_bfmt(&out, "  printf(\":%d\\n\")\n", web.port);
    cb_bw(&out,   "  wh 1{cfd:=accept(sfd,null,null);if cfd<0{printf(\"accept fail\\n\");ret 1}\n"
                  "    pid:=fork();if pid==0{close(sfd);__cb_serve(cfd);exit(0)};close(cfd)}\n}\n");

    free(passthru.d);
    return cb_bz(&out);
}
