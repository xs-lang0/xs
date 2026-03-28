#include "optimizer/optimizer.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef enum {
    CV_NONE,
    CV_INT,
    CV_FLOAT,
    CV_BOOL,
    CV_STRING,
} ConstKind;

typedef struct {
    ConstKind kind;
    int64_t   ival;
    double    fval;
    int       bval;
    char     *sval;   /* not owned: points into node */
} ConstVal;

static ConstVal const_val(const Node *n) {
    ConstVal cv = {CV_NONE, 0, 0.0, 0, NULL};
    if (!n) return cv;
    switch (n->tag) {
    case NODE_LIT_INT:   cv.kind = CV_INT;    cv.ival = n->lit_int.ival; break;
    case NODE_LIT_BIGINT: break;  /* bigint not supported for constant folding */
    case NODE_LIT_FLOAT: cv.kind = CV_FLOAT;  cv.fval = n->lit_float.fval; break;
    case NODE_LIT_BOOL:  cv.kind = CV_BOOL;   cv.bval = n->lit_bool.bval; break;
    case NODE_LIT_STRING:
        if (!n->lit_string.interpolated) {
            cv.kind = CV_STRING;
            cv.sval = n->lit_string.sval;
        }
        break;
    default: break;
    }
    return cv;
}

static Node *make_int(int64_t v, Span span) {
    Node *n = node_new(NODE_LIT_INT, span);
    n->lit_int.ival = v;
    return n;
}

static Node *make_float(double v, Span span) {
    Node *n = node_new(NODE_LIT_FLOAT, span);
    n->lit_float.fval = v;
    return n;
}

static Node *make_bool(int v, Span span) {
    Node *n = node_new(NODE_LIT_BOOL, span);
    n->lit_bool.bval = v ? 1 : 0;
    return n;
}

static Node *make_string(const char *s, Span span) {
    Node *n = node_new(NODE_LIT_STRING, span);
    n->lit_string.sval = xs_strdup(s);
    n->lit_string.interpolated = 0;
    n->lit_string.parts = nodelist_new();
    return n;
}

static int is_exit(const Node *n) {
    if (!n) return 0;
    return n->tag == NODE_RETURN ||
           n->tag == NODE_BREAK  ||
           n->tag == NODE_CONTINUE;
}

static int is_power2(int64_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static int log2_int(int64_t n) {
    int r = 0;
    while (n > 1) { n >>= 1; r++; }
    return r;
}

/* expression hashing for CSE */
static uint64_t expr_hash(const Node *n) {
    if (!n) return 0;
    uint64_t h = (uint64_t)n->tag * 2654435761ULL;
    switch (n->tag) {
    case NODE_LIT_INT:
        h ^= (uint64_t)n->lit_int.ival * 6364136223846793005ULL;
        break;
    case NODE_LIT_BIGINT: {
        const char *bs = n->lit_bigint.bigint_str;
        if (bs) for (; *bs; bs++) h = h * 31 + (uint64_t)*bs;
        break;
    }
    case NODE_LIT_FLOAT: {
        uint64_t bits;
        memcpy(&bits, &n->lit_float.fval, sizeof(bits));
        h ^= bits * 6364136223846793005ULL;
        break;
    }
    case NODE_LIT_BOOL:
        h ^= (uint64_t)n->lit_bool.bval;
        break;
    case NODE_IDENT:
        if (n->ident.name) {
            const char *s = n->ident.name;
            while (*s) { h = h * 31 + (unsigned char)*s; s++; }
        }
        break;
    case NODE_BINOP:
        h ^= expr_hash(n->binop.left) * 17;
        h ^= expr_hash(n->binop.right) * 37;
        { const char *o = n->binop.op; while (*o) { h = h * 31 + (unsigned char)*o; o++; } }
        break;
    case NODE_UNARY:
        h ^= expr_hash(n->unary.expr) * 17;
        { const char *o = n->unary.op; while (*o) { h = h * 31 + (unsigned char)*o; o++; } }
        break;
    case NODE_CALL:
        h ^= expr_hash(n->call.callee) * 13;
        for (int i = 0; i < n->call.args.len; i++)
            h ^= expr_hash(n->call.args.items[i]) * (uint64_t)(i + 7);
        break;
    case NODE_INDEX:
        h ^= expr_hash(n->index.obj) * 17;
        h ^= expr_hash(n->index.index) * 37;
        break;
    case NODE_FIELD:
        h ^= expr_hash(n->field.obj) * 17;
        if (n->field.name) {
            const char *s = n->field.name;
            while (*s) { h = h * 31 + (unsigned char)*s; s++; }
        }
        break;
    default:
        h ^= (uint64_t)(uintptr_t)n;
        break;
    }
    return h;
}

static int expr_equal(const Node *a, const Node *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->tag != b->tag) return 0;
    switch (a->tag) {
    case NODE_LIT_INT:   return a->lit_int.ival == b->lit_int.ival;
    case NODE_LIT_BIGINT:
        return a->lit_bigint.bigint_str && b->lit_bigint.bigint_str &&
               strcmp(a->lit_bigint.bigint_str, b->lit_bigint.bigint_str) == 0;
    case NODE_LIT_FLOAT: return a->lit_float.fval == b->lit_float.fval;
    case NODE_LIT_BOOL:  return a->lit_bool.bval == b->lit_bool.bval;
    case NODE_IDENT:
        return a->ident.name && b->ident.name &&
               strcmp(a->ident.name, b->ident.name) == 0;
    case NODE_BINOP:
        return strcmp(a->binop.op, b->binop.op) == 0 &&
               expr_equal(a->binop.left, b->binop.left) &&
               expr_equal(a->binop.right, b->binop.right);
    case NODE_UNARY:
        return strcmp(a->unary.op, b->unary.op) == 0 &&
               expr_equal(a->unary.expr, b->unary.expr);
    case NODE_INDEX:
        return expr_equal(a->index.obj, b->index.obj) &&
               expr_equal(a->index.index, b->index.index);
    case NODE_FIELD:
        return a->field.name && b->field.name &&
               strcmp(a->field.name, b->field.name) == 0 &&
               expr_equal(a->field.obj, b->field.obj);
    case NODE_CALL:
        if (!expr_equal(a->call.callee, b->call.callee)) return 0;
        if (a->call.args.len != b->call.args.len) return 0;
        for (int i = 0; i < a->call.args.len; i++)
            if (!expr_equal(a->call.args.items[i], b->call.args.items[i])) return 0;
        return 1;
    default:
        return 0;
    }
}

static int is_pure(const Node *n) {
    if (!n) return 1;
    switch (n->tag) {
    case NODE_LIT_INT: case NODE_LIT_BIGINT: case NODE_LIT_FLOAT: case NODE_LIT_BOOL:
    case NODE_LIT_NULL: case NODE_LIT_STRING: case NODE_LIT_CHAR:
    case NODE_IDENT:
        return 1;
    case NODE_BINOP:
        return is_pure(n->binop.left) && is_pure(n->binop.right);
    case NODE_UNARY:
        return is_pure(n->unary.expr);
    case NODE_INDEX:
        return is_pure(n->index.obj) && is_pure(n->index.index);
    case NODE_FIELD:
        return is_pure(n->field.obj);
    default:
        return 0;
    }
}

/* constant folding */

