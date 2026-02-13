/* inference.c -- Hindley-Milner type inference for XS. */
#include "types/inference.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

extern char *strdup(const char *);

/*

 */

static int tvar_counter = 0;

typedef struct TVar TVar;
struct TVar {
    int   id;      /* unique identifier */
    void *link;    /* NULL = unbound; points to TVar* or XsType* */
    int   is_type; /* 0 = link is TVar*, 1 = link is XsType* (only when link != NULL) */
};

static TVar *tvar_new(void) {
    TVar *tv = xs_calloc(1, sizeof(TVar));
    tv->id = ++tvar_counter;
    tv->link = NULL;
    tv->is_type = 0;
    return tv;
}

static TVar *tvar_find(TVar *tv, XsType **out_type) {
    if (!tv) { *out_type = NULL; return NULL; }
    *out_type = NULL;
    while (tv->link) {
        if (tv->is_type) {
            *out_type = (XsType *)tv->link;
            return NULL; /* resolved to concrete type */
        }
        tv = (TVar *)tv->link;
    }
    return tv; /* unbound representative */
}

/*

 */

typedef struct RType RType;
struct RType {
    int is_var; /* 1 = tvar, 0 = concrete XsType* */
    union {
        TVar   *var;
        XsType *type;
    };
};

static RType rt_var(TVar *tv) {
    RType r; r.is_var = 1; r.var = tv; return r;
}

static RType rt_type(XsType *t) {
    RType r; r.is_var = 0; r.type = t; return r;
}

static RType rt_resolve(RType r) {
    if (!r.is_var) return r;
    XsType *t = NULL;
    TVar *rep = tvar_find(r.var, &t);
    if (t) return rt_type(t);
    return rt_var(rep);
}

static XsType *rt_to_xstype(RType r) {
    r = rt_resolve(r);
    if (!r.is_var) return r.type ? r.type : ty_unknown();
    return ty_unknown();
}

/*
 * substitution
 */

#define SUBST_INIT_CAP 64

typedef struct SubstEntry {
    int    tvar_id;
    RType  target;
} SubstEntry;

typedef struct Substitution {
    SubstEntry *entries;
    int         len;
    int         cap;
} Substitution;

static Substitution subst_new(void) {
    Substitution s;
    s.cap = SUBST_INIT_CAP;
    s.len = 0;
    s.entries = xs_calloc((size_t)s.cap, sizeof(SubstEntry));
    return s;
}

static void subst_free(Substitution *s) {
    if (s->entries) free(s->entries);
    s->entries = NULL;
    s->len = s->cap = 0;
}

static RType *subst_lookup(Substitution *s, int tvar_id) {
    for (int i = 0; i < s->len; i++) {
        if (s->entries[i].tvar_id == tvar_id)
            return &s->entries[i].target;
    }
    return NULL;
}

static void subst_set(Substitution *s, int tvar_id, RType target) {
    /* overwrite if exists */
    for (int i = 0; i < s->len; i++) {
        if (s->entries[i].tvar_id == tvar_id) {
            s->entries[i].target = target;
            return;
        }
    }
    /* append */
    if (s->len >= s->cap) {
        s->cap *= 2;
        s->entries = xs_realloc(s->entries, (size_t)s->cap * sizeof(SubstEntry));
    }
    s->entries[s->len].tvar_id = tvar_id;
    s->entries[s->len].target  = target;
    s->len++;
}

static RType subst_apply(Substitution *s, RType r) {
    int depth = 0;
    while (r.is_var && depth < 100) {
        XsType *t = NULL;
        TVar *rep = tvar_find(r.var, &t);
        if (t) return rt_type(t);
        if (!rep) return rt_type(ty_unknown());
        RType *mapped = subst_lookup(s, rep->id);
        if (mapped) {
            r = *mapped;
            depth++;
        } else {
            return rt_var(rep);
        }
    }
    return r;
}

static XsType *subst_apply_deep(Substitution *s, RType r);

static XsType *subst_apply_xstype(Substitution *s, XsType *t) {
    if (!t) return ty_unknown();
    switch (t->kind) {
    case TY_FN: {
        int changed = 0;
        XsType **new_params = xs_malloc(sizeof(XsType*) * (t->fn_.nparams ? t->fn_.nparams : 1));
        for (int i = 0; i < t->fn_.nparams; i++) {
            new_params[i] = subst_apply_xstype(s, t->fn_.params[i]);
            if (new_params[i] != t->fn_.params[i]) changed = 1;
        }
        XsType *new_ret = subst_apply_xstype(s, t->fn_.ret);
        if (new_ret != t->fn_.ret) changed = 1;
        if (!changed) { free(new_params); return t; }
        return ty_fn(new_params, t->fn_.nparams, new_ret);
    }
    case TY_ARRAY: {
        XsType *inner = subst_apply_xstype(s, t->array.inner);
        if (inner == t->array.inner) return t;
        return ty_array(inner);
    }
    case TY_TUPLE: {
        int changed = 0;
        XsType **new_elems = xs_malloc(sizeof(XsType*) * (t->tuple.nelems ? t->tuple.nelems : 1));
        for (int i = 0; i < t->tuple.nelems; i++) {
            new_elems[i] = subst_apply_xstype(s, t->tuple.elems[i]);
            if (new_elems[i] != t->tuple.elems[i]) changed = 1;
        }
        if (!changed) { free(new_elems); return t; }
        XsType *r = ty_tuple(new_elems, t->tuple.nelems);
        free(new_elems);
        return r;
    }
    case TY_OPTION: {
        XsType *inner = subst_apply_xstype(s, t->option.inner);
        if (inner == t->option.inner) return t;
        return ty_option(inner);
    }
    case TY_RESULT: {
        XsType *ok  = subst_apply_xstype(s, t->result.ok);
        XsType *err = subst_apply_xstype(s, t->result.err);
        if (ok == t->result.ok && err == t->result.err) return t;
        return ty_result(ok, err);
    }
    case TY_NAMED: {
        if (t->named.nargs == 0) return t;
        int changed = 0;
        XsType **new_args = xs_malloc(sizeof(XsType*) * t->named.nargs);
        for (int i = 0; i < t->named.nargs; i++) {
            new_args[i] = subst_apply_xstype(s, t->named.args[i]);
            if (new_args[i] != t->named.args[i]) changed = 1;
        }
        if (!changed) { free(new_args); return t; }
        return ty_named(t->named.name, new_args, t->named.nargs);
    }
    default:
        return t; /* primitive types unchanged */
    }
}

static XsType *subst_apply_deep(Substitution *s, RType r) {
    r = subst_apply(s, r);
    if (r.is_var) return ty_unknown();
    return subst_apply_xstype(s, r.type);
}

/*
 * poly type
 */

#define MAX_QUANT 32

typedef struct PolyType {
    int   quantified[MAX_QUANT]; /* TVar ids that are universally quantified */
    int   nquant;
    RType mono;                  /* the monomorphic body */
} PolyType;

static PolyType poly_mono(RType t) {
    PolyType p;
    memset(&p, 0, sizeof(p));
    p.nquant = 0;
    p.mono = t;
    return p;
}

/*
 * free variable collection
 */

#define MAX_FREE_VARS 256

typedef struct FreeVarSet {
    int ids[MAX_FREE_VARS];
    int len;
} FreeVarSet;

static void fvs_init(FreeVarSet *s) { s->len = 0; }

static int fvs_contains(FreeVarSet *s, int id) {
    for (int i = 0; i < s->len; i++)
        if (s->ids[i] == id) return 1;
    return 0;
}

static void fvs_add(FreeVarSet *s, int id) {
    if (s->len >= MAX_FREE_VARS || fvs_contains(s, id)) return;
    s->ids[s->len++] = id;
}

static void fvs_remove(FreeVarSet *s, int id) {
    for (int i = 0; i < s->len; i++) {
        if (s->ids[i] == id) {
            s->ids[i] = s->ids[--s->len];
            return;
        }
    }
}

static void free_vars_xstype(XsType *t, FreeVarSet *fvs) {
    if (!t) return;
    switch (t->kind) {
    case TY_FN:
        for (int i = 0; i < t->fn_.nparams; i++)
            free_vars_xstype(t->fn_.params[i], fvs);
        free_vars_xstype(t->fn_.ret, fvs);
        break;
    case TY_ARRAY:  free_vars_xstype(t->array.inner, fvs); break;
    case TY_TUPLE:
        for (int i = 0; i < t->tuple.nelems; i++)
            free_vars_xstype(t->tuple.elems[i], fvs);
        break;
    case TY_OPTION: free_vars_xstype(t->option.inner, fvs); break;
    case TY_RESULT:
        free_vars_xstype(t->result.ok, fvs);
        free_vars_xstype(t->result.err, fvs);
        break;
    case TY_NAMED:
        for (int i = 0; i < t->named.nargs; i++)
            free_vars_xstype(t->named.args[i], fvs);
        break;
    default: break;
    }
}

static void free_vars_rtype(RType r, FreeVarSet *fvs) {
    r = rt_resolve(r);
    if (r.is_var) {
        fvs_add(fvs, r.var->id);
    } else {
        free_vars_xstype(r.type, fvs);
    }
}

/*
 * HM type environment
 */

#define HMENV_INIT_CAP 32

typedef struct HMBinding {
    char     *name;
    PolyType  poly;
} HMBinding;

typedef struct HMEnv HMEnv;
struct HMEnv {
    HMEnv     *parent;
    HMBinding *bindings;
    int        len;
    int        cap;
};

static HMEnv *hmenv_new(HMEnv *parent) {
    HMEnv *e = xs_calloc(1, sizeof(HMEnv));
    e->parent = parent;
    e->cap = HMENV_INIT_CAP;
    e->len = 0;
    e->bindings = xs_calloc((size_t)e->cap, sizeof(HMBinding));
    return e;
}

static void hmenv_free(HMEnv *e) {
    if (!e) return;
    for (int i = 0; i < e->len; i++)
        free(e->bindings[i].name);
    free(e->bindings);
    free(e);
}

static void hmenv_define(HMEnv *e, const char *name, PolyType poly) {
    if (e->len >= e->cap) {
        e->cap *= 2;
        e->bindings = xs_realloc(e->bindings, (size_t)e->cap * sizeof(HMBinding));
    }
    e->bindings[e->len].name = xs_strdup(name);
    e->bindings[e->len].poly = poly;
    e->len++;
}

static void hmenv_define_mono(HMEnv *e, const char *name, RType t) {
    hmenv_define(e, name, poly_mono(t));
}

static PolyType *hmenv_lookup(HMEnv *e, const char *name) {
    if (!e || !name) return NULL;
    for (int i = e->len - 1; i >= 0; i--) {
        if (strcmp(e->bindings[i].name, name) == 0)
            return &e->bindings[i].poly;
    }
    if (e->parent) return hmenv_lookup(e->parent, name);
    return NULL;
}

