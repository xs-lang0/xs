#include "core/xs_compat.h"
#include "core/xs_bigint.h"
#include "core/value.h"
#include <ctype.h>
#include <limits.h>


#define LIMB_BITS 32
#define LIMB_BASE ((uint64_t)1 << LIMB_BITS)

static XSBigInt *bigint_alloc(int cap) {
    XSBigInt *b = (XSBigInt *)xs_malloc(sizeof(XSBigInt));
    if (cap < 1) cap = 1;
    b->limbs = (uint32_t *)xs_calloc(cap, sizeof(uint32_t));
    b->len = 0;
    b->cap = cap;
    b->sign = 0;
    b->refcount = 1;
    return b;
}

static void bigint_ensure(XSBigInt *b, int needed) {
    if (needed > b->cap) {
        int newcap = b->cap;
        while (newcap < needed) newcap *= 2;
        b->limbs = (uint32_t *)xs_realloc(b->limbs, newcap * sizeof(uint32_t));
        memset(b->limbs + b->cap, 0, (newcap - b->cap) * sizeof(uint32_t));
        b->cap = newcap;
    }
}

/* Strip leading zero limbs, ensure zero has sign=0 */
static void bigint_normalize(XSBigInt *b) {
    while (b->len > 0 && b->limbs[b->len - 1] == 0)
        b->len--;
    if (b->len == 0)
        b->sign = 0;
}

/* Compare magnitudes (absolute values). Returns -1, 0, or 1. */
static int bigint_cmp_mag(const XSBigInt *a, const XSBigInt *b) {
    if (a->len != b->len)
        return (a->len > b->len) ? 1 : -1;
    for (int i = a->len - 1; i >= 0; i--) {
        if (a->limbs[i] != b->limbs[i])
            return (a->limbs[i] > b->limbs[i]) ? 1 : -1;
    }
    return 0;
}

/* Add magnitudes: r = |a| + |b|. r must have enough capacity. */
static void mag_add(XSBigInt *r, const XSBigInt *a, const XSBigInt *b) {
    int maxlen = (a->len > b->len) ? a->len : b->len;
    bigint_ensure(r, maxlen + 1);
    uint64_t carry = 0;
    for (int i = 0; i < maxlen || carry; i++) {
        uint64_t sum = carry;
        if (i < a->len) sum += a->limbs[i];
        if (i < b->len) sum += b->limbs[i];
        r->limbs[i] = (uint32_t)(sum & 0xFFFFFFFF);
        carry = sum >> LIMB_BITS;
        if (i >= r->len) r->len = i + 1;
    }
}

/* Subtract magnitudes: r = |a| - |b|, assuming |a| >= |b|. */
static void mag_sub(XSBigInt *r, const XSBigInt *a, const XSBigInt *b) {
    bigint_ensure(r, a->len);
    int64_t borrow = 0;
    for (int i = 0; i < a->len; i++) {
        int64_t diff = (int64_t)a->limbs[i] - borrow;
        if (i < b->len) diff -= b->limbs[i];
        if (diff < 0) {
            diff += (int64_t)LIMB_BASE;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r->limbs[i] = (uint32_t)diff;
    }
    r->len = a->len;
    bigint_normalize(r);
}

/* Divide magnitude by a single uint32_t limb, return remainder */
static uint32_t mag_div_limb(XSBigInt *q, const XSBigInt *a, uint32_t d) {
    bigint_ensure(q, a->len);
    q->len = a->len;
    uint64_t rem = 0;
    for (int i = a->len - 1; i >= 0; i--) {
        uint64_t cur = (rem << LIMB_BITS) | a->limbs[i];
        q->limbs[i] = (uint32_t)(cur / d);
        rem = cur % d;
    }
    bigint_normalize(q);
    return (uint32_t)rem;
}


XSBigInt *bigint_from_i64(int64_t val) {
    XSBigInt *b = bigint_alloc(2);
    if (val == 0) {
        b->len = 0;
        return b;
    }
    b->sign = (val < 0) ? 1 : 0;
    uint64_t uv;
    if (val == INT64_MIN) {
        uv = (uint64_t)INT64_MAX + 1;
    } else {
        uv = (uint64_t)(val < 0 ? -val : val);
    }
    b->limbs[0] = (uint32_t)(uv & 0xFFFFFFFF);
    b->limbs[1] = (uint32_t)(uv >> 32);
    b->len = b->limbs[1] ? 2 : 1;
    return b;
}

XSBigInt *bigint_from_u64(uint64_t val) {
    XSBigInt *b = bigint_alloc(2);
    if (val == 0) {
        b->len = 0;
        return b;
    }
    b->limbs[0] = (uint32_t)(val & 0xFFFFFFFF);
    b->limbs[1] = (uint32_t)(val >> 32);
    b->len = b->limbs[1] ? 2 : 1;
    return b;
}

XSBigInt *bigint_from_str(const char *s, int base) {
    if (!s || !*s) return bigint_from_i64(0);
    int sign = 0;
    if (*s == '-') { sign = 1; s++; }
    else if (*s == '+') { s++; }

    /* skip 0x/0b/0o prefix */
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) { base = 2; s += 2; }
        else if (s[0] == '0' && (s[1] == 'o' || s[1] == 'O')) { base = 8; s += 2; }
        else { base = 10; }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    } else if (base == 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        s += 2;
    } else if (base == 8 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O')) {
        s += 2;
    }

    XSBigInt *result = bigint_from_i64(0);
    XSBigInt *bbase = bigint_from_i64(base);

    for (; *s; s++) {
        if (*s == '_') continue; /* allow underscore separators */
        int digit;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;

        XSBigInt *tmp = bigint_mul(result, bbase);
        bigint_free(result);
        XSBigInt *dval = bigint_from_i64(digit);
        result = bigint_add(tmp, dval);
        bigint_free(tmp);
        bigint_free(dval);
    }

    bigint_free(bbase);
    result->sign = (bigint_is_zero(result)) ? 0 : sign;
    return result;
}

