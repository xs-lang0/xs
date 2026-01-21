#include "core/xs_compat.h"
#include "core/value.h"
#include "core/xs_bigint.h"
#include "core/ast.h"
#include "core/env.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef XSC_ENABLE_VM
#include "vm/bytecode.h"
#include "vm/vm.h"
extern void proto_free(struct XSProto *);
#endif

Value *XS_NULL_VAL = NULL;
Value *XS_TRUE_VAL  = NULL;
Value *XS_FALSE_VAL = NULL;

static Value *val_alloc(ValueTag tag) {
    Value *v = xs_malloc(sizeof(Value));
    memset(v, 0, sizeof(Value));
    v->tag      = tag;
    v->refcount = 1;
    return v;
}

void value_init_singletons(void) {
    XS_NULL_VAL  = val_alloc(XS_NULL);
    XS_NULL_VAL->refcount = 1000000; /* pinned */
    XS_TRUE_VAL  = val_alloc(XS_BOOL);
    XS_TRUE_VAL->i = 1;
    XS_TRUE_VAL->refcount = 1000000;
    XS_FALSE_VAL = val_alloc(XS_BOOL);
    XS_FALSE_VAL->i = 0;
    XS_FALSE_VAL->refcount = 1000000;
}

Value *xs_null(void) { return value_incref(XS_NULL_VAL); }
Value *xs_bool(int b) { return value_incref(b ? XS_TRUE_VAL : XS_FALSE_VAL); }

Value *xs_int(int64_t i) {
    Value *v = val_alloc(XS_INT);
    v->i = i;
    return v;
}

Value *xs_float(double f) {
    Value *v = val_alloc(XS_FLOAT);
    v->f = f;
    return v;
}

Value *xs_str(const char *s) {
    Value *v = val_alloc(XS_STR);
    v->s = xs_strdup(s ? s : "");
    return v;
}

Value *xs_str_n(const char *s, size_t n) {
    Value *v = val_alloc(XS_STR);
    v->s = xs_strndup(s, n);
    return v;
}

Value *xs_char(char c) {
    Value *v = val_alloc(XS_CHAR);
    char buf[2] = {c, 0};
    v->s = xs_strdup(buf);
    return v;
}

Value *xs_array_new(void) {
    Value *v = val_alloc(XS_ARRAY);
    v->arr = array_new();
    return v;
}

Value *xs_tuple_new(void) {
    Value *v = val_alloc(XS_TUPLE);
    v->arr = array_new();
    return v;
}

Value *xs_map_new(void) {
    Value *v = val_alloc(XS_MAP);
    v->map = map_new();
    return v;
}

Value *xs_func_new(XSFunc *fn) {
    Value *v = val_alloc(XS_FUNC);
    v->fn = fn;
    fn->refcount++;
    return v;
}

Value *xs_native(NativeFn fn) {
    Value *v = val_alloc(XS_NATIVE);
    v->native = fn;
    return v;
}

Value *xs_range(int64_t start, int64_t end, int inclusive) {
    return xs_range_step(start, end, inclusive, 1);
}

Value *xs_range_step(int64_t start, int64_t end, int inclusive, int64_t step) {
    Value *v = val_alloc(XS_RANGE);
    XSRange *r = xs_malloc(sizeof *r);
    r->start = start;
    r->end = end;
    r->step = step ? step : 1;
    r->inclusive = inclusive;
    r->refcount = 1;
    v->range = r;
    return v;
}

Value *xs_module(XSMap *m) {
    Value *v = val_alloc(XS_MODULE);
    v->map = m;
    m->refcount++;
    return v;
}

// refcount
Value *value_incref(Value *v) {
    if (v) v->refcount++;
    return v;
}

