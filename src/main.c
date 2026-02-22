#include "es.h"
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "sexpr.h"
#include "codegen.h"
#include "preproc.h"

static void usage(void) {
    fprintf(stderr, "usage: esc <input.es> [-o <output>] [-O<level>] [--emit-ir]\n");
    exit(1);
}

int main(int argc, char **argv) {
    if (argc < 2) usage();

    const char *input = NULL;
    const char *output = "a.out";
    int opt_level = 0;
    bool emit_ir = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) usage();
            output = argv[i];
        } else if (strncmp(argv[i], "-O", 2) == 0) {
            opt_level = atoi(argv[i] + 2);
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            emit_ir = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage();
        } else {
            input = argv[i];
        }
    }

    if (!input) usage();

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
    if (codegen(program, obj_path, input, opt_level) != 0) {
        fprintf(stderr, "compilation failed\n");
        return 1;
    }

    /* link with system cc */
    char link_cmd[1024];
    snprintf(link_cmd, sizeof(link_cmd), "cc \"%s\" -o \"%s\" -lc -lm", obj_path, output);
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
