#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "core/xs_compat.h"
#include "runtime/interp.h"
#include "tls/xs_tls.h"
#include <strings.h>
#ifdef __MINGW32__
#define re_nsub nsub
#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#ifndef __MINGW32__
#  include <unistd.h>
#  include <sys/select.h>
#endif
#ifndef __MINGW32__
#  include <sys/time.h>
#  include <sys/stat.h>
#  include <dirent.h>
#  include <glob.h>
#else
#  include <sys/stat.h>
#  include <errno.h>
#endif
#if defined(__has_include)
#  if __has_include(<regex.h>)
#    include <regex.h>
#    define XS_HAS_REGEX 1
#  endif
#elif !defined(__MINGW32__)
#  include <regex.h>
#  define XS_HAS_REGEX 1
#endif
#ifndef XS_HAS_REGEX
#include "core/xs_regex.h"
#endif
#include <errno.h>

#ifndef M_PI
#define M_PI   3.14159265358979323846
#endif
#ifndef M_E
#define M_E    2.71828182845904523536
#endif
#ifndef M_TAU
#define M_TAU  6.28318530717958647692
#endif

static char *inst_to_str(Interp *interp, Value *v, int repr_mode) {
    if (v->tag == XS_INST && v->inst) {
        const char *mname = repr_mode ? "__repr__" : "__str__";
        Value *fn = map_get(v->inst->methods, mname);
        if (!fn && v->inst->class_ && v->inst->class_->methods)
            fn = map_get(v->inst->class_->methods, mname);
        if (!fn && !repr_mode) {
            fn = map_get(v->inst->methods, "to_string");
            if (!fn && v->inst->class_ && v->inst->class_->methods)
                fn = map_get(v->inst->class_->methods, "to_string");
        }
        if (fn && (fn->tag == XS_FUNC || fn->tag == XS_NATIVE)) {
            int has_self = 0;
            if (fn->tag == XS_FUNC && fn->fn->nparams > 0) {
                Node *p0 = fn->fn->params[0];
                if (p0->tag == NODE_PAT_IDENT && strcmp(p0->pat_ident.name, "self") == 0)
                    has_self = 1;
            }
            if (fn->tag == XS_NATIVE) has_self = 1;
            Value *result;
            if (has_self) {
                Value *call_args[1] = { v };
                result = call_value(interp, fn, call_args, 1, mname);
            } else {
                result = call_value(interp, fn, NULL, 0, mname);
            }
            if (result && result->tag == XS_STR) {
                char *s = xs_strdup(result->s);
                value_decref(result);
                return s;
            }
            if (result) {
                char *s = value_str(result);
                value_decref(result);
                return s;
            }
        }
    }
    return repr_mode ? value_repr(v) : value_str(v);
}

static Value *builtin_print(Interp *i, Value **args, int argc) {
    if (argc >= 1 && args[0]->tag == XS_STR && args[0]->s) {
        const char *fmt = args[0]->s;
        int has_placeholder = 0;
        for (const char *p = fmt; *p; p++) {
            if (*p == '{' && *(p+1) == '}') { has_placeholder = 1; break; }
        }
        if (has_placeholder) {
            int argidx = 1;
            for (const char *p = fmt; *p; ) {
                if (*p == '{' && *(p+1) == '}') {
                    if (argidx < argc) {
                        char *s = inst_to_str(i, args[argidx++], 0);
                        printf("%s", s); free(s);
                    }
                    p += 2;
                } else {
                    putchar(*p++);
                }
            }
            for (int j = argidx; j < argc; j++) {
                printf(" ");
                char *s = inst_to_str(i, args[j], 0); printf("%s", s); free(s);
            }
            printf("\n");
            return value_incref(XS_NULL_VAL);
        }
    }
    for (int j = 0; j < argc; j++) {
        if (j) printf(" ");
        char *s = inst_to_str(i, args[j], 0);
        printf("%s", s);
        free(s);
    }
    printf("\n");
    return value_incref(XS_NULL_VAL);
}

static Value *builtin_print_no_nl(Interp *i, Value **args, int argc) {
    (void)i;
    for (int j = 0; j < argc; j++) {
        if (j) printf(" ");
        char *s = value_str(args[j]);
        printf("%s", s);
        free(s);
    }
    fflush(stdout);
    return value_incref(XS_NULL_VAL);
}

static Value *builtin_eprint(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc >= 1 && args[0]->tag == XS_STR && args[0]->s) {
        const char *fmt = args[0]->s;
        int has_placeholder = 0;
        for (const char *p = fmt; *p; p++) {
            if (*p == '{' && *(p+1) == '}') { has_placeholder = 1; break; }
        }
        if (has_placeholder) {
            int argidx = 1;
            for (const char *p = fmt; *p; ) {
                if (*p == '{' && *(p+1) == '}') {
                    if (argidx < argc) {
                        char *s = value_str(args[argidx++]);
                        fprintf(stderr,"%s",s); free(s);
                    }
                    p += 2;
                } else { fputc(*p++, stderr); }
            }
            for (int j = argidx; j < argc; j++) {
                fprintf(stderr," ");
                char *s = value_str(args[j]); fprintf(stderr,"%s",s); free(s);
            }
            fprintf(stderr,"\n");
            return value_incref(XS_NULL_VAL);
        }
    }
    for (int j = 0; j < argc; j++) {
        if (j) fprintf(stderr," ");
        char *s = value_str(args[j]);
        fprintf(stderr,"%s",s); free(s);
    }
    fprintf(stderr,"\n");
    return value_incref(XS_NULL_VAL);
}

/* type predicates */
static Value *builtin_type(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc < 1) return xs_str("null");
    switch (args[0]->tag) {
    case XS_NULL:   return xs_str("null");
    case XS_BOOL:   return xs_str("bool");
    case XS_INT:    return xs_str("int");
    case XS_FLOAT:  return xs_str("float");
    case XS_STR:    return xs_str("str");
    case XS_CHAR:   return xs_str("char");
    case XS_ARRAY:  return xs_str("array");
    case XS_TUPLE:  return xs_str("tuple");
    case XS_MAP:    return xs_str("map");
    case XS_FUNC:   return xs_str("fn");
    case XS_NATIVE: return xs_str("fn");
    case XS_STRUCT_VAL: return xs_str(args[0]->st->type_name ? args[0]->st->type_name : "struct");
    case XS_ENUM_VAL:   return xs_str(args[0]->en->type_name ? args[0]->en->type_name : "enum");
    case XS_CLASS_VAL:  return xs_str(args[0]->cls->name ? args[0]->cls->name : "class");
    case XS_INST:       return xs_str(args[0]->inst->class_->name ? args[0]->inst->class_->name : "object");
    case XS_RANGE:  return xs_str("range");
    case XS_SIGNAL: return xs_str("signal");
    case XS_MODULE: return xs_str("module");
    default:        return xs_str("unknown");
    }
}

static Value *builtin_typeof(Interp *i, Value **args, int argc) {
    return builtin_type(i, args, argc);
}

/* type_of returns Python-compat capitalized type names */
static Value *builtin_type_of(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc < 1) return xs_str("Null");
    switch (args[0]->tag) {
    case XS_NULL:   return xs_str("Null");
    case XS_BOOL:   return xs_str("Bool");
    case XS_INT:    return xs_str("Int");
    case XS_FLOAT:  return xs_str("Float");
    case XS_STR:    return xs_str("Str");
    case XS_CHAR:   return xs_str("Char");
    case XS_ARRAY:  return xs_str("Array");
    case XS_TUPLE:  return xs_str("Tuple");
    case XS_MAP:    return xs_str("Map");
    case XS_MODULE: return xs_str("Map");
    case XS_FUNC:   return xs_str("Fn");
    case XS_NATIVE: return xs_str("Fn");
    case XS_STRUCT_VAL: return xs_str(args[0]->st->type_name ? args[0]->st->type_name : "Struct");
    case XS_ENUM_VAL:   return xs_str(args[0]->en->type_name ? args[0]->en->type_name : "Enum");
    case XS_CLASS_VAL:  return xs_str(args[0]->cls->name ? args[0]->cls->name : "Class");
    case XS_INST:       return xs_str(args[0]->inst->class_->name ? args[0]->inst->class_->name : "Object");
    case XS_RANGE:  return xs_str("Range");
    case XS_SIGNAL: return xs_str("Signal");
    default:        return xs_str("Unknown");
    }
}

static inline Value *tag_check(Value **args, int argc, ValueTag tag) {
    return xs_bool(argc > 0 && args[0]->tag == tag);
}

static inline Value *tag_check2(Value **args, int argc, ValueTag t1, ValueTag t2) {
    return xs_bool(argc > 0 && (args[0]->tag == t1 || args[0]->tag == t2));
}

static Value *builtin_is_null(Interp *i, Value **a, int n)  { (void)i; return n < 1 ? xs_bool(1) : tag_check(a,n,XS_NULL); }
static Value *builtin_is_int(Interp *i, Value **a, int n)   { (void)i; return tag_check(a, n, XS_INT); }
static Value *builtin_is_float(Interp *i, Value **a, int n) { (void)i; return tag_check(a, n, XS_FLOAT); }
static Value *builtin_is_str(Interp *i, Value **a, int n)   { (void)i; return tag_check(a, n, XS_STR); }
static Value *builtin_is_bool(Interp *i, Value **a, int n)  { (void)i; return tag_check(a, n, XS_BOOL); }
static Value *builtin_is_array(Interp *i, Value **a, int n) { (void)i; return tag_check2(a, n, XS_ARRAY, XS_TUPLE); }
static Value *builtin_is_fn(Interp *i, Value **a, int n)    { (void)i; return tag_check2(a, n, XS_FUNC, XS_NATIVE); }

/* conversions */
static Value *builtin_int(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return xs_int(0);
    Value *v=args[0];
    if (v->tag==XS_INT) return value_incref(v);
    if (v->tag==XS_FLOAT) return xs_int((int64_t)v->f);
    if (v->tag==XS_STR) return xs_int(atoll(v->s));
    if (v->tag==XS_BOOL) return xs_int(v->i);
    return xs_int(0);
}

static Value *builtin_float(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return xs_float(0.0);
    Value *v=args[0];
    if (v->tag==XS_FLOAT) return value_incref(v);
    if (v->tag==XS_INT) return xs_float((double)v->i);
    if (v->tag==XS_STR) return xs_float(atof(v->s));
    return xs_float(0.0);
}

static Value *builtin_str(Interp *i, Value **args, int argc) {
    if (argc<1) return xs_str("");
    char *s = inst_to_str(i, args[0], 0);
    Value *v = xs_str(s); free(s); return v;
}

static Value *builtin_bool(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return value_incref(XS_FALSE_VAL);
    return value_truthy(args[0])?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}

static Value *builtin_char(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return xs_char(0);
    if (args[0]->tag==XS_INT) return xs_char((char)args[0]->i);
    if (args[0]->tag==XS_STR&&args[0]->s[0]) return xs_char(args[0]->s[0]);
    return xs_char(0);
}

static Value *builtin_repr(Interp *i, Value **args, int argc) {
    if (argc<1) return xs_str("null");
    char *s = inst_to_str(i, args[0], 1);
    Value *v = xs_str(s); free(s); return v;
}

static Value *builtin_abs(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return xs_int(0);
    if (args[0]->tag==XS_INT) return xs_int(args[0]->i<0?-args[0]->i:args[0]->i);
    if (args[0]->tag==XS_FLOAT) return xs_float(fabs(args[0]->f));
    return value_incref(args[0]);
}

static Value *builtin_min(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc==0) return value_incref(XS_NULL_VAL);
    if (argc==1 && args[0]->tag==XS_ARRAY) {
        XSArray *arr=args[0]->arr;
        if (arr->len==0) return value_incref(XS_NULL_VAL);
        Value *m=arr->items[0];
        for (int j=1;j<arr->len;j++) if(value_cmp(arr->items[j],m)<0) m=arr->items[j];
        return value_incref(m);
    }
    Value *m=args[0];
    for (int j=1;j<argc;j++) if(value_cmp(args[j],m)<0) m=args[j];
    return value_incref(m);
}

static Value *builtin_max(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc==0) return value_incref(XS_NULL_VAL);
    if (argc==1 && args[0]->tag==XS_ARRAY) {
        XSArray *arr=args[0]->arr;
        if (arr->len==0) return value_incref(XS_NULL_VAL);
        Value *m=arr->items[0];
        for (int j=1;j<arr->len;j++) if(value_cmp(arr->items[j],m)>0) m=arr->items[j];
        return value_incref(m);
    }
    Value *m=args[0];
    for (int j=1;j<argc;j++) if(value_cmp(args[j],m)>0) m=args[j];
    return value_incref(m);
}

static Value *builtin_pow(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<2) return xs_float(0.0);
    double base=args[0]->tag==XS_FLOAT?args[0]->f:(double)args[0]->i;
    double exp2=args[1]->tag==XS_FLOAT?args[1]->f:(double)args[1]->i;
    return xs_float(pow(base,exp2));
}

/* one-arg math wrappers: all the same shape */
#define MATH1(name, fn) \
    static Value *builtin_##name(Interp *i, Value **args, int argc) { \
        (void)i; \
        if (argc < 1) return xs_float(0.0); \
        double v = args[0]->tag == XS_FLOAT ? args[0]->f : (double)args[0]->i; \
        return xs_float(fn(v)); \
    }
MATH1(sqrt,  sqrt)
MATH1(floor, floor)
MATH1(ceil,  ceil)
MATH1(round, round)

static Value *builtin_log(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return xs_float(0.0);
    double v=args[0]->tag==XS_FLOAT?args[0]->f:(double)args[0]->i;
    if (argc>1) {
        double base=args[1]->tag==XS_FLOAT?args[1]->f:(double)args[1]->i;
        return xs_float(log(v)/log(base));
    }
    return xs_float(log(v));
}

MATH1(sin, sin)
MATH1(cos, cos)

MATH1(tan, tan)
#undef MATH1

/* collections */
static Value *builtin_len(Interp *i, Value **args, int argc) {
    if (argc<1) return xs_int(0);
    Value *v=args[0];
    if (v->tag==XS_ARRAY||v->tag==XS_TUPLE) return xs_int(v->arr->len);
    if (v->tag==XS_STR) return xs_int((int64_t)strlen(v->s));
    if (v->tag==XS_MAP||v->tag==XS_MODULE) return xs_int(v->map->len);
    if (v->tag==XS_RANGE) {
        int64_t span = v->range->end - v->range->start;
        if (v->range->inclusive) span += (span >= 0) ? 1 : -1;
        int64_t step = v->range->step ? v->range->step : 1;
        int64_t n2;
        if (step > 0) n2 = (span > 0) ? (span + step - 1) / step : 0;
        else           n2 = (span < 0) ? (-span + (-step) - 1) / (-step) : 0;
        return xs_int(n2);
    }
    /* __len__ dunder method on instances */
    if (v->tag==XS_INST && v->inst) {
        Value *fn = map_get(v->inst->methods, "__len__");
        if (!fn && v->inst->class_ && v->inst->class_->methods)
            fn = map_get(v->inst->class_->methods, "__len__");
        if (fn && (fn->tag == XS_FUNC || fn->tag == XS_NATIVE)) {
            int has_self = 0;
            if (fn->tag == XS_FUNC && fn->fn->nparams > 0) {
                Node *p0 = fn->fn->params[0];
                if (p0->tag == NODE_PAT_IDENT && strcmp(p0->pat_ident.name, "self") == 0)
                    has_self = 1;
            }
            Value *result;
            if (has_self) {
                Value *call_args[1] = { v };
                result = call_value(i, fn, call_args, 1, "__len__");
            } else {
                result = call_value(i, fn, NULL, 0, "__len__");
            }
            return result;
        }
    }
    return xs_int(0);
}

static Value *builtin_range(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc==1) {
        int64_t end2 = (args[0]->tag==XS_INT)?args[0]->i:(int64_t)args[0]->f;
        return xs_range(0, end2, 0);
    }
    if (argc>=2) {
        int64_t start2=(args[0]->tag==XS_INT)?args[0]->i:(int64_t)args[0]->f;
        int64_t end2=(args[1]->tag==XS_INT)?args[1]->i:(int64_t)args[1]->f;
        int64_t step = 1;
        if (argc >= 3) {
            if (args[2]->tag==XS_INT) step = args[2]->i;
            else if (args[2]->tag==XS_FLOAT) step = (int64_t)args[2]->f;
        }
        if (step == 0) {
            fprintf(stderr, "range: step cannot be zero\n");
            return xs_range(0, 0, 0);
        }
        return xs_range_step(start2, end2, 0, step);
    }
    return xs_range(0, 0, 0);
}

static Value *builtin_array(Interp *i, Value **args, int argc) {
    (void)i;
    Value *arr = xs_array_new();
    for (int j=0;j<argc;j++) array_push(arr->arr, value_incref(args[j]));
    return arr;
}

static Value *builtin_map(Interp *i, Value **args, int argc) {
    /* map() -> empty map, map(arr, fn) -> mapped array */
    if (argc == 0) return xs_map_new();
    if (argc >= 2 && (args[0]->tag == XS_ARRAY || args[0]->tag == XS_TUPLE) &&
        (args[1]->tag == XS_FUNC || args[1]->tag == XS_NATIVE || args[1]->tag == XS_CLOSURE)) {
        Value *arr = args[0], *fn = args[1];
        Value *result = xs_array_new();
        for (int j = 0; j < arr->arr->len; j++) {
            Value *elem = arr->arr->items[j];
            Value *call_args[] = { elem };
            Value *mapped = call_value(i, fn, call_args, 1, "map");
            array_push(result->arr, mapped);
        }
        return result;
    }
    return xs_map_new();
}

static Value *builtin_filter(Interp *i, Value **args, int argc) {
    if (argc < 2) return xs_array_new();
    Value *arr = args[0], *fn = args[1];
    if (arr->tag != XS_ARRAY && arr->tag != XS_TUPLE) return xs_array_new();
    Value *result = xs_array_new();
    for (int j = 0; j < arr->arr->len; j++) {
        Value *elem = arr->arr->items[j];
        Value *call_args[] = { elem };
        Value *keep = call_value(i, fn, call_args, 1, "filter");
        if (value_truthy(keep)) array_push(result->arr, value_incref(elem));
        value_decref(keep);
    }
    return result;
}

static Value *builtin_reduce(Interp *i, Value **args, int argc) {
    if (argc < 2) return value_incref(XS_NULL_VAL);
    Value *arr = args[0], *fn = args[1];
    if (arr->tag != XS_ARRAY && arr->tag != XS_TUPLE) return value_incref(XS_NULL_VAL);
    Value *acc = (argc >= 3) ? value_incref(args[2]) : (arr->arr->len > 0 ? value_incref(arr->arr->items[0]) : value_incref(XS_NULL_VAL));
    int start = (argc >= 3) ? 0 : 1;
    for (int j = start; j < arr->arr->len; j++) {
        Value *call_args[] = { acc, arr->arr->items[j] };
        Value *next = call_value(i, fn, call_args, 2, "reduce");
        value_decref(acc);
        acc = next;
    }
    return acc;
}

static Value *builtin_keys(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return xs_array_new();
    Value *obj=args[0];
    if (obj->tag==XS_MAP||obj->tag==XS_MODULE) {
        int nk=0; char **ks=map_keys(obj->map,&nk);
        Value *arr=xs_array_new();
        for (int j=0;j<nk;j++){array_push(arr->arr,xs_str(ks[j]));free(ks[j]);}
        free(ks); return arr;
    }
    return xs_array_new();
}

static Value *builtin_values(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return xs_array_new();
    Value *obj=args[0];
    if (obj->tag==XS_MAP||obj->tag==XS_MODULE) {
        int nk=0; char **ks=map_keys(obj->map,&nk);
        Value *arr=xs_array_new();
        for (int j=0;j<nk;j++){
            Value *v=map_get(obj->map,ks[j]);
            array_push(arr->arr,v?value_incref(v):value_incref(XS_NULL_VAL));
            free(ks[j]);
        }
        free(ks); return arr;
    }
    return xs_array_new();
}

static Value *builtin_entries(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return xs_array_new();
    Value *obj=args[0];
    if (obj->tag==XS_MAP||obj->tag==XS_MODULE) {
        int nk=0; char **ks=map_keys(obj->map,&nk);
        Value *arr=xs_array_new();
        for (int j=0;j<nk;j++){
            Value *tup=xs_tuple_new();
            Value *v=map_get(obj->map,ks[j]);
            array_push(tup->arr,xs_str(ks[j]));
            array_push(tup->arr,v?value_incref(v):value_incref(XS_NULL_VAL));
            array_push(arr->arr,tup);
            free(ks[j]);
        }
        free(ks); return arr;
    }
    return xs_array_new();
}

static Value *builtin_flatten(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1||args[0]->tag!=XS_ARRAY) return xs_array_new();
    Value *res=xs_array_new();
    XSArray *arr=args[0]->arr;
    for (int j=0;j<arr->len;j++) {
        if (arr->items[j]->tag==XS_ARRAY) {
            XSArray *inner=arr->items[j]->arr;
            for (int k=0;k<inner->len;k++) array_push(res->arr,value_incref(inner->items[k]));
        } else {
            array_push(res->arr,value_incref(arr->items[j]));
        }
    }
    return res;
}

static Value *builtin_chars(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1||args[0]->tag!=XS_STR) return xs_array_new();
    Value *arr=xs_array_new();
    const char *s=args[0]->s;
    for (int j=0;s[j];j++) array_push(arr->arr,xs_str_n(s+j,1));
    return arr;
}

static Value *builtin_bytes(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1||args[0]->tag!=XS_STR) return xs_array_new();
    Value *arr=xs_array_new();
    const char *s=args[0]->s;
    for (int j=0;s[j];j++) array_push(arr->arr,xs_int((unsigned char)s[j]));
    return arr;
}

static Value *builtin_zip(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<2||args[0]->tag!=XS_ARRAY||args[1]->tag!=XS_ARRAY) return xs_array_new();
    XSArray *a=args[0]->arr, *b=args[1]->arr;
    int n2=a->len<b->len?a->len:b->len;
    Value *res=xs_array_new();
    for (int j=0;j<n2;j++) {
        Value *tup=xs_tuple_new();
        array_push(tup->arr,value_incref(a->items[j]));
        array_push(tup->arr,value_incref(b->items[j]));
        array_push(res->arr,tup);
    }
    return res;
}

static Value *builtin_enumerate(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1||args[0]->tag!=XS_ARRAY) return xs_array_new();
    int64_t start = 0;
    if (argc >= 2 && args[1]->tag == XS_INT) start = args[1]->i;
    else if (argc >= 2 && args[1]->tag == XS_FLOAT) start = (int64_t)args[1]->f;
    Value *res=xs_array_new();
    XSArray *arr=args[0]->arr;
    for (int j=0;j<arr->len;j++) {
        Value *tup=xs_tuple_new();
        array_push(tup->arr,xs_int(start + j));
        array_push(tup->arr,value_incref(arr->items[j]));
        array_push(res->arr,tup);
    }
    return res;
}

static Value *builtin_sum(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1||args[0]->tag!=XS_ARRAY) return xs_int(0);
    XSArray *arr=args[0]->arr;
    int64_t si=0; double sf=0; int is_f=0;
    for (int j=0;j<arr->len;j++) {
        if (arr->items[j]->tag==XS_FLOAT){is_f=1;sf+=arr->items[j]->f;}
        else if(arr->items[j]->tag==XS_INT) si+=arr->items[j]->i;
    }
    return is_f?xs_float(sf+(double)si):xs_int(si);
}

/* string helpers */
static Value *builtin_format(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1||args[0]->tag!=XS_STR) return xs_str("");
    /* Simple format: {} placeholders replaced by args in order */
    const char *fmt = args[0]->s;
    int argidx = 1;
    char *result = xs_strdup(""); int rlen = 0;
    for (const char *p = fmt; *p; ) {
        if (*p=='{' && *(p+1)=='}') {
            char *s = (argidx<argc)?value_str(args[argidx++]):xs_strdup("{}");
            int slen=(int)strlen(s);
            result=xs_realloc(result,rlen+slen+1);
            memcpy(result+rlen,s,slen+1); rlen+=slen; free(s);
            p+=2;
        } else {
            result=xs_realloc(result,rlen+2);
            result[rlen++]=*p++; result[rlen]='\0';
        }
    }
    Value *v=xs_str(result); free(result); return v;
}

static Value *builtin_input(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc>0) {
        char *s=value_str(args[0]); printf("%s",s); free(s); fflush(stdout);
    }
    char buf[4096]; buf[0]='\0';
    if (fgets(buf,sizeof(buf),stdin)) {
        int n=(int)strlen(buf);
        if (n>0&&buf[n-1]=='\n') buf[n-1]='\0';
    }
    return xs_str(buf);
}

static Value *builtin_exit(Interp *i, Value **args, int argc) {
    (void)i;
    int code = (argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:0;
    exit(code);
}

static Value *builtin_clear(Interp *i, Value **args, int argc) {
    (void)i;(void)args;(void)argc;
    printf("\033[2J\033[H");
    fflush(stdout);
    return value_incref(XS_NULL_VAL);
}

/* sorted(arr [, key_fn]) — returns a sorted copy */
static int cmp_values(const void *a, const void *b) {
    Value *va = *(Value **)a;
    Value *vb = *(Value **)b;
    if (va->tag == XS_INT && vb->tag == XS_INT)
        return (va->i > vb->i) - (va->i < vb->i);
    if ((va->tag == XS_FLOAT || va->tag == XS_INT) &&
        (vb->tag == XS_FLOAT || vb->tag == XS_INT)) {
        double fa = va->tag==XS_FLOAT ? va->f : (double)va->i;
        double fb = vb->tag==XS_FLOAT ? vb->f : (double)vb->i;
        return (fa > fb) - (fa < fb);
    }
    char *sa = value_str(va); char *sb = value_str(vb);
    int r = strcmp(sa, sb); free(sa); free(sb);
    return r;
}
static Value *builtin_sorted(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc < 1 || args[0]->tag != XS_ARRAY) return value_incref(XS_NULL_VAL);
    XSArray *src = args[0]->arr;
    Value *copy = xs_array_new();
    for (int j = 0; j < src->len; j++)
        array_push(copy->arr, value_incref(src->items[j]));
    qsort(copy->arr->items, (size_t)copy->arr->len, sizeof(Value*), cmp_values);
    return copy;
}

static Value *builtin_assert_eq(Interp *i, Value **args, int argc) {
    if (argc < 2) {
        fprintf(stderr, "xs: assert_eq requires 2 arguments\n");
        if (i) { i->cf.signal = CF_PANIC; i->cf.value = xs_str("assert_eq requires 2 arguments"); }
        return value_incref(XS_NULL_VAL);
    }
    if (!value_equal(args[0], args[1])) {
        char *a = value_repr(args[0]);
        char *b = value_repr(args[1]);
        const char *msg = (argc >= 3 && args[2]->tag == XS_STR) ? args[2]->s : "";
        fprintf(stderr, "xs: assertion failed: assert_eq(%s, %s)%s%s\n",
                a, b, msg[0] ? " — " : "", msg);
        free(a); free(b);
        if (i) { i->cf.signal = CF_PANIC; i->cf.value = xs_str("assert_eq failed"); }
    }
    return value_incref(XS_NULL_VAL);
}

static Value *builtin_assert(Interp *i, Value **args, int argc) {
    if (argc<1||!value_truthy(args[0])) {
        const char *msg = (argc>1&&args[1]->tag==XS_STR)?args[1]->s:"assertion failed";
        fprintf(stderr,"xs: assertion error: %s\n",msg);
        if (i) {
            i->cf.signal = CF_PANIC;
            i->cf.value  = xs_str(msg);
        }
    }
    return value_incref(XS_NULL_VAL);
}

static Value *builtin_panic(Interp *i, Value **args, int argc) {
    char *msg;
    if (argc>0) msg=value_str(args[0]);
    else msg=xs_strdup("panic");
    fprintf(stderr,"xs: panic: %s\n",msg);
    free(msg);
    if (i) {
        i->cf.signal=CF_PANIC;
        i->cf.value=xs_str("panic");
    }
    exit(1);
}

static Value *builtin_copy(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return value_incref(XS_NULL_VAL);
    return value_copy(args[0]);
}

static Value *builtin_clone(Interp *i, Value **args, int argc) {
    return builtin_copy(i, args, argc);
}

/* Runtime for the todo() builtin -- panics with a message, like Rust's todo!() */
/* todo() / unreachable() — à la Rust */
static Value *builtin_todo(Interp *i, Value **args, int argc) {
    const char *msg = (argc >= 1 && args[0]->tag == XS_STR) ? args[0]->s : "not yet implemented";
    fprintf(stderr, "todo: %s\n", msg);
    if (i) { i->cf.signal = CF_PANIC; i->cf.value = xs_str(msg); }
    exit(1);
}

static Value *builtin_unreachable(Interp *i, Value **args, int argc) {
    (void)args; (void)argc;
    fprintf(stderr, "xs: Reached unreachable code\n");
    if (i) { i->cf.signal = CF_PANIC; i->cf.value = xs_str("Reached unreachable code"); }
    exit(1);
}

static Value *builtin_vec(Interp *i, Value **args, int argc) {
    (void)i;
    Value *arr = xs_array_new();
    for (int j = 0; j < argc; j++) {
        array_push(arr->arr, value_incref(args[j]));
    }
    return arr;
}

static Value *builtin_dbg(Interp *i, Value **args, int argc) {
    (void)i;
    for (int j=0;j<argc;j++) {
        char *s=value_repr(args[j]);
        fprintf(stderr,"[dbg] %s\n",s); free(s);
    }
    if (argc==1) return value_incref(args[0]);
    return value_incref(XS_NULL_VAL);
}

/* pprint */
static void pprint_value(Value *v, int indent) {
    char *pad = xs_malloc(indent + 1);
    memset(pad, ' ', indent); pad[indent] = '\0';

    if (v->tag == XS_MAP) {
        fprintf(stdout, "{\n");
        int printed = 0;
        for (int j = 0; j < v->map->cap; j++) {
            if (!v->map->keys[j]) continue;
            if (printed > 0) fprintf(stdout, ",\n");
            fprintf(stdout, "%s  \"%s\": ", pad, v->map->keys[j]);
            pprint_value(v->map->vals[j], indent + 2);
            printed++;
        }
        if (printed > 0) fprintf(stdout, "\n");
        fprintf(stdout, "%s}", pad);
    } else if (v->tag == XS_ARRAY) {
        fprintf(stdout, "[\n");
        for (int j = 0; j < v->arr->len; j++) {
            fprintf(stdout, "%s  ", pad);
            pprint_value(v->arr->items[j], indent + 2);
            if (j < v->arr->len - 1) fprintf(stdout, ",");
            fprintf(stdout, "\n");
        }
        fprintf(stdout, "%s]", pad);
    } else {
        char *s = value_repr(v);
        fprintf(stdout, "%s", s);
        free(s);
    }
    free(pad);
}

static Value *builtin_pprint(Interp *i, Value **args, int argc) {
    (void)i;
    for (int j = 0; j < argc; j++) {
        pprint_value(args[j], 0);
        fprintf(stdout, "\n");
    }
    return value_incref(XS_NULL_VAL);
}

static Value *builtin_ord(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return xs_int(0);
    if (args[0]->tag==XS_STR) return xs_int((unsigned char)args[0]->s[0]);
    if (args[0]->tag==XS_CHAR) return xs_int((unsigned char)args[0]->s[0]);
    return xs_int(0);
}

