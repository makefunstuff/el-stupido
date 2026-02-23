#include "codegen.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>

/* ---- struct definition ---- */
typedef struct {
    char *name;
    char **field_names;
    EsType **field_types;
    int field_count;
    LLVMTypeRef llvm_type;
} StructDef;

/* ---- codegen context ---- */
typedef struct {
    LLVMContextRef ctx;
    LLVMModuleRef  mod;
    LLVMBuilderRef bld;

    struct Symbol {
        char          *name;
        LLVMValueRef   value;
        EsType        *type;
        LLVMTypeRef    llvm_fn_type;
    } syms[1024];
    int sym_count;

    StructDef structs[128];
    int struct_count;

    LLVMValueRef  cur_fn;
    EsType       *cur_ret_type;

    /* loop control flow for break/continue */
    LLVMBasicBlockRef loop_cond_bb;   /* continue target */
    LLVMBasicBlockRef loop_end_bb;    /* break target */

    /* defer stack */
    Node *defers[64];
    int defer_count;
} CG;

/* ---- symbol table ---- */
static void sym_push(CG *g, const char *name, LLVMValueRef val, EsType *ty, LLVMTypeRef ft) {
    assert(g->sym_count < 1024);
    g->syms[g->sym_count].name = es_strdup(name);
    g->syms[g->sym_count].value = val;
    g->syms[g->sym_count].type = ty;
    g->syms[g->sym_count].llvm_fn_type = ft;
    g->sym_count++;
}

static struct Symbol *sym_lookup(CG *g, const char *name) {
    for (int i = g->sym_count - 1; i >= 0; i--)
        if (strcmp(g->syms[i].name, name) == 0) return &g->syms[i];
    return NULL;
}

/* ---- struct table ---- */
static StructDef *struct_lookup(CG *g, const char *name) {
    for (int i = 0; i < g->struct_count; i++)
        if (strcmp(g->structs[i].name, name) == 0) return &g->structs[i];
    return NULL;
}

static int struct_field_index(StructDef *sd, const char *name) {
    for (int i = 0; i < sd->field_count; i++)
        if (strcmp(sd->field_names[i], name) == 0) return i;
    return -1;
}

/* resolve a struct EsType name to its StructDef */
static StructDef *resolve_struct(CG *g, EsType *t) {
    if (!t) return NULL;
    if (t->kind == TY_STRUCT) return struct_lookup(g, t->strct.name);
    if (t->kind == TY_PTR && t->ptr.base && t->ptr.base->kind == TY_STRUCT)
        return struct_lookup(g, t->ptr.base->strct.name);
    return NULL;
}

/* ---- type mapping ---- */
static LLVMTypeRef es_to_llvm(CG *g, EsType *t) {
    if (!t) return LLVMVoidTypeInContext(g->ctx);
    switch (t->kind) {
    case TY_I8:  case TY_U8:  return LLVMInt8TypeInContext(g->ctx);
    case TY_I16: case TY_U16: return LLVMInt16TypeInContext(g->ctx);
    case TY_I32: case TY_U32: return LLVMInt32TypeInContext(g->ctx);
    case TY_I64: case TY_U64: return LLVMInt64TypeInContext(g->ctx);
    case TY_F32: return LLVMFloatTypeInContext(g->ctx);
    case TY_F64: return LLVMDoubleTypeInContext(g->ctx);
    case TY_VOID: return LLVMVoidTypeInContext(g->ctx);
    case TY_PTR:  return LLVMPointerTypeInContext(g->ctx, 0);
    case TY_ARRAY: return LLVMArrayType2(es_to_llvm(g, t->array.elem), t->array.size);
    case TY_STRUCT: {
        StructDef *sd = struct_lookup(g, t->strct.name);
        if (!sd) es_fatal("undefined struct '%s'", t->strct.name);
        return sd->llvm_type;
    }
    default:
        es_fatal("unsupported type in codegen (kind=%d)", t->kind);
        return NULL;
    }
}

static LLVMTypeRef build_fn_type(CG *g, EsType *ret, Param *params, int pc, bool vararg) {
    LLVMTypeRef *pt = NULL;
    if (pc > 0) {
        pt = (LLVMTypeRef *)malloc(pc * sizeof(LLVMTypeRef));
        for (int i = 0; i < pc; i++) pt[i] = es_to_llvm(g, params[i].type);
    }
    LLVMTypeRef ft = LLVMFunctionType(es_to_llvm(g, ret), pt, pc, vararg);
    free(pt);
    return ft;
}

/* ---- type helpers ---- */
static bool is_float_kind(LLVMTypeKind k) {
    return k == LLVMFloatTypeKind || k == LLVMDoubleTypeKind;
}

/* ---- bool conversion helper ---- */
static LLVMValueRef to_bool(CG *g, LLVMValueRef val) {
    LLVMTypeKind k = LLVMGetTypeKind(LLVMTypeOf(val));
    if (k == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(LLVMTypeOf(val)) == 1)
        return val;
    if (is_float_kind(k))
        return LLVMBuildFCmp(g->bld, LLVMRealONE, val,
                   LLVMConstReal(LLVMTypeOf(val), 0.0), "tobool");
    return LLVMBuildICmp(g->bld, LLVMIntNE, val,
               LLVMConstInt(LLVMTypeOf(val), 0, 0), "tobool");
}

/* ---- type coercion ---- */
static LLVMValueRef coerce(CG *g, LLVMValueRef val, LLVMTypeRef target) {
    LLVMTypeRef src = LLVMTypeOf(val);
    if (src == target) return val;
    LLVMTypeKind sk = LLVMGetTypeKind(src);
    LLVMTypeKind tk = LLVMGetTypeKind(target);

    if (sk == LLVMIntegerTypeKind && tk == LLVMIntegerTypeKind) {
        unsigned sw = LLVMGetIntTypeWidth(src);
        unsigned tw = LLVMGetIntTypeWidth(target);
        if (tw > sw) return LLVMBuildZExt(g->bld, val, target, "widen");
        if (tw < sw) return LLVMBuildTrunc(g->bld, val, target, "narrow");
        return val;
    }
    /* int -> float */
    if (sk == LLVMIntegerTypeKind && is_float_kind(tk))
        return LLVMBuildSIToFP(g->bld, val, target, "i2f");
    /* float -> int */
    if (is_float_kind(sk) && tk == LLVMIntegerTypeKind)
        return LLVMBuildFPToSI(g->bld, val, target, "f2i");
    /* float -> float (f32 <-> f64) */
    if (is_float_kind(sk) && is_float_kind(tk))
        return LLVMBuildFPCast(g->bld, val, target, "fcast");
    if (sk == LLVMPointerTypeKind && tk == LLVMPointerTypeKind)
        return val; /* opaque pointers are all the same */
    if (sk == LLVMIntegerTypeKind && tk == LLVMPointerTypeKind)
        return LLVMBuildIntToPtr(g->bld, val, target, "i2p");
    if (sk == LLVMPointerTypeKind && tk == LLVMIntegerTypeKind)
        return LLVMBuildPtrToInt(g->bld, val, target, "p2i");
    return val;
}

/* ---- expression codegen ---- */
static LLVMValueRef cg_expr(CG *g, Node *n);

static LLVMValueRef cg_builtin_print(CG *g, Node *n) {
    if (n->call.arg_count < 1) es_fatal("print requires at least 1 argument");

    /* Look up printf */
    struct Symbol *printf_sym = sym_lookup(g, "printf");
    if (!printf_sym) es_fatal("print requires printf (load std prelude)");

    LLVMValueRef val = cg_expr(g, n->call.args[0]);
    EsType *ty = n->call.args[0]->type;

    const char *fmt;
    LLVMValueRef cast_val = val;

    if (!ty || type_is_int(ty)) {
        if (ty && (ty->kind == TY_I64 || ty->kind == TY_U64)) {
            fmt = "%lld\n";
        } else {
            fmt = "%d\n";
        }
    } else if (type_is_float(ty)) {
        fmt = "%f\n";
        if (ty->kind == TY_F32)
            cast_val = LLVMBuildFPExt(g->bld, val, LLVMDoubleTypeInContext(g->ctx), "fpext");
    } else if (type_is_ptr(ty)) {
        /* assume *u8 = string */
        fmt = "%s\n";
    } else {
        fmt = "%d\n"; /* fallback */
    }

    LLVMValueRef fmt_str = LLVMBuildGlobalStringPtr(g->bld, fmt, "print_fmt");
    LLVMValueRef args[2] = { fmt_str, cast_val };

    LLVMTypeRef printf_type = printf_sym->llvm_fn_type;
    return LLVMBuildCall2(g->bld, printf_type, printf_sym->value, args, 2, "");
}