static void hmenv_free_vars(HMEnv *e, FreeVarSet *fvs) {
    if (!e) return;
    for (int i = 0; i < e->len; i++) {
        PolyType *p = &e->bindings[i].poly;
        FreeVarSet mono_fvs;
        fvs_init(&mono_fvs);
        free_vars_rtype(p->mono, &mono_fvs);
        /* Remove quantified vars */
        for (int q = 0; q < p->nquant; q++)
            fvs_remove(&mono_fvs, p->quantified[q]);
        for (int j = 0; j < mono_fvs.len; j++)
            fvs_add(fvs, mono_fvs.ids[j]);
    }
    if (e->parent) hmenv_free_vars(e->parent, fvs);
}

/*
 * generalization / instantiation
 */

static PolyType generalize(RType t, HMEnv *env) {
    FreeVarSet t_fvs, env_fvs;
    fvs_init(&t_fvs);
    fvs_init(&env_fvs);
    free_vars_rtype(t, &t_fvs);
    hmenv_free_vars(env, &env_fvs);

    PolyType poly;
    memset(&poly, 0, sizeof(poly));
    poly.mono = t;
    poly.nquant = 0;
    for (int i = 0; i < t_fvs.len && poly.nquant < MAX_QUANT; i++) {
        if (!fvs_contains(&env_fvs, t_fvs.ids[i]))
            poly.quantified[poly.nquant++] = t_fvs.ids[i];
    }
    return poly;
}

static RType instantiate(PolyType *poly) {
    if (poly->nquant == 0) return poly->mono;

    /* Build refresh mapping: old_id → new TVar */
    TVar *fresh_map[MAX_QUANT];
    int   old_ids[MAX_QUANT];
    for (int i = 0; i < poly->nquant; i++) {
        old_ids[i] = poly->quantified[i];
        fresh_map[i] = tvar_new();
    }

    /* We need to recursively refresh the mono type.
       For simplicity, if mono is a TVar matching a quantified var, replace it.
       For compound types, we'd need to walk into XsType structures.
       Since our generalization is mostly at the top-level let binding,
       we handle the common case: mono is a TVar or an XsType with no embedded TVars. */
    RType m = rt_resolve(poly->mono);
    if (m.is_var) {
        for (int i = 0; i < poly->nquant; i++) {
            if (m.var->id == old_ids[i])
                return rt_var(fresh_map[i]);
        }
    }
    /* For concrete types with no embedded TVars, just return as-is.
       Full deep refresh would require wrapping types to track TVars;
       for now this handles the common patterns well. */
    return m;
}

/*
 * constraints
 */

typedef struct Constraint {
    RType lhs;
    RType rhs;
    Span  span;
    char  context[64];
} Constraint;

#define CLIST_INIT_CAP 256

typedef struct ConstraintList {
    Constraint *items;
    int         len;
    int         cap;
} ConstraintList;

static ConstraintList clist_new(void) {
    ConstraintList cl;
    cl.cap = CLIST_INIT_CAP;
    cl.len = 0;
    cl.items = xs_calloc((size_t)cl.cap, sizeof(Constraint));
    return cl;
}

static void clist_free(ConstraintList *cl) {
    free(cl->items);
    cl->items = NULL;
    cl->len = cl->cap = 0;
}

static void clist_push(ConstraintList *cl, RType lhs, RType rhs,
                        Span span, const char *ctx) {
    if (cl->len >= cl->cap) {
        cl->cap *= 2;
        cl->items = xs_realloc(cl->items, (size_t)cl->cap * sizeof(Constraint));
    }
    Constraint *c = &cl->items[cl->len++];
    c->lhs = lhs;
    c->rhs = rhs;
    c->span = span;
    memset(c->context, 0, sizeof(c->context));
    if (ctx) {
        size_t n = strlen(ctx);
        if (n >= sizeof(c->context)) n = sizeof(c->context) - 1;
        memcpy(c->context, ctx, n);
    }
}

/*
 * occurs check
 */

static int occurs_in_xstype(TVar *v, XsType *t);

static int occurs_in(TVar *v, RType r) {
    r = rt_resolve(r);
    if (r.is_var) return r.var->id == v->id;
    return occurs_in_xstype(v, r.type);
}

static int occurs_in_xstype(TVar *v, XsType *t) {
    if (!t) return 0;
    switch (t->kind) {
    case TY_FN:
        for (int i = 0; i < t->fn_.nparams; i++)
            if (occurs_in_xstype(v, t->fn_.params[i])) return 1;
        return occurs_in_xstype(v, t->fn_.ret);
    case TY_ARRAY:  return occurs_in_xstype(v, t->array.inner);
    case TY_TUPLE:
        for (int i = 0; i < t->tuple.nelems; i++)
            if (occurs_in_xstype(v, t->tuple.elems[i])) return 1;
        return 0;
    case TY_OPTION: return occurs_in_xstype(v, t->option.inner);
    case TY_RESULT:
        return occurs_in_xstype(v, t->result.ok) ||
               occurs_in_xstype(v, t->result.err);
    case TY_NAMED:
        for (int i = 0; i < t->named.nargs; i++)
            if (occurs_in_xstype(v, t->named.args[i])) return 1;
        return 0;
    default: return 0;
    }
}

/*
 * errors
 */

#define MAX_ERRORS 256
#define ERROR_BUF_SZ 256

typedef struct ErrorList {
    char  errors[MAX_ERRORS][ERROR_BUF_SZ];
    int   len;
} ErrorList;

static void errlist_init(ErrorList *el) { el->len = 0; }

static void errlist_add(ErrorList *el, const char *fmt, ...) {
    if (el->len >= MAX_ERRORS) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(el->errors[el->len], ERROR_BUF_SZ, fmt, ap);
    va_end(ap);
    el->len++;
}

/*
 * unification
 */

static int is_primitive_kind(TyKind k) {
    return k >= TY_BOOL && k <= TY_NEVER;
}

static int is_integer_kind(TyKind k) {
    return k >= TY_I8 && k <= TY_U64;
}

static int is_float_kind(TyKind k) {
    return k == TY_F32 || k == TY_F64;
}

static int is_numeric_kind(TyKind k) {
    return is_integer_kind(k) || is_float_kind(k);
}

/*
 * unify(t1, t2, subst, errors) — unify two resolved types.
 * On success, may extend subst (and set TVar links).
 * On failure, appends to errors and continues.
 * Returns 1 on success, 0 on failure.
 */
static int unify(RType t1, RType t2, Substitution *subst, ErrorList *errors,
                 const char *ctx, Span span) {
    t1 = subst_apply(subst, t1);
    t2 = subst_apply(subst, t2);

    /* Both identical TVars */
    if (t1.is_var && t2.is_var && t1.var->id == t2.var->id)
        return 1;

    /* Both concrete and pointer-equal */
    if (!t1.is_var && !t2.is_var && t1.type == t2.type)
        return 1;

    /* Any / Unknown / Never absorbs */
    if (!t1.is_var && t1.type) {
        TyKind k1 = t1.type->kind;
        if (k1 == TY_DYN || k1 == TY_UNKNOWN || k1 == TY_NEVER)
            return 1;
    }
    if (!t2.is_var && t2.type) {
        TyKind k2 = t2.type->kind;
        if (k2 == TY_DYN || k2 == TY_UNKNOWN || k2 == TY_NEVER)
            return 1;
    }

    /* TVar on left — bind */
    if (t1.is_var) {
        if (!t1.var) return 0;
        if (occurs_in(t1.var, t2)) {
            /* Recursive type — bind anyway (XS supports nominal recursion) */
        }
        subst_set(subst, t1.var->id, t2);
        if (t2.is_var) {
            t1.var->link = t2.var;
            t1.var->is_type = 0;
        } else {
            t1.var->link = t2.type;
            t1.var->is_type = 1;
        }
        return 1;
    }

    /* TVar on right — bind */
    if (t2.is_var) {
        if (!t2.var) return 0;
        if (occurs_in(t2.var, t1)) {
            /* Recursive type */
        }
        subst_set(subst, t2.var->id, t1);
        if (t1.is_var) {
            t2.var->link = t1.var;
            t2.var->is_type = 0;
        } else {
            t2.var->link = t1.type;
            t2.var->is_type = 1;
        }
        return 1;
    }

    /* Both concrete XsType* */
    XsType *a = t1.type;
    XsType *b = t2.type;
    if (!a || !b) return 1; /* null safety */

    /* Same primitive kind */
    if (a->kind == b->kind && is_primitive_kind(a->kind))
        return 1;

    /* Numeric coercion: all integer types unify, all float types unify,
       int<->float also unifies (XS is numeric-coercion friendly) */
    if (is_numeric_kind(a->kind) && is_numeric_kind(b->kind))
        return 1;

    /* Kind mismatch */
    if (a->kind != b->kind) {
        char *sa = ty_to_str(a);
        char *sb = ty_to_str(b);
        errlist_add(errors, "Cannot unify %s with %s%s%s",
                    sa, sb, ctx ? " (" : "", ctx ? ctx : "");
        free(sa); free(sb);
        return 0;
    }

    /* Structural unification of compound types */
    switch (a->kind) {
    case TY_FN: {
        if (a->fn_.nparams != b->fn_.nparams) {
            char *sa = ty_to_str(a);
            char *sb = ty_to_str(b);
            errlist_add(errors, "Arity mismatch: %s vs %s", sa, sb);
            free(sa); free(sb);
            return 0;
        }
        int ok = 1;
        for (int i = 0; i < a->fn_.nparams; i++) {
            if (!unify(rt_type(a->fn_.params[i]), rt_type(b->fn_.params[i]),
                       subst, errors, ctx, span))
                ok = 0;
        }
        if (!unify(rt_type(a->fn_.ret), rt_type(b->fn_.ret),
                   subst, errors, ctx, span))
            ok = 0;
        return ok;
    }
    case TY_ARRAY:
        return unify(rt_type(a->array.inner), rt_type(b->array.inner),
                     subst, errors, ctx, span);

    case TY_TUPLE: {
        if (a->tuple.nelems != b->tuple.nelems) {
            errlist_add(errors, "Tuple arity mismatch: %d vs %d",
                        a->tuple.nelems, b->tuple.nelems);
            return 0;
        }
        int ok = 1;
        for (int i = 0; i < a->tuple.nelems; i++) {
            if (!unify(rt_type(a->tuple.elems[i]), rt_type(b->tuple.elems[i]),
                       subst, errors, ctx, span))
                ok = 0;
        }
        return ok;
    }
    case TY_OPTION:
        return unify(rt_type(a->option.inner), rt_type(b->option.inner),
                     subst, errors, ctx, span);

    case TY_RESULT: {
        int ok = unify(rt_type(a->result.ok), rt_type(b->result.ok),
                       subst, errors, ctx, span);
        ok = unify(rt_type(a->result.err), rt_type(b->result.err),
                   subst, errors, ctx, span) && ok;
        return ok;
    }
    case TY_NAMED: {
        if (!a->named.name || !b->named.name ||
            strcmp(a->named.name, b->named.name) != 0) {
            char *sa = ty_to_str(a);
            char *sb = ty_to_str(b);
            errlist_add(errors, "Cannot unify %s with %s", sa, sb);
            free(sa); free(sb);
            return 0;
        }
        int n = a->named.nargs < b->named.nargs ? a->named.nargs : b->named.nargs;
        int ok = 1;
        for (int i = 0; i < n; i++) {
            if (!unify(rt_type(a->named.args[i]), rt_type(b->named.args[i]),
                       subst, errors, ctx, span))
                ok = 0;
        }
        return ok;
    }
    case TY_GENERIC: {
        /* Generic params: same name = same type, different = wildcard */
        if (a->generic.name && b->generic.name &&
            strcmp(a->generic.name, b->generic.name) == 0)
            return 1;
        return 1; /* treat generic params as wildcards */
    }
    default:
        /* Exact equality for remaining primitives */
        if (ty_equal(a, b)) return 1;
        {
            char *sa = ty_to_str(a);
            char *sb = ty_to_str(b);
            errlist_add(errors, "Cannot unify %s with %s", sa, sb);
            free(sa); free(sb);
        }
        return 0;
    }
}

