/* jit.c -- x86-64 copy-patch codegen for XS bytecode.
 *
 * Callee-saved register mapping:
 *   r12 = XS stack ptr, r13 = consts, r14 = locals, r15 = globals
 */

#ifdef XSC_ENABLE_JIT

#include "jit/jit.h"
#include "core/value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(__x86_64__) || defined(_M_X64)
#define JIT_ARCH_X86_64 1
#else
#define JIT_ARCH_X86_64 0
#endif

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/mman.h>
#include <unistd.h>
#define JIT_HAS_MMAP 1
#define JIT_HAS_WIN 0
#elif defined(_WIN32)
#include <windows.h>
#define JIT_HAS_MMAP 0
#define JIT_HAS_WIN 1
#else
#define JIT_HAS_MMAP 0
#define JIT_HAS_WIN 0
#endif

/* coerce both operands to double, returns 1 on success */
static int numcoerce(Value *a, Value *b, double *fa, double *fb) {
    if ((a->tag != XS_INT && a->tag != XS_FLOAT) ||
        (b->tag != XS_INT && b->tag != XS_FLOAT))
        return 0;
    *fa = a->tag == XS_INT ? (double)a->i : a->f;
    *fb = b->tag == XS_INT ? (double)b->i : b->f;
    return 1;
}

static Value *jit_rt_add(Value *a, Value *b) {
    if (a->tag == XS_INT && b->tag == XS_INT)
        return xs_int(a->i + b->i);
    double fa, fb;
    if (numcoerce(a, b, &fa, &fb))
        return xs_float(fa + fb);
    if (a->tag == XS_STR && b->tag == XS_STR) {
        size_t la = strlen(a->s), lb = strlen(b->s);
        char *buf = xs_malloc(la + lb + 1);
        memcpy(buf, a->s, la);
        memcpy(buf + la, b->s, lb + 1);
        Value *r = xs_str(buf);
        free(buf);
        return r;
    }
    return xs_null();
}

static Value *jit_rt_sub(Value *a, Value *b) {
    if (a->tag == XS_INT && b->tag == XS_INT)
        return xs_int(a->i - b->i);
    double fa, fb;
    if (numcoerce(a, b, &fa, &fb))
        return xs_float(fa - fb);
    return xs_null();
}

static Value *jit_rt_mul(Value *a, Value *b) {
    if (a->tag == XS_INT && b->tag == XS_INT)
        return xs_int(a->i * b->i);
    double fa, fb;
    if (numcoerce(a, b, &fa, &fb))
        return xs_float(fa * fb);
    return xs_null();
}

static Value *jit_rt_div(Value *a, Value *b) {
    if (a->tag == XS_INT && b->tag == XS_INT) {
        if (b->i == 0) return xs_null();
        return xs_int(a->i / b->i);
    }
    double fa, fb;
    if (numcoerce(a, b, &fa, &fb)) {
        if (fb == 0.0) return xs_null();
        return xs_float(fa / fb);
    }
    return xs_null();
}

static Value *jit_rt_mod(Value *a, Value *b) {
    if (a->tag == XS_INT && b->tag == XS_INT) {
        if (b->i == 0) return xs_null();
        return xs_int(a->i % b->i);
    }
    double fa, fb;
    if (numcoerce(a, b, &fa, &fb)) {
        if (fb == 0.0) return xs_null();
        return xs_float(fmod(fa, fb));
    }
    return xs_null();
}

static Value *jit_rt_pow(Value *a, Value *b) {
    double fa = (a->tag == XS_INT) ? (double)a->i : (a->tag == XS_FLOAT ? a->f : 0.0);
    double fb = (b->tag == XS_INT) ? (double)b->i : (b->tag == XS_FLOAT ? b->f : 0.0);
    double r = pow(fa, fb);
    if (a->tag == XS_INT && b->tag == XS_INT && b->i >= 0 &&
        r == (double)(int64_t)r)
        return xs_int((int64_t)r);
    return xs_float(r);
}

static Value *jit_rt_neg(Value *a) {
    if (a->tag == XS_INT) return xs_int(-a->i);
    if (a->tag == XS_FLOAT) return xs_float(-a->f);
    return xs_null();
}

static Value *jit_rt_not(Value *a) {
    return xs_bool(!value_truthy(a));
}

static Value *jit_rt_eq(Value *a, Value *b)  { return xs_bool(value_equal(a, b)); }
static Value *jit_rt_neq(Value *a, Value *b) { return xs_bool(!value_equal(a, b)); }
static Value *jit_rt_lt(Value *a, Value *b)  { return xs_bool(value_cmp(a, b) < 0); }
static Value *jit_rt_gt(Value *a, Value *b)  { return xs_bool(value_cmp(a, b) > 0); }
static Value *jit_rt_lte(Value *a, Value *b) { return xs_bool(value_cmp(a, b) <= 0); }
static Value *jit_rt_gte(Value *a, Value *b) { return xs_bool(value_cmp(a, b) >= 0); }

static Value *jit_rt_concat(Value *a, Value *b) {
    char *sa = value_str(a);
    char *sb = value_str(b);
    size_t la = strlen(sa), lb = strlen(sb);
    char *buf = xs_malloc(la + lb + 1);
    memcpy(buf, sa, la);
    memcpy(buf + la, sb, lb + 1);
    Value *r = xs_str(buf);
    free(sa); free(sb); free(buf);
    return r;
}

static Value *jit_rt_band(Value *a, Value *b) {
    if (a->tag == XS_INT && b->tag == XS_INT) return xs_int(a->i & b->i);
    return xs_null();
}
static Value *jit_rt_bor(Value *a, Value *b) {
    if (a->tag == XS_INT && b->tag == XS_INT) return xs_int(a->i | b->i);
    return xs_null();
}
static Value *jit_rt_bxor(Value *a, Value *b) {
    if (a->tag == XS_INT && b->tag == XS_INT) return xs_int(a->i ^ b->i);
    return xs_null();
}
static Value *jit_rt_bnot(Value *a) {
    if (a->tag == XS_INT) return xs_int(~a->i);
    return xs_null();
}
static Value *jit_rt_shl(Value *a, Value *b) {
    if (a->tag == XS_INT && b->tag == XS_INT) return xs_int(a->i << b->i);
    return xs_null();
}
static Value *jit_rt_shr(Value *a, Value *b) {
    if (a->tag == XS_INT && b->tag == XS_INT) return xs_int(a->i >> b->i);
    return xs_null();
}