/* Expand product(start..end) or product(start..=end) to accumulator loop */
static LLVMValueRef cg_builtin_reduce(CG *g, Node *n, const char *name) {
    if (n->call.arg_count != 1) es_fatal("%s requires exactly 1 range argument", name);

    Node *arg = n->call.args[0];
    if (arg->kind != ND_BINARY || (arg->binary.op != TOK_RANGE && arg->binary.op != TOK_RANGE_INC))
        es_fatal("%s argument must be a range (start..end or start..=end)", name);

    bool inclusive = (arg->binary.op == TOK_RANGE_INC);
    LLVMValueRef start_val = cg_expr(g, arg->binary.left);
    LLVMValueRef end_val = cg_expr(g, arg->binary.right);

    /* coerce to i32 */
    LLVMTypeRef i32_ty = LLVMInt32TypeInContext(g->ctx);
    start_val = LLVMBuildIntCast2(g->bld, start_val, i32_ty, true, "s");
    end_val = LLVMBuildIntCast2(g->bld, end_val, i32_ty, true, "e");

    /* determine init value and operation */
    bool is_product = (strcmp(name, "product") == 0);
    bool is_sum = (strcmp(name, "sum") == 0);
    bool is_count = (strcmp(name, "count") == 0);
    bool is_min = (strcmp(name, "min") == 0);
    bool is_max = (strcmp(name, "max") == 0);

    LLVMValueRef init;
    if (is_product) init = LLVMConstInt(i32_ty, 1, false);
    else if (is_min) init = LLVMConstInt(i32_ty, 2147483647, false);
    else if (is_max) init = LLVMConstInt(i32_ty, -2147483648ULL, true);
    else init = LLVMConstInt(i32_ty, 0, false); /* sum, count */

    /* alloca for accumulator and iterator */
    LLVMValueRef acc_ptr = LLVMBuildAlloca(g->bld, i32_ty, "acc");
    LLVMValueRef i_ptr = LLVMBuildAlloca(g->bld, i32_ty, "i");
    LLVMBuildStore(g->bld, init, acc_ptr);
    LLVMBuildStore(g->bld, start_val, i_ptr);

    /* basic blocks */
    LLVMValueRef fn = g->cur_fn;
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "red_cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "red_body");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "red_end");

    LLVMBuildBr(g->bld, cond_bb);

    /* condition: i < end (exclusive) or i <= end (inclusive) */
    LLVMPositionBuilderAtEnd(g->bld, cond_bb);
    LLVMValueRef i_val = LLVMBuildLoad2(g->bld, i32_ty, i_ptr, "iv");
    LLVMValueRef cmp = inclusive
        ? LLVMBuildICmp(g->bld, LLVMIntSLE, i_val, end_val, "cmp")
        : LLVMBuildICmp(g->bld, LLVMIntSLT, i_val, end_val, "cmp");
    LLVMBuildCondBr(g->bld, cmp, body_bb, end_bb);

    /* body: acc = acc OP i */
    LLVMPositionBuilderAtEnd(g->bld, body_bb);
    LLVMValueRef acc_val = LLVMBuildLoad2(g->bld, i32_ty, acc_ptr, "av");
    LLVMValueRef i_val2 = LLVMBuildLoad2(g->bld, i32_ty, i_ptr, "iv2");

    LLVMValueRef new_acc;
    if (is_product) new_acc = LLVMBuildMul(g->bld, acc_val, i_val2, "mul");
    else if (is_sum) new_acc = LLVMBuildAdd(g->bld, acc_val, i_val2, "add");
    else if (is_count) new_acc = LLVMBuildAdd(g->bld, acc_val, LLVMConstInt(i32_ty, 1, false), "cnt");
    else if (is_min) {
        LLVMValueRef lt = LLVMBuildICmp(g->bld, LLVMIntSLT, i_val2, acc_val, "lt");
        new_acc = LLVMBuildSelect(g->bld, lt, i_val2, acc_val, "min");
    } else { /* max */
        LLVMValueRef gt = LLVMBuildICmp(g->bld, LLVMIntSGT, i_val2, acc_val, "gt");
        new_acc = LLVMBuildSelect(g->bld, gt, i_val2, acc_val, "max");
    }

    LLVMBuildStore(g->bld, new_acc, acc_ptr);

    /* increment: i = i + 1 */
    LLVMValueRef one = LLVMConstInt(i32_ty, 1, false);
    LLVMValueRef next_i = LLVMBuildAdd(g->bld, i_val2, one, "next");
    LLVMBuildStore(g->bld, next_i, i_ptr);
    LLVMBuildBr(g->bld, cond_bb);

    /* end: load result */
    LLVMPositionBuilderAtEnd(g->bld, end_bb);
    return LLVMBuildLoad2(g->bld, i32_ty, acc_ptr, "result");
}

/* infer EsType from expression (call after cg_expr sets n->type) */
static EsType *infer_expr_type(CG *g, Node *n) {
    if (n->type) return n->type;
    switch (n->kind) {
    case ND_INT_LIT:   return type_basic(TY_I32);
    case ND_FLOAT_LIT: return type_basic(TY_F64);
    case ND_STR_LIT:   return type_ptr(type_basic(TY_U8));
    case ND_NULL_LIT: return type_ptr(type_basic(TY_VOID));
    case ND_IDENT: {
        struct Symbol *s = sym_lookup(g, n->ident.name);
        return s ? s->type : NULL;
    }
    case ND_CALL: {
        Node *callee = n->call.callee;
        if (callee->kind == ND_IDENT) {
            struct Symbol *s = sym_lookup(g, callee->ident.name);
            if (s && s->type && s->type->kind == TY_FN) return s->type->fn.ret;
        }
        /* UFCS: obj.func(args) — look up func as a global function */
        if (callee->kind == ND_FIELD) {
            struct Symbol *s = sym_lookup(g, callee->field.field);
            if (s && s->type && s->type->kind == TY_FN) return s->type->fn.ret;
        }
        return type_basic(TY_I32);
    }
    case ND_CAST: return n->cast.target;
    case ND_UNARY:
        if (n->unary.op == TOK_AMP) {
            EsType *inner = infer_expr_type(g, n->unary.operand);
            return inner ? type_ptr(inner) : type_ptr(type_basic(TY_VOID));
        }
        if (n->unary.op == TOK_STAR) {
            EsType *inner = infer_expr_type(g, n->unary.operand);
            return (inner && inner->kind == TY_PTR) ? inner->ptr.base : type_basic(TY_I32);
        }
        return infer_expr_type(g, n->unary.operand);
    case ND_BINARY: {
        if (n->binary.op >= TOK_EQ && n->binary.op <= TOK_GEQ) return type_basic(TY_I32);
        EsType *lt = infer_expr_type(g, n->binary.left);
        EsType *rt = infer_expr_type(g, n->binary.right);
        /* promote to float if either side is float */
        if (type_is_float(lt) || type_is_float(rt)) return type_basic(TY_F64);
        return lt;
    }
    case ND_FIELD: {
        EsType *obj_ty = infer_expr_type(g, n->field.object);
        StructDef *sd = resolve_struct(g, obj_ty);
        if (sd) {
            int idx = struct_field_index(sd, n->field.field);
            if (idx >= 0) return sd->field_types[idx];
        }
        return type_basic(TY_I32);
    }
    case ND_INDEX: {
        EsType *obj_ty = infer_expr_type(g, n->idx.object);
        if (obj_ty && obj_ty->kind == TY_ARRAY) return obj_ty->array.elem;
        if (obj_ty && obj_ty->kind == TY_PTR)   return obj_ty->ptr.base;
        return type_basic(TY_I32);
    }
    case ND_TERNARY: return infer_expr_type(g, n->ternary.then_expr);
    case ND_SIZEOF: return type_basic(TY_I64);
    case ND_STRUCT_INIT: return type_ptr(n->struct_init.stype);
    default: return type_basic(TY_I32);
    }
}