XSBigInt *bigint_copy(const XSBigInt *b) {
    XSBigInt *c = bigint_alloc(b->len > 0 ? b->len : 1);
    c->len = b->len;
    c->sign = b->sign;
    if (b->len > 0)
        memcpy(c->limbs, b->limbs, b->len * sizeof(uint32_t));
    return c;
}

void bigint_free(XSBigInt *b) {
    if (!b) return;
    if (--b->refcount > 0) return;
    free(b->limbs);
    free(b);
}

// conversion

int bigint_fits_i64(const XSBigInt *b) {
    if (b->len == 0) return 1;
    if (b->len > 2) return 0;
    uint64_t mag = b->limbs[0];
    if (b->len == 2)
        mag |= (uint64_t)b->limbs[1] << 32;
    if (b->sign) {
        return mag <= (uint64_t)INT64_MAX + 1;
    } else {
        return mag <= (uint64_t)INT64_MAX;
    }
}

int64_t bigint_to_i64(const XSBigInt *b) {
    if (b->len == 0) return 0;
    uint64_t mag = b->limbs[0];
    if (b->len >= 2)
        mag |= (uint64_t)b->limbs[1] << 32;
    if (b->sign) {
        if (mag == (uint64_t)INT64_MAX + 1) return INT64_MIN;
        return -(int64_t)mag;
    }
    return (int64_t)mag;
}

double bigint_to_double(const XSBigInt *b) {
    if (b->len == 0) return 0.0;
    double result = 0.0;
    double base = 1.0;
    for (int i = 0; i < b->len; i++) {
        result += (double)b->limbs[i] * base;
        base *= (double)LIMB_BASE;
    }
    return b->sign ? -result : result;
}

char *bigint_to_str(const XSBigInt *b, int base) {
    if (base < 2 || base > 36) base = 10;
    if (b->len == 0) return xs_strdup("0");

    /* Collect digits by repeatedly dividing by base */
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    int cap = 64;
    char *buf = (char *)xs_malloc(cap);
    int pos = 0;

    XSBigInt *tmp = bigint_copy(b);
    tmp->sign = 0;

    while (tmp->len > 0) {
        uint32_t rem = mag_div_limb(tmp, tmp, (uint32_t)base);
        if (pos + 2 >= cap) {
            cap *= 2;
            buf = (char *)xs_realloc(buf, cap);
        }
        buf[pos++] = digits[rem];
    }
    bigint_free(tmp);

    if (b->sign) buf[pos++] = '-';
    buf[pos] = '\0';

    /* Reverse */
    for (int i = 0, j = pos - 1; i < j; i++, j--) {
        char c = buf[i]; buf[i] = buf[j]; buf[j] = c;
    }
    return buf;
}


int bigint_is_zero(const XSBigInt *b) {
    return b->len == 0;
}

int bigint_cmp(const XSBigInt *a, const XSBigInt *b) {
    if (a->sign != b->sign) {
        if (bigint_is_zero(a) && bigint_is_zero(b)) return 0;
        return a->sign ? -1 : 1;
    }
    int mc = bigint_cmp_mag(a, b);
    return a->sign ? -mc : mc;
}

int bigint_cmp_i64(const XSBigInt *a, int64_t bval) {
    XSBigInt *bb = bigint_from_i64(bval);
    int r = bigint_cmp(a, bb);
    bigint_free(bb);
    return r;
}

/* arithmetic */

