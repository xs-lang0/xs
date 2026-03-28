#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core/xs.h"
#include "core/xs_utils.h"
#include "semantic/resolve.h"

static const char *find_similar_name(SymTab *st, const char *name) {
    if (!st || !st->current || !name) return NULL;
    const char *best = NULL;
    int best_dist = 4;
    int name_len = (int)strlen(name);
    for (Scope *sc = st->current; sc; sc = sc->parent) {
        for (int b = 0; b < sc->n_buckets; b++) {
            for (Symbol *sym = sc->buckets[b]; sym; sym = sym->next) {
                if (!sym->name) continue;
                int sym_len = (int)strlen(sym->name);
                int diff = name_len - sym_len;
                if (diff < 0) diff = -diff;
                if (diff >= best_dist) continue;
                int d = xs_edit_distance(name, sym->name);
                if (d > 0 && d < best_dist) {
                    best_dist = d;
                    best = sym->name;
                }
            }
        }
    }
    return best;
}

static int is_builtin_name(const char *name) {
    static const char *builtins[] = {
        "println","print","eprintln","eprint","print_no_nl","input",
        "assert","assert_eq","panic","exit","todo","unreachable",
        "len","push","pop","keys","values","has","entries","flatten",
        "array","map","vec","sorted","copy","clone",
        "zip","enumerate","sum","contains","filter","reduce",
        "starts_with","ends_with","split","join","trim",
        "to_upper","to_lower","to_string","to_int","to_float","chars","bytes",
        "replace","find","substr","format","parse_int","parse_float",
        "read_file","write_file","read_line",
        "abs","min","max","pow","sqrt","floor","ceil","round","log","sin","cos","tan",
        "Some","None","Ok","Err","true","false","null","self","super",
        "range",
        "typeof","instanceof","type_of","type","is_null","is_int","is_float",
        "is_str","is_bool","is_array","is_fn",
        "int","float","str","bool","char","repr","ord","chr",
        "dbg","pprint","clear","sprintf",
        "channel","spawn","nursery","signal","derived",
        "math","time","io","string","path","base64","hash","uuid",
        "collections","random","json","log","fmt","test","csv","url",
        "re","process","os","async","net","crypto","thread","buf",
        "encode","db","cli","ffi","reflect","gc","reactive",
        "argc","argv",
        "i8","i16","i32","i64","i128","isize",
        "u8","u16","u32","u64","u128","usize",
        "f32","f64","void","byte","any","never",
        NULL
    };
    for (int i = 0; builtins[i]; i++)
        if (strcmp(name, builtins[i]) == 0) return 1;
    return 0;
}

static void define_pattern_bindings(Node *pat, SymTab *st) {
    if (!pat) return;
    switch (pat->tag) {
    case NODE_PAT_IDENT:
        if (pat->pat_ident.name && strcmp(pat->pat_ident.name, "_") != 0)
            sym_define(st, pat->pat_ident.name, SYM_LOCAL, NULL, pat, pat->pat_ident.mutable);
        break;
    case NODE_PAT_CAPTURE:
        if (pat->pat_capture.name)
            sym_define(st, pat->pat_capture.name, SYM_LOCAL, NULL, pat, 0);
        define_pattern_bindings(pat->pat_capture.pattern, st);
        break;
    case NODE_PAT_TUPLE:
        for (int i = 0; i < pat->pat_tuple.elems.len; i++)
            define_pattern_bindings(pat->pat_tuple.elems.items[i], st);
        break;
    case NODE_PAT_ENUM:
        for (int i = 0; i < pat->pat_enum.args.len; i++)
            define_pattern_bindings(pat->pat_enum.args.items[i], st);
        break;
    case NODE_PAT_STRUCT:
        for (int i = 0; i < pat->pat_struct.fields.len; i++) {
            Node *sub = pat->pat_struct.fields.items[i].val;
            if (sub) {
                define_pattern_bindings(sub, st);
            } else {
                char *fname = pat->pat_struct.fields.items[i].key;
                if (fname && strcmp(fname, "_") != 0)
                    sym_define(st, fname, SYM_LOCAL, NULL, pat, 0);
            }
        }
        break;
    case NODE_PAT_OR:
        define_pattern_bindings(pat->pat_or.left, st);
        break;
    case NODE_PAT_GUARD:
        define_pattern_bindings(pat->pat_guard.pattern, st);
        break;
    case NODE_PAT_SLICE:
        for (int i = 0; i < pat->pat_slice.elems.len; i++)
            define_pattern_bindings(pat->pat_slice.elems.items[i], st);
        if (pat->pat_slice.rest && strcmp(pat->pat_slice.rest, "_") != 0)
            sym_define(st, pat->pat_slice.rest, SYM_LOCAL, NULL, pat, 0);
        break;
    case NODE_PAT_STRING_CONCAT:
        define_pattern_bindings(pat->pat_str_concat.rest, st);
        break;
    case NODE_PAT_REGEX:
        break;
    default:
        break;
    }
}

