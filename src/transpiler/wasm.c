#include "core/xs_compat.h"
#include "transpiler/wasm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern char *strdup(const char *);

/* ========================================================================
   WASM opcodes
   ======================================================================== */

#define OP_UNREACHABLE   0x00
#define OP_NOP           0x01
#define OP_BLOCK         0x02
#define OP_LOOP          0x03
#define OP_IF            0x04
#define OP_ELSE          0x05
#define OP_END           0x0B
#define OP_BR            0x0C
#define OP_BR_IF         0x0D
#define OP_BR_TABLE      0x0E
#define OP_RETURN        0x0F
#define OP_CALL          0x10
#define OP_CALL_INDIRECT 0x11
#define OP_DROP          0x1A
#define OP_SELECT        0x1B
#define OP_LOCAL_GET     0x20
#define OP_LOCAL_SET     0x21
#define OP_LOCAL_TEE     0x22
#define OP_GLOBAL_GET    0x23
#define OP_GLOBAL_SET    0x24
#define OP_I32_LOAD      0x28
#define OP_I64_LOAD      0x29
#define OP_F64_LOAD      0x2B
#define OP_I32_LOAD8_S   0x2C
#define OP_I32_LOAD8_U   0x2D
#define OP_I32_STORE     0x36
#define OP_I64_STORE     0x37
#define OP_F64_STORE     0x39
#define OP_I32_STORE8    0x3A
#define OP_MEMORY_SIZE   0x3F
#define OP_MEMORY_GROW   0x40
#define OP_I32_CONST     0x41
#define OP_I64_CONST     0x42
#define OP_F64_CONST     0x44
#define OP_I32_EQZ       0x45
#define OP_I32_EQ        0x46
#define OP_I32_NE        0x47
#define OP_I32_LT_S      0x48
#define OP_I32_LT_U      0x49
#define OP_I32_GT_S      0x4A
#define OP_I32_GT_U      0x4B
#define OP_I32_LE_S      0x4C
#define OP_I32_LE_U      0x4D
#define OP_I32_GE_S      0x4E
#define OP_I32_GE_U      0x4F
#define OP_F64_EQ        0x61
#define OP_F64_NE        0x62
#define OP_F64_LT        0x63
#define OP_F64_GT        0x64
#define OP_F64_LE        0x65
#define OP_F64_GE        0x66
#define OP_I32_CLZ       0x67
#define OP_I32_ADD       0x6A
#define OP_I32_SUB       0x6B
#define OP_I32_MUL       0x6C
#define OP_I32_DIV_S     0x6D
#define OP_I32_DIV_U     0x6E
#define OP_I32_REM_S     0x6F
#define OP_I32_REM_U     0x70
#define OP_I32_AND       0x71
#define OP_I32_OR        0x72
#define OP_I32_XOR       0x73
#define OP_I32_SHL       0x74
#define OP_I32_SHR_S     0x75
#define OP_I32_SHR_U     0x76
#define OP_F64_ABS       0x99
#define OP_F64_NEG       0x9A
#define OP_F64_ADD       0xA0
#define OP_F64_SUB       0xA1
#define OP_F64_MUL       0xA2
#define OP_F64_DIV       0xA3
#define OP_I32_TRUNC_F64_S 0xAA
#define OP_F64_CONVERT_I32_S 0xB7
#define OP_I32_REINTERPRET_F32 0xBC
#define OP_F32_REINTERPRET_I32 0xBE

/* WASM type codes */
#define WASM_TYPE_I32    0x7F
#define WASM_TYPE_I64    0x7E
#define WASM_TYPE_F32    0x7D
#define WASM_TYPE_F64    0x7C
#define WASM_TYPE_VOID   0x40

/* ========================================================================
   Dynamic buffer (WasmBuf)
   ======================================================================== */

typedef struct {
    uint8_t *data;
    int len;
    int cap;
} WasmBuf;

