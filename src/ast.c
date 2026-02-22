#include "ast.h"

EsType *type_basic(TypeKind k) {
    EsType *t = (EsType *)calloc(1, sizeof(EsType));
    t->kind = k;
    return t;
}

EsType *type_ptr(EsType *base) {
    EsType *t = (EsType *)calloc(1, sizeof(EsType));
    t->kind = TY_PTR;
    t->ptr.base = base;
    return t;
}

EsType *type_array(int size, EsType *elem) {
    EsType *t = (EsType *)calloc(1, sizeof(EsType));
    t->kind = TY_ARRAY;
    t->array.size = size;
    t->array.elem = elem;
    return t;
}

EsType *type_fn(EsType *ret, EsType **params, int pc, bool vararg) {
    EsType *t = (EsType *)calloc(1, sizeof(EsType));
    t->kind = TY_FN;
    t->fn.ret = ret;
    t->fn.params = params;
    t->fn.param_count = pc;
    t->fn.is_vararg = vararg;
    return t;
}

Node *node_new(NodeKind k, int line, int col) {
    Node *n = (Node *)calloc(1, sizeof(Node));
    n->kind = k;
    n->line = line;
    n->col = col;
    return n;
}

bool type_is_int(EsType *t) {
    return t && t->kind >= TY_I8 && t->kind <= TY_U64;
}

bool type_is_unsigned(EsType *t) {
    return t && t->kind >= TY_U8 && t->kind <= TY_U64;
}

bool type_is_float(EsType *t) {
    return t && (t->kind == TY_F32 || t->kind == TY_F64);
}

bool type_is_ptr(EsType *t) {
    return t && t->kind == TY_PTR;
}

int type_size(EsType *t) {
    if (!t) return 0;
    switch (t->kind) {
    case TY_I8:  case TY_U8:  return 1;
    case TY_I16: case TY_U16: return 2;
    case TY_I32: case TY_U32: case TY_F32: return 4;
    case TY_I64: case TY_U64: case TY_F64: return 8;
    case TY_PTR: return 8;
    case TY_VOID: return 0;
    case TY_ARRAY: return t->array.size * type_size(t->array.elem);
    default: return 0;
    }
}
