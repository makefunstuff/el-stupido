#include "es.h"
#include "llm.h"

#include <llama.h>
#include <ggml.h>
#include <ggml-backend.h>

/* ---- GBNF grammar for codebook syntax ---- */

static const char *CODEBOOK_GBNF =
    "# el-stupido codebook grammar\n"
    "# constrains LLM output to valid codebook specs\n"
    "\n"
    "root ::= codebook \"\\n\"\n"
    "\n"
    "codebook ::= web-codebook | cli-codebook | rest-codebook\n"
    "\n"
    "# ---- web codebook ----\n"
    "web-codebook ::= \"use web\" nl listen-dir nl web-dir+\n"
    "listen-dir   ::= \"listen \" port\n"
    "web-dir      ::= route-dir nl | crud-dir nl\n"
    "route-dir    ::= \"/\" path \" \" dqstr\n"
    "crud-dir     ::= \"crud \" ident (\" \" field)+\n"
    "\n"
    "# ---- cli codebook ----\n"
    "cli-codebook ::= \"use cli\" nl name-dir nl desc-dir nl cli-dir+\n"
    "name-dir     ::= \"name \" dqstr\n"
    "desc-dir     ::= \"desc \" dqstr\n"
    "cli-dir      ::= flag-dir nl | arg-dir nl\n"
    "flag-dir     ::= \"flag \" ident \" -\" [a-zA-Z] \" \" dqstr\n"
    "arg-dir      ::= \"arg \" ident \" \" dqstr\n"
    "\n"
    "# ---- rest codebook (supports multiple models) ----\n"
    "rest-codebook ::= \"use rest\" nl \"listen \" port nl rest-line+\n"
    "rest-line     ::= model-dir nl | rest-route\n"
    "model-dir     ::= \"model \" ident (\" \" ident)+\n"
    "rest-route    ::= http-method \" /\" path \" \" rest-action nl\n"
    "http-method   ::= \"GET\" | \"POST\" | \"DELETE\"\n"
    "rest-action   ::= \"list \" ident | \"create \" ident | \"delete \" ident | dqstr\n"
    "\n"
    "# ---- shared rules ----\n"
    "# note: test codebook omitted — it requires imperative fn bodies,\n"
    "# which don't fit the declarative form-fill paradigm.\n"
    "dqstr   ::= \"\\\"\" [a-zA-Z0-9 _.,!?/:{}-]+ \"\\\"\"\n"
    "port    ::= [0-9] [0-9] [0-9]? [0-9]? [0-9]?\n"
    "path    ::= [a-zA-Z0-9_/-]+\n"
    "ident   ::= [a-zA-Z_] [a-zA-Z0-9_]*\n"
    "field   ::= [a-zA-Z_] [a-zA-Z0-9_+]*\n"
    "nl      ::= \"\\n\"\n";

const char *llm_codebook_grammar(void) {
    return CODEBOOK_GBNF;
}

/* ---- LLM context ---- */

struct llm_ctx {
    struct llama_model   *model;
    const struct llama_vocab *vocab;
    const char           *grammar_str;
    int                   n_ctx;
};

/* suppress llama.cpp's extremely verbose logging */
static void llm_log_noop(enum ggml_log_level level, const char *text, void *ud) {
    (void)level; (void)text; (void)ud;
}

llm_ctx *llm_init(const char *model_path, const char *grammar_str) {
    /* silence llama.cpp logging */
    llama_log_set(llm_log_noop, NULL);

    /* init backend */
    llama_backend_init();
    ggml_backend_load_all();

    /* load model */
    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 0;  /* CPU only — this is for edge */

    struct llama_model *model = llama_model_load_from_file(model_path, mparams);
    if (!model) {
        fprintf(stderr, "llm: failed to load model '%s'\n", model_path);
        return NULL;
    }

    const struct llama_vocab *vocab = llama_model_get_vocab(model);

    llm_ctx *ctx = (llm_ctx *)calloc(1, sizeof(llm_ctx));
    ctx->model       = model;
    ctx->vocab       = vocab;
    ctx->grammar_str = grammar_str;  /* may be NULL */
    ctx->n_ctx       = 1024;         /* GPT-2 context length */

    return ctx;
}