XSBigInt *bigint_add(const XSBigInt *a, const XSBigInt *b) {
    XSBigInt *r;
    if (a->sign == b->sign) {
        /* Same sign: add magnitudes, keep sign */
        int maxlen = (a->len > b->len) ? a->len : b->len;
        r = bigint_alloc(maxlen + 1);
        mag_add(r, a, b);
        r->sign = a->sign;
    } else {
        /* Different signs: subtract smaller magnitude from larger */
        int mc = bigint_cmp_mag(a, b);
        if (mc == 0) {
            return bigint_from_i64(0);
        } else if (mc > 0) {
            r = bigint_alloc(a->len);
            mag_sub(r, a, b);
            r->sign = a->sign;
        } else {
            r = bigint_alloc(b->len);
            mag_sub(r, b, a);
            r->sign = b->sign;
        }
    }
    bigint_normalize(r);
    return r;
}

XSBigInt *bigint_sub(const XSBigInt *a, const XSBigInt *b) {
    XSBigInt bneg = *b;
    bneg.sign = b->sign ? 0 : 1;
    if (bigint_is_zero(b)) bneg.sign = 0;
    return bigint_add(a, &bneg);
}

XSBigInt *bigint_mul(const XSBigInt *a, const XSBigInt *b) {
    if (bigint_is_zero(a) || bigint_is_zero(b))
        return bigint_from_i64(0);

    int rlen = a->len + b->len;
    XSBigInt *r = bigint_alloc(rlen);
    r->len = rlen;
    memset(r->limbs, 0, rlen * sizeof(uint32_t));

    for (int i = 0; i < a->len; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < b->len; j++) {
            uint64_t prod = (uint64_t)a->limbs[i] * b->limbs[j]
                          + r->limbs[i + j] + carry;
            r->limbs[i + j] = (uint32_t)(prod & 0xFFFFFFFF);
            carry = prod >> LIMB_BITS;
        }
        if (carry)
            r->limbs[i + b->len] += (uint32_t)carry;
    }

    r->sign = (a->sign != b->sign) ? 1 : 0;
    bigint_normalize(r);
    return r;
}

XSBigInt *bigint_neg(const XSBigInt *b) {
    XSBigInt *r = bigint_copy(b);
    if (!bigint_is_zero(r))
        r->sign = r->sign ? 0 : 1;
    return r;
}

XSBigInt *bigint_abs(const XSBigInt *b) {
    XSBigInt *r = bigint_copy(b);
    r->sign = 0;
    return r;
}

