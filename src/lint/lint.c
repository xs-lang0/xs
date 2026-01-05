#include "lint/lint.h"
#include "core/xs_compat.h"
#include "core/lexer.h"
#include "core/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

typedef enum { SEV_WARNING, SEV_STYLE, SEV_ERROR } Severity;

typedef struct {
    Severity    sev;
    int         line;
    char        msg[256];
} LintIssue;

typedef struct {
    char   *name;
    int     line;
    int     used;
} VarEntry;

typedef struct {
    VarEntry *vars;
    int       nvars;
    int       cap;
} Scope;

struct XSLint {
    int         auto_fix;
    int         issues;

    LintIssue  *issue_list;
    int         nissues;
    int         issue_cap;

    Scope      *scopes;
    int         nscopes;
    int         scope_cap;
    int         depth;

    const char *filename;
};

static void lint_add(XSLint *l, Severity sev, int line, const char *fmt, ...) {
    if (l->nissues >= l->issue_cap) {
        l->issue_cap = l->issue_cap ? l->issue_cap * 2 : 32;
        LintIssue *tmp = (LintIssue *)realloc(l->issue_list,
            (size_t)l->issue_cap * sizeof(LintIssue));
        if (!tmp) return;
        l->issue_list = tmp;
    }
    LintIssue *iss = &l->issue_list[l->nissues++];
    iss->sev = sev;
    iss->line = line;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(iss->msg, sizeof iss->msg, fmt, ap);
    va_end(ap);
    l->issues++;
}


static void scope_push(XSLint *l) {
    if (l->nscopes >= l->scope_cap) {
        l->scope_cap = l->scope_cap ? l->scope_cap * 2 : 16;
        l->scopes = (Scope *)realloc(l->scopes,
            (size_t)l->scope_cap * sizeof(Scope));
    }
    Scope *s = &l->scopes[l->nscopes++];
    s->vars = NULL;
    s->nvars = 0;
    s->cap = 0;
    l->depth++;
}

static void scope_add_var(XSLint *l, const char *name, int line) {
    if (l->nscopes == 0) return;
    for (int si = 0; si < l->nscopes - 1; si++) {
        Scope *outer = &l->scopes[si];
        for (int vi = 0; vi < outer->nvars; vi++) {
            if (outer->vars[vi].name && strcmp(outer->vars[vi].name, name) == 0) {
                lint_add(l, SEV_WARNING, line,
                         "shadowed variable '%s' (previously at line %d)",
                         name, outer->vars[vi].line);
            }
        }
    }
    Scope *s = &l->scopes[l->nscopes - 1];
    if (s->nvars >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        VarEntry *tmp = (VarEntry *)realloc(s->vars, (size_t)s->cap * sizeof(VarEntry));
        if (!tmp) return;
        s->vars = tmp;
    }
    VarEntry *v = &s->vars[s->nvars++];
    v->name = xs_strdup(name);
    v->line = line;
    v->used = 0;
}

static void scope_mark_used(XSLint *l, const char *name) {
    for (int si = l->nscopes - 1; si >= 0; si--) {
        Scope *s = &l->scopes[si];
        for (int vi = 0; vi < s->nvars; vi++) {
            if (s->vars[vi].name && strcmp(s->vars[vi].name, name) == 0) {
                s->vars[vi].used = 1;
                return;
            }
        }
    }
}

static void scope_pop(XSLint *l) {
    if (l->nscopes == 0) return;
    Scope *s = &l->scopes[l->nscopes - 1];
    for (int i = 0; i < s->nvars; i++) {
        if (!s->vars[i].used && s->vars[i].name && s->vars[i].name[0] != '_') {
            lint_add(l, SEV_WARNING, s->vars[i].line,
                     "unused variable '%s'", s->vars[i].name);
        }
        free(s->vars[i].name);
    }
    free(s->vars);
    l->nscopes--;
    l->depth--;
}

/* Naming conventions */
static int is_snake_case(const char *name) {
    if (!name || !name[0]) return 1;
    const char *p = name;
    if (*p == '_') p++;
    if (!*p) return 1;
    if (*p >= 'A' && *p <= 'Z') return 0;
    for (; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') return 0;
        if (!(*p >= 'a' && *p <= 'z') && !(*p >= '0' && *p <= '9') && *p != '_') return 0;
    }
    return 1;
}

