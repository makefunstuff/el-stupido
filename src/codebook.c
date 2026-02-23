/*  Codebook expander — text-level pass between preprocess() and parser.
 *
 *  Directives (after 📥 web activates web codebook):
 *    🌐 PORT                              — listen on port
 *    📍 PATH 📄 "text"                    — static html
 *    📍 PATH 📊 func                      — call func(fd,req)
 *    📍 PATH 📁 "file" "ctype"            — serve file
 *    📍 PATH 🎨 "tpl.grug"               — serve .grug template (static)
 *    📍 PATH 🎨 "tpl.grug" "data.grug"   — serve template + iterate data
 *    📍 PATH { body }                     — inline handler (fd/req in scope)
 *    📍 POST PATH 💾 "store.grug" "f1,f2" /redir — save form, redirect
 *    📡 PATH 🔁                           — websocket echo
 *    📡 PATH { body }                     — websocket handler (fd/msg in scope)
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

/* ---- main codebook expansion ---- */
char *codebook_expand(const char *src) {
    WebCB web = {0};
    CBuf passthru = {0};

    const char *p = src;
    int found_codebook = 0;

    while (*p) {
        const char *q = cb_skip_ws(p);

        /* 📥 web  OR  use web */
        int cl = cb_cem(q, CP_USE);
        if (!cl) cl = cb_kw(q, "use");
        if (cl) {
            const char *after = cb_skip_ws(q+cl);
            if (strncmp(after,"web",3)==0 && (after[3]=='\0'||after[3]=='\n'||after[3]==' '||after[3]=='\t'||after[3]=='\r'||after[3]==';')) {
                web.active=1; found_codebook=1;
                p=cb_line_end(after+3); if (*p=='\n') p++; continue;
            }
        }
        if (!web.active) { while (*p&&*p!='\n') cb_bc(&passthru,*p++); if (*p=='\n'){cb_bc(&passthru,'\n');p++;} continue; }

        /* 🌐 PORT  OR  listen PORT */
        cl=cb_cem(q,CP_LISTEN);
        if (!cl) cl=cb_kw(q,"listen");
        if (cl) { const char *a=cb_skip_ws(q+cl); web.port=atoi(a); if (web.port<=0) web.port=8080; /* atoi ignores trailing ; */ p=cb_line_end(a); if(*p=='\n')p++; continue; }

        /* route: 📍  OR  line starts with /  OR  starts with GET/POST */
        cl=cb_cem(q,CP_ROUTE);
        int is_route = cl>0;
        int is_ascii_route = 0;
        if (!is_route && (*q=='/' || strncmp(q,"GET ",4)==0 || strncmp(q,"POST ",5)==0)) { is_ascii_route=1; is_route=1; cl=0; }
        if (is_route) {
            Route *r=&web.routes[web.route_count]; memset(r,0,sizeof(*r)); r->method=RM_GET;
            const char *a = is_ascii_route ? q : cb_skip_ws(q+cl);
            if (strncmp(a,"POST",4)==0&&(a[4]==' '||a[4]=='\t')) { r->method=RM_POST; a=cb_skip_ws(a+4); }
            else if (strncmp(a,"GET",3)==0&&(a[3]==' '||a[3]=='\t')) { a=cb_skip_ws(a+3); }
            int pl=cb_read_path(a,r->path,sizeof(r->path)); a=cb_skip_ws(a+pl);
            a = parse_route_type(a, r, &web.needs_grug, &passthru);
            web.route_count++;
            p=cb_line_end(a); while (*p=='\n'||*p=='\r') p++; continue;
        }

        /* websocket: 📡 PATH  OR  ws PATH */
        cl=cb_cem(q,CP_WS);
        if (!cl) cl=cb_kw(q,"ws");
        if (cl) {
            const char *a=cb_skip_ws(q+cl); int wi=web.ws_count;
            int pl=cb_read_path(a,web.ws[wi].path,sizeof(web.ws[wi].path)); a=cb_skip_ws(a+pl);
            int ecl; if ((ecl=cb_cem(a,CP_ECHO))>0) { web.ws[wi].is_echo=1; a+=ecl; }
            else if ((ecl=cb_kw(a,"echo"))>0) { web.ws[wi].is_echo=1; a+=ecl; }
            else if (*a=='{') { web.ws[wi].is_echo=0; int bl=cb_read_block(a,web.ws[wi].body,sizeof(web.ws[wi].body)); a+=bl; }
            web.ws_count++; p=cb_line_end(a); while (*p=='\n'||*p=='\r') p++; continue;
        }

        /* crud NAME field1 field2+ ... → pushes GET(RT_CRUD) + POST(RT_SAVE) */
        cl=cb_kw(q,"crud");
        if (cl) {
            const char *a=cb_skip_ws(q+cl);
            /* read app name */
            char cname[128]; int nl=cb_read_word(a,cname,sizeof(cname)); a=cb_skip_ws(a+nl);
            /* read fields (space-sep, + suffix = textarea) */
            char fraw[512]; int fi=0;     /* raw: "name,msg+" */
            char fclean[512]; int ci=0;   /* clean: "name,msg" (no +, for RT_SAVE) */
            int first_f=1;
            while (*a && *a!='\n' && *a!='\r') {
                char fw[64]; int wl=0;
                while (wl<63&&*a&&*a!=' '&&*a!='\t'&&*a!='\n'&&*a!='\r') { fw[wl++]=*a++; }
                fw[wl]='\0'; a=cb_skip_ws(a);
                if (!wl) break;
                if (!first_f) { fraw[fi++]=','; fclean[ci++]=','; }
                /* copy to fraw as-is (with + marker) */
                for (int j=0; fw[j]; j++) { if (fi<(int)sizeof(fraw)-1) fraw[fi++]=fw[j]; }
                /* copy to fclean without + */
                for (int j=0; fw[j] && fw[j]!='+'; j++) { if (ci<(int)sizeof(fclean)-1) fclean[ci++]=fw[j]; }
                first_f=0;
            }
            fraw[fi]='\0'; fclean[ci]='\0';
            /* build store path */
            char store[256]; snprintf(store,sizeof(store),"%s.grug",cname);
            /* push POST / first (needs priority over GET /) */
            Route *rp=&web.routes[web.route_count]; memset(rp,0,sizeof(*rp));
            rp->kind=RT_SAVE; rp->method=RM_POST; strcpy(rp->path,"/");
            strncpy(rp->content,store,sizeof(rp->content)-1);   /* store path */
            strncpy(rp->fields,fclean,sizeof(rp->fields)-1);    /* clean field names */
            strcpy(rp->redirect,"/");
            web.route_count++;
            /* push GET / → RT_CRUD */
            Route *rg=&web.routes[web.route_count]; memset(rg,0,sizeof(*rg));
            rg->kind=RT_CRUD; rg->method=RM_GET; strcpy(rg->path,"/");
            strncpy(rg->content,cname,sizeof(rg->content)-1);   /* app name */
            strncpy(rg->fields,fraw,sizeof(rg->fields)-1);      /* fields with + markers */
            strncpy(rg->data_path,store,sizeof(rg->data_path)-1); /* store file */
            web.route_count++;
            web.needs_grug=1;
            p=cb_line_end(a); while (*p=='\n'||*p=='\r') p++; continue;
        }

        /* passthrough */
        while (*p&&*p!='\n') cb_bc(&passthru,*p++);
        if (*p=='\n'){cb_bc(&passthru,'\n');p++;}
    }

    if (!found_codebook) { free(passthru.d); return (char*)src; }

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