/* Long division: returns quotient. If rem_out is not NULL, stores remainder. */
static void bigint_divmod(const XSBigInt *a, const XSBigInt *b,
                           XSBigInt **q_out, XSBigInt **r_out) {
    /* Handle division by zero: return 0 */
    if (bigint_is_zero(b)) {
        if (q_out) *q_out = bigint_from_i64(0);
        if (r_out) *r_out = bigint_from_i64(0);
        return;
    }

    int mc = bigint_cmp_mag(a, b);
    if (mc < 0) {
        /* |a| < |b|: quotient=0, remainder=a */
        if (q_out) *q_out = bigint_from_i64(0);
        if (r_out) *r_out = bigint_copy(a);
        return;
    }
    if (mc == 0) {
        /* |a| == |b|: quotient=+-1, remainder=0 */
        if (q_out) {
            *q_out = bigint_from_i64(1);
            (*q_out)->sign = (a->sign != b->sign) ? 1 : 0;
        }
        if (r_out) *r_out = bigint_from_i64(0);
        return;
    }

    /* Single-limb divisor: use fast path */
    if (b->len == 1) {
        XSBigInt *q = bigint_alloc(a->len);
        uint32_t rem = mag_div_limb(q, a, b->limbs[0]);
        q->sign = (a->sign != b->sign) ? 1 : 0;
        if (bigint_is_zero(q)) q->sign = 0;
        if (q_out) *q_out = q; else bigint_free(q);
        if (r_out) {
            *r_out = bigint_from_u64(rem);
            (*r_out)->sign = a->sign;
            if (bigint_is_zero(*r_out)) (*r_out)->sign = 0;
        }
        return;
    }

    /* Schoolbook long division for multi-limb divisor.
     * We perform division on absolute values, then fix signs. */
    int n = b->len;
    int m = a->len - n;

    /* Normalize: shift so that the top bit of divisor's MSL is set */
    int shift = 0;
    {
        uint32_t top = b->limbs[n - 1];
        while (!(top & 0x80000000u)) { top <<= 1; shift++; }
    }

    /* Create shifted copies */
    XSBigInt *u = bigint_alloc(a->len + 1);
    u->len = a->len + 1;
    if (shift > 0) {
        uint64_t carry = 0;
        for (int i = 0; i < a->len; i++) {
            uint64_t val = ((uint64_t)a->limbs[i] << shift) | carry;
            u->limbs[i] = (uint32_t)(val & 0xFFFFFFFF);
            carry = val >> 32;
        }
        u->limbs[a->len] = (uint32_t)carry;
    } else {
        memcpy(u->limbs, a->limbs, a->len * sizeof(uint32_t));
        u->limbs[a->len] = 0;
    }

    XSBigInt *v = bigint_alloc(n);
    v->len = n;
    if (shift > 0) {
        uint64_t carry = 0;
        for (int i = 0; i < n; i++) {
            uint64_t val = ((uint64_t)b->limbs[i] << shift) | carry;
            v->limbs[i] = (uint32_t)(val & 0xFFFFFFFF);
            carry = val >> 32;
        }
    } else {
        memcpy(v->limbs, b->limbs, n * sizeof(uint32_t));
    }

    XSBigInt *q = bigint_alloc(m + 1);
    q->len = m + 1;
    memset(q->limbs, 0, (m + 1) * sizeof(uint32_t));

    uint32_t vn1 = v->limbs[n - 1];
    uint32_t vn2 = (n >= 2) ? v->limbs[n - 2] : 0;

    for (int j = m; j >= 0; j--) {
        /* Estimate q_hat */
        uint64_t u_hi = ((uint64_t)u->limbs[j + n] << 32) | u->limbs[j + n - 1];
        uint64_t q_hat = u_hi / vn1;
        uint64_t r_hat = u_hi % vn1;

        /* Refine estimate */
        while (q_hat >= LIMB_BASE ||
               q_hat * vn2 > (r_hat << 32) + (uint64_t)u->limbs[j + n - 2]) {
            q_hat--;
            r_hat += vn1;
            if (r_hat >= LIMB_BASE) break;
        }

        /* Multiply and subtract: u[j..j+n] -= q_hat * v[0..n-1] */
        int64_t borrow_s = 0;
        for (int i = 0; i < n; i++) {
            uint64_t prod = q_hat * v->limbs[i];
            int64_t diff = (int64_t)u->limbs[j + i] - (int64_t)(prod & 0xFFFFFFFF) + borrow_s;
            u->limbs[j + i] = (uint32_t)(diff & 0xFFFFFFFF);
            borrow_s = (diff >> 32) - (int64_t)(prod >> 32);
        }
        int64_t diff = (int64_t)u->limbs[j + n] + borrow_s;
        u->limbs[j + n] = (uint32_t)(diff & 0xFFFFFFFF);

        q->limbs[j] = (uint32_t)q_hat;

        /* If we subtracted too much, add back */
        if (diff < 0) {
            q->limbs[j]--;
            uint64_t carry = 0;
            for (int i = 0; i < n; i++) {
                uint64_t sum = (uint64_t)u->limbs[j + i] + v->limbs[i] + carry;
                u->limbs[j + i] = (uint32_t)(sum & 0xFFFFFFFF);
                carry = sum >> 32;
            }
            u->limbs[j + n] += (uint32_t)carry;
        }
    }

    bigint_normalize(q);
    q->sign = (a->sign != b->sign) ? 1 : 0;
    if (bigint_is_zero(q)) q->sign = 0;
    if (q_out) *q_out = q; else bigint_free(q);

    if (r_out) {
        /* Remainder = u[0..n-1] >> shift (un-normalize) */
        XSBigInt *rem = bigint_alloc(n);
        rem->len = n;
        if (shift > 0) {
            uint32_t carry = 0;
            for (int i = n - 1; i >= 0; i--) {
                uint64_t val = ((uint64_t)carry << 32) | u->limbs[i];
                rem->limbs[i] = (uint32_t)(val >> shift);
                carry = (uint32_t)(val & ((1u << shift) - 1));
            }
        } else {
            memcpy(rem->limbs, u->limbs, n * sizeof(uint32_t));
        }
        bigint_normalize(rem);
        rem->sign = a->sign;
        if (bigint_is_zero(rem)) rem->sign = 0;
        *r_out = rem;
    }

    bigint_free(u);
    bigint_free(v);
}

XSBigInt *bigint_div(const XSBigInt *a, const XSBigInt *b) {
    XSBigInt *q;
    bigint_divmod(a, b, &q, NULL);
    return q;
}

XSBigInt *bigint_mod(const XSBigInt *a, const XSBigInt *b) {
    XSBigInt *r;
    bigint_divmod(a, b, NULL, &r);
    return r;
}

XSBigInt *bigint_pow(const XSBigInt *base, int64_t exp) {
    if (exp < 0) {
        /* Negative exponent: integer result is 0 (truncation) unless base is +-1 */
        if (base->len == 1 && base->limbs[0] == 1) {
            if (base->sign && (exp & 1))
                return bigint_from_i64(-1);
            return bigint_from_i64(1);
        }
        return bigint_from_i64(0);
    }
    if (exp == 0)
        return bigint_from_i64(1);

    XSBigInt *result = bigint_from_i64(1);
    XSBigInt *b = bigint_copy(base);
    b->sign = 0; /* work with magnitude */
    uint64_t e = (uint64_t)exp;

    while (e > 0) {
        if (e & 1) {
            XSBigInt *tmp = bigint_mul(result, b);
            bigint_free(result);
            result = tmp;
        }
        e >>= 1;
        if (e > 0) {
            XSBigInt *tmp = bigint_mul(b, b);
            bigint_free(b);
            b = tmp;
        }
    }
    bigint_free(b);

    /* Sign: negative base with odd exponent gives negative result */
    if (base->sign && (exp & 1))
        result->sign = 1;

    return result;
}

