#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core/xs.h"
#include "semantic/typecheck.h"

static XsType *texpr_to_xstype(TypeExpr *te) {
    if (!te) return ty_unknown();
    switch (te->kind) {
        case TEXPR_NAMED: {
            /* handle generic array<T> and map<K,V> */
            if (te->name && strcmp(te->name, "array") == 0) {
                XsType *elem = (te->nargs > 0) ? texpr_to_xstype(te->args[0]) : ty_unknown();
                return ty_array(elem);
            }
            if (te->name && strcmp(te->name, "map") == 0) {
                /* map<K,V>: we only track value type for now */
                return ty_named("map", NULL, 0);
            }
            if (te->name && strcmp(te->name, "tuple") == 0 && te->nargs > 0) {
                XsType **elems = xs_malloc(sizeof(XsType*) * te->nargs);
                for (int i = 0; i < te->nargs; i++)
                    elems[i] = texpr_to_xstype(te->args[i]);
                XsType *t = ty_tuple(elems, te->nargs);
                free(elems);
                return t;
            }
            XsType *t = ty_from_name(te->name);
            if (t) return t;
            /* pass generic args through for named types */
            if (te->nargs > 0) {
                XsType **args = xs_malloc(sizeof(XsType*) * te->nargs);
                for (int i = 0; i < te->nargs; i++)
                    args[i] = texpr_to_xstype(te->args[i]);
                XsType *nt = ty_named(te->name ? te->name : "?", args, te->nargs);
                free(args);
                return nt;
            }
            return ty_named(te->name ? te->name : "?", NULL, 0);
        }
        case TEXPR_ARRAY:  return ty_array(texpr_to_xstype(te->inner));
        case TEXPR_OPTION: return ty_option(texpr_to_xstype(te->inner));
        case TEXPR_TUPLE: {
            XsType **elems = NULL;
            if (te->nelems > 0) {
                elems = xs_malloc(sizeof(XsType*) * te->nelems);
                for (int i = 0; i < te->nelems; i++)
                    elems[i] = texpr_to_xstype(te->elems[i]);
            }
            XsType *t = ty_tuple(elems, te->nelems);
            free(elems);
            return t;
        }
        case TEXPR_FN: {
            XsType **params = NULL;
            if (te->nparams > 0) {
                params = xs_malloc(sizeof(XsType*) * te->nparams);
                for (int i = 0; i < te->nparams; i++)
                    params[i] = texpr_to_xstype(te->params[i]);
            }
            XsType *ret = te->ret ? texpr_to_xstype(te->ret) : ty_unit();
            XsType *t = ty_fn(params, te->nparams, ret);
            free(params);
            return t;
        }
        case TEXPR_INFER:
        default:
            return ty_unknown();
    }
}

