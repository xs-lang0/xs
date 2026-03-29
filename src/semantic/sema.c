#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/xs.h"
#include "semantic/sema.h"
#include "semantic/exhaust.h"
#include "semantic/resolve.h"
#include "semantic/symtable.h"
#include "semantic/typecheck.h"

/* growable bind stack */
typedef struct { const char *name; int mutable; } Bind;
static Bind *g_binds = NULL;
static int   g_nbinds = 0;
static int   g_binds_cap = 0;
static void  bind_push(const char *n, int m) {
    if (g_nbinds >= g_binds_cap) {
        g_binds_cap = g_binds_cap ? g_binds_cap * 2 : 64;
        g_binds = realloc(g_binds, g_binds_cap * sizeof(Bind));
    }
    g_binds[g_nbinds].name = n;
    g_binds[g_nbinds++].mutable = m;
}
static int   bind_is_mut(const char *n) {
    for (int i = g_nbinds-1; i >= 0; i--)
        if (g_binds[i].name && strcmp(g_binds[i].name, n) == 0) return g_binds[i].mutable;
    return 1;
}
static int   bind_save(void)      { return g_nbinds; }
static void  bind_restore(int s)  { g_nbinds = s; }

/* growable defined-names set */
static const char **g_defnames = NULL;
static int          g_ndefnames = 0;
static int          g_defnames_cap = 0;
static void defname_add(const char *n) {
    if (g_ndefnames >= g_defnames_cap) {
        g_defnames_cap = g_defnames_cap ? g_defnames_cap * 2 : 32;
        g_defnames = realloc(g_defnames, g_defnames_cap * sizeof(char *));
    }
    g_defnames[g_ndefnames++] = n;
}
static int  defname_has(const char *n) {
    for (int i = 0; i < g_ndefnames; i++)
        if (g_defnames[i] && strcmp(g_defnames[i], n) == 0) return 1;
    return 0;
}

/* growable impl registry */
typedef struct { const char *trait_name; const char *type_name; } ImplRec;
static ImplRec *g_impls = NULL;
static int      g_nimpls = 0;
static int      g_impls_cap = 0;
static void impl_add(const char *tr, const char *ty) {
    if (g_nimpls >= g_impls_cap) {
        g_impls_cap = g_impls_cap ? g_impls_cap * 2 : 32;
        g_impls = realloc(g_impls, g_impls_cap * sizeof(ImplRec));
    }
    g_impls[g_nimpls].trait_name = tr;
    g_impls[g_nimpls++].type_name = ty;
}

/* growable trait registry */
typedef struct {
    const char  *name;
    const char **methods;     int n_methods;
    int         *method_param_counts;
    const char **method_ret_types;
    const char **assoc_types; int n_assoc;
    Node       **method_nodes;
} TraitInfo;
static TraitInfo *g_traits = NULL;
static int        g_ntraits = 0;
static int        g_traits_cap = 0;

static int g_in_pure = 0;
static int g_tc_ran = 0;

/* return type annotation stack for literal return checks */
#define RET_ANN_MAX 64
static TypeExpr *g_ret_ann_stack[RET_ANN_MAX];
static int       g_ret_ann_depth = 0;

static void ret_ann_push(TypeExpr *te) {
    if (g_ret_ann_depth < RET_ANN_MAX) g_ret_ann_stack[g_ret_ann_depth++] = te;
}
static void ret_ann_pop(void) {
    if (g_ret_ann_depth > 0) g_ret_ann_depth--;
}
static TypeExpr *ret_ann_current(void) {
    return g_ret_ann_depth > 0 ? g_ret_ann_stack[g_ret_ann_depth - 1] : NULL;
}

/* check if a literal node obviously mismatches a type annotation */
static const char *literal_type_name(NodeTag tag) {
    switch (tag) {
        case NODE_LIT_INT:    return "int";
        case NODE_LIT_FLOAT:  return "float";
        case NODE_LIT_STRING: return "string";
        case NODE_LIT_BOOL:   return "bool";
        default:              return NULL;
    }
}

static int ann_matches_literal(const char *ann, NodeTag tag) {
    if (!ann) return 1;
    if (tag == NODE_LIT_INT)
        return strcmp(ann,"int")==0 || strcmp(ann,"i32")==0 || strcmp(ann,"i64")==0;
    if (tag == NODE_LIT_FLOAT)
        return strcmp(ann,"float")==0 || strcmp(ann,"f32")==0 || strcmp(ann,"f64")==0;
    if (tag == NODE_LIT_STRING)
        return strcmp(ann,"str")==0 || strcmp(ann,"string")==0;
    if (tag == NODE_LIT_BOOL)
        return strcmp(ann,"bool")==0;
    return 1; /* not a literal we check */
}

