#ifndef AST_H
#define AST_H

#include "es.h"
#include "lexer.h"

/* ---- types ---- */
typedef enum {
    TY_I8, TY_I16, TY_I32, TY_I64,
    TY_U8, TY_U16, TY_U32, TY_U64,
    TY_F32, TY_F64,
    TY_VOID,
    TY_PTR,
    TY_ARRAY,
    TY_STRUCT,
    TY_FN,
} TypeKind;

typedef struct EsType {
    TypeKind kind;
    union {
        struct { struct EsType *base; } ptr;
        struct { int size; struct EsType *elem; } array;
        struct { char *name; } strct;
        struct {
            struct EsType *ret;
            struct EsType **params;
            int param_count;
            bool is_vararg;
        } fn;
    };
} EsType;

/* ---- parameter ---- */
typedef struct {
    char *name;
    EsType *type;
} Param;

/* ---- AST nodes ---- */
typedef enum {
    /* declarations */
    ND_PROGRAM,
    ND_EXT_DECL,
    ND_FN_DECL,
    ND_ST_DECL,

    /* statements */
    ND_BLOCK,
    ND_RET,
    ND_EXPR_STMT,
    ND_DECL_STMT,
    ND_ASSIGN,
    ND_IF,
    ND_WHILE,
    ND_BREAK,
    ND_CONTINUE,
    ND_FOR,
    ND_MATCH,
    ND_DEFER,
    ND_ENUM_DECL,

    /* expressions */
    ND_INT_LIT,
    ND_FLOAT_LIT,
    ND_STR_LIT,
    ND_IDENT,
    ND_CALL,
    ND_BINARY,
    ND_UNARY,
    ND_FIELD,
    ND_INDEX,
    ND_CAST,
    ND_TERNARY,
    ND_NULL_LIT,
    ND_STRUCT_INIT,
    ND_SIZEOF,
    ND_INLINE_ASM,
    ND_COMPTIME,
} NodeKind;

typedef struct Node {
    NodeKind kind;
    int line, col;
    EsType *type;   /* set by sema */
    union {
        /* ND_PROGRAM */
        struct { struct Node **decls; int count; } program;

        /* ND_EXT_DECL */
        struct {
            char *name;
            Param *params; int param_count;
            EsType *ret_type;
            bool is_vararg;
        } ext;

        /* ND_FN_DECL */
        struct {
            char *name;
            Param *params; int param_count;
            EsType *ret_type;
            struct Node *body;
        } fn;

        /* ND_ST_DECL */
        struct {
            char *name;
            Param *fields; int field_count;
        } st;

        /* ND_BLOCK */
        struct { struct Node **stmts; int count; } block;

        /* ND_RET */
        struct { struct Node *value; } ret;

        /* ND_EXPR_STMT */
        struct { struct Node *expr; } expr_stmt;

        /* ND_DECL_STMT:  name : type = value  OR  name := value */
        struct {
            char *name;
            EsType *decl_type;      /* NULL if inferred */
            struct Node *init;
        } decl;

        /* ND_ASSIGN */
        struct { struct Node *target; struct Node *value; } assign;

        /* ND_IF */
        struct {
            struct Node *cond;
            struct Node *then_blk;
            struct Node *else_blk;  /* NULL if no else */
        } if_stmt;

        /* ND_WHILE */
        struct { struct Node *cond; struct Node *body; } while_stmt;

        /* ND_FOR — ➰ i:=start..end { body } */
        struct {
            struct Node *init;   /* ND_DECL_STMT */
            struct Node *cond;   /* expression: i < end */
            struct Node *incr;   /* ND_ASSIGN: i = i + 1 */
            struct Node *body;   /* ND_BLOCK */
        } for_stmt;

        /* ND_MATCH — 🎯 expr { val { body } ... _ { default } } */
        struct {
            struct Node *expr;
            struct Node **case_vals;    /* NULL entry = default */
            struct Node **case_bodies;
            int case_count;
        } match_stmt;

        /* ND_DEFER — 🔜 expr */
        struct { struct Node *body; } defer_stmt;

        /* ND_ENUM_DECL — 🏷 Name { A; B; C = 5 } */
        struct {
            char *name;
            char **members;
            int *values;
            int member_count;
        } enum_decl;

        /* ND_INT_LIT */
        struct { int64_t value; } int_lit;

        /* ND_FLOAT_LIT */
        struct { double value; } float_lit;

        /* ND_STR_LIT */
        struct { char *value; int len; } str_lit;

        /* ND_IDENT */
        struct { char *name; } ident;

        /* ND_CALL */
        struct {
            struct Node *callee;
            struct Node **args; int arg_count;
        } call;

        /* ND_BINARY */
        struct {
            TokenKind op;
            struct Node *left;
            struct Node *right;
        } binary;

        /* ND_UNARY */
        struct {
            TokenKind op;
            struct Node *operand;
        } unary;

        /* ND_FIELD */
        struct {
            struct Node *object;
            char *field;
        } field;

        /* ND_INDEX */
        struct {
            struct Node *object;
            struct Node *index;
        } idx;

        /* ND_CAST */
        struct {
            struct Node *expr;
            EsType *target;
        } cast;

        /* ND_TERNARY */
        struct {
            struct Node *cond;
            struct Node *then_expr;
            struct Node *else_expr;
        } ternary;

        /* ND_SIZEOF */
        struct { EsType *target; } size_of;

        /* ND_STRUCT_INIT — ✨ T { field: val, ... } */
        struct {
            EsType *stype;
            char **fields;
            struct Node **vals;
            int field_count;
        } struct_init;

        /* ND_INLINE_ASM — 🔩("template" : "=c"(out),... : "c"(in),... : "clobber",...) */
        struct {
            char *templ;
            char **out_constraints;   struct Node **out_exprs;   int out_count;
            char **in_constraints;    struct Node **in_exprs;    int in_count;
            char **clobbers;          int clobber_count;
            bool is_volatile;
            bool has_side_effects;
        } iasm;

        /* ND_COMPTIME — ⚡ expr */
        struct { struct Node *expr; } comptime;
    };
} Node;

/* ---- constructors ---- */
EsType *type_basic(TypeKind k);
EsType *type_ptr(EsType *base);
EsType *type_array(int size, EsType *elem);
EsType *type_fn(EsType *ret, EsType **params, int pc, bool vararg);

Node *node_new(NodeKind k, int line, int col);

/* ---- helpers ---- */
bool type_is_int(EsType *t);
bool type_is_unsigned(EsType *t);
bool type_is_float(EsType *t);
bool type_is_ptr(EsType *t);
int  type_size(EsType *t);

#endif /* AST_H */