Node *opt_constant_fold(Node *node, int *count) {
    if (!node) return NULL;

    switch (node->tag) {
    case NODE_BINOP: {
        node->binop.left  = opt_constant_fold(node->binop.left, count);
        node->binop.right = opt_constant_fold(node->binop.right, count);

        ConstVal lv = const_val(node->binop.left);
        ConstVal rv = const_val(node->binop.right);
        const char *op = node->binop.op;
        Span span = node->span;

        if (lv.kind != CV_NONE && rv.kind != CV_NONE) {
            if (lv.kind == CV_INT && rv.kind == CV_INT) {
                int64_t l = lv.ival, r = rv.ival;
                int64_t result = 0;
                int folded = 1;
                if      (strcmp(op, "+") == 0)  result = l + r;
                else if (strcmp(op, "-") == 0)  result = l - r;
                else if (strcmp(op, "*") == 0)  result = l * r;
                else if (strcmp(op, "/") == 0) { if (r == 0) folded = 0; else result = l / r; }
                else if (strcmp(op, "%") == 0) { if (r == 0) folded = 0; else result = l % r; }
                else if (strcmp(op, "**") == 0) result = (int64_t)pow((double)l, (double)r);
                else if (strcmp(op, "&") == 0)  result = l & r;
                else if (strcmp(op, "|") == 0)  result = l | r;
                else if (strcmp(op, "^") == 0)  result = l ^ r;
                else if (strcmp(op, "<<") == 0) result = l << r;
                else if (strcmp(op, ">>") == 0) result = l >> r;
                else folded = 0;
                if (folded) {
                    if (count) (*count)++;
                    Node *r2 = make_int(result, span);
                    node_free(node);
                    return r2;
                }
                int bres = 0; folded = 1;
                if      (strcmp(op, "==") == 0) bres = (l == r);
                else if (strcmp(op, "!=") == 0) bres = (l != r);
                else if (strcmp(op, "<") == 0)  bres = (l < r);
                else if (strcmp(op, "<=") == 0) bres = (l <= r);
                else if (strcmp(op, ">") == 0)  bres = (l > r);
                else if (strcmp(op, ">=") == 0) bres = (l >= r);
                else folded = 0;
                if (folded) {
                    if (count) (*count)++;
                    Node *r2 = make_bool(bres, span);
                    node_free(node);
                    return r2;
                }
            }
            if ((lv.kind == CV_FLOAT || lv.kind == CV_INT) &&
                (rv.kind == CV_FLOAT || rv.kind == CV_INT) &&
                (lv.kind == CV_FLOAT || rv.kind == CV_FLOAT)) {
                double l = lv.kind == CV_FLOAT ? lv.fval : (double)lv.ival;
                double r = rv.kind == CV_FLOAT ? rv.fval : (double)rv.ival;
                double result = 0.0;
                int folded = 1;
                if      (strcmp(op, "+") == 0) result = l + r;
                else if (strcmp(op, "-") == 0) result = l - r;
                else if (strcmp(op, "*") == 0) result = l * r;
                else if (strcmp(op, "/") == 0) { if (r == 0.0) folded = 0; else result = l / r; }
                else if (strcmp(op, "**") == 0) result = pow(l, r);
                else folded = 0;
                if (folded) {
                    if (count) (*count)++;
                    Node *r2 = make_float(result, span);
                    node_free(node);
                    return r2;
                }
                int bres = 0; folded = 1;
                if      (strcmp(op, "==") == 0) bres = (l == r);
                else if (strcmp(op, "!=") == 0) bres = (l != r);
                else if (strcmp(op, "<") == 0)  bres = (l < r);
                else if (strcmp(op, "<=") == 0) bres = (l <= r);
                else if (strcmp(op, ">") == 0)  bres = (l > r);
                else if (strcmp(op, ">=") == 0) bres = (l >= r);
                else folded = 0;
                if (folded) {
                    if (count) (*count)++;
                    Node *r2 = make_bool(bres, span);
                    node_free(node);
                    return r2;
                }
            }
            if (lv.kind == CV_BOOL && rv.kind == CV_BOOL) {
                int l = lv.bval, r = rv.bval;
                int result = 0, folded = 1;
                if      (strcmp(op, "&&") == 0) result = l && r;
                else if (strcmp(op, "||") == 0) result = l || r;
                else if (strcmp(op, "==") == 0) result = (l == r);
                else if (strcmp(op, "!=") == 0) result = (l != r);
                else folded = 0;
                if (folded) {
                    if (count) (*count)++;
                    Node *r2 = make_bool(result, span);
                    node_free(node);
                    return r2;
                }
            }
            if (lv.kind == CV_STRING && rv.kind == CV_STRING && strcmp(op, "++") == 0) {
                size_t la = strlen(lv.sval), ra = strlen(rv.sval);
                char *buf = xs_malloc(la + ra + 1);
                memcpy(buf, lv.sval, la);
                memcpy(buf + la, rv.sval, ra);
                buf[la + ra] = '\0';
                if (count) (*count)++;
                Node *r2 = make_string(buf, span);
                free(buf);
                node_free(node);
                return r2;
            }
        }

        /* identity / absorbing elements */
        if (strcmp(op, "+") == 0 && rv.kind == CV_INT && rv.ival == 0) {
            if (count) (*count)++;
            Node *result = node->binop.left;
            node->binop.left = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "+") == 0 && lv.kind == CV_INT && lv.ival == 0) {
            if (count) (*count)++;
            Node *result = node->binop.right;
            node->binop.right = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "-") == 0 && rv.kind == CV_INT && rv.ival == 0) {
            if (count) (*count)++;
            Node *result = node->binop.left;
            node->binop.left = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "*") == 0 && rv.kind == CV_INT && rv.ival == 1) {
            if (count) (*count)++;
            Node *result = node->binop.left;
            node->binop.left = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "*") == 0 && lv.kind == CV_INT && lv.ival == 1) {
            if (count) (*count)++;
            Node *result = node->binop.right;
            node->binop.right = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "*") == 0 && rv.kind == CV_INT && rv.ival == 0) {
            if (count) (*count)++;
            Node *r2 = make_int(0, span);
            node_free(node);
            return r2;
        }
        if (strcmp(op, "*") == 0 && lv.kind == CV_INT && lv.ival == 0) {
            if (count) (*count)++;
            Node *r2 = make_int(0, span);
            node_free(node);
            return r2;
        }
        if (strcmp(op, "**") == 0 && rv.kind == CV_INT && rv.ival == 0) {
            if (count) (*count)++;
            Node *r2 = make_int(1, span);
            node_free(node);
            return r2;
        }
        if (strcmp(op, "**") == 0 && rv.kind == CV_INT && rv.ival == 1) {
            if (count) (*count)++;
            Node *result = node->binop.left;
            node->binop.left = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "/") == 0 && rv.kind == CV_INT && rv.ival == 1) {
            if (count) (*count)++;
            Node *result = node->binop.left;
            node->binop.left = NULL;
            node_free(node);
            return result;
        }
        return node;
    }

    case NODE_UNARY: {
        node->unary.expr = opt_constant_fold(node->unary.expr, count);
        ConstVal v = const_val(node->unary.expr);
        const char *op = node->unary.op;
        Span span = node->span;

        if (v.kind == CV_INT && strcmp(op, "-") == 0) {
            if (count) (*count)++;
            Node *r = make_int(-v.ival, span);
            node_free(node);
            return r;
        }
        if (v.kind == CV_FLOAT && strcmp(op, "-") == 0) {
            if (count) (*count)++;
            Node *r = make_float(-v.fval, span);
            node_free(node);
            return r;
        }
        if (v.kind == CV_BOOL && strcmp(op, "!") == 0) {
            if (count) (*count)++;
            Node *r = make_bool(!v.bval, span);
            node_free(node);
            return r;
        }
        if (v.kind == CV_INT && strcmp(op, "~") == 0) {
            if (count) (*count)++;
            Node *r = make_int(~v.ival, span);
            node_free(node);
            return r;
        }
        return node;
    }

    /* Recurse into containers */
    case NODE_BLOCK:
        for (int i = 0; i < node->block.stmts.len; i++)
            node->block.stmts.items[i] = opt_constant_fold(node->block.stmts.items[i], count);
        node->block.expr = opt_constant_fold(node->block.expr, count);
        return node;

    case NODE_IF:
        node->if_expr.cond = opt_constant_fold(node->if_expr.cond, count);
        node->if_expr.then = opt_constant_fold(node->if_expr.then, count);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++)
            node->if_expr.elif_conds.items[i] = opt_constant_fold(node->if_expr.elif_conds.items[i], count);
        for (int i = 0; i < node->if_expr.elif_thens.len; i++)
            node->if_expr.elif_thens.items[i] = opt_constant_fold(node->if_expr.elif_thens.items[i], count);
        node->if_expr.else_branch = opt_constant_fold(node->if_expr.else_branch, count);
        return node;

    case NODE_WHILE:
        node->while_loop.cond = opt_constant_fold(node->while_loop.cond, count);
        node->while_loop.body = opt_constant_fold(node->while_loop.body, count);
        return node;

    case NODE_FOR:
        node->for_loop.iter = opt_constant_fold(node->for_loop.iter, count);
        node->for_loop.body = opt_constant_fold(node->for_loop.body, count);
        return node;

    case NODE_LOOP:
        node->loop.body = opt_constant_fold(node->loop.body, count);
        return node;

    case NODE_RETURN:
        node->ret.value = opt_constant_fold(node->ret.value, count);
        return node;

    case NODE_LET: case NODE_VAR:
        node->let.value = opt_constant_fold(node->let.value, count);
        return node;

    case NODE_CONST:
        node->const_.value = opt_constant_fold(node->const_.value, count);
        return node;

    case NODE_EXPR_STMT:
        node->expr_stmt.expr = opt_constant_fold(node->expr_stmt.expr, count);
        return node;

    case NODE_FN_DECL:
        node->fn_decl.body = opt_constant_fold(node->fn_decl.body, count);
        return node;

    case NODE_LAMBDA:
        node->lambda.body = opt_constant_fold(node->lambda.body, count);
        return node;

    case NODE_CALL:
        node->call.callee = opt_constant_fold(node->call.callee, count);
        for (int i = 0; i < node->call.args.len; i++)
            node->call.args.items[i] = opt_constant_fold(node->call.args.items[i], count);
        return node;

    case NODE_METHOD_CALL:
        node->method_call.obj = opt_constant_fold(node->method_call.obj, count);
        for (int i = 0; i < node->method_call.args.len; i++)
            node->method_call.args.items[i] = opt_constant_fold(node->method_call.args.items[i], count);
        return node;

    case NODE_INDEX:
        node->index.obj   = opt_constant_fold(node->index.obj, count);
        node->index.index = opt_constant_fold(node->index.index, count);
        return node;

    case NODE_FIELD:
        node->field.obj = opt_constant_fold(node->field.obj, count);
        return node;

    case NODE_ASSIGN:
        node->assign.target = opt_constant_fold(node->assign.target, count);
        node->assign.value  = opt_constant_fold(node->assign.value, count);
        return node;

    case NODE_CAST:
        node->cast.expr = opt_constant_fold(node->cast.expr, count);
        return node;

    case NODE_MATCH:
        node->match.subject = opt_constant_fold(node->match.subject, count);
        for (int i = 0; i < node->match.arms.len; i++)
            node->match.arms.items[i].body = opt_constant_fold(node->match.arms.items[i].body, count);
        return node;

    case NODE_PROGRAM:
        for (int i = 0; i < node->program.stmts.len; i++)
            node->program.stmts.items[i] = opt_constant_fold(node->program.stmts.items[i], count);
        return node;

    default:
        return node;
    }
}


Node *opt_dead_code_elim(Node *node, int *count) {
    if (!node) return NULL;

    switch (node->tag) {
    case NODE_BLOCK: {
        for (int i = 0; i < node->block.stmts.len; i++)
            node->block.stmts.items[i] = opt_dead_code_elim(node->block.stmts.items[i], count);

        int cut = -1;
        for (int i = 0; i < node->block.stmts.len; i++) {
            if (is_exit(node->block.stmts.items[i])) {
                cut = i;
                break;
            }
        }
        if (cut >= 0 && cut + 1 < node->block.stmts.len) {
            int removed = node->block.stmts.len - (cut + 1);
            if (count) *count += removed;
            for (int i = cut + 1; i < node->block.stmts.len; i++)
                node_free(node->block.stmts.items[i]);
            node->block.stmts.len = cut + 1;
            if (node->block.expr) {
                node_free(node->block.expr);
                node->block.expr = NULL;
            }
        }
        node->block.expr = opt_dead_code_elim(node->block.expr, count);
        return node;
    }

    case NODE_IF: {
        node->if_expr.cond = opt_dead_code_elim(node->if_expr.cond, count);
        ConstVal cv = const_val(node->if_expr.cond);

        if (cv.kind == CV_BOOL && cv.bval) {
            if (count) (*count)++;
            Node *then = node->if_expr.then;
            node->if_expr.then = NULL;
            then = opt_dead_code_elim(then, count);
            node_free(node);
            return then;
        }
        if (cv.kind == CV_BOOL && !cv.bval) {
            if (count) (*count)++;
            if (node->if_expr.elif_conds.len > 0) {
                Node *new_cond = node->if_expr.elif_conds.items[0];
                Node *new_then = node->if_expr.elif_thens.items[0];
                node->if_expr.elif_conds.items[0] = NULL;
                node->if_expr.elif_thens.items[0] = NULL;
                for (int i = 1; i < node->if_expr.elif_conds.len; i++) {
                    node->if_expr.elif_conds.items[i - 1] = node->if_expr.elif_conds.items[i];
                    node->if_expr.elif_thens.items[i - 1] = node->if_expr.elif_thens.items[i];
                }
                node->if_expr.elif_conds.len--;
                node->if_expr.elif_thens.len--;
                node_free(node->if_expr.cond);
                node_free(node->if_expr.then);
                node->if_expr.cond = new_cond;
                node->if_expr.then = new_then;
                return opt_dead_code_elim(node, count);
            }
            if (node->if_expr.else_branch) {
                Node *else_b = node->if_expr.else_branch;
                node->if_expr.else_branch = NULL;
                else_b = opt_dead_code_elim(else_b, count);
                node_free(node);
                return else_b;
            }
            Span span = node->span;
            node_free(node);
            Node *empty = node_new(NODE_BLOCK, span);
            empty->block.stmts = nodelist_new();
            empty->block.expr = NULL;
            return empty;
        }

        node->if_expr.then = opt_dead_code_elim(node->if_expr.then, count);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++)
            node->if_expr.elif_conds.items[i] = opt_dead_code_elim(node->if_expr.elif_conds.items[i], count);
        for (int i = 0; i < node->if_expr.elif_thens.len; i++)
            node->if_expr.elif_thens.items[i] = opt_dead_code_elim(node->if_expr.elif_thens.items[i], count);
        node->if_expr.else_branch = opt_dead_code_elim(node->if_expr.else_branch, count);
        return node;
    }

    case NODE_WHILE: {
        node->while_loop.cond = opt_dead_code_elim(node->while_loop.cond, count);
        ConstVal cv = const_val(node->while_loop.cond);
        if (cv.kind == CV_BOOL && !cv.bval) {
            if (count) (*count)++;
            Span span = node->span;
            node_free(node);
            Node *empty = node_new(NODE_BLOCK, span);
            empty->block.stmts = nodelist_new();
            empty->block.expr = NULL;
            return empty;
        }
        node->while_loop.body = opt_dead_code_elim(node->while_loop.body, count);
        return node;
    }

    case NODE_FOR:
        node->for_loop.iter = opt_dead_code_elim(node->for_loop.iter, count);
        node->for_loop.body = opt_dead_code_elim(node->for_loop.body, count);
        return node;

    case NODE_LOOP:
        node->loop.body = opt_dead_code_elim(node->loop.body, count);
        return node;

    case NODE_RETURN:
        node->ret.value = opt_dead_code_elim(node->ret.value, count);
        return node;

    case NODE_LET: case NODE_VAR:
        node->let.value = opt_dead_code_elim(node->let.value, count);
        return node;

    case NODE_CONST:
        node->const_.value = opt_dead_code_elim(node->const_.value, count);
        return node;

    case NODE_EXPR_STMT:
        node->expr_stmt.expr = opt_dead_code_elim(node->expr_stmt.expr, count);
        return node;

    case NODE_FN_DECL:
        node->fn_decl.body = opt_dead_code_elim(node->fn_decl.body, count);
        return node;

    case NODE_LAMBDA:
        node->lambda.body = opt_dead_code_elim(node->lambda.body, count);
        return node;

    case NODE_MATCH:
        node->match.subject = opt_dead_code_elim(node->match.subject, count);
        for (int i = 0; i < node->match.arms.len; i++)
            node->match.arms.items[i].body = opt_dead_code_elim(node->match.arms.items[i].body, count);
        return node;

    case NODE_PROGRAM:
        for (int i = 0; i < node->program.stmts.len; i++)
            node->program.stmts.items[i] = opt_dead_code_elim(node->program.stmts.items[i], count);
        return node;

    default:
        return node;
    }
}

