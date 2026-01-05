#include "fmt/fmt.h"
#include "core/xs_compat.h"
#include "core/lexer.h"
#include "core/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include "core/strbuf.h"

static CommentList *fmt_comments = NULL;
static int          fmt_comment_idx = 0;

static int fmt_emit_leading_comments(SB *s, int node_line, int depth) {
    int count = 0;
    if (!fmt_comments) return 0;
    while (fmt_comment_idx < fmt_comments->len) {
        Comment *c = &fmt_comments->items[fmt_comment_idx];
        if (c->line >= node_line) break;
        sb_indent(s, depth);
        sb_add(s, c->text);
        sb_addc(s, '\n');
        fmt_comment_idx++;
        count++;
    }
    return count;
}

static void fmt_node(SB *s, Node *n, int depth);
static void fmt_expr(SB *s, Node *n, int depth);
static void fmt_stmt(SB *s, Node *n, int depth);
static void fmt_pattern(SB *s, Node *n, int depth);
static void fmt_type(SB *s, TypeExpr *te);

static void fmt_params(SB *s, ParamList *pl) {
    sb_addc(s, '(');
    for (int i = 0; i < pl->len; i++) {
        if (i > 0) sb_add(s, ", ");
        Param *p = &pl->items[i];
        if (p->variadic) sb_add(s, "...");
        if (p->name) {
            sb_add(s, p->name);
        } else if (p->pattern) {
            fmt_pattern(s, p->pattern, 0);
        }
        if (p->type_ann) {
            sb_add(s, ": ");
            fmt_type(s, p->type_ann);
        }
        if (p->default_val) {
            sb_add(s, " = ");
            fmt_expr(s, p->default_val, 0);
        }
    }
    sb_addc(s, ')');
}

static void fmt_type(SB *s, TypeExpr *te) {
    if (!te) return;
    switch (te->kind) {
    case TEXPR_NAMED:
        if (te->name) sb_add(s, te->name);
        if (te->nargs > 0) {
            sb_addc(s, '<');
            for (int i = 0; i < te->nargs; i++) {
                if (i > 0) sb_add(s, ", ");
                fmt_type(s, te->args[i]);
            }
            sb_addc(s, '>');
        }
        break;
    case TEXPR_ARRAY:
        sb_addc(s, '[');
        fmt_type(s, te->inner);
        sb_addc(s, ']');
        break;
    case TEXPR_TUPLE:
        sb_addc(s, '(');
        for (int i = 0; i < te->nelems; i++) {
            if (i > 0) sb_add(s, ", ");
            fmt_type(s, te->elems[i]);
        }
        sb_addc(s, ')');
        break;
    case TEXPR_FN:
        sb_add(s, "fn(");
        for (int i = 0; i < te->nparams; i++) {
            if (i > 0) sb_add(s, ", ");
            fmt_type(s, te->params[i]);
        }
        sb_addc(s, ')');
        if (te->ret) {
            sb_add(s, " -> ");
            fmt_type(s, te->ret);
        }
        break;
    case TEXPR_OPTION:
        fmt_type(s, te->inner);
        sb_addc(s, '?');
        break;
    case TEXPR_INFER:
        sb_addc(s, '_');
        break;
    }
}

static void fmt_block_body(SB *s, Node *block, int depth) {
    if (!block) return;
    if (block->tag == NODE_BLOCK) {
        for (int i = 0; i < block->block.stmts.len; i++) {
            fmt_stmt(s, block->block.stmts.items[i], depth);
        }
        if (block->block.expr) {
            sb_indent(s, depth);
            fmt_expr(s, block->block.expr, depth);
            sb_addc(s, '\n');
        }
    } else {
        fmt_stmt(s, block, depth);
    }
}

static void fmt_block(SB *s, Node *block, int depth) {
    sb_add(s, "{\n");
    fmt_block_body(s, block, depth + 1);
    sb_indent(s, depth);
    sb_addc(s, '}');
}


