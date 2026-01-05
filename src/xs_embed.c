#include "xs_embed.h"
#include "core/xs.h"
#include "core/value.h"
#include "core/lexer.h"
#include "core/parser.h"
#include "runtime/interp.h"
#include "core/env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XS_EMBED_STACK_SIZE 256

struct XSState {
    Interp  *interp;
    Value   *stack[XS_EMBED_STACK_SIZE];
    int      sp;
    int      has_error;
    char     error_msg[512];
};

XSState *xs_new(void) {
    XSState *xs = (XSState *)calloc(1, sizeof(XSState));
    if (!xs) return NULL;
    xs->interp = interp_new("<embed>");
    xs->sp = 0;
    xs->has_error = 0;
    xs->error_msg[0] = '\0';
    return xs;
}

void xs_free(XSState *xs) {
    if (!xs) return;
    for (int i = 0; i < xs->sp; i++) {
        if (xs->stack[i]) value_decref(xs->stack[i]);
    }
    interp_free(xs->interp);
    free(xs);
}


static void xs_set_error(XSState *xs, const char *msg) {
    xs->has_error = 1;
    snprintf(xs->error_msg, sizeof(xs->error_msg), "%s", msg);
}

static void xs_clear_error(XSState *xs) {
    xs->has_error = 0;
    xs->error_msg[0] = '\0';
}

static void xs_push(XSState *xs, Value *v) {
    if (xs->sp >= XS_EMBED_STACK_SIZE) {
        xs_set_error(xs, "embed stack overflow");
        return;
    }
    xs->stack[xs->sp++] = value_incref(v);
}

static Value *xs_pop(XSState *xs) {
    if (xs->sp <= 0) {
        xs_set_error(xs, "embed stack underflow");
        return NULL;
    }
    return xs->stack[--xs->sp]; /* caller owns the ref */
}


int xs_eval(XSState *xs, const char *src) {
    xs_clear_error(xs);

    Lexer lex;
    lexer_init(&lex, src, "<embed>");
    TokenArray ta = lexer_tokenize(&lex);

    Parser parser;
    parser_init(&parser, &ta, "<embed>");
    Node *program = parser_parse(&parser);
    token_array_free(&ta);

    if (!program || parser.had_error) {
        xs_set_error(xs, parser.had_error ? parser.error.msg : "parse error");
        if (program) node_free(program);
        return 1;
    }

    interp_run(xs->interp, program);
    node_free(program);

    if (xs->interp->cf.signal == CF_ERROR || xs->interp->cf.signal == CF_PANIC) {
        Value *err = xs->interp->cf.value;
        if (err && err->tag == XS_STR) {
            xs_set_error(xs, err->s);
        } else if (err) {
            char *s = value_repr(err);
            if (s) { xs_set_error(xs, s); free(s); }
            else   { xs_set_error(xs, "runtime error"); }
        } else {
            xs_set_error(xs, "runtime error");
        }
        if (xs->interp->cf.value) {
            value_decref(xs->interp->cf.value);
            xs->interp->cf.value = NULL;
        }
        xs->interp->cf.signal = 0;
        return 1;
    }

    if (xs->interp->cf.value && xs->interp->cf.value->tag != XS_NULL) {
        xs_push(xs, xs->interp->cf.value);
    }
    if (xs->interp->cf.value) {
        value_decref(xs->interp->cf.value);
        xs->interp->cf.value = NULL;
    }
    xs->interp->cf.signal = 0;

    return 0;
}

int xs_eval_file(XSState *xs, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        char buf[600];
        snprintf(buf, sizeof(buf), "cannot open '%s'", path);
        xs_set_error(xs, buf);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = (char *)malloc((size_t)(sz + 1));
    if (!src) { fclose(f); xs_set_error(xs, "out of memory"); return 1; }
    if (fread(src, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f); free(src);
        xs_set_error(xs, "read error");
        return 1;
    }
    src[sz] = '\0';
    fclose(f);

    int rc = xs_eval(xs, src);
    free(src);
    return rc;
}

