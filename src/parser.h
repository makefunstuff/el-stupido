#ifndef PARSER_H
#define PARSER_H

#include "ast.h"

typedef struct {
    Lexer lex;
    Token tok;          /* current token */
    const char *file;
} Parser;

void  parser_init(Parser *p, const char *src, const char *file);
Node *parser_parse(Parser *p);           /* returns ND_PROGRAM (auto-loads std) */
Node *parser_parse_prelude(Parser *p);   /* returns ND_PROGRAM (no auto-load) */

#endif /* PARSER_H */
