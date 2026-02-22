#ifndef SEXPR_H
#define SEXPR_H

#include "ast.h"

Node *sexpr_parse(const char *src, const char *file);

#endif /* SEXPR_H */