static void free_value(Value *v) {
    if (!v) return;
    switch (v->tag) {
        case XS_STR:
        case XS_CHAR:
            free(v->s);
            break;
        case XS_ARRAY:
        case XS_TUPLE:
            if (v->arr) array_free(v->arr);
            break;
        case XS_MAP:
        case XS_MODULE:
            if (v->map) map_free(v->map);
            break;
        case XS_FUNC:
            if (v->fn) {
                v->fn->refcount--;
                if (v->fn->refcount <= 0) func_free(v->fn);
            }
            break;
        case XS_RANGE:
            if (v->range) {
                v->range->refcount--;
                if (v->range->refcount <= 0) free(v->range);
            }
            break;
        case XS_BIGINT:
            if (v->bigint) {
                v->bigint->refcount--;
                if (v->bigint->refcount <= 0) bigint_free(v->bigint);
            }
            break;
        case XS_SIGNAL:
            if (v->signal) {
                v->signal->refcount--;
                if (v->signal->refcount <= 0) {
                    value_decref(v->signal->value);
                    for (int si = 0; si < v->signal->nsubs; si++)
                        value_decref(v->signal->subscribers[si]);
                    free(v->signal->subscribers);
                    if (v->signal->compute) value_decref(v->signal->compute);
                    free(v->signal);
                }
            }
            break;
        case XS_STRUCT_VAL:
            if (v->st) {
                v->st->refcount--;
                if (v->st->refcount <= 0) {
                    free(v->st->type_name);
                    if (v->st->fields) map_free(v->st->fields);
                    free(v->st);
                }
            }
            break;
        case XS_ENUM_VAL:
            if (v->en) {
                v->en->refcount--;
                if (v->en->refcount <= 0) {
                    free(v->en->type_name);
                    free(v->en->variant);
                    if (v->en->arr_data) array_free(v->en->arr_data);
                    if (v->en->map_data) map_free(v->en->map_data);
                    free(v->en);
                }
            }
            break;
        case XS_CLASS_VAL:
            if (v->cls) {
                v->cls->refcount--;
                if (v->cls->refcount <= 0) {
                    free(v->cls->name);
                    if (v->cls->fields)         map_free(v->cls->fields);
                    if (v->cls->methods)        map_free(v->cls->methods);
                    if (v->cls->static_methods) map_free(v->cls->static_methods);
                    free(v->cls->bases); /* base pointers only, not the classes themselves */
                    for (int ti = 0; ti < v->cls->ntraits; ti++) free(v->cls->traits[ti]);
                    free(v->cls->traits);
                    free(v->cls);
                }
            }
            break;
        case XS_INST:
            if (v->inst) {
                v->inst->refcount--;
                if (v->inst->refcount <= 0) {
                    if (v->inst->fields)  map_free(v->inst->fields);
                    if (v->inst->methods) map_free(v->inst->methods);
                    free(v->inst);
                }
            }
            break;
        case XS_ACTOR:
            if (v->actor) {
                v->actor->refcount--;
                if (v->actor->refcount <= 0) {
                    free(v->actor->name);
                    if (v->actor->state)     map_free(v->actor->state);
                    if (v->actor->handle_fn) {
                        v->actor->handle_fn->refcount--;
                        if (v->actor->handle_fn->refcount <= 0)
                            func_free(v->actor->handle_fn);
                    }
                    if (v->actor->methods) map_free(v->actor->methods);
                    if (v->actor->closure) env_decref(v->actor->closure);
                    free(v->actor);
                }
            }
            break;
#ifdef XSC_ENABLE_VM
        case XS_CLOSURE:
            if (v->cl) {
                v->cl->refcount--;
                if (v->cl->refcount <= 0) {
                    proto_free((struct XSProto *)v->cl->proto);
                    for (int i = 0; i < v->cl->proto->n_upvalues; i++) {
                        Upvalue *uv = v->cl->upvalues[i];
                        uv->refcount--;
                        if (uv->refcount <= 0) {
                            if (!uv->is_open && uv->closed_val)
                                value_decref(uv->closed_val);
                            free(uv);
                        }
                    }
                    free(v->cl->upvalues);
                    free(v->cl);
                }
            }
            break;
#endif
        default: break;
    }
    free(v);
}

void value_decref(Value *v) {
    if (!v) return;
    v->refcount--;
    if (v->refcount <= 0) free_value(v);
}