/* generate address of an lvalue (returns pointer, does NOT load) */
static LLVMValueRef cg_lvalue(CG *g, Node *n, EsType **out_type) {
    switch (n->kind) {
    case ND_IDENT: {
        struct Symbol *sym = sym_lookup(g, n->ident.name);
        if (!sym) es_fatal("undefined '%s'", n->ident.name);
        if (out_type) *out_type = sym->type;
        return sym->value;
    }
    case ND_FIELD: {
        /* obj.field -- obj could be a struct value or a pointer to struct */
        EsType *obj_ty = infer_expr_type(g, n->field.object);
        StructDef *sd = resolve_struct(g, obj_ty);
        if (!sd) es_fatal("field access on non-struct type");
        int idx = struct_field_index(sd, n->field.field);
        if (idx < 0) es_fatal("struct '%s' has no field '%s'", sd->name, n->field.field);

        LLVMValueRef base;
        if (obj_ty->kind == TY_PTR) {
            /* pointer to struct: load pointer, then GEP */
            base = cg_expr(g, n->field.object);
        } else {
            /* struct value: get its alloca address */
            base = cg_lvalue(g, n->field.object, NULL);
        }

        LLVMValueRef indices[2] = {
            LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
            LLVMConstInt(LLVMInt32TypeInContext(g->ctx), idx, 0)
        };
        LLVMValueRef field_ptr = LLVMBuildGEP2(g->bld, sd->llvm_type, base, indices, 2, "fptr");
        if (out_type) *out_type = sd->field_types[idx];
        return field_ptr;
    }
    case ND_UNARY:
        if (n->unary.op == TOK_STAR) {
            /* *p as lvalue = the pointer p itself */
            LLVMValueRef ptr = cg_expr(g, n->unary.operand);
            EsType *operand_ty = infer_expr_type(g, n->unary.operand);
            if (out_type) {
                *out_type = (operand_ty && operand_ty->kind == TY_PTR)
                    ? operand_ty->ptr.base : type_basic(TY_I32);
            }
            return ptr;
        }
        break;
    case ND_INDEX: {
        EsType *obj_ty = infer_expr_type(g, n->idx.object);
        LLVMValueRef base = cg_lvalue(g, n->idx.object, NULL);
        LLVMValueRef index = cg_expr(g, n->idx.index);

        LLVMTypeRef elem_llvm;
        EsType *elem_ty;
        if (obj_ty && obj_ty->kind == TY_ARRAY) {
            elem_ty = obj_ty->array.elem;
            elem_llvm = es_to_llvm(g, elem_ty);
            LLVMValueRef indices[2] = {
                LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), index
            };
            LLVMValueRef ptr = LLVMBuildGEP2(g->bld, es_to_llvm(g, obj_ty), base, indices, 2, "idx");
            if (out_type) *out_type = elem_ty;
            return ptr;
        }
        /* pointer indexing */
        elem_ty = (obj_ty && obj_ty->kind == TY_PTR) ? obj_ty->ptr.base : type_basic(TY_I32);
        elem_llvm = es_to_llvm(g, elem_ty);
        LLVMValueRef loaded = LLVMBuildLoad2(g->bld, LLVMPointerTypeInContext(g->ctx, 0), base, "lp");
        LLVMValueRef ptr = LLVMBuildGEP2(g->bld, elem_llvm, loaded, &index, 1, "idx");
        if (out_type) *out_type = elem_ty;
        return ptr;
    }
    default: break;
    }
    es_fatal("expression is not an lvalue");
    return NULL;
}

static LLVMValueRef cg_call(CG *g, Node *n) {
    Node *callee = n->call.callee;
    struct Symbol *sym = NULL;
    LLVMValueRef self_val = 0;   /* UFCS: the object being piped in */
    bool is_ufcs = false;

    if (callee->kind == ND_IDENT) {
        sym = sym_lookup(g, callee->ident.name);
        if (!sym) {
            /* check for builtin functions */
            if (strcmp(callee->ident.name, "print") == 0) {
                return cg_builtin_print(g, n);
            }
            if (strcmp(callee->ident.name, "product") == 0 ||
                strcmp(callee->ident.name, "sum") == 0 ||
                strcmp(callee->ident.name, "count") == 0 ||
                strcmp(callee->ident.name, "min") == 0 ||
                strcmp(callee->ident.name, "max") == 0) {
                return cg_builtin_reduce(g, n, callee->ident.name);
            }
            es_fatal("undefined function '%s'", callee->ident.name);
        }
        /* function pointer variable: *fn(...) -> ret */
        if (!sym->llvm_fn_type && sym->type &&
            sym->type->kind == TY_PTR && sym->type->ptr.base &&
            sym->type->ptr.base->kind == TY_FN) {
            EsType *ft = sym->type->ptr.base;
            LLVMTypeRef *pt = NULL;
            if (ft->fn.param_count > 0) {
                pt = (LLVMTypeRef *)malloc(ft->fn.param_count * sizeof(LLVMTypeRef));
                for (int i = 0; i < ft->fn.param_count; i++) pt[i] = es_to_llvm(g, ft->fn.params[i]);
            }
            LLVMTypeRef fnt = LLVMFunctionType(es_to_llvm(g, ft->fn.ret), pt, ft->fn.param_count, ft->fn.is_vararg);
            free(pt);
            LLVMValueRef fp = LLVMBuildLoad2(g->bld, LLVMPointerTypeInContext(g->ctx, 0), sym->value, "fp");
            int argc = n->call.arg_count;
            LLVMValueRef *args = (LLVMValueRef *)malloc(argc * sizeof(LLVMValueRef));
            for (int i = 0; i < argc; i++) {
                args[i] = cg_expr(g, n->call.args[i]);
                if (i < ft->fn.param_count && ft->fn.params[i])
                    args[i] = coerce(g, args[i], es_to_llvm(g, ft->fn.params[i]));
            }
            LLVMTypeRef ret_llvm = LLVMGetReturnType(fnt);
            const char *cname = (LLVMGetTypeKind(ret_llvm) == LLVMVoidTypeKind) ? "" : "fpcall";
            LLVMValueRef result = LLVMBuildCall2(g->bld, fnt, fp, args, argc, cname);
            n->type = ft->fn.ret;
            free(args);
            return result;
        }
    } else if (callee->kind == ND_FIELD) {
        /* UFCS: obj.func(args) -> func(obj, args) */
        /* first check if func is a known function */
        sym = sym_lookup(g, callee->field.field);
        if (sym && sym->llvm_fn_type) {
            /* it's a function, use UFCS */
            self_val = cg_expr(g, callee->field.object);
            is_ufcs = true;
        } else {
            /* not a function -- could be calling through a function pointer field */
            es_fatal("'%s' is not a function (UFCS lookup failed)", callee->field.field);
        }
    } else {
        es_fatal("unsupported callee expression in call");
    }

    LLVMTypeRef fn_type = sym->llvm_fn_type;
    if (!fn_type) es_fatal("'%s' is not a function", sym->name);

    int orig_argc = n->call.arg_count;
    int total_argc = is_ufcs ? orig_argc + 1 : orig_argc;
    int param_count = sym->type ? sym->type->fn.param_count : 0;

    LLVMValueRef *args = (LLVMValueRef *)malloc(total_argc * sizeof(LLVMValueRef));
    int a = 0;

    if (is_ufcs) {
        args[a] = self_val;
        if (a < param_count && sym->type && sym->type->fn.params[a])
            args[a] = coerce(g, args[a], es_to_llvm(g, sym->type->fn.params[a]));
        a++;
    }

    for (int i = 0; i < orig_argc; i++, a++) {
        args[a] = cg_expr(g, n->call.args[i]);
        if (a < param_count && sym->type && sym->type->fn.params[a])
            args[a] = coerce(g, args[a], es_to_llvm(g, sym->type->fn.params[a]));
    }

    LLVMTypeRef ret_llvm = LLVMGetReturnType(fn_type);
    const char *name = (LLVMGetTypeKind(ret_llvm) == LLVMVoidTypeKind) ? "" : "call";
    LLVMValueRef result = LLVMBuildCall2(g->bld, fn_type, sym->value, args, total_argc, name);

    if (sym->type && sym->type->kind == TY_FN)
        n->type = sym->type->fn.ret;

    free(args);
    return result;
}