/* pass 1: top-level decls */
static void collect_toplevel(Node *prog, SymTab *st) {
    if (!prog || prog->tag != NODE_PROGRAM) return;
    for (int i = 0; i < prog->program.stmts.len; i++) {
        Node *s = prog->program.stmts.items[i];
        if (!s) continue;
        switch (s->tag) {
            case NODE_FN_DECL:
                sym_define(st, s->fn_decl.name ? s->fn_decl.name : "",
                           SYM_FN, NULL, s, 0);
                break;
            case NODE_TAG_DECL:
                sym_define(st, s->tag_decl.name ? s->tag_decl.name : "",
                           SYM_FN, NULL, s, 0);
                break;
            case NODE_STRUCT_DECL:
                sym_define(st, s->struct_decl.name ? s->struct_decl.name : "",
                           SYM_STRUCT, NULL, s, 0);
                break;
            case NODE_ENUM_DECL:
                sym_define(st, s->enum_decl.name ? s->enum_decl.name : "",
                           SYM_ENUM, NULL, s, 0);
                break;
            case NODE_TRAIT_DECL:
                sym_define(st, s->trait_decl.name ? s->trait_decl.name : "",
                           SYM_TRAIT, NULL, s, 0);
                break;
            case NODE_CONST:
                sym_define(st, s->const_.name ? s->const_.name : "",
                           SYM_CONST, NULL, s, 0);
                break;
            case NODE_MODULE_DECL:
                sym_define(st, s->module_decl.name ? s->module_decl.name : "",
                           SYM_STRUCT /* reuse kind for module */, NULL, s, 0);
                break;
            case NODE_TYPE_ALIAS:
                sym_define(st, s->type_alias.name ? s->type_alias.name : "",
                           SYM_STRUCT /* reuse kind for type alias */, NULL, s, 0);
                break;
            case NODE_CLASS_DECL:
                sym_define(st, s->class_decl.name ? s->class_decl.name : "",
                           SYM_STRUCT /* reuse kind for class */, NULL, s, 0);
                break;
            case NODE_EFFECT_DECL:
                sym_define(st, s->effect_decl.name ? s->effect_decl.name : "",
                           SYM_TRAIT /* reuse kind for effect */, NULL, s, 0);
                break;
            case NODE_ACTOR_DECL:
                sym_define(st, s->actor_decl.name ? s->actor_decl.name : "",
                           SYM_STRUCT /* reuse kind for actor */, NULL, s, 0);
                break;
            default: break;
        }
    }
}

// pass 2: resolve identifiers
static void resolve_node(Node *n, SymTab *st, SemaCtx *ctx);

static void resolve_list(NodeList *nl, SymTab *st, SemaCtx *ctx) {
    if (!nl) return;
    for (int i = 0; i < nl->len; i++) resolve_node(nl->items[i], st, ctx);
}

