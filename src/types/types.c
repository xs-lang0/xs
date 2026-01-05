#include "types/types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define PRIM(name, kind) \
    static XsType _ty_##name = {kind, 1, {{0}}}; \
    XsType *ty_##name(void) { return &_ty_##name; }

PRIM(unknown, TY_UNKNOWN)
PRIM(bool,    TY_BOOL)
PRIM(char_,   TY_CHAR)
PRIM(i8,      TY_I8)
PRIM(i16,     TY_I16)
PRIM(i32,     TY_I32)
PRIM(i64,     TY_I64)
PRIM(u8,      TY_U8)
PRIM(u16,     TY_U16)
PRIM(u32,     TY_U32)
PRIM(u64,     TY_U64)
PRIM(f32,     TY_F32)
PRIM(f64,     TY_F64)
PRIM(str,     TY_STR)
PRIM(unit,    TY_UNIT)
PRIM(never,   TY_NEVER)
PRIM(dyn,     TY_DYN)
#undef PRIM

XsType *ty_char(void) { return ty_char_(); }

XsType *ty_array(XsType *elem) {
    XsType *t = xs_malloc(sizeof *t); memset(t, 0, sizeof *t);
    t->kind = TY_ARRAY; t->array.inner = elem; return t;
}
XsType *ty_option(XsType *inner) {
    XsType *t = xs_malloc(sizeof *t); memset(t, 0, sizeof *t);
    t->kind = TY_OPTION; t->option.inner = inner; return t;
}
XsType *ty_result(XsType *ok, XsType *err) {
    XsType *t = xs_malloc(sizeof *t); memset(t, 0, sizeof *t);
    t->kind = TY_RESULT; t->result.ok = ok; t->result.err = err; return t;
}
XsType *ty_tuple(XsType **elems, int n) {
    XsType *t = xs_malloc(sizeof *t); memset(t, 0, sizeof *t);
    t->kind = TY_TUPLE; t->tuple.nelems = n;
    t->tuple.elems = xs_malloc(sizeof(XsType*) * (n ? n : 1));
    if (n > 0) memcpy(t->tuple.elems, elems, sizeof(XsType*) * n);
    return t;
}
XsType *ty_fn(XsType **params, int n, XsType *ret) {
    XsType *t = xs_malloc(sizeof *t); memset(t, 0, sizeof *t);
    t->kind = TY_FN; t->fn_.nparams = n; t->fn_.ret = ret;
    t->fn_.params = xs_malloc(sizeof(XsType*) * (n ? n : 1));
    if (n > 0) memcpy(t->fn_.params, params, sizeof(XsType*) * n);
    return t;
}
XsType *ty_named(const char *name, XsType **args, int n) {
    XsType *t = xs_malloc(sizeof *t); memset(t, 0, sizeof *t);
    t->kind = TY_NAMED; t->named.name = xs_strdup(name);
    t->named.nargs = n;
    t->named.args = xs_malloc(sizeof(XsType*) * (n ? n : 1));
    if (n > 0) memcpy(t->named.args, args, sizeof(XsType*) * n);
    return t;
}
XsType *ty_generic(const char *name) {
    XsType *t = xs_malloc(sizeof *t); memset(t, 0, sizeof *t);
    t->kind = TY_GENERIC; t->generic.name = xs_strdup(name); return t;
}

void ty_free(XsType *t) {
    if (!t || t->is_singleton) return;
    switch (t->kind) {
        case TY_ARRAY:  ty_free(t->array.inner); break;
        case TY_OPTION: ty_free(t->option.inner); break;
        case TY_RESULT: ty_free(t->result.ok); ty_free(t->result.err); break;
        case TY_TUPLE:
            for (int i = 0; i < t->tuple.nelems; i++) ty_free(t->tuple.elems[i]);
            free(t->tuple.elems); break;
        case TY_FN:
            for (int i = 0; i < t->fn_.nparams; i++) ty_free(t->fn_.params[i]);
            free(t->fn_.params); ty_free(t->fn_.ret); break;
        case TY_NAMED:
            free(t->named.name);
            for (int i = 0; i < t->named.nargs; i++) ty_free(t->named.args[i]);
            free(t->named.args); break;
        case TY_GENERIC: free(t->generic.name); break;
        default: break;
    }
    free(t);
}