/* bitwise — negative bigints just return zero for now */

XSBigInt *bigint_and(const XSBigInt *a, const XSBigInt *b) {
    if (a->sign || b->sign) return bigint_from_i64(0);
    int len = (a->len < b->len) ? a->len : b->len;
    if (len == 0) return bigint_from_i64(0);
    XSBigInt *r = bigint_alloc(len);
    r->len = len;
    for (int i = 0; i < len; i++)
        r->limbs[i] = a->limbs[i] & b->limbs[i];
    bigint_normalize(r);
    return r;
}

XSBigInt *bigint_or(const XSBigInt *a, const XSBigInt *b) {
    if (a->sign || b->sign) return bigint_from_i64(0);
    int len = (a->len > b->len) ? a->len : b->len;
    if (len == 0) return bigint_from_i64(0);
    XSBigInt *r = bigint_alloc(len);
    r->len = len;
    for (int i = 0; i < len; i++) {
        uint32_t va = (i < a->len) ? a->limbs[i] : 0;
        uint32_t vb = (i < b->len) ? b->limbs[i] : 0;
        r->limbs[i] = va | vb;
    }
    bigint_normalize(r);
    return r;
}

XSBigInt *bigint_xor(const XSBigInt *a, const XSBigInt *b) {
    if (a->sign || b->sign) return bigint_from_i64(0);
    int len = (a->len > b->len) ? a->len : b->len;
    if (len == 0) return bigint_from_i64(0);
    XSBigInt *r = bigint_alloc(len);
    r->len = len;
    for (int i = 0; i < len; i++) {
        uint32_t va = (i < a->len) ? a->limbs[i] : 0;
        uint32_t vb = (i < b->len) ? b->limbs[i] : 0;
        r->limbs[i] = va ^ vb;
    }
    bigint_normalize(r);
    return r;
}

XSBigInt *bigint_shl(const XSBigInt *a, int64_t shift) {
    if (shift < 0) return bigint_shr(a, -shift);
    if (bigint_is_zero(a) || shift == 0) return bigint_copy(a);

    int limb_shift = (int)(shift / LIMB_BITS);
    int bit_shift = (int)(shift % LIMB_BITS);
    int newlen = a->len + limb_shift + 1;
    XSBigInt *r = bigint_alloc(newlen);
    r->len = newlen;
    memset(r->limbs, 0, newlen * sizeof(uint32_t));

    uint64_t carry = 0;
    for (int i = 0; i < a->len; i++) {
        uint64_t val = ((uint64_t)a->limbs[i] << bit_shift) | carry;
        r->limbs[i + limb_shift] = (uint32_t)(val & 0xFFFFFFFF);
        carry = val >> 32;
    }
    if (carry)
        r->limbs[a->len + limb_shift] = (uint32_t)carry;

    r->sign = a->sign;
    bigint_normalize(r);
    return r;
}

XSBigInt *bigint_shr(const XSBigInt *a, int64_t shift) {
    if (shift < 0) return bigint_shl(a, -shift);
    if (bigint_is_zero(a) || shift == 0) return bigint_copy(a);

    int limb_shift = (int)(shift / LIMB_BITS);
    int bit_shift = (int)(shift % LIMB_BITS);

    if (limb_shift >= a->len) {
        /* For negative numbers, arithmetic right shift floors to -1 */
        if (a->sign) return bigint_from_i64(-1);
        return bigint_from_i64(0);
    }

    int newlen = a->len - limb_shift;
    XSBigInt *r = bigint_alloc(newlen);
    r->len = newlen;

    if (bit_shift == 0) {
        memcpy(r->limbs, a->limbs + limb_shift, newlen * sizeof(uint32_t));
    } else {
        for (int i = 0; i < newlen; i++) {
            uint32_t lo = a->limbs[i + limb_shift] >> bit_shift;
            uint32_t hi = 0;
            if (i + limb_shift + 1 < a->len)
                hi = a->limbs[i + limb_shift + 1] << (32 - bit_shift);
            r->limbs[i] = lo | hi;
        }
    }

    r->sign = a->sign;
    bigint_normalize(r);

    /* Arithmetic right shift for negative: if any bits were shifted out, floor toward -inf */
    if (a->sign && !bigint_is_zero(r)) {
        /* Check if any bits were lost */
        int lost = 0;
        for (int i = 0; i < limb_shift && i < a->len; i++) {
            if (a->limbs[i]) { lost = 1; break; }
        }
        if (!lost && bit_shift > 0 && limb_shift < a->len) {
            if (a->limbs[limb_shift] & ((1u << bit_shift) - 1))
                lost = 1;
        }
        if (lost) {
            /* Add 1 to magnitude (floor toward -inf) */
            XSBigInt *one = bigint_from_i64(1);
            one->sign = 0;
            r->sign = 0;
            XSBigInt *tmp = bigint_add(r, one);
            bigint_free(one);
            bigint_free(r);
            r = tmp;
            r->sign = 1;
        }
    }

    return r;
}