int value_truthy(Value *v) {
    if (!v) return 0;
    switch (v->tag) {
        case XS_NULL:   return 0;
        case XS_BOOL:   return (int)v->i;
        case XS_INT:    return v->i != 0;
        case XS_BIGINT: return !bigint_is_zero(v->bigint);
        case XS_FLOAT:  return v->f != 0.0;
        case XS_STR:    return v->s && v->s[0] != '\0';
        case XS_CHAR:   return v->s && v->s[0] != '\0';
        case XS_ARRAY:
        case XS_TUPLE:  return v->arr && v->arr->len > 0;
        case XS_MAP:    return v->map && v->map->len > 0;
        default:        return 1;
    }
}

/* repr */
char *value_repr(Value *v) {
    if (!v) return xs_strdup("null");
    char buf[64];
    switch (v->tag) {
        case XS_NULL:  return xs_strdup("null");
        case XS_BOOL:  return xs_strdup(v->i ? "true" : "false");
        case XS_INT:
            snprintf(buf, sizeof(buf), "%lld", (long long)v->i);
            return xs_strdup(buf);
        case XS_BIGINT: {
            char *s = bigint_to_str(v->bigint, 10);
            return s;  /* already heap-allocated */
        }
        case XS_FLOAT: {
            snprintf(buf, sizeof(buf), "%g", v->f);
            return xs_strdup(buf);
        }
        case XS_STR:   return xs_strdup(v->s ? v->s : "");
        case XS_CHAR:  return xs_strdup(v->s ? v->s : "");
        case XS_ARRAY:
        case XS_TUPLE: {
            size_t sz = 64;
            char *out = xs_malloc(sz);
            size_t pos = 0;
            out[pos++] = (v->tag == XS_TUPLE) ? '(' : '[';
            if (v->arr) {
                for (int i = 0; i < v->arr->len; i++) {
                    char *item = value_repr(v->arr->items[i]);
                    size_t ilen = strlen(item);
                    while (pos + ilen + 4 >= sz) { sz *= 2; out = xs_realloc(out, sz); }
                    if (i > 0) { out[pos++] = ','; out[pos++] = ' '; }
                    memcpy(out+pos, item, ilen); pos += ilen;
                    free(item);
                }
            }
            while (pos + 2 >= sz) { sz *= 2; out = xs_realloc(out, sz); }
            out[pos++] = (v->tag == XS_TUPLE) ? ')' : ']';
            out[pos] = '\0';
            return out;
        }
        case XS_MAP: {
            size_t sz = 64;
            char *out = xs_malloc(sz);
            size_t pos = 0;
            out[pos++] = '{';
            if (v->map) {
                int first = 1;
                for (int i = 0; i < v->map->cap; i++) {
                    if (!v->map->keys[i]) continue;
                    char *val_s = value_repr(v->map->vals[i]);
                    size_t needed = strlen(v->map->keys[i]) + strlen(val_s) + 8;
                    while (pos + needed >= sz) { sz *= 2; out = xs_realloc(out, sz); }
                    if (!first) { out[pos++] = ','; out[pos++] = ' '; }
                    first = 0;
                    size_t kl = strlen(v->map->keys[i]);
                    memcpy(out+pos, v->map->keys[i], kl); pos += kl;
                    out[pos++] = ':'; out[pos++] = ' ';
                    size_t vl = strlen(val_s);
                    memcpy(out+pos, val_s, vl); pos += vl;
                    free(val_s);
                }
            }
            while (pos + 2 >= sz) { sz *= 2; out = xs_realloc(out, sz); }
            out[pos++] = '}';
            out[pos] = '\0';
            return out;
        }
        case XS_FUNC:
            snprintf(buf, sizeof(buf), "<fn %s>", v->fn && v->fn->name ? v->fn->name : "<anonymous>");
            return xs_strdup(buf);
        case XS_NATIVE:
            return xs_strdup("<native fn>");
        case XS_RANGE: {
            if (v->range)
                snprintf(buf, sizeof(buf), "%lld%s%lld",
                    (long long)v->range->start,
                    v->range->inclusive ? "..=" : "..",
                    (long long)v->range->end);
            else snprintf(buf, sizeof(buf), "0..0");
            return xs_strdup(buf);
        }
        case XS_SIGNAL: {
            char *inner = v->signal ? value_repr(v->signal->value) : xs_strdup("null");
            size_t slen = strlen(inner) + 16;
            char *out = xs_malloc(slen);
            snprintf(out, slen, "Signal(%s)", inner);
            free(inner);
            return out;
        }
        case XS_STRUCT_VAL:
            if (v->st) {
                char *fields_s = value_repr(&(Value){.tag=XS_MAP, .map=v->st->fields});
                size_t len = strlen(v->st->type_name) + strlen(fields_s) + 4;
                char *out = xs_malloc(len);
                snprintf(out, len, "%s %s", v->st->type_name, fields_s);
                free(fields_s);
                return out;
            }
            return xs_strdup("<struct>");
        case XS_ENUM_VAL:
            if (v->en) {
                /* If type_name == variant (e.g. Ok/Err/Some), omit the type prefix */
                int bare = (strcmp(v->en->type_name, v->en->variant) == 0);
                if (v->en->arr_data && v->en->arr_data->len > 0) {
                    size_t prefix_len = bare ? strlen(v->en->variant)
                                             : strlen(v->en->type_name) + 2 + strlen(v->en->variant);
                    size_t needed = prefix_len + 8;
                    for (int ei = 0; ei < v->en->arr_data->len; ei++) {
                        char *ds = value_repr(v->en->arr_data->items[ei]);
                        needed += strlen(ds) + 2;
                        free(ds);
                    }
                    char *out = xs_malloc(needed);
                    int pos;
                    if (bare) pos = sprintf(out, "%s(", v->en->variant);
                    else      pos = sprintf(out, "%s::%s(", v->en->type_name, v->en->variant);
                    for (int ei = 0; ei < v->en->arr_data->len; ei++) {
                        if (ei > 0) { out[pos++] = ','; out[pos++] = ' '; }
                        char *ds = value_repr(v->en->arr_data->items[ei]);
                        pos += sprintf(out + pos, "%s", ds);
                        free(ds);
                    }
                    out[pos++] = ')'; out[pos] = '\0';
                    return out;
                } else {
                    if (bare) snprintf(buf, sizeof(buf), "%s", v->en->variant);
                    else      snprintf(buf, sizeof(buf), "%s::%s", v->en->type_name, v->en->variant);
                }
                return xs_strdup(buf);
            }
            return xs_strdup("<enum>");
        case XS_CLASS_VAL:
            snprintf(buf, sizeof(buf), "<class %s>", v->cls ? v->cls->name : "?");
            return xs_strdup(buf);
        case XS_INST:
            if (v->inst && v->inst->class_ && v->inst->fields) {
                const char *cname = v->inst->class_->name;
                int nk = 0;
                char **ks = map_keys(v->inst->fields, &nk);
                size_t needed = strlen(cname) + 8;
                char **reprs = xs_malloc(sizeof(char*) * (nk > 0 ? nk : 1));
                for (int fi = 0; fi < nk; fi++) {
                    reprs[fi] = value_repr(map_get(v->inst->fields, ks[fi]));
                    needed += strlen(ks[fi]) + strlen(reprs[fi]) + 6;
                }
                char *out = xs_malloc(needed);
                int pos = sprintf(out, "%s { ", cname);
                for (int fi = 0; fi < nk; fi++) {
                    if (fi > 0) { pos += sprintf(out + pos, ", "); }
                    pos += sprintf(out + pos, "%s: %s", ks[fi], reprs[fi]);
                    free(reprs[fi]);
                    free(ks[fi]);
                }
                free(reprs);
                free(ks);
                pos += sprintf(out + pos, " }");
                return out;
            }
            snprintf(buf, sizeof(buf), "<instance of %s>",
                v->inst && v->inst->class_ ? v->inst->class_->name : "?");
            return xs_strdup(buf);
        case XS_ACTOR:
            snprintf(buf, sizeof(buf), "<actor %s>", v->actor ? v->actor->name : "?");
            return xs_strdup(buf);
        case XS_MODULE:
            return xs_strdup("<module>");
#ifdef XSC_ENABLE_VM
        case XS_CLOSURE: return xs_strdup("<closure>");
#endif
        default:
            return xs_strdup("<value>");
    }
}

