#include "es.h"
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "sexpr.h"
#include "codegen.h"
#include "preproc.h"

static void usage(void) {
    fprintf(stderr, "usage: esc <input.es> [-o <output>] [-O<level>] [--wasm] [--emit-ir]\n");
    exit(1);
}

int main(int argc, char **argv) {
    if (argc < 2) usage();

    const char *input = NULL;
    const char *output = NULL;
    int opt_level = 0;
    bool emit_ir = false;
    bool target_wasm = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) usage();
            output = argv[i];
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            opt_level = atoi(argv[i] + 2);
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            emit_ir = true;
        } else if (strcmp(argv[i], "--wasm") == 0) {
            target_wasm = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage();
        } else {
            input = argv[i];
        }
    }

    if (!input) usage();
    if (!output) output = target_wasm ? "out.wasm" : "a.out";
    if (target_wasm) parser_no_std = 1;  /* skip libc std prelude for WASM */

    /* read source */
    char *raw = es_read_file(input);

    /* preprocess: expand 📎 macros before parsing */
    char *src = preprocess(raw);
    if (src != raw) free(raw);

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