static void buf_init(WasmBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_free(WasmBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static void buf_ensure(WasmBuf *b, int need) {
    if (b->len + need > b->cap) {
        int newcap = b->cap < 256 ? 256 : b->cap * 2;
        while (newcap < b->len + need) newcap *= 2;
        unsigned char *tmp = realloc(b->data, (size_t)newcap);
        if (!tmp) return;
        b->data = tmp;
        b->cap = newcap;
    }
}

static void buf_byte(WasmBuf *b, uint8_t v) {
    buf_ensure(b, 1);
    b->data[b->len++] = v;
}

static void buf_bytes(WasmBuf *b, const uint8_t *src, int n) {
    buf_ensure(b, n);
    memcpy(b->data + b->len, src, (size_t)n);
    b->len += n;
}

static void buf_append(WasmBuf *dst, WasmBuf *src) {
    buf_bytes(dst, src->data, src->len);
}

static void buf_leb128_u(WasmBuf *b, uint32_t val) {
    do {
        uint8_t byte = val & 0x7F;
        val >>= 7;
        if (val) byte |= 0x80;
        buf_byte(b, byte);
    } while (val);
}

static void buf_leb128_s(WasmBuf *b, int32_t val) {
    int more = 1;
    while (more) {
        uint8_t byte = (uint8_t)(val & 0x7F);
        val >>= 7;
        if ((val == 0 && !(byte & 0x40)) || (val == -1 && (byte & 0x40)))
            more = 0;
        else
            byte |= 0x80;
        buf_byte(b, byte);
    }
}

static void buf_section(WasmBuf *out, uint8_t id, WasmBuf *content) {
    buf_byte(out, id);
    buf_leb128_u(out, (uint32_t)content->len);
    buf_append(out, content);
}

static void buf_name(WasmBuf *b, const char *s) {
    uint32_t len = (uint32_t)strlen(s);
    buf_leb128_u(b, len);
    buf_bytes(b, (const uint8_t *)s, (int)len);
}

static void buf_f64(WasmBuf *b, double val) {
    uint8_t raw[8];
    memcpy(raw, &val, 8);
    buf_bytes(b, raw, 8);
}

/* ========================================================================
   Value tags for the dynamic type system
   All runtime values are 12-byte cells in linear memory:
     [0..3] tag  [4..7] payload  [8..11] extra
   ======================================================================== */

#define TAG_NULL    0
#define TAG_BOOL    1
#define TAG_INT     2
#define TAG_FLOAT   3
#define TAG_STRING  4
#define TAG_ARRAY   5
#define TAG_MAP     6
#define TAG_FUNC    7
#define TAG_STRUCT  8
#define TAG_CLASS   9
#define TAG_TUPLE   10
#define TAG_RANGE   11

/* Size of a value cell */
#define VAL_SIZE 16

/* ========================================================================
   String table for data segment
   ======================================================================== */

#define MAX_STRINGS 4096

typedef struct {
    const char *strs[MAX_STRINGS];
    int         offsets[MAX_STRINGS];
    int         lengths[MAX_STRINGS];
    int         count;
    int         total_len;
} StringTable;

static void strtab_init(StringTable *st) {
    st->count = 0;
    st->total_len = 0;
}

static int strtab_add(StringTable *st, const char *s) {
    /* Deduplicate: if this string is already in the table, return its offset */
    int len = (int)strlen(s);
    for (int i = 0; i < st->count; i++) {
        if (st->lengths[i] == len && memcmp(st->strs[i], s, (size_t)len) == 0)
            return st->offsets[i];
    }
    if (st->count >= MAX_STRINGS) return 0;
    int off = st->total_len;
    st->strs[st->count] = s;
    st->offsets[st->count] = off;
    st->lengths[st->count] = len;
    st->count++;
    st->total_len += len + 1;
    return off;
}

static int strtab_add_with_len(StringTable *st, const char *s, int *out_len) {
    int len = (int)strlen(s);
    /* Deduplicate */
    for (int i = 0; i < st->count; i++) {
        if (st->lengths[i] == len && memcmp(st->strs[i], s, (size_t)len) == 0) {
            *out_len = len;
            return st->offsets[i];
        }
    }
    if (st->count >= MAX_STRINGS) { *out_len = 0; return 0; }
    int off = st->total_len;
    st->strs[st->count] = s;
    st->offsets[st->count] = off;
    st->lengths[st->count] = len;
    st->count++;
    st->total_len += len + 1;
    *out_len = len;
    return off;
}

/* ========================================================================
   Global indices
   ======================================================================== */

#define GLOBAL_HEAP_PTR  0   /* bump allocator pointer (mutable i32) */
#define GLOBAL_ERR_FLAG  1   /* error flag for try/catch (mutable i32) */
#define GLOBAL_ERR_VAL   2   /* error value pointer (mutable i32) */
#define NUM_GLOBALS      3

/* ========================================================================
   Runtime function indices (built into the module, not imported)
   ======================================================================== */

/* Single WASI import: fd_write */
#define IMPORT_FD_WRITE  0
#define NUM_IMPORTS      1

/* Runtime function indices (base = NUM_IMPORTS) */
#define RT_ALLOC         (NUM_IMPORTS + 0)
#define RT_VAL_NEW       (NUM_IMPORTS + 1)
#define RT_VAL_TAG       (NUM_IMPORTS + 2)
#define RT_VAL_I32       (NUM_IMPORTS + 3)
#define RT_VAL_F64_BITS  (NUM_IMPORTS + 4)
#define RT_STR_NEW       (NUM_IMPORTS + 5)
#define RT_STR_CAT       (NUM_IMPORTS + 6)
#define RT_ARR_NEW       (NUM_IMPORTS + 7)
#define RT_ARR_PUSH      (NUM_IMPORTS + 8)
#define RT_ARR_GET       (NUM_IMPORTS + 9)
#define RT_ARR_LEN       (NUM_IMPORTS + 10)
#define RT_PRINT_VAL     (NUM_IMPORTS + 11)
#define RT_VAL_TRUTHY    (NUM_IMPORTS + 12)
#define RT_VAL_EQ        (NUM_IMPORTS + 13)
#define RT_VAL_ADD       (NUM_IMPORTS + 14)
#define RT_VAL_SUB       (NUM_IMPORTS + 15)
#define RT_VAL_MUL       (NUM_IMPORTS + 16)
#define RT_VAL_DIV       (NUM_IMPORTS + 17)
#define RT_VAL_MOD       (NUM_IMPORTS + 18)
#define RT_VAL_LT        (NUM_IMPORTS + 19)
#define RT_VAL_GT        (NUM_IMPORTS + 20)
#define RT_VAL_LE        (NUM_IMPORTS + 21)
#define RT_VAL_GE        (NUM_IMPORTS + 22)
#define RT_VAL_NEG       (NUM_IMPORTS + 23)
#define RT_VAL_NOT       (NUM_IMPORTS + 24)
#define RT_VAL_TO_STR    (NUM_IMPORTS + 25)
#define RT_MAP_NEW       (NUM_IMPORTS + 26)
#define RT_MAP_SET       (NUM_IMPORTS + 27)
#define RT_MAP_GET       (NUM_IMPORTS + 28)
#define RT_VAL_INDEX     (NUM_IMPORTS + 29)
#define RT_VAL_INDEX_SET (NUM_IMPORTS + 30)
#define RT_VAL_FIELD     (NUM_IMPORTS + 31)
#define RT_VAL_FIELD_SET (NUM_IMPORTS + 32)
#define RT_STRUCT_NEW    (NUM_IMPORTS + 33)
#define RT_PRINT_NEWLINE (NUM_IMPORTS + 34)
#define RT_VAL_AND       (NUM_IMPORTS + 35)
#define RT_VAL_OR        (NUM_IMPORTS + 36)
#define RT_VAL_BIT_AND   (NUM_IMPORTS + 37)
#define RT_VAL_BIT_OR    (NUM_IMPORTS + 38)
#define RT_VAL_BIT_XOR   (NUM_IMPORTS + 39)
#define RT_VAL_SHL       (NUM_IMPORTS + 40)
#define RT_VAL_SHR       (NUM_IMPORTS + 41)
#define RT_VAL_POW       (NUM_IMPORTS + 42)
#define RT_RANGE_NEW     (NUM_IMPORTS + 43)
#define RT_VAL_NE        (NUM_IMPORTS + 44)
#define RT_VAL_INTDIV    (NUM_IMPORTS + 45)
#define RT_VAL_BIT_NOT   (NUM_IMPORTS + 46)
#define RT_TUPLE_NEW     (NUM_IMPORTS + 47)
#define RT_VAL_NULLCOAL  (NUM_IMPORTS + 48)
#define RT_I32_TO_STR    (NUM_IMPORTS + 49)
#define RT_STR_LEN       (NUM_IMPORTS + 50)

#define NUM_RT_FUNCS     51
#define USER_FUNC_BASE   (NUM_IMPORTS + NUM_RT_FUNCS)

/* ========================================================================
   Struct field layout tracker
   ======================================================================== */

#define MAX_STRUCTS      256
#define MAX_STRUCT_FIELDS 64

typedef struct {
    char *name;
    char *fields[MAX_STRUCT_FIELDS];
    int n_fields;
    int name_str_offset;  /* offset into data segment */
    int field_str_offsets[MAX_STRUCT_FIELDS];
} StructLayout;

typedef struct {
    StructLayout layouts[MAX_STRUCTS];
    int count;
} StructLayoutMap;

static void struct_layouts_init(StructLayoutMap *m) { m->count = 0; }

static void struct_layouts_free(StructLayoutMap *m) {
    for (int i = 0; i < m->count; i++) {
        free(m->layouts[i].name);
        for (int j = 0; j < m->layouts[i].n_fields; j++)
            free(m->layouts[i].fields[j]);
    }
    m->count = 0;
}

static int struct_layouts_find(StructLayoutMap *m, const char *name) {
    for (int i = 0; i < m->count; i++) {
        if (strcmp(m->layouts[i].name, name) == 0) return i;
    }
    return -1;
}

static void struct_layouts_add(StructLayoutMap *m, const char *name,
                                NodePairList *fields, StringTable *strtab) {
    if (m->count >= MAX_STRUCTS) return;
    StructLayout *sl = &m->layouts[m->count++];
    sl->name = strdup(name);
    sl->name_str_offset = strtab_add(strtab, sl->name);
    sl->n_fields = 0;
    for (int i = 0; i < fields->len && i < MAX_STRUCT_FIELDS; i++) {
        sl->fields[sl->n_fields] = strdup(fields->items[i].key);
        sl->field_str_offsets[sl->n_fields] = strtab_add(strtab, sl->fields[sl->n_fields]);
        sl->n_fields++;
    }
}

static int struct_field_index(StructLayoutMap *m, const char *struct_name,
                              const char *field_name) {
    for (int i = 0; i < m->count; i++) {
        if (struct_name && strcmp(m->layouts[i].name, struct_name) != 0) continue;
        for (int j = 0; j < m->layouts[i].n_fields; j++) {
            if (strcmp(m->layouts[i].fields[j], field_name) == 0)
                return j;
        }
    }
    return -1;
}

/* ========================================================================
   Defer list
   ======================================================================== */

#define MAX_DEFERS 64

typedef struct {
    Node *stmts[MAX_DEFERS];
    int count;
} DeferList;

static void defer_list_init(DeferList *d) { d->count = 0; }

static void defer_list_push(DeferList *d, Node *stmt) {
    if (d->count < MAX_DEFERS) d->stmts[d->count++] = stmt;
}

/* ========================================================================
   Local variable tracker
   ======================================================================== */

#define MAX_LOCALS 512

typedef struct {
    char *names[MAX_LOCALS];
    int n_locals;
} LocalMap;

static void locals_init(LocalMap *l) {
    l->n_locals = 0;
}

static int locals_find(LocalMap *l, const char *name) {
    for (int i = 0; i < l->n_locals; i++) {
        if (l->names[i] && strcmp(l->names[i], name) == 0) return i;
    }
    return -1;
}

static int locals_add(LocalMap *l, const char *name) {
    if (l->n_locals >= MAX_LOCALS) return -1;
    int idx = l->n_locals;
    l->names[idx] = strdup(name);
    l->n_locals++;
    return idx;
}

static int locals_ensure(LocalMap *l, const char *name) {
    int idx = locals_find(l, name);
    if (idx >= 0) return idx;
    return locals_add(l, name);
}

static void locals_free(LocalMap *l) {
    for (int i = 0; i < l->n_locals; i++) free(l->names[i]);
    l->n_locals = 0;
}

/* ========================================================================
   Function table (maps names to indices)
   ======================================================================== */

#define MAX_FUNCS 512

typedef struct {
    char *names[MAX_FUNCS];
    int n_funcs;
} FuncMap;

static void funcs_init(FuncMap *f) { f->n_funcs = 0; }

static int funcs_find(FuncMap *f, const char *name) {
    for (int i = 0; i < f->n_funcs; i++)
        if (f->names[i] && strcmp(f->names[i], name) == 0) return i;
    return -1;
}

static int funcs_add(FuncMap *f, const char *name) {
    if (f->n_funcs >= MAX_FUNCS) return -1;
    int idx = f->n_funcs;
    f->names[idx] = strdup(name);
    f->n_funcs++;
    return idx;
}

static void funcs_free(FuncMap *f) {
    for (int i = 0; i < f->n_funcs; i++) free(f->names[i]);
    f->n_funcs = 0;
}

/* ========================================================================
   Enum layout tracker
   ======================================================================== */

#define MAX_ENUMS 128
#define MAX_ENUM_VARIANTS 64

typedef struct {
    char *name;
    char *variants[MAX_ENUM_VARIANTS];
    int n_variants;
} EnumLayout;

typedef struct {
    EnumLayout layouts[MAX_ENUMS];
    int count;
} EnumLayoutMap;

static void enum_layouts_init(EnumLayoutMap *m) { m->count = 0; }

static void enum_layouts_free(EnumLayoutMap *m) {
    for (int i = 0; i < m->count; i++) {
        free(m->layouts[i].name);
        for (int j = 0; j < m->layouts[i].n_variants; j++)
            free(m->layouts[i].variants[j]);
    }
    m->count = 0;
}

static void enum_layouts_add(EnumLayoutMap *m, const char *name,
                              EnumVariantList *variants) {
    if (m->count >= MAX_ENUMS) return;
    EnumLayout *el = &m->layouts[m->count++];
    el->name = strdup(name);
    el->n_variants = 0;
    for (int i = 0; i < variants->len && i < MAX_ENUM_VARIANTS; i++) {
        el->variants[el->n_variants++] = strdup(variants->items[i].name);
    }
}

static int enum_variant_tag(EnumLayoutMap *m, const char *path) {
    /* path is like "Color::Red" or just "Red" */
    const char *sep = strstr(path, "::");
    const char *vname = sep ? sep + 2 : path;
    const char *ename = NULL;
    char ename_buf[256];
    if (sep) {
        int len = (int)(sep - path);
        if (len > 255) len = 255;
        memcpy(ename_buf, path, (size_t)len);
        ename_buf[len] = 0;
        ename = ename_buf;
    }
    for (int i = 0; i < m->count; i++) {
        if (ename && strcmp(m->layouts[i].name, ename) != 0) continue;
        for (int j = 0; j < m->layouts[i].n_variants; j++) {
            if (strcmp(m->layouts[i].variants[j], vname) == 0)
                return j;
        }
    }
    return -1;
}

/* ========================================================================
   Compiler context (avoids globals, passed around)
   ======================================================================== */

typedef struct {
    FuncMap         *funcs;
    StringTable     *strtab;
    StructLayoutMap *structs;
    EnumLayoutMap   *enums;
    DeferList        defers;
    int              loop_depth;    /* nesting depth for break/continue */
    int              in_loop;       /* whether we are inside a loop */
    int              break_depth;   /* br depth for break (from body) */
    int              continue_depth; /* br depth for continue (from body) */
} CompilerCtx;

/* ========================================================================
   Forward declarations
   ======================================================================== */

static void compile_expr(Node *node, WasmBuf *code, LocalMap *locals, CompilerCtx *ctx);
static void compile_stmt(Node *node, WasmBuf *code, LocalMap *locals, CompilerCtx *ctx);
static int arity_to_type(int arity);
static void compile_block(Node *block, WasmBuf *code, LocalMap *locals, CompilerCtx *ctx);
static void compile_pattern_cond(Node *pat, int subject_local, WasmBuf *code,
                                  LocalMap *locals, CompilerCtx *ctx);
static void compile_pattern_bindings(Node *pat, int subject_local, WasmBuf *code,
                                      LocalMap *locals, CompilerCtx *ctx);

/* ========================================================================
   Helpers: emit common patterns
   ======================================================================== */

/* Emit: push an i32 constant */
static void emit_i32(WasmBuf *code, int32_t val) {
    buf_byte(code, OP_I32_CONST);
    buf_leb128_s(code, val);
}

/* Emit: call a function by absolute index */
static void emit_call(WasmBuf *code, int func_idx) {
    buf_byte(code, OP_CALL);
    buf_leb128_u(code, (uint32_t)func_idx);
}

/* Emit: local.get */
static void emit_local_get(WasmBuf *code, int idx) {
    buf_byte(code, OP_LOCAL_GET);
    buf_leb128_u(code, (uint32_t)idx);
}

/* Emit: local.set */
static void emit_local_set(WasmBuf *code, int idx) {
    buf_byte(code, OP_LOCAL_SET);
    buf_leb128_u(code, (uint32_t)idx);
}

/* Emit: local.tee */
static void emit_local_tee(WasmBuf *code, int idx) {
    buf_byte(code, OP_LOCAL_TEE);
    buf_leb128_u(code, (uint32_t)idx);
}

/* Emit: global.get */
static void emit_global_get(WasmBuf *code, int idx) {
    buf_byte(code, OP_GLOBAL_GET);
    buf_leb128_u(code, (uint32_t)idx);
}

/* Emit: global.set */
static void emit_global_set(WasmBuf *code, int idx) {
    buf_byte(code, OP_GLOBAL_SET);
    buf_leb128_u(code, (uint32_t)idx);
}

/* Emit: create a new runtime value with tag and i32 payload.
   Leaves the value pointer on the stack. */
static void emit_val_new(WasmBuf *code, int tag, int32_t payload) {
    emit_i32(code, tag);
    emit_i32(code, payload);
    emit_call(code, RT_VAL_NEW);
}

/* Emit: create a null value */
static void emit_null(WasmBuf *code) {
    emit_val_new(code, TAG_NULL, 0);
}

/* Emit: create a bool value */
static void emit_bool_val(WasmBuf *code, int bval) {
    emit_val_new(code, TAG_BOOL, bval ? 1 : 0);
}

/* Emit: create an int value */
static void emit_int_val(WasmBuf *code, int32_t ival) {
    emit_i32(code, TAG_INT);
    emit_i32(code, ival);
    emit_call(code, RT_VAL_NEW);
}

/* Emit: create a string value from data segment offset + length */
static void emit_str_val(WasmBuf *code, int offset, int len) {
    emit_i32(code, offset);
    emit_i32(code, len);
    emit_call(code, RT_STR_NEW);
}

/* Emit: get the tag of a value on top of stack */
static void emit_val_tag(WasmBuf *code) {
    emit_call(code, RT_VAL_TAG);
}

/* Emit: get i32 payload of a value on top of stack */
static void emit_val_i32(WasmBuf *code) {
    emit_call(code, RT_VAL_I32);
}

/* Emit: compile deferred stmts in LIFO order */
static void emit_defers(WasmBuf *code, LocalMap *locals, CompilerCtx *ctx) {
    for (int i = ctx->defers.count - 1; i >= 0; i--) {
        compile_stmt(ctx->defers.stmts[i], code, locals, ctx);
    }
}

/* ========================================================================
   compile_block
   ======================================================================== */

static void compile_block(Node *block, WasmBuf *code, LocalMap *locals, CompilerCtx *ctx) {
    if (!block) return;
    if (block->tag == NODE_BLOCK) {
        for (int i = 0; i < block->block.stmts.len; i++)
            compile_stmt(block->block.stmts.items[i], code, locals, ctx);
        /* Handle trailing expression (block as expression) */
        if (block->block.expr) {
            compile_expr(block->block.expr, code, locals, ctx);
            buf_byte(code, OP_DROP); /* discard expression result in statement context */
        }
    } else {
        compile_stmt(block, code, locals, ctx);
    }
}

/* Compile a block as an expression (returns a value pointer) */
static void compile_block_expr(Node *block, WasmBuf *code, LocalMap *locals, CompilerCtx *ctx) {
    if (!block) { emit_null(code); return; }
    if (block->tag == NODE_BLOCK) {
        for (int i = 0; i < block->block.stmts.len; i++)
            compile_stmt(block->block.stmts.items[i], code, locals, ctx);
        if (block->block.expr)
            compile_expr(block->block.expr, code, locals, ctx);
        else
            emit_null(code);
    } else {
        compile_expr(block, code, locals, ctx);
    }
}

/* ========================================================================
   compile_expr - handle every expression node type
   ======================================================================== */

static void compile_expr(Node *node, WasmBuf *code, LocalMap *locals, CompilerCtx *ctx) {
    if (!node) {
        emit_null(code);
        return;
    }

    switch (node->tag) {

    /* ---- Literals ---- */

    case NODE_LIT_INT:
        emit_int_val(code, (int32_t)node->lit_int.ival);
        break;

    case NODE_LIT_BIGINT: {
        /* truncate to i32 */
        int32_t v = (int32_t)strtol(node->lit_bigint.bigint_str, NULL, 10);
        emit_int_val(code, v);
        break;
    }

    case NODE_LIT_FLOAT: {
        /* Store float: payload = integer part (for arithmetic),
           extra = string table offset (for printing).
           We pre-format the float as a string at compile time. */
        double fval = node->lit_float.fval;
        int32_t ipart = (int32_t)fval;
        char fbuf[64];
        snprintf(fbuf, sizeof(fbuf), "%g", fval);
        int slen = 0;
        int soff = strtab_add_with_len(ctx->strtab, fbuf, &slen);
        emit_i32(code, TAG_FLOAT);
        emit_i32(code, ipart);
        emit_call(code, RT_VAL_NEW);
        /* Store string offset in extra field for val_to_str */
        {
            int tmp = locals_add(locals, "__ftmp");
            emit_local_tee(code, tmp);
            emit_i32(code, 8);
            buf_byte(code, OP_I32_ADD);
            emit_i32(code, soff);
            buf_byte(code, OP_I32_STORE);
            buf_leb128_u(code, 2);
            buf_leb128_u(code, 0);
            /* Also store length at extra+4 */
            emit_local_get(code, tmp);
            emit_i32(code, 12);
            buf_byte(code, OP_I32_ADD);
            emit_i32(code, slen);
            buf_byte(code, OP_I32_STORE);
            buf_leb128_u(code, 2);
            buf_leb128_u(code, 0);
            emit_local_get(code, tmp);
        }
        break;
    }

    case NODE_LIT_BOOL:
        emit_bool_val(code, node->lit_bool.bval);
        break;

    case NODE_LIT_NULL:
        emit_null(code);
        break;

    case NODE_LIT_CHAR: {
        /* char -> int value */
        emit_int_val(code, (int32_t)node->lit_char.cval);
        break;
    }

    case NODE_LIT_STRING: {
        const char *s = node->lit_string.sval ? node->lit_string.sval : "";
        int slen = 0;
        int off = strtab_add_with_len(ctx->strtab, s, &slen);
        emit_str_val(code, off, slen);
        break;
    }

    case NODE_INTERP_STRING: {
        /* Build by concatenating parts */
        NodeList *parts = &node->lit_string.parts;
        if (parts->len == 0) {
            emit_str_val(code, strtab_add(ctx->strtab, ""), 0);
            break;
        }
        /* compile first part */
        if (parts->items[0]->tag == NODE_LIT_STRING) {
            const char *s = parts->items[0]->lit_string.sval ? parts->items[0]->lit_string.sval : "";
            int slen = 0;
            int off = strtab_add_with_len(ctx->strtab, s, &slen);
            emit_str_val(code, off, slen);
        } else {
            compile_expr(parts->items[0], code, locals, ctx);
            emit_call(code, RT_VAL_TO_STR);
        }
        /* concatenate remaining parts */
        for (int i = 1; i < parts->len; i++) {
            if (parts->items[i]->tag == NODE_LIT_STRING) {
                const char *s = parts->items[i]->lit_string.sval ? parts->items[i]->lit_string.sval : "";
                int slen = 0;
                int off = strtab_add_with_len(ctx->strtab, s, &slen);
                emit_str_val(code, off, slen);
            } else {
                compile_expr(parts->items[i], code, locals, ctx);
                emit_call(code, RT_VAL_TO_STR);
            }
            emit_call(code, RT_STR_CAT);
        }
        break;
    }

    case NODE_LIT_ARRAY: {
        int n = node->lit_array.elems.len;
        emit_call(code, RT_ARR_NEW);  /* -> arr_ptr */
        int arr_tmp = locals_add(locals, "__arr");
        emit_local_set(code, arr_tmp);
        for (int i = 0; i < n; i++) {
            emit_local_get(code, arr_tmp);
            compile_expr(node->lit_array.elems.items[i], code, locals, ctx);
            emit_call(code, RT_ARR_PUSH);
        }
        emit_local_get(code, arr_tmp);
        break;
    }

    case NODE_LIT_TUPLE: {
        int n = node->lit_array.elems.len;
        /* Build as array, then retag as tuple */
        emit_call(code, RT_ARR_NEW);
        int arr_tmp = locals_add(locals, "__tup");
        emit_local_set(code, arr_tmp);
        for (int i = 0; i < n; i++) {
            emit_local_get(code, arr_tmp);
            compile_expr(node->lit_array.elems.items[i], code, locals, ctx);
            emit_call(code, RT_ARR_PUSH);
        }
        /* retag: arr value cell tag -> TAG_TUPLE */
        emit_local_get(code, arr_tmp);
        emit_call(code, RT_VAL_I32); /* get the underlying array data ptr */
        emit_i32(code, TAG_TUPLE);
        buf_byte(code, OP_I32_ADD); /* placeholder - actually we call tuple_new */
        buf_byte(code, OP_DROP);
        /* Simpler: just call tuple_new with the arr */
        emit_local_get(code, arr_tmp);
        emit_call(code, RT_TUPLE_NEW);
        break;
    }

    case NODE_LIT_MAP: {
        emit_call(code, RT_MAP_NEW);
        int map_tmp = locals_add(locals, "__map");
        emit_local_set(code, map_tmp);
        for (int i = 0; i < node->lit_map.keys.len; i++) {
            emit_local_get(code, map_tmp);
            compile_expr(node->lit_map.keys.items[i], code, locals, ctx);
            compile_expr(node->lit_map.vals.items[i], code, locals, ctx);
            emit_call(code, RT_MAP_SET);
        }
        emit_local_get(code, map_tmp);
        break;
    }

    case NODE_LIT_REGEX: {
        /* Store regex pattern as a string value */
        const char *p = node->lit_regex.pattern ? node->lit_regex.pattern : "";
        int slen = 0;
        int off = strtab_add_with_len(ctx->strtab, p, &slen);
        emit_str_val(code, off, slen);
        break;
    }

    /* ---- Identifiers ---- */

    case NODE_IDENT: {
        int idx = locals_find(locals, node->ident.name);
        if (idx >= 0) {
            emit_local_get(code, idx);
        } else {
            /* Check if it is a known function name */
            int fidx = funcs_find(ctx->funcs, node->ident.name);
            if (fidx >= 0) {
                /* Wrap function index as a func value */
                emit_val_new(code, TAG_FUNC, NUM_RT_FUNCS + fidx);
            } else {
                /* Unknown variable - return null */
                emit_null(code);
            }
        }
        break;
    }

    /* ---- Binary operators ---- */

    case NODE_BINOP: {
        const char *op = node->binop.op;

        /* Short-circuit: and/&&, or/|| - inlined for correctness */
        if (strcmp(op, "and") == 0 || strcmp(op, "&&") == 0) {
            int tmp_a = locals_add(locals, "__and_a");
            compile_expr(node->binop.left, code, locals, ctx);
            emit_local_set(code, tmp_a);
            emit_local_get(code, tmp_a);
            emit_call(code, RT_VAL_TRUTHY);
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_I32);
            compile_expr(node->binop.right, code, locals, ctx);
            buf_byte(code, OP_ELSE);
            emit_local_get(code, tmp_a);
            buf_byte(code, OP_END);
            break;
        }
        if (strcmp(op, "or") == 0 || strcmp(op, "||") == 0) {
            int tmp_a = locals_add(locals, "__or_a");
            compile_expr(node->binop.left, code, locals, ctx);
            emit_local_set(code, tmp_a);
            emit_local_get(code, tmp_a);
            emit_call(code, RT_VAL_TRUTHY);
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_I32);
            emit_local_get(code, tmp_a);
            buf_byte(code, OP_ELSE);
            compile_expr(node->binop.right, code, locals, ctx);
            buf_byte(code, OP_END);
            break;
        }

        /* Null coalescing */
        if (strcmp(op, "??") == 0) {
            compile_expr(node->binop.left, code, locals, ctx);
            compile_expr(node->binop.right, code, locals, ctx);
            emit_call(code, RT_VAL_NULLCOAL);
            break;
        }

        /* Compile both sides */
        compile_expr(node->binop.left, code, locals, ctx);
        compile_expr(node->binop.right, code, locals, ctx);

        /* Dispatch to runtime */
        if      (strcmp(op, "+")  == 0) emit_call(code, RT_VAL_ADD);
        else if (strcmp(op, "++") == 0) emit_call(code, RT_STR_CAT);
        else if (strcmp(op, "-")  == 0) emit_call(code, RT_VAL_SUB);
        else if (strcmp(op, "*")  == 0) emit_call(code, RT_VAL_MUL);
        else if (strcmp(op, "/")  == 0) emit_call(code, RT_VAL_DIV);
        else if (strcmp(op, "//") == 0) emit_call(code, RT_VAL_INTDIV);
        else if (strcmp(op, "%")  == 0) emit_call(code, RT_VAL_MOD);
        else if (strcmp(op, "**") == 0) emit_call(code, RT_VAL_POW);
        else if (strcmp(op, "==") == 0) emit_call(code, RT_VAL_EQ);
        else if (strcmp(op, "!=") == 0) emit_call(code, RT_VAL_NE);
        else if (strcmp(op, "<")  == 0) emit_call(code, RT_VAL_LT);
        else if (strcmp(op, ">")  == 0) emit_call(code, RT_VAL_GT);
        else if (strcmp(op, "<=") == 0) emit_call(code, RT_VAL_LE);
        else if (strcmp(op, ">=") == 0) emit_call(code, RT_VAL_GE);
        else if (strcmp(op, "&")  == 0) emit_call(code, RT_VAL_BIT_AND);
        else if (strcmp(op, "|")  == 0) emit_call(code, RT_VAL_BIT_OR);
        else if (strcmp(op, "^")  == 0) emit_call(code, RT_VAL_BIT_XOR);
        else if (strcmp(op, "<<") == 0) emit_call(code, RT_VAL_SHL);
        else if (strcmp(op, ">>") == 0) emit_call(code, RT_VAL_SHR);
        else {
            /* Unknown binary op - just return left (drop right) */
            buf_byte(code, OP_DROP);
        }
        break;
    }

    /* ---- Unary operators ---- */

    case NODE_UNARY: {
        const char *op = node->unary.op;
        compile_expr(node->unary.expr, code, locals, ctx);

        if (strcmp(op, "-") == 0) {
            emit_call(code, RT_VAL_NEG);
        } else if (strcmp(op, "not") == 0 || strcmp(op, "!") == 0) {
            emit_call(code, RT_VAL_NOT);
        } else if (strcmp(op, "~") == 0) {
            emit_call(code, RT_VAL_BIT_NOT);
        }
        /* postfix ++/-- handled as sugar in the parser, but if we see them: */
        /* just return the value as-is for unknown ops */
        break;
    }

    /* ---- Assignments (as expression, returns the assigned value) ---- */

    case NODE_ASSIGN: {
        if (node->assign.target && node->assign.target->tag == NODE_IDENT) {
            int idx = locals_ensure(locals, node->assign.target->ident.name);
            const char *op = node->assign.op;
            if (strcmp(op, "=") == 0) {
                compile_expr(node->assign.value, code, locals, ctx);
            } else if (strcmp(op, "+=") == 0) {
                emit_local_get(code, idx);
                compile_expr(node->assign.value, code, locals, ctx);
                emit_call(code, RT_VAL_ADD);
            } else if (strcmp(op, "-=") == 0) {
                emit_local_get(code, idx);
                compile_expr(node->assign.value, code, locals, ctx);
                emit_call(code, RT_VAL_SUB);
            } else if (strcmp(op, "*=") == 0) {
                emit_local_get(code, idx);
                compile_expr(node->assign.value, code, locals, ctx);
                emit_call(code, RT_VAL_MUL);
            } else if (strcmp(op, "/=") == 0) {
                emit_local_get(code, idx);
                compile_expr(node->assign.value, code, locals, ctx);
                emit_call(code, RT_VAL_DIV);
            } else if (strcmp(op, "%=") == 0) {
                emit_local_get(code, idx);
                compile_expr(node->assign.value, code, locals, ctx);
                emit_call(code, RT_VAL_MOD);
            } else if (strcmp(op, "++=") == 0) {
                emit_local_get(code, idx);
                compile_expr(node->assign.value, code, locals, ctx);
                emit_call(code, RT_STR_CAT);
            } else {
                compile_expr(node->assign.value, code, locals, ctx);
            }
            emit_local_tee(code, idx);
        } else if (node->assign.target && node->assign.target->tag == NODE_INDEX) {
            /* array/map index assignment: obj[idx] = val */
            compile_expr(node->assign.target->index.obj, code, locals, ctx);
            compile_expr(node->assign.target->index.index, code, locals, ctx);
            compile_expr(node->assign.value, code, locals, ctx);
            emit_call(code, RT_VAL_INDEX_SET);
            /* return the value */
            compile_expr(node->assign.value, code, locals, ctx);
        } else if (node->assign.target && node->assign.target->tag == NODE_FIELD) {
            /* field assignment: obj.name = val */
            compile_expr(node->assign.target->field.obj, code, locals, ctx);
            const char *fname = node->assign.target->field.name;
            int slen = 0;
            int foff = strtab_add_with_len(ctx->strtab, fname, &slen);
            emit_str_val(code, foff, slen);
            compile_expr(node->assign.value, code, locals, ctx);
            emit_call(code, RT_VAL_FIELD_SET);
            compile_expr(node->assign.value, code, locals, ctx);
        } else {
            compile_expr(node->assign.value, code, locals, ctx);
        }
        break;
    }

    /* ---- Function calls ---- */

    case NODE_CALL: {
        Node *callee = node->call.callee;
        int nargs = node->call.args.len;

        /* Built-in functions */
        if (callee && callee->tag == NODE_IDENT) {
            const char *name = callee->ident.name;

            /* println(args...) */
            if (strcmp(name, "println") == 0) {
                for (int i = 0; i < nargs; i++) {
                    compile_expr(node->call.args.items[i], code, locals, ctx);
                    emit_call(code, RT_PRINT_VAL);
                    if (i < nargs - 1) {
                        int slen = 0;
                        int off = strtab_add_with_len(ctx->strtab, " ", &slen);
                        emit_str_val(code, off, slen);
                        emit_call(code, RT_PRINT_VAL);
                    }
                }
                emit_call(code, RT_PRINT_NEWLINE);
                emit_null(code);
                break;
            }
            if (strcmp(name, "print") == 0) {
                for (int i = 0; i < nargs; i++) {
                    compile_expr(node->call.args.items[i], code, locals, ctx);
                    emit_call(code, RT_PRINT_VAL);
                }
                emit_null(code);
                break;
            }
            if (strcmp(name, "str") == 0 && nargs >= 1) {
                compile_expr(node->call.args.items[0], code, locals, ctx);
                emit_call(code, RT_VAL_TO_STR);
                break;
            }
            if (strcmp(name, "int") == 0 && nargs >= 1) {
                /* Convert to int: get the i32 payload */
                compile_expr(node->call.args.items[0], code, locals, ctx);
                emit_call(code, RT_VAL_I32);
                emit_i32(code, TAG_INT);
                /* swap: we need (tag, payload) order for val_new */
                /* Easier: just dup and call */
                {
                    int tmp = locals_add(locals, "__cvt");
                    emit_local_set(code, tmp);
                    emit_i32(code, TAG_INT);
                    emit_local_get(code, tmp);
                    emit_call(code, RT_VAL_NEW);
                }
                break;
            }
            if (strcmp(name, "float") == 0 && nargs >= 1) {
                /* For now, float conversion is approximate */
                compile_expr(node->call.args.items[0], code, locals, ctx);
                break;
            }
            if (strcmp(name, "len") == 0 && nargs >= 1) {
                compile_expr(node->call.args.items[0], code, locals, ctx);
                emit_call(code, RT_STR_LEN);
                break;
            }
            if (strcmp(name, "push") == 0 && nargs >= 2) {
                compile_expr(node->call.args.items[0], code, locals, ctx);
                compile_expr(node->call.args.items[1], code, locals, ctx);
                emit_call(code, RT_ARR_PUSH);
                break;
            }
            if (strcmp(name, "type") == 0 && nargs >= 1) {
                compile_expr(node->call.args.items[0], code, locals, ctx);
                emit_call(code, RT_VAL_TAG);
                emit_call(code, RT_I32_TO_STR);
                break;
            }
            if (strcmp(name, "assert") == 0 && nargs >= 1) {
                compile_expr(node->call.args.items[0], code, locals, ctx);
                emit_call(code, RT_VAL_TRUTHY);
                buf_byte(code, OP_I32_EQZ);
                buf_byte(code, OP_IF);
                buf_byte(code, WASM_TYPE_VOID);
                buf_byte(code, OP_UNREACHABLE);
                buf_byte(code, OP_END);
                emit_null(code);
                break;
            }
            if (strcmp(name, "assert_eq") == 0 && nargs >= 2) {
                compile_expr(node->call.args.items[0], code, locals, ctx);
                compile_expr(node->call.args.items[1], code, locals, ctx);
                emit_call(code, RT_VAL_EQ);
                emit_call(code, RT_VAL_TRUTHY);
                buf_byte(code, OP_I32_EQZ);
                buf_byte(code, OP_IF);
                buf_byte(code, WASM_TYPE_VOID);
                buf_byte(code, OP_UNREACHABLE);
                buf_byte(code, OP_END);
                emit_null(code);
                break;
            }
            if (strcmp(name, "input") == 0) {
                /* No stdin in WASI minimal - return empty string */
                emit_str_val(code, strtab_add(ctx->strtab, ""), 0);
                break;
            }

            /* Check user-defined function */
            int fidx = funcs_find(ctx->funcs, name);
            if (fidx >= 0) {
                for (int i = 0; i < nargs; i++)
                    compile_expr(node->call.args.items[i], code, locals, ctx);
                /* Pad missing args with null */
                /* (We do not know arity at this point, caller is responsible) */
                emit_call(code, USER_FUNC_BASE + fidx);
                break;
            }

            /* Check if it is a struct constructor (capitalized name) */
            if (name[0] >= 'A' && name[0] <= 'Z') {
                int si = struct_layouts_find(ctx->structs, name);
                if (si >= 0) {
                    StructLayout *sl = &ctx->structs->layouts[si];
                    /* Create a map to hold fields */
                    emit_call(code, RT_MAP_NEW);
                    int stmp = locals_add(locals, "__sinit");
                    emit_local_set(code, stmp);
                    /* Set fields - kwargs first, then positional */
                    if (node->call.kwargs.len > 0) {
                        for (int k = 0; k < node->call.kwargs.len; k++) {
                            emit_local_get(code, stmp);
                            int fl = 0;
                            int foff = strtab_add_with_len(ctx->strtab,
                                                            node->call.kwargs.items[k].key, &fl);
                            emit_str_val(code, foff, fl);
                            compile_expr(node->call.kwargs.items[k].val, code, locals, ctx);
                            emit_call(code, RT_MAP_SET);
                        }
                    }
                    for (int a = 0; a < nargs && a < sl->n_fields; a++) {
                        emit_local_get(code, stmp);
                        int fl = 0;
                        int foff = strtab_add_with_len(ctx->strtab, sl->fields[a], &fl);
                        emit_str_val(code, foff, fl);
                        compile_expr(node->call.args.items[a], code, locals, ctx);
                        emit_call(code, RT_MAP_SET);
                    }
                    emit_local_get(code, stmp);
                    break;
                }
                /* Check if it is a class constructor - look for init method */
                {
                    char init_name[256];
                    snprintf(init_name, sizeof(init_name), "init");
                    int init_fidx = funcs_find(ctx->funcs, init_name);
                    if (init_fidx >= 0) {
                        /* Create a map as the instance */
                        emit_call(code, RT_MAP_NEW);
                        int ctmp = locals_add(locals, "__cinst");
                        emit_local_set(code, ctmp);
                        /* Call init(self, args...) */
                        emit_local_get(code, ctmp); /* self */
                        for (int i = 0; i < nargs; i++)
                            compile_expr(node->call.args.items[i], code, locals, ctx);
                        emit_call(code, USER_FUNC_BASE + init_fidx);
                        buf_byte(code, OP_DROP); /* drop init return value */
                        emit_local_get(code, ctmp); /* return instance */
                        break;
                    }
                }
            }

            /* Check if it is a local variable holding a func value */
            int local_idx = locals_find(locals, name);
            if (local_idx >= 0) {
                /* Indirect call through table */
                for (int i = 0; i < nargs; i++)
                    compile_expr(node->call.args.items[i], code, locals, ctx);
                /* Get the func table index from the value */
                emit_local_get(code, local_idx);
                emit_call(code, RT_VAL_I32);
                buf_byte(code, OP_CALL_INDIRECT);
                buf_leb128_u(code, (uint32_t)arity_to_type(nargs));
                buf_leb128_u(code, 0); /* table index */
                break;
            }

            /* Unknown function - return null */
            for (int i = 0; i < nargs; i++) {
                compile_expr(node->call.args.items[i], code, locals, ctx);
                buf_byte(code, OP_DROP);
            }
            emit_null(code);
            break;
        }

        /* Non-ident callee: lambda or expression call */
        for (int i = 0; i < nargs; i++)
            compile_expr(node->call.args.items[i], code, locals, ctx);
        compile_expr(callee, code, locals, ctx);
        emit_call(code, RT_VAL_I32); /* get table index */
        buf_byte(code, OP_CALL_INDIRECT);
        buf_leb128_u(code, (uint32_t)arity_to_type(nargs));
        buf_leb128_u(code, 0);
        break;
    }

    /* ---- Method calls ---- */

    case NODE_METHOD_CALL: {
        const char *method = node->method_call.method;
        int nargs = node->method_call.args.len;

        /* Check user-defined methods FIRST (impl methods, class methods) */
        {
            int fidx = funcs_find(ctx->funcs, method);
            if (fidx >= 0) {
                /* Call with obj as first arg (self) */
                compile_expr(node->method_call.obj, code, locals, ctx);
                for (int i = 0; i < nargs; i++)
                    compile_expr(node->method_call.args.items[i], code, locals, ctx);
                emit_call(code, USER_FUNC_BASE + fidx);
                break;
            }
        }

        /* Common array/string methods */
        if (strcmp(method, "push") == 0 && nargs == 1) {
            compile_expr(node->method_call.obj, code, locals, ctx);
            compile_expr(node->method_call.args.items[0], code, locals, ctx);
            emit_call(code, RT_ARR_PUSH);
            emit_null(code); /* push returns void, need a value for expr context */
            break;
        }
        if (strcmp(method, "pop") == 0) {
            /* Pop last element: get len-1, decrement len. Simplified: return null */
            compile_expr(node->method_call.obj, code, locals, ctx);
            buf_byte(code, OP_DROP);
            emit_null(code);
            break;
        }
        if (strcmp(method, "upper") == 0) {
            /* String upper: copy string, convert a-z to A-Z */
            compile_expr(node->method_call.obj, code, locals, ctx);
            int sv = locals_add(locals, "__ustr");
            emit_local_set(code, sv);
            /* Get data ptr and length */
            emit_local_get(code, sv);
            emit_call(code, RT_VAL_I32);
            int sp = locals_add(locals, "__usrc");
            emit_local_set(code, sp);
            emit_local_get(code, sv);
            emit_i32(code, 8);
            buf_byte(code, OP_I32_ADD);
            buf_byte(code, OP_I32_LOAD);
            buf_leb128_u(code, 2);
            buf_leb128_u(code, 0);
            int slen = locals_add(locals, "__ulen");
            emit_local_set(code, slen);
            /* Allocate new buffer */
            emit_local_get(code, slen);
            emit_i32(code, 1);
            buf_byte(code, OP_I32_ADD);
            emit_call(code, RT_ALLOC);
            int dp = locals_add(locals, "__udst");
            emit_local_set(code, dp);
            /* Loop: copy and convert */
            emit_i32(code, 0);
            int ui = locals_add(locals, "__ui");
            emit_local_set(code, ui);
            buf_byte(code, OP_BLOCK); buf_byte(code, WASM_TYPE_VOID);
            buf_byte(code, OP_LOOP); buf_byte(code, WASM_TYPE_VOID);
            emit_local_get(code, ui);
            emit_local_get(code, slen);
            buf_byte(code, OP_I32_GE_S);
            buf_byte(code, OP_BR_IF); buf_leb128_u(code, 1);
            /* Load byte */
            emit_local_get(code, sp);
            emit_local_get(code, ui);
            buf_byte(code, OP_I32_ADD);
            buf_byte(code, OP_I32_LOAD8_U);
            buf_leb128_u(code, 0);
            buf_leb128_u(code, 0);
            int ch = locals_add(locals, "__uch");
            emit_local_set(code, ch);
            /* If a-z, subtract 32 */
            emit_local_get(code, ch);
            emit_i32(code, 97); /* 'a' */
            buf_byte(code, OP_I32_GE_S);
            emit_local_get(code, ch);
            emit_i32(code, 122); /* 'z' */
            buf_byte(code, OP_I32_LE_S);
            buf_byte(code, OP_I32_AND);
            buf_byte(code, OP_IF); buf_byte(code, WASM_TYPE_VOID);
            emit_local_get(code, ch);
            emit_i32(code, 32);
            buf_byte(code, OP_I32_SUB);
            emit_local_set(code, ch);
            buf_byte(code, OP_END);
            /* Store byte */
            emit_local_get(code, dp);
            emit_local_get(code, ui);
            buf_byte(code, OP_I32_ADD);
            emit_local_get(code, ch);
            buf_byte(code, OP_I32_STORE8);
            buf_leb128_u(code, 0);
            buf_leb128_u(code, 0);
            /* i++ */
            emit_local_get(code, ui);
            emit_i32(code, 1);
            buf_byte(code, OP_I32_ADD);
            emit_local_set(code, ui);
            buf_byte(code, OP_BR); buf_leb128_u(code, 0);
            buf_byte(code, OP_END);
            buf_byte(code, OP_END);
            /* NUL terminate */
            emit_local_get(code, dp);
            emit_local_get(code, slen);
            buf_byte(code, OP_I32_ADD);
            emit_i32(code, 0);
            buf_byte(code, OP_I32_STORE8);
            buf_leb128_u(code, 0);
            buf_leb128_u(code, 0);
            /* Create string value */
            emit_local_get(code, dp);
            emit_local_get(code, slen);
            emit_call(code, RT_STR_NEW);
            break;
        }
        if (strcmp(method, "lower") == 0) {
            compile_expr(node->method_call.obj, code, locals, ctx);
            break;
        }
        if (strcmp(method, "trim") == 0) {
            compile_expr(node->method_call.obj, code, locals, ctx);
            break;
        }
        if (strcmp(method, "contains") == 0 && nargs == 1) {
            compile_expr(node->method_call.obj, code, locals, ctx);
            buf_byte(code, OP_DROP);
            compile_expr(node->method_call.args.items[0], code, locals, ctx);
            buf_byte(code, OP_DROP);
            emit_bool_val(code, 0); /* stub: always false */
            break;
        }
        if (strcmp(method, "split") == 0 && nargs == 1) {
            /* Stub: return array with the string itself */
            emit_call(code, RT_ARR_NEW);
            {
                int stmp = locals_add(locals, "__split_r");
                emit_local_set(code, stmp);
                emit_local_get(code, stmp);
                compile_expr(node->method_call.obj, code, locals, ctx);
                emit_call(code, RT_ARR_PUSH);
                compile_expr(node->method_call.args.items[0], code, locals, ctx);
                buf_byte(code, OP_DROP);
                emit_local_get(code, stmp);
            }
            break;
        }
        if (strcmp(method, "starts_with") == 0 && nargs == 1) {
            compile_expr(node->method_call.obj, code, locals, ctx);
            buf_byte(code, OP_DROP);
            compile_expr(node->method_call.args.items[0], code, locals, ctx);
            buf_byte(code, OP_DROP);
            emit_bool_val(code, 0);
            break;
        }
        if (strcmp(method, "ends_with") == 0 && nargs == 1) {
            compile_expr(node->method_call.obj, code, locals, ctx);
            buf_byte(code, OP_DROP);
            compile_expr(node->method_call.args.items[0], code, locals, ctx);
            buf_byte(code, OP_DROP);
            emit_bool_val(code, 0);
            break;
        }
        if (strcmp(method, "map") == 0 && nargs == 1) {
            /* arr.map(fn) - simplified: return empty array */
            compile_expr(node->method_call.obj, code, locals, ctx);
            buf_byte(code, OP_DROP);
            compile_expr(node->method_call.args.items[0], code, locals, ctx);
            buf_byte(code, OP_DROP);
            emit_call(code, RT_ARR_NEW);
            break;
        }
        if (strcmp(method, "filter") == 0 && nargs == 1) {
            compile_expr(node->method_call.obj, code, locals, ctx);
            buf_byte(code, OP_DROP);
            compile_expr(node->method_call.args.items[0], code, locals, ctx);
            buf_byte(code, OP_DROP);
            emit_call(code, RT_ARR_NEW);
            break;
        }
        if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0) {
            compile_expr(node->method_call.obj, code, locals, ctx);
            emit_call(code, RT_STR_LEN);
            break;
        }
        if (strcmp(method, "get") == 0 && nargs == 1) {
            compile_expr(node->method_call.obj, code, locals, ctx);
            compile_expr(node->method_call.args.items[0], code, locals, ctx);
            emit_call(code, RT_VAL_INDEX);
            break;
        }
        if (strcmp(method, "to_string") == 0 || strcmp(method, "str") == 0) {
            compile_expr(node->method_call.obj, code, locals, ctx);
            emit_call(code, RT_VAL_TO_STR);
            break;
        }

        /* Unknown method - compile obj but return null */
        compile_expr(node->method_call.obj, code, locals, ctx);
        buf_byte(code, OP_DROP);
        for (int i = 0; i < nargs; i++) {
            compile_expr(node->method_call.args.items[i], code, locals, ctx);
            buf_byte(code, OP_DROP);
        }
        emit_null(code);
        break;
    }

    /* ---- Index access ---- */

    case NODE_INDEX:
        compile_expr(node->index.obj, code, locals, ctx);
        compile_expr(node->index.index, code, locals, ctx);
        emit_call(code, RT_VAL_INDEX);
        break;

    /* ---- Field access ---- */

    case NODE_FIELD: {
        const char *fname = node->field.name;
        /* Special-case .len for strings and arrays */
        if (strcmp(fname, "len") == 0) {
            compile_expr(node->field.obj, code, locals, ctx);
            emit_call(code, RT_STR_LEN);
        } else {
            /* Try compile-time struct field index first */
            int fidx = struct_field_index(ctx->structs, NULL, fname);
            if (fidx >= 0) {
                compile_expr(node->field.obj, code, locals, ctx);
                emit_call(code, RT_VAL_I32); /* get data pointer */
                emit_i32(code, 4 + fidx * 4);
                buf_byte(code, OP_I32_ADD);
                buf_byte(code, OP_I32_LOAD);
                buf_leb128_u(code, 2);
                buf_leb128_u(code, 0);
            } else {
                /* Map-based field access for class instances and dynamic objects */
                compile_expr(node->field.obj, code, locals, ctx);
                int slen = 0;
                int foff = strtab_add_with_len(ctx->strtab, fname, &slen);
                emit_str_val(code, foff, slen);
                emit_call(code, RT_VAL_FIELD);
            }
        }
        break;
    }

    /* ---- Scope resolution A::B::C ---- */

    case NODE_SCOPE: {
        if (node->scope.nparts > 0) {
            /* Check if first part is an enum */
            char full[512] = {0};
            for (int i = 0; i < node->scope.nparts; i++) {
                if (i) strcat(full, "::");
                strcat(full, node->scope.parts[i]);
            }
            int vtag = enum_variant_tag(ctx->enums, full);
            if (vtag >= 0) {
                /* Store as string so it prints as "Color::Red" etc. */
                int slen = 0;
                int soff = strtab_add_with_len(ctx->strtab, full, &slen);
                emit_str_val(code, soff, slen);
            } else {
                /* Try as a regular identifier (last part) */
                int idx = locals_find(locals, node->scope.parts[node->scope.nparts - 1]);
                if (idx >= 0) {
                    emit_local_get(code, idx);
                } else {
                    int fidx = funcs_find(ctx->funcs, node->scope.parts[node->scope.nparts - 1]);
                    if (fidx >= 0) {
                        emit_val_new(code, TAG_FUNC, NUM_RT_FUNCS + fidx);
                    } else {
                        emit_null(code);
                    }
                }
            }
        } else {
            emit_null(code);
        }
        break;
    }

    /* ---- Range ---- */

    case NODE_RANGE: {
        compile_expr(node->range.start, code, locals, ctx);
        compile_expr(node->range.end, code, locals, ctx);
        emit_i32(code, node->range.inclusive ? 1 : 0);
        emit_call(code, RT_RANGE_NEW);
        break;
    }

    /* ---- If expression ---- */

    case NODE_IF: {
        ctx->break_depth++;
        ctx->continue_depth++;
        compile_expr(node->if_expr.cond, code, locals, ctx);
        emit_call(code, RT_VAL_TRUTHY);
        buf_byte(code, OP_IF);
        buf_byte(code, WASM_TYPE_I32); /* result: i32 (value ptr) */

        /* then branch */
        compile_block_expr(node->if_expr.then, code, locals, ctx);

        /* elif chains */
        for (int i = 0; i < node->if_expr.elif_conds.len; i++) {
            buf_byte(code, OP_ELSE);
            ctx->break_depth++;
            ctx->continue_depth++;
            compile_expr(node->if_expr.elif_conds.items[i], code, locals, ctx);
            emit_call(code, RT_VAL_TRUTHY);
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_I32);
            compile_block_expr(node->if_expr.elif_thens.items[i], code, locals, ctx);
        }

        /* else branch */
        buf_byte(code, OP_ELSE);
        if (node->if_expr.else_branch) {
            compile_block_expr(node->if_expr.else_branch, code, locals, ctx);
        } else {
            emit_null(code);
        }
        buf_byte(code, OP_END);

        /* Close elif chain ends */
        for (int i = 0; i < node->if_expr.elif_conds.len; i++) {
            buf_byte(code, OP_END);
            ctx->break_depth--;
            ctx->continue_depth--;
        }
        ctx->break_depth--;
        ctx->continue_depth--;
        break;
    }

    /* ---- Match expression ---- */

    case NODE_MATCH: {
        int subject_idx = locals_add(locals, "__match_sub");
        compile_expr(node->match.subject, code, locals, ctx);
        emit_local_set(code, subject_idx);

        for (int i = 0; i < node->match.arms.len; i++) {
            MatchArm *arm = &node->match.arms.items[i];
            compile_pattern_cond(arm->pattern, subject_idx, code, locals, ctx);
            if (arm->guard) {
                compile_expr(arm->guard, code, locals, ctx);
                emit_call(code, RT_VAL_TRUTHY);
                buf_byte(code, OP_I32_AND);
            }
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_I32);
            compile_pattern_bindings(arm->pattern, subject_idx, code, locals, ctx);
            compile_block_expr(arm->body, code, locals, ctx);
            buf_byte(code, OP_ELSE);
        }
        /* Default */
        emit_null(code);
        for (int i = 0; i < node->match.arms.len; i++) {
            buf_byte(code, OP_END);
        }
        break;
    }

    /* ---- Block expression ---- */

    case NODE_BLOCK:
        compile_block_expr(node, code, locals, ctx);
        break;

    /* ---- Lambda ---- */

    case NODE_LAMBDA: {
        /* Lambda was collected during the function pass. Retrieve the func index
           from the is_generator field (high bits store the fn_infos index). */
        int fn_info_idx = (node->lambda.is_generator >> 16) & 0xFFFF;
        emit_val_new(code, TAG_FUNC, NUM_RT_FUNCS + fn_info_idx);
        break;
    }

    /* ---- Cast ---- */

    case NODE_CAST:
        compile_expr(node->cast.expr, code, locals, ctx);
        break;

    /* ---- Struct initializer ---- */

    case NODE_STRUCT_INIT: {
        const char *path = node->struct_init.path ? node->struct_init.path : "Object";
        int nf = node->struct_init.fields.len;
        /* Allocate: 4 (n_fields) + nf * 4 (value pointers) */
        emit_i32(code, 4 + nf * 4);
        emit_call(code, RT_ALLOC);
        int stmp = locals_add(locals, "__sinit");
        emit_local_tee(code, stmp);
        /* Store n_fields */
        emit_i32(code, nf);
        buf_byte(code, OP_I32_STORE);
        buf_leb128_u(code, 2);
        buf_leb128_u(code, 0);
        /* Store each field value at data_ptr + 4 + field_index * 4 */
        for (int i = 0; i < nf; i++) {
            const char *fname = node->struct_init.fields.items[i].key;
            int fidx = struct_field_index(ctx->structs, path, fname);
            if (fidx < 0) fidx = i; /* fallback to order */
            emit_local_get(code, stmp);
            emit_i32(code, 4 + fidx * 4);
            buf_byte(code, OP_I32_ADD);
            compile_expr(node->struct_init.fields.items[i].val, code, locals, ctx);
            buf_byte(code, OP_I32_STORE);
            buf_leb128_u(code, 2);
            buf_leb128_u(code, 0);
        }
        /* Create tagged value */
        emit_i32(code, TAG_STRUCT);
        emit_local_get(code, stmp);
        emit_call(code, RT_VAL_NEW);
        break;
    }

    /* ---- Spread ---- */

    case NODE_SPREAD:
        compile_expr(node->spread.expr, code, locals, ctx);
        break;

    /* ---- List comprehension ---- */

    case NODE_LIST_COMP: {
        emit_call(code, RT_ARR_NEW);
        int result_arr = locals_add(locals, "__lc_arr");
        emit_local_set(code, result_arr);

        /* Track per-clause loop index locals for proper increment */
        int clause_i_locals[16];
        int clause_is_range[16];

        for (int c = 0; c < node->list_comp.clause_pats.len && c < 16; c++) {
            Node *iter = node->list_comp.clause_iters.items[c];
            Node *pat = node->list_comp.clause_pats.items[c];
            const char *vname = "__lc_elem";
            if (pat && pat->tag == NODE_PAT_IDENT) vname = pat->pat_ident.name;
            int elem_idx = locals_ensure(locals, vname);

            /* Check if iterator is a range */
            if (iter && iter->tag == NODE_RANGE) {
                clause_is_range[c] = 1;
                int i_idx = locals_add(locals, "__lc_ri");
                int end_idx = locals_add(locals, "__lc_re");
                clause_i_locals[c] = i_idx;

                compile_expr(iter->range.start, code, locals, ctx);
                emit_call(code, RT_VAL_I32);
                emit_local_set(code, i_idx);
                compile_expr(iter->range.end, code, locals, ctx);
                emit_call(code, RT_VAL_I32);
                if (iter->range.inclusive) {
                    emit_i32(code, 1);
                    buf_byte(code, OP_I32_ADD);
                }
                emit_local_set(code, end_idx);

                buf_byte(code, OP_BLOCK);
                buf_byte(code, WASM_TYPE_VOID);
                buf_byte(code, OP_LOOP);
                buf_byte(code, WASM_TYPE_VOID);

                emit_local_get(code, i_idx);
                emit_local_get(code, end_idx);
                buf_byte(code, OP_I32_GE_S);
                buf_byte(code, OP_BR_IF);
                buf_leb128_u(code, 1);

                /* Set loop variable as a runtime value */
                emit_i32(code, TAG_INT);
                emit_local_get(code, i_idx);
                emit_call(code, RT_VAL_NEW);
                emit_local_set(code, elem_idx);
            } else {
                clause_is_range[c] = 0;
                int arr_src = locals_add(locals, "__lc_src");
                int i_idx = locals_add(locals, "__lc_i");
                int len_idx = locals_add(locals, "__lc_len");
                clause_i_locals[c] = i_idx;

                compile_expr(iter, code, locals, ctx);
                emit_local_set(code, arr_src);
                emit_local_get(code, arr_src);
                emit_call(code, RT_ARR_LEN);
                emit_local_set(code, len_idx);
                emit_i32(code, 0);
                emit_local_set(code, i_idx);

                buf_byte(code, OP_BLOCK);
                buf_byte(code, WASM_TYPE_VOID);
                buf_byte(code, OP_LOOP);
                buf_byte(code, WASM_TYPE_VOID);

                emit_local_get(code, i_idx);
                emit_local_get(code, len_idx);
                buf_byte(code, OP_I32_GE_S);
                buf_byte(code, OP_BR_IF);
                buf_leb128_u(code, 1);

                /* Load element from array */
                emit_local_get(code, arr_src);
                emit_i32(code, TAG_INT);
                emit_local_get(code, i_idx);
                emit_call(code, RT_VAL_NEW);
                emit_call(code, RT_ARR_GET);
                emit_local_set(code, elem_idx);
            }

            /* Optional condition */
            if (c < node->list_comp.clause_conds.len && node->list_comp.clause_conds.items[c]) {
                compile_expr(node->list_comp.clause_conds.items[c], code, locals, ctx);
                emit_call(code, RT_VAL_TRUTHY);
                buf_byte(code, OP_IF);
                buf_byte(code, WASM_TYPE_VOID);
            }
        }

        /* The element expression */
        emit_local_get(code, result_arr);
        compile_expr(node->list_comp.element, code, locals, ctx);
        emit_call(code, RT_ARR_PUSH);

        /* Close conditions and loops */
        for (int c = node->list_comp.clause_pats.len - 1; c >= 0; c--) {
            if (c < node->list_comp.clause_conds.len && node->list_comp.clause_conds.items[c]) {
                buf_byte(code, OP_END); /* end if */
            }

            /* Increment and loop back */
            int ii = clause_i_locals[c];
            emit_local_get(code, ii);
            emit_i32(code, 1);
            buf_byte(code, OP_I32_ADD);
            emit_local_set(code, ii);
            buf_byte(code, OP_BR);
            buf_leb128_u(code, 0);
            buf_byte(code, OP_END); /* loop */
            buf_byte(code, OP_END); /* block */
        }

        emit_local_get(code, result_arr);
        break;
    }

    /* ---- Map comprehension ---- */

    case NODE_MAP_COMP: {
        emit_call(code, RT_MAP_NEW);
        int result_map = locals_add(locals, "__mc_map");
        emit_local_set(code, result_map);

        /* Simplified: single clause */
        if (node->map_comp.clause_pats.len > 0) {
            Node *iter = node->map_comp.clause_iters.items[0];
            Node *pat = node->map_comp.clause_pats.items[0];
            const char *vname = "__mc_elem";
            if (pat && pat->tag == NODE_PAT_IDENT) vname = pat->pat_ident.name;
            int elem_idx = locals_ensure(locals, vname);
            int arr_src = locals_add(locals, "__mc_src");
            int i_idx = locals_add(locals, "__mc_i");
            int len_idx = locals_add(locals, "__mc_len");

            compile_expr(iter, code, locals, ctx);
            emit_local_set(code, arr_src);
            emit_local_get(code, arr_src);
            emit_call(code, RT_ARR_LEN);
            emit_local_set(code, len_idx);
            emit_i32(code, 0);
            emit_local_set(code, i_idx);

            buf_byte(code, OP_BLOCK);
            buf_byte(code, WASM_TYPE_VOID);
            buf_byte(code, OP_LOOP);
            buf_byte(code, WASM_TYPE_VOID);

            emit_local_get(code, i_idx);
            emit_local_get(code, len_idx);
            buf_byte(code, OP_I32_GE_S);
            buf_byte(code, OP_BR_IF);
            buf_leb128_u(code, 1);

            emit_local_get(code, arr_src);
            emit_i32(code, TAG_INT);
            emit_local_get(code, i_idx);
            emit_call(code, RT_VAL_NEW);
            emit_call(code, RT_ARR_GET);
            emit_local_set(code, elem_idx);

            if (node->map_comp.clause_conds.len > 0 && node->map_comp.clause_conds.items[0]) {
                compile_expr(node->map_comp.clause_conds.items[0], code, locals, ctx);
                emit_call(code, RT_VAL_TRUTHY);
                buf_byte(code, OP_IF);
                buf_byte(code, WASM_TYPE_VOID);
            }

            emit_local_get(code, result_map);
            compile_expr(node->map_comp.key, code, locals, ctx);
            compile_expr(node->map_comp.value, code, locals, ctx);
            emit_call(code, RT_MAP_SET);

            if (node->map_comp.clause_conds.len > 0 && node->map_comp.clause_conds.items[0]) {
                buf_byte(code, OP_END);
            }

            emit_local_get(code, i_idx);
            emit_i32(code, 1);
            buf_byte(code, OP_I32_ADD);
            emit_local_set(code, i_idx);
            buf_byte(code, OP_BR);
            buf_leb128_u(code, 0);
            buf_byte(code, OP_END);
            buf_byte(code, OP_END);
        }

        emit_local_get(code, result_map);
        break;
    }

    /* ---- Await (no real async in WASM, just pass through) ---- */

    case NODE_AWAIT:
        compile_expr(node->await_.expr, code, locals, ctx);
        break;

    /* ---- Yield ---- */

    case NODE_YIELD:
        if (node->yield_.value)
            compile_expr(node->yield_.value, code, locals, ctx);
        else
            emit_null(code);
        break;

    /* ---- Spawn ---- */

    case NODE_SPAWN:
        compile_expr(node->spawn_.expr, code, locals, ctx);
        break;

    /* ---- Do expression ---- */

    case NODE_DO_EXPR:
        compile_block_expr(node->do_expr.body, code, locals, ctx);
        break;

    /* ---- With expression ---- */

    case NODE_WITH: {
        compile_expr(node->with_.expr, code, locals, ctx);
        if (node->with_.name) {
            int idx = locals_ensure(locals, node->with_.name);
            emit_local_set(code, idx);
        } else {
            buf_byte(code, OP_DROP);
        }
        compile_block_expr(node->with_.body, code, locals, ctx);
        break;
    }

    /* ---- Resume ---- */

    case NODE_RESUME:
        if (node->resume_.value)
            compile_expr(node->resume_.value, code, locals, ctx);
        else
            emit_null(code);
        break;

    /* ---- Perform ---- */

    case NODE_PERFORM:
        /* Effects not supported in WASM - return null */
        emit_null(code);
        break;

    /* ---- Handle ---- */

    case NODE_HANDLE:
        compile_expr(node->handle.expr, code, locals, ctx);
        break;

    /* ---- Nursery ---- */

    case NODE_NURSERY:
        compile_block_expr(node->nursery_.body, code, locals, ctx);
        break;

    /* ---- Throw (as expression) ---- */

    case NODE_THROW: {
        if (node->throw_.value)
            compile_expr(node->throw_.value, code, locals, ctx);
        else
            emit_null(code);
        emit_global_set(code, GLOBAL_ERR_VAL);
        emit_i32(code, 1);
        emit_global_set(code, GLOBAL_ERR_FLAG);
        emit_null(code);
        break;
    }

    /* ---- Return (as expression) ---- */

    case NODE_RETURN:
        if (node->ret.value)
            compile_expr(node->ret.value, code, locals, ctx);
        else
            emit_null(code);
        buf_byte(code, OP_RETURN);
        /* Dead code after return, but WASM needs stack balance */
        emit_null(code);
        break;

    /* ---- Try (as expression) ---- */

    case NODE_TRY: {
        /* Emit try body, check error flag, branch to catch */
        emit_i32(code, 0);
        emit_global_set(code, GLOBAL_ERR_FLAG);
        compile_block_expr(node->try_.body, code, locals, ctx);
        /* Check if error was thrown */
        int try_result = locals_add(locals, "__try_r");
        emit_local_set(code, try_result);
        emit_global_get(code, GLOBAL_ERR_FLAG);
        buf_byte(code, OP_IF);
        buf_byte(code, WASM_TYPE_I32);
        emit_i32(code, 0);
        emit_global_set(code, GLOBAL_ERR_FLAG);
        if (node->try_.catch_arms.len > 0) {
            MatchArm *arm = &node->try_.catch_arms.items[0];
            if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT) {
                int eidx = locals_ensure(locals, arm->pattern->pat_ident.name);
                emit_global_get(code, GLOBAL_ERR_VAL);
                emit_local_set(code, eidx);
            }
            compile_block_expr(arm->body, code, locals, ctx);
        } else {
            emit_null(code);
        }
        buf_byte(code, OP_ELSE);
        emit_local_get(code, try_result);
        buf_byte(code, OP_END);
        /* Finally */
        if (node->try_.finally_block) {
            compile_block(node->try_.finally_block, code, locals, ctx);
        }
        break;
    }

    /* ---- Defer (as expression) ---- */

    case NODE_DEFER:
        emit_null(code);
        break;

    /* ---- Send expression (actor ! message) ---- */

    case NODE_SEND_EXPR:
        compile_expr(node->send_expr.target, code, locals, ctx);
        compile_expr(node->send_expr.message, code, locals, ctx);
        /* No real actor model in WASM - just return the message */
        buf_byte(code, OP_DROP); /* drop target */
        buf_byte(code, OP_DROP); /* drop message too */
        emit_null(code);
        break;

    /* ---- Special literal types ---- */

    case NODE_LIT_DURATION:
        /* Store duration as float (milliseconds) */
        emit_int_val(code, (int32_t)node->lit_duration.ms);
        break;

    case NODE_LIT_COLOR: {
        /* Pack RGBA as i32: 0xRRGGBBAA */
        int32_t packed = (node->lit_color.r << 24) | (node->lit_color.g << 16) |
                         (node->lit_color.b << 8) | node->lit_color.a;
        emit_int_val(code, packed);
        break;
    }

    case NODE_LIT_DATE: {
        const char *d = node->lit_date.value ? node->lit_date.value : "";
        int slen = 0;
        int off = strtab_add_with_len(ctx->strtab, d, &slen);
        emit_str_val(code, off, slen);
        break;
    }

    case NODE_LIT_SIZE:
        emit_int_val(code, (int32_t)node->lit_size.bytes);
        break;

    case NODE_LIT_ANGLE:
        /* Store radians as int (scaled by 1000) */
        emit_int_val(code, (int32_t)(node->lit_angle.radians * 1000.0));
        break;

    /* ---- Pattern nodes used as expressions ---- */

    case NODE_PAT_IDENT: {
        int idx = locals_find(locals, node->pat_ident.name);
        if (idx >= 0) emit_local_get(code, idx);
        else emit_null(code);
        break;
    }

    case NODE_PAT_WILD:
        emit_null(code);
        break;

    case NODE_PAT_LIT:
        switch (node->pat_lit.tag) {
        case 0: emit_int_val(code, (int32_t)node->pat_lit.ival); break;
        case 1: emit_int_val(code, (int32_t)node->pat_lit.fval); break;
        case 2: {
            const char *s = node->pat_lit.sval ? node->pat_lit.sval : "";
            int slen = 0;
            int off = strtab_add_with_len(ctx->strtab, s, &slen);
            emit_str_val(code, off, slen);
            break;
        }
        case 3: emit_bool_val(code, node->pat_lit.bval); break;
        case 4: emit_null(code); break;
        default: emit_null(code); break;
        }
        break;

    case NODE_PAT_TUPLE:
    case NODE_PAT_STRUCT:
    case NODE_PAT_ENUM:
    case NODE_PAT_OR:
    case NODE_PAT_RANGE:
    case NODE_PAT_SLICE:
    case NODE_PAT_GUARD:
    case NODE_PAT_EXPR:
    case NODE_PAT_CAPTURE:
    case NODE_PAT_STRING_CONCAT:
    case NODE_PAT_REGEX:
        emit_null(code);
        break;

    /* ---- Declaration nodes used as expressions ---- */

    case NODE_FN_DECL:
        if (node->fn_decl.name) {
            int fidx = funcs_find(ctx->funcs, node->fn_decl.name);
            if (fidx >= 0) {
                emit_val_new(code, TAG_FUNC, NUM_RT_FUNCS + fidx);
            } else {
                emit_null(code);
            }
        } else {
            emit_null(code);
        }
        break;

    case NODE_CLASS_DECL:
    case NODE_STRUCT_DECL:
    case NODE_ENUM_DECL:
    case NODE_IMPL_DECL:
    case NODE_TRAIT_DECL:
    case NODE_TYPE_ALIAS:
    case NODE_IMPORT:
    case NODE_USE:
    case NODE_MODULE_DECL:
    case NODE_EFFECT_DECL:
    case NODE_ACTOR_DECL:
    case NODE_TAG_DECL:
    case NODE_BIND:
    case NODE_ADAPT_FN:
    case NODE_INLINE_C:
        emit_null(code);
        break;

    case NODE_LET:
    case NODE_VAR:
    case NODE_CONST:
        if (node->let.name) {
            int idx = locals_find(locals, node->let.name);
            if (idx >= 0) emit_local_get(code, idx);
            else emit_null(code);
        } else {
            emit_null(code);
        }
        break;

    case NODE_EXPR_STMT:
        if (node->expr_stmt.expr)
            compile_expr(node->expr_stmt.expr, code, locals, ctx);
        else
            emit_null(code);
        break;

    /* ---- Loops as expressions: compile via stmt path, push null result ---- */

    case NODE_WHILE:
    case NODE_FOR:
    case NODE_LOOP:
        compile_stmt(node, code, locals, ctx);
        emit_null(code);
        break;
    case NODE_BREAK:
    case NODE_CONTINUE:
        compile_stmt(node, code, locals, ctx);
        emit_null(code);
        break;

    case NODE_EVERY:
    case NODE_AFTER:
    case NODE_TIMEOUT:
    case NODE_DEBOUNCE:
    case NODE_PAUSE:
    case NODE_DEL:
        emit_null(code);
        break;

    case NODE_PROGRAM:
        emit_null(code);
        break;

    default:
        emit_null(code);
        break;
    }
}

