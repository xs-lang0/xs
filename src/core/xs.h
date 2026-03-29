/* xs.h: shared types and forward decls */
#ifndef XS_H
#define XS_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <setjmp.h>
#include <math.h>
#include <time.h>

extern int g_no_color;

typedef struct Value    Value;
typedef struct Env      Env;
typedef struct Node     Node;
typedef struct XSArray  XSArray;
typedef struct XSMap    XSMap;
typedef struct XSFunc   XSFunc;
typedef struct XSStruct XSStruct;
typedef struct XSEnum   XSEnum;
typedef struct XSClass  XSClass;
typedef struct XSInst   XSInst;
typedef struct XSRange  XSRange;
typedef struct XSSignal XSSignal;
typedef struct XSActor  XSActor;
typedef struct XSBigInt XSBigInt;
typedef struct Interp   Interp;
#ifdef XSC_ENABLE_VM
typedef struct XSClosure XSClosure;
#endif

typedef enum {
    XS_NULL = 0,
    XS_BOOL,
    XS_INT,
    XS_BIGINT,
    XS_FLOAT,
    XS_STR,
    XS_CHAR,
    XS_ARRAY,
    XS_MAP,
    XS_TUPLE,
    XS_FUNC,
    XS_NATIVE,
    XS_STRUCT_VAL,
    XS_ENUM_VAL,
    XS_CLASS_VAL,
    XS_INST,
    XS_RANGE,
    XS_SIGNAL,
    XS_ACTOR,
    XS_REGEX,
    XS_MODULE,
    XS_OVERLOAD,
#ifdef XSC_ENABLE_VM
    XS_CLOSURE,
#endif
} ValueTag;

typedef Value* (*NativeFn)(Interp *interp, Value **args, int argc);

/* composite types */
struct XSArray {
    Value  **items;
    int      len;
    int      cap;
    int      refcount;
};

struct XSMap {
    char   **keys;
    Value  **vals;
    int      len;
    int      cap;
    int      refcount;
};

struct XSFunc {
    char    *name;
    Node   **params;
    int      nparams;
    Node    *body;
    Env     *closure;
    Node   **default_vals;
    int     *variadic_flags;
    int      is_generator;
    int      is_async;
    char    *deprecated_msg;
    char   **param_type_names;
    char    *ret_type_name;
    Node   **param_contracts; /* where clause per param, may be NULL */
    int      refcount;
};

struct XSStruct {
    char    *type_name;
    XSMap   *fields;
    int      refcount;
};

struct XSEnum {
    char    *type_name;
    char    *variant;
    XSArray *arr_data;
    XSMap   *map_data;
    int      refcount;
};

struct XSClass {
    char    *name;
    XSMap   *fields;
    XSMap   *methods;
    XSMap   *static_methods;
    XSClass **bases;
    int      nbases;
    char   **traits;
    int      ntraits;
    int      refcount;
};

struct XSInst {
    XSClass *class_;
    XSMap   *fields;
    XSMap   *methods;
    int      refcount;
};

struct XSRange {
    int64_t  start;
    int64_t  end;
    int64_t  step;
    int      inclusive;
    int      refcount;
};

struct XSSignal {
    Value   *value;
    Value  **subscribers;
    int      nsubs;
    int      subcap;
    Value   *compute;      /* derived signal: recompute fn, or NULL */
    int      notifying;    /* re-entrancy guard */
    int      refcount;
};

struct XSActor {
    char    *name;
    XSMap   *state;
    XSFunc  *handle_fn;
    XSMap   *methods;
    Env     *closure;
    int      refcount;
};

/* value */
struct Value {
    ValueTag tag;
    int      refcount;
    union {
        int64_t    i;
        XSBigInt  *bigint;
        double     f;
        char      *s;
        XSArray   *arr;
        XSMap     *map;
        XSFunc    *fn;
        NativeFn   native;
        XSStruct  *st;
        XSEnum    *en;
        XSClass   *cls;
        XSInst    *inst;
        XSRange   *range;
        XSSignal  *signal;
        XSActor   *actor;
        XSArray   *overload;  /* array of XS_FUNC values for overloaded fns */
#ifdef XSC_ENABLE_VM
        XSClosure *cl;
#endif
    };
};

/* control-flow signals */
#define CF_RETURN   1
#define CF_BREAK    2
#define CF_CONTINUE 3
#define CF_THROW    4
#define CF_PANIC    5
#define CF_ERROR    6
#define CF_YIELD    7
#define CF_RESUME   8
#define CF_TAIL_CALL 9

static inline void *xs_malloc(size_t n) {
    void *p = malloc(n);
    if (!p) { fprintf(stderr, "xs: oom\n"); exit(1); }
    return p;
}
static inline void *xs_calloc(size_t n, size_t sz) {
    void *p = calloc(n, sz);
    if (!p) { fprintf(stderr, "xs: oom\n"); exit(1); }
    return p;
}
static inline void *xs_realloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) { fprintf(stderr, "xs: oom\n"); exit(1); }
    return q;
}
static inline char *xs_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s)+1;
    char *d = xs_malloc(n);
    memcpy(d, s, n);
    return d;
}
static inline char *xs_strndup(const char *s, size_t n) {
    char *d = xs_malloc(n+1);
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

#ifdef XSC_ENABLE_VM
typedef struct Upvalue Upvalue;
struct XSClosure {
    struct XSProto *proto;
    Upvalue       **upvalues;
    int             refcount;
};
#endif /* XSC_ENABLE_VM */

#endif /* XS_H */
