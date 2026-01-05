/*
 * xs_embed.h -- embed the XS interpreter in a C/C++ application.
 * Link with: libxs (or the xs object files) + -lm
 */
#ifndef XS_EMBED_H
#define XS_EMBED_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XSState   XSState;
typedef struct XSValue   XSValue;
typedef struct { void *opaque; } XSRef;
typedef XSValue* (*xs_cfunc)(XSState *xs, XSValue **args, int argc);

XSState* xs_new(void);
void     xs_free(XSState *xs);

int xs_eval(XSState *xs, const char *src);
int xs_eval_file(XSState *xs, const char *path);
int xs_call(XSState *xs, const char *fn, int argc);

void xs_push_int(XSState *xs, int64_t v);
void xs_push_float(XSState *xs, double v);
void xs_push_str(XSState *xs, const char *v);
void xs_push_bool(XSState *xs, int v);
void xs_push_null(XSState *xs);

int64_t     xs_pop_int(XSState *xs);
double      xs_pop_float(XSState *xs);
char*       xs_pop_str(XSState *xs);    /* caller frees */
int         xs_pop_bool(XSState *xs);

XSRef       xs_pin(XSState *xs, int stack_index);
void        xs_unpin(XSState *xs, XSRef ref);
const char* xs_ref_str(XSRef ref);

void xs_register(XSState *xs, const char *name, xs_cfunc fn);

int         xs_error(XSState *xs);
const char* xs_error_msg(XSState *xs);

XSValue* xs_make_int(XSState *xs, int64_t v);
XSValue* xs_make_float(XSState *xs, double v);
XSValue* xs_make_str(XSState *xs, const char *s);
XSValue* xs_make_bool(XSState *xs, int v);
XSValue* xs_make_null(XSState *xs);
XSValue* xs_make_array(XSState *xs, XSValue **elems, int n);
XSValue* xs_make_map(XSState *xs);
XSValue* xs_map_set(XSState *xs, XSValue *map, const char *key, XSValue *val);

int         xs_is_int(XSValue *v);
int64_t     xs_get_int(XSValue *v);
const char* xs_get_str(XSValue *v);

#ifdef __cplusplus
}
#endif

#endif