/* ========================================================================
   Pattern matching compilation
   ======================================================================== */

/* Compile a pattern condition - leaves an i32 (0 or 1) on the stack */
static void compile_pattern_cond(Node *pat, int subject_local, WasmBuf *code,
                                  LocalMap *locals, CompilerCtx *ctx) {
    if (!pat) {
        emit_i32(code, 1);
        return;
    }

    switch (pat->tag) {
    case NODE_PAT_WILD:
        emit_i32(code, 1);
        break;

    case NODE_PAT_IDENT:
        /* Identifier pattern always matches */
        emit_i32(code, 1);
        break;

    case NODE_PAT_LIT: {
        emit_local_get(code, subject_local);
        switch (pat->pat_lit.tag) {
        case 0: /* int */
            emit_int_val(code, (int32_t)pat->pat_lit.ival);
            emit_call(code, RT_VAL_EQ);
            emit_call(code, RT_VAL_TRUTHY);
            break;
        case 1: /* float */
            emit_int_val(code, (int32_t)pat->pat_lit.fval);
            emit_call(code, RT_VAL_EQ);
            emit_call(code, RT_VAL_TRUTHY);
            break;
        case 2: { /* string */
            const char *s = pat->pat_lit.sval ? pat->pat_lit.sval : "";
            int slen = 0;
            int off = strtab_add_with_len(ctx->strtab, s, &slen);
            emit_str_val(code, off, slen);
            emit_call(code, RT_VAL_EQ);
            emit_call(code, RT_VAL_TRUTHY);
            break;
        }
        case 3: /* bool */
            emit_bool_val(code, pat->pat_lit.bval);
            emit_call(code, RT_VAL_EQ);
            emit_call(code, RT_VAL_TRUTHY);
            break;
        case 4: /* null */
            emit_null(code);
            emit_call(code, RT_VAL_EQ);
            emit_call(code, RT_VAL_TRUTHY);
            break;
        default:
            emit_i32(code, 1);
            break;
        }
        break;
    }

    case NODE_PAT_OR:
        compile_pattern_cond(pat->pat_or.left, subject_local, code, locals, ctx);
        compile_pattern_cond(pat->pat_or.right, subject_local, code, locals, ctx);
        buf_byte(code, OP_I32_OR);
        break;

    case NODE_PAT_RANGE: {
        /* subject >= start && subject < end (or <= end if inclusive) */
        emit_local_get(code, subject_local);
        compile_expr(pat->pat_range.start, code, locals, ctx);
        emit_call(code, RT_VAL_GE);
        emit_call(code, RT_VAL_TRUTHY);
        emit_local_get(code, subject_local);
        compile_expr(pat->pat_range.end, code, locals, ctx);
        if (pat->pat_range.inclusive) {
            emit_call(code, RT_VAL_LE);
        } else {
            emit_call(code, RT_VAL_LT);
        }
        emit_call(code, RT_VAL_TRUTHY);
        buf_byte(code, OP_I32_AND);
        break;
    }

    case NODE_PAT_GUARD:
        compile_pattern_cond(pat->pat_guard.pattern, subject_local, code, locals, ctx);
        /* Guard condition evaluated after bindings, but we check it here */
        if (pat->pat_guard.guard) {
            compile_pattern_bindings(pat->pat_guard.pattern, subject_local, code, locals, ctx);
            compile_expr(pat->pat_guard.guard, code, locals, ctx);
            emit_call(code, RT_VAL_TRUTHY);
            buf_byte(code, OP_I32_AND);
        }
        break;

    case NODE_PAT_EXPR:
        emit_local_get(code, subject_local);
        compile_expr(pat->pat_expr.expr, code, locals, ctx);
        emit_call(code, RT_VAL_EQ);
        emit_call(code, RT_VAL_TRUTHY);
        break;

    case NODE_PAT_CAPTURE:
        compile_pattern_cond(pat->pat_capture.pattern, subject_local, code, locals, ctx);
        break;

    case NODE_PAT_TUPLE: {
        /* Check array tag and length */
        emit_local_get(code, subject_local);
        emit_call(code, RT_VAL_TAG);
        emit_i32(code, TAG_TUPLE);
        buf_byte(code, OP_I32_EQ);
        emit_local_get(code, subject_local);
        emit_call(code, RT_VAL_TAG);
        emit_i32(code, TAG_ARRAY);
        buf_byte(code, OP_I32_EQ);
        buf_byte(code, OP_I32_OR); /* tuple or array */
        emit_local_get(code, subject_local);
        emit_call(code, RT_ARR_LEN);
        emit_i32(code, pat->pat_tuple.elems.len);
        buf_byte(code, OP_I32_EQ);
        buf_byte(code, OP_I32_AND);
        /* Check each element */
        for (int i = 0; i < pat->pat_tuple.elems.len; i++) {
            int elem_local = locals_add(locals, "__ptup_e");
            emit_local_get(code, subject_local);
            emit_int_val(code, i);
            emit_call(code, RT_ARR_GET);
            emit_local_set(code, elem_local);
            compile_pattern_cond(pat->pat_tuple.elems.items[i], elem_local, code, locals, ctx);
            buf_byte(code, OP_I32_AND);
        }
        break;
    }

    case NODE_PAT_STRUCT: {
        /* Check that subject is a struct with matching type name */
        emit_local_get(code, subject_local);
        emit_call(code, RT_VAL_TAG);
        emit_i32(code, TAG_STRUCT);
        buf_byte(code, OP_I32_EQ);
        /* TODO: check type name if pat->pat_struct.path is set */
        /* For each field pattern, check the field value */
        for (int i = 0; i < pat->pat_struct.fields.len; i++) {
            if (pat->pat_struct.fields.items[i].val) {
                int flocal = locals_add(locals, "__pstf");
                emit_local_get(code, subject_local);
                const char *fn = pat->pat_struct.fields.items[i].key;
                int fl = 0;
                int foff = strtab_add_with_len(ctx->strtab, fn, &fl);
                emit_str_val(code, foff, fl);
                emit_call(code, RT_VAL_FIELD);
                emit_local_set(code, flocal);
                compile_pattern_cond(pat->pat_struct.fields.items[i].val, flocal, code, locals, ctx);
                buf_byte(code, OP_I32_AND);
            }
        }
        break;
    }

    case NODE_PAT_ENUM: {
        /* Check variant tag */
        int vtag = -1;
        if (pat->pat_enum.path) {
            vtag = enum_variant_tag(ctx->enums, pat->pat_enum.path);
        }
        if (vtag >= 0) {
            emit_local_get(code, subject_local);
            emit_call(code, RT_VAL_I32);
            emit_i32(code, vtag);
            buf_byte(code, OP_I32_EQ);
        } else {
            emit_i32(code, 1);
        }
        break;
    }

    case NODE_PAT_SLICE: {
        /* Similar to tuple: check array type and length */
        emit_local_get(code, subject_local);
        emit_call(code, RT_VAL_TAG);
        emit_i32(code, TAG_ARRAY);
        buf_byte(code, OP_I32_EQ);
        if (pat->pat_slice.rest) {
            emit_local_get(code, subject_local);
            emit_call(code, RT_ARR_LEN);
            emit_i32(code, pat->pat_slice.elems.len);
            buf_byte(code, OP_I32_GE_S);
        } else {
            emit_local_get(code, subject_local);
            emit_call(code, RT_ARR_LEN);
            emit_i32(code, pat->pat_slice.elems.len);
            buf_byte(code, OP_I32_EQ);
        }
        buf_byte(code, OP_I32_AND);
        for (int i = 0; i < pat->pat_slice.elems.len; i++) {
            int el = locals_add(locals, "__psl_e");
            emit_local_get(code, subject_local);
            emit_int_val(code, i);
            emit_call(code, RT_ARR_GET);
            emit_local_set(code, el);
            compile_pattern_cond(pat->pat_slice.elems.items[i], el, code, locals, ctx);
            buf_byte(code, OP_I32_AND);
        }
        break;
    }

    default:
        emit_i32(code, 1);
        break;
    }
}