XsType *tc_synth(Node *n, SymTab *st, SemaCtx *ctx) {
    if (!n) return ty_unknown();
    switch (n->tag) {
        case NODE_LIT_INT:    return ty_i64();
        case NODE_LIT_BIGINT: return ty_i64();
        case NODE_LIT_FLOAT:  return ty_f64();
        case NODE_LIT_STRING: return ty_str();
        case NODE_LIT_BOOL:   return ty_bool();
        case NODE_LIT_CHAR:   return ty_char();
        case NODE_LIT_NULL:   return ty_unit();
        case NODE_LIT_ARRAY: {
            if (n->lit_array.elems.len > 0 && n->lit_array.elems.items[0])
                return ty_array(tc_synth(n->lit_array.elems.items[0], st, ctx));
            return ty_array(ty_unknown());
        }
        case NODE_LIT_TUPLE: {
            int n2 = n->lit_array.elems.len;
            XsType **elems = n2 > 0 ? xs_malloc(sizeof(XsType*) * n2) : NULL;
            for (int i = 0; i < n2; i++)
                elems[i] = tc_synth(n->lit_array.elems.items[i], st, ctx);
            XsType *t = ty_tuple(elems, n2);
            free(elems);
            return t;
        }
        case NODE_IDENT: {
            if (!n->ident.name) return ty_unknown();
            Symbol *sym = sym_lookup(st, n->ident.name);
            if (sym && sym->type) return sym->type;
            return ty_unknown();
        }
        case NODE_BINOP: {
            const char *op = n->binop.op;
            if (strcmp(op,"==")==0 || strcmp(op,"!=")==0 ||
                strcmp(op,"<")==0  || strcmp(op,">")==0  ||
                strcmp(op,"<=")==0 || strcmp(op,">=")==0 ||
                strcmp(op,"&&")==0 || strcmp(op,"||")==0 ||
                strcmp(op,"in")==0 || strcmp(op,"not in")==0 ||
                strcmp(op,"is")==0)
                return ty_bool();
            return tc_synth(n->binop.left, st, ctx);
        }
        case NODE_UNARY:
            if (strcmp(n->unary.op, "!") == 0) return ty_bool();
            return tc_synth(n->unary.expr, st, ctx);
        case NODE_CALL: {
            XsType *callee_ty = tc_synth(n->call.callee, st, ctx);
            if (callee_ty && callee_ty->kind == TY_FN && callee_ty->fn_.ret)
                return callee_ty->fn_.ret;
            if (n->call.callee && n->call.callee->tag == NODE_IDENT &&
                n->call.callee->ident.name) {
                Symbol *sym = sym_lookup(st, n->call.callee->ident.name);
                if (sym && sym->type && sym->type->kind == TY_FN &&
                    sym->type->fn_.ret)
                    return sym->type->fn_.ret;
                if (sym && sym->type && sym->type->kind == TY_NAMED)
                    return sym->type;
            }
            return ty_unknown();
        }
        case NODE_FIELD: {
            XsType *obj_ty = tc_synth(n->field.obj, st, ctx);
            if (!obj_ty || !n->field.name) return ty_unknown();
            if (obj_ty->kind == TY_TUPLE) {
                char *end;
                long idx = strtol(n->field.name, &end, 10);
                if (*end == '\0' && idx >= 0 && idx < obj_ty->tuple.nelems)
                    return obj_ty->tuple.elems[idx];
            }
            if (obj_ty->kind == TY_NAMED && obj_ty->named.name) {
                Symbol *st_sym = sym_lookup(st, obj_ty->named.name);
                if (st_sym && st_sym->decl &&
                    st_sym->decl->tag == NODE_STRUCT_DECL) {
                    NodePairList *fl = &st_sym->decl->struct_decl.fields;
                    for (int i = 0; i < fl->len; i++) {
                        if (fl->items[i].key &&
                            strcmp(fl->items[i].key, n->field.name) == 0) {
                            if (fl->items[i].val)
                                return tc_synth(fl->items[i].val, st, ctx);
                            break;
                        }
                    }
                }
            }
            return ty_unknown();
        }
        case NODE_INDEX: {
            XsType *obj_ty = tc_synth(n->index.obj, st, ctx);
            if (obj_ty && obj_ty->kind == TY_ARRAY && obj_ty->array.inner)
                return obj_ty->array.inner;
            if (obj_ty && obj_ty->kind == TY_TUPLE &&
                n->index.index && n->index.index->tag == NODE_LIT_INT) {
                int64_t idx = n->index.index->lit_int.ival;
                if (idx >= 0 && idx < obj_ty->tuple.nelems)
                    return obj_ty->tuple.elems[idx];
            }
            if (obj_ty && obj_ty->kind == TY_NAMED && obj_ty->named.name &&
                strcmp(obj_ty->named.name, "Map") == 0 &&
                obj_ty->named.nargs >= 2 && obj_ty->named.args[1])
                return obj_ty->named.args[1];
            if (obj_ty && obj_ty->kind == TY_STR)
                return ty_str();
            return ty_unknown();
        }
        case NODE_IF: {
            if (n->if_expr.then) {
                XsType *then_ty = tc_synth(n->if_expr.then, st, ctx);
                if (n->if_expr.else_branch) {
                    XsType *else_ty = tc_synth(n->if_expr.else_branch, st, ctx);
                    if (ty_equal(then_ty, else_ty))
                        return then_ty;
                }
                return then_ty;
            }
            return ty_unknown();
        }
        case NODE_BLOCK: {
            if (n->block.expr)
                return tc_synth(n->block.expr, st, ctx);
            if (n->block.stmts.len > 0) {
                Node *last = n->block.stmts.items[n->block.stmts.len - 1];
                if (last && last->tag == NODE_EXPR_STMT)
                    return tc_synth(last->expr_stmt.expr, st, ctx);
            }
            return ty_unit();
        }
        case NODE_LAMBDA: {
            int np = n->lambda.params.len;
            XsType **params = np > 0 ? xs_malloc(sizeof(XsType*) * np) : NULL;
            for (int i = 0; i < np; i++) {
                Param *pm = &n->lambda.params.items[i];
                params[i] = pm->type_ann
                          ? texpr_to_xstype(pm->type_ann) : ty_unknown();
            }
            XsType *ret = n->lambda.body
                        ? tc_synth(n->lambda.body, st, ctx) : ty_unknown();
            XsType *t = ty_fn(params, np, ret);
            free(params);
            return t;
        }
        case NODE_CAST: {
            if (n->cast.type_name) {
                XsType *t = ty_from_name(n->cast.type_name);
                if (t) return t;
                return ty_named(n->cast.type_name, NULL, 0);
            }
            return ty_unknown();
        }
        case NODE_MATCH: {
            if (n->match.arms.len == 0) return ty_unknown();
            XsType *first_ty = NULL;
            for (int i = 0; i < n->match.arms.len; i++) {
                if (!n->match.arms.items[i].body) continue;
                XsType *arm_ty = tc_synth(n->match.arms.items[i].body, st, ctx);
                if (!first_ty) {
                    first_ty = arm_ty;
                } else if (arm_ty->kind != TY_UNKNOWN && first_ty->kind != TY_UNKNOWN &&
                           !ty_equal(first_ty, arm_ty)) {
                    char *es = ty_to_str(first_ty);
                    char *as = ty_to_str(arm_ty);
                    Diagnostic *diag = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0001",
                        "match arm type mismatch: expected '%s', got '%s'", es, as);
                    diag_annotate(diag, n->match.arms.items[i].body->span, 1,
                        "this arm returns '%s'", as);
                    if (n->match.arms.items[0].body) {
                        diag_annotate(diag, n->match.arms.items[0].body->span, 0,
                            "first arm returns '%s' here", es);
                    }
                    diag_hint(diag, "all match arms must return the same type");
                    free(es); free(as);
                    diag_emit(ctx->diag, diag);
                }
            }
            return first_ty ? first_ty : ty_unknown();
        }
        case NODE_LIT_MAP: {
            Node *first_k = NULL, *first_v = NULL;
            for (int i = 0; i < n->lit_map.keys.len; i++) {
                Node *k = n->lit_map.keys.items[i];
                if (k && k->tag != NODE_SPREAD && i < n->lit_map.vals.len && n->lit_map.vals.items[i]) {
                    first_k = k; first_v = n->lit_map.vals.items[i]; break;
                }
            }
            if (first_k && first_v) {
                XsType *kt = tc_synth(first_k, st, ctx);
                XsType *vt = tc_synth(first_v, st, ctx);
                XsType **args = xs_malloc(sizeof(XsType*) * 2);
                args[0] = kt;
                args[1] = vt;
                XsType *t = ty_named("Map", args, 2);
                free(args);
                return t;
            }
            return ty_named("Map", NULL, 0);
        }
        case NODE_RANGE:
            return ty_array(ty_i64());
        case NODE_METHOD_CALL: {
            XsType *recv_ty = tc_synth(n->method_call.obj, st, ctx);
            const char *method = n->method_call.method;
            if (recv_ty && method) {
                if (recv_ty->kind == TY_NAMED && recv_ty->named.name) {
                    char qual[256];
                    snprintf(qual, sizeof qual, "%s.%s", recv_ty->named.name, method);
                    Symbol *msym = sym_lookup(st, qual);
                    if (msym && msym->type && msym->type->kind == TY_FN &&
                        msym->type->fn_.ret)
                        return msym->type->fn_.ret;
                }
                if (strcmp(method, "len") == 0 || strcmp(method, "count") == 0 ||
                    strcmp(method, "index") == 0 || strcmp(method, "find") == 0)
                    return ty_i64();
                if (strcmp(method, "contains") == 0 || strcmp(method, "is_empty") == 0 ||
                    strcmp(method, "starts_with") == 0 || strcmp(method, "ends_with") == 0)
                    return ty_bool();
                if (strcmp(method, "to_str") == 0 || strcmp(method, "trim") == 0 ||
                    strcmp(method, "upper") == 0 || strcmp(method, "lower") == 0 ||
                    strcmp(method, "join") == 0 || strcmp(method, "replace") == 0)
                    return ty_str();
                if (strcmp(method, "push") == 0 || strcmp(method, "pop") == 0 ||
                    strcmp(method, "append") == 0 || strcmp(method, "insert") == 0 ||
                    strcmp(method, "remove") == 0 || strcmp(method, "clear") == 0 ||
                    strcmp(method, "sort") == 0 || strcmp(method, "reverse") == 0)
                    return recv_ty;
                if (recv_ty->kind == TY_ARRAY &&
                    (strcmp(method, "map") == 0 || strcmp(method, "filter") == 0))
                    return recv_ty;
            }
            return ty_unknown();
        }
        case NODE_STRUCT_INIT: {
            if (n->struct_init.path)
                return ty_named(n->struct_init.path, NULL, 0);
            return ty_unknown();
        }
        default:
            return ty_unknown();
    }
}

