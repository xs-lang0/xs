#include "core/xs_compat.h"
#include "transpiler/wasm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern char *strdup(const char *);

/* WASM opcodes */
#define OP_UNREACHABLE 0x00
#define OP_NOP         0x01
#define OP_BLOCK       0x02
#define OP_LOOP        0x03
#define OP_IF          0x04
#define OP_ELSE        0x05
#define OP_END         0x0B
#define OP_BR          0x0C
#define OP_BR_IF       0x0D
#define OP_BR_TABLE    0x0E
#define OP_RETURN      0x0F
#define OP_CALL        0x10
#define OP_DROP        0x1A
#define OP_SELECT      0x1B
#define OP_LOCAL_GET   0x20
#define OP_LOCAL_SET   0x21
#define OP_LOCAL_TEE   0x22
#define OP_GLOBAL_GET  0x23
#define OP_GLOBAL_SET  0x24
#define OP_I32_LOAD    0x28
#define OP_I32_STORE   0x36
#define OP_I32_CONST   0x41
#define OP_I64_CONST   0x42
#define OP_F64_CONST   0x44
#define OP_I32_EQZ     0x45
#define OP_I32_EQ      0x46
#define OP_I32_NE      0x47
#define OP_I32_LT_S    0x48
#define OP_I32_GT_S    0x4A
#define OP_I32_LE_S    0x4C
#define OP_I32_GE_S    0x4E
#define OP_I32_ADD     0x6A
#define OP_I32_SUB     0x6B
#define OP_I32_MUL     0x6C
#define OP_I32_DIV_S   0x6D
#define OP_I32_REM_S   0x6F
#define OP_I32_AND     0x71
#define OP_I32_OR      0x72
#define OP_I32_XOR     0x73
#define OP_I32_SHL     0x74
#define OP_I32_SHR_S   0x75
#define OP_CALL_INDIRECT 0x11

#define OP_F64_ADD     0xA0
#define OP_F64_SUB     0xA1
#define OP_F64_MUL     0xA2
#define OP_F64_DIV     0xA3
#define OP_F64_NEG     0x9A
#define OP_F64_EQ      0x61

/* WASM type codes */
#define WASM_TYPE_I32  0x7F
#define WASM_TYPE_I64  0x7E
#define WASM_TYPE_F32  0x7D
#define WASM_TYPE_F64  0x7C
#define WASM_TYPE_VOID 0x40

/* Dynamic buffer */
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

/* Write unsigned LEB128 */
static void buf_leb128_u(WasmBuf *b, uint32_t val) {
    do {
        uint8_t byte = val & 0x7F;
        val >>= 7;
        if (val) byte |= 0x80;
        buf_byte(b, byte);
    } while (val);
}

/* Write signed LEB128 */
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

/* Write a WASM section: id + LEB128(size) + content */
static void buf_section(WasmBuf *out, uint8_t id, WasmBuf *content) {
    buf_byte(out, id);
    buf_leb128_u(out, (uint32_t)content->len);
    buf_append(out, content);
}

/* Write a UTF-8 name (length-prefixed) */
static void buf_name(WasmBuf *b, const char *s) {
    uint32_t len = (uint32_t)strlen(s);
    buf_leb128_u(b, len);
    buf_bytes(b, (const uint8_t *)s, (int)len);
}

/* Write an IEEE-754 f64 in little-endian byte order */
static void buf_f64(WasmBuf *b, double val) {
    uint8_t raw[8];
    memcpy(raw, &val, 8);
    /* ensure little-endian (WASM spec requires LE) */
    buf_bytes(b, raw, 8);
}

/* String table */
#define MAX_STRINGS 1024

typedef struct {
    const char *strs[MAX_STRINGS];
    int         offsets[MAX_STRINGS];
    int         lengths[MAX_STRINGS];
    int         count;
    int         total_len;  /* running byte offset into data segment */
} StringTable;

static void strtab_init(StringTable *st) {
    st->count = 0;
    st->total_len = 0;
}

/* Add a string literal; returns offset into the data segment.
   Duplicates are allowed (de-dup could be added later). */
static int strtab_add(StringTable *st, const char *s) {
    if (st->count >= MAX_STRINGS) return 0;
    int len = (int)strlen(s);
    int off = st->total_len;
    st->strs[st->count]    = s;
    st->offsets[st->count]  = off;
    st->lengths[st->count]  = len;
    st->count++;
    st->total_len += len + 1;  /* NUL terminated */
    return off;
}

/* import indices */
#define IMPORT_STR_CAT  0   /* env.str_cat(i32,i32)->i32  */
#define IMPORT_POW      1   /* env.pow(f64,f64)->f64       */
#define NUM_IMPORTS     2

/* Heap pointer global index */
#define GLOBAL_HEAP_PTR 0   /* mutable i32 global for bump allocator */
#define GLOBAL_ERR_FLAG 1   /* mutable i32 global: 0=ok, 1=error (for try/catch) */

/* Struct field layout tracker */
#define MAX_STRUCTS 64
#define MAX_STRUCT_FIELDS 64