static LLVMValueRef cg_expr(CG *g, Node *n) {
    switch (n->kind) {
    case ND_INT_LIT:
        n->type = type_basic(TY_I32);
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), n->int_lit.value, 0);

    case ND_FLOAT_LIT:
        n->type = type_basic(TY_F64);
        return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), n->float_lit.value);

    case ND_STR_LIT:
        n->type = type_ptr(type_basic(TY_U8));
        return LLVMBuildGlobalStringPtr(g->bld, n->str_lit.value, "str");

    case ND_NULL_LIT:
        n->type = type_ptr(type_basic(TY_VOID));
        return LLVMConstPointerNull(LLVMPointerTypeInContext(g->ctx, 0));

    case ND_IDENT: {
        struct Symbol *sym = sym_lookup(g, n->ident.name);
        if (!sym) es_fatal("undefined '%s'", n->ident.name);
        n->type = sym->type;
        if (sym->type && sym->type->kind != TY_FN) {
            return LLVMBuildLoad2(g->bld, es_to_llvm(g, sym->type), sym->value, n->ident.name);
        }
        return sym->value;
    }

    case ND_CALL:
        return cg_call(g, n);

    case ND_FIELD: {
        EsType *field_ty = NULL;
        LLVMValueRef field_ptr = cg_lvalue(g, n, &field_ty);
        n->type = field_ty;
        return LLVMBuildLoad2(g->bld, es_to_llvm(g, field_ty), field_ptr, n->field.field);
    }

    case ND_INDEX: {
        EsType *elem_ty = NULL;
        LLVMValueRef elem_ptr = cg_lvalue(g, n, &elem_ty);
        n->type = elem_ty;
        return LLVMBuildLoad2(g->bld, es_to_llvm(g, elem_ty), elem_ptr, "elem");
    }

    case ND_BINARY: {
        /* short-circuit &&/|| — RHS evaluated conditionally */
        if (n->binary.op == TOK_LAND || n->binary.op == TOK_LOR) {
            LLVMValueRef lv = cg_expr(g, n->binary.left);
            LLVMValueRef lb = to_bool(g, lv);
            LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->bld);
            LLVMBasicBlockRef rhs_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "sc.rhs");
            LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "sc.end");
            if (n->binary.op == TOK_LAND)
                LLVMBuildCondBr(g->bld, lb, rhs_bb, merge_bb);
            else
                LLVMBuildCondBr(g->bld, lb, merge_bb, rhs_bb);
            LLVMPositionBuilderAtEnd(g->bld, rhs_bb);
            LLVMValueRef rv = cg_expr(g, n->binary.right);
            LLVMValueRef rb = to_bool(g, rv);
            LLVMBasicBlockRef rhs_end = LLVMGetInsertBlock(g->bld);
            LLVMBuildBr(g->bld, merge_bb);
            LLVMPositionBuilderAtEnd(g->bld, merge_bb);
            LLVMValueRef phi = LLVMBuildPhi(g->bld, LLVMInt1TypeInContext(g->ctx), "sc");
            LLVMValueRef sv = LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                n->binary.op == TOK_LAND ? 0 : 1, 0);
            LLVMValueRef vals[2] = { sv, rb };
            LLVMBasicBlockRef bbs[2] = { entry_bb, rhs_end };
            LLVMAddIncoming(phi, vals, bbs, 2);
            n->type = type_basic(TY_I32);
            return phi;
        }

        LLVMValueRef left = cg_expr(g, n->binary.left);
        LLVMValueRef right = cg_expr(g, n->binary.right);
        LLVMTypeRef lt = LLVMTypeOf(left), rt = LLVMTypeOf(right);
        LLVMTypeKind lk = LLVMGetTypeKind(lt), rk = LLVMGetTypeKind(rt);

        /* widen integers to match if sizes differ */
        if (lk == LLVMIntegerTypeKind && rk == LLVMIntegerTypeKind) {
            unsigned lw = LLVMGetIntTypeWidth(lt), rw = LLVMGetIntTypeWidth(rt);
            if (lw > rw) right = LLVMBuildZExt(g->bld, right, lt, "widen");
            else if (rw > lw) left = LLVMBuildZExt(g->bld, left, rt, "widen");
        }

        /* promote int to float if one side is float */
        bool lf = is_float_kind(lk), rf = is_float_kind(rk);
        if (lf && !rf) {
            right = coerce(g, right, lt);
            rt = lt; rk = lk; rf = true;
        } else if (rf && !lf) {
            left = coerce(g, left, rt);
            lt = rt; lk = rk; lf = true;
        } else if (lf && rf && lt != rt) {
            /* both float but different width: widen to f64 */
            LLVMTypeRef f64t = LLVMDoubleTypeInContext(g->ctx);
            left = LLVMBuildFPCast(g->bld, left, f64t, "fw");
            right = LLVMBuildFPCast(g->bld, right, f64t, "fw");
            lt = rt = f64t; lk = rk = LLVMDoubleTypeKind;
        }

        n->type = n->binary.left->type ? n->binary.left->type : infer_expr_type(g, n->binary.left);

        /* pointer arithmetic */
        if (lk == LLVMPointerTypeKind &&
            (n->binary.op == TOK_PLUS || n->binary.op == TOK_MINUS)) {
            LLVMTypeKind rk = LLVMGetTypeKind(LLVMTypeOf(right));
            /* ptr - ptr -> integer difference */
            if (n->binary.op == TOK_MINUS && rk == LLVMPointerTypeKind) {
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef li = LLVMBuildPtrToInt(g->bld, left, i64t, "lp2i");
                LLVMValueRef ri = LLVMBuildPtrToInt(g->bld, right, i64t, "rp2i");
                n->type = type_basic(TY_I64);
                return LLVMBuildSub(g->bld, li, ri, "ptrdiff");
            }
            /* ptr +/- int -> GEP */
            EsType *ptr_ty = n->binary.left->type;
            EsType *elem_ty = (ptr_ty && ptr_ty->kind == TY_PTR) ? ptr_ty->ptr.base : type_basic(TY_U8);
            LLVMTypeRef elem_llvm = es_to_llvm(g, elem_ty);
            n->type = ptr_ty;
            LLVMValueRef idx = right;
            if (n->binary.op == TOK_MINUS)
                idx = LLVMBuildNeg(g->bld, right, "neg");
            /* ensure index is i64 for GEP */
            if (LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                idx = LLVMBuildSExt(g->bld, idx, LLVMInt64TypeInContext(g->ctx), "sext");
            return LLVMBuildGEP2(g->bld, elem_llvm, left, &idx, 1, "ptradd");
        }

        /* float binary ops */
        if (lf) {
            switch (n->binary.op) {
            case TOK_PLUS:    n->type = type_basic(TY_F64); return LLVMBuildFAdd(g->bld, left, right, "fadd");
            case TOK_MINUS:   n->type = type_basic(TY_F64); return LLVMBuildFSub(g->bld, left, right, "fsub");
            case TOK_STAR:    n->type = type_basic(TY_F64); return LLVMBuildFMul(g->bld, left, right, "fmul");
            case TOK_SLASH:   n->type = type_basic(TY_F64); return LLVMBuildFDiv(g->bld, left, right, "fdiv");
            case TOK_PERCENT: n->type = type_basic(TY_F64); return LLVMBuildFRem(g->bld, left, right, "frem");
            case TOK_EQ:  n->type = type_basic(TY_I32); return LLVMBuildFCmp(g->bld, LLVMRealOEQ, left, right, "feq");
            case TOK_NEQ: n->type = type_basic(TY_I32); return LLVMBuildFCmp(g->bld, LLVMRealONE, left, right, "fne");
            case TOK_LT:  n->type = type_basic(TY_I32); return LLVMBuildFCmp(g->bld, LLVMRealOLT, left, right, "flt");
            case TOK_GT:  n->type = type_basic(TY_I32); return LLVMBuildFCmp(g->bld, LLVMRealOGT, left, right, "fgt");
            case TOK_LEQ: n->type = type_basic(TY_I32); return LLVMBuildFCmp(g->bld, LLVMRealOLE, left, right, "fle");
            case TOK_GEQ: n->type = type_basic(TY_I32); return LLVMBuildFCmp(g->bld, LLVMRealOGE, left, right, "fge");
            default:
                es_fatal("unsupported float binary op %s", tok_str(n->binary.op));
                return NULL;
            }
        }

        /* integer binary ops — unsigned-aware for div/rem/shift/cmp */
        int is_unsigned = n->type && (n->type->kind == TY_U8 || n->type->kind == TY_U16 ||
                                      n->type->kind == TY_U32 || n->type->kind == TY_U64);
        switch (n->binary.op) {
        case TOK_PLUS:    return LLVMBuildAdd(g->bld, left, right, "add");
        case TOK_MINUS:   return LLVMBuildSub(g->bld, left, right, "sub");
        case TOK_STAR:    return LLVMBuildMul(g->bld, left, right, "mul");
        case TOK_SLASH:   return is_unsigned ? LLVMBuildUDiv(g->bld, left, right, "udiv")
                                             : LLVMBuildSDiv(g->bld, left, right, "div");
        case TOK_PERCENT: return is_unsigned ? LLVMBuildURem(g->bld, left, right, "urem")
                                             : LLVMBuildSRem(g->bld, left, right, "rem");
        case TOK_EQ:  n->type = type_basic(TY_I32); return LLVMBuildICmp(g->bld, LLVMIntEQ, left, right, "eq");
        case TOK_NEQ: n->type = type_basic(TY_I32); return LLVMBuildICmp(g->bld, LLVMIntNE, left, right, "ne");
        case TOK_LT:  n->type = type_basic(TY_I32); return LLVMBuildICmp(g->bld, is_unsigned ? LLVMIntULT : LLVMIntSLT, left, right, "lt");
        case TOK_GT:  n->type = type_basic(TY_I32); return LLVMBuildICmp(g->bld, is_unsigned ? LLVMIntUGT : LLVMIntSGT, left, right, "gt");
        case TOK_LEQ: n->type = type_basic(TY_I32); return LLVMBuildICmp(g->bld, is_unsigned ? LLVMIntULE : LLVMIntSLE, left, right, "le");
        case TOK_GEQ: n->type = type_basic(TY_I32); return LLVMBuildICmp(g->bld, is_unsigned ? LLVMIntUGE : LLVMIntSGE, left, right, "ge");
        case TOK_AMP:   return LLVMBuildAnd(g->bld, left, right, "and");
        case TOK_PIPE:  return LLVMBuildOr(g->bld, left, right, "or");
        case TOK_CARET: return LLVMBuildXor(g->bld, left, right, "xor");
        case TOK_SHL:   return LLVMBuildShl(g->bld, left, right, "shl");
        case TOK_SHR:   return is_unsigned ? LLVMBuildLShr(g->bld, left, right, "lshr")
                                           : LLVMBuildAShr(g->bld, left, right, "shr");
        /* TOK_LAND/TOK_LOR handled above with short-circuit */
        default:
            es_fatal("unsupported binary op %s", tok_str(n->binary.op));
            return NULL;
        }
    }

    case ND_UNARY: {
        switch (n->unary.op) {
        case TOK_MINUS: {
            LLVMValueRef v = cg_expr(g, n->unary.operand);
            n->type = n->unary.operand->type;
            if (is_float_kind(LLVMGetTypeKind(LLVMTypeOf(v))))
                return LLVMBuildFNeg(g->bld, v, "fneg");
            return LLVMBuildNeg(g->bld, v, "neg");
        }
        case TOK_BANG: {
            LLVMValueRef v = cg_expr(g, n->unary.operand);
            n->type = type_basic(TY_I32);
            return LLVMBuildNot(g->bld, v, "not");
        }
        case TOK_AMP: {
            /* address-of: return the alloca/GEP pointer without loading */
            EsType *inner_type = NULL;
            LLVMValueRef ptr = cg_lvalue(g, n->unary.operand, &inner_type);
            n->type = inner_type ? type_ptr(inner_type) : type_ptr(type_basic(TY_VOID));
            return ptr;
        }
        case TOK_STAR: {
            /* dereference: load through pointer */
            LLVMValueRef ptr = cg_expr(g, n->unary.operand);
            EsType *ptr_ty = n->unary.operand->type;
            EsType *base_ty = (ptr_ty && ptr_ty->kind == TY_PTR) ? ptr_ty->ptr.base : type_basic(TY_I32);
            n->type = base_ty;
            return LLVMBuildLoad2(g->bld, es_to_llvm(g, base_ty), ptr, "deref");
        }
        default:
            es_fatal("unsupported unary op %s", tok_str(n->unary.op));
            return NULL;
        }
    }

    case ND_CAST: {
        LLVMValueRef val = cg_expr(g, n->cast.expr);
        n->type = n->cast.target;
        if (n->cast.target->kind == TY_PTR)
            return val; /* opaque pointers, no-op */
        LLVMTypeRef target = es_to_llvm(g, n->cast.target);
        return coerce(g, val, target);
    }

    case ND_TERNARY: {
        LLVMValueRef cond = cg_expr(g, n->ternary.cond);
        LLVMTypeKind ck = LLVMGetTypeKind(LLVMTypeOf(cond));
        if (is_float_kind(ck)) {
            cond = LLVMBuildFCmp(g->bld, LLVMRealONE, cond,
                       LLVMConstReal(LLVMTypeOf(cond), 0.0), "tcond");
        } else if (ck != LLVMIntegerTypeKind || LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
            cond = LLVMBuildICmp(g->bld, LLVMIntNE, cond,
                       LLVMConstInt(LLVMTypeOf(cond), 0, 0), "tcond");
        }

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "tthen");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "telse");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "tmerge");

        LLVMBuildCondBr(g->bld, cond, then_bb, else_bb);

        LLVMPositionBuilderAtEnd(g->bld, then_bb);
        LLVMValueRef then_val = cg_expr(g, n->ternary.then_expr);
        LLVMBasicBlockRef then_end = LLVMGetInsertBlock(g->bld);
        LLVMBuildBr(g->bld, merge_bb);

        LLVMPositionBuilderAtEnd(g->bld, else_bb);
        LLVMValueRef else_val = cg_expr(g, n->ternary.else_expr);
        LLVMBasicBlockRef else_end = LLVMGetInsertBlock(g->bld);
        /* coerce else to match then type */
        else_val = coerce(g, else_val, LLVMTypeOf(then_val));
        LLVMBuildBr(g->bld, merge_bb);

        LLVMPositionBuilderAtEnd(g->bld, merge_bb);
        LLVMValueRef phi = LLVMBuildPhi(g->bld, LLVMTypeOf(then_val), "tval");
        LLVMValueRef vals[2] = { then_val, else_val };
        LLVMBasicBlockRef bbs[2] = { then_end, else_end };
        LLVMAddIncoming(phi, vals, bbs, 2);

        n->type = n->ternary.then_expr->type;
        return phi;
    }

    case ND_STRUCT_INIT: {
        /* ✨ T { f: v, ... } → alloc + field stores, return pointer */
        EsType *sty = n->struct_init.stype;
        StructDef *sd = struct_lookup(g, sty->strct.name);
        if (!sd) es_fatal("undefined struct '%s'", sty->strct.name);
        /* malloc(sizeof(T)) */
        LLVMValueRef sz = LLVMSizeOf(sd->llvm_type);
        struct Symbol *mal = sym_lookup(g, "malloc");
        if (!mal) es_fatal("struct init requires malloc (use std)");
        LLVMValueRef args[1] = { sz };
        LLVMValueRef ptr = LLVMBuildCall2(g->bld, mal->llvm_fn_type, mal->value, args, 1, "sinit");
        /* store each field */
        for (int i = 0; i < n->struct_init.field_count; i++) {
            int fi = struct_field_index(sd, n->struct_init.fields[i]);
            if (fi < 0) es_fatal("struct '%s' has no field '%s'", sd->name, n->struct_init.fields[i]);
            LLVMValueRef indices[2] = {
                LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                LLVMConstInt(LLVMInt32TypeInContext(g->ctx), fi, 0)
            };
            LLVMValueRef fp = LLVMBuildGEP2(g->bld, sd->llvm_type, ptr, indices, 2, "fip");
            LLVMValueRef val = cg_expr(g, n->struct_init.vals[i]);
            val = coerce(g, val, es_to_llvm(g, sd->field_types[fi]));
            LLVMBuildStore(g->bld, val, fp);
        }
        n->type = type_ptr(sty);
        return ptr;
    }

    case ND_SIZEOF: {
        n->type = type_basic(TY_I64);
        LLVMTypeRef st = es_to_llvm(g, n->size_of.target);
        /* use LLVM's sizeof for accurate struct layout */
        return LLVMBuildIntCast2(g->bld, LLVMSizeOf(st),
                   LLVMInt64TypeInContext(g->ctx), 0, "sz");
    }

    case ND_COMPTIME: {
        /* ⚡ compile-time constant fold — evaluate expression at compile time */
        Node *e = n->comptime.expr;
        int64_t val = 0;
        if (e->kind == ND_INT_LIT) {
            val = e->int_lit.value;
        } else if (e->kind == ND_BINARY) {
            /* recursively evaluate both sides */
            Node fake_l = { .kind = ND_COMPTIME, .comptime.expr = e->binary.left };
            Node fake_r = { .kind = ND_COMPTIME, .comptime.expr = e->binary.right };
            LLVMValueRef lv = cg_expr(g, &fake_l);
            LLVMValueRef rv = cg_expr(g, &fake_r);
            int64_t l = LLVMConstIntGetSExtValue(lv);
            int64_t r = LLVMConstIntGetSExtValue(rv);
            switch (e->binary.op) {
            case TOK_PLUS:    val = l + r; break;
            case TOK_MINUS:   val = l - r; break;
            case TOK_STAR:    val = l * r; break;
            case TOK_SLASH:   val = r ? l / r : 0; break;
            case TOK_PERCENT: val = r ? l % r : 0; break;
            case TOK_SHL:     val = l << r; break;
            case TOK_SHR:     val = l >> r; break;
            case TOK_AMP:     val = l & r; break;
            case TOK_PIPE:    val = l | r; break;
            case TOK_CARET:   val = l ^ r; break;
            case TOK_EQ:      val = l == r; break;
            case TOK_NEQ:     val = l != r; break;
            case TOK_LT:      val = l < r; break;
            case TOK_GT:      val = l > r; break;
            case TOK_LEQ:     val = l <= r; break;
            case TOK_GEQ:     val = l >= r; break;
            default: es_fatal("unsupported op in compile-time eval");
            }
        } else if (e->kind == ND_UNARY && e->unary.op == TOK_MINUS) {
            Node fake = { .kind = ND_COMPTIME, .comptime.expr = e->unary.operand };
            val = -LLVMConstIntGetSExtValue(cg_expr(g, &fake));
        } else if (e->kind == ND_TERNARY) {
            Node fake_c = { .kind = ND_COMPTIME, .comptime.expr = e->ternary.cond };
            int64_t c = LLVMConstIntGetSExtValue(cg_expr(g, &fake_c));
            Node *pick = c ? e->ternary.then_expr : e->ternary.else_expr;
            Node fake = { .kind = ND_COMPTIME, .comptime.expr = pick };
            val = LLVMConstIntGetSExtValue(cg_expr(g, &fake));
        } else if (e->kind == ND_SIZEOF) {
            LLVMTypeRef st = es_to_llvm(g, e->size_of.target);
            n->type = type_basic(TY_I64);
            return LLVMSizeOf(st);
        } else {
            es_fatal("cannot evaluate expression at compile time");
        }
        n->type = type_basic(TY_I64);
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), (uint64_t)val, 1);
    }

    default:
        es_fatal("unsupported expr kind %d in codegen", n->kind);
        return NULL;
    }
}

