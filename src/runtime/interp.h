// interp.h -- tree-walking interpreter
#ifndef INTERP_H
#define INTERP_H

#include "core/xs.h"
#include "core/value.h"
#include "core/env.h"
#include "core/ast.h"
#include "coverage/coverage.h"
#include "diagnostic/diagnostic.h"

typedef struct {
    const char *func_name;
    Span call_span;
} InterpFrame;

/* control flow */
typedef struct {
    int    signal;
    Value *value;
    char  *label;
} CFResult;

typedef struct EffectFrame {
    char             *effect_name;
    char             *op_name;
    ParamList         params;
    Node             *handler_body;
    Env              *handler_env;
    struct EffectFrame *prev;
} EffectFrame;

#define CF_RESULT_OK    { 0, NULL }
#define CF_CLEAR(i)  do { (i)->cf.signal=0; value_decref((i)->cf.value); (i)->cf.value=NULL; free((i)->cf.label); (i)->cf.label=NULL; } while(0)

typedef struct Interp Interp;
struct Interp {
    Env    *globals;
    Env    *env;
    CFResult cf;
    int      max_depth;
    int      depth;
    const char *filename;

    struct { Node **items; int len, cap; } defers;
    Value  *yield_collect;

    /* effects */
    EffectFrame *effect_stack;
    Value       *resume_value;
    int          in_handler;

    Value       *nursery_queue;

    /* cooperative task queue */
    struct { Value *fn; Value *result; int done; } task_queue[64];
    int          n_tasks;

    Span         current_span;
    XSCoverage  *coverage;

    /* tail call trampoline */
    Value       *tc_callee;
    Value      **tc_args;
    int          tc_argc;

    InterpFrame *call_stack;
    int          call_stack_len;
    int          call_stack_cap;
    DiagContext  *diag;

    /* phase 2: source kept alive for plugin re-parse */
    const char  *source;
    int          needs_reparse;
};

Interp *interp_new(const char *filename);
void    interp_free(Interp *i);
void    interp_run(Interp *i, Node *program);
void    interp_exec(Interp *i, Node *stmt);

/* caller does NOT own the refcount; incref to keep */
Value  *interp_eval(Interp *i, Node *expr);

Value  *call_value(Interp *i, Value *callee, Value **args, int argc, const char *label);
void    interp_define_native(Interp *i, const char *name, NativeFn fn);
void    stdlib_register(Interp *i);

#endif
