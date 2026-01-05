#define _POSIX_C_SOURCE 199309L
#include "vm/vm.h"
#include "core/value.h"
#include "core/xs_bigint.h"
#include "runtime/builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

#define PUSH(v)  (*vm->sp++ = (v))
#define POP()    (*--vm->sp)
#define PEEK(n)  (vm->sp[-(n)-1])
#define FRAME    (&vm->frames[vm->frame_count - 1])
#define CL       (FRAME->closure_val->cl)
#define PROTO    (CL->proto)

static int vm_dispatch(VM *vm, int stop_frame);
static Value *vm_invoke(VM *vm, Value *fn, Value **args, int argc);
static VM *g_vm_for_invoke;

static Upvalue *upvalue_new_open(Value **slot) {
    Upvalue *u = xs_malloc(sizeof *u);
    u->ptr = slot; u->closed_val = NULL; u->is_open = 1; u->refcount = 0; u->next = NULL;
    return u;
}

void upvalue_close_all(Upvalue **list, Value **cutoff) {
    while (*list && (*list)->ptr >= cutoff) {
        Upvalue *u = *list;
        u->closed_val = value_incref(*u->ptr);
        u->ptr        = &u->closed_val;
        u->is_open    = 0;
        *list = u->next;
    }
}

static Upvalue *capture_upvalue(VM *vm, Value **slot) {
    Upvalue **p = &vm->open_upvalues;
    while (*p && (*p)->ptr > slot) p = &(*p)->next;
    if (*p && (*p)->ptr == slot) return *p;
    Upvalue *u = upvalue_new_open(slot);
    u->next = *p; *p = u;
    return u;
}

/* stdlib functions */

static Value *vm_println(Interp *interp, Value **args, int argc) {
    (void)interp;
    for (int i = 0; i < argc; i++) {
        char *s = value_str(args[i]);
        if (i) printf(" ");
        printf("%s", s);
        free(s);
    }
    printf("\n");
    return xs_null();
}

static Value *vm_print(Interp *interp, Value **args, int argc) {
    (void)interp;
    for (int i = 0; i < argc; i++) {
        char *s = value_str(args[i]);
        if (i) printf(" ");
        printf("%s", s);
        free(s);
    }
    return xs_null();
}

static Value *vm_len(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_int(0);
    Value *v = args[0];
    if (v->tag == XS_ARRAY || v->tag == XS_TUPLE) return xs_int(v->arr->len);
    if (v->tag == XS_MAP)   return xs_int(v->map->len);
    if (v->tag == XS_STR)   return xs_int((int64_t)strlen(v->s));
    if (v->tag == XS_RANGE && v->range) {
        int64_t n = v->range->end - v->range->start + (v->range->inclusive ? 1 : 0);
        return xs_int(n < 0 ? 0 : n);
    }
    return xs_int(0);
}

static Value *vm_str(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_str("");
    char *s = value_str(args[0]);
    Value *r = xs_str(s);
    free(s);
    return r;
}

static Value *vm_int_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_int(0);
    Value *v = args[0];
    if (v->tag == XS_INT)   return xs_int(v->i);
    if (v->tag == XS_FLOAT) return xs_int((int64_t)v->f);
    if (v->tag == XS_STR)   return xs_int(atoll(v->s));
    return xs_int(0);
}

static Value *vm_float_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_float(0.0);
    Value *v = args[0];
    if (v->tag == XS_INT)   return xs_float((double)v->i);
    if (v->tag == XS_FLOAT) return xs_float(v->f);
    if (v->tag == XS_STR)   return xs_float(atof(v->s));
    return xs_float(0.0);
}

static Value *vm_type(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_str("null");
    static const char *names[] = {
        "null","bool","int","float","str","char",
        "array","map","tuple","fn","native",
        "struct","enum","class","inst","range","module","closure"
    };
    int tag = (int)args[0]->tag;
    return xs_str(tag >= 0 && tag < (int)(sizeof names/sizeof *names) ? names[tag] : "?");
}

static Value *vm_range(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_array_new();
    int64_t start = 0, end = 0, step = 1;
    if (argc == 1) { end = args[0]->tag==XS_INT ? args[0]->i : 0; }
    else { start = args[0]->tag==XS_INT ? args[0]->i : 0;
           end = args[1]->tag==XS_INT ? args[1]->i : 0; }
    if (argc >= 3 && args[2]->tag==XS_INT) step = args[2]->i;
    if (step == 0) {
        fprintf(stderr, "range: step cannot be zero\n");
        return xs_range(0, 0, 0);
    }
    return xs_range_step(start, end, 0, step);
}

static Value *vm_abs(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_int(0);
    if (args[0]->tag==XS_INT) return xs_int(args[0]->i < 0 ? -args[0]->i : args[0]->i);
    if (args[0]->tag==XS_FLOAT) return xs_float(fabs(args[0]->f));
    return xs_int(0);
}

static Value *vm_min(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2) return argc > 0 ? value_incref(args[0]) : xs_null();
    return value_cmp(args[0],args[1]) <= 0 ? value_incref(args[0]) : value_incref(args[1]);
}

static Value *vm_max(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2) return argc > 0 ? value_incref(args[0]) : xs_null();
    return value_cmp(args[0],args[1]) >= 0 ? value_incref(args[0]) : value_incref(args[1]);
}

static Value *vm_sqrt(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_float(0.0);
    double v = args[0]->tag==XS_INT ? (double)args[0]->i : args[0]->f;
    return xs_float(sqrt(v));
}

static Value *vm_pow(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2) return xs_float(0.0);
    double a = args[0]->tag==XS_INT ? (double)args[0]->i : args[0]->f;
    double b = args[1]->tag==XS_INT ? (double)args[1]->i : args[1]->f;
    return xs_float(pow(a, b));
}

static Value *vm_floor(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_int(0);
    double v = args[0]->tag==XS_INT ? (double)args[0]->i : args[0]->f;
    return xs_int((int64_t)floor(v));
}

static Value *vm_ceil(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_int(0);
    double v = args[0]->tag==XS_INT ? (double)args[0]->i : args[0]->f;
    return xs_int((int64_t)ceil(v));
}

static Value *vm_round(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_int(0);
    double v = args[0]->tag==XS_INT ? (double)args[0]->i : args[0]->f;
    return xs_int((int64_t)round(v));
}

static Value *vm_assert(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || !value_truthy(args[0])) {
        const char *msg = (argc >= 2 && args[1]->tag == XS_STR) ? args[1]->s : "assertion failed";
        fprintf(stderr, "xs: %s\n", msg);
        exit(1);
    }
    return xs_null();
}

static Value *vm_panic(Interp *interp, Value **args, int argc) {
    (void)interp;
    const char *msg = (argc >= 1 && args[0]->tag == XS_STR) ? args[0]->s : "panic";
    fprintf(stderr, "xs: panic: %s\n", msg);
    exit(1);
}

static Value *vm_exit_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    int code = (argc >= 1 && args[0]->tag == XS_INT) ? (int)args[0]->i : 0;
    exit(code);
}

static Value *vm_input(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc >= 1) { char *s = value_str(args[0]); printf("%s", s); free(s); fflush(stdout); }
    char buf[4096];
    if (fgets(buf, sizeof buf, stdin)) {
        size_t n = strlen(buf);
        if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';
        return xs_str(buf);
    }
    return xs_str("");
}

static Value *vm_bool_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_bool(0);
    return xs_bool(value_truthy(args[0]));
}

static Value *vm_repr(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_str("null");
    char *s = value_repr(args[0]);
    Value *r = xs_str(s); free(s); return r;
}

static Value *vm_typeof(Interp *interp, Value **args, int argc) {
    return vm_type(interp, args, argc);
}

static Value *vm_contains(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2) return xs_bool(0);
    Value *col = args[0], *item = args[1];
    if (col->tag == XS_ARRAY || col->tag == XS_TUPLE) {
        for (int j = 0; j < col->arr->len; j++)
            if (value_equal(col->arr->items[j], item)) return xs_bool(1);
    } else if (col->tag == XS_STR && item->tag == XS_STR) {
        return xs_bool(strstr(col->s, item->s) != NULL);
    } else if (col->tag == XS_MAP && item->tag == XS_STR) {
        return xs_bool(map_get(col->map, item->s) != NULL);
    }
    return xs_bool(0);
}

static Value *vm_sorted(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || (args[0]->tag != XS_ARRAY && args[0]->tag != XS_TUPLE))
        return xs_array_new();
    Value *arr = xs_array_new();
    for (int j = 0; j < args[0]->arr->len; j++)
        array_push(arr->arr, value_incref(args[0]->arr->items[j]));
    for (int j = 0; j < arr->arr->len-1; j++)
        for (int k = 0; k < arr->arr->len-1-j; k++)
            if (value_cmp(arr->arr->items[k], arr->arr->items[k+1]) > 0) {
                Value *tmp2 = arr->arr->items[k];
                arr->arr->items[k] = arr->arr->items[k+1];
                arr->arr->items[k+1] = tmp2;
            }
    return arr;
}

static Value *vm_reversed(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || (args[0]->tag != XS_ARRAY && args[0]->tag != XS_TUPLE))
        return xs_array_new();
    Value *arr = xs_array_new();
    for (int j = args[0]->arr->len - 1; j >= 0; j--)
        array_push(arr->arr, value_incref(args[0]->arr->items[j]));
    return arr;
}

static Value *vm_enumerate(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || (args[0]->tag != XS_ARRAY && args[0]->tag != XS_TUPLE))
        return xs_array_new();
    Value *arr = xs_array_new();
    for (int j = 0; j < args[0]->arr->len; j++) {
        Value *pair = xs_tuple_new();
        array_push(pair->arr, xs_int(j));
        array_push(pair->arr, value_incref(args[0]->arr->items[j]));
        array_push(arr->arr, pair);
    }
    return arr;
}

static Value *vm_zip(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2) return xs_array_new();
    Value *a = args[0], *b = args[1];
    if ((a->tag != XS_ARRAY && a->tag != XS_TUPLE) ||
        (b->tag != XS_ARRAY && b->tag != XS_TUPLE)) return xs_array_new();
    Value *arr = xs_array_new();
    int n = a->arr->len < b->arr->len ? a->arr->len : b->arr->len;
    for (int j = 0; j < n; j++) {
        Value *pair = xs_tuple_new();
        array_push(pair->arr, value_incref(a->arr->items[j]));
        array_push(pair->arr, value_incref(b->arr->items[j]));
        array_push(arr->arr, pair);
    }
    return arr;
}

static Value *vm_sum(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || (args[0]->tag != XS_ARRAY && args[0]->tag != XS_TUPLE))
        return xs_int(0);
    int64_t si = 0; double sf = 0; int is_float = 0;
    for (int j = 0; j < args[0]->arr->len; j++) {
        Value *v = args[0]->arr->items[j];
        if (v->tag == XS_INT) si += v->i;
        else if (v->tag == XS_FLOAT) { sf += v->f; is_float = 1; }
    }
    return is_float ? xs_float(sf + (double)si) : xs_int(si);
}

static Value *vm_map_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2) return xs_array_new();
    Value *fn = args[0], *col = args[1];
    if ((col->tag != XS_ARRAY && col->tag != XS_TUPLE)) return xs_array_new();
    Value *arr = xs_array_new();
    for (int j = 0; j < col->arr->len; j++) {
        Value *elem = col->arr->items[j];
        Value *r = (fn->tag == XS_NATIVE)
            ? fn->native(NULL, &elem, 1)
            : (fn->tag == XS_CLOSURE ? vm_invoke(g_vm_for_invoke, fn, &elem, 1) : value_incref(elem));
        array_push(arr->arr, r ? r : value_incref(XS_NULL_VAL));
    }
    return arr;
}

static Value *vm_filter_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2) return xs_array_new();
    Value *fn = args[0], *col = args[1];
    if ((col->tag != XS_ARRAY && col->tag != XS_TUPLE)) return xs_array_new();
    Value *arr = xs_array_new();
    for (int j = 0; j < col->arr->len; j++) {
        Value *elem = col->arr->items[j];
        Value *r = (fn->tag == XS_NATIVE)
            ? fn->native(NULL, &elem, 1)
            : (fn->tag == XS_CLOSURE ? vm_invoke(g_vm_for_invoke, fn, &elem, 1) : value_incref(XS_FALSE_VAL));
        if (value_truthy(r)) array_push(arr->arr, value_incref(elem));
        value_decref(r);
    }
    return arr;
}

static Value *vm_reduce_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2) return xs_null();
    Value *fn = args[0], *col = args[1];
    if ((col->tag != XS_ARRAY && col->tag != XS_TUPLE) || col->arr->len == 0)
        return argc >= 3 ? value_incref(args[2]) : xs_null();
    Value *acc = argc >= 3 ? value_incref(args[2]) : value_incref(col->arr->items[0]);
    int start = argc >= 3 ? 0 : 1;
    if (fn->tag == XS_NATIVE || fn->tag == XS_CLOSURE) {
        for (int j = start; j < col->arr->len; j++) {
            Value *pair[2] = {acc, col->arr->items[j]};
            Value *r = (fn->tag == XS_NATIVE)
                ? fn->native(NULL, pair, 2)
                : vm_invoke(g_vm_for_invoke, fn, pair, 2);
            value_decref(acc); acc = r ? r : xs_null();
        }
    }
    return acc;
}

static Value *vm_keys(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || (args[0]->tag != XS_MAP && args[0]->tag != XS_MODULE))
        return xs_array_new();
    Value *arr = xs_array_new();
    for (int j = 0; j < args[0]->map->cap; j++)
        if (args[0]->map->keys[j])
            array_push(arr->arr, xs_str(args[0]->map->keys[j]));
    return arr;
}

static Value *vm_values(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || (args[0]->tag != XS_MAP && args[0]->tag != XS_MODULE))
        return xs_array_new();
    Value *arr = xs_array_new();
    for (int j = 0; j < args[0]->map->cap; j++)
        if (args[0]->map->keys[j])
            array_push(arr->arr, value_incref(args[0]->map->vals[j]));
    return arr;
}

static Value *vm_entries(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || (args[0]->tag != XS_MAP && args[0]->tag != XS_MODULE))
        return xs_array_new();
    Value *arr = xs_array_new();
    for (int j = 0; j < args[0]->map->cap; j++)
        if (args[0]->map->keys[j]) {
            Value *pair = xs_tuple_new();
            array_push(pair->arr, xs_str(args[0]->map->keys[j]));
            array_push(pair->arr, value_incref(args[0]->map->vals[j]));
            array_push(arr->arr, pair);
        }
    return arr;
}