char *value_str(Value *v) { return value_repr(v); }

/* equality */
int value_equal(Value *a, Value *b) {
    if (!a || !b) return a == b;
    if (a == b) return 1;
    if (a->tag == XS_NULL && b->tag == XS_NULL) return 1;
    if (a->tag == XS_BOOL && b->tag == XS_BOOL) return a->i == b->i;
    if (a->tag == XS_INT  && b->tag == XS_INT)  return a->i == b->i;
    if (a->tag == XS_FLOAT && b->tag == XS_FLOAT) return a->f == b->f;
    if (a->tag == XS_INT  && b->tag == XS_FLOAT) return (double)a->i == b->f;
    if (a->tag == XS_FLOAT && b->tag == XS_INT)  return a->f == (double)b->i;
    if (a->tag == XS_BIGINT && b->tag == XS_BIGINT) return bigint_cmp(a->bigint, b->bigint) == 0;
    if (a->tag == XS_BIGINT && b->tag == XS_INT)    return bigint_cmp_i64(a->bigint, b->i) == 0;
    if (a->tag == XS_INT    && b->tag == XS_BIGINT)  return bigint_cmp_i64(b->bigint, a->i) == 0;
    if (a->tag == XS_BIGINT && b->tag == XS_FLOAT)  return bigint_to_double(a->bigint) == b->f;
    if (a->tag == XS_FLOAT  && b->tag == XS_BIGINT) return a->f == bigint_to_double(b->bigint);
    if ((a->tag == XS_STR || a->tag == XS_CHAR) &&
        (b->tag == XS_STR || b->tag == XS_CHAR))
        return strcmp(a->s, b->s) == 0;
    /* structural equality for collections */
    if ((a->tag == XS_ARRAY || a->tag == XS_TUPLE) &&
        (b->tag == XS_ARRAY || b->tag == XS_TUPLE) &&
        a->tag == b->tag) {
        if (a->arr->len != b->arr->len) return 0;
        for (int i = 0; i < a->arr->len; i++) {
            if (!value_equal(a->arr->items[i], b->arr->items[i])) return 0;
        }
        return 1;
    }
    if (a->tag == XS_ENUM_VAL && b->tag == XS_ENUM_VAL) {
        if (strcmp(a->en->type_name, b->en->type_name) != 0) return 0;
        if (strcmp(a->en->variant,   b->en->variant)   != 0) return 0;
        if (a->en->arr_data && b->en->arr_data) {
            if (a->en->arr_data->len != b->en->arr_data->len) return 0;
            for (int i = 0; i < a->en->arr_data->len; i++) {
                if (!value_equal(a->en->arr_data->items[i],
                                 b->en->arr_data->items[i])) return 0;
            }
            return 1;
        }
        if (!a->en->arr_data && !b->en->arr_data &&
            !a->en->map_data && !b->en->map_data) return 1;
        return 0;
    }
    if (a->tag == XS_INST && b->tag == XS_INST) {
        if (a->inst->class_ != b->inst->class_) return 0;
        if (!a->inst->fields || !b->inst->fields) return a->inst->fields == b->inst->fields;
        int nk = 0; char **ks = map_keys(a->inst->fields, &nk);
        int eq = 1;
        for (int j = 0; j < nk; j++) {
            Value *av = map_get(a->inst->fields, ks[j]);
            Value *bv = map_get(b->inst->fields, ks[j]);
            if (!bv || !value_equal(av, bv)) eq = 0;
            free(ks[j]);
        }
        free(ks);
        return eq;
    }
    if (a->tag == XS_STRUCT_VAL && b->tag == XS_STRUCT_VAL) {
        if (strcmp(a->st->type_name, b->st->type_name) != 0) return 0;
        if (!a->st->fields || !b->st->fields) return a->st->fields == b->st->fields;
        if (a->st->fields->len != b->st->fields->len) return 0;
        for (int i = 0; i < a->st->fields->cap; i++) {
            if (!a->st->fields->keys[i]) continue;
            Value *bv = map_get(b->st->fields, a->st->fields->keys[i]);
            if (!value_equal(a->st->fields->vals[i], bv)) return 0;
        }
        return 1;
    }
    return 0;
}

