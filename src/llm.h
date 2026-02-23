#ifndef LLM_H
#define LLM_H

/* Embedded LLM inference for el-stupido compiler.
   Wraps llama.cpp C API. Only compiled when HAS_LLAMA is defined.

   Usage:
     llm_ctx *ctx = llm_init("model.gguf", grammar_str);
     char *code   = llm_generate(ctx, "make a web server on port 3000");
     // code is now valid codebook syntax, constrained by GBNF grammar
     llm_free(ctx);
*/

typedef struct llm_ctx llm_ctx;

/* Load GGUF model and optional GBNF grammar for constrained generation.
   grammar_str may be NULL for unconstrained generation.
   Returns NULL on failure. */
llm_ctx *llm_init(const char *model_path, const char *grammar_str);

/* Generate text from prompt, constrained by grammar if set.
   Returns malloc'd string (caller frees). NULL on failure.
   max_tokens: max tokens to generate (0 = default 512). */
char *llm_generate(llm_ctx *ctx, const char *prompt, int max_tokens);

/* Free all resources. */
void llm_free(llm_ctx *ctx);

/* Built-in GBNF grammar for codebook syntax. */
const char *llm_codebook_grammar(void);

#endif /* LLM_H */