int xs_call(XSState *xs, const char *fn_name, int argc) {
    if (argc > xs->sp) {
        xs_set_error(xs, "xs_call: not enough values on stack");
        return 1;
    }

    if (argc == 0) {
        size_t len = strlen(fn_name) + 4;
        char *call_str = (char *)malloc(len);
        snprintf(call_str, len, "%s()", fn_name);
        int rc = xs_eval(xs, call_str);
        free(call_str);
        return rc;
    }

    int base = xs->sp - argc;
    char *arg_strs[256];
    size_t total_len = strlen(fn_name) + 3; /* fn_name( ) \0 */
    for (int i = 0; i < argc; i++) {
        arg_strs[i] = value_repr(xs->stack[base + i]);
        if (!arg_strs[i]) arg_strs[i] = xs_strdup("null");
        total_len += strlen(arg_strs[i]);
        if (i > 0) total_len += 2; /* ", " */
    }

    char *call_str = (char *)malloc(total_len);
    char *p = call_str;
    p += sprintf(p, "%s(", fn_name);
    for (int i = 0; i < argc; i++) {
        if (i > 0) { *p++ = ','; *p++ = ' '; }
        size_t slen = strlen(arg_strs[i]);
        memcpy(p, arg_strs[i], slen);
        p += slen;
        free(arg_strs[i]);
    }
    *p++ = ')';
    *p = '\0';

    for (int i = 0; i < argc; i++) {
        Value *v = xs_pop(xs);
        if (v) value_decref(v);
    }

    int rc = xs_eval(xs, call_str);
    free(call_str);
    return rc;
}


void xs_push_int(XSState *xs, int64_t v) {
    Value *val = xs_int(v);
    xs_push(xs, val);
    value_decref(val);
}

void xs_push_float(XSState *xs, double v) {
    Value *val = xs_float(v);
    xs_push(xs, val);
    value_decref(val);
}

void xs_push_str(XSState *xs, const char *v) {
    Value *val = xs_str(v);
    xs_push(xs, val);
    value_decref(val);
}

void xs_push_bool(XSState *xs, int v) {
    Value *val = xs_bool(v);
    xs_push(xs, val);
}

void xs_push_null(XSState *xs) {
    Value *val = xs_null();
    xs_push(xs, val);
}


int64_t xs_pop_int(XSState *xs) {
    Value *v = xs_pop(xs);
    if (!v) return 0;
    int64_t result = 0;
    if (v->tag == XS_INT) result = v->i;
    else xs_set_error(xs, "pop_int: value is not an int");
    value_decref(v);
    return result;
}

double xs_pop_float(XSState *xs) {
    Value *v = xs_pop(xs);
    if (!v) return 0.0;
    double result = 0.0;
    if (v->tag == XS_FLOAT) result = v->f;
    else if (v->tag == XS_INT) result = (double)v->i;
    else xs_set_error(xs, "pop_float: value is not a float");
    value_decref(v);
    return result;
}

char *xs_pop_str(XSState *xs) {
    Value *v = xs_pop(xs);
    if (!v) return NULL;
    char *result = NULL;
    if (v->tag == XS_STR) result = xs_strdup(v->s);
    else {
        result = value_repr(v);
    }
    value_decref(v);
    return result;
}

int xs_pop_bool(XSState *xs) {
    Value *v = xs_pop(xs);
    if (!v) return 0;
    int result = value_truthy(v);
    value_decref(v);
    return result;
}


XSRef xs_pin(XSState *xs, int stack_index) {
    XSRef ref = { NULL };
    if (stack_index < 0 || stack_index >= xs->sp) {
        xs_set_error(xs, "pin: invalid stack index");
        return ref;
    }
    Value *v = xs->stack[stack_index];
    value_incref(v);
    ref.opaque = v;
    return ref;
}