/* Compile pattern bindings - bind variables from a matched pattern */
static void compile_pattern_bindings(Node *pat, int subject_local, WasmBuf *code,
                                      LocalMap *locals, CompilerCtx *ctx) {
    if (!pat) return;

    switch (pat->tag) {
    case NODE_PAT_IDENT: {
        int idx = locals_ensure(locals, pat->pat_ident.name);
        emit_local_get(code, subject_local);
        emit_local_set(code, idx);
        break;
    }

    case NODE_PAT_CAPTURE: {
        if (pat->pat_capture.name) {
            int idx = locals_ensure(locals, pat->pat_capture.name);
            emit_local_get(code, subject_local);
            emit_local_set(code, idx);
        }
        compile_pattern_bindings(pat->pat_capture.pattern, subject_local, code, locals, ctx);
        break;
    }

    case NODE_PAT_TUPLE:
        for (int i = 0; i < pat->pat_tuple.elems.len; i++) {
            int el = locals_add(locals, "__bt_e");
            emit_local_get(code, subject_local);
            emit_int_val(code, i);
            emit_call(code, RT_ARR_GET);
            emit_local_set(code, el);
            compile_pattern_bindings(pat->pat_tuple.elems.items[i], el, code, locals, ctx);
        }
        break;

    case NODE_PAT_STRUCT:
        for (int i = 0; i < pat->pat_struct.fields.len; i++) {
            int fl = locals_add(locals, "__bs_f");
            emit_local_get(code, subject_local);
            const char *fn = pat->pat_struct.fields.items[i].key;
            int fnl = 0;
            int foff = strtab_add_with_len(ctx->strtab, fn, &fnl);
            emit_str_val(code, foff, fnl);
            emit_call(code, RT_VAL_FIELD);
            emit_local_set(code, fl);
            if (pat->pat_struct.fields.items[i].val) {
                compile_pattern_bindings(pat->pat_struct.fields.items[i].val, fl, code, locals, ctx);
            } else {
                /* Shorthand: field name is the binding */
                int bidx = locals_ensure(locals, fn);
                emit_local_get(code, fl);
                emit_local_set(code, bidx);
            }
        }
        break;

    case NODE_PAT_ENUM:
        for (int i = 0; i < pat->pat_enum.args.len; i++) {
            int al = locals_add(locals, "__be_a");
            emit_local_get(code, subject_local);
            emit_int_val(code, i);
            emit_call(code, RT_ARR_GET);
            emit_local_set(code, al);
            compile_pattern_bindings(pat->pat_enum.args.items[i], al, code, locals, ctx);
        }
        break;

    case NODE_PAT_SLICE:
        for (int i = 0; i < pat->pat_slice.elems.len; i++) {
            int el = locals_add(locals, "__bsl_e");
            emit_local_get(code, subject_local);
            emit_int_val(code, i);
            emit_call(code, RT_ARR_GET);
            emit_local_set(code, el);
            compile_pattern_bindings(pat->pat_slice.elems.items[i], el, code, locals, ctx);
        }
        /* rest binding: collect remaining elements */
        if (pat->pat_slice.rest) {
            int rest_idx = locals_ensure(locals, pat->pat_slice.rest);
            /* TODO: slice remaining elements into new array */
            emit_call(code, RT_ARR_NEW);
            emit_local_set(code, rest_idx);
        }
        break;

    case NODE_PAT_OR:
        compile_pattern_bindings(pat->pat_or.left, subject_local, code, locals, ctx);
        break;

    case NODE_PAT_GUARD:
        compile_pattern_bindings(pat->pat_guard.pattern, subject_local, code, locals, ctx);
        break;

    default:
        break;
    }
}

/* ========================================================================
   compile_stmt - handle every statement node type
   ======================================================================== */