static int jit_rt_truthy(Value *v) {
    return value_truthy(v);
}

static Value *jit_rt_floor_div(Value *a, Value *b) {
    double av = a->tag == XS_INT ? (double)a->i : a->f;
    double bv = b->tag == XS_INT ? (double)b->i : b->f;
    if (bv == 0.0) return xs_null();
    return xs_int((int64_t)floor(av / bv));
}

static Value *jit_rt_spaceship(Value *a, Value *b) {
    int cmp = value_cmp(a, b);
    return xs_int(cmp < 0 ? -1 : cmp > 0 ? 1 : 0);
}

static Value *jit_rt_in(Value *a, Value *b) {
    int found = 0;
    if (b->tag == XS_ARRAY) {
        for (int j = 0; j < b->arr->len; j++)
            if (value_equal(a, b->arr->items[j])) { found = 1; break; }
    } else if (b->tag == XS_MAP || b->tag == XS_MODULE) {
        if (a->tag == XS_STR) found = map_has(b->map, a->s);
    } else if (b->tag == XS_STR && a->tag == XS_STR) {
        found = strstr(b->s, a->s) != NULL;
    } else if (b->tag == XS_RANGE) {
        if (a->tag == XS_INT) {
            int64_t v = a->i;
            found = v >= b->range->start &&
                    (b->range->inclusive ? v <= b->range->end : v < b->range->end);
        }
    }
    return xs_bool(found);
}

static Value *jit_rt_is(Value *a, Value *b) {
    int match = 0;
    if (b->tag == XS_STR) {
        const char *t = b->s;
        if      (strcmp(t, "int") == 0 || strcmp(t, "i64") == 0) match = (a->tag == XS_INT);
        else if (strcmp(t, "float") == 0 || strcmp(t, "f64") == 0) match = (a->tag == XS_FLOAT);
        else if (strcmp(t, "str") == 0 || strcmp(t, "string") == 0) match = (a->tag == XS_STR);
        else if (strcmp(t, "bool") == 0) match = (a->tag == XS_BOOL);
        else if (strcmp(t, "array") == 0) match = (a->tag == XS_ARRAY);
        else if (strcmp(t, "map") == 0) match = (a->tag == XS_MAP);
        else if (strcmp(t, "null") == 0) match = (a->tag == XS_NULL);
        else if (strcmp(t, "fn") == 0 || strcmp(t, "function") == 0) match = (a->tag == XS_FUNC || a->tag == XS_NATIVE || a->tag == XS_CLOSURE);
        else if (strcmp(t, "tuple") == 0) match = (a->tag == XS_TUPLE);
    }
    return xs_bool(match);
}

static Value *jit_rt_make_tuple(Value **sp_bottom, int n) {
    Value *tup = xs_tuple_new();
    for (int i = 0; i < n; i++) {
        array_push(tup->arr, sp_bottom[i]);
        value_decref(sp_bottom[i]);
    }
    return tup;
}

static Value *jit_rt_make_range(Value *start, Value *end, int inclusive) {
    int64_t s = start->tag == XS_INT ? start->i : 0;
    int64_t e = end->tag == XS_INT ? end->i : 0;
    value_decref(start);
    value_decref(end);
    return xs_range(s, e, inclusive);
}

static Value *jit_rt_iter_len(Value *iter) {
    int64_t len = 0;
    if (iter->tag == XS_ARRAY || iter->tag == XS_TUPLE) len = iter->arr->len;
    else if (iter->tag == XS_STR) len = (int64_t)strlen(iter->s);
    else if (iter->tag == XS_RANGE) {
        int64_t diff = iter->range->end - iter->range->start;
        if (!iter->range->inclusive) len = diff > 0 ? diff : 0;
        else len = diff >= 0 ? diff + 1 : 0;
    }
    value_decref(iter);
    return xs_int(len);
}

static Value *jit_rt_iter_get(Value *iter, Value *idx) {
    Value *r;
    int64_t i = idx->tag == XS_INT ? idx->i : 0;
    if (iter->tag == XS_ARRAY || iter->tag == XS_TUPLE) {
        r = (i >= 0 && i < iter->arr->len) ? value_incref(iter->arr->items[i]) : value_incref(XS_NULL_VAL);
    } else if (iter->tag == XS_STR) {
        int64_t slen = (int64_t)strlen(iter->s);
        if (i >= 0 && i < slen) { char buf[2] = {iter->s[i], 0}; r = xs_str(buf); }
        else r = value_incref(XS_NULL_VAL);
    } else if (iter->tag == XS_RANGE) {
        r = xs_int(iter->range->start + i);
    } else {
        r = value_incref(XS_NULL_VAL);
    }
    value_decref(iter);
    value_decref(idx);
    return r;
}

static Value *jit_rt_method_call(Value **sp_bottom, int argc, Value *name_val) {
    Value *obj = sp_bottom[0];
    Value **args = sp_bottom + 1;
    Value *result = NULL;

    /* Look up method on object */
    const char *mname = name_val->s;
    Value *method = NULL;

    if (obj->tag == XS_MAP || obj->tag == XS_MODULE) {
        method = map_get(obj->map, mname);
        if (!method) {
            Value *methods = map_get(obj->map, "__methods");
            if (methods && methods->tag == XS_MAP)
                method = map_get(methods->map, mname);
        }
    } else if (obj->tag == XS_INST && obj->inst) {
        method = map_get(obj->inst->fields, mname);
        if (!method && obj->inst->methods)
            method = map_get(obj->inst->methods, mname);
    }

    if (method && method->tag == XS_NATIVE) {
        /* For native methods, pass obj as first arg */
        Value *margs[17];
        margs[0] = obj;
        int total = 1 + argc;
        if (total > 17) total = 17;
        for (int i = 0; i < argc && i < 16; i++) margs[i + 1] = args[i];
        result = method->native(NULL, margs, total);
    }

    if (!result) result = value_incref(XS_NULL_VAL);
    for (int i = 0; i < argc; i++) value_decref(args[i]);
    value_decref(obj);
    return result;
}

/* Store-local helper: decrefs old value, stores new, returns new for caller convenience */
static Value *jit_rt_store_local(Value **locals, int slot, Value *new_val) {
    Value *old = locals[slot];
    if (old) value_decref(old);
    locals[slot] = new_val;
    return new_val;
}