/* safe arithmetic (i64 fast path) */

Value *xs_safe_add(int64_t a, int64_t b) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        XSBigInt *ba = bigint_from_i64(a);
        XSBigInt *bb = bigint_from_i64(b);
        XSBigInt *r = bigint_add(ba, bb);
        bigint_free(ba);
        bigint_free(bb);
        return xs_bigint_val(r);
    }
    return xs_int(a + b);
}

Value *xs_safe_sub(int64_t a, int64_t b) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
        XSBigInt *ba = bigint_from_i64(a);
        XSBigInt *bb = bigint_from_i64(b);
        XSBigInt *r = bigint_sub(ba, bb);
        bigint_free(ba);
        bigint_free(bb);
        return xs_bigint_val(r);
    }
    return xs_int(a - b);
}

Value *xs_safe_mul(int64_t a, int64_t b) {
    /* Use 128-bit or careful overflow check */
    if (a == 0 || b == 0) return xs_int(0);
    /* Check overflow: |a| * |b| > INT64_MAX */
    int overflow = 0;
    if (a > 0) {
        if (b > 0) { if (a > INT64_MAX / b) overflow = 1; }
        else       { if (b < INT64_MIN / a) overflow = 1; }
    } else {
        if (b > 0) { if (a < INT64_MIN / b) overflow = 1; }
        else       { if (a != -1 && b < INT64_MAX / a) overflow = 1;
                     else if (a == -1) overflow = 0; /* -1 * b always fits if b fits */ }
    }
    if (overflow) {
        XSBigInt *ba = bigint_from_i64(a);
        XSBigInt *bb = bigint_from_i64(b);
        XSBigInt *r = bigint_mul(ba, bb);
        bigint_free(ba);
        bigint_free(bb);
        return xs_bigint_val(r);
    }
    return xs_int(a * b);
}

Value *xs_safe_neg(int64_t a) {
    if (a == INT64_MIN) {
        XSBigInt *ba = bigint_from_i64(a);
        XSBigInt *r = bigint_neg(ba);
        bigint_free(ba);
        return xs_bigint_val(r);
    }
    return xs_int(-a);
}

Value *xs_safe_pow(int64_t base, int64_t exp) {
    if (exp < 0) {
        /* Integer power with negative exponent */
        if (base == 1) return xs_int(1);
        if (base == -1) return xs_int((exp & 1) ? -1 : 1);
        return xs_int(0);
    }
    if (exp == 0) return xs_int(1);
    if (base == 0) return xs_int(0);
    if (base == 1) return xs_int(1);
    if (base == -1) return xs_int((exp & 1) ? -1 : 1);

    /* Try i64 fast path with overflow detection */
    int64_t result = 1;
    int64_t b = base;
    int64_t e = exp;
    int overflowed = 0;

    while (e > 0) {
        if (e & 1) {
            /* Check multiply overflow */
            if (b > 0) {
                if (result > INT64_MAX / b) { overflowed = 1; break; }
            } else if (b < -1) {
                if (result > 0 && result > INT64_MIN / b) { overflowed = 1; break; }
                if (result < 0 && result < INT64_MAX / b) { overflowed = 1; break; }
            }
            result *= b;
        }
        e >>= 1;
        if (e > 0) {
            /* Check square overflow */
            if (b > 0 && b > (int64_t)46340) { /* sqrt(INT64_MAX) ~= 3037000499 but be conservative */
                if (b > 3037000499LL) { overflowed = 1; break; }
                if (b * b < 0) { overflowed = 1; break; }  /* sanity */
            }
            if (b < 0) {
                int64_t ab = (b == INT64_MIN) ? INT64_MAX : (b < 0 ? -b : b);
                if (ab > 3037000499LL) { overflowed = 1; break; }
            }
            b *= b;
        }
    }

    if (overflowed) {
        XSBigInt *bb = bigint_from_i64(base);
        XSBigInt *r = bigint_pow(bb, exp);
        bigint_free(bb);
        return xs_bigint_val(r);
    }
    return xs_int(result);
}


Value *xs_bigint_val(XSBigInt *b) {
    if (bigint_fits_i64(b)) {
        int64_t v = bigint_to_i64(b);
        bigint_free(b);
        return xs_int(v);
    }
    Value *val = (Value *)xs_malloc(sizeof(Value));
    val->tag = XS_BIGINT;
    val->refcount = 1;
    val->bigint = b;
    return val;
}