static void check_literal_type(SemaCtx *ctx, TypeExpr *te, Node *val) {
    if (g_tc_ran) return; /* full type checker already handled this */
    if (!te || !val) return;
    if (te->kind != TEXPR_NAMED || !te->name) return;
    const char *lit = literal_type_name(val->tag);
    if (!lit) return;
    if (!ann_matches_literal(te->name, val->tag)) {
        Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0010",
            "type mismatch: expected '%s', got %s literal", te->name, lit);
        diag_annotate(d, val->span, 1, "expected '%s'", te->name);
        diag_emit(ctx->diag, d);
    }
}

static void walk(SemaCtx *ctx, Node *n);

static void walk_list(SemaCtx *ctx, NodeList *nl) {
    if (!nl) return;
    for (int i = 0; i < nl->len; i++) walk(ctx, nl->items[i]);
}

static void walk_children(SemaCtx *ctx, Node *n) {
    if (!n) return;
    switch (n->tag) {
        case NODE_PROGRAM:   walk_list(ctx, &n->program.stmts); break;
        case NODE_BLOCK:
            walk_list(ctx, &n->block.stmts);
            if (n->block.expr) walk(ctx, n->block.expr);
            break;
        case NODE_BINOP:
            walk(ctx, n->binop.left); walk(ctx, n->binop.right);
            break;
        case NODE_UNARY:
            walk(ctx, n->unary.expr);
            break;
        case NODE_IF:
            walk(ctx, n->if_expr.cond);
            walk(ctx, n->if_expr.then);
            if (n->if_expr.else_branch) walk(ctx, n->if_expr.else_branch);
            break;
        case NODE_WHILE:
            walk(ctx, n->while_loop.cond); walk(ctx, n->while_loop.body);
            break;
        case NODE_FOR:
            walk(ctx, n->for_loop.iter); walk(ctx, n->for_loop.body);
            break;
        case NODE_RETURN:
            if (n->ret.value) walk(ctx, n->ret.value);
            break;
        case NODE_EXPR_STMT:
            walk(ctx, n->expr_stmt.expr);
            break;
        case NODE_LET: case NODE_VAR:
            if (n->let.value) walk(ctx, n->let.value);
            break;
        case NODE_CONST:
            if (n->const_.value) walk(ctx, n->const_.value);
            break;
        case NODE_CALL:
            walk(ctx, n->call.callee);
            walk_list(ctx, &n->call.args);
            break;
        case NODE_METHOD_CALL:
            walk(ctx, n->method_call.obj);
            walk_list(ctx, &n->method_call.args);
            break;
        case NODE_MATCH:
            walk(ctx, n->match.subject);
            for (int i = 0; i < n->match.arms.len; i++)
                walk(ctx, n->match.arms.items[i].body);
            break;
        case NODE_TAG_DECL:
            if (n->tag_decl.body) walk(ctx, n->tag_decl.body);
            break;
        case NODE_BIND:
            if (n->bind_decl.expr) walk(ctx, n->bind_decl.expr);
            break;
        case NODE_ADAPT_FN:
            for (int i = 0; i < n->adapt_fn.nbranches; i++)
                if (n->adapt_fn.bodies[i]) walk(ctx, n->adapt_fn.bodies[i]);
            break;
        case NODE_INLINE_C:
            break;
        default: break;
    }
}