static int is_pascal_case(const char *name) {
    if (!name || !name[0]) return 1;
    if (!(name[0] >= 'A' && name[0] <= 'Z')) return 0;
    for (const char *p = name + 1; *p; p++) {
        if (*p == '_') return 0;
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')))
            return 0;
    }
    return 1;
}

static int is_upper_snake_case(const char *name) {
    if (!name || !name[0]) return 1;
    int has_lower = 0;
    for (const char *p = name; *p; p++) {
        if (*p >= 'a' && *p <= 'z') has_lower = 1;
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_'))
            return 0;
    }
    return !has_lower;
}

static int is_literal_bool(Node *n) {
    return n && n->tag == NODE_LIT_BOOL;
}

static int block_ends_with_return(Node *n) {
    if (!n) return 0;
    if (n->tag != NODE_BLOCK) return 0;
    if (n->block.stmts.len == 0) return 0;
    Node *last = n->block.stmts.items[n->block.stmts.len - 1];
    if (!last) return 0;
    if (last->tag == NODE_RETURN) return 1;
    if (last->tag == NODE_EXPR_STMT && last->expr_stmt.expr &&
        last->expr_stmt.expr->tag == NODE_RETURN) return 1;
    return 0;
}

static void check_null_comparison(XSLint *l, Node *n) {
    if (!n || n->tag != NODE_BINOP) return;
    if (strcmp(n->binop.op, "==") != 0 && strcmp(n->binop.op, "!=") != 0) return;
    int left_null = (n->binop.left && n->binop.left->tag == NODE_LIT_NULL);
    int right_null = (n->binop.right && n->binop.right->tag == NODE_LIT_NULL);
    if (left_null || right_null) {
        const char *suggest = strcmp(n->binop.op, "==") == 0 ? "is null" : "is not null";
        lint_add(l, SEV_STYLE, n->span.line,
                 "use '%s' instead of '%s null'", suggest, n->binop.op);
    }
}

static int is_empty_block(Node *n) {
    if (!n) return 1;
    if (n->tag != NODE_BLOCK) return 0;
    return (n->block.stmts.len == 0 && !n->block.expr);
}

static void lint_node(XSLint *l, Node *n);
static void lint_expr(XSLint *l, Node *n);
static void lint_block(XSLint *l, Node *n);

static int is_terminator(Node *n) {
    if (!n) return 0;
    switch (n->tag) {
    case NODE_RETURN: case NODE_BREAK: case NODE_CONTINUE: case NODE_THROW:
        return 1;
    case NODE_EXPR_STMT:
        return is_terminator(n->expr_stmt.expr);
    default:
        return 0;
    }
}

static void check_unreachable(XSLint *l, NodeList *stmts) {
    for (int i = 0; i < stmts->len - 1; i++) {
        if (is_terminator(stmts->items[i])) {
            int line = stmts->items[i + 1]->span.line;
            if (line <= 0) line = stmts->items[i]->span.line;
            lint_add(l, SEV_WARNING, line, "unreachable code after %s",
                     stmts->items[i]->tag == NODE_RETURN ? "return" :
                     stmts->items[i]->tag == NODE_BREAK ? "break" :
                     stmts->items[i]->tag == NODE_CONTINUE ? "continue" : "throw");
            break;
        }
    }
}

