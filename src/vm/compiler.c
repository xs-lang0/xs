#include "vm/compiler.h"
#include "core/value.h"
#include "core/xs_bigint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOCALS   256
#define MAX_UPVALUES 256

typedef struct Local {
    char  name[128];
    int   slot;
    int   depth;
    int   is_captured;
} Local;

typedef struct CompilerScope {
    XSProto              *proto;
    Local                 locals[MAX_LOCALS];
    int                   n_locals;
    int                   scope_depth;
    UVDesc                uv_descs[MAX_UPVALUES];
    int                   n_upvalues;
    struct CompilerScope *enclosing;
    /* actor method state write-back */
    char                **actor_state_names;
    int                   actor_nstate;
} CompilerScope;

#define MAX_LOOP_DEPTH 64
#define MAX_BREAK_PATCHES 256

typedef struct {
    int break_patches[MAX_BREAK_PATCHES];
    int n_break_patches;
    int continue_patches[MAX_BREAK_PATCHES];
    int n_continue_patches;
    int continue_target;  /* ip to jump for continue */
    char label[64];       /* loop label (empty string if none) */
} LoopCtx;

typedef struct {
    CompilerScope *current;
    int            n_errors;
    LoopCtx        loop_stack[MAX_LOOP_DEPTH];
    int            loop_depth;
} Compiler;

static void scope_push(Compiler *c, CompilerScope *s, XSProto *proto) {
    memset(s, 0, sizeof *s);
    s->proto     = proto;
    s->enclosing = c->current;
    c->current   = s;
}

static void scope_pop(Compiler *c) {
    c->current = c->current->enclosing;
}

static void scope_begin(Compiler *c) {
    c->current->scope_depth++;
}

static void scope_end(Compiler *c) {
    CompilerScope *s = c->current;
    s->scope_depth--;
    while (s->n_locals > 0 &&
           s->locals[s->n_locals - 1].depth > s->scope_depth) {
        s->n_locals--;
    }
}

static int local_add(CompilerScope *scope, const char *name) {
    if (scope->n_locals >= MAX_LOCALS) {
        fprintf(stderr, "too many locals\n");
        return 0;
    }
    Local *l = &scope->locals[scope->n_locals++];
    strncpy(l->name, name, sizeof(l->name) - 1);
    l->name[sizeof(l->name) - 1] = '\0';
    l->slot        = scope->proto->nlocals++;
    l->depth       = scope->scope_depth;
    l->is_captured = 0;
    return l->slot;
}

static int local_resolve(CompilerScope *scope, const char *name) {
    for (int i = scope->n_locals - 1; i >= 0; i--)
        if (strcmp(scope->locals[i].name, name) == 0)
            return scope->locals[i].slot;
    return -1;
}

/* upvalue tracking */

static int upvalue_add(CompilerScope *scope, int is_local, int index) {
    for (int i = 0; i < scope->n_upvalues; i++)
        if (scope->uv_descs[i].is_local == is_local &&
            scope->uv_descs[i].index    == index)
            return i;
    if (scope->n_upvalues >= MAX_UPVALUES) {
        fprintf(stderr, "too many upvalues\n");
        return 0;
    }
    scope->uv_descs[scope->n_upvalues].is_local = is_local;
    scope->uv_descs[scope->n_upvalues].index    = index;
    return scope->n_upvalues++;
}

static int upvalue_resolve(CompilerScope *scope, const char *name) {
    if (!scope->enclosing) return -1;
    int slot = local_resolve(scope->enclosing, name);
    if (slot >= 0) {
        for (int i = 0; i < scope->enclosing->n_locals; i++) {
            if (scope->enclosing->locals[i].slot == slot) {
                scope->enclosing->locals[i].is_captured = 1;
                break;
            }
        }
        return upvalue_add(scope, 1, slot);
    }
    int uv = upvalue_resolve(scope->enclosing, name);
    if (uv >= 0)
        return upvalue_add(scope, 0, uv);
    return -1;
}

static void emit(Compiler *c, Instruction instr) {
    chunk_write(&c->current->proto->chunk, instr);
}

static void emit_a(Compiler *c, Opcode op, int bx) {
    emit(c, MAKE_A(op, 0, (uint16_t)(unsigned)bx));
}

static void emit_const(Compiler *c, Value *v) {
    int idx = chunk_add_const(&c->current->proto->chunk, v);
    value_decref(v);
    emit_a(c, OP_PUSH_CONST, idx);
}

static int emit_jump(Compiler *c, Opcode op) {
    int idx = c->current->proto->chunk.len;
    emit(c, MAKE_A(op, 0, 0));
    return idx;
}

static void patch_jump(Compiler *c, int instr_idx) {
    int offset = c->current->proto->chunk.len - instr_idx - 1;
    Instruction *ip = &c->current->proto->chunk.code[instr_idx];
    *ip = (*ip & 0x0000FFFFU) | ((Instruction)(uint16_t)(int16_t)offset << 16);
}

static int emit_global_name(Compiler *c, const char *name) {
    Value *v = xs_str(name);
    int idx  = chunk_add_const(&c->current->proto->chunk, v);
    value_decref(v);
    return idx;
}

static int local_add_hidden(Compiler *c) {
    static int _hidden = 0;
    char buf[32];
    snprintf(buf, sizeof buf, "__h%d", _hidden++);
    return local_add(c->current, buf);
}

static void loop_push_label(Compiler *c, int continue_target, const char *label) {
    if (c->loop_depth >= MAX_LOOP_DEPTH) return;
    LoopCtx *lc = &c->loop_stack[c->loop_depth++];
    lc->n_break_patches = 0;
    lc->n_continue_patches = 0;
    lc->continue_target = continue_target;
    if (label) { strncpy(lc->label, label, 63); lc->label[63] = '\0'; }
    else lc->label[0] = '\0';
}

static void loop_pop_patch_breaks(Compiler *c) {
    if (c->loop_depth <= 0) return;
    LoopCtx *lc = &c->loop_stack[--c->loop_depth];
    int exit_ip = c->current->proto->chunk.len;
    for (int i = 0; i < lc->n_break_patches; i++) {
        int idx = lc->break_patches[i];
        int offset = exit_ip - idx - 1;
        Instruction *ip2 = &c->current->proto->chunk.code[idx];
        *ip2 = (*ip2 & 0x0000FFFFU) | ((Instruction)(uint16_t)(int16_t)offset << 16);
    }
}

static void loop_add_break(Compiler *c, int patch_idx) {
    if (c->loop_depth <= 0) return;
    LoopCtx *lc = &c->loop_stack[c->loop_depth - 1];
    if (lc->n_break_patches < MAX_BREAK_PATCHES)
        lc->break_patches[lc->n_break_patches++] = patch_idx;
}

/* name resolution */

static void compile_name_load(Compiler *c, const char *name) {
    int slot = local_resolve(c->current, name);
    if (slot >= 0) { emit_a(c, OP_LOAD_LOCAL,   slot); return; }
    int uv = upvalue_resolve(c->current, name);
    if (uv   >= 0) { emit_a(c, OP_LOAD_UPVALUE, uv);   return; }
    emit_a(c, OP_LOAD_GLOBAL, emit_global_name(c, name));
}

static void compile_name_store(Compiler *c, const char *name) {
    int slot = local_resolve(c->current, name);
    if (slot >= 0) { emit_a(c, OP_STORE_LOCAL,   slot); return; }
    int uv = upvalue_resolve(c->current, name);
    if (uv   >= 0) { emit_a(c, OP_STORE_UPVALUE, uv);   return; }
    emit_a(c, OP_STORE_GLOBAL, emit_global_name(c, name));
}

static void compile_node(Compiler *c, Node *n, int want_value);

static int compile_fn(Compiler *c, const char *name,
                      ParamList *params, Node *body)
{
    int total_params = params ? params->len : 0;
    int has_variadic = 0;
    int non_variadic = total_params;
    if (params) {
        for (int i = 0; i < params->len; i++) {
            if (params->items[i].variadic) { has_variadic = 1; non_variadic = i; break; }
        }
    }
    int arity = has_variadic ? -(non_variadic + 1) : non_variadic;
    XSProto *parent = c->current->proto;
    XSProto *inner  = proto_new(name ? name : "<lambda>", arity);

    if (parent->n_inner == parent->cap_inner) {
        parent->cap_inner = parent->cap_inner ? parent->cap_inner * 2 : 4;
        parent->inner = xs_realloc(parent->inner,
                            (size_t)parent->cap_inner * sizeof(XSProto *));
    }
    int inner_idx = parent->n_inner;
    parent->inner[parent->n_inner++] = inner;

    CompilerScope fn_scope;
    scope_push(c, &fn_scope, inner);

    if (params) {
        for (int i = 0; i < total_params; i++) {
            const char *pname = params->items[i].name;
            if (params->items[i].variadic) {
                local_add(c->current, pname ? pname : "args");
            } else {
                local_add(c->current, pname ? pname : "<param>");
            }
        }
    }

    /* emit default value fill-ins for optional params */
    if (params) {
        for (int i = 0; i < total_params; i++) {
            if (params->items[i].default_val && !params->items[i].variadic) {
                int slot = i;
                emit_a(c, OP_LOAD_LOCAL, slot);
                emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
                emit(c, MAKE_A(OP_EQ, 0, 0));
                int skip = emit_jump(c, OP_JUMP_IF_FALSE);
                compile_node(c, params->items[i].default_val, 1);
                emit_a(c, OP_STORE_LOCAL, slot);
                patch_jump(c, skip);
            }
        }
    }

    compile_node(c, body, 1);

    XSChunk *ch = &inner->chunk;
    if (ch->len == 0 ||
        INSTR_OPCODE(ch->code[ch->len - 1]) != OP_RETURN)
        emit(c, MAKE_A(OP_RETURN, 0, 0));

    if (fn_scope.n_upvalues > 0) {
        inner->uv_descs = xs_malloc(
            (size_t)fn_scope.n_upvalues * sizeof(UVDesc));
        memcpy(inner->uv_descs, fn_scope.uv_descs,
               (size_t)fn_scope.n_upvalues * sizeof(UVDesc));
        inner->n_upvalues = fn_scope.n_upvalues;
    }

    scope_pop(c);
    return inner_idx;
}

static void emit_make_closure(Compiler *c, int inner_idx) {
    Value *v = xs_int((int64_t)inner_idx);
    int idx  = chunk_add_const(&c->current->proto->chunk, v);
    value_decref(v);
    emit_a(c, OP_MAKE_CLOSURE, idx);
}