static void fmt_pattern(SB *s, Node *n, int depth) {
    (void)depth;
    if (!n) { sb_add(s, "_"); return; }
    switch (n->tag) {
    case NODE_PAT_WILD:
        sb_addc(s, '_');
        break;
    case NODE_PAT_IDENT:
        if (n->pat_ident.mutable) sb_add(s, "mut ");
        sb_add(s, n->pat_ident.name);
        break;
    case NODE_PAT_LIT:
        switch (n->pat_lit.tag) {
        case 0: sb_printf(s, "%" PRId64, n->pat_lit.ival); break;
        case 1: sb_printf(s, "%g", n->pat_lit.fval); break;
        case 2: sb_printf(s, "\"%s\"", n->pat_lit.sval ? n->pat_lit.sval : ""); break;
        case 3: sb_add(s, n->pat_lit.bval ? "true" : "false"); break;
        case 4: sb_add(s, "null"); break;
        default: sb_add(s, "?"); break;
        }
        break;
    case NODE_PAT_TUPLE:
        sb_addc(s, '(');
        for (int i = 0; i < n->pat_tuple.elems.len; i++) {
            if (i > 0) sb_add(s, ", ");
            fmt_pattern(s, n->pat_tuple.elems.items[i], 0);
        }
        sb_addc(s, ')');
        break;
    case NODE_PAT_STRUCT:
        if (n->pat_struct.path) sb_add(s, n->pat_struct.path);
        sb_add(s, " { ");
        for (int i = 0; i < n->pat_struct.fields.len; i++) {
            if (i > 0) sb_add(s, ", ");
            sb_add(s, n->pat_struct.fields.items[i].key);
            if (n->pat_struct.fields.items[i].val) {
                sb_add(s, ": ");
                fmt_pattern(s, n->pat_struct.fields.items[i].val, 0);
            }
        }
        if (n->pat_struct.rest) sb_add(s, ", ..");
        sb_add(s, " }");
        break;
    case NODE_PAT_ENUM:
        if (n->pat_enum.path) sb_add(s, n->pat_enum.path);
        if (n->pat_enum.args.len > 0) {
            sb_addc(s, '(');
            for (int i = 0; i < n->pat_enum.args.len; i++) {
                if (i > 0) sb_add(s, ", ");
                fmt_pattern(s, n->pat_enum.args.items[i], 0);
            }
            sb_addc(s, ')');
        }
        break;
    case NODE_PAT_OR:
        fmt_pattern(s, n->pat_or.left, 0);
        sb_add(s, " | ");
        fmt_pattern(s, n->pat_or.right, 0);
        break;
    case NODE_PAT_RANGE:
        fmt_pattern(s, n->pat_range.start, 0);
        sb_add(s, n->pat_range.inclusive ? "..=" : "..");
        fmt_pattern(s, n->pat_range.end, 0);
        break;
    case NODE_PAT_SLICE:
        sb_addc(s, '[');
        for (int i = 0; i < n->pat_slice.elems.len; i++) {
            if (i > 0) sb_add(s, ", ");
            fmt_pattern(s, n->pat_slice.elems.items[i], 0);
        }
        if (n->pat_slice.rest) {
            if (n->pat_slice.elems.len > 0) sb_add(s, ", ");
            sb_add(s, "..");
            sb_add(s, n->pat_slice.rest);
        }
        sb_addc(s, ']');
        break;
    case NODE_PAT_GUARD:
        fmt_pattern(s, n->pat_guard.pattern, 0);
        sb_add(s, " if ");
        fmt_expr(s, n->pat_guard.guard, 0);
        break;
    case NODE_PAT_EXPR:
        fmt_expr(s, n->pat_expr.expr, 0);
        break;
    case NODE_PAT_CAPTURE:
        sb_add(s, n->pat_capture.name);
        sb_add(s, " @ ");
        fmt_pattern(s, n->pat_capture.pattern, 0);
        break;
    case NODE_PAT_STRING_CONCAT:
        sb_add(s, "\"");
        sb_add(s, n->pat_str_concat.prefix);
        sb_add(s, "\" ++ ");
        fmt_pattern(s, n->pat_str_concat.rest, 0);
        break;
    default:
        fmt_expr(s, n, 0);
        break;
    }
}