static void lint_expr(XSLint *l, Node *n) {
    if (!n) return;
    switch (n->tag) {
    case NODE_IDENT:
        scope_mark_used(l, n->ident.name);
        break;
    case NODE_BINOP:
        check_null_comparison(l, n);
        lint_expr(l, n->binop.left);
        lint_expr(l, n->binop.right);
        break;
    case NODE_UNARY:
        lint_expr(l, n->unary.expr);
        break;
    case NODE_ASSIGN:
        lint_expr(l, n->assign.target);
        lint_expr(l, n->assign.value);
        break;
    case NODE_CALL:
        lint_expr(l, n->call.callee);
        for (int i = 0; i < n->call.args.len; i++)
            lint_expr(l, n->call.args.items[i]);
        for (int i = 0; i < n->call.kwargs.len; i++)
            lint_expr(l, n->call.kwargs.items[i].val);
        break;
    case NODE_METHOD_CALL:
        lint_expr(l, n->method_call.obj);
        for (int i = 0; i < n->method_call.args.len; i++)
            lint_expr(l, n->method_call.args.items[i]);
        for (int i = 0; i < n->method_call.kwargs.len; i++)
            lint_expr(l, n->method_call.kwargs.items[i].val);
        break;
    case NODE_INDEX:
        lint_expr(l, n->index.obj);
        lint_expr(l, n->index.index);
        break;
    case NODE_FIELD:
        lint_expr(l, n->field.obj);
        break;
    case NODE_IF:
        if (is_literal_bool(n->if_expr.cond)) {
            lint_add(l, SEV_WARNING, n->span.line,
                     "constant condition '%s' in if statement",
                     n->if_expr.cond->lit_bool.bval ? "true" : "false");
        }
        if (is_empty_block(n->if_expr.then)) {
            lint_add(l, SEV_WARNING, n->span.line, "empty if body");
        }
        if (n->if_expr.else_branch && block_ends_with_return(n->if_expr.then)) {
            lint_add(l, SEV_STYLE, n->span.line,
                     "redundant else after return; consider removing the else block");
        }
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            if (is_literal_bool(n->if_expr.elif_conds.items[i])) {
                lint_add(l, SEV_WARNING, n->if_expr.elif_conds.items[i]->span.line,
                         "constant condition '%s' in elif",
                         n->if_expr.elif_conds.items[i]->lit_bool.bval ? "true" : "false");
            }
        }
        lint_expr(l, n->if_expr.cond);
        lint_block(l, n->if_expr.then);
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            lint_expr(l, n->if_expr.elif_conds.items[i]);
            lint_block(l, n->if_expr.elif_thens.items[i]);
        }
        if (n->if_expr.else_branch)
            lint_block(l, n->if_expr.else_branch);
        break;
    case NODE_MATCH:
        lint_expr(l, n->match.subject);
        for (int i = 0; i < n->match.arms.len; i++) {
            MatchArm *arm = &n->match.arms.items[i];
            if (arm->guard) lint_expr(l, arm->guard);
            lint_expr(l, arm->body);
        }
        break;
    case NODE_LAMBDA:
        scope_push(l);
        for (int i = 0; i < n->lambda.params.len; i++) {
            Param *p = &n->lambda.params.items[i];
            if (p->name) scope_add_var(l, p->name, n->span.line);
        }
        lint_expr(l, n->lambda.body);
        scope_pop(l);
        break;
    case NODE_BLOCK:
        lint_block(l, n);
        break;
    case NODE_LIT_ARRAY:
    case NODE_LIT_TUPLE:
        for (int i = 0; i < n->lit_array.elems.len; i++)
            lint_expr(l, n->lit_array.elems.items[i]);
        break;
    case NODE_LIT_MAP:
        for (int i = 0; i < n->lit_map.keys.len; i++) {
            lint_expr(l, n->lit_map.keys.items[i]);
            lint_expr(l, n->lit_map.vals.items[i]);
        }
        break;
    case NODE_RANGE:
        lint_expr(l, n->range.start);
        lint_expr(l, n->range.end);
        break;
    case NODE_CAST:
        lint_expr(l, n->cast.expr);
        break;
    case NODE_SPREAD:
        lint_expr(l, n->spread.expr);
        break;
    case NODE_STRUCT_INIT:
        for (int i = 0; i < n->struct_init.fields.len; i++)
            lint_expr(l, n->struct_init.fields.items[i].val);
        if (n->struct_init.rest) lint_expr(l, n->struct_init.rest);
        break;
    case NODE_LIST_COMP:
        scope_push(l);
        for (int i = 0; i < n->list_comp.clause_pats.len; i++) {
            lint_expr(l, n->list_comp.clause_iters.items[i]);
            Node *pat = n->list_comp.clause_pats.items[i];
            if (pat && pat->tag == NODE_PAT_IDENT)
                scope_add_var(l, pat->pat_ident.name, pat->span.line);
            if (i < n->list_comp.clause_conds.len && n->list_comp.clause_conds.items[i])
                lint_expr(l, n->list_comp.clause_conds.items[i]);
        }
        lint_expr(l, n->list_comp.element);
        scope_pop(l);
        break;
    case NODE_MAP_COMP:
        scope_push(l);
        for (int i = 0; i < n->map_comp.clause_pats.len; i++) {
            lint_expr(l, n->map_comp.clause_iters.items[i]);
            Node *pat = n->map_comp.clause_pats.items[i];
            if (pat && pat->tag == NODE_PAT_IDENT)
                scope_add_var(l, pat->pat_ident.name, pat->span.line);
            if (i < n->map_comp.clause_conds.len && n->map_comp.clause_conds.items[i])
                lint_expr(l, n->map_comp.clause_conds.items[i]);
        }
        lint_expr(l, n->map_comp.key);
        lint_expr(l, n->map_comp.value);
        scope_pop(l);
        break;
    case NODE_RETURN:
        if (n->ret.value) lint_expr(l, n->ret.value);
        break;
    case NODE_THROW:
        if (n->throw_.value) lint_expr(l, n->throw_.value);
        break;
    case NODE_YIELD:
        if (n->yield_.value) lint_expr(l, n->yield_.value);
        break;
    case NODE_BREAK:
        if (n->brk.value) lint_expr(l, n->brk.value);
        break;
    case NODE_PERFORM:
        for (int i = 0; i < n->perform.args.len; i++)
            lint_expr(l, n->perform.args.items[i]);
        break;
    case NODE_HANDLE:
        lint_expr(l, n->handle.expr);
        for (int i = 0; i < n->handle.arms.len; i++)
            lint_expr(l, n->handle.arms.items[i].body);
        break;
    case NODE_RESUME:
        if (n->resume_.value) lint_expr(l, n->resume_.value);
        break;
    case NODE_AWAIT:
        lint_expr(l, n->await_.expr);
        break;
    case NODE_NURSERY:
        lint_block(l, n->nursery_.body);
        break;
    case NODE_SPAWN:
        lint_expr(l, n->spawn_.expr);
        break;
    case NODE_INTERP_STRING:
    case NODE_LIT_STRING:
        if (n->lit_string.interpolated) {
            for (int i = 0; i < n->lit_string.parts.len; i++)
                lint_expr(l, n->lit_string.parts.items[i]);
        }
        break;
    default:
        break;
    }
}