/* Load global: look up name in globals map */
static Value *jit_rt_load_global(XSMap *globals, Value *name_val) {
    if (!globals || !name_val || name_val->tag != XS_STR) return xs_null();
    Value *v = map_get(globals, name_val->s);
    return v ? value_incref(v) : value_incref(XS_NULL_VAL);
}

/* Store global: set name in globals map */
static Value *jit_rt_store_global(XSMap *globals, Value *name_val, Value *val) {
    if (!globals || !name_val || name_val->tag != XS_STR) return val;
    map_set(globals, name_val->s, val);
    return val;
}

/* Make array: given stack pointer and count, pop n values and build array */
static Value *jit_rt_make_array(Value **sp_bottom, int n) {
    Value *arr = xs_array_new();
    /* sp_bottom points to first element (elements are sp_bottom[0..n-1]) */
    for (int i = 0; i < n; i++) {
        array_push(arr->arr, sp_bottom[i]);
        value_decref(sp_bottom[i]);
    }
    return arr;
}

/* Make map: given stack pointer and count of pairs */
static Value *jit_rt_make_map(Value **sp_bottom, int npairs) {
    Value *m = xs_map_new();
    for (int i = 0; i < npairs; i++) {
        Value *k = sp_bottom[i * 2];
        Value *v = sp_bottom[i * 2 + 1];
        if (k->tag == XS_STR) map_set(m->map, k->s, v);
        value_decref(k);
        value_decref(v);
    }
    return m;
}

/* Index get: col[idx] */
static Value *jit_rt_index_get(Value *col, Value *idx) {
    Value *r;
    if ((col->tag == XS_ARRAY || col->tag == XS_TUPLE) && idx->tag == XS_INT) {
        int64_t i = idx->i;
        if (i < 0) i += col->arr->len;
        r = (i >= 0 && i < col->arr->len) ? value_incref(col->arr->items[i])
                                            : value_incref(XS_NULL_VAL);
    } else if (col->tag == XS_MAP && idx->tag == XS_STR) {
        Value *v = map_get(col->map, idx->s);
        r = v ? value_incref(v) : value_incref(XS_NULL_VAL);
    } else if (col->tag == XS_STR && idx->tag == XS_INT) {
        const char *s = col->s;
        int64_t slen = (int64_t)strlen(s);
        int64_t i = idx->i;
        if (i < 0) i += slen;
        if (i >= 0 && i < slen) {
            char buf[2] = {s[i], 0};
            r = xs_str(buf);
        } else {
            r = value_incref(XS_NULL_VAL);
        }
    } else {
        r = value_incref(XS_NULL_VAL);
    }
    value_decref(col);
    value_decref(idx);
    return r;
}

/* Index set: col[idx] = val */
static Value *jit_rt_index_set(Value *col, Value *idx, Value *val) {
    if ((col->tag == XS_ARRAY || col->tag == XS_TUPLE) && idx->tag == XS_INT) {
        int64_t i = idx->i;
        if (i >= 0 && i < col->arr->len) {
            value_decref(col->arr->items[i]);
            col->arr->items[i] = value_incref(val);
        }
    } else if (col->tag == XS_MAP && idx->tag == XS_STR) {
        map_set(col->map, idx->s, val);
    }
    value_decref(val);
    value_decref(idx);
    value_decref(col);
    return xs_null();
}

/* Load field: obj.name */
static Value *jit_rt_load_field(Value *obj, Value *name_val) {
    Value *r = NULL;
    const char *name = name_val->s;
    if (obj->tag == XS_MAP || obj->tag == XS_MODULE) {
        Value *v = map_get(obj->map, name);
        if (v) {
            r = value_incref(v);
        } else {
            Value *methods = map_get(obj->map, "__methods");
            if (methods && methods->tag == XS_MAP) {
                Value *mv = map_get(methods->map, name);
                if (mv) r = value_incref(mv);
            }
            if (!r) {
                Value *impl = map_get(obj->map, "__impl__");
                if (impl && impl->tag == XS_MAP) {
                    Value *mv = map_get(impl->map, name);
                    if (mv) r = value_incref(mv);
                }
            }
        }
    } else if (obj->tag == XS_INST && obj->inst) {
        Value *v = map_get(obj->inst->fields, name);
        if (v) r = value_incref(v);
        if (!r && obj->inst->methods) {
            Value *mv = map_get(obj->inst->methods, name);
            if (mv) r = value_incref(mv);
        }
    }
    if (!r) r = value_incref(XS_NULL_VAL);
    value_decref(obj);
    return r;
}

/* Store field: obj.name = val */
static Value *jit_rt_store_field(Value *obj, Value *name_val, Value *val) {
    const char *name = name_val->s;
    if (obj->tag == XS_MAP || obj->tag == XS_MODULE) {
        map_set(obj->map, name, val);
    } else if (obj->tag == XS_INST && obj->inst) {
        map_set(obj->inst->fields, name, val);
    }
    value_decref(val);
    value_decref(obj);
    return xs_null();
}

/* Call dispatch: native function call from JIT.
 * sp_bottom points to [callee, arg0, arg1, ...] on the XS stack.
 * argc is the number of arguments (not counting callee).
 * Returns the result value. Callee and args are decref'd.
 */
static Value *jit_rt_call(Value **sp_bottom, int argc) {
    Value *callee = sp_bottom[0];
    Value **args  = sp_bottom + 1;
    Value *result = NULL;

    if (callee->tag == XS_NATIVE) {
        result = callee->native(NULL, args, argc);
        if (!result) result = value_incref(XS_NULL_VAL);
    } else {
        /* Non-native callables cannot be handled purely in JIT;
         * return null to signal bailout. In practice, closures
         * trigger the bail path in jit_compile so this is only
         * reached for XS_NATIVE. */
        fprintf(stderr, "jit: call on non-native (tag=%d), returning null\n", callee->tag);
        result = value_incref(XS_NULL_VAL);
    }

    /* Decref args and callee */
    for (int i = 0; i < argc; i++) value_decref(args[i]);
    value_decref(callee);

    return result;
}

/* emitter */

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
    int      overflow;
} Emitter;

static void emit_init(Emitter *e, uint8_t *buf, size_t cap, size_t start) {
    e->buf = buf; e->cap = cap; e->pos = start; e->overflow = 0;
}

static inline void emit_byte(Emitter *e, uint8_t b) {
    if (e->pos < e->cap) e->buf[e->pos++] = b;
    else e->overflow = 1;
}