static void walk(SemaCtx *ctx, Node *n) {
    if (!n) return;
    switch (n->tag) {

    case NODE_LET:
        if (ctx->strict && n->let.name && !n->let.type_ann) {
            Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "S0010",
                "missing type annotation for '%s' in strict mode", n->let.name);
            diag_annotate(d, n->span, 1, "add a type annotation here");
            diag_hint(d, "use 'let %s: <type> = ...'", n->let.name);
            diag_emit(ctx->diag, d);
        }
        check_literal_type(ctx, n->let.type_ann, n->let.value);
        if (n->let.name) bind_push(n->let.name, 0);
        if (n->let.value) walk(ctx, n->let.value);
        break;

    case NODE_VAR:
        if (ctx->strict && n->let.name && !n->let.type_ann) {
            Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "S0010",
                "missing type annotation for '%s' in strict mode", n->let.name);
            diag_annotate(d, n->span, 1, "add a type annotation here");
            diag_hint(d, "use 'var %s: <type> = ...'", n->let.name);
            diag_emit(ctx->diag, d);
        }
        check_literal_type(ctx, n->let.type_ann, n->let.value);
        if (n->let.name) bind_push(n->let.name, 1);
        if (n->let.value) walk(ctx, n->let.value);
        break;

    case NODE_BIND:
        /* treat bind like a mutable var for sema purposes */
        if (n->bind_decl.name) bind_push(n->bind_decl.name, 1);
        if (n->bind_decl.expr) walk(ctx, n->bind_decl.expr);
        break;

    case NODE_ADAPT_FN:
        /* treat adapt fn like a fn_decl for sema purposes */
        if (n->adapt_fn.name) bind_push(n->adapt_fn.name, 0);
        for (int i = 0; i < n->adapt_fn.nbranches; i++)
            if (n->adapt_fn.bodies[i]) walk(ctx, n->adapt_fn.bodies[i]);
        break;

    case NODE_ASSIGN:
        if (n->assign.target->tag == NODE_IDENT) {
            const char *name = n->assign.target->ident.name;
            if (!bind_is_mut(name)) {
                Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0004",
                    "cannot assign to immutable binding '%s'", name);
                diag_annotate(d, n->assign.target->span, 1, "cannot assign to immutable variable '%s'", name);
                diag_hint(d, "consider declaring '%s' with 'var' instead of 'let' to make it mutable", name);
                diag_note(d, "'let' bindings are immutable by default in XS");
                diag_emit(ctx->diag, d);
            }
        }
        walk(ctx, n->assign.target);
        walk(ctx, n->assign.value);
        break;

    case NODE_FN_DECL: {
        if (ctx->strict) {
            for (int i = 0; i < n->fn_decl.params.len; i++) {
                Param *pm = &n->fn_decl.params.items[i];
                if (pm->name && !pm->type_ann) {
                    Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "S0010",
                        "missing type annotation for parameter '%s' in strict mode", pm->name);
                    diag_annotate(d, pm->span, 1, "add a type annotation here");
                    diag_hint(d, "use '%s: <type>'", pm->name);
                    diag_emit(ctx->diag, d);
                }
            }
            if (n->fn_decl.body && !n->fn_decl.ret_type) {
                Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "S0010",
                    "missing return type annotation for function '%s' in strict mode",
                    n->fn_decl.name ? n->fn_decl.name : "<anonymous>");
                diag_annotate(d, n->span, 1, "add a return type annotation");
                diag_hint(d, "use 'fn %s(...) -> <type>'", n->fn_decl.name ? n->fn_decl.name : "<anonymous>");
                diag_emit(ctx->diag, d);
            }
        }
        int saved_pure  = g_in_pure;
        int saved_binds = bind_save();
        if (n->fn_decl.is_pure) g_in_pure++;
        ret_ann_push(n->fn_decl.ret_type);
        for (int i = 0; i < n->fn_decl.params.len; i++) {
            Param *pm = &n->fn_decl.params.items[i];
            if (pm->name) bind_push(pm->name, 0);
        }
        if (n->fn_decl.body) walk(ctx, n->fn_decl.body);
        ret_ann_pop();
        bind_restore(saved_binds);
        g_in_pure = saved_pure;
        break;
    }

    case NODE_CALL:
        if (g_in_pure > 0 && n->call.callee->tag == NODE_IDENT) {
            static const char *impure[] = {
                "println","print","eprintln","eprint",
                "read_file","write_file","read_line","sleep","exit", NULL
            };
            const char *nm = n->call.callee->ident.name;
            for (int i = 0; impure[i]; i++) {
                if (strcmp(nm, impure[i]) == 0) {
                    Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "S0003",
                        "call to impure function '%s' inside @pure function", nm);
                    diag_annotate(d, n->call.callee->span, 1, "impure function '%s' called here", nm);
                    diag_hint(d, "remove the @pure annotation or avoid calling '%s'", nm);
                    diag_note(d, "@pure functions must not perform side effects");
                    diag_emit(ctx->diag, d);
                    break;
                }
            }
        }
        walk(ctx, n->call.callee);
        walk_list(ctx, &n->call.args);
        break;

    case NODE_MATCH: {
        walk(ctx, n->match.subject);
        const char **variants = NULL;
        int n_variants = 0;
        if (n->match.subject && n->match.subject->tag == NODE_IDENT) {
            Symbol *subj_sym = sym_lookup(ctx->st, n->match.subject->ident.name);
            if (subj_sym && subj_sym->decl && subj_sym->decl->tag == NODE_ENUM_DECL) {
                EnumVariantList *vl = &subj_sym->decl->enum_decl.variants;
                n_variants = vl->len;
                if (n_variants > 0) {
                    variants = (const char **)xs_malloc(sizeof(char*) * n_variants);
                    for (int v = 0; v < n_variants; v++)
                        variants[v] = vl->items[v].name;
                }
            }
        }
        /* try first arm's enum pattern as fallback */
        if (!variants && n->match.arms.len > 0) {
            Node *first_pat = n->match.arms.items[0].pattern;
            if (first_pat && first_pat->tag == NODE_PAT_ENUM && first_pat->pat_enum.path) {
                const char *path = first_pat->pat_enum.path;
                const char *sep = strstr(path, "::");
                if (sep) {
                    size_t elen = sep - path;
                    char ename[256];
                    if (elen < sizeof ename) {
                        memcpy(ename, path, elen);
                        ename[elen] = '\0';
                        Symbol *enum_sym = sym_lookup(ctx->st, ename);
                        if (enum_sym && enum_sym->decl &&
                            enum_sym->decl->tag == NODE_ENUM_DECL) {
                            EnumVariantList *vl = &enum_sym->decl->enum_decl.variants;
                            n_variants = vl->len;
                            if (n_variants > 0) {
                                variants = (const char **)xs_malloc(sizeof(char*) * n_variants);
                                for (int v = 0; v < n_variants; v++)
                                    variants[v] = vl->items[v].name;
                            }
                        }
                    }
                }
            }
        }
        char *missing = exhaust_check(n->match.arms.items, n->match.arms.len,
                                      variants, n_variants);
        if (variants) free(variants);
        if (missing) {
            int is_unverifiable = (strstr(missing, "cannot be verified") != NULL);
            Diagnostic *d = diag_new(is_unverifiable ? DIAG_WARNING : DIAG_ERROR,
                DIAG_PHASE_SEMANTIC, "S0001",
                "match is not exhaustive: missing pattern '%s'", missing);
            diag_annotate(d, n->span, 1, "non-exhaustive match");
            diag_hint(d, "add a wildcard '_' pattern to cover remaining cases");
            diag_note(d, "all match expressions must cover every possible value");
            diag_emit(ctx->diag, d);
            free(missing);
        }
        int saved = bind_save();
        for (int i = 0; i < n->match.arms.len; i++) walk(ctx, n->match.arms.items[i].body);
        bind_restore(saved);
        break;
    }

    case NODE_RETURN:
        check_literal_type(ctx, ret_ann_current(), n->ret.value);
        if (n->ret.value) walk(ctx, n->ret.value);
        break;

    case NODE_CONST:
        if (ctx->strict && n->const_.name && !n->const_.type_ann) {
            Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "S0010",
                "missing type annotation for '%s' in strict mode", n->const_.name);
            diag_annotate(d, n->span, 1, "add a type annotation here");
            diag_hint(d, "use 'const %s: <type> = ...'", n->const_.name);
            diag_emit(ctx->diag, d);
        }
        check_literal_type(ctx, n->const_.type_ann, n->const_.value);
        if (n->const_.value) walk(ctx, n->const_.value);
        break;

    default:
        walk_children(ctx, n);
        break;
    }
}

