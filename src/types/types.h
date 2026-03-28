/* types.h: XS type IR */
#ifndef TYPES_H
#define TYPES_H

#include "core/xs.h"

typedef enum {
    TY_UNKNOWN = 0,
    TY_BOOL, TY_CHAR,
    TY_I8,  TY_I16,  TY_I32,  TY_I64,
    TY_U8,  TY_U16,  TY_U32,  TY_U64,
    TY_F32, TY_F64,
    TY_STR, TY_UNIT, TY_NEVER,
    TY_ARRAY,
    TY_TUPLE,
    TY_OPTION,
    TY_RESULT,
    TY_FN,
    TY_NAMED,
    TY_GENERIC,
    TY_DYN,
} TyKind;

typedef struct XsType XsType;
struct XsType {
    TyKind kind;
    int    is_singleton;
    union {
        struct { XsType *inner; }                             array;
        struct { XsType **elems; int nelems; }                tuple;
        struct { XsType *inner; }                             option;
        struct { XsType *ok; XsType *err; }                   result;
        struct { XsType **params; int nparams; XsType *ret; } fn_;
        struct { char *name; XsType **args; int nargs; }      named;
        struct { char *name; }                                generic;
    };
};

XsType *ty_unknown(void);
XsType *ty_bool(void);   XsType *ty_char(void);
XsType *ty_i8(void);     XsType *ty_i16(void);
XsType *ty_i32(void);    XsType *ty_i64(void);
XsType *ty_u8(void);     XsType *ty_u16(void);
XsType *ty_u32(void);    XsType *ty_u64(void);
XsType *ty_f32(void);    XsType *ty_f64(void);
XsType *ty_str(void);    XsType *ty_unit(void);
XsType *ty_never(void);  XsType *ty_dyn(void);

XsType *ty_array(XsType *elem);
XsType *ty_tuple(XsType **elems, int nelems);
XsType *ty_option(XsType *inner);
XsType *ty_result(XsType *ok, XsType *err);
XsType *ty_fn(XsType **params, int nparams, XsType *ret);
XsType *ty_named(const char *name, XsType **args, int nargs);
XsType *ty_generic(const char *name);

int     ty_equal(const XsType *a, const XsType *b);
char   *ty_to_str(const XsType *t);
void    ty_free(XsType *t);
XsType *ty_from_name(const char *name);

#endif /* TYPES_H */