/* ---- statement codegen ---- */
static void cg_stmt(CG *g, Node *n);

static void cg_block(CG *g, Node *n) {
    for (int i = 0; i < n->block.count; i++) {
        cg_stmt(g, n->block.stmts[i]);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->bld))) break;
    }
}

static void cg_stmt(CG *g, Node *n) {
    switch (n->kind) {
    case ND_RET: {
        LLVMValueRef retval = NULL;
        if (n->ret.value) {
            retval = cg_expr(g, n->ret.value);
            retval = coerce(g, retval, es_to_llvm(g, g->cur_ret_type));
        }
        /* emit defers in reverse order before returning */
        for (int di = g->defer_count - 1; di >= 0; di--)
            cg_stmt(g, g->defers[di]);
        if (retval)
            LLVMBuildRet(g->bld, retval);
        else
            LLVMBuildRetVoid(g->bld);
        break;
    }

    case ND_EXPR_STMT:
        cg_expr(g, n->expr_stmt.expr);
        break;

    case ND_DECL_STMT: {
        EsType *ty = n->decl.decl_type;
        if (!ty && n->decl.init) {
            /* type inference: figure out type from init expression */
            ty = infer_expr_type(g, n->decl.init);
        }
        if (!ty) es_fatal("cannot infer type for '%s'", n->decl.name);

        LLVMTypeRef llty = es_to_llvm(g, ty);
        LLVMValueRef alloca = LLVMBuildAlloca(g->bld, llty, n->decl.name);
        if (n->decl.init) {
            LLVMValueRef val = cg_expr(g, n->decl.init);
            val = coerce(g, val, llty);
            LLVMBuildStore(g->bld, val, alloca);
        }
        sym_push(g, n->decl.name, alloca, ty, NULL);
        break;
    }

    case ND_ASSIGN: {
        LLVMValueRef val = cg_expr(g, n->assign.value);
        EsType *target_ty = NULL;
        LLVMValueRef ptr = cg_lvalue(g, n->assign.target, &target_ty);
        if (target_ty) val = coerce(g, val, es_to_llvm(g, target_ty));
        LLVMBuildStore(g->bld, val, ptr);
        break;
    }

    case ND_IF: {
        LLVMValueRef cond = cg_expr(g, n->if_stmt.cond);
        LLVMTypeKind ck = LLVMGetTypeKind(LLVMTypeOf(cond));
        if (is_float_kind(ck)) {
            cond = LLVMBuildFCmp(g->bld, LLVMRealONE, cond,
                       LLVMConstReal(LLVMTypeOf(cond), 0.0), "tobool");
        } else if (ck != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
            cond = LLVMBuildICmp(g->bld, LLVMIntNE, cond,
                       LLVMConstInt(LLVMTypeOf(cond), 0, 0), "tobool");
        }

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "then");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "else");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "merge");

        LLVMBuildCondBr(g->bld, cond, then_bb, n->if_stmt.else_blk ? else_bb : merge_bb);

        LLVMPositionBuilderAtEnd(g->bld, then_bb);
        cg_block(g, n->if_stmt.then_blk);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->bld)))
            LLVMBuildBr(g->bld, merge_bb);

        LLVMPositionBuilderAtEnd(g->bld, else_bb);
        if (n->if_stmt.else_blk) cg_block(g, n->if_stmt.else_blk);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->bld)))
            LLVMBuildBr(g->bld, merge_bb);

        LLVMPositionBuilderAtEnd(g->bld, merge_bb);
        break;
    }

    case ND_WHILE: {
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "whcond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "whbody");
        LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "whend");

        /* save outer loop targets */
        LLVMBasicBlockRef prev_cond = g->loop_cond_bb;
        LLVMBasicBlockRef prev_end  = g->loop_end_bb;
        g->loop_cond_bb = cond_bb;
        g->loop_end_bb  = end_bb;

        LLVMBuildBr(g->bld, cond_bb);
        LLVMPositionBuilderAtEnd(g->bld, cond_bb);
        LLVMValueRef cond = cg_expr(g, n->while_stmt.cond);
        LLVMTypeKind wck = LLVMGetTypeKind(LLVMTypeOf(cond));
        if (is_float_kind(wck)) {
            cond = LLVMBuildFCmp(g->bld, LLVMRealONE, cond,
                       LLVMConstReal(LLVMTypeOf(cond), 0.0), "tobool");
        } else if (wck != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
            cond = LLVMBuildICmp(g->bld, LLVMIntNE, cond,
                       LLVMConstInt(LLVMTypeOf(cond), 0, 0), "tobool");
        }
        LLVMBuildCondBr(g->bld, cond, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->bld, body_bb);
        cg_block(g, n->while_stmt.body);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->bld)))
            LLVMBuildBr(g->bld, cond_bb);

        /* restore outer loop targets */
        g->loop_cond_bb = prev_cond;
        g->loop_end_bb  = prev_end;

        LLVMPositionBuilderAtEnd(g->bld, end_bb);
        break;
    }

    case ND_BREAK:
        if (!g->loop_end_bb) es_fatal("'brk' outside of loop");
        LLVMBuildBr(g->bld, g->loop_end_bb);
        break;

    case ND_CONTINUE:
        if (!g->loop_cond_bb) es_fatal("'cont' outside of loop");
        LLVMBuildBr(g->bld, g->loop_cond_bb);
        break;

    case ND_FOR: {
        cg_stmt(g, n->for_stmt.init);
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "fo.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "fo.body");
        LLVMBasicBlockRef incr_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "fo.incr");
        LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "fo.end");
        LLVMBasicBlockRef prev_cond = g->loop_cond_bb;
        LLVMBasicBlockRef prev_end  = g->loop_end_bb;
        g->loop_cond_bb = incr_bb;   /* continue → increment */
        g->loop_end_bb  = end_bb;    /* break → end */
        LLVMBuildBr(g->bld, cond_bb);
        LLVMPositionBuilderAtEnd(g->bld, cond_bb);
        LLVMValueRef fc = cg_expr(g, n->for_stmt.cond);
        LLVMValueRef fb = to_bool(g, fc);
        LLVMBuildCondBr(g->bld, fb, body_bb, end_bb);
        LLVMPositionBuilderAtEnd(g->bld, body_bb);
        cg_block(g, n->for_stmt.body);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->bld)))
            LLVMBuildBr(g->bld, incr_bb);
        LLVMPositionBuilderAtEnd(g->bld, incr_bb);
        cg_stmt(g, n->for_stmt.incr);
        LLVMBuildBr(g->bld, cond_bb);
        g->loop_cond_bb = prev_cond;
        g->loop_end_bb  = prev_end;
        LLVMPositionBuilderAtEnd(g->bld, end_bb);
        break;
    }

    case ND_INLINE_ASM: {
        /* Build constraint string: "outputs,inputs,~clobbers" */
        int total_len = 1;
        for (int i = 0; i < n->iasm.out_count; i++)
            total_len += strlen(n->iasm.out_constraints[i]) + 1;
        for (int i = 0; i < n->iasm.in_count; i++)
            total_len += strlen(n->iasm.in_constraints[i]) + 1;
        for (int i = 0; i < n->iasm.clobber_count; i++)
            total_len += strlen(n->iasm.clobbers[i]) + 3; /* ~{X}, */

        char *constraints = (char *)calloc(1, total_len + 64);
        char *cp = constraints;
        for (int i = 0; i < n->iasm.out_count; i++) {
            if (i > 0) *cp++ = ',';
            cp += sprintf(cp, "%s", n->iasm.out_constraints[i]);
        }
        for (int i = 0; i < n->iasm.in_count; i++) {
            if (cp > constraints) *cp++ = ',';
            cp += sprintf(cp, "%s", n->iasm.in_constraints[i]);
        }
        for (int i = 0; i < n->iasm.clobber_count; i++) {
            if (cp > constraints) *cp++ = ',';
            cp += sprintf(cp, "~{%s}", n->iasm.clobbers[i]);
        }

        /* Collect input values */
        int num_inputs = n->iasm.in_count;
        LLVMValueRef *in_vals = NULL;
        LLVMTypeRef *in_types = NULL;
        if (num_inputs > 0) {
            in_vals = (LLVMValueRef *)malloc(num_inputs * sizeof(LLVMValueRef));
            in_types = (LLVMTypeRef *)malloc(num_inputs * sizeof(LLVMTypeRef));
            for (int i = 0; i < num_inputs; i++) {
                in_vals[i] = cg_expr(g, n->iasm.in_exprs[i]);
                in_types[i] = LLVMTypeOf(in_vals[i]);
            }
        }

        /* Output type: void if no outputs, else the output type */
        LLVMTypeRef out_ty;
        if (n->iasm.out_count == 0) {
            out_ty = LLVMVoidTypeInContext(g->ctx);
        } else if (n->iasm.out_count == 1) {
            /* for single output, infer type from the target expr */
            EsType *ety = infer_expr_type(g, n->iasm.out_exprs[0]);
            out_ty = es_to_llvm(g, ety);
        } else {
            /* multiple outputs → struct type */
            LLVMTypeRef *otypes = (LLVMTypeRef *)malloc(n->iasm.out_count * sizeof(LLVMTypeRef));
            for (int i = 0; i < n->iasm.out_count; i++) {
                EsType *ety = infer_expr_type(g, n->iasm.out_exprs[i]);
                otypes[i] = es_to_llvm(g, ety);
            }
            out_ty = LLVMStructTypeInContext(g->ctx, otypes, n->iasm.out_count, 0);
            free(otypes);
        }

        /* Build function type for the asm call */
        LLVMTypeRef fn_ty = LLVMFunctionType(out_ty, in_types, num_inputs, 0);

        /* Create the inline asm value */
        LLVMValueRef asm_val = LLVMGetInlineAsm(
            fn_ty,
            (char *)n->iasm.templ, strlen(n->iasm.templ),
            constraints, strlen(constraints),
            n->iasm.has_side_effects,
            /* align stack */ 0,
            LLVMInlineAsmDialectATT,
            /* can throw */ 0
        );

        LLVMValueRef result = LLVMBuildCall2(g->bld, fn_ty, asm_val,
                                              in_vals, num_inputs, "");

        /* Store outputs back to their targets */
        if (n->iasm.out_count == 1) {
            LLVMValueRef addr = cg_lvalue(g, n->iasm.out_exprs[0], NULL);
            LLVMBuildStore(g->bld, result, addr);
        } else if (n->iasm.out_count > 1) {
            for (int i = 0; i < n->iasm.out_count; i++) {
                LLVMValueRef ev = LLVMBuildExtractValue(g->bld, result, i, "");
                LLVMValueRef addr = cg_lvalue(g, n->iasm.out_exprs[i], NULL);
                LLVMBuildStore(g->bld, ev, addr);
            }
        }

        free(constraints);
        free(in_vals);
        free(in_types);
        break;
    }

    case ND_MATCH: {
        /* 🎯 expr { val { body } ... _ { body } } → if-else chain */
        LLVMValueRef mval = cg_expr(g, n->match_stmt.expr);
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "ma.end");
        for (int i = 0; i < n->match_stmt.case_count; i++) {
            if (!n->match_stmt.case_vals[i]) {
                /* default case — just emit body */
                cg_block(g, n->match_stmt.case_bodies[i]);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->bld)))
                    LLVMBuildBr(g->bld, end_bb);
            } else {
                LLVMValueRef cv = cg_expr(g, n->match_stmt.case_vals[i]);
                cv = coerce(g, cv, LLVMTypeOf(mval));
                LLVMValueRef eq;
                if (is_float_kind(LLVMGetTypeKind(LLVMTypeOf(mval))))
                    eq = LLVMBuildFCmp(g->bld, LLVMRealOEQ, mval, cv, "meq");
                else
                    eq = LLVMBuildICmp(g->bld, LLVMIntEQ, mval, cv, "meq");
                LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "ma.then");
                LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->cur_fn, "ma.next");
                LLVMBuildCondBr(g->bld, eq, then_bb, next_bb);
                LLVMPositionBuilderAtEnd(g->bld, then_bb);
                cg_block(g, n->match_stmt.case_bodies[i]);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->bld)))
                    LLVMBuildBr(g->bld, end_bb);
                LLVMPositionBuilderAtEnd(g->bld, next_bb);
            }
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->bld)))
            LLVMBuildBr(g->bld, end_bb);
        LLVMPositionBuilderAtEnd(g->bld, end_bb);
        break;
    }

    case ND_DEFER:
        assert(g->defer_count < 64);
        g->defers[g->defer_count++] = n->defer_stmt.body;
        break;

    case ND_COMPTIME:
        /* compile-time expression used as statement — evaluate and discard */
        cg_expr(g, n);
        break;

    default:
        es_fatal("unsupported stmt kind %d in codegen", n->kind);
    }
}

