#ifndef VALUE_H
#define VALUE_H

#include "core/xs.h"

Value *xs_null(void);
Value *xs_bool(int b);
Value *xs_int(int64_t i);
Value *xs_float(double f);
Value *xs_str(const char *s);
Value *xs_str_n(const char *s, size_t n);
Value *xs_char(char c);
Value *xs_array_new(void);
Value *xs_tuple_new(void);
Value *xs_map_new(void);
Value *xs_func_new(XSFunc *fn);
Value *xs_native(NativeFn fn);
Value *xs_range(int64_t start, int64_t end, int inclusive);
Value *xs_range_step(int64_t start, int64_t end, int inclusive, int64_t step);
Value *xs_module(XSMap *m);
Value *xs_bigint_val(XSBigInt *b);

extern Value *XS_NULL_VAL;
extern Value *XS_TRUE_VAL;
extern Value *XS_FALSE_VAL;

Value *value_incref(Value *v);
void   value_decref(Value *v);

int     value_truthy(Value *v);
char   *value_repr(Value *v);
char   *value_str(Value *v);
int     value_equal(Value *a, Value *b);
int     value_cmp(Value *a, Value *b);
Value  *value_copy(Value *v);

XSArray *array_new(void);
void     array_push(XSArray *a, Value *v);
Value   *array_get(XSArray *a, int i);
void     array_free(XSArray *a);

XSMap  *map_new(void);
void    map_set(XSMap *m, const char *k, Value *v);
Value  *map_get(XSMap *m, const char *k);
int     map_has(XSMap *m, const char *k);
void    map_del(XSMap *m, const char *k);
char  **map_keys(XSMap *m, int *len_out);
void    map_free(XSMap *m);

XSFunc *func_new(const char *name, Node **params, int nparams, Node *body, Env *closure);
XSFunc *func_new_ex(const char *name, Node **params, int nparams, Node *body, Env *closure,
                     Node **default_vals, int *variadic_flags);
void    func_free(XSFunc *fn);

void value_init_singletons(void);

#endif /* VALUE_H */
