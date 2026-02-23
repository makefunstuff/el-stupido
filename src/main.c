#include "es.h"
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "sexpr.h"
#include "codegen.h"
#include "preproc.h"
#include "codebook.h"
#include "normalize.h"
#include "manifest.h"
#include <sys/stat.h>

#ifdef HAS_LLAMA
#include "llm.h"
#include <time.h>
#endif

static void usage(void) {
    fprintf(stderr,
        "usage: esc <input.es> [-o <output>] [-O<level>] [--wasm] [--emit-ir]\n"
        "           [--dump-expanded] [--dump-normalized]\n"
        "           [--manifest <input.json>] [--manifest-grammar]\n"
        "           [--manifest-expand <input.json>]\n"
#ifdef HAS_LLAMA
        "           [--llm <model.gguf>] [--llm-grammar] [--llm-raw]\n"
        "           [--generate <prompt-file> --llm <model.gguf>]\n"
#endif
    );
    exit(1);
}

int main(int argc, char **argv) {
    if (argc < 2) usage();

    const char *input = NULL;
    const char *output = NULL;
    int opt_level = 0;
    bool emit_ir = false;
    bool target_wasm = false;
    bool dump_expanded = false;
    bool dump_normalized = false;
    const char *manifest_path = NULL;
    bool manifest_expand_only = false;  /* --manifest-expand: print source, don't compile */

#ifdef HAS_LLAMA
    const char *llm_model_path = NULL;
    bool llm_no_grammar = false;  /* --llm-raw disables grammar constraint */
    const char *generate_input = NULL;  /* --generate: LLM -> manifest -> compile */
#endif

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) usage();
            output = argv[i];
        } else if (strcmp(argv[i], "--manifest") == 0) {
            if (++i >= argc) usage();
            manifest_path = argv[i];
        } else if (strcmp(argv[i], "--manifest-expand") == 0) {
            if (++i >= argc) usage();
            manifest_path = argv[i];
            manifest_expand_only = true;
        } else if (strcmp(argv[i], "--manifest-grammar") == 0) {
            printf("%s", manifest_grammar());
            return 0;
#ifdef HAS_LLAMA
        } else if (strcmp(argv[i], "--llm") == 0) {
            if (++i >= argc) usage();
            llm_model_path = argv[i];
        } else if (strcmp(argv[i], "--llm-raw") == 0) {
            llm_no_grammar = true;
        } else if (strcmp(argv[i], "--llm-grammar") == 0) {
            /* print the built-in GBNF grammar and exit */
            printf("%s", llm_codebook_grammar());
            return 0;
        } else if (strcmp(argv[i], "--generate") == 0) {
            if (++i >= argc) usage();
            generate_input = argv[i];
#endif
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            opt_level = atoi(argv[i] + 2);
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            emit_ir = true;
        } else if (strcmp(argv[i], "--wasm") == 0) {
            target_wasm = true;
        } else if (strcmp(argv[i], "--dump-expanded") == 0) {
            dump_expanded = true;
        } else if (strcmp(argv[i], "--dump-normalized") == 0) {
            dump_normalized = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage();
        } else {
            input = argv[i];
        }
    }

    /* ---- Generate mode: natural language -> LLM -> manifest JSON -> compile ---- */