/* collect declarations */
static void collect_decls(SemaCtx *ctx, Node *prog) {
    (void)ctx;
    if (!prog || prog->tag != NODE_PROGRAM) return;
    for (int i = 0; i < prog->program.stmts.len; i++) {
        Node *s = prog->program.stmts.items[i];
        if (!s) continue;
        if (s->tag == NODE_FN_DECL     && s->fn_decl.name)     defname_add(s->fn_decl.name);
        if (s->tag == NODE_TAG_DECL    && s->tag_decl.name)    defname_add(s->tag_decl.name);
        if (s->tag == NODE_STRUCT_DECL  && s->struct_decl.name) defname_add(s->struct_decl.name);
        if (s->tag == NODE_ENUM_DECL    && s->enum_decl.name)   defname_add(s->enum_decl.name);
        if (s->tag == NODE_TRAIT_DECL   && s->trait_decl.name) {
            defname_add(s->trait_decl.name);
            {
                if (g_ntraits >= g_traits_cap) {
                    g_traits_cap = g_traits_cap ? g_traits_cap * 2 : 16;
                    g_traits = realloc(g_traits, g_traits_cap * sizeof(TraitInfo));
                }
                TraitInfo *ti = &g_traits[g_ntraits++];
                ti->name        = s->trait_decl.name;
                ti->methods     = (const char **)s->trait_decl.method_names;
                ti->n_methods   = s->trait_decl.n_methods;
                ti->assoc_types = (const char **)s->trait_decl.assoc_types;
                ti->n_assoc     = s->trait_decl.n_assoc_types;
                ti->method_nodes        = NULL;
                ti->method_param_counts = NULL;
                ti->method_ret_types    = NULL;
                if (s->trait_decl.methods.len > 0) {
                    ti->method_nodes = (Node **)xs_malloc(sizeof(Node*) * ti->n_methods);
                    ti->method_param_counts = (int *)xs_malloc(sizeof(int) * ti->n_methods);
                    ti->method_ret_types = (const char **)xs_malloc(sizeof(char*) * ti->n_methods);
                    for (int m = 0; m < ti->n_methods; m++) {
                        ti->method_param_counts[m] = -1;
                        ti->method_ret_types[m]    = NULL;
                        ti->method_nodes[m]        = NULL;
                    }
                    for (int m = 0; m < s->trait_decl.methods.len && m < ti->n_methods; m++) {
                        Node *mn = s->trait_decl.methods.items[m];
                        if (mn && mn->tag == NODE_FN_DECL) {
                            ti->method_nodes[m]        = mn;
                            ti->method_param_counts[m] = mn->fn_decl.params.len;
                            ti->method_ret_types[m]    = (mn->fn_decl.ret_type &&
                                                          mn->fn_decl.ret_type->name)
                                                         ? mn->fn_decl.ret_type->name : NULL;
                        }
                    }
                }
            }
        }
        if (s->tag == NODE_IMPL_DECL && s->impl_decl.trait_name && s->impl_decl.type_name)
            impl_add(s->impl_decl.trait_name, s->impl_decl.type_name);
    }
}

