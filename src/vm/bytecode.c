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

/* .xsc binary format:
   header: "XSC\0" (4 bytes) + version u16
   per proto (recursive):
     name_len u16, name bytes
     arity u16, nlocals u16, n_upvalues u16
     n_code u32, code (n_code * 4 bytes)
     n_consts u16, per const: tag u8 + payload
     n_uv_descs u16, per desc: is_local u8 + index u16
     n_inner u16, then each inner proto recursively
*/

static void write_u8(FILE *f, uint8_t v)   { fwrite(&v, 1, 1, f); }
static void write_u16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void write_u32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void write_i64(FILE *f, int64_t v)  { fwrite(&v, 8, 1, f); }
static void write_f64(FILE *f, double v)   { fwrite(&v, 8, 1, f); }
static void write_str(FILE *f, const char *s) {
    uint16_t len = s ? (uint16_t)strlen(s) : 0;
    write_u16(f, len);
    if (len) fwrite(s, 1, len, f);
}

static uint8_t  read_u8(FILE *f)  { uint8_t v = 0;  fread(&v, 1, 1, f); return v; }
static uint16_t read_u16(FILE *f) { uint16_t v = 0; fread(&v, 2, 1, f); return v; }
static uint32_t read_u32(FILE *f) { uint32_t v = 0; fread(&v, 4, 1, f); return v; }
static int64_t  read_i64(FILE *f) { int64_t v = 0;  fread(&v, 8, 1, f); return v; }
static double   read_f64(FILE *f) { double v = 0;   fread(&v, 8, 1, f); return v; }
static char *read_str(FILE *f) {
    uint16_t len = read_u16(f);
    if (!len) return NULL;
    char *s = xs_malloc(len + 1);
    fread(s, 1, len, f);
    s[len] = 0;
    return s;
}

static void proto_write(FILE *f, XSProto *p) {
    write_str(f, p->name);
    write_u16(f, (uint16_t)p->arity);
    write_u16(f, (uint16_t)p->nlocals);
    write_u16(f, (uint16_t)p->n_upvalues);
    /* code */
    write_u32(f, (uint32_t)p->chunk.len);
    for (int i = 0; i < p->chunk.len; i++)
        write_u32(f, p->chunk.code[i]);
    /* constants */
    write_u16(f, (uint16_t)p->chunk.nconsts);
    for (int i = 0; i < p->chunk.nconsts; i++) {
        Value *v = p->chunk.consts[i];
        if (!v || v->tag == XS_NULL) { write_u8(f, 0); }
        else if (v->tag == XS_INT)   { write_u8(f, 1); write_i64(f, v->i); }
        else if (v->tag == XS_FLOAT) { write_u8(f, 2); write_f64(f, v->f); }
        else if (v->tag == XS_STR)   { write_u8(f, 3); write_str(f, v->s); }
        else if (v->tag == XS_BOOL)  { write_u8(f, 4); write_u8(f, v->i ? 1 : 0); }
        else { write_u8(f, 0); } /* unsupported const type → null */
    }
    /* upvalue descriptors */
    write_u16(f, (uint16_t)p->n_upvalues);
    for (int i = 0; i < p->n_upvalues; i++) {
        write_u8(f, (uint8_t)p->uv_descs[i].is_local);
        write_u16(f, (uint16_t)p->uv_descs[i].index);
    }
    /* inner protos */
    write_u16(f, (uint16_t)p->n_inner);
    for (int i = 0; i < p->n_inner; i++)
        proto_write(f, p->inner[i]);
}

int proto_write_file(XSProto *p, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite("XSC", 1, 4, f); /* includes null terminator */
    write_u16(f, 1); /* format version */
    proto_write(f, p);
    fclose(f);
    return 0;
}

static XSProto *proto_read(FILE *f) {
    char *name = read_str(f);
    int arity = read_u16(f);
    XSProto *p = proto_new(name, arity);
    free(name);
    p->nlocals = read_u16(f);
    p->n_upvalues = read_u16(f);
    /* code */
    int ncode = (int)read_u32(f);
    for (int i = 0; i < ncode; i++)
        chunk_write(&p->chunk, read_u32(f));
    /* constants */
    int nconsts = read_u16(f);
    for (int i = 0; i < nconsts; i++) {
        uint8_t tag = read_u8(f);
        Value *v = NULL;
        switch (tag) {
            case 0: v = xs_null(); break;
            case 1: v = xs_int(read_i64(f)); break;
            case 2: v = xs_float(read_f64(f)); break;
            case 3: { char *s = read_str(f); v = xs_str(s ? s : ""); free(s); break; }
            case 4: v = read_u8(f) ? xs_bool(1) : xs_bool(0); break;
            default: v = xs_null(); break;
        }
        chunk_add_const(&p->chunk, v);
        value_decref(v);
    }
    /* upvalue descriptors */
    int nuv = read_u16(f);
    if (nuv > 0) {
        p->uv_descs = xs_malloc(nuv * sizeof(UVDesc));
        for (int i = 0; i < nuv; i++) {
            p->uv_descs[i].is_local = read_u8(f);
            p->uv_descs[i].index = read_u16(f);
        }
    }
    /* inner protos */
    int ninner = read_u16(f);
    for (int i = 0; i < ninner; i++) {
        XSProto *inner = proto_read(f);
        if (p->n_inner >= p->cap_inner) {
            p->cap_inner = p->cap_inner ? p->cap_inner * 2 : 4;
            p->inner = xs_realloc(p->inner, p->cap_inner * sizeof(XSProto*));
        }
        p->inner[p->n_inner++] = inner;
    }
    return p;
}

XSProto *proto_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[4] = {0};
    fread(magic, 1, 4, f);
    if (memcmp(magic, "XSC", 4) != 0) { fclose(f); return NULL; }
    uint16_t ver = read_u16(f);
    if (ver != 1) { fclose(f); return NULL; }
    XSProto *p = proto_read(f);
    fclose(f);
    return p;
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