/*
 * constraint solving
 */

static void solve_constraints(ConstraintList *cl, Substitution *subst,
                               ErrorList *errors) {
    for (int i = 0; i < cl->len; i++) {
        Constraint *c = &cl->items[i];
        unify(c->lhs, c->rhs, subst, errors, c->context, c->span);
    }
}

/*
 * node type map
 */

#define NODEMAP_INIT_CAP 512

typedef struct NodeTypeEntry {
    int    node_id; /* (int)(intptr_t)node pointer */
    RType  rtype;
} NodeTypeEntry;

typedef struct NodeTypeMap {
    NodeTypeEntry *entries;
    int            len;
    int            cap;
} NodeTypeMap;

static NodeTypeMap ntmap_new(void) {
    NodeTypeMap m;
    m.cap = NODEMAP_INIT_CAP;
    m.len = 0;
    m.entries = xs_calloc((size_t)m.cap, sizeof(NodeTypeEntry));
    return m;
}

static void ntmap_free(NodeTypeMap *m) {
    free(m->entries);
    m->entries = NULL;
    m->len = m->cap = 0;
}

static void ntmap_set(NodeTypeMap *m, Node *node, RType rt) {
    int nid = (int)(intptr_t)node;
    /* check existing */
    for (int i = 0; i < m->len; i++) {
        if (m->entries[i].node_id == nid) {
            m->entries[i].rtype = rt;
            return;
        }
    }
    if (m->len >= m->cap) {
        m->cap *= 2;
        m->entries = xs_realloc(m->entries, (size_t)m->cap * sizeof(NodeTypeEntry));
    }
    m->entries[m->len].node_id = nid;
    m->entries[m->len].rtype   = rt;
    m->len++;
}

/*
 * constraint gen context
 */

/* struct registry */

#define STRUCT_REG_CAP 64

typedef struct StructRegEntry {
    char *name;
    Node *decl;
} StructRegEntry;

typedef struct StructRegistry {
    StructRegEntry *entries;
    int len, cap;
} StructRegistry;

static StructRegistry sreg_new(void) {
    StructRegistry r;
    r.cap = STRUCT_REG_CAP;
    r.len = 0;
    r.entries = xs_calloc((size_t)r.cap, sizeof(StructRegEntry));
    return r;
}

static void sreg_free(StructRegistry *r) {
    for (int i = 0; i < r->len; i++) free(r->entries[i].name);
    free(r->entries);
    r->entries = NULL;
    r->len = r->cap = 0;
}

static void sreg_put(StructRegistry *r, const char *name, Node *decl) {
    if (!name) return;
    for (int i = 0; i < r->len; i++) {
        if (strcmp(r->entries[i].name, name) == 0) {
            r->entries[i].decl = decl;
            return;
        }
    }
    if (r->len >= r->cap) {
        r->cap *= 2;
        r->entries = xs_realloc(r->entries, (size_t)r->cap * sizeof(StructRegEntry));
    }
    r->entries[r->len].name = strdup(name);
    r->entries[r->len].decl = decl;
    r->len++;
}

static Node *sreg_get(StructRegistry *r, const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < r->len; i++)
        if (strcmp(r->entries[i].name, name) == 0) return r->entries[i].decl;
    return NULL;
}

/* impl registry */

#define IMPL_REG_CAP 64

typedef struct ImplRegEntry {
    char *key;   /* "TypeName.method" */
    Node *fn_decl;
} ImplRegEntry;

typedef struct ImplRegistry {
    ImplRegEntry *entries;
    int len, cap;
} ImplRegistry;

static ImplRegistry ireg_new(void) {
    ImplRegistry r;
    r.cap = IMPL_REG_CAP;
    r.len = 0;
    r.entries = xs_calloc((size_t)r.cap, sizeof(ImplRegEntry));
    return r;
}

static void ireg_free(ImplRegistry *r) {
    for (int i = 0; i < r->len; i++) free(r->entries[i].key);
    free(r->entries);
    r->entries = NULL;
    r->len = r->cap = 0;
}

static void ireg_put(ImplRegistry *r, const char *type_name, const char *method, Node *fn) {
    if (!type_name || !method) return;
    size_t klen = strlen(type_name) + 1 + strlen(method) + 1;
    char *key = xs_malloc(klen);
    snprintf(key, klen, "%s.%s", type_name, method);
    for (int i = 0; i < r->len; i++) {
        if (strcmp(r->entries[i].key, key) == 0) {
            r->entries[i].fn_decl = fn;
            free(key);
            return;
        }
    }
    if (r->len >= r->cap) {
        r->cap *= 2;
        r->entries = xs_realloc(r->entries, (size_t)r->cap * sizeof(ImplRegEntry));
    }
    r->entries[r->len].key = key;
    r->entries[r->len].fn_decl = fn;
    r->len++;
}

static Node *ireg_get(ImplRegistry *r, const char *type_name, const char *method) {
    if (!type_name || !method) return NULL;
    size_t klen = strlen(type_name) + 1 + strlen(method) + 1;
    char *key = xs_malloc(klen);
    snprintf(key, klen, "%s.%s", type_name, method);
    for (int i = 0; i < r->len; i++) {
        if (strcmp(r->entries[i].key, key) == 0) {
            free(key);
            return r->entries[i].fn_decl;
        }
    }
    free(key);
    return NULL;
}

typedef struct CGen {
    HMEnv          *env;
    ConstraintList  constraints;
    NodeTypeMap     node_types;
    ErrorList      *errors;
    RType           current_fn_ret; /* return type of enclosing fn, or rt_type(NULL) */
    int             has_fn_ret;
    StructRegistry  struct_reg;     /* struct name → decl node */
    ImplRegistry    impl_reg;       /* "Type.method" → fn decl node */
} CGen;

static CGen cgen_new(HMEnv *env, ErrorList *errors) {
    CGen cg;
    cg.env = env;
    cg.constraints = clist_new();
    cg.node_types = ntmap_new();
    cg.errors = errors;
    cg.current_fn_ret = rt_type(NULL);
    cg.has_fn_ret = 0;
    cg.struct_reg = sreg_new();
    cg.impl_reg = ireg_new();
    return cg;
}

static void cgen_destroy(CGen *cg) {
    clist_free(&cg->constraints);
    ntmap_free(&cg->node_types);
    sreg_free(&cg->struct_reg);
    ireg_free(&cg->impl_reg);
}

static TVar *cgen_fresh(void) {
    return tvar_new();
}

static void cgen_emit(CGen *cg, RType lhs, RType rhs, Span span, const char *ctx) {
    clist_push(&cg->constraints, lhs, rhs, span, ctx);
}

static RType cgen_record(CGen *cg, Node *node, RType t) {
    ntmap_set(&cg->node_types, node, t);
    return t;
}

/* Forward declarations */
static RType cgen_infer_expr(CGen *cg, Node *expr);
static void  cgen_infer_stmt(CGen *cg, Node *stmt);
static RType cgen_infer_block(CGen *cg, Node *block);

/*
 * type annotation resolution
 */

static RType resolve_type_ann(TypeExpr *ann) {
    if (!ann) return rt_var(cgen_fresh());

    switch (ann->kind) {
    case TEXPR_NAMED: {
        if (!ann->name) return rt_var(cgen_fresh());
        XsType *t = ty_from_name(ann->name);
        if (t) return rt_type(t);
        /* Not a builtin — could be a user-defined type */
        if (ann->nargs > 0) {
            XsType **args = xs_malloc(sizeof(XsType*) * ann->nargs);
            for (int i = 0; i < ann->nargs; i++)
                args[i] = rt_to_xstype(resolve_type_ann(ann->args[i]));
            return rt_type(ty_named(ann->name, args, ann->nargs));
        }
        return rt_type(ty_named(ann->name, NULL, 0));
    }
    case TEXPR_ARRAY: {
        XsType *inner = rt_to_xstype(resolve_type_ann(ann->inner));
        return rt_type(ty_array(inner));
    }
    case TEXPR_TUPLE: {
        XsType **elems = xs_malloc(sizeof(XsType*) * (ann->nelems ? ann->nelems : 1));
        for (int i = 0; i < ann->nelems; i++)
            elems[i] = rt_to_xstype(resolve_type_ann(ann->elems[i]));
        XsType *t = ty_tuple(elems, ann->nelems);
        free(elems);
        return rt_type(t);
    }
    case TEXPR_FN: {
        XsType **params = xs_malloc(sizeof(XsType*) * (ann->nparams ? ann->nparams : 1));
        for (int i = 0; i < ann->nparams; i++)
            params[i] = rt_to_xstype(resolve_type_ann(ann->params[i]));
        XsType *ret = rt_to_xstype(resolve_type_ann(ann->ret));
        XsType *t = ty_fn(params, ann->nparams, ret);
        free(params);
        return rt_type(t);
    }
    case TEXPR_OPTION: {
        XsType *inner = rt_to_xstype(resolve_type_ann(ann->inner));
        return rt_type(ty_option(inner));
    }
    case TEXPR_INFER:
        return rt_var(cgen_fresh());
    }
    return rt_var(cgen_fresh());
}

/*
 * pattern binding
 */