/* Expressions */
static void fmt_expr(SB *s, Node *n, int depth) {
    if (!n) return;
    switch (n->tag) {
    case NODE_LIT_INT:
        sb_printf(s, "%" PRId64, n->lit_int.ival);
        break;
    case NODE_LIT_BIGINT:
        sb_add(s, n->lit_bigint.bigint_str);
        break;
    case NODE_LIT_FLOAT: {
        char buf[64];
        snprintf(buf, sizeof buf, "%g", n->lit_float.fval);
        sb_add(s, buf);
        break;
    }
    case NODE_LIT_STRING:
        sb_addc(s, '"');
        if (n->lit_string.sval) sb_add(s, n->lit_string.sval);
        sb_addc(s, '"');
        break;
    case NODE_INTERP_STRING:
        sb_addc(s, '"');
        if (n->lit_string.sval) sb_add(s, n->lit_string.sval);
        sb_addc(s, '"');
        break;
    case NODE_LIT_BOOL:
        sb_add(s, n->lit_bool.bval ? "true" : "false");
        break;
    case NODE_LIT_NULL:
        sb_add(s, "null");
        break;
    case NODE_LIT_CHAR:
        sb_printf(s, "'%c'", n->lit_char.cval);
        break;
    case NODE_LIT_ARRAY:
        sb_addc(s, '[');
        for (int i = 0; i < n->lit_array.elems.len; i++) {
            if (i > 0) sb_add(s, ", ");
            fmt_expr(s, n->lit_array.elems.items[i], depth);
        }
        sb_addc(s, ']');
        break;
    case NODE_LIT_TUPLE:
        sb_addc(s, '(');
        for (int i = 0; i < n->lit_array.elems.len; i++) {
            if (i > 0) sb_add(s, ", ");
            fmt_expr(s, n->lit_array.elems.items[i], depth);
        }
        sb_addc(s, ')');
        break;
    case NODE_LIT_MAP:
        sb_addc(s, '{');
        for (int i = 0; i < n->lit_map.keys.len; i++) {
            if (i > 0) sb_add(s, ", ");
            fmt_expr(s, n->lit_map.keys.items[i], depth);
            sb_add(s, ": ");
            fmt_expr(s, n->lit_map.vals.items[i], depth);
        }
        sb_addc(s, '}');
        break;
    case NODE_IDENT:
        sb_add(s, n->ident.name);
        break;
    case NODE_BINOP:
        fmt_expr(s, n->binop.left, depth);
        sb_printf(s, " %s ", n->binop.op);
        fmt_expr(s, n->binop.right, depth);
        break;
    case NODE_UNARY:
        if (n->unary.prefix) {
            sb_add(s, n->unary.op);
            fmt_expr(s, n->unary.expr, depth);
        } else {
            fmt_expr(s, n->unary.expr, depth);
            sb_add(s, n->unary.op);
        }
        break;
    case NODE_ASSIGN:
        fmt_expr(s, n->assign.target, depth);
        if (n->assign.op[0] == '=' && n->assign.op[1] == '\0')
            sb_add(s, " = ");
        else
            sb_printf(s, " %s ", n->assign.op);
        fmt_expr(s, n->assign.value, depth);
        break;
    case NODE_CALL:
        fmt_expr(s, n->call.callee, depth);
        sb_addc(s, '(');
        for (int i = 0; i < n->call.args.len; i++) {
            if (i > 0) sb_add(s, ", ");
            fmt_expr(s, n->call.args.items[i], depth);
        }
        for (int i = 0; i < n->call.kwargs.len; i++) {
            if (n->call.args.len > 0 || i > 0) sb_add(s, ", ");
            sb_add(s, n->call.kwargs.items[i].key);
            sb_add(s, ": ");
            fmt_expr(s, n->call.kwargs.items[i].val, depth);
        }
        sb_addc(s, ')');
        break;
    case NODE_METHOD_CALL:
        fmt_expr(s, n->method_call.obj, depth);
        sb_add(s, n->method_call.optional ? "?." : ".");
        sb_add(s, n->method_call.method);
        sb_addc(s, '(');
        for (int i = 0; i < n->method_call.args.len; i++) {
            if (i > 0) sb_add(s, ", ");
            fmt_expr(s, n->method_call.args.items[i], depth);
        }
        for (int i = 0; i < n->method_call.kwargs.len; i++) {
            if (n->method_call.args.len > 0 || i > 0) sb_add(s, ", ");
            sb_add(s, n->method_call.kwargs.items[i].key);
            sb_add(s, ": ");
            fmt_expr(s, n->method_call.kwargs.items[i].val, depth);
        }
        sb_addc(s, ')');
        break;
    case NODE_INDEX:
        fmt_expr(s, n->index.obj, depth);
        sb_addc(s, '[');
        fmt_expr(s, n->index.index, depth);
        sb_addc(s, ']');
        break;
    case NODE_FIELD:
        fmt_expr(s, n->field.obj, depth);
        sb_add(s, n->field.optional ? "?." : ".");
        sb_add(s, n->field.name);
        break;
    case NODE_SCOPE:
        for (int i = 0; i < n->scope.nparts; i++) {
            if (i > 0) sb_add(s, "::");
            sb_add(s, n->scope.parts[i]);
        }
        break;
    case NODE_RANGE:
        if (n->range.start) fmt_expr(s, n->range.start, depth);
        sb_add(s, n->range.inclusive ? "..=" : "..");
        if (n->range.end) fmt_expr(s, n->range.end, depth);
        break;
    case NODE_CAST:
        fmt_expr(s, n->cast.expr, depth);
        sb_add(s, " as ");
        sb_add(s, n->cast.type_name);
        break;
    case NODE_SPREAD:
        sb_add(s, "...");
        fmt_expr(s, n->spread.expr, depth);
        break;
    case NODE_STRUCT_INIT:
        sb_add(s, n->struct_init.path);
        sb_add(s, " { ");
        for (int i = 0; i < n->struct_init.fields.len; i++) {
            if (i > 0) sb_add(s, ", ");
            sb_add(s, n->struct_init.fields.items[i].key);
            sb_add(s, ": ");
            fmt_expr(s, n->struct_init.fields.items[i].val, depth);
        }
        if (n->struct_init.rest) {
            if (n->struct_init.fields.len > 0) sb_add(s, ", ");
            sb_add(s, "..");
            fmt_expr(s, n->struct_init.rest, depth);
        }
        sb_add(s, " }");
        break;
    case NODE_IF:
        sb_add(s, "if ");
        fmt_expr(s, n->if_expr.cond, depth);
        sb_addc(s, ' ');
        fmt_block(s, n->if_expr.then, depth);
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            sb_add(s, " else if ");
            fmt_expr(s, n->if_expr.elif_conds.items[i], depth);
            sb_addc(s, ' ');
            fmt_block(s, n->if_expr.elif_thens.items[i], depth);
        }
        if (n->if_expr.else_branch) {
            sb_add(s, " else ");
            fmt_block(s, n->if_expr.else_branch, depth);
        }
        break;
    case NODE_MATCH:
        sb_add(s, "match ");
        fmt_expr(s, n->match.subject, depth);
        sb_add(s, " {\n");
        for (int i = 0; i < n->match.arms.len; i++) {
            MatchArm *arm = &n->match.arms.items[i];
            sb_indent(s, depth + 1);
            fmt_pattern(s, arm->pattern, depth + 1);
            if (arm->guard) {
                sb_add(s, " if ");
                fmt_expr(s, arm->guard, depth + 1);
            }
            sb_add(s, " => ");
            if (arm->body && arm->body->tag == NODE_BLOCK) {
                fmt_block(s, arm->body, depth + 1);
                sb_addc(s, '\n');
            } else {
                fmt_expr(s, arm->body, depth + 1);
                sb_addc(s, '\n');
            }
        }
        sb_indent(s, depth);
        sb_addc(s, '}');
        break;
    case NODE_BLOCK:
        fmt_block(s, n, depth);
        break;
    case NODE_LAMBDA:
        sb_add(s, "fn");
        fmt_params(s, &n->lambda.params);
        sb_addc(s, ' ');
        if (n->lambda.body && n->lambda.body->tag == NODE_BLOCK) {
            fmt_block(s, n->lambda.body, depth);
        } else {
            sb_add(s, "{ ");
            fmt_expr(s, n->lambda.body, depth);
            sb_add(s, " }");
        }
        break;
    case NODE_LIST_COMP:
        sb_addc(s, '[');
        fmt_expr(s, n->list_comp.element, depth);
        for (int i = 0; i < n->list_comp.clause_pats.len; i++) {
            sb_add(s, " for ");
            fmt_pattern(s, n->list_comp.clause_pats.items[i], depth);
            sb_add(s, " in ");
            fmt_expr(s, n->list_comp.clause_iters.items[i], depth);
            if (i < n->list_comp.clause_conds.len && n->list_comp.clause_conds.items[i]) {
                sb_add(s, " if ");
                fmt_expr(s, n->list_comp.clause_conds.items[i], depth);
            }
        }
        sb_addc(s, ']');
        break;
    case NODE_MAP_COMP:
        sb_addc(s, '{');
        fmt_expr(s, n->map_comp.key, depth);
        sb_add(s, ": ");
        fmt_expr(s, n->map_comp.value, depth);
        for (int i = 0; i < n->map_comp.clause_pats.len; i++) {
            sb_add(s, " for ");
            fmt_pattern(s, n->map_comp.clause_pats.items[i], depth);
            sb_add(s, " in ");
            fmt_expr(s, n->map_comp.clause_iters.items[i], depth);
            if (i < n->map_comp.clause_conds.len && n->map_comp.clause_conds.items[i]) {
                sb_add(s, " if ");
                fmt_expr(s, n->map_comp.clause_conds.items[i], depth);
            }
        }
        sb_addc(s, '}');
        break;
    case NODE_PERFORM:
        sb_add(s, "perform ");
        sb_add(s, n->perform.effect_name);
        sb_addc(s, '.');
        sb_add(s, n->perform.op_name);
        sb_addc(s, '(');
        for (int i = 0; i < n->perform.args.len; i++) {
            if (i > 0) sb_add(s, ", ");
            fmt_expr(s, n->perform.args.items[i], depth);
        }
        sb_addc(s, ')');
        break;
    case NODE_HANDLE:
        sb_add(s, "handle ");
        fmt_expr(s, n->handle.expr, depth);
        sb_add(s, " {\n");
        for (int i = 0; i < n->handle.arms.len; i++) {
            EffectArm *arm = &n->handle.arms.items[i];
            sb_indent(s, depth + 1);
            sb_add(s, arm->effect_name);
            sb_addc(s, '.');
            sb_add(s, arm->op_name);
            fmt_params(s, &arm->params);
            sb_add(s, " => ");
            if (arm->body && arm->body->tag == NODE_BLOCK)
                fmt_block(s, arm->body, depth + 1);
            else
                fmt_expr(s, arm->body, depth + 1);
            sb_addc(s, '\n');
        }
        sb_indent(s, depth);
        sb_addc(s, '}');
        break;
    case NODE_RESUME:
        sb_add(s, "resume");
        if (n->resume_.value) {
            sb_addc(s, ' ');
            fmt_expr(s, n->resume_.value, depth);
        }
        break;
    case NODE_AWAIT:
        sb_add(s, "await ");
        fmt_expr(s, n->await_.expr, depth);
        break;
    case NODE_NURSERY:
        sb_add(s, "nursery ");
        fmt_block(s, n->nursery_.body, depth);
        break;
    case NODE_SPAWN:
        sb_add(s, "spawn ");
        fmt_expr(s, n->spawn_.expr, depth);
        break;
    case NODE_RETURN:
        sb_add(s, "return");
        if (n->ret.value) {
            sb_addc(s, ' ');
            fmt_expr(s, n->ret.value, depth);
        }
        break;
    case NODE_BREAK:
        sb_add(s, "break");
        if (n->brk.label) sb_printf(s, " '%s", n->brk.label);
        if (n->brk.value) {
            sb_addc(s, ' ');
            fmt_expr(s, n->brk.value, depth);
        }
        break;
    case NODE_CONTINUE:
        sb_add(s, "continue");
        if (n->cont.label) sb_printf(s, " '%s", n->cont.label);
        break;
    case NODE_THROW:
        sb_add(s, "throw ");
        fmt_expr(s, n->throw_.value, depth);
        break;
    case NODE_YIELD:
        sb_add(s, "yield");
        if (n->yield_.value) {
            sb_addc(s, ' ');
            fmt_expr(s, n->yield_.value, depth);
        }
        break;
    default:
        fmt_stmt(s, n, depth);
        break;
    }
}

