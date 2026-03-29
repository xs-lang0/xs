#include "core/ast.h"
#include <stdlib.h>
#include <string.h>

NodeList nodelist_new(void) {
    NodeList nl = {NULL, 0, 0};
    return nl;
}

void nodelist_push(NodeList *nl, Node *n) {
    if (nl->len >= nl->cap) {
        nl->cap = nl->cap ? nl->cap * 2 : 4;
        nl->items = xs_realloc(nl->items, nl->cap * sizeof(Node*));
    }
    nl->items[nl->len++] = n;
}

void nodelist_free(NodeList *nl) {
    for (int i = 0; i < nl->len; i++)
        node_free(nl->items[i]);
    free(nl->items);
    nl->items = NULL; nl->len = nl->cap = 0;
}

NodePairList nodepairlist_new(void) {
    NodePairList pl = {NULL, 0, 0};
    return pl;
}

void nodepairlist_push(NodePairList *pl, const char *k, Node *v) {
    if (pl->len >= pl->cap) {
        pl->cap = pl->cap ? pl->cap * 2 : 4;
        pl->items = xs_realloc(pl->items, pl->cap * sizeof(NodePair));
    }
    pl->items[pl->len].key = xs_strdup(k);
    pl->items[pl->len].val = v;
    pl->len++;
}

void nodepairlist_free(NodePairList *pl) {
    for (int i = 0; i < pl->len; i++) {
        free(pl->items[i].key);
        node_free(pl->items[i].val);
    }
    free(pl->items);
    pl->items = NULL; pl->len = pl->cap = 0;
}

void typeexpr_free(TypeExpr *te) {
    if (!te) return;
    free(te->name);
    for (int i = 0; i < te->nargs;   i++) typeexpr_free(te->args[i]);
    free(te->args);
    typeexpr_free(te->inner);
    for (int i = 0; i < te->nelems;  i++) typeexpr_free(te->elems[i]);
    free(te->elems);
    for (int i = 0; i < te->nparams; i++) typeexpr_free(te->params[i]);
    free(te->params);
    typeexpr_free(te->ret);
    free(te);
}

/* ParamList */
ParamList paramlist_new(void) {
    ParamList pl = {NULL, 0, 0};
    return pl;
}

void paramlist_push(ParamList *pl, Param p) {
    if (pl->len >= pl->cap) {
        pl->cap = pl->cap ? pl->cap * 2 : 4;
        pl->items = xs_realloc(pl->items, pl->cap * sizeof(Param));
    }
    pl->items[pl->len++] = p;
}

void paramlist_free(ParamList *pl) {
    for (int i = 0; i < pl->len; i++) {
        node_free(pl->items[i].pattern);
        free(pl->items[i].name);
        node_free(pl->items[i].default_val);
        typeexpr_free(pl->items[i].type_ann);
        node_free(pl->items[i].contract);
    }
    free(pl->items);
    pl->items = NULL; pl->len = pl->cap = 0;
}

MatchArmList matcharmlist_new(void) {
    MatchArmList ml = {NULL, 0, 0};
    return ml;
}

void matcharmlist_push(MatchArmList *ml, MatchArm arm) {
    if (ml->len >= ml->cap) {
        ml->cap = ml->cap ? ml->cap * 2 : 4;
        ml->items = xs_realloc(ml->items, ml->cap * sizeof(MatchArm));
    }
    ml->items[ml->len++] = arm;
}

void matcharmlist_free(MatchArmList *ml) {
    for (int i = 0; i < ml->len; i++) {
        node_free(ml->items[i].pattern);
        node_free(ml->items[i].guard);
        node_free(ml->items[i].body);
    }
    free(ml->items);
    ml->items = NULL; ml->len = ml->cap = 0;
}

EffectArmList effectarmlist_new(void) {
    EffectArmList el = {NULL, 0, 0};
    return el;
}

void effectarmlist_push(EffectArmList *el, EffectArm arm) {
    if (el->len >= el->cap) {
        el->cap = el->cap ? el->cap * 2 : 4;
        el->items = xs_realloc(el->items, el->cap * sizeof(EffectArm));
    }
    el->items[el->len++] = arm;
}

void effectarmlist_free(EffectArmList *el) {
    for (int i = 0; i < el->len; i++) {
        free(el->items[i].effect_name);
        free(el->items[i].op_name);
        paramlist_free(&el->items[i].params);
        node_free(el->items[i].body);
    }
    free(el->items);
    el->items = NULL; el->len = el->cap = 0;
}

