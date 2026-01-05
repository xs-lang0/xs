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

typedef struct Env Env;
struct Env {
    Env      *parent;
    Binding  *bindings;
    int       len, cap;
    int       refcount;
};

Env    *env_new(Env *parent);
Env    *env_incref(Env *e);
void    env_decref(Env *e);

void    env_define(Env *e, const char *name, Value *val, int mutable);
Value  *env_get(Env *e, const char *name);
int     env_set(Env *e, const char *name, Value *val);
int     env_has_local(Env *e, const char *name);

#endif /* ENV_H */