// orphan rule check
static void check_orphans(SemaCtx *ctx, Node *prog) {
    for (int i = 0; i < prog->program.stmts.len; i++) {
        Node *s = prog->program.stmts.items[i];
        if (!s || s->tag != NODE_IMPL_DECL) continue;
        const char *tr = s->impl_decl.trait_name;
        const char *ty = s->impl_decl.type_name;
        if (!tr || !ty) continue;
        if (!defname_has(tr) && !defname_has(ty)) {
            Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "S0006",
                "orphan impl: neither trait '%s' nor type '%s' is defined in this file", tr, ty);
            diag_annotate(d, s->span, 1, "orphan impl declaration");
            diag_hint(d, "define trait '%s' or type '%s' in this file, or move the impl to the crate that defines them", tr, ty);
            diag_note(d, "the orphan rule requires that either the trait or the type is local");
            diag_emit(ctx->diag, d);
        }
    }
}

/* trait method completeness */
static void check_impls(SemaCtx *ctx, Node *prog) {
    for (int i = 0; i < prog->program.stmts.len; i++) {
        Node *s = prog->program.stmts.items[i];
        if (!s || s->tag != NODE_IMPL_DECL) continue;
        const char *tr = s->impl_decl.trait_name;
        if (!tr) continue;
        TraitInfo *ti = NULL;
        for (int t = 0; t < g_ntraits; t++)
            if (strcmp(g_traits[t].name, tr) == 0) { ti = &g_traits[t]; break; }
        if (!ti) continue;

        const char *impl_m[256]; int nim = 0;
        for (int m = 0; m < s->impl_decl.members.len; m++) {
            Node *mem = s->impl_decl.members.items[m];
            if (mem && mem->tag == NODE_FN_DECL && mem->fn_decl.name && nim < 256)
                impl_m[nim++] = mem->fn_decl.name;
        }
        for (int m = 0; m < ti->n_methods; m++) {
            int found = 0;
            Node *impl_fn = NULL;
            for (int im = 0; im < nim; im++) {
                if (strcmp(impl_m[im], ti->methods[m]) == 0) {
                    found = 1;
                    for (int mm = 0; mm < s->impl_decl.members.len; mm++) {
                        Node *mem = s->impl_decl.members.items[mm];
                        if (mem && mem->tag == NODE_FN_DECL && mem->fn_decl.name
                            && strcmp(mem->fn_decl.name, ti->methods[m]) == 0) {
                            impl_fn = mem;
                            break;
                        }
                    }
                    break;
                }
            }
            if (!found) {
                int has_default = 0;
                if (ti->method_nodes && m < ti->n_methods && ti->method_nodes[m]) {
                    Node *mn = ti->method_nodes[m];
                    if (mn->tag == NODE_FN_DECL && mn->fn_decl.body)
                        has_default = 1;
                }
                if (has_default) continue;
                Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0005",
                    "impl of trait '%s' for '%s' is missing method '%s'",
                    tr, s->impl_decl.type_name ? s->impl_decl.type_name : "?",
                    ti->methods[m]);
                diag_annotate(d, s->span, 1, "incomplete impl");
                diag_hint(d, "add method '%s' to the impl block", ti->methods[m]);
                diag_emit(ctx->diag, d);
                continue;
            }
            if (impl_fn && ti->method_param_counts && ti->method_param_counts[m] >= 0) {
                int trait_pc = ti->method_param_counts[m];
                int impl_pc = impl_fn->fn_decl.params.len;
                if (trait_pc != impl_pc) {
                    Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0005",
                        "method '%s' expects %d params but impl has %d",
                        ti->methods[m], trait_pc, impl_pc);
                    diag_annotate(d, impl_fn->span, 1, "wrong number of parameters");
                    if (ti->method_nodes && ti->method_nodes[m]) {
                        diag_annotate(d, ti->method_nodes[m]->span, 0,
                            "trait requires %d parameters here", trait_pc);
                    }
                    diag_hint(d, "change the parameter list to match the trait signature (%d params)", trait_pc);
                    diag_emit(ctx->diag, d);
                }
            }
            if (impl_fn && ti->method_ret_types && ti->method_ret_types[m]) {
                const char *trait_ret = ti->method_ret_types[m];
                const char *impl_ret = (impl_fn->fn_decl.ret_type &&
                                        impl_fn->fn_decl.ret_type->name)
                                       ? impl_fn->fn_decl.ret_type->name : NULL;
                if (impl_ret && strcmp(trait_ret, impl_ret) != 0) {
                    Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0001",
                        "method '%s' return type mismatch: trait expects '%s' but impl has '%s'",
                        ti->methods[m], trait_ret, impl_ret);
                    diag_annotate(d, impl_fn->span, 1, "return type '%s' does not match trait", impl_ret);
                    if (ti->method_nodes && ti->method_nodes[m]) {
                        diag_annotate(d, ti->method_nodes[m]->span, 0,
                            "trait declares return type '%s' here", trait_ret);
                    }
                    diag_hint(d, "change the return type to '%s'", trait_ret);
                    diag_emit(ctx->diag, d);
                }
            }
        }
    }
}

