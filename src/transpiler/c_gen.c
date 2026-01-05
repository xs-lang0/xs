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
            emit_expr(s, n->lit_map.keys.items[i], depth);
            sb_add(s, ", ");
            emit_expr(s, n->lit_map.vals.items[i], depth);
        }
        sb_addc(s, ')');
        break;
    }
    case NODE_IDENT:
        sb_add(s, n->ident.name);
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
        } else {
            emit_expr(s, n->call.callee, depth);
            sb_addc(s, '(');
            for (int i = 0; i < n->call.args.len; i++) {
                if (i) sb_add(s, ", ");
                emit_expr(s, n->call.args.items[i], depth);
            }
            sb_addc(s, ')');
        }
        break;
    }
    case NODE_METHOD_CALL: {
        /* method call -> function call with obj as first arg */
        sb_add(s, n->method_call.method);
        sb_addc(s, '(');
        emit_expr(s, n->method_call.obj, depth);
        for (int i = 0; i < n->method_call.args.len; i++) {
            sb_add(s, ", ");
            emit_expr(s, n->method_call.args.items[i], depth);
        }
        sb_addc(s, ')');
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
        emit_expr(s, n->field.obj, depth);
        sb_printf(s, ".%s", n->field.name);
        break;
    case NODE_SCOPE:
        for (int i = 0; i < n->scope.nparts; i++) {
            if (i) sb_add(s, "_");
            sb_add(s, n->scope.parts[i]);
        }
        break;
    case NODE_ASSIGN:
        emit_expr(s, n->assign.target, depth);
        sb_addc(s, ' ');
        sb_add(s, n->assign.op);
        sb_addc(s, ' ');
        emit_expr(s, n->assign.value, depth);
        break;
    case NODE_RANGE:
        sb_add(s, "xs_range(");
        emit_expr(s, n->range.start, depth);
        sb_add(s, ", ");
        emit_expr(s, n->range.end, depth);
        sb_printf(s, ", %d)", n->range.inclusive ? 1 : 0);
        break;
    case NODE_LAMBDA: {
        /* Lambda -> function pointer (limited in C; emit as named static function) */
        int lbl = defer_label_counter++;
        sb_printf(s, "__xs_lambda_%d", lbl);
        break;
    }
    case NODE_CAST:
        sb_printf(s, "/* (%s) */ ", n->cast.type_name ? n->cast.type_name : "?");
        emit_expr(s, n->cast.expr, depth);
        break;
    case NODE_STRUCT_INIT:
        sb_printf(s, "((%s)", n->struct_init.path ? n->struct_init.path : "xs_val");
        sb_add(s, "{ ");
        for (int i = 0; i < n->struct_init.fields.len; i++) {
            if (i) sb_add(s, ", ");
            sb_printf(s, ".%s = ", n->struct_init.fields.items[i].key);
            emit_expr(s, n->struct_init.fields.items[i].val, depth);
        }
        sb_add(s, " })");
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
        sb_printf(s, "xs_arr_push(&__lc_%d, ", lc_id);
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
        sb_add(s, "(fprintf(stderr, \"xs: await not supported in C target\\n\"), exit(1), XS_NULL)");
        break;
    case NODE_YIELD:
        sb_add(s, "(fprintf(stderr, \"xs: yield not supported in C target\\n\"), exit(1), XS_NULL)");
        break;
    case NODE_SPAWN:
        sb_add(s, "(fprintf(stderr, \"xs: spawn not supported in C target\\n\"), exit(1), XS_NULL)");
        break;
    case NODE_ACTOR_DECL: {
        /* Emit a C struct for actor state + function pointers for methods */
        const char *aname = n->actor_decl.name ? n->actor_decl.name : "Actor";
        sb_printf(s, "({ /* actor %s */\n", aname);
        sb_indent(s, depth + 1);
        sb_printf(s, "typedef struct %s_state {\n", aname);
        /* State fields */
        for (int i = 0; i < n->actor_decl.state_fields.len; i++) {
            sb_indent(s, depth + 2);
            sb_printf(s, "xs_val %s;\n", n->actor_decl.state_fields.items[i].key);
        }
        /* Method function pointers */
        for (int i = 0; i < n->actor_decl.methods.len; i++) {
            Node *m = n->actor_decl.methods.items[i];
            if (m->tag != NODE_FN_DECL) continue;
            sb_indent(s, depth + 2);
            sb_printf(s, "xs_val (*%s)", m->fn_decl.name ? m->fn_decl.name : "method");
            sb_addc(s, '(');
            sb_printf(s, "struct %s_state *self", aname);
            for (int j = 0; j < m->fn_decl.params.len; j++) {
                sb_add(s, ", xs_val ");
                Param *p = &m->fn_decl.params.items[j];
                sb_add(s, p->name ? p->name : "_");
            }
            sb_add(s, ");\n");
        }
        sb_indent(s, depth + 1);
        sb_printf(s, "} %s_state;\n", aname);
        sb_indent(s, depth + 1);
        sb_printf(s, "%s_state __actor = {", aname);
        /* Initialize state fields */
        for (int i = 0; i < n->actor_decl.state_fields.len; i++) {
            if (i) sb_add(s, ", ");
            if (n->actor_decl.state_fields.items[i].val)
                emit_expr(s, n->actor_decl.state_fields.items[i].val, depth + 1);
            else
                sb_add(s, "XS_NULL");
        }
        sb_add(s, "};\n");
        sb_indent(s, depth + 1);
        sb_add(s, "(void)__actor;\n");
        sb_indent(s, depth + 1);
        sb_add(s, "XS_NULL;\n");
        sb_indent(s, depth);
        sb_add(s, "})");
        break;
    }
    case NODE_SEND_EXPR:
        sb_add(s, "/* send */ ");
        emit_expr(s, n->send_expr.target, depth);
        sb_add(s, " /* ! */ ");
        emit_expr(s, n->send_expr.message, depth);
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
        sb_add(s, "/* return expr */ XS_NULL");
        break;
    case NODE_IF: {
        /* if as expression — ternary */
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
            if (i == 0) sb_add(s, "if (");
            else sb_add(s, "else if (");
            emit_pattern_cond(s, arm->pattern, "__subject", depth + 1);
            if (arm->guard) {
                sb_add(s, " && xs_truthy(");
                emit_expr(s, arm->guard, depth + 1);
                sb_addc(s, ')');
            }
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
        sb_add(s, "/* pattern */ XS_NULL");
        break;
    /* declaration nodes as expressions emit their identifier */
    case NODE_FN_DECL:
        if (n->fn_decl.name) sb_add(s, n->fn_decl.name);
        else sb_add(s, "XS_NULL");
        break;
    case NODE_STRUCT_DECL:
    case NODE_ENUM_DECL:
    case NODE_CLASS_DECL:
    case NODE_TRAIT_DECL:
    case NODE_IMPL_DECL:
    case NODE_TYPE_ALIAS:
    case NODE_IMPORT:
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
    case NODE_NURSERY:
    case NODE_HANDLE:
    case NODE_DEFER:
    case NODE_TRY:
    case NODE_WHILE:
    case NODE_FOR:
    case NODE_LOOP:
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
    case NODE_PAT_GUARD:
        emit_pattern_cond(s, pat->pat_guard.pattern, subject, depth);
        sb_add(s, " && xs_truthy(");
        emit_expr(s, pat->pat_guard.guard, depth);
        sb_addc(s, ')');
        break;
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
            sb_printf(s, "(%s.tag != 4", subject);
        } else {
            sb_add(s, "(1");
        }
        for (int i = 0; i < pat->pat_enum.args.len; i++) {
            char sub[256];
            snprintf(sub, sizeof sub, "xs_index(%s, XS_INT(%d))", subject, i);
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
    default:
        break;
    }
}