/* ---- top-level codegen ---- */
static void cg_st_decl(CG *g, Node *n) {
    /* skip duplicate struct declarations */
    if (struct_lookup(g, n->st.name)) return;

    assert(g->struct_count < 128);
    StructDef *sd = &g->structs[g->struct_count++];
    sd->name = n->st.name;
    sd->field_count = n->st.field_count;
    sd->field_names = (char **)malloc(sd->field_count * sizeof(char *));
    sd->field_types = (EsType **)malloc(sd->field_count * sizeof(EsType *));

    LLVMTypeRef *ftypes = (LLVMTypeRef *)malloc(sd->field_count * sizeof(LLVMTypeRef));
    for (int i = 0; i < sd->field_count; i++) {
        sd->field_names[i] = n->st.fields[i].name;
        sd->field_types[i] = n->st.fields[i].type;
        ftypes[i] = es_to_llvm(g, n->st.fields[i].type);
    }

    sd->llvm_type = LLVMStructCreateNamed(g->ctx, sd->name);
    LLVMStructSetBody(sd->llvm_type, ftypes, sd->field_count, 0);
    free(ftypes);
}

static void cg_ext_decl(CG *g, Node *n) {
    /* skip duplicate extern declarations */
    if (sym_lookup(g, n->ext.name)) return;

    LLVMTypeRef ft = build_fn_type(g, n->ext.ret_type, n->ext.params, n->ext.param_count, n->ext.is_vararg);
    LLVMValueRef fn = LLVMAddFunction(g->mod, n->ext.name, ft);

    EsType **ptypes = NULL;
    if (n->ext.param_count > 0) {
        ptypes = (EsType **)malloc(n->ext.param_count * sizeof(EsType *));
        for (int i = 0; i < n->ext.param_count; i++) ptypes[i] = n->ext.params[i].type;
    }
    sym_push(g, n->ext.name, fn, type_fn(n->ext.ret_type, ptypes, n->ext.param_count, n->ext.is_vararg), ft);
}

