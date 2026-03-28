#define _POSIX_C_SOURCE 200112L
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
        "null","bool","int","int","float","str","char",
        "array","map","tuple","fn","native",
        "struct","enum","class","inst","range","signal","actor","re","module",
        "closure"
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

static Value *vm_assert_eq(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2) {
        fprintf(stderr, "xs: assert_eq requires 2 arguments\n");
        exit(1);
    }
    if (!value_equal(args[0], args[1])) {
        char *a = value_repr(args[0]);
        char *b = value_repr(args[1]);
        const char *msg = (argc >= 3 && args[2]->tag == XS_STR) ? args[2]->s : "";
        fprintf(stderr, "xs: assertion failed: assert_eq(%s, %s)%s%s\n",
                a, b, msg[0] ? ": " : "", msg);
        free(a); free(b);
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

/* plugin loading */
static VM *g_plugin_vm;

static Value *vm_plugin_global_set(Interp *interp, Value **args, int argc) {
    (void)interp;
    /* called as method: global.set(name, fn) → args = [self, name, fn] */
    if (argc >= 3 && args[1]->tag == XS_STR && g_plugin_vm) {
        map_set(g_plugin_vm->globals, args[1]->s, args[2]);
    } else if (argc >= 2 && args[0]->tag == XS_STR && g_plugin_vm) {
        map_set(g_plugin_vm->globals, args[0]->s, args[1]);
    }
    return xs_null();
}

static Value *vm_plugin_add_method(Interp *interp, Value **args, int argc) {
    (void)interp;
    /* called as method: runtime.add_method(type, name, fn) → args = [self, type, name, fn] */
    int off = (argc >= 4 && args[0]->tag == XS_MAP) ? 1 : 0;
    if (argc >= 3 + off && args[off]->tag == XS_STR && args[off+1]->tag == XS_STR && g_plugin_vm) {
        Value *pmethods = map_get(g_plugin_vm->globals, "__plugin_methods");
        if (!pmethods) {
            pmethods = xs_map_new();
            map_set(g_plugin_vm->globals, "__plugin_methods", pmethods);
            value_decref(pmethods);
            pmethods = map_get(g_plugin_vm->globals, "__plugin_methods");
        }
        Value *type_methods = map_get(pmethods->map, args[off]->s);
        if (!type_methods) {
            type_methods = xs_map_new();
            map_set(pmethods->map, args[off]->s, type_methods);
            value_decref(type_methods);
            type_methods = map_get(pmethods->map, args[off]->s);
        }
        map_set(type_methods->map, args[off+1]->s, args[off+2]);
    }
    return xs_null();
}

extern XSProto *compile_program(Node *program);

#include "core/lexer.h"
#include "core/parser.h"

static Value *vm_load_plugin(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || args[0]->tag != XS_STR || !g_plugin_vm) return xs_null();

    const char *path = args[0]->s;
    FILE *f = fopen(path, "r");
    /* try relative to source file directory */
    char resolved[1024];
    if (!f) {
        Value *src_file = map_get(g_plugin_vm->globals, "__source_file");
        if (src_file && src_file->tag == XS_STR) {
            const char *dir_end = strrchr(src_file->s, '/');
            if (!dir_end) dir_end = strrchr(src_file->s, '\\');
            if (dir_end) {
                int dir_len = (int)(dir_end - src_file->s + 1);
                snprintf(resolved, sizeof resolved, "%.*s%s", dir_len, src_file->s, path);
                f = fopen(resolved, "r");
                if (f) path = resolved;
            }
        }
    }
    /* try tests/ prefix */
    if (!f) {
        snprintf(resolved, sizeof resolved, "tests/%s", path);
        f = fopen(resolved, "r");
        if (f) path = resolved;
    }
    if (!f) { fprintf(stderr, "plugin not found: %s\n", args[0]->s); return xs_null(); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = xs_malloc((size_t)len + 1);
    fread(src, 1, (size_t)len, f);
    src[len] = '\0';
    fclose(f);

    Lexer lex; lexer_init(&lex, src, path);
    TokenArray ta = lexer_tokenize(&lex);
    Parser psr; parser_init(&psr, &ta, path);
    Node *prog = parser_parse(&psr);
    token_array_free(&ta);
    comment_list_free(&lex.comments);
    free(src);
    if (!prog) return xs_null();

    /* set up plugin object in globals */
    Value *plugin = xs_map_new();
    Value *runtime = xs_map_new();
    Value *global_obj = xs_map_new();
    { Value *v = xs_native(vm_plugin_global_set); map_set(global_obj->map, "set", v); value_decref(v); }
    { Value *v = xs_str("plugin_global"); map_set(global_obj->map, "__type", v); value_decref(v); }
    map_set(runtime->map, "global", global_obj); value_decref(global_obj);
    { Value *v = xs_native(vm_plugin_add_method); map_set(runtime->map, "add_method", v); value_decref(v); }
    map_set(plugin->map, "runtime", runtime); value_decref(runtime);
    Value *meta = xs_map_new();
    map_set(plugin->map, "meta", meta); value_decref(meta);
    map_set(g_plugin_vm->globals, "plugin", plugin); value_decref(plugin);

    XSProto *proto = compile_program(prog);
    node_free(prog);

    /* create a temporary VM for plugin execution, sharing globals */
    VM *plugin_vm = xs_malloc(sizeof(VM));
    memset(plugin_vm, 0, sizeof(VM));
    plugin_vm->sp = plugin_vm->stack;
    plugin_vm->globals = g_plugin_vm->globals;  /* shared globals */
    VM *saved_main_vm = g_plugin_vm;
    VM *saved_invoke = g_vm_for_invoke;
    /* g_plugin_vm stays pointing to main VM for global.set */
    g_vm_for_invoke = plugin_vm;
    vm_run(plugin_vm, proto);
    g_vm_for_invoke = saved_invoke;
    g_plugin_vm = saved_main_vm;
    /* don't free globals: they're shared */
    plugin_vm->globals = NULL;
    /* cleanup plugin_vm manually */
    while (plugin_vm->sp > plugin_vm->stack) value_decref(*--plugin_vm->sp);
    free(plugin_vm);
    proto_free(proto);

    return xs_null();
}

static Value *vm_channel(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *ch = xs_map_new();
    Value *type = xs_str("channel");
    map_set(ch->map, "__type", type); value_decref(type);
    Value *buf = xs_array_new();
    map_set(ch->map, "_buf", buf); value_decref(buf);
    int64_t cap = (argc >= 1 && args[0]->tag == XS_INT) ? args[0]->i : 0;
    Value *cap_v = xs_int(cap);
    map_set(ch->map, "_cap", cap_v); value_decref(cap_v);
    return ch;
}

static Value *vm_is_int(Interp *interp, Value **args, int argc) {
    (void)interp;
    return xs_bool(argc >= 1 && (args[0]->tag == XS_INT || args[0]->tag == XS_BIGINT));
}
static Value *vm_is_float(Interp *interp, Value **args, int argc) {
    (void)interp;
    return xs_bool(argc >= 1 && args[0]->tag == XS_FLOAT);
}
static Value *vm_is_str(Interp *interp, Value **args, int argc) {
    (void)interp;
    return xs_bool(argc >= 1 && args[0]->tag == XS_STR);
}
static Value *vm_is_bool(Interp *interp, Value **args, int argc) {
    (void)interp;
    return xs_bool(argc >= 1 && args[0]->tag == XS_BOOL);
}
static Value *vm_is_array(Interp *interp, Value **args, int argc) {
    (void)interp;
    return xs_bool(argc >= 1 && args[0]->tag == XS_ARRAY);
}
static Value *vm_is_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_bool(0);
    ValueTag t = args[0]->tag;
    return xs_bool(t == XS_FUNC || t == XS_NATIVE || t == XS_CLOSURE);
}
static Value *vm_char_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_str("");
    Value *v = args[0];
    if (v->tag == XS_INT) {
        char buf[2] = { (char)v->i, '\0' };
        return xs_str(buf);
    }
    if (v->tag == XS_STR && v->s[0]) {
        char buf[2] = { v->s[0], '\0' };
        return xs_str(buf);
    }
    return xs_str("");
}
static Value *vm_ord(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_int(0);
    Value *v = args[0];
    if (v->tag == XS_STR) return xs_int((int64_t)(unsigned char)v->s[0]);
    if (v->tag == XS_INT) return xs_int(v->i);
    return xs_int(0);
}
static Value *vm_bytes(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *arr = xs_array_new();
    if (argc < 1) return arr;
    Value *v = args[0];
    if (v->tag == XS_STR) {
        for (const unsigned char *p = (const unsigned char *)v->s; *p; p++) {
            Value *b = xs_int((int64_t)*p);
            array_push(arr->arr, b);
        }
    }
    return arr;
}
static Value *vm_array_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *arr = xs_array_new();
    for (int j = 0; j < argc; j++)
        array_push(arr->arr, value_incref(args[j]));
    return arr;
}
static Value *vm_print_no_nl(Interp *interp, Value **args, int argc) {
    (void)interp;
    for (int j = 0; j < argc; j++) {
        if (j) printf(" ");
        char *s = value_str(args[j]);
        printf("%s", s); free(s);
    }
    fflush(stdout);
    return xs_null();
}
static Value *vm_pprint(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) { printf("null\n"); return xs_null(); }
    char *s = value_repr(args[0]);
    printf("%s\n", s); free(s);
    return xs_null();
}
static Value *vm_clear(Interp *interp, Value **args, int argc) {
    (void)interp; (void)args; (void)argc;
    printf("\033[2J\033[H");
    fflush(stdout);
    return xs_null();
}
static Value *vm_signal_fn(Interp *interp, Value **args, int argc) {
    (void)interp;
    XSSignal *sig = xs_calloc(1, sizeof(XSSignal));
    sig->value = (argc >= 1) ? value_incref(args[0]) : xs_null();
    sig->subscribers = NULL;
    sig->nsubs = 0;
    sig->subcap = 0;
    sig->compute = NULL;
    sig->notifying = 0;
    sig->refcount = 1;
    Value *v = xs_calloc(1, sizeof(Value));
    v->tag = XS_SIGNAL;
    v->refcount = 1;
    v->signal = sig;
    return v;
}
static Value *vm_derived(Interp *interp, Value **args, int argc) {
    (void)interp; (void)args; (void)argc;
    return xs_null();
}

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
    REG("assert",     vm_assert);
    REG("assert_eq",  vm_assert_eq);
    REG("panic",      vm_panic);
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
    REG("channel", vm_channel);
    REG("__load_plugin", vm_load_plugin);
    REG("is_int",      vm_is_int);
    REG("is_float",    vm_is_float);
    REG("is_str",      vm_is_str);
    REG("is_bool",     vm_is_bool);
    REG("is_array",    vm_is_array);
    REG("is_fn",       vm_is_fn);
    REG("i64",         vm_int_fn);
    REG("f64",         vm_float_fn);
    REG("char",        vm_char_fn);
    REG("chr",         vm_char_fn);
    REG("ord",         vm_ord);
    REG("bytes",       vm_bytes);
    REG("array",       vm_array_fn);
    REG("print_no_nl", vm_print_no_nl);
    REG("pprint",      vm_pprint);
    REG("clear",       vm_clear);
    REG("signal",      vm_signal_fn);
    REG("derived",     vm_derived);
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
    vm->eff_cont.valid = 0;
    /* skip detailed cleanup to avoid segfaults from complex state */
    if (vm->globals) { map_free(vm->globals); vm->globals = NULL; }
    free(vm);
    return;
