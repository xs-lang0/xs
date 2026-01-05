/* xs_bigint.h — arbitrary-precision integers (little-endian limb array) */
#ifndef XS_BIGINT_H
#define XS_BIGINT_H

#include "core/xs.h"

struct XSBigInt {
    uint32_t *limbs;
    int       len, cap;
    int       sign;     /* 0 = non-negative, 1 = negative */
    int       refcount;
};

XSBigInt *bigint_from_i64(int64_t val);
XSBigInt *bigint_from_u64(uint64_t val);
XSBigInt *bigint_from_str(const char *s, int base);
XSBigInt *bigint_copy(const XSBigInt *b);
void      bigint_free(XSBigInt *b);

int       bigint_fits_i64(const XSBigInt *b);
int64_t   bigint_to_i64(const XSBigInt *b);
double    bigint_to_double(const XSBigInt *b);
char     *bigint_to_str(const XSBigInt *b, int base);

int       bigint_is_zero(const XSBigInt *b);
int       bigint_cmp(const XSBigInt *a, const XSBigInt *b);  /* -1, 0, 1 */
int       bigint_cmp_i64(const XSBigInt *a, int64_t b);

XSBigInt *bigint_add(const XSBigInt *a, const XSBigInt *b);
XSBigInt *bigint_sub(const XSBigInt *a, const XSBigInt *b);
XSBigInt *bigint_mul(const XSBigInt *a, const XSBigInt *b);
XSBigInt *bigint_div(const XSBigInt *a, const XSBigInt *b);
XSBigInt *bigint_mod(const XSBigInt *a, const XSBigInt *b);
XSBigInt *bigint_neg(const XSBigInt *b);
XSBigInt *bigint_abs(const XSBigInt *b);
XSBigInt *bigint_pow(const XSBigInt *base, int64_t exp);

XSBigInt *bigint_and(const XSBigInt *a, const XSBigInt *b);
XSBigInt *bigint_or(const XSBigInt *a, const XSBigInt *b);
XSBigInt *bigint_xor(const XSBigInt *a, const XSBigInt *b);
XSBigInt *bigint_shl(const XSBigInt *a, int64_t shift);
XSBigInt *bigint_shr(const XSBigInt *a, int64_t shift);

/* safe arithmetic: i64 fast path, auto-promotes to bigint */
Value *xs_safe_add(int64_t a, int64_t b);
Value *xs_safe_sub(int64_t a, int64_t b);
Value *xs_safe_mul(int64_t a, int64_t b);
Value *xs_safe_neg(int64_t a);
Value *xs_safe_pow(int64_t base, int64_t exp);

/* mixed bigint+int operations */
Value *xs_numeric_add(Value *a, Value *b);
Value *xs_numeric_sub(Value *a, Value *b);
Value *xs_numeric_mul(Value *a, Value *b);
Value *xs_numeric_div(Value *a, Value *b);
Value *xs_numeric_mod(Value *a, Value *b);
Value *xs_numeric_pow(Value *a, Value *b);
Value *xs_numeric_neg(Value *a);
Value *xs_numeric_floordiv(Value *a, Value *b);

Value *xs_bigint_val(XSBigInt *b);   /* takes ownership */

#endif /* XS_BIGINT_H */