EnumVariantList enumvariantlist_new(void) {
    EnumVariantList el = {NULL, 0, 0};
    return el;
}

void enumvariantlist_push(EnumVariantList *el, EnumVariant v) {
    if (el->len >= el->cap) {
        el->cap = el->cap ? el->cap * 2 : 4;
        el->items = xs_realloc(el->items, el->cap * sizeof(EnumVariant));
    }
    el->items[el->len++] = v;
}

void enumvariantlist_free(EnumVariantList *el) {
    for (int i = 0; i < el->len; i++) {
        free(el->items[i].name);
        nodelist_free(&el->items[i].fields);
        nodelist_free(&el->items[i].field_names);
    }
    free(el->items);
    el->items = NULL; el->len = el->cap = 0;
}

Node *node_new(NodeTag tag, Span span) {
    Node *n = xs_calloc(1, sizeof(Node));
    n->tag  = tag;
    n->span = span;
    if (tag == NODE_BLOCK) n->block.has_decls = -1; /* -1 = unknown */
    return n;
}

static void free_string_array(char **arr, int n) {
    if (!arr) return;
    for (int i = 0; i < n; i++) free(arr[i]);
    free(arr);
}

void node_free(Node *n) {
    if (!n) return;
    switch (n->tag) {
    case NODE_LIT_INT: case NODE_LIT_FLOAT: case NODE_LIT_BOOL:
    case NODE_LIT_NULL: case NODE_LIT_CHAR:
        break;
    case NODE_LIT_REGEX:
        free(n->lit_regex.pattern);
        break;
    case NODE_LIT_BIGINT:
        free(n->lit_bigint.bigint_str);
        break;
    case NODE_LIT_STRING: case NODE_INTERP_STRING:
        free(n->lit_string.sval);
        nodelist_free(&n->lit_string.parts);
        break;
    case NODE_LIT_ARRAY: case NODE_LIT_TUPLE:
        nodelist_free(&n->lit_array.elems);
        if (n->lit_array.repeat_val) node_free(n->lit_array.repeat_val);
        break;
    case NODE_LIT_MAP:
        nodelist_free(&n->lit_map.keys);
        nodelist_free(&n->lit_map.vals);
        break;
    case NODE_IDENT:
        free(n->ident.name);
        break;
    case NODE_BINOP:
        node_free(n->binop.left);
        node_free(n->binop.right);
        break;
    case NODE_UNARY:
        node_free(n->unary.expr);
        break;
    case NODE_ASSIGN:
        node_free(n->assign.target);
        node_free(n->assign.value);
        break;
    case NODE_CALL:
        node_free(n->call.callee);
        nodelist_free(&n->call.args);
        nodepairlist_free(&n->call.kwargs);
        break;
    case NODE_METHOD_CALL:
        node_free(n->method_call.obj);
        free(n->method_call.method);
        nodelist_free(&n->method_call.args);
        nodepairlist_free(&n->method_call.kwargs);
        break;
    case NODE_INDEX:
        node_free(n->index.obj);
        node_free(n->index.index);
        break;
    case NODE_FIELD:
        node_free(n->field.obj);
        free(n->field.name);
        break;
    case NODE_SCOPE:
        free_string_array(n->scope.parts, n->scope.nparts);
        break;
    case NODE_IF:
        node_free(n->if_expr.cond);
        node_free(n->if_expr.then);
        nodelist_free(&n->if_expr.elif_conds);
        nodelist_free(&n->if_expr.elif_thens);
        node_free(n->if_expr.else_branch);
        break;
    case NODE_MATCH:
        node_free(n->match.subject);
        matcharmlist_free(&n->match.arms);
        break;
    case NODE_WHILE:
        node_free(n->while_loop.cond);
        node_free(n->while_loop.body);
        free(n->while_loop.label);
        break;
    case NODE_FOR:
        node_free(n->for_loop.pattern);
        node_free(n->for_loop.iter);
        node_free(n->for_loop.body);
        free(n->for_loop.label);
        break;
    case NODE_LOOP:
        node_free(n->loop.body);
        free(n->loop.label);
        break;
    case NODE_BREAK:
        free(n->brk.label);
        node_free(n->brk.value);
        break;
    case NODE_CONTINUE:
        free(n->cont.label);
        break;
    case NODE_RETURN:
        node_free(n->ret.value);
        break;
    case NODE_YIELD:
        node_free(n->yield_.value);
        break;
    case NODE_THROW:
        node_free(n->throw_.value);
        break;
    case NODE_TRY:
        node_free(n->try_.body);
        matcharmlist_free(&n->try_.catch_arms);
        node_free(n->try_.finally_block);
        break;
    case NODE_DEFER:
        node_free(n->defer_.body);
        break;
    case NODE_LAMBDA:
        paramlist_free(&n->lambda.params);
        node_free(n->lambda.body);
        break;
    case NODE_BLOCK:
        nodelist_free(&n->block.stmts);
        node_free(n->block.expr);
        break;
    case NODE_CAST:
        node_free(n->cast.expr);
        free(n->cast.type_name);
        break;
    case NODE_RANGE:
        node_free(n->range.start);
        node_free(n->range.end);
        break;
    case NODE_STRUCT_INIT:
        free(n->struct_init.path);
        nodepairlist_free(&n->struct_init.fields);
        node_free(n->struct_init.rest);
        break;
    case NODE_SPREAD:
        node_free(n->spread.expr);
        break;
    case NODE_LIST_COMP:
        node_free(n->list_comp.element);
        nodelist_free(&n->list_comp.clause_pats);
        nodelist_free(&n->list_comp.clause_iters);
        nodelist_free(&n->list_comp.clause_conds);
        break;
    case NODE_MAP_COMP:
        node_free(n->map_comp.key);
        node_free(n->map_comp.value);
        nodelist_free(&n->map_comp.clause_pats);
        nodelist_free(&n->map_comp.clause_iters);
        nodelist_free(&n->map_comp.clause_conds);
        break;
    case NODE_LET: case NODE_VAR:
        node_free(n->let.pattern);
        free(n->let.name);
        node_free(n->let.value);
        typeexpr_free(n->let.type_ann);
        node_free(n->let.contract);
        break;
    case NODE_CONST:
        free(n->const_.name);
        node_free(n->const_.value);
        typeexpr_free(n->const_.type_ann);
        node_free(n->const_.contract);
        break;
    case NODE_EXPR_STMT:
        node_free(n->expr_stmt.expr);
        break;
    case NODE_FN_DECL:
        free(n->fn_decl.name);
        free(n->fn_decl.deprecated_msg);
        paramlist_free(&n->fn_decl.params);
        node_free(n->fn_decl.body);
        typeexpr_free(n->fn_decl.ret_type);
        for (int i = 0; i < n->fn_decl.n_type_params; i++) {
            free(n->fn_decl.type_params[i]);
            typeexpr_free(n->fn_decl.type_bounds[i]);
        }
        free(n->fn_decl.type_params);
        free(n->fn_decl.type_bounds);
        break;
    case NODE_CLASS_DECL:
        free(n->class_decl.name);
        free_string_array(n->class_decl.bases, n->class_decl.nbases);
        nodelist_free(&n->class_decl.members);
        break;
    case NODE_STRUCT_DECL:
        free(n->struct_decl.name);
        nodepairlist_free(&n->struct_decl.fields);
        for (int i = 0; i < n->struct_decl.n_type_params; i++) free(n->struct_decl.type_params[i]);
        free(n->struct_decl.type_params);
        break;
    case NODE_ENUM_DECL:
        free(n->enum_decl.name);
        enumvariantlist_free(&n->enum_decl.variants);
        for (int i = 0; i < n->enum_decl.n_type_params; i++) free(n->enum_decl.type_params[i]);
        free(n->enum_decl.type_params);
        break;
    case NODE_IMPL_DECL:
        free(n->impl_decl.type_name);
        free(n->impl_decl.trait_name);
        nodelist_free(&n->impl_decl.members);
        break;
    case NODE_TRAIT_DECL:
        free(n->trait_decl.name);
        for (int i = 0; i < n->trait_decl.n_assoc_types; i++) free(n->trait_decl.assoc_types[i]);
        free(n->trait_decl.assoc_types);
        for (int i = 0; i < n->trait_decl.n_methods; i++) free(n->trait_decl.method_names[i]);
        free(n->trait_decl.method_names);
        free(n->trait_decl.super_trait);
        nodelist_free(&n->trait_decl.methods);
        break;
    case NODE_IMPORT:
        free_string_array(n->import.path, n->import.nparts);
        free(n->import.alias);
        free_string_array(n->import.items, n->import.nitems);
        break;
    case NODE_USE:
        free(n->use_.path);
        free(n->use_.alias);
        free_string_array(n->use_.names, n->use_.nnames);
        free_string_array(n->use_.name_aliases, n->use_.nnames);
        break;
    case NODE_MODULE_DECL:
        free(n->module_decl.name);
        nodelist_free(&n->module_decl.body);
        break;
    case NODE_TYPE_ALIAS:
        free(n->type_alias.name);
        free(n->type_alias.target);
        break;
    case NODE_PAT_WILD:
        break;
    case NODE_PAT_IDENT:
        free(n->pat_ident.name);
        break;
    case NODE_PAT_LIT:
        free(n->pat_lit.sval);
        break;
    case NODE_PAT_TUPLE:
        nodelist_free(&n->pat_tuple.elems);
        break;
    case NODE_PAT_STRUCT:
        free(n->pat_struct.path);
        nodepairlist_free(&n->pat_struct.fields);
        nodelist_free(&n->pat_struct.defaults);
        break;
    case NODE_PAT_ENUM:
        free(n->pat_enum.path);
        nodelist_free(&n->pat_enum.args);
        break;
    case NODE_PAT_OR:
        node_free(n->pat_or.left);
        node_free(n->pat_or.right);
        break;
    case NODE_PAT_RANGE:
        node_free(n->pat_range.start);
        node_free(n->pat_range.end);
        break;
    case NODE_PAT_SLICE:
        nodelist_free(&n->pat_slice.elems);
        free(n->pat_slice.rest);
        break;
    case NODE_PAT_GUARD:
        node_free(n->pat_guard.pattern);
        node_free(n->pat_guard.guard);
        break;
    case NODE_PAT_EXPR:
        node_free(n->pat_expr.expr);
        break;
    case NODE_PAT_CAPTURE:
        free(n->pat_capture.name);
        node_free(n->pat_capture.pattern);
        break;
    case NODE_PAT_STRING_CONCAT:
        free(n->pat_str_concat.prefix);
        node_free(n->pat_str_concat.rest);
        break;
    case NODE_PAT_REGEX:
        free(n->pat_regex.pattern);
        break;
    case NODE_INLINE_C:
        free(n->inline_c.code);
        break;
    case NODE_TAG_DECL:
        free(n->tag_decl.name);
        paramlist_free(&n->tag_decl.params);
        node_free(n->tag_decl.body);
        break;
    case NODE_BIND:
        free(n->bind_decl.name);
        node_free(n->bind_decl.expr);
        break;
    case NODE_ADAPT_FN:
        free(n->adapt_fn.name);
        paramlist_free(&n->adapt_fn.params);
        typeexpr_free(n->adapt_fn.ret_type);
        for (int i = 0; i < n->adapt_fn.nbranches; i++) {
            free(n->adapt_fn.targets[i]);
            node_free(n->adapt_fn.bodies[i]);
        }
        free(n->adapt_fn.targets);
        free(n->adapt_fn.bodies);
        break;
    case NODE_PROGRAM:
        nodelist_free(&n->program.stmts);
        break;
    case NODE_EFFECT_DECL:
        free(n->effect_decl.name);
        nodelist_free(&n->effect_decl.ops);
        break;
    case NODE_PERFORM:
        free(n->perform.effect_name);
        free(n->perform.op_name);
        nodelist_free(&n->perform.args);
        break;
    case NODE_HANDLE:
        node_free(n->handle.expr);
        effectarmlist_free(&n->handle.arms);
        break;
    case NODE_RESUME:
        node_free(n->resume_.value);
        break;
    case NODE_AWAIT:
        node_free(n->await_.expr);
        break;
    case NODE_NURSERY:
        node_free(n->nursery_.body);
        break;
    case NODE_SPAWN:
        node_free(n->spawn_.expr);
        break;
    case NODE_ACTOR_DECL:
        free(n->actor_decl.name);
        nodepairlist_free(&n->actor_decl.state_fields);
        nodelist_free(&n->actor_decl.methods);
        break;
    case NODE_SEND_EXPR:
        node_free(n->send_expr.target);
        node_free(n->send_expr.message);
        break;
    }
    free(n);
}

Node *program_new(NodeList stmts, Span span) {
    Node *n = node_new(NODE_PROGRAM, span);
    n->program.stmts = stmts;
    return n;
}