#ifdef HAS_LLAMA
    if (generate_input && llm_model_path) {
        char *prompt_text = es_read_file(generate_input);
        const char *grammar = manifest_grammar();

        fprintf(stderr, "generate: loading model '%s'...\n", llm_model_path);
        llm_ctx *lctx = llm_init(llm_model_path, grammar);
        if (!lctx) {
            fprintf(stderr, "error: failed to init LLM from '%s'\n", llm_model_path);
            free(prompt_text);
            return 1;
        }

        /* Build prompt: system instruction + user description */
        char full_prompt[8192];
        snprintf(full_prompt, sizeof(full_prompt),
            "You are a decision manifest generator. Convert the user's app description "
            "into a JSON decision manifest. Output ONLY valid JSON.\n"
            "The manifest has: domain (crud/rest/cli/test), app (name, port), "
            "models (name, fields with name/type/required).\n"
            "Field types: string, int, bool, text.\n\n"
            "User: %s\n\nJSON:", prompt_text);

        fprintf(stderr, "generate: running LLM with manifest grammar...\n");
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        char *json = llm_generate(lctx, full_prompt, 512);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        llm_free(lctx);
        free(prompt_text);

        double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

        if (!json || json[0] == '\0') {
            fprintf(stderr, "error: LLM generated empty output\n");
            free(json);
            return 1;
        }

        fprintf(stderr, "generate: LLM produced %zu bytes in %.2fs\n", strlen(json), elapsed);
        fprintf(stderr, "--- manifest ---\n%s\n--- end ---\n", json);

        /* Parse the generated manifest */
        Manifest mf;
        if (manifest_parse(json, &mf) != 0) {
            fprintf(stderr, "error: failed to parse generated manifest\n");
            free(json);
            return 1;
        }

        /* Expand */
        char *src = manifest_expand(&mf);
        free(json);
        if (!src) { fprintf(stderr, "error: manifest expansion failed\n"); return 1; }

        fprintf(stderr, "generate: expanded to %zu bytes of el-stupido source\n", strlen(src));

        /* Compile */
        if (!output) output = "a.out";
        char *pp2 = preprocess(src);
        if (pp2 != src) free(src);
        char *expanded2 = codebook_expand(pp2);
        if (expanded2 != pp2) free(pp2);
        char *norm2 = normalize_source(expanded2);
        free(expanded2);

        Parser parser2;
        parser_init(&parser2, norm2, generate_input);
        Node *program2 = parser_parse(&parser2);

        char obj_path2[256];
        snprintf(obj_path2, sizeof(obj_path2), "%s.o", output);
        if (codegen(program2, obj_path2, generate_input, opt_level, 0) != 0) {
            fprintf(stderr, "compilation failed\n"); return 1;
        }

        char link_cmd2[1024];
        snprintf(link_cmd2, sizeof(link_cmd2), "cc \"%s\" -o \"%s\" -lc -lm", obj_path2, output);
        int rc2 = system(link_cmd2);
        if (rc2 != 0) { fprintf(stderr, "linking failed\n"); return 1; }
        remove(obj_path2);

        fprintf(stderr, "generate: compiled -> %s (%.2fs total)\n", output, elapsed);
        free(norm2);
        return 0;
    }
    if (generate_input && !llm_model_path) {
        fprintf(stderr, "error: --generate requires --llm <model.gguf>\n");
        return 1;
    }
#endif

    /* ---- Manifest mode: JSON manifest -> el-stupido source -> compile ---- */
    if (manifest_path) {
        char *json = es_read_file(manifest_path);
        Manifest mf;
        if (manifest_parse(json, &mf) != 0) {
            fprintf(stderr, "error: failed to parse manifest '%s'\n", manifest_path);
            free(json);
            return 1;
        }

        char *src = manifest_expand(&mf);
        free(json);

        if (!src) {
            fprintf(stderr, "error: manifest expansion failed\n");
            return 1;
        }

        /* --manifest-expand: just print expanded source and exit */
        if (manifest_expand_only) {
            fprintf(stdout, "%s", src);
            free(src);
            return 0;
        }

        /* Compile the expanded source */
        if (!output) output = "a.out";
        fprintf(stderr, "manifest: %s -> %zu bytes of el-stupido source\n",
                manifest_path, strlen(src));

        /* Expansion ratio */
        struct stat st;
        if (stat(manifest_path, &st) == 0) {
            double ratio = (double)strlen(src) / (double)st.st_size;
            fprintf(stderr, "manifest: %.0fx expansion (%ld -> %zu bytes)\n",
                    ratio, (long)st.st_size, strlen(src));
        }

        /* Feed expanded source into the normal compiler pipeline */
        char *pp = preprocess(src);
        if (pp != src) free(src);
        char *expanded = codebook_expand(pp);
        if (expanded != pp) free(pp);
        char *norm = normalize_source(expanded);
        free(expanded);

        Node *program;
        Parser parser;
        parser_init(&parser, norm, manifest_path);
        program = parser_parse(&parser);

        char obj_path[256];
        snprintf(obj_path, sizeof(obj_path), "%s.o", output);
        if (codegen(program, obj_path, manifest_path, opt_level, 0) != 0) {
            fprintf(stderr, "compilation failed\n");
            return 1;
        }

        char link_cmd[1024];
        snprintf(link_cmd, sizeof(link_cmd),
            "cc \"%s\" -o \"%s\" -lc -lm", obj_path, output);
        int rc = system(link_cmd);
        if (rc != 0) { fprintf(stderr, "linking failed (exit %d)\n", rc); return 1; }
        remove(obj_path);

        fprintf(stderr, "manifest: compiled -> %s\n", output);
        free(norm);
        return 0;
    }

    if (!input) usage();
    if (!output) output = target_wasm ? "out.wasm" : "a.out";
    if (target_wasm) parser_no_std = 1;  /* skip libc std prelude for WASM */

    /* read source */
    char *raw = es_read_file(input);

    /* ---- LLM pass: natural language -> codebook syntax ---- */