static void bind_pattern_mono(CGen *cg, Node *pat, RType t) {
    if (!pat) return;

    switch (pat->tag) {
    case NODE_PAT_IDENT:
        hmenv_define_mono(cg->env, pat->pat_ident.name, t);
        break;
    case NODE_PAT_WILD:
        break;
    case NODE_PAT_TUPLE: {
        int n = pat->pat_tuple.elems.len;
        RType resolved = rt_resolve(t);
        if (!resolved.is_var && resolved.type &&
            resolved.type->kind == TY_TUPLE &&
            resolved.type->tuple.nelems == n) {
            for (int i = 0; i < n; i++)
                bind_pattern_mono(cg, pat->pat_tuple.elems.items[i],
                                  rt_type(resolved.type->tuple.elems[i]));
        } else {
            /* Create fresh TVars for each element and build a tuple
             * whose element types are connected to these TVars via
             * constraints, so that unification propagates element
             * types back through the tuple structure. */
            TVar **elem_tvars = xs_malloc(sizeof(TVar*) * (n ? n : 1));
            XsType **elem_types = xs_malloc(sizeof(XsType*) * (n ? n : 1));
            for (int i = 0; i < n; i++) {
                elem_tvars[i] = cgen_fresh();
                /* Use rt_to_xstype to snapshot the TVar into an XsType
                 * for tuple construction.  The real type flows through
                 * the per-element constraints emitted below. */
                elem_types[i] = rt_to_xstype(rt_var(elem_tvars[i]));
            }
            XsType *tup = ty_tuple(elem_types, n);
            /* Structural constraint: t must be this tuple shape */
            cgen_emit(cg, t, rt_type(tup), pat->span, "tuple pattern");
            /* Per-element constraints: when t resolves to a concrete
             * tuple, each element TVar gets unified with the actual
             * element type, ensuring propagation back to bound names. */
            RType t_resolved = rt_resolve(t);
            if (!t_resolved.is_var && t_resolved.type &&
                t_resolved.type->kind == TY_TUPLE &&
                t_resolved.type->tuple.nelems == n) {
                for (int i = 0; i < n; i++)
                    cgen_emit(cg, rt_var(elem_tvars[i]),
                              rt_type(t_resolved.type->tuple.elems[i]),
                              pat->span, "tuple elem");
            }
            for (int i = 0; i < n; i++)
                bind_pattern_mono(cg, pat->pat_tuple.elems.items[i],
                                  rt_var(elem_tvars[i]));
            free(elem_types);
            free(elem_tvars);
        }
        break;
    }
    case NODE_PAT_STRUCT:
        /* Bind each field name from the struct pattern */
        for (int i = 0; i < pat->pat_struct.fields.len; i++) {
            NodePair *fp = &pat->pat_struct.fields.items[i];
            TVar *ftv = cgen_fresh();
            if (fp->val)
                bind_pattern_mono(cg, fp->val, rt_var(ftv));
            else if (fp->key)
                hmenv_define_mono(cg->env, fp->key, rt_var(ftv));
        }
        break;
    case NODE_PAT_ENUM:
        /* Bind args of enum variant pattern */
        for (int i = 0; i < pat->pat_enum.args.len; i++) {
            TVar *tv = cgen_fresh();
            bind_pattern_mono(cg, pat->pat_enum.args.items[i], rt_var(tv));
        }
        break;
    case NODE_PAT_OR:
        if (pat->pat_or.left) bind_pattern_mono(cg, pat->pat_or.left, t);
        if (pat->pat_or.right) bind_pattern_mono(cg, pat->pat_or.right, t);
        break;
    case NODE_PAT_GUARD:
        if (pat->pat_guard.pattern) bind_pattern_mono(cg, pat->pat_guard.pattern, t);
        if (pat->pat_guard.guard) cgen_infer_expr(cg, pat->pat_guard.guard);
        break;
    case NODE_PAT_CAPTURE:
        if (pat->pat_capture.name)
            hmenv_define_mono(cg->env, pat->pat_capture.name, t);
        if (pat->pat_capture.pattern)
            bind_pattern_mono(cg, pat->pat_capture.pattern, t);
        break;
    default:
        break; /* PAT_LIT, PAT_RANGE, PAT_SLICE — no bindings */
    }
}

/*
 * expression inference
 */

/* Helper: check operators */
static int op_is_comparison(const char *op) {
    return (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
            strcmp(op, "<")  == 0 || strcmp(op, ">")  == 0 ||
            strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0);
}

static int op_is_logical(const char *op) {
    return (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0);
}

static int op_is_arithmetic(const char *op) {
    return (strcmp(op, "+")  == 0 || strcmp(op, "-")  == 0 ||
            strcmp(op, "*")  == 0 || strcmp(op, "/")  == 0 ||
            strcmp(op, "%")  == 0 || strcmp(op, "**") == 0);
}

static int op_is_bitwise(const char *op) {
    return (strcmp(op, "&") == 0 || strcmp(op, "|")  == 0 ||
            strcmp(op, "^") == 0 || strcmp(op, "<<") == 0 ||
            strcmp(op, ">>") == 0);
}

static RType infer_binop(CGen *cg, Node *expr) {
    RType lt = cgen_infer_expr(cg, expr->binop.left);
    RType rt = cgen_infer_expr(cg, expr->binop.right);
    const char *op = expr->binop.op;

    /* Comparison → Bool */
    if (op_is_comparison(op)) {
        cgen_emit(cg, lt, rt, expr->span, "comparison operands");
        return rt_type(ty_bool());
    }

    /* Logical → Bool */
    if (op_is_logical(op)) {
        cgen_emit(cg, lt, rt_type(ty_bool()), expr->span, "logical operand");
        cgen_emit(cg, rt, rt_type(ty_bool()), expr->span, "logical operand");
        return rt_type(ty_bool());
    }

    /* Arithmetic: unify operands, result is the unified type */
    if (op_is_arithmetic(op)) {
        /* String concatenation: str + anything → str */
        RType lt_r = rt_resolve(lt);
        if (!lt_r.is_var && lt_r.type && lt_r.type->kind == TY_STR)
            return rt_type(ty_str());

        TVar *result_tv = cgen_fresh();
        cgen_emit(cg, rt_var(result_tv), lt, expr->span, "arith operand");
        cgen_emit(cg, rt_var(result_tv), rt, expr->span, "arith operand");
        return rt_var(result_tv);
    }

    /* Bitwise */
    if (op_is_bitwise(op)) {
        cgen_emit(cg, lt, rt, expr->span, "bitwise operands");
        return lt;
    }

    /* Range operators */
    if (strcmp(op, "..") == 0 || strcmp(op, "..=") == 0) {
        XsType *args[1];
        args[0] = rt_to_xstype(lt);
        return rt_type(ty_named("Range", args, 1));
    }

    /* Default: unify with left operand */
    TVar *result_tv = cgen_fresh();
    cgen_emit(cg, rt_var(result_tv), lt, expr->span, "binop");
    return rt_var(result_tv);
}

static RType infer_unary(CGen *cg, Node *expr) {
    RType t = cgen_infer_expr(cg, expr->unary.expr);
    const char *op = expr->unary.op;

    if (strcmp(op, "!") == 0 || strcmp(op, "not") == 0)
        return rt_type(ty_bool());
    if (strcmp(op, "-") == 0 || strcmp(op, "+") == 0 || strcmp(op, "~") == 0)
        return t;
    return t;
}

static RType infer_call(CGen *cg, Node *expr) {
    RType callee_t = cgen_infer_expr(cg, expr->call.callee);
    int argc = expr->call.args.len;
    RType *arg_ts = xs_malloc(sizeof(RType) * (argc ? argc : 1));

    for (int i = 0; i < argc; i++)
        arg_ts[i] = cgen_infer_expr(cg, expr->call.args.items[i]);

    /* Also infer kwargs */
    for (int i = 0; i < expr->call.kwargs.len; i++) {
        RType kt = cgen_infer_expr(cg, expr->call.kwargs.items[i].val);
        (void)kt;
    }

    TVar *ret_tv = cgen_fresh();

    /* If callee resolves to a known FunctionType, use its return directly */
    RType callee_r = rt_resolve(callee_t);
    if (!callee_r.is_var && callee_r.type && callee_r.type->kind == TY_FN) {
        XsType *fn = callee_r.type;
        /* Unify arg types with param types */
        int n = argc < fn->fn_.nparams ? argc : fn->fn_.nparams;
        for (int i = 0; i < n; i++)
            cgen_emit(cg, arg_ts[i], rt_type(fn->fn_.params[i]),
                      expr->span, "call arg");
        free(arg_ts);
        return rt_type(fn->fn_.ret);
    }

    /* If callee resolves to a named type (struct/class constructor) */
    if (!callee_r.is_var && callee_r.type && callee_r.type->kind == TY_NAMED) {
        free(arg_ts);
        return callee_r;
    }

    /* Build expected fn type and constrain callee */
    XsType **param_types = xs_malloc(sizeof(XsType*) * (argc ? argc : 1));
    for (int i = 0; i < argc; i++)
        param_types[i] = rt_to_xstype(arg_ts[i]);
    XsType *expected_fn = ty_fn(param_types, argc, ty_unknown());

    /* Only emit constraint if callee might be callable */
    if (callee_r.is_var || (callee_r.type &&
        (callee_r.type->kind == TY_FN || callee_r.type->kind == TY_DYN ||
         callee_r.type->kind == TY_UNKNOWN))) {
        cgen_emit(cg, callee_t, rt_type(expected_fn), expr->span, "call");
    }

    free(param_types);
    free(arg_ts);
    return rt_var(ret_tv);
}

static RType infer_method_call(CGen *cg, Node *expr) {
    RType obj_t = cgen_infer_expr(cg, expr->method_call.obj);
    int argc = expr->method_call.args.len;
    RType *arg_ts = xs_malloc(sizeof(RType) * (argc ? argc : 1));
    for (int i = 0; i < argc; i++)
        arg_ts[i] = cgen_infer_expr(cg, expr->method_call.args.items[i]);

    const char *method = expr->method_call.method;
    RType obj_r = rt_resolve(obj_t);

    /* array methods */
    if (!obj_r.is_var && obj_r.type && obj_r.type->kind == TY_ARRAY && method) {
        XsType *elem = obj_r.type->array.inner;
        if (strcmp(method, "push") == 0) {
            /* push returns unit; constrain arg to elem type */
            if (argc > 0 && elem)
                cgen_emit(cg, arg_ts[0], rt_type(elem), expr->span, "array.push arg");
            free(arg_ts);
            return rt_type(ty_unit());
        }
        if (strcmp(method, "pop") == 0) {
            /* pop returns Option<elem> — approximate as elem */
            free(arg_ts);
            return elem ? rt_type(elem) : rt_var(cgen_fresh());
        }
        if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0) {
            free(arg_ts);
            return rt_type(ty_i64());
        }
        if (strcmp(method, "map") == 0 || strcmp(method, "filter") == 0) {
            /* map/filter: returns Array<?>  */
            free(arg_ts);
            if (strcmp(method, "filter") == 0)
                return rt_type(ty_array(elem ? elem : ty_unknown()));
            /* map: result element type is unknown without analyzing the closure */
            return rt_type(ty_array(ty_unknown()));
        }
    }

    // string methods
    if (!obj_r.is_var && obj_r.type && obj_r.type->kind == TY_STR && method) {
        if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0) {
            free(arg_ts);
            return rt_type(ty_i64());
        }
        if (strcmp(method, "split") == 0) {
            free(arg_ts);
            return rt_type(ty_array(ty_str()));
        }
        if (strcmp(method, "trim") == 0 || strcmp(method, "to_upper") == 0 ||
            strcmp(method, "to_lower") == 0 || strcmp(method, "replace") == 0 ||
            strcmp(method, "substr") == 0 || strcmp(method, "slice") == 0) {
            free(arg_ts);
            return rt_type(ty_str());
        }
        if (strcmp(method, "contains") == 0 || strcmp(method, "starts_with") == 0 ||
            strcmp(method, "ends_with") == 0) {
            free(arg_ts);
            return rt_type(ty_bool());
        }
    }

    /* impl lookup */
    if (!obj_r.is_var && obj_r.type && obj_r.type->kind == TY_NAMED &&
        obj_r.type->named.name && method) {
        Node *fn = ireg_get(&cg->impl_reg, obj_r.type->named.name, method);
        if (fn && fn->tag == NODE_FN_DECL) {
            /* Use the fn decl's return type annotation if available */
            if (fn->fn_decl.ret_type) {
                RType ret = resolve_type_ann(fn->fn_decl.ret_type);
                free(arg_ts);
                return ret;
            }
        }
    }

    /* Fall back to fresh type variable for unknown receiver/method */
    free(arg_ts);
    return rt_var(cgen_fresh());
}