static void cg_fn_decl(CG *g, Node *n) {
    LLVMTypeRef ft = build_fn_type(g, n->fn.ret_type, n->fn.params, n->fn.param_count, false);
    LLVMValueRef fn = LLVMAddFunction(g->mod, n->fn.name, ft);

    EsType **ptypes = NULL;
    if (n->fn.param_count > 0) {
        ptypes = (EsType **)malloc(n->fn.param_count * sizeof(EsType *));
        for (int i = 0; i < n->fn.param_count; i++) ptypes[i] = n->fn.params[i].type;
    }
    sym_push(g, n->fn.name, fn, type_fn(n->fn.ret_type, ptypes, n->fn.param_count, false), ft);

    LLVMValueRef prev_fn = g->cur_fn;
    EsType *prev_ret = g->cur_ret_type;
    int prev_sym_count = g->sym_count;
    int prev_defer_count = g->defer_count;
    g->defer_count = 0;

    g->cur_fn = fn;
    g->cur_ret_type = n->fn.ret_type;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->bld, entry);

    for (int i = 0; i < n->fn.param_count; i++) {
        LLVMValueRef param = LLVMGetParam(fn, i);
        LLVMTypeRef pty = es_to_llvm(g, n->fn.params[i].type);
        LLVMValueRef alloca = LLVMBuildAlloca(g->bld, pty, n->fn.params[i].name);
        LLVMBuildStore(g->bld, param, alloca);
        sym_push(g, n->fn.params[i].name, alloca, n->fn.params[i].type, NULL);
    }

    cg_block(g, n->fn.body);

    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->bld))) {
        /* emit defers at implicit return */
        for (int di = g->defer_count - 1; di >= 0; di--)
            cg_stmt(g, g->defers[di]);
        if (n->fn.ret_type->kind == TY_VOID)
            LLVMBuildRetVoid(g->bld);
        else if (type_is_float(n->fn.ret_type))
            LLVMBuildRet(g->bld, LLVMConstReal(es_to_llvm(g, n->fn.ret_type), 0.0));
        else
            LLVMBuildRet(g->bld, LLVMConstInt(es_to_llvm(g, n->fn.ret_type), 0, 0));
    }

    g->cur_fn = prev_fn;
    g->cur_ret_type = prev_ret;
    g->sym_count = prev_sym_count;
    g->defer_count = prev_defer_count;
}