/* strength reduction */

Node *opt_strength_reduce(Node *node, int *count) {
    if (!node) return NULL;

    switch (node->tag) {
    case NODE_BINOP: {
        node->binop.left  = opt_strength_reduce(node->binop.left, count);
        node->binop.right = opt_strength_reduce(node->binop.right, count);

        ConstVal lv = const_val(node->binop.left);
        ConstVal rv = const_val(node->binop.right);
        const char *op = node->binop.op;
        Span span = node->span;

        if (strcmp(op, "**") == 0 && rv.kind == CV_INT && rv.ival == 2) {
            if (count) (*count)++;
            Node *left = node->binop.left;
            node->binop.left = NULL;
            node->binop.right = NULL;

            Node *mul = node_new(NODE_BINOP, span);
            strcpy(mul->binop.op, "*");
            mul->binop.left = left;
            Node *dup = node_new(left->tag, left->span);
            *dup = *left;
            if (left->tag == NODE_IDENT && left->ident.name)
                dup->ident.name = xs_strdup(left->ident.name);
            mul->binop.right = dup;
            node_free(node);
            return mul;
        }

        /* mul/div by power-of-2 -> shift */
        if (strcmp(op, "*") == 0 && rv.kind == CV_INT && rv.ival > 0 && is_power2(rv.ival)) {
            int n = log2_int(rv.ival);
            if (count) (*count)++;
            strcpy(node->binop.op, "<<");
            node_free(node->binop.right);
            node->binop.right = make_int(n, span);
            return node;
        }
        if (strcmp(op, "*") == 0 && lv.kind == CV_INT && lv.ival > 0 && is_power2(lv.ival)) {
            int n = log2_int(lv.ival);
            if (count) (*count)++;
            strcpy(node->binop.op, "<<");
            Node *right = node->binop.right;
            node->binop.right = make_int(n, span);
            node_free(node->binop.left);
            node->binop.left = right;
            return node;
        }

        if (strcmp(op, "/") == 0 && rv.kind == CV_INT && rv.ival > 0 && is_power2(rv.ival)) {
            int n = log2_int(rv.ival);
            if (count) (*count)++;
            strcpy(node->binop.op, ">>");
            node_free(node->binop.right);
            node->binop.right = make_int(n, span);
            return node;
        }

        if (strcmp(op, "%") == 0 && rv.kind == CV_INT && rv.ival == 2) {
            if (count) (*count)++;
            strcpy(node->binop.op, "&");
            node_free(node->binop.right);
            node->binop.right = make_int(1, span);
            return node;
        }

        return node;
    }

    case NODE_BLOCK:
        for (int i = 0; i < node->block.stmts.len; i++)
            node->block.stmts.items[i] = opt_strength_reduce(node->block.stmts.items[i], count);
        node->block.expr = opt_strength_reduce(node->block.expr, count);
        return node;

    case NODE_IF:
        node->if_expr.cond = opt_strength_reduce(node->if_expr.cond, count);
        node->if_expr.then = opt_strength_reduce(node->if_expr.then, count);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++)
            node->if_expr.elif_conds.items[i] = opt_strength_reduce(node->if_expr.elif_conds.items[i], count);
        for (int i = 0; i < node->if_expr.elif_thens.len; i++)
            node->if_expr.elif_thens.items[i] = opt_strength_reduce(node->if_expr.elif_thens.items[i], count);
        node->if_expr.else_branch = opt_strength_reduce(node->if_expr.else_branch, count);
        return node;

    case NODE_WHILE:
        node->while_loop.cond = opt_strength_reduce(node->while_loop.cond, count);
        node->while_loop.body = opt_strength_reduce(node->while_loop.body, count);
        return node;

    case NODE_FOR:
        node->for_loop.iter = opt_strength_reduce(node->for_loop.iter, count);
        node->for_loop.body = opt_strength_reduce(node->for_loop.body, count);
        return node;

    case NODE_LOOP:
        node->loop.body = opt_strength_reduce(node->loop.body, count);
        return node;

    case NODE_RETURN:
        node->ret.value = opt_strength_reduce(node->ret.value, count);
        return node;

    case NODE_LET: case NODE_VAR:
        node->let.value = opt_strength_reduce(node->let.value, count);
        return node;

    case NODE_CONST:
        node->const_.value = opt_strength_reduce(node->const_.value, count);
        return node;

    case NODE_EXPR_STMT:
        node->expr_stmt.expr = opt_strength_reduce(node->expr_stmt.expr, count);
        return node;

    case NODE_FN_DECL:
        node->fn_decl.body = opt_strength_reduce(node->fn_decl.body, count);
        return node;

    case NODE_LAMBDA:
        node->lambda.body = opt_strength_reduce(node->lambda.body, count);
        return node;

    case NODE_CALL:
        node->call.callee = opt_strength_reduce(node->call.callee, count);
        for (int i = 0; i < node->call.args.len; i++)
            node->call.args.items[i] = opt_strength_reduce(node->call.args.items[i], count);
        return node;

    case NODE_MATCH:
        node->match.subject = opt_strength_reduce(node->match.subject, count);
        for (int i = 0; i < node->match.arms.len; i++)
            node->match.arms.items[i].body = opt_strength_reduce(node->match.arms.items[i].body, count);
        return node;

    case NODE_PROGRAM:
        for (int i = 0; i < node->program.stmts.len; i++)
            node->program.stmts.items[i] = opt_strength_reduce(node->program.stmts.items[i], count);
        return node;

    default:
        return node;
    }
}

// inline expansion

#define MAX_INLINE_PARAMS 4

typedef struct {
    char *name;
    Node *fn_decl;   /* not owned: points into AST */
} InlineCandidate;

typedef struct {
    InlineCandidate *items;
    int              len, cap;
} InlineCandidateList;

static int fn_is_inlineable(const Node *fn) {
    if (fn->tag != NODE_FN_DECL) return 0;
    if (!fn->fn_decl.body) return 0;
    if (fn->fn_decl.body->tag == NODE_BLOCK) return 0;
    if (fn->fn_decl.params.len > MAX_INLINE_PARAMS) return 0;
    if (fn->fn_decl.n_type_params > 0) return 0;
    return 1;
}

static Node *substitute_idents(Node *node,
                               const char **param_names, Node **arg_vals,
                               int nparam);

static Node *node_dup(const Node *n) {
    if (!n) return NULL;
    Node *d = node_new(n->tag, n->span);
    switch (n->tag) {
    case NODE_LIT_INT:
        d->lit_int.ival = n->lit_int.ival;
        break;
    case NODE_LIT_BIGINT:
        d->lit_bigint.bigint_str = n->lit_bigint.bigint_str;
        break;
    case NODE_LIT_FLOAT:
        d->lit_float.fval = n->lit_float.fval;
        break;
    case NODE_LIT_BOOL:
        d->lit_bool.bval = n->lit_bool.bval;
        break;
    case NODE_LIT_NULL:
        break;
    case NODE_LIT_STRING:
        d->lit_string.sval = xs_strdup(n->lit_string.sval);
        d->lit_string.interpolated = n->lit_string.interpolated;
        d->lit_string.parts = nodelist_new();
        for (int i = 0; i < n->lit_string.parts.len; i++)
            nodelist_push(&d->lit_string.parts, node_dup(n->lit_string.parts.items[i]));
        break;
    case NODE_IDENT:
        d->ident.name = xs_strdup(n->ident.name);
        break;
    case NODE_BINOP:
        memcpy(d->binop.op, n->binop.op, sizeof(n->binop.op));
        d->binop.left  = node_dup(n->binop.left);
        d->binop.right = node_dup(n->binop.right);
        break;
    case NODE_UNARY:
        memcpy(d->unary.op, n->unary.op, sizeof(n->unary.op));
        d->unary.expr   = node_dup(n->unary.expr);
        d->unary.prefix = n->unary.prefix;
        break;
    case NODE_CALL:
        d->call.callee = node_dup(n->call.callee);
        d->call.args   = nodelist_new();
        for (int i = 0; i < n->call.args.len; i++)
            nodelist_push(&d->call.args, node_dup(n->call.args.items[i]));
        d->call.kwargs = nodepairlist_new();
        break;
    case NODE_INDEX:
        d->index.obj   = node_dup(n->index.obj);
        d->index.index = node_dup(n->index.index);
        break;
    case NODE_FIELD:
        d->field.obj      = node_dup(n->field.obj);
        d->field.name     = xs_strdup(n->field.name);
        d->field.optional = n->field.optional;
        break;
    case NODE_CAST:
        d->cast.expr      = node_dup(n->cast.expr);
        d->cast.type_name = xs_strdup(n->cast.type_name);
        break;
    default:
        break;
    }
    return d;
}

