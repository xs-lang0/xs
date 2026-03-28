#include "transpiler/c_gen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>

#include "core/strbuf.h"

/* forward declarations */
static void emit_expr(SB *s, Node *n, int depth);
static void emit_stmt(SB *s, Node *n, int depth);
static void emit_block_body(SB *s, Node *block, int depth);
static void emit_pattern_cond(SB *s, Node *pat, const char *subject, int depth);
static void emit_pattern_bindings(SB *s, Node *pat, const char *subject, int depth);

/* track if we've seen a main function */
static int seen_main = 0;
/* track defers for goto-based cleanup */
static int defer_label_counter = 0;

/* actor tracking for type-aware dispatch */
#define MAX_ACTORS 32
static struct { const char *name; } actors[MAX_ACTORS];
static int n_actors = 0;

#define MAX_ACTOR_VARS 64
static struct { const char *var_name; const char *actor_name; } actor_vars[MAX_ACTOR_VARS];
static int n_actor_vars = 0;

static void register_actor(const char *name) {
    if (n_actors < MAX_ACTORS) actors[n_actors++].name = name;
}

static const char *find_actor(const char *name) {
    for (int i = 0; i < n_actors; i++)
        if (strcmp(actors[i].name, name) == 0) return name;
    return NULL;
}

static void register_actor_var(const char *var, const char *actor) {
    if (n_actor_vars < MAX_ACTOR_VARS) {
        actor_vars[n_actor_vars].var_name = var;
        actor_vars[n_actor_vars].actor_name = actor;
        n_actor_vars++;
    }
}

static const char *lookup_actor_var(const char *var) {
    for (int i = 0; i < n_actor_vars; i++)
        if (strcmp(actor_vars[i].var_name, var) == 0) return actor_vars[i].actor_name;
    return NULL;
}

/* C keyword escaping */
static const char *c_keywords[] = {
    "auto","break","case","char","const","continue","default","do","double",
    "else","enum","extern","float","for","goto","if","int","long","register",
    "return","short","signed","sizeof","static","struct","switch","typedef",
    "union","unsigned","void","volatile","while","inline","restrict",NULL
};
static int is_c_keyword(const char *name) {
    if (!name) return 0;
    for (int i = 0; c_keywords[i]; i++)
        if (strcmp(c_keywords[i], name) == 0) return 1;
    return 0;
}
static void emit_safe_name(SB *s, const char *name) {
    if (is_c_keyword(name)) sb_printf(s, "xs_%s", name);
    else sb_add(s, name);
}

/* struct impl tracking (like actor tracking) */
#define MAX_IMPL_TYPES 32
static struct { const char *type_name; } impl_types[MAX_IMPL_TYPES];
static int n_impl_types = 0;
#define MAX_STRUCT_VARS 64
static struct { const char *var_name; const char *type_name; } struct_vars[MAX_STRUCT_VARS];
static int n_struct_vars = 0;

static void register_impl_type(const char *name) {
    for (int i = 0; i < n_impl_types; i++)
        if (strcmp(impl_types[i].type_name, name) == 0) return;
    if (n_impl_types < MAX_IMPL_TYPES) impl_types[n_impl_types++].type_name = name;
}
static const char *find_impl_type(const char *name) {
    for (int i = 0; i < n_impl_types; i++)
        if (strcmp(impl_types[i].type_name, name) == 0) return name;
    return NULL;
}
static void register_struct_var(const char *var, const char *type) {
    if (n_struct_vars < MAX_STRUCT_VARS) {
        struct_vars[n_struct_vars].var_name = var;
        struct_vars[n_struct_vars].type_name = type;
        n_struct_vars++;
    }
}
static const char *lookup_struct_var(const char *var) {
    for (int i = 0; i < n_struct_vars; i++)
        if (strcmp(struct_vars[i].var_name, var) == 0) return struct_vars[i].type_name;
    return NULL;
}

/* lambda tracking */
#define MAX_LAMBDAS 128
typedef struct {
    int id;
    Node *node;        /* the lambda/fn-expr node */
    int n_params;
    /* simple capture list: names of free variables */
    const char *captures[16];
    int n_captures;
} LambdaInfo;
static LambdaInfo lambdas[MAX_LAMBDAS];
static int n_lambdas = 0;
static int lambda_counter = 0;

static int register_lambda(Node *n) {
    /* check if already registered */
    for (int i = 0; i < n_lambdas; i++)
        if (lambdas[i].node == n) return lambdas[i].id;
    int id = lambda_counter++;
    if (n_lambdas < MAX_LAMBDAS) {
        lambdas[n_lambdas].id = id;
        lambdas[n_lambdas].node = n;
        lambdas[n_lambdas].n_params = n->tag == NODE_LAMBDA ?
            n->lambda.params.len : 0;
        n_lambdas++;
    }
    return id;
}

/* function param count tracking for default param padding */
#define MAX_FN_SIGS 64
static struct { const char *name; int n_params; } fn_sigs[MAX_FN_SIGS];
static int n_fn_sigs = 0;
static void register_fn_sig(const char *name, int n_params) {
    if (n_fn_sigs < MAX_FN_SIGS) {
        fn_sigs[n_fn_sigs].name = name;
        fn_sigs[n_fn_sigs].n_params = n_params;
        n_fn_sigs++;
    }
}
static int lookup_fn_param_count(const char *name) {
    for (int i = 0; i < n_fn_sigs; i++)
        if (strcmp(fn_sigs[i].name, name) == 0) return fn_sigs[i].n_params;
    return -1;
}

/* track if we're inside an impl/actor method (self is a pointer) */
static int in_method_body = 0;

/* track which lambda we're currently emitting (for capture access) */
static LambdaInfo *current_lambda = NULL;

/* track boxed variables in the current enclosing scope */
#define MAX_BOXED 32
static const char *boxed_vars[MAX_BOXED];
static int n_boxed = 0;

static int is_boxed_var(const char *name) {
    for (int i = 0; i < n_boxed; i++)
        if (strcmp(boxed_vars[i], name) == 0) return 1;
    return 0;
}
static void add_boxed_var(const char *name) {
    if (n_boxed < MAX_BOXED) boxed_vars[n_boxed++] = name;
}

/* actor field rewriting: when emitting actor method bodies, identifiers
   that match state fields get rewritten to self->field */
static const char **actor_fields = NULL;
static int n_actor_fields = 0;

static int is_actor_field(const char *name) {
    for (int i = 0; i < n_actor_fields; i++)
        if (strcmp(actor_fields[i], name) == 0) return 1;
    return 0;
}

/* free variable collector for lambda capture analysis.
   Collects identifiers used in the body that are NOT declared locally within it. */
static void collect_local_decls(Node *n, const char **out, int *nout, int max) {
    if (!n || *nout >= max) return;
    switch (n->tag) {
    case NODE_LET: case NODE_VAR:
        if (n->let.name && *nout < max) out[(*nout)++] = n->let.name;
        break;
    case NODE_CONST:
        if (n->const_.name && *nout < max) out[(*nout)++] = n->const_.name;
        break;
    case NODE_FOR:
        /* for loop pattern declares a variable */
        if (n->for_loop.pattern && n->for_loop.pattern->tag == NODE_PAT_IDENT)
            if (*nout < max) out[(*nout)++] = n->for_loop.pattern->pat_ident.name;
        collect_local_decls(n->for_loop.body, out, nout, max);
        break;
    case NODE_BLOCK:
        for (int i = 0; i < n->block.stmts.len; i++)
            collect_local_decls(n->block.stmts.items[i], out, nout, max);
        break;
    case NODE_EXPR_STMT:
        collect_local_decls(n->expr_stmt.expr, out, nout, max);
        break;
    case NODE_IF:
        collect_local_decls(n->if_expr.then, out, nout, max);
        collect_local_decls(n->if_expr.else_branch, out, nout, max);
        break;
    default: break;
    }
}

static void collect_idents(Node *n, const char **out, int *nout, int max) {
    if (!n || *nout >= max) return;
    if (n->tag == NODE_IDENT) {
        for (int i = 0; i < *nout; i++)
            if (strcmp(out[i], n->ident.name) == 0) return;
        out[(*nout)++] = n->ident.name;
        return;
    }
    switch (n->tag) {
    case NODE_BINOP: collect_idents(n->binop.left, out, nout, max);
                     collect_idents(n->binop.right, out, nout, max); break;
    case NODE_UNARY: collect_idents(n->unary.expr, out, nout, max); break;
    case NODE_CALL: collect_idents(n->call.callee, out, nout, max);
                    for (int i=0;i<n->call.args.len;i++) collect_idents(n->call.args.items[i], out, nout, max); break;
    case NODE_METHOD_CALL: collect_idents(n->method_call.obj, out, nout, max);
                           for (int i=0;i<n->method_call.args.len;i++) collect_idents(n->method_call.args.items[i], out, nout, max); break;
    case NODE_INDEX: collect_idents(n->index.obj, out, nout, max);
                     collect_idents(n->index.index, out, nout, max); break;
    case NODE_FIELD: collect_idents(n->field.obj, out, nout, max); break;
    case NODE_ASSIGN: collect_idents(n->assign.target, out, nout, max);
                      collect_idents(n->assign.value, out, nout, max); break;
    case NODE_RETURN: collect_idents(n->ret.value, out, nout, max); break;
    case NODE_IF: collect_idents(n->if_expr.cond, out, nout, max);
                  collect_idents(n->if_expr.then, out, nout, max);
                  collect_idents(n->if_expr.else_branch, out, nout, max); break;
    case NODE_BLOCK: for (int i=0;i<n->block.stmts.len;i++) collect_idents(n->block.stmts.items[i], out, nout, max);
                     collect_idents(n->block.expr, out, nout, max); break;
    case NODE_EXPR_STMT: collect_idents(n->expr_stmt.expr, out, nout, max); break;
    case NODE_LET: case NODE_VAR: collect_idents(n->let.value, out, nout, max); break;
    default: break;
    }
}

/* recursive lambda scanner */
static void scan_lambdas(Node *n) {
    if (!n) return;
    if (n->tag == NODE_LAMBDA) {
        int lid = register_lambda(n);
        /* find captures: idents used in body minus params and local declarations */
        const char *all_idents[64];
        int n_all = 0;
        collect_idents(n->lambda.body, all_idents, &n_all, 64);

        /* collect locally declared names inside the lambda body */
        const char *local_decls[64];
        int n_locals = 0;
        collect_local_decls(n->lambda.body, local_decls, &n_locals, 64);

        static const char *skip_names[] = {
            "println","print","str","len","type","assert","assert_eq",
            "int","float","true","false","null","sqrt","abs","not",
            "channel","range","map","filter","reduce","sort","Err","Ok",
            "assert_eq","panic","input","spawn","await",NULL
        };

        LambdaInfo *li = NULL;
        for (int i = 0; i < n_lambdas; i++)
            if (lambdas[i].id == lid) { li = &lambdas[i]; break; }
        if (li) {
            li->n_captures = 0;
            for (int i = 0; i < n_all && li->n_captures < 16; i++) {
                const char *name = all_idents[i];
                /* skip params */
                int skip = 0;
                for (int p = 0; p < n->lambda.params.len && !skip; p++)
                    if (n->lambda.params.items[p].name &&
                        strcmp(n->lambda.params.items[p].name, name) == 0) skip = 1;
                /* skip local declarations */
                for (int d = 0; d < n_locals && !skip; d++)
                    if (strcmp(local_decls[d], name) == 0) skip = 1;
                /* skip known builtins/globals */
                for (int k = 0; skip_names[k] && !skip; k++)
                    if (strcmp(skip_names[k], name) == 0) skip = 1;
                /* skip known functions */
                if (!skip && lookup_fn_param_count(name) >= 0) skip = 1;
                if (!skip) li->captures[li->n_captures++] = name;
            }
        }
        /* scan body too for nested lambdas */
        if (n->lambda.body) scan_lambdas(n->lambda.body);
        return;
    }
    /* walk children looking for lambdas */
    switch (n->tag) {
    case NODE_PROGRAM: for (int i=0;i<n->program.stmts.len;i++) scan_lambdas(n->program.stmts.items[i]); break;
    case NODE_BLOCK: for (int i=0;i<n->block.stmts.len;i++) scan_lambdas(n->block.stmts.items[i]);
                     if (n->block.expr) scan_lambdas(n->block.expr); break;
    case NODE_BINOP: scan_lambdas(n->binop.left); scan_lambdas(n->binop.right); break;
    case NODE_UNARY: scan_lambdas(n->unary.expr); break;
    case NODE_CALL: scan_lambdas(n->call.callee);
                    for (int i=0;i<n->call.args.len;i++) scan_lambdas(n->call.args.items[i]); break;
    case NODE_METHOD_CALL: scan_lambdas(n->method_call.obj);
                           for (int i=0;i<n->method_call.args.len;i++) scan_lambdas(n->method_call.args.items[i]); break;
    case NODE_INDEX: scan_lambdas(n->index.obj); scan_lambdas(n->index.index); break;
    case NODE_FIELD: scan_lambdas(n->field.obj); break;
    case NODE_ASSIGN: scan_lambdas(n->assign.target); scan_lambdas(n->assign.value); break;
    case NODE_IF: scan_lambdas(n->if_expr.cond); scan_lambdas(n->if_expr.then);
                  scan_lambdas(n->if_expr.else_branch); break;
    case NODE_FOR: scan_lambdas(n->for_loop.iter); scan_lambdas(n->for_loop.body); break;
    case NODE_WHILE: scan_lambdas(n->while_loop.cond); scan_lambdas(n->while_loop.body); break;
    case NODE_RETURN: scan_lambdas(n->ret.value); break;
    case NODE_LET: case NODE_VAR: scan_lambdas(n->let.value); break;
    case NODE_CONST: scan_lambdas(n->const_.value); break;
    case NODE_FN_DECL: scan_lambdas(n->fn_decl.body); break;
    case NODE_EXPR_STMT: scan_lambdas(n->expr_stmt.expr); break;
    case NODE_TRY: scan_lambdas(n->try_.body); scan_lambdas(n->try_.finally_block); break;
    case NODE_THROW: scan_lambdas(n->throw_.value); break;
    case NODE_MATCH: scan_lambdas(n->match.subject);
                     for (int i=0;i<n->match.arms.len;i++) {
                         scan_lambdas(n->match.arms.items[i].guard);
                         scan_lambdas(n->match.arms.items[i].body);
                     } break;
    case NODE_LIT_ARRAY: case NODE_LIT_TUPLE:
        for (int i=0;i<n->lit_array.elems.len;i++) scan_lambdas(n->lit_array.elems.items[i]); break;
    case NODE_LIT_MAP:
        for (int i=0;i<n->lit_map.vals.len;i++) scan_lambdas(n->lit_map.vals.items[i]); break;
    case NODE_IMPL_DECL:
        for (int i=0;i<n->impl_decl.members.len;i++) scan_lambdas(n->impl_decl.members.items[i]); break;
    case NODE_ACTOR_DECL:
        for (int i=0;i<n->actor_decl.methods.len;i++) scan_lambdas(n->actor_decl.methods.items[i]); break;
    case NODE_NURSERY: scan_lambdas(n->nursery_.body); break;
    case NODE_SPAWN: scan_lambdas(n->spawn_.expr); break;
    case NODE_AWAIT: scan_lambdas(n->await_.expr); break;
    case NODE_SEND_EXPR: scan_lambdas(n->send_expr.target); scan_lambdas(n->send_expr.message); break;
    case NODE_RANGE: scan_lambdas(n->range.start); scan_lambdas(n->range.end); break;
    case NODE_LIST_COMP: scan_lambdas(n->list_comp.element);
        for (int i=0;i<n->list_comp.clause_iters.len;i++) scan_lambdas(n->list_comp.clause_iters.items[i]);
        for (int i=0;i<n->list_comp.clause_conds.len;i++) scan_lambdas(n->list_comp.clause_conds.items[i]); break;
    default: break;
    }
}

/* pre-scan to collect actor declarations and actor variable bindings */
static void prescan_stmts(Node *program) {
    if (!program || program->tag != NODE_PROGRAM) return;
    for (int i = 0; i < program->program.stmts.len; i++) {
        Node *st = program->program.stmts.items[i];
        if (!st) continue;
        /* register actor declarations */
        if (st->tag == NODE_EXPR_STMT && st->expr_stmt.expr &&
            st->expr_stmt.expr->tag == NODE_ACTOR_DECL) {
            Node *ad = st->expr_stmt.expr;
            if (ad->actor_decl.name) register_actor(ad->actor_decl.name);
        }
        if (st->tag == NODE_ACTOR_DECL && st->actor_decl.name)
            register_actor(st->actor_decl.name);
        /* register let x = spawn ActorName */
        if ((st->tag == NODE_LET || st->tag == NODE_VAR) && st->let.name && st->let.value) {
            Node *val = st->let.value;
            if (val->tag == NODE_SPAWN && val->spawn_.expr &&
                val->spawn_.expr->tag == NODE_IDENT) {
                const char *aname = val->spawn_.expr->ident.name;
                if (find_actor(aname))
                    register_actor_var(st->let.name, aname);
            }
            /* register let x = StructName { ... } */
            if (val->tag == NODE_STRUCT_INIT && val->struct_init.path) {
                const char *sname = val->struct_init.path;
                if (find_impl_type(sname))
                    register_struct_var(st->let.name, sname);
            }
            /* register let x = y.method(...) where y is a known struct var */
            if (val->tag == NODE_METHOD_CALL && val->method_call.obj &&
                val->method_call.obj->tag == NODE_IDENT) {
                const char *otype = lookup_struct_var(val->method_call.obj->ident.name);
                if (otype)
                    register_struct_var(st->let.name, otype);
            }
        }
        /* register impl declarations */
        if (st->tag == NODE_IMPL_DECL && st->impl_decl.type_name)
            register_impl_type(st->impl_decl.type_name);
        /* register function signatures for default param padding */
        if (st->tag == NODE_FN_DECL && st->fn_decl.name)
            register_fn_sig(st->fn_decl.name, st->fn_decl.params.len);
    }
}

/* helpers */
static int is_callee_name(Node *callee, const char *name) {
    return callee && callee->tag == NODE_IDENT && strcmp(callee->ident.name, name) == 0;
}

static int is_main_fn(Node *n) {
    return n->tag == NODE_FN_DECL && n->fn_decl.name && strcmp(n->fn_decl.name, "main") == 0;
}

static void emit_params_c(SB *s, ParamList *pl) {
    sb_addc(s, '(');
    if (pl->len == 0) {
        sb_add(s, "void");
    } else {
        for (int i = 0; i < pl->len; i++) {
            if (i) sb_add(s, ", ");
            Param *p = &pl->items[i];
            sb_add(s, "xs_val ");
            if (p->name) sb_add(s, p->name);
            else sb_add(s, "_");
        }
    }
    sb_addc(s, ')');
}

/* collect defer bodies from a block */
static int block_has_defers(Node *block) {
    if (!block || block->tag != NODE_BLOCK) return 0;
    for (int i = 0; i < block->block.stmts.len; i++) {
        if (block->block.stmts.items[i] && block->block.stmts.items[i]->tag == NODE_DEFER)
            return 1;
    }
    return 0;
}

static void emit_deferred_cleanup(SB *s, Node *block, int depth) {
    if (!block || block->tag != NODE_BLOCK) return;
    /* Emit deferred bodies in reverse order (LIFO) */
    for (int i = block->block.stmts.len - 1; i >= 0; i--) {
        Node *st = block->block.stmts.items[i];
        if (st && st->tag == NODE_DEFER && st->defer_.body) {
            sb_indent(s, depth);
            emit_expr(s, st->defer_.body, depth);
            sb_add(s, ";\n");
        }
    }
}