void xs_unpin(XSState *xs, XSRef ref) {
    (void)xs;
    Value *v = (Value *)ref.opaque;
    if (v) value_decref(v);
}

const char *xs_ref_str(XSRef ref) {
    Value *v = (Value *)ref.opaque;
    if (!v || v->tag != XS_STR) return NULL;
    return v->s;
}

// C function trampolines

#define XS_MAX_REGISTERED 64

typedef struct {
    xs_cfunc  fn;       /* original C callback */
    XSState  *xs;       /* owning embed state  */
    int       used;
} CFuncSlot;

static CFuncSlot cfunc_slots[XS_MAX_REGISTERED];
static int       cfunc_slot_count = 0;

static Value *cfunc_dispatch(int slot, Interp *interp, Value **args, int argc) {
    (void)interp;
    CFuncSlot *s = &cfunc_slots[slot];
    XSValue *result = s->fn(s->xs, (XSValue **)args, argc);
    return (Value *)result;
}

#define TRAMPOLINE(N) \
    static Value *cfunc_trampoline_##N(Interp *interp, Value **args, int argc) { \
        return cfunc_dispatch(N, interp, args, argc); \
    }

TRAMPOLINE(0)  TRAMPOLINE(1)  TRAMPOLINE(2)  TRAMPOLINE(3)
TRAMPOLINE(4)  TRAMPOLINE(5)  TRAMPOLINE(6)  TRAMPOLINE(7)
TRAMPOLINE(8)  TRAMPOLINE(9)  TRAMPOLINE(10) TRAMPOLINE(11)
TRAMPOLINE(12) TRAMPOLINE(13) TRAMPOLINE(14) TRAMPOLINE(15)
TRAMPOLINE(16) TRAMPOLINE(17) TRAMPOLINE(18) TRAMPOLINE(19)
TRAMPOLINE(20) TRAMPOLINE(21) TRAMPOLINE(22) TRAMPOLINE(23)
TRAMPOLINE(24) TRAMPOLINE(25) TRAMPOLINE(26) TRAMPOLINE(27)
TRAMPOLINE(28) TRAMPOLINE(29) TRAMPOLINE(30) TRAMPOLINE(31)
TRAMPOLINE(32) TRAMPOLINE(33) TRAMPOLINE(34) TRAMPOLINE(35)
TRAMPOLINE(36) TRAMPOLINE(37) TRAMPOLINE(38) TRAMPOLINE(39)
TRAMPOLINE(40) TRAMPOLINE(41) TRAMPOLINE(42) TRAMPOLINE(43)
TRAMPOLINE(44) TRAMPOLINE(45) TRAMPOLINE(46) TRAMPOLINE(47)
TRAMPOLINE(48) TRAMPOLINE(49) TRAMPOLINE(50) TRAMPOLINE(51)
TRAMPOLINE(52) TRAMPOLINE(53) TRAMPOLINE(54) TRAMPOLINE(55)
TRAMPOLINE(56) TRAMPOLINE(57) TRAMPOLINE(58) TRAMPOLINE(59)
TRAMPOLINE(60) TRAMPOLINE(61) TRAMPOLINE(62) TRAMPOLINE(63)