static Node *substitute_idents(Node *node,
                               const char **param_names, Node **arg_vals,
                               int nparam) {
    if (!node) return NULL;

    if (node->tag == NODE_IDENT && node->ident.name) {
        for (int i = 0; i < nparam; i++) {
            if (param_names[i] && strcmp(node->ident.name, param_names[i]) == 0) {
                Node *replacement = node_dup(arg_vals[i]);
                node_free(node);
                return replacement;
            }
        }
        return node;
    }

    switch (node->tag) {
    case NODE_BINOP:
        node->binop.left  = substitute_idents(node->binop.left, param_names, arg_vals, nparam);
        node->binop.right = substitute_idents(node->binop.right, param_names, arg_vals, nparam);
        break;
    case NODE_UNARY:
        node->unary.expr = substitute_idents(node->unary.expr, param_names, arg_vals, nparam);
        break;
    case NODE_CALL:
        node->call.callee = substitute_idents(node->call.callee, param_names, arg_vals, nparam);
        for (int i = 0; i < node->call.args.len; i++)
            node->call.args.items[i] = substitute_idents(node->call.args.items[i], param_names, arg_vals, nparam);
        break;
    case NODE_INDEX:
        node->index.obj   = substitute_idents(node->index.obj, param_names, arg_vals, nparam);
        node->index.index = substitute_idents(node->index.index, param_names, arg_vals, nparam);
        break;
    case NODE_FIELD:
        node->field.obj = substitute_idents(node->field.obj, param_names, arg_vals, nparam);
        break;
    case NODE_CAST:
        node->cast.expr = substitute_idents(node->cast.expr, param_names, arg_vals, nparam);
        break;
    default:
        break;
    }
    return node;
}

static Node *try_inline_call(Node *call_node,
                             InlineCandidateList *candidates,
                             int *count) {
    if (call_node->tag != NODE_CALL) return NULL;
    if (!call_node->call.callee) return NULL;
    if (call_node->call.callee->tag != NODE_IDENT) return NULL;

    const char *name = call_node->call.callee->ident.name;
    if (!name) return NULL;

    Node *fn = NULL;
    for (int i = 0; i < candidates->len; i++) {
        if (strcmp(candidates->items[i].name, name) == 0) {
            fn = candidates->items[i].fn_decl;
            break;
        }
    }
    if (!fn) return NULL;
    if (call_node->call.args.len != fn->fn_decl.params.len) return NULL;

    int np = fn->fn_decl.params.len;
    const char **param_names = xs_malloc(sizeof(char*) * (np + 1));
    Node **arg_vals = xs_malloc(sizeof(Node*) * (np + 1));
    for (int i = 0; i < np; i++) {
        param_names[i] = fn->fn_decl.params.items[i].name;
        arg_vals[i] = call_node->call.args.items[i];
    }

    Node *body = node_dup(fn->fn_decl.body);
    body = substitute_idents(body, param_names, arg_vals, np);

    free(param_names);
    free(arg_vals);

    if (count) (*count)++;
    return body;
}

static Node *inline_walk(Node *node, InlineCandidateList *candidates, int *count) {
    if (!node) return NULL;

    switch (node->tag) {
    case NODE_CALL: {
        node->call.callee = inline_walk(node->call.callee, candidates, count);
        for (int i = 0; i < node->call.args.len; i++)
            node->call.args.items[i] = inline_walk(node->call.args.items[i], candidates, count);

        Node *inlined = try_inline_call(node, candidates, count);
        if (inlined) {
            node_free(node);
            return inlined;
        }
        return node;
    }

    case NODE_BINOP:
        node->binop.left  = inline_walk(node->binop.left, candidates, count);
        node->binop.right = inline_walk(node->binop.right, candidates, count);
        return node;

    case NODE_UNARY:
        node->unary.expr = inline_walk(node->unary.expr, candidates, count);
        return node;

    case NODE_BLOCK:
        for (int i = 0; i < node->block.stmts.len; i++)
            node->block.stmts.items[i] = inline_walk(node->block.stmts.items[i], candidates, count);
        node->block.expr = inline_walk(node->block.expr, candidates, count);
        return node;

    case NODE_IF:
        node->if_expr.cond = inline_walk(node->if_expr.cond, candidates, count);
        node->if_expr.then = inline_walk(node->if_expr.then, candidates, count);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++)
            node->if_expr.elif_conds.items[i] = inline_walk(node->if_expr.elif_conds.items[i], candidates, count);
        for (int i = 0; i < node->if_expr.elif_thens.len; i++)
            node->if_expr.elif_thens.items[i] = inline_walk(node->if_expr.elif_thens.items[i], candidates, count);
        node->if_expr.else_branch = inline_walk(node->if_expr.else_branch, candidates, count);
        return node;

    case NODE_WHILE:
        node->while_loop.cond = inline_walk(node->while_loop.cond, candidates, count);
        node->while_loop.body = inline_walk(node->while_loop.body, candidates, count);
        return node;

    case NODE_FOR:
        node->for_loop.iter = inline_walk(node->for_loop.iter, candidates, count);
        node->for_loop.body = inline_walk(node->for_loop.body, candidates, count);
        return node;

    case NODE_LOOP:
        node->loop.body = inline_walk(node->loop.body, candidates, count);
        return node;

    case NODE_RETURN:
        node->ret.value = inline_walk(node->ret.value, candidates, count);
        return node;

    case NODE_LET: case NODE_VAR:
        node->let.value = inline_walk(node->let.value, candidates, count);
        return node;

    case NODE_CONST:
        node->const_.value = inline_walk(node->const_.value, candidates, count);
        return node;

    case NODE_EXPR_STMT:
        node->expr_stmt.expr = inline_walk(node->expr_stmt.expr, candidates, count);
        return node;

    case NODE_FN_DECL:
        node->fn_decl.body = inline_walk(node->fn_decl.body, candidates, count);
        return node;

    case NODE_LAMBDA:
        node->lambda.body = inline_walk(node->lambda.body, candidates, count);
        return node;

    case NODE_INDEX:
        node->index.obj   = inline_walk(node->index.obj, candidates, count);
        node->index.index = inline_walk(node->index.index, candidates, count);
        return node;

    case NODE_FIELD:
        node->field.obj = inline_walk(node->field.obj, candidates, count);
        return node;

    case NODE_ASSIGN:
        node->assign.target = inline_walk(node->assign.target, candidates, count);
        node->assign.value  = inline_walk(node->assign.value, candidates, count);
        return node;

    case NODE_MATCH:
        node->match.subject = inline_walk(node->match.subject, candidates, count);
        for (int i = 0; i < node->match.arms.len; i++)
            node->match.arms.items[i].body = inline_walk(node->match.arms.items[i].body, candidates, count);
        return node;

    case NODE_METHOD_CALL:
        node->method_call.obj = inline_walk(node->method_call.obj, candidates, count);
        for (int i = 0; i < node->method_call.args.len; i++)
            node->method_call.args.items[i] = inline_walk(node->method_call.args.items[i], candidates, count);
        return node;

    case NODE_CAST:
        node->cast.expr = inline_walk(node->cast.expr, candidates, count);
        return node;

    default:
        return node;
    }
}

Node *opt_inline_expand(Node *program, int *count) {
    if (!program) return NULL;

    NodeList *stmts = NULL;
    if (program->tag == NODE_PROGRAM)
        stmts = &program->program.stmts;
    else
        return program; /* nothing to do for non-program nodes */

    InlineCandidateList candidates = {NULL, 0, 0};
    for (int i = 0; i < stmts->len; i++) {
        Node *s = stmts->items[i];
        if (fn_is_inlineable(s)) {
            if (candidates.len >= candidates.cap) {
                candidates.cap = candidates.cap ? candidates.cap * 2 : 8;
                candidates.items = xs_realloc(candidates.items,
                    candidates.cap * sizeof(InlineCandidate));
            }
            candidates.items[candidates.len].name = s->fn_decl.name;
            candidates.items[candidates.len].fn_decl = s;
            candidates.len++;
        }
    }

    if (candidates.len == 0) {
        free(candidates.items);
        return program;
    }

    for (int i = 0; i < stmts->len; i++)
        stmts->items[i] = inline_walk(stmts->items[i], &candidates, count);

    free(candidates.items);
    return program;
}

/* CSE */

#define CSE_BUCKETS 64

typedef struct CSEEntry {
    uint64_t         hash;
    char            *temp_name;
    const Node      *expr;       /* not owned, for comparison */
    struct CSEEntry *next;
} CSEEntry;

typedef struct {
    CSEEntry *buckets[CSE_BUCKETS];
    int       temp_counter;
} CSEMap;

static void cse_map_init(CSEMap *m) {
    memset(m->buckets, 0, sizeof(m->buckets));
    m->temp_counter = 0;
}

static void cse_map_free(CSEMap *m) {
    for (int i = 0; i < CSE_BUCKETS; i++) {
        CSEEntry *e = m->buckets[i];
        while (e) {
            CSEEntry *next = e->next;
            free(e->temp_name);
            free(e);
            e = next;
        }
    }
}

static const char *cse_map_lookup(CSEMap *m, const Node *expr, uint64_t h) {
    int idx = (int)(h % CSE_BUCKETS);
    for (CSEEntry *e = m->buckets[idx]; e; e = e->next) {
        if (e->hash == h && expr_equal(e->expr, expr))
            return e->temp_name;
    }
    return NULL;
}

static const char *cse_map_insert(CSEMap *m, const Node *expr, uint64_t h) {
    int idx = (int)(h % CSE_BUCKETS);
    CSEEntry *e = xs_malloc(sizeof(CSEEntry));
    e->hash = h;
    char buf[32];
    snprintf(buf, sizeof(buf), "_cse_%d", m->temp_counter++);
    e->temp_name = xs_strdup(buf);
    e->expr = expr;
    e->next = m->buckets[idx];
    m->buckets[idx] = e;
    return e->temp_name;
}