// Statements

static void fmt_stmt_core(SB *s, Node *n, int depth);
static void fmt_stmt(SB *s, Node *n, int depth) {
    if (!n) return;
    fmt_emit_leading_comments(s, n->span.line, depth);
    int before_len = s->len;
    fmt_stmt_core(s, n, depth);
    int end_line = n->span.end_line ? n->span.end_line : n->span.line;
    if (fmt_comments && fmt_comment_idx < fmt_comments->len) {
        Comment *c = &fmt_comments->items[fmt_comment_idx];
        if (c->line == end_line && s->len > before_len && s->data[s->len - 1] == '\n') {
            s->len--;  /* remove trailing \n */
            s->data[s->len] = '\0';
            sb_add(s, " ");
            sb_add(s, c->text);
            sb_addc(s, '\n');
            fmt_comment_idx++;
        }
    }
}
static void fmt_stmt_core(SB *s, Node *n, int depth) {
    if (!n) return;
    switch (n->tag) {
    case NODE_EXPR_STMT:
        sb_indent(s, depth);
        fmt_expr(s, n->expr_stmt.expr, depth);
        sb_addc(s, '\n');
        break;
    case NODE_LET:
    case NODE_VAR:
        sb_indent(s, depth);
        sb_add(s, n->let.mutable ? "var " : "let ");
        if (n->let.pattern) {
            fmt_pattern(s, n->let.pattern, depth);
        } else if (n->let.name) {
            sb_add(s, n->let.name);
        }
        if (n->let.type_ann) {
            sb_add(s, ": ");
            fmt_type(s, n->let.type_ann);
        }
        if (n->let.value) {
            sb_add(s, " = ");
            fmt_expr(s, n->let.value, depth);
        }
        sb_addc(s, '\n');
        break;
    case NODE_CONST:
        sb_indent(s, depth);
        sb_add(s, "const ");
        sb_add(s, n->const_.name);
        if (n->const_.type_ann) {
            sb_add(s, ": ");
            fmt_type(s, n->const_.type_ann);
        }
        if (n->const_.value) {
            sb_add(s, " = ");
            fmt_expr(s, n->const_.value, depth);
        }
        sb_addc(s, '\n');
        break;
    case NODE_FN_DECL:
        sb_indent(s, depth);
        if (n->fn_decl.is_pub) sb_add(s, "pub ");
        if (n->fn_decl.is_async) sb_add(s, "async ");
        if (n->fn_decl.is_pure) { sb_add(s, "@pure\n"); sb_indent(s, depth); }
        sb_add(s, "fn ");
        if (n->fn_decl.is_generator) sb_addc(s, '*');
        sb_add(s, n->fn_decl.name);
        if (n->fn_decl.n_type_params > 0) {
            sb_addc(s, '<');
            for (int i = 0; i < n->fn_decl.n_type_params; i++) {
                if (i > 0) sb_add(s, ", ");
                sb_add(s, n->fn_decl.type_params[i]);
            }
            sb_addc(s, '>');
        }
        fmt_params(s, &n->fn_decl.params);
        if (n->fn_decl.ret_type) {
            sb_add(s, " -> ");
            fmt_type(s, n->fn_decl.ret_type);
        }
        sb_addc(s, ' ');
        fmt_block(s, n->fn_decl.body, depth);
        sb_addc(s, '\n');
        break;
    case NODE_STRUCT_DECL:
        sb_indent(s, depth);
        sb_add(s, "struct ");
        sb_add(s, n->struct_decl.name);
        if (n->struct_decl.n_type_params > 0) {
            sb_addc(s, '<');
            for (int i = 0; i < n->struct_decl.n_type_params; i++) {
                if (i > 0) sb_add(s, ", ");
                sb_add(s, n->struct_decl.type_params[i]);
            }
            sb_addc(s, '>');
        }
        sb_add(s, " {\n");
        for (int i = 0; i < n->struct_decl.fields.len; i++) {
            sb_indent(s, depth + 1);
            sb_add(s, n->struct_decl.fields.items[i].key);
            if (n->struct_decl.fields.items[i].val) {
                sb_add(s, ": ");
                fmt_expr(s, n->struct_decl.fields.items[i].val, depth + 1);
            }
            sb_addc(s, '\n');
        }
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_ENUM_DECL:
        sb_indent(s, depth);
        sb_add(s, "enum ");
        sb_add(s, n->enum_decl.name);
        if (n->enum_decl.n_type_params > 0) {
            sb_addc(s, '<');
            for (int i = 0; i < n->enum_decl.n_type_params; i++) {
                if (i > 0) sb_add(s, ", ");
                sb_add(s, n->enum_decl.type_params[i]);
            }
            sb_addc(s, '>');
        }
        sb_add(s, " {\n");
        for (int i = 0; i < n->enum_decl.variants.len; i++) {
            EnumVariant *v = &n->enum_decl.variants.items[i];
            sb_indent(s, depth + 1);
            sb_add(s, v->name);
            if (v->fields.len > 0) {
                if (v->is_struct) {
                    sb_add(s, " { ");
                    for (int j = 0; j < v->fields.len; j++) {
                        if (j > 0) sb_add(s, ", ");
                        if (j < v->field_names.len && v->field_names.items[j])
                            fmt_expr(s, v->field_names.items[j], depth);
                        else
                            fmt_expr(s, v->fields.items[j], depth);
                    }
                    sb_add(s, " }");
                } else {
                    sb_addc(s, '(');
                    for (int j = 0; j < v->fields.len; j++) {
                        if (j > 0) sb_add(s, ", ");
                        fmt_expr(s, v->fields.items[j], depth);
                    }
                    sb_addc(s, ')');
                }
            }
            sb_addc(s, '\n');
        }
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_TRAIT_DECL:
        sb_indent(s, depth);
        sb_add(s, "trait ");
        sb_add(s, n->trait_decl.name);
        if (n->trait_decl.super_trait) {
            sb_add(s, ": ");
            sb_add(s, n->trait_decl.super_trait);
        }
        sb_add(s, " {\n");
        for (int i = 0; i < n->trait_decl.n_methods; i++) {
            sb_indent(s, depth + 1);
            sb_add(s, "fn ");
            sb_add(s, n->trait_decl.method_names[i]);
            sb_add(s, "()\n");
        }
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_IMPL_DECL:
        sb_indent(s, depth);
        sb_add(s, "impl ");
        if (n->impl_decl.trait_name) {
            sb_add(s, n->impl_decl.trait_name);
            sb_add(s, " for ");
        }
        sb_add(s, n->impl_decl.type_name);
        sb_add(s, " {\n");
        for (int i = 0; i < n->impl_decl.members.len; i++) {
            fmt_stmt(s, n->impl_decl.members.items[i], depth + 1);
        }
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_CLASS_DECL:
        sb_indent(s, depth);
        sb_add(s, "class ");
        sb_add(s, n->class_decl.name);
        sb_add(s, " {\n");
        for (int i = 0; i < n->class_decl.members.len; i++) {
            fmt_stmt(s, n->class_decl.members.items[i], depth + 1);
        }
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_MODULE_DECL:
        sb_indent(s, depth);
        sb_add(s, "module ");
        sb_add(s, n->module_decl.name);
        sb_add(s, " {\n");
        for (int i = 0; i < n->module_decl.body.len; i++) {
            fmt_stmt(s, n->module_decl.body.items[i], depth + 1);
        }
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_IMPORT:
        sb_indent(s, depth);
        if (n->import.nitems > 0) {
            sb_add(s, "import { ");
            for (int i = 0; i < n->import.nitems; i++) {
                if (i > 0) sb_add(s, ", ");
                sb_add(s, n->import.items[i]);
            }
            sb_add(s, " } from ");
        } else {
            sb_add(s, "import ");
        }
        for (int i = 0; i < n->import.nparts; i++) {
            if (i > 0) sb_addc(s, '/');
            sb_add(s, n->import.path[i]);
        }
        if (n->import.alias) {
            sb_add(s, " as ");
            sb_add(s, n->import.alias);
        }
        sb_addc(s, '\n');
        break;
    case NODE_TYPE_ALIAS:
        sb_indent(s, depth);
        sb_add(s, "type ");
        sb_add(s, n->type_alias.name);
        sb_add(s, " = ");
        sb_add(s, n->type_alias.target);
        sb_addc(s, '\n');
        break;
    case NODE_EFFECT_DECL:
        sb_indent(s, depth);
        sb_add(s, "effect ");
        sb_add(s, n->effect_decl.name);
        sb_add(s, " {\n");
        for (int i = 0; i < n->effect_decl.ops.len; i++) {
            fmt_stmt(s, n->effect_decl.ops.items[i], depth + 1);
        }
        sb_indent(s, depth);
        sb_add(s, "}\n");
        break;
    case NODE_WHILE:
        sb_indent(s, depth);
        if (n->while_loop.label) sb_printf(s, "'%s: ", n->while_loop.label);
        sb_add(s, "while ");
        fmt_expr(s, n->while_loop.cond, depth);
        sb_addc(s, ' ');
        fmt_block(s, n->while_loop.body, depth);
        sb_addc(s, '\n');
        break;
    case NODE_FOR:
        sb_indent(s, depth);
        if (n->for_loop.label) sb_printf(s, "'%s: ", n->for_loop.label);
        sb_add(s, "for ");
        fmt_pattern(s, n->for_loop.pattern, depth);
        sb_add(s, " in ");
        fmt_expr(s, n->for_loop.iter, depth);
        sb_addc(s, ' ');
        fmt_block(s, n->for_loop.body, depth);
        sb_addc(s, '\n');
        break;
    case NODE_LOOP:
        sb_indent(s, depth);
        if (n->loop.label) sb_printf(s, "'%s: ", n->loop.label);
        sb_add(s, "loop ");
        fmt_block(s, n->loop.body, depth);
        sb_addc(s, '\n');
        break;
    case NODE_TRY:
        sb_indent(s, depth);
        sb_add(s, "try ");
        fmt_block(s, n->try_.body, depth);
        for (int i = 0; i < n->try_.catch_arms.len; i++) {
            MatchArm *arm = &n->try_.catch_arms.items[i];
            sb_add(s, " catch ");
            fmt_pattern(s, arm->pattern, depth);
            sb_addc(s, ' ');
            if (arm->body && arm->body->tag == NODE_BLOCK)
                fmt_block(s, arm->body, depth);
            else
                fmt_expr(s, arm->body, depth);
        }
        if (n->try_.finally_block) {
            sb_add(s, " finally ");
            fmt_block(s, n->try_.finally_block, depth);
        }
        sb_addc(s, '\n');
        break;
    case NODE_DEFER:
        sb_indent(s, depth);
        sb_add(s, "defer ");
        if (n->defer_.body && n->defer_.body->tag == NODE_BLOCK)
            fmt_block(s, n->defer_.body, depth);
        else
            fmt_expr(s, n->defer_.body, depth);
        sb_addc(s, '\n');
        break;
    case NODE_RETURN:
    case NODE_BREAK:
    case NODE_CONTINUE:
    case NODE_THROW:
    case NODE_YIELD:
        sb_indent(s, depth);
        fmt_expr(s, n, depth);
        sb_addc(s, '\n');
        break;
    case NODE_IF:
    case NODE_MATCH:
        sb_indent(s, depth);
        fmt_expr(s, n, depth);
        sb_addc(s, '\n');
        break;
    case NODE_ASSIGN:
        sb_indent(s, depth);
        fmt_expr(s, n, depth);
        sb_addc(s, '\n');
        break;
    default:
        sb_indent(s, depth);
        fmt_expr(s, n, depth);
        sb_addc(s, '\n');
        break;
    }
}