static void compile_stmt(Node *node, WasmBuf *code, LocalMap *locals, CompilerCtx *ctx) {
    if (!node) return;

    switch (node->tag) {

    /* ---- Variable declarations ---- */

    case NODE_LET:
    case NODE_VAR: {
        if (node->let.name) {
            int idx = locals_ensure(locals, node->let.name);
            if (node->let.value) {
                compile_expr(node->let.value, code, locals, ctx);
                emit_local_set(code, idx);
            } else {
                emit_null(code);
                emit_local_set(code, idx);
            }
        } else if (node->let.pattern) {
            /* Destructuring let */
            if (node->let.value) {
                int tmp = locals_add(locals, "__let_v");
                compile_expr(node->let.value, code, locals, ctx);
                emit_local_set(code, tmp);
                compile_pattern_bindings(node->let.pattern, tmp, code, locals, ctx);
            }
        }
        break;
    }

    case NODE_CONST: {
        if (node->const_.name) {
            int idx = locals_ensure(locals, node->const_.name);
            if (node->const_.value) {
                compile_expr(node->const_.value, code, locals, ctx);
                emit_local_set(code, idx);
            }
        }
        break;
    }

    /* ---- Assignment statement ---- */

    case NODE_ASSIGN: {
        compile_expr(node, code, locals, ctx);
        buf_byte(code, OP_DROP);
        break;
    }

    /* ---- Return ---- */

    case NODE_RETURN: {
        if (ctx->defers.count > 0) {
            if (node->ret.value) {
                int ret_tmp = locals_add(locals, "__ret_tmp");
                compile_expr(node->ret.value, code, locals, ctx);
                emit_local_set(code, ret_tmp);
                emit_defers(code, locals, ctx);
                emit_local_get(code, ret_tmp);
            } else {
                emit_defers(code, locals, ctx);
                emit_null(code);
            }
        } else {
            if (node->ret.value)
                compile_expr(node->ret.value, code, locals, ctx);
            else
                emit_null(code);
        }
        buf_byte(code, OP_RETURN);
        break;
    }

    /* ---- Expression statement ---- */

    case NODE_EXPR_STMT:
        if (node->expr_stmt.expr) {
            compile_expr(node->expr_stmt.expr, code, locals, ctx);
            buf_byte(code, OP_DROP);
        }
        break;

    /* ---- While loop ---- */

    case NODE_WHILE: {
        int saved_break = ctx->break_depth;
        int saved_continue = ctx->continue_depth;
        /* while: block(break) > loop > body
           From body: br 0 = loop (continue), br 1 = block (break) */
        ctx->break_depth = 1;
        ctx->continue_depth = 0;
        buf_byte(code, OP_BLOCK);
        buf_byte(code, WASM_TYPE_VOID);
        buf_byte(code, OP_LOOP);
        buf_byte(code, WASM_TYPE_VOID);
        compile_expr(node->while_loop.cond, code, locals, ctx);
        emit_call(code, RT_VAL_TRUTHY);
        buf_byte(code, OP_I32_EQZ);
        buf_byte(code, OP_BR_IF);
        buf_leb128_u(code, 1);
        compile_block(node->while_loop.body, code, locals, ctx);
        buf_byte(code, OP_BR);
        buf_leb128_u(code, 0);
        buf_byte(code, OP_END); /* loop */
        buf_byte(code, OP_END); /* block */
        ctx->break_depth = saved_break;
        ctx->continue_depth = saved_continue;
        break;
    }

    /* ---- For loop ---- */

    case NODE_FOR: {
        int saved_break = ctx->break_depth;
        int saved_continue = ctx->continue_depth;
        const char *var_name = "__for_elem";
        if (node->for_loop.pattern && node->for_loop.pattern->tag == NODE_PAT_IDENT)
            var_name = node->for_loop.pattern->pat_ident.name;

        /* Check if iterator is a range */
        if (node->for_loop.iter && node->for_loop.iter->tag == NODE_RANGE) {
            Node *range = node->for_loop.iter;
            int idx = locals_ensure(locals, var_name);
            int end_idx = locals_add(locals, "__for_end");

            /* Initialize: get start and end as raw i32 */
            compile_expr(range->range.start, code, locals, ctx);
            emit_call(code, RT_VAL_I32);
            int raw_start = locals_add(locals, "__for_rs");
            emit_local_set(code, raw_start);

            compile_expr(range->range.end, code, locals, ctx);
            emit_call(code, RT_VAL_I32);
            if (range->range.inclusive) {
                emit_i32(code, 1);
                buf_byte(code, OP_I32_ADD);
            }
            emit_local_set(code, end_idx);

            /* Set loop variable to start value */
            emit_local_get(code, raw_start);
            int raw_idx = locals_add(locals, "__for_ri");
            emit_local_set(code, raw_idx);

            /* Structure: block(break) > loop > block(continue) > body
               break = br 2, continue = br 0 (jumps to end of inner block,
               falls through to increment, then br back to loop) */
            buf_byte(code, OP_BLOCK);       /* break target */
            buf_byte(code, WASM_TYPE_VOID);
            buf_byte(code, OP_LOOP);        /* loop back-edge */
            buf_byte(code, WASM_TYPE_VOID);

            /* Check: raw_i >= end -> break */
            emit_local_get(code, raw_idx);
            emit_local_get(code, end_idx);
            buf_byte(code, OP_I32_GE_S);
            buf_byte(code, OP_BR_IF);
            buf_leb128_u(code, 1);

            /* Create value for loop variable */
            emit_i32(code, TAG_INT);
            emit_local_get(code, raw_idx);
            emit_call(code, RT_VAL_NEW);
            emit_local_set(code, idx);

            /* From body: br 0 = end of continue block (then falls to increment),
               br 2 = break (exits outer block) */
            ctx->continue_depth = 0;
            ctx->break_depth = 2;
            buf_byte(code, OP_BLOCK);       /* continue target */
            buf_byte(code, WASM_TYPE_VOID);
            compile_block(node->for_loop.body, code, locals, ctx);
            buf_byte(code, OP_END);         /* end continue block */

            /* Increment */
            emit_local_get(code, raw_idx);
            emit_i32(code, 1);
            buf_byte(code, OP_I32_ADD);
            emit_local_set(code, raw_idx);
            buf_byte(code, OP_BR);
            buf_leb128_u(code, 0);
            buf_byte(code, OP_END); /* loop */
            buf_byte(code, OP_END); /* block */
        } else {
            /* Array-based for loop */
            int elem_idx = locals_ensure(locals, var_name);
            int arr_idx = locals_add(locals, "__for_arr");
            int len_idx = locals_add(locals, "__for_len");
            int i_idx = locals_add(locals, "__for_i");

            compile_expr(node->for_loop.iter, code, locals, ctx);
            emit_local_set(code, arr_idx);
            emit_local_get(code, arr_idx);
            emit_call(code, RT_ARR_LEN);
            emit_local_set(code, len_idx);
            emit_i32(code, 0);
            emit_local_set(code, i_idx);

            buf_byte(code, OP_BLOCK);       /* break target */
            buf_byte(code, WASM_TYPE_VOID);
            buf_byte(code, OP_LOOP);
            buf_byte(code, WASM_TYPE_VOID);

            emit_local_get(code, i_idx);
            emit_local_get(code, len_idx);
            buf_byte(code, OP_I32_GE_S);
            buf_byte(code, OP_BR_IF);
            buf_leb128_u(code, 1);

            /* Load element */
            emit_local_get(code, arr_idx);
            emit_i32(code, TAG_INT);
            emit_local_get(code, i_idx);
            emit_call(code, RT_VAL_NEW);
            emit_call(code, RT_ARR_GET);
            emit_local_set(code, elem_idx);

            /* If pattern is a destructuring pattern, bind it */
            if (node->for_loop.pattern && node->for_loop.pattern->tag != NODE_PAT_IDENT) {
                compile_pattern_bindings(node->for_loop.pattern, elem_idx, code, locals, ctx);
            }

            /* From body: br 0 = end of continue block, br 2 = break */
            ctx->continue_depth = 0;
            ctx->break_depth = 2;
            buf_byte(code, OP_BLOCK);       /* continue target */
            buf_byte(code, WASM_TYPE_VOID);
            compile_block(node->for_loop.body, code, locals, ctx);
            buf_byte(code, OP_END);         /* end continue block */

            emit_local_get(code, i_idx);
            emit_i32(code, 1);
            buf_byte(code, OP_I32_ADD);
            emit_local_set(code, i_idx);
            buf_byte(code, OP_BR);
            buf_leb128_u(code, 0);
            buf_byte(code, OP_END); /* loop */
            buf_byte(code, OP_END); /* block */
        }
        ctx->break_depth = saved_break;
        ctx->continue_depth = saved_continue;
        break;
    }

    /* ---- Loop ---- */

    case NODE_LOOP: {
        int saved_break = ctx->break_depth;
        int saved_continue = ctx->continue_depth;
        ctx->break_depth = 1;
        ctx->continue_depth = 0;
        buf_byte(code, OP_BLOCK);
        buf_byte(code, WASM_TYPE_VOID);
        buf_byte(code, OP_LOOP);
        buf_byte(code, WASM_TYPE_VOID);
        compile_block(node->loop.body, code, locals, ctx);
        buf_byte(code, OP_BR);
        buf_leb128_u(code, 0);
        buf_byte(code, OP_END); /* loop */
        buf_byte(code, OP_END); /* block */
        ctx->break_depth = saved_break;
        ctx->continue_depth = saved_continue;
        break;
    }

    /* ---- Break ---- */

    case NODE_BREAK:
        buf_byte(code, OP_BR);
        buf_leb128_u(code, (uint32_t)ctx->break_depth);
        break;

    /* ---- Continue ---- */

    case NODE_CONTINUE:
        buf_byte(code, OP_BR);
        buf_leb128_u(code, (uint32_t)ctx->continue_depth);
        break;

    /* ---- If statement ---- */

    case NODE_IF: {
        /* Bump break/continue depths since if adds a block level */
        ctx->break_depth++;
        ctx->continue_depth++;
        compile_expr(node->if_expr.cond, code, locals, ctx);
        emit_call(code, RT_VAL_TRUTHY);
        buf_byte(code, OP_IF);
        buf_byte(code, WASM_TYPE_VOID);
        compile_block(node->if_expr.then, code, locals, ctx);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++) {
            buf_byte(code, OP_ELSE);
            ctx->break_depth++;
            ctx->continue_depth++;
            compile_expr(node->if_expr.elif_conds.items[i], code, locals, ctx);
            emit_call(code, RT_VAL_TRUTHY);
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_VOID);
            compile_block(node->if_expr.elif_thens.items[i], code, locals, ctx);
        }
        if (node->if_expr.else_branch) {
            buf_byte(code, OP_ELSE);
            compile_block(node->if_expr.else_branch, code, locals, ctx);
        }
        buf_byte(code, OP_END);
        for (int i = 0; i < node->if_expr.elif_conds.len; i++) {
            buf_byte(code, OP_END);
            ctx->break_depth--;
            ctx->continue_depth--;
        }
        ctx->break_depth--;
        ctx->continue_depth--;
        break;
    }

    /* ---- Match statement ---- */

    case NODE_MATCH: {
        int subject_idx = locals_add(locals, "__match_sub");
        compile_expr(node->match.subject, code, locals, ctx);
        emit_local_set(code, subject_idx);

        for (int i = 0; i < node->match.arms.len; i++) {
            MatchArm *arm = &node->match.arms.items[i];
            compile_pattern_cond(arm->pattern, subject_idx, code, locals, ctx);
            if (arm->guard) {
                compile_expr(arm->guard, code, locals, ctx);
                emit_call(code, RT_VAL_TRUTHY);
                buf_byte(code, OP_I32_AND);
            }
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_VOID);
            compile_pattern_bindings(arm->pattern, subject_idx, code, locals, ctx);
            compile_block(arm->body, code, locals, ctx);
            buf_byte(code, OP_ELSE);
        }
        buf_byte(code, OP_NOP);
        for (int i = 0; i < node->match.arms.len; i++) {
            buf_byte(code, OP_END);
        }
        break;
    }

    /* ---- Try/Catch ---- */

    case NODE_TRY: {
        emit_i32(code, 0);
        emit_global_set(code, GLOBAL_ERR_FLAG);
        compile_block(node->try_.body, code, locals, ctx);
        if (node->try_.catch_arms.len > 0 || node->try_.finally_block) {
            emit_global_get(code, GLOBAL_ERR_FLAG);
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_VOID);
            emit_i32(code, 0);
            emit_global_set(code, GLOBAL_ERR_FLAG);
            if (node->try_.catch_arms.len > 0) {
                MatchArm *arm = &node->try_.catch_arms.items[0];
                if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT) {
                    int eidx = locals_ensure(locals, arm->pattern->pat_ident.name);
                    emit_global_get(code, GLOBAL_ERR_VAL);
                    emit_local_set(code, eidx);
                }
                compile_block(arm->body, code, locals, ctx);
            }
            buf_byte(code, OP_END);
        }
        if (node->try_.finally_block) {
            compile_block(node->try_.finally_block, code, locals, ctx);
        }
        break;
    }

    /* ---- Throw ---- */

    case NODE_THROW: {
        if (node->throw_.value) {
            compile_expr(node->throw_.value, code, locals, ctx);
            emit_global_set(code, GLOBAL_ERR_VAL);
        }
        emit_i32(code, 1);
        emit_global_set(code, GLOBAL_ERR_FLAG);
        break;
    }

    /* ---- Defer ---- */

    case NODE_DEFER:
        if (node->defer_.body) {
            defer_list_push(&ctx->defers, node->defer_.body);
        }
        break;

    /* ---- Block ---- */

    case NODE_BLOCK:
        for (int i = 0; i < node->block.stmts.len; i++)
            compile_stmt(node->block.stmts.items[i], code, locals, ctx);
        break;

    /* ---- Function declaration ---- */

    case NODE_FN_DECL:
        /* Functions are compiled at the top level, skip in statement context */
        break;

    /* ---- Struct declaration ---- */

    case NODE_STRUCT_DECL:
        /* Already processed during layout collection */
        break;

    /* ---- Enum declaration ---- */

    case NODE_ENUM_DECL:
        /* Already processed during enum layout collection */
        break;

    /* ---- Class declaration ---- */

    case NODE_CLASS_DECL: {
        /* Compile class methods as standalone functions */
        for (int i = 0; i < node->class_decl.members.len; i++) {
            Node *m = node->class_decl.members.items[i];
            if (m && m->tag == NODE_FN_DECL) {
                /* These should already be in the function table */
            }
        }
        break;
    }

    /* ---- Impl declaration ---- */

    case NODE_IMPL_DECL: {
        /* Methods should already be in the function table */
        break;
    }

    /* ---- Trait declaration ---- */

    case NODE_TRAIT_DECL:
        break;

    /* ---- Type alias ---- */

    case NODE_TYPE_ALIAS:
        break;

    /* ---- Module declaration ---- */

    case NODE_MODULE_DECL: {
        for (int i = 0; i < node->module_decl.body.len; i++) {
            compile_stmt(node->module_decl.body.items[i], code, locals, ctx);
        }
        break;
    }

    /* ---- Import/Use ---- */

    case NODE_IMPORT:
    case NODE_USE:
        break;

    /* ---- Effect declaration ---- */

    case NODE_EFFECT_DECL:
        break;

    /* ---- Actor declaration ---- */

    case NODE_ACTOR_DECL:
        break;

    /* ---- Tag/Bind/Adapt ---- */

    case NODE_TAG_DECL:
    case NODE_BIND:
    case NODE_ADAPT_FN:
    case NODE_INLINE_C:
        break;

    /* ---- Nursery ---- */

    case NODE_NURSERY:
        compile_block(node->nursery_.body, code, locals, ctx);
        break;

    /* ---- Handle ---- */

    case NODE_HANDLE:
        if (node->handle.expr) {
            compile_expr(node->handle.expr, code, locals, ctx);
            buf_byte(code, OP_DROP);
        }
        break;

    /* ---- Spawn ---- */

    case NODE_SPAWN:
        if (node->spawn_.expr) {
            compile_expr(node->spawn_.expr, code, locals, ctx);
            buf_byte(code, OP_DROP);
        }
        break;

    /* ---- Await/Yield/Perform/Resume ---- */

    case NODE_AWAIT:
        if (node->await_.expr) {
            compile_expr(node->await_.expr, code, locals, ctx);
            buf_byte(code, OP_DROP);
        }
        break;

    case NODE_YIELD:
        if (node->yield_.value) {
            compile_expr(node->yield_.value, code, locals, ctx);
            buf_byte(code, OP_DROP);
        }
        break;

    case NODE_PERFORM:
    case NODE_RESUME:
        break;

    /* ---- Timing constructs ---- */

    case NODE_EVERY:
        if (node->every_.body) compile_block(node->every_.body, code, locals, ctx);
        break;

    case NODE_AFTER:
        if (node->after_.body) compile_block(node->after_.body, code, locals, ctx);
        break;

    case NODE_TIMEOUT:
        if (node->timeout_.body) compile_block(node->timeout_.body, code, locals, ctx);
        break;

    case NODE_DEBOUNCE:
        if (node->debounce_.body) compile_block(node->debounce_.body, code, locals, ctx);
        break;

    case NODE_PAUSE:
        break;

    case NODE_DEL: {
        /* del name - set local to null */
        if (node->del_.name) {
            int idx = locals_find(locals, node->del_.name);
            if (idx >= 0) {
                emit_null(code);
                emit_local_set(code, idx);
            }
        }
        break;
    }

    /* ---- With statement ---- */

    case NODE_WITH: {
        compile_expr(node->with_.expr, code, locals, ctx);
        if (node->with_.name) {
            int idx = locals_ensure(locals, node->with_.name);
            emit_local_set(code, idx);
        } else {
            buf_byte(code, OP_DROP);
        }
        compile_block(node->with_.body, code, locals, ctx);
        break;
    }

    default:
        /* Try to compile as expression and drop the result */
        compile_expr(node, code, locals, ctx);
        buf_byte(code, OP_DROP);
        break;
    }
}

/* ========================================================================
   Runtime function code generation

   These functions are built into the WASM module itself - no external
   dependencies required.
   ======================================================================== */

/* Helper to emit a complete runtime function body into a WasmBuf.
   The body buf should NOT include the end opcode - we add it. */

/* $alloc(size: i32) -> i32
   Bump allocator. Returns pointer, advances heap. */
static void emit_rt_alloc(WasmBuf *body) {
    /* local 0 = size (param) */
    /* result = global heap_ptr */
    emit_global_get(body, GLOBAL_HEAP_PTR);
    /* new heap = heap_ptr + size, aligned to 4 bytes */
    emit_global_get(body, GLOBAL_HEAP_PTR);
    emit_local_get(body, 0);
    /* Align size up to 4 */
    emit_i32(body, 3);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, ~3);
    buf_byte(body, OP_I32_AND);
    buf_byte(body, OP_I32_ADD);
    emit_global_set(body, GLOBAL_HEAP_PTR);
    /* Check if we need to grow memory */
    emit_global_get(body, GLOBAL_HEAP_PTR);
    buf_byte(body, OP_MEMORY_SIZE);
    buf_leb128_u(body, 0);
    emit_i32(body, 16); /* pages -> bytes shift */
    buf_byte(body, OP_I32_SHL);
    buf_byte(body, OP_I32_GT_U);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_VOID);
    emit_i32(body, 1);
    buf_byte(body, OP_MEMORY_GROW);
    buf_leb128_u(body, 0);
    buf_byte(body, OP_DROP);
    buf_byte(body, OP_END);
    /* Return saved heap_ptr (already on stack from first global.get) */
}

/* $val_new(tag: i32, payload: i32) -> i32
   Allocate 12-byte cell, set tag and payload, zero extra. */
static void emit_rt_val_new(WasmBuf *body) {
    /* local 0 = tag, local 1 = payload */
    /* local 2 = ptr */
    emit_i32(body, VAL_SIZE);
    emit_call(body, RT_ALLOC);
    int ptr = 2;
    emit_local_tee(body, ptr);
    emit_local_get(body, 0); /* tag */
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);

    emit_local_get(body, ptr);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, 1); /* payload */
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);

    /* Zero extra */
    emit_local_get(body, ptr);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, 0);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);

    emit_local_get(body, ptr);
}

/* $val_tag(ptr: i32) -> i32 */
static void emit_rt_val_tag(WasmBuf *body) {
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_i32(body, TAG_NULL);
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    buf_byte(body, OP_END);
}

/* $val_i32(ptr: i32) -> i32 */
static void emit_rt_val_i32(WasmBuf *body) {
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_i32(body, 0);
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    buf_byte(body, OP_END);
}

/* $val_f64_bits(ptr: i32) -> i32 (returns high bits from extra field) */
static void emit_rt_val_f64_bits(WasmBuf *body) {
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_i32(body, 0);
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    buf_byte(body, OP_END);
}

/* $str_new(data_ptr: i32, len: i32) -> i32
   Create a string value. Payload = data_ptr, extra = len. */
static void emit_rt_str_new(WasmBuf *body) {
    /* local 0 = data_ptr, local 1 = len */
    emit_i32(body, TAG_STRING);
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_NEW);
    /* Set extra field to length */
    int ptr = 2;
    emit_local_tee(body, ptr);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, 1);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    emit_local_get(body, ptr);
}

/* $str_cat(a: i32, b: i32) -> i32
   Concatenate two values as strings. */
static void emit_rt_str_cat(WasmBuf *body) {
    /* local 0 = a, local 1 = b */
    /* Convert both to string, copy bytes, create new string */
    /* Get a's string ptr and len */
    /* For simplicity: allocate new buffer, copy both, create str_new */
    /* local 2 = a_ptr, 3 = a_len, 4 = b_ptr, 5 = b_len, 6 = new_ptr, 7 = total */

    /* a string data */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_TO_STR);
    int a_val = 2;
    emit_local_set(body, a_val);
    emit_local_get(body, a_val);
    emit_call(body, RT_VAL_I32);  /* a data ptr */
    int a_ptr = 3;
    emit_local_set(body, a_ptr);
    emit_local_get(body, a_val);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    int a_len = 4;
    emit_local_set(body, a_len);

    /* b string data */
    emit_local_get(body, 1);
    emit_call(body, RT_VAL_TO_STR);
    int b_val = 5;
    emit_local_set(body, b_val);
    emit_local_get(body, b_val);
    emit_call(body, RT_VAL_I32);
    int b_ptr = 6;
    emit_local_set(body, b_ptr);
    emit_local_get(body, b_val);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    int b_len = 7;
    emit_local_set(body, b_len);

    /* total length */
    emit_local_get(body, a_len);
    emit_local_get(body, b_len);
    buf_byte(body, OP_I32_ADD);
    int total = 8;
    emit_local_tee(body, total);

    /* Allocate buffer */
    emit_i32(body, 1);
    buf_byte(body, OP_I32_ADD); /* +1 for NUL */
    emit_call(body, RT_ALLOC);
    int new_ptr = 9;
    emit_local_set(body, new_ptr);

    /* Copy a bytes: memory.copy not in MVP, use byte-by-byte loop */
    /* We use a simple loop: i=0; while i<a_len: mem[new+i]=mem[a_ptr+i]; i++ */
    {
        int ci = 10;
        emit_i32(body, 0);
        emit_local_set(body, ci);
        buf_byte(body, OP_BLOCK);
        buf_byte(body, WASM_TYPE_VOID);
        buf_byte(body, OP_LOOP);
        buf_byte(body, WASM_TYPE_VOID);
        emit_local_get(body, ci);
        emit_local_get(body, a_len);
        buf_byte(body, OP_I32_GE_S);
        buf_byte(body, OP_BR_IF);
        buf_leb128_u(body, 1);
        /* store byte */
        emit_local_get(body, new_ptr);
        emit_local_get(body, ci);
        buf_byte(body, OP_I32_ADD);
        emit_local_get(body, a_ptr);
        emit_local_get(body, ci);
        buf_byte(body, OP_I32_ADD);
        buf_byte(body, OP_I32_LOAD8_U);
        buf_leb128_u(body, 0);
        buf_leb128_u(body, 0);
        buf_byte(body, OP_I32_STORE8);
        buf_leb128_u(body, 0);
        buf_leb128_u(body, 0);
        /* i++ */
        emit_local_get(body, ci);
        emit_i32(body, 1);
        buf_byte(body, OP_I32_ADD);
        emit_local_set(body, ci);
        buf_byte(body, OP_BR);
        buf_leb128_u(body, 0);
        buf_byte(body, OP_END);
        buf_byte(body, OP_END);
    }

    /* Copy b bytes */
    {
        int ci = 10;
        emit_i32(body, 0);
        emit_local_set(body, ci);
        buf_byte(body, OP_BLOCK);
        buf_byte(body, WASM_TYPE_VOID);
        buf_byte(body, OP_LOOP);
        buf_byte(body, WASM_TYPE_VOID);
        emit_local_get(body, ci);
        emit_local_get(body, b_len);
        buf_byte(body, OP_I32_GE_S);
        buf_byte(body, OP_BR_IF);
        buf_leb128_u(body, 1);
        emit_local_get(body, new_ptr);
        emit_local_get(body, a_len);
        buf_byte(body, OP_I32_ADD);
        emit_local_get(body, ci);
        buf_byte(body, OP_I32_ADD);
        emit_local_get(body, b_ptr);
        emit_local_get(body, ci);
        buf_byte(body, OP_I32_ADD);
        buf_byte(body, OP_I32_LOAD8_U);
        buf_leb128_u(body, 0);
        buf_leb128_u(body, 0);
        buf_byte(body, OP_I32_STORE8);
        buf_leb128_u(body, 0);
        buf_leb128_u(body, 0);
        emit_local_get(body, ci);
        emit_i32(body, 1);
        buf_byte(body, OP_I32_ADD);
        emit_local_set(body, ci);
        buf_byte(body, OP_BR);
        buf_leb128_u(body, 0);
        buf_byte(body, OP_END);
        buf_byte(body, OP_END);
    }

    /* NUL terminate */
    emit_local_get(body, new_ptr);
    emit_local_get(body, total);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, 0);
    buf_byte(body, OP_I32_STORE8);
    buf_leb128_u(body, 0);
    buf_leb128_u(body, 0);

    /* Create string value */
    emit_local_get(body, new_ptr);
    emit_local_get(body, total);
    emit_call(body, RT_STR_NEW);
}

/* $arr_new() -> i32
   Create empty array. Layout in memory: [cap, len, elem0, elem1, ...]
   We wrap it as a value with tag=ARRAY, payload=data_ptr, extra=0 (len) */
static void emit_rt_arr_new(WasmBuf *body) {
    /* Allocate initial space: 4 (cap) + 4 (len) + 8*4 (initial cap=8 elements) */
    emit_i32(body, 4 + 4 + 8 * 4);
    emit_call(body, RT_ALLOC);
    /* local 0 = data_ptr */
    int dp = 0;
    emit_local_tee(body, dp);
    /* cap = 8 */
    emit_i32(body, 8);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    /* len = 0 */
    emit_local_get(body, dp);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, 0);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    /* Create value */
    emit_i32(body, TAG_ARRAY);
    emit_local_get(body, dp);
    emit_call(body, RT_VAL_NEW);
}

/* $arr_push(arr_val: i32, val: i32) -> void
   Push a value onto an array. */