Node *opt_cse(Node *node, int *count) {
    if (!node) return NULL;

    switch (node->tag) {
    case NODE_BLOCK: {
        for (int i = 0; i < node->block.stmts.len; i++)
            node->block.stmts.items[i] = opt_cse(node->block.stmts.items[i], count);
        node->block.expr = opt_cse(node->block.expr, count);

        CSEMap map;
        cse_map_init(&map);

        NodeList new_stmts = nodelist_new();
        for (int i = 0; i < node->block.stmts.len; i++) {
            Node *stmt = node->block.stmts.items[i];
            Node *rhs = NULL;
            if ((stmt->tag == NODE_LET || stmt->tag == NODE_VAR) && stmt->let.value)
                rhs = stmt->let.value;
            else if (stmt->tag == NODE_CONST && stmt->const_.value)
                rhs = stmt->const_.value;
            else if (stmt->tag == NODE_EXPR_STMT)
                rhs = stmt->expr_stmt.expr;

            if (rhs && (rhs->tag == NODE_BINOP || rhs->tag == NODE_UNARY) && is_pure(rhs)) {
                if (rhs->tag == NODE_BINOP) {
                    Node *parts[2] = { rhs->binop.left, rhs->binop.right };
                    for (int p = 0; p < 2; p++) {
                        Node *sub = parts[p];
                        if (!sub) continue;
                        if (!is_pure(sub)) continue;
                        if (sub->tag != NODE_BINOP && sub->tag != NODE_UNARY) continue;

                        uint64_t h = expr_hash(sub);
                        const char *existing = cse_map_lookup(&map, sub, h);
                        if (existing) {
                            Node *ident = node_new(NODE_IDENT, sub->span);
                            ident->ident.name = xs_strdup(existing);
                            if (p == 0) {
                                node_free(rhs->binop.left);
                                rhs->binop.left = ident;
                            } else {
                                node_free(rhs->binop.right);
                                rhs->binop.right = ident;
                            }
                            if (count) (*count)++;
                        } else {
                            cse_map_insert(&map, sub, h);
                        }
                    }
                }
                uint64_t rh = expr_hash(rhs);
                const char *existing = cse_map_lookup(&map, rhs, rh);
                if (existing) {
                    Node *ident = node_new(NODE_IDENT, rhs->span);
                    ident->ident.name = xs_strdup(existing);
                    if ((stmt->tag == NODE_LET || stmt->tag == NODE_VAR)) {
                        node_free(stmt->let.value);
                        stmt->let.value = ident;
                    } else if (stmt->tag == NODE_CONST) {
                        node_free(stmt->const_.value);
                        stmt->const_.value = ident;
                    } else if (stmt->tag == NODE_EXPR_STMT) {
                        node_free(stmt->expr_stmt.expr);
                        stmt->expr_stmt.expr = ident;
                    }
                    if (count) (*count)++;
                } else {
                    const char *temp = cse_map_insert(&map, rhs, rh);

                    Node *temp_let = node_new(NODE_LET, rhs->span);
                    temp_let->let.name     = xs_strdup(temp);
                    temp_let->let.value    = node_dup(rhs);
                    temp_let->let.mutable  = 0;
                    temp_let->let.pattern  = NULL;
                    temp_let->let.type_ann = NULL;
                    nodelist_push(&new_stmts, temp_let);
                }
            } else if (rhs && is_pure(rhs)) {
                uint64_t h = expr_hash(rhs);
                if (!cse_map_lookup(&map, rhs, h))
                    cse_map_insert(&map, rhs, h);
            }
            nodelist_push(&new_stmts, stmt);
        }
        free(node->block.stmts.items);
        node->block.stmts = new_stmts;

        cse_map_free(&map);
        return node;
    }

    case NODE_IF:
        node->if_expr.cond = opt_cse(node->if_expr.cond, count);
        node->if_expr.then = opt_cse(node->if_expr.then, count);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++)
            node->if_expr.elif_conds.items[i] = opt_cse(node->if_expr.elif_conds.items[i], count);
        for (int i = 0; i < node->if_expr.elif_thens.len; i++)
            node->if_expr.elif_thens.items[i] = opt_cse(node->if_expr.elif_thens.items[i], count);
        node->if_expr.else_branch = opt_cse(node->if_expr.else_branch, count);
        return node;

    case NODE_WHILE:
        node->while_loop.cond = opt_cse(node->while_loop.cond, count);
        node->while_loop.body = opt_cse(node->while_loop.body, count);
        return node;

    case NODE_FOR:
        node->for_loop.iter = opt_cse(node->for_loop.iter, count);
        node->for_loop.body = opt_cse(node->for_loop.body, count);
        return node;

    case NODE_FN_DECL:
        node->fn_decl.body = opt_cse(node->fn_decl.body, count);
        return node;

    case NODE_LAMBDA:
        node->lambda.body = opt_cse(node->lambda.body, count);
        return node;

    case NODE_RETURN:
        node->ret.value = opt_cse(node->ret.value, count);
        return node;

    case NODE_LET: case NODE_VAR:
        node->let.value = opt_cse(node->let.value, count);
        return node;

    case NODE_EXPR_STMT:
        node->expr_stmt.expr = opt_cse(node->expr_stmt.expr, count);
        return node;

    case NODE_MATCH:
        node->match.subject = opt_cse(node->match.subject, count);
        for (int i = 0; i < node->match.arms.len; i++)
            node->match.arms.items[i].body = opt_cse(node->match.arms.items[i].body, count);
        return node;

    case NODE_PROGRAM:
        for (int i = 0; i < node->program.stmts.len; i++)
            node->program.stmts.items[i] = opt_cse(node->program.stmts.items[i], count);
        return node;

    default:
        return node;
    }
}

/* algebraic simplification */

static int same_expr(const Node *a, const Node *b) {
    return expr_equal(a, b);
}

Node *opt_algebraic_simplify(Node *node, int *count) {
    if (!node) return NULL;

    switch (node->tag) {
    case NODE_BINOP: {
        node->binop.left  = opt_algebraic_simplify(node->binop.left, count);
        node->binop.right = opt_algebraic_simplify(node->binop.right, count);

        const char *op = node->binop.op;
        Span span = node->span;
        ConstVal lv = const_val(node->binop.left);
        ConstVal rv = const_val(node->binop.right);

        if (strcmp(op, "+") == 0 && rv.kind == CV_FLOAT && rv.fval == 0.0) {
            if (count) (*count)++;
            Node *result = node->binop.left;
            node->binop.left = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "+") == 0 && lv.kind == CV_FLOAT && lv.fval == 0.0) {
            if (count) (*count)++;
            Node *result = node->binop.right;
            node->binop.right = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "*") == 0 && rv.kind == CV_FLOAT && rv.fval == 1.0) {
            if (count) (*count)++;
            Node *result = node->binop.left;
            node->binop.left = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "*") == 0 && lv.kind == CV_FLOAT && lv.fval == 1.0) {
            if (count) (*count)++;
            Node *result = node->binop.right;
            node->binop.right = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "*") == 0 && rv.kind == CV_FLOAT && rv.fval == 0.0) {
            if (count) (*count)++;
            Node *r2 = make_float(0.0, span);
            node_free(node);
            return r2;
        }
        if (strcmp(op, "*") == 0 && lv.kind == CV_FLOAT && lv.fval == 0.0) {
            if (count) (*count)++;
            Node *r2 = make_float(0.0, span);
            node_free(node);
            return r2;
        }
        /* x - x, x / x */
        if (strcmp(op, "-") == 0 && is_pure(node->binop.left) &&
            same_expr(node->binop.left, node->binop.right)) {
            if (count) (*count)++;
            Node *r2 = make_int(0, span);
            node_free(node);
            return r2;
        }
        if (strcmp(op, "/") == 0 && is_pure(node->binop.left) &&
            same_expr(node->binop.left, node->binop.right)) {
            /* Only fold if we can confirm non-zero (literal check) */
            ConstVal cv = const_val(node->binop.left);
            int known_nonzero = 0;
            if (cv.kind == CV_INT && cv.ival != 0) known_nonzero = 1;
            if (cv.kind == CV_FLOAT && cv.fval != 0.0) known_nonzero = 1;
            /* Identifiers: we conservatively fold x/x for identifiers too,
             * since division by zero would be a bug anyway. */
            if (node->binop.left->tag == NODE_IDENT) known_nonzero = 1;
            if (known_nonzero) {
                if (count) (*count)++;
                Node *r2 = make_int(1, span);
                node_free(node);
                return r2;
            }
        }

        if (strcmp(op, "&&") == 0 && rv.kind == CV_BOOL && rv.bval) {
            if (count) (*count)++;
            Node *result = node->binop.left;
            node->binop.left = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "&&") == 0 && lv.kind == CV_BOOL && lv.bval) {
            if (count) (*count)++;
            Node *result = node->binop.right;
            node->binop.right = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "&&") == 0 && rv.kind == CV_BOOL && !rv.bval) {
            if (count) (*count)++;
            Node *r2 = make_bool(0, span);
            node_free(node);
            return r2;
        }
        if (strcmp(op, "&&") == 0 && lv.kind == CV_BOOL && !lv.bval) {
            if (count) (*count)++;
            Node *r2 = make_bool(0, span);
            node_free(node);
            return r2;
        }
        if (strcmp(op, "||") == 0 && rv.kind == CV_BOOL && !rv.bval) {
            if (count) (*count)++;
            Node *result = node->binop.left;
            node->binop.left = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "||") == 0 && lv.kind == CV_BOOL && !lv.bval) {
            if (count) (*count)++;
            Node *result = node->binop.right;
            node->binop.right = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "||") == 0 && rv.kind == CV_BOOL && rv.bval) {
            if (count) (*count)++;
            Node *r2 = make_bool(1, span);
            node_free(node);
            return r2;
        }
        if (strcmp(op, "||") == 0 && lv.kind == CV_BOOL && lv.bval) {
            if (count) (*count)++;
            Node *r2 = make_bool(1, span);
            node_free(node);
            return r2;
        }

        if (strcmp(op, "++") == 0 && rv.kind == CV_STRING && rv.sval[0] == '\0') {
            if (count) (*count)++;
            Node *result = node->binop.left;
            node->binop.left = NULL;
            node_free(node);
            return result;
        }
        if (strcmp(op, "++") == 0 && lv.kind == CV_STRING && lv.sval[0] == '\0') {
            if (count) (*count)++;
            Node *result = node->binop.right;
            node->binop.right = NULL;
            node_free(node);
            return result;
        }

        return node;
    }

    case NODE_UNARY: {
        node->unary.expr = opt_algebraic_simplify(node->unary.expr, count);
        const char *op = node->unary.op;

        /* double negation */
        if (strcmp(op, "!") == 0 && node->unary.expr &&
            node->unary.expr->tag == NODE_UNARY &&
            strcmp(node->unary.expr->unary.op, "!") == 0) {
            if (count) (*count)++;
            Node *inner = node->unary.expr->unary.expr;
            node->unary.expr->unary.expr = NULL;
            node_free(node);
            return inner;
        }
        if (strcmp(op, "-") == 0 && node->unary.expr &&
            node->unary.expr->tag == NODE_UNARY &&
            strcmp(node->unary.expr->unary.op, "-") == 0) {
            if (count) (*count)++;
            Node *inner = node->unary.expr->unary.expr;
            node->unary.expr->unary.expr = NULL;
            node_free(node);
            return inner;
        }
        if (strcmp(op, "~") == 0 && node->unary.expr &&
            node->unary.expr->tag == NODE_UNARY &&
            strcmp(node->unary.expr->unary.op, "~") == 0) {
            if (count) (*count)++;
            Node *inner = node->unary.expr->unary.expr;
            node->unary.expr->unary.expr = NULL;
            node_free(node);
            return inner;
        }
        return node;
    }

    case NODE_BLOCK:
        for (int i = 0; i < node->block.stmts.len; i++)
            node->block.stmts.items[i] = opt_algebraic_simplify(node->block.stmts.items[i], count);
        node->block.expr = opt_algebraic_simplify(node->block.expr, count);
        return node;

    case NODE_IF:
        node->if_expr.cond = opt_algebraic_simplify(node->if_expr.cond, count);
        node->if_expr.then = opt_algebraic_simplify(node->if_expr.then, count);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++)
            node->if_expr.elif_conds.items[i] = opt_algebraic_simplify(node->if_expr.elif_conds.items[i], count);
        for (int i = 0; i < node->if_expr.elif_thens.len; i++)
            node->if_expr.elif_thens.items[i] = opt_algebraic_simplify(node->if_expr.elif_thens.items[i], count);
        node->if_expr.else_branch = opt_algebraic_simplify(node->if_expr.else_branch, count);
        return node;

    case NODE_WHILE:
        node->while_loop.cond = opt_algebraic_simplify(node->while_loop.cond, count);
        node->while_loop.body = opt_algebraic_simplify(node->while_loop.body, count);
        return node;

    case NODE_FOR:
        node->for_loop.iter = opt_algebraic_simplify(node->for_loop.iter, count);
        node->for_loop.body = opt_algebraic_simplify(node->for_loop.body, count);
        return node;

    case NODE_LOOP:
        node->loop.body = opt_algebraic_simplify(node->loop.body, count);
        return node;

    case NODE_RETURN:
        node->ret.value = opt_algebraic_simplify(node->ret.value, count);
        return node;

    case NODE_LET: case NODE_VAR:
        node->let.value = opt_algebraic_simplify(node->let.value, count);
        return node;

    case NODE_CONST:
        node->const_.value = opt_algebraic_simplify(node->const_.value, count);
        return node;

    case NODE_EXPR_STMT:
        node->expr_stmt.expr = opt_algebraic_simplify(node->expr_stmt.expr, count);
        return node;

    case NODE_FN_DECL:
        node->fn_decl.body = opt_algebraic_simplify(node->fn_decl.body, count);
        return node;

    case NODE_LAMBDA:
        node->lambda.body = opt_algebraic_simplify(node->lambda.body, count);
        return node;

    case NODE_CALL:
        node->call.callee = opt_algebraic_simplify(node->call.callee, count);
        for (int i = 0; i < node->call.args.len; i++)
            node->call.args.items[i] = opt_algebraic_simplify(node->call.args.items[i], count);
        return node;

    case NODE_METHOD_CALL:
        node->method_call.obj = opt_algebraic_simplify(node->method_call.obj, count);
        for (int i = 0; i < node->method_call.args.len; i++)
            node->method_call.args.items[i] = opt_algebraic_simplify(node->method_call.args.items[i], count);
        return node;

    case NODE_INDEX:
        node->index.obj   = opt_algebraic_simplify(node->index.obj, count);
        node->index.index = opt_algebraic_simplify(node->index.index, count);
        return node;

    case NODE_FIELD:
        node->field.obj = opt_algebraic_simplify(node->field.obj, count);
        return node;

    case NODE_ASSIGN:
        node->assign.target = opt_algebraic_simplify(node->assign.target, count);
        node->assign.value  = opt_algebraic_simplify(node->assign.value, count);
        return node;

    case NODE_CAST:
        node->cast.expr = opt_algebraic_simplify(node->cast.expr, count);
        return node;

    case NODE_MATCH:
        node->match.subject = opt_algebraic_simplify(node->match.subject, count);
        for (int i = 0; i < node->match.arms.len; i++)
            node->match.arms.items[i].body = opt_algebraic_simplify(node->match.arms.items[i].body, count);
        return node;

    case NODE_PROGRAM:
        for (int i = 0; i < node->program.stmts.len; i++)
            node->program.stmts.items[i] = opt_algebraic_simplify(node->program.stmts.items[i], count);
        return node;

    default:
        return node;
    }
}