static RType infer_field(CGen *cg, Node *expr) {
    RType obj_t = cgen_infer_expr(cg, expr->field.obj);
    RType obj_r = rt_resolve(obj_t);

    /* Tuple field: .0, .1, etc. */
    if (!obj_r.is_var && obj_r.type && obj_r.type->kind == TY_TUPLE) {
        char *end;
        long idx = strtol(expr->field.name, &end, 10);
        if (*end == '\0' && idx >= 0 && idx < obj_r.type->tuple.nelems)
            return rt_type(obj_r.type->tuple.elems[idx]);
    }

    /* Named struct field lookup */
    if (!obj_r.is_var && obj_r.type && obj_r.type->kind == TY_NAMED &&
        obj_r.type->named.name && expr->field.name) {
        Node *decl = sreg_get(&cg->struct_reg, obj_r.type->named.name);
        if (decl && decl->tag == NODE_STRUCT_DECL) {
            NodePairList *fl = &decl->struct_decl.fields;
            for (int i = 0; i < fl->len; i++) {
                if (fl->items[i].key &&
                    strcmp(fl->items[i].key, expr->field.name) == 0) {
                    /* If the field has a default value, infer its type */
                    if (fl->items[i].val) {
                        RType ft = cgen_infer_expr(cg, fl->items[i].val);
                        RType fr = rt_resolve(ft);
                        if (!fr.is_var && fr.type &&
                            fr.type->kind != TY_UNKNOWN)
                            return fr;
                    }
                    /* Field found but no inferrable type — return fresh var
                       (still better than not finding the field at all) */
                    return rt_var(cgen_fresh());
                }
            }
        }
    }

    return rt_var(cgen_fresh());
}

static RType infer_index(CGen *cg, Node *expr) {
    RType obj_t = cgen_infer_expr(cg, expr->index.obj);
    cgen_infer_expr(cg, expr->index.index);

    RType obj_r = rt_resolve(obj_t);
    if (!obj_r.is_var && obj_r.type) {
        if (obj_r.type->kind == TY_ARRAY && obj_r.type->array.inner)
            return rt_type(obj_r.type->array.inner);
        if (obj_r.type->kind == TY_STR)
            return rt_type(ty_char());
    }

    /* Emit constraint: obj must be Array<elem_tv> */
    TVar *elem_tv = cgen_fresh();
    XsType *arr_t = ty_array(ty_unknown());
    cgen_emit(cg, obj_t, rt_type(arr_t), expr->span, "index");
    return rt_var(elem_tv);
}

static RType infer_if(CGen *cg, Node *expr) {
    /* Infer condition */
    cgen_infer_expr(cg, expr->if_expr.cond);

    /* Infer then branch */
    RType then_t;
    if (expr->if_expr.then) {
        if (expr->if_expr.then->tag == NODE_BLOCK)
            then_t = cgen_infer_block(cg, expr->if_expr.then);
        else
            then_t = cgen_infer_expr(cg, expr->if_expr.then);
    } else {
        then_t = rt_type(ty_unit());
    }

    /* Infer elif branches */
    for (int i = 0; i < expr->if_expr.elif_conds.len; i++) {
        cgen_infer_expr(cg, expr->if_expr.elif_conds.items[i]);
        if (i < expr->if_expr.elif_thens.len) {
            Node *elif_body = expr->if_expr.elif_thens.items[i];
            if (elif_body->tag == NODE_BLOCK)
                cgen_infer_block(cg, elif_body);
            else
                cgen_infer_expr(cg, elif_body);
        }
    }

    /* Infer else branch */
    if (expr->if_expr.else_branch) {
        RType else_t;
        if (expr->if_expr.else_branch->tag == NODE_BLOCK)
            else_t = cgen_infer_block(cg, expr->if_expr.else_branch);
        else
            else_t = cgen_infer_expr(cg, expr->if_expr.else_branch);

        /* Unify then and else branches */
        RType then_r = rt_resolve(then_t);
        RType else_r = rt_resolve(else_t);
        if (!then_r.is_var && !else_r.is_var && then_r.type && else_r.type) {
            if (then_r.type->kind == else_r.type->kind &&
                ty_equal(then_r.type, else_r.type)) {
                return then_t;
            }
            if (then_r.type->kind == TY_NEVER || then_r.type->kind == TY_UNKNOWN)
                return else_t;
            if (else_r.type->kind == TY_NEVER || else_r.type->kind == TY_UNKNOWN)
                return then_t;
            /* Different types: try unification via fresh TVar */
            TVar *result_tv = cgen_fresh();
            cgen_emit(cg, rt_var(result_tv), then_t, expr->span, "if then");
            cgen_emit(cg, rt_var(result_tv), else_t, expr->span, "if else");
            return rt_var(result_tv);
        }
        /* At least one is a TVar: emit constraints */
        TVar *result_tv = cgen_fresh();
        cgen_emit(cg, rt_var(result_tv), then_t, expr->span, "if then");
        cgen_emit(cg, rt_var(result_tv), else_t, expr->span, "if else");
        return rt_var(result_tv);
    }

    /* No else: if-expression type is Unit */
    return rt_type(ty_unit());
}

static RType infer_match(CGen *cg, Node *expr) {
    cgen_infer_expr(cg, expr->match.subject);

    int narms = expr->match.arms.len;
    if (narms == 0) return rt_type(ty_unit());

    TVar *result_tv = cgen_fresh();

    for (int i = 0; i < narms; i++) {
        MatchArm *arm = &expr->match.arms.items[i];
        /* New scope for each arm */
        HMEnv *arm_env = hmenv_new(cg->env);
        HMEnv *saved = cg->env;
        cg->env = arm_env;

        /* Bind pattern variables */
        if (arm->pattern)
            bind_pattern_mono(cg, arm->pattern, rt_var(cgen_fresh()));

        /* Infer guard */
        if (arm->guard)
            cgen_infer_expr(cg, arm->guard);

        /* Infer body */
        RType arm_t;
        if (arm->body) {
            if (arm->body->tag == NODE_BLOCK)
                arm_t = cgen_infer_block(cg, arm->body);
            else
                arm_t = cgen_infer_expr(cg, arm->body);
        } else {
            arm_t = rt_type(ty_unit());
        }

        cgen_emit(cg, rt_var(result_tv), arm_t, expr->span, "match arm");

        cg->env = saved;
        hmenv_free(arm_env);
    }

    return rt_var(result_tv);
}

static RType cgen_infer_block(CGen *cg, Node *block) {
    if (!block) return rt_type(ty_unit());

    HMEnv *child_env = hmenv_new(cg->env);
    HMEnv *saved = cg->env;
    cg->env = child_env;

    if (block->tag == NODE_BLOCK) {
        for (int i = 0; i < block->block.stmts.len; i++)
            cgen_infer_stmt(cg, block->block.stmts.items[i]);
    }

    RType result = rt_type(ty_unit());
    if (block->tag == NODE_BLOCK && block->block.expr)
        result = cgen_infer_expr(cg, block->block.expr);

    cg->env = saved;
    hmenv_free(child_env);
    return result;
}

static RType infer_lambda(CGen *cg, Node *expr) {
    HMEnv *child_env = hmenv_new(cg->env);
    HMEnv *saved_env = cg->env;
    cg->env = child_env;

    int np = expr->lambda.params.len;
    TVar **param_tvars = xs_malloc(sizeof(TVar*) * (np ? np : 1));
    XsType **param_types = xs_malloc(sizeof(XsType*) * (np ? np : 1));

    for (int i = 0; i < np; i++) {
        Param *p = &expr->lambda.params.items[i];
        if (p->type_ann) {
            RType ann = resolve_type_ann(p->type_ann);
            param_tvars[i] = NULL;
            param_types[i] = rt_to_xstype(ann);
            if (p->name)
                hmenv_define_mono(cg->env, p->name, ann);
        } else {
            param_tvars[i] = cgen_fresh();
            param_types[i] = ty_unknown(); /* will be resolved via subst */
            if (p->name)
                hmenv_define_mono(cg->env, p->name, rt_var(param_tvars[i]));
        }
    }

    /* Return type */
    TVar *ret_tv = cgen_fresh();
    RType saved_ret = cg->current_fn_ret;
    int saved_has = cg->has_fn_ret;
    cg->current_fn_ret = rt_var(ret_tv);
    cg->has_fn_ret = 1;

    /* Infer body */
    RType body_t;
    if (expr->lambda.body) {
        if (expr->lambda.body->tag == NODE_BLOCK)
            body_t = cgen_infer_block(cg, expr->lambda.body);
        else
            body_t = cgen_infer_expr(cg, expr->lambda.body);
    } else {
        body_t = rt_type(ty_unit());
    }

    cgen_emit(cg, rt_var(ret_tv), body_t, expr->span, "lambda return");

    cg->current_fn_ret = saved_ret;
    cg->has_fn_ret = saved_has;
    cg->env = saved_env;
    hmenv_free(child_env);

    XsType *fn_t = ty_fn(param_types, np, rt_to_xstype(rt_var(ret_tv)));
    free(param_tvars);
    free(param_types);
    return rt_type(fn_t);
}