static void fmt_node(SB *s, Node *n, int depth) { fmt_stmt(s, n, depth); }

char *fmt_format(Node *program, const char *src) {
    if (!program || program->tag != NODE_PROGRAM) return src ? xs_strdup(src) : NULL;

    CommentList cl = {NULL, 0, 0};
    if (src) {
        Lexer comment_lex;
        lexer_init(&comment_lex, src, "<fmt>");
        lexer_tokenize(&comment_lex);
        cl = comment_lex.comments;
        comment_lex.comments.items = NULL;
        comment_lex.comments.len = 0;
        comment_lex.comments.cap = 0;
        lexer_free(&comment_lex);
    }

    fmt_comments = cl.len > 0 ? &cl : NULL;
    fmt_comment_idx = 0;

    SB sb;
    sb_init(&sb);
    for (int i = 0; i < program->program.stmts.len; i++) {
        if (i > 0) {
            Node *prev = program->program.stmts.items[i - 1];
            Node *cur  = program->program.stmts.items[i];
            if (prev->tag == NODE_FN_DECL || prev->tag == NODE_STRUCT_DECL ||
                prev->tag == NODE_ENUM_DECL || prev->tag == NODE_IMPL_DECL ||
                prev->tag == NODE_TRAIT_DECL || prev->tag == NODE_CLASS_DECL ||
                prev->tag == NODE_MODULE_DECL || prev->tag == NODE_EFFECT_DECL ||
                cur->tag == NODE_FN_DECL || cur->tag == NODE_STRUCT_DECL ||
                cur->tag == NODE_ENUM_DECL || cur->tag == NODE_IMPL_DECL ||
                cur->tag == NODE_TRAIT_DECL || cur->tag == NODE_CLASS_DECL ||
                cur->tag == NODE_MODULE_DECL || cur->tag == NODE_EFFECT_DECL) {
                sb_addc(&sb, '\n');
            }
        }
        fmt_node(&sb, program->program.stmts.items[i], 0);
    }

    /* Trailing comments */
    if (fmt_comments) {
        while (fmt_comment_idx < fmt_comments->len) {
            Comment *c = &fmt_comments->items[fmt_comment_idx];
            sb_add(&sb, c->text);
            sb_addc(&sb, '\n');
            fmt_comment_idx++;
        }
    }

    fmt_comments = NULL;
    fmt_comment_idx = 0;
    comment_list_free(&cl);

    if (!sb.data) return xs_strdup("");
    return sb.data;
}