#if 0
    upvalue_close_all(&vm->open_upvalues, vm->stack);
    while (vm->sp > vm->stack) { Value *v = *--vm->sp; if (v) value_decref(v); }
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
        vm->tasks[i].fn = NULL;
        vm->tasks[i].result = NULL;
    }
    vm->n_tasks = 0;
    if (vm->globals) map_free(vm->globals);
    free(vm);
#endif
}

static int call_frame_push(VM *vm, Value *closure_val, int argc) {
    if (vm->frame_count >= VM_FRAMES_MAX) {
        fprintf(stderr, "stack overflow\n"); return 1;
    }
    XSClosure *cl = closure_val->cl;
    int raw_arity = cl->proto->arity;
    int is_gen = 0;
    int is_variadic = 0;
    int arity = raw_arity;
    if (arity < 0) {
        /* could be generator (old encoding) or variadic (new encoding) */
        int decoded = -(arity + 1);
        /* check if proto has more locals than decoded: variadic indicator */
        if (cl->proto->nlocals > decoded + 1 || argc != decoded) {
            is_variadic = 1;
            arity = decoded;
        } else {
            is_gen = 1;
            arity = decoded;
        }
    }
    if (is_variadic) {
        /* arity = min required. Collect extra args into array for variadic param */
        if (argc < arity) {
            fprintf(stderr, "arity: expected at least %d got %d\n", arity, argc);
            return 1;
        }
        /* collect variadic args into an array */
        int n_extra = argc - arity;
        Value *varargs = xs_array_new();
        Value *tmp_extra[256];
        for (int i = n_extra - 1; i >= 0; i--) tmp_extra[i] = POP();
        for (int i = 0; i < n_extra; i++) {
            array_push(varargs->arr, tmp_extra[i]);
            value_decref(tmp_extra[i]);
        }
        PUSH(varargs);
        argc = arity + 1; /* required + 1 varargs array */
    } else {
        if (argc < arity) {
            /* fill missing args with null for default params */
            for (int i = argc; i < arity; i++) PUSH(value_incref(XS_NULL_VAL));
            argc = arity;
        } else if (argc > arity && !is_gen) {
            /* too many args: pop extras */
            for (int i = argc; i > arity; i--) value_decref(POP());
            argc = arity;
        }
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

static Value *vm_try_struct_op(VM *vm, Value *a, const char *op, Value *b) {
    if (a->tag != XS_STRUCT_VAL || !a->st || !a->st->type_name) return NULL;
    /* look up the operator method from globals: stored by impl under the op name */
    Value *fn = map_get(vm->globals, op);
    if (!fn || (fn->tag != XS_CLOSURE && fn->tag != XS_FUNC && fn->tag != XS_NATIVE))
        return NULL;
    Value *args[2] = { a, b };
    return vm_invoke(vm, fn, args, 2);
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
    g_plugin_vm = vm;
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
            } else if (a->tag == XS_STRUCT_VAL && (r = vm_try_struct_op(vm, a, "+", b)) != NULL) {
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
            } else if (a->tag == XS_STRUCT_VAL && (r = vm_try_struct_op(vm, a, "-", b)) != NULL) {
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
            } else if (a->tag == XS_STRUCT_VAL && (r = vm_try_struct_op(vm, a, "*", b)) != NULL) {
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
                if (b->i == 0) { r = value_incref(XS_NULL_VAL); }
                else r = xs_int(a->i / b->i);
            } else if ((a->tag==XS_INT||a->tag==XS_BIGINT) && (b->tag==XS_INT||b->tag==XS_BIGINT)) {
                r = xs_numeric_div(a, b);
            } else if (a->tag == XS_MAP && (r = vm_try_dunder(vm, a, "__div__", b)) != NULL) {
                /* dunder */
            } else {
                double bv = b->tag==XS_INT?(double)b->i:(b->tag==XS_BIGINT?bigint_to_double(b->bigint):b->f);
                if (bv == 0.0) { r = value_incref(XS_NULL_VAL); } else {
                double av = a->tag==XS_INT?(double)a->i:(a->tag==XS_BIGINT?bigint_to_double(a->bigint):a->f);
                r = xs_float(av / bv);
                }
            }
            value_decref(a); value_decref(b); PUSH(r); break;
        }
        case OP_MOD: {
            Value *b=POP(), *a=POP();
            Value *r;
            if (a->tag==XS_INT && b->tag==XS_INT) {
                if (b->i == 0) r = value_incref(XS_NULL_VAL);
                else r = xs_int(a->i % b->i);
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
            } else if ((col->tag==XS_ARRAY||col->tag==XS_TUPLE) && idx->tag==XS_RANGE && idx->range) {
                int64_t start = idx->range->start;
                int64_t end = idx->range->end;
                if (idx->range->inclusive) end++;
                if (start < 0) start += col->arr->len;
                if (end < 0) end += col->arr->len;
                if (start < 0) start = 0;
                if (end > col->arr->len) end = col->arr->len;
                Value *arr = xs_array_new();
                for (int64_t j = start; j < end; j++)
                    array_push(arr->arr, value_incref(col->arr->items[j]));
                r = arr;
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
            } else if (col->tag==XS_STR && idx->tag==XS_RANGE && idx->range) {
                const char *s = col->s;
                int64_t slen = (int64_t)strlen(s);
                int64_t start = idx->range->start;
                int64_t end = idx->range->end;
                if (idx->range->inclusive) end++;
                if (start < 0) start += slen;
                if (end < 0) end += slen;
                if (start < 0) start = 0;
                if (end > slen) end = slen;
                if (start >= end) { r = xs_str(""); }
                else {
                    int64_t len = end - start;
                    char *buf = malloc(len + 1);
                    memcpy(buf, s + start, len);
                    buf[len] = '\0';
                    r = xs_str(buf);
                    free(buf);
                }
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
            } else if ((obj->tag == XS_TUPLE || obj->tag == XS_ARRAY) && name[0] >= '0' && name[0] <= '9') {
                int64_t idx2 = atoll(name);
                if (idx2 >= 0 && idx2 < obj->arr->len)
                    r = value_incref(obj->arr->items[idx2]);
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
                    if (bases) {
                        map_set(inst->map, "__bases", value_incref(bases));
                        if (bases->tag == XS_ARRAY && bases->arr->len > 0) {
                            Value *super_inst = xs_map_new();
                            Value *base_cls = bases->arr->items[0];
                            if (base_cls->tag == XS_MAP) {
                                Value *bm = map_get(base_cls->map, "__methods");
                                if (bm && bm->tag == XS_MAP)
                                    for (int bj = 0; bj < bm->map->cap; bj++)
                                        if (bm->map->keys[bj])
                                            map_set(super_inst->map, bm->map->keys[bj],
                                                    value_incref(bm->map->vals[bj]));
                            }
                            map_set(super_inst->map, "__self", value_incref(inst));
                            map_set(inst->map, "super", super_inst);
                            value_decref(super_inst);
                        }
                    }
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
            int want_pairs = INSTR_A(instr);
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
                            if (want_pairs) {
                                r = xs_tuple_new();
                                Value *ks = xs_str(iter->map->keys[j]);
                                Value *val = iter->map->vals[j];
                                array_push(r->arr, ks);
                                array_push(r->arr, val ? val : XS_NULL_VAL);
                                value_decref(ks);
                            } else {
                                r = xs_str(iter->map->keys[j]);
                            }
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
                    Value *ch_type = map_get(mc_obj->map, "__type");
                    if (ch_type && ch_type->tag == XS_STR && strcmp(ch_type->s, "channel") == 0) {
                        Value *buf = map_get(mc_obj->map, "_buf");
                        mc_result = xs_int(buf && buf->tag == XS_ARRAY ? buf->arr->len : 0);
                    } else
                    mc_result=xs_int(mc_obj->map->len);
                } else if (strcmp(mc_name,"has")==0||strcmp(mc_name,"contains_key")==0) {
                    mc_result=(mc_argc>=1&&mc_args[0]->tag==XS_STR&&map_get(mc_obj->map,mc_args[0]->s))
                        ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"get")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    Value *v=map_get(mc_obj->map,mc_args[0]->s);
                    mc_result=v?value_incref(v):(mc_argc>=2?value_incref(mc_args[1]):value_incref(XS_NULL_VAL));
                } else if (strcmp(mc_name,"set")==0&&mc_argc>=2&&mc_args[0]->tag==XS_STR) {
                    /* check if this is a plugin global.set: delegate to native */
                    Value *set_fn = map_get(mc_obj->map, "set");
                    if (set_fn && set_fn->tag == XS_NATIVE) {
                        goto map_generic_method;
                    }
                    map_set(mc_obj->map,mc_args[0]->s,mc_args[1]);
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"delete")==0||strcmp(mc_name,"remove")==0) {
                    if(mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                        Value *nv = value_incref(XS_NULL_VAL);
                        map_set(mc_obj->map,mc_args[0]->s,nv);
                        value_decref(nv);
                    }
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"send")==0 && mc_argc>=1) {
                    Value *ch_type = map_get(mc_obj->map, "__type");
                    if (ch_type && ch_type->tag == XS_STR && strcmp(ch_type->s, "channel") == 0) {
                        Value *buf = map_get(mc_obj->map, "_buf");
                        if (buf && buf->tag == XS_ARRAY) array_push(buf->arr, value_incref(mc_args[0]));
                        mc_result = value_incref(XS_NULL_VAL);
                    } else goto map_generic_method;
                } else if (strcmp(mc_name,"recv")==0) {
                    Value *ch_type = map_get(mc_obj->map, "__type");
                    if (ch_type && ch_type->tag == XS_STR && strcmp(ch_type->s, "channel") == 0) {
                        Value *buf = map_get(mc_obj->map, "_buf");
                        if (buf && buf->tag == XS_ARRAY && buf->arr->len > 0) {
                            mc_result = value_incref(buf->arr->items[0]);
                            value_decref(buf->arr->items[0]);
                            for (int j = 1; j < buf->arr->len; j++) buf->arr->items[j-1] = buf->arr->items[j];
                            buf->arr->len--;
                        } else mc_result = value_incref(XS_NULL_VAL);
                    } else goto map_generic_method;
                } else if (strcmp(mc_name,"is_empty")==0) {
                    Value *ch_type = map_get(mc_obj->map, "__type");
                    if (ch_type && ch_type->tag == XS_STR && strcmp(ch_type->s, "channel") == 0) {
                        Value *buf = map_get(mc_obj->map, "_buf");
                        mc_result = xs_bool(buf && buf->tag == XS_ARRAY && buf->arr->len == 0);
                    } else {
                        mc_result = xs_bool(mc_obj->map->len == 0);
                    }
                } else if (strcmp(mc_name,"is_full")==0) {
                    Value *ch_type = map_get(mc_obj->map, "__type");
                    if (ch_type && ch_type->tag == XS_STR && strcmp(ch_type->s, "channel") == 0) {
                        Value *buf = map_get(mc_obj->map, "_buf");
                        Value *cap = map_get(mc_obj->map, "_cap");
                        int64_t c2 = (cap && cap->tag == XS_INT) ? cap->i : 0;
                        int full = (c2 > 0 && buf && buf->tag == XS_ARRAY && buf->arr->len >= (int)c2);
                        mc_result = xs_bool(full);
                    } else goto map_generic_method;
                } else if (strcmp(mc_name,"merge")==0&&mc_argc>=1&&mc_args[0]->tag==XS_MAP) {
                    for(int j=0;j<mc_args[0]->map->cap;j++){
                        if(mc_args[0]->map->keys[j])
                            map_set(mc_obj->map,mc_args[0]->map->keys[j],value_incref(mc_args[0]->map->vals[j]));
                    }
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"clone")==0||strcmp(mc_name,"copy")==0) {
                    Value *m=xs_map_new();
                    for(int j=0;j<mc_obj->map->cap;j++){
                        if(mc_obj->map->keys[j])
                            map_set(m->map,mc_obj->map->keys[j],value_incref(mc_obj->map->vals[j]));
                    }
                    mc_result=m;
                } else if (strcmp(mc_name,"items")==0) {
                    /* alias for entries */
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
                } else if (strcmp(mc_name,"has_key")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    mc_result=map_get(mc_obj->map,mc_args[0]->s)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"intersection")==0&&mc_argc>=1&&mc_args[0]->tag==XS_MAP) {
                    Value *m=xs_map_new();
                    for(int j=0;j<mc_obj->map->cap;j++){
                        if(mc_obj->map->keys[j]&&map_get(mc_args[0]->map,mc_obj->map->keys[j]))
                            map_set(m->map,mc_obj->map->keys[j],value_incref(mc_obj->map->vals[j]));
                    }
                    mc_result=m;
                } else if (strcmp(mc_name,"union")==0&&mc_argc>=1&&mc_args[0]->tag==XS_MAP) {
                    Value *m=xs_map_new();
                    for(int j=0;j<mc_obj->map->cap;j++){
                        if(mc_obj->map->keys[j])
                            map_set(m->map,mc_obj->map->keys[j],value_incref(mc_obj->map->vals[j]));
                    }
                    for(int j=0;j<mc_args[0]->map->cap;j++){
                        if(mc_args[0]->map->keys[j]&&!map_get(m->map,mc_args[0]->map->keys[j]))
                            map_set(m->map,mc_args[0]->map->keys[j],value_incref(mc_args[0]->map->vals[j]));
                    }
                    mc_result=m;
                } else if (strcmp(mc_name,"difference")==0&&mc_argc>=1&&mc_args[0]->tag==XS_MAP) {
                    Value *m=xs_map_new();
                    for(int j=0;j<mc_obj->map->cap;j++){
                        if(mc_obj->map->keys[j]&&!map_get(mc_args[0]->map,mc_obj->map->keys[j]))
                            map_set(m->map,mc_obj->map->keys[j],value_incref(mc_obj->map->vals[j]));
                    }
                    mc_result=m;
                } else if (strcmp(mc_name,"most_common")==0) {
                    int64_t n2=(mc_argc>=1&&mc_args[0]->tag==XS_INT)?mc_args[0]->i:mc_obj->map->len;
                    /* collect entries, sort by value descending, return top n */
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->map->cap;j++){
                        if(mc_obj->map->keys[j]){
                            Value *pair=xs_tuple_new();
                            array_push(pair->arr,xs_str(mc_obj->map->keys[j]));
                            array_push(pair->arr,value_incref(mc_obj->map->vals[j]));
                            array_push(arr->arr,pair);
                        }
                    }
                    /* bubble sort descending by second element */
                    int alen=arr->arr->len;
                    for(int j=0;j<alen-1;j++) for(int k=0;k<alen-1-j;k++){
                        Value *va=arr->arr->items[k]->arr->items[1];
                        Value *vb=arr->arr->items[k+1]->arr->items[1];
                        if(value_cmp(va,vb)<0){
                            Value *tmp=arr->arr->items[k]; arr->arr->items[k]=arr->arr->items[k+1]; arr->arr->items[k+1]=tmp;
                        }
                    }
                    if(n2<alen) arr->arr->len=(int)n2;
                    mc_result=arr;
                } else if (strcmp(mc_name,"elapsed")==0) {
                    Value *start_v = map_get(mc_obj->map, "_start");
                    if (start_v && (start_v->tag == XS_FLOAT || start_v->tag == XS_INT)) {
                        struct timespec _ts; clock_gettime(CLOCK_REALTIME, &_ts);
                        double now = (double)_ts.tv_sec + (double)_ts.tv_nsec/1e9;
                        double start = start_v->tag == XS_FLOAT ? start_v->f : (double)start_v->i;
                        mc_result = xs_float(now - start);
                    } else mc_result = value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"to_map")==0) {
                    mc_result=value_incref(mc_obj);
                } else if (strcmp(mc_name,"unwrap")==0) {
                    Value *tag_v = map_get(mc_obj->map, "_tag");
                    Value *val_v = map_get(mc_obj->map, "_val");
                    if (tag_v && tag_v->tag == XS_STR && strcmp(tag_v->s, "Err")==0) {
                        char *es = val_v ? value_str(val_v) : xs_strdup("Err");
                        fprintf(stderr, "unwrap called on Err: %s\n", es); free(es);
                        mc_result = value_incref(XS_NULL_VAL);
                    } else if (tag_v && tag_v->tag == XS_STR && strcmp(tag_v->s, "None")==0) {
                        fprintf(stderr, "unwrap called on None\n");
                        mc_result = value_incref(XS_NULL_VAL);
                    } else {
                        mc_result = val_v ? value_incref(val_v) : value_incref(XS_NULL_VAL);
                    }
                } else if (strcmp(mc_name,"unwrap_or")==0&&mc_argc>=1) {
                    Value *tag_v = map_get(mc_obj->map, "_tag");
                    Value *val_v = map_get(mc_obj->map, "_val");
                    int is_err = tag_v && tag_v->tag == XS_STR &&
                        (strcmp(tag_v->s,"Err")==0 || strcmp(tag_v->s,"None")==0);
                    mc_result = is_err ? value_incref(mc_args[0]) : (val_v ? value_incref(val_v) : value_incref(XS_NULL_VAL));
                } else if (strcmp(mc_name,"is_ok")==0) {
                    Value *tag_v = map_get(mc_obj->map, "_tag");
                    mc_result = xs_bool(tag_v && tag_v->tag == XS_STR && strcmp(tag_v->s,"Ok")==0);
                } else if (strcmp(mc_name,"is_err")==0) {
                    Value *tag_v = map_get(mc_obj->map, "_tag");
                    mc_result = xs_bool(tag_v && tag_v->tag == XS_STR && strcmp(tag_v->s,"Err")==0);
                } else if (strcmp(mc_name,"is_some")==0) {
                    Value *tag_v = map_get(mc_obj->map, "_tag");
                    mc_result = xs_bool(tag_v && tag_v->tag == XS_STR && strcmp(tag_v->s,"Some")==0);
                } else if (strcmp(mc_name,"is_none")==0) {
                    Value *tag_v = map_get(mc_obj->map, "_tag");
                    mc_result = xs_bool(tag_v && tag_v->tag == XS_STR && strcmp(tag_v->s,"None")==0);
                } else if (strcmp(mc_name,"ok")==0) {
                    Value *tag_v = map_get(mc_obj->map, "_tag");
                    Value *val_v = map_get(mc_obj->map, "_val");
                    if (tag_v && tag_v->tag == XS_STR && strcmp(tag_v->s,"Ok")==0)
                        mc_result = val_v ? value_incref(val_v) : value_incref(XS_NULL_VAL);
                    else mc_result = value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"or_else")==0&&mc_argc>=1) {
                    Value *tag_v = map_get(mc_obj->map, "_tag");
                    if (tag_v && tag_v->tag == XS_STR && strcmp(tag_v->s,"Err")==0) {
                        Value *val_v = map_get(mc_obj->map, "_val");
                        Value *arg = val_v ? val_v : XS_NULL_VAL;
                        mc_result = vm_invoke(vm, mc_args[0], &arg, 1);
                        frame = FRAME;
                        if (!mc_result) mc_result = value_incref(XS_NULL_VAL);
                    } else mc_result = value_incref(mc_obj);
                } else if (strcmp(mc_name,"map_err")==0&&mc_argc>=1) {
                    Value *tag_v = map_get(mc_obj->map, "_tag");
                    if (tag_v && tag_v->tag == XS_STR && strcmp(tag_v->s,"Err")==0) {
                        Value *val_v = map_get(mc_obj->map, "_val");
                        Value *arg = val_v ? val_v : XS_NULL_VAL;
                        Value *new_err = vm_invoke(vm, mc_args[0], &arg, 1);
                        frame = FRAME;
                        Value *m = xs_map_new();
                        Value *etag = xs_str("Err");
                        map_set(m->map, "_tag", etag); value_decref(etag);
                        if (new_err) { map_set(m->map, "_val", new_err); value_decref(new_err); }
                        mc_result = m;
                    } else mc_result = value_incref(mc_obj);
                } else if (strcmp(mc_name,"is_a")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    Value *type_name = map_get(mc_obj->map, "__type");
                    int match = type_name && type_name->tag == XS_STR && strcmp(type_name->s, mc_args[0]->s)==0;
                    mc_result = xs_bool(match);
                } else if (strcmp(mc_name,"subscribe")==0||strcmp(mc_name,"reset")==0||
                           strcmp(mc_name,"peek")==0||strcmp(mc_name,"step")==0||
                           strcmp(mc_name,"elapsed_ms")==0) {
                    mc_result = value_incref(XS_NULL_VAL);
                } else { map_generic_method: {
                    Value *fn = map_get(mc_obj->map, mc_name);
                    if (!fn) {
                        Value *methods = map_get(mc_obj->map, "__methods");
                        if (methods && methods->tag == XS_MAP)
                            fn = map_get(methods->map, mc_name);
                    }
                    if (!fn) {
                        Value *impl = map_get(mc_obj->map, "__impl__");
                        if (impl && impl->tag == XS_MAP)
                            fn = map_get(impl->map, mc_name);
                    }
                    /* look up methods on the type (for struct impl) */
                    if (!fn) {
                        Value *type_name = map_get(mc_obj->map, "__type");
                        if (type_name && type_name->tag == XS_STR) {
                            Value *type_val = map_get(vm->globals, type_name->s);
                            if (type_val && type_val->tag == XS_MAP) {
                                Value *tm = map_get(type_val->map, "__methods");
                                if (tm && tm->tag == XS_MAP)
                                    fn = map_get(tm->map, mc_name);
                                if (!fn) {
                                    Value *ti = map_get(type_val->map, "__impl__");
                                    if (ti && ti->tag == XS_MAP)
                                        fn = map_get(ti->map, mc_name);
                                }
                            }
                        }
                    }
                    if (fn && (fn->tag == XS_CLOSURE || fn->tag == XS_NATIVE)) {
                        int is_module_call = (mc_obj->tag == XS_MODULE) ||
                            (mc_obj->tag == XS_MAP && !map_get(mc_obj->map, "__type") &&
                             !map_get(mc_obj->map, "__methods") && !map_get(mc_obj->map, "__fields"));
                        /* Check if first param is 'self' for struct/class methods */
                        int needs_self = 0;
                        if (fn->tag == XS_CLOSURE) {
                            int fn_arity = fn->cl->proto->arity;
                            if (fn_arity < 0) fn_arity = -(fn_arity + 1);
                            needs_self = (fn_arity == mc_argc + 1);
                        }
                        if (fn->tag == XS_CLOSURE && needs_self && !is_module_call) {
                            /* super proxy: replace self with __self */
                            Value *self_ref = map_get(mc_obj->map, "__self");
                            if (self_ref) {
                                value_incref(self_ref);
                                value_decref(vm->sp[-mc_argc - 1]);
                                vm->sp[-mc_argc - 1] = self_ref;
                            }
                            /* self is on stack below args */
                            Value *fn_val = value_incref(fn);
                            if (call_frame_push(vm, fn_val, mc_argc + 1)) {
                                value_decref(fn_val); return 1;
                            }
                            value_decref(fn_val); frame = FRAME;
                            mc_called = 1;
                        } else if (fn->tag == XS_NATIVE) {
                            if (is_module_call) {
                                /* module call: don't pass module as self */
                                Value *r2 = fn->native(NULL, mc_args, mc_argc);
                                for (int j = 0; j < mc_argc; j++) value_decref(POP());
                                value_decref(POP());
                                PUSH(r2 ? r2 : value_incref(XS_NULL_VAL));
                            } else {
                                Value *nargs[17];
                                nargs[0] = mc_obj;
                                int total = 1 + mc_argc;
                                if (total > 17) total = 17;
                                for (int j = 0; j < mc_argc && j < 16; j++) nargs[j+1] = mc_args[j];
                                Value *r2 = fn->native(NULL, nargs, total);
                                for (int j = 0; j < mc_argc; j++) value_decref(POP());
                                value_decref(POP());
                                PUSH(r2 ? r2 : value_incref(XS_NULL_VAL));
                            }
                        } else {
                            /* closure without self: treat as plain call */
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
                }}
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
                } else if (strcmp(mc_name,"reverse")==0) {
                    char *r=xs_malloc((size_t)slen+1);
                    for(int j=0;j<slen;j++) r[j]=s[slen-1-j];
                    r[slen]='\0'; mc_result=xs_str(r); free(r);
                } else if (strcmp(mc_name,"join")==0&&mc_argc>=1) {
                    /* "sep".join(arr) */
                    if(mc_args[0]->tag==XS_ARRAY||mc_args[0]->tag==XS_TUPLE){
                        size_t cap=256; char *buf=xs_malloc(cap); size_t wpos=0;
                        for(int j=0;j<mc_args[0]->arr->len;j++){
                            char *sv=value_str(mc_args[0]->arr->items[j]); size_t svl=strlen(sv);
                            if(j>0){while(wpos+(size_t)slen+svl+2>cap){cap*=2;buf=xs_realloc(buf,cap);}memcpy(buf+wpos,s,(size_t)slen);wpos+=(size_t)slen;}
                            while(wpos+svl+2>cap){cap*=2;buf=xs_realloc(buf,cap);}
                            memcpy(buf+wpos,sv,svl); wpos+=svl; free(sv);
                        }
                        buf[wpos]='\0'; mc_result=xs_str(buf); free(buf);
                    } else mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"trim_start")==0||strcmp(mc_name,"ltrim")==0) {
                    const char *p2=s; while(*p2==' '||*p2=='\t'||*p2=='\n'||*p2=='\r') p2++;
                    mc_result=xs_str(p2);
                } else if (strcmp(mc_name,"trim_end")==0||strcmp(mc_name,"rtrim")==0) {
                    char *r=xs_strdup(s); int rlen=(int)strlen(r);
                    while(rlen>0&&(r[rlen-1]==' '||r[rlen-1]=='\t'||r[rlen-1]=='\n'||r[rlen-1]=='\r')) rlen--;
                    r[rlen]='\0'; mc_result=xs_str(r); free(r);
                } else if (strcmp(mc_name,"title")==0) {
                    char *r=xs_strdup(s); int prev_space=1;
                    for(int j=0;r[j];j++){
                        if(r[j]==' '||r[j]=='\t'||r[j]=='\n'||r[j]=='\r'){prev_space=1;}
                        else if(prev_space){r[j]=(char)toupper((unsigned char)r[j]);prev_space=0;}
                        else{r[j]=(char)tolower((unsigned char)r[j]);}
                    }
                    mc_result=xs_str(r); free(r);
                } else if (strcmp(mc_name,"center")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t n2=mc_args[0]->i; char ch=' ';
                    if(mc_argc>=2&&mc_args[1]->tag==XS_STR&&mc_args[1]->s[0]) ch=mc_args[1]->s[0];
                    if(n2<=(int64_t)slen) mc_result=xs_str(s);
                    else{
                        int64_t total_pad=n2-(int64_t)slen;
                        int64_t lpad=total_pad/2, rpad=total_pad-lpad;
                        char *buf=xs_malloc((size_t)n2+1);
                        for(int64_t j=0;j<lpad;j++) buf[j]=ch;
                        memcpy(buf+lpad,s,(size_t)slen);
                        for(int64_t j=0;j<rpad;j++) buf[lpad+(int64_t)slen+j]=ch;
                        buf[n2]='\0'; mc_result=xs_str(buf); free(buf);
                    }
                } else if (strcmp(mc_name,"char_at")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t idx=mc_args[0]->i;
                    if(idx<0) idx+=slen;
                    if(idx>=0&&idx<(int64_t)slen){char b[2]={s[idx],0};mc_result=xs_str(b);}
                    else mc_result=xs_str("");
                } else if (strcmp(mc_name,"lines")==0) {
                    Value *arr=xs_array_new(); const char *p2=s;
                    while(1){
                        const char *nl=strchr(p2,'\n');
                        if(!nl){Value *cv=xs_str(p2);array_push(arr->arr,cv);value_decref(cv);break;}
                        size_t chunk=(size_t)(nl-p2);
                        /* strip trailing \r */
                        size_t clen=chunk; if(clen>0&&p2[clen-1]=='\r') clen--;
                        char *b=xs_malloc(clen+1); memcpy(b,p2,clen); b[clen]='\0';
                        Value *cv=xs_str(b); free(b); array_push(arr->arr,cv); value_decref(cv);
                        p2=nl+1;
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"is_empty")==0) {
                    mc_result=slen==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"is_ascii")==0) {
                    int ok=1; for(int j=0;j<slen;j++) if((unsigned char)s[j]>=128){ok=0;break;}
                    mc_result=ok?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"is_digit")==0||strcmp(mc_name,"is_numeric")==0) {
                    int ok=(slen>0); for(int j=0;j<slen;j++) if(!isdigit((unsigned char)s[j])){ok=0;break;}
                    mc_result=ok?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"is_alpha")==0) {
                    int ok=(slen>0); for(int j=0;j<slen;j++) if(!isalpha((unsigned char)s[j])){ok=0;break;}
                    mc_result=ok?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"is_alnum")==0) {
                    int ok=(slen>0); for(int j=0;j<slen;j++) if(!isalnum((unsigned char)s[j])){ok=0;break;}
                    mc_result=ok?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"is_upper")==0) {
                    int ok=(slen>0); for(int j=0;j<slen;j++) if(isalpha((unsigned char)s[j])&&!isupper((unsigned char)s[j])){ok=0;break;}
                    mc_result=ok?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"is_lower")==0) {
                    int ok=(slen>0); for(int j=0;j<slen;j++) if(isalpha((unsigned char)s[j])&&!islower((unsigned char)s[j])){ok=0;break;}
                    mc_result=ok?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"remove_prefix")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    size_t pl=strlen(mc_args[0]->s);
                    if(pl>0&&strncmp(s,mc_args[0]->s,pl)==0) mc_result=xs_str(s+pl);
                    else mc_result=xs_str(s);
                } else if (strcmp(mc_name,"remove_suffix")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    size_t pl=strlen(mc_args[0]->s);
                    if(pl>0&&(size_t)slen>=pl&&strcmp(s+slen-pl,mc_args[0]->s)==0){
                        char *r=xs_malloc(slen-pl+1); memcpy(r,s,slen-pl); r[slen-pl]='\0';
                        mc_result=xs_str(r); free(r);
                    } else mc_result=xs_str(s);
                } else if (strcmp(mc_name,"truncate")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t n2=mc_args[0]->i; if(n2<0) n2=0;
                    if((int64_t)slen<=n2) mc_result=xs_str(s);
                    else{
                        size_t tlen=(size_t)(n2>3?n2-3:n2);
                        char *buf=xs_malloc(tlen+4);
                        memcpy(buf,s,tlen);
                        if(n2>3){memcpy(buf+tlen,"...",3);buf[tlen+3]='\0';}
                        else{buf[tlen]='\0';}
                        mc_result=xs_str(buf); free(buf);
                    }
                } else if (strcmp(mc_name,"substr")==0||strcmp(mc_name,"substring")==0) {
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
                } else if (strcmp(mc_name,"to_str")==0||strcmp(mc_name,"to_string")==0) {
                    mc_result=value_incref(mc_obj);
                } else if (strcmp(mc_name,"startswith")==0) {
                    if(mc_argc>=1&&mc_args[0]->tag==XS_STR){
                        size_t pl=strlen(mc_args[0]->s);
                        mc_result=strncmp(s,mc_args[0]->s,pl)==0
                            ?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                    } else mc_result=value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"endswith")==0) {
                    if(mc_argc>=1&&mc_args[0]->tag==XS_STR){
                        size_t pl=strlen(mc_args[0]->s);
                        mc_result=(size_t)slen>=pl&&strcmp(s+slen-pl,mc_args[0]->s)==0
                            ?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                    } else mc_result=value_incref(XS_FALSE_VAL);
                } else if (strcmp(mc_name,"rfind")==0) {
                    mc_result=xs_int(-1);
                    if(mc_argc>=1&&mc_args[0]->tag==XS_STR){
                        size_t sl2=strlen(mc_args[0]->s);
                        if(sl2>0){
                            for(int j=slen-(int)sl2;j>=0;j--){
                                if(strncmp(s+j,mc_args[0]->s,sl2)==0){value_decref(mc_result);mc_result=xs_int(j);break;}
                            }
                        }
                    }
                } else if (strcmp(mc_name,"split_at")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t idx=mc_args[0]->i;
                    if(idx<0) idx+=slen;
                    if(idx<0) idx=0;
                    if(idx>(int64_t)slen) idx=(int64_t)slen;
                    Value *arr=xs_array_new();
                    char *a=xs_malloc((size_t)idx+1); memcpy(a,s,(size_t)idx); a[idx]='\0';
                    Value *va=xs_str(a); free(a); array_push(arr->arr,va); value_decref(va);
                    Value *vb=xs_str(s+idx); array_push(arr->arr,vb); value_decref(vb);
                    mc_result=arr;
                } else if (strcmp(mc_name,"to_chars")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<slen;j++){char b[2]={s[j],0};Value*cv=xs_str(b);array_push(arr->arr,cv);value_decref(cv);}
                    mc_result=arr;
                } else if (strcmp(mc_name,"to_bytes")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<slen;j++) array_push(arr->arr,xs_int((unsigned char)s[j]));
                    mc_result=arr;
                } else if (strcmp(mc_name,"length")==0) {
                    mc_result=xs_int(slen);
                } else if (strcmp(mc_name,"lpad")==0||strcmp(mc_name,"pad_start")==0) {
                    int64_t n2=(mc_argc>=1&&mc_args[0]->tag==XS_INT)?mc_args[0]->i:(int64_t)slen;
                    char ch=' ';
                    if(mc_argc>=2&&mc_args[1]->tag==XS_STR&&mc_args[1]->s[0]) ch=mc_args[1]->s[0];
                    if(n2<=(int64_t)slen) mc_result=xs_str(s);
                    else{
                        char *buf=xs_malloc((size_t)n2+1);
                        int64_t pad=n2-(int64_t)slen;
                        for(int64_t j=0;j<pad;j++) buf[j]=ch;
                        memcpy(buf+pad,s,(size_t)slen); buf[n2]='\0';
                        mc_result=xs_str(buf); free(buf);
                    }
                } else if (strcmp(mc_name,"rpad")==0||strcmp(mc_name,"pad_end")==0) {
                    int64_t n2=(mc_argc>=1&&mc_args[0]->tag==XS_INT)?mc_args[0]->i:(int64_t)slen;
                    char ch=' ';
                    if(mc_argc>=2&&mc_args[1]->tag==XS_STR&&mc_args[1]->s[0]) ch=mc_args[1]->s[0];
                    if(n2<=(int64_t)slen) mc_result=xs_str(s);
                    else{
                        char *buf=xs_malloc((size_t)n2+1);
                        memcpy(buf,s,(size_t)slen);
                        for(int64_t j=(int64_t)slen;j<n2;j++) buf[j]=ch;
                        buf[n2]='\0'; mc_result=xs_str(buf); free(buf);
                    }
                } else if (strcmp(mc_name,"reversed")==0) {
                    char *r=xs_malloc((size_t)slen+1);
                    for(int j=0;j<slen;j++) r[j]=s[slen-1-j];
                    r[slen]='\0'; mc_result=xs_str(r); free(r);
                } else if (strcmp(mc_name,"find")==0) {
                    mc_result=xs_int(-1);
                    if(mc_argc>=1&&mc_args[0]->tag==XS_STR){
                        const char *fnd=strstr(s,mc_args[0]->s);
                        if(fnd){value_decref(mc_result);mc_result=xs_int((int64_t)(fnd-s));}
                    }
                } else if (strcmp(mc_name,"format")==0) {
                    /* simple sprintf-style: replace %s/%d/%f with args in order */
                    size_t cap=strlen(s)*2+64; char *buf=xs_malloc(cap); size_t wpos=0;
                    const char *p2=s; int ai=0;
                    while(*p2){
                        if(*p2=='%'&&*(p2+1)){
                            char spec=*(p2+1);
                            if(spec=='s'||spec=='d'||spec=='f'||spec=='i'){
                                char tmp[64]; tmp[0]='\0';
                                if(ai<mc_argc){
                                    if(spec=='s'){
                                        char *sv=value_str(mc_args[ai]);
                                        size_t svl=strlen(sv);
                                        while(wpos+svl+1>cap){cap*=2;buf=xs_realloc(buf,cap);}
                                        memcpy(buf+wpos,sv,svl); wpos+=svl; free(sv); ai++;
                                        p2+=2; continue;
                                    } else if((spec=='d'||spec=='i')&&mc_args[ai]->tag==XS_INT){
                                        snprintf(tmp,sizeof(tmp),"%lld",(long long)mc_args[ai]->i); ai++;
                                    } else if(spec=='f'&&mc_args[ai]->tag==XS_FLOAT){
                                        snprintf(tmp,sizeof(tmp),"%g",mc_args[ai]->f); ai++;
                                    } else if((spec=='d'||spec=='i')&&mc_args[ai]->tag==XS_FLOAT){
                                        snprintf(tmp,sizeof(tmp),"%lld",(long long)mc_args[ai]->f); ai++;
                                    } else if(spec=='f'&&mc_args[ai]->tag==XS_INT){
                                        snprintf(tmp,sizeof(tmp),"%g",(double)mc_args[ai]->i); ai++;
                                    }
                                }
                                size_t tl=strlen(tmp);
                                while(wpos+tl+1>cap){cap*=2;buf=xs_realloc(buf,cap);}
                                memcpy(buf+wpos,tmp,tl); wpos+=tl; p2+=2; continue;
                            }
                        }
                        while(wpos+2>cap){cap*=2;buf=xs_realloc(buf,cap);}
                        buf[wpos++]=*p2++;
                    }
                    buf[wpos]='\0'; mc_result=xs_str(buf); free(buf);
                } else if (strcmp(mc_name,"as_int")==0)
                    mc_result=xs_int(atoll(s));
                else if (strcmp(mc_name,"as_float")==0)
                    mc_result=xs_float(atof(s));
                else if (strcmp(mc_name,"as_str")==0)
                    mc_result=value_incref(mc_obj);
                else if (strcmp(mc_name,"parse")==0) {
                    int base=(mc_argc>=1&&mc_args[0]->tag==XS_INT)?(int)mc_args[0]->i:10;
                    mc_result=xs_int((int64_t)strtoll(s,NULL,base));
                } else if (strcmp(mc_name,"from_chars")==0) {
                    /* join chars into string: just return self if called on a string */
                    mc_result=value_incref(mc_obj);
                } else if (strcmp(mc_name,"is_a")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    mc_result=xs_bool(strcmp(mc_args[0]->s,"str")==0||strcmp(mc_args[0]->s,"String")==0);
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
                } else if (strcmp(mc_name,"is_empty")==0) {
                    mc_result=xs_bool(mc_obj->arr->len==0);
                } else if (strcmp(mc_name,"each")==0||strcmp(mc_name,"for_each")==0) {
                    if(mc_argc>=1){
                        for(int j=0;j<mc_obj->arr->len;j++){
                            Value *r=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1);
                            value_decref(r);
                        }
                        frame=FRAME;
                    }
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"take")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t n2=mc_args[0]->i; if(n2<0) n2=0;
                    if(n2>mc_obj->arr->len) n2=mc_obj->arr->len;
                    Value *arr=xs_array_new();
                    for(int64_t j=0;j<n2;j++) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    mc_result=arr;
                } else if (strcmp(mc_name,"drop")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t n2=mc_args[0]->i; if(n2<0) n2=0;
                    if(n2>mc_obj->arr->len) n2=mc_obj->arr->len;
                    Value *arr=xs_array_new();
                    for(int64_t j=n2;j<mc_obj->arr->len;j++) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    mc_result=arr;
                } else if (strcmp(mc_name,"take_while")==0&&mc_argc>=1) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *r=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1);
                        int ok=value_truthy(r); value_decref(r);
                        if(!ok) break;
                        array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    }
                    frame=FRAME;
                    mc_result=arr;
                } else if (strcmp(mc_name,"drop_while")==0&&mc_argc>=1) {
                    Value *arr=xs_array_new();
                    int dropping=1;
                    for(int j=0;j<mc_obj->arr->len;j++){
                        if(dropping){
                            Value *r=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1);
                            dropping=value_truthy(r); value_decref(r);
                        }
                        if(!dropping) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    }
                    frame=FRAME;
                    mc_result=arr;
                } else if (strcmp(mc_name,"flat_map")==0&&mc_argc>=1) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *r=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1);
                        if(r&&(r->tag==XS_ARRAY||r->tag==XS_TUPLE)){
                            for(int k=0;k<r->arr->len;k++) array_push(arr->arr,value_incref(r->arr->items[k]));
                            value_decref(r);
                        } else if(r) { array_push(arr->arr,r); }
                        else { array_push(arr->arr,value_incref(XS_NULL_VAL)); }
                    }
                    frame=FRAME;
                    mc_result=arr;
                } else if (strcmp(mc_name,"zip")==0&&mc_argc>=1&&(mc_args[0]->tag==XS_ARRAY||mc_args[0]->tag==XS_TUPLE)) {
                    Value *arr=xs_array_new();
                    int n2=mc_obj->arr->len<mc_args[0]->arr->len?mc_obj->arr->len:mc_args[0]->arr->len;
                    for(int j=0;j<n2;j++){
                        Value *pair=xs_tuple_new();
                        array_push(pair->arr,value_incref(mc_obj->arr->items[j]));
                        array_push(pair->arr,value_incref(mc_args[0]->arr->items[j]));
                        array_push(arr->arr,pair);
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"zip_with")==0&&mc_argc>=2&&(mc_args[0]->tag==XS_ARRAY||mc_args[0]->tag==XS_TUPLE)) {
                    Value *arr=xs_array_new();
                    int n2=mc_obj->arr->len<mc_args[0]->arr->len?mc_obj->arr->len:mc_args[0]->arr->len;
                    for(int j=0;j<n2;j++){
                        Value *pair[2]={mc_obj->arr->items[j],mc_args[0]->arr->items[j]};
                        Value *r=vm_invoke(vm,mc_args[1],pair,2);
                        array_push(arr->arr,r?r:value_incref(XS_NULL_VAL));
                    }
                    frame=FRAME;
                    mc_result=arr;
                } else if (strcmp(mc_name,"partition")==0&&mc_argc>=1) {
                    Value *yes=xs_array_new(), *no=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *r=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1);
                        if(value_truthy(r)) array_push(yes->arr,value_incref(mc_obj->arr->items[j]));
                        else array_push(no->arr,value_incref(mc_obj->arr->items[j]));
                        value_decref(r);
                    }
                    frame=FRAME;
                    Value *parr=xs_array_new();
                    array_push(parr->arr,yes); array_push(parr->arr,no);
                    mc_result=parr;
                } else if (strcmp(mc_name,"group_by")==0&&mc_argc>=1) {
                    Value *m=xs_map_new();
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *k=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1);
                        if(!k) k=value_incref(XS_NULL_VAL);
                        char *ks=value_str(k); value_decref(k);
                        Value *bucket=map_get(m->map,ks);
                        if(!bucket){bucket=xs_array_new();map_set(m->map,ks,bucket);value_decref(bucket);bucket=map_get(m->map,ks);}
                        array_push(bucket->arr,value_incref(mc_obj->arr->items[j]));
                        free(ks);
                    }
                    frame=FRAME;
                    mc_result=m;
                } else if (strcmp(mc_name,"sort_by")==0&&mc_argc>=1) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    {
                        int slen2=arr->arr->len;
                        Value **skeys=(Value**)xs_malloc(sizeof(Value*)*(size_t)(slen2>0?slen2:1));
                        for(int j=0;j<slen2;j++){skeys[j]=vm_invoke(vm,mc_args[0],&arr->arr->items[j],1);if(!skeys[j])skeys[j]=value_incref(XS_NULL_VAL);}
                        frame=FRAME;
                        for(int j=0;j<slen2-1;j++) for(int k=0;k<slen2-1-j;k++){
                            if(value_cmp(skeys[k],skeys[k+1])>0){
                                Value *tv=arr->arr->items[k]; arr->arr->items[k]=arr->arr->items[k+1]; arr->arr->items[k+1]=tv;
                                Value *tk=skeys[k]; skeys[k]=skeys[k+1]; skeys[k+1]=tk;
                            }
                        }
                        for(int j=0;j<slen2;j++) value_decref(skeys[j]);
                        free(skeys);
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"min_by")==0&&mc_argc>=1) {
                    if(mc_obj->arr->len==0){mc_result=value_incref(XS_NULL_VAL);}
                    else{
                        Value *best=mc_obj->arr->items[0];
                        Value *bkey=vm_invoke(vm,mc_args[0],&best,1); if(!bkey) bkey=value_incref(XS_NULL_VAL);
                        for(int j=1;j<mc_obj->arr->len;j++){
                            Value *k=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1); if(!k) k=value_incref(XS_NULL_VAL);
                            if(value_cmp(k,bkey)<0){value_decref(bkey);bkey=k;best=mc_obj->arr->items[j];}
                            else value_decref(k);
                        }
                        value_decref(bkey);
                        frame=FRAME;
                        mc_result=value_incref(best);
                    }
                } else if (strcmp(mc_name,"max_by")==0&&mc_argc>=1) {
                    if(mc_obj->arr->len==0){mc_result=value_incref(XS_NULL_VAL);}
                    else{
                        Value *best=mc_obj->arr->items[0];
                        Value *bkey=vm_invoke(vm,mc_args[0],&best,1); if(!bkey) bkey=value_incref(XS_NULL_VAL);
                        for(int j=1;j<mc_obj->arr->len;j++){
                            Value *k=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1); if(!k) k=value_incref(XS_NULL_VAL);
                            if(value_cmp(k,bkey)>0){value_decref(bkey);bkey=k;best=mc_obj->arr->items[j];}
                            else value_decref(k);
                        }
                        value_decref(bkey);
                        frame=FRAME;
                        mc_result=value_incref(best);
                    }
                } else if (strcmp(mc_name,"dedup")==0) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++){
                        if(j==0||!value_equal(mc_obj->arr->items[j],mc_obj->arr->items[j-1]))
                            array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"intersperse")==0&&mc_argc>=1) {
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;j++){
                        if(j>0) array_push(arr->arr,value_incref(mc_args[0]));
                        array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"window")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t n2=mc_args[0]->i; Value *arr=xs_array_new();
                    if(n2>0){
                        for(int j=0;j<=mc_obj->arr->len-(int)n2;j++){
                            Value *win=xs_array_new();
                            for(int64_t k=0;k<n2;k++) array_push(win->arr,value_incref(mc_obj->arr->items[j+k]));
                            array_push(arr->arr,win);
                        }
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"chunk")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t n2=mc_args[0]->i; if(n2<1) n2=1;
                    Value *arr=xs_array_new();
                    for(int j=0;j<mc_obj->arr->len;){
                        Value *ch=xs_array_new();
                        for(int64_t k=0;k<n2&&j<mc_obj->arr->len;k++,j++) array_push(ch->arr,value_incref(mc_obj->arr->items[j]));
                        array_push(arr->arr,ch);
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"rotate")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int alen=mc_obj->arr->len;
                    Value *arr=xs_array_new();
                    if(alen>0){
                        int64_t n2=mc_args[0]->i%alen;
                        if(n2<0) n2+=alen;
                        for(int j=(int)n2;j<alen;j++) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                        for(int j=0;j<(int)n2;j++) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"sample")==0) {
                    int64_t n2=(mc_argc>=1&&mc_args[0]->tag==XS_INT)?mc_args[0]->i:1;
                    if(n2<0) n2=0;
                    if(n2>mc_obj->arr->len) n2=mc_obj->arr->len;
                    Value *arr=xs_array_new();
                    for(int64_t j=0;j<n2;j++) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    mc_result=arr;
                } else if (strcmp(mc_name,"product")==0) {
                    int64_t pi=1; double pf=1.0; int is_float=0;
                    for(int j=0;j<mc_obj->arr->len;j++){
                        if(mc_obj->arr->items[j]->tag==XS_INT) pi*=mc_obj->arr->items[j]->i;
                        else if(mc_obj->arr->items[j]->tag==XS_FLOAT){pf*=mc_obj->arr->items[j]->f;is_float=1;}
                    }
                    mc_result=is_float?xs_float(pf*(double)pi):xs_int(pi);
                } else if (strcmp(mc_name,"frequencies")==0) {
                    Value *m=xs_map_new();
                    for(int j=0;j<mc_obj->arr->len;j++){
                        char *ks=value_str(mc_obj->arr->items[j]);
                        Value *cur=map_get(m->map,ks);
                        int64_t cnt=cur&&cur->tag==XS_INT?cur->i:0;
                        Value *nv=xs_int(cnt+1); map_set(m->map,ks,nv); value_decref(nv);
                        free(ks);
                    }
                    mc_result=m;
                } else if (strcmp(mc_name,"reversed")==0) {
                    Value *arr=xs_array_new();
                    for(int j=mc_obj->arr->len-1;j>=0;j--) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    mc_result=arr;
                } else if (strcmp(mc_name,"scan")==0&&mc_argc>=2) {
                    /* scan(init, fn): init first, fn second, matching interpreter */
                    Value *acc=value_incref(mc_args[0]);
                    Value *arr=xs_array_new();
                    array_push(arr->arr,value_incref(acc));
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *pair[2]={acc,mc_obj->arr->items[j]};
                        Value *r=vm_invoke(vm,mc_args[1],pair,2);
                        value_decref(acc); acc=r?r:value_incref(XS_NULL_VAL);
                        array_push(arr->arr,value_incref(acc));
                    }
                    value_decref(acc);
                    frame=FRAME;
                    mc_result=arr;
                } else if (strcmp(mc_name,"prepend")==0&&mc_argc>=1) {
                    array_push(mc_obj->arr,value_incref(XS_NULL_VAL));
                    for(int k=mc_obj->arr->len-1;k>0;k--) mc_obj->arr->items[k]=mc_obj->arr->items[k-1];
                    mc_obj->arr->items[0]=value_incref(mc_args[0]);
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"clear")==0) {
                    for(int j=0;j<mc_obj->arr->len;j++) value_decref(mc_obj->arr->items[j]);
                    mc_obj->arr->len=0;
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"push_back")==0||strcmp(mc_name,"add")==0) {
                    for(int j=0;j<mc_argc;j++) array_push(mc_obj->arr,value_incref(mc_args[j]));
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"push_front")==0) {
                    for(int j=0;j<mc_argc;j++){
                        array_push(mc_obj->arr,value_incref(XS_NULL_VAL));
                        for(int k=mc_obj->arr->len-1;k>0;k--) mc_obj->arr->items[k]=mc_obj->arr->items[k-1];
                        mc_obj->arr->items[0]=value_incref(mc_args[j]);
                    }
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"pop_back")==0) {
                    if(mc_obj->arr->len>0){
                        mc_result=value_incref(mc_obj->arr->items[mc_obj->arr->len-1]);
                        value_decref(mc_obj->arr->items[--mc_obj->arr->len]);
                    } else mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"pop_front")==0) {
                    if(mc_obj->arr->len>0){
                        mc_result=value_incref(mc_obj->arr->items[0]);
                        value_decref(mc_obj->arr->items[0]);
                        for(int j=1;j<mc_obj->arr->len;j++) mc_obj->arr->items[j-1]=mc_obj->arr->items[j];
                        mc_obj->arr->len--;
                    } else mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"extend")==0&&mc_argc>=1&&(mc_args[0]->tag==XS_ARRAY||mc_args[0]->tag==XS_TUPLE)) {
                    for(int j=0;j<mc_args[0]->arr->len;j++) array_push(mc_obj->arr,value_incref(mc_args[0]->arr->items[j]));
                    mc_result=value_incref(XS_NULL_VAL);
                } else if (strcmp(mc_name,"shuffle")==0) {
                    int n2=mc_obj->arr->len;
                    for(int j=n2-1;j>0;j--){
                        int k=rand()%(j+1);
                        Value *tmp=mc_obj->arr->items[j]; mc_obj->arr->items[j]=mc_obj->arr->items[k]; mc_obj->arr->items[k]=tmp;
                    }
                    mc_result=value_incref(mc_obj);
                } else if (strcmp(mc_name,"skip")==0&&mc_argc>=1&&mc_args[0]->tag==XS_INT) {
                    int64_t n2=mc_args[0]->i; if(n2<0) n2=0;
                    if(n2>mc_obj->arr->len) n2=mc_obj->arr->len;
                    Value *arr=xs_array_new();
                    for(int64_t j=n2;j<mc_obj->arr->len;j++) array_push(arr->arr,value_incref(mc_obj->arr->items[j]));
                    mc_result=arr;
                } else if (strcmp(mc_name,"to_array")==0) {
                    mc_result=value_incref(mc_obj);
                } else if (strcmp(mc_name,"total")==0) {
                    int64_t si=0; double sf=0; int is_float=0;
                    for(int j=0;j<mc_obj->arr->len;j++){
                        if(mc_obj->arr->items[j]->tag==XS_INT) si+=mc_obj->arr->items[j]->i;
                        else if(mc_obj->arr->items[j]->tag==XS_FLOAT){sf+=mc_obj->arr->items[j]->f;is_float=1;}
                    }
                    mc_result=is_float?xs_float(sf+(double)si):xs_int(si);
                } else if (strcmp(mc_name,"sum_by")==0&&mc_argc>=1) {
                    int64_t si=0; double sf=0; int is_float=0;
                    for(int j=0;j<mc_obj->arr->len;j++){
                        Value *r=vm_invoke(vm,mc_args[0],&mc_obj->arr->items[j],1);
                        if(r&&r->tag==XS_INT) si+=r->i;
                        else if(r&&r->tag==XS_FLOAT){sf+=r->f;is_float=1;}
                        value_decref(r);
                    }
                    frame=FRAME;
                    mc_result=is_float?xs_float(sf+(double)si):xs_int(si);
                } else if (strcmp(mc_name,"from_chars")==0) {
                    /* join array of chars into a string */
                    size_t cap=256; char *buf=xs_malloc(cap); size_t wpos=0;
                    for(int j=0;j<mc_obj->arr->len;j++){
                        if(mc_obj->arr->items[j]->tag==XS_STR){
                            size_t cl=strlen(mc_obj->arr->items[j]->s);
                            while(wpos+cl+1>cap){cap*=2;buf=xs_realloc(buf,cap);}
                            memcpy(buf+wpos,mc_obj->arr->items[j]->s,cl); wpos+=cl;
                        }
                    }
                    buf[wpos]='\0'; mc_result=xs_str(buf); free(buf);
                } else if (strcmp(mc_name,"is_a")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    mc_result=xs_bool(strcmp(mc_args[0]->s,"array")==0||strcmp(mc_args[0]->s,"Array")==0||strcmp(mc_args[0]->s,"List")==0);
                } else mc_result=value_incref(XS_NULL_VAL);
            }
            else if (mc_obj->tag==XS_INT||mc_obj->tag==XS_FLOAT) {
                double num_f=(mc_obj->tag==XS_FLOAT)?mc_obj->f:(double)mc_obj->i;
                int64_t num_i=(mc_obj->tag==XS_INT)?mc_obj->i:(int64_t)mc_obj->f;
                if (strcmp(mc_name,"is_even")==0)
                    mc_result=mc_obj->tag==XS_INT?xs_bool(mc_obj->i%2==0):value_incref(XS_FALSE_VAL);
                else if (strcmp(mc_name,"is_odd")==0)
                    mc_result=mc_obj->tag==XS_INT?xs_bool(mc_obj->i%2!=0):value_incref(XS_FALSE_VAL);
                else if (strcmp(mc_name,"is_nan")==0)
                    mc_result=mc_obj->tag==XS_FLOAT?xs_bool(isnan(mc_obj->f)):value_incref(XS_FALSE_VAL);
                else if (strcmp(mc_name,"is_inf")==0)
                    mc_result=mc_obj->tag==XS_FLOAT?xs_bool(isinf(mc_obj->f)):value_incref(XS_FALSE_VAL);
                else if (strcmp(mc_name,"abs")==0) {
                    if(mc_obj->tag==XS_INT) mc_result=xs_int(mc_obj->i<0?-mc_obj->i:mc_obj->i);
                    else mc_result=xs_float(fabs(mc_obj->f));
                } else if (strcmp(mc_name,"sign")==0) {
                    if(mc_obj->tag==XS_INT) mc_result=xs_int(mc_obj->i>0?1:(mc_obj->i<0?-1:0));
                    else mc_result=xs_int(mc_obj->f>0.0?1:(mc_obj->f<0.0?-1:0));
                } else if (strcmp(mc_name,"clamp")==0&&mc_argc>=2) {
                    double lo=(mc_args[0]->tag==XS_FLOAT)?mc_args[0]->f:(double)mc_args[0]->i;
                    double hi=(mc_args[1]->tag==XS_FLOAT)?mc_args[1]->f:(double)mc_args[1]->i;
                    if(mc_obj->tag==XS_INT){
                        int64_t loi=(int64_t)lo,hii=(int64_t)hi;
                        int64_t v=mc_obj->i<loi?loi:(mc_obj->i>hii?hii:mc_obj->i);
                        mc_result=xs_int(v);
                    } else {
                        double v=num_f<lo?lo:(num_f>hi?hi:num_f);
                        mc_result=xs_float(v);
                    }
                } else if (strcmp(mc_name,"to_str")==0||strcmp(mc_name,"to_string")==0) {
                    char buf[64];
                    if(mc_obj->tag==XS_INT) snprintf(buf,sizeof(buf),"%lld",(long long)mc_obj->i);
                    else snprintf(buf,sizeof(buf),"%g",mc_obj->f);
                    mc_result=xs_str(buf);
                } else if (strcmp(mc_name,"to_char")==0) {
                    char buf[2]={(char)(num_i&0xFF),0};
                    mc_result=xs_str(buf);
                } else if (strcmp(mc_name,"digits")==0) {
                    int64_t n2=mc_obj->tag==XS_INT?mc_obj->i:(int64_t)mc_obj->f;
                    if(n2<0) n2=-n2;
                    Value *arr=xs_array_new();
                    if(n2==0){ array_push(arr->arr,xs_int(0)); }
                    else {
                        char tmp[32]; int tlen=snprintf(tmp,sizeof(tmp),"%lld",(long long)n2);
                        for(int j=0;j<tlen;j++) array_push(arr->arr,xs_int(tmp[j]-'0'));
                    }
                    mc_result=arr;
                } else if (strcmp(mc_name,"to_hex")==0) {
                    char buf[32]; snprintf(buf,sizeof(buf),"%llx",(long long)num_i);
                    mc_result=xs_str(buf);
                } else if (strcmp(mc_name,"to_oct")==0) {
                    char buf[32]; snprintf(buf,sizeof(buf),"%llo",(long long)num_i);
                    mc_result=xs_str(buf);
                } else if (strcmp(mc_name,"to_bin")==0) {
                    uint64_t v2=(uint64_t)num_i;
                    if(v2==0){mc_result=xs_str("0");}
                    else{
                        char buf[65]; int pos=64; buf[pos]='\0';
                        while(v2>0){buf[--pos]=(char)('0'+(v2&1));v2>>=1;}
                        mc_result=xs_str(buf+pos);
                    }
                } else if (strcmp(mc_name,"is_a")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    int match=(mc_obj->tag==XS_INT&&(strcmp(mc_args[0]->s,"int")==0||strcmp(mc_args[0]->s,"Int")==0))||
                              (mc_obj->tag==XS_FLOAT&&(strcmp(mc_args[0]->s,"float")==0||strcmp(mc_args[0]->s,"Float")==0));
                    mc_result=xs_bool(match);
                } else mc_result=value_incref(XS_NULL_VAL);
                (void)num_f; (void)num_i;
            }
            else {
                /* generic methods for any remaining types */
                if (strcmp(mc_name,"is_a")==0&&mc_argc>=1&&mc_args[0]->tag==XS_STR) {
                    const char *tn = mc_args[0]->s;
                    int match = 0;
                    if (mc_obj->tag==XS_STR && (strcmp(tn,"str")==0||strcmp(tn,"String")==0)) match=1;
                    else if (mc_obj->tag==XS_INT && (strcmp(tn,"int")==0||strcmp(tn,"Int")==0)) match=1;
                    else if (mc_obj->tag==XS_FLOAT && (strcmp(tn,"float")==0||strcmp(tn,"Float")==0)) match=1;
                    else if ((mc_obj->tag==XS_ARRAY||mc_obj->tag==XS_TUPLE) && (strcmp(tn,"array")==0||strcmp(tn,"Array")==0||strcmp(tn,"List")==0)) match=1;
                    else if (mc_obj->tag==XS_BOOL && (strcmp(tn,"bool")==0||strcmp(tn,"Bool")==0)) match=1;
                    mc_result = xs_bool(match);
                } else {
                /* check plugin methods */
                Value *pmethods = map_get(vm->globals, "__plugin_methods");
                if (pmethods && pmethods->tag == XS_MAP) {
                    const char *type_name = NULL;
                    if (mc_obj->tag == XS_STR) type_name = "str";
                    else if (mc_obj->tag == XS_INT) type_name = "int";
                    else if (mc_obj->tag == XS_FLOAT) type_name = "float";
                    else if (mc_obj->tag == XS_ARRAY) type_name = "array";
                    else if (mc_obj->tag == XS_BOOL) type_name = "bool";
                    if (type_name) {
                        Value *tm = map_get(pmethods->map, type_name);
                        if (tm && tm->tag == XS_MAP) {
                            Value *pfn = map_get(tm->map, mc_name);
                            if (pfn && (pfn->tag == XS_CLOSURE || pfn->tag == XS_NATIVE)) {
                                /* call plugin method with self as first arg */
                                Value *pargs[17];
                                pargs[0] = mc_obj;
                                int total = 1 + mc_argc;
                                if (total > 17) total = 17;
                                for (int pj = 0; pj < mc_argc && pj < 16; pj++) pargs[pj+1] = mc_args[pj];
                                mc_result = vm_invoke(vm, pfn, pargs, total);
                                frame = FRAME;
                                if (!mc_result) mc_result = value_incref(XS_NULL_VAL);
                            } else mc_result = value_incref(XS_NULL_VAL);
                        } else mc_result = value_incref(XS_NULL_VAL);
                    } else mc_result = value_incref(XS_NULL_VAL);
                } else mc_result = value_incref(XS_NULL_VAL);
                } /* end is_a else */
            }

            /* check plugin methods if result is null */
            if (!mc_called && mc_result && mc_result->tag == XS_NULL) {
                Value *pmethods = map_get(vm->globals, "__plugin_methods");
                if (pmethods && pmethods->tag == XS_MAP) {
                    const char *ptype = NULL;
                    if (mc_obj->tag == XS_STR) ptype = "str";
                    else if (mc_obj->tag == XS_INT) ptype = "int";
                    else if (mc_obj->tag == XS_FLOAT) ptype = "float";
                    else if (mc_obj->tag == XS_ARRAY) ptype = "array";
                    else if (mc_obj->tag == XS_BOOL) ptype = "bool";
                    if (ptype) {
                        Value *tm = map_get(pmethods->map, ptype);
                        if (tm && tm->tag == XS_MAP) {
                            Value *pfn = map_get(tm->map, mc_name);
                            if (pfn && (pfn->tag == XS_CLOSURE || pfn->tag == XS_NATIVE)) {
                                Value *pargs[17];
                                pargs[0] = mc_obj;
                                int ptotal = 1 + mc_argc;
                                if (ptotal > 17) ptotal = 17;
                                for (int pj = 0; pj < mc_argc && pj < 16; pj++) pargs[pj+1] = mc_args[pj];
                                value_decref(mc_result);
                                mc_result = vm_invoke(vm, pfn, pargs, ptotal);
                                frame = FRAME;
                                if (!mc_result) mc_result = value_incref(XS_NULL_VAL);
                            }
                        }
                    }
                }
            }
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
            (void)eff_name;

            Value *eff_args[16];
            for (int i = argc_eff - 1; i >= 0; i--) eff_args[i] = POP();

            /* save full continuation (frames + stack pointer) */
            vm->eff_cont.frame_count = vm->frame_count;
            memcpy(vm->eff_cont.frames, vm->frames,
                   sizeof(CallFrame) * (size_t)vm->frame_count);
            vm->eff_cont.sp_offset = vm->sp;
            vm->eff_cont.valid = 1;

            Value *eff_val = (argc_eff > 0) ? eff_args[0] : value_incref(XS_NULL_VAL);
            for (int i = 1; i < argc_eff; i++) value_decref(eff_args[i]);

            /* find handler (scan try stack) */
            int eff_handled = 0;
            for (int fi = vm->frame_count - 1; fi >= 0; fi--) {
                CallFrame *cf = &vm->frames[fi];
                if (cf->try_depth > 0) {
                    TryEntry *te = &cf->try_stack[--cf->try_depth];
                    /* don't unwind stack: keep it intact for resume */
                    /* just jump to handler within the handler frame */
                    vm->sp = te->stack_top;
                    PUSH(eff_val);
                    vm->frame_count = fi + 1;
                    frame = cf;
                    frame->ip = te->catch_ip;
                    eff_handled = 1;
                    break;
                }
            }
            if (!eff_handled) {
                fprintf(stderr, "unhandled effect\n");
                value_decref(eff_val);
                return 1;
            }
            break;
        }
        case OP_EFFECT_RESUME: {
            Value *resume_val = POP();
            if (vm->eff_cont.valid) {
                /* restore continuation: all frames and stack pointer */
                memcpy(vm->frames, vm->eff_cont.frames,
                       sizeof(CallFrame) * (size_t)vm->eff_cont.frame_count);
                vm->frame_count = vm->eff_cont.frame_count;
                vm->sp = vm->eff_cont.sp_offset;
                frame = FRAME;
                vm->eff_cont.valid = 0;
                /* push resume value as result of the perform expression */
                PUSH(resume_val);
            } else {
                PUSH(resume_val);
            }
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
                /* immediately execute spawn blocks */
                if (fn->tag == XS_CLOSURE) {
                    int cl_arity = fn->cl->proto->arity;
                    if (cl_arity < 0) cl_arity = -(cl_arity + 1);
                    if (cl_arity == 0) {
                        Value *result = vm_invoke(vm, fn, NULL, 0);
                        frame = FRAME;
                        /* check if result is an actor: unwrap as actor instance */
                        if (result && result->tag == XS_MAP && map_get(result->map, "__actor_name")) {
                            Value *actor_inst = xs_map_new();
                            Value *state = map_get(result->map, "__state");
                            if (state && state->tag == XS_MAP)
                                for (int aj = 0; aj < state->map->cap; aj++)
                                    if (state->map->keys[aj])
                                        map_set(actor_inst->map, state->map->keys[aj],
                                                value_incref(state->map->vals[aj]));
                            Value *methods = map_get(result->map, "__methods");
                            if (methods && methods->tag == XS_MAP)
                                map_set(actor_inst->map, "__methods", value_incref(methods));
                            Value *aname = map_get(result->map, "__actor_name");
                            if (aname) map_set(actor_inst->map, "__type", value_incref(aname));
                            value_decref(result);
                            value_decref(task);
                            value_decref(fn);
                            PUSH(actor_inst);
                            break;
                        }
                        { Value *sv = xs_str("done"); map_set(task->map, "_status", sv); value_decref(sv); }
                        map_set(task->map, "_result", result ? result : value_incref(XS_NULL_VAL));
                        if (result) value_decref(result);
                    } else {
                        { Value *sv = xs_str("done"); map_set(task->map, "_status", sv); value_decref(sv); }
                        map_set(task->map, "_result", value_incref(XS_NULL_VAL));
                    }
                } else if (fn->tag == XS_NATIVE) {
                    Value *result = fn->native(NULL, NULL, 0);
                    { Value *sv = xs_str("done"); map_set(task->map, "_status", sv); value_decref(sv); }
                    map_set(task->map, "_result", result ? result : value_incref(XS_NULL_VAL));
                    if (result) value_decref(result);
                }
                if (1) { /* immediate execution done above, skip old deferred path */
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
            } else if (fn->tag == XS_MAP && map_get(fn->map, "__actor_name")) {
                /* spawn an actor: create instance with state + methods merged */
                value_decref(task);
                Value *actor_inst = xs_map_new();
                Value *state = map_get(fn->map, "__state");
                if (state && state->tag == XS_MAP) {
                    for (int j = 0; j < state->map->cap; j++)
                        if (state->map->keys[j])
                            map_set(actor_inst->map, state->map->keys[j],
                                    value_incref(state->map->vals[j]));
                }
                Value *methods = map_get(fn->map, "__methods");
                if (methods && methods->tag == XS_MAP) {
                    map_set(actor_inst->map, "__methods", value_incref(methods));
                }
                Value *aname = map_get(fn->map, "__actor_name");
                if (aname) map_set(actor_inst->map, "__type", value_incref(aname));
                value_decref(fn);
                PUSH(actor_inst);
                break;
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
                    Value *handle_fn = map_get(methods->map, "handle");
                    if (handle_fn && handle_fn->tag == XS_CLOSURE) {
                        /* call handle(self, msg) via vm_invoke */
                        Value *args2[2] = { actor_val, msg };
                        value_decref(result);
                        result = vm_invoke(vm, handle_fn, args2, 2);
                        frame = FRAME;
                        if (!result) result = value_incref(XS_NULL_VAL);
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
            Value *r;
            if (bv == 0.0) { r = value_incref(XS_NULL_VAL); }
            else r = xs_int((int64_t)floor(av / bv));
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
            Value *base = POP();
            Value *child = POP();
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