static int ty_kind_canonical(const XsType *t) {
    if (t->kind == TY_NAMED && t->named.name) {
        const char *n = t->named.name;
        if (strcmp(n, "int") == 0 || strcmp(n, "i64") == 0 || strcmp(n, "i32") == 0) return TY_I64;
        if (strcmp(n, "float") == 0 || strcmp(n, "f64") == 0 || strcmp(n, "f32") == 0) return TY_F64;
        if (strcmp(n, "str") == 0 || strcmp(n, "string") == 0) return TY_STR;
        if (strcmp(n, "bool") == 0) return TY_BOOL;
        if (strcmp(n, "array") == 0) return TY_ARRAY;
        if (strcmp(n, "any") == 0 || strcmp(n, "dyn") == 0) return TY_DYN;
        if (strcmp(n, "map") == 0) return TY_NAMED;  /* maps stay as named */
    }
    return t->kind;
}

int ty_equal(const XsType *a, const XsType *b) {
    if (!a || !b) return a == b;
    int ka = ty_kind_canonical(a);
    int kb = ty_kind_canonical(b);
    if (ka == TY_DYN || kb == TY_DYN) return 1;
    if (ka != kb) return 0;
    if (ka != a->kind || kb != b->kind) return 1;
    switch (a->kind) {
        case TY_ARRAY:  return ty_equal(a->array.inner, b->array.inner);
        case TY_OPTION: return ty_equal(a->option.inner, b->option.inner);
        case TY_RESULT: return ty_equal(a->result.ok, b->result.ok) &&
                               ty_equal(a->result.err, b->result.err);
        case TY_TUPLE:
            if (a->tuple.nelems != b->tuple.nelems) return 0;
            for (int i = 0; i < a->tuple.nelems; i++)
                if (!ty_equal(a->tuple.elems[i], b->tuple.elems[i])) return 0;
            return 1;
        case TY_FN:
            if (a->fn_.nparams != b->fn_.nparams) return 0;
            if (!ty_equal(a->fn_.ret, b->fn_.ret)) return 0;
            for (int i = 0; i < a->fn_.nparams; i++)
                if (!ty_equal(a->fn_.params[i], b->fn_.params[i])) return 0;
            return 1;
        case TY_NAMED: {
            int name_match = (strcmp(a->named.name, b->named.name) == 0);
            if (!name_match) {
                const char *an = a->named.name, *bn = b->named.name;
                if ((strcmp(an,"map")==0 && strcmp(bn,"Map")==0) ||
                    (strcmp(an,"Map")==0 && strcmp(bn,"map")==0) ||
                    (strcmp(an,"array")==0 && strcmp(bn,"Array")==0) ||
                    (strcmp(an,"Array")==0 && strcmp(bn,"array")==0))
                    name_match = 1;
            }
            if (!name_match) return 0;
            if (a->named.nargs == 0 || b->named.nargs == 0) return 1;
            if (a->named.nargs != b->named.nargs) return 0;
            for (int i = 0; i < a->named.nargs; i++)
                if (!ty_equal(a->named.args[i], b->named.args[i])) return 0;
            return 1;
        }
        case TY_GENERIC:
            return strcmp(a->generic.name, b->generic.name) == 0;
        default: return 1;
    }
}

static const char *prim_name(TyKind k) {
    switch (k) {
        case TY_BOOL: return "bool"; case TY_CHAR: return "char";
        case TY_I8:   return "i8";   case TY_I16:  return "i16";
        case TY_I32:  return "i32";  case TY_I64:  return "i64";
        case TY_U8:   return "u8";   case TY_U16:  return "u16";
        case TY_U32:  return "u32";  case TY_U64:  return "u64";
        case TY_F32:  return "f32";  case TY_F64:  return "f64";
        case TY_STR:  return "str";  case TY_UNIT: return "unit";
        case TY_NEVER:return "never";case TY_DYN:  return "dyn";
        default:      return "?";
    }
}