/* ---- public API ---- */
int codegen(Node *program, const char *out_obj, const char *module_name, int opt_level, int target_wasm) {
    CG g = {0};
    g.ctx = LLVMContextCreate();
    g.mod = LLVMModuleCreateWithNameInContext(module_name, g.ctx);
    g.bld = LLVMCreateBuilderInContext(g.ctx);

    char *triple;
    if (target_wasm) {
        LLVMInitializeWebAssemblyTargetInfo();
        LLVMInitializeWebAssemblyTarget();
        LLVMInitializeWebAssemblyTargetMC();
        LLVMInitializeWebAssemblyAsmPrinter();
        triple = LLVMCreateMessage("wasm32-unknown-unknown");
    } else {
        LLVMInitializeNativeTarget();
        LLVMInitializeNativeAsmPrinter();
        LLVMInitializeNativeAsmParser();
        triple = LLVMGetDefaultTargetTriple();
    }
    LLVMSetTarget(g.mod, triple);

    LLVMTargetRef target;
    char *err = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &err)) {
        fprintf(stderr, "target error: %s\n", err);
        LLVMDisposeMessage(err);
        return 1;
    }

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, "generic", "",
        opt_level >= 2 ? LLVMCodeGenLevelAggressive : LLVMCodeGenLevelDefault,
        LLVMRelocPIC, LLVMCodeModelDefault);

    LLVMTargetDataRef dl = LLVMCreateTargetDataLayout(tm);
    LLVMSetModuleDataLayout(g.mod, dl);

    /* three-pass: structs, then enums, then functions */
    for (int i = 0; i < program->program.count; i++) {
        if (program->program.decls[i]->kind == ND_ST_DECL)
            cg_st_decl(&g, program->program.decls[i]);
    }
    for (int i = 0; i < program->program.count; i++) {
        Node *d = program->program.decls[i];
        if (d->kind == ND_ENUM_DECL) {
            /* register enum members as global i32 constants */
            for (int j = 0; j < d->enum_decl.member_count; j++) {
                LLVMValueRef gv = LLVMAddGlobal(g.mod, LLVMInt32TypeInContext(g.ctx), d->enum_decl.members[j]);
                LLVMSetInitializer(gv, LLVMConstInt(LLVMInt32TypeInContext(g.ctx), d->enum_decl.values[j], 0));
                LLVMSetGlobalConstant(gv, 1);
                LLVMSetLinkage(gv, LLVMPrivateLinkage);
                sym_push(&g, d->enum_decl.members[j], gv, type_basic(TY_I32), NULL);
            }
        }
    }
    for (int i = 0; i < program->program.count; i++) {
        Node *d = program->program.decls[i];
        switch (d->kind) {
        case ND_ST_DECL:    break; /* already handled */
        case ND_ENUM_DECL:  break; /* already handled */
        case ND_EXT_DECL: cg_ext_decl(&g, d); break;
        case ND_FN_DECL:  cg_fn_decl(&g, d);  break;
        default: es_fatal("unexpected top-level node %d", d->kind);
        }
    }

    /* WASM: export all user-defined functions */
    if (target_wasm) {
        for (int i = 0; i < program->program.count; i++) {
            Node *d = program->program.decls[i];
            if (d->kind == ND_FN_DECL) {
                struct Symbol *s = sym_lookup(&g, d->fn.name);
                if (s) {
                    LLVMSetLinkage(s->value, LLVMExternalLinkage);
                    LLVMSetVisibility(s->value, LLVMDefaultVisibility);
                }
            }
        }
    }

    char *verify_err = NULL;
    if (LLVMVerifyModule(g.mod, LLVMReturnStatusAction, &verify_err)) {
        fprintf(stderr, "LLVM verify error:\n%s\n", verify_err);
        char *ir = LLVMPrintModuleToString(g.mod);
        fprintf(stderr, "--- IR ---\n%s\n", ir);
        LLVMDisposeMessage(ir);
        LLVMDisposeMessage(verify_err);
        return 1;
    }
    LLVMDisposeMessage(verify_err);

    if (opt_level > 0) {
        char passes[32];
        snprintf(passes, sizeof(passes), "default<O%d>", opt_level > 3 ? 3 : opt_level);
        LLVMPassBuilderOptionsRef pbo = LLVMCreatePassBuilderOptions();
        LLVMErrorRef perr = LLVMRunPasses(g.mod, passes, tm, pbo);
        if (perr) {
            char *msg = LLVMGetErrorMessage(perr);
            fprintf(stderr, "pass error: %s\n", msg);
            LLVMDisposeErrorMessage(msg);
        }
        LLVMDisposePassBuilderOptions(pbo);
    }

    char *emit_err = NULL;
    if (LLVMTargetMachineEmitToFile(tm, g.mod, (char *)out_obj, LLVMObjectFile, &emit_err)) {
        fprintf(stderr, "emit error: %s\n", emit_err);
        LLVMDisposeMessage(emit_err);
        return 1;
    }

    LLVMDisposeTargetData(dl);
    LLVMDisposeTargetMachine(tm);
    LLVMDisposeMessage(triple);
    LLVMDisposeBuilder(g.bld);
    LLVMDisposeModule(g.mod);
    LLVMContextDispose(g.ctx);
    return 0;
}