static Value *vm_chars(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || args[0]->tag != XS_STR) return xs_array_new();
    Value *arr = xs_array_new();
    const char *s = args[0]->s;
    for (int j = 0; s[j]; j++) {
        char buf[2] = {s[j], 0};
        array_push(arr->arr, xs_str(buf));
    }
    return arr;
}

static Value *vm_flatten(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || args[0]->tag != XS_ARRAY) return xs_array_new();
    Value *arr = xs_array_new();
    for (int j = 0; j < args[0]->arr->len; j++) {
        Value *el = args[0]->arr->items[j];
        if (el->tag == XS_ARRAY)
            for (int k = 0; k < el->arr->len; k++)
                array_push(arr->arr, value_incref(el->arr->items[k]));
        else
            array_push(arr->arr, value_incref(el));
    }
    return arr;
}

static Value *vm_is_null(Interp *interp, Value **args, int argc) {
    (void)interp;
    return xs_bool(argc >= 1 && args[0]->tag == XS_NULL);
}

static Value *vm_copy(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_null();
    return value_copy(args[0]);
}

static Value *vm_Ok(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *r = xs_map_new();
    Value *tag = xs_str("Ok"); map_set(r->map, "_tag", tag); value_decref(tag);
    Value *val = argc >= 1 ? value_incref(args[0]) : xs_null();
    map_set(r->map, "_val", val); value_decref(val);
    return r;
}

static Value *vm_Err(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *r = xs_map_new();
    Value *tag = xs_str("Err"); map_set(r->map, "_tag", tag); value_decref(tag);
    Value *val = argc >= 1 ? value_incref(args[0]) : xs_null();
    map_set(r->map, "_val", val); value_decref(val);
    return r;
}

static Value *vm_Some(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *r = xs_map_new();
    Value *tag = xs_str("Some"); map_set(r->map, "_tag", tag); value_decref(tag);
    Value *val = argc >= 1 ? value_incref(args[0]) : xs_null();
    map_set(r->map, "_val", val); value_decref(val);
    return r;
}

static Value *vm_None_fn(Interp *interp, Value **args, int argc) {
    (void)interp; (void)args; (void)argc;
    Value *r = xs_map_new();
    Value *tag = xs_str("None"); map_set(r->map, "_tag", tag); value_decref(tag);
    return r;
}

static Value *vm_format(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_str("");
    if (args[0]->tag != XS_STR) { char *s = value_str(args[0]); Value *r = xs_str(s); free(s); return r; }
    const char *fmt = args[0]->s;
    int argidx = 1;
    char *result = xs_strdup(""); int rlen = 0;
    for (const char *p = fmt; *p; ) {
        if (*p == '{' && *(p+1) == '}') {
            char *s = (argidx < argc) ? value_str(args[argidx++]) : xs_strdup("{}");
            int slen = (int)strlen(s);
            result = xs_realloc(result, rlen + slen + 1);
            memcpy(result + rlen, s, slen + 1); rlen += slen; free(s);
            p += 2;
        } else {
            result = xs_realloc(result, rlen + 2);
            result[rlen++] = *p++; result[rlen] = '\0';
        }
    }
    Value *v = xs_str(result); free(result); return v;
}

static Value *vm_todo(Interp *interp, Value **args, int argc) {
    (void)interp;
    const char *msg = (argc >= 1 && args[0]->tag == XS_STR) ? args[0]->s : "not yet implemented";
    fprintf(stderr, "xs: TODO: %s\n", msg);
    exit(1);
}

static Value *vm_unreachable(Interp *interp, Value **args, int argc) {
    (void)interp; (void)args; (void)argc;
    fprintf(stderr, "xs: Reached unreachable code\n");
    exit(1);
}

static Value *vm_vec(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *arr = xs_array_new();
    for (int j = 0; j < argc; j++) {
        array_push(arr->arr, value_incref(args[j]));
    }
    return arr;
}

static Value *vm_eprint(Interp *interp, Value **args, int argc) {
    (void)interp;
    for (int j = 0; j < argc; j++) {
        if (j) fprintf(stderr, " ");
        char *s = value_str(args[j]);
        fprintf(stderr, "%s", s); free(s);
    }
    return xs_null();
}

static Value *vm_eprintln(Interp *interp, Value **args, int argc) {
    (void)interp;
    for (int j = 0; j < argc; j++) {
        if (j) fprintf(stderr, " ");
        char *s = value_str(args[j]);
        fprintf(stderr, "%s", s); free(s);
    }
    fprintf(stderr, "\n");
    return xs_null();
}

static Value *vm_sin(Interp *i, Value **a, int n) { (void)i; return n>=1 ? xs_float(sin(a[0]->tag==XS_INT?(double)a[0]->i:a[0]->f)) : xs_float(0); }
static Value *vm_cos(Interp *i, Value **a, int n) { (void)i; return n>=1 ? xs_float(cos(a[0]->tag==XS_INT?(double)a[0]->i:a[0]->f)) : xs_float(0); }
static Value *vm_tan(Interp *i, Value **a, int n) { (void)i; return n>=1 ? xs_float(tan(a[0]->tag==XS_INT?(double)a[0]->i:a[0]->f)) : xs_float(0); }
static Value *vm_log_fn(Interp *i, Value **a, int n) { (void)i; return n>=1 ? xs_float(log(a[0]->tag==XS_INT?(double)a[0]->i:a[0]->f)) : xs_float(0); }

static void vm_register_stdlib(VM *vm) {
    Value *v;
#define REG(name, fn) v = xs_native(fn); map_set(vm->globals, name, v); value_decref(v)
    REG("println", vm_println);
    REG("print",   vm_print);
    REG("len",     vm_len);
    REG("str",     vm_str);
    REG("int",     vm_int_fn);
    REG("float",   vm_float_fn);
    REG("type",    vm_type);
    REG("type_of", vm_typeof);
    REG("typeof",  vm_typeof);
    REG("bool",    vm_bool_fn);
    REG("repr",    vm_repr);
    REG("dbg",     vm_repr);
    REG("range",   vm_range);
    REG("abs",     vm_abs);
    REG("min",     vm_min);
    REG("max",     vm_max);
    REG("sqrt",    vm_sqrt);
    REG("pow",     vm_pow);
    REG("floor",   vm_floor);
    REG("ceil",    vm_ceil);
    REG("round",   vm_round);
    REG("sin",     vm_sin);
    REG("cos",     vm_cos);
    REG("tan",     vm_tan);
    REG("log",     vm_log_fn);
    REG("assert",  vm_assert);
    REG("panic",   vm_panic);
    REG("exit",    vm_exit_fn);
    REG("input",   vm_input);
    REG("contains", vm_contains);
    REG("sorted",  vm_sorted);
    REG("reversed", vm_reversed);
    REG("enumerate", vm_enumerate);
    REG("zip",     vm_zip);
    REG("sum",     vm_sum);
    REG("map",     vm_map_fn);
    REG("filter",  vm_filter_fn);
    REG("reduce",  vm_reduce_fn);
    REG("keys",    vm_keys);
    REG("values",  vm_values);
    REG("entries", vm_entries);
    REG("chars",   vm_chars);
    REG("flatten", vm_flatten);
    REG("is_null", vm_is_null);
    REG("copy",    vm_copy);
    REG("clone",   vm_copy);
    REG("Ok",      vm_Ok);
    REG("Err",     vm_Err);
    REG("Some",    vm_Some);
    REG("None",    vm_None_fn);
    REG("format",  vm_format);
    REG("sprintf", vm_format);
    REG("todo",    vm_todo);
    REG("unreachable", vm_unreachable);
    REG("vec",     vm_vec);
    REG("eprint",  vm_eprint);
    REG("eprintln", vm_eprintln);
#undef REG
    {
        extern Value *make_math_module(void);
        extern Value *make_time_module(void);
        extern Value *make_string_module(void);
        extern Value *make_path_module(void);
        extern Value *make_base64_module(void);
        extern Value *make_hash_module(void);
        extern Value *make_uuid_module(void);
        extern Value *make_collections_module(void);
        extern Value *make_random_module(void);
        extern Value *make_json_module(void);
        extern Value *make_log_module(void);
        extern Value *make_fmt_module(void);
        extern Value *make_csv_module(void);
        extern Value *make_url_module(void);
        extern Value *make_re_module(void);
        extern Value *make_process_module(void);
        extern Value *make_io_module(void);
        extern Value *make_async_module(void);
        extern Value *make_net_module(void);
        extern Value *make_crypto_module(void);
        extern Value *make_thread_module(void);
        extern Value *make_buf_module(void);
        extern Value *make_encode_module(void);
        extern Value *make_db_module(void);
        extern Value *make_cli_module(void);
        extern Value *make_ffi_module(void);
        extern Value *make_reflect_module(void);
        extern Value *make_gc_module(void);
        extern Value *make_reactive_module(void);
        extern Value *make_os_module(Interp *ig);
        extern Value *make_test_module(void);
#define REG_MOD(name, fn) do { Value *_m = fn(); map_set(vm->globals, name, _m); value_decref(_m); } while(0)
        REG_MOD("math",        make_math_module);
        REG_MOD("time",        make_time_module);
        REG_MOD("string",      make_string_module);
        REG_MOD("path",        make_path_module);
        REG_MOD("base64",      make_base64_module);
        REG_MOD("hash",        make_hash_module);
        REG_MOD("uuid",        make_uuid_module);
        REG_MOD("collections", make_collections_module);
        REG_MOD("random",      make_random_module);
        REG_MOD("json",        make_json_module);
        REG_MOD("log",         make_log_module);
        REG_MOD("fmt",         make_fmt_module);
        REG_MOD("csv",         make_csv_module);
        REG_MOD("url",         make_url_module);
        REG_MOD("re",          make_re_module);
        REG_MOD("process",     make_process_module);
        { Value *_m = make_io_module();
          extern Value *native_io_read_json(Interp*,Value**,int);
          extern Value *native_io_write_json(Interp*,Value**,int);
          map_set(_m->map,"read_json",  xs_native(native_io_read_json));
          map_set(_m->map,"write_json", xs_native(native_io_write_json));
          map_set(vm->globals, "io", _m); value_decref(_m); }
        REG_MOD("async",       make_async_module);
        REG_MOD("net",         make_net_module);
        REG_MOD("crypto",      make_crypto_module);
        REG_MOD("thread",      make_thread_module);
        REG_MOD("buf",         make_buf_module);
        REG_MOD("encode",      make_encode_module);
        REG_MOD("db",          make_db_module);
        REG_MOD("cli",         make_cli_module);
        REG_MOD("ffi",         make_ffi_module);
        REG_MOD("reflect",     make_reflect_module);
        REG_MOD("gc",          make_gc_module);
        REG_MOD("reactive",    make_reactive_module);
        REG_MOD("test",        make_test_module);
#undef REG_MOD
        { Value *_m = make_os_module(NULL); map_set(vm->globals, "os", _m); value_decref(_m); }
        { Value *cv = xs_float(3.14159265358979323846); map_set(vm->globals, "PI", cv); value_decref(cv); }
        { Value *cv = xs_float(2.71828182845904523536); map_set(vm->globals, "E",  cv); value_decref(cv); }
    }
}

VM *vm_new(void) {
    value_init_singletons();
    VM *vm = xs_malloc(sizeof *vm);
    memset(vm, 0, sizeof *vm);
    vm->sp      = vm->stack;
    vm->globals = map_new();
    vm->n_tasks = 0;
    vm_register_stdlib(vm);
    return vm;
}

void vm_free(VM *vm) {
    if (!vm) return;
    upvalue_close_all(&vm->open_upvalues, vm->stack);
    while (vm->sp > vm->stack) value_decref(POP());
    for (int i = 0; i < vm->frame_count; i++)
        if (vm->frames[i].closure_val)
            value_decref(vm->frames[i].closure_val);
    Upvalue *u = vm->open_upvalues;
    while (u) {
        Upvalue *nxt = u->next;
        if (u->refcount <= 0) free(u);
        u = nxt;
    }
    for (int i = 0; i < vm->n_tasks; i++) {
        if (vm->tasks[i].fn)     value_decref(vm->tasks[i].fn);
        if (vm->tasks[i].result) value_decref(vm->tasks[i].result);
    }
    vm->n_tasks = 0;
    map_free(vm->globals);
    free(vm);
}

static int call_frame_push(VM *vm, Value *closure_val, int argc) {
    if (vm->frame_count >= VM_FRAMES_MAX) {
        fprintf(stderr, "stack overflow\n"); return 1;
    }
    XSClosure *cl = closure_val->cl;
    int arity = cl->proto->arity;
    int is_gen = 0;
    if (arity < 0) {
        is_gen = 1;
        arity = -(arity + 1);
    }
    if (argc != arity) {
        fprintf(stderr, "arity: expected %d got %d\n", arity, argc);
        return 1;
    }
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->closure_val = value_incref(closure_val);
    frame->ip          = cl->proto->chunk.code;
    frame->base        = vm->sp - argc;
    frame->try_depth   = 0;
    frame->defer_depth = 0;
    frame->defer_return_ip = NULL;
    frame->is_generator = is_gen;
    frame->yield_arr    = is_gen ? xs_array_new() : NULL;
    frame->yield_index  = 0;
    for (int i = 0; i < cl->proto->nlocals - arity; i++) PUSH(xs_null());
    return 0;
}

static Value *vm_invoke(VM *vm, Value *fn, Value **args, int argc) {
    if (fn->tag == XS_NATIVE) {
        return fn->native(NULL, args, argc);
    }
    if (fn->tag != XS_CLOSURE) return value_incref(XS_NULL_VAL);

    for (int i = 0; i < argc; i++) PUSH(value_incref(args[i]));

    int saved_fc = vm->frame_count;
    value_incref(fn);
    if (call_frame_push(vm, fn, argc)) {
        value_decref(fn);
        for (int i = 0; i < argc; i++) value_decref(POP());
        return value_incref(XS_NULL_VAL);
    }
    value_decref(fn);

    if (vm_dispatch(vm, saved_fc) != 0) return value_incref(XS_NULL_VAL);
    return POP();
}

static Value *vm_try_dunder(VM *vm, Value *obj, const char *dunder, Value *other) {
    if (obj->tag != XS_MAP) return NULL;
    Value *fn = map_get(obj->map, dunder);
    if (!fn) {
        Value *methods = map_get(obj->map, "__methods");
        if (methods && methods->tag == XS_MAP)
            fn = map_get(methods->map, dunder);
    }
    if (!fn || (fn->tag != XS_CLOSURE && fn->tag != XS_NATIVE)) return NULL;
    Value *args[2] = { obj, other };
    return vm_invoke(vm, fn, args, 2);
}