static void resolve_node(Node *n, SymTab *st, SemaCtx *ctx) {
    if (!n) return;
    switch (n->tag) {

    case NODE_FN_DECL: {
        scope_push(st);
        for (int i = 0; i < n->fn_decl.params.len; i++) {
            Param *pm = &n->fn_decl.params.items[i];
            if (pm->name)
                sym_define(st, pm->name, SYM_PARAM, NULL, NULL, 0);
        }
        if (n->fn_decl.body) resolve_node(n->fn_decl.body, st, ctx);
        scope_pop(st);
        break;
    }

    case NODE_TAG_DECL: {
        scope_push(st);
        for (int i = 0; i < n->tag_decl.params.len; i++) {
            Param *pm = &n->tag_decl.params.items[i];
            if (pm->name)
                sym_define(st, pm->name, SYM_PARAM, NULL, NULL, 0);
        }
        sym_define(st, "__block", SYM_PARAM, NULL, NULL, 0);
        if (n->tag_decl.body) resolve_node(n->tag_decl.body, st, ctx);
        scope_pop(st);
        break;
    }

    case NODE_IMPL_DECL: {
        resolve_list(&n->impl_decl.members, st, ctx);
        break;
    }

    case NODE_MODULE_DECL: {
        resolve_list(&n->module_decl.body, st, ctx);
        break;
    }

    case NODE_BLOCK: {
        scope_push(st);
        /* hoist fn/class/struct/enum decls */
        for (int hi = 0; hi < n->block.stmts.len; hi++) {
            Node *s = n->block.stmts.items[hi];
            if (!s) continue;
            if (s->tag == NODE_FN_DECL && s->fn_decl.name)
                sym_define(st, s->fn_decl.name, SYM_FN, NULL, s, 0);
            else if (s->tag == NODE_CLASS_DECL && s->class_decl.name)
                sym_define(st, s->class_decl.name, SYM_STRUCT, NULL, s, 0);
            else if (s->tag == NODE_ACTOR_DECL && s->actor_decl.name)
                sym_define(st, s->actor_decl.name, SYM_STRUCT, NULL, s, 0);
            else if (s->tag == NODE_STRUCT_DECL && s->struct_decl.name)
                sym_define(st, s->struct_decl.name, SYM_STRUCT, NULL, s, 0);
            else if (s->tag == NODE_ENUM_DECL && s->enum_decl.name)
                sym_define(st, s->enum_decl.name, SYM_ENUM, NULL, s, 0);
        }
        resolve_list(&n->block.stmts, st, ctx);
        if (n->block.expr) resolve_node(n->block.expr, st, ctx);
        int nu = 0;
        Symbol **unused = scope_unused_locals(st, &nu);
        for (int i = 0; i < nu; i++) {
            Diagnostic *d = diag_new(DIAG_WARNING, DIAG_PHASE_SEMANTIC, "T0003",
                "unused variable '%s'", unused[i]->name);
            Span decl_span = n->span;
            if (unused[i]->decl) {
                Node *decl = unused[i]->decl;
                if ((decl->tag == NODE_LET || decl->tag == NODE_VAR || decl->tag == NODE_CONST)
                    && decl->let.pattern) {
                    decl_span = decl->let.pattern->span;
                } else {
                    decl_span = decl->span;
                }
            }
            diag_annotate(d, decl_span, 1, "'%s' defined here but never used", unused[i]->name);
            diag_hint(d, "prefix with '_' to suppress: _%s", unused[i]->name);
            diag_emit(ctx->diag, d);
        }
        free(unused);
        scope_pop(st);
        break;
    }

    case NODE_LET:
        if (n->let.value) resolve_node(n->let.value, st, ctx);
        if (n->let.name)
            sym_define(st, n->let.name, SYM_LOCAL, NULL, n, 0);
        else if (n->let.pattern)
            define_pattern_bindings(n->let.pattern, st);
        break;

    case NODE_VAR:
        if (n->let.value) resolve_node(n->let.value, st, ctx);
        if (n->let.name)
            sym_define(st, n->let.name, SYM_LOCAL, NULL, n, 1);
        else if (n->let.pattern)
            define_pattern_bindings(n->let.pattern, st);
        break;

    case NODE_CONST:
        if (n->const_.value) resolve_node(n->const_.value, st, ctx);
        if (n->const_.name)
            sym_define(st, n->const_.name, SYM_CONST, NULL, n, 0);
        break;

    case NODE_IDENT: {
        const char *name = n->ident.name;
        if (!name) break;
        if (!is_builtin_name(name)) {
            Symbol *sym = sym_lookup(st, name);
            if (!sym) {
                Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0002",
                    "undefined name '%s'", name);
                diag_annotate(d, n->span, 1, "not found in this scope");
                const char *similar = find_similar_name(st, name);
                if (similar) {
                    diag_hint(d, "did you mean '%s'?", similar);
                }
                diag_emit(ctx->diag, d);
            } else {
                sym->is_used = 1;
            }
        }
        break;
    }

    case NODE_ASSIGN:
        resolve_node(n->assign.target, st, ctx);
        resolve_node(n->assign.value,  st, ctx);
        break;

    case NODE_BINOP:
        resolve_node(n->binop.left, st, ctx);
        resolve_node(n->binop.right, st, ctx);
        break;

    case NODE_UNARY:    resolve_node(n->unary.expr, st, ctx); break;

    case NODE_IF:
        resolve_node(n->if_expr.cond, st, ctx);
        resolve_node(n->if_expr.then, st, ctx);
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            resolve_node(n->if_expr.elif_conds.items[i], st, ctx);
            if (i < n->if_expr.elif_thens.len)
                resolve_node(n->if_expr.elif_thens.items[i], st, ctx);
        }
        if (n->if_expr.else_branch) resolve_node(n->if_expr.else_branch, st, ctx);
        break;

    case NODE_WHILE:
        resolve_node(n->while_loop.cond, st, ctx);
        resolve_node(n->while_loop.body, st, ctx);
        break;

    case NODE_FOR:
        resolve_node(n->for_loop.iter, st, ctx);
        scope_push(st);
        if (n->for_loop.pattern)
            define_pattern_bindings(n->for_loop.pattern, st);
        resolve_node(n->for_loop.body, st, ctx);
        scope_pop(st);
        break;

    case NODE_LOOP:
        resolve_node(n->loop.body, st, ctx);
        break;

    case NODE_RETURN:
        if (n->ret.value) resolve_node(n->ret.value, st, ctx);
        break;

    case NODE_YIELD:
        if (n->yield_.value) resolve_node(n->yield_.value, st, ctx);
        break;

    case NODE_THROW:
        if (n->throw_.value) resolve_node(n->throw_.value, st, ctx);
        break;

    case NODE_DEFER:
        resolve_node(n->defer_.body, st, ctx);
        break;

    case NODE_TRY:
        resolve_node(n->try_.body, st, ctx);
        for (int i = 0; i < n->try_.catch_arms.len; i++) {
            scope_push(st);
            define_pattern_bindings(n->try_.catch_arms.items[i].pattern, st);
            resolve_node(n->try_.catch_arms.items[i].body, st, ctx);
            scope_pop(st);
        }
        if (n->try_.finally_block) resolve_node(n->try_.finally_block, st, ctx);
        break;

    case NODE_CALL:
        resolve_node(n->call.callee, st, ctx);
        resolve_list(&n->call.args, st, ctx);
        for (int i = 0; i < n->call.kwargs.len; i++)
            resolve_node(n->call.kwargs.items[i].val, st, ctx);
        break;

    case NODE_METHOD_CALL:
        resolve_node(n->method_call.obj, st, ctx);
        resolve_list(&n->method_call.args, st, ctx);
        for (int i = 0; i < n->method_call.kwargs.len; i++)
            resolve_node(n->method_call.kwargs.items[i].val, st, ctx);
        break;

    case NODE_MATCH:
        resolve_node(n->match.subject, st, ctx);
        for (int i = 0; i < n->match.arms.len; i++) {
            MatchArm *arm = &n->match.arms.items[i];
            scope_push(st);
            define_pattern_bindings(arm->pattern, st);
            if (arm->guard) resolve_node(arm->guard, st, ctx);
            resolve_node(arm->body, st, ctx);
            scope_pop(st);
        }
        break;

    case NODE_EXPR_STMT: resolve_node(n->expr_stmt.expr, st, ctx); break;

    case NODE_INDEX:
        resolve_node(n->index.obj, st, ctx);
        resolve_node(n->index.index, st, ctx);
        break;

    case NODE_FIELD:
        resolve_node(n->field.obj, st, ctx);
        break;

    case NODE_SCOPE:
        break;

    case NODE_RANGE:
        if (n->range.start) resolve_node(n->range.start, st, ctx);
        if (n->range.end)   resolve_node(n->range.end, st, ctx);
        break;

    case NODE_CAST:
        resolve_node(n->cast.expr, st, ctx);
        break;

    case NODE_LAMBDA: {
        scope_push(st);
        for (int i = 0; i < n->lambda.params.len; i++) {
            Param *pm = &n->lambda.params.items[i];
            if (pm->name)
                sym_define(st, pm->name, SYM_PARAM, NULL, NULL, 0);
        }
        if (n->lambda.body) resolve_node(n->lambda.body, st, ctx);
        scope_pop(st);
        break;
    }

    case NODE_STRUCT_INIT:
        for (int i = 0; i < n->struct_init.fields.len; i++)
            resolve_node(n->struct_init.fields.items[i].val, st, ctx);
        if (n->struct_init.rest) resolve_node(n->struct_init.rest, st, ctx);
        break;

    case NODE_LIT_ARRAY:
    case NODE_LIT_TUPLE:
        resolve_list(&n->lit_array.elems, st, ctx);
        if (n->lit_array.repeat_val)
            resolve_node(n->lit_array.repeat_val, st, ctx);
        break;

    case NODE_LIT_MAP:
        for (int i = 0; i < n->lit_map.keys.len; i++) {
            Node *k = n->lit_map.keys.items[i];
            if (k && k->tag == NODE_SPREAD) resolve_node(k, st, ctx);
        }
        for (int i = 0; i < n->lit_map.vals.len; i++) {
            if (n->lit_map.vals.items[i])
                resolve_node(n->lit_map.vals.items[i], st, ctx);
        }
        break;

    case NODE_LIT_STRING:
    case NODE_INTERP_STRING:
        if (n->lit_string.interpolated || n->tag == NODE_INTERP_STRING)
            resolve_list(&n->lit_string.parts, st, ctx);
        break;

    case NODE_SPREAD:
        resolve_node(n->spread.expr, st, ctx);
        break;

    case NODE_LIST_COMP: {
        scope_push(st);
        for (int i = 0; i < n->list_comp.clause_pats.len; i++) {
            if (i < n->list_comp.clause_iters.len)
                resolve_node(n->list_comp.clause_iters.items[i], st, ctx);
            define_pattern_bindings(n->list_comp.clause_pats.items[i], st);
            if (i < n->list_comp.clause_conds.len && n->list_comp.clause_conds.items[i])
                resolve_node(n->list_comp.clause_conds.items[i], st, ctx);
        }
        resolve_node(n->list_comp.element, st, ctx);
        scope_pop(st);
        break;
    }

    case NODE_AWAIT:
        resolve_node(n->await_.expr, st, ctx);
        break;

    case NODE_SPAWN:
        resolve_node(n->spawn_.expr, st, ctx);
        break;

    case NODE_NURSERY: {
        scope_push(st);
        resolve_node(n->nursery_.body, st, ctx);
        scope_pop(st);
        break;
    }

    case NODE_PERFORM:
        resolve_list(&n->perform.args, st, ctx);
        break;

    case NODE_HANDLE:
        resolve_node(n->handle.expr, st, ctx);
        for (int i = 0; i < n->handle.arms.len; i++) {
            scope_push(st);
            EffectArm *arm = &n->handle.arms.items[i];
            for (int j = 0; j < arm->params.len; j++) {
                Param *pm = &arm->params.items[j];
                if (pm->name)
                    sym_define(st, pm->name, SYM_PARAM, NULL, NULL, 0);
            }
            resolve_node(arm->body, st, ctx);
            scope_pop(st);
        }
        break;

    case NODE_RESUME:
        if (n->resume_.value) resolve_node(n->resume_.value, st, ctx);
        break;

    case NODE_BREAK:
        if (n->brk.value) resolve_node(n->brk.value, st, ctx);
        break;

    case NODE_PROGRAM:
        for (int i = 0; i < n->program.stmts.len; i++)
            resolve_node(n->program.stmts.items[i], st, ctx);
        break;

    case NODE_LIT_INT:
    case NODE_LIT_BIGINT:
    case NODE_LIT_FLOAT:
    case NODE_LIT_BOOL:
    case NODE_LIT_NULL:
    case NODE_LIT_CHAR:
    case NODE_CONTINUE:
    case NODE_PAT_WILD:
    case NODE_PAT_LIT:
    case NODE_PAT_IDENT:
    case NODE_PAT_RANGE:
    case NODE_PAT_EXPR:
    case NODE_IMPORT:
        if (n->import.alias) {
            sym_define(st, n->import.alias, SYM_MODULE_ITEM, NULL, n, 0);
        } else if (n->import.nparts > 0 && n->import.path[0]) {
            sym_define(st, n->import.path[0], SYM_MODULE_ITEM, NULL, n, 0);
        }
        for (int ii = 0; ii < n->import.nitems; ii++) {
            if (n->import.items[ii])
                sym_define(st, n->import.items[ii], SYM_MODULE_ITEM, NULL, n, 0);
        }
        break;
    case NODE_USE:
        if (n->use_.import_all && n->use_.alias) {
            sym_define(st, n->use_.alias, SYM_MODULE_ITEM, NULL, n, 0);
        }
        for (int ii = 0; ii < n->use_.nnames; ii++) {
            if (n->use_.name_aliases[ii])
                sym_define(st, n->use_.name_aliases[ii], SYM_MODULE_ITEM, NULL, n, 0);
        }
        break;
    case NODE_STRUCT_DECL:
    case NODE_ENUM_DECL:
    case NODE_TRAIT_DECL:
        break;

    case NODE_TYPE_ALIAS:
        break;

    case NODE_CLASS_DECL: {
        scope_push(st);
        sym_define(st, "self", SYM_LOCAL, NULL, n, 0);
        resolve_list(&n->class_decl.members, st, ctx);
        scope_pop(st);
        break;
    }

    case NODE_EFFECT_DECL: {
        for (int i = 0; i < n->effect_decl.ops.len; i++) {
            resolve_node(n->effect_decl.ops.items[i], st, ctx);
        }
        break;
    }

    default: break;
    }
}

void resolve_program(Node *prog, SymTab *st, SemaCtx *ctx) {
    if (!prog || !st || !ctx) return;
    collect_toplevel(prog, st);
    resolve_node(prog, st, ctx);
}