static Value *builtin_chr(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1||args[0]->tag!=XS_INT) return xs_char(0);
    return xs_char((char)args[0]->i);
}

/* Math.* namespace */
#define MATH1(fname, cfunc) \
static Value *native_math_##fname(Interp *ig, Value **a, int n) { \
    (void)ig; \
    double v=(n>0&&a[0]->tag==XS_FLOAT)?a[0]->f:(n>0&&a[0]->tag==XS_INT)?(double)a[0]->i:0.0; \
    return xs_float(cfunc(v)); \
}
MATH1(cbrt, cbrt)
MATH1(log2_fn, log2)
MATH1(log10_fn, log10)
MATH1(sinh_fn, sinh)
MATH1(cosh_fn, cosh)
MATH1(tanh_fn, tanh)
MATH1(asin_fn, asin)
MATH1(acos_fn, acos)
MATH1(atan_fn, atan)
MATH1(exp_fn, exp)
#undef MATH1

static Value *native_math_gcd(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return xs_int(0);
    int64_t x=(a[0]->tag==XS_INT)?a[0]->i:(int64_t)a[0]->f;
    int64_t y=(a[1]->tag==XS_INT)?a[1]->i:(int64_t)a[1]->f;
    if (x<0) x=-x;
    if (y<0) y=-y;
    while (y) { int64_t t=y; y=x%y; x=t; }
    return xs_int(x);
}
static Value *native_math_lcm(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return xs_int(0);
    int64_t x=(a[0]->tag==XS_INT)?a[0]->i:(int64_t)a[0]->f;
    int64_t y=(a[1]->tag==XS_INT)?a[1]->i:(int64_t)a[1]->f;
    if (x<0) x=-x;
    if (y<0) y=-y;
    if (x==0||y==0) return xs_int(0);
    int64_t gcd=x, b2=y;
    while (b2) { int64_t t=b2; b2=gcd%b2; gcd=t; }
    return xs_int(x/gcd*y);
}
static Value *native_math_factorial(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_int(1);
    int64_t k=(a[0]->tag==XS_INT)?a[0]->i:(int64_t)a[0]->f;
    if (k<0) return xs_int(0);
    int64_t r=1; for (int64_t j=2;j<=k;j++) r*=j;
    return xs_int(r);
}
static Value *native_math_sign(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_int(0);
    if (a[0]->tag==XS_INT) return xs_int(a[0]->i>0?1:a[0]->i<0?-1:0);
    double v=a[0]->f; return xs_int(v>0.0?1:v<0.0?-1:0);
}
static Value *native_math_degrees(Interp *ig, Value **a, int n) {
    (void)ig;
    double v=(n>0&&a[0]->tag==XS_FLOAT)?a[0]->f:(n>0&&a[0]->tag==XS_INT)?(double)a[0]->i:0.0;
    return xs_float(v*180.0/M_PI);
}
static Value *native_math_radians(Interp *ig, Value **a, int n) {
    (void)ig;
    double v=(n>0&&a[0]->tag==XS_FLOAT)?a[0]->f:(n>0&&a[0]->tag==XS_INT)?(double)a[0]->i:0.0;
    return xs_float(v*M_PI/180.0);
}
static Value *native_math_trunc(Interp *ig, Value **a, int n) {
    (void)ig;
    double v=(n>0&&a[0]->tag==XS_FLOAT)?a[0]->f:(n>0&&a[0]->tag==XS_INT)?(double)a[0]->i:0.0;
    return xs_float(trunc(v));
}
static Value *native_math_lerp(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<3) return xs_float(0.0);
    double av=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    double bv=(a[1]->tag==XS_FLOAT)?a[1]->f:(double)a[1]->i;
    double tv=(a[2]->tag==XS_FLOAT)?a[2]->f:(double)a[2]->i;
    return xs_float(av+(bv-av)*tv);
}
static Value *native_math_clamp(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<3) return xs_float(0.0);
    double val=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    double lo =(a[1]->tag==XS_FLOAT)?a[1]->f:(double)a[1]->i;
    double hi =(a[2]->tag==XS_FLOAT)?a[2]->f:(double)a[2]->i;
    if (val<lo) return xs_float(lo);
    if (val>hi) return xs_float(hi);
    return xs_float(val);
}
static Value *native_math_atan2(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return xs_float(0.0);
    double y=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    double x=(a[1]->tag==XS_FLOAT)?a[1]->f:(double)a[1]->i;
    return xs_float(atan2(y,x));
}
static Value *native_math_hypot(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return xs_float(0.0);
    double x=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    double y=(a[1]->tag==XS_FLOAT)?a[1]->f:(double)a[1]->i;
    return xs_float(hypot(x,y));
}
static Value *native_math_isnan(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return value_incref(XS_FALSE_VAL);
    double v=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    return isnan(v)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_math_isinf(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return value_incref(XS_FALSE_VAL);
    double v=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    return isinf(v)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}

/* ── additional math: hyperbolic inverse ─────────────────── */
#define MATH1_EXTRA(fname, cfunc) \
static Value *native_math_##fname(Interp *ig, Value **a, int n) { \
    (void)ig; \
    double v=(n>0&&a[0]->tag==XS_FLOAT)?a[0]->f:(n>0&&a[0]->tag==XS_INT)?(double)a[0]->i:0.0; \
    return xs_float(cfunc(v)); \
}
MATH1_EXTRA(asinh_fn, asinh)
MATH1_EXTRA(acosh_fn, acosh)
MATH1_EXTRA(atanh_fn, atanh)
MATH1_EXTRA(expm1_fn, expm1)
MATH1_EXTRA(log1p_fn, log1p)
MATH1_EXTRA(erf_fn,   erf)
MATH1_EXTRA(erfc_fn,  erfc)
MATH1_EXTRA(gamma_fn, tgamma)
MATH1_EXTRA(lgamma_fn,lgamma)
#undef MATH1_EXTRA

/* ── fmod(x, y) ─────────────────────────────────────────── */
static Value *native_math_fmod(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return xs_float(0.0);
    double x=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    double y=(a[1]->tag==XS_FLOAT)?a[1]->f:(double)a[1]->i;
    return xs_float(fmod(x,y));
}

/* ── modf(x) -> [integer_part, fractional_part] ──────────── */
static Value *native_math_modf(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_array_new();
    double x=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    double ipart;
    double fpart=modf(x,&ipart);
    Value *arr=xs_array_new();
    Value *vi=xs_float(ipart); array_push(arr->arr,vi); value_decref(vi);
    Value *vf=xs_float(fpart); array_push(arr->arr,vf); value_decref(vf);
    return arr;
}

/* ── copysign(x, y) ─────────────────────────────────────── */
static Value *native_math_copysign(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return xs_float(0.0);
    double x=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    double y=(a[1]->tag==XS_FLOAT)?a[1]->f:(double)a[1]->i;
    return xs_float(copysign(x,y));
}

/* ── isclose(a, b, rel_tol?, abs_tol?) ───────────────────── */
static Value *native_math_isclose(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return value_incref(XS_FALSE_VAL);
    double x=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    double y=(a[1]->tag==XS_FLOAT)?a[1]->f:(double)a[1]->i;
    double rel_tol=(n>=3)?((a[2]->tag==XS_FLOAT)?a[2]->f:(double)a[2]->i):1e-9;
    double abs_tol=(n>=4)?((a[3]->tag==XS_FLOAT)?a[3]->f:(double)a[3]->i):0.0;
    double diff=fabs(x-y);
    if (diff<=abs_tol) return value_incref(XS_TRUE_VAL);
    double mx=fabs(x)>fabs(y)?fabs(x):fabs(y);
    return (diff<=rel_tol*mx)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}

/* ── frexp(x) -> [mantissa, exponent] ────────────────────── */
static Value *native_math_frexp(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_array_new();
    double x=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    int exp_val;
    double mant=frexp(x,&exp_val);
    Value *arr=xs_array_new();
    Value *vm=xs_float(mant); array_push(arr->arr,vm); value_decref(vm);
    Value *ve=xs_int(exp_val); array_push(arr->arr,ve); value_decref(ve);
    return arr;
}

/* ── ldexp(x, i) ─────────────────────────────────────────── */
static Value *native_math_ldexp(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return xs_float(0.0);
    double x=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    int exp_val=(a[1]->tag==XS_INT)?(int)a[1]->i:(int)a[1]->f;
    return xs_float(ldexp(x,exp_val));
}

/* ── comb(n, k) = n! / (k! * (n-k)!) ────────────────────── */
static Value *native_math_comb(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return xs_int(0);
    int64_t nn=(a[0]->tag==XS_INT)?a[0]->i:(int64_t)a[0]->f;
    int64_t kk=(a[1]->tag==XS_INT)?a[1]->i:(int64_t)a[1]->f;
    if (kk<0||kk>nn||nn<0) return xs_int(0);
    if (kk>nn-kk) kk=nn-kk;
    int64_t r=1;
    for (int64_t j=0;j<kk;j++) { r=r*(nn-j)/(j+1); }
    return xs_int(r);
}

/* ── perm(n, k?) ─────────────────────────────────────────── */
static Value *native_math_perm(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_int(0);
    int64_t nn=(a[0]->tag==XS_INT)?a[0]->i:(int64_t)a[0]->f;
    int64_t kk=(n>=2)?((a[1]->tag==XS_INT)?a[1]->i:(int64_t)a[1]->f):nn;
    if (kk<0||kk>nn||nn<0) return xs_int(0);
    int64_t r=1;
    for (int64_t j=0;j<kk;j++) r*=(nn-j);
    return xs_int(r);
}

/* ── helper: extract double from value ───────────────────── */
static double math_to_double(Value *v) {
    if (v->tag==XS_FLOAT) return v->f;
    if (v->tag==XS_INT)   return (double)v->i;
    return 0.0;
}

/* ── prod(arr) ───────────────────────────────────────────── */
static Value *native_math_prod(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_ARRAY) return xs_float(1.0);
    XSArray *arr=a[0]->arr;
    double r=1.0;
    for (int j=0;j<arr->len;j++) r*=math_to_double(arr->items[j]);
    return xs_float(r);
}

/* ── sum(arr) ────────────────────────────────────────────── */
static Value *native_math_sum(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_ARRAY) return xs_float(0.0);
    XSArray *arr=a[0]->arr;
    double r=0.0;
    for (int j=0;j<arr->len;j++) r+=math_to_double(arr->items[j]);
    return xs_float(r);
}

/* ── min(arr) ────────────────────────────────────────────── */
static Value *native_math_min_arr(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_ARRAY||a[0]->arr->len==0) return value_incref(XS_NULL_VAL);
    XSArray *arr=a[0]->arr;
    double r=math_to_double(arr->items[0]);
    for (int j=1;j<arr->len;j++) { double v=math_to_double(arr->items[j]); if (v<r) r=v; }
    return xs_float(r);
}

/* ── max(arr) ────────────────────────────────────────────── */
static Value *native_math_max_arr(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_ARRAY||a[0]->arr->len==0) return value_incref(XS_NULL_VAL);
    XSArray *arr=a[0]->arr;
    double r=math_to_double(arr->items[0]);
    for (int j=1;j<arr->len;j++) { double v=math_to_double(arr->items[j]); if (v>r) r=v; }
    return xs_float(r);
}

/* ── mean(arr) ───────────────────────────────────────────── */
static Value *native_math_mean(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_ARRAY||a[0]->arr->len==0) return xs_float(0.0);
    XSArray *arr=a[0]->arr;
    double r=0.0;
    for (int j=0;j<arr->len;j++) r+=math_to_double(arr->items[j]);
    return xs_float(r/(double)arr->len);
}

Value *make_math_module(void) {
    XSMap *m = map_new();

    /* ── constants (both uppercase and lowercase per reference) ── */
#define MATH_CONST(name, val) do { \
    Value *cv = xs_float(val); map_set(m, name, cv); value_decref(cv); \
} while(0)
    MATH_CONST("PI",     M_PI);
    MATH_CONST("pi",     M_PI);
    MATH_CONST("E",      M_E);
    MATH_CONST("e",      M_E);
    MATH_CONST("TAU",    2*M_PI);
    MATH_CONST("tau",    2*M_PI);
    MATH_CONST("INF",    HUGE_VAL);
    MATH_CONST("inf",    HUGE_VAL);
    MATH_CONST("NAN",    0.0/0.0);
    MATH_CONST("nan",    0.0/0.0);
#undef MATH_CONST

#define REG(name, fn) map_set(m, name, xs_native(fn))

    /* ── trigonometric ───────────────────────────────────────── */
    REG("sin",       builtin_sin);
    REG("cos",       builtin_cos);
    REG("tan",       builtin_tan);
    REG("asin",      native_math_asin_fn);
    REG("acos",      native_math_acos_fn);
    REG("atan",      native_math_atan_fn);
    REG("atan2",     native_math_atan2);
    REG("sinh",      native_math_sinh_fn);
    REG("cosh",      native_math_cosh_fn);
    REG("tanh",      native_math_tanh_fn);
    REG("asinh",     native_math_asinh_fn);
    REG("acosh",     native_math_acosh_fn);
    REG("atanh",     native_math_atanh_fn);

    /* ── exponents / logarithms ──────────────────────────────── */
    REG("sqrt",      builtin_sqrt);
    REG("cbrt",      native_math_cbrt);
    REG("exp",       native_math_exp_fn);
    REG("expm1",     native_math_expm1_fn);
    REG("log",       builtin_log);
    REG("log2",      native_math_log2_fn);
    REG("log10",     native_math_log10_fn);
    REG("log1p",     native_math_log1p_fn);

    /* ── rounding ────────────────────────────────────────────── */
    REG("floor",     builtin_floor);
    REG("ceil",      builtin_ceil);
    REG("round",     builtin_round);
    REG("trunc",     native_math_trunc);

    /* ── utility ─────────────────────────────────────────────── */
    REG("abs",       builtin_abs);
    REG("pow",       builtin_pow);
    REG("hypot",     native_math_hypot);
    REG("gcd",       native_math_gcd);
    REG("lcm",       native_math_lcm);
    REG("factorial", native_math_factorial);
    REG("is_nan",    native_math_isnan);
    REG("is_inf",    native_math_isinf);
    REG("isnan",     native_math_isnan);   /* compat alias */
    REG("isinf",     native_math_isinf);   /* compat alias */
    REG("clamp",     native_math_clamp);
    REG("lerp",      native_math_lerp);
    REG("sign",      native_math_sign);
    REG("degrees",   native_math_degrees);
    REG("radians",   native_math_radians);
    REG("fmod",      native_math_fmod);
    REG("modf",      native_math_modf);
    REG("copysign",  native_math_copysign);
    REG("isclose",   native_math_isclose);
    REG("frexp",     native_math_frexp);
    REG("ldexp",     native_math_ldexp);

    /* ── combinatorial ───────────────────────────────────────── */
    REG("comb",      native_math_comb);
    REG("perm",      native_math_perm);

    /* ── aggregate ───────────────────────────────────────────── */
    REG("prod",      native_math_prod);
    REG("sum",       native_math_sum);
    REG("min",       native_math_min_arr);
    REG("max",       native_math_max_arr);
    REG("mean",      native_math_mean);

    /* ── special functions ───────────────────────────────────── */
    REG("erf",       native_math_erf_fn);
    REG("erfc",      native_math_erfc_fn);
    REG("gamma",     native_math_gamma_fn);
    REG("lgamma",    native_math_lgamma_fn);

#undef REG
    return xs_module(m);
}

/* ── time module ─────────────────────────────────────────── */
static Value *native_time_now(Interp *i, Value **args, int argc) {
    (void)i; (void)args; (void)argc;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return xs_float((double)ts.tv_sec + (double)ts.tv_nsec/1e9);
}

static Value *native_time_sleep(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1) return value_incref(XS_NULL_VAL);
    double secs = args[0]->tag==XS_FLOAT?args[0]->f:(double)args[0]->i;
    struct timespec ts;
    ts.tv_sec  = (time_t)secs;
    ts.tv_nsec = (long)((secs - ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
    return value_incref(XS_NULL_VAL);
}

static Value *native_time_stopwatch(Interp *i, Value **args, int argc) {
    (void)i; (void)args; (void)argc;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double start = (double)ts.tv_sec + (double)ts.tv_nsec/1e9;
    Value *sw = xs_map_new();
    Value *sv = xs_float(start);
    map_set(sw->map, "_start", sv);
    value_decref(sv);
    return sw;
}

/* ── time module extra ───────────────────────────────────── */
static Value *native_time_millis(Interp *i, Value **a, int n) {
    (void)i;(void)a;(void)n;
    struct timespec ts; clock_gettime(CLOCK_REALTIME,&ts);
    return xs_int((int64_t)(ts.tv_sec*1000 + ts.tv_nsec/1000000));
}
#define TIME_COMPONENT(name, field) \
    static Value *native_time_##name(Interp *i, Value **a, int n) { \
        (void)i;(void)a;(void)n; \
        time_t t=time(NULL); struct tm *tm=localtime(&t); \
        return xs_int(tm->field); }
TIME_COMPONENT(year,   tm_year+1900)
TIME_COMPONENT(month,  tm_mon+1)
TIME_COMPONENT(day,    tm_mday)
TIME_COMPONENT(hour,   tm_hour)
TIME_COMPONENT(minute, tm_min)
TIME_COMPONENT(second, tm_sec)