int value_cmp(Value *a, Value *b) {
    if (!a || !b) return 0;
    if (a->tag == XS_INT && b->tag == XS_INT) {
        if (a->i < b->i) return -1;
        if (a->i > b->i) return  1;
        return 0;
    }
    if (a->tag == XS_BIGINT && b->tag == XS_BIGINT) return bigint_cmp(a->bigint, b->bigint);
    if (a->tag == XS_BIGINT && b->tag == XS_INT)    return bigint_cmp_i64(a->bigint, b->i);
    if (a->tag == XS_INT    && b->tag == XS_BIGINT)  return -bigint_cmp_i64(b->bigint, a->i);
    if (a->tag == XS_BIGINT && b->tag == XS_FLOAT) {
        double da = bigint_to_double(a->bigint);
        if (da < b->f) return -1;
        if (da > b->f) return  1;
        return 0;
    }
    if (a->tag == XS_FLOAT && b->tag == XS_BIGINT) {
        double db = bigint_to_double(b->bigint);
        if (a->f < db) return -1;
        if (a->f > db) return  1;
        return 0;
    }
    if (a->tag == XS_STR && b->tag == XS_STR) {
        return strcmp(a->s, b->s);
    }
    if ((a->tag == XS_INT || a->tag == XS_FLOAT) &&
        (b->tag == XS_INT || b->tag == XS_FLOAT)) {
        double fa = (a->tag == XS_FLOAT) ? a->f : (double)a->i;
        double fb = (b->tag == XS_FLOAT) ? b->f : (double)b->i;
        if (fa < fb) return -1;
        if (fa > fb) return  1;
        return 0;
    }
    /* fallback: compare by type tag */
    return (a->tag > b->tag) - (a->tag < b->tag);
}