#ifdef HAS_LLAMA
    if (llm_model_path) {
        const char *grammar = llm_no_grammar ? NULL : llm_codebook_grammar();
        llm_ctx *lctx = llm_init(llm_model_path, grammar);
        if (!lctx) {
            fprintf(stderr, "error: failed to init LLM from '%s'\n", llm_model_path);
            free(raw);
            return 1;
        }

        fprintf(stderr, "llm: generating codebook from input...\n");
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        char *generated = llm_generate(lctx, raw, 256);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        llm_free(lctx);

        double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

        if (!generated || generated[0] == '\0') {
            fprintf(stderr, "error: LLM generated empty output\n");
            free(raw);
            free(generated);
            return 1;
        }

        fprintf(stderr, "llm: generated %zu bytes in %.2fs\n", strlen(generated), elapsed);
        fprintf(stderr, "--- generated code ---\n%s\n--- end ---\n", generated);

        /* replace raw input with LLM output */
        free(raw);
        raw = generated;
    }
#endif

    /* preprocess: expand macros before parsing */
    char *pp = preprocess(raw);
    if (pp != raw) free(raw);

    /* codebook: expand domain directives (use web, etc.) */
    char *expanded = codebook_expand(pp);
    if (expanded != pp) free(pp);

    /* --dump-expanded: print codebook/macro-expanded source and exit */
    if (dump_expanded) {
        fprintf(stdout, "%s", expanded);
        free(expanded);
        return 0;
    }

    /* intent normalization: canonicalize friendly aliases before parse */
    char *src = normalize_source(expanded);
    free(expanded);

    /* --dump-normalized: print canonicalized source and exit */
    if (dump_normalized) {
        fprintf(stdout, "%s", src);
        free(src);
        return 0;
    }

    /* parse -- detect front-end by file extension */
    Node *program;
    const char *dot = strrchr(input, '.');
    if (dot && strcmp(dot, ".el") == 0) {
        program = sexpr_parse(src, input);
    } else {
        Parser parser;
        parser_init(&parser, src, input);
        program = parser_parse(&parser);
    }

    /* determine object file path */
    char obj_path[256];
    snprintf(obj_path, sizeof(obj_path), "%s.o", output);

    /* codegen -> object file */
    if (codegen(program, obj_path, input, opt_level, target_wasm ? 1 : 0) != 0) {
        fprintf(stderr, "compilation failed\n");
        return 1;
    }

    /* link */
    char link_cmd[1024];
    if (target_wasm) {
        /* wasm-ld: freestanding, export all, no entry (user calls exports from JS)
           --allow-undefined lets extern decls resolve to WASM imports */
        snprintf(link_cmd, sizeof(link_cmd),
            "wasm-ld \"%s\" -o \"%s\" --no-entry --export-all --allow-undefined "
            "--initial-memory=1048576 --max-memory=16777216",
            obj_path, output);
    } else {
        snprintf(link_cmd, sizeof(link_cmd),
            "cc \"%s\" -o \"%s\" -lc -lm", obj_path, output);
    }

    int rc = system(link_cmd);
    if (rc != 0) {
        fprintf(stderr, "linking failed (exit %d)\n", rc);
        return 1;
    }

    /* cleanup temp object file */
    remove(obj_path);

    (void)emit_ir; /* TODO: use this flag */
    free(src);
    return 0;
}