static char *read_file_contents(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)(sz + 1));
    if (!buf) { fclose(f); return NULL; }
    if ((long)fread(buf, 1, (size_t)sz, f) != sz) { fclose(f); free(buf); return NULL; }
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

int fmt_file(const char *path) {
    char *src = read_file_contents(path);
    if (!src) {
        fprintf(stderr, "xs fmt: cannot open '%s'\n", path);
        return 1;
    }
    Lexer lex;
    lexer_init(&lex, src, path);
    TokenArray ta = lexer_tokenize(&lex);

    Parser p;
    parser_init(&p, &ta, path);
    Node *prog = parser_parse(&p);
    token_array_free(&ta);

    if (!prog || p.had_error) {
        fprintf(stderr, "xs fmt: parse error at %s:%d:%d: %s\n",
                path, p.error.span.line, p.error.span.col, p.error.msg);
        if (prog) node_free(prog);
        free(src);
        return 1;
    }

    char *out = fmt_format(prog, src);
    node_free(prog);
    free(src);
    if (!out) {
        fprintf(stderr, "xs fmt: format failed for '%s'\n", path);
        return 1;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "xs fmt: cannot write '%s'\n", path);
        free(out);
        return 1;
    }
    fwrite(out, 1, strlen(out), f);
    fclose(f);
    free(out);
    return 0;
}

int fmt_file_check(const char *path) {
    char *src = read_file_contents(path);
    if (!src) {
        fprintf(stderr, "xs fmt: cannot open '%s'\n", path);
        return 1;
    }
    Lexer lex;
    lexer_init(&lex, src, path);
    TokenArray ta = lexer_tokenize(&lex);

    Parser p;
    parser_init(&p, &ta, path);
    Node *prog = parser_parse(&p);
    token_array_free(&ta);

    if (!prog || p.had_error) {
        fprintf(stderr, "xs fmt: parse error at %s:%d:%d: %s\n",
                path, p.error.span.line, p.error.span.col, p.error.msg);
        if (prog) node_free(prog);
        free(src);
        return 1;
    }

    char *out = fmt_format(prog, src);
    node_free(prog);
    if (!out) {
        fprintf(stderr, "xs fmt: format failed for '%s'\n", path);
        free(src);
        return 1;
    }

    int differs = (strcmp(src, out) != 0);
    free(src);
    free(out);
    if (differs) {
        fprintf(stderr, "%s: would be reformatted\n", path);
        return 1;
    }
    return 0;
}