/* emit block body */
static void emit_block_body(SB *s, Node *block, int depth) {
    if (!block || block->tag != NODE_BLOCK) return;
    for (int i = 0; i < block->block.stmts.len; i++) {
        emit_stmt(s, block->block.stmts.items[i], depth);
    }
}

/* statement emitter */
static void emit_stmt(SB *s, Node *n, int depth) {
    if (!n) return;
    switch (n->tag) {
    case NODE_LET:
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
        break;
    case NODE_VAR:
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
        break;
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
            sb_add(s, n->fn_decl.name);
            emit_params_c(s, &n->fn_decl.params);
            sb_add(s, " {\n");
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
                    emit_block_body(s, n->fn_decl.body, depth + 1);
                    if (n->fn_decl.body->block.expr) {
                        sb_indent(s, depth + 1);
                        sb_add(s, "xs_run_defers(__saved_defer_top);\n");
                        sb_indent(s, depth + 1);
                        sb_add(s, "xs_pop_frame();\n");
                        sb_indent(s, depth + 1);
                        sb_add(s, "return ");
                        emit_expr(s, n->fn_decl.body->block.expr, depth + 1);
                        sb_add(s, ";\n");
                    } else {
                        sb_indent(s, depth + 1);
                        sb_add(s, "xs_run_defers(__saved_defer_top);\n");
                        sb_indent(s, depth + 1);
                        sb_add(s, "xs_pop_frame();\n");
                        sb_indent(s, depth + 1);
                        sb_add(s, "return XS_NULL;\n");
                    }
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
            if (i == 0) sb_add(s, "if (");
            else sb_add(s, "else if (");
            emit_pattern_cond(s, arm->pattern, "__subject", depth + 1);
            if (arm->guard) {
                sb_add(s, " && xs_truthy(");
                emit_expr(s, arm->guard, depth + 1);
                sb_addc(s, ')');
            }
            sb_add(s, ") {\n");
            emit_pattern_bindings(s, arm->pattern, "__subject", depth + 2);
            if (arm->body && arm->body->tag == NODE_BLOCK) {
                emit_block_body(s, arm->body, depth + 2);
            } else if (arm->body) {
                sb_indent(s, depth + 2);
                emit_expr(s, arm->body, depth + 2);
                sb_add(s, ";\n");
            }
            sb_indent(s, depth + 1);
            sb_add(s, "}\n");
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
                /* Rename method to TypeName_methodName */
                sb_indent(s, depth);
                sb_add(s, "xs_val ");
                sb_add(s, n->impl_decl.type_name);
                sb_addc(s, '_');
                sb_add(s, m->fn_decl.name);
                sb_addc(s, '(');
                /* First param is self */
                sb_printf(s, "%s *self", n->impl_decl.type_name);
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
        sb_indent(s, depth);
        sb_add(s, "#include \"");
        for (int i = 0; i < n->import.nparts; i++) {
            if (i) sb_addc(s, '/');
            sb_add(s, n->import.path[i]);
        }
        sb_add(s, ".h\"\n");
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
        emit_expr(s, n->assign.target, depth);
        sb_addc(s, ' ');
        sb_add(s, n->assign.op);
        sb_addc(s, ' ');
        emit_expr(s, n->assign.value, depth);
        sb_add(s, ";\n");
        break;
    case NODE_EXPR_STMT:
        sb_indent(s, depth);
        emit_expr(s, n->expr_stmt.expr, depth);
        sb_add(s, ";\n");
        break;
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
    case NODE_SPAWN:
    case NODE_PERFORM:
    case NODE_RESUME:
    case NODE_AWAIT:
    case NODE_ACTOR_DECL:
    case NODE_SEND_EXPR:
        sb_indent(s, depth);
        emit_expr(s, n, depth);
        sb_add(s, ";\n");
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
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdint.h>\n"
        "#include <stdarg.h>\n"
        "#include <math.h>\n"
        "#include <setjmp.h>\n\n"
        "/* ── XS runtime types ── */\n"
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
        "static int xs_eq(xs_val a, xs_val b) {\n"
        "    if (a.tag != b.tag) return 0;\n"
        "    switch (a.tag) {\n"
        "        case 0: return a.i == b.i;\n"
        "        case 1: return a.f == b.f;\n"
        "        case 2: return a.s && b.s && strcmp(a.s, b.s) == 0;\n"
        "        case 3: return a.b == b.b;\n"
        "        case 4: return 1;\n"
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
        "static const char *xs_to_str(xs_val v) {\n"
        "    static char buf[256];\n"
        "    switch (v.tag) {\n"
        "        case 0: snprintf(buf, sizeof buf, \"%lld\", (long long)v.i); return buf;\n"
        "        case 1: snprintf(buf, sizeof buf, \"%g\", v.f); return buf;\n"
        "        case 2: return v.s ? v.s : \"null\";\n"
        "        case 3: return v.b ? \"true\" : \"false\";\n"
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
        "static void xs_println(xs_val v) {\n"
        "    switch(v.tag) {\n"
        "        case 0: printf(\"%lld\\n\", (long long)v.i); break;\n"
        "        case 1: printf(\"%g\\n\", v.f); break;\n"
        "        case 2: printf(\"%s\\n\", v.s ? v.s : \"null\"); break;\n"
        "        case 3: printf(\"%s\\n\", v.b ? \"true\" : \"false\"); break;\n"
        "        default: printf(\"null\\n\");\n"
        "    }\n"
        "}\n\n"
        "static void xs_print(xs_val v) {\n"
        "    switch(v.tag) {\n"
        "        case 0: printf(\"%lld\", (long long)v.i); break;\n"
        "        case 1: printf(\"%g\", v.f); break;\n"
        "        case 2: printf(\"%s\", v.s ? v.s : \"null\"); break;\n"
        "        case 3: printf(\"%s\", v.b ? \"true\" : \"false\"); break;\n"
        "        default: printf(\"null\");\n"
        "    }\n"
        "}\n\n"
        "/* ── exception handling runtime ── */\n"
        "#define XS_MAX_HANDLERS 64\n"
        "static jmp_buf *__xs_handlers[XS_MAX_HANDLERS];\n"
        "static int __xs_handler_top = 0;\n"
        "static xs_val __xs_exception = {.tag=4};\n"
        "static int __xs_exception_tag = 0; /* 0=none, 1=string, 2=int, 3=float, 4=bool, 5=array, 6=map */\n"
        "static int __xs_in_catch = 0; /* nonzero when executing inside a catch block */\n\n"

        "/* ── call-stack tracing ── */\n"
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

        "/* ── defer stack ── */\n"
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

        "/* ── finally handler stack ── */\n"
        "typedef void (*xs_finally_fn)(void);\n"
        "#define XS_MAX_FINALLY 64\n"
        "static xs_finally_fn __xs_finally_handlers[XS_MAX_FINALLY];\n"
        "static int __xs_finally_top = 0;\n"
        "static void xs_push_finally(xs_finally_fn fn) {\n"
        "    if (__xs_finally_top < XS_MAX_FINALLY) __xs_finally_handlers[__xs_finally_top++] = fn;\n"
        "}\n"
        "static void xs_pop_finally(void) { if (__xs_finally_top > 0) __xs_finally_top--; }\n\n"

        "/* ── handler push/pop ── */\n"
        "static void xs_push_handler(jmp_buf *jb) {\n"
        "    if (__xs_handler_top < XS_MAX_HANDLERS) __xs_handlers[__xs_handler_top++] = jb;\n"
        "}\n"
        "static void xs_pop_handler(void) { if (__xs_handler_top > 0) __xs_handler_top--; }\n\n"

        "/* ── throw / rethrow ── */\n"
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
        "/* ── array/map heap types ── */\n"
        "typedef struct { xs_val *items; int len; int cap; } xs_arr;\n"
        "typedef struct { char **keys; xs_val *vals; int len; int cap; } xs_hmap;\n\n"
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
        "static void xs_arr_push(xs_val *arr, xs_val v) {\n"
        "    if (arr->tag != 5 || !arr->p) return;\n"
        "    xs_arr *a = (xs_arr*)arr->p;\n"
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
    );

    if (!program) {
        sb_add(&s, "/* (empty program) */\n");
        return s.data;
    }

    if (program->tag == NODE_PROGRAM) {
        for (int i = 0; i < program->program.stmts.len; i++) {
            emit_stmt(&s, program->program.stmts.items[i], 0);
        }
    } else {
        emit_stmt(&s, program, 0);
    }

    return s.data;
}