typedef struct {
    char *name;
    char *fields[MAX_STRUCT_FIELDS];
    int n_fields;
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

static void struct_layouts_add(StructLayoutMap *m, const char *name, NodePairList *fields) {
    if (m->count >= MAX_STRUCTS) return;
    StructLayout *sl = &m->layouts[m->count++];
    sl->name = strdup(name);
    sl->n_fields = 0;
    for (int i = 0; i < fields->len && i < MAX_STRUCT_FIELDS; i++) {
        sl->fields[sl->n_fields++] = strdup(fields->items[i].key);
    }
}

static int struct_field_offset(StructLayoutMap *m, const char *field_name) {
    /* Search all structs for the field name; return index * 4 as byte offset.
       This is a simple approach: no type tracking, first match wins. */
    for (int i = 0; i < m->count; i++) {
        for (int j = 0; j < m->layouts[i].n_fields; j++) {
            if (strcmp(m->layouts[i].fields[j], field_name) == 0)
                return j * 4;
        }
    }
    return -1; /* unknown field */
}

/* Defer list (for collecting deferred statements) */
#define MAX_DEFERS 64

typedef struct {
    Node *stmts[MAX_DEFERS];
    int count;
} DeferList;

static void defer_list_init(DeferList *d) { d->count = 0; }

static void defer_list_push(DeferList *d, Node *stmt) {
    if (d->count < MAX_DEFERS) d->stmts[d->count++] = stmt;
}

/* Local variable tracker */
#define MAX_LOCALS 256

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

/* Function table for calls */
#define MAX_FUNCS 256

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

/* Nesting depth tracker for break/continue */
typedef struct {
    int break_depth;    /* depth of nearest enclosing block for break */
    int continue_depth; /* depth of nearest enclosing loop for continue */
} LoopCtx;

/* File-scope state for struct layouts and defer */
static StructLayoutMap g_struct_layouts;
static DeferList g_defer_list;

/* Helper: emit deferred statements in reverse order */
static void emit_defers(WasmBuf *code, LocalMap *locals, FuncMap *funcs, StringTable *strtab);

/* AST compilation to WASM bytecode */

static void compile_expr(Node *node, WasmBuf *code, LocalMap *locals, FuncMap *funcs, StringTable *strtab);
static void compile_stmt(Node *node, WasmBuf *code, LocalMap *locals, FuncMap *funcs, StringTable *strtab);
static void compile_block(Node *block, WasmBuf *code, LocalMap *locals, FuncMap *funcs, StringTable *strtab);

static void compile_block(Node *block, WasmBuf *code, LocalMap *locals, FuncMap *funcs, StringTable *strtab) {
    if (!block) return;
    if (block->tag == NODE_BLOCK) {
        for (int i = 0; i < block->block.stmts.len; i++)
            compile_stmt(block->block.stmts.items[i], code, locals, funcs, strtab);
    } else {
        compile_stmt(block, code, locals, funcs, strtab);
    }
}

/* Emit deferred statements in LIFO (reverse) order */
static void emit_defers(WasmBuf *code, LocalMap *locals, FuncMap *funcs, StringTable *strtab) {
    for (int i = g_defer_list.count - 1; i >= 0; i--) {
        compile_stmt(g_defer_list.stmts[i], code, locals, funcs, strtab);
    }
}

static void compile_expr(Node *node, WasmBuf *code, LocalMap *locals, FuncMap *funcs, StringTable *strtab) {
    if (!node) {
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, 0);
        return;
    }

    switch (node->tag) {
    case NODE_LIT_INT:
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, (int32_t)node->lit_int.ival);
        break;

    case NODE_LIT_BIGINT:
        /* Truncate bigint to i32 for WASM (limited support) */
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, (int32_t)strtol(node->lit_bigint.bigint_str, NULL, 10));
        break;

    case NODE_LIT_FLOAT:
        buf_byte(code, OP_F64_CONST);
        buf_f64(code, node->lit_float.fval);
        break;

    case NODE_LIT_BOOL:
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, node->lit_bool.bval ? 1 : 0);
        break;

    case NODE_LIT_NULL:
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, 0);
        break;

    case NODE_LIT_CHAR:
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, (int32_t)node->lit_char.cval);
        break;

    case NODE_LIT_STRING:
    case NODE_INTERP_STRING: {
        /* Emit i32.const pointing to offset in the data segment */
        const char *s = node->lit_string.sval ? node->lit_string.sval : "";
        int off = strtab_add(strtab, s);
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, (int32_t)off);
        break;
    }

    case NODE_LIT_ARRAY:
    case NODE_LIT_TUPLE: {
        /* Bump-allocate in linear memory.  Layout: [elem0, elem1, ...] as i32 slots.
           Returns the base pointer (i32) of the allocated region. */
        int n = node->lit_array.elems.len;
        /* Save current heap pointer as base address */
        buf_byte(code, OP_GLOBAL_GET);
        buf_leb128_u(code, GLOBAL_HEAP_PTR);
        /* Store each element at base + i*4 */
        for (int i = 0; i < n; i++) {
            /* address = heap_ptr + i*4 */
            buf_byte(code, OP_GLOBAL_GET);
            buf_leb128_u(code, GLOBAL_HEAP_PTR);
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, (int32_t)(i * 4));
            buf_byte(code, OP_I32_ADD);
            /* value */
            compile_expr(node->lit_array.elems.items[i], code, locals, funcs, strtab);
            /* i32.store align=2 offset=0 */
            buf_byte(code, OP_I32_STORE);
            buf_leb128_u(code, 2);  /* alignment */
            buf_leb128_u(code, 0);  /* offset */
        }
        /* Bump the heap pointer past the allocated region */
        buf_byte(code, OP_GLOBAL_GET);
        buf_leb128_u(code, GLOBAL_HEAP_PTR);
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, (int32_t)(n * 4));
        buf_byte(code, OP_I32_ADD);
        buf_byte(code, OP_GLOBAL_SET);
        buf_leb128_u(code, GLOBAL_HEAP_PTR);
        /* The base pointer is already on the stack from the first global.get */
        break;
    }

    case NODE_LIT_MAP:
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, (int32_t)node->lit_map.keys.len);
        break;

    case NODE_IDENT: {
        int idx = locals_find(locals, node->ident.name);
        if (idx >= 0) {
            buf_byte(code, OP_LOCAL_GET);
            buf_leb128_u(code, (uint32_t)idx);
        } else {
            /* Unknown variable, push 0 */
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
        }
        break;
    }

    case NODE_BINOP: {
        compile_expr(node->binop.left, code, locals, funcs, strtab);
        compile_expr(node->binop.right, code, locals, funcs, strtab);
        const char *op = node->binop.op;
        if      (strcmp(op, "+")  == 0) buf_byte(code, OP_I32_ADD);
        else if (strcmp(op, "-")  == 0) buf_byte(code, OP_I32_SUB);
        else if (strcmp(op, "*")  == 0) buf_byte(code, OP_I32_MUL);
        else if (strcmp(op, "/")  == 0) buf_byte(code, OP_I32_DIV_S);
        else if (strcmp(op, "//") == 0) buf_byte(code, OP_I32_DIV_S);
        else if (strcmp(op, "%")  == 0) buf_byte(code, OP_I32_REM_S);
        else if (strcmp(op, "==") == 0) buf_byte(code, OP_I32_EQ);
        else if (strcmp(op, "!=") == 0) buf_byte(code, OP_I32_NE);
        else if (strcmp(op, "<")  == 0) buf_byte(code, OP_I32_LT_S);
        else if (strcmp(op, ">")  == 0) buf_byte(code, OP_I32_GT_S);
        else if (strcmp(op, "<=") == 0) buf_byte(code, OP_I32_LE_S);
        else if (strcmp(op, ">=") == 0) buf_byte(code, OP_I32_GE_S);
        else if (strcmp(op, "&")  == 0) buf_byte(code, OP_I32_AND);
        else if (strcmp(op, "|")  == 0) buf_byte(code, OP_I32_OR);
        else if (strcmp(op, "^")  == 0) buf_byte(code, OP_I32_XOR);
        else if (strcmp(op, "<<") == 0) buf_byte(code, OP_I32_SHL);
        else if (strcmp(op, ">>") == 0) buf_byte(code, OP_I32_SHR_S);
        else if (strcmp(op, "and") == 0) buf_byte(code, OP_I32_AND);
        else if (strcmp(op, "or") == 0) buf_byte(code, OP_I32_OR);
        else if (strcmp(op, "**") == 0) {
            /* Power: call imported env.pow(f64,f64)->f64.
               Operands already compiled above; drop them and re-emit for the call. */
            buf_byte(code, OP_DROP);
            buf_byte(code, OP_DROP);
            compile_expr(node->binop.left, code, locals, funcs, strtab);
            compile_expr(node->binop.right, code, locals, funcs, strtab);
            buf_byte(code, OP_CALL);
            buf_leb128_u(code, IMPORT_POW);
        } else if (strcmp(op, "++") == 0) {
            /* String concatenation: call imported env.str_cat(i32,i32)->i32 */
            buf_byte(code, OP_CALL);
            buf_leb128_u(code, IMPORT_STR_CAT);
        } else if (strcmp(op, "??") == 0) {
            /* Null coalescing: left if non-zero, else right.
               Use select: left, right, left_is_nonzero */
            /* Stack has: left, right. We need: left, right, left!=0 */
            /* Re-compile left for the condition */
            buf_byte(code, OP_DROP);
            buf_byte(code, OP_DROP);
            compile_expr(node->binop.left, code, locals, funcs, strtab);
            compile_expr(node->binop.right, code, locals, funcs, strtab);
            compile_expr(node->binop.left, code, locals, funcs, strtab);
            buf_byte(code, OP_SELECT);
        } else {
            /* Unsupported operator; trap at runtime */
            buf_byte(code, OP_UNREACHABLE);
        }
        break;
    }

    case NODE_UNARY: {
        const char *op = node->unary.op;
        if (strcmp(op, "-") == 0) {
            /* Negate: 0 - expr */
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
            compile_expr(node->unary.expr, code, locals, funcs, strtab);
            buf_byte(code, OP_I32_SUB);
        } else if (strcmp(op, "not") == 0 || strcmp(op, "!") == 0) {
            compile_expr(node->unary.expr, code, locals, funcs, strtab);
            buf_byte(code, OP_I32_EQZ);
        } else if (strcmp(op, "~") == 0) {
            /* Bitwise NOT: XOR with -1 */
            compile_expr(node->unary.expr, code, locals, funcs, strtab);
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, -1);
            buf_byte(code, OP_I32_XOR);
        } else {
            compile_expr(node->unary.expr, code, locals, funcs, strtab);
        }
        break;
    }

    case NODE_CALL: {
        /* Compile arguments */
        for (int i = 0; i < node->call.args.len; i++) {
            compile_expr(node->call.args.items[i], code, locals, funcs, strtab);
        }
        /* Look up function */
        if (node->call.callee && node->call.callee->tag == NODE_IDENT) {
            int fidx = funcs_find(funcs, node->call.callee->ident.name);
            if (fidx >= 0) {
                buf_byte(code, OP_CALL);
                buf_leb128_u(code, (uint32_t)(NUM_IMPORTS + fidx));
            } else {
                /* Unknown function: drop args, push 0 */
                for (int i = 0; i < node->call.args.len; i++) {
                    buf_byte(code, OP_DROP);
                }
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 0);
            }
        } else {
            /* Indirect call: compile callee expression as table index,
               then call_indirect with generic (i32)->i32 signature */
            compile_expr(node->call.callee, code, locals, funcs, strtab);
            buf_byte(code, OP_CALL_INDIRECT);
            buf_leb128_u(code, 2);  /* type index 2: (i32)->i32 */
            buf_leb128_u(code, 0);  /* table index 0 */
        }
        break;
    }

    case NODE_METHOD_CALL: {
        /* Method / indirect call: compile obj as the function table index,
           push arguments, then call_indirect with a generic (i32)->i32 sig.
           The first argument is the object itself. */
        compile_expr(node->method_call.obj, code, locals, funcs, strtab);  /* arg0 = obj */
        for (int i = 0; i < node->method_call.args.len; i++) {
            compile_expr(node->method_call.args.items[i], code, locals, funcs, strtab);
        }
        /* The table index is the object value (treated as funcref index) */
        compile_expr(node->method_call.obj, code, locals, funcs, strtab);
        buf_byte(code, OP_CALL_INDIRECT);
        buf_leb128_u(code, 2);  /* type index 2: (i32)->i32 */
        buf_leb128_u(code, 0);  /* table index 0 */
        break;
    }

    case NODE_INDEX: {
        /* Compute memory address: base + idx * 4, then i32.load */
        compile_expr(node->index.obj, code, locals, funcs, strtab);   /* base ptr */
        compile_expr(node->index.index, code, locals, funcs, strtab); /* index */
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, 4);
        buf_byte(code, OP_I32_MUL);    /* idx * 4 */
        buf_byte(code, OP_I32_ADD);    /* base + idx*4 */
        buf_byte(code, OP_I32_LOAD);
        buf_leb128_u(code, 2);  /* alignment */
        buf_leb128_u(code, 0);  /* offset */
        break;
    }

    case NODE_FIELD: {
        /* Compute field access: object_ptr + field_offset, then i32.load */
        compile_expr(node->field.obj, code, locals, funcs, strtab);
        int foff = struct_field_offset(&g_struct_layouts, node->field.name);
        if (foff > 0) {
            /* Add the byte offset to the object pointer */
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, (int32_t)foff);
            buf_byte(code, OP_I32_ADD);
        }
        /* foff == 0 means first field, no add needed; foff < 0 means unknown,
           treat the object value itself as the field (offset 0) */
        buf_byte(code, OP_I32_LOAD);
        buf_leb128_u(code, 2);  /* alignment: 4 bytes */
        buf_leb128_u(code, 0);  /* offset */
        break;
    }

    case NODE_SCOPE: {
        /* Scope resolution A::B::C -> look up last part */
        if (node->scope.nparts > 0) {
            int idx = locals_find(locals, node->scope.parts[node->scope.nparts - 1]);
            if (idx >= 0) {
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)idx);
            } else {
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 0);
            }
        } else {
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
        }
        break;
    }

    case NODE_ASSIGN: {
        if (node->assign.target && node->assign.target->tag == NODE_IDENT) {
            int idx = locals_ensure(locals, node->assign.target->ident.name);
            const char *op = node->assign.op;
            if (strcmp(op, "=") == 0) {
                compile_expr(node->assign.value, code, locals, funcs, strtab);
            } else if (strcmp(op, "+=") == 0) {
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)idx);
                compile_expr(node->assign.value, code, locals, funcs, strtab);
                buf_byte(code, OP_I32_ADD);
            } else if (strcmp(op, "-=") == 0) {
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)idx);
                compile_expr(node->assign.value, code, locals, funcs, strtab);
                buf_byte(code, OP_I32_SUB);
            } else if (strcmp(op, "*=") == 0) {
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)idx);
                compile_expr(node->assign.value, code, locals, funcs, strtab);
                buf_byte(code, OP_I32_MUL);
            } else {
                compile_expr(node->assign.value, code, locals, funcs, strtab);
            }
            buf_byte(code, OP_LOCAL_TEE);
            buf_leb128_u(code, (uint32_t)idx);
        } else {
            compile_expr(node->assign.value, code, locals, funcs, strtab);
        }
        break;
    }

    case NODE_IF: {
        compile_expr(node->if_expr.cond, code, locals, funcs, strtab);
        buf_byte(code, OP_IF);
        buf_byte(code, WASM_TYPE_I32); /* result type i32 */
        /* then */
        if (node->if_expr.then) {
            if (node->if_expr.then->tag == NODE_BLOCK) {
                NodeList *stmts = &node->if_expr.then->block.stmts;
                for (int i = 0; i < stmts->len; i++)
                    compile_stmt(stmts->items[i], code, locals, funcs, strtab);
                if (node->if_expr.then->block.expr)
                    compile_expr(node->if_expr.then->block.expr, code, locals, funcs, strtab);
                else {
                    buf_byte(code, OP_I32_CONST);
                    buf_leb128_s(code, 0);
                }
            } else {
                compile_expr(node->if_expr.then, code, locals, funcs, strtab);
            }
        } else {
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
        }
        /* Handle elif chains */
        for (int i = 0; i < node->if_expr.elif_conds.len; i++) {
            buf_byte(code, OP_ELSE);
            compile_expr(node->if_expr.elif_conds.items[i], code, locals, funcs, strtab);
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_I32);
            Node *et = node->if_expr.elif_thens.items[i];
            if (et && et->tag == NODE_BLOCK) {
                for (int j = 0; j < et->block.stmts.len; j++)
                    compile_stmt(et->block.stmts.items[j], code, locals, funcs, strtab);
                if (et->block.expr)
                    compile_expr(et->block.expr, code, locals, funcs, strtab);
                else {
                    buf_byte(code, OP_I32_CONST);
                    buf_leb128_s(code, 0);
                }
            } else {
                compile_expr(et, code, locals, funcs, strtab);
            }
        }
        /* else branch */
        buf_byte(code, OP_ELSE);
        if (node->if_expr.else_branch) {
            if (node->if_expr.else_branch->tag == NODE_BLOCK) {
                NodeList *stmts = &node->if_expr.else_branch->block.stmts;
                for (int i = 0; i < stmts->len; i++)
                    compile_stmt(stmts->items[i], code, locals, funcs, strtab);
                if (node->if_expr.else_branch->block.expr)
                    compile_expr(node->if_expr.else_branch->block.expr, code, locals, funcs, strtab);
                else {
                    buf_byte(code, OP_I32_CONST);
                    buf_leb128_s(code, 0);
                }
            } else {
                compile_expr(node->if_expr.else_branch, code, locals, funcs, strtab);
            }
        } else {
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
        }
        buf_byte(code, OP_END);
        /* Close elif chain ends */
        for (int i = 0; i < node->if_expr.elif_conds.len; i++) {
            buf_byte(code, OP_END);
        }
        break;
    }

    case NODE_MATCH: {
        /* Match as expression: evaluate subject, then if/else chain */
        int subject_idx = locals_add(locals, "__match_sub");
        compile_expr(node->match.subject, code, locals, funcs, strtab);
        buf_byte(code, OP_LOCAL_SET);
        buf_leb128_u(code, (uint32_t)subject_idx);

        /* Build nested if/else for each arm */
        for (int i = 0; i < node->match.arms.len; i++) {
            MatchArm *arm = &node->match.arms.items[i];
            /* Condition check */
            if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT && arm->pattern->pat_lit.tag == 0) {
                /* int literal */
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, (int32_t)arm->pattern->pat_lit.ival);
                buf_byte(code, OP_I32_EQ);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT && arm->pattern->pat_lit.tag == 1) {
                /* float literal: compare as f64 */
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                buf_byte(code, OP_F64_CONST);
                buf_f64(code, arm->pattern->pat_lit.fval);
                buf_byte(code, OP_F64_EQ);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT && arm->pattern->pat_lit.tag == 2) {
                /* string literal: compare pointer offsets */
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                const char *s = arm->pattern->pat_lit.sval ? arm->pattern->pat_lit.sval : "";
                int off = strtab_add(strtab, s);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, (int32_t)off);
                buf_byte(code, OP_I32_EQ);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT && arm->pattern->pat_lit.tag == 3) {
                /* bool literal */
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, arm->pattern->pat_lit.bval ? 1 : 0);
                buf_byte(code, OP_I32_EQ);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT && arm->pattern->pat_lit.tag == 4) {
                /* null literal */
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 0);
                buf_byte(code, OP_I32_EQ);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_WILD) {
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 1);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT) {
                /* binding pattern: always matches, bind the variable */
                int bind_idx = locals_ensure(locals, arm->pattern->pat_ident.name);
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                buf_byte(code, OP_LOCAL_SET);
                buf_leb128_u(code, (uint32_t)bind_idx);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 1);
            } else {
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 1);
            }
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_I32);
            /* arm body */
            if (arm->body && arm->body->tag == NODE_BLOCK) {
                for (int j = 0; j < arm->body->block.stmts.len; j++)
                    compile_stmt(arm->body->block.stmts.items[j], code, locals, funcs, strtab);
                if (arm->body->block.expr)
                    compile_expr(arm->body->block.expr, code, locals, funcs, strtab);
                else {
                    buf_byte(code, OP_I32_CONST);
                    buf_leb128_s(code, 0);
                }
            } else if (arm->body) {
                compile_expr(arm->body, code, locals, funcs, strtab);
            } else {
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 0);
            }
            buf_byte(code, OP_ELSE);
        }
        /* Default: push 0 */
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, 0);
        /* Close all if/else ends */
        for (int i = 0; i < node->match.arms.len; i++) {
            buf_byte(code, OP_END);
        }
        break;
    }

    case NODE_BLOCK: {
        /* Block as expression */
        for (int i = 0; i < node->block.stmts.len; i++)
            compile_stmt(node->block.stmts.items[i], code, locals, funcs, strtab);
        if (node->block.expr)
            compile_expr(node->block.expr, code, locals, funcs, strtab);
        else {
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
        }
        break;
    }

    case NODE_LAMBDA: {
        /* Lambda not supported in WASM; trap at runtime */
        buf_byte(code, OP_UNREACHABLE);
        break;
    }

    case NODE_CAST: {
        /* Cast: just compile the inner expression */
        compile_expr(node->cast.expr, code, locals, funcs, strtab);
        break;
    }

    case NODE_STRUCT_INIT:
    case NODE_SPREAD:
    case NODE_LIST_COMP:
    case NODE_MAP_COMP:
    case NODE_RANGE:
        /* Complex types not supported in WASM; trap at runtime */
        buf_byte(code, OP_UNREACHABLE);
        break;

    case NODE_AWAIT:
        /* Compile the inner expression */
        compile_expr(node->await_.expr, code, locals, funcs, strtab);
        break;

    case NODE_YIELD:
        if (node->yield_.value)
            compile_expr(node->yield_.value, code, locals, funcs, strtab);
        else {
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
        }
        break;

    case NODE_SPAWN:
        compile_expr(node->spawn_.expr, code, locals, funcs, strtab);
        break;

    case NODE_RESUME:
        if (node->resume_.value)
            compile_expr(node->resume_.value, code, locals, funcs, strtab);
        else {
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
        }
        break;

    case NODE_PERFORM:
    case NODE_HANDLE:
    case NODE_NURSERY:
    case NODE_THROW:
    case NODE_RETURN:
    case NODE_DEFER:
    case NODE_TRY:
        /* These are control flow; unsupported as expressions in WASM */
        buf_byte(code, OP_UNREACHABLE);
        break;

    case NODE_PAT_IDENT: {
        int idx = locals_find(locals, node->pat_ident.name);
        if (idx >= 0) {
            buf_byte(code, OP_LOCAL_GET);
            buf_leb128_u(code, (uint32_t)idx);
        } else {
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
        }
        break;
    }

    default:
        /* Unsupported expression type; trap at runtime */
        buf_byte(code, OP_UNREACHABLE);
        break;
    }
}