/* constant propagation */

#define CPROP_BUCKETS 64

typedef struct CPropEntry {
    char              *name;
    Node              *value;   /* owned deep copy */
    struct CPropEntry *next;
} CPropEntry;

typedef struct {
    CPropEntry *buckets[CPROP_BUCKETS];
} CPropMap;

static void cprop_init(CPropMap *m) {
    memset(m->buckets, 0, sizeof(m->buckets));
}

static void cprop_free(CPropMap *m) {
    for (int i = 0; i < CPROP_BUCKETS; i++) {
        CPropEntry *e = m->buckets[i];
        while (e) {
            CPropEntry *next = e->next;
            free(e->name);
            node_free(e->value);
            free(e);
            e = next;
        }
        m->buckets[i] = NULL;
    }
}

static uint32_t cprop_hash(const char *s) {
    uint32_t h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h;
}

static void cprop_insert(CPropMap *m, const char *name, Node *value) {
    uint32_t idx = cprop_hash(name) % CPROP_BUCKETS;
    CPropEntry *e = xs_malloc(sizeof(CPropEntry));
    e->name  = xs_strdup(name);
    e->value = node_dup(value);
    e->next  = m->buckets[idx];
    m->buckets[idx] = e;
}

static Node *cprop_lookup(CPropMap *m, const char *name) {
    uint32_t idx = cprop_hash(name) % CPROP_BUCKETS;
    for (CPropEntry *e = m->buckets[idx]; e; e = e->next) {
        if (strcmp(e->name, name) == 0)
            return e->value;
    }
    return NULL;
}

static void cprop_remove(CPropMap *m, const char *name) {
    uint32_t idx = cprop_hash(name) % CPROP_BUCKETS;
    CPropEntry **pp = &m->buckets[idx];
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            CPropEntry *doomed = *pp;
            *pp = doomed->next;
            free(doomed->name);
            node_free(doomed->value);
            free(doomed);
            return;
        }
        pp = &(*pp)->next;
    }
}

static Node *cprop_walk(Node *node, CPropMap *env, int *count) {
    if (!node) return NULL;

    switch (node->tag) {
    case NODE_IDENT: {
        if (node->ident.name) {
            Node *val = cprop_lookup(env, node->ident.name);
            if (val) {
                if (count) (*count)++;
                Node *replacement = node_dup(val);
                replacement->span = node->span;
                node_free(node);
                return replacement;
            }
        }
        return node;
    }

    case NODE_LET: {
        node->let.value = cprop_walk(node->let.value, env, count);
        if (!node->let.mutable && node->let.name && node->let.value) {
            ConstVal cv = const_val(node->let.value);
            if (cv.kind != CV_NONE) {
                cprop_insert(env, node->let.name, node->let.value);
            }
        }
        return node;
    }

    case NODE_CONST: {
        node->const_.value = cprop_walk(node->const_.value, env, count);
        if (node->const_.name && node->const_.value) {
            ConstVal cv = const_val(node->const_.value);
            if (cv.kind != CV_NONE) {
                cprop_insert(env, node->const_.name, node->const_.value);
            }
        }
        return node;
    }

    case NODE_VAR: {
        node->let.value = cprop_walk(node->let.value, env, count);
        return node;
    }

    case NODE_ASSIGN: {
        node->assign.value = cprop_walk(node->assign.value, env, count);
        if (node->assign.target && node->assign.target->tag == NODE_IDENT &&
            node->assign.target->ident.name) {
            cprop_remove(env, node->assign.target->ident.name);
        }
        return node;
    }

    case NODE_BLOCK: {
        for (int i = 0; i < node->block.stmts.len; i++)
            node->block.stmts.items[i] = cprop_walk(node->block.stmts.items[i], env, count);
        node->block.expr = cprop_walk(node->block.expr, env, count);
        return node;
    }

    case NODE_BINOP:
        node->binop.left  = cprop_walk(node->binop.left, env, count);
        node->binop.right = cprop_walk(node->binop.right, env, count);
        return node;

    case NODE_UNARY:
        node->unary.expr = cprop_walk(node->unary.expr, env, count);
        return node;

    case NODE_IF:
        node->if_expr.cond = cprop_walk(node->if_expr.cond, env, count);
        node->if_expr.then = cprop_walk(node->if_expr.then, env, count);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++)
            node->if_expr.elif_conds.items[i] = cprop_walk(node->if_expr.elif_conds.items[i], env, count);
        for (int i = 0; i < node->if_expr.elif_thens.len; i++)
            node->if_expr.elif_thens.items[i] = cprop_walk(node->if_expr.elif_thens.items[i], env, count);
        node->if_expr.else_branch = cprop_walk(node->if_expr.else_branch, env, count);
        return node;

    case NODE_WHILE:
        node->while_loop.cond = cprop_walk(node->while_loop.cond, env, count);
        node->while_loop.body = cprop_walk(node->while_loop.body, env, count);
        return node;

    case NODE_FOR:
        node->for_loop.iter = cprop_walk(node->for_loop.iter, env, count);
        node->for_loop.body = cprop_walk(node->for_loop.body, env, count);
        return node;

    case NODE_LOOP:
        node->loop.body = cprop_walk(node->loop.body, env, count);
        return node;

    case NODE_RETURN:
        node->ret.value = cprop_walk(node->ret.value, env, count);
        return node;

    case NODE_EXPR_STMT:
        node->expr_stmt.expr = cprop_walk(node->expr_stmt.expr, env, count);
        return node;

    case NODE_FN_DECL: {
            CPropMap fn_env;
            cprop_init(&fn_env);
            node->fn_decl.body = cprop_walk(node->fn_decl.body, &fn_env, count);
            cprop_free(&fn_env);
        }
        return node;

    case NODE_LAMBDA: {
        CPropMap lam_env;
        cprop_init(&lam_env);
        node->lambda.body = cprop_walk(node->lambda.body, &lam_env, count);
        cprop_free(&lam_env);
        return node;
    }

    case NODE_CALL:
        node->call.callee = cprop_walk(node->call.callee, env, count);
        for (int i = 0; i < node->call.args.len; i++)
            node->call.args.items[i] = cprop_walk(node->call.args.items[i], env, count);
        return node;

    case NODE_METHOD_CALL:
        node->method_call.obj = cprop_walk(node->method_call.obj, env, count);
        for (int i = 0; i < node->method_call.args.len; i++)
            node->method_call.args.items[i] = cprop_walk(node->method_call.args.items[i], env, count);
        return node;

    case NODE_INDEX:
        node->index.obj   = cprop_walk(node->index.obj, env, count);
        node->index.index = cprop_walk(node->index.index, env, count);
        return node;

    case NODE_FIELD:
        node->field.obj = cprop_walk(node->field.obj, env, count);
        return node;

    case NODE_CAST:
        node->cast.expr = cprop_walk(node->cast.expr, env, count);
        return node;

    case NODE_MATCH:
        node->match.subject = cprop_walk(node->match.subject, env, count);
        for (int i = 0; i < node->match.arms.len; i++)
            node->match.arms.items[i].body = cprop_walk(node->match.arms.items[i].body, env, count);
        return node;

    case NODE_PROGRAM:
        for (int i = 0; i < node->program.stmts.len; i++)
            node->program.stmts.items[i] = cprop_walk(node->program.stmts.items[i], env, count);
        return node;

    default:
        return node;
    }
}

