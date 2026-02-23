#ifndef LEXER_H
#define LEXER_H

#include "es.h"

typedef enum {
    /* keywords — group base 0, room for 32 */
    TOK_EXT = 0, TOK_FN, TOK_RET, TOK_IF, TOK_EL, TOK_WH,
    TOK_ST, TOK_USE, TOK_AS, TOK_SZ, TOK_NULL_KW,
    TOK_BRK, TOK_CONT, TOK_NW, TOK_DEL,
    TOK_ASM, TOK_CT,
    TOK_FOR, TOK_MATCH, TOK_ENUM, TOK_DEFER, TOK_VAR,
    /* keyword slots 22..31 reserved */

    /* type keywords — group base 32 */
    TOK_I8 = 32, TOK_I16, TOK_I32, TOK_I64,
    TOK_U8, TOK_U16, TOK_U32, TOK_U64,
    TOK_F32, TOK_F64, TOK_VOID,
    TOK_BOOL,
    /* type slots 44..47 reserved */

    /* literals — group base 48 */
    TOK_INT_LIT = 48, TOK_FLOAT_LIT, TOK_STR_LIT,

    /* identifier — group base 52 */
    TOK_IDENT = 52,

    /* operators — group base 64 */
    TOK_PLUS = 64, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE, TOK_BANG,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LEQ, TOK_GEQ,
    TOK_LAND, TOK_LOR,
    TOK_SHL, TOK_SHR,
    TOK_QUESTION,      /* ? */
    TOK_ASSIGN,        /* = */
    TOK_PLUS_EQ,       /* += */
    TOK_MINUS_EQ,      /* -= */
    TOK_STAR_EQ,       /* *= */
    TOK_SLASH_EQ,      /* /= */
    TOK_PERCENT_EQ,    /* %= */
    TOK_DECL_ASSIGN,   /* := */
    TOK_COLON,         /* : */
    TOK_ARROW,         /* -> */
    TOK_DOT,           /* . */
    TOK_ELLIPSIS,      /* ... */
    TOK_RANGE,         /* .. */
    TOK_RANGE_INC,     /* ..= */
    TOK_PIPE_OP,       /* |> */
    TOK_COMMA,         /* , */
    /* operator slots ..111 reserved */

    /* delimiters — group base 112 */
    TOK_LPAREN = 112, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,

    /* special — group base 120 */
    TOK_SEMI = 120,    /* ; (same as newline) */
    TOK_NEWLINE, TOK_EOF, TOK_ERROR,
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *start;
    int len;
    int line;
    int col;
    union {
        int64_t int_val;
        double float_val;
        struct { char *data; int len; } str_val;
    };
} Token;

typedef struct {
    const char *src;
    const char *cur;
    const char *filename;
    int line;
    int col;
} Lexer;

void  lexer_init(Lexer *l, const char *src, const char *filename);
Token lexer_next(Lexer *l);
const char *tok_str(TokenKind k);

#endif /* LEXER_H */