static void lint_block(XSLint *l, Node *n) {
    if (!n) return;
    if (n->tag != NODE_BLOCK) {
        lint_node(l, n);
        return;
    }
    if (l->depth > 5) {
        lint_add(l, SEV_WARNING, n->span.line,
                 "deeply nested code (depth %d > 5)", l->depth);
    }
    if (n->block.stmts.len == 0 && !n->block.expr) {
        lint_add(l, SEV_STYLE, n->span.line, "empty block");
    }

    scope_push(l);
    if (n->block.stmts.len > 1) {
        check_unreachable(l, &n->block.stmts);
    }
    for (int i = 0; i < n->block.stmts.len; i++) {
        lint_node(l, n->block.stmts.items[i]);
    }
    if (n->block.expr) {
        lint_expr(l, n->block.expr);
    }
    scope_pop(l);
}

static void lint_node(XSLint *l, Node *n) {
    if (!n) return;
    switch (n->tag) {
    case NODE_LET:
    case NODE_VAR: {
        const char *vname = n->let.name;
        if (vname) {
            if (!is_snake_case(vname)) {
                lint_add(l, SEV_STYLE, n->span.line,
                         "variable '%s' should use snake_case", vname);
            }
            scope_add_var(l, vname, n->span.line);
        }
        if (n->let.value) lint_expr(l, n->let.value);
        break;
    }
    case NODE_CONST:
        if (n->const_.name && !is_upper_snake_case(n->const_.name)) {
            lint_add(l, SEV_STYLE, n->span.line,
                     "constant '%s' should use UPPER_SNAKE_CASE", n->const_.name);
        }
        if (n->const_.value) lint_expr(l, n->const_.value);
        break;
    case NODE_FN_DECL: {
        if (!is_snake_case(n->fn_decl.name)) {
            lint_add(l, SEV_STYLE, n->span.line,
                     "function '%s' should use snake_case", n->fn_decl.name);
        }
        if (is_empty_block(n->fn_decl.body)) {
            lint_add(l, SEV_WARNING, n->span.line,
                     "empty function body for '%s'", n->fn_decl.name);
        }
        scope_push(l);
        for (int i = 0; i < n->fn_decl.params.len; i++) {
            Param *p = &n->fn_decl.params.items[i];
            if (p->name) {
                if (!is_snake_case(p->name)) {
                    lint_add(l, SEV_STYLE, n->span.line,
                             "parameter '%s' should use snake_case", p->name);
                }
                scope_add_var(l, p->name, n->span.line);
            }
        }
        lint_block(l, n->fn_decl.body);
        scope_pop(l);
        break;
    }
    case NODE_STRUCT_DECL:
        if (n->struct_decl.name && !is_pascal_case(n->struct_decl.name)) {
            lint_add(l, SEV_STYLE, n->span.line,
                     "struct '%s' should use PascalCase", n->struct_decl.name);
        }
        break;
    case NODE_ENUM_DECL:
        if (n->enum_decl.name && !is_pascal_case(n->enum_decl.name)) {
            lint_add(l, SEV_STYLE, n->span.line,
                     "enum '%s' should use PascalCase", n->enum_decl.name);
        }
        break;
    case NODE_TRAIT_DECL:
        if (n->trait_decl.name && !is_pascal_case(n->trait_decl.name)) {
            lint_add(l, SEV_STYLE, n->span.line,
                     "trait '%s' should use PascalCase", n->trait_decl.name);
        }
        break;
    case NODE_TYPE_ALIAS:
        if (n->type_alias.name && !is_pascal_case(n->type_alias.name)) {
            lint_add(l, SEV_STYLE, n->span.line,
                     "type alias '%s' should use PascalCase", n->type_alias.name);
        }
        break;
    case NODE_EFFECT_DECL:
        if (n->effect_decl.name && !is_pascal_case(n->effect_decl.name)) {
            lint_add(l, SEV_STYLE, n->span.line,
                     "effect '%s' should use PascalCase", n->effect_decl.name);
        }
        break;
    case NODE_IMPL_DECL:
        scope_push(l);
        for (int i = 0; i < n->impl_decl.members.len; i++)
            lint_node(l, n->impl_decl.members.items[i]);
        scope_pop(l);
        break;
    case NODE_CLASS_DECL:
        scope_push(l);
        for (int i = 0; i < n->class_decl.members.len; i++)
            lint_node(l, n->class_decl.members.items[i]);
        scope_pop(l);
        break;
    case NODE_MODULE_DECL:
        scope_push(l);
        for (int i = 0; i < n->module_decl.body.len; i++)
            lint_node(l, n->module_decl.body.items[i]);
        scope_pop(l);
        break;
    case NODE_EXPR_STMT:
        lint_expr(l, n->expr_stmt.expr);
        break;
    case NODE_WHILE:
        if (is_literal_bool(n->while_loop.cond)) {
            lint_add(l, SEV_STYLE, n->span.line,
                     "constant condition in while; use 'loop' for infinite loops or remove dead code");
        }
        if (is_empty_block(n->while_loop.body)) {
            lint_add(l, SEV_WARNING, n->span.line, "empty while body");
        }
        lint_expr(l, n->while_loop.cond);
        lint_block(l, n->while_loop.body);
        break;
    case NODE_FOR:
        if (is_empty_block(n->for_loop.body)) {
            lint_add(l, SEV_WARNING, n->span.line, "empty for body");
        }
        lint_expr(l, n->for_loop.iter);
        scope_push(l);
        if (n->for_loop.pattern && n->for_loop.pattern->tag == NODE_PAT_IDENT) {
            const char *pname = n->for_loop.pattern->pat_ident.name;
            if (pname) scope_add_var(l, pname, n->span.line);
        }
        lint_block(l, n->for_loop.body);
        scope_pop(l);
        break;
    case NODE_LOOP:
        if (is_empty_block(n->loop.body)) {
            lint_add(l, SEV_WARNING, n->span.line, "empty loop body");
        }
        lint_block(l, n->loop.body);
        break;
    case NODE_IF:
        if (is_literal_bool(n->if_expr.cond)) {
            lint_add(l, SEV_WARNING, n->span.line,
                     "constant condition '%s' in if statement",
                     n->if_expr.cond->lit_bool.bval ? "true" : "false");
        }
        if (is_empty_block(n->if_expr.then)) {
            lint_add(l, SEV_WARNING, n->span.line, "empty if body");
        }
        if (n->if_expr.else_branch && block_ends_with_return(n->if_expr.then)) {
            lint_add(l, SEV_STYLE, n->span.line,
                     "redundant else after return; consider removing the else block");
        }
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            if (is_literal_bool(n->if_expr.elif_conds.items[i])) {
                lint_add(l, SEV_WARNING, n->if_expr.elif_conds.items[i]->span.line,
                         "constant condition '%s' in elif",
                         n->if_expr.elif_conds.items[i]->lit_bool.bval ? "true" : "false");
            }
        }
        lint_expr(l, n->if_expr.cond);
        lint_block(l, n->if_expr.then);
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            lint_expr(l, n->if_expr.elif_conds.items[i]);
            lint_block(l, n->if_expr.elif_thens.items[i]);
        }
        if (n->if_expr.else_branch)
            lint_block(l, n->if_expr.else_branch);
        break;
    case NODE_MATCH:
        lint_expr(l, n->match.subject);
        for (int i = 0; i < n->match.arms.len; i++) {
            MatchArm *arm = &n->match.arms.items[i];
            if (arm->guard) lint_expr(l, arm->guard);
            scope_push(l);
            if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT) {
                scope_add_var(l, arm->pattern->pat_ident.name, arm->span.line);
            }
            lint_expr(l, arm->body);
            scope_pop(l);
        }
        break;
    case NODE_TRY:
        lint_block(l, n->try_.body);
        for (int i = 0; i < n->try_.catch_arms.len; i++) {
            MatchArm *arm = &n->try_.catch_arms.items[i];
            scope_push(l);
            if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT)
                scope_add_var(l, arm->pattern->pat_ident.name, arm->span.line);
            lint_expr(l, arm->body);
            scope_pop(l);
        }
        if (n->try_.finally_block)
            lint_block(l, n->try_.finally_block);
        break;
    case NODE_DEFER:
        lint_expr(l, n->defer_.body);
        break;
    case NODE_BLOCK:
        lint_block(l, n);
        break;
    case NODE_IMPORT:
        break;
    case NODE_RETURN:
    case NODE_BREAK:
    case NODE_CONTINUE:
    case NODE_THROW:
    case NODE_YIELD:
        lint_expr(l, n);
        break;
    case NODE_ASSIGN:
        lint_expr(l, n);
        break;
    default:
        lint_expr(l, n);
        break;
    }
}

