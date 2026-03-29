/* env.h */
#ifndef ENV_H
#define ENV_H

#include "core/xs.h"
#include "core/value.h"

typedef struct {
    char   *name;
    Value  *value;
    int     mutable;
} Binding;

/* reactive binding entry */
typedef struct {
    char  *name;
    Node  *expr;      /* AST expr to re-eval (shared, don't free) */
    Env   *env;       /* environment for re-eval */
    char **deps;
    int    ndeps;
} ReactiveBinding;

typedef struct Env Env;
struct Env {
    Env      *parent;
    Binding  *bindings;
    int       len, cap;
    int       refcount;

    /* reactive bindings (stored at root env) */
    ReactiveBinding *reactive_bindings;
    int              reactive_len, reactive_cap;
};

Env    *env_new(Env *parent);
Env    *env_incref(Env *e);
void    env_decref(Env *e);

void    env_define(Env *e, const char *name, Value *val, int mutable);
Value  *env_get(Env *e, const char *name);
int     env_set(Env *e, const char *name, Value *val);
int     env_has_local(Env *e, const char *name);

/* reactive binding support */
void env_add_reactive(Env *e, const char *name, Node *expr, Env *eval_env,
                      char **deps, int ndeps);
void env_notify_reactive(Env *e, const char *changed_name);
void env_set_reactive_interp(Interp *interp);

#endif /* ENV_H */