Node *opt_constant_propagate(Node *node, int *count) {
    CPropMap env;
    cprop_init(&env);
    node = cprop_walk(node, &env, count);
    cprop_free(&env);
    return node;
}


typedef struct {
    char **items;
    int    len, cap;
} NameList;

static void namelist_init(NameList *nl) { nl->items = NULL; nl->len = nl->cap = 0; }
static void namelist_free(NameList *nl) {
    for (int i = 0; i < nl->len; i++) free(nl->items[i]);
    free(nl->items);
}
static void namelist_push(NameList *nl, const char *name) {
    for (int i = 0; i < nl->len; i++)
        if (strcmp(nl->items[i], name) == 0) return;
    if (nl->len >= nl->cap) {
        nl->cap = nl->cap ? nl->cap * 2 : 8;
        nl->items = xs_realloc(nl->items, nl->cap * sizeof(char*));
    }
    nl->items[nl->len++] = xs_strdup(name);
}
static int namelist_contains(const NameList *nl, const char *name) {
    for (int i = 0; i < nl->len; i++)
        if (strcmp(nl->items[i], name) == 0) return 1;
    return 0;
}

static void collect_modified_names(const Node *n, NameList *names) {
    if (!n) return;
    switch (n->tag) {
    case NODE_ASSIGN:
        if (n->assign.target && n->assign.target->tag == NODE_IDENT &&
            n->assign.target->ident.name)
            namelist_push(names, n->assign.target->ident.name);
        collect_modified_names(n->assign.value, names);
        break;
    case NODE_LET: case NODE_VAR:
        if (n->let.name) namelist_push(names, n->let.name);
        collect_modified_names(n->let.value, names);
        break;
    case NODE_CONST:
        if (n->const_.name) namelist_push(names, n->const_.name);
        collect_modified_names(n->const_.value, names);
        break;
    case NODE_BLOCK:
        for (int i = 0; i < n->block.stmts.len; i++)
            collect_modified_names(n->block.stmts.items[i], names);
        collect_modified_names(n->block.expr, names);
        break;
    case NODE_IF:
        collect_modified_names(n->if_expr.cond, names);
        collect_modified_names(n->if_expr.then, names);
        for (int i = 0; i < n->if_expr.elif_conds.len; i++)
            collect_modified_names(n->if_expr.elif_conds.items[i], names);
        for (int i = 0; i < n->if_expr.elif_thens.len; i++)
            collect_modified_names(n->if_expr.elif_thens.items[i], names);
        collect_modified_names(n->if_expr.else_branch, names);
        break;
    case NODE_WHILE:
        collect_modified_names(n->while_loop.cond, names);
        collect_modified_names(n->while_loop.body, names);
        break;
    case NODE_FOR:
        if (n->for_loop.pattern && n->for_loop.pattern->tag == NODE_PAT_IDENT) {
            if (n->for_loop.pattern->pat_ident.name)
                namelist_push(names, n->for_loop.pattern->pat_ident.name);
        }
        collect_modified_names(n->for_loop.body, names);
        break;
    case NODE_LOOP:
        collect_modified_names(n->loop.body, names);
        break;
    case NODE_BINOP:
        collect_modified_names(n->binop.left, names);
        collect_modified_names(n->binop.right, names);
        break;
    case NODE_UNARY:
        collect_modified_names(n->unary.expr, names);
        break;
    case NODE_CALL:
        collect_modified_names(n->call.callee, names);
        for (int i = 0; i < n->call.args.len; i++)
            collect_modified_names(n->call.args.items[i], names);
        break;
    case NODE_RETURN:
        collect_modified_names(n->ret.value, names);
        break;
    case NODE_EXPR_STMT:
        collect_modified_names(n->expr_stmt.expr, names);
        break;
    default:
        break;
    }
}

static int references_modified(const Node *n, const NameList *modified) {
    if (!n) return 0;
    switch (n->tag) {
    case NODE_IDENT:
        return n->ident.name && namelist_contains(modified, n->ident.name);
    case NODE_BINOP:
        return references_modified(n->binop.left, modified) ||
               references_modified(n->binop.right, modified);
    case NODE_UNARY:
        return references_modified(n->unary.expr, modified);
    case NODE_INDEX:
        return references_modified(n->index.obj, modified) ||
               references_modified(n->index.index, modified);
    case NODE_FIELD:
        return references_modified(n->field.obj, modified);
    case NODE_CALL:
        return 1;
    case NODE_METHOD_CALL:
        return 1;
    default:
        return 0;
    }
}

static int is_loop_invariant(const Node *expr, const NameList *modified) {
    if (!expr) return 0;
    if (!is_pure(expr)) return 0;
    if (references_modified(expr, modified)) return 0;
    if (expr->tag == NODE_LIT_INT || expr->tag == NODE_LIT_BIGINT ||
        expr->tag == NODE_LIT_FLOAT ||
        expr->tag == NODE_LIT_BOOL || expr->tag == NODE_LIT_NULL ||
        expr->tag == NODE_LIT_STRING || expr->tag == NODE_LIT_CHAR ||
        expr->tag == NODE_IDENT)
        return 0;
    return 1;
}

static NodeList hoist_from_block(Node *block, const NameList *modified,
                                 int *count, int *hoist_counter) {
    NodeList hoisted = nodelist_new();
    if (!block || block->tag != NODE_BLOCK) return hoisted;

    for (int i = 0; i < block->block.stmts.len; i++) {
        Node *stmt = block->block.stmts.items[i];
        if ((stmt->tag == NODE_LET || stmt->tag == NODE_VAR) &&
            stmt->let.value && !stmt->let.mutable) {
            Node *rhs = stmt->let.value;
            if (is_loop_invariant(rhs, modified)) {
                char temp_name[32];
                snprintf(temp_name, sizeof(temp_name), "_licm_%d", (*hoist_counter)++);

                Node *hoisted_let = node_new(NODE_LET, rhs->span);
                hoisted_let->let.name     = xs_strdup(temp_name);
                hoisted_let->let.value    = rhs;
                hoisted_let->let.mutable  = 0;
                hoisted_let->let.pattern  = NULL;
                hoisted_let->let.type_ann = NULL;

                Node *ref = node_new(NODE_IDENT, rhs->span);
                ref->ident.name = xs_strdup(temp_name);
                stmt->let.value = ref;

                nodelist_push(&hoisted, hoisted_let);
                if (count) (*count)++;
            }
        }
    }
    return hoisted;
}

/* Recursively walk and apply LICM to while/for/loop nodes. */
static int licm_counter = 0;

static Node *licm_walk(Node *node, int *count);

/* Wrap a loop node: insert hoisted statements before the loop in a new block. */
static Node *wrap_with_hoisted(Node *loop_node, NodeList hoisted) {
    if (hoisted.len == 0) {
        free(hoisted.items);
        return loop_node;
    }
    /* Create a block: { hoisted_lets... ; loop } */
    Node *wrapper = node_new(NODE_BLOCK, loop_node->span);
    wrapper->block.stmts = hoisted;
    nodelist_push(&wrapper->block.stmts, loop_node);
    wrapper->block.expr = NULL;
    return wrapper;
}

static Node *licm_walk(Node *node, int *count) {
    if (!node) return NULL;

    switch (node->tag) {
    case NODE_WHILE: {
        /* Recurse into body first */
        node->while_loop.body = licm_walk(node->while_loop.body, count);

        /* Collect modified names in the loop body */
        NameList modified;
        namelist_init(&modified);
        collect_modified_names(node->while_loop.body, &modified);
        collect_modified_names(node->while_loop.cond, &modified);

        /* Hoist invariant expressions from the body */
        NodeList hoisted = hoist_from_block(node->while_loop.body, &modified,
                                            count, &licm_counter);
        namelist_free(&modified);
        return wrap_with_hoisted(node, hoisted);
    }

    case NODE_FOR: {
        node->for_loop.body = licm_walk(node->for_loop.body, count);

        NameList modified;
        namelist_init(&modified);
        collect_modified_names(node->for_loop.body, &modified);
        /* The loop variable is also modified each iteration */
        if (node->for_loop.pattern && node->for_loop.pattern->tag == NODE_PAT_IDENT) {
            /* Access pattern name - check the struct */
            const Node *pat = node->for_loop.pattern;
            if (pat->pat_ident.name)
                namelist_push(&modified, pat->pat_ident.name);
        }

        NodeList hoisted = hoist_from_block(node->for_loop.body, &modified,
                                            count, &licm_counter);
        namelist_free(&modified);
        return wrap_with_hoisted(node, hoisted);
    }

    case NODE_LOOP: {
        node->loop.body = licm_walk(node->loop.body, count);

        NameList modified;
        namelist_init(&modified);
        collect_modified_names(node->loop.body, &modified);

        NodeList hoisted = hoist_from_block(node->loop.body, &modified,
                                            count, &licm_counter);
        namelist_free(&modified);
        return wrap_with_hoisted(node, hoisted);
    }

    /* Recurse into containers */
    case NODE_BLOCK:
        for (int i = 0; i < node->block.stmts.len; i++)
            node->block.stmts.items[i] = licm_walk(node->block.stmts.items[i], count);
        node->block.expr = licm_walk(node->block.expr, count);
        return node;

    case NODE_IF:
        node->if_expr.cond = licm_walk(node->if_expr.cond, count);
        node->if_expr.then = licm_walk(node->if_expr.then, count);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++)
            node->if_expr.elif_conds.items[i] = licm_walk(node->if_expr.elif_conds.items[i], count);
        for (int i = 0; i < node->if_expr.elif_thens.len; i++)
            node->if_expr.elif_thens.items[i] = licm_walk(node->if_expr.elif_thens.items[i], count);
        node->if_expr.else_branch = licm_walk(node->if_expr.else_branch, count);
        return node;

    case NODE_RETURN:
        node->ret.value = licm_walk(node->ret.value, count);
        return node;

    case NODE_LET: case NODE_VAR:
        node->let.value = licm_walk(node->let.value, count);
        return node;

    case NODE_CONST:
        node->const_.value = licm_walk(node->const_.value, count);
        return node;

    case NODE_EXPR_STMT:
        node->expr_stmt.expr = licm_walk(node->expr_stmt.expr, count);
        return node;

    case NODE_FN_DECL:
        node->fn_decl.body = licm_walk(node->fn_decl.body, count);
        return node;

    case NODE_LAMBDA:
        node->lambda.body = licm_walk(node->lambda.body, count);
        return node;

    case NODE_MATCH:
        node->match.subject = licm_walk(node->match.subject, count);
        for (int i = 0; i < node->match.arms.len; i++)
            node->match.arms.items[i].body = licm_walk(node->match.arms.items[i].body, count);
        return node;

    case NODE_PROGRAM:
        for (int i = 0; i < node->program.stmts.len; i++)
            node->program.stmts.items[i] = licm_walk(node->program.stmts.items[i], count);
        return node;

    default:
        return node;
    }
}

