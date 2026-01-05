#include "effects/effects.h"
#include "runtime/interp.h"
#include "core/env.h"
#include "core/value.h"
#include "core/ast.h"
#include "core/xs.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

EffectStack *effect_stack_new(void) {
    EffectStack *es = calloc(1, sizeof(EffectStack));
    if (!es) return NULL;
    return es;
}

void effect_stack_free(EffectStack *es) {
    if (!es) return;
    while (es->top) {
        effect_stack_pop(es);
    }
    free(es);
}

void effect_stack_push(EffectStack *es, const char *effect, const char *op,
                       void *data) {
    if (!es) return;

    EffectHandler *h = calloc(1, sizeof(EffectHandler));
    if (!h) return;

    h->effect_name   = effect ? xs_strdup(effect) : NULL;
    h->op_name       = op     ? xs_strdup(op)     : NULL;
    h->handler_data  = data;
    h->next          = es->top;
    es->top          = h;
    es->depth++;
}

void effect_stack_push_full(EffectStack *es, const char *effect,
                            const char *op, Node *body, Env *env) {
    if (!es) return;

    EffectHandler *h = calloc(1, sizeof(EffectHandler));
    if (!h) return;

    h->effect_name   = effect ? xs_strdup(effect) : NULL;
    h->op_name       = op     ? xs_strdup(op)     : NULL;
    h->handler_body  = body;
    h->handler_env   = env;
    h->continuation  = continuation_new();
    h->next          = es->top;
    es->top          = h;
    es->depth++;
}

EffectHandler *effect_stack_find(EffectStack *es, const char *effect,
                                 const char *op) {
    if (!es) return NULL;

    for (EffectHandler *h = es->top; h; h = h->next) {
        int ematch = (!effect && !h->effect_name) ||
                     (effect && h->effect_name && strcmp(effect, h->effect_name) == 0);
        int omatch = (!op && !h->op_name) ||
                     (op && h->op_name && strcmp(op, h->op_name) == 0);
        if (ematch && omatch) {
            return h;
        }
    }
    return NULL;
}

void effect_stack_pop(EffectStack *es) {
    if (!es || !es->top) return;

    EffectHandler *h = es->top;
    es->top = h->next;
    es->depth--;

    free(h->effect_name);
    free(h->op_name);
    continuation_free(h->continuation);
    /* handler_env lifetime managed by caller via incref/decref */
    free(h);
}

void effect_stack_pop_n(EffectStack *es, int n) {
    for (int j = 0; j < n && es && es->top; j++) {
        effect_stack_pop(es);
    }
}

int effect_stack_depth(EffectStack *es) {
    return es ? es->depth : 0;
}

// perform / resume

Value *effect_perform(Interp *interp, const char *effect, const char *op,
                      Value **args, int argc) {
    if (!interp) {
        fprintf(stderr, "effect_perform: no interp (effect=%s, op=%s)\n",
                effect ? effect : "(null)", op ? op : "(null)");
        return NULL;
    }

    EffectFrame *frame = interp->effect_stack;
    while (frame) {
        if (frame->effect_name && effect &&
            strcmp(frame->effect_name, effect) == 0 &&
            frame->op_name && op &&
            strcmp(frame->op_name, op) == 0)
            break;
        frame = frame->prev;
    }

    if (!frame) {
        fprintf(stderr, "unhandled effect %s.%s\n",
                effect ? effect : "(null)", op ? op : "(null)");
        return NULL;
    }

    Env *handler_call_env = env_new(frame->handler_env);
    for (int j = 0; j < frame->params.len && j < argc; j++) {
        Param *pm = &frame->params.items[j];
        const char *pname = pm->name ? pm->name :
            (pm->pattern && pm->pattern->tag == NODE_PAT_IDENT ?
             pm->pattern->pat_ident.name : NULL);
        if (pname)
            env_define(handler_call_env, pname, args[j], 1);
    }

    Env *saved_env = interp->env;
    env_incref(saved_env);
    interp->env = env_incref(handler_call_env);

    Value *saved_resume = interp->resume_value;
    interp->resume_value = value_incref(XS_NULL_VAL);

    int saved_in_handler = interp->in_handler;
    interp->in_handler = 1;

    Value *body_result = interp_eval(interp, frame->handler_body);
    value_decref(body_result);

    if (interp->cf.signal == CF_RESUME)
        CF_CLEAR(interp);

    Value *resume_val = interp->resume_value;
    interp->resume_value = saved_resume;
    interp->in_handler   = saved_in_handler;

    env_decref(interp->env);
    interp->env = saved_env;
    env_decref(handler_call_env);

    return resume_val;
}

void effect_resume(Interp *interp, Value *value) {
    if (!interp) {
        fprintf(stderr, "effect_resume: no interp\n");
        return;
    }

    if (interp->resume_value)
        value_decref(interp->resume_value);
    interp->resume_value = value ? value : value_incref(XS_NULL_VAL);

    if (interp->cf.value)
        value_decref(interp->cf.value);
    interp->cf.signal = CF_RESUME;
    interp->cf.value  = NULL;
}

int effects_init(void)    { return 0; } /* per-interp allocation, nothing global to init */
void effects_cleanup(void) { }         /* symmetric with init */