/* expression emitter */
static void emit_expr(SB *s, Node *n, int depth) {
    if (!n) { sb_add(s, "XS_NULL"); return; }
    switch (n->tag) {
    case NODE_LIT_INT:
        sb_printf(s, "XS_INT(%" PRId64 ")", n->lit_int.ival);
        break;
    case NODE_LIT_BIGINT:
        sb_printf(s, "XS_INT(%s)", n->lit_bigint.bigint_str);
        break;
    case NODE_LIT_FLOAT:
        sb_printf(s, "XS_FLOAT(%g)", n->lit_float.fval);
        break;
    case NODE_LIT_STRING:
        sb_add(s, "XS_STR(\"");
        if (n->lit_string.sval) {
            for (const char *p = n->lit_string.sval; *p; p++) {
                if (*p == '"') sb_add(s, "\\\"");
                else if (*p == '\\') sb_add(s, "\\\\");
                else if (*p == '\n') sb_add(s, "\\n");
                else if (*p == '\r') sb_add(s, "\\r");
                else if (*p == '\t') sb_add(s, "\\t");
                else sb_addc(s, *p);
            }
        }
        sb_add(s, "\")");
        break;
    case NODE_INTERP_STRING: {
        /* String interpolation -> xs_sprintf with format + args */
        sb_add(s, "xs_sprintf(");
        /* Build format string from parts */
        NodeList *parts = &n->lit_string.parts;
        sb_addc(s, '"');
        for (int i = 0; i < parts->len; i++) {
            Node *part = parts->items[i];
            if (part->tag == NODE_LIT_STRING && part->lit_string.sval) {
                for (const char *p = part->lit_string.sval; *p; p++) {
                    if (*p == '"') sb_add(s, "\\\"");
                    else if (*p == '\\') sb_add(s, "\\\\");
                    else if (*p == '\n') sb_add(s, "\\n");
                    else if (*p == '%') sb_add(s, "%%");
                    else sb_addc(s, *p);
                }
            } else {
                sb_add(s, "%s");
            }
        }
        sb_addc(s, '"');
        /* Now emit the expression arguments */
        for (int i = 0; i < parts->len; i++) {
            Node *part = parts->items[i];
            if (part->tag != NODE_LIT_STRING) {
                sb_add(s, ", xs_to_str(");
                emit_expr(s, part, depth);
                sb_addc(s, ')');
            }
        }
        sb_addc(s, ')');
        break;
    }
    case NODE_LIT_BOOL:
        sb_printf(s, "XS_BOOL(%d)", n->lit_bool.bval ? 1 : 0);
        break;
    case NODE_LIT_NULL:
        sb_add(s, "XS_NULL");
        break;
    case NODE_LIT_CHAR:
        sb_printf(s, "XS_INT('%c')", n->lit_char.cval);
        break;
    case NODE_LIT_ARRAY:
    case NODE_LIT_TUPLE: {
        /* Emit as xs_array_new + pushes */
        sb_printf(s, "xs_array(%d", n->lit_array.elems.len);
        for (int i = 0; i < n->lit_array.elems.len; i++) {
            sb_add(s, ", ");
            emit_expr(s, n->lit_array.elems.items[i], depth);
        }
        sb_addc(s, ')');
        break;
    }
    case NODE_LIT_MAP: {
        sb_printf(s, "xs_map(%d", n->lit_map.keys.len);
        for (int i = 0; i < n->lit_map.keys.len; i++) {
            sb_add(s, ", ");
            /* map keys: if ident, emit as string */
            Node *mk = n->lit_map.keys.items[i];
            if (mk && mk->tag == NODE_IDENT)
                sb_printf(s, "XS_STR(\"%s\")", mk->ident.name);
            else
                emit_expr(s, mk, depth);
            sb_add(s, ", ");
            emit_expr(s, n->lit_map.vals.items[i], depth);
        }
        sb_addc(s, ')');
        break;
    }
    case NODE_IDENT:
        if (n_actor_fields > 0 && is_actor_field(n->ident.name))
            sb_printf(s, "self->%s", n->ident.name);
        else if (current_lambda) {
            /* inside a lambda: check if capture */
            int cap_idx = -1;
            for (int ci = 0; ci < current_lambda->n_captures; ci++)
                if (strcmp(current_lambda->captures[ci], n->ident.name) == 0)
                    { cap_idx = ci; break; }
            if (cap_idx >= 0)
                sb_printf(s, "(*((xs_val**)__env)[%d])", cap_idx);
            else
                emit_safe_name(s, n->ident.name);
        } else if (is_boxed_var(n->ident.name)) {
            /* in enclosing scope: access through box */
            sb_printf(s, "(*__box_%s)", n->ident.name);
        } else
            emit_safe_name(s, n->ident.name);
        break;
    case NODE_BINOP: {
        const char *op = n->binop.op;
        if (strcmp(op, "+") == 0) {
            sb_add(s, "xs_add(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            emit_expr(s, n->binop.right, depth);
            sb_addc(s, ')');
        } else if (strcmp(op, "-") == 0) {
            sb_add(s, "xs_sub(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            emit_expr(s, n->binop.right, depth);
            sb_addc(s, ')');
        } else if (strcmp(op, "*") == 0) {
            sb_add(s, "xs_mul(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            emit_expr(s, n->binop.right, depth);
            sb_addc(s, ')');
        } else if (strcmp(op, "/") == 0) {
            sb_add(s, "xs_div(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            emit_expr(s, n->binop.right, depth);
            sb_addc(s, ')');
        } else if (strcmp(op, "%") == 0) {
            sb_add(s, "xs_mod(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            emit_expr(s, n->binop.right, depth);
            sb_addc(s, ')');
        } else if (strcmp(op, "//") == 0) {
            sb_add(s, "xs_idiv(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            emit_expr(s, n->binop.right, depth);
            sb_addc(s, ')');
        } else if (strcmp(op, "**") == 0) {
            sb_add(s, "XS_FLOAT(pow((double)(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ").i, (double)(");
            emit_expr(s, n->binop.right, depth);
            sb_add(s, ").i))");
        } else if (strcmp(op, "++") == 0) {
            sb_add(s, "xs_strcat(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            emit_expr(s, n->binop.right, depth);
            sb_addc(s, ')');
        } else if (strcmp(op, "==") == 0) {
            sb_add(s, "XS_BOOL(xs_eq(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            emit_expr(s, n->binop.right, depth);
            sb_add(s, "))");
        } else if (strcmp(op, "!=") == 0) {
            sb_add(s, "XS_BOOL(!xs_eq(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            emit_expr(s, n->binop.right, depth);
            sb_add(s, "))");
        } else if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                   strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {
            sb_add(s, "XS_BOOL(xs_cmp(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            emit_expr(s, n->binop.right, depth);
            sb_add(s, ") ");
            sb_add(s, op);
            sb_add(s, " 0)");
        } else if (strcmp(op, "and") == 0) {
            sb_add(s, "XS_BOOL(xs_truthy(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ") && xs_truthy(");
            emit_expr(s, n->binop.right, depth);
            sb_add(s, "))");
        } else if (strcmp(op, "or") == 0) {
            sb_add(s, "XS_BOOL(xs_truthy(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ") || xs_truthy(");
            emit_expr(s, n->binop.right, depth);
            sb_add(s, "))");
        } else if (strcmp(op, "&") == 0 || strcmp(op, "|") == 0 ||
                   strcmp(op, "^") == 0 || strcmp(op, "<<") == 0 ||
                   strcmp(op, ">>") == 0) {
            sb_add(s, "XS_INT((");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ").i ");
            sb_add(s, op);
            sb_add(s, " (");
            emit_expr(s, n->binop.right, depth);
            sb_add(s, ").i)");
        } else if (strcmp(op, "is") == 0) {
            /* type check: val is int/str/bool/float/null/array/map */
            sb_add(s, "xs_is_type(");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ", ");
            if (n->binop.right && n->binop.right->tag == NODE_LIT_STRING &&
                n->binop.right->lit_string.sval)
                sb_printf(s, "\"%s\"", n->binop.right->lit_string.sval);
            else if (n->binop.right && n->binop.right->tag == NODE_IDENT)
                sb_printf(s, "\"%s\"", n->binop.right->ident.name);
            else
                sb_add(s, "\"?\"");
            sb_addc(s, ')');
        } else if (strcmp(op, "??") == 0) {
            /* null coalescing: if left is null, use right */
            sb_add(s, "((");
            emit_expr(s, n->binop.left, depth);
            sb_add(s, ").tag == 4 ? ");
            emit_expr(s, n->binop.right, depth);
            sb_add(s, " : ");
            emit_expr(s, n->binop.left, depth);
            sb_addc(s, ')');
        } else {
            sb_add(s, "/* binop ");
            sb_add(s, op);
            sb_add(s, " */ XS_NULL");
        }
        break;
    }
    case NODE_UNARY: {
        const char *op = n->unary.op;
        if (strcmp(op, "-") == 0) {
            sb_add(s, "xs_neg(");
            emit_expr(s, n->unary.expr, depth);
            sb_addc(s, ')');
        } else if (strcmp(op, "not") == 0 || strcmp(op, "!") == 0) {
            sb_add(s, "XS_BOOL(!xs_truthy(");
            emit_expr(s, n->unary.expr, depth);
            sb_add(s, "))");
        } else if (strcmp(op, "~") == 0) {
            sb_add(s, "XS_INT(~(");
            emit_expr(s, n->unary.expr, depth);
            sb_add(s, ").i)");
        } else {
            sb_add(s, "/* unary ");
            sb_add(s, op);
            sb_add(s, " */ ");
            emit_expr(s, n->unary.expr, depth);
        }
        break;
    }
    case NODE_CALL: {
        if (is_callee_name(n->call.callee, "println")) {
            sb_add(s, "xs_println(");
            if (n->call.args.len > 0)
                emit_expr(s, n->call.args.items[0], depth);
            else
                sb_add(s, "XS_NULL");
            sb_addc(s, ')');
        } else if (is_callee_name(n->call.callee, "print")) {
            sb_add(s, "xs_print(");
            if (n->call.args.len > 0)
                emit_expr(s, n->call.args.items[0], depth);
            else
                sb_add(s, "XS_NULL");
            sb_addc(s, ')');
        } else if (n->call.callee && n->call.callee->tag == NODE_SCOPE &&
                   n->call.callee->scope.nparts == 2) {
            /* enum constructor call: Shape::Circle(5) -> map */
            sb_printf(s, "xs_map(%d, XS_STR(\"_type\"), XS_STR(\"%s\"), XS_STR(\"_variant\"), XS_STR(\"%s\")",
                      2 + n->call.args.len,
                      n->call.callee->scope.parts[0],
                      n->call.callee->scope.parts[1]);
            for (int i = 0; i < n->call.args.len; i++) {
                sb_printf(s, ", XS_STR(\"%d\"), ", i);
                emit_expr(s, n->call.args.items[i], depth);
            }
            sb_addc(s, ')');
        } else if (is_callee_name(n->call.callee, "sqrt")) {
            sb_add(s, "XS_FLOAT(sqrt(xs_to_f64(");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_INT(0)");
            sb_add(s, ")))");
        } else if (is_callee_name(n->call.callee, "abs")) {
            sb_add(s, "XS_FLOAT(fabs(xs_to_f64(");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_INT(0)");
            sb_add(s, ")))");
        } else if (is_callee_name(n->call.callee, "int")) {
            sb_add(s, "XS_INT((int64_t)xs_to_f64(");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_INT(0)");
            sb_add(s, "))");
        } else if (is_callee_name(n->call.callee, "float")) {
            sb_add(s, "XS_FLOAT(xs_to_f64(");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_INT(0)");
            sb_add(s, "))");
        } else if (is_callee_name(n->call.callee, "len")) {
            sb_add(s, "xs_len(");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_NULL");
            sb_addc(s, ')');
        } else if (is_callee_name(n->call.callee, "type")) {
            sb_add(s, "xs_type(");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_NULL");
            sb_addc(s, ')');
        } else if (is_callee_name(n->call.callee, "channel")) {
            sb_add(s, "xs_channel_new(");
            if (n->call.args.len > 0) {
                sb_add(s, "(");
                emit_expr(s, n->call.args.items[0], depth);
                sb_add(s, ").i");
            } else {
                sb_add(s, "0");
            }
            sb_addc(s, ')');
        } else if (is_callee_name(n->call.callee, "assert_eq")) {
            sb_add(s, "xs_assert_eq(");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_NULL");
            sb_add(s, ", ");
            if (n->call.args.len > 1) emit_expr(s, n->call.args.items[1], depth);
            else sb_add(s, "XS_NULL");
            sb_addc(s, ')');
        } else if (is_callee_name(n->call.callee, "Err")) {
            sb_add(s, "xs_map(2, XS_STR(\"tag\"), XS_STR(\"Err\"), XS_STR(\"value\"), ");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_NULL");
            sb_addc(s, ')');
        } else if (is_callee_name(n->call.callee, "Ok")) {
            sb_add(s, "xs_map(2, XS_STR(\"tag\"), XS_STR(\"Ok\"), XS_STR(\"value\"), ");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_NULL");
            sb_addc(s, ')');
        } else if (is_callee_name(n->call.callee, "str")) {
            sb_add(s, "XS_STR(strdup(xs_to_str(");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_NULL");
            sb_add(s, ")))");
        } else if (is_callee_name(n->call.callee, "assert")) {
            sb_add(s, "xs_assert(");
            if (n->call.args.len > 0) emit_expr(s, n->call.args.items[0], depth);
            else sb_add(s, "XS_NULL");
            sb_add(s, ", ");
            if (n->call.args.len > 1) emit_expr(s, n->call.args.items[1], depth);
            else sb_add(s, "XS_STR(\"assertion failed\")");
            sb_addc(s, ')');
        } else {
            /* check if callee might be a closure (variable holding fn) */
            int might_be_closure = 0;
            if (n->call.callee && n->call.callee->tag == NODE_INDEX) {
                /* e.g. counter["inc"](): indexing into map, likely returns closure */
                might_be_closure = 1;
            }
            /* variable calls might be closures if the var was assigned from a function
               that returns closures */
            if (n->call.callee && n->call.callee->tag == NODE_IDENT &&
                lookup_fn_param_count(n->call.callee->ident.name) < 0 &&
                !is_c_keyword(n->call.callee->ident.name)) {
                /* not a known function: might be a closure variable */
                might_be_closure = 1;
            }
            if (might_be_closure) {
                sb_add(s, "xs_call(");
                emit_expr(s, n->call.callee, depth);
                sb_add(s, ", (xs_val[]){");
                for (int i = 0; i < n->call.args.len; i++) {
                    if (i) sb_add(s, ", ");
                    emit_expr(s, n->call.args.items[i], depth);
                }
                sb_printf(s, "}, %d)", n->call.args.len);
            } else {
                emit_expr(s, n->call.callee, depth);
                sb_addc(s, '(');
                /* determine expected param count for padding */
                int expected = -1;
                if (n->call.callee && n->call.callee->tag == NODE_IDENT)
                    expected = lookup_fn_param_count(n->call.callee->ident.name);
                for (int i = 0; i < n->call.args.len; i++) {
                    if (i) sb_add(s, ", ");
                    emit_expr(s, n->call.args.items[i], depth);
                }
                /* pad missing args with XS_NULL for default params */
                if (expected > n->call.args.len) {
                    for (int i = n->call.args.len; i < expected; i++) {
                        if (i) sb_add(s, ", ");
                        sb_add(s, "(xs_val){.tag=4}");
                    }
                }
                sb_addc(s, ')');
            }
        }
        break;
    }
    case NODE_METHOD_CALL: {
        const char *meth = n->method_call.method;
        /* check if receiver is a known actor variable */
        const char *actor_type = NULL;
        if (n->method_call.obj && n->method_call.obj->tag == NODE_IDENT)
            actor_type = lookup_actor_var(n->method_call.obj->ident.name);
        if (actor_type) {
            /* actor method dispatch */
            sb_printf(s, "%s_%s(&%s_state", actor_type, meth,
                      n->method_call.obj->ident.name);
            for (int i = 0; i < n->method_call.args.len; i++) {
                sb_add(s, ", ");
                emit_expr(s, n->method_call.args.items[i], depth);
            }
            sb_addc(s, ')');
        } else if (strcmp(meth, "send") == 0) {
            sb_add(s, "xs_channel_send(");
            emit_expr(s, n->method_call.obj, depth);
            sb_add(s, ", ");
            if (n->method_call.args.len > 0) emit_expr(s, n->method_call.args.items[0], depth);
            else sb_add(s, "XS_NULL");
            sb_addc(s, ')');
        } else if (strcmp(meth, "recv") == 0) {
            sb_add(s, "xs_channel_recv(");
            emit_expr(s, n->method_call.obj, depth);
            sb_addc(s, ')');
        } else if (strcmp(meth, "len") == 0) {
            sb_add(s, "xs_len(");
            emit_expr(s, n->method_call.obj, depth);
            sb_addc(s, ')');
        } else if (strcmp(meth, "is_empty") == 0) {
            sb_add(s, "xs_channel_is_empty(");
            emit_expr(s, n->method_call.obj, depth);
            sb_addc(s, ')');
        } else if (strcmp(meth, "is_full") == 0) {
            sb_add(s, "xs_channel_is_full(");
            emit_expr(s, n->method_call.obj, depth);
            sb_addc(s, ')');
        } else if (strcmp(meth, "push") == 0) {
            sb_add(s, "xs_arr_push(");
            emit_expr(s, n->method_call.obj, depth);
            sb_add(s, ", ");
            if (n->method_call.args.len > 0) emit_expr(s, n->method_call.args.items[0], depth);
            else sb_add(s, "XS_NULL");
            sb_addc(s, ')');
        } else if (strcmp(meth, "sort") == 0) {
            sb_add(s, "xs_arr_sort(");
            emit_expr(s, n->method_call.obj, depth);
            sb_addc(s, ')');
        } else if (strcmp(meth, "has") == 0) {
            sb_add(s, "xs_map_has(");
            emit_expr(s, n->method_call.obj, depth);
            sb_add(s, ", ");
            if (n->method_call.args.len > 0) emit_expr(s, n->method_call.args.items[0], depth);
            else sb_add(s, "XS_NULL");
            sb_addc(s, ')');
        } else if (strcmp(meth, "split") == 0) {
            sb_add(s, "xs_str_split(");
            emit_expr(s, n->method_call.obj, depth);
            sb_add(s, ", ");
            if (n->method_call.args.len > 0) emit_expr(s, n->method_call.args.items[0], depth);
            else sb_add(s, "XS_STR(\" \")");
            sb_addc(s, ')');
        } else if (strcmp(meth, "upper") == 0) {
            sb_add(s, "xs_str_upper(");
            emit_expr(s, n->method_call.obj, depth);
            sb_addc(s, ')');
        } else if (strcmp(meth, "lower") == 0) {
            sb_add(s, "xs_str_lower(");
            emit_expr(s, n->method_call.obj, depth);
            sb_addc(s, ')');
        } else if (strcmp(meth, "join") == 0) {
            sb_add(s, "xs_arr_join(");
            emit_expr(s, n->method_call.obj, depth);
            sb_add(s, ", ");
            if (n->method_call.args.len > 0) emit_expr(s, n->method_call.args.items[0], depth);
            else sb_add(s, "XS_STR(\"\")");
            sb_addc(s, ')');
        } else if (strcmp(meth, "parse_float") == 0) {
            sb_add(s, "xs_str_parse_float(");
            emit_expr(s, n->method_call.obj, depth);
            sb_addc(s, ')');
        } else if (strcmp(meth, "map") == 0 || strcmp(meth, "filter") == 0 ||
                   strcmp(meth, "reduce") == 0 || strcmp(meth, "any") == 0 ||
                   strcmp(meth, "all") == 0) {
            /* array method with callback: use xs_call */
            int mid = defer_label_counter++;
            if (strcmp(meth, "map") == 0) {
                sb_printf(s, "({ xs_val __am_%d = xs_array(0);\n", mid);
                sb_indent(s, depth+1); sb_printf(s, "xs_arr *__src_%d = (xs_arr*)(", mid);
                emit_expr(s, n->method_call.obj, depth); sb_printf(s, ").p;\n");
                sb_indent(s, depth+1); sb_printf(s, "xs_val __fn_%d = ", mid);
                if (n->method_call.args.len > 0) emit_expr(s, n->method_call.args.items[0], depth);
                sb_add(s, ";\n");
                sb_indent(s, depth+1); sb_printf(s, "for (int __i=0; __src_%d && __i < __src_%d->len; __i++) {\n", mid, mid);
                sb_indent(s, depth+2); sb_printf(s, "xs_val __a = __src_%d->items[__i];\n", mid);
                sb_indent(s, depth+2); sb_printf(s, "xs_arr_push(__am_%d, xs_call(__fn_%d, &__a, 1));\n", mid, mid);
                sb_indent(s, depth+1); sb_add(s, "}\n");
                sb_indent(s, depth); sb_printf(s, "__am_%d; })", mid);
            } else if (strcmp(meth, "filter") == 0) {
                sb_printf(s, "({ xs_val __am_%d = xs_array(0);\n", mid);
                sb_indent(s, depth+1); sb_printf(s, "xs_arr *__src_%d = (xs_arr*)(", mid);
                emit_expr(s, n->method_call.obj, depth); sb_printf(s, ").p;\n");
                sb_indent(s, depth+1); sb_printf(s, "xs_val __fn_%d = ", mid);
                if (n->method_call.args.len > 0) emit_expr(s, n->method_call.args.items[0], depth);
                sb_add(s, ";\n");
                sb_indent(s, depth+1); sb_printf(s, "for (int __i=0; __src_%d && __i < __src_%d->len; __i++) {\n", mid, mid);
                sb_indent(s, depth+2); sb_printf(s, "xs_val __a = __src_%d->items[__i];\n", mid);
                sb_indent(s, depth+2); sb_printf(s, "if (xs_truthy(xs_call(__fn_%d, &__a, 1))) xs_arr_push(__am_%d, __a);\n", mid, mid);
                sb_indent(s, depth+1); sb_add(s, "}\n");
                sb_indent(s, depth); sb_printf(s, "__am_%d; })", mid);
            } else if (strcmp(meth, "reduce") == 0) {
                sb_printf(s, "({ xs_val __acc_%d = ", mid);
                if (n->method_call.args.len > 1) emit_expr(s, n->method_call.args.items[1], depth);
                else sb_add(s, "XS_INT(0)");
                sb_add(s, ";\n");
                sb_indent(s, depth+1); sb_printf(s, "xs_arr *__src_%d = (xs_arr*)(", mid);
                emit_expr(s, n->method_call.obj, depth); sb_printf(s, ").p;\n");
                sb_indent(s, depth+1); sb_printf(s, "xs_val __fn_%d = ", mid);
                if (n->method_call.args.len > 0) emit_expr(s, n->method_call.args.items[0], depth);
                sb_add(s, ";\n");
                sb_indent(s, depth+1); sb_printf(s, "for (int __i=0; __src_%d && __i < __src_%d->len; __i++) {\n", mid, mid);
                sb_indent(s, depth+2); sb_printf(s, "xs_val __ra[2] = { __acc_%d, __src_%d->items[__i] };\n", mid, mid);
                sb_indent(s, depth+2); sb_printf(s, "__acc_%d = xs_call(__fn_%d, __ra, 2);\n", mid, mid);
                sb_indent(s, depth+1); sb_add(s, "}\n");
                sb_indent(s, depth); sb_printf(s, "__acc_%d; })", mid);
            } else if (strcmp(meth, "any") == 0) {
                sb_printf(s, "({ int __found_%d = 0;\n", mid);
                sb_indent(s, depth+1); sb_printf(s, "xs_arr *__src_%d = (xs_arr*)(", mid);
                emit_expr(s, n->method_call.obj, depth); sb_printf(s, ").p;\n");
                sb_indent(s, depth+1); sb_printf(s, "xs_val __fn_%d = ", mid);
                if (n->method_call.args.len > 0) emit_expr(s, n->method_call.args.items[0], depth);
                sb_add(s, ";\n");
                sb_indent(s, depth+1); sb_printf(s, "for (int __i=0; __src_%d && __i < __src_%d->len; __i++) {\n", mid, mid);
                sb_indent(s, depth+2); sb_printf(s, "xs_val __a = __src_%d->items[__i];\n", mid);
                sb_indent(s, depth+2); sb_printf(s, "if (xs_truthy(xs_call(__fn_%d, &__a, 1))) { __found_%d = 1; break; }\n", mid, mid);
                sb_indent(s, depth+1); sb_add(s, "}\n");
                sb_indent(s, depth); sb_printf(s, "XS_BOOL(__found_%d); })", mid);
            } else { /* all */
                sb_printf(s, "({ int __all_%d = 1;\n", mid);
                sb_indent(s, depth+1); sb_printf(s, "xs_arr *__src_%d = (xs_arr*)(", mid);
                emit_expr(s, n->method_call.obj, depth); sb_printf(s, ").p;\n");
                sb_indent(s, depth+1); sb_printf(s, "xs_val __fn_%d = ", mid);
                if (n->method_call.args.len > 0) emit_expr(s, n->method_call.args.items[0], depth);
                sb_add(s, ";\n");
                sb_indent(s, depth+1); sb_printf(s, "for (int __i=0; __src_%d && __i < __src_%d->len; __i++) {\n", mid, mid);
                sb_indent(s, depth+2); sb_printf(s, "xs_val __a = __src_%d->items[__i];\n", mid);
                sb_indent(s, depth+2); sb_printf(s, "if (!xs_truthy(xs_call(__fn_%d, &__a, 1))) { __all_%d = 0; break; }\n", mid, mid);
                sb_indent(s, depth+1); sb_add(s, "}\n");
                sb_indent(s, depth); sb_printf(s, "XS_BOOL(__all_%d); })", mid);
            }
        } else {
            /* check if receiver is a known struct with impl */
            const char *stype = NULL;
            if (n->method_call.obj && n->method_call.obj->tag == NODE_IDENT)
                stype = lookup_struct_var(n->method_call.obj->ident.name);
            if (stype) {
                sb_printf(s, "%s_%s(", stype, meth);
                emit_expr(s, n->method_call.obj, depth);
                for (int i = 0; i < n->method_call.args.len; i++) {
                    sb_add(s, ", ");
                    emit_expr(s, n->method_call.args.items[i], depth);
                }
                sb_addc(s, ')');
            } else if (n_impl_types == 1) {
                /* single impl type: assume method belongs to it */
                sb_printf(s, "%s_%s(", impl_types[0].type_name, meth);
                emit_expr(s, n->method_call.obj, depth);
                for (int i = 0; i < n->method_call.args.len; i++) {
                    sb_add(s, ", ");
                    emit_expr(s, n->method_call.args.items[i], depth);
                }
                sb_addc(s, ')');
            } else {
                /* generic: function-style call */
                sb_add(s, meth);
                sb_addc(s, '(');
                emit_expr(s, n->method_call.obj, depth);
                for (int i = 0; i < n->method_call.args.len; i++) {
                    sb_add(s, ", ");
                    emit_expr(s, n->method_call.args.items[i], depth);
                }
                sb_addc(s, ')');
            }
        }
        break;
    }
    case NODE_INDEX:
        sb_add(s, "xs_index(");
        emit_expr(s, n->index.obj, depth);
        sb_add(s, ", ");
        emit_expr(s, n->index.index, depth);
        sb_addc(s, ')');
        break;
    case NODE_FIELD:
        if (n_actor_fields > 0 && in_method_body && n->field.obj &&
            n->field.obj->tag == NODE_IDENT &&
            strcmp(n->field.obj->ident.name, "self") == 0) {
            /* actor method: self is a struct pointer */
            sb_printf(s, "self->%s", n->field.name);
        } else {
            /* struct/map fields or tuple indices */
            sb_printf(s, "xs_index(");
            emit_expr(s, n->field.obj, depth);
            /* check if field name is numeric (tuple index) */
            if (n->field.name && n->field.name[0] >= '0' && n->field.name[0] <= '9')
                sb_printf(s, ", XS_INT(%s))", n->field.name);
            else
                sb_printf(s, ", XS_STR(\"%s\"))", n->field.name);
        }
        break;
    case NODE_SCOPE:
        /* enum variant: Foo::Bar -> map with _variant and _type */
        if (n->scope.nparts == 2) {
            sb_printf(s, "xs_map(2, XS_STR(\"_type\"), XS_STR(\"%s\"), XS_STR(\"_variant\"), XS_STR(\"%s\"))",
                      n->scope.parts[0], n->scope.parts[1]);
        } else {
            for (int i = 0; i < n->scope.nparts; i++) {
                if (i) sb_add(s, "_");
                sb_add(s, n->scope.parts[i]);
            }
        }
        break;
    case NODE_ASSIGN:
        if (n->assign.target && n->assign.target->tag == NODE_INDEX) {
            sb_add(s, "xs_map_put(&");
            emit_expr(s, n->assign.target->index.obj, depth);
            sb_add(s, ", ");
            emit_expr(s, n->assign.target->index.index, depth);
            sb_add(s, ", ");
            emit_expr(s, n->assign.value, depth);
            sb_addc(s, ')');
        } else {
            emit_expr(s, n->assign.target, depth);
            sb_addc(s, ' ');
            sb_add(s, n->assign.op);
            sb_addc(s, ' ');
            emit_expr(s, n->assign.value, depth);
        }
        break;
    case NODE_RANGE:
        sb_add(s, "xs_range(");
        emit_expr(s, n->range.start, depth);
        sb_add(s, ", ");
        emit_expr(s, n->range.end, depth);
        sb_printf(s, ", %d)", n->range.inclusive ? 1 : 0);
        break;
    case NODE_LAMBDA: {
        int lid = register_lambda(n);
        /* find the lambda info to check for captures */
        LambdaInfo *linfo = NULL;
        for (int i = 0; i < n_lambdas; i++)
            if (lambdas[i].id == lid) { linfo = &lambdas[i]; break; }
        if (linfo && linfo->n_captures > 0) {
            /* create env: array of xs_val* pointers to captured variables */
            sb_printf(s, "({ xs_val **__cenv_%d = (xs_val**)malloc(%d * sizeof(xs_val*));\n",
                      lid, linfo->n_captures);
            for (int ci = 0; ci < linfo->n_captures; ci++) {
                sb_indent(s, depth + 1);
                sb_printf(s, "__cenv_%d[%d] = __box_%s;\n", lid, ci, linfo->captures[ci]);
            }
            sb_indent(s, depth);
            sb_printf(s, "xs_fn_new(__xs_lambda_%d, __cenv_%d); })", lid, lid);
        } else {
            sb_printf(s, "xs_fn_new(__xs_lambda_%d, NULL)", lid);
        }
        break;
    }
    case NODE_CAST: {
        const char *ty = n->cast.type_name ? n->cast.type_name : "?";
        if (strcmp(ty, "float") == 0 || strcmp(ty, "f64") == 0 || strcmp(ty, "f32") == 0) {
            sb_add(s, "XS_FLOAT(xs_to_f64(");
            emit_expr(s, n->cast.expr, depth);
            sb_add(s, "))");
        } else if (strcmp(ty, "int") == 0 || strcmp(ty, "i32") == 0 || strcmp(ty, "i64") == 0) {
            sb_add(s, "XS_INT((int64_t)xs_to_f64(");
            emit_expr(s, n->cast.expr, depth);
            sb_add(s, "))");
        } else if (strcmp(ty, "str") == 0 || strcmp(ty, "string") == 0) {
            sb_add(s, "XS_STR(strdup(xs_to_str(");
            emit_expr(s, n->cast.expr, depth);
            sb_add(s, ")))");
        } else if (strcmp(ty, "bool") == 0) {
            sb_add(s, "XS_BOOL(xs_truthy(");
            emit_expr(s, n->cast.expr, depth);
            sb_add(s, "))");
        } else {
            emit_expr(s, n->cast.expr, depth);
        }
        break;
    }
    case NODE_STRUCT_INIT:
        if (n->struct_init.rest) {
            /* spread: start with base, override fields */
            int sid = defer_label_counter++;
            sb_printf(s, "({ xs_val __si_%d = ", sid);
            emit_expr(s, n->struct_init.rest, depth);
            sb_add(s, ";\n");
            /* copy the base map */
            sb_indent(s, depth + 1);
            sb_printf(s, "xs_val __sr_%d = xs_map(0);\n", sid);
            sb_indent(s, depth + 1);
            sb_printf(s, "if (__si_%d.tag == 6 && __si_%d.p) {\n", sid, sid);
            sb_indent(s, depth + 2);
            sb_printf(s, "xs_hmap *__m = (xs_hmap*)__si_%d.p;\n", sid);
            sb_indent(s, depth + 2);
            sb_printf(s, "for (int __k = 0; __k < __m->len; __k++)\n");
            sb_indent(s, depth + 3);
            sb_printf(s, "xs_map_put(&__sr_%d, XS_STR(__m->keys[__k]), __m->vals[__k]);\n", sid);
            sb_indent(s, depth + 1);
            sb_add(s, "}\n");
            for (int i = 0; i < n->struct_init.fields.len; i++) {
                sb_indent(s, depth + 1);
                sb_printf(s, "xs_map_put(&__sr_%d, XS_STR(\"%s\"), ", sid, n->struct_init.fields.items[i].key);
                emit_expr(s, n->struct_init.fields.items[i].val, depth);
                sb_add(s, ");\n");
            }
            sb_indent(s, depth);
            sb_printf(s, "__sr_%d; })", sid);
        } else {
            /* emit struct init as map */
            sb_printf(s, "xs_map(%d", n->struct_init.fields.len);
            for (int i = 0; i < n->struct_init.fields.len; i++) {
                sb_printf(s, ", XS_STR(\"%s\"), ", n->struct_init.fields.items[i].key);
                emit_expr(s, n->struct_init.fields.items[i].val, depth);
            }
            sb_addc(s, ')');
        }
        break;
    case NODE_SPREAD:
        sb_add(s, "(fprintf(stderr, \"xs: spread operator not supported in C target\\n\"), exit(1), XS_NULL)");
        break;
    case NODE_LIST_COMP: {
        /* [expr for x in iter if cond] -> inline block that builds an array */
        int lc_id = defer_label_counter++;
        sb_printf(s, "({ xs_val __lc_%d = xs_array(0);\n", lc_id);
        /* Emit nested for-loops for each clause */
        for (int ci = 0; ci < n->list_comp.clause_pats.len; ci++) {
            sb_indent(s, depth + 1 + ci);
            sb_add(s, "{ xs_val __lc_iter = xs_iter(");
            emit_expr(s, n->list_comp.clause_iters.items[ci], depth);
            sb_add(s, ");\n");
            sb_indent(s, depth + 1 + ci);
            sb_add(s, "xs_val ");
            Node *cpat = n->list_comp.clause_pats.items[ci];
            if (cpat && cpat->tag == NODE_PAT_IDENT)
                sb_add(s, cpat->pat_ident.name);
            else
                sb_printf(s, "__lc_v_%d", ci);
            sb_add(s, ";\n");
            sb_indent(s, depth + 1 + ci);
            sb_add(s, "while (xs_iter_next(&__lc_iter, &");
            if (cpat && cpat->tag == NODE_PAT_IDENT)
                sb_add(s, cpat->pat_ident.name);
            else
                sb_printf(s, "__lc_v_%d", ci);
            sb_add(s, ")) {\n");
        }
        /* Emit condition guard if present */
        int last_ci = n->list_comp.clause_pats.len - 1;
        int inner_depth = depth + 1 + n->list_comp.clause_pats.len;
        if (last_ci >= 0 && n->list_comp.clause_conds.items[last_ci]) {
            sb_indent(s, inner_depth);
            sb_add(s, "if (xs_truthy(");
            emit_expr(s, n->list_comp.clause_conds.items[last_ci], depth);
            sb_add(s, ")) ");
        } else {
            sb_indent(s, inner_depth);
        }
        sb_printf(s, "xs_arr_push(__lc_%d, ", lc_id);
        emit_expr(s, n->list_comp.element, depth);
        sb_add(s, ");\n");
        /* Close nested loops in reverse */
        for (int ci = n->list_comp.clause_pats.len - 1; ci >= 0; ci--) {
            sb_indent(s, depth + 1 + ci);
            sb_add(s, "} }\n");
        }
        sb_indent(s, depth);
        sb_printf(s, "__lc_%d; })", lc_id);
        break;
    }
    case NODE_MAP_COMP: {
        /* {k: v for x in iter if cond} -> inline block that builds a map */
        int mc_id = defer_label_counter++;
        sb_printf(s, "({ xs_val __mc_%d = xs_map(0);\n", mc_id);
        for (int ci = 0; ci < n->map_comp.clause_pats.len; ci++) {
            sb_indent(s, depth + 1 + ci);
            sb_add(s, "{ xs_val __mc_iter = xs_iter(");
            emit_expr(s, n->map_comp.clause_iters.items[ci], depth);
            sb_add(s, ");\n");
            sb_indent(s, depth + 1 + ci);
            sb_add(s, "xs_val ");
            Node *cpat = n->map_comp.clause_pats.items[ci];
            if (cpat && cpat->tag == NODE_PAT_IDENT)
                sb_add(s, cpat->pat_ident.name);
            else
                sb_printf(s, "__mc_v_%d", ci);
            sb_add(s, ";\n");
            sb_indent(s, depth + 1 + ci);
            sb_add(s, "while (xs_iter_next(&__mc_iter, &");
            if (cpat && cpat->tag == NODE_PAT_IDENT)
                sb_add(s, cpat->pat_ident.name);
            else
                sb_printf(s, "__mc_v_%d", ci);
            sb_add(s, ")) {\n");
        }
        int mc_last = n->map_comp.clause_pats.len - 1;
        int mc_inner = depth + 1 + n->map_comp.clause_pats.len;
        if (mc_last >= 0 && n->map_comp.clause_conds.items[mc_last]) {
            sb_indent(s, mc_inner);
            sb_add(s, "if (xs_truthy(");
            emit_expr(s, n->map_comp.clause_conds.items[mc_last], depth);
            sb_add(s, ")) ");
        } else {
            sb_indent(s, mc_inner);
        }
        sb_printf(s, "xs_map_put(&__mc_%d, ", mc_id);
        emit_expr(s, n->map_comp.key, depth);
        sb_add(s, ", ");
        emit_expr(s, n->map_comp.value, depth);
        sb_add(s, ");\n");
        for (int ci = n->map_comp.clause_pats.len - 1; ci >= 0; ci--) {
            sb_indent(s, depth + 1 + ci);
            sb_add(s, "} }\n");
        }
        sb_indent(s, depth);
        sb_printf(s, "__mc_%d; })", mc_id);
        break;
    }
    case NODE_AWAIT:
        /* await in C target -> just evaluate the expression (single-threaded) */
        if (n->await_.expr) emit_expr(s, n->await_.expr, depth);
        else sb_add(s, "XS_NULL");
        break;
    case NODE_YIELD:
        sb_add(s, "(fprintf(stderr, \"xs: yield not supported in C target\\n\"), exit(1), XS_NULL)");
        break;
    case NODE_SPAWN: {
        Node *se = n->spawn_.expr;
        if (se && se->tag == NODE_IDENT && find_actor(se->ident.name)) {
            /* spawn ActorName -> already handled as statement, return dummy */
            sb_add(s, "XS_NULL /* spawn actor */");
        } else if (se && se->tag == NODE_BLOCK) {
            /* spawn { block } -> execute inline, return result map */
            sb_add(s, "({ xs_val __spawn_result = XS_NULL;\n");
            emit_block_body(s, se, depth + 1);
            if (se->block.expr) {
                sb_indent(s, depth + 1);
                sb_add(s, "__spawn_result = ");
                emit_expr(s, se->block.expr, depth + 1);
                sb_add(s, ";\n");
            }
            sb_indent(s, depth + 1);
            sb_add(s, "xs_val __spawn_map = xs_map(2, XS_STR(\"_result\"), __spawn_result, XS_STR(\"_status\"), XS_STR(\"done\"));\n");
            sb_indent(s, depth + 1);
            sb_add(s, "__spawn_map; })");
        } else {
            /* spawn <expr> -> just evaluate it */
            if (se) emit_expr(s, se, depth);
            else sb_add(s, "XS_NULL");
        }
        break;
    }
    case NODE_ACTOR_DECL:
        /* actor decl as expression: already emitted at file scope, just return null */
        sb_add(s, "XS_NULL");
        break;
    case NODE_SEND_EXPR: {
        /* actor ! message -> call handle method */
        const char *atype = NULL;
        if (n->send_expr.target && n->send_expr.target->tag == NODE_IDENT)
            atype = lookup_actor_var(n->send_expr.target->ident.name);
        if (atype) {
            sb_printf(s, "%s_handle(&%s_state, ", atype, n->send_expr.target->ident.name);
            emit_expr(s, n->send_expr.message, depth);
            sb_addc(s, ')');
        } else {
            sb_add(s, "/* send */ XS_NULL");
        }
        break;
    }
        break;
    case NODE_RESUME:
        sb_add(s, "(fprintf(stderr, \"xs: resume not supported in C target\\n\"), exit(1), XS_NULL)");
        break;
    case NODE_PERFORM:
        sb_printf(s, "(fprintf(stderr, \"xs: perform (%s.%s) not supported in C target\\n\"), exit(1), XS_NULL)",
                  n->perform.effect_name ? n->perform.effect_name : "?",
                  n->perform.op_name ? n->perform.op_name : "?");
        break;
    case NODE_THROW:
        sb_add(s, "(xs_throw(");
        emit_expr(s, n->throw_.value, depth);
        sb_add(s, "), XS_NULL)");
        break;
    case NODE_RETURN:
        if (n->ret.value) emit_expr(s, n->ret.value, depth);
        else sb_add(s, "XS_NULL");
        break;
    case NODE_IF: {
        /* if as expression: ternary */
        sb_add(s, "(xs_truthy(");
        emit_expr(s, n->if_expr.cond, depth);
        sb_add(s, ") ? ");
        if (n->if_expr.then && n->if_expr.then->tag == NODE_BLOCK && n->if_expr.then->block.expr) {
            emit_expr(s, n->if_expr.then->block.expr, depth);
        } else {
            sb_add(s, "XS_NULL");
        }
        sb_add(s, " : ");
        if (n->if_expr.else_branch && n->if_expr.else_branch->tag == NODE_BLOCK
            && n->if_expr.else_branch->block.expr) {
            emit_expr(s, n->if_expr.else_branch->block.expr, depth);
        } else {
            sb_add(s, "XS_NULL");
        }
        sb_addc(s, ')');
        break;
    }
    case NODE_MATCH: {
        /* match as expression -> GCC statement expression with if/else chain */
        sb_add(s, "({ xs_val __subject = ");
        emit_expr(s, n->match.subject, depth);
        sb_add(s, ";\n");
        sb_indent(s, depth + 1);
        sb_add(s, "xs_val __match_result = XS_NULL;\n");
        for (int i = 0; i < n->match.arms.len; i++) {
            MatchArm *arm = &n->match.arms.items[i];
            sb_indent(s, depth + 1);
            if (arm->guard) {
                if (i == 0) sb_add(s, "if (");
                else sb_add(s, "else if (");
                emit_pattern_cond(s, arm->pattern, "__subject", depth + 1);
                sb_add(s, ") {\n");
                emit_pattern_bindings(s, arm->pattern, "__subject", depth + 2);
                sb_indent(s, depth + 2);
                sb_add(s, "if (xs_truthy(");
                emit_expr(s, arm->guard, depth + 2);
                sb_add(s, ")) {\n");
                if (arm->body && arm->body->tag == NODE_BLOCK) {
                    emit_block_body(s, arm->body, depth + 3);
                    if (arm->body->block.expr) {
                        sb_indent(s, depth + 3);
                        sb_add(s, "__match_result = ");
                        emit_expr(s, arm->body->block.expr, depth + 3);
                        sb_add(s, ";\n");
                    }
                } else if (arm->body) {
                    sb_indent(s, depth + 3);
                    sb_add(s, "__match_result = ");
                    emit_expr(s, arm->body, depth + 3);
                    sb_add(s, ";\n");
                }
                sb_indent(s, depth + 2);
                sb_add(s, "}\n");
                sb_indent(s, depth + 1);
                sb_add(s, "}\n");
            } else {
                if (i == 0) sb_add(s, "if (");
                else sb_add(s, "else if (");
                emit_pattern_cond(s, arm->pattern, "__subject", depth + 1);
                sb_add(s, ") {\n");
                emit_pattern_bindings(s, arm->pattern, "__subject", depth + 2);
                if (arm->body && arm->body->tag == NODE_BLOCK) {
                    emit_block_body(s, arm->body, depth + 2);
                    if (arm->body->block.expr) {
                        sb_indent(s, depth + 2);
                        sb_add(s, "__match_result = ");
                        emit_expr(s, arm->body->block.expr, depth + 2);
                        sb_add(s, ";\n");
                    }
                } else if (arm->body) {
                    sb_indent(s, depth + 2);
                    sb_add(s, "__match_result = ");
                    emit_expr(s, arm->body, depth + 2);
                    sb_add(s, ";\n");
                }
                sb_indent(s, depth + 1);
                sb_add(s, "}\n");
            }
        }
        sb_indent(s, depth + 1);
        sb_add(s, "__match_result;\n");
        sb_indent(s, depth);
        sb_add(s, "})");
        break;
    }
    case NODE_BLOCK:
        if (n->block.expr) {
            emit_expr(s, n->block.expr, depth);
        } else {
            sb_add(s, "XS_NULL");
        }
        break;
    case NODE_PAT_IDENT:
        sb_add(s, n->pat_ident.name);
        break;
    case NODE_PAT_WILD:
        sb_add(s, "_");
        break;
    case NODE_PAT_LIT:
        switch (n->pat_lit.tag) {
        case 0: sb_printf(s, "%" PRId64, n->pat_lit.ival); break;
        case 1: sb_printf(s, "%g", n->pat_lit.fval); break;
        case 2: sb_printf(s, "\"%s\"", n->pat_lit.sval ? n->pat_lit.sval : ""); break;
        case 3: sb_add(s, n->pat_lit.bval ? "1" : "0"); break;
        case 4: sb_add(s, "XS_NULL"); break;
        default: sb_add(s, "XS_NULL"); break;
        }
        break;
    case NODE_PAT_TUPLE: {
        /* Emit tuple as an array literal */
        sb_printf(s, "xs_array(%d", n->pat_tuple.elems.len);
        for (int i = 0; i < n->pat_tuple.elems.len; i++) {
            sb_add(s, ", ");
            emit_expr(s, n->pat_tuple.elems.items[i], depth);
        }
        sb_addc(s, ')');
        break;
    }
    case NODE_PAT_ENUM: {
        /* Emit enum constructor tag check value */
        if (n->pat_enum.path) {
            sb_add(s, n->pat_enum.path);
        } else {
            sb_add(s, "XS_NULL");
        }
        break;
    }
    case NODE_PAT_GUARD:
        /* Emit the guard condition as a boolean expression */
        sb_add(s, "XS_BOOL(");
        emit_expr(s, n->pat_guard.guard, depth);
        sb_addc(s, ')');
        break;
    case NODE_PAT_STRUCT:
    case NODE_PAT_OR:
    case NODE_PAT_RANGE:
    case NODE_PAT_SLICE:
    case NODE_PAT_EXPR:
    case NODE_PAT_CAPTURE:
    case NODE_PAT_STRING_CONCAT:
    case NODE_PAT_REGEX:
        sb_add(s, "/* pattern */ XS_NULL");
        break;
    /* declaration nodes as expressions emit their identifier */
    case NODE_FN_DECL:
        if (n->fn_decl.name) emit_safe_name(s, n->fn_decl.name);
        else sb_add(s, "XS_NULL");
        break;
    case NODE_STRUCT_DECL:
    case NODE_ENUM_DECL:
    case NODE_CLASS_DECL:
    case NODE_TRAIT_DECL:
    case NODE_IMPL_DECL:
    case NODE_TYPE_ALIAS:
    case NODE_IMPORT:
    case NODE_USE:
    case NODE_MODULE_DECL:
    case NODE_EFFECT_DECL:
        sb_add(s, "XS_NULL");
        break;
    case NODE_LET:
    case NODE_VAR:
        if (n->let.name) sb_add(s, n->let.name);
        else sb_add(s, "XS_NULL");
        break;
    case NODE_CONST:
        if (n->const_.name) sb_add(s, n->const_.name);
        else sb_add(s, "XS_NULL");
        break;
    case NODE_EXPR_STMT:
        emit_expr(s, n->expr_stmt.expr, depth);
        break;
    case NODE_WHILE:
    case NODE_FOR:
    case NODE_LOOP:
    case NODE_TRY:
    case NODE_NURSERY:
    case NODE_HANDLE:
    case NODE_DEFER:
    case NODE_BREAK:
    case NODE_CONTINUE:
    case NODE_PROGRAM:
        sb_add(s, "XS_NULL");
        break;
    }
}

/* pattern condition for match */
static void emit_pattern_cond(SB *s, Node *pat, const char *subject, int depth) {
    if (!pat) { sb_add(s, "1"); return; }
    switch (pat->tag) {
    case NODE_PAT_WILD:
    case NODE_PAT_IDENT:
        sb_add(s, "1");
        break;
    case NODE_PAT_LIT:
        switch (pat->pat_lit.tag) {
        case 0: sb_printf(s, "(%s.i == %" PRId64 ")", subject, pat->pat_lit.ival); break;
        case 1: sb_printf(s, "(%s.f == %g)", subject, pat->pat_lit.fval); break;
        case 2: sb_printf(s, "(%s.s && strcmp(%s.s, \"%s\") == 0)", subject, subject,
                          pat->pat_lit.sval ? pat->pat_lit.sval : ""); break;
        case 3: sb_printf(s, "(xs_truthy(%s) == %d)", subject, pat->pat_lit.bval ? 1 : 0); break;
        case 4: sb_printf(s, "(%s.tag == 4)", subject); break;
        default: sb_add(s, "1"); break;
        }
        break;
    case NODE_PAT_OR:
        sb_addc(s, '(');
        emit_pattern_cond(s, pat->pat_or.left, subject, depth);
        sb_add(s, " || ");
        emit_pattern_cond(s, pat->pat_or.right, subject, depth);
        sb_addc(s, ')');
        break;
    case NODE_PAT_RANGE:
        sb_printf(s, "(xs_cmp(%s, ", subject);
        emit_expr(s, pat->pat_range.start, depth);
        sb_add(s, ") >= 0 && xs_cmp(");
        sb_add(s, subject);
        sb_add(s, ", ");
        emit_expr(s, pat->pat_range.end, depth);
        if (pat->pat_range.inclusive) sb_add(s, ") <= 0)");
        else sb_add(s, ") < 0)");
        break;
    case NODE_PAT_EXPR:
        sb_printf(s, "xs_eq(%s, ", subject);
        emit_expr(s, pat->pat_expr.expr, depth);
        sb_addc(s, ')');
        break;
    case NODE_PAT_GUARD: {
        /* for guards with ident patterns, bind first then check */
        Node *inner = pat->pat_guard.pattern;
        if (inner && inner->tag == NODE_PAT_IDENT) {
            /* (({ xs_val name = subject; truthy(guard) })) */
            sb_printf(s, "({ xs_val %s = %s; xs_truthy(", inner->pat_ident.name, subject);
            emit_expr(s, pat->pat_guard.guard, depth);
            sb_add(s, "); })");
        } else {
            emit_pattern_cond(s, inner, subject, depth);
            sb_add(s, " && xs_truthy(");
            emit_expr(s, pat->pat_guard.guard, depth);
            sb_addc(s, ')');
        }
        break;
    }
    case NODE_PAT_CAPTURE:
        emit_pattern_cond(s, pat->pat_capture.pattern, subject, depth);
        break;
    case NODE_PAT_TUPLE: {
        sb_printf(s, "(%s.tag == 5", subject); /* tag 5 = array/tuple */
        for (int i = 0; i < pat->pat_tuple.elems.len; i++) {
            char sub[256];
            snprintf(sub, sizeof sub, "xs_index(%s, XS_INT(%d))", subject, i);
            sb_add(s, " && ");
            emit_pattern_cond(s, pat->pat_tuple.elems.items[i], sub, depth);
        }
        sb_addc(s, ')');
        break;
    }
    case NODE_PAT_STRUCT: {
        sb_printf(s, "(%s.tag != 4", subject); /* not null */
        for (int i = 0; i < pat->pat_struct.fields.len; i++) {
            if (pat->pat_struct.fields.items[i].val) {
                char sub[256];
                snprintf(sub, sizeof sub, "%s.%s", subject, pat->pat_struct.fields.items[i].key);
                sb_add(s, " && ");
                emit_pattern_cond(s, pat->pat_struct.fields.items[i].val, sub, depth);
            }
        }
        sb_addc(s, ')');
        break;
    }
    case NODE_PAT_ENUM: {
        if (pat->pat_enum.path) {
            /* check _variant matches: extract variant name from path like "Shape::Circle" */
            const char *vname = pat->pat_enum.path;
            if (vname) {
                const char *sep = strstr(vname, "::");
                if (sep) vname = sep + 2;
            }
            sb_printf(s, "(xs_eq(xs_index(%s, XS_STR(\"_variant\")), XS_STR(\"%s\"))",
                      subject, vname ? vname : "?");
        } else {
            sb_add(s, "(1");
        }
        for (int i = 0; i < pat->pat_enum.args.len; i++) {
            char sub[256];
            snprintf(sub, sizeof sub, "xs_index(%s, XS_STR(\"%d\"))", subject, i);
            sb_add(s, " && ");
            emit_pattern_cond(s, pat->pat_enum.args.items[i], sub, depth);
        }
        sb_addc(s, ')');
        break;
    }
    case NODE_PAT_SLICE: {
        sb_printf(s, "(%s.tag == 5", subject);
        for (int i = 0; i < pat->pat_slice.elems.len; i++) {
            char sub[256];
            snprintf(sub, sizeof sub, "xs_index(%s, XS_INT(%d))", subject, i);
            sb_add(s, " && ");
            emit_pattern_cond(s, pat->pat_slice.elems.items[i], sub, depth);
        }
        sb_addc(s, ')');
        break;
    }
    default:
        sb_add(s, "1");
        break;
    }
}

/* pattern binding emitter */
static void emit_pattern_bindings(SB *s, Node *pat, const char *subject, int depth) {
    if (!pat) return;
    switch (pat->tag) {
    case NODE_PAT_IDENT:
        sb_indent(s, depth);
        sb_printf(s, "xs_val %s = %s;\n", pat->pat_ident.name, subject);
        break;
    case NODE_PAT_CAPTURE:
        if (pat->pat_capture.name) {
            sb_indent(s, depth);
            sb_printf(s, "xs_val %s = %s;\n", pat->pat_capture.name, subject);
        }
        emit_pattern_bindings(s, pat->pat_capture.pattern, subject, depth);
        break;
    case NODE_PAT_TUPLE:
        for (int i = 0; i < pat->pat_tuple.elems.len; i++) {
            char sub[256];
            snprintf(sub, sizeof sub, "xs_index(%s, XS_INT(%d))", subject, i);
            emit_pattern_bindings(s, pat->pat_tuple.elems.items[i], sub, depth);
        }
        break;
    case NODE_PAT_STRUCT:
        for (int i = 0; i < pat->pat_struct.fields.len; i++) {
            if (pat->pat_struct.fields.items[i].val) {
                char sub[256];
                snprintf(sub, sizeof sub, "%s.%s", subject, pat->pat_struct.fields.items[i].key);
                emit_pattern_bindings(s, pat->pat_struct.fields.items[i].val, sub, depth);
            }
        }
        break;
    case NODE_PAT_OR:
        emit_pattern_bindings(s, pat->pat_or.left, subject, depth);
        break;
    case NODE_PAT_GUARD:
        emit_pattern_bindings(s, pat->pat_guard.pattern, subject, depth);
        break;
    case NODE_PAT_ENUM:
        for (int i = 0; i < pat->pat_enum.args.len; i++) {
            char sub[256];
            snprintf(sub, sizeof sub, "xs_index(%s, XS_STR(\"%d\"))", subject, i);
            emit_pattern_bindings(s, pat->pat_enum.args.items[i], sub, depth);
        }
        break;
    default:
        break;
    }
}

/* emit block body */
static void emit_block_body(SB *s, Node *block, int depth) {
    if (!block) return;
    if (block->tag != NODE_BLOCK) {
        emit_stmt(s, block, depth);
        return;
    }
    for (int i = 0; i < block->block.stmts.len; i++) {
        emit_stmt(s, block->block.stmts.items[i], depth);
    }
    if (block->block.expr) {
        /* statement-like nodes should be emitted as statements */
        if (block->block.expr->tag == NODE_SPAWN ||
            block->block.expr->tag == NODE_NURSERY ||
            block->block.expr->tag == NODE_FOR ||
            block->block.expr->tag == NODE_WHILE ||
            block->block.expr->tag == NODE_LOOP ||
            block->block.expr->tag == NODE_IF ||
            block->block.expr->tag == NODE_BLOCK) {
            emit_stmt(s, block->block.expr, depth);
        } else {
            sb_indent(s, depth);
            emit_expr(s, block->block.expr, depth);
            sb_add(s, ";\n");
        }
    }
}

/* statement emitter */
static void emit_stmt(SB *s, Node *n, int depth) {
    if (!n) return;
    switch (n->tag) {
    case NODE_LET: {
        /* check if captured by lambda (needs boxing for closures) */
        if (n->let.name) {
            int is_captured = 0;
            for (int li = 0; li < n_lambdas && !is_captured; li++)
                for (int ci = 0; ci < lambdas[li].n_captures; ci++)
                    if (strcmp(lambdas[li].captures[ci], n->let.name) == 0)
                        { is_captured = 1; break; }
            if (is_captured) {
                /* heap-allocate so closure can outlive this scope */
                sb_indent(s, depth);
                sb_printf(s, "xs_val *__box_%s = (xs_val*)malloc(sizeof(xs_val));\n", n->let.name);
                sb_indent(s, depth);
                sb_printf(s, "*__box_%s = ", n->let.name);
                if (n->let.value) emit_expr(s, n->let.value, depth);
                else sb_add(s, "XS_NULL");
                sb_add(s, ";\n");
                add_boxed_var(n->let.name);
                break;
            }
        }
        /* check for let x = spawn ActorName */
        int is_actor_spawn = 0;
        if (n->let.name && n->let.value && n->let.value->tag == NODE_SPAWN) {
            Node *se = n->let.value->spawn_.expr;
            if (se && se->tag == NODE_IDENT && find_actor(se->ident.name)) {
                is_actor_spawn = 1;
                sb_indent(s, depth);
                sb_printf(s, "/* spawn actor %s */\n", se->ident.name);
            }
        }
        if (!is_actor_spawn) {
            sb_indent(s, depth);
            sb_add(s, "const xs_val ");
            if (n->let.name) sb_add(s, n->let.name);
            else if (n->let.pattern) emit_expr(s, n->let.pattern, depth);
            else sb_add(s, "_");
            if (n->let.value) {
                sb_add(s, " = ");
                emit_expr(s, n->let.value, depth);
            } else {
                sb_add(s, " = XS_NULL");
            }
            sb_add(s, ";\n");
        }
        break;
    }
    case NODE_VAR: {
        /* check if this variable is captured by any lambda */
        int is_captured = 0;
        if (n->let.name) {
            for (int li = 0; li < n_lambdas && !is_captured; li++)
                for (int ci = 0; ci < lambdas[li].n_captures; ci++)
                    if (strcmp(lambdas[li].captures[ci], n->let.name) == 0)
                        { is_captured = 1; break; }
        }
        if (is_captured && n->let.name) {
            /* heap-allocate for closure capture */
            sb_indent(s, depth);
            sb_printf(s, "xs_val *__box_%s = (xs_val*)malloc(sizeof(xs_val));\n", n->let.name);
            sb_indent(s, depth);
            sb_printf(s, "*__box_%s = ", n->let.name);
            if (n->let.value) emit_expr(s, n->let.value, depth);
            else sb_add(s, "XS_NULL");
            sb_add(s, ";\n");
            add_boxed_var(n->let.name);
        } else {
            sb_indent(s, depth);
            sb_add(s, "xs_val ");
            if (n->let.name) sb_add(s, n->let.name);
            else if (n->let.pattern) emit_expr(s, n->let.pattern, depth);
            else sb_add(s, "_");
            if (n->let.value) {
                sb_add(s, " = ");
                emit_expr(s, n->let.value, depth);
            } else {
                sb_add(s, " = XS_NULL");
            }
            sb_add(s, ";\n");
        }
        break;
    }
    case NODE_CONST:
        sb_indent(s, depth);
        sb_printf(s, "const xs_val %s", n->const_.name);
        if (n->const_.value) {
            sb_add(s, " = ");
            emit_expr(s, n->const_.value, depth);
        } else {
            sb_add(s, " = XS_NULL");
        }
        sb_add(s, ";\n");
        break;
    case NODE_FN_DECL: {
        if (is_main_fn(n)) {
            seen_main = 1;
            sb_indent(s, depth);
            sb_add(s, "int main(int argc, char **argv) {\n");
            sb_indent(s, depth + 1);
            sb_add(s, "(void)argc; (void)argv;\n");
            sb_indent(s, depth + 1);
            sb_add(s, "xs_push_frame(\"main\");\n");
            if (n->fn_decl.body && n->fn_decl.body->tag == NODE_BLOCK) {
                int has_defer = block_has_defers(n->fn_decl.body);
                if (has_defer) {
                    /* emit non-defer statements */
                    for (int i = 0; i < n->fn_decl.body->block.stmts.len; i++) {
                        Node *st = n->fn_decl.body->block.stmts.items[i];
                        if (st && st->tag != NODE_DEFER)
                            emit_stmt(s, st, depth + 1);
                    }
                    if (n->fn_decl.body->block.expr) {
                        sb_indent(s, depth + 1);
                        emit_expr(s, n->fn_decl.body->block.expr, depth + 1);
                        sb_add(s, ";\n");
                    }
                    /* cleanup label */
                    sb_indent(s, depth);
                    sb_add(s, "__cleanup:\n");
                    emit_deferred_cleanup(s, n->fn_decl.body, depth + 1);
                } else {
                    emit_block_body(s, n->fn_decl.body, depth + 1);
                    if (n->fn_decl.body->block.expr) {
                        sb_indent(s, depth + 1);
                        emit_expr(s, n->fn_decl.body->block.expr, depth + 1);
                        sb_add(s, ";\n");
                    }
                }
            }
            sb_indent(s, depth + 1);
            sb_add(s, "xs_run_defers(0);\n");
            sb_indent(s, depth + 1);
            sb_add(s, "xs_pop_frame();\n");
            sb_indent(s, depth + 1);
            sb_add(s, "return 0;\n");
            sb_indent(s, depth);
            sb_add(s, "}\n\n");
        } else {
            sb_indent(s, depth);
            sb_add(s, "xs_val ");
            emit_safe_name(s, n->fn_decl.name);
            emit_params_c(s, &n->fn_decl.params);
            sb_add(s, " {\n");
            /* default param handling */
            for (int p = 0; p < n->fn_decl.params.len; p++) {
                Param *pm = &n->fn_decl.params.items[p];
                if (pm->default_val && pm->name) {
                    sb_indent(s, depth + 1);
                    sb_printf(s, "if (%s.tag == 4) %s = ", pm->name, pm->name);
                    emit_expr(s, pm->default_val, depth + 1);
                    sb_add(s, ";\n");
                }
            }
            /* box params that are captured by lambdas */
            for (int p = 0; p < n->fn_decl.params.len; p++) {
                const char *pname = n->fn_decl.params.items[p].name;
                if (!pname) continue;
                int is_captured = 0;
                for (int li = 0; li < n_lambdas && !is_captured; li++)
                    for (int ci = 0; ci < lambdas[li].n_captures; ci++)
                        if (strcmp(lambdas[li].captures[ci], pname) == 0)
                            { is_captured = 1; break; }
                if (is_captured) {
                    sb_indent(s, depth + 1);
                    sb_printf(s, "xs_val *__box_%s = (xs_val*)malloc(sizeof(xs_val));\n", pname);
                    sb_indent(s, depth + 1);
                    sb_printf(s, "*__box_%s = %s;\n", pname, pname);
                    add_boxed_var(pname);
                }
            }
            /* push call stack frame */
            sb_indent(s, depth + 1);
            sb_printf(s, "xs_push_frame(\"%s\");\n", n->fn_decl.name);
            sb_indent(s, depth + 1);
            sb_add(s, "int __saved_defer_top = __xs_defer_top;\n");
            if (n->fn_decl.body && n->fn_decl.body->tag == NODE_BLOCK) {
                int has_defer = block_has_defers(n->fn_decl.body);
                if (has_defer) {
                    sb_indent(s, depth + 1);
                    sb_add(s, "xs_val __retval = XS_NULL;\n");
                    /* emit non-defer statements */
                    for (int i = 0; i < n->fn_decl.body->block.stmts.len; i++) {
                        Node *st = n->fn_decl.body->block.stmts.items[i];
                        if (st && st->tag != NODE_DEFER)
                            emit_stmt(s, st, depth + 1);
                    }
                    if (n->fn_decl.body->block.expr) {
                        sb_indent(s, depth + 1);
                        sb_add(s, "__retval = ");
                        emit_expr(s, n->fn_decl.body->block.expr, depth + 1);
                        sb_add(s, ";\n");
                    }
                    sb_indent(s, depth + 1);
                    sb_add(s, "goto __cleanup;\n");
                    sb_indent(s, depth);
                    sb_add(s, "__cleanup:\n");
                    emit_deferred_cleanup(s, n->fn_decl.body, depth + 1);
                    sb_indent(s, depth + 1);
                    sb_add(s, "xs_run_defers(__saved_defer_top);\n");
                    sb_indent(s, depth + 1);
                    sb_add(s, "xs_pop_frame();\n");
                    sb_indent(s, depth + 1);
                    sb_add(s, "return __retval;\n");
                } else {
                    /* emit stmts */
                    for (int bi = 0; bi < n->fn_decl.body->block.stmts.len; bi++)
                        emit_stmt(s, n->fn_decl.body->block.stmts.items[bi], depth + 1);
                    /* emit block.expr as return (implicit return) */
                    if (n->fn_decl.body->block.expr) {
                        Node *be = n->fn_decl.body->block.expr;
                        /* statement-like exprs need special handling */
                        if (be->tag == NODE_IF || be->tag == NODE_MATCH ||
                            be->tag == NODE_FOR || be->tag == NODE_WHILE ||
                            be->tag == NODE_LOOP || be->tag == NODE_BLOCK) {
                            emit_stmt(s, be, depth + 1);
                        } else if (be->tag == NODE_RETURN) {
                            emit_stmt(s, be, depth + 1);
                        } else {
                            sb_indent(s, depth + 1);
                            sb_add(s, "xs_run_defers(__saved_defer_top);\n");
                            sb_indent(s, depth + 1);
                            sb_add(s, "xs_pop_frame();\n");
                            sb_indent(s, depth + 1);
                            sb_add(s, "return ");
                            emit_expr(s, be, depth + 1);
                            sb_add(s, ";\n");
                        }
                    }
                    sb_indent(s, depth + 1);
                    sb_add(s, "xs_run_defers(__saved_defer_top);\n");
                    sb_indent(s, depth + 1);
                    sb_add(s, "xs_pop_frame();\n");
                    sb_indent(s, depth + 1);
                    sb_add(s, "return XS_NULL;\n");
                }
                /* clear boxed vars for this function scope */
                {
                    n_boxed = 0;
                }
            } else if (n->fn_decl.body) {
                sb_indent(s, depth + 1);
                sb_add(s, "xs_run_defers(__saved_defer_top);\n");
                sb_indent(s, depth + 1);
                sb_add(s, "xs_pop_frame();\n");
                sb_indent(s, depth + 1);
                sb_add(s, "return ");
                emit_expr(s, n->fn_decl.body, depth + 1);
                sb_add(s, ";\n");
            } else {
                sb_indent(s, depth + 1);
                sb_add(s, "xs_run_defers(__saved_defer_top);\n");
                sb_indent(s, depth + 1);
                sb_add(s, "xs_pop_frame();\n");
                sb_indent(s, depth + 1);
                sb_add(s, "return XS_NULL;\n");
            }
            sb_indent(s, depth);
            sb_add(s, "}\n\n");
        }
        break;
    }
    case NODE_RETURN:
        sb_indent(s, depth);
        sb_add(s, "return ");
        if (n->ret.value)
            emit_expr(s, n->ret.value, depth);
        else
            sb_add(s, "XS_NULL");
        sb_add(s, ";\n");
        break;
    case NODE_BREAK:
        sb_indent(s, depth);
        sb_add(s, "break;\n");
        break;
    case NODE_CONTINUE:
        sb_indent(s, depth);
        sb_add(s, "continue;\n");
        break;
    case NODE_IF: {
        sb_indent(s, depth);
        sb_add(s, "if (xs_truthy(");
        emit_expr(s, n->if_expr.cond, depth);
        sb_add(s, ")) {\n");
        if (n->if_expr.then)
            emit_block_body(s, n->if_expr.then, depth + 1);
        sb_indent(s, depth);
        sb_addc(s, '}');
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            sb_add(s, " else if (xs_truthy(");
            emit_expr(s, n->if_expr.elif_conds.items[i], depth);
            sb_add(s, ")) {\n");
            emit_block_body(s, n->if_expr.elif_thens.items[i], depth + 1);
            sb_indent(s, depth);
            sb_addc(s, '}');
        }
        if (n->if_expr.else_branch) {
            sb_add(s, " else {\n");
            emit_block_body(s, n->if_expr.else_branch, depth + 1);
            sb_indent(s, depth);
            sb_addc(s, '}');
        }
        sb_addc(s, '\n');
        break;
    }
    case NODE_WHILE:
        sb_indent(s, depth);
        sb_add(s, "while (xs_truthy(");
        emit_expr(s, n->while_loop.cond, depth);
        sb_add(s, ")) {\n");
        if (n->while_loop.body)
            emit_block_body(s, n->while_loop.body, depth + 1);
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_FOR: {
        /* for pattern in iter -> iteration via xs_iter / xs_next */
        sb_indent(s, depth);
        sb_add(s, "{\n");
        sb_indent(s, depth + 1);
        sb_add(s, "xs_val __iter = xs_iter(");
        emit_expr(s, n->for_loop.iter, depth + 1);
        sb_add(s, ");\n");
        sb_indent(s, depth + 1);
        sb_add(s, "xs_val ");
        if (n->for_loop.pattern && n->for_loop.pattern->tag == NODE_PAT_IDENT)
            sb_add(s, n->for_loop.pattern->pat_ident.name);
        else
            sb_add(s, "__item");
        sb_add(s, ";\n");
        sb_indent(s, depth + 1);
        sb_add(s, "while (xs_iter_next(&__iter, &");
        if (n->for_loop.pattern && n->for_loop.pattern->tag == NODE_PAT_IDENT)
            sb_add(s, n->for_loop.pattern->pat_ident.name);
        else
            sb_add(s, "__item");
        sb_add(s, ")) {\n");
        if (n->for_loop.body)
            emit_block_body(s, n->for_loop.body, depth + 2);
        sb_indent(s, depth + 1);
        sb_add(s, "}\n");
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    }
    case NODE_LOOP:
        sb_indent(s, depth);
        sb_add(s, "while (1) {\n");
        if (n->loop.body)
            emit_block_body(s, n->loop.body, depth + 1);
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_MATCH: {
        sb_indent(s, depth);
        sb_add(s, "{\n");
        sb_indent(s, depth + 1);
        sb_add(s, "xs_val __subject = ");
        emit_expr(s, n->match.subject, depth + 1);
        sb_add(s, ";\n");
        for (int i = 0; i < n->match.arms.len; i++) {
            MatchArm *arm = &n->match.arms.items[i];
            sb_indent(s, depth + 1);
            if (arm->guard) {
                /* for guards, bind pattern vars first then check guard */
                if (i == 0) sb_add(s, "if (");
                else sb_add(s, "else if (");
                emit_pattern_cond(s, arm->pattern, "__subject", depth + 1);
                sb_add(s, ") {\n");
                emit_pattern_bindings(s, arm->pattern, "__subject", depth + 2);
                sb_indent(s, depth + 2);
                sb_add(s, "if (xs_truthy(");
                emit_expr(s, arm->guard, depth + 2);
                sb_add(s, ")) {\n");
                if (arm->body && arm->body->tag == NODE_BLOCK)
                    emit_block_body(s, arm->body, depth + 3);
                else if (arm->body) {
                    sb_indent(s, depth + 3);
                    emit_expr(s, arm->body, depth + 3);
                    sb_add(s, ";\n");
                }
                sb_indent(s, depth + 2);
                sb_add(s, "}\n");
                sb_indent(s, depth + 1);
                sb_add(s, "}\n");
            } else {
                if (i == 0) sb_add(s, "if (");
                else sb_add(s, "else if (");
                emit_pattern_cond(s, arm->pattern, "__subject", depth + 1);
                sb_add(s, ") {\n");
                emit_pattern_bindings(s, arm->pattern, "__subject", depth + 2);
                if (arm->body && arm->body->tag == NODE_BLOCK)
                    emit_block_body(s, arm->body, depth + 2);
                else if (arm->body) {
                    sb_indent(s, depth + 2);
                    emit_expr(s, arm->body, depth + 2);
                    sb_add(s, ";\n");
                }
                sb_indent(s, depth + 1);
                sb_add(s, "}\n");
            }
        }
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    }
    case NODE_TRY: {
        /* try/catch/finally -> setjmp/longjmp with proper unwinding */
        sb_indent(s, depth);
        sb_add(s, "{\n");

        /* save defer stack position for unwinding */
        sb_indent(s, depth + 1);
        sb_add(s, "int __saved_defer_top = __xs_defer_top;\n");

        sb_indent(s, depth + 1);
        sb_add(s, "jmp_buf __jmpbuf;\n");
        sb_indent(s, depth + 1);
        sb_add(s, "xs_val __exception = XS_NULL;\n");
        sb_indent(s, depth + 1);
        sb_add(s, "int __caught = 0;\n");

        sb_indent(s, depth + 1);
        sb_add(s, "if (setjmp(__jmpbuf) == 0) {\n");
        sb_indent(s, depth + 2);
        sb_add(s, "xs_push_handler(&__jmpbuf);\n");
        if (n->try_.body)
            emit_block_body(s, n->try_.body, depth + 2);
        sb_indent(s, depth + 2);
        sb_add(s, "xs_pop_handler();\n");
        sb_indent(s, depth + 2);
        sb_add(s, "__caught = 1; /* normal completion */\n");
        sb_indent(s, depth + 1);
        sb_add(s, "}");

        if (n->try_.catch_arms.len > 0) {
            sb_add(s, " else {\n");
            sb_indent(s, depth + 2);
            sb_add(s, "/* handler already popped by xs_throw/longjmp */\n");
            sb_indent(s, depth + 2);
            sb_add(s, "__exception = xs_get_exception();\n");
            sb_indent(s, depth + 2);
            sb_add(s, "int __exc_tag = xs_get_exception_tag();\n");
            sb_indent(s, depth + 2);
            sb_add(s, "(void)__exc_tag; /* available for typed catch */\n");
            sb_indent(s, depth + 2);
            sb_add(s, "__caught = 1;\n");

            /* Emit catch arms with pattern matching */
            int has_multi = n->try_.catch_arms.len > 1;
            for (int i = 0; i < n->try_.catch_arms.len; i++) {
                MatchArm *arm = &n->try_.catch_arms.items[i];
                if (has_multi && arm->pattern) {
                    sb_indent(s, depth + 2);
                    if (i == 0) sb_add(s, "if (");
                    else sb_add(s, "else if (");
                    /* Check if pattern has a type constraint by checking tag */
                    emit_pattern_cond(s, arm->pattern, "__exception", depth + 2);
                    if (arm->guard) {
                        sb_add(s, " && xs_truthy(");
                        emit_expr(s, arm->guard, depth + 2);
                        sb_addc(s, ')');
                    }
                    sb_add(s, ") {\n");
                    emit_pattern_bindings(s, arm->pattern, "__exception", depth + 3);
                    if (arm->body && arm->body->tag == NODE_BLOCK) {
                        emit_block_body(s, arm->body, depth + 3);
                    } else if (arm->body) {
                        sb_indent(s, depth + 3);
                        emit_expr(s, arm->body, depth + 3);
                        sb_add(s, ";\n");
                    }
                    sb_indent(s, depth + 2);
                    sb_add(s, "}\n");
                } else {
                    /* Single catch arm or wildcard - bind and execute directly */
                    emit_pattern_bindings(s, arm->pattern, "__exception", depth + 2);
                    if (arm->body && arm->body->tag == NODE_BLOCK) {
                        emit_block_body(s, arm->body, depth + 2);
                    } else if (arm->body) {
                        sb_indent(s, depth + 2);
                        emit_expr(s, arm->body, depth + 2);
                        sb_add(s, ";\n");
                    }
                }
            }
            sb_indent(s, depth + 1);
            sb_add(s, "}\n");
        } else {
            sb_add(s, " else {\n");
            sb_indent(s, depth + 2);
            sb_add(s, "/* no catch arms: rethrow after finally */\n");
            sb_indent(s, depth + 2);
            sb_add(s, "__exception = xs_get_exception();\n");
            sb_indent(s, depth + 1);
            sb_add(s, "}\n");
        }

        /* finally block: always runs regardless of exception */
        if (n->try_.finally_block) {
            sb_indent(s, depth + 1);
            sb_add(s, "/* finally block - always executes */\n");
            sb_indent(s, depth + 1);
            sb_add(s, "{\n");
            emit_block_body(s, n->try_.finally_block, depth + 2);
            sb_indent(s, depth + 1);
            sb_add(s, "}\n");
        }

        /* run defers accumulated in this try scope */
        sb_indent(s, depth + 1);
        sb_add(s, "xs_run_defers(__saved_defer_top);\n");

        /* if exception was not caught, rethrow */
        if (n->try_.catch_arms.len == 0) {
            sb_indent(s, depth + 1);
            sb_add(s, "if (!__caught) xs_rethrow();\n");
        }

        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    }
    case NODE_THROW:
        sb_indent(s, depth);
        sb_add(s, "xs_throw(");
        emit_expr(s, n->throw_.value, depth);
        sb_add(s, ");\n");
        break;
    case NODE_DEFER: {
        /* defer: register a cleanup function on the defer stack.
           We emit an inline block and register it via xs_push_defer.
           For simplicity, we use a static nested function (GCC extension)
           or emit the body inline with a saved marker. */
        sb_indent(s, depth);
        sb_add(s, "{ /* defer registration */\n");
        sb_indent(s, depth + 1);
        sb_printf(s, "auto void __defer_fn_%d(void);\n", defer_label_counter);
        sb_indent(s, depth + 1);
        sb_printf(s, "void __defer_fn_%d(void) {\n", defer_label_counter);
        if (n->defer_.body) {
            if (n->defer_.body->tag == NODE_BLOCK) {
                emit_block_body(s, n->defer_.body, depth + 2);
            } else {
                sb_indent(s, depth + 2);
                emit_expr(s, n->defer_.body, depth + 2);
                sb_add(s, ";\n");
            }
        }
        sb_indent(s, depth + 1);
        sb_add(s, "}\n");
        sb_indent(s, depth + 1);
        sb_printf(s, "xs_push_defer(__defer_fn_%d);\n", defer_label_counter);
        defer_label_counter++;
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    }
    case NODE_YIELD:
        sb_indent(s, depth);
        sb_add(s, "fprintf(stderr, \"xs: yield not supported in C target\\n\"); exit(1);\n");
        break;
    case NODE_STRUCT_DECL: {
        sb_indent(s, depth);
        sb_printf(s, "typedef struct %s {\n", n->struct_decl.name);
        for (int i = 0; i < n->struct_decl.fields.len; i++) {
            sb_indent(s, depth + 1);
            sb_printf(s, "xs_val %s;\n", n->struct_decl.fields.items[i].key);
        }
        sb_indent(s, depth);
        sb_printf(s, "} %s;\n\n", n->struct_decl.name);
        break;
    }
    case NODE_ENUM_DECL: {
        /* Enum -> C enum for tag + optional data struct */
        sb_indent(s, depth);
        sb_printf(s, "typedef enum {\n");
        for (int i = 0; i < n->enum_decl.variants.len; i++) {
            EnumVariant *v = &n->enum_decl.variants.items[i];
            sb_indent(s, depth + 1);
            sb_printf(s, "%s_%s", n->enum_decl.name, v->name);
            if (i < n->enum_decl.variants.len - 1) sb_addc(s, ',');
            sb_addc(s, '\n');
        }
        sb_indent(s, depth);
        sb_printf(s, "} %s_Tag;\n\n", n->enum_decl.name);
        /* Also emit a tagged union struct for data-carrying variants */
        int has_data = 0;
        for (int i = 0; i < n->enum_decl.variants.len; i++) {
            if (n->enum_decl.variants.items[i].fields.len > 0) { has_data = 1; break; }
        }
        if (has_data) {
            sb_indent(s, depth);
            sb_printf(s, "typedef struct {\n");
            sb_indent(s, depth + 1);
            sb_printf(s, "%s_Tag tag;\n", n->enum_decl.name);
            sb_indent(s, depth + 1);
            sb_add(s, "union {\n");
            for (int i = 0; i < n->enum_decl.variants.len; i++) {
                EnumVariant *v = &n->enum_decl.variants.items[i];
                if (v->fields.len > 0) {
                    sb_indent(s, depth + 2);
                    sb_add(s, "struct {\n");
                    for (int j = 0; j < v->fields.len; j++) {
                        sb_indent(s, depth + 3);
                        sb_printf(s, "xs_val f%d;\n", j);
                    }
                    sb_indent(s, depth + 2);
                    sb_printf(s, "} %s;\n", v->name);
                }
            }
            sb_indent(s, depth + 1);
            sb_add(s, "};\n");
            sb_indent(s, depth);
            sb_printf(s, "} %s;\n\n", n->enum_decl.name);
        }
        break;
    }
    case NODE_IMPL_DECL: {
        sb_indent(s, depth);
        sb_printf(s, "/* impl %s", n->impl_decl.type_name);
        if (n->impl_decl.trait_name) sb_printf(s, " : %s", n->impl_decl.trait_name);
        sb_add(s, " */\n");
        for (int i = 0; i < n->impl_decl.members.len; i++) {
            Node *m = n->impl_decl.members.items[i];
            if (m && m->tag == NODE_FN_DECL && m->fn_decl.name) {
                /* Rename method to TypeName_methodName(xs_val self, ...) */
                sb_indent(s, depth);
                sb_printf(s, "static xs_val %s_%s(xs_val self",
                          n->impl_decl.type_name, m->fn_decl.name);
                for (int p = 0; p < m->fn_decl.params.len; p++) {
                    const char *pname = m->fn_decl.params.items[p].name;
                    if (pname && strcmp(pname, "self") == 0) continue;
                    sb_add(s, ", xs_val ");
                    emit_safe_name(s, pname ? pname : "_");
                }
                sb_add(s, ") {\n");
                in_method_body = 1;
                if (m->fn_decl.body && m->fn_decl.body->tag == NODE_BLOCK) {
                    for (int si = 0; si < m->fn_decl.body->block.stmts.len; si++)
                        emit_stmt(s, m->fn_decl.body->block.stmts.items[si], depth + 1);
                    if (m->fn_decl.body->block.expr) {
                        sb_indent(s, depth + 1);
                        sb_add(s, "return ");
                        emit_expr(s, m->fn_decl.body->block.expr, depth + 1);
                        sb_add(s, ";\n");
                    } else {
                        sb_indent(s, depth + 1);
                        sb_add(s, "return XS_NULL;\n");
                    }
                } else {
                    sb_indent(s, depth + 1);
                    sb_add(s, "return XS_NULL;\n");
                }
                in_method_body = 0;
                sb_indent(s, depth);
                sb_add(s, "}\n\n");
            } else if (m) {
                emit_stmt(s, m, depth);
            }
        }
        break;
    }
    case NODE_TRAIT_DECL: {
        /* Trait -> vtable typedef */
        sb_indent(s, depth);
        sb_printf(s, "/* trait %s */\n", n->trait_decl.name);
        sb_indent(s, depth);
        sb_printf(s, "typedef struct %s_vtable {\n", n->trait_decl.name);
        for (int i = 0; i < n->trait_decl.n_methods; i++) {
            sb_indent(s, depth + 1);
            sb_printf(s, "xs_val (*%s)(void *self);\n", n->trait_decl.method_names[i]);
        }
        sb_indent(s, depth);
        sb_printf(s, "} %s_vtable;\n\n", n->trait_decl.name);
        break;
    }
    case NODE_TYPE_ALIAS:
        sb_indent(s, depth);
        sb_printf(s, "/* type %s = %s */\n", n->type_alias.name,
                  n->type_alias.target ? n->type_alias.target : "?");
        break;
    case NODE_IMPORT:
    case NODE_USE:
        sb_indent(s, depth);
        if (n->tag == NODE_USE) {
            sb_add(s, "/* use ");
            for (int i = 0; i < n->import.nparts; i++) {
                if (i) sb_addc(s, '/');
                sb_add(s, n->import.path[i]);
            }
            sb_add(s, " (not supported in C target) */\n");
        } else {
            sb_add(s, "/* import ");
            for (int i = 0; i < n->import.nparts; i++) {
                if (i) sb_addc(s, '.');
                sb_add(s, n->import.path[i]);
            }
            sb_add(s, " */\n");
        }
        break;
    case NODE_MODULE_DECL:
        sb_indent(s, depth);
        sb_printf(s, "/* module %s */\n", n->module_decl.name);
        for (int i = 0; i < n->module_decl.body.len; i++)
            emit_stmt(s, n->module_decl.body.items[i], depth);
        break;
    case NODE_CLASS_DECL: {
        /* Class -> struct with vtable pointer */
        sb_indent(s, depth);
        sb_printf(s, "typedef struct %s {\n", n->class_decl.name);
        sb_indent(s, depth + 1);
        sb_add(s, "void *__vtable;\n");
        sb_indent(s, depth + 1);
        sb_add(s, "int __tag;\n");
        sb_indent(s, depth);
        sb_printf(s, "} %s;\n\n", n->class_decl.name);
        /* emit methods as standalone functions */
        for (int i = 0; i < n->class_decl.members.len; i++) {
            Node *m = n->class_decl.members.items[i];
            if (m && m->tag == NODE_FN_DECL) {
                sb_indent(s, depth);
                sb_add(s, "xs_val ");
                sb_add(s, n->class_decl.name);
                sb_addc(s, '_');
                sb_add(s, m->fn_decl.name);
                sb_addc(s, '(');
                sb_printf(s, "%s *self", n->class_decl.name);
                for (int p = 0; p < m->fn_decl.params.len; p++) {
                    sb_add(s, ", xs_val ");
                    if (m->fn_decl.params.items[p].name)
                        sb_add(s, m->fn_decl.params.items[p].name);
                    else
                        sb_add(s, "_");
                }
                sb_add(s, ") {\n");
                if (m->fn_decl.body && m->fn_decl.body->tag == NODE_BLOCK) {
                    emit_block_body(s, m->fn_decl.body, depth + 1);
                    if (m->fn_decl.body->block.expr) {
                        sb_indent(s, depth + 1);
                        sb_add(s, "return ");
                        emit_expr(s, m->fn_decl.body->block.expr, depth + 1);
                        sb_add(s, ";\n");
                    }
                }
                sb_indent(s, depth + 1);
                sb_add(s, "return XS_NULL;\n");
                sb_indent(s, depth);
                sb_add(s, "}\n\n");
            }
        }
        break;
    }
    case NODE_EFFECT_DECL:
        sb_indent(s, depth);
        sb_printf(s, "/* effect %s */\n", n->effect_decl.name);
        sb_indent(s, depth);
        sb_printf(s, "typedef struct {\n");
        sb_indent(s, depth + 1);
        sb_add(s, "int op;\n");
        sb_indent(s, depth);
        sb_printf(s, "} %s_effect;\n\n", n->effect_decl.name);
        break;
    case NODE_HANDLE:
        sb_indent(s, depth);
        sb_add(s, "/* handle expression */\n");
        sb_indent(s, depth);
        sb_add(s, "{\n");
        if (n->handle.expr) {
            sb_indent(s, depth + 1);
            emit_expr(s, n->handle.expr, depth + 1);
            sb_add(s, ";\n");
        }
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_NURSERY:
        sb_indent(s, depth);
        sb_add(s, "/* nursery (no native thread support) */\n");
        sb_indent(s, depth);
        sb_add(s, "{\n");
        if (n->nursery_.body) emit_block_body(s, n->nursery_.body, depth + 1);
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_ASSIGN:
        sb_indent(s, depth);
        /* field assignment on non-self objects → xs_map_put
           (actor self uses ->, impl self uses xs_index so also needs map_put) */
        if (n->assign.target && n->assign.target->tag == NODE_INDEX) {
            /* index assignment: obj[key] = val → xs_map_put */
            sb_add(s, "xs_map_put(&");
            emit_expr(s, n->assign.target->index.obj, depth);
            sb_add(s, ", ");
            emit_expr(s, n->assign.target->index.index, depth);
            sb_add(s, ", ");
            emit_expr(s, n->assign.value, depth);
            sb_add(s, ");\n");
        } else if (n->assign.target && n->assign.target->tag == NODE_FIELD &&
            !(n_actor_fields > 0 && in_method_body &&
              n->assign.target->field.obj &&
              n->assign.target->field.obj->tag == NODE_IDENT &&
              strcmp(n->assign.target->field.obj->ident.name, "self") == 0)) {
            sb_add(s, "xs_map_put(&");
            emit_expr(s, n->assign.target->field.obj, depth);
            sb_printf(s, ", XS_STR(\"%s\"), ", n->assign.target->field.name);
            emit_expr(s, n->assign.value, depth);
            sb_add(s, ");\n");
        } else {
            emit_expr(s, n->assign.target, depth);
            sb_addc(s, ' ');
            sb_add(s, n->assign.op);
            sb_addc(s, ' ');
            emit_expr(s, n->assign.value, depth);
            sb_add(s, ";\n");
        }
        break;
    case NODE_EXPR_STMT: {
        Node *inner = n->expr_stmt.expr;
        if (inner && (inner->tag == NODE_IF || inner->tag == NODE_MATCH ||
                      inner->tag == NODE_FOR || inner->tag == NODE_WHILE ||
                      inner->tag == NODE_LOOP || inner->tag == NODE_TRY ||
                      inner->tag == NODE_BLOCK || inner->tag == NODE_NURSERY ||
                      inner->tag == NODE_SPAWN || inner->tag == NODE_ACTOR_DECL ||
                      inner->tag == NODE_RETURN)) {
            emit_stmt(s, inner, depth);
        } else {
            sb_indent(s, depth);
            emit_expr(s, inner, depth);
            sb_add(s, ";\n");
        }
        break;
    }
    case NODE_BLOCK:
        sb_indent(s, depth);
        sb_add(s, "{\n");
        emit_block_body(s, n, depth + 1);
        if (n->block.expr) {
            sb_indent(s, depth + 1);
            emit_expr(s, n->block.expr, depth + 1);
            sb_add(s, ";\n");
        }
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_SPAWN: {
        /* spawn as statement */
        Node *se = n->spawn_.expr;
        if (se && se->tag == NODE_IDENT && find_actor(se->ident.name)) {
            /* spawn ActorName -> not useful as a standalone stmt, skip */
            sb_indent(s, depth);
            sb_add(s, "/* spawn actor (see let binding) */\n");
        } else if (se && se->tag == NODE_BLOCK) {
            /* spawn { block } as statement -> just execute the block */
            sb_indent(s, depth);
            sb_add(s, "{\n");
            emit_block_body(s, se, depth + 1);
            sb_indent(s, depth);
            sb_add(s, "}\n");
        } else {
            sb_indent(s, depth);
            emit_expr(s, n, depth);
            sb_add(s, ";\n");
        }
        break;
    }
    case NODE_ACTOR_DECL:
        /* actor decl handled at file scope by emit_actor_decl */
        break;
    case NODE_SEND_EXPR:
        sb_indent(s, depth);
        emit_expr(s, n, depth);
        sb_add(s, ";\n");
        break;
    case NODE_PERFORM:
    case NODE_RESUME:
    case NODE_AWAIT:
        sb_indent(s, depth);
        emit_expr(s, n, depth);
        sb_add(s, ";\n");
        break;
    case NODE_INLINE_C:
        sb_indent(s, depth);
        sb_add(s, "/* inline C */\n");
        if (n->inline_c.code) sb_add(s, n->inline_c.code);
        sb_addc(s, '\n');
        break;
    case NODE_TAG_DECL:
        /* Emit as regular function with __block as last param */
        sb_indent(s, depth);
        sb_printf(s, "XsValue %s(", n->tag_decl.name ? n->tag_decl.name : "_tag");
        for (int ti = 0; ti < n->tag_decl.params.len; ti++) {
            Param *pm = &n->tag_decl.params.items[ti];
            sb_printf(s, "XsValue %s, ", pm->name ? pm->name : "_");
        }
        sb_add(s, "XsValue __block) {\n");
        if (n->tag_decl.body) emit_stmt(s, n->tag_decl.body, depth + 1);
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    default:
        /* emit as expression statement for any unhandled node */
        sb_indent(s, depth);
        emit_expr(s, n, depth);
        sb_add(s, ";\n");
        break;
    }
}

/* public entry point */
char *transpile_c(Node *program, const char *filename) {
    SB s;
    sb_init(&s);
    seen_main = 0;
    defer_label_counter = 0;

    /* preamble */
    sb_add(&s, "/* Generated by xs transpile --target c */\n");
    if (filename) sb_printf(&s, "/* Source: %s */\n", filename);
    sb_add(&s,
        "#define _POSIX_C_SOURCE 200809L\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdint.h>\n"
        "#include <stdarg.h>\n"
        "#include <math.h>\n"
        "#include <setjmp.h>\n\n"
        "/* XS runtime types */\n"
        "typedef struct xs_val {\n"
        "    int tag; /* 0=int, 1=float, 2=str, 3=bool, 4=null, 5=array, 6=map */\n"
        "    union { int64_t i; double f; char *s; int b; void *p; };\n"
        "} xs_val;\n\n"
        "#define XS_INT(v)   ((xs_val){.tag=0, .i=(v)})\n"
        "#define XS_FLOAT(v) ((xs_val){.tag=1, .f=(v)})\n"
        "#define XS_STR(v)   ((xs_val){.tag=2, .s=(char*)(v)})\n"
        "#define XS_BOOL(v)  ((xs_val){.tag=3, .b=(v)})\n"
        "#define XS_NULL     ((xs_val){.tag=4})\n\n"
        "static int xs_truthy(xs_val v) {\n"
        "    switch (v.tag) {\n"
        "        case 0: return v.i != 0;\n"
        "        case 1: return v.f != 0.0;\n"
        "        case 2: return v.s != NULL && v.s[0] != '\\0';\n"
        "        case 3: return v.b;\n"
        "        default: return 0;\n"
        "    }\n"
        "}\n\n"
        "/* forward declare heap types */\n"
        "typedef struct { xs_val *items; int len; int cap; } xs_arr;\n"
        "typedef struct { char **keys; xs_val *vals; int len; int cap; } xs_hmap;\n\n"
        "static int xs_eq(xs_val a, xs_val b) {\n"
        "    if (a.tag != b.tag) return 0;\n"
        "    switch (a.tag) {\n"
        "        case 0: return a.i == b.i;\n"
        "        case 1: return a.f == b.f;\n"
        "        case 2: return a.s && b.s && strcmp(a.s, b.s) == 0;\n"
        "        case 3: return a.b == b.b;\n"
        "        case 4: return 1;\n"
        "        case 5: if (a.p && b.p) {\n"
        "            xs_arr *aa = (xs_arr*)a.p, *bb = (xs_arr*)b.p;\n"
        "            if (aa->len != bb->len) return 0;\n"
        "            for (int i = 0; i < aa->len; i++)\n"
        "                if (!xs_eq(aa->items[i], bb->items[i])) return 0;\n"
        "            return 1;\n"
        "        } return a.p == b.p;\n"
        "        default: return 0;\n"
        "    }\n"
        "}\n\n"
        "static int xs_cmp(xs_val a, xs_val b) {\n"
        "    if (a.tag == 0 && b.tag == 0) return (a.i > b.i) - (a.i < b.i);\n"
        "    if (a.tag == 1 || b.tag == 1) {\n"
        "        double af = a.tag == 1 ? a.f : (double)a.i;\n"
        "        double bf = b.tag == 1 ? b.f : (double)b.i;\n"
        "        return (af > bf) - (af < bf);\n"
        "    }\n"
        "    if (a.tag == 2 && b.tag == 2) return strcmp(a.s ? a.s : \"\", b.s ? b.s : \"\");\n"
        "    return 0;\n"
        "}\n\n"
        "static xs_val xs_add(xs_val a, xs_val b) {\n"
        "    if (a.tag == 0 && b.tag == 0) return XS_INT(a.i + b.i);\n"
        "    if (a.tag == 1 || b.tag == 1) {\n"
        "        double af = a.tag == 1 ? a.f : (double)a.i;\n"
        "        double bf = b.tag == 1 ? b.f : (double)b.i;\n"
        "        return XS_FLOAT(af + bf);\n"
        "    }\n"
        "    return XS_INT(a.i + b.i);\n"
        "}\n\n"
        "static xs_val xs_sub(xs_val a, xs_val b) {\n"
        "    if (a.tag == 1 || b.tag == 1) {\n"
        "        double af = a.tag == 1 ? a.f : (double)a.i;\n"
        "        double bf = b.tag == 1 ? b.f : (double)b.i;\n"
        "        return XS_FLOAT(af - bf);\n"
        "    }\n"
        "    return XS_INT(a.i - b.i);\n"
        "}\n\n"
        "static xs_val xs_mul(xs_val a, xs_val b) {\n"
        "    if (a.tag == 1 || b.tag == 1) {\n"
        "        double af = a.tag == 1 ? a.f : (double)a.i;\n"
        "        double bf = b.tag == 1 ? b.f : (double)b.i;\n"
        "        return XS_FLOAT(af * bf);\n"
        "    }\n"
        "    return XS_INT(a.i * b.i);\n"
        "}\n\n"
        "static xs_val xs_div(xs_val a, xs_val b) {\n"
        "    if (a.tag == 1 || b.tag == 1) {\n"
        "        double af = a.tag == 1 ? a.f : (double)a.i;\n"
        "        double bf = b.tag == 1 ? b.f : (double)b.i;\n"
        "        return XS_FLOAT(af / bf);\n"
        "    }\n"
        "    if (b.i == 0) return XS_NULL;\n"
        "    return XS_INT(a.i / b.i);\n"
        "}\n\n"
        "static xs_val xs_mod(xs_val a, xs_val b) {\n"
        "    if (b.i == 0) return XS_NULL;\n"
        "    return XS_INT(a.i % b.i);\n"
        "}\n\n"
        "static xs_val xs_idiv(xs_val a, xs_val b) {\n"
        "    if (b.i == 0) return XS_NULL;\n"
        "    return XS_INT(a.i / b.i);\n"
        "}\n\n"
        "static xs_val xs_neg(xs_val a) {\n"
        "    if (a.tag == 1) return XS_FLOAT(-a.f);\n"
        "    return XS_INT(-a.i);\n"
        "}\n\n"
        "static xs_val xs_strcat(xs_val a, xs_val b) {\n"
        "    const char *sa = a.tag == 2 ? a.s : \"\";\n"
        "    const char *sb = b.tag == 2 ? b.s : \"\";\n"
        "    size_t la = strlen(sa), lb = strlen(sb);\n"
        "    char *r = (char*)malloc(la + lb + 1);\n"
        "    memcpy(r, sa, la); memcpy(r + la, sb, lb); r[la + lb] = 0;\n"
        "    return XS_STR(r);\n"
        "}\n\n"
        "static const char *xs_to_str(xs_val v);\n"
        "/* uses rotating buffers to avoid clobbering on recursive/nested calls */\n"
        "static const char *xs_to_str(xs_val v) {\n"
        "    static char bufs[8][4096];\n"
        "    static int buf_idx = 0;\n"
        "    char *buf = bufs[buf_idx++ & 7];\n"
        "    switch (v.tag) {\n"
        "        case 0: snprintf(buf, 4096, \"%lld\", (long long)v.i); return buf;\n"
        "        case 1: snprintf(buf, 4096, \"%g\", v.f); return buf;\n"
        "        case 2: return v.s ? v.s : \"null\";\n"
        "        case 3: return v.b ? \"true\" : \"false\";\n"
        "        case 5: if (v.p) {\n"
        "            xs_arr *a = (xs_arr*)v.p;\n"
        "            int pos = 0;\n"
        "            pos += snprintf(buf + pos, 4096 - pos, \"[\");\n"
        "            for (int i = 0; i < a->len && pos < 4096 - 32; i++) {\n"
        "                if (i) pos += snprintf(buf + pos, 4096 - pos, \", \");\n"
        "                if (a->items[i].tag == 2) pos += snprintf(buf + pos, 4096 - pos, \"%s\", a->items[i].s ? a->items[i].s : \"null\");\n"
        "                else pos += snprintf(buf + pos, 4096 - pos, \"%s\", xs_to_str(a->items[i]));\n"
        "            }\n"
        "            snprintf(buf + pos, 4096 - pos, \"]\");\n"
        "            return buf;\n"
        "        } return \"[]\";\n"
        "        case 6: if (v.p) {\n"
        "            xs_hmap *m = (xs_hmap*)v.p;\n"
        "            int pos = 0;\n"
        "            pos += snprintf(buf + pos, 4096 - pos, \"{\");\n"
        "            for (int i = 0; i < m->len && pos < 4096 - 64; i++) {\n"
        "                if (i) pos += snprintf(buf + pos, 4096 - pos, \", \");\n"
        "                pos += snprintf(buf + pos, 4096 - pos, \"%s: %s\", m->keys[i], xs_to_str(m->vals[i]));\n"
        "            }\n"
        "            snprintf(buf + pos, 4096 - pos, \"}\");\n"
        "            return buf;\n"
        "        } return \"{}\";\n"
        "        default: return \"null\";\n"
        "    }\n"
        "}\n\n"
        "static xs_val xs_sprintf(const char *fmt, ...) {\n"
        "    char buf[4096];\n"
        "    va_list ap; va_start(ap, fmt);\n"
        "    vsnprintf(buf, sizeof buf, fmt, ap);\n"
        "    va_end(ap);\n"
        "    return XS_STR(strdup(buf));\n"
        "}\n\n"
        "static xs_val xs_println(xs_val v) {\n"
        "    switch(v.tag) {\n"
        "        case 0: printf(\"%lld\\n\", (long long)v.i); break;\n"
        "        case 1: printf(\"%g\\n\", v.f); break;\n"
        "        case 2: printf(\"%s\\n\", v.s ? v.s : \"null\"); break;\n"
        "        case 3: printf(\"%s\\n\", v.b ? \"true\" : \"false\"); break;\n"
        "        default: printf(\"null\\n\");\n"
        "    }\n"
        "    return (xs_val){.tag=4};\n"
        "}\n\n"
        "static xs_val xs_print(xs_val v) {\n"
        "    switch(v.tag) {\n"
        "        case 0: printf(\"%lld\", (long long)v.i); break;\n"
        "        case 1: printf(\"%g\", v.f); break;\n"
        "        case 2: printf(\"%s\", v.s ? v.s : \"null\"); break;\n"
        "        case 3: printf(\"%s\", v.b ? \"true\" : \"false\"); break;\n"
        "        default: printf(\"null\");\n"
        "    }\n"
        "    return (xs_val){.tag=4};\n"
        "}\n\n"
        "/* exception handling runtime */\n"
        "#define XS_MAX_HANDLERS 64\n"
        "static jmp_buf *__xs_handlers[XS_MAX_HANDLERS];\n"
        "static int __xs_handler_top = 0;\n"
        "static xs_val __xs_exception = {.tag=4};\n"
        "static int __xs_exception_tag = 0; /* 0=none, 1=string, 2=int, 3=float, 4=bool, 5=array, 6=map */\n"
        "static int __xs_in_catch = 0; /* nonzero when executing inside a catch block */\n\n"

        "/* call-stack tracing */\n"
        "#define XS_MAX_STACK 256\n"
        "static const char *__xs_call_stack[XS_MAX_STACK];\n"
        "static int __xs_stack_top = 0;\n"
        "static void xs_push_frame(const char *name) {\n"
        "    if (__xs_stack_top < XS_MAX_STACK) __xs_call_stack[__xs_stack_top++] = name;\n"
        "}\n"
        "static void xs_pop_frame(void) { if (__xs_stack_top > 0) __xs_stack_top--; }\n"
        "static void xs_print_stack_trace(void) {\n"
        "    fprintf(stderr, \"Stack trace:\\n\");\n"
        "    for (int i = __xs_stack_top - 1; i >= 0; i--)\n"
        "        fprintf(stderr, \"  at %s\\n\", __xs_call_stack[i]);\n"
        "}\n\n"

        "/* defer stack */\n"
        "typedef void (*xs_defer_fn)(void);\n"
        "#define XS_MAX_DEFERS 256\n"
        "static xs_defer_fn __xs_defers[XS_MAX_DEFERS];\n"
        "static int __xs_defer_top = 0;\n"
        "static void xs_push_defer(xs_defer_fn fn) {\n"
        "    if (__xs_defer_top < XS_MAX_DEFERS) __xs_defers[__xs_defer_top++] = fn;\n"
        "}\n"
        "static void xs_run_defers(int from) {\n"
        "    for (int i = __xs_defer_top - 1; i >= from; i--) __xs_defers[i]();\n"
        "    __xs_defer_top = from;\n"
        "}\n\n"

        "/* finally handler stack */\n"
        "typedef void (*xs_finally_fn)(void);\n"
        "#define XS_MAX_FINALLY 64\n"
        "static xs_finally_fn __xs_finally_handlers[XS_MAX_FINALLY];\n"
        "static int __xs_finally_top = 0;\n"
        "static void xs_push_finally(xs_finally_fn fn) {\n"
        "    if (__xs_finally_top < XS_MAX_FINALLY) __xs_finally_handlers[__xs_finally_top++] = fn;\n"
        "}\n"
        "static void xs_pop_finally(void) { if (__xs_finally_top > 0) __xs_finally_top--; }\n\n"

        "/* handler push/pop */\n"
        "static void xs_push_handler(jmp_buf *jb) {\n"
        "    if (__xs_handler_top < XS_MAX_HANDLERS) __xs_handlers[__xs_handler_top++] = jb;\n"
        "}\n"
        "static void xs_pop_handler(void) { if (__xs_handler_top > 0) __xs_handler_top--; }\n\n"

        "/* throw / rethrow */\n"
        "static void xs_throw(xs_val v) {\n"
        "    __xs_exception = v;\n"
        "    /* derive exception type tag from xs_val tag */\n"
        "    switch (v.tag) {\n"
        "        case 0: __xs_exception_tag = 2; break; /* int */\n"
        "        case 1: __xs_exception_tag = 3; break; /* float */\n"
        "        case 2: __xs_exception_tag = 1; break; /* string */\n"
        "        case 3: __xs_exception_tag = 4; break; /* bool */\n"
        "        case 5: __xs_exception_tag = 5; break; /* array */\n"
        "        case 6: __xs_exception_tag = 6; break; /* map */\n"
        "        default: __xs_exception_tag = 0; break;\n"
        "    }\n"
        "    /* run defers during stack unwinding */\n"
        "    xs_run_defers(0);\n"
        "    if (__xs_handler_top > 0) longjmp(*__xs_handlers[--__xs_handler_top], 1);\n"
        "    fprintf(stderr, \"unhandled exception: %s\\n\", xs_to_str(v));\n"
        "    xs_print_stack_trace();\n"
        "    abort();\n"
        "}\n"
        "static void xs_rethrow(void) {\n"
        "    /* re-throw the current exception to the next outer handler */\n"
        "    if (__xs_handler_top > 0) longjmp(*__xs_handlers[--__xs_handler_top], 1);\n"
        "    fprintf(stderr, \"unhandled exception (rethrown): %s\\n\", xs_to_str(__xs_exception));\n"
        "    xs_print_stack_trace();\n"
        "    abort();\n"
        "}\n"
        "static xs_val xs_get_exception(void) { return __xs_exception; }\n"
        "static int xs_get_exception_tag(void) { return __xs_exception_tag; }\n\n"
        "/* array/map constructors */\n"
        "static xs_val xs_array(int n, ...) {\n"
        "    xs_arr *a = (xs_arr*)malloc(sizeof(xs_arr));\n"
        "    a->cap = n > 4 ? n : 4;\n"
        "    a->items = (xs_val*)malloc(sizeof(xs_val) * a->cap);\n"
        "    a->len = n;\n"
        "    va_list ap; va_start(ap, n);\n"
        "    for (int i = 0; i < n; i++) a->items[i] = va_arg(ap, xs_val);\n"
        "    va_end(ap);\n"
        "    return (xs_val){.tag=5, .p=a};\n"
        "}\n\n"
        "static void xs_arr_push(xs_val arr, xs_val v) {\n"
        "    if (arr.tag != 5 || !arr.p) return;\n"
        "    xs_arr *a = (xs_arr*)arr.p;\n"
        "    if (a->len >= a->cap) {\n"
        "        a->cap = a->cap * 2;\n"
        "        a->items = (xs_val*)realloc(a->items, sizeof(xs_val) * a->cap);\n"
        "    }\n"
        "    a->items[a->len++] = v;\n"
        "}\n\n"
        "static xs_val xs_index(xs_val arr, xs_val idx) {\n"
        "    if (arr.tag == 5 && arr.p) {\n"
        "        xs_arr *a = (xs_arr*)arr.p;\n"
        "        int64_t i = idx.tag == 0 ? idx.i : 0;\n"
        "        if (i < 0) i += a->len;\n"
        "        if (i >= 0 && i < a->len) return a->items[i];\n"
        "        return XS_NULL;\n"
        "    }\n"
        "    if (arr.tag == 2 && arr.s) {\n"
        "        int64_t i = idx.tag == 0 ? idx.i : 0;\n"
        "        int slen = (int)strlen(arr.s);\n"
        "        if (i < 0) i += slen;\n"
        "        if (i >= 0 && i < slen) {\n"
        "            char *c = (char*)malloc(2);\n"
        "            c[0] = arr.s[i]; c[1] = 0;\n"
        "            return XS_STR(c);\n"
        "        }\n"
        "        return XS_NULL;\n"
        "    }\n"
        "    if (arr.tag == 6 && arr.p) {\n"
        "        xs_hmap *m = (xs_hmap*)arr.p;\n"
        "        const char *k = idx.tag == 2 ? idx.s : xs_to_str(idx);\n"
        "        for (int i = 0; i < m->len; i++) {\n"
        "            if (strcmp(m->keys[i], k) == 0) return m->vals[i];\n"
        "        }\n"
        "        return XS_NULL;\n"
        "    }\n"
        "    return XS_NULL;\n"
        "}\n\n"
        "static xs_val xs_range(xs_val start, xs_val end, int inclusive) {\n"
        "    int64_t s = start.tag == 0 ? start.i : 0;\n"
        "    int64_t e = end.tag == 0 ? end.i : 0;\n"
        "    if (inclusive) e += 1;\n"
        "    int64_t step = s <= e ? 1 : -1;\n"
        "    int64_t count = (e - s) * step;\n"
        "    if (count < 0) count = 0;\n"
        "    xs_arr *a = (xs_arr*)malloc(sizeof(xs_arr));\n"
        "    a->cap = (int)(count > 4 ? count : 4);\n"
        "    a->items = (xs_val*)malloc(sizeof(xs_val) * a->cap);\n"
        "    a->len = 0;\n"
        "    for (int64_t i = s; step > 0 ? i < e : i > e; i += step) {\n"
        "        if (a->len >= a->cap) {\n"
        "            a->cap *= 2;\n"
        "            a->items = (xs_val*)realloc(a->items, sizeof(xs_val) * a->cap);\n"
        "        }\n"
        "        a->items[a->len++] = XS_INT(i);\n"
        "    }\n"
        "    return (xs_val){.tag=5, .p=a};\n"
        "}\n\n"
        "static xs_val xs_iter(xs_val v) {\n"
        "    if (v.tag == 5 && v.p) {\n"
        "        xs_arr *state = (xs_arr*)malloc(sizeof(xs_arr));\n"
        "        state->cap = 2; state->len = 2;\n"
        "        state->items = (xs_val*)malloc(sizeof(xs_val) * 2);\n"
        "        state->items[0] = v;\n"
        "        state->items[1] = XS_INT(0);\n"
        "        return (xs_val){.tag=5, .p=state};\n"
        "    }\n"
        "    return v;\n"
        "}\n\n"
        "static int xs_iter_next(xs_val *iter, xs_val *out) {\n"
        "    if (iter->tag != 5 || !iter->p) return 0;\n"
        "    xs_arr *state = (xs_arr*)iter->p;\n"
        "    if (state->len < 2) return 0;\n"
        "    xs_val src = state->items[0];\n"
        "    int64_t idx = state->items[1].i;\n"
        "    if (src.tag == 5 && src.p) {\n"
        "        xs_arr *a = (xs_arr*)src.p;\n"
        "        if (idx >= a->len) return 0;\n"
        "        *out = a->items[idx];\n"
        "        state->items[1] = XS_INT(idx + 1);\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n\n"
        "static xs_val xs_map(int n, ...) {\n"
        "    xs_hmap *m = (xs_hmap*)malloc(sizeof(xs_hmap));\n"
        "    m->cap = n > 4 ? n : 4;\n"
        "    m->keys = (char**)malloc(sizeof(char*) * m->cap);\n"
        "    m->vals = (xs_val*)malloc(sizeof(xs_val) * m->cap);\n"
        "    m->len = 0;\n"
        "    va_list ap; va_start(ap, n);\n"
        "    for (int i = 0; i < n; i++) {\n"
        "        xs_val k = va_arg(ap, xs_val);\n"
        "        xs_val v = va_arg(ap, xs_val);\n"
        "        const char *ks = k.tag == 2 ? k.s : xs_to_str(k);\n"
        "        m->keys[m->len] = strdup(ks);\n"
        "        m->vals[m->len] = v;\n"
        "        m->len++;\n"
        "    }\n"
        "    va_end(ap);\n"
        "    return (xs_val){.tag=6, .p=m};\n"
        "}\n\n"
        "static void xs_map_put(xs_val *map, xs_val key, xs_val val) {\n"
        "    if (map->tag != 6 || !map->p) return;\n"
        "    xs_hmap *m = (xs_hmap*)map->p;\n"
        "    const char *ks = key.tag == 2 ? key.s : xs_to_str(key);\n"
        "    for (int i = 0; i < m->len; i++) {\n"
        "        if (strcmp(m->keys[i], ks) == 0) { m->vals[i] = val; return; }\n"
        "    }\n"
        "    if (m->len >= m->cap) {\n"
        "        m->cap *= 2;\n"
        "        m->keys = (char**)realloc(m->keys, sizeof(char*) * m->cap);\n"
        "        m->vals = (xs_val*)realloc(m->vals, sizeof(xs_val) * m->cap);\n"
        "    }\n"
        "    m->keys[m->len] = strdup(ks);\n"
        "    m->vals[m->len] = val;\n"
        "    m->len++;\n"
        "}\n\n"
        "static xs_val xs_map_has(xs_val map, xs_val key) {\n"
        "    if (map.tag != 6 || !map.p) return XS_BOOL(0);\n"
        "    xs_hmap *m = (xs_hmap*)map.p;\n"
        "    const char *ks = key.tag == 2 ? key.s : xs_to_str(key);\n"
        "    for (int i = 0; i < m->len; i++)\n"
        "        if (strcmp(m->keys[i], ks) == 0) return XS_BOOL(1);\n"
        "    return XS_BOOL(0);\n"
        "}\n\n"
        "static xs_val xs_len(xs_val v) {\n"
        "    if (v.tag == 5 && v.p) return XS_INT(((xs_arr*)v.p)->len);\n"
        "    if (v.tag == 6 && v.p) return XS_INT(((xs_hmap*)v.p)->len);\n"
        "    if (v.tag == 2 && v.s) return XS_INT((int64_t)strlen(v.s));\n"
        "    if (v.tag == 7 && v.p) return XS_INT(((xs_arr*)v.p)->len);\n"
        "    return XS_INT(0);\n"
        "}\n\n"
        "/* channel runtime (single-threaded FIFO queue) */\n"
        "static xs_val xs_channel_new(int max_cap) {\n"
        "    xs_arr *ch = (xs_arr*)malloc(sizeof(xs_arr));\n"
        "    ch->cap = max_cap > 0 ? max_cap : 16;\n"
        "    ch->items = (xs_val*)malloc(sizeof(xs_val) * ch->cap);\n"
        "    ch->len = 0;\n"
        "    xs_val v; v.tag = 7; v.p = ch;\n"
        "    /* store max_cap in a side field: use items[cap-1] trick? no, just embed */\n"
        "    /* we'll use a wrapper struct instead */\n"
        "    return v;\n"
        "}\n\n"
        "static int __xs_channel_max_caps[256];\n"
        "static int __xs_channel_count = 0;\n\n"
        "static void xs_channel_send(xs_val ch, xs_val v) {\n"
        "    if (ch.tag != 7 || !ch.p) return;\n"
        "    xs_arr *a = (xs_arr*)ch.p;\n"
        "    if (a->len >= a->cap) {\n"
        "        a->cap *= 2;\n"
        "        a->items = (xs_val*)realloc(a->items, sizeof(xs_val) * a->cap);\n"
        "    }\n"
        "    a->items[a->len++] = v;\n"
        "}\n\n"
        "static xs_val xs_channel_recv(xs_val ch) {\n"
        "    if (ch.tag != 7 || !ch.p) return XS_NULL;\n"
        "    xs_arr *a = (xs_arr*)ch.p;\n"
        "    if (a->len == 0) return XS_NULL;\n"
        "    xs_val v = a->items[0];\n"
        "    memmove(a->items, a->items + 1, sizeof(xs_val) * (a->len - 1));\n"
        "    a->len--;\n"
        "    return v;\n"
        "}\n\n"
        "static xs_val xs_channel_is_empty(xs_val ch) {\n"
        "    if (ch.tag != 7 || !ch.p) return XS_BOOL(1);\n"
        "    return XS_BOOL(((xs_arr*)ch.p)->len == 0);\n"
        "}\n\n"
        "static xs_val xs_channel_is_full(xs_val ch) {\n"
        "    if (ch.tag != 7 || !ch.p) return XS_BOOL(0);\n"
        "    xs_arr *a = (xs_arr*)ch.p;\n"
        "    /* bounded channel: cap was set to max_cap */\n"
        "    return XS_BOOL(a->len >= a->cap);\n"
        "}\n\n"
        "/* assert runtime */\n"
        "static void xs_assert_eq(xs_val a, xs_val b) {\n"
        "    if (!xs_eq(a, b)) {\n"
        "        fprintf(stderr, \"assert_eq failed: %s != %s\\n\", xs_to_str(a), xs_to_str(b));\n"
        "        exit(1);\n"
        "    }\n"
        "}\n\n"
        "static void xs_assert(xs_val cond, xs_val msg) {\n"
        "    if (!xs_truthy(cond)) {\n"
        "        fprintf(stderr, \"assert failed: %s\\n\", msg.tag == 2 ? msg.s : xs_to_str(msg));\n"
        "        exit(1);\n"
        "    }\n"
        "}\n\n"
        "static double xs_to_f64(xs_val v) {\n"
        "    if (v.tag == 1) return v.f;\n"
        "    if (v.tag == 0) return (double)v.i;\n"
        "    return 0.0;\n"
        "}\n\n"
        "static xs_val xs_type(xs_val v) {\n"
        "    static const char *names[] = {\"int\",\"float\",\"str\",\"bool\",\"null\",\"array\",\"map\",\"channel\"};\n"
        "    if (v.tag >= 0 && v.tag < 8) return XS_STR((char*)names[v.tag]);\n"
        "    return XS_STR(\"unknown\");\n"
        "}\n\n"
        "/* closure runtime */\n"
        "typedef struct { xs_val (*fn)(void*, xs_val*, int); void *env; } xs_fn_t;\n\n"
        "static xs_val xs_fn_new(xs_val (*fn)(void*, xs_val*, int), void *env) {\n"
        "    xs_fn_t *f = (xs_fn_t*)malloc(sizeof(xs_fn_t));\n"
        "    f->fn = fn; f->env = env;\n"
        "    return (xs_val){.tag=8, .p=f};\n"
        "}\n\n"
        "static xs_val xs_call(xs_val fn, xs_val *args, int argc) {\n"
        "    if (fn.tag == 8 && fn.p) {\n"
        "        xs_fn_t *f = (xs_fn_t*)fn.p;\n"
        "        return f->fn(f->env, args, argc);\n"
        "    }\n"
        "    fprintf(stderr, \"xs: called non-function value\\n\"); return (xs_val){.tag=4};\n"
        "}\n\n"
        "/* string methods */\n"
        "static xs_val xs_str_split(xs_val s, xs_val delim) {\n"
        "    if (s.tag != 2 || !s.s) return xs_array(0);\n"
        "    const char *d = (delim.tag == 2 && delim.s) ? delim.s : \" \";\n"
        "    xs_val result = xs_array(0);\n"
        "    char *copy = strdup(s.s);\n"
        "    char *tok = strtok(copy, d);\n"
        "    while (tok) { xs_arr_push(result, XS_STR(strdup(tok))); tok = strtok(NULL, d); }\n"
        "    free(copy);\n"
        "    return result;\n"
        "}\n\n"
        "static xs_val xs_str_upper(xs_val s) {\n"
        "    if (s.tag != 2 || !s.s) return s;\n"
        "    char *r = strdup(s.s);\n"
        "    for (char *p = r; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;\n"
        "    return XS_STR(r);\n"
        "}\n\n"
        "static xs_val xs_str_lower(xs_val s) {\n"
        "    if (s.tag != 2 || !s.s) return s;\n"
        "    char *r = strdup(s.s);\n"
        "    for (char *p = r; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;\n"
        "    return XS_STR(r);\n"
        "}\n\n"
        "static xs_val xs_arr_join(xs_val arr, xs_val sep) {\n"
        "    if (arr.tag != 5 || !arr.p) return XS_STR(strdup(\"\"));\n"
        "    xs_arr *a = (xs_arr*)arr.p;\n"
        "    const char *s = (sep.tag == 2 && sep.s) ? sep.s : \"\";\n"
        "    int total = 0;\n"
        "    for (int i = 0; i < a->len; i++) {\n"
        "        if (i) total += (int)strlen(s);\n"
        "        total += (int)strlen(xs_to_str(a->items[i]));\n"
        "    }\n"
        "    char *buf = (char*)malloc(total + 1); buf[0] = 0;\n"
        "    for (int i = 0; i < a->len; i++) {\n"
        "        if (i) strcat(buf, s);\n"
        "        strcat(buf, xs_to_str(a->items[i]));\n"
        "    }\n"
        "    return XS_STR(buf);\n"
        "}\n\n"
        "static xs_val xs_str_parse_float(xs_val s) {\n"
        "    if (s.tag != 2 || !s.s) return (xs_val){.tag=4};\n"
        "    return XS_FLOAT(atof(s.s));\n"
        "}\n\n"
        "static xs_val xs_is_type(xs_val v, const char *t) {\n"
        "    static const char *tags[] = {\"int\",\"float\",\"str\",\"bool\",\"null\",\"array\",\"map\"};\n"
        "    if (v.tag >= 0 && v.tag < 7 && strcmp(tags[v.tag], t) == 0) return XS_BOOL(1);\n"
        "    return XS_BOOL(0);\n"
        "}\n\n"
        "/* array sort */\n"
        "static int __xs_sort_cmp(const void *a, const void *b) {\n"
        "    return xs_cmp(*(const xs_val*)a, *(const xs_val*)b);\n"
        "}\n"
        "static xs_val xs_arr_sort(xs_val arr) {\n"
        "    if (arr.tag != 5 || !arr.p) return arr;\n"
        "    xs_arr *a = (xs_arr*)arr.p;\n"
        "    qsort(a->items, a->len, sizeof(xs_val), __xs_sort_cmp);\n"
        "    return arr;\n"
        "}\n\n"
    );

    if (!program) {
        sb_add(&s, "/* (empty program) */\n");
        return s.data;
    }

    /* pre-scan for actor declarations, impl types, and variable bindings */
    n_actors = 0;
    n_actor_vars = 0;
    n_impl_types = 0;
    n_struct_vars = 0;
    n_lambdas = 0;
    lambda_counter = 0;
    n_fn_sigs = 0;
    prescan_stmts(program);
    scan_lambdas(program);

    /* emit actor struct + method definitions at file scope */
    if (program->tag == NODE_PROGRAM) {
        for (int i = 0; i < program->program.stmts.len; i++) {
            Node *st = program->program.stmts.items[i];
            if (!st) continue;
            /* find actor decl nodes (may be wrapped in EXPR_STMT) */
            Node *ad = NULL;
            if (st->tag == NODE_ACTOR_DECL) ad = st;
            else if (st->tag == NODE_EXPR_STMT && st->expr_stmt.expr &&
                     st->expr_stmt.expr->tag == NODE_ACTOR_DECL)
                ad = st->expr_stmt.expr;
            if (!ad || !ad->actor_decl.name) continue;

            const char *aname = ad->actor_decl.name;
            /* emit state struct */
            sb_printf(&s, "typedef struct %s_state {\n", aname);
            for (int j = 0; j < ad->actor_decl.state_fields.len; j++)
                sb_printf(&s, "    xs_val %s;\n", ad->actor_decl.state_fields.items[j].key);
            sb_printf(&s, "} %s_state;\n\n", aname);

            /* set up actor field list for identifier rewriting */
            const char *field_names[64];
            int nfields = 0;
            for (int j = 0; j < ad->actor_decl.state_fields.len && nfields < 64; j++)
                field_names[nfields++] = ad->actor_decl.state_fields.items[j].key;
            actor_fields = field_names;
            n_actor_fields = nfields;

            /* emit methods as static functions */
            for (int j = 0; j < ad->actor_decl.methods.len; j++) {
                Node *m = ad->actor_decl.methods.items[j];
                if (!m || m->tag != NODE_FN_DECL || !m->fn_decl.name) continue;
                sb_printf(&s, "static xs_val %s_%s(%s_state *self",
                          aname, m->fn_decl.name, aname);
                for (int p = 0; p < m->fn_decl.params.len; p++) {
                    sb_add(&s, ", xs_val ");
                    sb_add(&s, m->fn_decl.params.items[p].name ?
                           m->fn_decl.params.items[p].name : "_");
                }
                sb_add(&s, ") {\n");
                in_method_body = 1;
                if (m->fn_decl.body && m->fn_decl.body->tag == NODE_BLOCK) {
                    /* emit statements but not the trailing expression (it becomes return) */
                    for (int si = 0; si < m->fn_decl.body->block.stmts.len; si++)
                        emit_stmt(&s, m->fn_decl.body->block.stmts.items[si], 1);
                    if (m->fn_decl.body->block.expr) {
                        sb_add(&s, "    return ");
                        emit_expr(&s, m->fn_decl.body->block.expr, 1);
                        sb_add(&s, ";\n");
                    } else {
                        sb_add(&s, "    return XS_NULL;\n");
                    }
                } else {
                    sb_add(&s, "    return XS_NULL;\n");
                }
                in_method_body = 0;
                sb_add(&s, "}\n\n");
            }

            /* clear actor field rewriting */
            actor_fields = NULL;
            n_actor_fields = 0;
        }

        /* emit async functions at file scope */
        for (int i = 0; i < program->program.stmts.len; i++) {
            Node *st = program->program.stmts.items[i];
            if (!st || st->tag != NODE_FN_DECL) continue;
            if (is_main_fn(st)) continue;
            if (st->fn_decl.is_async) {
                /* async fn -> regular function at file scope */
                sb_add(&s, "static xs_val ");
                sb_add(&s, st->fn_decl.name);
                emit_params_c(&s, &st->fn_decl.params);
                sb_add(&s, " {\n");
                if (st->fn_decl.body && st->fn_decl.body->tag == NODE_BLOCK) {
                    emit_block_body(&s, st->fn_decl.body, 1);
                    if (st->fn_decl.body->block.expr) {
                        sb_add(&s, "    return ");
                        emit_expr(&s, st->fn_decl.body->block.expr, 1);
                        sb_add(&s, ";\n");
                    }
                }
                sb_add(&s, "    return XS_NULL;\n}\n\n");
            }
        }
    }

    /* emit lambda static functions */
    for (int li = 0; li < n_lambdas; li++) {
        Node *ln = lambdas[li].node;
        if (!ln || ln->tag != NODE_LAMBDA) continue;
        sb_printf(&s, "static xs_val __xs_lambda_%d(void *__env, xs_val *__args, int __argc) {\n", lambdas[li].id);
        /* bind params from __args */
        for (int p = 0; p < ln->lambda.params.len; p++) {
            const char *pname = ln->lambda.params.items[p].name;
            sb_printf(&s, "    xs_val %s = __argc > %d ? __args[%d] : (xs_val){.tag=4};\n",
                      pname ? pname : "_", p, p);
        }
        /* set current_lambda so NODE_IDENT emits capture access */
        current_lambda = &lambdas[li];
        if (ln->lambda.body && ln->lambda.body->tag == NODE_BLOCK) {
            for (int si = 0; si < ln->lambda.body->block.stmts.len; si++)
                emit_stmt(&s, ln->lambda.body->block.stmts.items[si], 1);
            if (ln->lambda.body->block.expr) {
                sb_add(&s, "    return ");
                emit_expr(&s, ln->lambda.body->block.expr, 1);
                sb_add(&s, ";\n");
            } else {
                sb_add(&s, "    return (xs_val){.tag=4};\n");
            }
        } else if (ln->lambda.body) {
            sb_add(&s, "    return ");
            emit_expr(&s, ln->lambda.body, 1);
            sb_add(&s, ";\n");
        } else {
            sb_add(&s, "    return (xs_val){.tag=4};\n");
        }
        current_lambda = NULL;
        sb_add(&s, "}\n\n");
    }

    /* check if top-level code needs wrapping in main() */
    int needs_main_wrap = 0;
    if (program->tag == NODE_PROGRAM) {
        int has_explicit_main = 0;
        for (int i = 0; i < program->program.stmts.len; i++) {
            Node *st = program->program.stmts.items[i];
            if (st && st->tag == NODE_FN_DECL && is_main_fn(st))
                has_explicit_main = 1;
        }
        /* if there are non-declaration statements and no main, wrap */
        if (!has_explicit_main) {
            for (int i = 0; i < program->program.stmts.len; i++) {
                Node *st = program->program.stmts.items[i];
                if (!st) continue;
                if (st->tag != NODE_FN_DECL && st->tag != NODE_STRUCT_DECL &&
                    st->tag != NODE_ENUM_DECL && st->tag != NODE_CLASS_DECL &&
                    st->tag != NODE_TRAIT_DECL && st->tag != NODE_IMPL_DECL &&
                    st->tag != NODE_TYPE_ALIAS && st->tag != NODE_IMPORT &&
                    st->tag != NODE_USE && st->tag != NODE_MODULE_DECL &&
                    st->tag != NODE_EFFECT_DECL) {
                    /* skip actor decls (already emitted) */
                    if (st->tag == NODE_ACTOR_DECL) continue;
                    if (st->tag == NODE_EXPR_STMT && st->expr_stmt.expr &&
                        st->expr_stmt.expr->tag == NODE_ACTOR_DECL) continue;
                    needs_main_wrap = 1;
                    break;
                }
            }
        }
    }

    if (needs_main_wrap) {
        /* emit file-scope declarations first */
        if (program->tag == NODE_PROGRAM) {
            for (int i = 0; i < program->program.stmts.len; i++) {
                Node *st = program->program.stmts.items[i];
                if (!st) continue;
                if (st->tag == NODE_FN_DECL && !is_main_fn(st) && !st->fn_decl.is_async)
                    emit_stmt(&s, st, 0);
                else if (st->tag == NODE_STRUCT_DECL || st->tag == NODE_ENUM_DECL ||
                         st->tag == NODE_CLASS_DECL || st->tag == NODE_TRAIT_DECL ||
                         st->tag == NODE_IMPL_DECL || st->tag == NODE_TYPE_ALIAS ||
                         st->tag == NODE_IMPORT || st->tag == NODE_USE ||
                         st->tag == NODE_MODULE_DECL || st->tag == NODE_EFFECT_DECL)
                    emit_stmt(&s, st, 0);
            }
        }

        sb_add(&s, "int main(int argc, char **argv) {\n");
        sb_add(&s, "    (void)argc; (void)argv;\n");

        /* emit actor state initializations */
        for (int i = 0; i < n_actor_vars; i++) {
            sb_printf(&s, "    %s_state %s_state;\n",
                      actor_vars[i].actor_name, actor_vars[i].var_name);
            /* find the actor decl and init fields */
            if (program->tag == NODE_PROGRAM) {
                for (int j = 0; j < program->program.stmts.len; j++) {
                    Node *st = program->program.stmts.items[j];
                    Node *ad = NULL;
                    if (st && st->tag == NODE_ACTOR_DECL) ad = st;
                    else if (st && st->tag == NODE_EXPR_STMT && st->expr_stmt.expr &&
                             st->expr_stmt.expr->tag == NODE_ACTOR_DECL)
                        ad = st->expr_stmt.expr;
                    if (!ad || !ad->actor_decl.name) continue;
                    if (strcmp(ad->actor_decl.name, actor_vars[i].actor_name) != 0) continue;
                    for (int k = 0; k < ad->actor_decl.state_fields.len; k++) {
                        sb_printf(&s, "    %s_state.%s = ",
                                  actor_vars[i].var_name,
                                  ad->actor_decl.state_fields.items[k].key);
                        if (ad->actor_decl.state_fields.items[k].val)
                            emit_expr(&s, ad->actor_decl.state_fields.items[k].val, 1);
                        else
                            sb_add(&s, "XS_NULL");
                        sb_add(&s, ";\n");
                    }
                    break;
                }
            }
        }

        /* emit non-declaration statements */
        if (program->tag == NODE_PROGRAM) {
            for (int i = 0; i < program->program.stmts.len; i++) {
                Node *st = program->program.stmts.items[i];
                if (!st) continue;
                /* skip declarations already emitted */
                if (st->tag == NODE_FN_DECL || st->tag == NODE_STRUCT_DECL ||
                    st->tag == NODE_ENUM_DECL || st->tag == NODE_CLASS_DECL ||
                    st->tag == NODE_TRAIT_DECL || st->tag == NODE_IMPL_DECL ||
                    st->tag == NODE_TYPE_ALIAS || st->tag == NODE_IMPORT ||
                    st->tag == NODE_USE || st->tag == NODE_MODULE_DECL ||
                    st->tag == NODE_EFFECT_DECL || st->tag == NODE_ACTOR_DECL)
                    continue;
                if (st->tag == NODE_EXPR_STMT && st->expr_stmt.expr &&
                    st->expr_stmt.expr->tag == NODE_ACTOR_DECL)
                    continue;
                emit_stmt(&s, st, 1);
            }
        }

        sb_add(&s, "    return 0;\n}\n");
    } else {
        if (program->tag == NODE_PROGRAM) {
            for (int i = 0; i < program->program.stmts.len; i++)
                emit_stmt(&s, program->program.stmts.items[i], 0);
        } else {
            emit_stmt(&s, program, 0);
        }
    }

    return s.data;
}
