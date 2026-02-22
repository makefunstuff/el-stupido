#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

/* Compile AST to object file. Returns 0 on success. */
int codegen(Node *program, const char *out_obj, const char *module_name, int opt_level);

#endif /* CODEGEN_H */