Value *value_copy(Value *v) {
    if (!v) return xs_null();
    return value_incref(v);
}

XSArray *array_new(void) {
    XSArray *a = xs_calloc(1, sizeof(XSArray));
    a->refcount = 1;
    a->cap = 4;
    a->items = xs_malloc(sizeof(Value*) * a->cap);
    return a;
}

void array_push(XSArray *a, Value *v) {
    if (a->len >= a->cap) {
        a->cap *= 2;
        a->items = xs_realloc(a->items, sizeof(Value*) * a->cap);
    }
    a->items[a->len++] = value_incref(v);
}

Value *array_get(XSArray *a, int i) {
    if (!a) return xs_null();
    if (i < 0) i = a->len + i;
    if (i < 0 || i >= a->len) return xs_null();
    return a->items[i];
}

void array_free(XSArray *a) {
    if (!a) return;
    for (int i = 0; i < a->len; i++) value_decref(a->items[i]);
    free(a->items);
    free(a);
}

/* map — open addressing, string keys */
#define MAP_LOAD_MAX 0.7

XSMap *map_new(void) {
    XSMap *m = xs_calloc(1, sizeof(XSMap));
    m->refcount = 1;
    m->cap  = 8;
    m->keys = xs_calloc(m->cap, sizeof(char*));
    m->vals = xs_calloc(m->cap, sizeof(Value*));
    return m;
}

static uint32_t hash_str(const char *s) {
    uint32_t h = 5381;
    unsigned char c;
    while ((c = (unsigned char)*s++)) h = ((h << 5) + h) + c;
    return h;
}

