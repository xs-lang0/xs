#include "vm/bytecode.h"
#include "core/value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

XSProto *proto_new(const char *name, int arity) {
    XSProto *p = xs_malloc(sizeof *p);
    memset(p, 0, sizeof *p);
    p->name = name ? xs_strdup(name) : NULL;
    p->arity = arity;
    p->refcount = 1;
    return p;
}

void proto_free(XSProto *p) {
    if (!p || --p->refcount > 0) return;
    free(p->name);
    for (int i = 0; i < p->chunk.nconsts; i++) value_decref(p->chunk.consts[i]);
    free(p->chunk.consts);
    free(p->chunk.code);
    for (int i = 0; i < p->n_inner; i++) proto_free(p->inner[i]);
    free(p->inner);
    free(p->uv_descs);
    free(p);
}

int chunk_write(XSChunk *c, Instruction i) {
    if (c->len == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 16;
        c->code = xs_realloc(c->code, (size_t)c->cap * sizeof(Instruction));
    }
    c->code[c->len++] = i;
    return c->len - 1;
}

int chunk_add_const(XSChunk *c, Value *v) {
    if (c->nconsts == c->cap_consts) {
        c->cap_consts = c->cap_consts ? c->cap_consts * 2 : 8;
        c->consts = xs_realloc(c->consts, (size_t)c->cap_consts * sizeof(Value*));
    }
    value_incref(v);
    c->consts[c->nconsts++] = v;
    return c->nconsts - 1;
}

static const char *op_name(Opcode op) {
    static const char *names[] = {
        "NOP","PUSH_CONST","PUSH_NULL","PUSH_TRUE","PUSH_FALSE","POP","DUP",
        "LOAD_LOCAL","STORE_LOCAL","LOAD_UPVALUE","STORE_UPVALUE",
        "LOAD_GLOBAL","STORE_GLOBAL",
        "ADD","SUB","MUL","DIV","MOD","POW","NEG","NOT",
        "EQ","NEQ","LT","GT","LTE","GTE","CONCAT",
        "MAKE_ARRAY","MAKE_TUPLE","MAKE_MAP","INDEX_GET","INDEX_SET",
        "LOAD_FIELD","STORE_FIELD",
        "JUMP","JUMP_IF_FALSE","JUMP_IF_TRUE",
        "MAKE_RANGE","ITER_LEN","ITER_GET","METHOD_CALL",
        "MAKE_CLOSURE","CALL","TAIL_CALL","RETURN",
        "SWAP",
        "BAND","BOR","BXOR","BNOT","SHL","SHR",
        "THROW","TRY_BEGIN","TRY_END","CATCH",
        "TRACE_CALL","TRACE_RETURN","TRACE_STORE","TRACE_IO",
        "AND","OR","SPREAD","LOOP",
        "EFFECT_CALL","EFFECT_RESUME","EFFECT_HANDLE",
        "AWAIT","YIELD","SPAWN",
    };
    return (unsigned)op < OP__MAX ? names[op] : "?";
}

void proto_dump(XSProto *p) {
    printf("=== proto <%s> arity=%d locals=%d ===\n",
           p->name ? p->name : "<anon>", p->arity, p->nlocals);
    for (int i = 0; i < p->chunk.len; i++) {
        Instruction in = p->chunk.code[i];
        printf("  %04d  %-20s A=%-3d B=%-3d C=%-3d Bx=%-5d sBx=%d\n",
               i, op_name(INSTR_OPCODE(in)),
               INSTR_A(in), INSTR_B(in), INSTR_C(in),
               INSTR_Bx(in), (int)INSTR_sBx(in));
    }
    for (int i = 0; i < p->n_inner; i++) proto_dump(p->inner[i]);
}