static Value *native_time_sleep_ms(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return value_incref(XS_NULL_VAL);
    int64_t ms=(a[0]->tag==XS_INT)?a[0]->i:(int64_t)a[0]->f;
    struct timespec ts;
    ts.tv_sec=ms/1000; ts.tv_nsec=(ms%1000)*1000000L;
    nanosleep(&ts,NULL);
    return value_incref(XS_NULL_VAL);
}
static Value *native_time_format(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_str("");
    time_t t=(time_t)((a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i);
    const char *fmt=(n>=2&&a[1]->tag==XS_STR)?a[1]->s:"%Y-%m-%d %H:%M:%S";
    struct tm *tm2=localtime(&t);
    char buf[256]; buf[0]='\0';
    strftime(buf,sizeof(buf),fmt,tm2);
    return xs_str(buf);
}
static Value *native_time_monotonic(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return xs_float((double)ts.tv_sec+(double)ts.tv_nsec/1e9);
}
static Value *native_time_parse(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    const char *input=a[0]->s;
    const char *fmt=(n>=2&&a[1]->tag==XS_STR)?a[1]->s:NULL;
    struct tm tm2; memset(&tm2,0,sizeof(tm2));
    int parsed=0;
    if (fmt) {
        /* Use explicit format string */
        if (strptime(input,fmt,&tm2)) parsed=1;
    } else {
        /* Auto-detect ISO 8601 formats */
        if (strptime(input,"%Y-%m-%dT%H:%M:%S",&tm2)) parsed=1;
        else if (strptime(input,"%Y-%m-%d",&tm2)) parsed=1;
    }
    if (!parsed) return value_incref(XS_NULL_VAL);
    /* Return a map with year, month, day, hour, minute, second fields */
    Value *m=xs_map_new();
    Value *v;
    v=xs_int(tm2.tm_year+1900); map_set(m->map,"year",v);   value_decref(v);
    v=xs_int(tm2.tm_mon+1);     map_set(m->map,"month",v);  value_decref(v);
    v=xs_int(tm2.tm_mday);      map_set(m->map,"day",v);    value_decref(v);
    v=xs_int(tm2.tm_hour);      map_set(m->map,"hour",v);   value_decref(v);
    v=xs_int(tm2.tm_min);       map_set(m->map,"minute",v); value_decref(v);
    v=xs_int(tm2.tm_sec);       map_set(m->map,"second",v); value_decref(v);
    return m;
}
static Value *native_time_now_ms(Interp *ig, Value **a, int n) {
    return native_time_millis(ig,a,n);
}

Value *make_time_module(void) {
    XSMap *m = map_new();
    map_set(m, "now",       xs_native(native_time_now));
    map_set(m, "now_ms",    xs_native(native_time_now_ms));
    map_set(m, "sleep",     xs_native(native_time_sleep));
    map_set(m, "sleep_ms",  xs_native(native_time_sleep_ms));
    map_set(m, "stopwatch", xs_native(native_time_stopwatch));
    map_set(m, "millis",    xs_native(native_time_millis));
    map_set(m, "format",    xs_native(native_time_format));
    map_set(m, "monotonic", xs_native(native_time_monotonic));
    map_set(m, "clock",     xs_native(native_time_monotonic));
    map_set(m, "parse",     xs_native(native_time_parse));
    map_set(m, "year",      xs_native(native_time_year));
    map_set(m, "month",     xs_native(native_time_month));
    map_set(m, "day",       xs_native(native_time_day));
    map_set(m, "hour",      xs_native(native_time_hour));
    map_set(m, "minute",    xs_native(native_time_minute));
    map_set(m, "second",    xs_native(native_time_second));
    return xs_module(m);
}

/* ── io module ───────────────────────────────────────────── */
static Value *native_io_read_file(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<1||args[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    FILE *f = fopen(args[0]->s, "r");
    if (!f) return value_incref(XS_NULL_VAL);
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=xs_malloc(sz+1);
    long nr = (long)fread(buf,1,sz,f); fclose(f); buf[nr]='\0';
    Value *v=xs_str(buf); free(buf); return v;
}

static Value *native_io_write_file(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc<2||args[0]->tag!=XS_STR||args[1]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    FILE *f=fopen(args[0]->s,"w");
    if (!f) return value_incref(XS_FALSE_VAL);
    fputs(args[1]->s,f); fclose(f);
    return value_incref(XS_TRUE_VAL);
}

/* ── string module ───────────────────────────────────────── */
static Value *native_str_pad_left(Interp *i, Value **a, int n) {
    (void)i;
    if (n<2||a[0]->tag!=XS_STR) return n>0?value_incref(a[0]):value_incref(XS_NULL_VAL);
    const char *s=a[0]->s; int width=(int)a[1]->i; char fill=' ';
    if (n>=3&&a[2]->tag==XS_STR&&a[2]->s[0]) fill=a[2]->s[0];
    int slen=(int)strlen(s);
    if (slen>=width) return value_incref(a[0]);
    char *r=xs_malloc(width+1);
    int pad=width-slen;
    for(int j=0;j<pad;j++) r[j]=fill;
    memcpy(r+pad,s,slen); r[width]='\0';
    Value *v=xs_str(r); free(r); return v;
}
static Value *native_str_pad_right(Interp *i, Value **a, int n) {
    (void)i;
    if (n<2||a[0]->tag!=XS_STR) return n>0?value_incref(a[0]):value_incref(XS_NULL_VAL);
    const char *s=a[0]->s; int width=(int)a[1]->i; char fill=' ';
    if (n>=3&&a[2]->tag==XS_STR&&a[2]->s[0]) fill=a[2]->s[0];
    int slen=(int)strlen(s);
    if (slen>=width) return value_incref(a[0]);
    char *r=xs_malloc(width+1);
    memcpy(r,s,slen);
    for(int j=slen;j<width;j++) r[j]=fill;
    r[width]='\0'; Value *v=xs_str(r); free(r); return v;
}
static Value *native_str_center(Interp *i, Value **a, int n) {
    (void)i;
    if (n<2||a[0]->tag!=XS_STR) return n>0?value_incref(a[0]):value_incref(XS_NULL_VAL);
    const char *s=a[0]->s; int width=(int)a[1]->i; char fill=' ';
    if (n>=3&&a[2]->tag==XS_STR&&a[2]->s[0]) fill=a[2]->s[0];
    int slen=(int)strlen(s);
    if (slen>=width) return value_incref(a[0]);
    int pad=width-slen; int lpad=pad/2; int rpad=pad-lpad;
    char *r=xs_malloc(width+1);
    for(int j=0;j<lpad;j++) r[j]=fill;
    memcpy(r+lpad,s,slen);
    for(int j=0;j<rpad;j++) r[lpad+slen+j]=fill;
    r[width]='\0'; Value *v=xs_str(r); free(r); return v;
}
static Value *native_str_truncate(Interp *i, Value **a, int n) {
    (void)i;
    if (n<2||a[0]->tag!=XS_STR) return n>0?value_incref(a[0]):value_incref(XS_NULL_VAL);
    const char *s=a[0]->s; int width=(int)a[1]->i;
    const char *suf=(n>=3&&a[2]->tag==XS_STR)?a[2]->s:"...";
    int slen=(int)strlen(s); int suflen=(int)strlen(suf);
    if (slen<=width) return value_incref(a[0]);
    int keep=width-suflen; if(keep<0)keep=0;
    char *r=xs_malloc(keep+suflen+1);
    memcpy(r,s,keep); memcpy(r+keep,suf,suflen); r[keep+suflen]='\0';
    Value *v=xs_str(r); free(r); return v;
}
static Value *native_str_camel_to_snake(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    const char *s=a[0]->s; int slen=(int)strlen(s);
    char *r=xs_malloc(slen*2+1); int ri=0;
    for (int j=0;j<slen;j++) {
        if (isupper((unsigned char)s[j])&&j>0) r[ri++]='_';
        r[ri++]=tolower((unsigned char)s[j]);
    }
    r[ri]='\0'; Value *v=xs_str(r); free(r); return v;
}
static Value *native_str_snake_to_camel(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    const char *s=a[0]->s; int slen=(int)strlen(s);
    char *r=xs_malloc(slen+1); int ri=0; int cap=0;
    for (int j=0;j<slen;j++) {
        if (s[j]=='_') { cap=1; continue; }
        r[ri++]=cap?toupper((unsigned char)s[j]):s[j]; cap=0;
    }
    r[ri]='\0'; Value *v=xs_str(r); free(r); return v;
}
static Value *native_str_escape_html(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    const char *s=a[0]->s;
    int cap=256; char *r=xs_malloc(cap); int ri=0;
    for (const char *p=s;*p;p++) {
        const char *ent=NULL; int elen=0;
        if (*p=='&'){ent="&amp;";elen=5;}
        else if(*p=='<'){ent="&lt;";elen=4;}
        else if(*p=='>'){ent="&gt;";elen=4;}
        else if(*p=='"'){ent="&quot;";elen=6;}
        else if(*p=='\''){ent="&#39;";elen=5;}
        if (ri+elen+2>cap){cap=cap*2+elen+2;r=realloc(r,cap);}
        if (ent){memcpy(r+ri,ent,elen);ri+=elen;}
        else r[ri++]=(char)*p;
    }
    r[ri]='\0'; Value *v=xs_str(r); free(r); return v;
}
static Value *native_str_is_numeric(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    const char *s=a[0]->s; if (!*s) return value_incref(XS_FALSE_VAL);
    int j=0; if(s[j]=='+'||s[j]=='-') j++;
    int digits=0, dots=0;
    for(;s[j];j++){
        if (isdigit((unsigned char)s[j])) digits++;
        else if (s[j]=='.'&&!dots) dots++;
        else return value_incref(XS_FALSE_VAL);
    }
    return digits>0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_str_words(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return xs_array_new();
    const char *s=a[0]->s; Value *arr=xs_array_new();
    const char *p=s;
    while (*p) {
        while (*p&&isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char *start=p;
        while (*p&&!isspace((unsigned char)*p)) p++;
        array_push(arr->arr,xs_str_n(start,(int)(p-start)));
    }
    return arr;
}
static int levenshtein_dist(const char *s1, const char *s2) {
    int l1=(int)strlen(s1), l2=(int)strlen(s2);
    int *dp=xs_malloc((l2+1)*sizeof(int));
    for(int j=0;j<=l2;j++) dp[j]=j;
    for(int i=1;i<=l1;i++){
        int prev=dp[0]; dp[0]=i;
        for(int j=1;j<=l2;j++){
            int old=dp[j];
            dp[j]=s1[i-1]==s2[j-1]?prev:1+( prev<dp[j-1]?(prev<dp[j]?prev:dp[j]):(dp[j-1]<dp[j]?dp[j-1]:dp[j]) );
            prev=old;
        }
    }
    int r=dp[l2]; free(dp); return r;
}
static Value *native_str_levenshtein(Interp *i, Value **a, int n) {
    (void)i;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return xs_int(0);
    return xs_int(levenshtein_dist(a[0]->s,a[1]->s));
}
static Value *native_str_similarity(Interp *i, Value **a, int n) {
    (void)i;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return xs_float(0.0);
    int l1=(int)strlen(a[0]->s), l2=(int)strlen(a[1]->s);
    int maxlen=l1>l2?l1:l2;
    if (maxlen==0) return xs_float(1.0);
    int d=levenshtein_dist(a[0]->s,a[1]->s);
    return xs_float(1.0-(double)d/maxlen);
}
static Value *native_str_repeat(Interp *i, Value **a, int n) {
    (void)i;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_INT) return value_incref(XS_NULL_VAL);
    const char *s=a[0]->s; int count=(int)a[1]->i;
    if (count<=0) return xs_str("");
    int slen=(int)strlen(s);
    int rlen=slen*count;
    char *r=xs_malloc(rlen+1);
    for(int j=0;j<count;j++) memcpy(r+j*slen,s,slen);
    r[rlen]='\0';
    Value *v=xs_str(r); free(r); return v;
}
static Value *native_str_chars(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    const char *s=a[0]->s; int slen=(int)strlen(s);
    Value *arr=xs_array_new();
    char buf[2]; buf[1]='\0';
    for(int j=0;j<slen;j++){
        buf[0]=s[j];
        Value *ch=xs_str(buf);
        array_push(arr->arr,ch);
    }
    return arr;
}
static Value *native_str_bytes(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    const unsigned char *s=(const unsigned char*)a[0]->s; int slen=(int)strlen(a[0]->s);
    Value *arr=xs_array_new();
    for(int j=0;j<slen;j++){
        Value *b=xs_int((int64_t)s[j]);
        array_push(arr->arr,b);
    }
    return arr;
}
Value *make_string_module(void) {
    XSMap *m=map_new();
    map_set(m,"pad_left",       xs_native(native_str_pad_left));
    map_set(m,"pad_right",      xs_native(native_str_pad_right));
    map_set(m,"center",         xs_native(native_str_center));
    map_set(m,"truncate",       xs_native(native_str_truncate));
    map_set(m,"camel_to_snake", xs_native(native_str_camel_to_snake));
    map_set(m,"snake_to_camel", xs_native(native_str_snake_to_camel));
    map_set(m,"escape_html",    xs_native(native_str_escape_html));
    map_set(m,"is_numeric",     xs_native(native_str_is_numeric));
    map_set(m,"words",          xs_native(native_str_words));
    map_set(m,"levenshtein",    xs_native(native_str_levenshtein));
    map_set(m,"similarity",     xs_native(native_str_similarity));
    map_set(m,"repeat",         xs_native(native_str_repeat));
    map_set(m,"chars",          xs_native(native_str_chars));
    map_set(m,"bytes",          xs_native(native_str_bytes));
    return xs_module(m);
}

/* ── path module ─────────────────────────────────────────── */
static Value *native_path_basename(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    const char *s=a[0]->s;
    const char *sl=strrchr(s,'/');
    const char *bs=strrchr(s,'\\');
    const char *last=sl>bs?sl:bs;
    return xs_str(last?last+1:s);
}
static Value *native_path_dirname(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return xs_str(".");
    const char *s=a[0]->s;
    const char *sl=strrchr(s,'/');
    const char *bs=strrchr(s,'\\');
    const char *last=sl>bs?sl:bs;
    if (!last) return xs_str(".");
    int dlen=(int)(last-s);
    if (dlen==0) return xs_str("/");
    char *r=xs_strndup(s,dlen); Value *v=xs_str(r); free(r); return v;
}
static Value *native_path_ext(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return xs_str("");
    const char *s=a[0]->s;
    const char *sl=strrchr(s,'/');
    const char *bs=strrchr(s,'\\');
    const char *last=sl>bs?sl:bs;
    const char *base=last?last+1:s;
    const char *dot=strrchr(base,'.');
    return xs_str(dot?dot:"");
}
static Value *native_path_stem(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return xs_str("");
    const char *s=a[0]->s;
    const char *sl=strrchr(s,'/'); const char *bs=strrchr(s,'\\');
    const char *last=sl>bs?sl:bs; const char *base=last?last+1:s;
    const char *dot=strrchr(base,'.');
    if (!dot) return xs_str(base);
    char *r=xs_strndup(base,(int)(dot-base)); Value *v=xs_str(r); free(r); return v;
}
static Value *native_path_join(Interp *i, Value **a, int argc) {
    (void)i;
    if (argc==0) return xs_str("");
    int total=0;
    for(int j=0;j<argc;j++) if(a[j]->tag==XS_STR) total+=(int)strlen(a[j]->s)+1;
    char *r=xs_malloc(total+2); int ri=0;
    for(int j=0;j<argc;j++){
        if(a[j]->tag!=XS_STR) continue;
        const char *s=a[j]->s;
        if(ri>0&&s[0]!='/'&&s[0]!='\\') r[ri++]='/';
        int slen=(int)strlen(s);
        memcpy(r+ri,s,slen); ri+=slen;
        /* remove trailing slash */
        while(ri>1&&(r[ri-1]=='/'||r[ri-1]=='\\')) ri--;
    }
    r[ri]='\0'; Value *v=xs_str(r); free(r); return v;
}
Value *make_path_module(void) {
    XSMap *m=map_new();
    map_set(m,"basename", xs_native(native_path_basename));
    map_set(m,"dirname",  xs_native(native_path_dirname));
    map_set(m,"ext",      xs_native(native_path_ext));
    map_set(m,"stem",     xs_native(native_path_stem));
    map_set(m,"join",     xs_native(native_path_join));
    { Value *v=xs_str("/"); map_set(m,"sep",v); value_decref(v); }
    return xs_module(m);
}

/* ── base64 module ───────────────────────────────────────── */
static const char b64_table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static Value *native_b64_encode(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    const unsigned char *s=(const unsigned char*)a[0]->s;
    int slen=(int)strlen(a[0]->s);
    int rlen=((slen+2)/3)*4; char *r=xs_malloc(rlen+1); int ri=0;
    for(int j=0;j<slen;j+=3){
        unsigned int v=(unsigned)s[j]<<16|(j+1<slen?(unsigned)s[j+1]:0)<<8|(j+2<slen?(unsigned)s[j+2]:0);
        r[ri++]=b64_table[(v>>18)&63]; r[ri++]=b64_table[(v>>12)&63];
        r[ri++]=(j+1<slen)?b64_table[(v>>6)&63]:'=';
        r[ri++]=(j+2<slen)?b64_table[v&63]:'=';
    }
    r[ri]='\0'; Value *v2=xs_str(r); free(r); return v2;
}
static Value *native_b64_decode(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    const char *s=a[0]->s; int slen=(int)strlen(s);
    char *r=xs_malloc(slen); int ri=0;
    static const signed char b64_inv[256]={
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    for(int j=0;j+3<slen;j+=4){
        int a0=b64_inv[(unsigned char)s[j]],a1=b64_inv[(unsigned char)s[j+1]];
        int a2=b64_inv[(unsigned char)s[j+2]],a3=b64_inv[(unsigned char)s[j+3]];
        if(a0<0||a1<0) break;
        r[ri++]=(char)((a0<<2)|(a1>>4));
        if(a2>=0) r[ri++]=(char)((a1<<4)|(a2>>2));
        if(a3>=0) r[ri++]=(char)((a2<<6)|a3);
    }
    r[ri]='\0'; Value *v2=xs_str(r); free(r); return v2;
}
Value *make_base64_module(void) {
    XSMap *m=map_new();
    map_set(m,"encode",xs_native(native_b64_encode));
    map_set(m,"decode",xs_native(native_b64_decode));
    return xs_module(m);
}

/* ── hash module ─────────────────────────────────────────── */
/* Forward declarations for real hash implementations (defined in crypto module section) */
static void md5_hash(const uint8_t *data, size_t len, uint8_t out[16]);
static void sha256_hash(const uint8_t *data, size_t len, uint8_t out[32]);

static Value *native_hash_md5(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return xs_str("");
    uint8_t hash[16];
    md5_hash((const uint8_t*)a[0]->s, strlen(a[0]->s), hash);
    char buf[33];
    for(int j=0;j<16;j++) sprintf(buf+j*2,"%02x",hash[j]);
    buf[32]='\0';
    return xs_str(buf);
}
static Value *native_hash_sha256(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return xs_str("");
    uint8_t hash[32];
    sha256_hash((const uint8_t*)a[0]->s, strlen(a[0]->s), hash);
    char buf[65];
    for(int j=0;j<32;j++) sprintf(buf+j*2,"%02x",hash[j]);
    buf[64]='\0';
    return xs_str(buf);
}
Value *make_hash_module(void) {
    XSMap *m=map_new();
    map_set(m,"md5",   xs_native(native_hash_md5));
    map_set(m,"sha256",xs_native(native_hash_sha256));
    return xs_module(m);
}

/* ── uuid module ─────────────────────────────────────────── */
static Value *native_uuid_v4(Interp *i, Value **a, int n) {
    (void)i;(void)a;(void)n;
    unsigned char b[16];
    for(int j=0;j<16;j++) b[j]=(unsigned char)(rand()&0xff);
    b[6]=(b[6]&0x0f)|0x40; b[8]=(b[8]&0x3f)|0x80;
    char buf[37];
    snprintf(buf,sizeof(buf),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
    return xs_str(buf);
}
static Value *native_uuid_is_valid(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    const char *s=a[0]->s; int l=(int)strlen(s);
    if (l!=36) return value_incref(XS_FALSE_VAL);
    for(int j=0;j<36;j++){
        if(j==8||j==13||j==18||j==23){if(s[j]!='-')return value_incref(XS_FALSE_VAL);}
        else if(!isxdigit((unsigned char)s[j])) return value_incref(XS_FALSE_VAL);
    }
    return value_incref(XS_TRUE_VAL);
}
Value *make_uuid_module(void) {
    XSMap *m=map_new();
    map_set(m,"v4",      xs_native(native_uuid_v4));
    map_set(m,"is_valid",xs_native(native_uuid_is_valid));
    return xs_module(m);
}

/* ── random module ───────────────────────────────────────── */
static Value *native_random_int(Interp *ig, Value **args, int argc) {
    (void)ig;
    int64_t lo = 0, hi = 100;
    if (argc >= 1 && args[0]->tag == XS_INT) lo = args[0]->i;
    if (argc >= 2 && args[1]->tag == XS_INT) hi = args[1]->i;
    if (hi < lo) { int64_t tmp=lo; lo=hi; hi=tmp; }
    int64_t range2 = hi - lo + 1;
    return xs_int(lo + (range2 > 0 ? (int64_t)(rand() % (int)range2) : 0));
}
static Value *native_random_float(Interp *ig, Value **args, int argc) {
    (void)ig; (void)args; (void)argc;
    return xs_float((double)rand() / ((double)RAND_MAX + 1.0));
}
static Value *native_random_choice(Interp *ig, Value **args, int argc) {
    (void)ig;
    if (argc < 1 || args[0]->tag != XS_ARRAY || args[0]->arr->len == 0)
        return value_incref(XS_NULL_VAL);
    int idx = rand() % args[0]->arr->len;
    return value_incref(array_get(args[0]->arr, idx));
}
static Value *native_random_shuffle(Interp *ig, Value **args, int argc) {
    (void)ig;
    if (argc < 1 || args[0]->tag != XS_ARRAY) return value_incref(XS_NULL_VAL);
    Value *result = xs_array_new();
    for (int j2 = 0; j2 < args[0]->arr->len; j2++)
        array_push(result->arr, value_incref(array_get(args[0]->arr, j2)));
    for (int j2 = result->arr->len - 1; j2 > 0; j2--) {
        int k = rand() % (j2 + 1);
        Value *tmp2 = result->arr->items[j2];
        result->arr->items[j2] = result->arr->items[k];
        result->arr->items[k] = tmp2;
    }
    return result;
}
static Value *native_random_seed(Interp *ig, Value **args, int argc) {
    (void)ig;
    if (argc >= 1 && args[0]->tag == XS_INT) srand((unsigned)args[0]->i);
    return value_incref(XS_NULL_VAL);
}
static Value *native_random_bool(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    return (rand()%2==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_random_choices(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_ARRAY||a[0]->arr->len==0) return xs_array_new();
    int64_t k=(a[1]->tag==XS_INT)?a[1]->i:1;
    Value *arr=xs_array_new();
    XSArray *src=a[0]->arr;
    for (int64_t j=0;j<k;j++) {
        int idx=rand()%src->len;
        array_push(arr->arr,value_incref(src->items[idx]));
    }
    return arr;
}
static Value *native_random_shuffled(Interp *ig, Value **a, int n) {
    return native_random_shuffle(ig,a,n);
}
static Value *native_random_sample(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_ARRAY) return xs_array_new();
    XSArray *src=a[0]->arr;
    int64_t k=(a[1]->tag==XS_INT)?a[1]->i:0;
    if (k>src->len) k=src->len;
    /* Fisher-Yates on a copy */
    Value *copy=xs_array_new();
    for (int j=0;j<src->len;j++) array_push(copy->arr,value_incref(src->items[j]));
    for (int j=copy->arr->len-1;j>0;j--) {
        int r=rand()%(j+1);
        Value *tmp=copy->arr->items[j];
        copy->arr->items[j]=copy->arr->items[r];
        copy->arr->items[r]=tmp;
    }
    /* Shrink to k */
    while (copy->arr->len>(int)k) {
        value_decref(copy->arr->items[--copy->arr->len]);
    }
    return copy;
}
static Value *native_random_gauss(Interp *ig, Value **a, int n) {
    (void)ig;
    double mu=(n>0)?(a[0]->tag==XS_FLOAT?a[0]->f:(double)a[0]->i):0.0;
    double sigma=(n>1)?(a[1]->tag==XS_FLOAT?a[1]->f:(double)a[1]->i):1.0;
    /* Box-Muller transform */
    double u1=((double)rand()+1.0)/((double)RAND_MAX+2.0);
    double u2=((double)rand()+1.0)/((double)RAND_MAX+2.0);
    double z=sqrt(-2.0*log(u1))*cos(2.0*M_PI*u2);
    return xs_float(mu+sigma*z);
}
static Value *native_random_uniform(Interp *ig, Value **a, int n) {
    (void)ig;
    double lo=(n>0)?(a[0]->tag==XS_FLOAT?a[0]->f:(double)a[0]->i):0.0;
    double hi=(n>1)?(a[1]->tag==XS_FLOAT?a[1]->f:(double)a[1]->i):1.0;
    double r=(double)rand()/((double)RAND_MAX+1.0);
    return xs_float(lo+r*(hi-lo));
}
static Value *native_random_bytes(Interp *ig, Value **a, int n) {
    (void)ig;
    int64_t cnt=(n>0&&a[0]->tag==XS_INT)?a[0]->i:0;
    Value *arr=xs_array_new();
    for (int64_t j=0;j<cnt;j++) array_push(arr->arr,xs_int((int64_t)(rand()&0xff)));
    return arr;
}
static Value *native_random_hex_str(Interp *ig, Value **a, int n) {
    (void)ig;
    int64_t cnt=(n>0&&a[0]->tag==XS_INT)?a[0]->i:0;
    char *buf=xs_malloc(cnt*2+1); buf[0]='\0';
    for (int64_t j=0;j<cnt;j++) snprintf(buf+j*2,3,"%02x",(unsigned)(rand()&0xff));
    Value *v=xs_str(buf); free(buf); return v;
}

Value *make_random_module(void) {
    XSMap *m = map_new();
    map_set(m, "int",     xs_native(native_random_int));
    map_set(m, "float",   xs_native(native_random_float));
    map_set(m, "bool",    xs_native(native_random_bool));
    map_set(m, "choice",  xs_native(native_random_choice));
    map_set(m, "choices", xs_native(native_random_choices));
    map_set(m, "shuffle", xs_native(native_random_shuffle));
    map_set(m, "shuffled",xs_native(native_random_shuffled));
    map_set(m, "sample",  xs_native(native_random_sample));
    map_set(m, "gauss",   xs_native(native_random_gauss));
    map_set(m, "uniform", xs_native(native_random_uniform));
    map_set(m, "bytes",   xs_native(native_random_bytes));
    map_set(m, "hex_str", xs_native(native_random_hex_str));
    map_set(m, "seed",    xs_native(native_random_seed));
    return xs_module(m);
}

/* ── collections module ──────────────────────────────────── */
/* Stack: returns a map with _type="Stack" and _data=[] */
static Value *collections_stack_new(Interp *i, Value **a, int n) {
    (void)i;(void)a;(void)n;
    Value *stack=xs_map_new();
    Value *type=xs_str("Stack"); map_set(stack->map,"_type",type); value_decref(type);
    Value *data=xs_array_new(); map_set(stack->map,"_data",data); value_decref(data);
    return stack;
}
/* PriorityQueue: returns a map with _type="PriorityQueue" and _data=[] */
static Value *collections_pq_new(Interp *i, Value **a, int n) {
    (void)i;(void)a;(void)n;
    Value *pq=xs_map_new();
    Value *type=xs_str("PriorityQueue"); map_set(pq->map,"_type",type); value_decref(type);
    Value *data=xs_array_new(); map_set(pq->map,"_data",data); value_decref(data);
    return pq;
}
/* Simple counter: Counter(arr) -> map of {item: count, _type: "Counter"} */
static Value *collections_counter(Interp *i, Value **a, int n) {
    (void)i;
    Value *result=xs_map_new();
    Value *type=xs_str("Counter"); map_set(result->map,"_type",type); value_decref(type);
    if (n<1||a[0]->tag!=XS_ARRAY) return result;
    XSArray *arr=a[0]->arr;
    for(int j=0;j<arr->len;j++){
        char *key=value_str(arr->items[j]);
        Value *cur=map_get(result->map,key);
        Value *next=xs_int(cur?(cur->tag==XS_INT?cur->i:0)+1:1);
        map_set(result->map,key,next); value_decref(next);
        free(key);
    }
    return result;
}
static Value *collections_deque_new(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    Value *d=xs_map_new();
    Value *t=xs_str("Deque"); map_set(d->map,"_type",t); value_decref(t);
    Value *data=xs_array_new(); map_set(d->map,"_data",data); value_decref(data);
    return d;
}
static Value *collections_set_new(Interp *ig, Value **a, int n) {
    (void)ig;
    Value *s=xs_map_new();
    Value *t=xs_str("Set"); map_set(s->map,"_type",t); value_decref(t);
    Value *data=xs_map_new(); map_set(s->map,"_data",data); value_decref(data);
    /* If array passed, pre-populate */
    if (n>0&&a[0]->tag==XS_ARRAY) {
        Value *d2=map_get(s->map,"_data");
        XSArray *arr=a[0]->arr;
        for (int j=0;j<arr->len;j++){
            char *k=value_str(arr->items[j]);
            Value *tv=value_incref(XS_TRUE_VAL);
            map_set(d2->map,k,tv); value_decref(tv);
            free(k);
        }
    }
    return s;
}
static Value *collections_ordered_map_new(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    Value *om=xs_map_new();
    Value *t=xs_str("OrderedMap"); map_set(om->map,"_type",t); value_decref(t);
    Value *keys=xs_array_new(); map_set(om->map,"_keys",keys); value_decref(keys);
    Value *data=xs_map_new(); map_set(om->map,"_data",data); value_decref(data);
    return om;
}

static Value *collections_set_simple(Interp *ig, Value **a, int n) {
    (void)ig;
    Value *result=xs_array_new();
    if (n<1||a[0]->tag!=XS_ARRAY) return result;
    XSArray *arr=a[0]->arr;
    /* track seen keys in a temporary map for dedup */
    XSMap *seen=map_new();
    for(int j=0;j<arr->len;j++){
        char *k=value_str(arr->items[j]);
        if (!map_get(seen,k)){
            Value *tv=value_incref(XS_TRUE_VAL);
            map_set(seen,k,tv); value_decref(tv);
            array_push(result->arr,value_incref(arr->items[j]));
        }
        free(k);
    }
    map_free(seen);
    return result;
}
static Value *collections_deque_simple(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    return xs_array_new();
}
static Value *collections_counter_simple(Interp *ig, Value **a, int n) {
    (void)ig;
    Value *result=xs_map_new();
    if (n<1||a[0]->tag!=XS_ARRAY) return result;
    XSArray *arr=a[0]->arr;
    for(int j=0;j<arr->len;j++){
        char *key=value_str(arr->items[j]);
        Value *cur=map_get(result->map,key);
        Value *next=xs_int(cur?(cur->tag==XS_INT?cur->i:0)+1:1);
        map_set(result->map,key,next); value_decref(next);
        free(key);
    }
    return result;
}
Value *make_collections_module(void) {
    XSMap *m=map_new();
    map_set(m,"Counter",      xs_native(collections_counter));
    map_set(m,"Stack",        xs_native(collections_stack_new));
    map_set(m,"PriorityQueue",xs_native(collections_pq_new));
    map_set(m,"Deque",        xs_native(collections_deque_new));
    map_set(m,"Set",          xs_native(collections_set_new));
    map_set(m,"OrderedMap",   xs_native(collections_ordered_map_new));
    map_set(m,"set",          xs_native(collections_set_simple));
    map_set(m,"deque",        xs_native(collections_deque_simple));
    map_set(m,"counter",      xs_native(collections_counter_simple));
    return xs_module(m);
}

/* ── process module ──────────────────────────────────────── */
static Value *native_process_pid(Interp *i, Value **a, int n) {
    (void)i;(void)a;(void)n;
    return xs_int((int64_t)getpid());
}
static Value *native_process_run(Interp *i, Value **a, int n) {
    (void)i;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    FILE *f = popen(a[0]->s, "r");
    Value *result = xs_map_new();
    if (!f) {
        Value *ok=value_incref(XS_FALSE_VAL); map_set(result->map,"ok",ok); value_decref(ok);
        Value *out=xs_str(""); map_set(result->map,"stdout",out); value_decref(out);
        Value *code=xs_int(-1); map_set(result->map,"code",code); value_decref(code);
        return result;
    }
    size_t cap=256, pos=0;
    char *buf=xs_malloc(cap);
    int c;
    while ((c=fgetc(f))!=EOF) {
        if (pos+1>=cap) { cap*=2; buf=xs_realloc(buf,cap); }
        buf[pos++]=(char)c;
    }
    buf[pos]='\0';
    int status=pclose(f);
    int code2=(status==-1)?-1:(status>>8)&0xff;
    Value *ok=code2==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
    map_set(result->map,"ok",ok); value_decref(ok);
    Value *out=xs_str(buf); free(buf); map_set(result->map,"stdout",out); value_decref(out);
    Value *cv=xs_int(code2); map_set(result->map,"code",cv); value_decref(cv);
    return result;
}
Value *make_process_module(void) {
    XSMap *m=map_new();
    map_set(m,"pid", xs_native(native_process_pid));
    map_set(m,"run", xs_native(native_process_run));
    return xs_module(m);
}

static Value *native_io_wait_for_key(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc >= 1 && args[0]->tag == XS_STR) printf("%s", args[0]->s);
    fflush(stdout);
    char buf[256]; buf[0] = '\0';
    if (fgets(buf, sizeof(buf), stdin)) {
        int len = (int)strlen(buf);
        while (len > 0 && (buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]='\0';
    }
    return xs_str(buf);
}
static Value *native_io_read_line(Interp *i, Value **args, int argc) {
    (void)i;
    if (argc >= 1 && args[0]->tag == XS_STR) printf("%s", args[0]->s);
    fflush(stdout);
    char buf[1024]; buf[0] = '\0';
    if (fgets(buf, sizeof(buf), stdin)) {
        int len = (int)strlen(buf);
        while (len > 0 && (buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]='\0';
    }
    return xs_str(buf);
}
static Value *native_io_get_key_nowait(Interp *ig, Value **args, int argc) {
    (void)ig;
    int timeout_ms = 0;
    if (argc >= 1 && args[0]->tag == XS_INT) timeout_ms = (int)args[0]->i;
#ifdef __MINGW32__
    /* Windows: poll with kbhit in a loop */
    #include <conio.h>
    DWORD deadline = GetTickCount() + (DWORD)timeout_ms;
    while (GetTickCount() < deadline || timeout_ms == 0) {
        if (_kbhit()) {
            char buf[64]; buf[0]='\0'; int bi=0;
            int c = _getch();
            if (c == 0 || c == 0xE0) { /* special key prefix */
                int c2 = _getch();
                if (c2==72) return xs_str("UP");
                if (c2==80) return xs_str("DOWN");
                if (c2==75) return xs_str("LEFT");
                if (c2==77) return xs_str("RIGHT");
                return xs_str("UNKNOWN");
            }
            buf[bi++]=(char)c; buf[bi]='\0';
            return xs_str(buf);
        }
        if (timeout_ms == 0) break;
        Sleep(10);
    }
    return value_incref(XS_NULL_VAL);
#else
    /* POSIX: use select() */
    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
    int ready = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (ready <= 0) return value_incref(XS_NULL_VAL);
    char buf[64]; buf[0] = '\0';
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf)-1);
    if (n <= 0) return value_incref(XS_NULL_VAL);
    buf[n] = '\0';
    int blen = (int)n;
    while (blen > 0 && (buf[blen-1]=='\n'||buf[blen-1]=='\r')) buf[--blen]='\0';
    if (buf[0] == '\033') {
        if (strcmp(buf, "\033[A")==0) return xs_str("UP");
        if (strcmp(buf, "\033[B")==0) return xs_str("DOWN");
        if (strcmp(buf, "\033[C")==0) return xs_str("RIGHT");
        if (strcmp(buf, "\033[D")==0) return xs_str("LEFT");
        return xs_str("ESC");
    }
    return xs_str(buf);
#endif
}
static Value *native_io_append_file(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    FILE *f=fopen(a[0]->s,"a");
    if (!f) return value_incref(XS_FALSE_VAL);
    fputs(a[1]->s,f); fclose(f);
    return value_incref(XS_TRUE_VAL);
}
static Value *native_io_read_lines(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    FILE *f=fopen(a[0]->s,"r");
    if (!f) return value_incref(XS_NULL_VAL);
    Value *arr=xs_array_new();
    char buf[4096];
    while (fgets(buf,sizeof(buf),f)) {
        int len=(int)strlen(buf);
        while (len>0&&(buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]='\0';
        array_push(arr->arr,xs_str(buf));
    }
    fclose(f); return arr;
}
static Value *native_io_write_lines(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_ARRAY) return value_incref(XS_FALSE_VAL);
    FILE *f=fopen(a[0]->s,"w");
    if (!f) return value_incref(XS_FALSE_VAL);
    XSArray *arr=a[1]->arr;
    for (int j=0;j<arr->len;j++) {
        char *s=value_str(arr->items[j]); fputs(s,f); fputc('\n',f); free(s);
    }
    fclose(f); return value_incref(XS_TRUE_VAL);
}
static Value *native_io_read_bytes(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    FILE *f=fopen(a[0]->s,"rb");
    if (!f) return value_incref(XS_NULL_VAL);
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char *buf=xs_malloc(sz+1);
    long nr = (long)fread(buf,1,sz,f); fclose(f);
    Value *arr=xs_array_new();
    for (long j=0;j<nr;j++) array_push(arr->arr,xs_int((int64_t)buf[j]));
    free(buf); return arr;
}
static Value *native_io_write_bytes(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_ARRAY) return value_incref(XS_FALSE_VAL);
    FILE *f=fopen(a[0]->s,"wb");
    if (!f) return value_incref(XS_FALSE_VAL);
    XSArray *arr=a[1]->arr;
    for (int j=0;j<arr->len;j++) {
        if (arr->items[j]->tag==XS_INT) {
            unsigned char b=(unsigned char)(arr->items[j]->i&0xff);
            fwrite(&b,1,1,f);
        }
    }
    fclose(f); return value_incref(XS_TRUE_VAL);
}
static Value *native_io_file_exists(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    return (access(a[0]->s,F_OK)==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_io_file_size(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_int(-1);
    struct stat st; if (stat(a[0]->s,&st)!=0) return xs_int(-1);
    return xs_int((int64_t)st.st_size);
}
static Value *native_io_delete_file(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    return (remove(a[0]->s)==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_io_copy_file(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    FILE *src=fopen(a[0]->s,"rb"); if (!src) return value_incref(XS_FALSE_VAL);
    FILE *dst=fopen(a[1]->s,"wb"); if (!dst){fclose(src);return value_incref(XS_FALSE_VAL);}
    char buf[8192]; size_t r;
    while ((r=fread(buf,1,sizeof(buf),src))>0) fwrite(buf,1,r,dst);
    fclose(src); fclose(dst); return value_incref(XS_TRUE_VAL);
}
static Value *native_io_rename_file(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    return (rename(a[0]->s,a[1]->s)==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static int io_mkdirs(const char *path) {
    char tmp[4096]; snprintf(tmp,sizeof(tmp),"%s",path);
    int len=(int)strlen(tmp);
    if (tmp[len-1]=='/') tmp[--len]='\0';
    for (int j=1;j<len;j++) {
        if (tmp[j]=='/') {
            tmp[j]='\0'; mkdir(tmp,0755); tmp[j]='/';
        }
    }
    return mkdir(tmp,0755);
}
static Value *native_io_make_dir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    int r=io_mkdirs(a[0]->s);
    return (r==0||errno==EEXIST)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_io_list_dir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_array_new();
    DIR *d=opendir(a[0]->s); if (!d) return xs_array_new();
    Value *arr=xs_array_new();
    struct dirent *ent;
    while ((ent=readdir(d))!=NULL) {
        if (strcmp(ent->d_name,".")==0||strcmp(ent->d_name,"..")==0) continue;
        array_push(arr->arr,xs_str(ent->d_name));
    }
    closedir(d); return arr;
}
static Value *native_io_stdin_read(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    size_t cap=256,pos=0; char *buf=xs_malloc(cap);
    int c;
    while ((c=fgetc(stdin))!=EOF) {
        if (pos+1>=cap){cap*=2;buf=xs_realloc(buf,cap);}
        buf[pos++]=(char)c;
    }
    buf[pos]='\0'; Value *v=xs_str(buf); free(buf); return v;
}
static Value *native_io_stdin_readline(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    char buf[4096]; buf[0]='\0';
    if (fgets(buf,sizeof(buf),stdin)){
        int len=(int)strlen(buf);
        while(len>0&&(buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]='\0';
    }
    return xs_str(buf);
}
static Value *native_io_is_file(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    struct stat st; if (stat(a[0]->s,&st)!=0) return value_incref(XS_FALSE_VAL);
    return S_ISREG(st.st_mode)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_io_is_dir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    struct stat st; if (stat(a[0]->s,&st)!=0) return value_incref(XS_FALSE_VAL);
    return S_ISDIR(st.st_mode)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}

/* move_file: rename, falling back to copy+delete for cross-device moves */
static Value *native_io_move_file(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    if (rename(a[0]->s,a[1]->s)==0) return value_incref(XS_TRUE_VAL);
    /* cross-device fallback: copy then delete */
    FILE *src=fopen(a[0]->s,"rb"); if (!src) return value_incref(XS_FALSE_VAL);
    FILE *dst=fopen(a[1]->s,"wb"); if (!dst){fclose(src);return value_incref(XS_FALSE_VAL);}
    char buf[8192]; size_t r;
    while ((r=fread(buf,1,sizeof(buf),src))>0) fwrite(buf,1,r,dst);
    fclose(src); fclose(dst);
    remove(a[0]->s);
    return value_incref(XS_TRUE_VAL);
}

/* temp_file: create a temp file, return its path */
static Value *native_io_temp_file(Interp *ig, Value **a, int n) {
    (void)ig;
    const char *suffix = (n>=1 && a[0]->tag==XS_STR) ? a[0]->s : "";
    const char *prefix = (n>=2 && a[1]->tag==XS_STR) ? a[1]->s : "xs_tmp_";
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char tmpl[4096];
    snprintf(tmpl,sizeof(tmpl),"%s/%sXXXXXX%s",tmpdir,prefix,suffix);
#ifndef __MINGW32__
    int fd;
    if (suffix[0]) {
        #ifdef __APPLE__
        fd = mkstemp(tmpl);
#else
        fd = mkstemps(tmpl,(int)strlen(suffix));
#endif
    } else {
        fd = mkstemp(tmpl);
    }
    if (fd<0) return value_incref(XS_NULL_VAL);
    close(fd);
#else
    if (!_mktemp(tmpl)) return value_incref(XS_NULL_VAL);
    FILE *f=fopen(tmpl,"w"); if (f) fclose(f);
#endif
    return xs_str(tmpl);
}

/* temp_dir: create a temp directory, return its path */
static Value *native_io_temp_dir(Interp *ig, Value **a, int n) {
    (void)ig;
    const char *prefix = (n>=1 && a[0]->tag==XS_STR) ? a[0]->s : "xs_tmpd_";
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    char tmpl[4096];
    snprintf(tmpl,sizeof(tmpl),"%s/%sXXXXXX",tmpdir,prefix);
#ifndef __MINGW32__
    #ifdef __APPLE__
    extern char *mkdtemp(char *);
#endif
    char *res = mkdtemp(tmpl);
    if (!res) return value_incref(XS_NULL_VAL);
    return xs_str(res);
#else
    if (!_mktemp(tmpl)) return value_incref(XS_NULL_VAL);
    mkdir(tmpl, 0700);
    return xs_str(tmpl);
#endif
}

/* file_info: return map with size, is_file, is_dir, modified, path */
static Value *native_io_file_info(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    struct stat st;
    if (stat(a[0]->s,&st)!=0) return value_incref(XS_NULL_VAL);
    Value *m = xs_map_new();
    Value *v;
    v=xs_int((int64_t)st.st_size); map_set(m->map,"size",v); value_decref(v);
    v=S_ISREG(st.st_mode)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
    map_set(m->map,"is_file",v); value_decref(v);
    v=S_ISDIR(st.st_mode)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
    map_set(m->map,"is_dir",v); value_decref(v);
    v=xs_int((int64_t)st.st_mtime); map_set(m->map,"modified",v); value_decref(v);
    v=xs_str(a[0]->s); map_set(m->map,"path",v); value_decref(v);
    return m;
}

/* stdin_lines: read all stdin lines as array */
static Value *native_io_stdin_lines(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    Value *arr=xs_array_new();
    char buf[4096];
    while (fgets(buf,sizeof(buf),stdin)) {
        int len2=(int)strlen(buf);
        while (len2>0&&(buf[len2-1]=='\n'||buf[len2-1]=='\r')) buf[--len2]='\0';
        array_push(arr->arr,xs_str(buf));
    }
    return arr;
}

/* glob: pattern matching for file paths */
static Value *native_io_glob(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_array_new();
#ifndef __MINGW32__
    glob_t g; memset(&g,0,sizeof(g));
    Value *arr=xs_array_new();
    if (glob(a[0]->s,0,NULL,&g)==0) {
        for (size_t j=0;j<g.gl_pathc;j++) array_push(arr->arr,xs_str(g.gl_pathv[j]));
    }
    globfree(&g); return arr;
#else
    return xs_array_new();
#endif
}

/* symlink: create a symbolic link */
static Value *native_io_symlink(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
#ifndef __MINGW32__
    return (symlink(a[0]->s,a[1]->s)==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
#else
    return value_incref(XS_FALSE_VAL);
#endif
}

/* stdout/stderr sub-module helpers */
static Value *native_io_stdout_write(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n>=1 && a[0]->tag==XS_STR) fputs(a[0]->s,stdout);
    return value_incref(XS_NULL_VAL);
}
static Value *native_io_stdout_writeln(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n>=1 && a[0]->tag==XS_STR) { fputs(a[0]->s,stdout); fputc('\n',stdout); }
    else fputc('\n',stdout);
    return value_incref(XS_NULL_VAL);
}
static Value *native_io_stdout_flush(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    fflush(stdout);
    return value_incref(XS_NULL_VAL);
}
static Value *native_io_stderr_write(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n>=1 && a[0]->tag==XS_STR) fputs(a[0]->s,stderr);
    return value_incref(XS_NULL_VAL);
}
static Value *native_io_stderr_writeln(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n>=1 && a[0]->tag==XS_STR) { fputs(a[0]->s,stderr); fputc('\n',stderr); }
    else fputc('\n',stderr);
    return value_incref(XS_NULL_VAL);
}
static Value *native_io_stderr_flush(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    fflush(stderr);
    return value_incref(XS_NULL_VAL);
}

Value *make_io_module(void) {
    XSMap *m = map_new();
    /* file operations */
    map_set(m,"read_file",      xs_native(native_io_read_file));
    map_set(m,"write_file",     xs_native(native_io_write_file));
    map_set(m,"append_file",    xs_native(native_io_append_file));
    map_set(m,"read_lines",     xs_native(native_io_read_lines));
    map_set(m,"write_lines",    xs_native(native_io_write_lines));
    map_set(m,"read_bytes",     xs_native(native_io_read_bytes));
    map_set(m,"write_bytes",    xs_native(native_io_write_bytes));
    /* file info */
    map_set(m,"file_exists",    xs_native(native_io_file_exists));
    map_set(m,"exists",         xs_native(native_io_file_exists));
    map_set(m,"file_size",      xs_native(native_io_file_size));
    map_set(m,"size",           xs_native(native_io_file_size));
    map_set(m,"file_info",      xs_native(native_io_file_info));
    map_set(m,"is_file",        xs_native(native_io_is_file));
    map_set(m,"is_dir",         xs_native(native_io_is_dir));
    /* file manipulation */
    map_set(m,"delete_file",    xs_native(native_io_delete_file));
    map_set(m,"copy_file",      xs_native(native_io_copy_file));
    map_set(m,"move_file",      xs_native(native_io_move_file));
    map_set(m,"rename_file",    xs_native(native_io_rename_file));
    map_set(m,"symlink",        xs_native(native_io_symlink));
    /* directories */
    map_set(m,"make_dir",       xs_native(native_io_make_dir));
    map_set(m,"list_dir",       xs_native(native_io_list_dir));
    map_set(m,"glob",           xs_native(native_io_glob));
    /* temp files */
    map_set(m,"temp_file",      xs_native(native_io_temp_file));
    map_set(m,"temp_dir",       xs_native(native_io_temp_dir));
    /* stdin */
    map_set(m,"stdin_read",     xs_native(native_io_stdin_read));
    map_set(m,"stdin_readline", xs_native(native_io_stdin_readline));
    map_set(m,"stdin_lines",    xs_native(native_io_stdin_lines));
    /* keyboard */
    map_set(m,"wait_for_key",   xs_native(native_io_wait_for_key));
    map_set(m,"read_line",      xs_native(native_io_read_line));
    map_set(m,"get_key_nowait", xs_native(native_io_get_key_nowait));
    /* stdout sub-module */
    Value *out_m=xs_map_new();
    map_set(out_m->map,"write",   xs_native(native_io_stdout_write));
    map_set(out_m->map,"writeln", xs_native(native_io_stdout_writeln));
    map_set(out_m->map,"flush",   xs_native(native_io_stdout_flush));
    map_set(m,"stdout",out_m); value_decref(out_m);
    /* stderr sub-module */
    Value *err_m=xs_map_new();
    map_set(err_m->map,"write",   xs_native(native_io_stderr_write));
    map_set(err_m->map,"writeln", xs_native(native_io_stderr_writeln));
    map_set(err_m->map,"flush",   xs_native(native_io_stderr_flush));
    map_set(m,"stderr",err_m); value_decref(err_m);
    return xs_module(m);
}

/* ── os module ───────────────────────────────────────────── */
static Value *native_os_cwd(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    char buf[4096]; if (!getcwd(buf,sizeof(buf))) return value_incref(XS_NULL_VAL);
    return xs_str(buf);
}
static Value *native_os_chdir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    return (chdir(a[0]->s)==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_os_home(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    const char *h=getenv("HOME"); return h?xs_str(h):xs_str("");
}
static Value *native_os_tempdir(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    const char *t=getenv("TMPDIR"); return xs_str(t?t:"/tmp");
}
static Value *native_os_mkdir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    int r=io_mkdirs(a[0]->s);
    return (r==0||errno==EEXIST)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_os_rmdir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    return (rmdir(a[0]->s)==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_os_remove(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    return (unlink(a[0]->s)==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_os_rename(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    return (rename(a[0]->s,a[1]->s)==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_os_exists(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    return (access(a[0]->s,F_OK)==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_os_is_file(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    struct stat st; if (stat(a[0]->s,&st)!=0) return value_incref(XS_FALSE_VAL);
    return S_ISREG(st.st_mode)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_os_is_dir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    struct stat st; if (stat(a[0]->s,&st)!=0) return value_incref(XS_FALSE_VAL);
    return S_ISDIR(st.st_mode)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_os_cpu_count(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    #ifdef _SC_NPROCESSORS_ONLN
    long c=sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(__APPLE__)
    long c=1; /* fallback */
#else
    long c=1;
#endif
    return xs_int(c>0?c:1);
}
static Value *native_os_pid(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    return xs_int((int64_t)getpid());
}
static Value *native_os_ppid(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    return xs_int((int64_t)getppid());
}
static Value *native_os_exit(Interp *ig, Value **a, int n) {
    (void)ig;
    int code=(n>0&&a[0]->tag==XS_INT)?(int)a[0]->i:0;
    exit(code);
}
static Value *native_os_list_dir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_array_new();
    DIR *d=opendir(a[0]->s); if (!d) return xs_array_new();
    Value *arr=xs_array_new();
    struct dirent *ent;
    while ((ent=readdir(d))!=NULL) {
        if (strcmp(ent->d_name,".")==0||strcmp(ent->d_name,"..")==0) continue;
        array_push(arr->arr,xs_str(ent->d_name));
    }
    closedir(d); return arr;
}
static Value *native_os_glob(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_array_new();
    glob_t g; memset(&g,0,sizeof(g));
    Value *arr=xs_array_new();
    if (glob(a[0]->s,0,NULL,&g)==0) {
        for (size_t j=0;j<g.gl_pathc;j++) array_push(arr->arr,xs_str(g.gl_pathv[j]));
    }
    globfree(&g); return arr;
}
static Value *native_os_env_get(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    const char *v=getenv(a[0]->s);
    if (!v) return (n>1)?value_incref(a[1]):value_incref(XS_NULL_VAL);
    return xs_str(v);
}
static Value *native_os_env_set(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    return (setenv(a[0]->s,a[1]->s,1)==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_os_env_has(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    return getenv(a[0]->s)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
#ifndef _WIN32
extern char **environ;
#endif
static Value *native_os_env_all(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    Value *m=xs_map_new();
    for (char **ep=environ;ep&&*ep;ep++) {
        char *eq=strchr(*ep,'=');
        if (!eq) continue;
        char key[256]; int klen=(int)(eq-*ep);
        if (klen>=256) klen=255;
        strncpy(key,*ep,klen); key[klen]='\0';
        Value *v=xs_str(eq+1); map_set(m->map,key,v); value_decref(v);
    }
    return m;
}

Value *make_os_module(Interp *ig) {
    XSMap *m=map_new();
    /* args - empty array for now */
    Value *args_arr=xs_array_new(); map_set(m,"args",args_arr); value_decref(args_arr);
    map_set(m,"cwd",      xs_native(native_os_cwd));
    map_set(m,"chdir",    xs_native(native_os_chdir));
    map_set(m,"home",     xs_native(native_os_home));
    map_set(m,"tempdir",  xs_native(native_os_tempdir));
    map_set(m,"mkdir",    xs_native(native_os_mkdir));
    map_set(m,"rmdir",    xs_native(native_os_rmdir));
    map_set(m,"remove",   xs_native(native_os_remove));
    map_set(m,"rename",   xs_native(native_os_rename));
    map_set(m,"exists",   xs_native(native_os_exists));
    map_set(m,"is_file",  xs_native(native_os_is_file));
    map_set(m,"is_dir",   xs_native(native_os_is_dir));
    map_set(m,"cpu_count",xs_native(native_os_cpu_count));
    map_set(m,"pid",      xs_native(native_os_pid));
    map_set(m,"ppid",     xs_native(native_os_ppid));
    map_set(m,"exit",     xs_native(native_os_exit));
    map_set(m,"list_dir", xs_native(native_os_list_dir));
    map_set(m,"glob",     xs_native(native_os_glob));
    /* platform / sep */
#ifdef __APPLE__
    { Value *v=xs_str("darwin"); map_set(m,"platform",v); value_decref(v); }
#else
    { Value *v=xs_str("linux"); map_set(m,"platform",v); value_decref(v); }
#endif
    { Value *v=xs_str("/"); map_set(m,"sep",v); value_decref(v); }
    /* env as a callable (getenv) + helper functions at top level */
    map_set(m,"env",      xs_native(native_os_env_get));
    map_set(m,"getenv",   xs_native(native_os_env_get));
    map_set(m,"setenv",   xs_native(native_os_env_set));
    map_set(m,"hasenv",   xs_native(native_os_env_has));
    map_set(m,"environ",  xs_native(native_os_env_all));
    (void)ig;
    return xs_module(m);
}

/* ── json module ─────────────────────────────────────────── */
typedef struct { const char *s; int pos; } JsonParser;
static Value *json_parse_value(JsonParser *p);
static void json_skip_ws(JsonParser *p) {
    while (p->s[p->pos]==' '||p->s[p->pos]=='\t'||p->s[p->pos]=='\n'||p->s[p->pos]=='\r') p->pos++;
}
static Value *json_parse_string(JsonParser *p) {
    if (p->s[p->pos]!='"') return NULL;
    p->pos++;
    int cap=64; char *buf=xs_malloc(cap); int ri=0;
    while (p->s[p->pos]&&p->s[p->pos]!='"') {
        if (ri+8>=cap){cap*=2;buf=xs_realloc(buf,cap);}
        if (p->s[p->pos]=='\\') {
            p->pos++;
            switch (p->s[p->pos]) {
            case '"': buf[ri++]='"'; break;
            case '\\': buf[ri++]='\\'; break;
            case '/': buf[ri++]='/'; break;
            case 'n': buf[ri++]='\n'; break;
            case 't': buf[ri++]='\t'; break;
            case 'r': buf[ri++]='\r'; break;
            case 'b': buf[ri++]='\b'; break;
            case 'f': buf[ri++]='\f'; break;
            case 'u': {
                /* \uXXXX — full UTF-8 encoding with surrogate pair support */
                char hex[5]={p->s[p->pos+1],p->s[p->pos+2],p->s[p->pos+3],p->s[p->pos+4],0};
                unsigned cp=(unsigned)strtoul(hex,NULL,16);
                p->pos+=4;
                /* Handle surrogate pairs: \uD800-\uDBFF followed by \uDC00-\uDFFF */
                if (cp>=0xD800 && cp<=0xDBFF && p->s[p->pos+1]=='\\' && p->s[p->pos+2]=='u') {
                    char hex2[5]={p->s[p->pos+3],p->s[p->pos+4],p->s[p->pos+5],p->s[p->pos+6],0};
                    unsigned lo=(unsigned)strtoul(hex2,NULL,16);
                    if (lo>=0xDC00 && lo<=0xDFFF) {
                        cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00);
                        p->pos+=6; /* skip \uXXXX of low surrogate */
                    }
                }
                /* Encode codepoint as UTF-8 */
                if (ri+4>=cap){cap*=2;buf=xs_realloc(buf,cap);}
                if (cp<0x80) {
                    buf[ri++]=(char)cp;
                } else if (cp<0x800) {
                    buf[ri++]=(char)(0xC0|(cp>>6));
                    buf[ri++]=(char)(0x80|(cp&0x3F));
                } else if (cp<0x10000) {
                    buf[ri++]=(char)(0xE0|(cp>>12));
                    buf[ri++]=(char)(0x80|((cp>>6)&0x3F));
                    buf[ri++]=(char)(0x80|(cp&0x3F));
                } else if (cp<=0x10FFFF) {
                    buf[ri++]=(char)(0xF0|(cp>>18));
                    buf[ri++]=(char)(0x80|((cp>>12)&0x3F));
                    buf[ri++]=(char)(0x80|((cp>>6)&0x3F));
                    buf[ri++]=(char)(0x80|(cp&0x3F));
                } else {
                    buf[ri++]='?'; /* invalid codepoint */
                }
                break;
            }
            default: buf[ri++]=p->s[p->pos]; break;
            }
        } else {
            buf[ri++]=p->s[p->pos];
        }
        p->pos++;
    }
    if (p->s[p->pos]=='"') p->pos++;
    buf[ri]='\0'; Value *v=xs_str(buf); free(buf); return v;
}
static Value *json_parse_number(JsonParser *p) {
    const char *start=p->s+p->pos;
    int is_float=0;
    if (p->s[p->pos]=='-') p->pos++;
    while (isdigit((unsigned char)p->s[p->pos])) p->pos++;
    if (p->s[p->pos]=='.'){is_float=1;p->pos++;while(isdigit((unsigned char)p->s[p->pos]))p->pos++;}
    if (p->s[p->pos]=='e'||p->s[p->pos]=='E'){is_float=1;p->pos++;if(p->s[p->pos]=='+'||p->s[p->pos]=='-')p->pos++;while(isdigit((unsigned char)p->s[p->pos]))p->pos++;}
    if (is_float) return xs_float(strtod(start,NULL));
    return xs_int((int64_t)strtoll(start,NULL,10));
}
static Value *json_parse_array(JsonParser *p) {
    p->pos++; /* skip [ */
    json_skip_ws(p);
    Value *arr=xs_array_new();
    if (p->s[p->pos]==']'){p->pos++;return arr;}
    while (p->s[p->pos]) {
        json_skip_ws(p);
        Value *v=json_parse_value(p);
        if (!v) break;
        array_push(arr->arr,v);
        json_skip_ws(p);
        if (p->s[p->pos]==','){p->pos++;continue;}
        if (p->s[p->pos]==']'){p->pos++;break;}
        break;
    }
    return arr;
}
static Value *json_parse_object(JsonParser *p) {
    p->pos++; /* skip { */
    json_skip_ws(p);
    Value *m=xs_map_new();
    if (p->s[p->pos]=='}'){p->pos++;return m;}
    while (p->s[p->pos]) {
        json_skip_ws(p);
        if (p->s[p->pos]!='"') break;
        Value *key=json_parse_string(p);
        if (!key) break;
        json_skip_ws(p);
        if (p->s[p->pos]!=':'){value_decref(key);break;}
        p->pos++;
        json_skip_ws(p);
        Value *val=json_parse_value(p);
        if (!val) val=value_incref(XS_NULL_VAL);
        map_set(m->map,key->s,val); value_decref(val);
        value_decref(key);
        json_skip_ws(p);
        if (p->s[p->pos]==','){p->pos++;continue;}
        if (p->s[p->pos]=='}'){p->pos++;break;}
        break;
    }
    return m;
}
static Value *json_parse_value(JsonParser *p) {
    json_skip_ws(p);
    char c=p->s[p->pos];
    if (c=='"') return json_parse_string(p);
    if (c=='[') return json_parse_array(p);
    if (c=='{') return json_parse_object(p);
    if (c=='-'||isdigit((unsigned char)c)) return json_parse_number(p);
    if (strncmp(p->s+p->pos,"true",4)==0){p->pos+=4;return value_incref(XS_TRUE_VAL);}
    if (strncmp(p->s+p->pos,"false",5)==0){p->pos+=5;return value_incref(XS_FALSE_VAL);}
    if (strncmp(p->s+p->pos,"null",4)==0){p->pos+=4;return value_incref(XS_NULL_VAL);}
    return NULL;
}

static void json_stringify_val(Value *v, int indent, int depth, char **out, int *len, int *cap);
static void json_append(char **out, int *len, int *cap, const char *s, int slen) {
    if (*len+slen+1>*cap){*cap=(*cap)*2+slen+64;*out=xs_realloc(*out,*cap);}
    memcpy(*out+*len,s,slen); *len+=slen; (*out)[*len]='\0';
}
static void json_append_str_escaped(const char *s, char **out, int *len, int *cap) {
    json_append(out,len,cap,"\"",1);
    for (const char *p=s;*p;p++){
        if (*p=='"'){json_append(out,len,cap,"\\\"",2);}
        else if(*p=='\\'){json_append(out,len,cap,"\\\\",2);}
        else if(*p=='\n'){json_append(out,len,cap,"\\n",2);}
        else if(*p=='\t'){json_append(out,len,cap,"\\t",2);}
        else if(*p=='\r'){json_append(out,len,cap,"\\r",2);}
        else { char cb[2]={*p,0}; json_append(out,len,cap,cb,1);}
    }
    json_append(out,len,cap,"\"",1);
}
static void json_indent_line(int indent, int depth, char **out, int *len, int *cap) {
    if (indent<=0) return;
    json_append(out,len,cap,"\n",1);
    for (int j=0;j<depth*indent;j++) json_append(out,len,cap," ",1);
}
static void json_stringify_val(Value *v, int indent, int depth, char **out, int *len, int *cap) {
    if (!v||v->tag==XS_NULL){json_append(out,len,cap,"null",4);return;}
    if (v->tag==XS_BOOL){
        if (v->i) json_append(out,len,cap,"true",4);
        else json_append(out,len,cap,"false",5);
        return;
    }
    if (v->tag==XS_INT){
        char buf[32]; int bl=snprintf(buf,sizeof(buf),"%lld",(long long)v->i);
        json_append(out,len,cap,buf,bl); return;
    }
    if (v->tag==XS_FLOAT){
        char buf[64]; int bl=snprintf(buf,sizeof(buf),"%g",v->f);
        json_append(out,len,cap,buf,bl); return;
    }
    if (v->tag==XS_STR){json_append_str_escaped(v->s,out,len,cap);return;}
    if (v->tag==XS_CHAR){
        char cb[2]={v->s?v->s[0]:0,0};
        json_append_str_escaped(cb,out,len,cap); return;
    }
    if (v->tag==XS_ARRAY||v->tag==XS_TUPLE){
        json_append(out,len,cap,"[",1);
        XSArray *arr=v->arr;
        for (int j=0;j<arr->len;j++){
            if (j) json_append(out,len,cap,",",1);
            if (indent>0) json_indent_line(indent,depth+1,out,len,cap);
            json_stringify_val(arr->items[j],indent,depth+1,out,len,cap);
        }
        if (indent>0&&arr->len>0) json_indent_line(indent,depth,out,len,cap);
        json_append(out,len,cap,"]",1); return;
    }
    if (v->tag==XS_MAP||v->tag==XS_MODULE){
        json_append(out,len,cap,"{",1);
        int nk=0; char **ks=map_keys(v->map,&nk);
        for (int j=0;j<nk;j++){
            if (j) json_append(out,len,cap,",",1);
            if (indent>0) json_indent_line(indent,depth+1,out,len,cap);
            json_append_str_escaped(ks[j],out,len,cap);
            json_append(out,len,cap,":",1);
            if (indent>0) json_append(out,len,cap," ",1);
            Value *mv=map_get(v->map,ks[j]);
            json_stringify_val(mv,indent,depth+1,out,len,cap);
            free(ks[j]);
        }
        free(ks);
        if (indent>0&&nk>0) json_indent_line(indent,depth,out,len,cap);
        json_append(out,len,cap,"}",1); return;
    }
    json_append(out,len,cap,"null",4);
}

static Value *native_json_parse(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    JsonParser p={a[0]->s,0};
    Value *v=json_parse_value(&p);
    return v?v:value_incref(XS_NULL_VAL);
}
static Value *native_json_stringify(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_str("null");
    int indent=(n>=2&&a[1]->tag==XS_INT)?(int)a[1]->i:0;
    int cap=256,len2=0; char *out=xs_malloc(cap); out[0]='\0';
    json_stringify_val(a[0],indent,0,&out,&len2,&cap);
    Value *v=xs_str(out); free(out); return v;
}
static Value *native_json_pretty(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_str("null");
    int cap=256,len2=0; char *out=xs_malloc(cap); out[0]='\0';
    json_stringify_val(a[0],2,0,&out,&len2,&cap);
    Value *v=xs_str(out); free(out); return v;
}
static Value *native_json_valid(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    JsonParser p={a[0]->s,0};
    Value *v=json_parse_value(&p);
    if (v){value_decref(v);return value_incref(XS_TRUE_VAL);}
    return value_incref(XS_FALSE_VAL);
}
static Value *native_json_parse_safe(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    JsonParser p={a[0]->s,0};
    Value *v=json_parse_value(&p);
    return v?v:value_incref(XS_NULL_VAL);
}
Value *make_json_module(void) {
    XSMap *m=map_new();
    map_set(m,"parse",       xs_native(native_json_parse));
    map_set(m,"stringify",   xs_native(native_json_stringify));
    map_set(m,"pretty",      xs_native(native_json_pretty));
    map_set(m,"valid",       xs_native(native_json_valid));
    map_set(m,"parse_safe",  xs_native(native_json_parse_safe));
    return xs_module(m);
}

/* io.read_json / io.write_json — defined here because they depend on json helpers */
Value *native_io_read_json(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    FILE *f=fopen(a[0]->s,"r");
    if (!f) return value_incref(XS_NULL_VAL);
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=xs_malloc(sz+1);
    long nr=(long)fread(buf,1,sz,f); fclose(f); buf[nr]='\0';
    JsonParser p={buf,0};
    json_skip_ws(&p);
    Value *v=json_parse_value(&p);
    free(buf);
    return v?v:value_incref(XS_NULL_VAL);
}
Value *native_io_write_json(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    int indent=(n>=3&&a[2]->tag==XS_INT)?(int)a[2]->i:2;
    char *out=xs_malloc(256); int len2=0,cap=256;
    json_stringify_val(a[1],indent,0,&out,&len2,&cap);
    out[len2]='\0';
    FILE *f=fopen(a[0]->s,"w");
    if (!f){free(out);return value_incref(XS_FALSE_VAL);}
    fputs(out,f); fputc('\n',f); fclose(f); free(out);
    return value_incref(XS_TRUE_VAL);
}

/* ── log module ──────────────────────────────────────────── */
static int xs_log_level = 1; /* default: info */
#define LOG_MSG(level_val, prefix) \
static Value *native_log_##prefix(Interp *ig, Value **a, int n) { \
    (void)ig; \
    if (xs_log_level > level_val) return value_incref(XS_NULL_VAL); \
    char *s=(n>0)?value_str(a[0]):xs_strdup(""); \
    fprintf(stderr,"[" #prefix "] %s\n",s); free(s); \
    return value_incref(XS_NULL_VAL); \
}
LOG_MSG(0, debug)
LOG_MSG(1, info)
LOG_MSG(2, warn)
LOG_MSG(3, error)
#undef LOG_MSG
static Value *native_log_fatal(Interp *ig, Value **a, int n) {
    (void)ig;
    char *s=(n>0)?value_str(a[0]):xs_strdup("fatal");
    fprintf(stderr,"[FATAL] %s\n",s); free(s);
    exit(1);
}
static Value *native_log_set_level(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n>0&&a[0]->tag==XS_INT) xs_log_level=(int)a[0]->i;
    return value_incref(XS_NULL_VAL);
}
Value *make_log_module(void) {
    XSMap *m=map_new();
    map_set(m,"debug",     xs_native(native_log_debug));
    map_set(m,"info",      xs_native(native_log_info));
    map_set(m,"warn",      xs_native(native_log_warn));
    map_set(m,"error",     xs_native(native_log_error));
    map_set(m,"fatal",     xs_native(native_log_fatal));
    map_set(m,"set_level", xs_native(native_log_set_level));
    return xs_module(m);
}

/* ── fmt module ──────────────────────────────────────────── */
static Value *native_fmt_number(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_str("0");
    double v=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    int dec=(n>=2&&a[1]->tag==XS_INT)?(int)a[1]->i:2;
    char fmt2[32]; snprintf(fmt2,sizeof(fmt2),"%%.%df",dec);
    char buf[128]; snprintf(buf,sizeof(buf),fmt2,v);
    return xs_str(buf);
}
static Value *native_fmt_hex(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_str("0x0");
    int64_t v=(a[0]->tag==XS_INT)?a[0]->i:(int64_t)a[0]->f;
    char buf[64]; snprintf(buf,sizeof(buf),"0x%llx",(long long)v);
    return xs_str(buf);
}
static Value *native_fmt_bin(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_str("0b0");
    int64_t v=(a[0]->tag==XS_INT)?a[0]->i:(int64_t)a[0]->f;
    if (v==0) return xs_str("0b0");
    /* buf: sign(1) + "0b"(2) + 64 bits + null = 68 */
    char buf[68]; int pos=66; buf[66]='\0';
    uint64_t uv=(uint64_t)(v<0?-v:v);
    while (uv){buf[--pos]=(uv&1)?'1':'0';uv>>=1;}
    buf[--pos]='b'; buf[--pos]='0';
    if (v<0) buf[--pos]='-';
    return xs_str(buf+pos);
}
static Value *native_fmt_pad(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return n>0?(value_incref(a[0])):xs_str("");
    const char *s=(a[0]->tag==XS_STR)?a[0]->s:"";
    int width=(a[1]->tag==XS_INT)?(int)a[1]->i:0;
    char fill=(n>=3&&a[2]->tag==XS_STR&&a[2]->s[0])?a[2]->s[0]:' ';
    int slen=(int)strlen(s);
    if (slen>=width) return xs_str(s);
    char *r=xs_malloc(width+1);
    int pad=width-slen;
    for(int j=0;j<pad;j++) r[j]=fill;
    memcpy(r+pad,s,slen); r[width]='\0';
    Value *v=xs_str(r); free(r); return v;
}
static Value *native_fmt_comma(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_str("0");
    char buf[64];
    if (a[0]->tag==XS_INT) snprintf(buf,sizeof(buf),"%lld",(long long)a[0]->i);
    else snprintf(buf,sizeof(buf),"%.0f",(a[0]->tag==XS_FLOAT)?a[0]->f:0.0);
    /* Insert commas */
    int len2=(int)strlen(buf);
    int neg=(buf[0]=='-'); int start=neg?1:0;
    int digits=len2-start;
    int commas=(digits-1)/3;
    char *r=xs_malloc(len2+commas+1); int ri=0;
    if (neg) r[ri++]='-';
    int first=digits%3; if(first==0)first=3;
    for(int j=start;j<len2;j++){
        if(j>start&&(j-start)%3==first%3&&commas>0){r[ri++]=',';commas--;}
        r[ri++]=buf[j];
    }
    r[ri]='\0'; Value *v=xs_str(r); free(r); return v;
}
static Value *native_fmt_filesize(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_str("0 B");
    double v=(a[0]->tag==XS_INT)?(double)a[0]->i:a[0]->f;
    const char *units[]={"B","KB","MB","GB","TB","PB"};
    int ui=0;
    while (v>=1024.0&&ui<5){v/=1024.0;ui++;}
    char buf[64];
    if (ui==0) snprintf(buf,sizeof(buf),"%.0f %s",v,units[ui]);
    else snprintf(buf,sizeof(buf),"%.2f %s",v,units[ui]);
    return xs_str(buf);
}
static Value *native_fmt_ordinal(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1) return xs_str("0th");
    int64_t v=(a[0]->tag==XS_INT)?a[0]->i:(int64_t)a[0]->f;
    const char *suf;
    int64_t abs_v=v<0?-v:v;
    if (abs_v%100>=11&&abs_v%100<=13) suf="th";
    else switch(abs_v%10){
        case 1: suf="st"; break;
        case 2: suf="nd"; break;
        case 3: suf="rd"; break;
        default: suf="th"; break;
    }
    char buf[32]; snprintf(buf,sizeof(buf),"%lld%s",(long long)v,suf);
    return xs_str(buf);
}
static Value *native_fmt_pluralize(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2) return xs_str("");
    int64_t cnt=(a[0]->tag==XS_INT)?a[0]->i:(int64_t)a[0]->f;
    const char *word=(a[1]->tag==XS_STR)?a[1]->s:"";
    const char *plural=(n>=3&&a[2]->tag==XS_STR)?a[2]->s:NULL;
    char buf[512];
    if (cnt==1) snprintf(buf,sizeof(buf),"%lld %s",(long long)cnt,word);
    else if (plural) snprintf(buf,sizeof(buf),"%lld %s",(long long)cnt,plural);
    else snprintf(buf,sizeof(buf),"%lld %ss",(long long)cnt,word);
    return xs_str(buf);
}
static Value *native_fmt_sprintf(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const char *fmt = a[0]->s;
    int flen = (int)strlen(fmt);
    /* estimate output size */
    int cap = flen + 256;
    char *buf = xs_malloc(cap);
    int bi = 0, ai = 1;
    for (int i = 0; i < flen; i++) {
        if (fmt[i] == '{' && i+1 < flen && fmt[i+1] == '}') {
            if (ai < n) {
                char *s = value_repr(a[ai++]);
                int slen = (int)strlen(s);
                /* strip quotes from string repr */
                if (slen >= 2 && s[0] == '"' && s[slen-1] == '"') {
                    s[slen-1] = '\0';
                    char *inner = s + 1;
                    slen -= 2;
                    while (bi + slen + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                    memcpy(buf + bi, inner, slen); bi += slen;
                } else {
                    while (bi + slen + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
                    memcpy(buf + bi, s, slen); bi += slen;
                }
                free(s);
            }
            i++; /* skip the '}' */
        } else {
            if (bi + 2 > cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[bi++] = fmt[i];
        }
    }
    buf[bi] = '\0';
    Value *r = xs_str(buf); free(buf); return r;
}

static Value *native_fmt_pad_left(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2) return n > 0 ? value_incref(a[0]) : xs_str("");
    const char *s = (a[0]->tag == XS_STR) ? a[0]->s : "";
    int width = (a[1]->tag == XS_INT) ? (int)a[1]->i : 0;
    char fill = (n >= 3 && a[2]->tag == XS_STR && a[2]->s[0]) ? a[2]->s[0] : ' ';
    int slen = (int)strlen(s);
    if (slen >= width) return xs_str(s);
    char *r = xs_malloc(width + 1);
    int pad = width - slen;
    for (int j = 0; j < pad; j++) r[j] = fill;
    memcpy(r + pad, s, slen); r[width] = '\0';
    Value *v = xs_str(r); free(r); return v;
}

static Value *native_fmt_pad_right(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2) return n > 0 ? value_incref(a[0]) : xs_str("");
    const char *s = (a[0]->tag == XS_STR) ? a[0]->s : "";
    int width = (a[1]->tag == XS_INT) ? (int)a[1]->i : 0;
    char fill = (n >= 3 && a[2]->tag == XS_STR && a[2]->s[0]) ? a[2]->s[0] : ' ';
    int slen = (int)strlen(s);
    if (slen >= width) return xs_str(s);
    char *r = xs_malloc(width + 1);
    memcpy(r, s, slen);
    for (int j = slen; j < width; j++) r[j] = fill;
    r[width] = '\0';
    Value *v = xs_str(r); free(r); return v;
}

static Value *native_fmt_center(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2) return n > 0 ? value_incref(a[0]) : xs_str("");
    const char *s = (a[0]->tag == XS_STR) ? a[0]->s : "";
    int width = (a[1]->tag == XS_INT) ? (int)a[1]->i : 0;
    char fill = (n >= 3 && a[2]->tag == XS_STR && a[2]->s[0]) ? a[2]->s[0] : ' ';
    int slen = (int)strlen(s);
    if (slen >= width) return xs_str(s);
    char *r = xs_malloc(width + 1);
    int total_pad = width - slen;
    int left_pad = total_pad / 2;
    int right_pad = total_pad - left_pad;
    for (int j = 0; j < left_pad; j++) r[j] = fill;
    memcpy(r + left_pad, s, slen);
    for (int j = 0; j < right_pad; j++) r[left_pad + slen + j] = fill;
    r[width] = '\0';
    Value *v = xs_str(r); free(r); return v;
}

static Value *native_fmt_oct(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1) return xs_str("0o0");
    int64_t v = (a[0]->tag == XS_INT) ? a[0]->i : (int64_t)a[0]->f;
    char buf[64]; snprintf(buf, sizeof(buf), "0o%llo", (long long)v);
    return xs_str(buf);
}

Value *make_fmt_module(void) {
    XSMap *m=map_new();
    map_set(m,"sprintf",   xs_native(native_fmt_sprintf));
    map_set(m,"pad_left",  xs_native(native_fmt_pad_left));
    map_set(m,"pad_right", xs_native(native_fmt_pad_right));
    map_set(m,"center",    xs_native(native_fmt_center));
    map_set(m,"number",    xs_native(native_fmt_number));
    map_set(m,"hex",       xs_native(native_fmt_hex));
    map_set(m,"bin",       xs_native(native_fmt_bin));
    map_set(m,"oct",       xs_native(native_fmt_oct));
    map_set(m,"pad",       xs_native(native_fmt_pad));
    map_set(m,"comma",     xs_native(native_fmt_comma));
    map_set(m,"filesize",  xs_native(native_fmt_filesize));
    map_set(m,"ordinal",   xs_native(native_fmt_ordinal));
    map_set(m,"pluralize", xs_native(native_fmt_pluralize));
    return xs_module(m);
}

/* ── test module ─────────────────────────────────────────── */
static int test_passed_count = 0;
static int test_failed_count = 0;

static Value *native_test_assert(Interp *ig, Value **a, int n) {
    (void)ig;
    int cond=(n>0&&value_truthy(a[0]));
    const char *msg=(n>1&&a[1]->tag==XS_STR)?a[1]->s:"assertion failed";
    if (cond) { test_passed_count++; }
    else { test_failed_count++; fprintf(stderr,"[FAIL] %s\n",msg); }
    return value_incref(XS_NULL_VAL);
}
static Value *native_test_assert_eq(Interp *ig, Value **a, int n) {
    (void)ig;
    int eq=(n>=2&&value_equal(a[0],a[1]));
    const char *msg=(n>=3&&a[2]->tag==XS_STR)?a[2]->s:"assert_eq failed";
    if (eq) { test_passed_count++; }
    else {
        test_failed_count++;
        char *s1=value_repr(a[0]); char *s2=value_repr(a[1]);
        fprintf(stderr,"[FAIL] %s: %s != %s\n",msg,s1,s2);
        free(s1); free(s2);
    }
    return value_incref(XS_NULL_VAL);
}
static Value *native_test_assert_ne(Interp *ig, Value **a, int n) {
    (void)ig;
    int ne=(n>=2&&!value_equal(a[0],a[1]));
    const char *msg=(n>=3&&a[2]->tag==XS_STR)?a[2]->s:"assert_ne failed";
    if (ne) test_passed_count++;
    else {
        test_failed_count++;
        char *s1=value_repr(a[0]);
        fprintf(stderr,"[FAIL] %s: values are equal: %s\n",msg,s1);
        free(s1);
    }
    return value_incref(XS_NULL_VAL);
}
static Value *native_test_assert_gt(Interp *ig, Value **a, int n) {
    (void)ig;
    int ok=(n>=2&&value_cmp(a[0],a[1])>0);
    const char *msg=(n>=3&&a[2]->tag==XS_STR)?a[2]->s:"assert_gt failed";
    if (ok) test_passed_count++;
    else { test_failed_count++; fprintf(stderr,"[FAIL] %s\n",msg); }
    return value_incref(XS_NULL_VAL);
}
static Value *native_test_assert_lt(Interp *ig, Value **a, int n) {
    (void)ig;
    int ok=(n>=2&&value_cmp(a[0],a[1])<0);
    const char *msg=(n>=3&&a[2]->tag==XS_STR)?a[2]->s:"assert_lt failed";
    if (ok) test_passed_count++;
    else { test_failed_count++; fprintf(stderr,"[FAIL] %s\n",msg); }
    return value_incref(XS_NULL_VAL);
}
static Value *native_test_assert_close(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<3) return value_incref(XS_NULL_VAL);
    double av=(a[0]->tag==XS_FLOAT)?a[0]->f:(double)a[0]->i;
    double bv=(a[1]->tag==XS_FLOAT)?a[1]->f:(double)a[1]->i;
    double eps=(a[2]->tag==XS_FLOAT)?a[2]->f:(double)a[2]->i;
    const char *msg=(n>=4&&a[3]->tag==XS_STR)?a[3]->s:"assert_close failed";
    double diff=av-bv; if(diff<0)diff=-diff;
    if (diff<=eps) test_passed_count++;
    else { test_failed_count++; fprintf(stderr,"[FAIL] %s: |%g - %g| = %g > %g\n",msg,av,bv,diff,eps); }
    return value_incref(XS_NULL_VAL);
}
static Value *native_test_run(Interp *ig, Value **a, int n) {
    const char *name=(n>0&&a[0]->tag==XS_STR)?a[0]->s:"test";
    fprintf(stderr,"[RUN] %s\n",name);
    /* If fn is a native, call it directly */
    if (n>=2&&a[1]->tag==XS_NATIVE&&a[1]->native) {
        Value *no_args=NULL;
        Value *res=a[1]->native(ig,&no_args,0);
        if (res) value_decref(res);
    }
    return value_incref(XS_NULL_VAL);
}
static Value *native_test_summary(Interp *ig, Value **a, int n) {
    (void)ig;(void)a;(void)n;
    fprintf(stderr,"[SUMMARY] %d passed, %d failed\n",test_passed_count,test_failed_count);
    return xs_int(test_failed_count);
}
Value *make_test_module(void) {
    XSMap *m=map_new();
    map_set(m,"assert",       xs_native(native_test_assert));
    map_set(m,"assert_eq",    xs_native(native_test_assert_eq));
    map_set(m,"assert_ne",    xs_native(native_test_assert_ne));
    map_set(m,"assert_gt",    xs_native(native_test_assert_gt));
    map_set(m,"assert_lt",    xs_native(native_test_assert_lt));
    map_set(m,"assert_close", xs_native(native_test_assert_close));
    map_set(m,"run",          xs_native(native_test_run));
    map_set(m,"summary",      xs_native(native_test_summary));
    return xs_module(m);
}

/* ── csv module ──────────────────────────────────────────── */
static Value *csv_parse_row(const char *s, int *pos2) {
    Value *row=xs_array_new();
    while (s[*pos2]&&s[*pos2]!='\n'&&s[*pos2]!='\r') {
        int start=*pos2;
        if (s[*pos2]=='"') {
            /* quoted field */
            (*pos2)++;
            int cap=64; char *buf=xs_malloc(cap); int ri=0;
            while (s[*pos2]&&!(s[*pos2]=='"'&&s[*pos2+1]!='"')) {
                if (ri+2>=cap){cap*=2;buf=xs_realloc(buf,cap);}
                if (s[*pos2]=='"'&&s[*pos2+1]=='"'){buf[ri++]='"';(*pos2)+=2;}
                else buf[ri++]=s[(*pos2)++];
            }
            if (s[*pos2]=='"') (*pos2)++;
            buf[ri]='\0'; array_push(row->arr,xs_str(buf)); free(buf);
        } else {
            /* unquoted field */
            while (s[*pos2]&&s[*pos2]!=','&&s[*pos2]!='\n'&&s[*pos2]!='\r') (*pos2)++;
            array_push(row->arr,xs_str_n(s+start,*pos2-start));
        }
        if (s[*pos2]==',') (*pos2)++;
        else break;
    }
    return row;
}
static Value *native_csv_parse(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_array_new();
    const char *s=a[0]->s; int pos2=0; int slen=(int)strlen(s);
    Value *rows=xs_array_new();
    while (pos2<slen) {
        Value *row=csv_parse_row(s,&pos2);
        array_push(rows->arr,row);
        while (s[pos2]=='\r'||s[pos2]=='\n') pos2++;
    }
    return rows;
}
static Value *native_csv_parse_with_headers(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_array_new();
    const char *s=a[0]->s; int pos2=0; int slen=(int)strlen(s);
    if (pos2>=slen) return xs_array_new();
    /* First row = headers */
    Value *hdr_row=csv_parse_row(s,&pos2);
    while (s[pos2]=='\r'||s[pos2]=='\n') pos2++;
    Value *result=xs_array_new();
    while (pos2<slen) {
        Value *row=csv_parse_row(s,&pos2);
        Value *rec=xs_map_new();
        for (int j=0;j<hdr_row->arr->len&&j<row->arr->len;j++){
            Value *hk=hdr_row->arr->items[j];
            char *ks=value_str(hk);
            Value *cell=value_incref(row->arr->items[j]);
            map_set(rec->map,ks,cell); value_decref(cell);
            free(ks);
        }
        value_decref(row);
        array_push(result->arr,rec);
        while (s[pos2]=='\r'||s[pos2]=='\n') pos2++;
    }
    value_decref(hdr_row);
    return result;
}
static Value *native_csv_stringify(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_ARRAY) return xs_str("");
    int cap=256,len2=0; char *out=xs_malloc(cap); out[0]='\0';
    XSArray *rows=a[0]->arr;
    for (int r=0;r<rows->len;r++){
        if (r) {
            if (len2+2>cap){cap*=2;out=xs_realloc(out,cap);}
            out[len2++]='\n'; out[len2]='\0';
        }
        if (rows->items[r]->tag!=XS_ARRAY) continue;
        XSArray *row=rows->items[r]->arr;
        for (int c=0;c<row->len;c++){
            if (c){
                if (len2+2>cap){cap*=2;out=xs_realloc(out,cap);}
                out[len2++]=','; out[len2]='\0';
            }
            char *s=value_str(row->items[c]); int sl=(int)strlen(s);
            /* Quote if contains comma, quote, or newline */
            int need_quote=0;
            for(int j=0;s[j];j++) if(s[j]==','||s[j]=='"'||s[j]=='\n'){need_quote=1;break;}
            if (need_quote) {
                if (len2+sl*2+4>cap){cap=cap*2+sl*2+4;out=xs_realloc(out,cap);}
                out[len2++]='"';
                for(int j=0;s[j];j++){if(s[j]=='"'){out[len2++]='"';}out[len2++]=s[j];}
                out[len2++]='"'; out[len2]='\0';
            } else {
                if (len2+sl+2>cap){cap=cap*2+sl+2;out=xs_realloc(out,cap);}
                memcpy(out+len2,s,sl); len2+=sl; out[len2]='\0';
            }
            free(s);
        }
    }
    Value *v=xs_str(out); free(out); return v;
}
static Value *native_csv_stringify_with_headers(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_ARRAY||a[1]->tag!=XS_ARRAY) return xs_str("");
    /* Build a new array with headers prepended */
    Value *all=xs_array_new();
    array_push(all->arr,value_incref(a[0]));
    XSArray *rows=a[1]->arr;
    for (int j=0;j<rows->len;j++) array_push(all->arr,value_incref(rows->items[j]));
    Value *args2[1]={all};
    Value *r=native_csv_stringify(ig,args2,1);
    value_decref(all); return r;
}
Value *make_csv_module(void) {
    XSMap *m=map_new();
    map_set(m,"parse",               xs_native(native_csv_parse));
    map_set(m,"parse_with_headers",  xs_native(native_csv_parse_with_headers));
    map_set(m,"stringify",           xs_native(native_csv_stringify));
    map_set(m,"stringify_with_headers",xs_native(native_csv_stringify_with_headers));
    return xs_module(m);
}

/* ── url module ──────────────────────────────────────────── */
static int url_hex_val(char c) {
    if (c>='0'&&c<='9') return c-'0';
    if (c>='A'&&c<='F') return c-'A'+10;
    if (c>='a'&&c<='f') return c-'a'+10;
    return -1;
}
static Value *native_url_encode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_str("");
    const char *s=a[0]->s; int cap=256; char *r=xs_malloc(cap); int ri=0;
    for (const unsigned char *p=(const unsigned char*)s;*p;p++){
        if (isalnum(*p)||*p=='-'||*p=='_'||*p=='.'||*p=='~'){
            if (ri+2>cap){cap*=2;r=xs_realloc(r,cap);}
            r[ri++]=(char)*p;
        } else {
            if (ri+4>cap){cap*=2;r=xs_realloc(r,cap);}
            snprintf(r+ri,4,"%%%02X",*p); ri+=3;
        }
    }
    r[ri]='\0'; Value *v=xs_str(r); free(r); return v;
}
static Value *native_url_decode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_str("");
    const char *s=a[0]->s; int slen=(int)strlen(s);
    char *r=xs_malloc(slen+1); int ri=0;
    for (int j=0;j<slen;) {
        if (s[j]=='%'&&j+2<slen){
            int h1=url_hex_val(s[j+1]),h2=url_hex_val(s[j+2]);
            if (h1>=0&&h2>=0){r[ri++]=(char)(h1*16+h2);j+=3;}
            else r[ri++]=s[j++];
        } else if (s[j]=='+') { r[ri++]=' '; j++; }
        else r[ri++]=s[j++];
    }
    r[ri]='\0'; Value *v=xs_str(r); free(r); return v;
}
static Value *native_url_encode_query(Interp *ig, Value **a, int n) {
    if (n<1||(a[0]->tag!=XS_MAP&&a[0]->tag!=XS_MODULE)) return xs_str("");
    int nk=0; char **ks=map_keys(a[0]->map,&nk);
    int cap=256; char *out=xs_malloc(cap); int oi=0; out[0]='\0';
    for (int j=0;j<nk;j++){
        Value *kv=xs_str(ks[j]); Value *args2[1]={kv};
        Value *ek=native_url_encode(ig,args2,1); value_decref(kv);
        Value *vv=map_get(a[0]->map,ks[j]);
        Value *vs=(vv&&vv->tag==XS_STR)?value_incref(vv):xs_str("");
        Value *args3[1]={vs};
        Value *ev=native_url_encode(ig,args3,1); value_decref(vs);
        int ekl=(int)strlen(ek->s),evl=(int)strlen(ev->s);
        if (oi+ekl+evl+4>cap){cap=(cap+ekl+evl)*2;out=xs_realloc(out,cap);}
        if (oi) out[oi++]='&';
        memcpy(out+oi,ek->s,ekl); oi+=ekl;
        out[oi++]='=';
        memcpy(out+oi,ev->s,evl); oi+=evl;
        out[oi]='\0';
        value_decref(ek); value_decref(ev); free(ks[j]);
    }
    free(ks); Value *v=xs_str(out); free(out); return v;
}
static Value *native_url_parse_query(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_map_new();
    Value *m=xs_map_new();
    const char *s=a[0]->s;
    while (*s) {
        const char *eq=strchr(s,'='); if (!eq) break;
        const char *amp=strchr(eq,'&');
        int klen=(int)(eq-s);
        int vlen=amp?(int)(amp-eq-1):(int)strlen(eq+1);
        char *key=xs_strndup(s,klen);
        char *val=xs_strndup(eq+1,vlen);
        /* decode key */
        Value *kv=xs_str(key); Value *args2[1]={kv};
        Value *dk=native_url_decode(ig,args2,1); value_decref(kv); free(key);
        Value *vv=xs_str(val); args2[0]=vv;
        Value *dv=native_url_decode(ig,args2,1); value_decref(vv); free(val);
        map_set(m->map,dk->s,dv); value_decref(dv); value_decref(dk);
        s=amp?amp+1:"";
    }
    return m;
}
static Value *native_url_parse(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<1||a[0]->tag!=XS_STR) return xs_map_new();
    Value *m=xs_map_new();
    const char *url=a[0]->s;
    /* scheme */
    const char *p=strstr(url,"://");
    if (p) {
        Value *v=xs_str_n(url,(int)(p-url)); map_set(m->map,"scheme",v); value_decref(v);
        url=p+3;
    } else {
        Value *v=xs_str(""); map_set(m->map,"scheme",v); value_decref(v);
    }
    /* host */
    const char *slash=strchr(url,'/');
    const char *qmark=strchr(url,'?');
    const char *end_host=slash?(qmark&&qmark<slash?qmark:slash):qmark;
    if (end_host){
        Value *v=xs_str_n(url,(int)(end_host-url)); map_set(m->map,"host",v); value_decref(v);
        url=end_host;
    } else {
        Value *v=xs_str(url); map_set(m->map,"host",v); value_decref(v); url="";
    }
    /* path */
    const char *qm=strchr(url,'?'); const char *hash=strchr(url,'#');
    const char *path_end=qm?qm:(hash?hash:url+strlen(url));
    { Value *v=xs_str_n(url,(int)(path_end-url)); map_set(m->map,"path",v); value_decref(v); }
    if (qm) {
        url=qm+1; const char *frag=strchr(url,'#');
        int qlen=frag?(int)(frag-url):(int)strlen(url);
        Value *v=xs_str_n(url,qlen); map_set(m->map,"query",v); value_decref(v);
        if (frag) { Value *fv=xs_str(frag+1); map_set(m->map,"fragment",fv); value_decref(fv); }
        else { Value *fv=xs_str(""); map_set(m->map,"fragment",fv); value_decref(fv); }
    } else {
        Value *v=xs_str(""); map_set(m->map,"query",v); value_decref(v);
        if (hash) { Value *fv=xs_str(hash+1); map_set(m->map,"fragment",fv); value_decref(fv); }
        else { Value *fv=xs_str(""); map_set(m->map,"fragment",fv); value_decref(fv); }
    }
    return m;
}
Value *make_url_module(void) {
    XSMap *m=map_new();
    map_set(m,"encode",       xs_native(native_url_encode));
    map_set(m,"decode",       xs_native(native_url_decode));
    map_set(m,"encode_query", xs_native(native_url_encode_query));
    map_set(m,"parse_query",  xs_native(native_url_parse_query));
    map_set(m,"parse",        xs_native(native_url_parse));
    return xs_module(m);
}

/* ── re (regex) module ───────────────────────────────────── */
/* Convert PCRE-style shorthand escapes to POSIX ERE equivalents */
static char *re_to_posix(const char *pat) {
    /* Allocate worst-case: each \X can expand to ~12 chars */
    size_t plen = strlen(pat);
    char *out = xs_malloc(plen * 12 + 1);
    char *p = out;
    for (size_t i = 0; i < plen; i++) {
        if (pat[i] == '\\' && i+1 < plen) {
            char c = pat[++i];
            switch (c) {
            case 'd': memcpy(p,"[0-9]",5);     p+=5; break;
            case 'D': memcpy(p,"[^0-9]",6);    p+=6; break;
            case 'w': memcpy(p,"[A-Za-z0-9_]",12); p+=12; break;
            case 'W': memcpy(p,"[^A-Za-z0-9_]",13); p+=13; break;
            case 's': memcpy(p,"[ \\t\\n\\r\\f\\v]",14); p+=14; break;
            case 'S': memcpy(p,"[^ \\t\\n\\r\\f\\v]",15); p+=15; break;
            default:  *p++='\\'; *p++=c; break;
            }
        } else {
            *p++ = pat[i];
        }
    }
    *p = '\0';
    return out;
}

static Value *native_re_test(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
    char *pat=re_to_posix(a[0]->s);
    regex_t re; int rc=regcomp(&re,pat,REG_EXTENDED); free(pat);
    if (rc!=0) return value_incref(XS_FALSE_VAL);
    int r=(regexec(&re,a[1]->s,0,NULL,0)==0);
    regfree(&re); return r?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
}
static Value *native_re_match(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
    char *pat=re_to_posix(a[0]->s);
    regex_t re; int rc=regcomp(&re,pat,REG_EXTENDED); free(pat);
    if (rc!=0) return value_incref(XS_NULL_VAL);
    regmatch_t m; Value *res=value_incref(XS_NULL_VAL);
    if (regexec(&re,a[1]->s,1,&m,0)==0) {
        value_decref(res);
        res=xs_str_n(a[1]->s+m.rm_so,(int)(m.rm_eo-m.rm_so));
    }
    regfree(&re); return res;
}
static Value *native_re_find_all(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return xs_array_new();
    char *pat=re_to_posix(a[0]->s);
    regex_t re; int rc=regcomp(&re,pat,REG_EXTENDED); free(pat);
    if (rc!=0) return xs_array_new();
    Value *arr=xs_array_new();
    const char *s=a[1]->s;
    regmatch_t m;
    while (*s&&regexec(&re,s,1,&m,0)==0) {
        array_push(arr->arr,xs_str_n(s+m.rm_so,(int)(m.rm_eo-m.rm_so)));
        if (m.rm_eo==m.rm_so) { s++; continue; }
        s+=m.rm_eo;
    }
    regfree(&re); return arr;
}
static Value *native_re_replace(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<3||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR||a[2]->tag!=XS_STR) return n>1?value_incref(a[1]):xs_str("");
    char *pat=re_to_posix(a[0]->s);
    regex_t re; int rc=regcomp(&re,pat,REG_EXTENDED); free(pat);
    if (rc!=0) return value_incref(a[1]);
    const char *s=a[1]->s; const char *rep=a[2]->s;
    regmatch_t m;
    if (regexec(&re,s,1,&m,0)!=0){regfree(&re);return value_incref(a[1]);}
    int replen=(int)strlen(rep);
    int rlen=(int)m.rm_so+replen+(int)strlen(s+m.rm_eo)+1;
    char *r=xs_malloc(rlen);
    memcpy(r,s,(size_t)m.rm_so);
    memcpy(r+m.rm_so,rep,(size_t)replen);
    strcpy(r+m.rm_so+replen,s+m.rm_eo);
    Value *v=xs_str(r); free(r); regfree(&re); return v;
}
static Value *native_re_replace_all(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<3||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR||a[2]->tag!=XS_STR) return n>1?value_incref(a[1]):xs_str("");
    char *pat=re_to_posix(a[0]->s);
    regex_t re; int rc=regcomp(&re,pat,REG_EXTENDED); free(pat);
    if (rc!=0) return value_incref(a[1]);
    const char *s=a[1]->s; const char *rep=a[2]->s; int replen=(int)strlen(rep);
    int cap=256; char *out=xs_malloc(cap); int oi=0;
    regmatch_t m;
    while (*s&&regexec(&re,s,1,&m,0)==0){
        int plen=(int)m.rm_so;
        if (oi+plen+replen+2>cap){cap=(cap+plen+replen)*2;out=xs_realloc(out,cap);}
        memcpy(out+oi,s,(size_t)plen); oi+=plen;
        memcpy(out+oi,rep,(size_t)replen); oi+=replen;
        if (m.rm_eo==m.rm_so){
            if (oi+2>cap){cap*=2;out=xs_realloc(out,cap);}
            out[oi++]=*s++;
        } else s+=m.rm_eo;
    }
    int sl=(int)strlen(s);
    if (oi+sl+2>cap){cap=oi+sl+2;out=xs_realloc(out,cap);}
    memcpy(out+oi,s,(size_t)sl); oi+=sl; out[oi]='\0';
    Value *v=xs_str(out); free(out); regfree(&re); return v;
}
static Value *native_re_split(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return xs_array_new();
    char *pat=re_to_posix(a[0]->s);
    regex_t re; int rc=regcomp(&re,pat,REG_EXTENDED); free(pat);
    if (rc!=0) return xs_array_new();
    Value *arr=xs_array_new();
    const char *s=a[1]->s;
    regmatch_t m;
    while (*s&&regexec(&re,s,1,&m,0)==0){
        array_push(arr->arr,xs_str_n(s,(int)m.rm_so));
        if (m.rm_eo==m.rm_so){s++;continue;}
        s+=m.rm_eo;
    }
    array_push(arr->arr,xs_str(s));
    regfree(&re); return arr;
}
static Value *native_re_groups(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n<2||a[0]->tag!=XS_STR||a[1]->tag!=XS_STR) return xs_array_new();
    regex_t re; if (regcomp(&re,a[0]->s,REG_EXTENDED)!=0) return xs_array_new();
    int ng=(int)re.re_nsub+1; if(ng<1)ng=1;
    regmatch_t *m=xs_malloc(ng*sizeof(regmatch_t));
    Value *arr=xs_array_new();
    if (regexec(&re,a[1]->s,(size_t)ng,m,0)==0){
        for(int j=1;j<ng;j++){
            if (m[j].rm_so<0) array_push(arr->arr,value_incref(XS_NULL_VAL));
            else array_push(arr->arr,xs_str_n(a[1]->s+m[j].rm_so,(int)(m[j].rm_eo-m[j].rm_so)));
        }
    }
    free(m); regfree(&re); return arr;
}
Value *make_re_module(void) {
    XSMap *m=map_new();
    map_set(m,"test",        xs_native(native_re_test));
    map_set(m,"is_match",    xs_native(native_re_test));
    map_set(m,"match",       xs_native(native_re_match));
    map_set(m,"find_all",    xs_native(native_re_find_all));
    map_set(m,"replace",     xs_native(native_re_replace));
    map_set(m,"replace_all", xs_native(native_re_replace_all));
    map_set(m,"split",       xs_native(native_re_split));
    map_set(m,"groups",      xs_native(native_re_groups));
    return xs_module(m);
}

/* ── reactive primitives ─────────────────────────────────── */
static Value *builtin_signal(Interp *i, Value **args, int argc) {
    (void)i;
    XSSignal *sig = xs_calloc(1, sizeof(XSSignal));
    sig->value = (argc > 0) ? value_incref(args[0]) : value_incref(XS_NULL_VAL);
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
static Value *builtin_derived(Interp *i, Value **args, int argc) {
    (void)i;
    XSSignal *sig = xs_calloc(1, sizeof(XSSignal));
    sig->value = value_incref(XS_NULL_VAL);
    sig->subscribers = NULL;
    sig->nsubs = 0;
    sig->subcap = 0;
    sig->compute = (argc > 0 && (args[0]->tag == XS_FUNC || args[0]->tag == XS_NATIVE))
                   ? value_incref(args[0]) : NULL;
    sig->notifying = 0;
    sig->refcount = 1;
    Value *v = xs_calloc(1, sizeof(Value));
    v->tag = XS_SIGNAL;
    v->refcount = 1;
    v->signal = sig;
    return v;
}

/* ── channel(capacity) ───────────────────────────────────── */
static Value *builtin_channel(Interp *i, Value **args, int argc) {
    (void)i;
    Value *ch = xs_map_new();
    Value *t = xs_str("Channel"); map_set(ch->map,"_type",t); value_decref(t);
    int64_t cap = (argc >= 1 && args[0]->tag == XS_INT) ? args[0]->i : 0;
    Value *cap_v = xs_int(cap); map_set(ch->map,"_cap",cap_v); value_decref(cap_v);
    Value *data = xs_array_new(); map_set(ch->map,"_data",data); value_decref(data);
    return ch;
}

/* ── contains ────────────────────────────────────────────── */
static Value *builtin_contains(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2 || args[0]->tag != XS_STR || args[1]->tag != XS_STR)
        return value_incref(XS_FALSE_VAL);
    if (strstr(args[0]->s, args[1]->s))
        return value_incref(XS_TRUE_VAL);
    return value_incref(XS_FALSE_VAL);
}

/* ── Result / Option constructors ───────────────────────── */
static Value *make_result_enum(const char *type_name, const char *variant, Value **args, int argc) {
    XSEnum *en = xs_calloc(1, sizeof(XSEnum));
    en->type_name = xs_strdup(type_name);
    en->variant   = xs_strdup(variant);
    en->arr_data  = array_new();
    en->refcount  = 1;
    for (int k = 0; k < argc; k++)
        array_push(en->arr_data, value_incref(args[k]));
    Value *ev = xs_calloc(1, sizeof(Value));
    ev->tag = XS_ENUM_VAL; ev->refcount = 1; ev->en = en;
    return ev;
}

static Value *builtin_ok(Interp *interp, Value **args, int argc) {
    (void)interp;
    return make_result_enum("Ok", "Ok", args, argc);
}
static Value *builtin_err(Interp *interp, Value **args, int argc) {
    (void)interp;
    return make_result_enum("Err", "Err", args, argc);
}
static Value *builtin_some(Interp *interp, Value **args, int argc) {
    (void)interp;
    return make_result_enum("Some", "Some", args, argc);
}
static Value *builtin_none_fn(Interp *interp, Value **args, int argc) {
    (void)interp; (void)args; (void)argc;
    return value_incref(XS_NULL_VAL);
}

/* ── forward declarations for new modules (defined below) ── */
Value *make_async_module(void);
Value *make_net_module(void);
Value *make_crypto_module(void);
Value *make_thread_module(void);
Value *make_buf_module(void);
Value *make_encode_module(void);
Value *make_db_module(void);
Value *make_cli_module(void);
Value *make_ffi_module(void);
Value *make_reflect_module(void);
Value *make_gc_module(void);
Value *make_reactive_module(void);
Value *make_fs_module(void);

/* ── register all ────────────────────────────────────────── */
void stdlib_register(Interp *i) {
    /* Core */
    interp_define_native(i, "print",     builtin_print);
    interp_define_native(i, "println",   builtin_print);
    interp_define_native(i, "eprint",    builtin_eprint);
    interp_define_native(i, "eprintln",  builtin_eprint);
    interp_define_native(i, "print_no_nl", builtin_print_no_nl);
    interp_define_native(i, "type",      builtin_type);
    interp_define_native(i, "typeof",    builtin_typeof);
    interp_define_native(i, "is_null",   builtin_is_null);
    interp_define_native(i, "is_int",    builtin_is_int);
    interp_define_native(i, "is_float",  builtin_is_float);
    interp_define_native(i, "is_str",    builtin_is_str);
    interp_define_native(i, "is_bool",   builtin_is_bool);
    interp_define_native(i, "is_array",  builtin_is_array);
    interp_define_native(i, "is_fn",     builtin_is_fn);
    interp_define_native(i, "int",       builtin_int);
    interp_define_native(i, "i64",       builtin_int);
    interp_define_native(i, "float",     builtin_float);
    interp_define_native(i, "f64",       builtin_float);
    interp_define_native(i, "str",       builtin_str);
    interp_define_native(i, "bool",      builtin_bool);
    interp_define_native(i, "char",      builtin_char);
    interp_define_native(i, "repr",      builtin_repr);
    interp_define_native(i, "dbg",       builtin_dbg);
    interp_define_native(i, "pprint",    builtin_pprint);
    interp_define_native(i, "len",       builtin_len);
    interp_define_native(i, "range",     builtin_range);
    interp_define_native(i, "array",     builtin_array);
    interp_define_native(i, "map",       builtin_map);
    interp_define_native(i, "filter",    builtin_filter);
    interp_define_native(i, "reduce",    builtin_reduce);
    interp_define_native(i, "keys",      builtin_keys);
    interp_define_native(i, "values",    builtin_values);
    interp_define_native(i, "entries",   builtin_entries);
    interp_define_native(i, "flatten",   builtin_flatten);
    interp_define_native(i, "chars",     builtin_chars);
    interp_define_native(i, "bytes",     builtin_bytes);
    interp_define_native(i, "zip",       builtin_zip);
    interp_define_native(i, "enumerate", builtin_enumerate);
    interp_define_native(i, "sum",       builtin_sum);
    interp_define_native(i, "abs",       builtin_abs);
    interp_define_native(i, "min",       builtin_min);
    interp_define_native(i, "max",       builtin_max);
    interp_define_native(i, "pow",       builtin_pow);
    interp_define_native(i, "sqrt",      builtin_sqrt);
    interp_define_native(i, "floor",     builtin_floor);
    interp_define_native(i, "ceil",      builtin_ceil);
    interp_define_native(i, "round",     builtin_round);
    interp_define_native(i, "log",       builtin_log);
    interp_define_native(i, "sin",       builtin_sin);
    interp_define_native(i, "cos",       builtin_cos);
    interp_define_native(i, "tan",       builtin_tan);
    interp_define_native(i, "format",    builtin_format);
    interp_define_native(i, "sprintf",   builtin_format);
    interp_define_native(i, "todo",      builtin_todo);
    interp_define_native(i, "unreachable", builtin_unreachable);
    interp_define_native(i, "vec",       builtin_vec);
    interp_define_native(i, "sorted",    builtin_sorted);
    interp_define_native(i, "input",     builtin_input);
    interp_define_native(i, "exit",      builtin_exit);
    interp_define_native(i, "clear",     builtin_clear);
    interp_define_native(i, "assert",    builtin_assert);
    interp_define_native(i, "assert_eq", builtin_assert_eq);
    interp_define_native(i, "panic",     builtin_panic);
    interp_define_native(i, "copy",      builtin_copy);
    interp_define_native(i, "clone",     builtin_clone);
    interp_define_native(i, "ord",       builtin_ord);
    interp_define_native(i, "chr",       builtin_chr);
    interp_define_native(i, "signal",    builtin_signal);
    interp_define_native(i, "derived",   builtin_derived);
    interp_define_native(i, "type_of",   builtin_type_of);
    interp_define_native(i, "contains",  builtin_contains);
    interp_define_native(i, "channel",   builtin_channel);

    /* Result / Option constructors */
    interp_define_native(i, "Ok",   builtin_ok);
    interp_define_native(i, "Err",  builtin_err);
    interp_define_native(i, "Some", builtin_some);
    interp_define_native(i, "None", builtin_none_fn);

    /* Modules */
    Value *math_mod = make_math_module();
    env_define(i->globals, "math", math_mod, 1);
    value_decref(math_mod);

    Value *time_mod = make_time_module();
    env_define(i->globals, "time", time_mod, 1);
    value_decref(time_mod);

    Value *io_mod = make_io_module();
    /* read_json/write_json are defined after json helpers, patch them in here */
    map_set(io_mod->map,"read_json",  xs_native(native_io_read_json));
    map_set(io_mod->map,"write_json", xs_native(native_io_write_json));
    env_define(i->globals, "io", io_mod, 1);
    value_decref(io_mod);

    Value *string_mod = make_string_module();
    env_define(i->globals, "string", string_mod, 1);
    value_decref(string_mod);

    Value *path_mod = make_path_module();
    env_define(i->globals, "path", path_mod, 1);
    value_decref(path_mod);

    Value *base64_mod = make_base64_module();
    env_define(i->globals, "base64", base64_mod, 1);
    value_decref(base64_mod);

    Value *hash_mod = make_hash_module();
    env_define(i->globals, "hash", hash_mod, 1);
    value_decref(hash_mod);

    Value *uuid_mod = make_uuid_module();
    env_define(i->globals, "uuid", uuid_mod, 1);
    value_decref(uuid_mod);

    Value *collections_mod = make_collections_module();
    env_define(i->globals, "collections", collections_mod, 1);
    value_decref(collections_mod);

    Value *process_mod = make_process_module();
    env_define(i->globals, "process", process_mod, 1);
    value_decref(process_mod);

    Value *random_mod = make_random_module();
    env_define(i->globals, "random", random_mod, 1);
    value_decref(random_mod);
    srand((unsigned)time(NULL));

    Value *os_mod = make_os_module(i);
    env_define(i->globals, "os", os_mod, 1);
    value_decref(os_mod);

    Value *json_mod = make_json_module();
    env_define(i->globals, "json", json_mod, 1);
    value_decref(json_mod);

    Value *log_mod = make_log_module();
    env_define(i->globals, "log", log_mod, 1);
    value_decref(log_mod);

    Value *fmt_mod = make_fmt_module();
    env_define(i->globals, "fmt", fmt_mod, 1);
    value_decref(fmt_mod);

    Value *test_mod = make_test_module();
    env_define(i->globals, "test", test_mod, 1);
    value_decref(test_mod);

    Value *csv_mod = make_csv_module();
    env_define(i->globals, "csv", csv_mod, 1);
    value_decref(csv_mod);

    Value *url_mod = make_url_module();
    env_define(i->globals, "url", url_mod, 1);
    value_decref(url_mod);

    Value *re_mod = make_re_module();
    env_define(i->globals, "re", re_mod, 1);
    value_decref(re_mod);

    /* Constants */
    {
        Value *v = xs_float(M_PI);
        env_define(i->globals, "PI", v, 0); value_decref(v);
    }
    {
        Value *v = xs_float(M_E);
        env_define(i->globals, "E", v, 0); value_decref(v);
    }
    {
        Value *v = xs_float(HUGE_VAL);
        env_define(i->globals, "INF", v, 0); value_decref(v);
    }
    {
        Value *v = xs_float(0.0/0.0);
        env_define(i->globals, "NAN", v, 0); value_decref(v);
    }

    /* Also expose math functions directly */
    interp_define_native(i, "sqrt", builtin_sqrt);
    interp_define_native(i, "floor",builtin_floor);
    interp_define_native(i, "ceil", builtin_ceil);
    interp_define_native(i, "round",builtin_round);

    /* ── new stdlib modules (12) ─────────────────────────────── */
    Value *async_mod = make_async_module();
    env_define(i->globals, "async", async_mod, 1);
    value_decref(async_mod);

    Value *net_mod = make_net_module();
    env_define(i->globals, "net", net_mod, 1);
    value_decref(net_mod);

    Value *crypto_mod = make_crypto_module();
    env_define(i->globals, "crypto", crypto_mod, 1);
    value_decref(crypto_mod);

    Value *thread_mod = make_thread_module();
    env_define(i->globals, "thread", thread_mod, 1);
    value_decref(thread_mod);

    Value *buf_mod = make_buf_module();
    env_define(i->globals, "buf", buf_mod, 1);
    value_decref(buf_mod);

    Value *encode_mod = make_encode_module();
    env_define(i->globals, "encode", encode_mod, 1);
    value_decref(encode_mod);

    Value *db_mod = make_db_module();
    env_define(i->globals, "db", db_mod, 1);
    value_decref(db_mod);

    Value *cli_mod = make_cli_module();
    env_define(i->globals, "cli", cli_mod, 1);
    value_decref(cli_mod);

    Value *ffi_mod = make_ffi_module();
    env_define(i->globals, "ffi", ffi_mod, 1);
    value_decref(ffi_mod);

    Value *reflect_mod = make_reflect_module();
    env_define(i->globals, "reflect", reflect_mod, 1);
    value_decref(reflect_mod);

    Value *gc_mod = make_gc_module();
    env_define(i->globals, "gc", gc_mod, 1);
    value_decref(gc_mod);

    Value *reactive_mod = make_reactive_module();
    env_define(i->globals, "reactive", reactive_mod, 1);
    value_decref(reactive_mod);

    Value *fs_mod = make_fs_module();
    env_define(i->globals, "fs", fs_mod, 1);
    value_decref(fs_mod);
}

/* ═══════════════════════════════════════════════════════════════
 *  12 NEW STDLIB MODULES
 * ═══════════════════════════════════════════════════════════════ */

/* async module */

static Value *native_async_spawn(Interp *ig, Value **a, int n) {
    (void)ig;
    /* Cooperative/synchronous semantics: call the function eagerly and
       wrap the result in a task map with _result / _status fields. */
    XSMap *task = map_new();
    if (n < 1 || (a[0]->tag != XS_NATIVE && a[0]->tag != XS_FUNC)) {
        map_set(task, "_status", xs_str("rejected"));
        map_set(task, "_error",  xs_str("spawn requires a callable"));
        return xs_module(task);
    }
    Value *result = call_value(ig, a[0], (n > 1 ? a + 1 : NULL), (n > 1 ? n - 1 : 0), "async.spawn");
    map_set(task, "_status", xs_str("resolved"));
    map_set(task, "_result", result);
    value_decref(result);
    return xs_module(task);
}

static Value *native_async_sleep(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1) return value_incref(XS_NULL_VAL);
    double secs = 0.0;
    if (a[0]->tag == XS_FLOAT) secs = a[0]->f;
    else if (a[0]->tag == XS_INT) secs = (double)a[0]->i;
#ifndef __MINGW32__
    struct timespec ts;
    ts.tv_sec  = (time_t)secs;
    ts.tv_nsec = (long)((secs - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
#else
    /* Windows: Sleep() takes milliseconds */
    DWORD ms = (DWORD)(secs * 1000.0);
    if (ms > 0) Sleep(ms);
#endif
    return value_incref(XS_NULL_VAL);
}

static Value *native_channel_send(Interp *ig, Value **a, int n) {
    (void)ig;
    /* a[0] is the channel map itself (bound via closure or passed explicitly)
       For simplicity, we expect: channel.send(channel, value) or the interp
       passes self as first arg. We store in _buf. */
    if (n < 2) return value_incref(XS_NULL_VAL);
    Value *ch_val = a[0];
    if ((ch_val->tag != XS_MAP && ch_val->tag != XS_MODULE) || !ch_val->map)
        return value_incref(XS_NULL_VAL);
    Value *buf = map_get(ch_val->map, "_buf");
    if (!buf || buf->tag != XS_ARRAY) return value_incref(XS_NULL_VAL);
    array_push(buf->arr, a[1]);
    return value_incref(XS_NULL_VAL);
}

static Value *native_channel_recv(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1) return value_incref(XS_NULL_VAL);
    Value *ch_val = a[0];
    if ((ch_val->tag != XS_MAP && ch_val->tag != XS_MODULE) || !ch_val->map)
        return value_incref(XS_NULL_VAL);
    Value *buf = map_get(ch_val->map, "_buf");
    if (!buf || buf->tag != XS_ARRAY || buf->arr->len == 0)
        return value_incref(XS_NULL_VAL);
    /* Remove and return the first element */
    Value *val = value_incref(buf->arr->items[0]);
    for (int j = 0; j < buf->arr->len - 1; j++)
        buf->arr->items[j] = buf->arr->items[j + 1];
    buf->arr->len--;
    return val;
}

static Value *native_async_channel(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    /* Simplified channel: a shared array with send/recv */
    XSMap *ch = map_new();
    Value *buf = xs_array_new();
    map_set(ch, "_buf", buf);
    value_decref(buf);
    map_set(ch, "send", xs_native(native_channel_send));
    map_set(ch, "recv", xs_native(native_channel_recv));
    return xs_module(ch);
}

static Value *native_async_select(Interp *ig, Value **a, int n) {
    (void)ig;
    /* select(channels_or_tasks) — poll an array of channel/task-like
       values and return a map { index: <idx>, value: <result> } for the
       first one that has a ready result.
       A channel is ready when its "_buf" array is non-empty.
       A task/promise is ready when it has a "_result" key.
       If nothing is ready, return null. */
    if (n < 1 || a[0]->tag != XS_ARRAY) return value_incref(XS_NULL_VAL);
    XSArray *arr = a[0]->arr;
    for (int i = 0; i < arr->len; i++) {
        Value *item = arr->items[i];
        if ((item->tag == XS_MAP || item->tag == XS_MODULE) && item->map) {
            /* Check for channel readiness: "_buf" array with len > 0 */
            Value *buf = map_get(item->map, "_buf");
            if (buf && buf->tag == XS_ARRAY && buf->arr->len > 0) {
                /* Consume the first buffered value */
                Value *val = value_incref(buf->arr->items[0]);
                /* Shift the buffer: remove first element */
                for (int j = 0; j < buf->arr->len - 1; j++)
                    buf->arr->items[j] = buf->arr->items[j + 1];
                buf->arr->len--;
                XSMap *result = map_new();
                map_set(result, "index", xs_int(i));
                map_set(result, "value", val);
                value_decref(val);
                return xs_module(result);
            }
            /* Check for task/promise readiness: "_result" key present */
            Value *res = map_get(item->map, "_result");
            if (res) {
                XSMap *result = map_new();
                map_set(result, "index", xs_int(i));
                map_set(result, "value", value_incref(res));
                value_decref(res);
                return xs_module(result);
            }
        }
    }
    /* Nothing ready */
    return value_incref(XS_NULL_VAL);
}

static Value *native_async_all(Interp *ig, Value **a, int n) {
    (void)ig;
    /* Collect _result from each task map into a results array.
       If the argument is not an array of task maps, return it as-is. */
    if (n < 1 || a[0]->tag != XS_ARRAY) return xs_array_new();
    XSArray *tasks = a[0]->arr;
    Value *results = xs_array_new();
    for (int i = 0; i < tasks->len; i++) {
        Value *t = tasks->items[i];
        if ((t->tag == XS_MAP || t->tag == XS_MODULE) && t->map) {
            Value *r = map_get(t->map, "_result");
            if (r) {
                array_push(results->arr, r);
            } else {
                array_push(results->arr, XS_NULL_VAL);
            }
        } else {
            /* Not a task map — include the value itself */
            array_push(results->arr, t);
        }
    }
    return results;
}

static Value *native_async_race(Interp *ig, Value **a, int n) {
    (void)ig;
    /* Return the _result of the first task in the array.
       Since we use cooperative semantics, all tasks are already resolved,
       so "first" is simply the first element. */
    if (n < 1 || a[0]->tag != XS_ARRAY || a[0]->arr->len == 0)
        return value_incref(XS_NULL_VAL);
    Value *first = a[0]->arr->items[0];
    if ((first->tag == XS_MAP || first->tag == XS_MODULE) && first->map) {
        Value *r = map_get(first->map, "_result");
        if (r) return value_incref(r);
    }
    return value_incref(first);
}

static Value *native_async_resolve(Interp *ig, Value **a, int n) {
    (void)ig;
    /* Create a resolved task/future with the given value */
    XSMap *task = map_new();
    map_set(task, "_status", xs_str("resolved"));
    if (n > 0) {
        map_set(task, "_result", value_incref(a[0]));
    } else {
        map_set(task, "_result", value_incref(XS_NULL_VAL));
    }
    return xs_module(task);
}

static Value *native_async_reject(Interp *ig, Value **a, int n) {
    (void)ig;
    /* Create a rejected task/future with an error value */
    XSMap *task = map_new();
    map_set(task, "_status", xs_str("rejected"));
    if (n > 0) {
        map_set(task, "_error", value_incref(a[0]));
    } else {
        map_set(task, "_error", xs_str("rejected"));
    }
    return xs_module(task);
}

Value *make_async_module(void) {
    XSMap *m = map_new();
    map_set(m, "spawn",   xs_native(native_async_spawn));
    map_set(m, "sleep",   xs_native(native_async_sleep));
    map_set(m, "channel", xs_native(native_async_channel));
    map_set(m, "select",  xs_native(native_async_select));
    map_set(m, "all",     xs_native(native_async_all));
    map_set(m, "race",    xs_native(native_async_race));
    map_set(m, "resolve", xs_native(native_async_resolve));
    map_set(m, "reject",  xs_native(native_async_reject));
    return xs_module(m);
}

/* net */
#ifndef __MINGW32__
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#endif

static Value *native_net_tcp_connect(Interp *ig, Value **a, int n) {
    (void)ig;
#ifndef __MINGW32__
    if (n < 2 || a[0]->tag != XS_STR) return value_incref(XS_NULL_VAL);
    const char *host = a[0]->s;
    int port = (a[1]->tag == XS_INT) ? (int)a[1]->i : 0;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof port_str, "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0)
        return value_incref(XS_NULL_VAL);

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return value_incref(XS_NULL_VAL); }
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd); freeaddrinfo(res); return value_incref(XS_NULL_VAL);
    }
    freeaddrinfo(res);

    XSMap *conn = map_new();
    map_set(conn, "fd", xs_int(fd));
    return xs_module(conn);
#else
    (void)a; (void)n;
    return value_incref(XS_NULL_VAL);
#endif
}