static RType cgen_infer_expr(CGen *cg, Node *expr) {
    if (!expr) return rt_type(ty_unknown());

    RType result;

    switch (expr->tag) {
    /* --- Literals --- */
    case NODE_LIT_INT:
    case NODE_LIT_BIGINT:
        result = rt_type(ty_i64());
        break;
    case NODE_LIT_FLOAT:
        result = rt_type(ty_f64());
        break;
    case NODE_LIT_STRING:
    case NODE_INTERP_STRING:
        result = rt_type(ty_str());
        break;
    case NODE_LIT_BOOL:
        result = rt_type(ty_bool());
        break;
    case NODE_LIT_NULL:
        result = rt_type(ty_unknown()); /* null is polymorphic */
        break;
    case NODE_LIT_CHAR:
        result = rt_type(ty_char());
        break;

    /* --- Collections --- */
    case NODE_LIT_ARRAY: {
        int n = expr->lit_array.elems.len;
        if (n == 0) {
            result = rt_type(ty_array(ty_unknown()));
            break;
        }
        TVar *elem_tv = cgen_fresh();
        RType first_et = cgen_infer_expr(cg, expr->lit_array.elems.items[0]);
        cgen_emit(cg, rt_var(elem_tv), first_et, expr->span, "array element");
        for (int i = 1; i < n; i++) {
            RType et = cgen_infer_expr(cg, expr->lit_array.elems.items[i]);
            cgen_emit(cg, rt_var(elem_tv), et, expr->span, "array element");
        }
        result = rt_type(ty_array(rt_to_xstype(rt_var(elem_tv))));
        break;
    }
    case NODE_LIT_TUPLE: {
        int n = expr->lit_array.elems.len;
        XsType **elems = xs_malloc(sizeof(XsType*) * (n ? n : 1));
        for (int i = 0; i < n; i++)
            elems[i] = rt_to_xstype(cgen_infer_expr(cg, expr->lit_array.elems.items[i]));
        XsType *t = ty_tuple(elems, n);
        free(elems);
        result = rt_type(t);
        break;
    }
    case NODE_LIT_MAP: {
        /* Infer key and value types */
        int n = expr->lit_map.keys.len;
        for (int i = 0; i < n; i++) {
            cgen_infer_expr(cg, expr->lit_map.keys.items[i]);
            cgen_infer_expr(cg, expr->lit_map.vals.items[i]);
        }
        result = rt_type(ty_named("Map", NULL, 0));
        break;
    }

    /* --- Identifiers --- */
    case NODE_IDENT: {
        PolyType *poly = hmenv_lookup(cg->env, expr->ident.name);
        if (poly) {
            result = instantiate(poly);
        } else {
            /* Unknown name — fresh TVar */
            result = rt_var(cgen_fresh());
        }
        break;
    }

    /* --- Operators --- */
    case NODE_BINOP:
        result = infer_binop(cg, expr);
        break;
    case NODE_UNARY:
        result = infer_unary(cg, expr);
        break;

    /* --- Assignment --- */
    case NODE_ASSIGN: {
        RType val_t = cgen_infer_expr(cg, expr->assign.value);
        RType tgt_t = cgen_infer_expr(cg, expr->assign.target);
        cgen_emit(cg, tgt_t, val_t, expr->span, "assignment");
        result = val_t;
        break;
    }

    /* --- Calls --- */
    case NODE_CALL:
        result = infer_call(cg, expr);
        break;
    case NODE_METHOD_CALL:
        result = infer_method_call(cg, expr);
        break;

    /* --- Field / Index --- */
    case NODE_FIELD:
        result = infer_field(cg, expr);
        break;
    case NODE_INDEX:
        result = infer_index(cg, expr);
        break;
    case NODE_SCOPE:
        /* A::B::C — return named type for first part */
        if (expr->scope.nparts > 0) {
            result = rt_type(ty_named(expr->scope.parts[0], NULL, 0));
        } else {
            result = rt_var(cgen_fresh());
        }
        break;

    /* --- Control Flow --- */
    case NODE_IF:
        result = infer_if(cg, expr);
        break;
    case NODE_MATCH:
        result = infer_match(cg, expr);
        break;
    case NODE_BLOCK:
        result = cgen_infer_block(cg, expr);
        break;

    case NODE_WHILE: {
        cgen_infer_expr(cg, expr->while_loop.cond);
        if (expr->while_loop.body)
            cgen_infer_block(cg, expr->while_loop.body);
        result = rt_type(ty_unit());
        break;
    }
    case NODE_FOR: {
        RType iter_t = cgen_infer_expr(cg, expr->for_loop.iter);
        TVar *elem_tv = cgen_fresh();
        /* Constrain: iter_t = Array<elem_tv> */
        XsType *arr = ty_array(ty_unknown());
        cgen_emit(cg, iter_t, rt_type(arr), expr->span, "for iterator");

        /* Bind loop variable */
        HMEnv *loop_env = hmenv_new(cg->env);
        HMEnv *saved = cg->env;
        cg->env = loop_env;
        if (expr->for_loop.pattern)
            bind_pattern_mono(cg, expr->for_loop.pattern, rt_var(elem_tv));
        if (expr->for_loop.body)
            cgen_infer_block(cg, expr->for_loop.body);
        cg->env = saved;
        hmenv_free(loop_env);
        result = rt_type(ty_unit());
        break;
    }
    case NODE_LOOP: {
        if (expr->loop.body)
            cgen_infer_block(cg, expr->loop.body);
        result = rt_type(ty_unit());
        break;
    }

    /* --- Return / Break / Continue --- */
    case NODE_RETURN: {
        if (expr->ret.value) {
            RType ret_t = cgen_infer_expr(cg, expr->ret.value);
            if (cg->has_fn_ret)
                cgen_emit(cg, cg->current_fn_ret, ret_t, expr->span, "return");
        }
        result = rt_type(ty_never());
        break;
    }
    case NODE_BREAK: {
        if (expr->brk.value)
            cgen_infer_expr(cg, expr->brk.value);
        result = rt_type(ty_unit());
        break;
    }
    case NODE_CONTINUE:
        result = rt_type(ty_never());
        break;
    case NODE_YIELD: {
        if (expr->yield_.value)
            cgen_infer_expr(cg, expr->yield_.value);
        result = rt_type(ty_unit());
        break;
    }

    /* --- Exception Handling --- */
    case NODE_THROW:
        if (expr->throw_.value)
            cgen_infer_expr(cg, expr->throw_.value);
        result = rt_type(ty_never());
        break;
    case NODE_TRY: {
        TVar *tv = cgen_fresh();
        if (expr->try_.body) {
            RType body_t = cgen_infer_block(cg, expr->try_.body);
            cgen_emit(cg, rt_var(tv), body_t, expr->span, "try body");
        }
        for (int i = 0; i < expr->try_.catch_arms.len; i++) {
            MatchArm *arm = &expr->try_.catch_arms.items[i];
            if (arm->body) {
                RType arm_t;
                if (arm->body->tag == NODE_BLOCK)
                    arm_t = cgen_infer_block(cg, arm->body);
                else
                    arm_t = cgen_infer_expr(cg, arm->body);
                cgen_emit(cg, rt_var(tv), arm_t, expr->span, "catch arm");
            }
        }
        if (expr->try_.finally_block)
            cgen_infer_block(cg, expr->try_.finally_block);
        result = rt_var(tv);
        break;
    }
    case NODE_DEFER:
        if (expr->defer_.body)
            cgen_infer_expr(cg, expr->defer_.body);
        result = rt_type(ty_unit());
        break;

    /* --- Lambda --- */
    case NODE_LAMBDA:
        result = infer_lambda(cg, expr);
        break;

    /* --- Cast --- */
    case NODE_CAST: {
        if (expr->cast.expr)
            cgen_infer_expr(cg, expr->cast.expr);
        if (expr->cast.type_name) {
            XsType *t = ty_from_name(expr->cast.type_name);
            result = rt_type(t ? t : ty_named(expr->cast.type_name, NULL, 0));
        } else {
            result = rt_var(cgen_fresh());
        }
        break;
    }

    /* --- Range --- */
    case NODE_RANGE: {
        if (expr->range.start) cgen_infer_expr(cg, expr->range.start);
        if (expr->range.end)   cgen_infer_expr(cg, expr->range.end);
        result = rt_type(ty_array(ty_i64()));
        break;
    }

    /* --- Struct Init --- */
    case NODE_STRUCT_INIT: {
        for (int i = 0; i < expr->struct_init.fields.len; i++)
            cgen_infer_expr(cg, expr->struct_init.fields.items[i].val);
        if (expr->struct_init.path) {
            result = rt_type(ty_named(expr->struct_init.path, NULL, 0));
        } else {
            result = rt_type(ty_named("anonymous", NULL, 0));
        }
        break;
    }

    /* --- Spread --- */
    case NODE_SPREAD:
        result = expr->spread.expr ? cgen_infer_expr(cg, expr->spread.expr)
                                   : rt_type(ty_unknown());
        break;

    /* --- List Comprehension --- */
    case NODE_LIST_COMP: {
        TVar *elem_tv = cgen_fresh();
        /* Process clauses */
        for (int i = 0; i < expr->list_comp.clause_iters.len; i++)
            cgen_infer_expr(cg, expr->list_comp.clause_iters.items[i]);
        /* Infer element expression */
        if (expr->list_comp.element) {
            RType et = cgen_infer_expr(cg, expr->list_comp.element);
            cgen_emit(cg, rt_var(elem_tv), et, expr->span, "list comp elem");
        }
        result = rt_type(ty_array(rt_to_xstype(rt_var(elem_tv))));
        break;
    }

    /* --- Map Comprehension --- */
    case NODE_MAP_COMP: {
        /* Process clauses */
        for (int i = 0; i < expr->map_comp.clause_iters.len; i++)
            cgen_infer_expr(cg, expr->map_comp.clause_iters.items[i]);
        /* Infer key and value expressions */
        if (expr->map_comp.key)
            cgen_infer_expr(cg, expr->map_comp.key);
        if (expr->map_comp.value)
            cgen_infer_expr(cg, expr->map_comp.value);
        /* Map comprehension yields a map type (use generic map for now) */
        result = rt_type(ty_named("Map", NULL, 0));
        break;
    }

    /* --- Algebraic Effects --- */
    case NODE_PERFORM:
        for (int i = 0; i < expr->perform.args.len; i++)
            cgen_infer_expr(cg, expr->perform.args.items[i]);
        result = rt_var(cgen_fresh());
        break;
    case NODE_HANDLE:
        if (expr->handle.expr) cgen_infer_expr(cg, expr->handle.expr);
        result = rt_var(cgen_fresh());
        break;
    case NODE_RESUME:
        if (expr->resume_.value) cgen_infer_expr(cg, expr->resume_.value);
        result = rt_var(cgen_fresh());
        break;

    /* --- Async --- */
    case NODE_AWAIT:
        result = expr->await_.expr ? cgen_infer_expr(cg, expr->await_.expr)
                                   : rt_type(ty_unit());
        break;
    case NODE_SPAWN:
        result = expr->spawn_.expr ? cgen_infer_expr(cg, expr->spawn_.expr)
                                   : rt_type(ty_unit());
        break;
    case NODE_NURSERY:
        if (expr->nursery_.body) cgen_infer_block(cg, expr->nursery_.body);
        result = rt_type(ty_unit());
        break;

    /* --- Patterns (shouldn't appear as expressions, but handle gracefully) --- */
    case NODE_PAT_WILD:
    case NODE_PAT_IDENT:
    case NODE_PAT_LIT:
    case NODE_PAT_TUPLE:
    case NODE_PAT_STRUCT:
    case NODE_PAT_ENUM:
    case NODE_PAT_OR:
    case NODE_PAT_RANGE:
    case NODE_PAT_SLICE:
    case NODE_PAT_GUARD:
    case NODE_PAT_EXPR:
    case NODE_PAT_CAPTURE:
        result = rt_var(cgen_fresh());
        break;

    default:
        result = rt_var(cgen_fresh());
        break;
    }

    return cgen_record(cg, expr, result);
}