static void emit_rt_arr_push(WasmBuf *body) {
    /* local 0 = arr_val, local 1 = val */
    /* Get data ptr from arr_val */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    int dp = 2;
    emit_local_set(body, dp);

    /* len = mem[dp+4] */
    emit_local_get(body, dp);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    int len = 3;
    emit_local_set(body, len);

    /* Store val at dp + 8 + len * 4 */
    emit_local_get(body, dp);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, len);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_MUL);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, 1);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);

    /* len++ */
    emit_local_get(body, dp);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, len);
    emit_i32(body, 1);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    /* void function - no return value */
}

/* $arr_get(arr_val: i32, idx_val: i32) -> i32 */
static void emit_rt_arr_get(WasmBuf *body) {
    /* local 0 = arr_val, local 1 = idx_val */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32); /* data_ptr */
    int dp = 2;
    emit_local_set(body, dp);
    emit_local_get(body, 1);
    emit_call(body, RT_VAL_I32); /* raw index */
    int idx = 3;
    emit_local_set(body, idx);
    /* result = mem[dp + 8 + idx * 4] */
    emit_local_get(body, dp);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, idx);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_MUL);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
}

/* $arr_len(arr_val: i32) -> i32 (raw i32, not a value) */
static void emit_rt_arr_len(WasmBuf *body) {
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_i32(body, 0);
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    buf_byte(body, OP_END);
}

/* $print_val(val: i32) -> void
   Print a value using fd_write. */
static void emit_rt_print_val(WasmBuf *body) {
    /* local 0 = val */
    /* We need to convert to string, then write via fd_write */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_TO_STR);
    int sv = 1;
    emit_local_set(body, sv);

    /* Get string data ptr and len */
    emit_local_get(body, sv);
    emit_call(body, RT_VAL_I32); /* data ptr */
    int sptr = 2;
    emit_local_set(body, sptr);
    emit_local_get(body, sv);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    int slen = 3;
    emit_local_set(body, slen);

    /* Write iov to a scratch area (we use alloc for simplicity).
       iov = [ptr, len] as two i32s. */
    emit_i32(body, 16);
    emit_call(body, RT_ALLOC);
    int iov = 4;
    emit_local_tee(body, iov);
    emit_local_get(body, sptr);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    emit_local_get(body, iov);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, slen);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);

    /* nwritten slot */
    emit_local_get(body, iov);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    int nw = 5;
    emit_local_set(body, nw);

    /* fd_write(1, iov, 1, nw) */
    emit_i32(body, 1); /* fd = stdout */
    emit_local_get(body, iov);
    emit_i32(body, 1); /* iovs_len */
    emit_local_get(body, nw);
    emit_call(body, IMPORT_FD_WRITE);
    buf_byte(body, OP_DROP); /* drop fd_write return value */
    /* void function - no return value */
}

/* $val_truthy(val: i32) -> i32 (raw 0 or 1) */
static void emit_rt_val_truthy(WasmBuf *body) {
    /* null -> 0, bool -> bval, int -> val!=0, string -> len>0, array -> len>0 */
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_i32(body, 0); /* null pointer -> falsy */
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_TAG);
    int tag = 1;
    emit_local_set(body, tag);
    emit_local_get(body, tag);
    emit_i32(body, TAG_NULL);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_i32(body, 0);
    buf_byte(body, OP_ELSE);
    emit_local_get(body, tag);
    emit_i32(body, TAG_BOOL);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    buf_byte(body, OP_ELSE);
    emit_local_get(body, tag);
    emit_i32(body, TAG_INT);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    emit_i32(body, 0);
    buf_byte(body, OP_I32_NE);
    buf_byte(body, OP_ELSE);
    /* string, array, etc. - check extra field (length) for strings */
    emit_i32(body, 1); /* default truthy for objects */
    buf_byte(body, OP_END);
    buf_byte(body, OP_END);
    buf_byte(body, OP_END);
    buf_byte(body, OP_END);
}

/* $val_eq(a: i32, b: i32) -> i32 (value: bool) */
static void emit_rt_val_eq(WasmBuf *body) {
    /* Compare by tag then payload */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_TAG);
    emit_local_get(body, 1);
    emit_call(body, RT_VAL_TAG);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    /* Same tag: compare payloads */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    emit_local_get(body, 1);
    emit_call(body, RT_VAL_I32);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_ELSE);
    emit_i32(body, 0);
    buf_byte(body, OP_END);
    /* Wrap as bool value */
    int tmp = 2;
    emit_local_set(body, tmp);
    emit_i32(body, TAG_BOOL);
    emit_local_get(body, tmp);
    emit_call(body, RT_VAL_NEW);
}

/* Binary arithmetic runtime function template.
   For int operands, performs the operation on i32 payloads.
   For other types, converts to int and operates. */
static void emit_rt_val_arith(WasmBuf *body, uint8_t op) {
    /* local 0 = a, local 1 = b */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    emit_local_get(body, 1);
    emit_call(body, RT_VAL_I32);
    buf_byte(body, op);
    int r = 2;
    emit_local_set(body, r);
    emit_i32(body, TAG_INT);
    emit_local_get(body, r);
    emit_call(body, RT_VAL_NEW);
}

/* Comparison operator template */
static void emit_rt_val_cmp(WasmBuf *body, uint8_t op) {
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    emit_local_get(body, 1);
    emit_call(body, RT_VAL_I32);
    buf_byte(body, op);
    int r = 2;
    emit_local_set(body, r);
    emit_i32(body, TAG_BOOL);
    emit_local_get(body, r);
    emit_call(body, RT_VAL_NEW);
}

/* Helper: write a short literal string to memory and return str_new */
static void emit_inline_str(WasmBuf *body, const char *s, int local_tmp) {
    int len = (int)strlen(s);
    emit_i32(body, len + 1);
    emit_call(body, RT_ALLOC);
    emit_local_set(body, local_tmp);
    /* Write bytes 4 at a time, then remainder */
    for (int i = 0; i < len; i++) {
        emit_local_get(body, local_tmp);
        if (i > 0) { emit_i32(body, i); buf_byte(body, OP_I32_ADD); }
        emit_i32(body, (uint8_t)s[i]);
        buf_byte(body, OP_I32_STORE8);
        buf_leb128_u(body, 0);
        buf_leb128_u(body, 0);
    }
    /* NUL terminator */
    emit_local_get(body, local_tmp);
    emit_i32(body, len);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, 0);
    buf_byte(body, OP_I32_STORE8);
    buf_leb128_u(body, 0);
    buf_leb128_u(body, 0);
    emit_local_get(body, local_tmp);
    emit_i32(body, len);
    emit_call(body, RT_STR_NEW);
}

/* $val_to_str(val: i32) -> i32 (string value)
   Convert any value to its string representation. */
static void emit_rt_val_to_str(WasmBuf *body) {
    /* local 0 = val, local 1 = scratch */
    /* null pointer */
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_inline_str(body, "null", 1);
    buf_byte(body, OP_ELSE);

    /* Get tag */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_TAG);
    int tag = 1;
    emit_local_set(body, tag);

    /* TAG_NULL */
    emit_local_get(body, tag);
    emit_i32(body, TAG_NULL);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_inline_str(body, "null", 1);
    buf_byte(body, OP_ELSE);

    /* TAG_STRING - return as is */
    emit_local_get(body, tag);
    emit_i32(body, TAG_STRING);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 0);
    buf_byte(body, OP_ELSE);

    /* TAG_BOOL */
    emit_local_get(body, tag);
    emit_i32(body, TAG_BOOL);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_inline_str(body, "true", 1);
    buf_byte(body, OP_ELSE);
    emit_inline_str(body, "false", 1);
    buf_byte(body, OP_END);
    buf_byte(body, OP_ELSE);

    /* TAG_FLOAT - read pre-formatted string from extra field */
    emit_local_get(body, tag);
    emit_i32(body, TAG_FLOAT);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    /* extra field at offset 8 has string table offset, offset 12 has length */
    emit_local_get(body, 0);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0); /* string data offset */
    emit_local_get(body, 0);
    emit_i32(body, 12);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0); /* string length */
    emit_call(body, RT_STR_NEW);
    buf_byte(body, OP_ELSE);

    /* TAG_ARRAY - format as [elem1, elem2, ...] */
    emit_local_get(body, tag);
    emit_i32(body, TAG_ARRAY);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    {
        /* Start with "[" */
        emit_inline_str(body, "[", 6);
        int result = 2;
        emit_local_set(body, result);
        /* Get array length */
        emit_local_get(body, 0);
        emit_call(body, RT_ARR_LEN);
        int len = 3;
        emit_local_set(body, len);
        /* Loop: i = 0; while i < len */
        emit_i32(body, 0);
        int idx = 4;
        emit_local_set(body, idx);
        buf_byte(body, OP_BLOCK); buf_byte(body, WASM_TYPE_VOID);
        buf_byte(body, OP_LOOP); buf_byte(body, WASM_TYPE_VOID);
        /* if i >= len, break */
        emit_local_get(body, idx);
        emit_local_get(body, len);
        buf_byte(body, OP_I32_GE_S);
        buf_byte(body, OP_BR_IF); buf_leb128_u(body, 1);
        /* if i > 0, append ", " */
        emit_local_get(body, idx);
        emit_i32(body, 0);
        buf_byte(body, OP_I32_GT_S);
        buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_VOID);
        emit_local_get(body, result);
        emit_inline_str(body, ", ", 6);
        emit_call(body, RT_STR_CAT);
        emit_local_set(body, result);
        buf_byte(body, OP_END);
        /* Get element: arr data_ptr + 8 + i*4 */
        emit_local_get(body, 0);
        emit_call(body, RT_VAL_I32); /* data_ptr */
        emit_i32(body, 8);
        buf_byte(body, OP_I32_ADD);
        emit_local_get(body, idx);
        emit_i32(body, 4);
        buf_byte(body, OP_I32_MUL);
        buf_byte(body, OP_I32_ADD);
        buf_byte(body, OP_I32_LOAD);
        buf_leb128_u(body, 2);
        buf_leb128_u(body, 0); /* element value ptr */
        emit_call(body, RT_VAL_TO_STR); /* recursive */
        int etmp = 5;
        emit_local_set(body, etmp);
        /* result = str_cat(result, elem_str) */
        emit_local_get(body, result);
        emit_local_get(body, etmp);
        emit_call(body, RT_STR_CAT);
        emit_local_set(body, result);
        /* i++ */
        emit_local_get(body, idx);
        emit_i32(body, 1);
        buf_byte(body, OP_I32_ADD);
        emit_local_set(body, idx);
        buf_byte(body, OP_BR); buf_leb128_u(body, 0);
        buf_byte(body, OP_END); /* loop */
        buf_byte(body, OP_END); /* block */
        /* Append "]" */
        emit_local_get(body, result);
        emit_inline_str(body, "]", 6);
        emit_call(body, RT_STR_CAT);
    }
    buf_byte(body, OP_ELSE);

    /* TAG_MAP */
    emit_local_get(body, tag);
    emit_i32(body, TAG_MAP);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    {
        /* Format as {key: val, ...} */
        emit_inline_str(body, "{", 6);
        int result = 2;
        emit_local_set(body, result);
        /* Get map data ptr -> dp */
        emit_local_get(body, 0);
        emit_call(body, RT_VAL_I32);
        int dp = 3;
        emit_local_set(body, dp);
        /* len at dp+4 */
        emit_local_get(body, dp);
        emit_i32(body, 4);
        buf_byte(body, OP_I32_ADD);
        buf_byte(body, OP_I32_LOAD);
        buf_leb128_u(body, 2);
        buf_leb128_u(body, 0);
        int len = 4;
        emit_local_set(body, len);
        /* Loop */
        emit_i32(body, 0);
        int idx = 5;
        emit_local_set(body, idx);
        buf_byte(body, OP_BLOCK); buf_byte(body, WASM_TYPE_VOID);
        buf_byte(body, OP_LOOP); buf_byte(body, WASM_TYPE_VOID);
        emit_local_get(body, idx);
        emit_local_get(body, len);
        buf_byte(body, OP_I32_GE_S);
        buf_byte(body, OP_BR_IF); buf_leb128_u(body, 1);
        /* separator */
        emit_local_get(body, idx);
        emit_i32(body, 0);
        buf_byte(body, OP_I32_GT_S);
        buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_VOID);
        emit_local_get(body, result);
        emit_inline_str(body, ", ", 6);
        emit_call(body, RT_STR_CAT);
        emit_local_set(body, result);
        buf_byte(body, OP_END);
        /* key at dp + 8 + idx*8 */
        emit_local_get(body, dp);
        emit_i32(body, 8);
        buf_byte(body, OP_I32_ADD);
        emit_local_get(body, idx);
        emit_i32(body, 8);
        buf_byte(body, OP_I32_MUL);
        buf_byte(body, OP_I32_ADD);
        buf_byte(body, OP_I32_LOAD);
        buf_leb128_u(body, 2);
        buf_leb128_u(body, 0);
        emit_call(body, RT_VAL_TO_STR);
        emit_local_set(body, 6);
        emit_local_get(body, result);
        emit_local_get(body, 6);
        emit_call(body, RT_STR_CAT);
        emit_local_set(body, result);
        /* ": " */
        emit_local_get(body, result);
        emit_inline_str(body, ": ", 6);
        emit_call(body, RT_STR_CAT);
        emit_local_set(body, result);
        /* val at dp + 12 + idx*8 */
        emit_local_get(body, dp);
        emit_i32(body, 12);
        buf_byte(body, OP_I32_ADD);
        emit_local_get(body, idx);
        emit_i32(body, 8);
        buf_byte(body, OP_I32_MUL);
        buf_byte(body, OP_I32_ADD);
        buf_byte(body, OP_I32_LOAD);
        buf_leb128_u(body, 2);
        buf_leb128_u(body, 0);
        emit_call(body, RT_VAL_TO_STR);
        emit_local_set(body, 6);
        emit_local_get(body, result);
        emit_local_get(body, 6);
        emit_call(body, RT_STR_CAT);
        emit_local_set(body, result);
        /* i++ */
        emit_local_get(body, idx);
        emit_i32(body, 1);
        buf_byte(body, OP_I32_ADD);
        emit_local_set(body, idx);
        buf_byte(body, OP_BR); buf_leb128_u(body, 0);
        buf_byte(body, OP_END); /* loop */
        buf_byte(body, OP_END); /* block */
        /* Append "}" */
        emit_local_get(body, result);
        emit_inline_str(body, "}", 6);
        emit_call(body, RT_STR_CAT);
    }
    buf_byte(body, OP_ELSE);

    /* TAG_TUPLE */
    emit_local_get(body, tag);
    emit_i32(body, TAG_TUPLE);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    {
        /* Format as (a, b, c) - similar to array but with parens */
        emit_inline_str(body, "(", 6);
        int result = 2;
        emit_local_set(body, result);
        emit_local_get(body, 0);
        emit_call(body, RT_ARR_LEN);
        int len = 3;
        emit_local_set(body, len);
        emit_i32(body, 0);
        int idx = 4;
        emit_local_set(body, idx);
        buf_byte(body, OP_BLOCK); buf_byte(body, WASM_TYPE_VOID);
        buf_byte(body, OP_LOOP); buf_byte(body, WASM_TYPE_VOID);
        emit_local_get(body, idx);
        emit_local_get(body, len);
        buf_byte(body, OP_I32_GE_S);
        buf_byte(body, OP_BR_IF); buf_leb128_u(body, 1);
        emit_local_get(body, idx);
        emit_i32(body, 0);
        buf_byte(body, OP_I32_GT_S);
        buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_VOID);
        emit_local_get(body, result);
        emit_inline_str(body, ", ", 6);
        emit_call(body, RT_STR_CAT);
        emit_local_set(body, result);
        buf_byte(body, OP_END);
        emit_local_get(body, 0);
        emit_call(body, RT_VAL_I32);
        emit_i32(body, 8);
        buf_byte(body, OP_I32_ADD);
        emit_local_get(body, idx);
        emit_i32(body, 4);
        buf_byte(body, OP_I32_MUL);
        buf_byte(body, OP_I32_ADD);
        buf_byte(body, OP_I32_LOAD);
        buf_leb128_u(body, 2);
        buf_leb128_u(body, 0);
        emit_call(body, RT_VAL_TO_STR);
        int etmp = 5;
        emit_local_set(body, etmp);
        emit_local_get(body, result);
        emit_local_get(body, etmp);
        emit_call(body, RT_STR_CAT);
        emit_local_set(body, result);
        emit_local_get(body, idx);
        emit_i32(body, 1);
        buf_byte(body, OP_I32_ADD);
        emit_local_set(body, idx);
        buf_byte(body, OP_BR); buf_leb128_u(body, 0);
        buf_byte(body, OP_END); /* loop */
        buf_byte(body, OP_END); /* block */
        emit_local_get(body, result);
        emit_inline_str(body, ")", 6);
        emit_call(body, RT_STR_CAT);
    }
    buf_byte(body, OP_ELSE);

    /* TAG_INT and everything else */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    emit_call(body, RT_I32_TO_STR);

    buf_byte(body, OP_END); /* tuple */
    buf_byte(body, OP_END); /* map */
    buf_byte(body, OP_END); /* array */
    buf_byte(body, OP_END); /* float */
    buf_byte(body, OP_END); /* bool */
    buf_byte(body, OP_END); /* string */
    buf_byte(body, OP_END); /* null tag */
    buf_byte(body, OP_END); /* null ptr */
}

/* $print_newline() -> void */
static void emit_rt_print_newline(WasmBuf *body) {
    /* Write a newline byte via fd_write */
    emit_i32(body, 16);
    emit_call(body, RT_ALLOC);
    int buf = 0;
    emit_local_set(body, buf);
    /* Write '\n' (0x0A) at buf */
    emit_local_get(body, buf);
    emit_i32(body, 0x0A);
    buf_byte(body, OP_I32_STORE8);
    buf_leb128_u(body, 0);
    buf_leb128_u(body, 0);
    /* iov at buf+4: [ptr=buf, len=1] */
    emit_local_get(body, buf);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, buf);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    emit_local_get(body, buf);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, 1);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    /* nwritten at buf+12 */
    emit_i32(body, 1); /* fd = stdout */
    emit_local_get(body, buf);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD); /* iovs */
    emit_i32(body, 1); /* iovs_len */
    emit_local_get(body, buf);
    emit_i32(body, 12);
    buf_byte(body, OP_I32_ADD); /* nwritten */
    emit_call(body, IMPORT_FD_WRITE);
    buf_byte(body, OP_DROP);
    /* void function - no return value */
}

/* $val_and(a, b) -> a if falsy else b */
static void emit_rt_val_and(WasmBuf *body) {
    /* local 0 = a, local 1 = b, local 2 = tag scratch */
    /* Inline truthiness check for a:
       null ptr -> falsy, TAG_NULL -> falsy,
       TAG_BOOL -> check payload, TAG_INT -> check != 0,
       everything else -> truthy */
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 0); /* null ptr -> return a (falsy) */
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0); /* load tag */
    emit_local_set(body, 2);
    /* Check if TAG_NULL */
    emit_local_get(body, 2);
    emit_i32(body, TAG_NULL);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 0); /* null tag -> return a */
    buf_byte(body, OP_ELSE);
    /* Check if TAG_BOOL or TAG_INT */
    emit_local_get(body, 2);
    emit_i32(body, TAG_BOOL);
    buf_byte(body, OP_I32_EQ);
    emit_local_get(body, 2);
    emit_i32(body, TAG_INT);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_I32_OR);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    /* Check payload */
    emit_local_get(body, 0);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0); /* payload */
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 1); /* truthy -> return b */
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0); /* falsy -> return a */
    buf_byte(body, OP_END);
    buf_byte(body, OP_ELSE);
    /* other types -> truthy, return b */
    emit_local_get(body, 1);
    buf_byte(body, OP_END);
    buf_byte(body, OP_END);
    buf_byte(body, OP_END);
}

/* $val_or(a, b) -> a if truthy else b */
static void emit_rt_val_or(WasmBuf *body) {
    /* local 0 = a, local 1 = b, local 2 = tag scratch */
    /* Inline truthiness check for a */
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 1); /* null ptr -> return b */
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0); /* load tag */
    emit_local_set(body, 2);
    /* Check if TAG_NULL */
    emit_local_get(body, 2);
    emit_i32(body, TAG_NULL);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 1); /* null tag -> return b */
    buf_byte(body, OP_ELSE);
    /* Check if TAG_BOOL or TAG_INT */
    emit_local_get(body, 2);
    emit_i32(body, TAG_BOOL);
    buf_byte(body, OP_I32_EQ);
    emit_local_get(body, 2);
    emit_i32(body, TAG_INT);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_I32_OR);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    /* Check payload */
    emit_local_get(body, 0);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0); /* payload */
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 0); /* truthy -> return a */
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 1); /* falsy -> return b */
    buf_byte(body, OP_END);
    buf_byte(body, OP_ELSE);
    /* other types -> truthy, return a */
    emit_local_get(body, 0);
    buf_byte(body, OP_END);
    buf_byte(body, OP_END);
    buf_byte(body, OP_END);
}

/* $val_neg(a) -> -a (int) */
static void emit_rt_val_neg(WasmBuf *body) {
    emit_i32(body, TAG_INT);
    emit_i32(body, 0);
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    buf_byte(body, OP_I32_SUB);
    emit_call(body, RT_VAL_NEW);
}

/* $val_not(a) -> !a (bool) */
static void emit_rt_val_not(WasmBuf *body) {
    emit_i32(body, TAG_BOOL);
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_TRUTHY);
    buf_byte(body, OP_I32_EQZ);
    emit_call(body, RT_VAL_NEW);
}

/* $map_new() -> i32 (map value)
   Simple map: array of key-value pairs. Layout: [cap, len, k0, v0, k1, v1, ...] */
static void emit_rt_map_new(WasmBuf *body) {
    emit_i32(body, 4 + 4 + 16 * 4);
    emit_call(body, RT_ALLOC);
    int dp = 0;
    emit_local_tee(body, dp);
    emit_i32(body, 8); /* cap = 8 pairs */
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    emit_local_get(body, dp);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, 0); /* len = 0 */
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    emit_i32(body, TAG_MAP);
    emit_local_get(body, dp);
    emit_call(body, RT_VAL_NEW);
}

/* $map_set(map_val, key_val, val_val) -> void */
static void emit_rt_map_set(WasmBuf *body) {
    /* local 0=map, 1=key, 2=val */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    int dp = 3;
    emit_local_set(body, dp);
    emit_local_get(body, dp);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    int len = 4;
    emit_local_set(body, len);
    /* store key at dp + 8 + len * 8 */
    emit_local_get(body, dp);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, len);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_MUL);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, 1);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    /* store val at dp + 8 + len * 8 + 4 */
    emit_local_get(body, dp);
    emit_i32(body, 12);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, len);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_MUL);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, 2);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    /* len++ */
    emit_local_get(body, dp);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, len);
    emit_i32(body, 1);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    /* void function - no return value */
}