int vm_run(VM *vm, XSProto *proto) {
    g_vm_for_invoke = vm;
    XSClosure *top_cl    = xs_malloc(sizeof *top_cl);
    top_cl->proto        = proto; proto->refcount++;
    top_cl->upvalues     = NULL;
    top_cl->refcount     = 1;
    Value *top_val       = xs_malloc(sizeof *top_val);
    top_val->tag         = XS_CLOSURE;
    top_val->refcount    = 1;
    top_val->cl          = top_cl;

    CallFrame *frame     = &vm->frames[vm->frame_count++];
    frame->closure_val   = top_val;
    frame->ip            = proto->chunk.code;
    frame->base          = vm->sp;
    frame->try_depth     = 0;
    for (int i = 0; i < proto->nlocals; i++) PUSH(xs_null());

    return vm_dispatch(vm, 0);
}

static int vm_dispatch(VM *vm, int stop_frame) {
    CallFrame *frame = FRAME;
    for (;;) {
        Instruction instr = *frame->ip++;
        Opcode op = INSTR_OPCODE(instr);

        switch (op) {

        case OP_NOP: break;

        case OP_PUSH_CONST:
            PUSH(value_incref(PROTO->chunk.consts[INSTR_Bx(instr)]));
            break;
        case OP_PUSH_NULL:  PUSH(value_incref(XS_NULL_VAL));  break;
        case OP_PUSH_TRUE:  PUSH(value_incref(XS_TRUE_VAL));  break;
        case OP_PUSH_FALSE: PUSH(value_incref(XS_FALSE_VAL)); break;
        case OP_POP:  value_decref(POP()); break;
        case OP_DUP:  { Value *_top = PEEK(0); PUSH(value_incref(_top)); break; }

        case OP_LOAD_LOCAL: {
            Value *v = frame->base[INSTR_Bx(instr)];
            PUSH(value_incref(v ? v : XS_NULL_VAL));
            break;
        }
        case OP_STORE_LOCAL: {
            int slot   = (int)INSTR_Bx(instr);
            Value *old = frame->base[slot];
            frame->base[slot] = POP();
            if (old) value_decref(old);
            break;
        }
        case OP_LOAD_UPVALUE:
            PUSH(value_incref(*CL->upvalues[INSTR_Bx(instr)]->ptr));
            break;
        case OP_STORE_UPVALUE: {
            Upvalue *uv = CL->upvalues[INSTR_Bx(instr)];
            Value *old  = *uv->ptr;
            *uv->ptr    = POP();
            if (old) value_decref(old);
            break;
        }
        case OP_LOAD_GLOBAL: {
            const char *name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *v = map_get(vm->globals, name);
            PUSH(v ? value_incref(v) : value_incref(XS_NULL_VAL));
            break;
        }
        case OP_STORE_GLOBAL: {
            const char *name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *v = POP();
            map_set(vm->globals, name, v);
            value_decref(v);
            break;
        }

        case OP_ADD: {
            Value *b = POP(), *a = POP(); Value *r;
            if (a->tag == XS_INT && b->tag == XS_INT) {
                r = xs_safe_add(a->i, b->i);
            } else if ((a->tag == XS_INT || a->tag == XS_BIGINT) &&
                       (b->tag == XS_INT || b->tag == XS_BIGINT)) {
                r = xs_numeric_add(a, b);
            } else if (a->tag == XS_STR || b->tag == XS_STR) {
                char *as = value_str(a), *bs = value_str(b);
                size_t n = strlen(as) + strlen(bs) + 1;
                char *buf = xs_malloc(n);
                strcpy(buf, as); strcat(buf, bs);
                free(as); free(bs);
                r = xs_str(buf); free(buf);
            } else if (a->tag == XS_MAP && (r = vm_try_dunder(vm, a, "__add__", b)) != NULL) {
            } else {
                double av = a->tag==XS_INT?(double)a->i:(a->tag==XS_BIGINT?bigint_to_double(a->bigint):a->f);
                double bv = b->tag==XS_INT?(double)b->i:(b->tag==XS_BIGINT?bigint_to_double(b->bigint):b->f);
                r = xs_float(av + bv);
            }
            value_decref(a); value_decref(b); PUSH(r); break;
        }
        case OP_SUB: {
            Value *b=POP(), *a=POP(); Value *r;
            if (a->tag==XS_INT && b->tag==XS_INT) {
                r = xs_safe_sub(a->i, b->i);
            } else if ((a->tag==XS_INT||a->tag==XS_BIGINT) && (b->tag==XS_INT||b->tag==XS_BIGINT)) {
                r = xs_numeric_sub(a, b);
            } else if (a->tag == XS_MAP && (r = vm_try_dunder(vm, a, "__sub__", b)) != NULL) {
                /* dunder */
            } else {
                double av = a->tag==XS_INT?(double)a->i:(a->tag==XS_BIGINT?bigint_to_double(a->bigint):a->f);
                double bv = b->tag==XS_INT?(double)b->i:(b->tag==XS_BIGINT?bigint_to_double(b->bigint):b->f);
                r = xs_float(av - bv);
            }
            value_decref(a); value_decref(b); PUSH(r); break;
        }
        case OP_MUL: {
            Value *b=POP(), *a=POP(); Value *r;
            if (a->tag==XS_INT && b->tag==XS_INT) {
                r = xs_safe_mul(a->i, b->i);
            } else if ((a->tag==XS_INT||a->tag==XS_BIGINT) && (b->tag==XS_INT||b->tag==XS_BIGINT)) {
                r = xs_numeric_mul(a, b);
            } else if (a->tag == XS_MAP && (r = vm_try_dunder(vm, a, "__mul__", b)) != NULL) {
                /* dunder */
            } else if (a->tag==XS_STR && b->tag==XS_INT) {
                int64_t count = b->i;
                if (count <= 0) { r = xs_str(""); }
                else {
                    size_t slen = strlen(a->s);
                    char *buf = xs_malloc(slen * (size_t)count + 1);
                    buf[0] = '\0';
                    for (int64_t ci = 0; ci < count; ci++) memcpy(buf + slen * (size_t)ci, a->s, slen);
                    buf[slen * (size_t)count] = '\0';
                    r = xs_str(buf); free(buf);
                }
            } else {
                double av = a->tag==XS_INT?(double)a->i:(a->tag==XS_BIGINT?bigint_to_double(a->bigint):a->f);
                double bv = b->tag==XS_INT?(double)b->i:(b->tag==XS_BIGINT?bigint_to_double(b->bigint):b->f);
                r = xs_float(av * bv);
            }
            value_decref(a); value_decref(b); PUSH(r); break;
        }
        case OP_DIV: {
            Value *b=POP(), *a=POP(); Value *r;
            if (a->tag==XS_INT && b->tag==XS_INT) {
                if (b->i == 0) { fprintf(stderr, "division by zero\n"); value_decref(a); value_decref(b); return 1; }
                r = xs_int(a->i / b->i);
            } else if ((a->tag==XS_INT||a->tag==XS_BIGINT) && (b->tag==XS_INT||b->tag==XS_BIGINT)) {
                r = xs_numeric_div(a, b);
            } else if (a->tag == XS_MAP && (r = vm_try_dunder(vm, a, "__div__", b)) != NULL) {
                /* dunder */
            } else {
                double bv = b->tag==XS_INT?(double)b->i:(b->tag==XS_BIGINT?bigint_to_double(b->bigint):b->f);
                if (bv == 0.0) { fprintf(stderr, "division by zero\n"); value_decref(a); value_decref(b); return 1; }
                double av = a->tag==XS_INT?(double)a->i:(a->tag==XS_BIGINT?bigint_to_double(a->bigint):a->f);
                r = xs_float(av / bv);
            }
            value_decref(a); value_decref(b); PUSH(r); break;
        }
        case OP_MOD: {
            Value *b=POP(), *a=POP();
            Value *r;
            if (a->tag==XS_INT && b->tag==XS_INT) {
                r = xs_int(a->i % b->i);
            } else if (a->tag == XS_MAP && (r = vm_try_dunder(vm, a, "__mod__", b)) != NULL) {
                /* dunder */
            } else {
                r = xs_numeric_mod(a, b);
            }
            value_decref(a); value_decref(b); PUSH(r); break;
        }
        case OP_POW: {
            Value *b=POP(), *a=POP();
            if (a->tag==XS_INT && b->tag==XS_INT && b->i >= 0) {
                Value *r = xs_safe_pow(a->i, b->i);
                value_decref(a); value_decref(b); PUSH(r);
            } else if ((a->tag==XS_INT||a->tag==XS_BIGINT) && (b->tag==XS_INT||b->tag==XS_BIGINT)) {
                Value *r = xs_numeric_pow(a, b);
                value_decref(a); value_decref(b); PUSH(r);
            } else {
                double av = a->tag==XS_INT?(double)a->i:(a->tag==XS_BIGINT?bigint_to_double(a->bigint):a->f);
                double bv = b->tag==XS_INT?(double)b->i:(b->tag==XS_BIGINT?bigint_to_double(b->bigint):b->f);
                value_decref(a); value_decref(b); PUSH(xs_float(pow(av,bv)));
            }
            break;
        }
        case OP_NEG: {
            Value *a = POP();
            if (a->tag == XS_INT) {
                PUSH(xs_safe_neg(a->i));
            } else if (a->tag == XS_BIGINT) {
                PUSH(xs_numeric_neg(a));
            } else {
                PUSH(xs_float(-a->f));
            }
            value_decref(a); break;
        }
        case OP_NOT: {
            Value *a = POP();
            PUSH(xs_bool(!value_truthy(a)));
            value_decref(a); break;
        }
        case OP_CONCAT: {
            Value *b=POP(), *a=POP();
            char *as=value_str(a), *bs=value_str(b);
            size_t n=strlen(as)+strlen(bs)+1;
            char *buf=xs_malloc(n); strcpy(buf,as); strcat(buf,bs);
            free(as); free(bs); value_decref(a); value_decref(b);
            Value *r=xs_str(buf); free(buf); PUSH(r); break;
        }

        // --- comparisons
        case OP_EQ:  { Value *b=POP(),*a=POP(); Value *r;
                       if (a->tag==XS_MAP && (r=vm_try_dunder(vm,a,"__eq__",b))!=NULL) { value_decref(a);value_decref(b);PUSH(r); }
                       else { PUSH(xs_bool(value_equal(a,b))); value_decref(a);value_decref(b); } break; }
        case OP_NEQ: { Value *b=POP(),*a=POP(); Value *r;
                       if (a->tag==XS_MAP && (r=vm_try_dunder(vm,a,"__ne__",b))!=NULL) { value_decref(a);value_decref(b);PUSH(r); }
                       else { PUSH(xs_bool(!value_equal(a,b))); value_decref(a);value_decref(b); } break; }
        case OP_LT:  { Value *b=POP(),*a=POP(); Value *r;
                       if (a->tag==XS_MAP && (r=vm_try_dunder(vm,a,"__lt__",b))!=NULL) { value_decref(a);value_decref(b);PUSH(r); }
                       else { PUSH(xs_bool(value_cmp(a,b)<0));  value_decref(a);value_decref(b); } break; }
        case OP_GT:  { Value *b=POP(),*a=POP(); Value *r;
                       if (a->tag==XS_MAP && (r=vm_try_dunder(vm,a,"__gt__",b))!=NULL) { value_decref(a);value_decref(b);PUSH(r); }
                       else { PUSH(xs_bool(value_cmp(a,b)>0));  value_decref(a);value_decref(b); } break; }
        case OP_LTE: { Value *b=POP(),*a=POP(); Value *r;
                       if (a->tag==XS_MAP && (r=vm_try_dunder(vm,a,"__le__",b))!=NULL) { value_decref(a);value_decref(b);PUSH(r); }
                       else { PUSH(xs_bool(value_cmp(a,b)<=0)); value_decref(a);value_decref(b); } break; }
        case OP_GTE: { Value *b=POP(),*a=POP(); Value *r;
                       if (a->tag==XS_MAP && (r=vm_try_dunder(vm,a,"__ge__",b))!=NULL) { value_decref(a);value_decref(b);PUSH(r); }
                       else { PUSH(xs_bool(value_cmp(a,b)>=0)); value_decref(a);value_decref(b); } break; }

        /* collections */
        case OP_MAKE_ARRAY: {
            int n = (int)INSTR_C(instr);
            Value *arr = xs_array_new();
            Value *tmp[256];
            for (int i = n-1; i >= 0; i--) tmp[i] = POP();
            for (int i = 0; i < n; i++) {
                array_push(arr->arr, tmp[i]);
                value_decref(tmp[i]);
            }
            PUSH(arr); break;
        }
        case OP_MAKE_TUPLE: {
            int n = (int)INSTR_C(instr);
            Value *tup = xs_tuple_new();
            Value *tmp[256];
            for (int i = n-1; i >= 0; i--) tmp[i] = POP();
            for (int i = 0; i < n; i++) {
                array_push(tup->arr, tmp[i]);
                value_decref(tmp[i]);
            }
            PUSH(tup); break;
        }
        case OP_INDEX_GET: {
            Value *idx = POP(), *col = POP(); Value *r;
            if ((col->tag==XS_ARRAY||col->tag==XS_TUPLE) && idx->tag==XS_INT) {
                int64_t i = idx->i;
                if (i < 0) i += col->arr->len;
                r = (i>=0 && i<col->arr->len) ? value_incref(col->arr->items[i]) : value_incref(XS_NULL_VAL);
            } else if (col->tag==XS_MAP && idx->tag==XS_STR) {
                Value *v = map_get(col->map, idx->s);
                r = v ? value_incref(v) : value_incref(XS_NULL_VAL);
            } else if (col->tag==XS_RANGE && idx->tag==XS_INT && col->range) {
                r = xs_int(col->range->start + idx->i);
            } else if (col->tag==XS_STR && idx->tag==XS_INT) {
                const char *s = col->s;
                int64_t slen = (int64_t)strlen(s);
                int64_t i = idx->i;
                if (i < 0) i += slen;
                if (i >= 0 && i < slen) { char buf[2] = {s[i], 0}; r = xs_str(buf); }
                else r = value_incref(XS_NULL_VAL);
            } else {
                r = value_incref(XS_NULL_VAL);
            }
            value_decref(idx); value_decref(col); PUSH(r); break;
        }
        case OP_INDEX_SET: {
            Value *val = POP(), *idx = POP(), *col = POP();
            if ((col->tag==XS_ARRAY||col->tag==XS_TUPLE) && idx->tag==XS_INT) {
                int64_t i = idx->i;
                if (i>=0 && i<col->arr->len) {
                    value_decref(col->arr->items[i]);
                    col->arr->items[i] = value_incref(val);
                }
            } else if (col->tag==XS_MAP && idx->tag==XS_STR) {
                map_set(col->map, idx->s, val);
            }
            value_decref(val); value_decref(idx); value_decref(col); break;
        }
        case OP_LOAD_FIELD: {
            const char *name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *obj = POP(); Value *r = NULL;
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
            } else if (obj->tag == XS_ENUM_VAL && obj->en) {
                if (strcmp(name, "variant") == 0 || strcmp(name, "_tag") == 0)
                    r = xs_str(obj->en->variant);
                else if (strcmp(name, "type") == 0 || strcmp(name, "__type") == 0)
                    r = xs_str(obj->en->type_name);
            }
            if (!r) r = value_incref(XS_NULL_VAL);
            value_decref(obj); PUSH(r); break;
        }
        case OP_STORE_FIELD: {
            const char *name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *val = POP(), *obj = POP();
            if (obj->tag == XS_MAP || obj->tag == XS_MODULE) map_set(obj->map, name, val);
            value_decref(val); value_decref(obj); break;
        }
        case OP_MAKE_MAP: {
            int n = (int)INSTR_C(instr); /* n key-value pairs */
            Value *m = xs_map_new();
            Value *tmp[512];
            for (int i = n*2-1; i >= 0; i--) tmp[i] = POP();
            for (int i = 0; i < n; i++) {
                Value *k = tmp[i*2], *v = tmp[i*2+1];
                if (k->tag == XS_STR) map_set(m->map, k->s, v);
                value_decref(k); value_decref(v);
            }
            PUSH(m); break;
        }

        case OP_JUMP:
            frame->ip += INSTR_sBx(instr); break;
        case OP_JUMP_IF_FALSE: {
            Value *cond = POP();
            if (!value_truthy(cond)) frame->ip += INSTR_sBx(instr);
            value_decref(cond); break;
        }
        case OP_JUMP_IF_TRUE: {
            Value *cond = POP();
            if (value_truthy(cond)) frame->ip += INSTR_sBx(instr);
            value_decref(cond); break;
        }

        case OP_MAKE_CLOSURE: {
            int inner_idx = (int)PROTO->chunk.consts[INSTR_Bx(instr)]->i;
            XSProto *inner = PROTO->inner[inner_idx];
            int nuv = inner->n_upvalues;
            Upvalue **uvs = nuv ? xs_malloc((size_t)nuv * sizeof(Upvalue*)) : NULL;
            for (int i = 0; i < nuv; i++) {
                UVDesc *d = &inner->uv_descs[i];
                uvs[i] = d->is_local
                    ? capture_upvalue(vm, &frame->base[d->index])
                    : CL->upvalues[d->index];
                uvs[i]->refcount++;
            }
            XSClosure *cl = xs_malloc(sizeof *cl);
            cl->proto    = inner; inner->refcount++;
            cl->upvalues = uvs;  cl->refcount = 1;
            Value *v     = xs_malloc(sizeof *v);
            v->tag       = XS_CLOSURE; v->refcount = 1; v->cl = cl;
            PUSH(v); break;
        }

        case OP_CALL: {
            int argc   = (int)INSTR_C(instr);
            Value *callee = vm->sp[-argc - 1];
            if (callee->tag == XS_NATIVE) {
                Value **args = vm->sp - argc;
                Value *result = callee->native(NULL, args, argc);
                for (int i = 0; i < argc; i++) value_decref(POP());
                value_decref(POP()); /* callee */
                PUSH(result ? result : value_incref(XS_NULL_VAL));
            } else if (callee->tag == XS_CLOSURE) {
                Value *saved = callee;
                value_incref(saved);
                for (int i = -argc-1; i < -1; i++) vm->sp[i] = vm->sp[i+1];
                vm->sp--;
                value_decref(saved);
                if (call_frame_push(vm, saved, argc)) {
                    value_decref(saved);
                    return 1;
                }
                value_decref(saved);
                frame = FRAME;
            } else if (callee->tag == XS_MAP || callee->tag == XS_MODULE) {
                Value *fields = map_get(callee->map, "__fields");
                if (fields && fields->tag == XS_MAP) {
                    Value *inst = xs_map_new();
                    for (int j = 0; j < fields->map->cap; j++)
                        if (fields->map->keys[j])
                            map_set(inst->map, fields->map->keys[j],
                                    value_incref(fields->map->vals[j]));
                    Value *methods = map_get(callee->map, "__methods");
                    if (methods && methods->tag == XS_MAP)
                        for (int j = 0; j < methods->map->cap; j++)
                            if (methods->map->keys[j])
                                map_set(inst->map, methods->map->keys[j],
                                        value_incref(methods->map->vals[j]));
                    Value *cls_name = map_get(callee->map, "__name");
                    if (cls_name) map_set(inst->map, "__type", value_incref(cls_name));
                    Value *bases = map_get(callee->map, "__bases");
                    if (bases) map_set(inst->map, "__bases", value_incref(bases));
                    if (argc > 0 && fields->map->len > 0) {
                        int fi = 0;
                        for (int j = 0; j < fields->map->cap && fi < argc; j++)
                            if (fields->map->keys[j])
                                map_set(inst->map, fields->map->keys[j],
                                        value_incref(vm->sp[-argc + fi++]));
                    }
                    Value *init_fn = map_get(inst->map, "init");
                    Value *ctor_args[256];
                    for (int j = 0; j < argc && j < 256; j++)
                        ctor_args[j] = value_incref(vm->sp[-argc + j]);
                    for (int j = 0; j < argc; j++) value_decref(POP());
                    value_decref(POP()); /* callee */
                    if (init_fn && init_fn->tag == XS_NATIVE && argc > 0) {
                        Value *init_call_args[257];
                        init_call_args[0] = inst;
                        for (int j = 0; j < argc; j++)
                            init_call_args[j + 1] = ctor_args[j];
                        Value *ir = init_fn->native(NULL, init_call_args, argc + 1);
                        if (ir) value_decref(ir);
                        for (int j = 0; j < argc; j++) value_decref(ctor_args[j]);
                        PUSH(inst);
                    } else if (init_fn && init_fn->tag == XS_CLOSURE && argc > 0) {
                        PUSH(inst);
                        PUSH(value_incref(inst));
                        for (int j = 0; j < argc; j++) PUSH(ctor_args[j]);
                        value_incref(init_fn);
                        if (call_frame_push(vm, init_fn, argc + 1) == 0) {
                            value_decref(init_fn);
                            frame = FRAME;
                            vm->init_inst = value_incref(inst);
                            break;
                        } else {
                            value_decref(init_fn);
                        }
                    } else {
                        for (int j = 0; j < argc; j++) value_decref(ctor_args[j]);
                        PUSH(inst);
                    }
                } else {
                    for (int j = 0; j < argc; j++) value_decref(POP());
                    value_decref(POP());
                    PUSH(value_incref(XS_NULL_VAL));
                }
            } else {
                fprintf(stderr, "call on non-callable (tag=%d)\n", callee->tag);
                return 1;
            }
            break;
        }

        case OP_TAIL_CALL: {
            int argc   = (int)INSTR_C(instr);
            Value *callee = vm->sp[-argc - 1];
            if (callee->tag == XS_NATIVE) {
                Value **args = vm->sp - argc;
                Value *result = callee->native(NULL, args, argc);
                for (int i = 0; i < argc; i++) value_decref(POP());
                value_decref(POP());
                upvalue_close_all(&vm->open_upvalues, frame->base);
                while (vm->sp > frame->base) value_decref(POP());
                value_decref(frame->closure_val);
                vm->frame_count--;
                if (vm->frame_count == 0) { value_decref(result); return 0; }
                frame = FRAME;
                PUSH(result ? result : value_incref(XS_NULL_VAL));
                break;
            }
            if (callee->tag != XS_CLOSURE) {
                fprintf(stderr, "tail call on non-callable\n");
                return 1;
            }
            Value *new_cv = callee;
            value_incref(new_cv);
            XSClosure *new_cl = new_cv->cl;
            if (argc != new_cl->proto->arity) {
                value_decref(new_cv);
                fprintf(stderr, "tail call arity mismatch\n");
                return 1;
            }
            Value *new_args[256];
            for (int i = argc-1; i >= 0; i--) new_args[i] = POP();
            value_decref(POP());
            upvalue_close_all(&vm->open_upvalues, frame->base);
            while (vm->sp > frame->base) value_decref(POP());
            Value *old_cv      = frame->closure_val;
            frame->closure_val = new_cv;
            frame->ip          = new_cl->proto->chunk.code;
            frame->base        = vm->sp;
            value_decref(old_cv);
            for (int i = 0; i < argc; i++) PUSH(new_args[i]);
            for (int i = argc; i < new_cl->proto->nlocals; i++) PUSH(xs_null());
            break;
        }

        case OP_RETURN: {
            if (frame->defer_return_ip) {
                Value *dret = POP();
                value_decref(dret);
                if (frame->defer_depth > 0) {
                    frame->defer_depth--;
                    frame->ip = frame->defer_stack[frame->defer_depth].defer_ip;
                } else {
                    frame->ip = frame->defer_return_ip;
                    frame->defer_return_ip = NULL;
                }
                break;
            }
            Value *result = POP();
            if (frame->is_generator && frame->yield_arr) {
                value_decref(result);
                Value *gen = xs_map_new();
                Value *type_v = xs_str("generator");
                map_set(gen->map, "__type", type_v); value_decref(type_v);
                map_set(gen->map, "_yields", frame->yield_arr);
                value_decref(frame->yield_arr);
                frame->yield_arr = NULL;
                Value *idx_v = xs_int(0);
                map_set(gen->map, "_index", idx_v); value_decref(idx_v);
                Value *done_v = value_incref(XS_FALSE_VAL);
                map_set(gen->map, "_done", done_v); value_decref(done_v);
                result = gen;
            }
            /* run deferred blocks before returning */
            if (frame->defer_depth > 0) {
                PUSH(result);
                frame->defer_return_ip = frame->ip - 1;
                frame->defer_depth--;
                frame->ip = frame->defer_stack[frame->defer_depth].defer_ip;
                break;
            }
            upvalue_close_all(&vm->open_upvalues, frame->base);
            while (vm->sp > frame->base) value_decref(POP());
            value_decref(frame->closure_val);
            vm->frame_count--;
            if (vm->frame_count <= stop_frame) {
                if (stop_frame > 0) {
                    PUSH(result);
                    return 0;
                }
                value_decref(result);
                if (!vm->main_called) {
                    Value *main_fn = map_get(vm->globals, "main");
                    if (main_fn && main_fn->tag == XS_CLOSURE) {
                        vm->main_called = 1;
                        value_incref(main_fn);
                        if (call_frame_push(vm, main_fn, 0) == 0) {
                            value_decref(main_fn);
                            frame = FRAME;
                            break;
                        }
                        value_decref(main_fn);
                    }
                }
                return 0;
            }
            frame = FRAME;
            PUSH(result);
            if (vm->init_inst) {
                Value *init_retval = POP();
                value_decref(init_retval);
                Value *saved_inst = POP();
                value_decref(saved_inst);
                PUSH(vm->init_inst);
                vm->init_inst = NULL;
            }
            if (vm->spawn_task) {
                Value *spawn_retval = POP();
                Value *saved_task = POP();
                { Value *sv = xs_str("done"); map_set(saved_task->map, "_status", sv); value_decref(sv); }
                map_set(saved_task->map, "_result", spawn_retval);
                value_decref(spawn_retval);
                value_decref(vm->spawn_task);
                vm->spawn_task = NULL;
                PUSH(saved_task);
            }
            break;
        }

        case OP_MAKE_RANGE: {
            int inclusive = (int)INSTR_A(instr);
            Value *end_v = POP(), *start_v = POP();
            int64_t s = start_v->tag==XS_INT ? start_v->i : (int64_t)start_v->f;
            int64_t e = end_v->tag==XS_INT   ? end_v->i   : (int64_t)end_v->f;
            Value *r = xs_range(s, e, inclusive);
            value_decref(start_v); value_decref(end_v); PUSH(r); break;
        }

        case OP_ITER_LEN: {
            Value *iter = POP(); Value *r;
            if (iter->tag==XS_ARRAY||iter->tag==XS_TUPLE) r = xs_int(iter->arr->len);
            else if (iter->tag==XS_STR) r = xs_int((int64_t)strlen(iter->s));
            else if (iter->tag==XS_MAP && map_get(iter->map, "__type") &&
                     map_get(iter->map, "__type")->tag == XS_STR &&
                     strcmp(map_get(iter->map, "__type")->s, "generator") == 0) {
                Value *yields = map_get(iter->map, "_yields");
                r = xs_int(yields && yields->tag == XS_ARRAY ? yields->arr->len : 0);
            }
            else if (iter->tag==XS_MAP||iter->tag==XS_MODULE) r = xs_int(iter->map->len);
            else if (iter->tag==XS_RANGE && iter->range) {
                int64_t span = iter->range->end - iter->range->start;
                if (iter->range->inclusive) span += (span >= 0) ? 1 : -1;
                int64_t step = iter->range->step ? iter->range->step : 1;
                int64_t len;
                if (step > 0) len = (span > 0) ? (span + step - 1) / step : 0;
                else           len = (span < 0) ? (-span + (-step) - 1) / (-step) : 0;
                r = xs_int(len);
            } else r = xs_int(0);
            value_decref(iter); PUSH(r); break;
        }
        case OP_ITER_GET: {
            Value *idx = POP(), *iter = POP(); Value *r;
            int64_t i = idx->tag==XS_INT ? idx->i : (int64_t)idx->f;
            if (iter->tag==XS_ARRAY||iter->tag==XS_TUPLE) {
                r = (i>=0&&i<iter->arr->len) ? value_incref(iter->arr->items[i]) : value_incref(XS_NULL_VAL);
            } else if (iter->tag==XS_STR) {
                const char *s = iter->s;
                int64_t slen = (int64_t)strlen(s);
                if (i>=0&&i<slen) { char buf[2]={s[i],0}; r=xs_str(buf); }
                else r = value_incref(XS_NULL_VAL);
            } else if (iter->tag==XS_MAP && iter->map &&
                       map_get(iter->map, "__type") &&
                       map_get(iter->map, "__type")->tag == XS_STR &&
                       strcmp(map_get(iter->map, "__type")->s, "generator") == 0) {
                Value *yields = map_get(iter->map, "_yields");
                if (yields && yields->tag == XS_ARRAY && i >= 0 && i < yields->arr->len)
                    r = value_incref(yields->arr->items[i]);
                else
                    r = value_incref(XS_NULL_VAL);
            } else if ((iter->tag==XS_MAP||iter->tag==XS_MODULE) && iter->map) {
                int64_t ki = 0;
                r = value_incref(XS_NULL_VAL);
                for (int j = 0; j < iter->map->cap; j++) {
                    if (iter->map->keys[j]) {
                        if (ki == i) {
                            r = xs_str(iter->map->keys[j]);
                            break;
                        }
                        ki++;
                    }
                }
            } else if (iter->tag==XS_RANGE && iter->range) {
                int64_t step = iter->range->step ? iter->range->step : 1;
                r = xs_int(iter->range->start + i * step);
            } else r = value_incref(XS_NULL_VAL);
            value_decref(idx); value_decref(iter); PUSH(r); break;
        }

        /* method call */
        case OP_METHOD_CALL: {
            int mc_argc   = (int)INSTR_A(instr);
            const char *mc_name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *mc_obj = vm->sp[-mc_argc - 1];
            Value **mc_args = vm->sp - mc_argc;
            Value *mc_result = NULL;
            int mc_called = 0; /* set to 1 if we pushed a call frame */

            if (mc_obj->tag == XS_MAP || mc_obj->tag == XS_MODULE) {
                Value *_gen_type = map_get(mc_obj->map, "__type");
                if (_gen_type && _gen_type->tag == XS_STR &&
                    strcmp(_gen_type->s, "generator") == 0 &&
                    strcmp(mc_name, "next") == 0) {
                    Value *yields = map_get(mc_obj->map, "_yields");
                    Value *idx_v  = map_get(mc_obj->map, "_index");
                    int idx = idx_v && idx_v->tag == XS_INT ? (int)idx_v->i : 0;
                    mc_result = xs_map_new();
                    if (yields && yields->tag == XS_ARRAY && idx < yields->arr->len) {
                        map_set(mc_result->map, "value", value_incref(yields->arr->items[idx]));
                        Value *dv = value_incref(XS_FALSE_VAL);
                        map_set(mc_result->map, "done", dv); value_decref(dv);
                        Value *new_idx = xs_int(idx + 1);
                        map_set(mc_obj->map, "_index", new_idx); value_decref(new_idx);
                    } else {
                        map_set(mc_result->map, "value", value_incref(XS_NULL_VAL));
                        Value *dv = value_incref(XS_TRUE_VAL);
                        map_set(mc_result->map, "done", dv); value_decref(dv);
                        Value *done_v = value_incref(XS_TRUE_VAL);
                        map_set(mc_obj->map, "_done", done_v); value_decref(done_v);
                    }
                } else if (strcmp(mc_name,"keys")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->map->cap;j++){
                        if(mc_obj->map->keys[j]){
                            Value *k=xs_str(mc_obj->map->keys[j]);
                            array_push(arr->arr,k); value_decref(k);
                        }
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"values")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->map->cap;j++){
                        if(mc_obj->map->keys[j]) array_push(arr->arr,value_incref(mc_obj->map->vals[j]));
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"entries")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->map->cap;j++){
                        if(mc_obj->map->keys[j]){
                            Value *pair=xs_tuple_new();
                            array_push(pair->arr,xs_str(mc_obj->map->keys[j]));
                            array_push(pair->arr,value_incref(mc_obj->map->vals[j]));
                            array_push(arr->arr,pair);
                        }
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"len")==0||strcmp(mc_name,"size")==0) {
                    mc_result=xs_int(mc_obj->map->len);
                } else if (strcmp(mc_name,"has")==0||strcmp(mc_name,"contains_key")==0) {
                    mc_result=(mc_argc>=1&&mc_args[0]->tag==XS_STR&&map_get(mc_obj->map,mc_args[0]->s))
                        ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"get")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    Value *v=map_get(mc_obj->map,mc_args[0]->s);
                    mc_result=v?value_incref(v):(mc_argc>=2?value_incref(mc_args[1]):value_incref(XS_NULL_VAL));
                } else if (strcmp(mc_name,"set")==0&&mc_argc>=2&&mc_args[0]->tag==XS_STR) {
                    map_set(mc_obj->map,mc_args[0]->s,mc_args[1]);
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"delete")==0||strcmp(mc_name,"remove")==0) {
                    if(mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                        Value *nv = value_incref(XS_NULL_VAL);
                        map_set(mc_obj->map,mc_args[0]->s,nv);
                        value_decref(nv);
                    }
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"elapsed")==0) {
                    Value *start_v = map_get(mc_obj->map, "_start");
                    if (start_v && (start_v->tag == XS_FLOAT || start_v->tag == XS_INT)) {
                        struct timespec _ts; clock_gettime(CLOCK_REALTIME, &_ts);
                        double now = (double)_ts.tv_sec + (double)_ts.tv_nsec/1e9;
                        double start = start_v->tag == XS_FLOAT ? start_v->f : (double)start_v->i;
                        mc_result = xs_float(now - start);
                    } else mc_result = value_incref(XS_NULL_VAL);
                } else {
                    Value *fn = map_get(mc_obj->map, mc_name);
                    if (!fn) {
                        Value *methods = map_get(mc_obj->map, "__methods");
                        if (methods && methods->tag == XS_MAP)
                            fn = map_get(methods->map, mc_name);
                    }
                    if (fn && (fn->tag == XS_CLOSURE || fn->tag == XS_NATIVE)) {
                        if (fn->tag == XS_CLOSURE && fn->cl->proto->arity == mc_argc + 1) {
                            /* self is on stack below args */
                            Value *fn_val = value_incref(fn);
                            if (call_frame_push(vm, fn_val, mc_argc + 1)) {
                                value_decref(fn_val); return 1;
                            }
                            value_decref(fn_val); frame = FRAME;
                            mc_called = 1;
                        } else if (fn->tag == XS_NATIVE) {
                            Value *nargs[17];
                            nargs[0] = mc_obj;
                            int total = 1 + mc_argc;
                            if (total > 17) total = 17;
                            for (int j = 0; j < mc_argc && j < 16; j++) nargs[j+1] = mc_args[j];
                            Value *r2 = fn->native(NULL, nargs, total);
                            for (int j = 0; j < mc_argc; j++) value_decref(POP());
                            value_decref(POP());
                            PUSH(r2 ? r2 : value_incref(XS_NULL_VAL));
                        } else {
                            value_decref(mc_obj);
                            vm->sp[-mc_argc - 1] = value_incref(fn);
                            Value *sv = vm->sp[-mc_argc - 1]; value_incref(sv);
                            for (int j = -mc_argc-1; j < -1; j++) vm->sp[j] = vm->sp[j+1];
                            vm->sp--; value_decref(sv);
                            if (call_frame_push(vm, sv, mc_argc)) { value_decref(sv); return 1; }
                            value_decref(sv); frame = FRAME;
                            mc_called = 1;
                        }
                        break;
                    }
                    mc_result = value_incref(XS_NULL_VAL);
                }
            }

            else if (mc_obj->tag == XS_STR) {
                const char *s = mc_obj->s;
                int slen = (int)strlen(s);
                if (strcmp(mc_name,"len")==0||strcmp(mc_name,"size")==0)
                    mc_result = xs_int(slen);
                else if (strcmp(mc_name,"upper")==0||strcmp(mc_name,"to_upper")==0) {
                    char *r=xs_strdup(s); for(int j=0;r[j];j++) r[j]=(char)toupper((unsigned char)r[j]);
                    mc_result=xs_str(r); free(r);
                } else if (strcmp(mc_name,"lower")==0||strcmp(mc_name,"to_lower")==0) {
                    char *r=xs_strdup(s); for(int j=0;r[j];j++) r[j]=(char)tolower((unsigned char)r[j]);
                    mc_result=xs_str(r); free(r);
                } else if (strcmp(mc_name,"trim")==0) {
                    if (slen == 0) { mc_result = xs_str(""); }
                    else {
                    const char *p2=s; while(*p2==' '||*p2=='\t'||*p2=='\n'||*p2=='\r') p2++;
                    const char *e2=s+slen-1;
                    while(e2>p2&&(*e2==' '||*e2=='\t'||*e2=='\n'||*e2=='\r')) e2--;
                    size_t n2=(size_t)(e2-p2+1); char *r=xs_malloc(n2+1);
                    memcpy(r,p2,n2); r[n2]='\0'; mc_result=xs_str(r); free(r);
                    }
                } else if (strcmp(mc_name,"contains")==0||strcmp(mc_name,"includes")==0) {
                    mc_result = (mc_argc>=1&&mc_args[0]->tag==XS_STR&&strstr(s,mc_args[0]->s))
                        ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"starts_with")==0) {
                    if (mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                        size_t pl=strlen(mc_args[0]->s);
                        mc_result=strncmp(s,mc_args[0]->s,pl)==0
                            ? value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                    } else mc_result=value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"ends_with")==0) {
                    if (mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                        size_t pl=strlen(mc_args[0]->s);
                        mc_result=(size_t)slen>=pl&&strcmp(s+slen-(int)pl,mc_args[0]->s)==0
                            ? value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                    } else mc_result=value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"split")==0) {
                    const char *sep=(mc_argc>=1&&mc_args[0]->tag==XS_STR)?mc_args[0]->s:" ";
                    Value *arr=xs_array_new(); size_t seplen=strlen(sep); const char *p3=s;
                    if (seplen==0) {
                        for(int j=0;j<slen;j++){char b[2]={s[j],0};Value*cv=xs_str(b);array_push(arr->arr,cv);value_decref(cv);}
                    } else {
                        const char *fnd;
                        while((fnd=strstr(p3,sep))!=NULL){
                            size_t chunk=(size_t)(fnd-p3); char *b=xs_malloc(chunk+1);
                            memcpy(b,p3,chunk); b[chunk]='\0';
                            Value *cv=xs_str(b); free(b); array_push(arr->arr,cv); value_decref(cv);
                            p3=fnd+seplen;
                        }
                        Value *cv=xs_str(p3); array_push(arr->arr,cv); value_decref(cv);
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"chars")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<slen;j++){char b[2]={s[j],0};Value*cv=xs_str(b);array_push(arr->arr,cv);value_decref(cv);}
                    mc_result=arr;
                } else if (strcmp(mc_name,"replace")==0&&mc_argc>=2&&mc_args[0]->tag==XS_STR&&mc_args[1]->tag==XS_STR) {
                    const char *from=mc_args[0]->s,*to=mc_args[1]->s;
                    size_t fl=strlen(from),tl=strlen(to);
                    size_t cap=strlen(s)*2+64; char *buf=xs_malloc(cap); size_t wpos=0;
                    const char *p3=s; const char *fnd2;
                    while(fl&&(fnd2=strstr(p3,from))!=NULL){
                        size_t prefix=(size_t)(fnd2-p3);
                        while(wpos+prefix+tl+1>cap){cap*=2;buf=xs_realloc(buf,cap);}
                        memcpy(buf+wpos,p3,prefix); wpos+=prefix;
                        memcpy(buf+wpos,to,tl); wpos+=tl;
                        p3=fnd2+fl;
                    }
                    size_t rest=strlen(p3);
                    while(wpos+rest+1>cap){cap*=2;buf=xs_realloc(buf,cap);}
                    memcpy(buf+wpos,p3,rest); buf[wpos+rest]='\0';
                    mc_result=xs_str(buf); free(buf);
                } else if (strcmp(mc_name,"parse_int")==0||strcmp(mc_name,"to_int")==0)
                    mc_result=xs_int(atoll(s));
                else if (strcmp(mc_name,"parse_float")==0||strcmp(mc_name,"to_float")==0)
                    mc_result=xs_float(atof(s));
                else if (strcmp(mc_name,"index_of")==0) {
                    mc_result=xs_int(-1);
                    if(mc_argc>=1&&mc_args[0]->tag==XS_STR){
                        const char *fnd=strstr(s,mc_args[0]->s);
                        if(fnd){value_decref(mc_result);mc_result=xs_int((int64_t)(fnd-s));}
                    }
                } else if (strcmp(mc_name,"last_index_of")==0) {
                    mc_result=xs_int(-1);
                    if(mc_argc>=1&&mc_args[0]->tag==XS_STR){
                        size_t sl2=strlen(mc_args[0]->s);
                        if(sl2>0){
                            for(int j=slen-(int)sl2;j>=0;j--){
                                if(strncmp(s+j,mc_args[0]->s,sl2)==0){value_decref(mc_result);mc_result=xs_int(j);break;}
                            }
                        }
                    }
                } else if (strcmp(mc_name,"repeat")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t n2=mc_args[0]->i; if(n2<0) n2=0;
                    size_t rlen=(size_t)slen*(size_t)n2;
                    char *buf=xs_malloc(rlen+1); size_t wpos=0;
                    for(int64_t j=0;j<n2;j++){memcpy(buf+wpos,s,(size_t)slen);wpos+=(size_t)slen;}
                    buf[wpos]='\0'; mc_result=xs_str(buf); free(buf);
                } else if (strcmp(mc_name,"slice")==0) {
                    int64_t st2=0, en2=(int64_t)slen;
                    if(mc_argc>=1&&mc_args[0]->tag==XS_INT) st2=mc_args[0]->i;
                    if(mc_argc>=2&&mc_args[1]->tag==XS_INT) en2=mc_args[1]->i;
                    if(st2<0) st2+=(int64_t)slen;
                    if(en2<0) en2+=(int64_t)slen;
                    if(st2<0) st2=0;
                    if(en2>(int64_t)slen) en2=(int64_t)slen;
                    if(st2>=en2){mc_result=xs_str("");}
                    else{
                        size_t n2=(size_t)(en2-st2); char *buf=xs_malloc(n2+1);
                        memcpy(buf,s+st2,n2); buf[n2]='\0'; mc_result=xs_str(buf); free(buf);
                    }
                } else if (strcmp(mc_name,"bytes")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<slen;j++) array_push(arr->arr,xs_int((unsigned char)s[j]));
                    mc_result=arr;
                } else if (strcmp(mc_name,"capitalize")==0) {
                    char *r=xs_strdup(s);
                    if(r[0]) r[0]=(char)toupper((unsigned char)r[0]);
                    for(int j=1;r[j];j++) r[j]=(char)tolower((unsigned char)r[j]);
                    mc_result=xs_str(r); free(r);
                } else if (strcmp(mc_name,"count")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    int cnt=0; size_t sl2=strlen(mc_args[0]->s);
                    if(sl2>0){const char *p3=s;while((p3=strstr(p3,mc_args[0]->s))!=NULL){cnt++;p3+=sl2;}}
                    mc_result=xs_int(cnt);
                } else if (strcmp(mc_name,"pad_left")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t n2=mc_args[0]->i; char ch=' ';
                    if(mc_argc>=2&&mc_args[1]->tag==XS_STR&&mc_args[1]->s[0]) ch=mc_args[1]->s[0];
                    if(n2<=(int64_t)slen) mc_result=xs_str(s);
                    else{
                        char *buf=xs_malloc((size_t)n2+1);
                        int64_t pad=n2-(int64_t)slen;
                        for(int64_t j=0;j<pad;j++) buf[j]=ch;
                        memcpy(buf+pad,s,(size_t)slen); buf[n2]='\0';
                        mc_result=xs_str(buf); free(buf);
                    }
                } else if (strcmp(mc_name,"pad_right")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t n2=mc_args[0]->i; char ch=' ';
                    if(mc_argc>=2&&mc_args[1]->tag==XS_STR&&mc_args[1]->s[0]) ch=mc_args[1]->s[0];
                    if(n2<=(int64_t)slen) mc_result=xs_str(s);
                    else{
                        char *buf=xs_malloc((size_t)n2+1);
                        memcpy(buf,s,(size_t)slen);
                        for(int64_t j=(int64_t)slen;j<n2;j++) buf[j]=ch;
                        buf[n2]='\0'; mc_result=xs_str(buf); free(buf);
                    }
                } else mc_result=value_incref(XS_NULL_VAL);
            }
            else if (mc_obj->tag==XS_ARRAY||mc_obj->tag==XS_TUPLE) {
                if (strcmp(mc_name,"len")==0||strcmp(mc_name,"size")==0)
                    mc_result=xs_int(mc_obj->arr->len);
                else if (strcmp(mc_name,"push")==0||strcmp(mc_name,"append")==0) {
                    for(int j=0;j<mc_argc;j++) array_push(mc_obj->arr,value_incref(mc_args[j]));
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"pop")==0) {
                    if(mc_obj->arr->len>0){
                        mc_result=value_incref(mc_obj->arr->items[mc_obj->arr->len-1]);
                        value_decref(mc_obj->arr->items[--mc_obj->arr->len]);
                    } else mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"first")==0||strcmp(mc_name,"head")==0)
                    mc_result=mc_obj->arr->len>0?value_incref(mc_obj->arr->items[0]):value_incref(XS_NULL_VAL);
                else if (strcmp(mc_name,"last")==0)
                    mc_result=mc_obj->arr->len>0?value_incref(mc_obj->arr->items[mc_obj->arr->len-1]):value_incref(XS_NULL_VAL);
                else if (strcmp(mc_name,"contains")==0||strcmp(mc_name,"includes")==0) {
                    mc_result=value_incref(XS_FALSE_VAL);
                    if(mc_argc>=1){for(int j=0;j<mc_obj->arr->len;j++){if(value_equal(mc_obj->arr->items[j],mc_args[0])){value_decref(mc_result);mc_result=value_incref(XS_TRUE_VAL);break;}}}
                } else if (strcmp(mc_name,"join")==0) {
                    const char *sep=(mc_argc>=1&&mc_args[0]->tag==XS_STR)?mc_args[0]->s:"";
                    size_t cap=256; char *buf=xs_malloc(cap); size_t wpos=0;
                    for(int j=0;j<mc_obj->arr->len;j++){
                        char *sv=value_str(mc_obj->arr->items[j]); size_t svl=strlen(sv);
                        if(j>0){size_t sl=strlen(sep);while(wpos+sl+svl+2>cap){cap*=2;buf=xs_realloc(buf,cap);}memcpy(buf+wpos,sep,sl);wpos+=sl;}
                        while(wpos+svl+2>cap){cap*=2;buf=xs_realloc(buf,cap);}
                        memcpy(buf+wpos,sv,svl); wpos+=svl; free(sv);
                    }
                    buf[wpos]='\0'; mc_result=xs_str(buf); free(buf);
                } else if (strcmp(mc_name,"reverse")==0) {
                    int n2=mc_obj->arr->len;
                    for(int j=0;j<n2/2;j++){
                        Value *tmp=mc_obj->arr->items[j];
                        mc_obj->arr->items[j]=mc_obj->arr->items[n2-1-j];
                        mc_obj->arr->items[n2-1-j]=tmp;
                    }
                    mc_result=value_incref(mc_obj);
                } else if (strcmp(mc_name,"shift")==0) {
                    if(mc_obj->arr->len>0){
                        mc_result=value_incref(mc_obj->arr->items[0]);
                        value_decref(mc_obj->arr->items[0]);
                        for(int j=1;j<mc_obj->arr->len;j++) mc_obj->arr->items[j-1]=mc_obj->arr->items[j];
                        mc_obj->arr->len--;
                    } else mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"unshift")==0) {
                    for(int j=0;j<mc_argc;j++){
                        array_push(mc_obj->arr, value_incref(XS_NULL_VAL));
                        for(int k=mc_obj->arr->len-1;k>0;k--) mc_obj->arr->items[k]=mc_obj->arr->items[k-1];
                        mc_obj->arr->items[0]=value_incref(mc_args[j]);
                    }
                    mc_result=xs_int(mc_obj->arr->len);
                } else if (strcmp(mc_name,"insert")==0&&mc_argc>=2&&mc_args[0]->tag==XS_INT) {
                    int64_t pos=mc_args[0]->i;
                    if(pos<0) pos=0;
                    if(pos>mc_obj->arr->len) pos=mc_obj->arr->len;
                    array_push(mc_obj->arr, value_incref(XS_NULL_VAL));
                    for(int k=mc_obj->arr->len-1;k>(int)pos;k--) mc_obj->arr->items[k]=mc_obj->arr->items[k-1];
                    mc_obj->arr->items[(int)pos]=value_incref(mc_args[1]);
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"remove")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t pos=mc_args[0]->i;
                    if(pos>=0&&pos<mc_obj->arr->len){
                        mc_result=value_incref(mc_obj->arr->items[pos]);
                        value_decref(mc_obj->arr->items[pos]);
                        for(int k=(int)pos;k<mc_obj->arr->len-1;k++) mc_obj->arr->items[k]=mc_obj->arr->items[k+1];
                        mc_obj->arr->len--;
                    } else mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"slice")==0) {
                    int64_t start=0, end=mc_obj->arr->len;
                    if(mc_argc>=1&&mc_args[0]->tag==XS_INT) start=mc_args[0]->i;
                    if(mc_argc>=2&&mc_args[1]->tag==XS_INT) end=mc_args[1]->i;
                    if(start<0) start+=mc_obj->arr->len;
                    if(end<0) end+=mc_obj->arr->len;
                    if(start<0) start=0;
                    if(end>mc_obj->arr->len) end=mc_obj->arr->len;
                    Value *arr=xs_array_new();
                    for(int64_t j=start;j<end;j++) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    mc_result=arr;
                } else if (strcmp(mc_name,"concat")==0&&mc_argc>=1) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    for(int a2=0;a2<mc_argc;a2++){
                        if(mc_args[a2]->tag==XS_ARRAY) for(int j=0;j<mc_args[a2]->arr->len;j++) array_push(arr->arr,value_incref(mc_args[a2]->arr->items[j]));
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"sort")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    for(int j=0;j<arr->arr->len-1;j++) for(int k=0;k<arr->arr->len-1-j;k++){
                        if(value_cmp(arr->arr->items[k],arr->arr->items[k+1])>0){
                            Value *tmp=arr->arr->items[k]; arr->arr->items[k]=arr->arr->items[k+1]; arr->arr->items[k+1]=tmp;
                        }
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"filter")==0&&mc_argc>=1) {
                    Value *arr=xs_array_new();
                    Value *pred=mc_args[0];
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *elem=mc_obj->arr->items[j];
                        Value *r=vm_invoke(vm,pred,&elem,1);
                        if(value_truthy(r)) array_push(arr->arr,value_incref(elem));
                        value_decref(r);
                    }
                    frame=FRAME;
                    mc_result=arr;
                } else if (strcmp(mc_name,"map")==0&&mc_argc>=1) {
                    Value *arr=xs_array_new();
                    Value *fn=mc_args[0];
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *elem=mc_obj->arr->items[j];
                        Value *r=vm_invoke(vm,fn,&elem,1);
                        array_push(arr->arr,r?r:value_incref(XS_NULL_VAL));
                    }
                    frame=FRAME;
                    mc_result=arr;
                } else if (strcmp(mc_name,"find")==0&&mc_argc>=1) {
                    mc_result=value_incref(XS_NULL_VAL);
                    Value *pred=mc_args[0];
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *elem=mc_obj->arr->items[j];
                        Value *r=vm_invoke(vm,pred,&elem,1);
                        if(value_truthy(r)){value_decref(mc_result);mc_result=value_incref(elem);value_decref(r);break;}
                        value_decref(r);
                    }
                    frame=FRAME;
                } else if (strcmp(mc_name,"reduce")==0||strcmp(mc_name,"fold")==0) {
                    Value *acc=(mc_argc>=2)?value_incref(mc_args[1]):value_incref(XS_NULL_VAL);
                    Value *fn=(mc_argc>=1)?mc_args[0]:NULL;
                    int start_j=(mc_argc>=2)?0:1;
                    if(mc_argc<2&&mc_obj->arr->len>0){value_decref(acc);acc=value_incref(mc_obj->arr->items[0]);}
                    if(fn&&(fn->tag==XS_NATIVE||fn->tag==XS_CLOSURE)){
                        for(int j=start_j;j<mc_obj->arr->len;j++){
                            Value *pair[2]={acc,mc_obj->arr->items[j]};
                            Value *r=vm_invoke(vm,fn,pair,2);
                            value_decref(acc); acc=r?r:value_incref(XS_NULL_VAL);
                        }
                        frame=FRAME;
                    }
                    mc_result=acc;
                } else if (strcmp(mc_name,"index_of")==0||strcmp(mc_name,"find_index")==0) {
                    mc_result=xs_int(-1);
                    if(mc_argc>=1){
                        for(int j=0;j<mc_obj->arr->len;j++){
                            if(value_equal(mc_obj->arr->items[j],mc_args[0])){value_decref(mc_result);mc_result=xs_int(j);break;}
                        }
                    }
                } else if (strcmp(mc_name,"any")==0&&mc_argc>=1) {
                    mc_result=value_incref(XS_FALSE_VAL);
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *r=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1);
                        if(value_truthy(r)){value_decref(mc_result);mc_result=value_incref(XS_TRUE_VAL);value_decref(r);break;}
                        value_decref(r);
                    }
                    frame=FRAME;
                } else if (strcmp(mc_name,"all")==0&&mc_argc>=1) {
                    mc_result=value_incref(XS_TRUE_VAL);
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *r=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1);
                        if(!value_truthy(r)){value_decref(mc_result);mc_result=value_incref(XS_FALSE_VAL);value_decref(r);break;}
                        value_decref(r);
                    }
                    frame=FRAME;
                } else if (strcmp(mc_name,"count")==0) mc_result=xs_int(mc_obj->arr->len);
                else if (strcmp(mc_name,"sum")==0) {
                    int64_t si=0; double sf=0; int is_float=0;
                    for(int j=0;j<mc_obj->arr->len;j++){
                        if(mc_obj->arr->items[j]->tag==XS_INT) si+=mc_obj->arr->items[j]->i;
                        else if(mc_obj->arr->items[j]->tag==XS_FLOAT){sf+=mc_obj->arr->items[j]->f;is_float=1;}
                    }
                    mc_result=is_float?xs_float(sf+(double)si):xs_int(si);
                } else if (strcmp(mc_name,"min")==0) {
                    mc_result=mc_obj->arr->len>0?value_incref(mc_obj->arr->items[0]):value_incref(XS_NULL_VAL);
                    for(int j=1;j<mc_obj->arr->len;j++) if(value_cmp(mc_obj->arr->items[j],mc_result)<0){value_decref(mc_result);mc_result=value_incref(mc_obj->arr->items[j]);}
                } else if (strcmp(mc_name,"max")==0) {
                    mc_result=mc_obj->arr->len>0?value_incref(mc_obj->arr->items[0]):value_incref(XS_NULL_VAL);
                    for(int j=1;j<mc_obj->arr->len;j++) if(value_cmp(mc_obj->arr->items[j],mc_result)>0){value_decref(mc_result);mc_result=value_incref(mc_obj->arr->items[j]);}
                } else if (strcmp(mc_name,"enumerate")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *pair=xs_tuple_new();
                        array_push(pair->arr,xs_int(j));
                        array_push(pair->arr,value_incref(mc_obj->arr->items[j]));
                        array_push(arr->arr,pair);
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"flat")==0||strcmp(mc_name,"flatten")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++){
                        if(mc_obj->arr->items[j]->tag==XS_ARRAY){
                            for(int k=0;k<mc_obj->arr->items[j]->arr->len;k++)
                                array_push(arr->arr,value_incref(mc_obj->arr->items[j]->arr->items[k]));
                        } else array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"unique")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++){
                        int dup=0;
                        for(int k=0;k<arr->arr->len;k++) if(value_equal(mc_obj->arr->items[j],arr->arr->items[k])){dup=1;break;}
                        if(!dup) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    }
                    mc_result=arr;
                } else mc_result=value_incref(XS_NULL_VAL);
            }
            else mc_result=value_incref(XS_NULL_VAL);

            if (!mc_called) {
                for(int j=0;j<mc_argc;j++) value_decref(POP());
                value_decref(POP()); /* obj */
                PUSH(mc_result ? mc_result : value_incref(XS_NULL_VAL));
            }
            break;
        }

        case OP_SWAP: {
            Value *b = POP(), *a = POP();
            PUSH(b); PUSH(a);
            break;
        }

        /* bitwise */
        case OP_BAND: { Value *b=POP(),*a=POP(); PUSH(xs_int(a->i & b->i)); value_decref(a);value_decref(b); break; }
        case OP_BOR:  { Value *b=POP(),*a=POP(); PUSH(xs_int(a->i | b->i)); value_decref(a);value_decref(b); break; }
        case OP_BXOR: { Value *b=POP(),*a=POP(); PUSH(xs_int(a->i ^ b->i)); value_decref(a);value_decref(b); break; }
        case OP_BNOT: { Value *a=POP(); PUSH(xs_int(~a->i)); value_decref(a); break; }
        case OP_SHL:  { Value *b=POP(),*a=POP(); PUSH(xs_int(a->i << (b->i & 63))); value_decref(a);value_decref(b); break; }
        case OP_SHR:  { Value *b=POP(),*a=POP(); PUSH(xs_int(a->i >> (b->i & 63))); value_decref(a);value_decref(b); break; }

        case OP_TRY_BEGIN: {
            if (frame->try_depth < VM_TRY_STACK_MAX) {
                TryEntry *te = &frame->try_stack[frame->try_depth++];
                te->catch_ip  = frame->ip + INSTR_sBx(instr);
                te->stack_top = vm->sp;
            }
            break;
        }
        case OP_TRY_END: {
            if (frame->try_depth > 0) frame->try_depth--;
            break;
        }
        case OP_THROW: {
            Value *exc = POP();
            int handled = 0;
            while (vm->frame_count > 0) {
                CallFrame *cf = &vm->frames[vm->frame_count - 1];
                if (cf->try_depth > 0) {
                    TryEntry *te = &cf->try_stack[--cf->try_depth];
                    while (vm->sp > te->stack_top) value_decref(POP());
                    PUSH(exc);
                    frame = cf;
                    frame->ip = te->catch_ip;
                    handled = 1;
                    break;
                }
                upvalue_close_all(&vm->open_upvalues, cf->base);
                while (vm->sp > cf->base) value_decref(POP());
                value_decref(cf->closure_val);
                vm->frame_count--;
            }
            if (!handled) {
                char *s = value_str(exc);
                fprintf(stderr, "uncaught: %s\n", s);
                free(s);
                value_decref(exc);
                return 1;
            }
            break;
        }
        case OP_CATCH:
            break;

        // --- trace
        case OP_TRACE_CALL: {
#ifdef XSC_ENABLE_TRACER
            if (vm->tracer) {
                const char *fn_name = PROTO->name ? PROTO->name : "<anon>";
                tracer_record_call(vm->tracer, fn_name, 0);
            }
#endif
            break;
        }
        case OP_TRACE_RETURN: {
#ifdef XSC_ENABLE_TRACER
            if (vm->tracer) {
                const char *fn_name = PROTO->name ? PROTO->name : "<anon>";
                Value *retval = (vm->sp > vm->stack) ? PEEK(0) : NULL;
                tracer_record_return(vm->tracer, fn_name, retval);
            }
#endif
            break;
        }
        case OP_TRACE_STORE: {
#ifdef XSC_ENABLE_TRACER
            if (vm->tracer) {
                uint16_t name_idx = INSTR_Bx(instr);
                Value *name_val = PROTO->chunk.consts[name_idx];
                const char *var_name = (name_val && name_val->tag == XS_STR) ? name_val->s : "?";
                Value *val = (vm->sp > vm->stack) ? PEEK(0) : NULL;
                tracer_record_store(vm->tracer, var_name, val);
            }
#endif
            break;
        }
        case OP_TRACE_IO: {
#ifdef XSC_ENABLE_TRACER
            if (vm->tracer) {
                Value *top = (vm->sp > vm->stack) ? PEEK(0) : NULL;
                const char *buf = NULL;
                int buf_len = 0;
                if (top && top->tag == XS_STR && top->s) {
                    buf = top->s;
                    buf_len = (int)strlen(top->s);
                }
                tracer_record_io(vm->tracer, "io", (void *)buf, buf_len);
            }
#endif
            break;
        }

        // --- logical
        case OP_AND: {
            Value *b = POP(), *a = POP();
            int at = value_truthy(a);
            if (!at) { PUSH(a); value_decref(b); }
            else     { PUSH(b); value_decref(a); }
            break;
        }
        case OP_OR: {
            Value *b = POP(), *a = POP();
            int at = value_truthy(a);
            if (at) { PUSH(a); value_decref(b); }
            else    { PUSH(b); value_decref(a); }
            break;
        }

        /* spread */
        case OP_SPREAD: {
            Value *arr = POP();
            if (arr->tag == XS_ARRAY) {
                for (int si = 0; si < arr->arr->len; si++) {
                    value_incref(arr->arr->items[si]);
                    PUSH(arr->arr->items[si]);
                }
            }
            value_decref(arr);
            break;
        }

        case OP_LOOP:
            frame->ip += INSTR_sBx(instr);
            break;

        // --- effects
        case OP_EFFECT_HANDLE: {
            if (frame->try_depth < VM_TRY_STACK_MAX) {
                TryEntry *te = &frame->try_stack[frame->try_depth++];
                te->catch_ip  = frame->ip + INSTR_sBx(instr);
                te->stack_top = vm->sp;
            }
            break;
        }
        case OP_EFFECT_CALL: {
            int argc_eff = (int)INSTR_A(instr);
            const char *eff_name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;

            Value *eff_args[16];
            for (int i = argc_eff - 1; i >= 0; i--) eff_args[i] = POP();

            Value *eff = xs_map_new();
            { Value *en = xs_str(eff_name); map_set(eff->map, "_effect", en); value_decref(en); }
            Value *args_arr = xs_array_new();
            for (int i = 0; i < argc_eff; i++) {
                array_push(args_arr->arr, eff_args[i]);
                value_decref(eff_args[i]);
            }
            map_set(eff->map, "_args", args_arr);
            value_decref(args_arr);

            int eff_handled = 0;
            while (vm->frame_count > 0) {
                CallFrame *cf = &vm->frames[vm->frame_count - 1];
                if (cf->try_depth > 0) {
                    TryEntry *te = &cf->try_stack[--cf->try_depth];
                    while (vm->sp > te->stack_top) value_decref(POP());
                    PUSH(eff);
                    frame = cf;
                    frame->ip = te->catch_ip;
                    eff_handled = 1;
                    break;
                }
                upvalue_close_all(&vm->open_upvalues, cf->base);
                while (vm->sp > cf->base) value_decref(POP());
                value_decref(cf->closure_val);
                vm->frame_count--;
            }
            if (!eff_handled) {
                char *s = value_str(eff);
                fprintf(stderr, "unhandled effect: %s\n", s);
                free(s);
                value_decref(eff);
                return 1;
            }
            break;
        }
        case OP_EFFECT_RESUME: {
            Value *resume_val = POP();
            PUSH(resume_val);
            break;
        }

        // --- async/generators
        case OP_AWAIT: {
            Value *task = POP();
            if (task->tag == XS_MAP) {
                Value *tid_val = map_get(task->map, "_task_id");
                if (tid_val && tid_val->tag == XS_INT) {
                    int tid = (int)tid_val->i;
                    for (int t = 0; t <= tid && t < vm->n_tasks; t++) {
                        if (vm->tasks[t].done) continue;
                        Value *tfn = vm->tasks[t].fn;
                        Value *tres = NULL;
                        if (tfn->tag == XS_NATIVE) {
                            tres = tfn->native(NULL, NULL, 0);
                        } else if (tfn->tag == XS_CLOSURE) {
                            tres = vm_invoke(vm, tfn, NULL, 0);
                        }
                        vm->tasks[t].result = tres ? tres : value_incref(XS_NULL_VAL);
                        vm->tasks[t].done = 1;
                    }
                    if (tid >= 0 && tid < vm->n_tasks && vm->tasks[tid].done) {
                        Value *await_result = vm->tasks[tid].result;
                        PUSH(await_result ? value_incref(await_result) : value_incref(XS_NULL_VAL));
                        { Value *sv = xs_str("done"); map_set(task->map, "_status", sv); value_decref(sv); }
                        if (await_result) map_set(task->map, "_result", await_result);
                    } else {
                        PUSH(value_incref(XS_NULL_VAL));
                    }
                    value_decref(task);
                } else {
                    Value *status = map_get(task->map, "_status");
                    if (status && status->tag == XS_STR && strcmp(status->s, "done") == 0) {
                        Value *await_result = map_get(task->map, "_result");
                        PUSH(await_result ? value_incref(await_result) : value_incref(XS_NULL_VAL));
                        value_decref(task);
                    } else if (status && status->tag == XS_STR && strcmp(status->s, "pending") == 0) {
                        Value *task_fn = map_get(task->map, "_fn");
                        if (task_fn && task_fn->tag == XS_NATIVE) {
                            Value *await_result = task_fn->native(NULL, NULL, 0);
                            { Value *sv = xs_str("done"); map_set(task->map, "_status", sv); value_decref(sv); }
                            map_set(task->map, "_result", await_result ? await_result : value_incref(XS_NULL_VAL));
                            PUSH(await_result ? await_result : value_incref(XS_NULL_VAL));
                            if (await_result) value_decref(await_result);
                            value_decref(task);
                        } else if (task_fn && task_fn->tag == XS_CLOSURE) {
                            int cl_arity = task_fn->cl->proto->arity;
                            if (cl_arity < 0) cl_arity = -(cl_arity + 1);
                            if (cl_arity == 0) {
                                PUSH(task);
                                value_incref(task_fn);
                                if (call_frame_push(vm, task_fn, 0) == 0) {
                                    value_decref(task_fn);
                                    frame = FRAME;
                                    vm->spawn_task = value_incref(task);
                                    value_decref(task);
                                    break;
                                }
                                value_decref(task_fn);
                                Value *st = POP();
                                PUSH(st);
                            } else {
                                PUSH(task);
                            }
                        } else {
                            Value *await_result = map_get(task->map, "_result");
                            PUSH(await_result ? value_incref(await_result) : value_incref(task));
                            if (!await_result) value_decref(task);
                            else value_decref(task);
                        }
                    } else {
                        Value *await_result = map_get(task->map, "_result");
                        if (await_result) {
                            PUSH(value_incref(await_result));
                            value_decref(task);
                        } else {
                            PUSH(task);
                        }
                    }
                }
            } else if (task->tag == XS_NATIVE) {
                Value *await_result = task->native(NULL, NULL, 0);
                value_decref(task);
                PUSH(await_result ? await_result : value_incref(XS_NULL_VAL));
            } else {
                PUSH(task);
            }
            break;
        }
        case OP_YIELD: {
            Value *val = POP();
            if (frame->is_generator && frame->yield_arr) {
                array_push(frame->yield_arr->arr, val);
                frame->yield_index++;
            } else {
                Value *result = xs_map_new();
                { Value *dv = value_incref(XS_FALSE_VAL); map_set(result->map, "done", dv); value_decref(dv); }
                map_set(result->map, "value", val);
                value_decref(val);
                upvalue_close_all(&vm->open_upvalues, frame->base);
                while (vm->sp > frame->base) value_decref(POP());
                value_decref(frame->closure_val);
                vm->frame_count--;
                if (vm->frame_count == 0) { value_decref(result); return 0; }
                frame = FRAME;
                PUSH(result);
            }
            break;
        }
        case OP_SPAWN: {
            Value *fn = POP();
            Value *task = xs_map_new();
            if (fn->tag == XS_NATIVE || fn->tag == XS_CLOSURE) {
                if (vm->n_tasks < VM_MAX_TASKS) {
                    int tid = vm->n_tasks++;
                    vm->tasks[tid].fn     = value_incref(fn);
                    vm->tasks[tid].result = NULL;
                    vm->tasks[tid].done   = 0;
                    { Value *sv = xs_str("pending"); map_set(task->map, "_status", sv); value_decref(sv); }
                    { Value *iv = xs_int(tid); map_set(task->map, "_task_id", iv); value_decref(iv); }
                    map_set(task->map, "_fn", fn);
                } else {
                    if (fn->tag == XS_NATIVE) {
                        Value *result = fn->native(NULL, NULL, 0);
                        { Value *sv = xs_str("done"); map_set(task->map, "_status", sv); value_decref(sv); }
                        map_set(task->map, "_result", result ? result : value_incref(XS_NULL_VAL));
                        if (result) value_decref(result);
                    } else {
                        int cl_arity = fn->cl->proto->arity;
                        if (cl_arity < 0) cl_arity = -(cl_arity + 1);
                        if (cl_arity == 0) {
                            PUSH(task);
                            value_incref(fn);
                            if (call_frame_push(vm, fn, 0) == 0) {
                                value_decref(fn);
                                frame = FRAME;
                                vm->spawn_task = value_incref(task);
                                break;
                            }
                            value_decref(fn);
                            Value *saved_task = POP();
                            { Value *sv = xs_str("error"); map_set(saved_task->map, "_status", sv); value_decref(sv); }
                            PUSH(saved_task);
                        } else {
                            { Value *sv = xs_str("pending"); map_set(task->map, "_status", sv); value_decref(sv); }
                            map_set(task->map, "_fn", fn);
                        }
                    }
                }
            } else {
                { Value *sv = xs_str("done"); map_set(task->map, "_status", sv); value_decref(sv); }
                map_set(task->map, "_result", fn);
            }
            value_decref(fn);
            PUSH(task);
            break;
        }

        /* MAKE_CLASS */
        case OP_MAKE_CLASS: {
            int nfields = (int)INSTR_A(instr);
            const char *cls_name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *cls = xs_map_new();
            { Value *nv = xs_str(cls_name); map_set(cls->map, "__name", nv); value_decref(nv); }
            { Value *nv = xs_str(cls_name); map_set(cls->map, "__type", nv); value_decref(nv); }
            Value *fields = xs_map_new();
            Value *tmp[512];
            for (int i = nfields*2-1; i >= 0; i--) tmp[i] = POP();
            for (int i = 0; i < nfields; i++) {
                Value *k = tmp[i*2], *v = tmp[i*2+1];
                if (k->tag == XS_STR) map_set(fields->map, k->s, v);
                value_decref(k); value_decref(v);
            }
            map_set(cls->map, "__fields", fields);
            value_decref(fields);
            Value *methods = xs_map_new();
            map_set(cls->map, "__methods", methods);
            value_decref(methods);
            PUSH(cls);
            break;
        }

        case OP_IMPL_METHOD: {
            Value *closure = POP();
            Value *name_val = POP();
            Value *type_val = POP();
            if (type_val->tag == XS_MAP || type_val->tag == XS_MODULE) {
                Value *methods = map_get(type_val->map, "__methods");
                if (methods && methods->tag == XS_MAP && name_val->tag == XS_STR) {
                    map_set(methods->map, name_val->s, closure);
                } else if (name_val->tag == XS_STR) {
                    Value *impl = map_get(type_val->map, "__impl__");
                    if (!impl) {
                        impl = xs_map_new();
                        map_set(type_val->map, "__impl__", impl);
                        value_decref(impl);
                        impl = map_get(type_val->map, "__impl__");
                    }
                    if (impl && impl->tag == XS_MAP)
                        map_set(impl->map, name_val->s, closure);
                }
            }
            value_decref(closure);
            value_decref(name_val);
            value_decref(type_val);
            break;
        }

        case OP_DEFER_PUSH: {
            if (frame->defer_depth < VM_DEFER_MAX) {
                frame->defer_stack[frame->defer_depth++].defer_ip =
                    frame->ip; /* ip already advanced past DEFER_PUSH */
            }
            frame->ip += INSTR_sBx(instr);
            break;
        }
        case OP_DEFER_RUN: {
            if (frame->defer_depth > 0) {
                frame->defer_return_ip = frame->ip;
                frame->defer_depth--;
                frame->ip = frame->defer_stack[frame->defer_depth].defer_ip;
            }
            break;
        }

        /* actor send */
        case OP_SEND: {
            Value *msg = POP();
            Value *actor_val = POP();
            Value *result = value_incref(XS_NULL_VAL);
            if (actor_val->tag == XS_MAP) {
                Value *methods = map_get(actor_val->map, "__methods");
                if (methods && methods->tag == XS_MAP) {
                    Value *handle = map_get(methods->map, "handle");
                    if (handle && handle->tag == XS_CLOSURE) {
                        value_incref(handle);
                        PUSH(handle);
                        PUSH(value_incref(msg));
                        value_incref(handle);
                        Value *callee_h = vm->sp[-2];
                        if (callee_h->tag == XS_CLOSURE) {
                            Value *sv = callee_h; value_incref(sv);
                            for (int j = -2; j < -1; j++) vm->sp[j] = vm->sp[j+1];
                            vm->sp--; value_decref(sv);
                            if (call_frame_push(vm, sv, 1) == 0) {
                                value_decref(sv);
                                frame = FRAME;
                                value_decref(msg);
                                value_decref(actor_val);
                                value_decref(result);
                                break;
                            }
                            value_decref(sv);
                        }
                        value_decref(handle);
                    }
                }
            } else if (actor_val->tag == XS_ACTOR && actor_val->actor) {
                XSActor *act = actor_val->actor;
                if (act->handle_fn) {
                    if (act->methods) {
                        Value *hfn = map_get(act->methods, "handle");
                        if (hfn && hfn->tag == XS_NATIVE) {
                            Value *hargs[1] = { msg };
                            Value *hr = hfn->native(NULL, hargs, 1);
                            value_decref(result);
                            result = hr ? hr : value_incref(XS_NULL_VAL);
                        } else if (hfn && hfn->tag == XS_CLOSURE) {
                            value_decref(result);
                            value_incref(hfn);
                            PUSH(value_incref(msg));
                            if (call_frame_push(vm, hfn, 1) == 0) {
                                value_decref(hfn);
                                frame = FRAME;
                                value_decref(msg);
                                value_decref(actor_val);
                                break;
                            }
                            value_decref(hfn);
                            result = value_incref(XS_NULL_VAL);
                        }
                    }
                }
            }
            value_decref(msg);
            value_decref(actor_val);
            PUSH(result);
            break;
        }

        case OP_FLOOR_DIV: {
            Value *b = POP(), *a = POP();
            double av = a->tag==XS_INT ? (double)a->i : a->f;
            double bv = b->tag==XS_INT ? (double)b->i : b->f;
            if (bv == 0.0) { fprintf(stderr, "division by zero\n"); value_decref(a); value_decref(b); return 1; }
            Value *r = xs_int((int64_t)floor(av / bv));
            value_decref(a); value_decref(b); PUSH(r);
            break;
        }
        case OP_SPACESHIP: {
            Value *b = POP(), *a = POP();
            int cmp = value_cmp(a, b);
            Value *r = xs_int(cmp < 0 ? -1 : cmp > 0 ? 1 : 0);
            value_decref(a); value_decref(b); PUSH(r);
            break;
        }

        case OP_MAKE_ENUM: {
            int nvariants = (int)INSTR_A(instr);
            const char *enum_name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *enum_map = xs_map_new();
            Value *tmp_e[512];
            for (int i = nvariants * 2 - 1; i >= 0; i--) tmp_e[i] = POP();
            for (int i = 0; i < nvariants; i++) {
                Value *k = tmp_e[i * 2], *v = tmp_e[i * 2 + 1];
                if (k->tag == XS_STR) map_set(enum_map->map, k->s, v);
                value_decref(k); value_decref(v);
            }
            { Value *nv = xs_str(enum_name); map_set(enum_map->map, "__type", nv); value_decref(nv); }
            { Value *nv = xs_str(enum_name); map_set(enum_map->map, "__name", nv); value_decref(nv); }
            PUSH(enum_map);
            break;
        }

        case OP_MAKE_INST: {
            int nargs = (int)INSTR_A(instr);
            const char *cls_name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *inst_args[256];
            for (int i = nargs - 1; i >= 0; i--) inst_args[i] = POP();
            Value *cls_val = POP();
            Value *inst = xs_map_new();
            if (cls_val->tag == XS_MAP || cls_val->tag == XS_MODULE) {
                Value *fields = map_get(cls_val->map, "__fields");
                if (fields && fields->tag == XS_MAP) {
                    for (int j = 0; j < fields->map->cap; j++) {
                        if (fields->map->keys[j])
                            map_set(inst->map, fields->map->keys[j],
                                    value_incref(fields->map->vals[j]));
                    }
                }
                Value *methods = map_get(cls_val->map, "__methods");
                if (methods && methods->tag == XS_MAP) {
                    for (int j = 0; j < methods->map->cap; j++) {
                        if (methods->map->keys[j])
                            map_set(inst->map, methods->map->keys[j],
                                    value_incref(methods->map->vals[j]));
                    }
                }
            }
            { Value *nv = xs_str(cls_name); map_set(inst->map, "__type", nv); value_decref(nv); }
            {
                Value *mi_init = map_get(inst->map, "init");
                if (mi_init && mi_init->tag == XS_NATIVE && nargs > 0) {
                    Value *mi_call_args[257];
                    mi_call_args[0] = inst;
                    for (int i = 0; i < nargs; i++)
                        mi_call_args[i + 1] = inst_args[i];
                    Value *ir = mi_init->native(NULL, mi_call_args, nargs + 1);
                    if (ir) value_decref(ir);
                    for (int i = 0; i < nargs; i++) value_decref(inst_args[i]);
                    value_decref(cls_val);
                    PUSH(inst);
                } else if (mi_init && mi_init->tag == XS_CLOSURE && nargs > 0) {
                    value_decref(cls_val);
                    PUSH(inst);
                    PUSH(value_incref(inst));
                    for (int i = 0; i < nargs; i++) PUSH(inst_args[i]);
                    value_incref(mi_init);
                    if (call_frame_push(vm, mi_init, nargs + 1) == 0) {
                        value_decref(mi_init);
                        frame = FRAME;
                        vm->init_inst = value_incref(inst);
                        break;
                    } else {
                        value_decref(mi_init);
                    }
                } else {
                    for (int i = 0; i < nargs; i++) value_decref(inst_args[i]);
                    value_decref(cls_val);
                    PUSH(inst);
                }
            }
            break;
        }

        case OP_INHERIT: {
            Value *child = POP();
            Value *base = POP();
            if ((base->tag == XS_MAP || base->tag == XS_MODULE) &&
                (child->tag == XS_MAP || child->tag == XS_MODULE)) {
                Value *base_fields = map_get(base->map, "__fields");
                Value *child_fields = map_get(child->map, "__fields");
                if (base_fields && base_fields->tag == XS_MAP && child_fields && child_fields->tag == XS_MAP) {
                    for (int j = 0; j < base_fields->map->cap; j++) {
                        if (base_fields->map->keys[j] &&
                            !map_get(child_fields->map, base_fields->map->keys[j]))
                            map_set(child_fields->map, base_fields->map->keys[j],
                                    value_incref(base_fields->map->vals[j]));
                    }
                }
                Value *base_methods = map_get(base->map, "__methods");
                Value *child_methods = map_get(child->map, "__methods");
                if (base_methods && base_methods->tag == XS_MAP && child_methods && child_methods->tag == XS_MAP) {
                    for (int j = 0; j < base_methods->map->cap; j++) {
                        if (base_methods->map->keys[j] &&
                            !map_get(child_methods->map, base_methods->map->keys[j]))
                            map_set(child_methods->map, base_methods->map->keys[j],
                                    value_incref(base_methods->map->vals[j]));
                    }
                }
            }
            value_decref(base);
            PUSH(child);
            break;
        }

        case OP_MAKE_MODULE: {
            Value *mod = xs_map_new();
            mod->tag = XS_MODULE;
            const char *mod_name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            { Value *nv = xs_str(mod_name); map_set(mod->map, "__name", nv); value_decref(nv); }
            PUSH(mod);
            break;
        }

        case OP_END_MODULE: {
            break;
        }

        case OP_IMPORT: {
            const char *mod_name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *mod = map_get(vm->globals, mod_name);
            if (mod) {
                PUSH(value_incref(mod));
            } else {
                PUSH(value_incref(XS_NULL_VAL));
            }
            break;
        }

        case OP_IMPORT_ITEM: {
            const char *item_name = PROTO->chunk.consts[INSTR_A(instr)]->s;
            const char *mod_name  = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *mod = map_get(vm->globals, mod_name);
            if (mod && (mod->tag == XS_MAP || mod->tag == XS_MODULE)) {
                Value *item = map_get(mod->map, item_name);
                PUSH(item ? value_incref(item) : value_incref(XS_NULL_VAL));
            } else {
                PUSH(value_incref(XS_NULL_VAL));
            }
            break;
        }

        case OP_MAKE_ACTOR: {
            int nstate = (int)INSTR_A(instr);
            const char *actor_name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *methods_map = POP();
            Value *state_tmp[512];
            for (int i = nstate * 2 - 1; i >= 0; i--) state_tmp[i] = POP();
            Value *actor = xs_map_new();
            { Value *nv = xs_str(actor_name); map_set(actor->map, "__actor_name", nv); value_decref(nv); }
            Value *state = xs_map_new();
            for (int i = 0; i < nstate; i++) {
                Value *k = state_tmp[i * 2], *v = state_tmp[i * 2 + 1];
                if (k->tag == XS_STR) map_set(state->map, k->s, v);
                value_decref(k); value_decref(v);
            }
            map_set(actor->map, "__state", state);
            value_decref(state);
            map_set(actor->map, "__methods", methods_map);
            value_decref(methods_map);
            PUSH(actor);
            break;
        }

        case OP_OPT_CHAIN: {
            const char *field_name = PROTO->chunk.consts[INSTR_Bx(instr)]->s;
            Value *obj = POP();
            if (obj->tag == XS_NULL) {
                value_decref(obj);
                PUSH(value_incref(XS_NULL_VAL));
            } else if (obj->tag == XS_MAP || obj->tag == XS_MODULE) {
                Value *val = map_get(obj->map, field_name);
                PUSH(val ? value_incref(val) : value_incref(XS_NULL_VAL));
                value_decref(obj);
            } else {
                value_decref(obj);
                PUSH(value_incref(XS_NULL_VAL));
            }
            break;
        }

        case OP_NULL_COALESCE: {
            Value *b = POP(), *a = POP();
            if (a->tag == XS_NULL) {
                value_decref(a);
                PUSH(b);
            } else {
                value_decref(b);
                PUSH(a);
            }
            break;
        }

        case OP_TRY_OP: {
            Value *val = POP();
            if (val->tag == XS_MAP) {
                Value *tag_val = map_get(val->map, "_tag");
                if (tag_val && tag_val->tag == XS_STR && strcmp(tag_val->s, "Err") == 0) {
                    upvalue_close_all(&vm->open_upvalues, frame->base);
                    while (vm->sp > frame->base) value_decref(POP());
                    value_decref(frame->closure_val);
                    vm->frame_count--;
                    if (vm->frame_count == 0) { value_decref(val); return 0; }
                    frame = FRAME;
                    PUSH(val);
                    break;
                }
                Value *inner = map_get(val->map, "_val");
                if (inner) {
                    PUSH(value_incref(inner));
                    value_decref(val);
                } else {
                    PUSH(val);
                }
            } else {
                PUSH(val);
            }
            break;
        }

        case OP_PIPE: {
            Value *fn = POP();
            Value *arg = POP();
            if (fn->tag == XS_NATIVE) {
                Value *args[1] = { arg };
                Value *result = fn->native(NULL, args, 1);
                value_decref(arg);
                value_decref(fn);
                PUSH(result ? result : value_incref(XS_NULL_VAL));
            } else if (fn->tag == XS_CLOSURE) {
                PUSH(arg);
                value_incref(fn);
                if (call_frame_push(vm, fn, 1)) {
                    value_decref(fn);
                    return 1;
                }
                value_decref(fn);
                frame = FRAME;
            } else {
                fprintf(stderr, "pipe to non-callable\n");
                value_decref(fn);
                value_decref(arg);
                return 1;
            }
            break;
        }

        // --- membership/type
        case OP_IN: {
            Value *right = POP(), *left = POP();
            int found = 0;
            if (right->tag == XS_ARRAY) {
                for (int j = 0; j < right->arr->len; j++)
                    if (value_equal(left, right->arr->items[j])) { found = 1; break; }
            } else if (right->tag == XS_MAP || right->tag == XS_MODULE) {
                if (left->tag == XS_STR) found = map_has(right->map, left->s);
            } else if (right->tag == XS_STR && left->tag == XS_STR) {
                found = strstr(right->s, left->s) != NULL;
            } else if (right->tag == XS_RANGE) {
                if (left->tag == XS_INT) {
                    int64_t v = left->i;
                    found = v >= right->range->start &&
                            (right->range->inclusive ? v <= right->range->end : v < right->range->end);
                }
            }
            value_decref(left); value_decref(right);
            PUSH(xs_bool(found));
            break;
        }
        case OP_IS: {
            Value *right = POP(), *left = POP();
            int match = 0;
            if (right->tag == XS_STR) {
                const char *t = right->s;
                if      (strcmp(t, "int") == 0 || strcmp(t, "i64") == 0) match = (left->tag == XS_INT);
                else if (strcmp(t, "float") == 0 || strcmp(t, "f64") == 0) match = (left->tag == XS_FLOAT);
                else if (strcmp(t, "str") == 0 || strcmp(t, "string") == 0) match = (left->tag == XS_STR);
                else if (strcmp(t, "bool") == 0) match = (left->tag == XS_BOOL);
                else if (strcmp(t, "array") == 0) match = (left->tag == XS_ARRAY);
                else if (strcmp(t, "map") == 0) match = (left->tag == XS_MAP);
                else if (strcmp(t, "null") == 0) match = (left->tag == XS_NULL);
                else if (strcmp(t, "fn") == 0 || strcmp(t, "function") == 0) match = (left->tag == XS_FUNC || left->tag == XS_NATIVE || left->tag == XS_CLOSURE);
                else if (strcmp(t, "tuple") == 0) match = (left->tag == XS_TUPLE);
                else if (left->tag == XS_STRUCT_VAL && left->st) match = (strcmp(left->st->type_name, t) == 0);
                else if (left->tag == XS_ENUM_VAL && left->en) match = (strcmp(left->en->type_name, t) == 0);
                else if (left->tag == XS_INST && left->inst && left->inst->class_) match = (strcmp(left->inst->class_->name, t) == 0);
            }
            value_decref(left); value_decref(right);
            PUSH(xs_bool(match));
            break;
        }

        case OP_MAP_MERGE: {
            Value *src = POP(), *dst = POP();
            if (dst->tag == XS_MAP && src->tag == XS_MAP) {
                int nk = 0;
                char **keys = map_keys(src->map, &nk);
                for (int i = 0; i < nk; i++) {
                    Value *v = map_get(src->map, keys[i]);
                    if (v) { value_incref(v); map_set(dst->map, keys[i], v); value_decref(v); }
                    free(keys[i]);
                }
                free(keys);
            }
            value_decref(src);
            PUSH(dst);
            break;
        }

        default:
            fprintf(stderr, "bad opcode %d\n", (int)op);
            return 1;
        }
    }
}