static Value *native_net_tcp_listen(Interp *ig, Value **a, int n) {
    (void)ig;
#ifndef __MINGW32__
    if (n < 1) return value_incref(XS_NULL_VAL);
    int port = (a[0]->tag == XS_INT) ? (int)a[0]->i : 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return value_incref(XS_NULL_VAL);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof addr) < 0) {
        close(fd); return value_incref(XS_NULL_VAL);
    }
    if (listen(fd, 128) < 0) {
        close(fd); return value_incref(XS_NULL_VAL);
    }

    XSMap *srv = map_new();
    map_set(srv, "fd", xs_int(fd));
    return xs_module(srv);
#else
    (void)a; (void)n;
    return value_incref(XS_NULL_VAL);
#endif
}

static Value *native_net_resolve(Interp *ig, Value **a, int n) {
    (void)ig;
#ifndef __MINGW32__
    if (n < 1 || a[0]->tag != XS_STR) return xs_array_new();
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(a[0]->s, NULL, &hints, &res) != 0)
        return xs_array_new();

    Value *arr = xs_array_new();
    for (p = res; p; p = p->ai_next) {
        char ip[INET6_ADDRSTRLEN];
        if (p->ai_family == AF_INET) {
            inet_ntop(AF_INET, &((struct sockaddr_in*)p->ai_addr)->sin_addr, ip, sizeof ip);
        } else {
            inet_ntop(AF_INET6, &((struct sockaddr_in6*)p->ai_addr)->sin6_addr, ip, sizeof ip);
        }
        Value *s = xs_str(ip);
        array_push(arr->arr, s);
        value_decref(s);
    }
    freeaddrinfo(res);
    return arr;