static inline void emit_u32(Emitter *e, uint32_t v) {
    emit_byte(e, (uint8_t)(v));
    emit_byte(e, (uint8_t)(v >> 8));
    emit_byte(e, (uint8_t)(v >> 16));
    emit_byte(e, (uint8_t)(v >> 24));
}

static inline void emit_u64(Emitter *e, uint64_t v) {
    emit_u32(e, (uint32_t)v);
    emit_u32(e, (uint32_t)(v >> 32));
}

static inline void emit_i32(Emitter *e, int32_t v) {
    emit_u32(e, (uint32_t)v);
}

static inline void emit_modrm(Emitter *e, uint8_t mod, uint8_t reg, uint8_t rm) {
    emit_byte(e, (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
}

/* Register encoding: low 3 bits; high bit goes in REX.R/B */
#define RAX 0
#define RCX 1
#define RDX 2
#define RBX 3
#define RSP 4
#define RBP 5
#define RSI 6
#define RDI 7
/* Extended registers use encoding 0-7 with REX.B or REX.R set */


/* REX byte: 0100WRXB */
static inline uint8_t rex(int w, int r_ext, int x_ext, int b_ext) {
    return (uint8_t)(0x40 | (w ? 8 : 0) | (r_ext ? 4 : 0) | (x_ext ? 2 : 0) | (b_ext ? 1 : 0));
}

/* mov reg64, imm64 */
static void emit_mov_reg_imm64(Emitter *e, int reg, uint64_t imm) {
    int ext = (reg >= 8);
    emit_byte(e, rex(1, 0, 0, ext));
    emit_byte(e, (uint8_t)(0xB8 + (reg & 7)));
    emit_u64(e, imm);
}

/* mov dst, src (64-bit reg-reg) */
static void emit_mov_reg_reg(Emitter *e, int dst, int src) {
    emit_byte(e, rex(1, src >= 8, 0, dst >= 8));
    emit_byte(e, 0x89);
    emit_modrm(e, 3, (uint8_t)(src & 7), (uint8_t)(dst & 7));
}

/* push reg64 */
static void emit_push_reg(Emitter *e, int reg) {
    if (reg >= 8) emit_byte(e, (uint8_t)(0x41));
    emit_byte(e, (uint8_t)(0x50 + (reg & 7)));
}

/* pop reg64 */
static void emit_pop_reg(Emitter *e, int reg) {
    if (reg >= 8) emit_byte(e, (uint8_t)(0x41));
    emit_byte(e, (uint8_t)(0x58 + (reg & 7)));
}

/* add reg64, imm8 */
static void emit_add_reg_imm8(Emitter *e, int reg, int8_t imm) {
    emit_byte(e, rex(1, 0, 0, reg >= 8));
    emit_byte(e, 0x83);
    emit_modrm(e, 3, 0, (uint8_t)(reg & 7));
    emit_byte(e, (uint8_t)imm);
}

/* sub reg64, imm8 */
static void emit_sub_reg_imm8(Emitter *e, int reg, int8_t imm) {
    emit_byte(e, rex(1, 0, 0, reg >= 8));
    emit_byte(e, 0x83);
    emit_modrm(e, 3, 5, (uint8_t)(reg & 7));
    emit_byte(e, (uint8_t)imm);
}

/* call rax  (FF /2) */
static void emit_call_rax(Emitter *e) {
    emit_byte(e, 0xFF);
    emit_modrm(e, 3, 2, RAX);
}

/* ret */
static void emit_ret(Emitter *e) { emit_byte(e, 0xC3); }

/* nop */
static void emit_nop(Emitter *e) { emit_byte(e, 0x90); }

/* Load addr into rax, call rax */
static void emit_call_abs(Emitter *e, void *fn) {
    emit_mov_reg_imm64(e, RAX, (uint64_t)(uintptr_t)fn);
    emit_call_rax(e);
}

/* test eax, eax */
static void emit_test_eax_eax(Emitter *e) {
    emit_byte(e, 0x85); emit_modrm(e, 3, RAX, RAX);
}

/* jmp rel32 — returns offset of the rel32 to patch */
static size_t emit_jmp_rel32(Emitter *e) {
    emit_byte(e, 0xE9);
    size_t patch = e->pos;
    emit_i32(e, 0);
    return patch;
}

/* je rel32 */
static size_t emit_je_rel32(Emitter *e) {
    emit_byte(e, 0x0F); emit_byte(e, 0x84);
    size_t patch = e->pos;
    emit_i32(e, 0);
    return patch;
}

/* jne rel32 */
static size_t emit_jne_rel32(Emitter *e) {
    emit_byte(e, 0x0F); emit_byte(e, 0x85);
    size_t patch = e->pos;
    emit_i32(e, 0);
    return patch;
}

/* Patch rel32 at patch_offset to jump to target_pos */
static void patch_rel32(Emitter *e, size_t patch_offset, size_t target_pos) {
    int32_t rel = (int32_t)(target_pos - (patch_offset + 4));
    e->buf[patch_offset + 0] = (uint8_t)(rel);
    e->buf[patch_offset + 1] = (uint8_t)(rel >> 8);
    e->buf[patch_offset + 2] = (uint8_t)(rel >> 16);
    e->buf[patch_offset + 3] = (uint8_t)(rel >> 24);
}

// XS stack ops (r12 = top)

/*
 * Memory access to [r12 + disp]:  r12 is extended (reg 12), encoded as
 *   rm=100 (RSP encoding) which triggers SIB.  SIB = 0x24 means base=r12,
 *   index=none, scale=1.
 */

/* Push rax onto XS stack: mov [r12], rax; add r12, 8 */
static void emit_xs_push_rax(Emitter *e) {
    /* mov [r12], rax — REX.WB=0x49, 0x89, ModRM(00,RAX,100), SIB(0x24) */
    emit_byte(e, 0x49); emit_byte(e, 0x89);
    emit_modrm(e, 0, RAX, RSP);
    emit_byte(e, 0x24);
    emit_add_reg_imm8(e, 12, 8);
}

/* Pop XS stack into rax: sub r12,8; mov rax,[r12] */
static void emit_xs_pop_rax(Emitter *e) {
    emit_sub_reg_imm8(e, 12, 8);
    /* mov rax, [r12] — REX.WB=0x49, 0x8B, ModRM(00,RAX,100), SIB(0x24) */
    emit_byte(e, 0x49); emit_byte(e, 0x8B);
    emit_modrm(e, 0, RAX, RSP);
    emit_byte(e, 0x24);
}

/* Pop XS stack into a GPR (0-7 range only, for rdi/rsi/rcx etc.) */
static void emit_xs_pop_gpr(Emitter *e, int reg) {
    emit_sub_reg_imm8(e, 12, 8);
    /* mov reg, [r12] */
    emit_byte(e, rex(1, reg >= 8, 0, 1)); /* REX.W, R=reg>=8, B=1 for r12 */
    emit_byte(e, 0x8B);
    emit_modrm(e, 0, (uint8_t)(reg & 7), RSP);
    emit_byte(e, 0x24);
}

/* Peek top of stack into rax: mov rax, [r12 - 8] */
static void emit_xs_peek_rax(Emitter *e) {
    /* mov rax, [r12 - 8] — disp8=-8 */
    emit_byte(e, 0x49); emit_byte(e, 0x8B);
    emit_modrm(e, 1, RAX, RSP); /* mod=01 disp8 */
    emit_byte(e, 0x24);
    emit_byte(e, 0xF8); /* -8 */
}

/* Load constant[idx] into rax: mov rax, [r13 + idx*8] */
static void emit_load_const(Emitter *e, int idx) {
    int32_t disp = idx * 8;
    /* r13 encoded as rm=101 (RBP encoding). With mod=10 it's [r13+disp32]. */
    emit_byte(e, 0x49); emit_byte(e, 0x8B);
    emit_modrm(e, 2, RAX, RBP); /* rm=101=r13 with REX.B */
    emit_i32(e, disp);
}

/* Load local[slot] into rax: mov rax, [r14 + slot*8] */
static void emit_load_local(Emitter *e, int slot) {
    int32_t disp = slot * 8;
    /* r14 encoded as rm=110 */
    emit_byte(e, 0x49); emit_byte(e, 0x8B);
    emit_modrm(e, 2, RAX, RSI); /* rm=110=r14 with REX.B */
    emit_i32(e, disp);
}


/* Call value_incref(rax), result in rax */
static void emit_incref_rax(Emitter *e) {
    emit_mov_reg_reg(e, RDI, RAX);
    emit_call_abs(e, (void *)(uintptr_t)value_incref);
}

/* Call value_decref(rax) */
static void emit_decref_rax(Emitter *e) {
    emit_mov_reg_reg(e, RDI, RAX);
    emit_call_abs(e, (void *)(uintptr_t)value_decref);
}

/* Binary op: pop b, pop a, call fn(a,b), push result */
static void emit_binary_op(Emitter *e, void *fn) {
    emit_xs_pop_gpr(e, RSI);  /* b */
    emit_xs_pop_gpr(e, RDI);  /* a */
    emit_call_abs(e, fn);
    emit_xs_push_rax(e);
}

/* Unary op: pop a, call fn(a), push result */
static void emit_unary_op(Emitter *e, void *fn) {
    emit_xs_pop_gpr(e, RDI);
    emit_call_abs(e, fn);
    emit_xs_push_rax(e);
}

/* prologue/epilogue -- 6 pushes + 8 alignment = 64 bytes (16-aligned) */
static void emit_prologue(Emitter *e) {
    emit_push_reg(e, RBP);
    emit_mov_reg_reg(e, RBP, RSP);
    emit_push_reg(e, 12);
    emit_push_reg(e, 13);
    emit_push_reg(e, 14);
    emit_push_reg(e, 15);
    emit_push_reg(e, RBX);
    emit_sub_reg_imm8(e, RSP, 8); /* align */
    /* r12 = rdi (stack), r13 = rsi (consts), r14 = rdx (locals), r15 = rcx (globals) */
    emit_byte(e, rex(1, 0, 0, 1)); emit_byte(e, 0x89); emit_modrm(e, 3, RDI, (uint8_t)(12 & 7));
    emit_byte(e, rex(1, 0, 0, 1)); emit_byte(e, 0x89); emit_modrm(e, 3, RSI, (uint8_t)(13 & 7));
    emit_byte(e, rex(1, 0, 0, 1)); emit_byte(e, 0x89); emit_modrm(e, 3, RDX, (uint8_t)(14 & 7));
    emit_byte(e, rex(1, 0, 0, 1)); emit_byte(e, 0x89); emit_modrm(e, 3, RCX, (uint8_t)(15 & 7));
}

static void emit_epilogue(Emitter *e) {
    emit_add_reg_imm8(e, RSP, 8);
    emit_pop_reg(e, RBX);
    emit_pop_reg(e, 15);
    emit_pop_reg(e, 14);
    emit_pop_reg(e, 13);
    emit_pop_reg(e, 12);
    emit_pop_reg(e, RBP);
    emit_ret(e);
}

/* Push xs_null() */
static void emit_push_null(Emitter *e) {
    emit_call_abs(e, (void *)(uintptr_t)xs_null);
    emit_xs_push_rax(e);
}

/* Push xs_bool(1) */
static void emit_push_true(Emitter *e) {
    emit_mov_reg_imm64(e, RDI, 1);
    emit_call_abs(e, (void *)(uintptr_t)xs_bool);
    emit_xs_push_rax(e);
}

/* Push xs_bool(0) */
static void emit_push_false(Emitter *e) {
    emit_mov_reg_imm64(e, RDI, 0);
    emit_call_abs(e, (void *)(uintptr_t)xs_bool);
    emit_xs_push_rax(e);
}


XSJIT *jit_new(void) {
    XSJIT *j = xs_calloc(1, sizeof *j);
    j->available = 0;

#if JIT_ARCH_X86_64 && JIT_HAS_MMAP
    j->code_size = XS_JIT_CODE_SIZE;
    j->code = (uint8_t *)mmap(NULL, j->code_size,
                               PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (j->code == MAP_FAILED) {
        j->code = NULL;
        j->code_size = 0;
        fprintf(stderr, "xs jit: mmap failed, JIT unavailable (interpreter fallback)\n");
    } else {
        j->code_used = 0;
        j->available = 1;
    }
#elif JIT_ARCH_X86_64 && JIT_HAS_WIN
    j->code_size = XS_JIT_CODE_SIZE;
    j->code = (uint8_t *)VirtualAlloc(NULL, j->code_size,
                                       MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE);
    if (!j->code) {
        j->code_size = 0;
        fprintf(stderr, "xs jit: VirtualAlloc failed, JIT unavailable\n");
    } else {
        j->code_used = 0;
        j->available = 1;
    }
#else
    j->code = NULL;
    j->code_size = 0;
#endif

    j->n_protos    = XS_JIT_MAX_PROTOS;
    j->compiled    = xs_calloc((size_t)j->n_protos, sizeof(void *));
    j->call_counts = xs_calloc((size_t)j->n_protos, sizeof(int));

    return j;
}

void jit_free(XSJIT *j) {
    if (!j) return;
#if JIT_HAS_MMAP
    if (j->code && j->code_size)
        munmap(j->code, j->code_size);
#elif JIT_HAS_WIN
    if (j->code && j->code_size)
        VirtualFree(j->code, 0, MEM_RELEASE);
#endif
    free(j->compiled);
    free(j->call_counts);
    free(j);
}


#if JIT_ARCH_X86_64 && JIT_HAS_MMAP

/* Jump-patch record */
typedef struct { size_t native_offset; int target_pc; } JitPatch;

void *jit_compile(XSJIT *j, XSProto *proto) {
    if (!j || !j->available || !proto) return NULL;

    Instruction *bc   = proto->chunk.code;
    int          blen = proto->chunk.len;
    if (blen <= 0) return NULL;

    size_t estimate = (size_t)blen * 128 + 512;
    if (j->code_used + estimate > j->code_size) return NULL;

    Emitter em;
    emit_init(&em, j->code, j->code_size, j->code_used);

    size_t fn_start = em.pos;

    /* Prologue */
    emit_prologue(&em);

    size_t *pc_map = xs_calloc((size_t)(blen + 1), sizeof(size_t));
    JitPatch *patches = xs_calloc((size_t)blen, sizeof(JitPatch));
    int       npatches = 0;

    int bail = 0;

    for (int pc = 0; pc < blen && !bail && !em.overflow; pc++) {
        pc_map[pc] = em.pos;

        Instruction instr = bc[pc];
        Opcode op  = INSTR_OPCODE(instr);
        int    bx  = (int)INSTR_Bx(instr);
        int    sbx = (int)INSTR_sBx(instr);
        (void)INSTR_A(instr);
        (void)INSTR_C(instr);

        switch (op) {

        case OP_NOP:
            emit_nop(&em);
            break;

        /* Stack literals */

        case OP_PUSH_CONST:
            emit_load_const(&em, bx);
            emit_incref_rax(&em);
            emit_xs_push_rax(&em);
            break;

        case OP_PUSH_NULL:  emit_push_null(&em);  break;
        case OP_PUSH_TRUE:  emit_push_true(&em);  break;
        case OP_PUSH_FALSE: emit_push_false(&em);  break;

        case OP_POP:
            emit_xs_pop_rax(&em);
            emit_decref_rax(&em);
            break;

        case OP_DUP:
            emit_xs_peek_rax(&em);
            emit_incref_rax(&em);
            emit_xs_push_rax(&em);
            break;

        /* Locals */

        case OP_LOAD_LOCAL:
            emit_load_local(&em, bx);
            emit_incref_rax(&em);
            emit_xs_push_rax(&em);
            break;

        case OP_STORE_LOCAL: {
            /* Pop new value into r15 (callee-saved, survives call) */
            emit_xs_pop_gpr(&em, 15); /* r15 = new_val */

            /* Call jit_rt_store_local(locals=r14, slot=bx, new_val=r15) */
            emit_mov_reg_reg(&em, RDI, 14); /* rdi = r14 (locals) */
            emit_mov_reg_imm64(&em, RSI, (uint64_t)bx); /* rsi = slot */
            emit_mov_reg_reg(&em, RDX, 15); /* rdx = new_val (r15) */
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_store_local);
            break;
        }

        /* Arithmetic */

        case OP_ADD:  emit_binary_op(&em, (void *)(uintptr_t)jit_rt_add); break;
        case OP_SUB:  emit_binary_op(&em, (void *)(uintptr_t)jit_rt_sub); break;
        case OP_MUL:  emit_binary_op(&em, (void *)(uintptr_t)jit_rt_mul); break;
        case OP_DIV:  emit_binary_op(&em, (void *)(uintptr_t)jit_rt_div); break;
        case OP_MOD:  emit_binary_op(&em, (void *)(uintptr_t)jit_rt_mod); break;
        case OP_POW:  emit_binary_op(&em, (void *)(uintptr_t)jit_rt_pow); break;
        case OP_NEG:  emit_unary_op(&em, (void *)(uintptr_t)jit_rt_neg);  break;
        case OP_NOT:  emit_unary_op(&em, (void *)(uintptr_t)jit_rt_not);  break;

        /* Comparisons */

        case OP_EQ:   emit_binary_op(&em, (void *)(uintptr_t)jit_rt_eq);  break;
        case OP_NEQ:  emit_binary_op(&em, (void *)(uintptr_t)jit_rt_neq); break;
        case OP_LT:   emit_binary_op(&em, (void *)(uintptr_t)jit_rt_lt);  break;
        case OP_GT:   emit_binary_op(&em, (void *)(uintptr_t)jit_rt_gt);  break;
        case OP_LTE:  emit_binary_op(&em, (void *)(uintptr_t)jit_rt_lte); break;
        case OP_GTE:  emit_binary_op(&em, (void *)(uintptr_t)jit_rt_gte); break;

        /* String / bitwise */

        case OP_CONCAT: emit_binary_op(&em, (void *)(uintptr_t)jit_rt_concat); break;
        case OP_BAND:   emit_binary_op(&em, (void *)(uintptr_t)jit_rt_band);   break;
        case OP_BOR:    emit_binary_op(&em, (void *)(uintptr_t)jit_rt_bor);    break;
        case OP_BXOR:   emit_binary_op(&em, (void *)(uintptr_t)jit_rt_bxor);   break;
        case OP_BNOT:   emit_unary_op(&em, (void *)(uintptr_t)jit_rt_bnot);    break;
        case OP_SHL:    emit_binary_op(&em, (void *)(uintptr_t)jit_rt_shl);    break;
        case OP_SHR:    emit_binary_op(&em, (void *)(uintptr_t)jit_rt_shr);    break;

        /* Control flow */

        case OP_JUMP:
        case OP_LOOP: {
            int target_pc = pc + 1 + sbx;
            size_t p = emit_jmp_rel32(&em);
            patches[npatches].native_offset = p;
            patches[npatches].target_pc     = target_pc;
            npatches++;
            break;
        }

        case OP_JUMP_IF_FALSE: {
            int target_pc = pc + 1 + sbx;
            /* Pop value, test truthiness, je if false */
            emit_xs_pop_gpr(&em, RDI);
            /* Save in rbx so we can potentially use it (callee-saved) */
            emit_mov_reg_reg(&em, RBX, RDI);
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_truthy);
            emit_test_eax_eax(&em);
            size_t p = emit_je_rel32(&em);
            patches[npatches].native_offset = p;
            patches[npatches].target_pc     = target_pc;
            npatches++;
            break;
        }

        case OP_JUMP_IF_TRUE: {
            int target_pc = pc + 1 + sbx;
            emit_xs_pop_gpr(&em, RDI);
            emit_mov_reg_reg(&em, RBX, RDI);
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_truthy);
            emit_test_eax_eax(&em);
            size_t p = emit_jne_rel32(&em);
            patches[npatches].native_offset = p;
            patches[npatches].target_pc     = target_pc;
            npatches++;
            break;
        }

        case OP_RETURN:
            emit_xs_pop_rax(&em);
            emit_epilogue(&em);
            break;

        case OP_SWAP: {
            /* Swap [r12-8] and [r12-16] */
            /* mov rax, [r12-8] */
            emit_byte(&em, 0x49); emit_byte(&em, 0x8B);
            emit_modrm(&em, 1, RAX, RSP);
            emit_byte(&em, 0x24); emit_byte(&em, 0xF8);
            /* mov rcx, [r12-16] */
            emit_byte(&em, 0x49); emit_byte(&em, 0x8B);
            emit_modrm(&em, 1, RCX, RSP);
            emit_byte(&em, 0x24); emit_byte(&em, 0xF0);
            /* mov [r12-8], rcx */
            emit_byte(&em, 0x49); emit_byte(&em, 0x89);
            emit_modrm(&em, 1, RCX, RSP);
            emit_byte(&em, 0x24); emit_byte(&em, 0xF8);
            /* mov [r12-16], rax */
            emit_byte(&em, 0x49); emit_byte(&em, 0x89);
            emit_modrm(&em, 1, RAX, RSP);
            emit_byte(&em, 0x24); emit_byte(&em, 0xF0);
            break;
        }

        /* Globals */

        case OP_LOAD_GLOBAL: {
            /* We need the globals map passed as a 4th hidden arg.
             * Convention: r15 holds globals pointer (set up in prologue).
             * The name constant is consts[bx]. */
            emit_mov_reg_reg(&em, RDI, 15);     /* rdi = globals (r15) */
            emit_load_const(&em, bx);            /* rax = consts[bx] */
            emit_mov_reg_reg(&em, RSI, RAX);     /* rsi = name_val */
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_load_global);
            emit_xs_push_rax(&em);
            break;
        }

        case OP_STORE_GLOBAL: {
            emit_xs_pop_gpr(&em, RDX);           /* rdx = val */
            emit_mov_reg_reg(&em, RDI, 15);      /* rdi = globals (r15) */
            emit_load_const(&em, bx);             /* rax = consts[bx] */
            emit_mov_reg_reg(&em, RSI, RAX);      /* rsi = name_val */
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_store_global);
            break;
        }

        /* Collections */

        case OP_MAKE_ARRAY: {
            int n = (int)INSTR_C(instr);
            /* r12 points past top; elements start at r12 - n*8 */
            /* rdi = r12 - n*8 (pointer to first element on XS stack) */
            emit_mov_reg_reg(&em, RDI, 12);
            if (n > 0) {
                emit_mov_reg_imm64(&em, RAX, (uint64_t)(n * 8));
                /* sub rdi, rax */
                emit_byte(&em, rex(1, 0, 0, 0)); emit_byte(&em, 0x29);
                emit_modrm(&em, 3, RAX, RDI);
            }
            emit_mov_reg_imm64(&em, RSI, (uint64_t)n);
            /* Save the adjusted sp */
            emit_push_reg(&em, RDI);
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_make_array);
            emit_pop_reg(&em, RDI);
            /* Adjust r12: pop n values, push result */
            /* r12 = rdi (base of popped region) */
            emit_mov_reg_reg(&em, 12, RDI);
            emit_xs_push_rax(&em);
            break;
        }

        case OP_MAKE_MAP: {
            int npairs = (int)INSTR_C(instr);
            int nvals = npairs * 2;
            emit_mov_reg_reg(&em, RDI, 12);
            if (nvals > 0) {
                emit_mov_reg_imm64(&em, RAX, (uint64_t)(nvals * 8));
                emit_byte(&em, rex(1, 0, 0, 0)); emit_byte(&em, 0x29);
                emit_modrm(&em, 3, RAX, RDI);
            }
            emit_mov_reg_imm64(&em, RSI, (uint64_t)npairs);
            emit_push_reg(&em, RDI);
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_make_map);
            emit_pop_reg(&em, RDI);
            emit_mov_reg_reg(&em, 12, RDI);
            emit_xs_push_rax(&em);
            break;
        }

        case OP_INDEX_GET: {
            /* pop idx, pop col, call jit_rt_index_get(col, idx) */
            emit_xs_pop_gpr(&em, RSI);  /* idx */
            emit_xs_pop_gpr(&em, RDI);  /* col */
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_index_get);
            emit_xs_push_rax(&em);
            break;
        }

        case OP_INDEX_SET: {
            /* pop val, pop idx, pop col */
            emit_xs_pop_gpr(&em, RDX);  /* val */
            emit_xs_pop_gpr(&em, RSI);  /* idx */
            emit_xs_pop_gpr(&em, RDI);  /* col */
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_index_set);
            break;
        }

        case OP_LOAD_FIELD: {
            /* pop obj, load consts[bx] as name, call jit_rt_load_field */
            emit_xs_pop_gpr(&em, RBX);           /* rbx = obj (callee-saved) */
            emit_load_const(&em, bx);             /* rax = consts[bx] (name) */
            emit_mov_reg_reg(&em, RDI, RBX);      /* rdi = obj */
            emit_mov_reg_reg(&em, RSI, RAX);       /* rsi = name_val */
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_load_field);
            emit_xs_push_rax(&em);
            break;
        }

        case OP_STORE_FIELD: {
            /* pop val, pop obj */
            emit_xs_pop_gpr(&em, RBX);            /* rbx = val (callee-saved) */
            emit_xs_pop_gpr(&em, RDI);             /* rdi = obj */
            emit_load_const(&em, bx);              /* rax = consts[bx] */
            emit_mov_reg_reg(&em, RSI, RAX);       /* rsi = name_val */
            emit_mov_reg_reg(&em, RDX, RBX);       /* rdx = val */
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_store_field);
            break;
        }

        /* Function calls */

        case OP_CALL: {
            int argc = (int)INSTR_C(instr);
            /* Stack has: [callee, arg0, arg1, ... argN-1]
             * sp_bottom = r12 - (argc+1)*8 */
            emit_mov_reg_reg(&em, RDI, 12);
            emit_mov_reg_imm64(&em, RAX, (uint64_t)((argc + 1) * 8));
            emit_byte(&em, rex(1, 0, 0, 0)); emit_byte(&em, 0x29);
            emit_modrm(&em, 3, RAX, RDI);
            emit_mov_reg_imm64(&em, RSI, (uint64_t)argc);
            /* Save sp_bottom */
            emit_push_reg(&em, RDI);
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_call);
            emit_pop_reg(&em, RDI);
            /* Pop callee+args from XS stack, push result */
            emit_mov_reg_reg(&em, 12, RDI);
            emit_xs_push_rax(&em);
            break;
        }

        /* Additional arithmetic/comparison */

        case OP_FLOOR_DIV: emit_binary_op(&em, (void *)(uintptr_t)jit_rt_floor_div); break;
        case OP_SPACESHIP: emit_binary_op(&em, (void *)(uintptr_t)jit_rt_spaceship); break;
        case OP_IN:        emit_binary_op(&em, (void *)(uintptr_t)jit_rt_in);        break;
        case OP_IS:        emit_binary_op(&em, (void *)(uintptr_t)jit_rt_is);        break;

        /* Tuple */

        case OP_MAKE_TUPLE: {
            int n = (int)INSTR_C(instr);
            emit_mov_reg_reg(&em, RDI, 12);
            if (n > 0) {
                emit_mov_reg_imm64(&em, RAX, (uint64_t)(n * 8));
                emit_byte(&em, rex(1, 0, 0, 0)); emit_byte(&em, 0x29);
                emit_modrm(&em, 3, RAX, RDI);
            }
            emit_mov_reg_imm64(&em, RSI, (uint64_t)n);
            emit_push_reg(&em, RDI);
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_make_tuple);
            emit_pop_reg(&em, RDI);
            emit_mov_reg_reg(&em, 12, RDI);
            emit_xs_push_rax(&em);
            break;
        }

        /* Range */

        case OP_MAKE_RANGE: {
            int inclusive = (int)INSTR_A(instr);
            emit_xs_pop_gpr(&em, RSI);          /* end */
            emit_xs_pop_gpr(&em, RDI);          /* start */
            emit_mov_reg_imm64(&em, RDX, (uint64_t)inclusive);
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_make_range);
            emit_xs_push_rax(&em);
            break;
        }

        /* Iterators */

        case OP_ITER_LEN: {
            emit_xs_pop_gpr(&em, RDI);
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_iter_len);
            emit_xs_push_rax(&em);
            break;
        }

        case OP_ITER_GET: {
            emit_xs_pop_gpr(&em, RSI);  /* idx */
            emit_xs_pop_gpr(&em, RDI);  /* iter */
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_iter_get);
            emit_xs_push_rax(&em);
            break;
        }

        /* Method calls */

        case OP_METHOD_CALL: {
            int argc = (int)INSTR_A(instr);
            /* Stack: [obj, arg0, arg1, ...] */
            emit_mov_reg_reg(&em, RDI, 12);
            emit_mov_reg_imm64(&em, RAX, (uint64_t)((argc + 1) * 8));
            emit_byte(&em, rex(1, 0, 0, 0)); emit_byte(&em, 0x29);
            emit_modrm(&em, 3, RAX, RDI);
            emit_mov_reg_imm64(&em, RSI, (uint64_t)argc);
            emit_load_const(&em, bx);
            emit_mov_reg_reg(&em, RDX, RAX);  /* name_val */
            emit_push_reg(&em, RDI);
            emit_call_abs(&em, (void *)(uintptr_t)jit_rt_method_call);
            emit_pop_reg(&em, RDI);
            emit_mov_reg_reg(&em, 12, RDI);
            emit_xs_push_rax(&em);
            break;
        }

        /* Opcodes we don't JIT: fall back to interpreter */
        default:
            bail = 1;
            break;

        } /* switch */
    } /* for pc */

    pc_map[blen] = em.pos;

    if (bail || em.overflow) {
        free(pc_map);
        free(patches);
        return NULL;
    }

    for (int i = 0; i < npatches; i++) {
        int tpc = patches[i].target_pc;
        if (tpc < 0 || tpc > blen) {
            free(pc_map); free(patches);
            return NULL;
        }
        patch_rel32(&em, patches[i].native_offset, pc_map[tpc]);
    }

    free(pc_map);
    free(patches);

    void *fn = (void *)(j->code + fn_start);
    j->code_used = em.pos;
    return fn;
}

#else

void *jit_compile(XSJIT *j, XSProto *proto) {
    (void)j; (void)proto;
    return NULL;
}

#endif


void *jit_maybe_compile(XSJIT *j, int proto_index, XSProto *proto) {
    if (!j || !j->available) return NULL;
    if (proto_index < 0 || proto_index >= j->n_protos) return NULL;

    if (j->compiled[proto_index]) return j->compiled[proto_index];

    j->call_counts[proto_index]++;
    if (j->call_counts[proto_index] < XS_JIT_THRESHOLD) return NULL;

    void *fn = jit_compile(j, proto);
    if (fn) j->compiled[proto_index] = fn;
    return fn;
}

JitFn jit_get_compiled(XSJIT *j, int proto_index) {
    if (!j || !j->available) return NULL;
    if (proto_index < 0 || proto_index >= j->n_protos) return NULL;
    return (JitFn)j->compiled[proto_index];
}

int jit_available(void) {
#if JIT_ARCH_X86_64 && JIT_HAS_MMAP
    return 1;
#else
    return 0;
#endif
}

#else
/* jit.h provides static inline fallbacks when JIT is disabled. */
#endif