/* $map_get(map_val, key_val) -> i32 (value or null) */
static void emit_rt_map_get(WasmBuf *body) {
    /* local 0=map, 1=key, 2=dp, 3=len, 4=i, 5=k_ptr */
    /* Linear scan for matching key by comparing payloads */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    int dp = 2;
    emit_local_set(body, dp);
    /* len at dp+4 */
    emit_local_get(body, dp);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    int len = 3;
    emit_local_set(body, len);
    /* Loop */
    emit_i32(body, 0);
    int idx = 4;
    emit_local_set(body, idx);
    buf_byte(body, OP_BLOCK); buf_byte(body, WASM_TYPE_I32);
    buf_byte(body, OP_LOOP); buf_byte(body, WASM_TYPE_VOID);
    /* if i >= len, return null */
    emit_local_get(body, idx);
    emit_local_get(body, len);
    buf_byte(body, OP_I32_GE_S);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_VOID);
    emit_i32(body, TAG_NULL);
    emit_i32(body, 0);
    emit_call(body, RT_VAL_NEW);
    buf_byte(body, OP_BR); buf_leb128_u(body, 2); /* break with value */
    buf_byte(body, OP_END);
    /* key at dp + 8 + i*8 */
    emit_local_get(body, dp);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, idx);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_MUL);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    int kptr = 5;
    emit_local_set(body, kptr);
    /* Compare key payloads (i32 values - string data ptrs) */
    emit_local_get(body, kptr);
    emit_call(body, RT_VAL_I32);
    emit_local_get(body, 1);
    emit_call(body, RT_VAL_I32);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF); buf_byte(body, WASM_TYPE_VOID);
    /* Found! Return value at dp + 12 + i*8 */
    emit_local_get(body, dp);
    emit_i32(body, 12);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, idx);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_MUL);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    buf_byte(body, OP_BR); buf_leb128_u(body, 2); /* break with value */
    buf_byte(body, OP_END);
    /* i++ */
    emit_local_get(body, idx);
    emit_i32(body, 1);
    buf_byte(body, OP_I32_ADD);
    emit_local_set(body, idx);
    buf_byte(body, OP_BR); buf_leb128_u(body, 0); /* loop */
    buf_byte(body, OP_END); /* loop */
    /* Should not reach here, but provide a default */
    emit_i32(body, TAG_NULL);
    emit_i32(body, 0);
    emit_call(body, RT_VAL_NEW);
    buf_byte(body, OP_END); /* block */
}

/* $val_index(obj, idx) -> i32 */
static void emit_rt_val_index(WasmBuf *body) {
    /* If array/tuple, use arr_get. If map, use map_get. */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_TAG);
    int tag = 2;
    emit_local_set(body, tag);
    emit_local_get(body, tag);
    emit_i32(body, TAG_ARRAY);
    buf_byte(body, OP_I32_EQ);
    emit_local_get(body, tag);
    emit_i32(body, TAG_TUPLE);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_I32_OR);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 0);
    emit_local_get(body, 1);
    emit_call(body, RT_ARR_GET);
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0);
    emit_local_get(body, 1);
    emit_call(body, RT_MAP_GET);
    buf_byte(body, OP_END);
}

/* $val_index_set(obj, idx, val) -> void */
static void emit_rt_val_index_set(WasmBuf *body) {
    /* For arrays: store at position */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    int dp = 3;
    emit_local_set(body, dp);
    emit_local_get(body, 1);
    emit_call(body, RT_VAL_I32);
    int idx = 4;
    emit_local_set(body, idx);
    emit_local_get(body, dp);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, idx);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_MUL);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, 2);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    /* void function - no return value */
}

/* $val_field(obj, name_str) -> i32
   Get field by name - delegates to map_get for map-based objects. */
static void emit_rt_val_field(WasmBuf *body) {
    /* local 0 = obj, local 1 = name_str */
    emit_local_get(body, 0);
    emit_local_get(body, 1);
    emit_call(body, RT_MAP_GET);
}

/* $val_field_set(obj, name_str, val) -> void */
static void emit_rt_val_field_set(WasmBuf *body) {
    /* local 0 = obj, local 1 = name_str, local 2 = val */
    emit_local_get(body, 0);
    emit_local_get(body, 1);
    emit_local_get(body, 2);
    emit_call(body, RT_MAP_SET);
}

/* $struct_new(name_str, n_fields) -> i32 (struct value)
   Allocate struct with space for n_fields. */
static void emit_rt_struct_new(WasmBuf *body) {
    /* local 0 = name_str_val, local 1 = n_fields */
    /* Allocate: 4 (n_fields) + 4 (name_ptr) + n_fields * 12 (name+value pairs) */
    emit_local_get(body, 1);
    emit_i32(body, 4);
    buf_byte(body, OP_I32_MUL);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    emit_call(body, RT_ALLOC);
    int dp = 2;
    emit_local_tee(body, dp);
    emit_local_get(body, 1);
    buf_byte(body, OP_I32_STORE);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    emit_i32(body, TAG_STRUCT);
    emit_local_get(body, dp);
    emit_call(body, RT_VAL_NEW);
}

/* $range_new(start, end, inclusive) -> i32 (range value)
   We store range as an array [start, end, inclusive_flag]. */
static void emit_rt_range_new(WasmBuf *body) {
    /* local 0=start, 1=end, 2=inclusive */
    emit_call(body, RT_ARR_NEW);
    int arr = 3;
    emit_local_set(body, arr);
    emit_local_get(body, arr);
    emit_local_get(body, 0);
    emit_call(body, RT_ARR_PUSH);
    emit_local_get(body, arr);
    emit_local_get(body, 1);
    emit_call(body, RT_ARR_PUSH);
    emit_local_get(body, arr);
    emit_i32(body, TAG_INT);
    emit_local_get(body, 2);
    emit_call(body, RT_VAL_NEW);
    emit_call(body, RT_ARR_PUSH);
    /* Retag as range */
    emit_local_get(body, arr);
    emit_call(body, RT_VAL_I32); /* get data_ptr */
    int dp = 4;
    emit_local_set(body, dp);
    /* Create a range-tagged value */
    emit_i32(body, TAG_RANGE);
    emit_local_get(body, dp);
    emit_call(body, RT_VAL_NEW);
}

/* $val_ne(a, b) -> bool value (not equal) */
static void emit_rt_val_ne(WasmBuf *body) {
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    emit_local_get(body, 1);
    emit_call(body, RT_VAL_I32);
    buf_byte(body, OP_I32_NE);
    int r = 2;
    emit_local_set(body, r);
    emit_i32(body, TAG_BOOL);
    emit_local_get(body, r);
    emit_call(body, RT_VAL_NEW);
}

/* $val_bit_not(a) -> ~a */
static void emit_rt_val_bit_not(WasmBuf *body) {
    emit_i32(body, TAG_INT);
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    emit_i32(body, -1);
    buf_byte(body, OP_I32_XOR);
    emit_call(body, RT_VAL_NEW);
}

/* $tuple_new(arr_val) -> i32 (tuple value)
   Repackage an array value as a tuple. */
static void emit_rt_tuple_new(WasmBuf *body) {
    /* Get data ptr from the array val, create tuple value with same data */
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_I32);
    int dp = 1;
    emit_local_set(body, dp);
    emit_i32(body, TAG_TUPLE);
    emit_local_get(body, dp);
    emit_call(body, RT_VAL_NEW);
}

/* $val_nullcoal(a, b) -> a if not null, else b */
static void emit_rt_val_nullcoal(WasmBuf *body) {
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_TAG);
    emit_i32(body, TAG_NULL);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_local_get(body, 1);
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0);
    buf_byte(body, OP_END);
}

/* $i32_to_str(val: i32) -> i32 (string value)
   Convert a raw i32 to its decimal string representation. */
static void emit_rt_i32_to_str(WasmBuf *body) {
    /* Allocate 12 bytes (enough for -2147483648), write digits backwards */
    emit_i32(body, 12);
    emit_call(body, RT_ALLOC);
    int buf = 1;
    emit_local_set(body, buf);

    /* local 0 = value, 1 = buf, 2 = pos, 3 = neg, 4 = digit, 5 = abs */
    emit_i32(body, 11); /* pos starts at end */
    int pos = 2;
    emit_local_set(body, pos);

    /* NUL terminator */
    emit_local_get(body, buf);
    emit_i32(body, 11);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, 0);
    buf_byte(body, OP_I32_STORE8);
    buf_leb128_u(body, 0);
    buf_leb128_u(body, 0);

    /* Check negative */
    emit_i32(body, 0);
    int neg = 3;
    emit_local_set(body, neg);
    emit_local_get(body, 0);
    emit_i32(body, 0);
    buf_byte(body, OP_I32_LT_S);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_VOID);
    emit_i32(body, 1);
    emit_local_set(body, neg);
    emit_i32(body, 0);
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_SUB);
    emit_local_set(body, 0);
    buf_byte(body, OP_END);

    /* Handle 0 */
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_VOID);
    emit_local_get(body, pos);
    emit_i32(body, 1);
    buf_byte(body, OP_I32_SUB);
    emit_local_set(body, pos);
    emit_local_get(body, buf);
    emit_local_get(body, pos);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, 0x30); /* '0' */
    buf_byte(body, OP_I32_STORE8);
    buf_leb128_u(body, 0);
    buf_leb128_u(body, 0);
    buf_byte(body, OP_ELSE);

    /* Digit loop */
    buf_byte(body, OP_BLOCK);
    buf_byte(body, WASM_TYPE_VOID);
    buf_byte(body, OP_LOOP);
    buf_byte(body, WASM_TYPE_VOID);
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_BR_IF);
    buf_leb128_u(body, 1);
    /* digit = val % 10 */
    emit_local_get(body, 0);
    emit_i32(body, 10);
    buf_byte(body, OP_I32_REM_U);
    int digit = 4;
    emit_local_set(body, digit);
    /* val /= 10 */
    emit_local_get(body, 0);
    emit_i32(body, 10);
    buf_byte(body, OP_I32_DIV_U);
    emit_local_set(body, 0);
    /* pos-- */
    emit_local_get(body, pos);
    emit_i32(body, 1);
    buf_byte(body, OP_I32_SUB);
    emit_local_set(body, pos);
    /* buf[pos] = '0' + digit */
    emit_local_get(body, buf);
    emit_local_get(body, pos);
    buf_byte(body, OP_I32_ADD);
    emit_local_get(body, digit);
    emit_i32(body, 0x30);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_STORE8);
    buf_leb128_u(body, 0);
    buf_leb128_u(body, 0);
    buf_byte(body, OP_BR);
    buf_leb128_u(body, 0);
    buf_byte(body, OP_END);
    buf_byte(body, OP_END);

    buf_byte(body, OP_END); /* end of if-zero-else */

    /* Prepend '-' if negative */
    emit_local_get(body, neg);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_VOID);
    emit_local_get(body, pos);
    emit_i32(body, 1);
    buf_byte(body, OP_I32_SUB);
    emit_local_set(body, pos);
    emit_local_get(body, buf);
    emit_local_get(body, pos);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, 0x2D); /* '-' */
    buf_byte(body, OP_I32_STORE8);
    buf_leb128_u(body, 0);
    buf_leb128_u(body, 0);
    buf_byte(body, OP_END);

    /* Result: str_new(buf + pos, 11 - pos) */
    emit_local_get(body, buf);
    emit_local_get(body, pos);
    buf_byte(body, OP_I32_ADD);
    emit_i32(body, 11);
    emit_local_get(body, pos);
    buf_byte(body, OP_I32_SUB);
    emit_call(body, RT_STR_NEW);
}

/* $str_len(val: i32) -> i32 (int value)
   Return length of string/array/etc. */
static void emit_rt_str_len(WasmBuf *body) {
    emit_local_get(body, 0);
    buf_byte(body, OP_I32_EQZ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    emit_i32(body, TAG_INT);
    emit_i32(body, 0);
    emit_call(body, RT_VAL_NEW);
    buf_byte(body, OP_ELSE);
    emit_local_get(body, 0);
    emit_call(body, RT_VAL_TAG);
    emit_i32(body, TAG_STRING);
    buf_byte(body, OP_I32_EQ);
    buf_byte(body, OP_IF);
    buf_byte(body, WASM_TYPE_I32);
    /* String: length is in extra field */
    emit_i32(body, TAG_INT);
    emit_local_get(body, 0);
    emit_i32(body, 8);
    buf_byte(body, OP_I32_ADD);
    buf_byte(body, OP_I32_LOAD);
    buf_leb128_u(body, 2);
    buf_leb128_u(body, 0);
    emit_call(body, RT_VAL_NEW);
    buf_byte(body, OP_ELSE);
    /* Array/tuple: length from data structure */
    emit_i32(body, TAG_INT);
    emit_local_get(body, 0);
    emit_call(body, RT_ARR_LEN);
    emit_call(body, RT_VAL_NEW);
    buf_byte(body, OP_END);
    buf_byte(body, OP_END);
}

/* ========================================================================
   Collect function declarations from program
   ======================================================================== */

typedef struct {
    Node *node;
    int n_params;
} FuncInfo;

/* Recursively collect lambdas and nested fn decls from any node */
static int collect_nested(Node *node, FuncInfo *out, int max, FuncMap *funcs, int count);

static int collect_nested_list(NodeList *list, FuncInfo *out, int max, FuncMap *funcs, int count) {
    for (int i = 0; i < list->len && count < max; i++)
        count = collect_nested(list->items[i], out, max, funcs, count);
    return count;
}

static int collect_nested(Node *node, FuncInfo *out, int max, FuncMap *funcs, int count) {
    if (!node || count >= max) return count;

    if (node->tag == NODE_LAMBDA) {
        /* Assign a synthetic name */
        char lname[64];
        snprintf(lname, sizeof(lname), "__lambda_%d", count);
        funcs_add(funcs, lname);
        out[count].node = node;
        out[count].n_params = node->lambda.params.len;
        /* Stash the func index in node for later retrieval.
           We abuse the is_generator field's high bits for this. */
        node->lambda.is_generator = (node->lambda.is_generator & 1) | ((count) << 16);
        count++;
        /* Also scan lambda body for nested lambdas */
        count = collect_nested(node->lambda.body, out, max, funcs, count);
        return count;
    }

    /* Scan children based on node type */
    switch (node->tag) {
    case NODE_BLOCK:
        count = collect_nested_list(&node->block.stmts, out, max, funcs, count);
        if (node->block.expr) count = collect_nested(node->block.expr, out, max, funcs, count);
        break;
    case NODE_LET: case NODE_VAR:
        if (node->let.value) count = collect_nested(node->let.value, out, max, funcs, count);
        break;
    case NODE_CONST:
        if (node->const_.value) count = collect_nested(node->const_.value, out, max, funcs, count);
        break;
    case NODE_EXPR_STMT:
        if (node->expr_stmt.expr) count = collect_nested(node->expr_stmt.expr, out, max, funcs, count);
        break;
    case NODE_ASSIGN:
        if (node->assign.value) count = collect_nested(node->assign.value, out, max, funcs, count);
        break;
    case NODE_CALL:
        count = collect_nested(node->call.callee, out, max, funcs, count);
        count = collect_nested_list(&node->call.args, out, max, funcs, count);
        break;
    case NODE_BINOP:
        count = collect_nested(node->binop.left, out, max, funcs, count);
        count = collect_nested(node->binop.right, out, max, funcs, count);
        break;
    case NODE_IF:
        count = collect_nested(node->if_expr.then, out, max, funcs, count);
        if (node->if_expr.else_branch)
            count = collect_nested(node->if_expr.else_branch, out, max, funcs, count);
        count = collect_nested_list(&node->if_expr.elif_thens, out, max, funcs, count);
        break;
    case NODE_FOR:
        count = collect_nested(node->for_loop.body, out, max, funcs, count);
        break;
    case NODE_WHILE:
        count = collect_nested(node->while_loop.body, out, max, funcs, count);
        break;
    case NODE_RETURN:
        if (node->ret.value) count = collect_nested(node->ret.value, out, max, funcs, count);
        break;
    case NODE_FN_DECL:
        /* Nested fn decl - collect it and scan its body */
        if (node->fn_decl.name) {
            /* Only collect if not already known (top-level ones are collected separately) */
            if (funcs_find(funcs, node->fn_decl.name) < 0) {
                funcs_add(funcs, node->fn_decl.name);
                out[count].node = node;
                out[count].n_params = node->fn_decl.params.len;
                count++;
            }
        }
        if (node->fn_decl.body) count = collect_nested(node->fn_decl.body, out, max, funcs, count);
        break;
    default:
        break;
    }
    return count;
}

static int collect_functions(Node *program, FuncInfo *out, int max, FuncMap *funcs) {
    if (!program || program->tag != NODE_PROGRAM) return 0;
    int count = 0;
    NodeList *stmts = &program->program.stmts;
    for (int i = 0; i < stmts->len && count < max; i++) {
        Node *s = stmts->items[i];
        if (s && s->tag == NODE_FN_DECL && s->fn_decl.name) {
            funcs_add(funcs, s->fn_decl.name);
            out[count].node = s;
            out[count].n_params = s->fn_decl.params.len;
            count++;
        }
        /* Also collect methods from impl blocks */
        if (s && s->tag == NODE_IMPL_DECL) {
            for (int j = 0; j < s->impl_decl.members.len && count < max; j++) {
                Node *m = s->impl_decl.members.items[j];
                if (m && m->tag == NODE_FN_DECL && m->fn_decl.name) {
                    funcs_add(funcs, m->fn_decl.name);
                    out[count].node = m;
                    out[count].n_params = m->fn_decl.params.len;
                    count++;
                }
            }
        }
        /* Methods from class declarations */
        if (s && s->tag == NODE_CLASS_DECL) {
            for (int j = 0; j < s->class_decl.members.len && count < max; j++) {
                Node *m = s->class_decl.members.items[j];
                if (m && m->tag == NODE_FN_DECL && m->fn_decl.name) {
                    funcs_add(funcs, m->fn_decl.name);
                    out[count].node = m;
                    out[count].n_params = m->fn_decl.params.len;
                    count++;
                }
            }
        }
    }
    /* Recursively scan for lambdas and nested fn decls */
    count = collect_nested_list(stmts, out, max, funcs, count);
    return count;
}

/* ========================================================================
   Build a single runtime function body with proper local declarations
   ======================================================================== */

typedef struct {
    int n_params;
    int n_extra_locals;
    void (*emit_fn)(WasmBuf *body);
    uint8_t arith_op; /* for arithmetic template functions */
} RtFuncSpec;

static void build_rt_func(WasmBuf *out, int n_params, int n_extra,
                           void (*emit_fn)(WasmBuf *body)) {
    WasmBuf body;
    buf_init(&body);
    emit_fn(&body);
    buf_byte(&body, OP_END);

    WasmBuf func;
    buf_init(&func);
    if (n_extra > 0) {
        buf_leb128_u(&func, 1);
        buf_leb128_u(&func, (uint32_t)n_extra);
        buf_byte(&func, WASM_TYPE_I32);
    } else {
        buf_leb128_u(&func, 0);
    }
    buf_append(&func, &body);

    buf_leb128_u(out, (uint32_t)func.len);
    buf_append(out, &func);

    buf_free(&func);
    buf_free(&body);
}

static void build_rt_arith_func(WasmBuf *out, int n_params, int n_extra, uint8_t op) {
    WasmBuf body;
    buf_init(&body);
    emit_rt_val_arith(&body, op);
    buf_byte(&body, OP_END);

    WasmBuf func;
    buf_init(&func);
    if (n_extra > 0) {
        buf_leb128_u(&func, 1);
        buf_leb128_u(&func, (uint32_t)n_extra);
        buf_byte(&func, WASM_TYPE_I32);
    } else {
        buf_leb128_u(&func, 0);
    }
    buf_append(&func, &body);

    buf_leb128_u(out, (uint32_t)func.len);
    buf_append(out, &func);

    buf_free(&func);
    buf_free(&body);
}

static void build_rt_cmp_func(WasmBuf *out, int n_params, int n_extra, uint8_t op) {
    WasmBuf body;
    buf_init(&body);
    emit_rt_val_cmp(&body, op);
    buf_byte(&body, OP_END);

    WasmBuf func;
    buf_init(&func);
    if (n_extra > 0) {
        buf_leb128_u(&func, 1);
        buf_leb128_u(&func, (uint32_t)n_extra);
        buf_byte(&func, WASM_TYPE_I32);
    } else {
        buf_leb128_u(&func, 0);
    }
    buf_append(&func, &body);

    buf_leb128_u(out, (uint32_t)func.len);
    buf_append(out, &func);

    buf_free(&func);
    buf_free(&body);
}

/* ========================================================================
   Main transpiler entry point
   ======================================================================== */

/* Map arity to type index for (i32 x N) -> i32 user functions */
static int arity_to_type(int arity) {
    /* type 1: () -> i32, type 2: (i32) -> i32, type 3: (i32,i32) -> i32,
       type 5: (i32,i32,i32) -> i32, type 9: (i32 x4) -> i32,
       type 10..13: (i32 x 5..8) -> i32 */
    switch (arity) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 3;
    case 3: return 5;
    case 4: return 9;
    case 5: return 10;
    case 6: return 11;
    case 7: return 12;
    case 8: return 13;
    default: return 2; /* fallback, should not happen for typical code */
    }
}