#else
    (void)a; (void)n;
    return xs_array_new();
#endif
}

static Value *native_net_url_parse(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_map_new();
    Value *m = xs_map_new();
    const char *url = a[0]->s;

    /* scheme */
    const char *p = strstr(url, "://");
    if (p) {
        Value *v = xs_str_n(url, (int)(p - url)); map_set(m->map, "scheme", v); value_decref(v);
        url = p + 3;
    } else {
        Value *v = xs_str(""); map_set(m->map, "scheme", v); value_decref(v);
    }

    /* host and port */
    const char *slash = strchr(url, '/');
    const char *host_end = slash ? slash : url + strlen(url);
    char host_buf[512];
    int hlen = (int)(host_end - url);
    if (hlen >= (int)sizeof(host_buf)) hlen = (int)sizeof(host_buf) - 1;
    memcpy(host_buf, url, hlen); host_buf[hlen] = '\0';

    const char *colon = strchr(host_buf, ':');
    if (colon) {
        Value *hv = xs_str_n(host_buf, (int)(colon - host_buf));
        map_set(m->map, "host", hv); value_decref(hv);
        Value *pv = xs_int(atoi(colon + 1));
        map_set(m->map, "port", pv); value_decref(pv);
    } else {
        Value *hv = xs_str(host_buf);
        map_set(m->map, "host", hv); value_decref(hv);
        Value *pv = xs_int(0);
        map_set(m->map, "port", pv); value_decref(pv);
    }

    url = host_end;

    /* path and query */
    const char *qm = strchr(url, '?');
    if (qm) {
        Value *pv = xs_str_n(url, (int)(qm - url)); map_set(m->map, "path", pv); value_decref(pv);
        Value *qv = xs_str(qm + 1); map_set(m->map, "query", qv); value_decref(qv);
    } else {
        Value *pv = xs_str(url); map_set(m->map, "path", pv); value_decref(pv);
        Value *qv = xs_str(""); map_set(m->map, "query", qv); value_decref(qv);
    }

    return m;
}