/*
 * statement inference
 */

static void infer_let_stmt(CGen *cg, Node *stmt) {
    RType ann_t;
    if (stmt->let.type_ann)
        ann_t = resolve_type_ann(stmt->let.type_ann);
    else
        ann_t = rt_var(cgen_fresh());

    if (stmt->let.value) {
        RType val_t = cgen_infer_expr(cg, stmt->let.value);
        cgen_emit(cg, ann_t, val_t, stmt->span, "let binding");
    }

    /* Bind: generalize if RHS is a lambda, otherwise monomorphic */
    PolyType poly;
    if (stmt->let.value && stmt->let.value->tag == NODE_LAMBDA) {
        poly = generalize(ann_t, cg->env);
    } else {
        poly = poly_mono(ann_t);
    }

    /* Bind via pattern or name */
    if (stmt->let.pattern) {
        bind_pattern_mono(cg, stmt->let.pattern, poly.mono);
    } else if (stmt->let.name) {
        hmenv_define(cg->env, stmt->let.name, poly);
    }
}

static void infer_fn_decl(CGen *cg, Node *stmt) {
    HMEnv *child_env = hmenv_new(cg->env);
    HMEnv *saved_env = cg->env;
    cg->env = child_env;

    int np = stmt->fn_decl.params.len;
    XsType **param_types = xs_malloc(sizeof(XsType*) * (np ? np : 1));

    for (int i = 0; i < np; i++) {
        Param *p = &stmt->fn_decl.params.items[i];
        if (p->type_ann) {
            RType ann = resolve_type_ann(p->type_ann);
            param_types[i] = rt_to_xstype(ann);
            if (p->name)
                hmenv_define_mono(cg->env, p->name, ann);
        } else {
            TVar *ptv = cgen_fresh();
            param_types[i] = ty_unknown();
            if (p->name)
                hmenv_define_mono(cg->env, p->name, rt_var(ptv));
        }
    }

    /* Return type */
    RType ret_t;
    if (stmt->fn_decl.ret_type)
        ret_t = resolve_type_ann(stmt->fn_decl.ret_type);
    else
        ret_t = rt_var(cgen_fresh());

    /* Set up return type tracking */
    RType saved_ret = cg->current_fn_ret;
    int saved_has = cg->has_fn_ret;
    cg->current_fn_ret = ret_t;
    cg->has_fn_ret = 1;

    /* Infer body */
    if (stmt->fn_decl.body) {
        RType body_t;
        if (stmt->fn_decl.body->tag == NODE_BLOCK)
            body_t = cgen_infer_block(cg, stmt->fn_decl.body);
        else
            body_t = cgen_infer_expr(cg, stmt->fn_decl.body);
        cgen_emit(cg, ret_t, body_t, stmt->span, "fn return");
    }

    cg->current_fn_ret = saved_ret;
    cg->has_fn_ret = saved_has;
    cg->env = saved_env;
    hmenv_free(child_env);

    /* Register fn type in enclosing env */
    XsType *fn_t = ty_fn(param_types, np, rt_to_xstype(ret_t));
    PolyType poly = poly_mono(rt_type(fn_t));
    if (stmt->fn_decl.name)
        hmenv_define(cg->env, stmt->fn_decl.name, poly);
    free(param_types);
}

static void infer_struct_decl(CGen *cg, Node *stmt) {
    if (stmt->struct_decl.name) {
        hmenv_define_mono(cg->env, stmt->struct_decl.name,
                          rt_type(ty_named(stmt->struct_decl.name, NULL, 0)));
        sreg_put(&cg->struct_reg, stmt->struct_decl.name, stmt);
    }
}

static void infer_enum_decl(CGen *cg, Node *stmt) {
    if (stmt->enum_decl.name)
        hmenv_define_mono(cg->env, stmt->enum_decl.name,
                          rt_type(ty_named(stmt->enum_decl.name, NULL, 0)));
    /* Also register each variant as a constructor */
    for (int i = 0; i < stmt->enum_decl.variants.len; i++) {
        EnumVariant *v = &stmt->enum_decl.variants.items[i];
        if (v->name)
            hmenv_define_mono(cg->env, v->name,
                              rt_type(ty_named(stmt->enum_decl.name, NULL, 0)));
    }
}

static void infer_impl_decl(CGen *cg, Node *stmt) {
    HMEnv *child_env = hmenv_new(cg->env);
    HMEnv *saved = cg->env;
    cg->env = child_env;

    /* Define "self" in the impl scope */
    if (stmt->impl_decl.type_name)
        hmenv_define_mono(cg->env, "self",
                          rt_type(ty_named(stmt->impl_decl.type_name, NULL, 0)));

    for (int i = 0; i < stmt->impl_decl.members.len; i++) {
        Node *m = stmt->impl_decl.members.items[i];
        if (m && m->tag == NODE_FN_DECL) {
            infer_fn_decl(cg, m);
            /* Register method in the impl registry */
            if (stmt->impl_decl.type_name && m->fn_decl.name)
                ireg_put(&cg->impl_reg, stmt->impl_decl.type_name,
                          m->fn_decl.name, m);
        }
    }

    cg->env = saved;
    hmenv_free(child_env);
}

static void infer_class_decl(CGen *cg, Node *stmt) {
    if (stmt->class_decl.name)
        hmenv_define_mono(cg->env, stmt->class_decl.name,
                          rt_type(ty_named(stmt->class_decl.name, NULL, 0)));

    HMEnv *child_env = hmenv_new(cg->env);
    HMEnv *saved = cg->env;
    cg->env = child_env;
    hmenv_define_mono(cg->env, "self",
                      rt_type(ty_named(stmt->class_decl.name, NULL, 0)));

    for (int i = 0; i < stmt->class_decl.members.len; i++) {
        Node *m = stmt->class_decl.members.items[i];
        if (m && m->tag == NODE_FN_DECL)
            infer_fn_decl(cg, m);
    }

    cg->env = saved;
    hmenv_free(child_env);
}

static void infer_trait_decl(CGen *cg, Node *stmt) {
    if (stmt->trait_decl.name)
        hmenv_define_mono(cg->env, stmt->trait_decl.name,
                          rt_type(ty_named(stmt->trait_decl.name, NULL, 0)));
}

static void infer_effect_decl(CGen *cg, Node *stmt) {
    if (stmt->effect_decl.name)
        hmenv_define_mono(cg->env, stmt->effect_decl.name,
                          rt_type(ty_named(stmt->effect_decl.name, NULL, 0)));
    for (int i = 0; i < stmt->effect_decl.ops.len; i++) {
        Node *op = stmt->effect_decl.ops.items[i];
        if (op && op->tag == NODE_FN_DECL)
            infer_fn_decl(cg, op);
    }
}

static void cgen_infer_stmt(CGen *cg, Node *stmt) {
    if (!stmt) return;

    switch (stmt->tag) {
    case NODE_LET:
    case NODE_VAR:
        infer_let_stmt(cg, stmt);
        break;

    case NODE_CONST: {
        RType val_t = rt_type(ty_unknown());
        if (stmt->const_.value)
            val_t = cgen_infer_expr(cg, stmt->const_.value);
        if (stmt->const_.type_ann) {
            RType ann_t = resolve_type_ann(stmt->const_.type_ann);
            cgen_emit(cg, ann_t, val_t, stmt->span, "const binding");
            if (stmt->const_.name)
                hmenv_define_mono(cg->env, stmt->const_.name, ann_t);
        } else {
            if (stmt->const_.name)
                hmenv_define_mono(cg->env, stmt->const_.name, val_t);
        }
        break;
    }

    case NODE_EXPR_STMT:
        if (stmt->expr_stmt.expr)
            cgen_infer_expr(cg, stmt->expr_stmt.expr);
        break;

    case NODE_FN_DECL:
        infer_fn_decl(cg, stmt);
        break;
    case NODE_STRUCT_DECL:
        infer_struct_decl(cg, stmt);
        break;
    case NODE_ENUM_DECL:
        infer_enum_decl(cg, stmt);
        break;
    case NODE_CLASS_DECL:
        infer_class_decl(cg, stmt);
        break;
    case NODE_IMPL_DECL:
        infer_impl_decl(cg, stmt);
        break;
    case NODE_TRAIT_DECL:
        infer_trait_decl(cg, stmt);
        break;
    case NODE_EFFECT_DECL:
        infer_effect_decl(cg, stmt);
        break;

    case NODE_TYPE_ALIAS:
        /* Nothing to infer for type aliases at this stage */
        break;

    case NODE_IMPORT:
    case NODE_USE:
        break;

    case NODE_MODULE_DECL: {
        HMEnv *child_env = hmenv_new(cg->env);
        HMEnv *saved = cg->env;
        cg->env = child_env;
        for (int i = 0; i < stmt->module_decl.body.len; i++)
            cgen_infer_stmt(cg, stmt->module_decl.body.items[i]);
        cg->env = saved;
        hmenv_free(child_env);
        break;
    }

    default:
        /* Try as expression */
        cgen_infer_expr(cg, stmt);
        break;
    }
}

/*
 * infer result
 */

#define RESULT_MAP_CAP 1024

typedef struct ResultEntry {
    int   node_id;
    char *type_str;
} ResultEntry;

struct InferResult {
    ResultEntry *entries;
    int          len;
    int          cap;
    ErrorList    errors;
};

static InferResult *result_new(void) {
    InferResult *r = xs_calloc(1, sizeof(InferResult));
    r->cap = RESULT_MAP_CAP;
    r->len = 0;
    r->entries = xs_calloc((size_t)r->cap, sizeof(ResultEntry));
    errlist_init(&r->errors);
    return r;
}

static void result_set(InferResult *r, int node_id, const char *type_str) {
    if (r->len >= r->cap) {
        r->cap *= 2;
        r->entries = xs_realloc(r->entries, (size_t)r->cap * sizeof(ResultEntry));
    }
    r->entries[r->len].node_id  = node_id;
    r->entries[r->len].type_str = xs_strdup(type_str);
    r->len++;
}