void sema_init(SemaCtx *ctx, int lenient, int strict) {
    memset(ctx, 0, sizeof *ctx);
    ctx->lenient = lenient;
    ctx->strict = strict;
    ctx->st = symtab_new();
    g_nbinds = 0; g_ndefnames = 0; g_nimpls = 0; g_ntraits = 0; g_in_pure = 0; g_ret_ann_depth = 0; g_tc_ran = 0;
}
void sema_free(SemaCtx *ctx) {
    if (ctx->st) { symtab_free(ctx->st); ctx->st = NULL; }
    memset(ctx, 0, sizeof *ctx);
}
int sema_analyze(SemaCtx *ctx, Node *program, const char *filename) {
    (void)filename;
    g_nbinds = 0;
    g_ndefnames = 0;
    g_nimpls = 0;
    g_ntraits = 0;
    g_in_pure = 0;
    g_ret_ann_depth = 0;
    resolve_program(program, ctx->st, ctx);
    /* skip typecheck if resolve had errors to avoid cascading noise */
    int resolve_errors = ctx->diag ? diag_context_error_count(ctx->diag) : 0;
    g_tc_ran = 0;
    if (resolve_errors == 0) {
        tc_program(program, ctx->st, ctx);
        g_tc_ran = 1;
    }
    collect_decls(ctx, program);
    check_orphans(ctx, program);
    check_impls(ctx, program);
    walk(ctx, program);
    return ctx->diag ? diag_context_error_count(ctx->diag) : 0;
}