/* ---- HTTP client helpers ---- */

#ifndef __MINGW32__

/* Parse a URL into host, port, path. Returns 0 on success, -1 on error. */
static int http_parse_url(const char *url, char *host, int hostlen,
                          int *port, char *path, int pathlen) {
    const char *start = url;
    if (strncmp(url, "https://", 8) == 0) { start = url + 8; *port = 443; }
    else if (strncmp(url, "http://", 7) == 0) { start = url + 7; *port = 80; }
    else { *port = 80; }

    const char *slash = strchr(start, '/');
    const char *host_end = slash ? slash : start + strlen(start);

    /* extract host:port */
    int hlen = (int)(host_end - start);
    char hbuf[512];
    if (hlen >= (int)sizeof(hbuf)) hlen = (int)sizeof(hbuf) - 1;
    memcpy(hbuf, start, hlen);
    hbuf[hlen] = '\0';

    const char *colon = strchr(hbuf, ':');
    if (colon) {
        int nlen = (int)(colon - hbuf);
        if (nlen >= hostlen) nlen = hostlen - 1;
        memcpy(host, hbuf, nlen);
        host[nlen] = '\0';
        *port = atoi(colon + 1);
    } else {
        if (hlen >= hostlen) hlen = hostlen - 1;
        memcpy(host, hbuf, hlen);
        host[hlen] = '\0';
    }

    if (slash) {
        int plen = (int)strlen(slash);
        if (plen >= pathlen) plen = pathlen - 1;
        memcpy(path, slash, plen);
        path[plen] = '\0';
    } else {
        path[0] = '/'; path[1] = '\0';
    }
    return 0;
}

