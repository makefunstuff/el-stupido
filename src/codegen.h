#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

/* Compile AST to object file. Returns 0 on success.
   target_wasm: 0 = native, 1 = wasm32 */
int codegen(Node *program, const char *out_obj, const char *module_name, int opt_level, int target_wasm);

#endif /* CODEGEN_H */