static void map_grow(XSMap *m) {
    int   old_cap  = m->cap;
    char  **old_k  = m->keys;
    Value **old_v  = m->vals;
    m->cap  = old_cap * 2;
    m->keys = xs_calloc(m->cap, sizeof(char*));
    m->vals = xs_calloc(m->cap, sizeof(Value*));
    m->len  = 0;
    for (int i = 0; i < old_cap; i++) {
        if (!old_k[i]) continue;
        map_set(m, old_k[i], old_v[i]);
        free(old_k[i]);
        value_decref(old_v[i]);
    }
    free(old_k);
    free(old_v);
}

void map_set(XSMap *m, const char *k, Value *v) {
    if ((double)m->len / m->cap > MAP_LOAD_MAX) map_grow(m);
    uint32_t idx = hash_str(k) % (uint32_t)m->cap;
    while (m->keys[idx]) {
        if (strcmp(m->keys[idx], k) == 0) {
            value_decref(m->vals[idx]);
            m->vals[idx] = value_incref(v);
            return;
        }
        idx = (idx + 1) % (uint32_t)m->cap;
    }
    m->keys[idx] = xs_strdup(k);
    m->vals[idx] = value_incref(v);
    m->len++;
}

Value *map_get(XSMap *m, const char *k) {
    if (!m) return NULL;
    uint32_t idx = hash_str(k) % (uint32_t)m->cap;
    int tries = 0;
    while (m->keys[idx] && tries < m->cap) {
        if (strcmp(m->keys[idx], k) == 0) return m->vals[idx];
        idx = (idx + 1) % (uint32_t)m->cap;
        tries++;
    }
    return NULL;
}

int map_has(XSMap *m, const char *k) { return map_get(m, k) != NULL; }

void map_del(XSMap *m, const char *k) {
    if (!m) return;
    uint32_t idx = hash_str(k) % (uint32_t)m->cap;
    while (m->keys[idx]) {
        if (strcmp(m->keys[idx], k) == 0) {
            free(m->keys[idx]);
            value_decref(m->vals[idx]);
            m->keys[idx] = NULL;
            m->vals[idx] = NULL;
            m->len--;
            return;
        }
        idx = (idx + 1) % (uint32_t)m->cap;
    }
}

char **map_keys(XSMap *m, int *len_out) {
    if (!m) { *len_out = 0; return NULL; }
    char **ks = xs_malloc(sizeof(char*) * (m->len + 1));
    int n = 0;
    for (int i = 0; i < m->cap; i++)
        if (m->keys[i]) ks[n++] = xs_strdup(m->keys[i]);
    ks[n] = NULL;
    *len_out = n;
    return ks;
}

void map_free(XSMap *m) {
    if (!m) return;
    for (int i = 0; i < m->cap; i++) {
        if (!m->keys[i]) continue;
        free(m->keys[i]);
        value_decref(m->vals[i]);
    }
    free(m->keys);
    free(m->vals);
    free(m);
}

/* func */
XSFunc *func_new(const char *name, Node **params, int nparams, Node *body, Env *closure) {
    XSFunc *fn = xs_calloc(1, sizeof(XSFunc));
    fn->name     = name ? xs_strdup(name) : NULL;
    fn->params   = params;
    fn->nparams  = nparams;
    fn->body     = body;
    fn->closure  = closure ? env_incref(closure) : NULL;
    fn->refcount = 1;
    return fn;
}

XSFunc *func_new_ex(const char *name, Node **params, int nparams, Node *body, Env *closure,
                     Node **default_vals, int *variadic_flags) {
    XSFunc *fn = func_new(name, params, nparams, body, closure);
    fn->default_vals   = default_vals;
    fn->variadic_flags = variadic_flags;
    return fn;
}

void func_free(XSFunc *fn) {
    if (!fn) return;
    free(fn->name);
    free(fn->deprecated_msg);
    if (fn->closure) env_decref(fn->closure);
    free(fn->params);       /* body + default_vals owned by AST */
    free(fn->default_vals);
    free(fn->variadic_flags);
    if (fn->param_type_names) {
        for (int j = 0; j < fn->nparams; j++)
            free(fn->param_type_names[j]);
        free(fn->param_type_names);
    }
    free(fn->ret_type_name);
    free(fn);
}