/* Connect to host:port via TCP. Returns fd or -1. */
static int http_connect(const char *host, int port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[16];
    snprintf(port_str, sizeof port_str, "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0) return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Dynamic buffer for reading response */
typedef struct { char *data; size_t len; size_t cap; } HttpBuf;

static void httpbuf_init(HttpBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

static void httpbuf_append(HttpBuf *b, const char *src, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t newcap = (b->cap == 0) ? 4096 : b->cap * 2;
        while (newcap < b->len + n + 1) newcap *= 2;
        b->data = realloc(b->data, newcap);
        b->cap = newcap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void httpbuf_free(HttpBuf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

/* Read entire response from fd into buf */
static void http_read_all(int fd, HttpBuf *buf) {
    char tmp[4096];
    ssize_t nr;
    while ((nr = read(fd, tmp, sizeof tmp)) > 0)
        httpbuf_append(buf, tmp, (size_t)nr);
}

/* Parse HTTP response and return XS map: #{ status, headers, body } */
static Value *http_parse_response(HttpBuf *buf) {
    Value *result = xs_map_new();
    if (!buf->data || buf->len == 0) {
        Value *sv = xs_int(0); map_set(result->map, "status", sv); value_decref(sv);
        Value *hv = xs_map_new(); map_set(result->map, "headers", hv); value_decref(hv);
        Value *bv = xs_str(""); map_set(result->map, "body", bv); value_decref(bv);
        return result;
    }

    /* find end of status line */
    char *p = buf->data;
    char *end = buf->data + buf->len;
    char *line_end = strstr(p, "\r\n");
    if (!line_end) line_end = strstr(p, "\n");
    if (!line_end) {
        Value *sv = xs_int(0); map_set(result->map, "status", sv); value_decref(sv);
        Value *hv = xs_map_new(); map_set(result->map, "headers", hv); value_decref(hv);
        Value *bv = xs_str(""); map_set(result->map, "body", bv); value_decref(bv);
        return result;
    }

    /* parse status code from "HTTP/1.x NNN ..." */
    int status = 0;
    {
        char *sp = strchr(p, ' ');
        if (sp && sp < line_end) status = atoi(sp + 1);
    }
    Value *sv = xs_int(status); map_set(result->map, "status", sv); value_decref(sv);

    /* advance past status line */
    p = line_end;
    if (*p == '\r') p++;
    if (*p == '\n') p++;

    /* parse headers */
    Value *headers = xs_map_new();
    int chunked = 0;
    long content_length = -1;
    while (p < end) {
        /* blank line = end of headers */
        if (*p == '\r' || *p == '\n') {
            if (*p == '\r') p++;
            if (*p == '\n') p++;
            break;
        }
        char *hline_end = strstr(p, "\r\n");
        if (!hline_end) hline_end = strstr(p, "\n");
        if (!hline_end) hline_end = end;

        char *colon = memchr(p, ':', (size_t)(hline_end - p));
        if (colon) {
            int klen = (int)(colon - p);
            char key[256];
            if (klen >= (int)sizeof(key)) klen = (int)sizeof(key) - 1;
            memcpy(key, p, klen);
            key[klen] = '\0';
            /* lowercase the key for easier matching */
            char lkey[256];
            for (int i = 0; i <= klen; i++) lkey[i] = (char)tolower((unsigned char)key[i]);

            const char *val = colon + 1;
            while (val < hline_end && *val == ' ') val++;
            int vlen = (int)(hline_end - val);

            Value *vv = xs_str_n(val, vlen);
            map_set(headers->map, key, vv);
            value_decref(vv);

            /* detect chunked / content-length */
            if (strcmp(lkey, "transfer-encoding") == 0) {
                char vbuf[128];
                int cl = vlen < (int)sizeof(vbuf) - 1 ? vlen : (int)sizeof(vbuf) - 1;
                memcpy(vbuf, val, cl); vbuf[cl] = '\0';
                for (int i = 0; vbuf[i]; i++) vbuf[i] = (char)tolower((unsigned char)vbuf[i]);
                if (strstr(vbuf, "chunked")) chunked = 1;
            } else if (strcmp(lkey, "content-length") == 0) {
                content_length = atol(val);
            }
        }
        p = hline_end;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }
    map_set(result->map, "headers", headers);
    value_decref(headers);

    /* extract body */
    size_t body_offset = (size_t)(p - buf->data);
    size_t body_avail = (body_offset < buf->len) ? buf->len - body_offset : 0;

    if (chunked) {
        /* decode chunked transfer encoding */
        HttpBuf decoded;
        httpbuf_init(&decoded);
        char *cp = p;
        while (cp < end) {
            /* read chunk size (hex) */
            char *chunk_end = strstr(cp, "\r\n");
            if (!chunk_end) break;
            long chunk_size = strtol(cp, NULL, 16);
            if (chunk_size <= 0) break;
            cp = chunk_end + 2; /* skip \r\n after size */
            if (cp + chunk_size > end) chunk_size = (long)(end - cp);
            httpbuf_append(&decoded, cp, (size_t)chunk_size);
            cp += chunk_size;
            if (cp + 2 <= end && cp[0] == '\r' && cp[1] == '\n') cp += 2;
        }
        Value *bv = xs_str_n(decoded.data ? decoded.data : "", (int)decoded.len);
        map_set(result->map, "body", bv);
        value_decref(bv);
        httpbuf_free(&decoded);
    } else if (content_length >= 0) {
        size_t blen = (size_t)content_length;
        if (blen > body_avail) blen = body_avail;
        Value *bv = xs_str_n(p, (int)blen);
        map_set(result->map, "body", bv);
        value_decref(bv);
    } else {
        /* read until close */
        Value *bv = xs_str_n(p, (int)body_avail);
        map_set(result->map, "body", bv);
        value_decref(bv);
    }

    return result;
}

/* Core: perform an HTTP request. Returns XS map. */
static Value *http_do_request(const char *method, const char *url,
                              XSMap *extra_headers, const char *body,
                              size_t body_len) {
    char host[512], path[2048];
    int port;
    if (http_parse_url(url, host, sizeof host, &port, path, sizeof path) < 0)
        return value_incref(XS_NULL_VAL);

    int use_tls = (strncmp(url, "https://", 8) == 0);
    int fd = http_connect(host, port);
    if (fd < 0) {
        fprintf(stderr, "error: could not connect to %s:%d\n", host, port);
        return value_incref(XS_NULL_VAL);
    }

    xs_tls_conn *tls = NULL;
    if (use_tls) {
        tls = xs_tls_connect(fd, host);
        if (!tls) {
            fprintf(stderr, "error: TLS handshake failed for %s\n", host);
            close(fd);
            return value_incref(XS_NULL_VAL);
        }
    }

    /* build request */
    HttpBuf req;
    httpbuf_init(&req);

    /* request line */
    httpbuf_append(&req, method, strlen(method));
    httpbuf_append(&req, " ", 1);
    httpbuf_append(&req, path, strlen(path));
    httpbuf_append(&req, " HTTP/1.1\r\n", 11);

    /* Host header */
    httpbuf_append(&req, "Host: ", 6);
    httpbuf_append(&req, host, strlen(host));
    if (port != 80) {
        char pbuf[16];
        snprintf(pbuf, sizeof pbuf, ":%d", port);
        httpbuf_append(&req, pbuf, strlen(pbuf));
    }
    httpbuf_append(&req, "\r\n", 2);

    /* Connection: close */
    httpbuf_append(&req, "Connection: close\r\n", 19);

    /* Content-Length if body present */
    if (body && body_len > 0) {
        char clbuf[64];
        snprintf(clbuf, sizeof clbuf, "Content-Length: %zu\r\n", body_len);
        httpbuf_append(&req, clbuf, strlen(clbuf));
    }

    /* extra headers from map */
    if (extra_headers) {
        for (int i = 0; i < extra_headers->cap; i++) {
            if (extra_headers->keys[i] && extra_headers->vals[i]) {
                const char *k = extra_headers->keys[i];
                Value *v = extra_headers->vals[i];
                if (v->tag == XS_STR) {
                    httpbuf_append(&req, k, strlen(k));
                    httpbuf_append(&req, ": ", 2);
                    httpbuf_append(&req, v->s, strlen(v->s));
                    httpbuf_append(&req, "\r\n", 2);
                }
            }
        }
    }

    /* end of headers */
    httpbuf_append(&req, "\r\n", 2);

    /* send request */
    if (tls) {
        if (xs_tls_write(tls, req.data, (int)req.len) < 0) {
            xs_tls_close(tls); httpbuf_free(&req); return value_incref(XS_NULL_VAL);
        }
    } else {
        size_t sent = 0;
        while (sent < req.len) {
            ssize_t w = write(fd, req.data + sent, req.len - sent);
            if (w <= 0) { close(fd); httpbuf_free(&req); return value_incref(XS_NULL_VAL); }
            sent += (size_t)w;
        }
    }
    httpbuf_free(&req);

    /* send body */
    if (body && body_len > 0) {
        if (tls) {
            if (xs_tls_write(tls, body, (int)body_len) < 0) {
                xs_tls_close(tls); return value_incref(XS_NULL_VAL);
            }
        } else {
            size_t sent = 0;
            while (sent < body_len) {
                ssize_t w = write(fd, body + sent, body_len - sent);
                if (w <= 0) { close(fd); return value_incref(XS_NULL_VAL); }
                sent += (size_t)w;
            }
        }
    }

    /* read response */
    HttpBuf resp;
    httpbuf_init(&resp);
    if (tls) {
        char tmp[4096];
        int nr;
        while ((nr = xs_tls_read(tls, tmp, sizeof tmp)) > 0)
            httpbuf_append(&resp, tmp, (size_t)nr);
        xs_tls_close(tls);
    } else {
        http_read_all(fd, &resp);
        close(fd);
    }

    Value *result = http_parse_response(&resp);
    httpbuf_free(&resp);
    return result;
}

#endif /* __MINGW32__ */

/* net.http_get(url) */
static Value *native_net_http_get(Interp *ig, Value **a, int n) {
    (void)ig;
#ifndef __MINGW32__
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_NULL_VAL);
    return http_do_request("GET", a[0]->s, NULL, NULL, 0);
#else
    (void)a; (void)n;
    return value_incref(XS_NULL_VAL);
#endif
}

/* net.http_post(url, body, content_type) */
static Value *native_net_http_post(Interp *ig, Value **a, int n) {
    (void)ig;
#ifndef __MINGW32__
    if (n < 3 || a[0]->tag != XS_STR || a[1]->tag != XS_STR || a[2]->tag != XS_STR)
        return value_incref(XS_NULL_VAL);

    /* build a temporary headers map with Content-Type */
    XSMap *hdrs = map_new();
    Value *ct = xs_str(a[2]->s);
    map_set(hdrs, "Content-Type", ct);
    value_decref(ct);

    Value *result = http_do_request("POST", a[0]->s, hdrs, a[1]->s, strlen(a[1]->s));
    map_free(hdrs);
    return result;
#else
    (void)a; (void)n;
    return value_incref(XS_NULL_VAL);
#endif
}

/* net.http(method, url, headers_map, body) */
static Value *native_net_http(Interp *ig, Value **a, int n) {
    (void)ig;
#ifndef __MINGW32__
    if (n < 2 || a[0]->tag != XS_STR || a[1]->tag != XS_STR)
        return value_incref(XS_NULL_VAL);

    XSMap *hdrs = NULL;
    if (n >= 3 && a[2]->tag == XS_MAP) hdrs = a[2]->map;
    const char *body = NULL;
    size_t body_len = 0;
    if (n >= 4 && a[3]->tag == XS_STR) {
        body = a[3]->s;
        body_len = strlen(a[3]->s);
    }

    return http_do_request(a[0]->s, a[1]->s, hdrs, body, body_len);
#else
    (void)a; (void)n;
    return value_incref(XS_NULL_VAL);
#endif
}

Value *make_net_module(void) {
    XSMap *m = map_new();
    map_set(m, "tcp_connect", xs_native(native_net_tcp_connect));
    map_set(m, "tcp_listen",  xs_native(native_net_tcp_listen));
    map_set(m, "resolve",     xs_native(native_net_resolve));
    map_set(m, "url_parse",   xs_native(native_net_url_parse));
    map_set(m, "http_get",    xs_native(native_net_http_get));
    map_set(m, "http_post",   xs_native(native_net_http_post));
    map_set(m, "http",        xs_native(native_net_http));
    return xs_module(m);
}

/* crypto */

/* SHA-256 implementation */
static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t sha256_rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64], a, b, c, d, e, f, g, h;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4]<<24) | ((uint32_t)block[i*4+1]<<16) |
               ((uint32_t)block[i*4+2]<<8) | (uint32_t)block[i*4+3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotr(w[i-15],7) ^ sha256_rotr(w[i-15],18) ^ (w[i-15]>>3);
        uint32_t s1 = sha256_rotr(w[i-2],17) ^ sha256_rotr(w[i-2],19) ^ (w[i-2]>>10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a=state[0]; b=state[1]; c=state[2]; d=state[3];
    e=state[4]; f=state[5]; g=state[6]; h=state[7];
    for (i = 0; i < 64; i++) {
        uint32_t S1 = sha256_rotr(e,6) ^ sha256_rotr(e,11) ^ sha256_rotr(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0 = sha256_rotr(a,2) ^ sha256_rotr(a,13) ^ sha256_rotr(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha256_hash(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    uint8_t block[64];
    size_t i;
    /* process full blocks */
    for (i = 0; i + 64 <= len; i += 64)
        sha256_transform(state, data + i);
    /* final block with padding */
    size_t rem = len - i;
    memset(block, 0, 64);
    if (rem > 0) memcpy(block, data + i, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        sha256_transform(state, block);
        memset(block, 0, 64);
    }
    /* length in bits (big-endian) */
    uint64_t bits = (uint64_t)len * 8;
    for (int j = 7; j >= 0; j--) {
        block[56 + (7 - j)] = (uint8_t)(bits >> (j * 8));
    }
    sha256_transform(state, block);
    /* output */
    for (i = 0; i < 8; i++) {
        out[i*4+0] = (uint8_t)(state[i]>>24);
        out[i*4+1] = (uint8_t)(state[i]>>16);
        out[i*4+2] = (uint8_t)(state[i]>>8);
        out[i*4+3] = (uint8_t)(state[i]);
    }
}

/* MD5 implementation */
static const uint32_t md5_s[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};
static const uint32_t md5_k_tab[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static uint32_t md5_leftrotate(uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); }

static void md5_hash(const uint8_t *data, size_t len, uint8_t out[16]) {
    uint32_t a0=0x67452301, b0=0xefcdab89, c0=0x98badcfe, d0=0x10325476;
    /* pad message */
    size_t new_len = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = xs_calloc(new_len, 1);
    memcpy(msg, data, len);
    msg[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    memcpy(msg + new_len - 8, &bits, 8); /* little-endian */

    for (size_t off = 0; off < new_len; off += 64) {
        uint32_t *M = (uint32_t*)(msg + off);
        uint32_t A=a0, B=b0, C=c0, D=d0;
        for (int i = 0; i < 64; i++) {
            uint32_t F, g;
            if (i < 16)      { F = (B&C)|(~B&D); g = (uint32_t)i; }
            else if (i < 32) { F = (D&B)|(~D&C); g = (5*(uint32_t)i+1)%16; }
            else if (i < 48) { F = B^C^D;        g = (3*(uint32_t)i+5)%16; }
            else              { F = C^(B|~D);     g = (7*(uint32_t)i)%16; }
            F = F + A + md5_k_tab[i] + M[g];
            A = D; D = C; C = B;
            B = B + md5_leftrotate(F, md5_s[i]);
        }
        a0+=A; b0+=B; c0+=C; d0+=D;
    }
    free(msg);
    memcpy(out+0,  &a0, 4);
    memcpy(out+4,  &b0, 4);
    memcpy(out+8,  &c0, 4);
    memcpy(out+12, &d0, 4);
}

static Value *native_crypto_sha256(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    uint8_t hash[32];
    sha256_hash((const uint8_t*)a[0]->s, strlen(a[0]->s), hash);
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + i*2, "%02x", hash[i]);
    hex[64] = '\0';
    return xs_str(hex);
}

static Value *native_crypto_md5(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    uint8_t hash[16];
    md5_hash((const uint8_t*)a[0]->s, strlen(a[0]->s), hash);
    char hex[33];
    for (int i = 0; i < 16; i++) sprintf(hex + i*2, "%02x", hash[i]);
    hex[32] = '\0';
    return xs_str(hex);
}

static Value *native_crypto_random_bytes(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_INT) return xs_str("");
    int count = (int)a[0]->i;
    if (count <= 0 || count > 65536) return xs_str("");
    uint8_t *buf = xs_malloc((size_t)count);
#ifndef __MINGW32__
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { if (fread(buf, 1, (size_t)count, f) < (size_t)count) { /* partial read ok */ } fclose(f); }
    else { for (int i=0;i<count;i++) buf[i]=(uint8_t)(rand()&0xff); }
#else
    /* Windows: use RtlGenRandom (SystemFunction036) from advapi32 */
    {
        typedef BOOLEAN (APIENTRY *RtlGenRandomFn)(PVOID, ULONG);
        HMODULE advapi = LoadLibraryA("advapi32.dll");
        int filled = 0;
        if (advapi) {
            RtlGenRandomFn RtlGenRandom = (RtlGenRandomFn)GetProcAddress(advapi, "SystemFunction036");
            if (RtlGenRandom && RtlGenRandom(buf, (ULONG)count)) filled = 1;
            FreeLibrary(advapi);
        }
        if (!filled) {
            /* Fallback: seed with time + pid for better entropy than raw rand() */
            srand((unsigned)(time(NULL) ^ GetCurrentProcessId() ^ GetTickCount()));
            for (int i=0;i<count;i++) buf[i]=(uint8_t)(rand()&0xff);
        }
    }
#endif
    /* return as hex string */
    char *hex = xs_malloc((size_t)count * 2 + 1);
    for (int i = 0; i < count; i++) sprintf(hex + i*2, "%02x", buf[i]);
    hex[count*2] = '\0';
    free(buf);
    Value *r = xs_str(hex);
    free(hex);
    return r;
}

static Value *native_crypto_random_int(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2) return xs_int(0);
    int64_t lo = (a[0]->tag == XS_INT) ? a[0]->i : 0;
    int64_t hi = (a[1]->tag == XS_INT) ? a[1]->i : 0;
    if (hi <= lo) return xs_int(lo);
    /* use /dev/urandom for better randomness */
    uint64_t r;
#ifndef __MINGW32__
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { if (fread(&r, sizeof r, 1, f) < 1) { r = (uint64_t)rand(); } fclose(f); }
    else { r = (uint64_t)rand(); }
#else
    {
        typedef BOOLEAN (APIENTRY *RtlGenRandomFn)(PVOID, ULONG);
        HMODULE advapi = LoadLibraryA("advapi32.dll");
        int filled = 0;
        if (advapi) {
            RtlGenRandomFn RtlGenRandom = (RtlGenRandomFn)GetProcAddress(advapi, "SystemFunction036");
            if (RtlGenRandom && RtlGenRandom(&r, (ULONG)sizeof(r))) filled = 1;
            FreeLibrary(advapi);
        }
        if (!filled) {
            srand((unsigned)(time(NULL) ^ GetCurrentProcessId() ^ GetTickCount()));
            r = (uint64_t)rand();
        }
    }
#endif
    return xs_int(lo + (int64_t)(r % (uint64_t)(hi - lo)));
}

static Value *native_crypto_uuid4(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    uint8_t bytes[16];
#ifndef __MINGW32__
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { if (fread(bytes, 1, 16, f) < 16) { /* partial read ok */ } fclose(f); }
    else { for (int i=0;i<16;i++) bytes[i]=(uint8_t)(rand()&0xff); }
#else
    {
        typedef BOOLEAN (APIENTRY *RtlGenRandomFn)(PVOID, ULONG);
        HMODULE advapi = LoadLibraryA("advapi32.dll");
        int filled = 0;
        if (advapi) {
            RtlGenRandomFn RtlGenRandom = (RtlGenRandomFn)GetProcAddress(advapi, "SystemFunction036");
            if (RtlGenRandom && RtlGenRandom(bytes, 16)) filled = 1;
            FreeLibrary(advapi);
        }
        if (!filled) {
            srand((unsigned)(time(NULL) ^ GetCurrentProcessId() ^ GetTickCount()));
            for (int i=0;i<16;i++) bytes[i]=(uint8_t)(rand()&0xff);
        }
    }
#endif
    bytes[6] = (bytes[6] & 0x0f) | 0x40; /* version 4 */
    bytes[8] = (bytes[8] & 0x3f) | 0x80; /* variant 1 */
    char uuid[37];
    snprintf(uuid, sizeof uuid,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0],bytes[1],bytes[2],bytes[3],
        bytes[4],bytes[5],bytes[6],bytes[7],
        bytes[8],bytes[9],bytes[10],bytes[11],
        bytes[12],bytes[13],bytes[14],bytes[15]);
    return xs_str(uuid);
}

static Value *native_crypto_hash(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_int(0);
    /* djb2 hash */
    const char *s = a[0]->s;
    uint64_t h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)*s++;
    return xs_int((int64_t)h);
}

static Value *native_crypto_hex_encode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const char *s = a[0]->s;
    int slen = (int)strlen(s);
    char *hex = xs_malloc(slen * 2 + 1);
    for (int i = 0; i < slen; i++) sprintf(hex + i*2, "%02x", (unsigned char)s[i]);
    hex[slen * 2] = '\0';
    Value *r = xs_str(hex); free(hex); return r;
}

static Value *native_crypto_hex_decode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const char *s = a[0]->s;
    int slen = (int)strlen(s);
    if (slen % 2 != 0) return xs_str("");
    int olen = slen / 2;
    char *out = xs_malloc(olen + 1);
    for (int i = 0; i < olen; i++) {
        unsigned int byte;
        if (sscanf(s + i*2, "%2x", &byte) != 1) { free(out); return xs_str(""); }
        out[i] = (char)byte;
    }
    out[olen] = '\0';
    Value *r = xs_str(out); free(out); return r;
}

static Value *native_crypto_base64_encode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const unsigned char *s = (const unsigned char*)a[0]->s;
    int slen = (int)strlen(a[0]->s);
    int rlen = ((slen + 2) / 3) * 4;
    char *r = xs_malloc(rlen + 1); int ri = 0;
    for (int j = 0; j < slen; j += 3) {
        unsigned int v = (unsigned)s[j]<<16 | (j+1<slen?(unsigned)s[j+1]:0)<<8 | (j+2<slen?(unsigned)s[j+2]:0);
        r[ri++] = b64_table[(v>>18)&63]; r[ri++] = b64_table[(v>>12)&63];
        r[ri++] = (j+1<slen) ? b64_table[(v>>6)&63] : '=';
        r[ri++] = (j+2<slen) ? b64_table[v&63] : '=';
    }
    r[ri] = '\0'; Value *v2 = xs_str(r); free(r); return v2;
}

static Value *native_crypto_base64_decode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const char *s = a[0]->s; int slen = (int)strlen(s);
    static const signed char inv[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    char *r = xs_malloc(slen); int ri = 0;
    for (int j = 0; j+3 < slen; j += 4) {
        int a0=inv[(unsigned char)s[j]], a1=inv[(unsigned char)s[j+1]];
        int a2=inv[(unsigned char)s[j+2]], a3=inv[(unsigned char)s[j+3]];
        if (a0<0||a1<0) break;
        r[ri++] = (char)((a0<<2)|(a1>>4));
        if (a2>=0) r[ri++] = (char)((a1<<4)|(a2>>2));
        if (a3>=0) r[ri++] = (char)((a2<<6)|a3);
    }
    r[ri] = '\0'; Value *v2 = xs_str(r); free(r); return v2;
}

Value *make_crypto_module(void) {
    XSMap *m = map_new();
    map_set(m, "sha256",        xs_native(native_crypto_sha256));
    map_set(m, "md5",           xs_native(native_crypto_md5));
    map_set(m, "hash",          xs_native(native_crypto_hash));
    map_set(m, "hex_encode",    xs_native(native_crypto_hex_encode));
    map_set(m, "hex_decode",    xs_native(native_crypto_hex_decode));
    map_set(m, "base64_encode", xs_native(native_crypto_base64_encode));
    map_set(m, "base64_decode", xs_native(native_crypto_base64_decode));
    map_set(m, "random_bytes",  xs_native(native_crypto_random_bytes));
    map_set(m, "random_int",    xs_native(native_crypto_random_int));
    map_set(m, "uuid4",         xs_native(native_crypto_uuid4));
    return xs_module(m);
}

/* threads */
#include "core/xs_thread.h"

typedef struct {
    Interp *interp;  /* parent interp (used read-only for call_value) */
    Value  *fn;      /* function to call — incref'd before thread start */
    Value  *result;  /* output — set by the thread */
} ThreadArg;

static void *thread_entry(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    /* Call the XS function in isolation.
       Since XS values are not thread-safe we keep it simple:
       the function receives no arguments and its return value is
       stored for later retrieval via thread.join(). */
    ta->result = call_value(ta->interp, ta->fn, NULL, 0, "thread.spawn");
    return NULL;
}

/* forward declaration so the non-POSIX stub in spawn can reference join */
static Value *native_thread_join(Interp *ig, Value **a, int n);

static Value *native_thread_spawn(Interp *ig, Value **a, int n) {
    if (n < 1 || (a[0]->tag != XS_FUNC && a[0]->tag != XS_NATIVE))
        return xs_str("error: thread.spawn requires a callable");

    ThreadArg *ta = xs_malloc(sizeof(ThreadArg));
    ta->interp = ig;
    ta->fn     = value_incref(a[0]);
    ta->result = NULL;

    xs_thread_t tid;
    if (xs_thread_create(&tid, thread_entry, ta) != 0) {
        value_decref(ta->fn);
        free(ta);
        return xs_str("error: thread creation failed");
    }

    XSMap *handle = map_new();
    map_set(handle, "_tid",  xs_int((int64_t)(uintptr_t)tid));
    map_set(handle, "_targ", xs_int((int64_t)(uintptr_t)ta));
    map_set(handle, "status", xs_str("running"));
    return xs_module(handle);
}

static Value *native_thread_join(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || (a[0]->tag != XS_MAP && a[0]->tag != XS_MODULE))
        return xs_str("error: thread.join requires a thread handle");
    Value *tid_v  = map_get(a[0]->map, "_tid");
    Value *targ_v = map_get(a[0]->map, "_targ");
    if (!tid_v || tid_v->tag != XS_INT || !targ_v || targ_v->tag != XS_INT)
        return xs_str("error: invalid thread handle");

    xs_thread_t tid = (xs_thread_t)(uintptr_t)tid_v->i;
    ThreadArg *ta = (ThreadArg *)(uintptr_t)targ_v->i;

    int err = xs_thread_join(tid, NULL);
    if (err != 0)
        return xs_str("error: thread join failed");

    Value *result = ta->result ? ta->result : value_incref(XS_NULL_VAL);
    value_decref(ta->fn);
    free(ta);

    /* Update handle status */
    map_set(a[0]->map, "status", xs_str("joined"));

    return result;
}

static Value *native_thread_id(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    return xs_int((int64_t)xs_thread_self_id());
}

static Value *native_thread_cpu_count(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
#if defined(_SC_NPROCESSORS_ONLN)
    return xs_int(sysconf(_SC_NPROCESSORS_ONLN));
#else
    return xs_int(1);
#endif
}

static Value *native_thread_sleep(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1) return value_incref(XS_NULL_VAL);
    double secs = 0.0;
    if (a[0]->tag == XS_FLOAT) secs = a[0]->f;
    else if (a[0]->tag == XS_INT) secs = (double)a[0]->i;
    xs_thread_sleep_ns(secs);
    return value_incref(XS_NULL_VAL);
}

/* ── cross-platform mutex implementation ───────────────────── */

static xs_mutex_t *mutex_from_map(XSMap *m) {
    Value *pv = map_get(m, "_ptr");
    if (!pv || pv->tag != XS_INT) return NULL;
    return (xs_mutex_t *)(uintptr_t)pv->i;
}

static Value *native_mutex_lock_fn(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    /* 'self' is passed as the first argument by the method-call dispatch */
    if (n < 1 || (a[0]->tag != XS_MAP && a[0]->tag != XS_MODULE))
        return value_incref(XS_FALSE_VAL);
    xs_mutex_t *mtx = mutex_from_map(a[0]->map);
    if (!mtx) return value_incref(XS_FALSE_VAL);
    int err = xs_mutex_lock(mtx);
    if (err == 0) map_set(a[0]->map, "locked", value_incref(XS_TRUE_VAL));
    return err == 0 ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_mutex_unlock_fn(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    if (n < 1 || (a[0]->tag != XS_MAP && a[0]->tag != XS_MODULE))
        return value_incref(XS_FALSE_VAL);
    xs_mutex_t *mtx = mutex_from_map(a[0]->map);
    if (!mtx) return value_incref(XS_FALSE_VAL);
    int err = xs_mutex_unlock(mtx);
    if (err == 0) map_set(a[0]->map, "locked", value_incref(XS_FALSE_VAL));
    return err == 0 ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_mutex_try_lock_fn(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    if (n < 1 || (a[0]->tag != XS_MAP && a[0]->tag != XS_MODULE))
        return value_incref(XS_FALSE_VAL);
    xs_mutex_t *mtx = mutex_from_map(a[0]->map);
    if (!mtx) return value_incref(XS_FALSE_VAL);
    int err = xs_mutex_trylock(mtx);
    if (err == 0) {
        map_set(a[0]->map, "locked", value_incref(XS_TRUE_VAL));
        return value_incref(XS_TRUE_VAL);
    }
    return value_incref(XS_FALSE_VAL);
}

static Value *native_mutex_destroy_fn(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    if (n < 1 || (a[0]->tag != XS_MAP && a[0]->tag != XS_MODULE))
        return value_incref(XS_NULL_VAL);
    xs_mutex_t *mtx = mutex_from_map(a[0]->map);
    if (mtx) {
        xs_mutex_destroy(mtx);
        free(mtx);
        /* Clear the pointer so double-destroy is harmless */
        map_set(a[0]->map, "_ptr", xs_int(0));
    }
    return value_incref(XS_NULL_VAL);
}

static Value *native_thread_mutex(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    xs_mutex_t *mtx = xs_malloc(sizeof(xs_mutex_t));
    if (xs_mutex_init(mtx) != 0) {
        free(mtx);
        return value_incref(XS_NULL_VAL);
    }
    XSMap *m = map_new();
    /* Store the mutex pointer as an opaque int (same pattern as XSBuf) */
    map_set(m, "_ptr", xs_int((int64_t)(uintptr_t)mtx));
    map_set(m, "locked", value_incref(XS_FALSE_VAL));
    map_set(m, "lock",    xs_native(native_mutex_lock_fn));
    map_set(m, "unlock",  xs_native(native_mutex_unlock_fn));
    map_set(m, "try_lock", xs_native(native_mutex_try_lock_fn));
    map_set(m, "destroy", xs_native(native_mutex_destroy_fn));
    return xs_module(m);
}

Value *make_thread_module(void) {
    XSMap *m = map_new();
    map_set(m, "spawn",     xs_native(native_thread_spawn));
    map_set(m, "join",      xs_native(native_thread_join));
    map_set(m, "id",        xs_native(native_thread_id));
    map_set(m, "cpu_count", xs_native(native_thread_cpu_count));
    map_set(m, "sleep",     xs_native(native_thread_sleep));
    map_set(m, "mutex",     xs_native(native_thread_mutex));
    return xs_module(m);
}

/* byte buffers */

typedef struct {
    uint8_t *data;
    int      len;
    int      cap;
    int      pos; /* read position */
} XSBuf;

static XSBuf *buf_create(int cap) {
    XSBuf *b = xs_malloc(sizeof(XSBuf));
    b->cap = cap > 0 ? cap : 64;
    b->data = xs_malloc((size_t)b->cap);
    b->len = 0;
    b->pos = 0;
    return b;
}

static void buf_ensure(XSBuf *b, int need) {
    while (b->len + need > b->cap) {
        b->cap *= 2;
        b->data = xs_realloc(b->data, (size_t)b->cap);
    }
}

/* We store the XSBuf pointer as an int (cast). A bit hacky but simple. */
static XSBuf *buf_from_map(XSMap *m) {
    Value *pv = map_get(m, "_ptr");
    if (!pv || pv->tag != XS_INT) return NULL;
    return (XSBuf*)(uintptr_t)pv->i;
}

static Value *native_buf_new(Interp *ig, Value **a, int n) {
    (void)ig;
    int cap = (n > 0 && a[0]->tag == XS_INT) ? (int)a[0]->i : 64;
    XSBuf *b = buf_create(cap);
    XSMap *m = map_new();
    map_set(m, "_ptr", xs_int((int64_t)(uintptr_t)b));
    return xs_module(m);
}

static Value *native_buf_write_u8(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || a[0]->tag != XS_MAP) return value_incref(XS_NULL_VAL);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return value_incref(XS_NULL_VAL);
    uint8_t val = (a[1]->tag == XS_INT) ? (uint8_t)a[1]->i : 0;
    buf_ensure(b, 1);
    b->data[b->len++] = val;
    return value_incref(XS_NULL_VAL);
}

static Value *native_buf_write_u16(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || a[0]->tag != XS_MAP) return value_incref(XS_NULL_VAL);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return value_incref(XS_NULL_VAL);
    uint16_t val = (a[1]->tag == XS_INT) ? (uint16_t)a[1]->i : 0;
    buf_ensure(b, 2);
    b->data[b->len++] = (uint8_t)(val & 0xff);
    b->data[b->len++] = (uint8_t)((val >> 8) & 0xff);
    return value_incref(XS_NULL_VAL);
}

static Value *native_buf_write_u32(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || a[0]->tag != XS_MAP) return value_incref(XS_NULL_VAL);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return value_incref(XS_NULL_VAL);
    uint32_t val = (a[1]->tag == XS_INT) ? (uint32_t)a[1]->i : 0;
    buf_ensure(b, 4);
    for (int i=0;i<4;i++) b->data[b->len++] = (uint8_t)((val >> (i*8)) & 0xff);
    return value_incref(XS_NULL_VAL);
}

static Value *native_buf_write_u64(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || a[0]->tag != XS_MAP) return value_incref(XS_NULL_VAL);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return value_incref(XS_NULL_VAL);
    uint64_t val = (a[1]->tag == XS_INT) ? (uint64_t)a[1]->i : 0;
    buf_ensure(b, 8);
    for (int i=0;i<8;i++) b->data[b->len++] = (uint8_t)((val >> (i*8)) & 0xff);
    return value_incref(XS_NULL_VAL);
}

static Value *native_buf_read_u8(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_MAP) return xs_int(0);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b || b->pos >= b->len) return xs_int(0);
    return xs_int(b->data[b->pos++]);
}

static Value *native_buf_read_u16(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_MAP) return xs_int(0);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b || b->pos + 2 > b->len) return xs_int(0);
    uint16_t v = (uint16_t)b->data[b->pos] | ((uint16_t)b->data[b->pos+1] << 8);
    b->pos += 2;
    return xs_int(v);
}

static Value *native_buf_read_u32(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_MAP) return xs_int(0);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b || b->pos + 4 > b->len) return xs_int(0);
    uint32_t v = 0;
    for (int i=0;i<4;i++) v |= ((uint32_t)b->data[b->pos++] << (i*8));
    return xs_int((int64_t)v);
}

static Value *native_buf_read_u64(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_MAP) return xs_int(0);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b || b->pos + 8 > b->len) return xs_int(0);
    uint64_t v = 0;
    for (int i=0;i<8;i++) v |= ((uint64_t)b->data[b->pos++] << (i*8));
    return xs_int((int64_t)v);
}

static Value *native_buf_write_str(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || a[0]->tag != XS_MAP || a[1]->tag != XS_STR)
        return value_incref(XS_NULL_VAL);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return value_incref(XS_NULL_VAL);
    size_t slen = strlen(a[1]->s);
    buf_ensure(b, (int)slen);
    memcpy(b->data + b->len, a[1]->s, slen);
    b->len += (int)slen;
    return value_incref(XS_NULL_VAL);
}

static Value *native_buf_to_str(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_MAP) return xs_str("");
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return xs_str("");
    return xs_str_n((const char*)b->data, (size_t)b->len);
}

static Value *native_buf_to_hex(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_MAP) return xs_str("");
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b || b->len == 0) return xs_str("");
    char *hex = xs_malloc((size_t)b->len * 2 + 1);
    for (int i = 0; i < b->len; i++) sprintf(hex + i*2, "%02x", b->data[i]);
    hex[b->len * 2] = '\0';
    Value *r = xs_str(hex);
    free(hex);
    return r;
}

static Value *native_buf_len(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_MAP) return xs_int(0);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return xs_int(0);
    return xs_int(b->len);
}

Value *make_buf_module(void) {
    XSMap *m = map_new();
    map_set(m, "new",       xs_native(native_buf_new));
    map_set(m, "write_u8",  xs_native(native_buf_write_u8));
    map_set(m, "write_u16", xs_native(native_buf_write_u16));
    map_set(m, "write_u32", xs_native(native_buf_write_u32));
    map_set(m, "write_u64", xs_native(native_buf_write_u64));
    map_set(m, "read_u8",   xs_native(native_buf_read_u8));
    map_set(m, "read_u16",  xs_native(native_buf_read_u16));
    map_set(m, "read_u32",  xs_native(native_buf_read_u32));
    map_set(m, "read_u64",  xs_native(native_buf_read_u64));
    map_set(m, "write_str", xs_native(native_buf_write_str));
    map_set(m, "to_str",    xs_native(native_buf_to_str));
    map_set(m, "to_hex",    xs_native(native_buf_to_hex));
    map_set(m, "len",       xs_native(native_buf_len));
    return xs_module(m);
}

/* encoding: base64, hex, json */

/* b64_table already defined above for base64 module */

static Value *native_encode_base64_encode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const uint8_t *in = (const uint8_t*)a[0]->s;
    size_t len = strlen(a[0]->s);
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = xs_malloc(out_len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (uint32_t)in[i] << 16;
        if (i+1 < len) val |= (uint32_t)in[i+1] << 8;
        if (i+2 < len) val |= (uint32_t)in[i+2];
        out[j++] = b64_table[(val >> 18) & 0x3f];
        out[j++] = b64_table[(val >> 12) & 0x3f];
        out[j++] = (i+1 < len) ? b64_table[(val >> 6) & 0x3f] : '=';
        out[j++] = (i+2 < len) ? b64_table[val & 0x3f] : '=';
    }
    out[j] = '\0';
    Value *r = xs_str(out);
    free(out);
    return r;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static Value *native_encode_base64_decode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const char *in = a[0]->s;
    size_t len = strlen(in);
    if (len % 4 != 0) return xs_str("");
    size_t out_len = len / 4 * 3;
    if (len > 0 && in[len-1] == '=') out_len--;
    if (len > 1 && in[len-2] == '=') out_len--;
    char *out = xs_malloc(out_len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i += 4) {
        int v0 = b64_decode_char(in[i]);
        int v1 = b64_decode_char(in[i+1]);
        int v2 = (in[i+2] == '=') ? 0 : b64_decode_char(in[i+2]);
        int v3 = (in[i+3] == '=') ? 0 : b64_decode_char(in[i+3]);
        if (v0<0||v1<0) break;
        uint32_t val = ((uint32_t)v0<<18)|((uint32_t)v1<<12)|((uint32_t)v2<<6)|(uint32_t)v3;
        out[j++] = (char)((val >> 16) & 0xff);
        if (in[i+2] != '=') out[j++] = (char)((val >> 8) & 0xff);
        if (in[i+3] != '=') out[j++] = (char)(val & 0xff);
    }
    out[j] = '\0';
    Value *r = xs_str_n(out, j);
    free(out);
    return r;
}

static Value *native_encode_hex_encode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const uint8_t *in = (const uint8_t*)a[0]->s;
    size_t len = strlen(a[0]->s);
    char *out = xs_malloc(len * 2 + 1);
    for (size_t i = 0; i < len; i++) sprintf(out + i*2, "%02x", in[i]);
    out[len*2] = '\0';
    Value *r = xs_str(out);
    free(out);
    return r;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static Value *native_encode_hex_decode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const char *in = a[0]->s;
    size_t len = strlen(in);
    if (len % 2 != 0) return xs_str("");
    size_t out_len = len / 2;
    char *out = xs_malloc(out_len + 1);
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_val(in[i*2]);
        int lo = hex_val(in[i*2+1]);
        if (hi < 0 || lo < 0) { free(out); return xs_str(""); }
        out[i] = (char)((hi << 4) | lo);
    }
    out[out_len] = '\0';
    Value *r = xs_str_n(out, out_len);
    free(out);
    return r;
}

