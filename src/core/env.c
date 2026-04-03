#include "core/env.h"
#include "runtime/interp.h"
#include <string.h>
#include <stdlib.h>

/* reactive binding support */
static Interp *g_reactive_interp = NULL;
static int g_reactive_depth = 0;
#define MAX_REACTIVE_DEPTH 16

void env_set_reactive_interp(Interp *interp) {
    g_reactive_interp = interp;
}

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
    /* free reactive bindings */
    for (int i = 0; i < e->reactive_len; i++) {
        free(e->reactive_bindings[i].name);
        for (int j = 0; j < e->reactive_bindings[i].ndeps; j++)
            free(e->reactive_bindings[i].deps[j]);
        free(e->reactive_bindings[i].deps);
    }
    free(e->reactive_bindings);
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
        e->bindings = xs_realloc(e->bindings, e->cap * sizeof(Binding));
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
                env_notify_reactive(e, name);
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

int env_delete(Env *e, const char *name) {
    for (Env *cur = e; cur; cur = cur->parent) {
        for (int i = 0; i < cur->len; i++) {
            if (strcmp(cur->bindings[i].name, name) == 0) {
                free(cur->bindings[i].name);
                value_decref(cur->bindings[i].value);
                cur->bindings[i] = cur->bindings[cur->len - 1];
                cur->len--;
                return 1;
            }
        }
    }
    return 0;
}

/* find root env (walk up parents) */
static Env *env_root(Env *e) {
    while (e->parent) e = e->parent;
    return e;
}

void env_add_reactive(Env *e, const char *name, Node *expr, Env *eval_env,
                      char **deps, int ndeps) {
    Env *root = env_root(e);
    if (root->reactive_len >= root->reactive_cap) {
        root->reactive_cap = root->reactive_cap ? root->reactive_cap * 2 : 8;
        root->reactive_bindings = xs_realloc(root->reactive_bindings,
            root->reactive_cap * sizeof(ReactiveBinding));
    }
    ReactiveBinding *rb = &root->reactive_bindings[root->reactive_len++];
    rb->name = xs_strdup(name);
    rb->expr = expr;  /* shared, don't free */
    rb->env  = eval_env;
    rb->deps = deps;
    rb->ndeps = ndeps;
}

void env_notify_reactive(Env *e, const char *changed_name) {
    if (g_reactive_depth >= MAX_REACTIVE_DEPTH || !g_reactive_interp) return;
    Env *root = env_root(e);
    if (root->reactive_len == 0) return;

    g_reactive_depth++;
    for (int i = 0; i < root->reactive_len; i++) {
        ReactiveBinding *rb = &root->reactive_bindings[i];
        int found = 0;
        for (int j = 0; j < rb->ndeps; j++) {
            if (strcmp(rb->deps[j], changed_name) == 0) { found = 1; break; }
        }
        if (!found) continue;

        /* save and restore interp env */
        Interp *interp = g_reactive_interp;
        Env *saved_env = interp->env;
        interp->env = rb->env;
        Value *new_val = interp_eval(interp, rb->expr);
        interp->env = saved_env;

        /* update the bound variable directly (bypass env_set to avoid
           re-triggering for this same binding, then manually notify
           downstream dependents) */
        for (Env *cur = rb->env; cur; cur = cur->parent) {
            for (int k = 0; k < cur->len; k++) {
                if (strcmp(cur->bindings[k].name, rb->name) == 0) {
                    value_decref(cur->bindings[k].value);
                    cur->bindings[k].value = value_incref(new_val);
                    value_decref(new_val);
                    /* cascade: notify dependents of this binding */
                    env_notify_reactive(e, rb->name);
                    goto next_binding;
                }
            }
        }
        value_decref(new_val);
        next_binding:;
    }
    g_reactive_depth--;
}