int transpile_wasm(Node *program, const char *filename, const char *out_path) {
    (void)filename;

    if (!program || !out_path) return 1;

    FuncMap funcs;
    funcs_init(&funcs);

    StringTable strtab;
    strtab_init(&strtab);

    StructLayoutMap struct_layouts;
    struct_layouts_init(&struct_layouts);

    EnumLayoutMap enum_layouts;
    enum_layouts_init(&enum_layouts);

    /* Pre-scan: collect struct and enum layouts */
    if (program->tag == NODE_PROGRAM) {
        NodeList *stmts = &program->program.stmts;
        for (int i = 0; i < stmts->len; i++) {
            Node *s = stmts->items[i];
            if (s && s->tag == NODE_STRUCT_DECL && s->struct_decl.name) {
                struct_layouts_add(&struct_layouts, s->struct_decl.name,
                                   &s->struct_decl.fields, &strtab);
            }
            if (s && s->tag == NODE_ENUM_DECL && s->enum_decl.name) {
                enum_layouts_add(&enum_layouts, s->enum_decl.name,
                                 &s->enum_decl.variants);
            }
        }
    }

    /* Collect user function declarations */
    FuncInfo fn_infos[MAX_FUNCS];
    int n_funcs = collect_functions(program, fn_infos, MAX_FUNCS, &funcs);

    int has_main = (funcs_find(&funcs, "main") >= 0);
    int main_func_idx = -1;

    if (!has_main) {
        main_func_idx = funcs_add(&funcs, "main");
    } else {
        main_func_idx = funcs_find(&funcs, "main");
    }

    int total_user_funcs = n_funcs + (has_main ? 0 : 1);

    /* Pre-compile function bodies */
    WasmBuf *compiled_funcs = calloc((size_t)total_user_funcs, sizeof(WasmBuf));
    int *local_counts = calloc((size_t)total_user_funcs, sizeof(int));
    int *param_counts = calloc((size_t)total_user_funcs, sizeof(int));

    for (int i = 0; i < n_funcs; i++) {
        Node *fn = fn_infos[i].node;
        WasmBuf body;
        buf_init(&body);

        LocalMap locals;
        locals_init(&locals);

        CompilerCtx ctx;
        ctx.funcs = &funcs;
        ctx.strtab = &strtab;
        ctx.structs = &struct_layouts;
        ctx.enums = &enum_layouts;
        defer_list_init(&ctx.defers);
        ctx.loop_depth = 0;
        ctx.in_loop = 0;
        ctx.break_depth = 1;
        ctx.continue_depth = 0;

        /* Add parameters */
        ParamList *params;
        Node *fn_body;
        if (fn->tag == NODE_LAMBDA) {
            params = &fn->lambda.params;
            fn_body = fn->lambda.body;
        } else {
            params = &fn->fn_decl.params;
            fn_body = fn->fn_decl.body;
        }
        for (int p = 0; p < params->len; p++) {
            const char *pname = params->items[p].name;
            if (pname) locals_add(&locals, pname);
            else locals_add(&locals, "_");
        }

        /* Compile body */
        if (fn_body) {
            if (fn_body->tag == NODE_BLOCK) {
                NodeList *stmts = &fn_body->block.stmts;
                for (int si = 0; si < stmts->len; si++)
                    compile_stmt(stmts->items[si], &body, &locals, &ctx);
                if (fn_body->block.expr)
                    compile_expr(fn_body->block.expr, &body, &locals, &ctx);
                else
                    emit_null(&body);
            } else {
                compile_expr(fn_body, &body, &locals, &ctx);
            }
        } else {
            emit_null(&body);
        }

        /* Emit deferred statements */
        emit_defers(&body, &locals, &ctx);

        buf_byte(&body, OP_END);

        param_counts[i] = params->len;
        local_counts[i] = locals.n_locals;
        compiled_funcs[i] = body;
        locals_free(&locals);
    }

    /* Synthesized main function */
    if (!has_main) {
        int mi = n_funcs;
        WasmBuf body;
        buf_init(&body);

        LocalMap locals;
        locals_init(&locals);

        CompilerCtx ctx;
        ctx.funcs = &funcs;
        ctx.strtab = &strtab;
        ctx.structs = &struct_layouts;
        ctx.enums = &enum_layouts;
        defer_list_init(&ctx.defers);
        ctx.loop_depth = 0;
        ctx.in_loop = 0;
        ctx.break_depth = 1;
        ctx.continue_depth = 0;

        if (program->tag == NODE_PROGRAM) {
            NodeList *stmts = &program->program.stmts;
            for (int i = 0; i < stmts->len; i++) {
                Node *s = stmts->items[i];
                if (s && s->tag != NODE_FN_DECL && s->tag != NODE_STRUCT_DECL &&
                    s->tag != NODE_ENUM_DECL && s->tag != NODE_CLASS_DECL &&
                    s->tag != NODE_IMPL_DECL && s->tag != NODE_TRAIT_DECL) {
                    compile_stmt(s, &body, &locals, &ctx);
                }
            }
        }

        emit_defers(&body, &locals, &ctx);
        buf_byte(&body, OP_END);

        param_counts[mi] = 0;
        local_counts[mi] = locals.n_locals;
        compiled_funcs[mi] = body;
        locals_free(&locals);
    }

    /* Heap starts after the string data segment, aligned to 16 */
    int heap_start = (strtab.total_len + 15) & ~15;
    if (heap_start < 1024) heap_start = 1024; /* leave room for scratch */

    WasmBuf output;
    buf_init(&output);

    /* WASM header */
    buf_bytes(&output, (const uint8_t *)"\0asm", 4);
    uint8_t ver[4] = {1, 0, 0, 0};
    buf_bytes(&output, ver, 4);

    /* ================================================================
       Section 1: Type section
       ================================================================ */
    {
        WasmBuf sec;
        buf_init(&sec);

        /* Type signatures:
           0: fd_write (i32, i32, i32, i32) -> i32
           1: () -> i32                          (nullary returning val)
           2: (i32) -> i32                       (unary)
           3: (i32, i32) -> i32                  (binary returning val)
           4: (i32, i32) -> void                 (binary void)
           5: (i32, i32, i32) -> i32             (ternary returning val)
           6: (i32, i32, i32) -> void            (ternary void)
           7: () -> void                         (nullary void)
           8: (i32) -> void                      (unary void)
           9: (i32,i32,i32,i32) -> i32           (4-ary returning val)
          10: (i32 x5) -> i32
          11: (i32 x6) -> i32
          12: (i32 x7) -> i32
          13: (i32 x8) -> i32
          14+: user function types (only for arities > 8)
        */

        /* Figure out which user funcs need custom types (arity > 8) */
        int n_base_types = 14;
        int n_custom_types = 0;
        for (int i = 0; i < n_funcs; i++) {
            if (fn_infos[i].n_params > 8) n_custom_types++;
        }
        int total_types = n_base_types + n_custom_types + (has_main ? 0 : 1);
        buf_leb128_u(&sec, (uint32_t)total_types);

        /* type 0: (i32, i32, i32, i32) -> i32 (fd_write signature) */
        buf_byte(&sec, 0x60);
        buf_leb128_u(&sec, 4);
        for (int j = 0; j < 4; j++) buf_byte(&sec, WASM_TYPE_I32);
        buf_leb128_u(&sec, 1);
        buf_byte(&sec, WASM_TYPE_I32);

        /* types 1-5, 9-13: (i32 x N) -> i32 for N = 0..8 */
        /* type 1: () -> i32 */
        buf_byte(&sec, 0x60); buf_leb128_u(&sec, 0);
        buf_leb128_u(&sec, 1); buf_byte(&sec, WASM_TYPE_I32);

        /* type 2: (i32) -> i32 */
        buf_byte(&sec, 0x60); buf_leb128_u(&sec, 1); buf_byte(&sec, WASM_TYPE_I32);
        buf_leb128_u(&sec, 1); buf_byte(&sec, WASM_TYPE_I32);

        /* type 3: (i32, i32) -> i32 */
        buf_byte(&sec, 0x60); buf_leb128_u(&sec, 2);
        buf_byte(&sec, WASM_TYPE_I32); buf_byte(&sec, WASM_TYPE_I32);
        buf_leb128_u(&sec, 1); buf_byte(&sec, WASM_TYPE_I32);

        /* type 4: (i32, i32) -> void */
        buf_byte(&sec, 0x60); buf_leb128_u(&sec, 2);
        buf_byte(&sec, WASM_TYPE_I32); buf_byte(&sec, WASM_TYPE_I32);
        buf_leb128_u(&sec, 0);

        /* type 5: (i32, i32, i32) -> i32 */
        buf_byte(&sec, 0x60); buf_leb128_u(&sec, 3);
        for (int j = 0; j < 3; j++) buf_byte(&sec, WASM_TYPE_I32);
        buf_leb128_u(&sec, 1); buf_byte(&sec, WASM_TYPE_I32);

        /* type 6: (i32, i32, i32) -> void */
        buf_byte(&sec, 0x60); buf_leb128_u(&sec, 3);
        for (int j = 0; j < 3; j++) buf_byte(&sec, WASM_TYPE_I32);
        buf_leb128_u(&sec, 0);

        /* type 7: () -> void */
        buf_byte(&sec, 0x60); buf_leb128_u(&sec, 0); buf_leb128_u(&sec, 0);

        /* type 8: (i32) -> void */
        buf_byte(&sec, 0x60); buf_leb128_u(&sec, 1); buf_byte(&sec, WASM_TYPE_I32);
        buf_leb128_u(&sec, 0);

        /* type 9: (i32 x4) -> i32 (same as type 0 but semantically for user funcs) */
        buf_byte(&sec, 0x60); buf_leb128_u(&sec, 4);
        for (int j = 0; j < 4; j++) buf_byte(&sec, WASM_TYPE_I32);
        buf_leb128_u(&sec, 1); buf_byte(&sec, WASM_TYPE_I32);

        /* types 10-13: (i32 x5..8) -> i32 */
        for (int arity = 5; arity <= 8; arity++) {
            buf_byte(&sec, 0x60); buf_leb128_u(&sec, (uint32_t)arity);
            for (int j = 0; j < arity; j++) buf_byte(&sec, WASM_TYPE_I32);
            buf_leb128_u(&sec, 1); buf_byte(&sec, WASM_TYPE_I32);
        }

        /* Custom types for user functions with arity > 8 */
        for (int i = 0; i < n_funcs; i++) {
            if (fn_infos[i].n_params > 8) {
                buf_byte(&sec, 0x60);
                buf_leb128_u(&sec, (uint32_t)fn_infos[i].n_params);
                for (int p = 0; p < fn_infos[i].n_params; p++)
                    buf_byte(&sec, WASM_TYPE_I32);
                buf_leb128_u(&sec, 1);
                buf_byte(&sec, WASM_TYPE_I32);
            }
        }
        if (!has_main) {
            /* _start: () -> void (WASI convention) */
            buf_byte(&sec, 0x60);
            buf_leb128_u(&sec, 0);
            buf_leb128_u(&sec, 0);
        }

        buf_section(&output, 1, &sec);
        buf_free(&sec);
    }

    /* ================================================================
       Section 2: Import section
       ================================================================ */
    {
        WasmBuf sec;
        buf_init(&sec);

        buf_leb128_u(&sec, NUM_IMPORTS);

        /* import 0: wasi_snapshot_preview1.fd_write -> type 0 */
        buf_name(&sec, "wasi_snapshot_preview1");
        buf_name(&sec, "fd_write");
        buf_byte(&sec, 0x00);
        buf_leb128_u(&sec, 0);

        buf_section(&output, 2, &sec);
        buf_free(&sec);
    }

    /* ================================================================
       Section 3: Function section
       ================================================================ */
    {
        WasmBuf sec;
        buf_init(&sec);

        int total_funcs = NUM_RT_FUNCS + total_user_funcs;
        buf_leb128_u(&sec, (uint32_t)total_funcs);

        /* Runtime function type indices */
        /* RT_ALLOC: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_VAL_NEW: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_VAL_TAG: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_VAL_I32: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_VAL_F64_BITS: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_STR_NEW: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_STR_CAT: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_ARR_NEW: () -> i32 = type 1 */
        buf_leb128_u(&sec, 1);
        /* RT_ARR_PUSH: (i32, i32) -> void = type 4 */
        buf_leb128_u(&sec, 4);
        /* RT_ARR_GET: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_ARR_LEN: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_PRINT_VAL: (i32) -> void = type 8 */
        buf_leb128_u(&sec, 8);
        /* RT_VAL_TRUTHY: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_VAL_EQ: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_VAL_ADD through RT_VAL_MOD: all (i32, i32) -> i32 = type 3 */
        for (int i = 0; i < 5; i++) buf_leb128_u(&sec, 3);
        /* RT_VAL_LT through RT_VAL_GE: (i32, i32) -> i32 = type 3 */
        for (int i = 0; i < 4; i++) buf_leb128_u(&sec, 3);
        /* RT_VAL_NEG: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_VAL_NOT: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_VAL_TO_STR: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_MAP_NEW: () -> i32 = type 1 */
        buf_leb128_u(&sec, 1);
        /* RT_MAP_SET: (i32, i32, i32) -> void = type 6 */
        buf_leb128_u(&sec, 6);
        /* RT_MAP_GET: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_VAL_INDEX: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_VAL_INDEX_SET: (i32, i32, i32) -> void = type 6 */
        buf_leb128_u(&sec, 6);
        /* RT_VAL_FIELD: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_VAL_FIELD_SET: (i32, i32, i32) -> void = type 6 */
        buf_leb128_u(&sec, 6);
        /* RT_STRUCT_NEW: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_PRINT_NEWLINE: () -> void = type 7 */
        buf_leb128_u(&sec, 7);
        /* RT_VAL_AND, RT_VAL_OR: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        buf_leb128_u(&sec, 3);
        /* RT_VAL_BIT_AND through RT_VAL_SHR: (i32, i32) -> i32 = type 3 */
        for (int i = 0; i < 5; i++) buf_leb128_u(&sec, 3);
        /* RT_VAL_POW: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_RANGE_NEW: (i32, i32, i32) -> i32 = type 5 */
        buf_leb128_u(&sec, 5);
        /* RT_VAL_NE: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_VAL_INTDIV: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_VAL_BIT_NOT: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_TUPLE_NEW: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_VAL_NULLCOAL: (i32, i32) -> i32 = type 3 */
        buf_leb128_u(&sec, 3);
        /* RT_I32_TO_STR: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);
        /* RT_STR_LEN: (i32) -> i32 = type 2 */
        buf_leb128_u(&sec, 2);

        /* User function type indices - use arity-based types */
        for (int i = 0; i < n_funcs; i++) {
            buf_leb128_u(&sec, (uint32_t)arity_to_type(fn_infos[i].n_params));
        }
        if (!has_main) {
            /* Synthesized main: () -> void = type 7 */
            buf_leb128_u(&sec, 7);
        }

        buf_section(&output, 3, &sec);
        buf_free(&sec);
    }

    /* ================================================================
       Section 4: Table section
       ================================================================ */
    {
        WasmBuf sec;
        buf_init(&sec);
        buf_leb128_u(&sec, 1);
        buf_byte(&sec, 0x70); /* funcref */
        buf_byte(&sec, 0x00); /* min only */
        int tbl_size = NUM_RT_FUNCS + total_user_funcs;
        if (tbl_size < 1) tbl_size = 1;
        buf_leb128_u(&sec, (uint32_t)tbl_size);
        buf_section(&output, 4, &sec);
        buf_free(&sec);
    }

    /* ================================================================
       Section 5: Memory section
       ================================================================ */
    {
        WasmBuf sec;
        buf_init(&sec);
        buf_leb128_u(&sec, 1);
        buf_byte(&sec, 0x00); /* min only */
        buf_leb128_u(&sec, 2); /* 2 pages = 128KB */
        buf_section(&output, 5, &sec);
        buf_free(&sec);
    }

    /* ================================================================
       Section 6: Global section
       ================================================================ */
    {
        WasmBuf sec;
        buf_init(&sec);
        buf_leb128_u(&sec, NUM_GLOBALS);

        /* GLOBAL_HEAP_PTR: mutable i32 */
        buf_byte(&sec, WASM_TYPE_I32);
        buf_byte(&sec, 0x01);
        buf_byte(&sec, OP_I32_CONST);
        buf_leb128_s(&sec, (int32_t)heap_start);
        buf_byte(&sec, OP_END);

        /* GLOBAL_ERR_FLAG: mutable i32, init 0 */
        buf_byte(&sec, WASM_TYPE_I32);
        buf_byte(&sec, 0x01);
        buf_byte(&sec, OP_I32_CONST);
        buf_leb128_s(&sec, 0);
        buf_byte(&sec, OP_END);

        /* GLOBAL_ERR_VAL: mutable i32, init 0 */
        buf_byte(&sec, WASM_TYPE_I32);
        buf_byte(&sec, 0x01);
        buf_byte(&sec, OP_I32_CONST);
        buf_leb128_s(&sec, 0);
        buf_byte(&sec, OP_END);

        buf_section(&output, 6, &sec);
        buf_free(&sec);
    }

    /* ================================================================
       Section 7: Export section
       ================================================================ */
    {
        WasmBuf sec;
        buf_init(&sec);
        buf_leb128_u(&sec, 2);

        /* Export _start (WASI convention) pointing to main */
        buf_name(&sec, "_start");
        buf_byte(&sec, 0x00);
        buf_leb128_u(&sec, (uint32_t)(USER_FUNC_BASE + main_func_idx));

        /* Export memory */
        buf_name(&sec, "memory");
        buf_byte(&sec, 0x02);
        buf_leb128_u(&sec, 0);

        buf_section(&output, 7, &sec);
        buf_free(&sec);
    }

    /* ================================================================
       Section 9: Element section (function table)
       ================================================================ */
    {
        int total_in_table = NUM_RT_FUNCS + total_user_funcs;
        if (total_in_table > 0) {
            WasmBuf sec;
            buf_init(&sec);
            buf_leb128_u(&sec, 1);
            buf_leb128_u(&sec, 0); /* table 0 */
            buf_byte(&sec, OP_I32_CONST);
            buf_leb128_s(&sec, 0);
            buf_byte(&sec, OP_END);
            buf_leb128_u(&sec, (uint32_t)total_in_table);
            for (int i = 0; i < total_in_table; i++)
                buf_leb128_u(&sec, (uint32_t)(NUM_IMPORTS + i));
            buf_section(&output, 9, &sec);
            buf_free(&sec);
        }
    }

    /* ================================================================
       Section 10: Code section
       ================================================================ */
    {
        WasmBuf sec;
        buf_init(&sec);

        int total_code_funcs = NUM_RT_FUNCS + total_user_funcs;
        buf_leb128_u(&sec, (uint32_t)total_code_funcs);

        /* Emit runtime function bodies */
        /* 0: $alloc (1 param, 0 extra) */
        build_rt_func(&sec, 1, 0, emit_rt_alloc);
        /* 1: $val_new (2 params, 1 extra: ptr) */
        build_rt_func(&sec, 2, 1, emit_rt_val_new);
        /* 2: $val_tag (1 param, 1 extra: tag) */
        build_rt_func(&sec, 1, 1, emit_rt_val_tag);
        /* 3: $val_i32 (1 param, 0 extra) */
        build_rt_func(&sec, 1, 0, emit_rt_val_i32);
        /* 4: $val_f64_bits (1 param, 0 extra) */
        build_rt_func(&sec, 1, 0, emit_rt_val_f64_bits);
        /* 5: $str_new (2 params, 1 extra: ptr) */
        build_rt_func(&sec, 2, 1, emit_rt_str_new);
        /* 6: $str_cat (2 params, 9 extra) */
        build_rt_func(&sec, 2, 9, emit_rt_str_cat);
        /* 7: $arr_new (0 params, 1 extra: dp) */
        build_rt_func(&sec, 0, 1, emit_rt_arr_new);
        /* 8: $arr_push (2 params, 2 extra) */
        build_rt_func(&sec, 2, 2, emit_rt_arr_push);
        /* 9: $arr_get (2 params, 2 extra) */
        build_rt_func(&sec, 2, 2, emit_rt_arr_get);
        /* 10: $arr_len (1 param, 0 extra) */
        build_rt_func(&sec, 1, 0, emit_rt_arr_len);
        /* 11: $print_val (1 param, 5 extra) */
        build_rt_func(&sec, 1, 5, emit_rt_print_val);
        /* 12: $val_truthy (1 param, 1 extra) */
        build_rt_func(&sec, 1, 1, emit_rt_val_truthy);
        /* 13: $val_eq (2 params, 1 extra) */
        build_rt_func(&sec, 2, 1, emit_rt_val_eq);
        /* 14-18: $val_add, $val_sub, $val_mul, $val_div, $val_mod */
        build_rt_arith_func(&sec, 2, 1, OP_I32_ADD);
        build_rt_arith_func(&sec, 2, 1, OP_I32_SUB);
        build_rt_arith_func(&sec, 2, 1, OP_I32_MUL);
        build_rt_arith_func(&sec, 2, 1, OP_I32_DIV_S);
        build_rt_arith_func(&sec, 2, 1, OP_I32_REM_S);
        /* 19-22: $val_lt, $val_gt, $val_le, $val_ge */
        build_rt_cmp_func(&sec, 2, 1, OP_I32_LT_S);
        build_rt_cmp_func(&sec, 2, 1, OP_I32_GT_S);
        build_rt_cmp_func(&sec, 2, 1, OP_I32_LE_S);
        build_rt_cmp_func(&sec, 2, 1, OP_I32_GE_S);
        /* 23: $val_neg */
        build_rt_func(&sec, 1, 0, emit_rt_val_neg);
        /* 24: $val_not */
        build_rt_func(&sec, 1, 0, emit_rt_val_not);
        /* 25: $val_to_str */
        build_rt_func(&sec, 1, 6, emit_rt_val_to_str);
        /* 26: $map_new */
        build_rt_func(&sec, 0, 1, emit_rt_map_new);
        /* 27: $map_set (3 params, 2 extra) */
        build_rt_func(&sec, 3, 2, emit_rt_map_set);
        /* 28: $map_get (2 params, 4 extra: dp, len, i, kptr) */
        build_rt_func(&sec, 2, 4, emit_rt_map_get);
        /* 29: $val_index */
        build_rt_func(&sec, 2, 1, emit_rt_val_index);
        /* 30: $val_index_set */
        build_rt_func(&sec, 3, 2, emit_rt_val_index_set);
        /* 31: $val_field (stub) */
        build_rt_func(&sec, 2, 0, emit_rt_val_field);
        /* 32: $val_field_set (stub) */
        build_rt_func(&sec, 3, 0, emit_rt_val_field_set);
        /* 33: $struct_new */
        build_rt_func(&sec, 2, 1, emit_rt_struct_new);
        /* 34: $print_newline */
        build_rt_func(&sec, 0, 1, emit_rt_print_newline);
        /* 35: $val_and */
        build_rt_func(&sec, 2, 1, emit_rt_val_and);
        /* 36: $val_or */
        build_rt_func(&sec, 2, 1, emit_rt_val_or);
        /* 37-41: bit ops */
        build_rt_arith_func(&sec, 2, 1, OP_I32_AND);
        build_rt_arith_func(&sec, 2, 1, OP_I32_OR);
        build_rt_arith_func(&sec, 2, 1, OP_I32_XOR);
        build_rt_arith_func(&sec, 2, 1, OP_I32_SHL);
        build_rt_arith_func(&sec, 2, 1, OP_I32_SHR_S);
        /* 42: $val_pow (simplified: use mul as placeholder) */
        build_rt_arith_func(&sec, 2, 1, OP_I32_MUL);
        /* 43: $range_new */
        build_rt_func(&sec, 3, 2, emit_rt_range_new);
        /* 44: $val_ne */
        build_rt_func(&sec, 2, 1, emit_rt_val_ne);
        /* 45: $val_intdiv */
        build_rt_arith_func(&sec, 2, 1, OP_I32_DIV_S);
        /* 46: $val_bit_not */
        build_rt_func(&sec, 1, 0, emit_rt_val_bit_not);
        /* 47: $tuple_new */
        build_rt_func(&sec, 1, 1, emit_rt_tuple_new);
        /* 48: $val_nullcoal */
        build_rt_func(&sec, 2, 0, emit_rt_val_nullcoal);
        /* 49: $i32_to_str */
        build_rt_func(&sec, 1, 5, emit_rt_i32_to_str);
        /* 50: $str_len */
        build_rt_func(&sec, 1, 0, emit_rt_str_len);

        /* User function bodies */
        for (int i = 0; i < total_user_funcs; i++) {
            WasmBuf func;
            buf_init(&func);

            int n_params = param_counts[i];
            int extra_locals = local_counts[i] - n_params;
            if (extra_locals > 0) {
                buf_leb128_u(&func, 1);
                buf_leb128_u(&func, (uint32_t)extra_locals);
                buf_byte(&func, WASM_TYPE_I32);
            } else {
                buf_leb128_u(&func, 0);
            }
            buf_append(&func, &compiled_funcs[i]);

            buf_leb128_u(&sec, (uint32_t)func.len);
            buf_append(&sec, &func);

            buf_free(&func);
            buf_free(&compiled_funcs[i]);
        }

        buf_section(&output, 10, &sec);
        buf_free(&sec);
    }

    free(compiled_funcs);
    free(local_counts);
    free(param_counts);

    /* ================================================================
       Section 11: Data section
       ================================================================ */
    if (strtab.count > 0) {
        WasmBuf sec;
        buf_init(&sec);

        buf_leb128_u(&sec, 1);
        buf_leb128_u(&sec, 0);
        buf_byte(&sec, OP_I32_CONST);
        buf_leb128_s(&sec, 0);
        buf_byte(&sec, OP_END);
        buf_leb128_u(&sec, (uint32_t)strtab.total_len);
        for (int i = 0; i < strtab.count; i++) {
            const char *s = strtab.strs[i];
            int slen = strtab.lengths[i];
            buf_bytes(&sec, (const uint8_t *)s, slen);
            buf_byte(&sec, 0x00);
        }

        buf_section(&output, 11, &sec);
        buf_free(&sec);
    }

    /* ================================================================
       Write output file
       ================================================================ */
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "xs wasm: cannot open '%s' for writing\n", out_path);
        buf_free(&output);
        funcs_free(&funcs);
        struct_layouts_free(&struct_layouts);
        enum_layouts_free(&enum_layouts);
        return 1;
    }

    fwrite(output.data, 1, (size_t)output.len, f);
    fclose(f);

    printf("xs wasm: wrote %d bytes to %s\n", output.len, out_path);

    buf_free(&output);
    funcs_free(&funcs);
    struct_layouts_free(&struct_layouts);
    enum_layouts_free(&enum_layouts);
    return 0;
}