static void compile_node(Compiler *c, Node *n, int want_value) {
    if (!n) {
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        return;
    }

    switch (n->tag) {

    case NODE_LIT_INT:
        emit_const(c, xs_int(n->lit_int.ival));
        break;

    case NODE_LIT_BIGINT: {
        XSBigInt *b = bigint_from_str(n->lit_bigint.bigint_str, 10);
        emit_const(c, xs_bigint_val(b));
        break;
    }

    case NODE_LIT_FLOAT:
        emit_const(c, xs_float(n->lit_float.fval));
        break;

    case NODE_LIT_STRING:
        emit_const(c, xs_str(n->lit_string.sval));
        break;

    case NODE_LIT_BOOL:
        emit(c, MAKE_A(n->lit_bool.bval ? OP_PUSH_TRUE : OP_PUSH_FALSE, 0, 0));
        break;

    case NODE_LIT_NULL:
        emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        break;

    case NODE_LIT_CHAR: {
        char s[2];
        s[0] = n->lit_char.cval;
        s[1] = '\0';
        emit_const(c, xs_str(s));
        break;
    }

    case NODE_LIT_ARRAY: {
        int cnt = n->lit_array.elems.len;
        int has_spread = 0;
        for (int i = 0; i < cnt; i++)
            if (n->lit_array.elems.items[i]->tag == NODE_SPREAD) { has_spread = 1; break; }
        if (has_spread) {
            emit(c, MAKE_B(OP_MAKE_ARRAY, 0, 0, 0));
            int arr_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, arr_slot);
            for (int i = 0; i < cnt; i++) {
                Node *elem = n->lit_array.elems.items[i];
                if (elem->tag == NODE_SPREAD) {
                    int sp_slot = local_add_hidden(c);
                    int sp_len  = local_add_hidden(c);
                    int sp_idx  = local_add_hidden(c);
                    compile_node(c, elem->spread.expr, 1);
                    emit_a(c, OP_STORE_LOCAL, sp_slot);
                    emit_a(c, OP_LOAD_LOCAL, sp_slot);
                    emit(c, MAKE_A(OP_ITER_LEN, 0, 0));
                    emit_a(c, OP_STORE_LOCAL, sp_len);
                    emit_const(c, xs_int(0));
                    emit_a(c, OP_STORE_LOCAL, sp_idx);
                    int loop_top = c->current->proto->chunk.len;
                    emit_a(c, OP_LOAD_LOCAL, sp_idx);
                    emit_a(c, OP_LOAD_LOCAL, sp_len);
                    emit(c, MAKE_A(OP_LT, 0, 0));
                    int j_exit = emit_jump(c, OP_JUMP_IF_FALSE);
                    emit_a(c, OP_LOAD_LOCAL, arr_slot);
                    emit_a(c, OP_LOAD_LOCAL, sp_slot);
                    emit_a(c, OP_LOAD_LOCAL, sp_idx);
                    emit(c, MAKE_A(OP_ITER_GET, 0, 0));
                    {
                        int pi = emit_global_name(c, "push");
                        emit(c, MAKE_A(OP_METHOD_CALL, 1, (uint16_t)(unsigned)pi));
                    }
                    emit(c, MAKE_A(OP_POP, 0, 0));
                    emit_a(c, OP_LOAD_LOCAL, sp_idx);
                    emit_const(c, xs_int(1));
                    emit(c, MAKE_A(OP_ADD, 0, 0));
                    emit_a(c, OP_STORE_LOCAL, sp_idx);
                    int back_off = loop_top - (c->current->proto->chunk.len + 1);
                    emit(c, MAKE_A(OP_JUMP, 0, (uint16_t)(int16_t)back_off));
                    patch_jump(c, j_exit);
                } else {
                    emit_a(c, OP_LOAD_LOCAL, arr_slot);
                    compile_node(c, elem, 1);
                    {
                        int pi = emit_global_name(c, "push");
                        emit(c, MAKE_A(OP_METHOD_CALL, 1, (uint16_t)(unsigned)pi));
                    }
                    emit(c, MAKE_A(OP_POP, 0, 0));
                }
            }
            emit_a(c, OP_LOAD_LOCAL, arr_slot);
        } else {
            for (int i = 0; i < cnt; i++)
                compile_node(c, n->lit_array.elems.items[i], 1);
            emit(c, MAKE_B(OP_MAKE_ARRAY, 0, 0, (uint8_t)(unsigned)cnt));
        }
        break;
    }

    case NODE_LIT_TUPLE: {
        int cnt = n->lit_array.elems.len;
        for (int i = 0; i < cnt; i++)
            compile_node(c, n->lit_array.elems.items[i], 1);
        emit(c, MAKE_B(OP_MAKE_TUPLE, 0, 0, (uint8_t)(unsigned)cnt));
        break;
    }

    case NODE_IDENT:
        compile_name_load(c, n->ident.name);
        break;

    case NODE_BINOP: {
        const char *op = n->binop.op;
        if (strcmp(op, "&&") == 0 || strcmp(op, "and") == 0) {
            compile_node(c, n->binop.left, 1);
            emit(c, MAKE_A(OP_DUP, 0, 0));
            int jf = emit_jump(c, OP_JUMP_IF_FALSE);
            emit(c, MAKE_A(OP_POP, 0, 0));
            compile_node(c, n->binop.right, 1);
            patch_jump(c, jf);
            break;
        }
        if (strcmp(op, "||") == 0 || strcmp(op, "or") == 0) {
            compile_node(c, n->binop.left, 1);
            emit(c, MAKE_A(OP_DUP, 0, 0));
            int jt = emit_jump(c, OP_JUMP_IF_TRUE);
            emit(c, MAKE_A(OP_POP, 0, 0));
            compile_node(c, n->binop.right, 1);
            patch_jump(c, jt);
            break;
        }
        if (strcmp(op, "??") == 0) {
            compile_node(c, n->binop.left, 1);
            emit(c, MAKE_A(OP_DUP, 0, 0));
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
            emit(c, MAKE_A(OP_NEQ, 0, 0));
            int jt = emit_jump(c, OP_JUMP_IF_TRUE);
            emit(c, MAKE_A(OP_POP, 0, 0));
            compile_node(c, n->binop.right, 1);
            patch_jump(c, jt);
            break;
        }
        if (strcmp(op, "|>") == 0) {
            compile_node(c, n->binop.right, 1);
            compile_node(c, n->binop.left, 1);
            emit(c, MAKE_B(OP_CALL, 0, 0, 1));
            break;
        }
        compile_node(c, n->binop.left,  1);
        compile_node(c, n->binop.right, 1);
        Opcode bop = OP_NOP;
        if      (strcmp(op, "+")  == 0) bop = OP_ADD;
        else if (strcmp(op, "-")  == 0) bop = OP_SUB;
        else if (strcmp(op, "*")  == 0) bop = OP_MUL;
        else if (strcmp(op, "/")  == 0) bop = OP_DIV;
        else if (strcmp(op, "%")  == 0) bop = OP_MOD;
        else if (strcmp(op, "**") == 0) bop = OP_POW;
        else if (strcmp(op, "++") == 0) bop = OP_CONCAT;
        else if (strcmp(op, "==") == 0) bop = OP_EQ;
        else if (strcmp(op, "!=") == 0) bop = OP_NEQ;
        else if (strcmp(op, "<")  == 0) bop = OP_LT;
        else if (strcmp(op, ">")  == 0) bop = OP_GT;
        else if (strcmp(op, "<=") == 0) bop = OP_LTE;
        else if (strcmp(op, ">=") == 0) bop = OP_GTE;
        else if (strcmp(op, "&")  == 0) bop = OP_BAND;
        else if (strcmp(op, "|")  == 0) bop = OP_BOR;
        else if (strcmp(op, "^")  == 0) bop = OP_BXOR;
        else if (strcmp(op, "<<") == 0) bop = OP_SHL;
        else if (strcmp(op, ">>") == 0) bop = OP_SHR;
        else if (strcmp(op, "//") == 0) bop = OP_FLOOR_DIV;
        else if (strcmp(op, "<=>") == 0) bop = OP_SPACESHIP;
        else if (strcmp(op, "in") == 0) bop = OP_IN;
        else if (strcmp(op, "is") == 0) bop = OP_IS;
        else if (strcmp(op, "not in") == 0) { emit(c, MAKE_A(OP_IN, 0, 0)); emit(c, MAKE_A(OP_NOT, 0, 0)); break; }
        else {
            fprintf(stderr, "unknown binop '%s'\n", op);
            emit(c, MAKE_A(OP_POP, 0, 0));
            if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
            return;
        }
        emit(c, MAKE_A(bop, 0, 0));
        break;
    }

    case NODE_UNARY:
        compile_node(c, n->unary.expr, 1);
        if (strcmp(n->unary.op, "-") == 0)
            emit(c, MAKE_A(OP_NEG, 0, 0));
        else if (strcmp(n->unary.op, "~") == 0)
            emit(c, MAKE_A(OP_BNOT, 0, 0));
        else
            emit(c, MAKE_A(OP_NOT, 0, 0));
        break;

    case NODE_ASSIGN: {
        Node *tgt = n->assign.target;
        Opcode compound_op = OP_NOP;
        const char *aop = n->assign.op;
        if (aop[0] != '=' || aop[1] != '\0') {
            if      (strcmp(aop, "+=")  == 0) compound_op = OP_ADD;
            else if (strcmp(aop, "-=")  == 0) compound_op = OP_SUB;
            else if (strcmp(aop, "*=")  == 0) compound_op = OP_MUL;
            else if (strcmp(aop, "/=")  == 0) compound_op = OP_DIV;
            else if (strcmp(aop, "%=")  == 0) compound_op = OP_MOD;
            else if (strcmp(aop, "**=") == 0) compound_op = OP_POW;
            else if (strcmp(aop, "&=")  == 0) compound_op = OP_BAND;
            else if (strcmp(aop, "|=")  == 0) compound_op = OP_BOR;
            else if (strcmp(aop, "^=")  == 0) compound_op = OP_BXOR;
            else if (strcmp(aop, "<<=") == 0) compound_op = OP_SHL;
            else if (strcmp(aop, ">>=") == 0) compound_op = OP_SHR;
            else if (strcmp(aop, "++=") == 0) compound_op = OP_CONCAT;
            else if (strcmp(aop, "//=") == 0) compound_op = OP_FLOOR_DIV;
        }

        if (tgt->tag == NODE_IDENT) {
            if (compound_op != OP_NOP) {
                compile_name_load(c, tgt->ident.name);
                compile_node(c, n->assign.value, 1);
                emit(c, MAKE_A(compound_op, 0, 0));
            } else {
                compile_node(c, n->assign.value, 1);
            }
            if (want_value) emit(c, MAKE_A(OP_DUP, 0, 0));
            compile_name_store(c, tgt->ident.name);
        } else if (tgt->tag == NODE_FIELD) {
            compile_node(c, tgt->field.obj, 1);
            if (compound_op != OP_NOP) {
                emit(c, MAKE_A(OP_DUP, 0, 0));
                int fi = emit_global_name(c, tgt->field.name);
                emit_a(c, OP_LOAD_FIELD, fi);
                compile_node(c, n->assign.value, 1);
                emit(c, MAKE_A(compound_op, 0, 0));
            } else {
                compile_node(c, n->assign.value, 1);
            }
            if (want_value) {
                /* stack: obj val → store val to field, return val */
                int tmp = local_add_hidden(c);
                emit(c, MAKE_A(OP_DUP, 0, 0));     /* obj val val */
                emit_a(c, OP_STORE_LOCAL, tmp);      /* obj val */
                int ni = emit_global_name(c, tgt->field.name);
                emit_a(c, OP_STORE_FIELD, ni);       /* (empty) */
                emit_a(c, OP_LOAD_LOCAL, tmp);       /* val */
            } else {
                int ni = emit_global_name(c, tgt->field.name);
                emit_a(c, OP_STORE_FIELD, ni);
            }
        } else if (tgt->tag == NODE_INDEX) {
            compile_node(c, tgt->index.obj,   1);
            compile_node(c, tgt->index.index, 1);
            if (compound_op != OP_NOP) {
                    emit(c, MAKE_A(OP_DUP, 0, 0));
                int idx_tmp = local_add_hidden(c);
                int col_tmp = local_add_hidden(c);
                emit(c, MAKE_A(OP_POP, 0, 0));
                emit_a(c, OP_STORE_LOCAL, idx_tmp);
                emit_a(c, OP_STORE_LOCAL, col_tmp);
                emit_a(c, OP_LOAD_LOCAL, col_tmp);
                emit_a(c, OP_LOAD_LOCAL, idx_tmp);
                emit(c, MAKE_A(OP_INDEX_GET, 0, 0));
                compile_node(c, n->assign.value, 1);
                emit(c, MAKE_A(compound_op, 0, 0));
                int new_tmp = local_add_hidden(c);
                emit_a(c, OP_STORE_LOCAL, new_tmp);
                emit_a(c, OP_LOAD_LOCAL, col_tmp);
                emit_a(c, OP_LOAD_LOCAL, idx_tmp);
                emit_a(c, OP_LOAD_LOCAL, new_tmp);
                emit(c, MAKE_A(OP_INDEX_SET, 0, 0));
                if (want_value) emit_a(c, OP_LOAD_LOCAL, new_tmp);
            } else {
                compile_node(c, n->assign.value,  1);
                if (want_value) {
                    int tmp = local_add_hidden(c);
                    emit_a(c, OP_STORE_LOCAL, tmp);
                    emit(c, MAKE_A(OP_INDEX_SET, 0, 0));
                    emit_a(c, OP_LOAD_LOCAL, tmp);
                } else {
                    emit(c, MAKE_A(OP_INDEX_SET, 0, 0));
                }
            }
        } else {
            compile_node(c, n->assign.value, 0);
            if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        }
        return;
    }

    case NODE_LET:
    case NODE_VAR: {
        if (n->let.value)
            compile_node(c, n->let.value, 1);
        else
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        Node *pat = n->let.pattern;
        if (pat && (pat->tag == NODE_PAT_TUPLE || pat->tag == NODE_PAT_SLICE)) {
            NodeList *elems = (pat->tag == NODE_PAT_TUPLE)
                ? &pat->pat_tuple.elems : &pat->pat_slice.elems;
            int val_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, val_slot);
            for (int di = 0; di < elems->len; di++) {
                Node *sub = elems->items[di];
                emit_a(c, OP_LOAD_LOCAL, val_slot);
                emit_const(c, xs_int(di));
                emit(c, MAKE_A(OP_INDEX_GET, 0, 0));
                if (sub->tag == NODE_PAT_IDENT && sub->pat_ident.name) {
                    int ds = local_add(c->current, sub->pat_ident.name);
                    emit_a(c, OP_STORE_LOCAL, ds);
                } else if (sub->tag == NODE_PAT_WILD) {
                    emit(c, MAKE_A(OP_POP, 0, 0));
                } else {
                    emit(c, MAKE_A(OP_POP, 0, 0));
                }
            }
            if (pat->tag == NODE_PAT_SLICE && pat->pat_slice.rest) {
                emit_a(c, OP_LOAD_LOCAL, val_slot);
                emit_const(c, xs_int(elems->len));
                int rest_ni = emit_global_name(c, "slice");
                emit(c, MAKE_A(OP_METHOD_CALL, 1, (uint16_t)(unsigned)rest_ni));
                int rs = local_add(c->current, pat->pat_slice.rest);
                emit_a(c, OP_STORE_LOCAL, rs);
            }
            if (want_value) emit_a(c, OP_LOAD_LOCAL, val_slot);
        } else if (pat && pat->tag == NODE_PAT_STRUCT) {
            int val_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, val_slot);
            for (int di = 0; di < pat->pat_struct.fields.len; di++) {
                const char *key = pat->pat_struct.fields.items[di].key;
                Node *fpat = pat->pat_struct.fields.items[di].val;
                emit_a(c, OP_LOAD_LOCAL, val_slot);
                {
                    int fi_idx = emit_global_name(c, key);
                    emit_a(c, OP_LOAD_FIELD, fi_idx);
                }
                const char *bind_name = key;
                if (fpat && fpat->tag == NODE_PAT_IDENT && fpat->pat_ident.name)
                    bind_name = fpat->pat_ident.name;
                int ds = local_add(c->current, bind_name);
                emit_a(c, OP_STORE_LOCAL, ds);
            }
            if (want_value) emit_a(c, OP_LOAD_LOCAL, val_slot);
        } else {
            if (want_value) emit(c, MAKE_A(OP_DUP, 0, 0));
            int slot = local_add(c->current, n->let.name ? n->let.name : "<anon>");
            emit_a(c, OP_STORE_LOCAL, slot);
        }
        return;
    }

    case NODE_CONST: {
        if (n->const_.value)
            compile_node(c, n->const_.value, 1);
        else
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        if (want_value) emit(c, MAKE_A(OP_DUP, 0, 0));
        int slot = local_add(c->current, n->const_.name);
        emit_a(c, OP_STORE_LOCAL, slot);
        return;
    }

    case NODE_BLOCK: {
        scope_begin(c);
        for (int i = 0; i < n->block.stmts.len; i++)
            compile_node(c, n->block.stmts.items[i], 0);
        if (n->block.expr)
            compile_node(c, n->block.expr, want_value);
        else if (want_value)
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        scope_end(c);
        return;
    }

    case NODE_IF: {
        int n_elif = n->if_expr.elif_conds.len;
        int end_jumps[256];
        int n_end_jumps = 0;

        compile_node(c, n->if_expr.cond, 1);
        int jf = emit_jump(c, OP_JUMP_IF_FALSE);
        compile_node(c, n->if_expr.then, want_value);
        end_jumps[n_end_jumps++] = emit_jump(c, OP_JUMP);
        patch_jump(c, jf);

        for (int i = 0; i < n_elif; i++) {
            compile_node(c, n->if_expr.elif_conds.items[i], 1);
            int jf2 = emit_jump(c, OP_JUMP_IF_FALSE);
            compile_node(c, n->if_expr.elif_thens.items[i], want_value);
            end_jumps[n_end_jumps++] = emit_jump(c, OP_JUMP);
            patch_jump(c, jf2);
        }

        if (n->if_expr.else_branch)
            compile_node(c, n->if_expr.else_branch, want_value);
        else if (want_value)
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));

        for (int i = 0; i < n_end_jumps; i++)
            patch_jump(c, end_jumps[i]);
        return;
    }

    case NODE_WHILE: {
        int loop_start = c->current->proto->chunk.len;
        loop_push_label(c, loop_start, n->while_loop.label);
        compile_node(c, n->while_loop.cond, 1);
        int j_exit = emit_jump(c, OP_JUMP_IF_FALSE);
        compile_node(c, n->while_loop.body, 0);
        int back_off = loop_start - (c->current->proto->chunk.len + 1);
        emit(c, MAKE_A(OP_JUMP, 0, (uint16_t)(int16_t)back_off));
        patch_jump(c, j_exit);
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        loop_pop_patch_breaks(c);
        return;
    }

    case NODE_RETURN: {
        if (n->ret.value)
            compile_node(c, n->ret.value, 1);
        else
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        /* actor method: write back state fields before returning */
        if (c->current->actor_state_names && c->current->actor_nstate > 0) {
            for (int si = 0; si < c->current->actor_nstate; si++) {
                int slot = local_resolve(c->current, c->current->actor_state_names[si]);
                if (slot >= 0) {
                    emit_a(c, OP_LOAD_LOCAL, 0); /* self */
                    emit_a(c, OP_LOAD_LOCAL, slot);
                    int fi = emit_global_name(c, c->current->actor_state_names[si]);
                    emit_a(c, OP_STORE_FIELD, fi);
                }
            }
        }
        emit(c, MAKE_A(OP_RETURN, 0, 0));
        return;
    }

    case NODE_EXPR_STMT:
        compile_node(c, n->expr_stmt.expr, 0);
        return;

    case NODE_CALL: {
        compile_node(c, n->call.callee, 1);
        int argc = n->call.args.len;
        for (int i = 0; i < argc; i++)
            compile_node(c, n->call.args.items[i], 1);
        emit(c, MAKE_B(OP_CALL, 0, 0, (uint8_t)(unsigned)argc));
        if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
        return;
    }

    case NODE_FN_DECL: {
        int idx = compile_fn(c, n->fn_decl.name,
                             &n->fn_decl.params,
                             n->fn_decl.body);
        /* Mark generator functions so VM knows to collect yields */
        if (n->fn_decl.is_generator) {
            XSProto *inner = c->current->proto->inner[idx];
            /* Use arity's sign bit as generator flag: encode arity as negative */
            inner->arity = -(inner->arity + 1); /* -1=gen(0 args), -2=gen(1 arg), etc. */
        }
        emit_make_closure(c, idx);
        if (want_value) emit(c, MAKE_A(OP_DUP, 0, 0));
        compile_name_store(c, n->fn_decl.name);
        return;
    }

    case NODE_LAMBDA: {
        int idx = compile_fn(c, NULL,
                             &n->lambda.params,
                             n->lambda.body);
        if (n->lambda.is_generator) {
            XSProto *inner = c->current->proto->inner[idx];
            inner->arity = -(inner->arity + 1);
        }
        emit_make_closure(c, idx);
        if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
        return;
    }

    case NODE_PROGRAM:
        for (int i = 0; i < n->program.stmts.len; i++)
            compile_node(c, n->program.stmts.items[i], 0);
        return;

    case NODE_LIT_MAP: {
        int cnt = n->lit_map.keys.len;
        int has_spread = 0;
        for (int i = 0; i < cnt; i++)
            if (n->lit_map.keys.items[i] && n->lit_map.keys.items[i]->tag == NODE_SPREAD) { has_spread = 1; break; }
        if (has_spread) {
            emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, 0));
            int map_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, map_slot);
            for (int i = 0; i < cnt; i++) {
                Node *key = n->lit_map.keys.items[i];
                if (key && key->tag == NODE_SPREAD) {
                    emit_a(c, OP_LOAD_LOCAL, map_slot);
                    compile_node(c, key->spread.expr, 1);
                    emit(c, MAKE_A(OP_MAP_MERGE, 0, 0));
                    emit(c, MAKE_A(OP_POP, 0, 0));
                } else {
                    emit_a(c, OP_LOAD_LOCAL, map_slot);
                    compile_node(c, key, 1);
                    compile_node(c, n->lit_map.vals.items[i], 1);
                    emit(c, MAKE_A(OP_INDEX_SET, 0, 0));
                }
            }
            emit_a(c, OP_LOAD_LOCAL, map_slot);
        } else {
            for (int i = 0; i < cnt; i++) {
                compile_node(c, n->lit_map.keys.items[i], 1);
                compile_node(c, n->lit_map.vals.items[i], 1);
            }
            emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, (uint8_t)(unsigned)cnt));
        }
        break;
    }

    case NODE_INDEX:
        compile_node(c, n->index.obj,   1);
        compile_node(c, n->index.index, 1);
        emit(c, MAKE_A(OP_INDEX_GET, 0, 0));
        break;

    case NODE_FIELD: {
        compile_node(c, n->field.obj, 1);
        if (n->field.optional) {
            emit(c, MAKE_A(OP_DUP, 0, 0));
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
            emit(c, MAKE_A(OP_EQ, 0, 0));
            int skip = emit_jump(c, OP_JUMP_IF_TRUE);
            int ni = emit_global_name(c, n->field.name);
            emit_a(c, OP_LOAD_FIELD, ni);
            int end = emit_jump(c, OP_JUMP);
            patch_jump(c, skip);
            emit(c, MAKE_A(OP_POP, 0, 0));
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
            patch_jump(c, end);
        } else {
            int ni = emit_global_name(c, n->field.name);
            emit_a(c, OP_LOAD_FIELD, ni);
        }
        break;
    }

    case NODE_SCOPE: {
        if (n->scope.nparts == 0) {
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
            break;
        }
        compile_name_load(c, n->scope.parts[0]);
        for (int i = 1; i < n->scope.nparts; i++) {
            int ni = emit_global_name(c, n->scope.parts[i]);
            emit_a(c, OP_LOAD_FIELD, ni);
        }
        break;
    }

    case NODE_RANGE:
        compile_node(c, n->range.start, 1);
        compile_node(c, n->range.end,   1);
        emit(c, MAKE_A(OP_MAKE_RANGE, n->range.inclusive, 0));
        break;

    case NODE_INTERP_STRING: {
        int cnt = n->lit_string.parts.len;
        if (cnt == 0) {
            emit_const(c, xs_str(""));
            break;
        }
        for (int i = 0; i < cnt; i++) {
            Node *part = n->lit_string.parts.items[i];
            if (part->tag == NODE_LIT_STRING) {
                emit_const(c, xs_str(part->lit_string.sval ? part->lit_string.sval : ""));
            } else {
                emit_a(c, OP_LOAD_GLOBAL, emit_global_name(c, "str"));
                compile_node(c, part, 1);
                emit(c, MAKE_B(OP_CALL, 0, 0, 1));
            }
        }
        for (int i = 1; i < cnt; i++)
            emit(c, MAKE_A(OP_CONCAT, 0, 0));
        break;
    }

    case NODE_METHOD_CALL: {
        compile_node(c, n->method_call.obj, 1);
        if (n->method_call.optional) {
            emit(c, MAKE_A(OP_DUP, 0, 0));
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
            emit(c, MAKE_A(OP_EQ, 0, 0));
            int skip = emit_jump(c, OP_JUMP_IF_TRUE);
            int argc = n->method_call.args.len;
            for (int i = 0; i < argc; i++)
                compile_node(c, n->method_call.args.items[i], 1);
            int ni = emit_global_name(c, n->method_call.method);
            emit(c, MAKE_A(OP_METHOD_CALL, (uint8_t)(unsigned)argc, (uint16_t)(unsigned)ni));
            int end = emit_jump(c, OP_JUMP);
            patch_jump(c, skip);
            emit(c, MAKE_A(OP_POP, 0, 0));
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
            patch_jump(c, end);
        } else {
            int argc = n->method_call.args.len;
            for (int i = 0; i < argc; i++)
                compile_node(c, n->method_call.args.items[i], 1);
            int ni = emit_global_name(c, n->method_call.method);
            emit(c, MAKE_A(OP_METHOD_CALL, (uint8_t)(unsigned)argc, (uint16_t)(unsigned)ni));
        }
        if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
        return;
    }

    case NODE_STRUCT_INIT: {
        int cnt = n->struct_init.fields.len;
        if (n->struct_init.rest) {
            /* has spread: start with spread base, then override fields */
            emit_const(c, xs_str("__type"));
            emit_const(c, xs_str(n->struct_init.path ? n->struct_init.path : "struct"));
            emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, 1));
            int map_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, map_slot);
            /* merge spread source */
            emit_a(c, OP_LOAD_LOCAL, map_slot);
            compile_node(c, n->struct_init.rest, 1);
            emit(c, MAKE_A(OP_MAP_MERGE, 0, 0));
            emit(c, MAKE_A(OP_POP, 0, 0));
            /* override with explicit fields */
            for (int i = 0; i < cnt; i++) {
                const char *k = n->struct_init.fields.items[i].key;
                Node *val = n->struct_init.fields.items[i].val;
                if (k) {
                    emit_a(c, OP_LOAD_LOCAL, map_slot);
                    compile_node(c, val, 1);
                    int fi = emit_global_name(c, k);
                    emit_a(c, OP_STORE_FIELD, fi);
                }
            }
            emit_a(c, OP_LOAD_LOCAL, map_slot);
        } else {
            emit_const(c, xs_str("__type"));
            emit_const(c, xs_str(n->struct_init.path ? n->struct_init.path : "struct"));
            for (int i = 0; i < cnt; i++) {
                const char *k = n->struct_init.fields.items[i].key;
                Node *val     = n->struct_init.fields.items[i].val;
                emit_const(c, xs_str(k ? k : "?"));
                compile_node(c, val, 1);
            }
            emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, (uint8_t)(unsigned)(cnt + 1)));
        }
        break;
    }

    case NODE_FOR: {
        int iter_slot = local_add_hidden(c);
        int len_slot  = local_add_hidden(c);
        int idx_slot  = local_add_hidden(c);

        compile_node(c, n->for_loop.iter, 1);
        emit_a(c, OP_STORE_LOCAL, iter_slot);

        emit_a(c, OP_LOAD_LOCAL, iter_slot);
        emit(c, MAKE_A(OP_ITER_LEN, 0, 0));
        emit_a(c, OP_STORE_LOCAL, len_slot);

        emit_const(c, xs_int(0));
        emit_a(c, OP_STORE_LOCAL, idx_slot);

        int loop_top = c->current->proto->chunk.len;
        loop_push_label(c, 0, n->for_loop.label); /* continue_target patched below */

        emit_a(c, OP_LOAD_LOCAL, idx_slot);
        emit_a(c, OP_LOAD_LOCAL, len_slot);
        emit(c, MAKE_A(OP_LT, 0, 0));
        int j_exit = emit_jump(c, OP_JUMP_IF_FALSE);

        emit_a(c, OP_LOAD_LOCAL, iter_slot);
        emit_a(c, OP_LOAD_LOCAL, idx_slot);
        emit(c, MAKE_A(OP_ITER_GET, 0, 0));
        Node *pat = n->for_loop.pattern;
        const char *pat_name = NULL;
        if (pat) {
            if (pat->tag == NODE_IDENT)     pat_name = pat->ident.name;
            if (pat->tag == NODE_PAT_IDENT) pat_name = pat->pat_ident.name;
        }
        if (pat_name) {
            int vs = local_add(c->current, pat_name);
            emit_a(c, OP_STORE_LOCAL, vs);
        } else if (pat && (pat->tag == NODE_PAT_TUPLE || pat->tag == NODE_PAT_SLICE)) {
            NodeList *elems = (pat->tag == NODE_PAT_TUPLE)
                ? &pat->pat_tuple.elems : &pat->pat_slice.elems;
            int elem_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, elem_slot);
            for (int di = 0; di < elems->len; di++) {
                Node *sub = elems->items[di];
                emit_a(c, OP_LOAD_LOCAL, elem_slot);
                emit_const(c, xs_int(di));
                emit(c, MAKE_A(OP_INDEX_GET, 0, 0));
                if (sub->tag == NODE_PAT_IDENT && sub->pat_ident.name) {
                    int ds = local_add(c->current, sub->pat_ident.name);
                    emit_a(c, OP_STORE_LOCAL, ds);
                } else if (sub->tag == NODE_PAT_WILD) {
                    emit(c, MAKE_A(OP_POP, 0, 0));
                } else {
                    emit(c, MAKE_A(OP_POP, 0, 0));
                }
            }
        } else {
            emit(c, MAKE_A(OP_POP, 0, 0));
        }

        compile_node(c, n->for_loop.body, 0);

        /* patch continue target to here (the increment) */
        {
            LoopCtx *lc = &c->loop_stack[c->loop_depth - 1];
            int cont_ip = c->current->proto->chunk.len;
            lc->continue_target = cont_ip;
            /* patch deferred continue jumps */
            for (int ci = 0; ci < lc->n_continue_patches; ci++) {
                int pidx = lc->continue_patches[ci];
                int offset = cont_ip - pidx - 1;
                Instruction *ip2 = &c->current->proto->chunk.code[pidx];
                *ip2 = (*ip2 & 0x0000FFFFU) | ((Instruction)(uint16_t)(int16_t)offset << 16);
            }
        }

        emit_a(c, OP_LOAD_LOCAL, idx_slot);
        emit_const(c, xs_int(1));
        emit(c, MAKE_A(OP_ADD, 0, 0));
        emit_a(c, OP_STORE_LOCAL, idx_slot);

        int back_off = loop_top - (c->current->proto->chunk.len + 1);
        emit(c, MAKE_A(OP_JUMP, 0, (uint16_t)(int16_t)back_off));

        patch_jump(c, j_exit);
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        loop_pop_patch_breaks(c);
        return;
    }

    case NODE_LOOP: {
        int loop_top = c->current->proto->chunk.len;
        loop_push_label(c, loop_top, n->loop.label);
        compile_node(c, n->loop.body, 0);
        int back_off = loop_top - (c->current->proto->chunk.len + 1);
        emit(c, MAKE_A(OP_JUMP, 0, (uint16_t)(int16_t)back_off));
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        loop_pop_patch_breaks(c);
        return;
    }

    case NODE_BREAK: {
        if (n->brk.value) {
            compile_node(c, n->brk.value, 1);
        }
        int idx = emit_jump(c, OP_JUMP);
        if (n->brk.label && c->loop_depth > 0) {
            int found_label = 0;
            for (int li = c->loop_depth - 1; li >= 0; li--) {
                if (c->loop_stack[li].label[0] && strcmp(c->loop_stack[li].label, n->brk.label) == 0) {
                    if (c->loop_stack[li].n_break_patches < MAX_BREAK_PATCHES)
                        c->loop_stack[li].break_patches[c->loop_stack[li].n_break_patches++] = idx;
                    found_label = 1;
                    break;
                }
            }
            if (!found_label) loop_add_break(c, idx);
        } else {
            loop_add_break(c, idx);
        }
        return;
    }
    case NODE_CONTINUE: {
        int target_depth = c->loop_depth - 1;
        if (n->cont.label) {
            for (int li = c->loop_depth - 1; li >= 0; li--) {
                if (c->loop_stack[li].label[0] && strcmp(c->loop_stack[li].label, n->cont.label) == 0) {
                    target_depth = li; break;
                }
            }
        }
        if (target_depth >= 0) {
            int top = c->loop_stack[target_depth].continue_target;
            if (top == 0) {
                /* continue target not yet known (for loop), defer via continue_patches */
                int idx = emit_jump(c, OP_JUMP);
                LoopCtx *lc = &c->loop_stack[target_depth];
                if (lc->n_continue_patches < MAX_BREAK_PATCHES)
                    lc->continue_patches[lc->n_continue_patches++] = idx;
            } else {
                int off = top - (c->current->proto->chunk.len + 1);
                emit(c, MAKE_A(OP_JUMP, 0, (uint16_t)(int16_t)off));
            }
        }
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        return;
    }

    // --- match
    case NODE_MATCH: {
        int subj_slot = local_add_hidden(c);
        compile_node(c, n->match.subject, 1);
        emit_a(c, OP_STORE_LOCAL, subj_slot);

        int n_arms = n->match.arms.len;
        int *arm_jumps = n_arms > 0 ? xs_malloc((size_t)n_arms * sizeof(int)) : NULL;
        int n_arm_jumps = 0;

        for (int ai = 0; ai < n_arms; ai++) {
            MatchArm *arm = &n->match.arms.items[ai];
            Node *pat = arm->pattern;

            int j_next = -1;
            int tuple_jumps[16];
            int n_tuple_jumps = 0;

            if (!pat || pat->tag == NODE_PAT_WILD ||
                (pat->tag == NODE_PAT_IDENT && !arm->guard)) {
                /* wildcard — always matches */
            } else if (pat->tag == NODE_PAT_LIT) {
                emit_a(c, OP_LOAD_LOCAL, subj_slot);
                switch (pat->pat_lit.tag) {
                case 0: emit_const(c, xs_int(pat->pat_lit.ival));   break;
                case 1: emit_const(c, xs_float(pat->pat_lit.fval)); break;
                case 2: emit_const(c, xs_str(pat->pat_lit.sval));   break;
                case 3: emit(c, MAKE_A(pat->pat_lit.bval ? OP_PUSH_TRUE : OP_PUSH_FALSE, 0, 0)); break;
                default: emit(c, MAKE_A(OP_PUSH_NULL, 0, 0)); break;
                }
                emit(c, MAKE_A(OP_EQ, 0, 0));
                j_next = emit_jump(c, OP_JUMP_IF_FALSE);
            } else if (pat->tag == NODE_PAT_RANGE || pat->tag == NODE_RANGE) {
                Node *start = (pat->tag == NODE_PAT_RANGE)
                    ? pat->pat_range.start : pat->range.start;
                Node *end_n = (pat->tag == NODE_PAT_RANGE)
                    ? pat->pat_range.end   : pat->range.end;
                int incl = (pat->tag == NODE_PAT_RANGE)
                    ? pat->pat_range.inclusive : pat->range.inclusive;
                compile_node(c, start, 1);
                emit_a(c, OP_LOAD_LOCAL, subj_slot);
                emit(c, MAKE_A(OP_LTE, 0, 0));
                int jf_lo = emit_jump(c, OP_JUMP_IF_FALSE);
                emit_a(c, OP_LOAD_LOCAL, subj_slot);
                compile_node(c, end_n, 1);
                emit(c, incl ? MAKE_A(OP_LTE, 0, 0) : MAKE_A(OP_LT, 0, 0));
                int jf_hi = emit_jump(c, OP_JUMP_IF_FALSE);
                if (arm->guard) {
                    compile_node(c, arm->guard, 1);
                    int jf_guard = emit_jump(c, OP_JUMP_IF_FALSE);
                    compile_node(c, arm->body, want_value);
                    arm_jumps[n_arm_jumps++] = emit_jump(c, OP_JUMP);
                    patch_jump(c, jf_lo);
                    patch_jump(c, jf_hi);
                    patch_jump(c, jf_guard);
                } else {
                    compile_node(c, arm->body, want_value);
                    arm_jumps[n_arm_jumps++] = emit_jump(c, OP_JUMP);
                    patch_jump(c, jf_lo);
                    patch_jump(c, jf_hi);
                }
                continue; /* handled */
            } else if (pat->tag == NODE_PAT_CAPTURE) {
                Node *sub = pat->pat_capture.pattern;
                if (sub && sub->tag == NODE_PAT_RANGE) {
                    Node *start = sub->range.start;
                    Node *end_n = sub->range.end;
                    int incl = sub->range.inclusive;
                    compile_node(c, start, 1);
                    emit_a(c, OP_LOAD_LOCAL, subj_slot);
                    emit(c, MAKE_A(OP_LTE, 0, 0));
                    int jf_start = emit_jump(c, OP_JUMP_IF_FALSE);
                    emit_a(c, OP_LOAD_LOCAL, subj_slot);
                    compile_node(c, end_n, 1);
                    emit(c, incl ? MAKE_A(OP_LTE, 0, 0) : MAKE_A(OP_LT, 0, 0));
                    int jf_end = emit_jump(c, OP_JUMP_IF_FALSE);
                    if (pat->pat_capture.name) {
                        int bs = local_add(c->current, pat->pat_capture.name);
                        emit_a(c, OP_LOAD_LOCAL, subj_slot);
                        emit_a(c, OP_STORE_LOCAL, bs);
                    }
                    if (arm->guard) {
                        compile_node(c, arm->guard, 1);
                        j_next = emit_jump(c, OP_JUMP_IF_FALSE);
                    }
                    compile_node(c, arm->body, want_value);
                    arm_jumps[n_arm_jumps++] = emit_jump(c, OP_JUMP);
                    patch_jump(c, jf_start);
                    patch_jump(c, jf_end);
                    if (j_next >= 0) patch_jump(c, j_next);
                    continue; /* handled */
                } else if (!sub || sub->tag == NODE_PAT_WILD) {
                    if (pat->pat_capture.name) {
                        int bs = local_add(c->current, pat->pat_capture.name);
                        emit_a(c, OP_LOAD_LOCAL, subj_slot);
                        emit_a(c, OP_STORE_LOCAL, bs);
                    }
                } else {
                    /* unsupported sub-pattern */
                }
            } else if (pat->tag == NODE_PAT_IDENT) {
                int bs = local_add(c->current, pat->pat_ident.name);
                emit_a(c, OP_LOAD_LOCAL, subj_slot);
                emit_a(c, OP_STORE_LOCAL, bs);
            } else if (pat->tag == NODE_PAT_TUPLE) {
                for (int ti = 0; ti < pat->pat_tuple.elems.len; ti++) {
                    Node *sub_pat = pat->pat_tuple.elems.items[ti];
                    emit_a(c, OP_LOAD_LOCAL, subj_slot);
                    emit_const(c, xs_int(ti));
                    emit(c, MAKE_A(OP_INDEX_GET, 0, 0));
                    if (sub_pat->tag == NODE_PAT_LIT) {
                        switch (sub_pat->pat_lit.tag) {
                        case 0: emit_const(c, xs_int(sub_pat->pat_lit.ival)); break;
                        case 1: emit_const(c, xs_float(sub_pat->pat_lit.fval)); break;
                        case 2: emit_const(c, xs_str(sub_pat->pat_lit.sval)); break;
                        case 3: emit(c, MAKE_A(sub_pat->pat_lit.bval ? OP_PUSH_TRUE : OP_PUSH_FALSE, 0, 0)); break;
                        default: emit(c, MAKE_A(OP_PUSH_NULL, 0, 0)); break;
                        }
                        emit(c, MAKE_A(OP_EQ, 0, 0));
                        if (n_tuple_jumps < 16)
                            tuple_jumps[n_tuple_jumps++] = emit_jump(c, OP_JUMP_IF_FALSE);
                    } else if (sub_pat->tag == NODE_PAT_IDENT) {
                        int slot = local_add(c->current, sub_pat->pat_ident.name);
                        emit_a(c, OP_STORE_LOCAL, slot);
                    } else if (sub_pat->tag == NODE_PAT_WILD) {
                        emit(c, MAKE_A(OP_POP, 0, 0));
                    } else {
                        emit(c, MAKE_A(OP_POP, 0, 0));
                    }
                }
            } else if (pat->tag == NODE_PAT_ENUM) {
                emit_a(c, OP_LOAD_LOCAL, subj_slot);
                {
                    int tag_idx = emit_global_name(c, "_tag");
                    emit_a(c, OP_LOAD_FIELD, tag_idx);
                }
                {
                    const char *epath = pat->pat_enum.path;
                    const char *last_colon = strrchr(epath, ':');
                    const char *variant = (last_colon && last_colon > epath) ? last_colon + 1 : epath;
                    emit_const(c, xs_str(variant));
                }
                emit(c, MAKE_A(OP_EQ, 0, 0));
                j_next = emit_jump(c, OP_JUMP_IF_FALSE);
                for (int eai = 0; eai < pat->pat_enum.args.len; eai++) {
                    Node *arg_pat = pat->pat_enum.args.items[eai];
                    if (arg_pat->tag == NODE_PAT_IDENT) {
                        emit_a(c, OP_LOAD_LOCAL, subj_slot);
                        {
                            int val_idx = emit_global_name(c, "_val");
                            emit_a(c, OP_LOAD_FIELD, val_idx);
                        }
                        if (pat->pat_enum.args.len > 1) {
                            emit_const(c, xs_int(eai));
                            emit(c, MAKE_A(OP_INDEX_GET, 0, 0));
                        }
                        int slot = local_add(c->current, arg_pat->pat_ident.name);
                        emit_a(c, OP_STORE_LOCAL, slot);
                    }
                }
            } else if (pat->tag == NODE_PAT_OR) {
                Node *left  = pat->pat_or.left;
                Node *right = pat->pat_or.right;

                if (left && left->tag == NODE_PAT_LIT) {
                    emit_a(c, OP_LOAD_LOCAL, subj_slot);
                    switch (left->pat_lit.tag) {
                    case 0: emit_const(c, xs_int(left->pat_lit.ival));   break;
                    case 1: emit_const(c, xs_float(left->pat_lit.fval)); break;
                    case 2: emit_const(c, xs_str(left->pat_lit.sval));   break;
                    case 3: emit(c, MAKE_A(left->pat_lit.bval ? OP_PUSH_TRUE : OP_PUSH_FALSE, 0, 0)); break;
                    default: emit(c, MAKE_A(OP_PUSH_NULL, 0, 0)); break;
                    }
                    emit(c, MAKE_A(OP_EQ, 0, 0));
                    int j_left_ok = emit_jump(c, OP_JUMP_IF_TRUE);

                    if (right && right->tag == NODE_PAT_LIT) {
                        emit_a(c, OP_LOAD_LOCAL, subj_slot);
                        switch (right->pat_lit.tag) {
                        case 0: emit_const(c, xs_int(right->pat_lit.ival));   break;
                        case 1: emit_const(c, xs_float(right->pat_lit.fval)); break;
                        case 2: emit_const(c, xs_str(right->pat_lit.sval));   break;
                        case 3: emit(c, MAKE_A(right->pat_lit.bval ? OP_PUSH_TRUE : OP_PUSH_FALSE, 0, 0)); break;
                        default: emit(c, MAKE_A(OP_PUSH_NULL, 0, 0)); break;
                        }
                        emit(c, MAKE_A(OP_EQ, 0, 0));
                        int jf_right = emit_jump(c, OP_JUMP_IF_FALSE);
                        patch_jump(c, j_left_ok);
                        if (arm->guard) {
                            compile_node(c, arm->guard, 1);
                            int jf_guard = emit_jump(c, OP_JUMP_IF_FALSE);
                            compile_node(c, arm->body, want_value);
                            arm_jumps[n_arm_jumps++] = emit_jump(c, OP_JUMP);
                            patch_jump(c, jf_right);
                            patch_jump(c, jf_guard);
                        } else {
                            compile_node(c, arm->body, want_value);
                            arm_jumps[n_arm_jumps++] = emit_jump(c, OP_JUMP);
                            patch_jump(c, jf_right);
                        }
                        continue;
                    } else if (right && (right->tag == NODE_PAT_WILD ||
                               right->tag == NODE_PAT_IDENT)) {
                        patch_jump(c, j_left_ok);
                        if (right->tag == NODE_PAT_IDENT) {
                            int bs = local_add(c->current, right->pat_ident.name);
                            emit_a(c, OP_LOAD_LOCAL, subj_slot);
                            emit_a(c, OP_STORE_LOCAL, bs);
                        }
                    } else {
                        patch_jump(c, j_left_ok);
                    }
                } else if (left && (left->tag == NODE_PAT_WILD ||
                           left->tag == NODE_PAT_IDENT)) {
                    if (left->tag == NODE_PAT_IDENT) {
                        int bs = local_add(c->current, left->pat_ident.name);
                        emit_a(c, OP_LOAD_LOCAL, subj_slot);
                        emit_a(c, OP_STORE_LOCAL, bs);
                    }
                } else {
                    /* unsupported sub-pattern */
                }
            } else if (pat->tag == NODE_PAT_SLICE) {
                for (int si = 0; si < pat->pat_slice.elems.len; si++) {
                    Node *elem_pat = pat->pat_slice.elems.items[si];
                    if (elem_pat->tag == NODE_PAT_IDENT) {
                        emit_a(c, OP_LOAD_LOCAL, subj_slot);
                        emit_const(c, xs_int(si));
                        emit(c, MAKE_A(OP_INDEX_GET, 0, 0));
                        int slot = local_add(c->current, elem_pat->pat_ident.name);
                        emit_a(c, OP_STORE_LOCAL, slot);
                    } else if (elem_pat->tag == NODE_PAT_LIT) {
                        emit_a(c, OP_LOAD_LOCAL, subj_slot);
                        emit_const(c, xs_int(si));
                        emit(c, MAKE_A(OP_INDEX_GET, 0, 0));
                        switch (elem_pat->pat_lit.tag) {
                        case 0: emit_const(c, xs_int(elem_pat->pat_lit.ival)); break;
                        case 1: emit_const(c, xs_float(elem_pat->pat_lit.fval)); break;
                        case 2: emit_const(c, xs_str(elem_pat->pat_lit.sval)); break;
                        case 3: emit(c, MAKE_A(elem_pat->pat_lit.bval ? OP_PUSH_TRUE : OP_PUSH_FALSE, 0, 0)); break;
                        default: emit(c, MAKE_A(OP_PUSH_NULL, 0, 0)); break;
                        }
                        emit(c, MAKE_A(OP_EQ, 0, 0));
                        j_next = emit_jump(c, OP_JUMP_IF_FALSE);
                    } else if (elem_pat->tag == NODE_PAT_WILD) {
                        /* nothing to do */
                    }
                }
            } else if (pat->tag == NODE_PAT_EXPR) {
                emit_a(c, OP_LOAD_LOCAL, subj_slot);
                compile_node(c, pat->pat_expr.expr, 1);
                emit(c, MAKE_A(OP_EQ, 0, 0));
                j_next = emit_jump(c, OP_JUMP_IF_FALSE);
            } else if (pat->tag == NODE_PAT_GUARD) {
                if (pat->pat_guard.pattern && pat->pat_guard.pattern->tag == NODE_PAT_IDENT) {
                    int bs = local_add(c->current, pat->pat_guard.pattern->pat_ident.name);
                    emit_a(c, OP_LOAD_LOCAL, subj_slot);
                    emit_a(c, OP_STORE_LOCAL, bs);
                }
                if (pat->pat_guard.guard) {
                    compile_node(c, pat->pat_guard.guard, 1);
                    j_next = emit_jump(c, OP_JUMP_IF_FALSE);
                }
            } else if (pat->tag == NODE_PAT_STRUCT) {
                for (int fi = 0; fi < pat->pat_struct.fields.len; fi++) {
                    const char *fname = pat->pat_struct.fields.items[fi].key;
                    Node *fpat = pat->pat_struct.fields.items[fi].val;
                    emit_a(c, OP_LOAD_LOCAL, subj_slot);
                    {
                        int fi_idx = emit_global_name(c, fname);
                        emit_a(c, OP_LOAD_FIELD, fi_idx);
                    }
                    if (fpat && fpat->tag == NODE_PAT_IDENT) {
                        int slot = local_add(c->current, fpat->pat_ident.name);
                        emit_a(c, OP_STORE_LOCAL, slot);
                    } else if (fpat && fpat->tag == NODE_PAT_LIT) {
                        switch (fpat->pat_lit.tag) {
                        case 0: emit_const(c, xs_int(fpat->pat_lit.ival)); break;
                        case 1: emit_const(c, xs_float(fpat->pat_lit.fval)); break;
                        case 2: emit_const(c, xs_str(fpat->pat_lit.sval)); break;
                        case 3: emit(c, MAKE_A(fpat->pat_lit.bval ? OP_PUSH_TRUE : OP_PUSH_FALSE, 0, 0)); break;
                        default: emit(c, MAKE_A(OP_PUSH_NULL, 0, 0)); break;
                        }
                        emit(c, MAKE_A(OP_EQ, 0, 0));
                        j_next = emit_jump(c, OP_JUMP_IF_FALSE);
                    } else {
                        emit(c, MAKE_A(OP_POP, 0, 0));
                    }
                }
            }

            int j_guard = -1;
            if (arm->guard) {
                compile_node(c, arm->guard, 1);
                j_guard = emit_jump(c, OP_JUMP_IF_FALSE);
            }

            compile_node(c, arm->body, want_value);
            arm_jumps[n_arm_jumps++] = emit_jump(c, OP_JUMP);
            if (j_next >= 0)  patch_jump(c, j_next);
            if (j_guard >= 0) patch_jump(c, j_guard);
            /* patch all tuple pattern element jumps */
            if (pat && pat->tag == NODE_PAT_TUPLE) {
                for (int tj = 0; tj < n_tuple_jumps; tj++)
                    patch_jump(c, tuple_jumps[tj]);
            }
        }

        for (int i = 0; i < n_arm_jumps; i++)
            patch_jump(c, arm_jumps[i]);
        if (arm_jumps) free(arm_jumps);

        if (want_value) {
            /* fallthrough with null if no arm matched */
        }
        return;
    }

    case NODE_STRUCT_DECL: {
        int nfields = n->struct_decl.fields.len;
        for (int fi = 0; fi < nfields; fi++) {
            const char *fname = n->struct_decl.fields.items[fi].key;
            Node *fdefault = n->struct_decl.fields.items[fi].val;
            emit_const(c, xs_str(fname ? fname : "?"));
            if (fdefault)
                compile_node(c, fdefault, 1);
            else
                emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        }
        int name_idx = emit_global_name(c, n->struct_decl.name);
        emit(c, MAKE_A(OP_MAKE_CLASS, (uint8_t)(unsigned)nfields, (uint16_t)(unsigned)name_idx));
        if (want_value) emit(c, MAKE_A(OP_DUP, 0, 0));
        compile_name_store(c, n->struct_decl.name);
        return;
    }

    case NODE_ENUM_DECL: {
        int nvariants = n->enum_decl.variants.len;
        for (int vi = 0; vi < nvariants; vi++) {
            EnumVariant *v = &n->enum_decl.variants.items[vi];
            emit_const(c, xs_str(v->name));
            if (v->fields.len == 0) {
                emit_const(c, xs_str("_tag"));
                emit_const(c, xs_str(v->name));
                emit_const(c, xs_str("__type"));
                emit_const(c, xs_str(n->enum_decl.name));
                emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, 2));
            } else {
                int arity = v->fields.len;
                XSProto *parent = c->current->proto;
                XSProto *ctor = proto_new(v->name, arity);

                if (parent->n_inner == parent->cap_inner) {
                    parent->cap_inner = parent->cap_inner ? parent->cap_inner * 2 : 4;
                    parent->inner = xs_realloc(parent->inner,
                                        (size_t)parent->cap_inner * sizeof(XSProto *));
                }
                int inner_idx = parent->n_inner;
                parent->inner[parent->n_inner++] = ctor;

                CompilerScope ctor_scope;
                scope_push(c, &ctor_scope, ctor);

                for (int pi = 0; pi < arity; pi++) {
                    char pbuf[32];
                    snprintf(pbuf, sizeof pbuf, "p%d", pi);
                    local_add(c->current, pbuf);
                }

                emit_const(c, xs_str("_tag"));
                emit_const(c, xs_str(v->name));
                emit_const(c, xs_str("__type"));
                emit_const(c, xs_str(n->enum_decl.name));
                emit_const(c, xs_str("_val"));
                if (arity == 1) {
                    emit_a(c, OP_LOAD_LOCAL, 0);
                } else {
                    for (int pi = 0; pi < arity; pi++)
                        emit_a(c, OP_LOAD_LOCAL, pi);
                    emit(c, MAKE_B(OP_MAKE_TUPLE, 0, 0, (uint8_t)(unsigned)arity));
                }
                emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, 3));
                emit(c, MAKE_A(OP_RETURN, 0, 0));

                scope_pop(c);
                emit_make_closure(c, inner_idx);
            }
        }
        emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, (uint8_t)(unsigned)nvariants));
        if (want_value) emit(c, MAKE_A(OP_DUP, 0, 0));
        compile_name_store(c, n->enum_decl.name);
        return;
    }

    case NODE_CLASS_DECL: {
        int nbases = n->class_decl.nbases;
        int field_count = 0, method_count = 0;
        for (int mi = 0; mi < n->class_decl.members.len; mi++) {
            Node *mem = n->class_decl.members.items[mi];
            if (mem->tag == NODE_LET || mem->tag == NODE_VAR) field_count++;
            else if (mem->tag == NODE_FN_DECL) method_count++;
        }

        int fields_slot = local_add_hidden(c);
        int methods_slot = local_add_hidden(c);
        int bases_slot = local_add_hidden(c);
        for (int mi = 0; mi < n->class_decl.members.len; mi++) {
            Node *mem = n->class_decl.members.items[mi];
            if (mem->tag == NODE_LET || mem->tag == NODE_VAR) {
                emit_const(c, xs_str(mem->let.name ? mem->let.name : "?"));
                if (mem->let.value) compile_node(c, mem->let.value, 1);
                else emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
            }
        }
        emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, (uint8_t)(unsigned)field_count));
        emit_a(c, OP_STORE_LOCAL, fields_slot);

        for (int mi = 0; mi < n->class_decl.members.len; mi++) {
            Node *mem = n->class_decl.members.items[mi];
            if (mem->tag == NODE_FN_DECL) {
                emit_const(c, xs_str(mem->fn_decl.name ? mem->fn_decl.name : "?"));
                int fidx = compile_fn(c, mem->fn_decl.name,
                                      &mem->fn_decl.params, mem->fn_decl.body);
                emit_make_closure(c, fidx);
            }
        }
        emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, (uint8_t)(unsigned)method_count));
        emit_a(c, OP_STORE_LOCAL, methods_slot);

        for (int bi = 0; bi < nbases; bi++)
            compile_name_load(c, n->class_decl.bases[bi]);
        emit(c, MAKE_B(OP_MAKE_ARRAY, 0, 0, (uint8_t)(unsigned)nbases));
        emit_a(c, OP_STORE_LOCAL, bases_slot);

        emit_const(c, xs_str("__name"));
        emit_const(c, xs_str(n->class_decl.name));
        emit_const(c, xs_str("__fields"));
        emit_a(c, OP_LOAD_LOCAL, fields_slot);
        emit_const(c, xs_str("__methods"));
        emit_a(c, OP_LOAD_LOCAL, methods_slot);
        emit_const(c, xs_str("__bases"));
        emit_a(c, OP_LOAD_LOCAL, bases_slot);
        emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, 4));

        for (int bi = 0; bi < nbases; bi++) {
            compile_name_load(c, n->class_decl.bases[bi]);
            emit(c, MAKE_A(OP_INHERIT, 0, 0));
        }

        if (want_value) emit(c, MAKE_A(OP_DUP, 0, 0));
        compile_name_store(c, n->class_decl.name);
        return;
    }

    case NODE_TRAIT_DECL: {
        int npairs = 0;

        /* __name */
        emit_const(c, xs_str("__name"));
        emit_const(c, xs_str(n->trait_decl.name));
        npairs++;

        /* __methods array */
        emit_const(c, xs_str("__methods"));
        for (int mi = 0; mi < n->trait_decl.n_methods; mi++)
            emit_const(c, xs_str(n->trait_decl.method_names[mi]));
        emit(c, MAKE_B(OP_MAKE_ARRAY, 0, 0,
                        (uint8_t)(unsigned)n->trait_decl.n_methods));
        npairs++;

        /* __assoc_types array */
        emit_const(c, xs_str("__assoc_types"));
        for (int ai = 0; ai < n->trait_decl.n_assoc_types; ai++)
            emit_const(c, xs_str(n->trait_decl.assoc_types[ai]));
        emit(c, MAKE_B(OP_MAKE_ARRAY, 0, 0,
                        (uint8_t)(unsigned)n->trait_decl.n_assoc_types));
        npairs++;

        /* __super (parent trait or null) */
        emit_const(c, xs_str("__super"));
        if (n->trait_decl.super_trait)
            emit_const(c, xs_str(n->trait_decl.super_trait));
        else
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        npairs++;

        emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, (uint8_t)(unsigned)npairs));

        if (want_value) emit(c, MAKE_A(OP_DUP, 0, 0));
        compile_name_store(c, n->trait_decl.name);
        return;
    }

    // --- impl
    case NODE_IMPL_DECL: {
        for (int mi = 0; mi < n->impl_decl.members.len; mi++) {
            Node *mem = n->impl_decl.members.items[mi];
            if (mem->tag != NODE_FN_DECL || !mem->fn_decl.name) continue;

            compile_name_load(c, n->impl_decl.type_name);
            emit_const(c, xs_str(mem->fn_decl.name));

            int fidx = compile_fn(c, mem->fn_decl.name,
                                  &mem->fn_decl.params,
                                  mem->fn_decl.body);
            emit_make_closure(c, fidx);
            emit(c, MAKE_A(OP_IMPL_METHOD, 0, 0));

            emit_make_closure(c, fidx);
            compile_name_store(c, mem->fn_decl.name);
        }
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        return;
    }

    case NODE_IMPORT: {
        if (n->import.nparts == 0) {
            if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
            return;
        }
        char *modname = n->import.path[0];

        if (n->import.nitems > 0) {
            compile_name_load(c, modname);
            int mod_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, mod_slot);
            for (int ii = 0; ii < n->import.nitems; ii++) {
                emit_a(c, OP_LOAD_LOCAL, mod_slot);
                int ni = emit_global_name(c, n->import.items[ii]);
                emit_a(c, OP_LOAD_FIELD, ni);
                compile_name_store(c, n->import.items[ii]);
            }
        } else if (n->import.alias) {
            compile_name_load(c, modname);
            compile_name_store(c, n->import.alias);
        } else {
            compile_name_load(c, modname);
            int slot = local_add(c->current, modname);
            emit_a(c, OP_STORE_LOCAL, slot);
        }
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        return;
    }

    /* module decl */
    case NODE_MODULE_DECL: {
        scope_begin(c);
        int mod_start_locals = c->current->n_locals;

        for (int mi = 0; mi < n->module_decl.body.len; mi++) {
            Node *item = n->module_decl.body.items[mi];
            if (item->tag == NODE_FN_DECL && item->fn_decl.name) {
                int fidx = compile_fn(c, item->fn_decl.name,
                                      &item->fn_decl.params, item->fn_decl.body);
                if (item->fn_decl.is_generator) {
                    XSProto *inner = c->current->proto->inner[fidx];
                    inner->arity = -(inner->arity + 1);
                }
                emit_make_closure(c, fidx);
                int slot = local_add(c->current, item->fn_decl.name);
                emit_a(c, OP_STORE_LOCAL, slot);
            } else if (item->tag == NODE_LET || item->tag == NODE_VAR) {
                compile_node(c, item, 0);
            } else {
                compile_node(c, item, 0);
            }
        }

        int mod_end_locals = c->current->n_locals;
        int n_mod_locals = mod_end_locals - mod_start_locals;

        for (int li = mod_start_locals; li < mod_end_locals; li++) {
            emit_const(c, xs_str(c->current->locals[li].name));
            emit_a(c, OP_LOAD_LOCAL, c->current->locals[li].slot);
        }
        emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, (uint8_t)(unsigned)n_mod_locals));

        scope_end(c);

        if (want_value) emit(c, MAKE_A(OP_DUP, 0, 0));
        compile_name_store(c, n->module_decl.name);
        return;
    }

    case NODE_TYPE_ALIAS:
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        return;

    case NODE_CAST:
        compile_node(c, n->cast.expr, want_value);
        return;

    case NODE_THROW:
        if (n->throw_.value)
            compile_node(c, n->throw_.value, 1);
        else
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        emit(c, MAKE_A(OP_THROW, 0, 0));
        return;

    // --- try/catch
    case NODE_TRY: {
        int try_start = emit_jump(c, OP_TRY_BEGIN);
        compile_node(c, n->try_.body, want_value);
        emit(c, MAKE_A(OP_TRY_END, 0, 0));
        int over_catch = emit_jump(c, OP_JUMP);
        patch_jump(c, try_start);
        emit(c, MAKE_A(OP_CATCH, 0, 0));
        if (n->try_.catch_arms.len > 0) {
            MatchArm *arm = &n->try_.catch_arms.items[0];
            if (arm->pattern && arm->pattern->tag == NODE_IDENT
                    && arm->pattern->ident.name) {
                int slot = local_add(c->current, arm->pattern->ident.name);
                emit_a(c, OP_STORE_LOCAL, slot);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT
                    && arm->pattern->pat_ident.name) {
                int slot = local_add(c->current, arm->pattern->pat_ident.name);
                emit_a(c, OP_STORE_LOCAL, slot);
            } else {
                emit(c, MAKE_A(OP_POP, 0, 0));
            }
            if (arm->body)
                compile_node(c, arm->body, want_value);
            else if (want_value)
                emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        } else {
            emit(c, MAKE_A(OP_POP, 0, 0));
            if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        }
        patch_jump(c, over_catch);
        if (n->try_.finally_block)
            compile_node(c, n->try_.finally_block, 0);
        return;
    }

    case NODE_SPREAD:
        compile_node(c, n->spread.expr, 1);
        emit(c, MAKE_A(OP_SPREAD, 0, 0));
        if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
        return;

    case NODE_LIST_COMP: {
        emit(c, MAKE_B(OP_MAKE_ARRAY, 0, 0, 0));
        int arr_slot = local_add_hidden(c);
        emit_a(c, OP_STORE_LOCAL, arr_slot);

        for (int ci = 0; ci < n->list_comp.clause_iters.len; ci++) {
            Node *iter_expr = n->list_comp.clause_iters.items[ci];
            Node *cpat = n->list_comp.clause_pats.items[ci];
            Node *cond = (ci < n->list_comp.clause_conds.len)
                         ? n->list_comp.clause_conds.items[ci] : NULL;

            /* compile iterator */
            compile_node(c, iter_expr, 1);
            int iter_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, iter_slot);

            /* len = iter.len */
            emit_a(c, OP_LOAD_LOCAL, iter_slot);
            emit(c, MAKE_A(OP_ITER_LEN, 0, 0));
            int len_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, len_slot);

            /* idx = 0 */
            emit_const(c, xs_int(0));
            int idx_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, idx_slot);

            /* loop: */
            int loop_top = c->current->proto->chunk.len;

            /* if idx >= len: break */
            emit_a(c, OP_LOAD_LOCAL, idx_slot);
            emit_a(c, OP_LOAD_LOCAL, len_slot);
            emit(c, MAKE_A(OP_GTE, 0, 0));
            int exit_jump = emit_jump(c, OP_JUMP_IF_TRUE);

            /* elem = iter[idx] */
            emit_a(c, OP_LOAD_LOCAL, iter_slot);
            emit_a(c, OP_LOAD_LOCAL, idx_slot);
            emit(c, MAKE_A(OP_ITER_GET, 0, 0));

            /* bind pattern variable */
            const char *cpat_name = NULL;
            if (cpat && cpat->tag == NODE_PAT_IDENT) cpat_name = cpat->pat_ident.name;
            else if (cpat && cpat->tag == NODE_IDENT) cpat_name = cpat->ident.name;
            if (cpat_name) {
                int var_slot = local_add(c->current, cpat_name);
                emit_a(c, OP_STORE_LOCAL, var_slot);
            } else {
                emit(c, MAKE_A(OP_POP, 0, 0));
            }

            /* if cond: only add if true */
            int skip_push = -1;
            if (cond) {
                compile_node(c, cond, 1);
                skip_push = emit_jump(c, OP_JUMP_IF_FALSE);
            }

            /* result.push(element_expr) */
            emit_a(c, OP_LOAD_LOCAL, arr_slot);
            compile_node(c, n->list_comp.element, 1);
            {
                int push_idx = emit_global_name(c, "push");
                emit(c, MAKE_A(OP_METHOD_CALL, 1, (uint16_t)(unsigned)push_idx));
            }
            emit(c, MAKE_A(OP_POP, 0, 0)); /* discard push return */

            if (skip_push >= 0)
                patch_jump(c, skip_push);

            /* idx++ */
            emit_a(c, OP_LOAD_LOCAL, idx_slot);
            emit_const(c, xs_int(1));
            emit(c, MAKE_A(OP_ADD, 0, 0));
            emit_a(c, OP_STORE_LOCAL, idx_slot);

            /* jump to loop top */
            {
                int back = c->current->proto->chunk.len;
                emit(c, MAKE_A(OP_JUMP, 0, (uint16_t)(int16_t)(loop_top - back - 1)));
            }

            patch_jump(c, exit_jump);
        }

        /* Push result array */
        if (want_value)
            emit_a(c, OP_LOAD_LOCAL, arr_slot);
        return;
    }


    case NODE_MAP_COMP: {
        /* result = {} */
        emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, 0));
        int map_slot = local_add_hidden(c);
        emit_a(c, OP_STORE_LOCAL, map_slot);

        for (int ci = 0; ci < n->map_comp.clause_iters.len; ci++) {
            Node *iter_expr = n->map_comp.clause_iters.items[ci];
            Node *cpat = n->map_comp.clause_pats.items[ci];
            Node *cond = (ci < n->map_comp.clause_conds.len)
                         ? n->map_comp.clause_conds.items[ci] : NULL;

            compile_node(c, iter_expr, 1);
            int iter_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, iter_slot);
            emit_a(c, OP_LOAD_LOCAL, iter_slot);
            emit(c, MAKE_A(OP_ITER_LEN, 0, 0));
            int len_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, len_slot);
            emit_const(c, xs_int(0));
            int idx_slot = local_add_hidden(c);
            emit_a(c, OP_STORE_LOCAL, idx_slot);

            int loop_top = c->current->proto->chunk.len;
            emit_a(c, OP_LOAD_LOCAL, idx_slot);
            emit_a(c, OP_LOAD_LOCAL, len_slot);
            emit(c, MAKE_A(OP_GTE, 0, 0));
            int exit_jump = emit_jump(c, OP_JUMP_IF_TRUE);

            emit_a(c, OP_LOAD_LOCAL, iter_slot);
            emit_a(c, OP_LOAD_LOCAL, idx_slot);
            emit(c, MAKE_A(OP_ITER_GET, 0, 0));

            const char *cpat_name = NULL;
            if (cpat && cpat->tag == NODE_PAT_IDENT) cpat_name = cpat->pat_ident.name;
            else if (cpat && cpat->tag == NODE_IDENT) cpat_name = cpat->ident.name;
            if (cpat_name) {
                int var_slot = local_add(c->current, cpat_name);
                emit_a(c, OP_STORE_LOCAL, var_slot);
            } else {
                emit(c, MAKE_A(OP_POP, 0, 0));
            }

            int skip_push = -1;
            if (cond) {
                compile_node(c, cond, 1);
                skip_push = emit_jump(c, OP_JUMP_IF_FALSE);
            }

            /* map[key] = value */
            emit_a(c, OP_LOAD_LOCAL, map_slot);
            compile_node(c, n->map_comp.key, 1);
            compile_node(c, n->map_comp.value, 1);
            emit(c, MAKE_A(OP_INDEX_SET, 0, 0));

            if (skip_push >= 0) patch_jump(c, skip_push);

            emit_a(c, OP_LOAD_LOCAL, idx_slot);
            emit_const(c, xs_int(1));
            emit(c, MAKE_A(OP_ADD, 0, 0));
            emit_a(c, OP_STORE_LOCAL, idx_slot);

            int back = c->current->proto->chunk.len;
            emit(c, MAKE_A(OP_JUMP, 0, (uint16_t)(int16_t)(loop_top - back - 1)));
            patch_jump(c, exit_jump);
        }

        if (want_value)
            emit_a(c, OP_LOAD_LOCAL, map_slot);
        return;
    }

    case NODE_DEFER: {
        int defer_start = emit_jump(c, OP_DEFER_PUSH);
        compile_node(c, n->defer_.body, 0);
        emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        emit(c, MAKE_A(OP_RETURN, 0, 0));
        patch_jump(c, defer_start);
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        return;
    }

    case NODE_YIELD:
        if (n->yield_.value)
            compile_node(c, n->yield_.value, 1);
        else
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        emit(c, MAKE_A(OP_YIELD, 0, 0));
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        return;

    /* actor */
    case NODE_ACTOR_DECL: {
        int nstate = n->actor_decl.state_fields.len;
        int nmethods = n->actor_decl.methods.len;
        int state_slot = local_add_hidden(c);
        int meth_slot = local_add_hidden(c);

        for (int si = 0; si < nstate; si++) {
            const char *fname = n->actor_decl.state_fields.items[si].key;
            Node *def = n->actor_decl.state_fields.items[si].val;
            emit_const(c, xs_str(fname ? fname : "?"));
            if (def) compile_node(c, def, 1);
            else emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        }
        emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, (uint8_t)(unsigned)nstate));
        emit_a(c, OP_STORE_LOCAL, state_slot);

        /* compile actor methods: add implicit 'self' param and state var accessors */
        char **state_names = NULL;
        if (nstate > 0) {
            state_names = xs_malloc((size_t)nstate * sizeof(char*));
            for (int si = 0; si < nstate; si++)
                state_names[si] = xs_strdup(n->actor_decl.state_fields.items[si].key ?
                    n->actor_decl.state_fields.items[si].key : "?");
        }
        int actual_methods = 0;
        for (int mi = 0; mi < nmethods; mi++) {
            Node *m = n->actor_decl.methods.items[mi];
            if (m->tag != NODE_FN_DECL) continue;
            emit_const(c, xs_str(m->fn_decl.name ? m->fn_decl.name : "?"));
            /* compile actor method with self + state field loading */
            {
                int method_arity = m->fn_decl.params.len + 1; /* +1 for self */
                XSProto *parent = c->current->proto;
                XSProto *inner = proto_new(m->fn_decl.name ? m->fn_decl.name : "<actor_method>", method_arity);
                if (parent->n_inner == parent->cap_inner) {
                    parent->cap_inner = parent->cap_inner ? parent->cap_inner * 2 : 4;
                    parent->inner = xs_realloc(parent->inner, (size_t)parent->cap_inner * sizeof(XSProto *));
                }
                int inner_idx = parent->n_inner;
                parent->inner[parent->n_inner++] = inner;
                CompilerScope fn_scope;
                scope_push(c, &fn_scope, inner);
                /* self is local 0 */
                int self_slot = local_add(c->current, "self");
                (void)self_slot;
                /* add user params */
                for (int pi = 0; pi < m->fn_decl.params.len; pi++) {
                    const char *pname = m->fn_decl.params.items[pi].name;
                    local_add(c->current, pname ? pname : "<param>");
                }
                /* add state field locals and load from self */
                for (int si = 0; si < nstate; si++) {
                    int slot = local_add(c->current, state_names[si]);
                    emit_a(c, OP_LOAD_LOCAL, 0); /* self */
                    int fi = emit_global_name(c, state_names[si]);
                    emit_a(c, OP_LOAD_FIELD, fi);
                    emit_a(c, OP_STORE_LOCAL, slot);
                }
                /* save state field info for write-back before returns */
                c->current->actor_state_names = state_names;
                c->current->actor_nstate = nstate;
                compile_node(c, m->fn_decl.body, 1);
                c->current->actor_state_names = NULL;
                c->current->actor_nstate = 0;
                /* write back state fields to self (for fall-through) */
                for (int si = 0; si < nstate; si++) {
                    int slot = local_resolve(c->current, state_names[si]);
                    if (slot >= 0) {
                        emit_a(c, OP_LOAD_LOCAL, 0); /* self */
                        emit_a(c, OP_LOAD_LOCAL, slot);
                        int fi = emit_global_name(c, state_names[si]);
                        emit_a(c, OP_STORE_FIELD, fi);
                    }
                }
                XSChunk *ch = &inner->chunk;
                if (ch->len == 0 || INSTR_OPCODE(ch->code[ch->len - 1]) != OP_RETURN)
                    emit(c, MAKE_A(OP_RETURN, 0, 0));
                if (fn_scope.n_upvalues > 0) {
                    inner->uv_descs = xs_malloc((size_t)fn_scope.n_upvalues * sizeof(UVDesc));
                    memcpy(inner->uv_descs, fn_scope.uv_descs, (size_t)fn_scope.n_upvalues * sizeof(UVDesc));
                    inner->n_upvalues = fn_scope.n_upvalues;
                }
                scope_pop(c);
                emit_make_closure(c, inner_idx);
            }
            actual_methods++;
        }
        if (state_names) { for (int si = 0; si < nstate; si++) free(state_names[si]); free(state_names); }
        emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, (uint8_t)(unsigned)actual_methods));
        emit_a(c, OP_STORE_LOCAL, meth_slot);

        emit_const(c, xs_str("__actor_name"));
        emit_const(c, xs_str(n->actor_decl.name));
        emit_const(c, xs_str("__state"));
        emit_a(c, OP_LOAD_LOCAL, state_slot);
        emit_const(c, xs_str("__methods"));
        emit_a(c, OP_LOAD_LOCAL, meth_slot);
        emit(c, MAKE_B(OP_MAKE_MAP, 0, 0, 3));

        if (want_value) emit(c, MAKE_A(OP_DUP, 0, 0));
        compile_name_store(c, n->actor_decl.name);
        return;
    }

    case NODE_SEND_EXPR: {
        compile_node(c, n->send_expr.target, 1);
        compile_node(c, n->send_expr.message, 1);
        emit(c, MAKE_A(OP_SEND, 0, 0));
        if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
        return;
    }

    case NODE_AWAIT:
        compile_node(c, n->await_.expr, 1);
        emit(c, MAKE_A(OP_AWAIT, 0, 0));
        if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
        return;

    case NODE_SPAWN: {
        int idx = compile_fn(c, "__spawn__", NULL, n->spawn_.expr);
        emit_make_closure(c, idx);
        emit(c, MAKE_A(OP_SPAWN, 0, 0));
        if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
        return;
    }

    case NODE_NURSERY:
        compile_node(c, n->nursery_.body, want_value);
        return;

    case NODE_PERFORM: {
        for (int i = 0; i < n->perform.args.len; i++)
            compile_node(c, n->perform.args.items[i], 1);
        char buf[256];
        snprintf(buf, sizeof buf, "%s.%s", n->perform.effect_name, n->perform.op_name);
        int name_idx = emit_global_name(c, buf);
        emit(c, MAKE_A(OP_EFFECT_CALL, (uint8_t)n->perform.args.len, (uint16_t)(unsigned)name_idx));
        if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
        return;
    }


    case NODE_HANDLE: {
        int try_start = emit_jump(c, OP_TRY_BEGIN);
        compile_node(c, n->handle.expr, want_value);
        emit(c, MAKE_A(OP_TRY_END, 0, 0));
        int over_handler = emit_jump(c, OP_JUMP);
        patch_jump(c, try_start);
        emit(c, MAKE_A(OP_CATCH, 0, 0));
        if (n->handle.arms.len > 0) {
            EffectArm *earm = &n->handle.arms.items[0];
            if (earm->params.len > 0) {
                int slot = local_add(c->current, earm->params.items[0].name);
                emit_a(c, OP_STORE_LOCAL, slot);
            } else {
                emit(c, MAKE_A(OP_POP, 0, 0));
            }
            compile_node(c, earm->body, want_value);
        } else {
            emit(c, MAKE_A(OP_POP, 0, 0));
            if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        }
        patch_jump(c, over_handler);
        return;
    }

    case NODE_RESUME:
        if (n->resume_.value)
            compile_node(c, n->resume_.value, 1);
        else
            emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        emit(c, MAKE_A(OP_EFFECT_RESUME, 0, 0));
        if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
        return;

    case NODE_EFFECT_DECL:
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        return;

    case NODE_USE: {
        if (n->use_.is_plugin && n->use_.path) {
            /* emit: __load_plugin("path") */
            emit_a(c, OP_LOAD_GLOBAL, emit_global_name(c, "__load_plugin"));
            emit_const(c, xs_str(n->use_.path));
            emit(c, MAKE_B(OP_CALL, 0, 0, 1));
            if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
        } else if (n->use_.path) {
            /* regular use — treat as no-op in VM for now */
            if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        } else {
            if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        }
        return;
    }

    default:
        fprintf(stderr, "unhandled node tag %d\n", (int)n->tag);
        if (want_value) emit(c, MAKE_A(OP_PUSH_NULL, 0, 0));
        return;
    }

    if (!want_value) emit(c, MAKE_A(OP_POP, 0, 0));
}

XSProto *compile_program(Node *program) {
    Compiler c = {0};
    CompilerScope top;
    XSProto *p = proto_new("<main>", 0);
    scope_push(&c, &top, p);
    compile_node(&c, program, 0);
    emit(&c, MAKE_A(OP_PUSH_NULL, 0, 0));
    emit(&c, MAKE_A(OP_RETURN,    0, 0));
    scope_pop(&c);
    return p;
}