/* check if a type name is a known builtin or user-defined type */
static int is_known_type_name(const char *name, SymTab *st) {
    if (!name) return 0;
    /* builtins recognized by ty_from_name */
    if (ty_from_name(name)) return 1;
    /* common aliases not in ty_from_name */
    if (strcmp(name, "int") == 0 || strcmp(name, "float") == 0 ||
        strcmp(name, "string") == 0 || strcmp(name, "byte") == 0 ||
        strcmp(name, "any") == 0 || strcmp(name, "void") == 0 ||
        strcmp(name, "re") == 0 || strcmp(name, "regex") == 0) return 1;
    /* container types */
    if (strcmp(name, "array") == 0 || strcmp(name, "map") == 0 ||
        strcmp(name, "tuple") == 0 || strcmp(name, "option") == 0 ||
        strcmp(name, "result") == 0 || strcmp(name, "fn") == 0) return 1;
    /* user-defined: struct, enum, trait, class */
    if (st) {
        Symbol *sym = sym_lookup(st, name);
        if (sym && (sym->kind == SYM_STRUCT || sym->kind == SYM_ENUM ||
                    sym->kind == SYM_TRAIT || sym->kind == SYM_GENERIC)) return 1;
    }
    return 0;
}

static void check_type_ann_exists(TypeExpr *te, SymTab *st, SemaCtx *ctx) {
    if (!te || te->kind != TEXPR_NAMED || !te->name) return;
    if (!is_known_type_name(te->name, st)) {
        Diagnostic *diag = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0011",
            "unknown type '%s'", te->name);
        diag_annotate(diag, te->span, 1, "unknown type '%s'", te->name);
        diag_hint(diag, "check spelling or define a struct/enum named '%s'", te->name);
        diag_emit(ctx->diag, diag);
    }
}