static Value *native_encode_url_encode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const char *in = a[0]->s;
    size_t len = strlen(in);
    /* worst case: every char becomes %XX (3x) */
    char *out = xs_malloc(len * 3 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = (char)c;
        } else {
            sprintf(out + j, "%%%02X", c);
            j += 3;
        }
    }
    out[j] = '\0';
    Value *r = xs_str(out);
    free(out);
    return r;
}

static Value *native_encode_url_decode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_str("");
    const char *in = a[0]->s;
    size_t len = strlen(in);
    char *out = xs_malloc(len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (in[i] == '%' && i+2 < len) {
            int hi = hex_val(in[i+1]);
            int lo = hex_val(in[i+2]);
            if (hi >= 0 && lo >= 0) {
                out[j++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        if (in[i] == '+') out[j++] = ' ';
        else out[j++] = in[i];
    }
    out[j] = '\0';
    Value *r = xs_str_n(out, j);
    free(out);
    return r;
}

Value *make_encode_module(void) {
    XSMap *m = map_new();
    map_set(m, "base64_encode", xs_native(native_encode_base64_encode));
    map_set(m, "base64_decode", xs_native(native_encode_base64_decode));
    map_set(m, "hex_encode",    xs_native(native_encode_hex_encode));
    map_set(m, "hex_decode",    xs_native(native_encode_hex_decode));
    map_set(m, "url_encode",    xs_native(native_encode_url_encode));
    map_set(m, "url_decode",    xs_native(native_encode_url_decode));
    return xs_module(m);
}

/* in-memory kv store */

/* Helper: skip whitespace */
static const char *db_skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Helper: case-insensitive prefix match, returns pointer past match or NULL */
static const char *db_match_kw(const char *s, const char *kw) {
    s = db_skip_ws(s);
    size_t klen = strlen(kw);
    if (strncasecmp(s, kw, klen) == 0 && (s[klen] == '\0' || isspace((unsigned char)s[klen]) || s[klen] == '(')) {
        return s + klen;
    }
    return NULL;
}

/* Helper: read an identifier (table name, etc.) */
static const char *db_read_ident(const char *s, char *buf, int bufsz) {
    s = db_skip_ws(s);
    int i = 0;
    while (*s && !isspace((unsigned char)*s) && *s != '(' && *s != ')' && *s != ',' && *s != ';' && i < bufsz - 1) {
        buf[i++] = *s++;
    }
    buf[i] = '\0';
    return s;
}

/* Internal: execute a SQL-like command on the db, returning a result.
   Supports:
     CREATE TABLE name
     INSERT INTO name VALUES (v1, v2, ...)
     SELECT * FROM name
     DELETE FROM name
     DELETE FROM name WHERE key = value
     DROP TABLE name
*/

static const char *xs_strcasestr_fn(const char *h, const char *n) {
    size_t nlen = strlen(n);
    if (!nlen) return h;
    for (; *h; h++) {
        if (strncasecmp(h, n, nlen) == 0) return h;
    }
    return NULL;
}

static Value *db_execute(Value *db_val, const char *sql, int return_rows) {
    if (!db_val || (db_val->tag != XS_MAP && db_val->tag != XS_MODULE) || !db_val->map)
        return xs_str("error: invalid db handle");
    Value *tables_v = map_get(db_val->map, "_tables");
    if (!tables_v || (tables_v->tag != XS_MAP && tables_v->tag != XS_MODULE) || !tables_v->map)
        return xs_str("error: corrupt db (no _tables)");
    XSMap *tables = tables_v->map;

    const char *p = sql;
    const char *rest;
    char tname[256];

    /* CREATE TABLE name */
    if ((rest = db_match_kw(p, "CREATE")) != NULL) {
        rest = db_match_kw(rest, "TABLE");
        if (!rest) return xs_str("error: expected TABLE after CREATE");
        rest = db_read_ident(rest, tname, sizeof tname);
        (void)rest;
        if (tname[0] == '\0') return xs_str("error: missing table name");
        /* Check if table already exists */
        if (map_get(tables, tname)) return xs_str("error: table already exists");
        Value *tbl = xs_array_new();
        map_set(tables, tname, tbl);
        value_decref(tbl);
        return xs_str("ok");
    }

    /* DROP TABLE name */
    if ((rest = db_match_kw(p, "DROP")) != NULL) {
        rest = db_match_kw(rest, "TABLE");
        if (!rest) return xs_str("error: expected TABLE after DROP");
        rest = db_read_ident(rest, tname, sizeof tname);
        (void)rest;
        if (tname[0] == '\0') return xs_str("error: missing table name");
        if (!map_get(tables, tname)) return xs_str("error: no such table");
        map_set(tables, tname, value_incref(XS_NULL_VAL));
        return xs_str("ok");
    }

    /* INSERT INTO name VALUES (...) */
    if ((rest = db_match_kw(p, "INSERT")) != NULL) {
        rest = db_match_kw(rest, "INTO");
        if (!rest) return xs_str("error: expected INTO after INSERT");
        rest = db_read_ident(rest, tname, sizeof tname);
        if (tname[0] == '\0') return xs_str("error: missing table name");
        Value *tbl = map_get(tables, tname);
        if (!tbl || tbl->tag != XS_ARRAY) return xs_str("error: no such table");

        rest = db_match_kw(rest, "VALUES");
        if (!rest) return xs_str("error: expected VALUES");
        rest = db_skip_ws(rest);
        if (*rest != '(') return xs_str("error: expected ( after VALUES");
        rest++; /* skip '(' */

        /* Parse comma-separated values until ')' */
        XSMap *row = map_new();
        int col = 0;
        while (*rest && *rest != ')') {
            rest = db_skip_ws(rest);
            if (*rest == ')') break;

            /* Read a value — string (quoted) or number or identifier */
            char vbuf[1024];
            int vi = 0;
            if (*rest == '\'' || *rest == '"') {
                char quote = *rest++;
                while (*rest && *rest != quote && vi < (int)sizeof(vbuf) - 1)
                    vbuf[vi++] = *rest++;
                if (*rest == quote) rest++;
                vbuf[vi] = '\0';
                char col_name[32];
                snprintf(col_name, sizeof col_name, "c%d", col);
                Value *sv = xs_str(vbuf);
                map_set(row, col_name, sv);
                value_decref(sv);
            } else {
                while (*rest && *rest != ',' && *rest != ')' && !isspace((unsigned char)*rest) && vi < (int)sizeof(vbuf) - 1)
                    vbuf[vi++] = *rest++;
                vbuf[vi] = '\0';
                char col_name[32];
                snprintf(col_name, sizeof col_name, "c%d", col);
                /* Try parsing as integer */
                char *endp;
                long long ival = strtoll(vbuf, &endp, 10);
                if (*endp == '\0' && vi > 0) {
                    Value *iv = xs_int((int64_t)ival);
                    map_set(row, col_name, iv);
                    value_decref(iv);
                } else {
                    Value *sv = xs_str(vbuf);
                    map_set(row, col_name, sv);
                    value_decref(sv);
                }
            }
            col++;
            rest = db_skip_ws(rest);
            if (*rest == ',') rest++;
        }
        Value *row_v = xs_module(row);
        array_push(tbl->arr, row_v);
        value_decref(row_v);
        return xs_str("ok");
    }

    /* SELECT * FROM name [WHERE key = value] */
    if ((rest = db_match_kw(p, "SELECT")) != NULL) {
        rest = db_skip_ws(rest);
        /* skip column list — we only support * */
        if (*rest == '*') rest++;
        else {
            /* skip until FROM */
            const char *from = xs_strcasestr_fn(rest, "FROM");
            if (!from) return xs_str("error: expected FROM");
            rest = from;
        }
        rest = db_match_kw(rest, "FROM");
        if (!rest) return xs_str("error: expected FROM");
        rest = db_read_ident(rest, tname, sizeof tname);
        if (tname[0] == '\0') return xs_str("error: missing table name");
        Value *tbl = map_get(tables, tname);
        if (!tbl || tbl->tag != XS_ARRAY) return xs_str("error: no such table");

        /* Check for WHERE clause */
        const char *where = db_match_kw(rest, "WHERE");
        char where_key[256] = {0};
        char where_val[1024] = {0};
        if (where) {
            where = db_read_ident(where, where_key, sizeof where_key);
            where = db_skip_ws(where);
            if (*where == '=') where++;
            where = db_skip_ws(where);
            /* Read value */
            int wi = 0;
            if (*where == '\'' || *where == '"') {
                char q = *where++;
                while (*where && *where != q && wi < (int)sizeof(where_val) - 1)
                    where_val[wi++] = *where++;
            } else {
                while (*where && !isspace((unsigned char)*where) && *where != ';' && wi < (int)sizeof(where_val) - 1)
                    where_val[wi++] = *where++;
            }
            where_val[wi] = '\0';
        }

        Value *results = xs_array_new();
        for (int i = 0; i < tbl->arr->len; i++) {
            Value *row = tbl->arr->items[i];
            if (!row || (row->tag != XS_MAP && row->tag != XS_MODULE)) continue;
            /* Apply WHERE filter if present */
            if (where_key[0]) {
                Value *fv = map_get(row->map, where_key);
                if (!fv) continue;
                char *fs = value_str(fv);
                int match = (strcmp(fs, where_val) == 0);
                free(fs);
                if (!match) continue;
            }
            array_push(results->arr, row);
        }
        return results;
    }

    /* DELETE FROM name [WHERE key = value] */
    if ((rest = db_match_kw(p, "DELETE")) != NULL) {
        rest = db_match_kw(rest, "FROM");
        if (!rest) return xs_str("error: expected FROM after DELETE");
        rest = db_read_ident(rest, tname, sizeof tname);
        if (tname[0] == '\0') return xs_str("error: missing table name");
        Value *tbl = map_get(tables, tname);
        if (!tbl || tbl->tag != XS_ARRAY) return xs_str("error: no such table");

        /* Check for WHERE clause */
        const char *where = db_match_kw(rest, "WHERE");
        if (!where) {
            /* Delete all rows */
            tbl->arr->len = 0;
            return xs_str("ok");
        }

        char where_key[256] = {0};
        char where_val[1024] = {0};
        where = db_read_ident(where, where_key, sizeof where_key);
        where = db_skip_ws(where);
        if (*where == '=') where++;
        where = db_skip_ws(where);
        int wi = 0;
        if (*where == '\'' || *where == '"') {
            char q = *where++;
            while (*where && *where != q && wi < (int)sizeof(where_val) - 1)
                where_val[wi++] = *where++;
        } else {
            while (*where && !isspace((unsigned char)*where) && *where != ';' && wi < (int)sizeof(where_val) - 1)
                where_val[wi++] = *where++;
        }
        where_val[wi] = '\0';

        /* Remove matching rows (compact in-place) */
        int dst = 0;
        for (int i = 0; i < tbl->arr->len; i++) {
            Value *row = tbl->arr->items[i];
            int keep = 1;
            if (row && (row->tag == XS_MAP || row->tag == XS_MODULE) && row->map) {
                Value *fv = map_get(row->map, where_key);
                if (fv) {
                    char *fs = value_str(fv);
                    if (strcmp(fs, where_val) == 0) keep = 0;
                    free(fs);
                }
            }
            if (keep) {
                tbl->arr->items[dst++] = tbl->arr->items[i];
            } else {
                value_decref(tbl->arr->items[i]);
            }
        }
        tbl->arr->len = dst;
        return xs_str("ok");
    }

    return xs_str("error: unrecognized SQL command");
}

static Value *native_db_open(Interp *ig, Value **a, int n) {
    (void)ig;
    XSMap *db = map_new();
    const char *name = (n > 0 && a[0]->tag == XS_STR) ? a[0]->s : "memdb";
    map_set(db, "_name", xs_str(name));
    XSMap *tables = map_new();
    Value *tv = xs_module(tables);
    map_set(db, "_tables", tv);
    value_decref(tv);
    return xs_module(db);
}

static Value *native_db_exec(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2) return xs_str("error: db.exec requires (db, sql)");
    if ((a[0]->tag != XS_MAP && a[0]->tag != XS_MODULE) || a[1]->tag != XS_STR)
        return xs_str("error: invalid arguments to db.exec");
    return db_execute(a[0], a[1]->s, 0);
}

static Value *native_db_query(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2) return xs_str("error: db.query requires (db, sql)");
    if ((a[0]->tag != XS_MAP && a[0]->tag != XS_MODULE) || a[1]->tag != XS_STR)
        return xs_str("error: invalid arguments to db.query");
    return db_execute(a[0], a[1]->s, 1);
}

static Value *native_db_close(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || (a[0]->tag != XS_MAP && a[0]->tag != XS_MODULE))
        return xs_str("error: db.close requires a db handle");
    /* Mark db as closed by removing _tables */
    map_set(a[0]->map, "_tables", value_incref(XS_NULL_VAL));
    map_set(a[0]->map, "_closed", value_incref(XS_TRUE_VAL));
    return xs_str("ok");
}

Value *make_db_module(void) {
    XSMap *m = map_new();
    map_set(m, "open",  xs_native(native_db_open));
    map_set(m, "exec",  xs_native(native_db_exec));
    map_set(m, "query", xs_native(native_db_query));
    map_set(m, "close", xs_native(native_db_close));
    return xs_module(m);
}

/* cli arg parsing */

static Value *native_cli_parse(Interp *ig, Value **a, int n) {
    (void)ig;
    /* parse(args_array) -> map of parsed flags/options */
    XSMap *result = map_new();
    Value *positionals = xs_array_new();
    if (n < 1 || a[0]->tag != XS_ARRAY) {
        map_set(result, "args", positionals);
        value_decref(positionals);
        return xs_module(result);
    }
    XSArray *args = a[0]->arr;
    for (int i = 0; i < args->len; i++) {
        Value *arg = args->items[i];
        if (arg->tag != XS_STR) {
            array_push(positionals->arr, arg);
            continue;
        }
        const char *s = arg->s;
        if (s[0] == '-' && s[1] == '-') {
            /* --key=value or --flag */
            const char *eq = strchr(s + 2, '=');
            if (eq) {
                char *key = xs_strndup(s + 2, (size_t)(eq - s - 2));
                Value *val = xs_str(eq + 1);
                map_set(result, key, val);
                value_decref(val);
                free(key);
            } else {
                map_set(result, s + 2, value_incref(XS_TRUE_VAL));
            }
        } else if (s[0] == '-' && s[1] != '\0') {
            /* -f (short flag) */
            char key[2] = { s[1], '\0' };
            if (s[2] == '\0' && i + 1 < args->len && args->items[i+1]->tag == XS_STR
                && args->items[i+1]->s[0] != '-') {
                /* -k value */
                i++;
                Value *val = value_incref(args->items[i]);
                map_set(result, key, val);
                value_decref(val);
            } else {
                map_set(result, key, value_incref(XS_TRUE_VAL));
            }
        } else {
            array_push(positionals->arr, arg);
        }
    }
    map_set(result, "args", positionals);
    value_decref(positionals);
    return xs_module(result);
}

static Value *native_cli_flag(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_NULL_VAL);
    XSMap *spec = map_new();
    map_set(spec, "name", value_incref(a[0]));
    map_set(spec, "type", xs_str("flag"));
    if (n > 1 && a[1]->tag == XS_MAP) {
        /* merge opts */
        int klen; char **keys = map_keys(a[1]->map, &klen);
        for (int i = 0; i < klen; i++) {
            Value *v = map_get(a[1]->map, keys[i]);
            if (v) map_set(spec, keys[i], value_incref(v));
            free(keys[i]);
        }
        free(keys);
    }
    return xs_module(spec);
}

static Value *native_cli_option(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_NULL_VAL);
    XSMap *spec = map_new();
    map_set(spec, "name", value_incref(a[0]));
    map_set(spec, "type", xs_str("option"));
    return xs_module(spec);
}

static Value *native_cli_positional(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_NULL_VAL);
    XSMap *spec = map_new();
    map_set(spec, "name", value_incref(a[0]));
    map_set(spec, "type", xs_str("positional"));
    return xs_module(spec);
}

Value *make_cli_module(void) {
    XSMap *m = map_new();
    map_set(m, "parse",      xs_native(native_cli_parse));
    map_set(m, "flag",       xs_native(native_cli_flag));
    map_set(m, "option",     xs_native(native_cli_option));
    map_set(m, "positional", xs_native(native_cli_positional));
    return xs_module(m);
}

/* ffi (dlopen/dlsym) */
#ifdef XSC_ENABLE_PLUGINS
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

static Value *native_ffi_load(Interp *ig, Value **a, int n) {
    (void)ig;
#ifdef XSC_ENABLE_PLUGINS
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_NULL_VAL);
#ifdef _WIN32
    void *handle = (void *)LoadLibraryA(a[0]->s);
#else
    void *handle = dlopen(a[0]->s, RTLD_LAZY);
#endif
    if (!handle) return value_incref(XS_NULL_VAL);
    XSMap *h = map_new();
    map_set(h, "_handle", xs_int((int64_t)(uintptr_t)handle));
    map_set(h, "path", value_incref(a[0]));
    return xs_module(h);
#else
    (void)a; (void)n;
    return xs_str("error: FFI not available (compile with XSC_ENABLE_PLUGINS=1)");
#endif
}

static Value *native_ffi_sym(Interp *ig, Value **a, int n) {
    (void)ig;
#ifdef XSC_ENABLE_PLUGINS
    if (n < 2 || a[0]->tag != XS_MAP || a[1]->tag != XS_STR)
        return value_incref(XS_NULL_VAL);
    Value *hp = map_get(a[0]->map, "_handle");
    if (!hp || hp->tag != XS_INT) return value_incref(XS_NULL_VAL);
    void *handle = (void*)(uintptr_t)hp->i;
#ifdef _WIN32
    void *sym = (void *)GetProcAddress((HMODULE)handle, a[1]->s);
#else
    void *sym = dlsym(handle, a[1]->s);
#endif
    if (!sym) return value_incref(XS_NULL_VAL);
    XSMap *s = map_new();
    map_set(s, "_sym", xs_int((int64_t)(uintptr_t)sym));
    map_set(s, "name", value_incref(a[1]));
    return xs_module(s);
#else
    (void)a; (void)n;
    return xs_str("error: FFI not available");
#endif
}

static Value *native_ffi_call(Interp *ig, Value **a, int n) {
    (void)ig;
    /* ffi.call(sym_handle, args_array) — call a foreign function.
       If the sym has a _fn field pointing to a native function wrapper, call it.
       Otherwise return an error. */
    if (n < 1 || (a[0]->tag != XS_MAP && a[0]->tag != XS_MODULE))
        return xs_str("error: ffi.call requires a symbol handle");

    /* Check for a native function wrapper in _fn field */
    Value *fn_v = map_get(a[0]->map, "_fn");
    if (fn_v && fn_v->tag == XS_NATIVE) {
        /* Pass args_array items as arguments */
        if (n >= 2 && a[1]->tag == XS_ARRAY) {
            return fn_v->native(ig, a[1]->arr->items, a[1]->arr->len);
        }
        return fn_v->native(ig, NULL, 0);
    }
    if (fn_v && fn_v->tag == XS_FUNC) {
        if (n >= 2 && a[1]->tag == XS_ARRAY) {
            return call_value(ig, fn_v, a[1]->arr->items, a[1]->arr->len, "ffi.call");
        }
        return call_value(ig, fn_v, NULL, 0, "ffi.call");
    }

    return xs_str("error: symbol has no callable _fn wrapper; generic FFI requires libffi");
}

static Value *native_ffi_close(Interp *ig, Value **a, int n) {
    (void)ig;
#ifdef XSC_ENABLE_PLUGINS
    if (n < 1 || (a[0]->tag != XS_MAP && a[0]->tag != XS_MODULE))
        return xs_str("error: ffi.close requires a library handle");
    Value *hp = map_get(a[0]->map, "_handle");
    if (!hp || hp->tag != XS_INT)
        return xs_str("error: invalid library handle");
    void *handle = (void *)(uintptr_t)hp->i;
#ifdef _WIN32
    FreeLibrary((HMODULE)handle);
#else
    dlclose(handle);
#endif
    /* Invalidate the handle */
    map_set(a[0]->map, "_handle", xs_int(0));
    map_set(a[0]->map, "_closed", value_incref(XS_TRUE_VAL));
    return xs_str("ok");
#else
    (void)a; (void)n;
    return xs_str("error: FFI not available (compile with XSC_ENABLE_PLUGINS=1)");
#endif
}

static Value *native_ffi_typeof(Interp *ig, Value **a, int n) {
    (void)ig;
    /* ffi.typeof(sym_handle) — return the type string of a symbol */
    if (n < 1) return xs_str("null");
    if (a[0]->tag == XS_MAP || a[0]->tag == XS_MODULE) {
        /* Check if it has a _sym field (it's a symbol handle) */
        Value *sym = map_get(a[0]->map, "_sym");
        if (sym) return xs_str("ffi_symbol");
        /* Check if it has a _handle field (it's a library handle) */
        Value *h = map_get(a[0]->map, "_handle");
        if (h) return xs_str("ffi_library");
        return xs_str("map");
    }
    /* For non-map values, return the XS type */
    switch (a[0]->tag) {
        case XS_NULL:   return xs_str("null");
        case XS_BOOL:   return xs_str("bool");
        case XS_INT:    return xs_str("int");
        case XS_FLOAT:  return xs_str("float");
        case XS_STR:    return xs_str("str");
        case XS_FUNC:   return xs_str("fn");
        case XS_NATIVE: return xs_str("native_fn");
        default:        return xs_str("unknown");
    }
}

Value *make_ffi_module(void) {
    XSMap *m = map_new();
    map_set(m, "load",   xs_native(native_ffi_load));
    map_set(m, "sym",    xs_native(native_ffi_sym));
    map_set(m, "call",   xs_native(native_ffi_call));
    map_set(m, "close",  xs_native(native_ffi_close));
    map_set(m, "typeof", xs_native(native_ffi_typeof));
    return xs_module(m);
}

/* reflect */

static Value *native_reflect_type_of(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1) return xs_str("null");
    switch (a[0]->tag) {
        case XS_NULL:       return xs_str("null");
        case XS_BOOL:       return xs_str("bool");
        case XS_INT:        return xs_str("int");
        case XS_FLOAT:      return xs_str("float");
        case XS_STR:        return xs_str("str");
        case XS_CHAR:       return xs_str("char");
        case XS_ARRAY:      return xs_str("array");
        case XS_MAP:        return xs_str("map");
        case XS_TUPLE:      return xs_str("tuple");
        case XS_FUNC:       return xs_str("fn");
        case XS_NATIVE:     return xs_str("native_fn");
        case XS_STRUCT_VAL: return xs_str("struct");
        case XS_ENUM_VAL:   return xs_str("enum");
        case XS_CLASS_VAL:  return xs_str("class");
        case XS_INST:       return xs_str("instance");
        case XS_RANGE:      return xs_str("range");
        case XS_MODULE:     return xs_str("module");
#ifdef XSC_ENABLE_VM
        case XS_CLOSURE:    return xs_str("closure");
#endif
        default:            return xs_str("unknown");
    }
}

static Value *native_reflect_fields(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1) return xs_array_new();
    XSMap *m = NULL;
    if (a[0]->tag == XS_MAP || a[0]->tag == XS_MODULE) m = a[0]->map;
    else if (a[0]->tag == XS_STRUCT_VAL) m = a[0]->st->fields;
    else if (a[0]->tag == XS_INST) m = a[0]->inst->fields;
    if (!m) return xs_array_new();

    int klen;
    char **keys = map_keys(m, &klen);
    Value *arr = xs_array_new();
    for (int i = 0; i < klen; i++) {
        XSMap *pair = map_new();
        map_set(pair, "name", xs_str(keys[i]));
        Value *v = map_get(m, keys[i]);
        if (v) map_set(pair, "value", value_incref(v));
        else   map_set(pair, "value", value_incref(XS_NULL_VAL));
        Value *pm = xs_module(pair);
        array_push(arr->arr, pm);
        value_decref(pm);
        free(keys[i]);
    }
    free(keys);
    return arr;
}

static Value *native_reflect_methods(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1) return xs_array_new();
    XSMap *m = NULL;
    if (a[0]->tag == XS_INST && a[0]->inst->methods)
        m = a[0]->inst->methods;
    else if (a[0]->tag == XS_CLASS_VAL && a[0]->cls->methods)
        m = a[0]->cls->methods;
    if (!m) return xs_array_new();

    int klen;
    char **keys = map_keys(m, &klen);
    Value *arr = xs_array_new();
    for (int i = 0; i < klen; i++) {
        Value *s = xs_str(keys[i]);
        array_push(arr->arr, s);
        value_decref(s);
        free(keys[i]);
    }
    free(keys);
    return arr;
}

static Value *native_reflect_is_instance(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2) return value_incref(XS_FALSE_VAL);
    if (a[1]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
    const char *type_name = a[1]->s;
    const char *actual = NULL;
    switch (a[0]->tag) {
        case XS_NULL:   actual = "null"; break;
        case XS_BOOL:   actual = "bool"; break;
        case XS_INT:    actual = "int"; break;
        case XS_FLOAT:  actual = "float"; break;
        case XS_STR:    actual = "str"; break;
        case XS_ARRAY:  actual = "array"; break;
        case XS_MAP:    actual = "map"; break;
        case XS_FUNC:   actual = "fn"; break;
        case XS_NATIVE: actual = "native_fn"; break;
        case XS_INST:   actual = a[0]->inst->class_ ? a[0]->inst->class_->name : "instance"; break;
        case XS_STRUCT_VAL: actual = a[0]->st->type_name; break;
        case XS_ENUM_VAL:   actual = a[0]->en->type_name; break;
        default: actual = "unknown"; break;
    }
    return (actual && strcmp(actual, type_name) == 0)
        ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

Value *make_reflect_module(void) {
    XSMap *m = map_new();
    map_set(m, "type_of",     xs_native(native_reflect_type_of));
    map_set(m, "fields",      xs_native(native_reflect_fields));
    map_set(m, "methods",     xs_native(native_reflect_methods));
    map_set(m, "is_instance", xs_native(native_reflect_is_instance));
    return xs_module(m);
}

/* gc */

static Value *native_gc_collect(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    /* XS uses refcounting, not tracing GC. This is a no-op. */
    return value_incref(XS_NULL_VAL);
}

static Value *native_gc_disable(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    return value_incref(XS_NULL_VAL);
}

static Value *native_gc_enable(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    return value_incref(XS_NULL_VAL);
}

static Value *native_gc_stats(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    XSMap *s = map_new();
    map_set(s, "heap_used",   xs_int(0));
    map_set(s, "collections", xs_int(0));
    map_set(s, "strategy",    xs_str("refcount"));
    return xs_module(s);
}

Value *make_gc_module(void) {
    XSMap *m = map_new();
    map_set(m, "collect", xs_native(native_gc_collect));
    map_set(m, "disable", xs_native(native_gc_disable));
    map_set(m, "enable",  xs_native(native_gc_enable));
    map_set(m, "stats",   xs_native(native_gc_stats));
    return xs_module(m);
}

/* reactive signals */

static Value *native_reactive_signal(Interp *ig, Value **a, int n) {
    return builtin_signal(ig, a, n);
}

static Value *native_reactive_derived(Interp *ig, Value **a, int n) {
    return builtin_derived(ig, a, n);
}

static Value *native_reactive_effect(Interp *ig, Value **a, int n) {
    /* effect(fn, ...signals) -> calls fn immediately, then subscribes fn
       to each signal argument so it re-runs when signal values change */
    if (n < 1 || (a[0]->tag != XS_FUNC && a[0]->tag != XS_NATIVE))
        return value_incref(XS_NULL_VAL);
    Value *fn = a[0];
    /* Call the effect function immediately */
    Value *result = call_value(ig, fn, NULL, 0, "effect");
    value_decref(result);
    /* Subscribe fn to any signal arguments passed after the function */
    for (int j = 1; j < n; j++) {
        if (a[j]->tag == XS_SIGNAL && a[j]->signal) {
            XSSignal *sig = a[j]->signal;
            if (sig->nsubs >= sig->subcap) {
                sig->subcap = sig->subcap ? sig->subcap * 2 : 4;
                sig->subscribers = xs_realloc(sig->subscribers, sig->subcap * sizeof(Value*));
            }
            sig->subscribers[sig->nsubs++] = value_incref(fn);
        }
    }
    return value_incref(XS_NULL_VAL);
}

Value *make_reactive_module(void) {
    XSMap *m = map_new();
    map_set(m, "signal",  xs_native(native_reactive_signal));
    map_set(m, "derived", xs_native(native_reactive_derived));
    map_set(m, "effect",  xs_native(native_reactive_effect));
    return xs_module(m);
}

/* ── fs module ───────────────────────────────────────────── */

static Value *native_fs_read(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_NULL_VAL);
    FILE *f = fopen(a[0]->s, "r");
    if (!f) return value_incref(XS_NULL_VAL);
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = xs_malloc(sz + 1);
    long nr = (long)fread(buf, 1, sz, f); fclose(f); buf[nr] = '\0';
    Value *v = xs_str(buf); free(buf); return v;
}

static Value *native_fs_write(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || a[0]->tag != XS_STR || a[1]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
    FILE *f = fopen(a[0]->s, "w");
    if (!f) return value_incref(XS_FALSE_VAL);
    fputs(a[1]->s, f); fclose(f);
    return value_incref(XS_TRUE_VAL);
}

static Value *native_fs_append(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || a[0]->tag != XS_STR || a[1]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
    FILE *f = fopen(a[0]->s, "a");
    if (!f) return value_incref(XS_FALSE_VAL);
    fputs(a[1]->s, f); fclose(f);
    return value_incref(XS_TRUE_VAL);
}

static Value *native_fs_exists(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
    return (access(a[0]->s, F_OK) == 0) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_remove(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
    return (unlink(a[0]->s) == 0) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_mkdir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
    int r = io_mkdirs(a[0]->s);
    return (r == 0 || errno == EEXIST) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_ls(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_array_new();
    DIR *d = opendir(a[0]->s); if (!d) return xs_array_new();
    Value *arr = xs_array_new();
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        array_push(arr->arr, xs_str(ent->d_name));
    }
    closedir(d); return arr;
}

static Value *native_fs_is_dir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
    struct stat st; if (stat(a[0]->s, &st) != 0) return value_incref(XS_FALSE_VAL);
    return S_ISDIR(st.st_mode) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_is_file(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
    struct stat st; if (stat(a[0]->s, &st) != 0) return value_incref(XS_FALSE_VAL);
    return S_ISREG(st.st_mode) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_size(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || a[0]->tag != XS_STR) return xs_int(-1);
    struct stat st; if (stat(a[0]->s, &st) != 0) return xs_int(-1);
    return xs_int((int64_t)st.st_size);
}

Value *make_fs_module(void) {
    XSMap *m = map_new();
    map_set(m, "read",    xs_native(native_fs_read));
    map_set(m, "write",   xs_native(native_fs_write));
    map_set(m, "append",  xs_native(native_fs_append));
    map_set(m, "exists",  xs_native(native_fs_exists));
    map_set(m, "remove",  xs_native(native_fs_remove));
    map_set(m, "mkdir",   xs_native(native_fs_mkdir));
    map_set(m, "ls",      xs_native(native_fs_ls));
    map_set(m, "is_dir",  xs_native(native_fs_is_dir));
    map_set(m, "is_file", xs_native(native_fs_is_file));
    map_set(m, "size",    xs_native(native_fs_size));
    return xs_module(m);
}