static NativeFn trampoline_table[XS_MAX_REGISTERED] = {
    cfunc_trampoline_0,  cfunc_trampoline_1,  cfunc_trampoline_2,  cfunc_trampoline_3,
    cfunc_trampoline_4,  cfunc_trampoline_5,  cfunc_trampoline_6,  cfunc_trampoline_7,
    cfunc_trampoline_8,  cfunc_trampoline_9,  cfunc_trampoline_10, cfunc_trampoline_11,
    cfunc_trampoline_12, cfunc_trampoline_13, cfunc_trampoline_14, cfunc_trampoline_15,
    cfunc_trampoline_16, cfunc_trampoline_17, cfunc_trampoline_18, cfunc_trampoline_19,
    cfunc_trampoline_20, cfunc_trampoline_21, cfunc_trampoline_22, cfunc_trampoline_23,
    cfunc_trampoline_24, cfunc_trampoline_25, cfunc_trampoline_26, cfunc_trampoline_27,
    cfunc_trampoline_28, cfunc_trampoline_29, cfunc_trampoline_30, cfunc_trampoline_31,
    cfunc_trampoline_32, cfunc_trampoline_33, cfunc_trampoline_34, cfunc_trampoline_35,
    cfunc_trampoline_36, cfunc_trampoline_37, cfunc_trampoline_38, cfunc_trampoline_39,
    cfunc_trampoline_40, cfunc_trampoline_41, cfunc_trampoline_42, cfunc_trampoline_43,
    cfunc_trampoline_44, cfunc_trampoline_45, cfunc_trampoline_46, cfunc_trampoline_47,
    cfunc_trampoline_48, cfunc_trampoline_49, cfunc_trampoline_50, cfunc_trampoline_51,
    cfunc_trampoline_52, cfunc_trampoline_53, cfunc_trampoline_54, cfunc_trampoline_55,
    cfunc_trampoline_56, cfunc_trampoline_57, cfunc_trampoline_58, cfunc_trampoline_59,
    cfunc_trampoline_60, cfunc_trampoline_61, cfunc_trampoline_62, cfunc_trampoline_63,
};

void xs_register(XSState *xs, const char *name, xs_cfunc fn) {
    if (cfunc_slot_count >= XS_MAX_REGISTERED) {
        xs_set_error(xs, "xs_register: too many registered C functions (max 64)");
        return;
    }
    int slot = cfunc_slot_count++;
    cfunc_slots[slot].fn   = fn;
    cfunc_slots[slot].xs   = xs;
    cfunc_slots[slot].used = 1;

    Value *nval = xs_native(trampoline_table[slot]);
    env_define(xs->interp->globals, name, nval, 0);
    value_decref(nval);
}

int xs_error(XSState *xs) {
    return xs->has_error;
}

const char *xs_error_msg(XSState *xs) {
    return xs->error_msg;
}

XSValue *xs_make_int(XSState *xs, int64_t v) {
    (void)xs;
    return (XSValue *)xs_int(v);
}

XSValue *xs_make_float(XSState *xs, double v) {
    (void)xs;
    return (XSValue *)xs_float(v);
}

XSValue *xs_make_str(XSState *xs, const char *s) {
    (void)xs;
    return (XSValue *)xs_str(s);
}

XSValue *xs_make_bool(XSState *xs, int v) {
    (void)xs;
    return (XSValue *)xs_bool(v);
}

XSValue *xs_make_null(XSState *xs) {
    (void)xs;
    return (XSValue *)xs_null();
}

XSValue *xs_make_array(XSState *xs, XSValue **elems, int n) {
    (void)xs;
    Value *arr = xs_array_new();
    for (int i = 0; i < n; i++) {
        array_push(arr->arr, (Value *)elems[i]);
    }
    return (XSValue *)arr;
}

XSValue *xs_make_map(XSState *xs) {
    (void)xs;
    return (XSValue *)xs_map_new();
}

XSValue *xs_map_set(XSState *xs, XSValue *map_val, const char *key, XSValue *val) {
    (void)xs;
    Value *m = (Value *)map_val;
    if (!m || m->tag != XS_MAP) return map_val;
    map_set(m->map, key, (Value *)val);
    return map_val;
}

int xs_is_int(XSValue *v) {
    Value *val = (Value *)v;
    return val && val->tag == XS_INT;
}

int64_t xs_get_int(XSValue *v) {
    Value *val = (Value *)v;
    if (!val || val->tag != XS_INT) return 0;
    return val->i;
}

const char *xs_get_str(XSValue *v) {
    Value *val = (Value *)v;
    if (!val || val->tag != XS_STR) return NULL;
    return val->s;
}