XSLint *lint_new(int auto_fix) {
    XSLint *l = (XSLint *)calloc(1, sizeof(XSLint));
    l->auto_fix = auto_fix;
    return l;
}

void lint_free(XSLint *l) {
    if (!l) return;
    free(l->issue_list);
    for (int i = 0; i < l->nscopes; i++) {
        Scope *s = &l->scopes[i];
        for (int j = 0; j < s->nvars; j++) free(s->vars[j].name);
        free(s->vars);
    }
    free(l->scopes);
    free(l);
}

int lint_program(XSLint *l, Node *program, const char *filename) {
    if (!program || program->tag != NODE_PROGRAM) return 0;
    l->filename = filename;
    l->depth = 0;
    l->nscopes = 0;

    scope_push(l);
    for (int i = 0; i < program->program.stmts.len; i++) {
        lint_node(l, program->program.stmts.items[i]);
    }
    if (program->program.stmts.len > 1) {
        check_unreachable(l, &program->program.stmts);
    }
    scope_pop(l);

    return l->issues;
}

void lint_report(XSLint *l) {
    if (!l) return;
    for (int i = 0; i < l->nissues; i++) {
        LintIssue *iss = &l->issue_list[i];
        const char *sev_str = iss->sev == SEV_WARNING ? "warning" :
                              iss->sev == SEV_STYLE   ? "style" : "error";
        fprintf(stderr, "lint: %s:%d: %s: %s\n",
                l->filename ? l->filename : "<unknown>",
                iss->line, sev_str, iss->msg);
    }
    if (l->nissues == 0) {
        fprintf(stderr, "xs lint: clean\n");
    }
}

static char *lint_read_file(const char *path) {
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

int lint_file(const char *path, int auto_fix) {
    char *src = lint_read_file(path);
    if (!src) {
        fprintf(stderr, "xs lint: cannot open '%s'\n", path);
        return -1;
    }

    Lexer lex;
    lexer_init(&lex, src, path);
    TokenArray ta = lexer_tokenize(&lex);
    free(src);

    Parser p;
    parser_init(&p, &ta, path);
    Node *prog = parser_parse(&p);
    token_array_free(&ta);

    if (!prog || p.had_error) {
        fprintf(stderr, "xs lint: parse error at %s:%d:%d: %s\n",
                path, p.error.span.line, p.error.span.col, p.error.msg);
        if (prog) node_free(prog);
        return -1;
    }

    XSLint *l = lint_new(auto_fix);
    int issues = lint_program(l, prog, path);
    lint_report(l);
    lint_free(l);
    node_free(prog);
    return issues;
}
