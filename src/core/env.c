#include "core/env.h"
#include <string.h>
#include <stdlib.h>

Env *env_new(Env *parent) {
    Env *e = xs_calloc(1, sizeof(Env));
    e->parent   = parent ? env_incref(parent) : NULL;
    e->refcount = 1;
    return e;
}

Env *env_incref(Env *e) {
    if (e) e->refcount++;
    return e;
}

void env_decref(Env *e) {
    if (!e) return;
    if (--e->refcount > 0) return;
    for (int i = 0; i < e->len; i++) {
        free(e->bindings[i].name);
        value_decref(e->bindings[i].value);
    }
    free(e->bindings);
    if (e->parent) env_decref(e->parent);
    free(e);
}

void env_define(Env *e, const char *name, Value *val, int mutable) {
    for (int i = 0; i < e->len; i++) {
        if (strcmp(e->bindings[i].name, name) == 0) {
            Value *old = e->bindings[i].value;
            e->bindings[i].value   = value_incref(val); /* incref first */
            e->bindings[i].mutable = mutable;
            value_decref(old); /* then decref old (handles old==val case) */
            return;
        }
    }
    if (e->len >= e->cap) {
        e->cap = e->cap ? e->cap * 2 : 8;
        e->bindings = xs_realloc(e->bindings, e->cap * sizeof(Binding)); /* FIXME: leaks old ptr if realloc fails */
    }
    e->bindings[e->len].name    = xs_strdup(name);
    e->bindings[e->len].value   = value_incref(val);
    e->bindings[e->len].mutable = mutable;
    e->len++;
}

Value *env_get(Env *e, const char *name) {
    for (Env *cur = e; cur; cur = cur->parent) {
        for (int i = 0; i < cur->len; i++) {
            if (strcmp(cur->bindings[i].name, name) == 0)
                return cur->bindings[i].value;
        }
    }
    return NULL;
}

int env_set(Env *e, const char *name, Value *val) {
    for (Env *cur = e; cur; cur = cur->parent) {
        for (int i = 0; i < cur->len; i++) {
            if (strcmp(cur->bindings[i].name, name) == 0) {
                if (!cur->bindings[i].mutable) return -2; /* immutable */
                value_decref(cur->bindings[i].value);
                cur->bindings[i].value = value_incref(val);
                return 0;
            }
        }
    }
    return -1; /* not found */
}

int env_has_local(Env *e, const char *name) {
    for (int i = 0; i < e->len; i++)
        if (strcmp(e->bindings[i].name, name) == 0) return 1;
    return 0;
}