char *llm_generate(llm_ctx *ctx, const char *prompt, int max_tokens) {
    if (!ctx || !prompt) return NULL;
    if (max_tokens <= 0) max_tokens = 512;

    /* build the full prompt — MUST match training format exactly:
       ### Instruction:\n...\n### Input:\n...\n### Response:\n */
    static const char *INSTRUCTION =
        "Convert the request into el-stupido codebook syntax. "
        "Output ONLY the codebook code, nothing else.";

    size_t prompt_len = strlen(prompt);
    size_t need = strlen(INSTRUCTION) + prompt_len + 256;  /* padding for framing */
    char *full_prompt = (char *)malloc(need);
    if (!full_prompt) return NULL;
    snprintf(full_prompt, need,
        "### Instruction:\n%s\n\n"
        "### Input:\n%s\n\n"
        "### Response:\n",
        INSTRUCTION, prompt);

    /* tokenize prompt */
    int full_len = (int)strlen(full_prompt);
    int n_prompt_tokens = -llama_tokenize(ctx->vocab,
        full_prompt, full_len, NULL, 0, true, true);

    llama_token *prompt_tokens = (llama_token *)malloc(
        n_prompt_tokens * sizeof(llama_token));
    llama_tokenize(ctx->vocab, full_prompt, full_len,
        prompt_tokens, n_prompt_tokens, true, true);
    free(full_prompt);  /* no longer needed after tokenization */

    /* create inference context */
    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx   = (uint32_t)(n_prompt_tokens + max_tokens);
    cparams.n_batch = (uint32_t)n_prompt_tokens;
    struct llama_context *lctx = llama_init_from_model(ctx->model, cparams);
    if (!lctx) {
        free(prompt_tokens);
        fprintf(stderr, "llm: failed to create context\n");
        return NULL;
    }

    /* create sampler chain */
    struct llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    struct llama_sampler *smpl = llama_sampler_chain_init(sparams);

    /* add grammar constraint if available */
    if (ctx->grammar_str) {
        struct llama_sampler *grammar = llama_sampler_init_grammar(
            ctx->vocab, ctx->grammar_str, "root");
        if (grammar) {
            llama_sampler_chain_add(smpl, grammar);
        } else {
            fprintf(stderr, "llm: warning: grammar parse failed, using unconstrained\n");
        }
    }

    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

    /* eval prompt */
    struct llama_batch batch = llama_batch_get_one(prompt_tokens, n_prompt_tokens);
    if (llama_decode(lctx, batch) != 0) {
        free(prompt_tokens);
        llama_sampler_free(smpl);
        llama_free(lctx);
        fprintf(stderr, "llm: prompt decode failed\n");
        return NULL;
    }

    /* generate output */
    size_t out_cap = 4096;
    size_t out_len = 0;
    char *output = (char *)malloc(out_cap);
    char piece[256];

    for (int i = 0; i < max_tokens; i++) {
        llama_token id = llama_sampler_sample(smpl, lctx, -1);

        /* end of generation? */
        if (llama_vocab_is_eog(ctx->vocab, id)) break;

        /* decode token to text */
        int n = llama_token_to_piece(ctx->vocab, id, piece, sizeof(piece), 0, true);
        if (n <= 0) continue;

        /* grow output buffer if needed */
        while (out_len + (size_t)n + 1 > out_cap) {
            out_cap *= 2;
            output = (char *)realloc(output, out_cap);
        }
        memcpy(output + out_len, piece, (size_t)n);
        out_len += (size_t)n;

        /* feed generated token back */
        batch = llama_batch_get_one(&id, 1);
        if (llama_decode(lctx, batch) != 0) break;
    }

    output[out_len] = '\0';

    /* cleanup inference context (model stays loaded) */
    free(prompt_tokens);
    llama_sampler_free(smpl);
    llama_free(lctx);

    return output;
}

void llm_free(llm_ctx *ctx) {
    if (!ctx) return;
    if (ctx->model) llama_model_free(ctx->model);
    free(ctx);
    llama_backend_free();
}