static int is_int_kind(TyKind k) {
    return k == TY_I8 || k == TY_I16 || k == TY_I32 || k == TY_I64 ||
           k == TY_U8 || k == TY_U16 || k == TY_U32 || k == TY_U64;
}
static int is_float_kind(TyKind k) {
    return k == TY_F32 || k == TY_F64;
}

int tc_check(Node *n, XsType *expected, SymTab *st, SemaCtx *ctx) {
    if (!expected || expected->kind == TY_UNKNOWN) return 1;
    XsType *actual = tc_synth(n, st, ctx);
    if (actual->kind == TY_UNKNOWN) return 1;
    if (actual->kind == TY_I64 && is_int_kind(expected->kind)) {
        if (!actual->is_singleton) ty_free(actual);
        return 1;
    }
    if (actual->kind == TY_F64 && is_float_kind(expected->kind)) {
        if (!actual->is_singleton) ty_free(actual);
        return 1;
    }
    if (!ty_equal(actual, expected)) {
        char *es = ty_to_str(expected);
        char *as = ty_to_str(actual);
        Diagnostic *diag = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0001",
            "type mismatch: expected '%s', got '%s'", es, as);
        diag_annotate(diag, n->span, 1, "expected '%s', found '%s'", es, as);
        if (expected->kind == TY_STR && (actual->kind == TY_I64 || actual->kind == TY_F64))
            diag_hint(diag, "use to_str() to convert a number to a string");
        else if ((expected->kind == TY_I64 || expected->kind == TY_F64) && actual->kind == TY_STR)
            diag_hint(diag, "use int() or float() to convert a string to a number");
        else if (expected->kind == TY_BOOL && actual->kind != TY_BOOL)
            diag_hint(diag, "use an explicit comparison to get a bool");
        else if (expected->kind == TY_ARRAY && actual->kind != TY_ARRAY)
            diag_hint(diag, "wrap the value in [...] to create an array");
        else if (expected->kind == TY_OPTION && actual->kind != TY_OPTION)
            diag_hint(diag, "wrap the value with Some(...) for an optional type");
        else if (expected->kind == TY_FN && actual->kind != TY_FN)
            diag_hint(diag, "expected a function; did you forget to pass a lambda or function name?");
        else if (is_int_kind(expected->kind) && is_float_kind(actual->kind))
            diag_hint(diag, "use an explicit cast: value as i64");
        else if (is_float_kind(expected->kind) && is_int_kind(actual->kind))
            diag_hint(diag, "use an explicit cast: value as f64");
        else if (expected->kind == TY_TUPLE && actual->kind == TY_ARRAY)
            diag_hint(diag, "tuples use (...) syntax, not [...]");
        else if (expected->kind == TY_ARRAY && actual->kind == TY_TUPLE)
            diag_hint(diag, "arrays use [...] syntax, not (...)");
        free(es); free(as);
        diag_emit(ctx->diag, diag);
        if (!actual->is_singleton) ty_free(actual);
        return 0;
    }
    if (!actual->is_singleton) ty_free(actual);
    return 1;
}