/* mixed operations */

static XSBigInt *val_to_bigint(Value *v) {
    if (v->tag == XS_BIGINT)
        return bigint_copy(v->bigint);
    if (v->tag == XS_INT)
        return bigint_from_i64(v->i);
    /* FLOAT: truncate to i64 first, then bigint */
    if (v->tag == XS_FLOAT) {
        if (v->f >= (double)INT64_MIN && v->f <= (double)INT64_MAX)
            return bigint_from_i64((int64_t)v->f);
        /* For very large floats, use string conversion */
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f", v->f < 0 ? -v->f : v->f);
        XSBigInt *b = bigint_from_str(buf, 10);
        if (v->f < 0) b->sign = 1;
        return b;
    }
    return bigint_from_i64(0);
}

/* If both operands are INT, try the safe i64 path first */
static int both_int(Value *a, Value *b) {
    return a->tag == XS_INT && b->tag == XS_INT;
}

Value *xs_numeric_add(Value *a, Value *b) {
    if (both_int(a, b)) return xs_safe_add(a->i, b->i);
    /* At least one is BIGINT (or mixed) */
    if (a->tag == XS_FLOAT || b->tag == XS_FLOAT) {
        double fa = (a->tag == XS_FLOAT) ? a->f :
                    (a->tag == XS_BIGINT) ? bigint_to_double(a->bigint) : (double)a->i;
        double fb = (b->tag == XS_FLOAT) ? b->f :
                    (b->tag == XS_BIGINT) ? bigint_to_double(b->bigint) : (double)b->i;
        return xs_float(fa + fb);
    }
    XSBigInt *ba = val_to_bigint(a);
    XSBigInt *bb = val_to_bigint(b);
    XSBigInt *r = bigint_add(ba, bb);
    bigint_free(ba);
    bigint_free(bb);
    return xs_bigint_val(r);
}

Value *xs_numeric_sub(Value *a, Value *b) {
    if (both_int(a, b)) return xs_safe_sub(a->i, b->i);
    if (a->tag == XS_FLOAT || b->tag == XS_FLOAT) {
        double fa = (a->tag == XS_FLOAT) ? a->f :
                    (a->tag == XS_BIGINT) ? bigint_to_double(a->bigint) : (double)a->i;
        double fb = (b->tag == XS_FLOAT) ? b->f :
                    (b->tag == XS_BIGINT) ? bigint_to_double(b->bigint) : (double)b->i;
        return xs_float(fa - fb);
    }
    XSBigInt *ba = val_to_bigint(a);
    XSBigInt *bb = val_to_bigint(b);
    XSBigInt *r = bigint_sub(ba, bb);
    bigint_free(ba);
    bigint_free(bb);
    return xs_bigint_val(r);
}

Value *xs_numeric_mul(Value *a, Value *b) {
    if (both_int(a, b)) return xs_safe_mul(a->i, b->i);
    if (a->tag == XS_FLOAT || b->tag == XS_FLOAT) {
        double fa = (a->tag == XS_FLOAT) ? a->f :
                    (a->tag == XS_BIGINT) ? bigint_to_double(a->bigint) : (double)a->i;
        double fb = (b->tag == XS_FLOAT) ? b->f :
                    (b->tag == XS_BIGINT) ? bigint_to_double(b->bigint) : (double)b->i;
        return xs_float(fa * fb);
    }
    XSBigInt *ba = val_to_bigint(a);
    XSBigInt *bb = val_to_bigint(b);
    XSBigInt *r = bigint_mul(ba, bb);
    bigint_free(ba);
    bigint_free(bb);
    return xs_bigint_val(r);
}

Value *xs_numeric_div(Value *a, Value *b) {
    /* Division always produces integer quotient for bigints */
    if (both_int(a, b)) {
        if (b->i == 0) return xs_int(0); /* or error */
        /* Check for INT64_MIN / -1 overflow */
        if (a->i == INT64_MIN && b->i == -1) {
            XSBigInt *ba = bigint_from_i64(a->i);
            XSBigInt *bb = bigint_from_i64(b->i);
            XSBigInt *r = bigint_div(ba, bb);
            bigint_free(ba);
            bigint_free(bb);
            return xs_bigint_val(r);
        }
        return xs_int(a->i / b->i);
    }
    if (a->tag == XS_FLOAT || b->tag == XS_FLOAT) {
        double fa = (a->tag == XS_FLOAT) ? a->f :
                    (a->tag == XS_BIGINT) ? bigint_to_double(a->bigint) : (double)a->i;
        double fb = (b->tag == XS_FLOAT) ? b->f :
                    (b->tag == XS_BIGINT) ? bigint_to_double(b->bigint) : (double)b->i;
        return xs_float(fa / fb);
    }
    XSBigInt *ba = val_to_bigint(a);
    XSBigInt *bb = val_to_bigint(b);
    XSBigInt *r = bigint_div(ba, bb);
    bigint_free(ba);
    bigint_free(bb);
    return xs_bigint_val(r);
}