Node *opt_loop_invariant_motion(Node *node, int *count) {
    licm_counter = 0;
    return licm_walk(node, count);
}

/* Count how many times a name appears as NODE_IDENT in the subtree,
 * excluding the binding site itself. */
static int count_ident_refs(const Node *n, const char *name) {
    if (!n) return 0;
    switch (n->tag) {
    case NODE_IDENT:
        return (n->ident.name && strcmp(n->ident.name, name) == 0) ? 1 : 0;
    case NODE_BINOP:
        return count_ident_refs(n->binop.left, name) +
               count_ident_refs(n->binop.right, name);
    case NODE_UNARY:
        return count_ident_refs(n->unary.expr, name);
    case NODE_BLOCK:
        { int c = 0;
          for (int i = 0; i < n->block.stmts.len; i++)
              c += count_ident_refs(n->block.stmts.items[i], name);
          c += count_ident_refs(n->block.expr, name);
          return c;
        }
    case NODE_IF:
        { int c = count_ident_refs(n->if_expr.cond, name) +
                   count_ident_refs(n->if_expr.then, name) +
                   count_ident_refs(n->if_expr.else_branch, name);
          for (int i = 0; i < n->if_expr.elif_conds.len; i++)
              c += count_ident_refs(n->if_expr.elif_conds.items[i], name);
          for (int i = 0; i < n->if_expr.elif_thens.len; i++)
              c += count_ident_refs(n->if_expr.elif_thens.items[i], name);
          return c;
        }
    case NODE_WHILE:
        return count_ident_refs(n->while_loop.cond, name) +
               count_ident_refs(n->while_loop.body, name);
    case NODE_FOR:
        return count_ident_refs(n->for_loop.iter, name) +
               count_ident_refs(n->for_loop.body, name);
    case NODE_LOOP:
        return count_ident_refs(n->loop.body, name);
    case NODE_RETURN:
        return count_ident_refs(n->ret.value, name);
    case NODE_LET: case NODE_VAR:
        return count_ident_refs(n->let.value, name);
    case NODE_CONST:
        return count_ident_refs(n->const_.value, name);
    case NODE_EXPR_STMT:
        return count_ident_refs(n->expr_stmt.expr, name);
    case NODE_FN_DECL:
        return count_ident_refs(n->fn_decl.body, name);
    case NODE_LAMBDA:
        return count_ident_refs(n->lambda.body, name);
    case NODE_CALL:
        { int c = count_ident_refs(n->call.callee, name);
          for (int i = 0; i < n->call.args.len; i++)
              c += count_ident_refs(n->call.args.items[i], name);
          return c;
        }
    case NODE_METHOD_CALL:
        { int c = count_ident_refs(n->method_call.obj, name);
          for (int i = 0; i < n->method_call.args.len; i++)
              c += count_ident_refs(n->method_call.args.items[i], name);
          return c;
        }
    case NODE_INDEX:
        return count_ident_refs(n->index.obj, name) +
               count_ident_refs(n->index.index, name);
    case NODE_FIELD:
        return count_ident_refs(n->field.obj, name);
    case NODE_ASSIGN:
        return count_ident_refs(n->assign.target, name) +
               count_ident_refs(n->assign.value, name);
    case NODE_CAST:
        return count_ident_refs(n->cast.expr, name);
    case NODE_MATCH:
        { int c = count_ident_refs(n->match.subject, name);
          for (int i = 0; i < n->match.arms.len; i++)
              c += count_ident_refs(n->match.arms.items[i].body, name);
          return c;
        }
    case NODE_PROGRAM:
        { int c = 0;
          for (int i = 0; i < n->program.stmts.len; i++)
              c += count_ident_refs(n->program.stmts.items[i], name);
          return c;
        }
    default:
        return 0;
    }
}

/* Check remaining siblings for references to a name (for block-scoped checks). */
static int refs_in_siblings(const NodeList *stmts, int start, const char *name,
                            const Node *block_expr) {
    for (int i = start; i < stmts->len; i++) {
        if (count_ident_refs(stmts->items[i], name) > 0)
            return 1;
    }
    if (count_ident_refs(block_expr, name) > 0)
        return 1;
    return 0;
}

Node *opt_unused_var_elim(Node *node, int *count) {
    if (!node) return NULL;

    switch (node->tag) {
    case NODE_BLOCK: {
        /* First recurse into all children */
        for (int i = 0; i < node->block.stmts.len; i++)
            node->block.stmts.items[i] = opt_unused_var_elim(node->block.stmts.items[i], count);
        node->block.expr = opt_unused_var_elim(node->block.expr, count);

        /* Now scan for unused let bindings with pure RHS */
        NodeList new_stmts = nodelist_new();
        for (int i = 0; i < node->block.stmts.len; i++) {
            Node *stmt = node->block.stmts.items[i];
            int can_remove = 0;

            if (stmt->tag == NODE_LET && stmt->let.name && !stmt->let.mutable) {
                /* Check if the RHS is pure (no side effects) */
                if (is_pure(stmt->let.value)) {
                    /* Check if the name is referenced anywhere in subsequent stmts
                     * or the block expression */
                    if (!refs_in_siblings(&node->block.stmts, i + 1,
                                         stmt->let.name, node->block.expr)) {
                        can_remove = 1;
                    }
                }
            }

            if (can_remove) {
                if (count) (*count)++;
                node_free(stmt);
            } else {
                nodelist_push(&new_stmts, stmt);
            }
        }
        free(node->block.stmts.items);
        node->block.stmts = new_stmts;
        return node;
    }

    case NODE_PROGRAM: {
        /* Recurse into all children first */
        for (int i = 0; i < node->program.stmts.len; i++)
            node->program.stmts.items[i] = opt_unused_var_elim(node->program.stmts.items[i], count);

        /* Scan for unused let bindings at program level */
        NodeList new_stmts = nodelist_new();
        for (int i = 0; i < node->program.stmts.len; i++) {
            Node *stmt = node->program.stmts.items[i];
            int can_remove = 0;

            if (stmt->tag == NODE_LET && stmt->let.name && !stmt->let.mutable) {
                if (is_pure(stmt->let.value)) {
                    /* Check remaining siblings */
                    int found = 0;
                    for (int j = i + 1; j < node->program.stmts.len; j++) {
                        if (count_ident_refs(node->program.stmts.items[j],
                                             stmt->let.name) > 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) can_remove = 1;
                }
            }

            if (can_remove) {
                if (count) (*count)++;
                node_free(stmt);
            } else {
                nodelist_push(&new_stmts, stmt);
            }
        }
        free(node->program.stmts.items);
        node->program.stmts = new_stmts;
        return node;
    }

    /* Recurse into other containers */
    case NODE_IF:
        node->if_expr.cond = opt_unused_var_elim(node->if_expr.cond, count);
        node->if_expr.then = opt_unused_var_elim(node->if_expr.then, count);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++)
            node->if_expr.elif_conds.items[i] = opt_unused_var_elim(node->if_expr.elif_conds.items[i], count);
        for (int i = 0; i < node->if_expr.elif_thens.len; i++)
            node->if_expr.elif_thens.items[i] = opt_unused_var_elim(node->if_expr.elif_thens.items[i], count);
        node->if_expr.else_branch = opt_unused_var_elim(node->if_expr.else_branch, count);
        return node;

    case NODE_WHILE:
        node->while_loop.cond = opt_unused_var_elim(node->while_loop.cond, count);
        node->while_loop.body = opt_unused_var_elim(node->while_loop.body, count);
        return node;

    case NODE_FOR:
        node->for_loop.iter = opt_unused_var_elim(node->for_loop.iter, count);
        node->for_loop.body = opt_unused_var_elim(node->for_loop.body, count);
        return node;

    case NODE_LOOP:
        node->loop.body = opt_unused_var_elim(node->loop.body, count);
        return node;

    case NODE_FN_DECL:
        node->fn_decl.body = opt_unused_var_elim(node->fn_decl.body, count);
        return node;

    case NODE_LAMBDA:
        node->lambda.body = opt_unused_var_elim(node->lambda.body, count);
        return node;

    case NODE_RETURN:
        node->ret.value = opt_unused_var_elim(node->ret.value, count);
        return node;

    case NODE_LET: case NODE_VAR:
        node->let.value = opt_unused_var_elim(node->let.value, count);
        return node;

    case NODE_CONST:
        node->const_.value = opt_unused_var_elim(node->const_.value, count);
        return node;

    case NODE_EXPR_STMT:
        node->expr_stmt.expr = opt_unused_var_elim(node->expr_stmt.expr, count);
        return node;

    case NODE_MATCH:
        node->match.subject = opt_unused_var_elim(node->match.subject, count);
        for (int i = 0; i < node->match.arms.len; i++)
            node->match.arms.items[i].body = opt_unused_var_elim(node->match.arms.items[i].body, count);
        return node;

    default:
        return node;
    }
}

Node *optimize(Node *program, OptStats *stats) {
    OptStats s = {0};

    program = opt_constant_propagate(program, &s.constants_propagated);
    program = opt_constant_fold(program, &s.constants_folded);
    program = opt_algebraic_simplify(program, &s.algebraic_simplified);
    program = opt_dead_code_elim(program, &s.dead_code_removed);
    program = opt_strength_reduce(program, &s.strengths_reduced);
    program = opt_constant_fold(program, &s.constants_folded);
    program = opt_inline_expand(program, &s.functions_inlined);
    program = opt_constant_propagate(program, &s.constants_propagated);
    program = opt_constant_fold(program, &s.constants_folded);
    program = opt_algebraic_simplify(program, &s.algebraic_simplified);
    program = opt_loop_invariant_motion(program, &s.loop_invariants_hoisted);
    program = opt_cse(program, &s.cses_eliminated);
    program = opt_unused_var_elim(program, &s.unused_vars_eliminated);
    program = opt_dead_code_elim(program, &s.dead_code_removed);
    program = opt_constant_fold(program, &s.constants_folded);

    if (stats) *stats = s;
    return program;
}