/* return type stack */
#define RET_STACK_MAX 64
static XsType *g_ret_stack[RET_STACK_MAX];
static int     g_ret_depth = 0;

static void ret_push(XsType *t) {
    if (g_ret_depth < RET_STACK_MAX) g_ret_stack[g_ret_depth++] = t;
}
static void ret_pop(void) {
    if (g_ret_depth > 0) g_ret_depth--;
}
static XsType *ret_current(void) {
    return g_ret_depth > 0 ? g_ret_stack[g_ret_depth-1] : NULL;
}

static void tc_walk(Node *n, SymTab *st, SemaCtx *ctx);

static void tc_walk_list(NodeList *nl, SymTab *st, SemaCtx *ctx) {
    if (!nl) return;
    for (int i = 0; i < nl->len; i++) tc_walk(nl->items[i], st, ctx);
}

static void tc_walk(Node *n, SymTab *st, SemaCtx *ctx) {
    if (!n) return;
    switch (n->tag) {

    case NODE_FN_DECL: {
        if (n->fn_decl.ret_type)
            check_type_ann_exists(n->fn_decl.ret_type, st, ctx);
        for (int i = 0; i < n->fn_decl.params.len; i++) {
            Param *pm = &n->fn_decl.params.items[i];
            if (pm->type_ann) check_type_ann_exists(pm->type_ann, st, ctx);
        }
        XsType *ret = n->fn_decl.ret_type
                    ? texpr_to_xstype(n->fn_decl.ret_type)
                    : ty_unknown();
        ret_push(ret);
        for (int i = 0; i < n->fn_decl.params.len; i++) {
            Param *pm = &n->fn_decl.params.items[i];
            if (!pm->name) continue;
            XsType *pt = pm->type_ann ? texpr_to_xstype(pm->type_ann) : ty_unknown();
            Symbol *sym = sym_lookup(st, pm->name);
            if (sym) sym->type = pt;
        }
        if (n->fn_decl.body) tc_walk(n->fn_decl.body, st, ctx);
        ret_pop();
        break;
    }

    case NODE_RETURN: {
        XsType *expected = ret_current();
        if (n->ret.value && expected && expected->kind != TY_UNKNOWN)
            tc_check(n->ret.value, expected, st, ctx);
        if (n->ret.value) tc_walk(n->ret.value, st, ctx);
        break;
    }

    case NODE_LET: case NODE_VAR: {
        if (n->let.value) tc_walk(n->let.value, st, ctx);
        if (n->let.type_ann) {
            check_type_ann_exists(n->let.type_ann, st, ctx);
            if (n->let.value) {
                int known = !n->let.type_ann->name ||
                            n->let.type_ann->kind != TEXPR_NAMED ||
                            is_known_type_name(n->let.type_ann->name, st);
                XsType *ann = texpr_to_xstype(n->let.type_ann);
                if (ann->kind != TY_UNKNOWN && known) {
                    tc_check(n->let.value, ann, st, ctx);
                    int stored = 0;
                    if (n->let.name) {
                        Symbol *sym = sym_lookup(st, n->let.name);
                        if (sym) { sym->type = ann; stored = 1; }
                    }
                    if (!stored) ty_free(ann);
                } else {
                    ty_free(ann);
                }
            }
        }
        break;
    }

    case NODE_CONST: {
        if (n->const_.value) tc_walk(n->const_.value, st, ctx);
        if (n->const_.type_ann) {
            check_type_ann_exists(n->const_.type_ann, st, ctx);
            if (n->const_.value) {
                int known = !n->const_.type_ann->name ||
                            n->const_.type_ann->kind != TEXPR_NAMED ||
                            is_known_type_name(n->const_.type_ann->name, st);
                XsType *ann = texpr_to_xstype(n->const_.type_ann);
                if (ann->kind != TY_UNKNOWN && known) {
                    tc_check(n->const_.value, ann, st, ctx);
                    int stored = 0;
                    if (n->const_.name) {
                        Symbol *sym = sym_lookup(st, n->const_.name);
                        if (sym) { sym->type = ann; stored = 1; }
                    }
                    if (!stored) ty_free(ann);
                } else {
                    ty_free(ann);
                }
            }
        }
        break;
    }

    case NODE_PROGRAM:
        for (int i = 0; i < n->program.stmts.len; i++)
            tc_walk(n->program.stmts.items[i], st, ctx);
        break;

    case NODE_BLOCK:
        tc_walk_list(&n->block.stmts, st, ctx);
        if (n->block.expr) tc_walk(n->block.expr, st, ctx);
        break;

    case NODE_IF:
        tc_walk(n->if_expr.cond, st, ctx);
        tc_walk(n->if_expr.then, st, ctx);
        if (n->if_expr.else_branch) tc_walk(n->if_expr.else_branch, st, ctx);
        break;

    case NODE_WHILE:
        tc_walk(n->while_loop.cond, st, ctx);
        tc_walk(n->while_loop.body, st, ctx);
        break;

    case NODE_FOR:
        tc_walk(n->for_loop.iter, st, ctx);
        tc_walk(n->for_loop.body, st, ctx);
        break;

    case NODE_EXPR_STMT: tc_walk(n->expr_stmt.expr, st, ctx); break;
    case NODE_CALL: {
        tc_walk(n->call.callee, st, ctx);
        tc_walk_list(&n->call.args, st, ctx);
        /* check argument types against parameter annotations */
        if (n->call.callee && n->call.callee->tag == NODE_IDENT) {
            Symbol *fn_sym = sym_lookup(st, n->call.callee->ident.name);
            if (fn_sym && fn_sym->decl && fn_sym->decl->tag == NODE_FN_DECL) {
                ParamList *params = &fn_sym->decl->fn_decl.params;
                int nargs = n->call.args.len;
                int nparams = params->len;
                int limit = nargs < nparams ? nargs : nparams;
                for (int i = 0; i < limit; i++) {
                    Param *pm = &params->items[i];
                    if (!pm->type_ann) continue;
                    if (pm->type_ann->kind == TEXPR_NAMED && pm->type_ann->name &&
                        !is_known_type_name(pm->type_ann->name, st)) continue;
                    XsType *expected = texpr_to_xstype(pm->type_ann);
                    if (expected->kind != TY_UNKNOWN) {
                        Node *arg = n->call.args.items[i];
                        if (arg) tc_check(arg, expected, st, ctx);
                    }
                    if (!expected->is_singleton) ty_free(expected);
                }
            }
        }
        break;
    }
    case NODE_METHOD_CALL: tc_walk(n->method_call.obj, st, ctx); tc_walk_list(&n->method_call.args, st, ctx); break;
    case NODE_ASSIGN:    tc_walk(n->assign.target, st, ctx); tc_walk(n->assign.value, st, ctx); break;
    case NODE_BINOP:     tc_walk(n->binop.left, st, ctx); tc_walk(n->binop.right, st, ctx); break;
    case NODE_UNARY:     tc_walk(n->unary.expr, st, ctx); break;

    case NODE_MATCH:
        tc_walk(n->match.subject, st, ctx);
        for (int i = 0; i < n->match.arms.len; i++) {
            MatchArm *arm = &n->match.arms.items[i];
            if (arm->guard) tc_walk(arm->guard, st, ctx);
            if (arm->body)  tc_walk(arm->body, st, ctx);
        }
        break;

    case NODE_LAMBDA:
        if (n->lambda.body) tc_walk(n->lambda.body, st, ctx);
        break;

    case NODE_INDEX:
        tc_walk(n->index.obj, st, ctx);
        tc_walk(n->index.index, st, ctx);
        break;

    case NODE_FIELD:
        tc_walk(n->field.obj, st, ctx);
        break;

    case NODE_RANGE:
        if (n->range.start) tc_walk(n->range.start, st, ctx);
        if (n->range.end)   tc_walk(n->range.end, st, ctx);
        break;

    case NODE_TRY:
        if (n->try_.body) tc_walk(n->try_.body, st, ctx);
        for (int i = 0; i < n->try_.catch_arms.len; i++) {
            MatchArm *arm = &n->try_.catch_arms.items[i];
            if (arm->body) tc_walk(arm->body, st, ctx);
        }
        if (n->try_.finally_block) tc_walk(n->try_.finally_block, st, ctx);
        break;

    case NODE_DEFER:
        if (n->defer_.body) tc_walk(n->defer_.body, st, ctx);
        break;

    case NODE_SPAWN:
        if (n->spawn_.expr) tc_walk(n->spawn_.expr, st, ctx);
        break;

    case NODE_STRUCT_INIT: {
        for (int i = 0; i < n->struct_init.fields.len; i++) {
            if (n->struct_init.fields.items[i].val)
                tc_walk(n->struct_init.fields.items[i].val, st, ctx);
        }
        if (n->struct_init.rest) tc_walk(n->struct_init.rest, st, ctx);
        if (n->struct_init.path) {
            Symbol *st_sym = sym_lookup(st, n->struct_init.path);
            if (st_sym && st_sym->decl && st_sym->decl->tag == NODE_STRUCT_DECL) {
                Node *sd = st_sym->decl;
                NodePairList *decl_fields = &sd->struct_decl.fields;
                TypeExpr **field_types = sd->struct_decl.field_types;
                int n_ft = sd->struct_decl.n_field_types;
                for (int i = 0; i < n->struct_init.fields.len; i++) {
                    const char *fname = n->struct_init.fields.items[i].key;
                    Node *fval = n->struct_init.fields.items[i].val;
                    if (!fname || !fval) continue;
                    for (int j = 0; j < decl_fields->len; j++) {
                        if (!decl_fields->items[j].key ||
                            strcmp(decl_fields->items[j].key, fname) != 0) continue;
                        /* check against field type annotation first */
                        if (field_types && j < n_ft && field_types[j]) {
                            XsType *ft = texpr_to_xstype(field_types[j]);
                            if (ft->kind != TY_UNKNOWN)
                                tc_check(fval, ft, st, ctx);
                            if (!ft->is_singleton) ty_free(ft);
                        } else if (decl_fields->items[j].val) {
                            /* fall back to inferring from default value */
                            XsType *field_ty = tc_synth(decl_fields->items[j].val, st, ctx);
                            if (field_ty && field_ty->kind != TY_UNKNOWN)
                                tc_check(fval, field_ty, st, ctx);
                        }
                        break;
                    }
                }
            }
        }
        break;
    }

    case NODE_CAST:
        if (n->cast.expr) tc_walk(n->cast.expr, st, ctx);
        break;

    default: break;
    }
}

void tc_program(Node *prog, SymTab *st, SemaCtx *ctx) {
    g_ret_depth = 0;
    memset(g_ret_stack, 0, sizeof g_ret_stack);
    tc_walk(prog, st, ctx);
}
