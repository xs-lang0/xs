#ifndef XS_EFFECTS_H
#define XS_EFFECTS_H

/* Algebraic effects: perform/handle/resume. */

#include "effects/continuation.h"

typedef struct Env   Env;
typedef struct Node  Node;
typedef struct Value Value;
typedef struct Interp Interp;


typedef struct EffectHandler EffectHandler;

struct EffectHandler {
    char            *effect_name;
    char            *op_name;
    void            *handler_data;
    Node            *handler_body;
    Env             *handler_env;
    XSContinuation  *continuation;
    EffectHandler   *next;
};


typedef struct EffectStack EffectStack;

struct EffectStack {
    EffectHandler *top;
    int            depth;
};

EffectStack    *effect_stack_new(void);
void            effect_stack_free(EffectStack *es);

void            effect_stack_push(EffectStack *es, const char *effect,
                                  const char *op, void *data);
void            effect_stack_push_full(EffectStack *es, const char *effect,
                                       const char *op, Node *body, Env *env);
EffectHandler  *effect_stack_find(EffectStack *es, const char *effect,
                                  const char *op);
void            effect_stack_pop(EffectStack *es);
void            effect_stack_pop_n(EffectStack *es, int n);
int             effect_stack_depth(EffectStack *es);

Value *effect_perform(Interp *interp, const char *effect, const char *op,
                      Value **args, int argc);
void effect_resume(Interp *interp, Value *value);

int  effects_init(void);
void effects_cleanup(void);

#endif /* XS_EFFECTS_H */