Value *xs_numeric_mod(Value *a, Value *b) {
    if (both_int(a, b)) {
        if (b->i == 0) return xs_int(0);
        return xs_int(a->i % b->i);
    }
    if (a->tag == XS_FLOAT || b->tag == XS_FLOAT) {
        double fa = (a->tag == XS_FLOAT) ? a->f :
                    (a->tag == XS_BIGINT) ? bigint_to_double(a->bigint) : (double)a->i;
        double fb = (b->tag == XS_FLOAT) ? b->f :
                    (b->tag == XS_BIGINT) ? bigint_to_double(b->bigint) : (double)b->i;
        return xs_float(fmod(fa, fb));
    }
    XSBigInt *ba = val_to_bigint(a);
    XSBigInt *bb = val_to_bigint(b);
    XSBigInt *r = bigint_mod(ba, bb);
    bigint_free(ba);
    bigint_free(bb);
    return xs_bigint_val(r);
}

Value *xs_numeric_pow(Value *a, Value *b) {
    if (both_int(a, b)) return xs_safe_pow(a->i, b->i);
    if (a->tag == XS_FLOAT || b->tag == XS_FLOAT) {
        double fa = (a->tag == XS_FLOAT) ? a->f :
                    (a->tag == XS_BIGINT) ? bigint_to_double(a->bigint) : (double)a->i;
        double fb = (b->tag == XS_FLOAT) ? b->f :
                    (b->tag == XS_BIGINT) ? bigint_to_double(b->bigint) : (double)b->i;
        return xs_float(pow(fa, fb));
    }
    /* exponent must be convertible to i64 for bigint_pow */
    XSBigInt *ba = val_to_bigint(a);
    int64_t exp_val = 0;
    if (b->tag == XS_INT) {
        exp_val = b->i;
    } else if (b->tag == XS_BIGINT) {
        if (bigint_fits_i64(b->bigint))
            exp_val = bigint_to_i64(b->bigint);
        else {
            /* Exponent too large -- result would be astronomical */
            bigint_free(ba);
            return xs_int(0);
        }
    }
    XSBigInt *r = bigint_pow(ba, exp_val);
    bigint_free(ba);
    return xs_bigint_val(r);
}

Value *xs_numeric_neg(Value *a) {
    if (a->tag == XS_INT) return xs_safe_neg(a->i);
    if (a->tag == XS_FLOAT) return xs_float(-a->f);
    if (a->tag == XS_BIGINT) {
        XSBigInt *r = bigint_neg(a->bigint);
        return xs_bigint_val(r);
    }
    return xs_int(0);
}

Value *xs_numeric_floordiv(Value *a, Value *b) {
    if (both_int(a, b)) {
        if (b->i == 0) return xs_int(0);
        if (a->i == INT64_MIN && b->i == -1) {
            XSBigInt *ba = bigint_from_i64(a->i);
            XSBigInt *bb = bigint_from_i64(b->i);
            XSBigInt *r = bigint_div(ba, bb);
            bigint_free(ba);
            bigint_free(bb);
            return xs_bigint_val(r);
        }
        int64_t q = a->i / b->i;
        int64_t r = a->i % b->i;
        /* Floor division: round toward negative infinity */
        if (r != 0 && ((a->i ^ b->i) < 0)) q--;
        return xs_int(q);
    }
    if (a->tag == XS_FLOAT || b->tag == XS_FLOAT) {
        double fa = (a->tag == XS_FLOAT) ? a->f :
                    (a->tag == XS_BIGINT) ? bigint_to_double(a->bigint) : (double)a->i;
        double fb = (b->tag == XS_FLOAT) ? b->f :
                    (b->tag == XS_BIGINT) ? bigint_to_double(b->bigint) : (double)b->i;
        return xs_float(floor(fa / fb));
    }
    /* Bigint floor division: q = a/b, adjust if remainder has opposite sign to divisor */
    XSBigInt *ba = val_to_bigint(a);
    XSBigInt *bb = val_to_bigint(b);
    XSBigInt *q, *r;
    bigint_divmod(ba, bb, &q, &r);
    /* If remainder is nonzero and signs of a and b differ, subtract 1 from quotient */
    if (!bigint_is_zero(r) && (ba->sign != bb->sign)) {
        XSBigInt *one = bigint_from_i64(1);
        XSBigInt *q2 = bigint_sub(q, one);
        bigint_free(q);
        bigint_free(one);
        q = q2;
    }
    bigint_free(ba);
    bigint_free(bb);
    bigint_free(r);
    return xs_bigint_val(q);
}