/*
 * built-in environment
 */

static void seed_builtins(HMEnv *env) {
    /* print/println: fn(any) -> () */
    {
        XsType *p[1]; p[0] = ty_dyn();
        XsType *fn = ty_fn(p, 1, ty_unit());
        hmenv_define_mono(env, "print",   rt_type(fn));
        hmenv_define_mono(env, "println", rt_type(fn));
        hmenv_define_mono(env, "eprint",  rt_type(fn));
    }
    /* len: fn(any) -> i64 */
    {
        XsType *p[1]; p[0] = ty_dyn();
        hmenv_define_mono(env, "len", rt_type(ty_fn(p, 1, ty_i64())));
    }
    /* range: fn(i64, i64) -> [i64] */
    {
        XsType *p[2]; p[0] = ty_i64(); p[1] = ty_i64();
        hmenv_define_mono(env, "range", rt_type(ty_fn(p, 2, ty_array(ty_i64()))));
    }
    /* clear: fn() -> () */
    hmenv_define_mono(env, "clear", rt_type(ty_fn(NULL, 0, ty_unit())));

    /* Type names */
    hmenv_define_mono(env, "Int",    rt_type(ty_i64()));
    hmenv_define_mono(env, "Float",  rt_type(ty_f64()));
    hmenv_define_mono(env, "String", rt_type(ty_str()));
    hmenv_define_mono(env, "Bool",   rt_type(ty_bool()));
    hmenv_define_mono(env, "Char",   rt_type(ty_char()));

    /* true / false / null */
    hmenv_define_mono(env, "true",  rt_type(ty_bool()));
    hmenv_define_mono(env, "false", rt_type(ty_bool()));
    hmenv_define_mono(env, "null",  rt_type(ty_unknown()));
}

/*
 * 21. Top-Level Driver: infer_types()
 */

InferResult *infer_types(Node *program) {
    InferResult *result = result_new();
    if (!program || program->tag != NODE_PROGRAM) return result;

    /* Build base environment */
    HMEnv *base_env = hmenv_new(NULL);
    seed_builtins(base_env);

    /* Constraint generation pass */
    CGen cg = cgen_new(base_env, &result->errors);

    for (int i = 0; i < program->program.stmts.len; i++)
        cgen_infer_stmt(&cg, program->program.stmts.items[i]);

    /* Constraint solving */
    Substitution subst = subst_new();
    solve_constraints(&cg.constraints, &subst, &result->errors);

    /* Resolve all node types and populate result map */
    for (int i = 0; i < cg.node_types.len; i++) {
        NodeTypeEntry *e = &cg.node_types.entries[i];
        XsType *resolved = subst_apply_deep(&subst, e->rtype);
        char *s = ty_to_str(resolved);
        result_set(result, e->node_id, s);
        free(s);
    }

    /* Cleanup */
    subst_free(&subst);
    cgen_destroy(&cg);
    hmenv_free(base_env);

    return result;
}

const char *infer_type_at(InferResult *r, int node_id) {
    if (!r) return NULL;
    for (int i = 0; i < r->len; i++) {
        if (r->entries[i].node_id == node_id)
            return r->entries[i].type_str;
    }
    return NULL;
}

int infer_error_count(InferResult *r) {
    if (!r) return 0;
    return r->errors.len;
}

const char *infer_error_at(InferResult *r, int idx) {
    if (!r || idx < 0 || idx >= r->errors.len) return NULL;
    return r->errors.errors[idx];
}

void infer_result_free(InferResult *r) {
    if (!r) return;
    for (int i = 0; i < r->len; i++)
        free(r->entries[i].type_str);
    free(r->entries);
    free(r);
}

/*
 * 22. Legacy API (backward compatibility)
 */

#define MAX_BINDINGS 1024

struct InferCtx {
    struct { char *name; XsType *type; } bindings[MAX_BINDINGS];
    int n_bindings;
};

InferCtx *infer_new(void) {
    InferCtx *ctx = xs_calloc(1, sizeof(InferCtx));
    return ctx;
}

void infer_free(InferCtx *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->n_bindings; i++) {
        free(ctx->bindings[i].name);
        if (ctx->bindings[i].type && !ctx->bindings[i].type->is_singleton)
            ty_free(ctx->bindings[i].type);
    }
    free(ctx);
}

static XsType *legacy_lookup(InferCtx *ctx, const char *name) {
    if (!ctx || !name) return NULL;
    for (int i = ctx->n_bindings - 1; i >= 0; i--) {
        if (ctx->bindings[i].name && strcmp(ctx->bindings[i].name, name) == 0)
            return ctx->bindings[i].type;
    }
    return NULL;
}

static void legacy_bind(InferCtx *ctx, const char *name, XsType *type) {
    if (!ctx || !name || ctx->n_bindings >= MAX_BINDINGS) return;
    int idx = ctx->n_bindings++;
    ctx->bindings[idx].name = xs_strdup(name);
    ctx->bindings[idx].type = type;
}

static int is_comparison(const char *op) {
    return (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
            strcmp(op, "<")  == 0 || strcmp(op, ">")  == 0 ||
            strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
            strcmp(op, "&&") == 0 || strcmp(op, "||") == 0);
}

static int is_arithmetic(const char *op) {
    return (strcmp(op, "+")  == 0 || strcmp(op, "-")  == 0 ||
            strcmp(op, "*")  == 0 || strcmp(op, "/")  == 0 ||
            strcmp(op, "%")  == 0 || strcmp(op, "**") == 0);
}

XsType *infer_expr(InferCtx *ctx, Node *expr) {
    if (!ctx || !expr) return NULL;

    switch (expr->tag) {
    case NODE_LIT_INT:    return ty_i64();
    case NODE_LIT_BIGINT: return ty_i64();
    case NODE_LIT_FLOAT:  return ty_f64();
    case NODE_LIT_STRING:
    case NODE_INTERP_STRING: return ty_str();
    case NODE_LIT_BOOL:   return ty_bool();
    case NODE_LIT_NULL:   return ty_unit();
    case NODE_LIT_CHAR:   return ty_char();

    case NODE_LIT_ARRAY: {
        if (expr->lit_array.elems.len > 0 && expr->lit_array.elems.items[0]) {
            XsType *elem_type = infer_expr(ctx, expr->lit_array.elems.items[0]);
            if (elem_type) return ty_array(elem_type);
        }
        return ty_array(ty_unknown());
    }
    case NODE_LIT_TUPLE: {
        int n = expr->lit_array.elems.len;
        if (n == 0) return ty_tuple(NULL, 0);
        XsType **elems = xs_calloc((size_t)n, sizeof(XsType*));
        for (int i = 0; i < n; i++) {
            elems[i] = infer_expr(ctx, expr->lit_array.elems.items[i]);
            if (!elems[i]) elems[i] = ty_unknown();
        }
        XsType *t = ty_tuple(elems, n);
        free(elems);
        return t;
    }
    case NODE_IDENT:
        return legacy_lookup(ctx, expr->ident.name);

    case NODE_BINOP: {
        const char *op = expr->binop.op;
        if (is_comparison(op)) return ty_bool();
        if (is_arithmetic(op)) {
            XsType *left = infer_expr(ctx, expr->binop.left);
            if (left) return left;
            return infer_expr(ctx, expr->binop.right);
        }
        if (strcmp(op, "+") == 0) {
            XsType *left = infer_expr(ctx, expr->binop.left);
            if (left && left->kind == TY_STR) return ty_str();
        }
        return infer_expr(ctx, expr->binop.left);
    }
    case NODE_UNARY: {
        const char *op = expr->unary.op;
        if (strcmp(op, "!") == 0 || strcmp(op, "not") == 0) return ty_bool();
        return infer_expr(ctx, expr->unary.expr);
    }
    case NODE_CALL:  return NULL;
    case NODE_IF: {
        if (expr->if_expr.then) {
            if (expr->if_expr.then->tag == NODE_BLOCK && expr->if_expr.then->block.expr)
                return infer_expr(ctx, expr->if_expr.then->block.expr);
            return infer_expr(ctx, expr->if_expr.then);
        }
        return NULL;
    }
    case NODE_BLOCK: {
        if (expr->block.expr) return infer_expr(ctx, expr->block.expr);
        return ty_unit();
    }
    case NODE_LAMBDA: {
        int np = expr->lambda.params.len;
        XsType **param_types = NULL;
        if (np > 0) {
            param_types = xs_calloc((size_t)np, sizeof(XsType*));
            for (int i = 0; i < np; i++)
                param_types[i] = ty_unknown();
        }
        XsType *ret = NULL;
        if (expr->lambda.body) ret = infer_expr(ctx, expr->lambda.body);
        if (!ret) ret = ty_unknown();
        XsType *t = ty_fn(param_types, np, ret);
        free(param_types);
        return t;
    }
    case NODE_INDEX: {
        XsType *obj_type = infer_expr(ctx, expr->index.obj);
        if (obj_type && obj_type->kind == TY_ARRAY && obj_type->array.inner)
            return obj_type->array.inner;
        return NULL;
    }
    case NODE_RANGE:   return ty_array(ty_i64());
    case NODE_CAST:
        if (expr->cast.type_name) return ty_from_name(expr->cast.type_name);
        return NULL;
    case NODE_LIT_MAP: return ty_named("Map", NULL, 0);

    case NODE_MATCH: {
        if (expr->match.arms.len > 0 && expr->match.arms.items[0].body)
            return infer_expr(ctx, expr->match.arms.items[0].body);
        return NULL;
    }
    case NODE_TRY: {
        if (expr->try_.body) return infer_expr(ctx, expr->try_.body);
        return NULL;
    }
    case NODE_SPAWN: {
        if (expr->spawn_.expr) return infer_expr(ctx, expr->spawn_.expr);
        return NULL;
    }
    case NODE_FIELD: {
        XsType *obj_type = infer_expr(ctx, expr->field.obj);
        (void)obj_type; /* field resolution needs full type info */
        return NULL;
    }
    case NODE_METHOD_CALL: {
        /* Method return type requires full resolution; walk the object */
        infer_expr(ctx, expr->method_call.obj);
        return NULL;
    }
    case NODE_STRUCT_INIT: {
        if (expr->struct_init.path)
            return ty_named(expr->struct_init.path, NULL, 0);
        return NULL;
    }

    default:           return NULL;
    }
}

int infer_check(InferCtx *ctx, Node *expr, XsType *expected) {
    if (!ctx || !expr || !expected) return 0;
    XsType *actual = infer_expr(ctx, expr);
    if (!actual) return 0;
    int result = ty_equal(actual, expected);
    if (expr->tag == NODE_LET || expr->tag == NODE_VAR) {
        if (expr->let.name) {
            XsType *val_type = NULL;
            if (expr->let.value)
                val_type = infer_expr(ctx, expr->let.value);
            if (val_type)
                legacy_bind(ctx, expr->let.name, val_type);
        }
    }
    return result;
}