static void compile_stmt(Node *node, WasmBuf *code, LocalMap *locals, FuncMap *funcs, StringTable *strtab) {
    if (!node) return;

    switch (node->tag) {
    case NODE_LET:
    case NODE_VAR: {
        const char *name = node->let.name;
        if (name) {
            int idx = locals_ensure(locals, name);
            if (node->let.value) {
                compile_expr(node->let.value, code, locals, funcs, strtab);
                buf_byte(code, OP_LOCAL_SET);
                buf_leb128_u(code, (uint32_t)idx);
            }
        } else if (node->let.pattern && node->let.pattern->tag == NODE_PAT_IDENT) {
            int idx = locals_ensure(locals, node->let.pattern->pat_ident.name);
            if (node->let.value) {
                compile_expr(node->let.value, code, locals, funcs, strtab);
                buf_byte(code, OP_LOCAL_SET);
                buf_leb128_u(code, (uint32_t)idx);
            }
        }
        break;
    }

    case NODE_CONST: {
        if (node->const_.name) {
            int idx = locals_ensure(locals, node->const_.name);
            if (node->const_.value) {
                compile_expr(node->const_.value, code, locals, funcs, strtab);
                buf_byte(code, OP_LOCAL_SET);
                buf_leb128_u(code, (uint32_t)idx);
            }
        }
        break;
    }

    case NODE_ASSIGN: {
        if (node->assign.target && node->assign.target->tag == NODE_IDENT) {
            int idx = locals_ensure(locals, node->assign.target->ident.name);
            const char *op = node->assign.op;
            if (strcmp(op, "=") == 0) {
                compile_expr(node->assign.value, code, locals, funcs, strtab);
            } else if (strcmp(op, "+=") == 0) {
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)idx);
                compile_expr(node->assign.value, code, locals, funcs, strtab);
                buf_byte(code, OP_I32_ADD);
            } else if (strcmp(op, "-=") == 0) {
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)idx);
                compile_expr(node->assign.value, code, locals, funcs, strtab);
                buf_byte(code, OP_I32_SUB);
            } else if (strcmp(op, "*=") == 0) {
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)idx);
                compile_expr(node->assign.value, code, locals, funcs, strtab);
                buf_byte(code, OP_I32_MUL);
            } else {
                compile_expr(node->assign.value, code, locals, funcs, strtab);
            }
            buf_byte(code, OP_LOCAL_SET);
            buf_leb128_u(code, (uint32_t)idx);
        }
        break;
    }

    case NODE_RETURN:
        /* Emit deferred statements before returning */
        if (g_defer_list.count > 0) {
            /* Save return value in a temp local, emit defers, restore it */
            if (node->ret.value) {
                int ret_tmp = locals_add(locals, "__ret_tmp");
                compile_expr(node->ret.value, code, locals, funcs, strtab);
                buf_byte(code, OP_LOCAL_SET);
                buf_leb128_u(code, (uint32_t)ret_tmp);
                emit_defers(code, locals, funcs, strtab);
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)ret_tmp);
            } else {
                emit_defers(code, locals, funcs, strtab);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 0);
            }
        } else {
            if (node->ret.value) {
                compile_expr(node->ret.value, code, locals, funcs, strtab);
            } else {
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 0);
            }
        }
        buf_byte(code, OP_RETURN);
        break;

    case NODE_EXPR_STMT:
        if (node->expr_stmt.expr) {
            compile_expr(node->expr_stmt.expr, code, locals, funcs, strtab);
            buf_byte(code, OP_DROP);
        }
        break;

    case NODE_WHILE: {
        /* block { loop { br_if (not cond) 1; body; br 0; } } */
        buf_byte(code, OP_BLOCK);
        buf_byte(code, WASM_TYPE_VOID);
        buf_byte(code, OP_LOOP);
        buf_byte(code, WASM_TYPE_VOID);
        /* condition: if false, break out */
        compile_expr(node->while_loop.cond, code, locals, funcs, strtab);
        buf_byte(code, OP_I32_EQZ);
        buf_byte(code, OP_BR_IF);
        buf_leb128_u(code, 1); /* break to outer block */
        /* body */
        compile_block(node->while_loop.body, code, locals, funcs, strtab);
        buf_byte(code, OP_BR);
        buf_leb128_u(code, 0); /* continue loop */
        buf_byte(code, OP_END); /* end loop */
        buf_byte(code, OP_END); /* end block */
        break;
    }

    case NODE_FOR: {
        /* for pattern in iter -> while-based iteration.
           Since WASM MVP has no iterators, we handle range-based for:
           if iter is a range, unroll as counter loop. */
        if (node->for_loop.iter && node->for_loop.iter->tag == NODE_RANGE) {
            Node *range = node->for_loop.iter;
            const char *var_name = "__for_i";
            if (node->for_loop.pattern && node->for_loop.pattern->tag == NODE_PAT_IDENT)
                var_name = node->for_loop.pattern->pat_ident.name;
            int idx = locals_ensure(locals, var_name);
            int end_idx = locals_add(locals, "__for_end");

            /* Initialize counter */
            compile_expr(range->range.start, code, locals, funcs, strtab);
            buf_byte(code, OP_LOCAL_SET);
            buf_leb128_u(code, (uint32_t)idx);
            /* Store end */
            compile_expr(range->range.end, code, locals, funcs, strtab);
            if (range->range.inclusive) {
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 1);
                buf_byte(code, OP_I32_ADD);
            }
            buf_byte(code, OP_LOCAL_SET);
            buf_leb128_u(code, (uint32_t)end_idx);

            /* block { loop { check; body; increment; br 0; } } */
            buf_byte(code, OP_BLOCK);
            buf_byte(code, WASM_TYPE_VOID);
            buf_byte(code, OP_LOOP);
            buf_byte(code, WASM_TYPE_VOID);
            /* check: i >= end -> break */
            buf_byte(code, OP_LOCAL_GET);
            buf_leb128_u(code, (uint32_t)idx);
            buf_byte(code, OP_LOCAL_GET);
            buf_leb128_u(code, (uint32_t)end_idx);
            buf_byte(code, OP_I32_GE_S);
            buf_byte(code, OP_BR_IF);
            buf_leb128_u(code, 1);
            /* body */
            compile_block(node->for_loop.body, code, locals, funcs, strtab);
            /* increment */
            buf_byte(code, OP_LOCAL_GET);
            buf_leb128_u(code, (uint32_t)idx);
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 1);
            buf_byte(code, OP_I32_ADD);
            buf_byte(code, OP_LOCAL_SET);
            buf_leb128_u(code, (uint32_t)idx);
            buf_byte(code, OP_BR);
            buf_leb128_u(code, 0);
            buf_byte(code, OP_END); /* end loop */
            buf_byte(code, OP_END); /* end block */
        } else {
            /* Non-range for loop: treat iter as array pointer in linear memory.
               Convention: array is stored as [length, elem0, elem1, ...] in memory,
               each element is i32 (4 bytes). */
            const char *var_name = "__for_elem";
            if (node->for_loop.pattern && node->for_loop.pattern->tag == NODE_PAT_IDENT)
                var_name = node->for_loop.pattern->pat_ident.name;

            int elem_idx = locals_ensure(locals, var_name);
            int arr_idx  = locals_add(locals, "__for_arr");
            int len_idx  = locals_add(locals, "__for_len");
            int i_idx    = locals_add(locals, "__for_idx");

            /* Evaluate the iterable expression -> array pointer */
            compile_expr(node->for_loop.iter, code, locals, funcs, strtab);
            buf_byte(code, OP_LOCAL_TEE);
            buf_leb128_u(code, (uint32_t)arr_idx);

            /* Load length from array[0] */
            buf_byte(code, OP_I32_LOAD);
            buf_leb128_u(code, 2);  /* alignment */
            buf_leb128_u(code, 0);  /* offset */
            buf_byte(code, OP_LOCAL_SET);
            buf_leb128_u(code, (uint32_t)len_idx);

            /* Initialize index counter to 0 */
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
            buf_byte(code, OP_LOCAL_SET);
            buf_leb128_u(code, (uint32_t)i_idx);

            /* block { loop { check; load elem; body; increment; br 0; } } */
            buf_byte(code, OP_BLOCK);
            buf_byte(code, WASM_TYPE_VOID);
            buf_byte(code, OP_LOOP);
            buf_byte(code, WASM_TYPE_VOID);

            /* check: i >= len -> break */
            buf_byte(code, OP_LOCAL_GET);
            buf_leb128_u(code, (uint32_t)i_idx);
            buf_byte(code, OP_LOCAL_GET);
            buf_leb128_u(code, (uint32_t)len_idx);
            buf_byte(code, OP_I32_GE_S);
            buf_byte(code, OP_BR_IF);
            buf_leb128_u(code, 1);  /* break to outer block */

            /* Load element: arr_ptr + (i + 1) * 4  (skip length at offset 0) */
            buf_byte(code, OP_LOCAL_GET);
            buf_leb128_u(code, (uint32_t)arr_idx);
            buf_byte(code, OP_LOCAL_GET);
            buf_leb128_u(code, (uint32_t)i_idx);
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 1);
            buf_byte(code, OP_I32_ADD);      /* i + 1 */
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 4);
            buf_byte(code, OP_I32_MUL);      /* (i + 1) * 4 */
            buf_byte(code, OP_I32_ADD);      /* arr_ptr + (i+1)*4 */
            buf_byte(code, OP_I32_LOAD);
            buf_leb128_u(code, 2);  /* alignment */
            buf_leb128_u(code, 0);  /* offset */
            buf_byte(code, OP_LOCAL_SET);
            buf_leb128_u(code, (uint32_t)elem_idx);

            /* body */
            compile_block(node->for_loop.body, code, locals, funcs, strtab);

            /* increment */
            buf_byte(code, OP_LOCAL_GET);
            buf_leb128_u(code, (uint32_t)i_idx);
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 1);
            buf_byte(code, OP_I32_ADD);
            buf_byte(code, OP_LOCAL_SET);
            buf_leb128_u(code, (uint32_t)i_idx);

            buf_byte(code, OP_BR);
            buf_leb128_u(code, 0);  /* continue loop */
            buf_byte(code, OP_END); /* end loop */
            buf_byte(code, OP_END); /* end block */
        }
        break;
    }

    case NODE_LOOP: {
        /* loop { body } -> block { loop { body; br 0; } } */
        buf_byte(code, OP_BLOCK);
        buf_byte(code, WASM_TYPE_VOID);
        buf_byte(code, OP_LOOP);
        buf_byte(code, WASM_TYPE_VOID);
        compile_block(node->loop.body, code, locals, funcs, strtab);
        buf_byte(code, OP_BR);
        buf_leb128_u(code, 0);
        buf_byte(code, OP_END); /* end loop */
        buf_byte(code, OP_END); /* end block */
        break;
    }

    case NODE_BREAK: {
        /* break -> br 1 (to outer block) */
        buf_byte(code, OP_BR);
        buf_leb128_u(code, 1);
        break;
    }

    case NODE_CONTINUE: {
        /* continue -> br 0 (to loop header) */
        buf_byte(code, OP_BR);
        buf_leb128_u(code, 0);
        break;
    }

    case NODE_IF: {
        compile_expr(node->if_expr.cond, code, locals, funcs, strtab);
        buf_byte(code, OP_IF);
        buf_byte(code, WASM_TYPE_VOID);
        compile_block(node->if_expr.then, code, locals, funcs, strtab);
        /* elif chains */
        for (int i = 0; i < node->if_expr.elif_conds.len; i++) {
            buf_byte(code, OP_ELSE);
            compile_expr(node->if_expr.elif_conds.items[i], code, locals, funcs, strtab);
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_VOID);
            compile_block(node->if_expr.elif_thens.items[i], code, locals, funcs, strtab);
        }
        if (node->if_expr.else_branch) {
            buf_byte(code, OP_ELSE);
            compile_block(node->if_expr.else_branch, code, locals, funcs, strtab);
        }
        buf_byte(code, OP_END);
        /* close elif ends */
        for (int i = 0; i < node->if_expr.elif_conds.len; i++) {
            buf_byte(code, OP_END);
        }
        break;
    }

    case NODE_MATCH: {
        /* Match as statement: evaluate subject, if/else chain */
        int subject_idx = locals_add(locals, "__match_sub");
        compile_expr(node->match.subject, code, locals, funcs, strtab);
        buf_byte(code, OP_LOCAL_SET);
        buf_leb128_u(code, (uint32_t)subject_idx);

        for (int i = 0; i < node->match.arms.len; i++) {
            MatchArm *arm = &node->match.arms.items[i];
            if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT && arm->pattern->pat_lit.tag == 0) {
                /* int literal */
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, (int32_t)arm->pattern->pat_lit.ival);
                buf_byte(code, OP_I32_EQ);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT && arm->pattern->pat_lit.tag == 1) {
                /* float literal */
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                buf_byte(code, OP_F64_CONST);
                buf_f64(code, arm->pattern->pat_lit.fval);
                buf_byte(code, OP_F64_EQ);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT && arm->pattern->pat_lit.tag == 2) {
                /* string literal */
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                const char *s = arm->pattern->pat_lit.sval ? arm->pattern->pat_lit.sval : "";
                int off = strtab_add(strtab, s);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, (int32_t)off);
                buf_byte(code, OP_I32_EQ);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT && arm->pattern->pat_lit.tag == 3) {
                /* bool literal */
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, arm->pattern->pat_lit.bval ? 1 : 0);
                buf_byte(code, OP_I32_EQ);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT && arm->pattern->pat_lit.tag == 4) {
                /* null literal */
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 0);
                buf_byte(code, OP_I32_EQ);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT) {
                int bind_idx = locals_ensure(locals, arm->pattern->pat_ident.name);
                buf_byte(code, OP_LOCAL_GET);
                buf_leb128_u(code, (uint32_t)subject_idx);
                buf_byte(code, OP_LOCAL_SET);
                buf_leb128_u(code, (uint32_t)bind_idx);
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 1);
            } else {
                buf_byte(code, OP_I32_CONST);
                buf_leb128_s(code, 1);
            }
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_VOID);
            compile_block(arm->body, code, locals, funcs, strtab);
            buf_byte(code, OP_ELSE);
        }
        /* nop for default */
        buf_byte(code, OP_NOP);
        for (int i = 0; i < node->match.arms.len; i++) {
            buf_byte(code, OP_END);
        }
        break;
    }

    case NODE_TRY: {
        /* try/catch emulation using a global error flag (GLOBAL_ERR_FLAG).
           1. Clear the error flag
           2. Compile the try body
           3. Check the error flag; if set, branch to catch body */

        /* Clear error flag */
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, 0);
        buf_byte(code, OP_GLOBAL_SET);
        buf_leb128_u(code, GLOBAL_ERR_FLAG);

        /* Compile try body */
        compile_block(node->try_.body, code, locals, funcs, strtab);

        /* Check error flag: if set, execute catch body */
        if (node->try_.catch_arms.len > 0 || node->try_.finally_block) {
            buf_byte(code, OP_GLOBAL_GET);
            buf_leb128_u(code, GLOBAL_ERR_FLAG);
            buf_byte(code, OP_IF);
            buf_byte(code, WASM_TYPE_VOID);

            /* Compile catch arms (execute first catch body as fallback) */
            if (node->try_.catch_arms.len > 0) {
                MatchArm *arm = &node->try_.catch_arms.items[0];
                /* Bind the caught error variable if the pattern is an ident */
                if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT) {
                    int err_idx = locals_ensure(locals, arm->pattern->pat_ident.name);
                    /* Push a placeholder error value (0) for the caught error */
                    buf_byte(code, OP_I32_CONST);
                    buf_leb128_s(code, 0);
                    buf_byte(code, OP_LOCAL_SET);
                    buf_leb128_u(code, (uint32_t)err_idx);
                }
                compile_block(arm->body, code, locals, funcs, strtab);
            }

            /* Clear error flag after catch */
            buf_byte(code, OP_I32_CONST);
            buf_leb128_s(code, 0);
            buf_byte(code, OP_GLOBAL_SET);
            buf_leb128_u(code, GLOBAL_ERR_FLAG);

            buf_byte(code, OP_END); /* end if */
        }

        /* Compile finally block (always runs) */
        if (node->try_.finally_block) {
            compile_block(node->try_.finally_block, code, locals, funcs, strtab);
        }
        break;
    }

    case NODE_THROW: {
        /* throw -> set error flag and trap */
        if (node->throw_.value)  {
            compile_expr(node->throw_.value, code, locals, funcs, strtab);
            buf_byte(code, OP_DROP);
        }
        /* Set the global error flag so try/catch can detect it */
        buf_byte(code, OP_I32_CONST);
        buf_leb128_s(code, 1);
        buf_byte(code, OP_GLOBAL_SET);
        buf_leb128_u(code, GLOBAL_ERR_FLAG);
        buf_byte(code, OP_UNREACHABLE);
        break;
    }

    case NODE_DEFER: {
        /* Collect deferred statement; it will be emitted in reverse order
           before every return point and at function end. */
        if (node->defer_.body) {
            defer_list_push(&g_defer_list, node->defer_.body);
        }
        break;
    }

    case NODE_BLOCK: {
        for (int i = 0; i < node->block.stmts.len; i++)
            compile_stmt(node->block.stmts.items[i], code, locals, funcs, strtab);
        break;
    }

    case NODE_FN_DECL:
    case NODE_STRUCT_DECL:
    case NODE_ENUM_DECL:
    case NODE_CLASS_DECL:
    case NODE_IMPL_DECL:
    case NODE_TRAIT_DECL:
    case NODE_TYPE_ALIAS:
    case NODE_MODULE_DECL:
    case NODE_IMPORT:
    case NODE_EFFECT_DECL:
        /* Declarations are handled at the top level, not inside statements */
        break;

    case NODE_NURSERY:
        compile_block(node->nursery_.body, code, locals, funcs, strtab);
        break;

    case NODE_HANDLE:
        if (node->handle.expr)  {
            compile_expr(node->handle.expr, code, locals, funcs, strtab);
            buf_byte(code, OP_DROP);
        }
        break;

    case NODE_SPAWN:
        if (node->spawn_.expr) {
            compile_expr(node->spawn_.expr, code, locals, funcs, strtab);
            buf_byte(code, OP_DROP);
        }
        break;

    case NODE_PERFORM:
    case NODE_RESUME:
    case NODE_AWAIT:
    case NODE_YIELD:
        /* These require runtime support not available in WASM MVP.
           Emit unreachable to trap at runtime rather than silently skipping. */
        buf_byte(code, OP_UNREACHABLE);
        break;

    default:
        /* Any other node: try to compile as expression and drop */
        compile_expr(node, code, locals, funcs, strtab);
        buf_byte(code, OP_DROP);
        break;
    }
}