char *ty_to_str(const XsType *t) {
    if (!t) return xs_strdup("?");
    char buf[512];
    switch (t->kind) {
        case TY_ARRAY: {
            char *inner = ty_to_str(t->array.inner);
            snprintf(buf, sizeof buf, "[%s]", inner); free(inner); return xs_strdup(buf);
        }
        case TY_OPTION: {
            char *inner = ty_to_str(t->option.inner);
            snprintf(buf, sizeof buf, "%s?", inner); free(inner); return xs_strdup(buf);
        }
        case TY_RESULT: {
            char *ok = ty_to_str(t->result.ok);
            char *err = ty_to_str(t->result.err);
            snprintf(buf, sizeof buf, "Result<%s, %s>", ok, err);
            free(ok); free(err); return xs_strdup(buf);
        }
        case TY_TUPLE: {
            buf[0] = '('; buf[1] = '\0';
            for (int i = 0; i < t->tuple.nelems; i++) {
                char *s = ty_to_str(t->tuple.elems[i]);
                strncat(buf, s, sizeof(buf)-strlen(buf)-1);
                free(s);
                if (i < t->tuple.nelems-1) strncat(buf, ", ", sizeof(buf)-strlen(buf)-1);
            }
            strncat(buf, ")", sizeof(buf)-strlen(buf)-1);
            return xs_strdup(buf);
        }
        case TY_FN: {
            buf[0] = '\0'; strncat(buf, "fn(", sizeof(buf)-1);
            for (int i = 0; i < t->fn_.nparams; i++) {
                char *s = ty_to_str(t->fn_.params[i]);
                strncat(buf, s, sizeof(buf)-strlen(buf)-1); free(s);
                if (i < t->fn_.nparams-1) strncat(buf, ", ", sizeof(buf)-strlen(buf)-1);
            }
            strncat(buf, ") -> ", sizeof(buf)-strlen(buf)-1);
            char *r = ty_to_str(t->fn_.ret);
            strncat(buf, r, sizeof(buf)-strlen(buf)-1); free(r);
            return xs_strdup(buf);
        }
        case TY_NAMED:
            if (t->named.nargs == 0) return xs_strdup(t->named.name);
            snprintf(buf, sizeof buf, "%s<", t->named.name);
            for (int i = 0; i < t->named.nargs; i++) {
                char *s = ty_to_str(t->named.args[i]);
                strncat(buf, s, sizeof(buf)-strlen(buf)-1); free(s);
                if (i < t->named.nargs-1) strncat(buf, ", ", sizeof(buf)-strlen(buf)-1);
            }
            strncat(buf, ">", sizeof(buf)-strlen(buf)-1);
            return xs_strdup(buf);
        case TY_GENERIC: return xs_strdup(t->generic.name);
        default: return xs_strdup(prim_name(t->kind));
    }
}

XsType *ty_from_name(const char *name) {
    if (!name) return NULL;
    if (strcmp(name,"bool")==0)  return ty_bool();
    if (strcmp(name,"char")==0)  return ty_char();
    if (strcmp(name,"i8")==0)    return ty_i8();
    if (strcmp(name,"i16")==0)   return ty_i16();
    if (strcmp(name,"i32")==0)   return ty_i32();
    if (strcmp(name,"i64")==0)   return ty_i64();
    if (strcmp(name,"u8")==0)    return ty_u8();
    if (strcmp(name,"u16")==0)   return ty_u16();
    if (strcmp(name,"u32")==0)   return ty_u32();
    if (strcmp(name,"u64")==0)   return ty_u64();
    if (strcmp(name,"f32")==0)   return ty_f32();
    if (strcmp(name,"f64")==0)   return ty_f64();
    if (strcmp(name,"str")==0)   return ty_str();
    if (strcmp(name,"unit")==0)  return ty_unit();
    if (strcmp(name,"never")==0) return ty_never();
    if (strcmp(name,"dyn")==0)   return ty_dyn();
    return NULL;
}