/* Collect function declarations from program */
typedef struct {
    Node *node;
    int n_params;
} FuncInfo;

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
    }
    return count;
}

/* Main transpiler entry point */
int transpile_wasm(Node *program, const char *filename, const char *out_path) {
    (void)filename;

    if (!program || !out_path) return 1;

    FuncMap funcs;
    funcs_init(&funcs);

    StringTable strtab;
    strtab_init(&strtab);

    /* Initialize struct layout map from struct declarations */
    struct_layouts_init(&g_struct_layouts);
    if (program->tag == NODE_PROGRAM) {
        NodeList *stmts = &program->program.stmts;
        for (int i = 0; i < stmts->len; i++) {
            Node *s = stmts->items[i];
            if (s && s->tag == NODE_STRUCT_DECL && s->struct_decl.name) {
                struct_layouts_add(&g_struct_layouts, s->struct_decl.name,
                                   &s->struct_decl.fields);
            }
        }
    }

    FuncInfo fn_infos[MAX_FUNCS];
    int n_funcs = collect_functions(program, fn_infos, MAX_FUNCS, &funcs);

    /* If no functions, create a single "main" function from top-level statements */
    int has_main = (funcs_find(&funcs, "main") >= 0);
    int main_func_idx = -1;

    if (!has_main) {
        main_func_idx = funcs_add(&funcs, "main");
        /* We'll synthesize a main function from top-level statements */
    } else {
        main_func_idx = funcs_find(&funcs, "main");
    }

    /* pre-compile function bodies */
    /* We compile into temporary buffers first, so the string table
       and local counts are known before emitting sections. */
    int total_user_funcs = n_funcs + (has_main ? 0 : 1);

    WasmBuf *compiled_funcs = calloc((size_t)total_user_funcs, sizeof(WasmBuf));
    int     *local_counts   = calloc((size_t)total_user_funcs, sizeof(int));
    int     *param_counts   = calloc((size_t)total_user_funcs, sizeof(int));

    for (int i = 0; i < n_funcs; i++) {
        Node *fn = fn_infos[i].node;
        WasmBuf body;
        buf_init(&body);

        LocalMap locals;
        locals_init(&locals);
        defer_list_init(&g_defer_list);

        /* Add parameters as locals */
        for (int p = 0; p < fn->fn_decl.params.len; p++) {
            const char *pname = fn->fn_decl.params.items[p].name;
            if (pname) locals_add(&locals, pname);
            else locals_add(&locals, "_");
        }

        /* Compile body */
        if (fn->fn_decl.body) {
            if (fn->fn_decl.body->tag == NODE_BLOCK) {
                NodeList *stmts = &fn->fn_decl.body->block.stmts;
                for (int si = 0; si < stmts->len; si++)
                    compile_stmt(stmts->items[si], &body, &locals, &funcs, &strtab);
                if (fn->fn_decl.body->block.expr)
                    compile_expr(fn->fn_decl.body->block.expr, &body, &locals, &funcs, &strtab);
            } else {
                compile_expr(fn->fn_decl.body, &body, &locals, &funcs, &strtab);
            }
        }

        /* Emit deferred statements at function end (before implicit return) */
        emit_defers(&body, &locals, &funcs, &strtab);

        /* Ensure there's a return value on stack */
        buf_byte(&body, OP_I32_CONST);
        buf_leb128_s(&body, 0);
        buf_byte(&body, OP_END);

        param_counts[i] = fn->fn_decl.params.len;
        local_counts[i] = locals.n_locals;
        compiled_funcs[i] = body;  /* transfer ownership */
        locals_free(&locals);
    }

    /* Synthesized main function (if no main was declared) */
    if (!has_main) {
        int mi = n_funcs;  /* index in compiled_funcs */
        WasmBuf body;
        buf_init(&body);

        LocalMap locals;
        locals_init(&locals);
        defer_list_init(&g_defer_list);

        if (program->tag == NODE_PROGRAM) {
            NodeList *stmts = &program->program.stmts;
            for (int i = 0; i < stmts->len; i++) {
                Node *s = stmts->items[i];
                if (s && s->tag != NODE_FN_DECL) {
                    compile_stmt(s, &body, &locals, &funcs, &strtab);
                }
            }
        }

        /* Emit deferred statements at function end */
        emit_defers(&body, &locals, &funcs, &strtab);

        buf_byte(&body, OP_I32_CONST);
        buf_leb128_s(&body, 0);
        buf_byte(&body, OP_END);

        param_counts[mi] = 0;
        local_counts[mi] = locals.n_locals;
        compiled_funcs[mi] = body;
        locals_free(&locals);
    }

    /* Imported functions occupy indices 0..NUM_IMPORTS-1.
       User-defined functions start at index NUM_IMPORTS. */
    int user_func_base = NUM_IMPORTS;

    /* Heap starts right after the string data segment (aligned to 16 bytes) */
    int heap_start = (strtab.total_len + 15) & ~15;

    WasmBuf output;
    buf_init(&output);

    /* header */
    buf_bytes(&output, (const uint8_t *)"\0asm", 4);
    uint8_t ver[4] = {1, 0, 0, 0};
    buf_bytes(&output, ver, 4);

    // type section
    {
        WasmBuf sec;
        buf_init(&sec);

        int n_import_types = 3;  /* str_cat, pow, call_indirect sig */
        int total_types = n_import_types + total_user_funcs;
        buf_leb128_u(&sec, (uint32_t)total_types);

        /* type 0: str_cat (i32, i32) -> i32 */
        buf_byte(&sec, 0x60);
        buf_leb128_u(&sec, 2);
        buf_byte(&sec, WASM_TYPE_I32);
        buf_byte(&sec, WASM_TYPE_I32);
        buf_leb128_u(&sec, 1);
        buf_byte(&sec, WASM_TYPE_I32);

        /* type 1: pow (f64, f64) -> f64 */
        buf_byte(&sec, 0x60);
        buf_leb128_u(&sec, 2);
        buf_byte(&sec, WASM_TYPE_F64);
        buf_byte(&sec, WASM_TYPE_F64);
        buf_leb128_u(&sec, 1);
        buf_byte(&sec, WASM_TYPE_F64);

        /* type 2: generic (i32) -> i32, used for call_indirect */
        buf_byte(&sec, 0x60);
        buf_leb128_u(&sec, 1);
        buf_byte(&sec, WASM_TYPE_I32);
        buf_leb128_u(&sec, 1);
        buf_byte(&sec, WASM_TYPE_I32);

        /* user function types (index 3..) */
        for (int i = 0; i < n_funcs; i++) {
            buf_byte(&sec, 0x60);
            buf_leb128_u(&sec, (uint32_t)fn_infos[i].n_params);
            for (int p = 0; p < fn_infos[i].n_params; p++)
                buf_byte(&sec, WASM_TYPE_I32);
            buf_leb128_u(&sec, 1);
            buf_byte(&sec, WASM_TYPE_I32);
        }

        /* main function type (if not declared) */
        if (!has_main) {
            buf_byte(&sec, 0x60);
            buf_leb128_u(&sec, 0);
            buf_leb128_u(&sec, 1);
            buf_byte(&sec, WASM_TYPE_I32);
        }

        buf_section(&output, 1, &sec);
        buf_free(&sec);
    }

    // imports
    {
        WasmBuf sec;
        buf_init(&sec);

        buf_leb128_u(&sec, NUM_IMPORTS);  /* 2 imports */

        /* import 0: env.str_cat -> type 0 */
        buf_name(&sec, "env");
        buf_name(&sec, "str_cat");
        buf_byte(&sec, 0x00);  /* import kind: function */
        buf_leb128_u(&sec, 0); /* type index 0 */

        /* import 1: env.pow -> type 1 */
        buf_name(&sec, "env");
        buf_name(&sec, "pow");
        buf_byte(&sec, 0x00);
        buf_leb128_u(&sec, 1); /* type index 1 */

        buf_section(&output, 2, &sec);
        buf_free(&sec);
    }

    // functions
    {
        WasmBuf sec;
        buf_init(&sec);

        buf_leb128_u(&sec, (uint32_t)total_user_funcs);
        /* type index = 3 + i (offset by import types) */
        int n_import_types = 3;
        for (int i = 0; i < total_user_funcs; i++)
            buf_leb128_u(&sec, (uint32_t)(n_import_types + i));

        buf_section(&output, 3, &sec);
        buf_free(&sec);
    }

    // table
    {
        WasmBuf sec;
        buf_init(&sec);

        buf_leb128_u(&sec, 1);       /* 1 table */
        buf_byte(&sec, 0x70);        /* funcref */
        buf_byte(&sec, 0x00);        /* limits: min only */
        int table_size = total_user_funcs > 0 ? total_user_funcs : 1;
        buf_leb128_u(&sec, (uint32_t)table_size);

        buf_section(&output, 4, &sec);
        buf_free(&sec);
    }

    // memory
    {
        WasmBuf sec;
        buf_init(&sec);
        buf_leb128_u(&sec, 1);    /* 1 memory */
        buf_byte(&sec, 0x00);     /* limits: min only */
        buf_leb128_u(&sec, 1);    /* 1 page (64KB) */
        buf_section(&output, 5, &sec);
        buf_free(&sec);
    }

    // globals
    {
        WasmBuf sec;
        buf_init(&sec);

        buf_leb128_u(&sec, 2);              /* 2 globals */

        /* Global 0: heap pointer (mutable i32) */
        buf_byte(&sec, WASM_TYPE_I32);      /* type: i32 */
        buf_byte(&sec, 0x01);               /* mutable */
        /* init expr: i32.const <heap_start>; end */
        buf_byte(&sec, OP_I32_CONST);
        buf_leb128_s(&sec, (int32_t)heap_start);
        buf_byte(&sec, OP_END);

        /* Global 1: error flag for try/catch (mutable i32, init 0) */
        buf_byte(&sec, WASM_TYPE_I32);      /* type: i32 */
        buf_byte(&sec, 0x01);               /* mutable */
        buf_byte(&sec, OP_I32_CONST);
        buf_leb128_s(&sec, 0);
        buf_byte(&sec, OP_END);

        buf_section(&output, 6, &sec);
        buf_free(&sec);
    }

    // exports
    {
        WasmBuf sec;
        buf_init(&sec);

        buf_leb128_u(&sec, 2); /* 2 exports */
        buf_name(&sec, "main");
        buf_byte(&sec, 0x00); /* export kind: function */
        buf_leb128_u(&sec, (uint32_t)(user_func_base + main_func_idx));
        buf_name(&sec, "memory");
        buf_byte(&sec, 0x02); /* export kind: memory */
        buf_leb128_u(&sec, 0);

        buf_section(&output, 7, &sec);
        buf_free(&sec);
    }

    // elements
    if (total_user_funcs > 0) {
        WasmBuf sec;
        buf_init(&sec);

        buf_leb128_u(&sec, 1);           /* 1 element segment */
        buf_leb128_u(&sec, 0);           /* table index 0 */
        /* offset expr: i32.const 0; end */
        buf_byte(&sec, OP_I32_CONST);
        buf_leb128_s(&sec, 0);
        buf_byte(&sec, OP_END);
        /* function indices */
        buf_leb128_u(&sec, (uint32_t)total_user_funcs);
        for (int i = 0; i < total_user_funcs; i++)
            buf_leb128_u(&sec, (uint32_t)(user_func_base + i));

        buf_section(&output, 9, &sec);
        buf_free(&sec);
    }

    // code
    {
        WasmBuf sec;
        buf_init(&sec);

        buf_leb128_u(&sec, (uint32_t)total_user_funcs);

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

    // data
    if (strtab.count > 0) {
        WasmBuf sec;
        buf_init(&sec);

        buf_leb128_u(&sec, 1);   /* 1 data segment */
        buf_leb128_u(&sec, 0);   /* memory index 0 */
        /* offset expr: i32.const 0; end */
        buf_byte(&sec, OP_I32_CONST);
        buf_leb128_s(&sec, 0);
        buf_byte(&sec, OP_END);
        /* data bytes */
        buf_leb128_u(&sec, (uint32_t)strtab.total_len);
        for (int i = 0; i < strtab.count; i++) {
            const char *s = strtab.strs[i];
            int slen = strtab.lengths[i];
            buf_bytes(&sec, (const uint8_t *)s, slen);
            buf_byte(&sec, 0x00);  /* NUL terminator */
        }

        buf_section(&output, 11, &sec);
        buf_free(&sec);
    }

    // write output
    FILE *f = fopen(out_path, "wb");
    if (!f) {
        fprintf(stderr, "xs wasm: cannot open '%s' for writing\n", out_path);
        buf_free(&output);
        funcs_free(&funcs);
        return 1;
    }

    fwrite(output.data, 1, (size_t)output.len, f);
    fclose(f);

    printf("xs wasm: wrote %d bytes to %s\n", output.len, out_path);

    buf_free(&output);
    funcs_free(&funcs);
    struct_layouts_free(&g_struct_layouts);
    return 0;
}
