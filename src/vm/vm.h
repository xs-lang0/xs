#ifndef VM_H
#define VM_H
#include "vm/bytecode.h"
#include "core/value.h"

#ifdef XSC_ENABLE_TRACER
#include "tracer/tracer.h"
#endif

#define VM_STACK_SIZE    4096 /* TODO: make these growable instead of fixed */
#define VM_FRAMES_MAX    256
#define VM_TRY_STACK_MAX 64
#define VM_DEFER_MAX     64
#define VM_YIELD_MAX     1024
#define VM_MAX_TASKS     64

typedef struct {
    Value *fn;
    Value *result;
    int    done;
} VMTask;

typedef struct {
    Instruction *catch_ip;
    Value      **stack_top;  /* sp at TRY_BEGIN (for unwinding) */
} TryEntry;

/* open upvalue points to stack slot; closed holds its own copy */
typedef struct Upvalue {
    Value          **ptr;
    Value           *closed_val;
    int              is_open;
    int              refcount;
    struct Upvalue  *next;
} Upvalue;

typedef struct {
    Instruction *defer_ip;
} DeferEntry;

/* call frame */
typedef struct {
    Value       *closure_val;
    Instruction *ip;
    Value      **base;
    TryEntry     try_stack[VM_TRY_STACK_MAX];
    int          try_depth;
    DeferEntry   defer_stack[VM_DEFER_MAX];
    int          defer_depth;
    Instruction *defer_return_ip;
    int          is_generator;
    Value       *yield_arr;
    int          yield_index;
} CallFrame;

typedef struct {
    CallFrame   frames[VM_FRAMES_MAX];
    int         frame_count;
    Value     **sp_offset; /* relative to stack base */
    int         valid;
} EffectCont;

typedef struct VM {
    Value      *stack[VM_STACK_SIZE];
    Value     **sp;
    CallFrame   frames[VM_FRAMES_MAX];
    int         frame_count;
    Upvalue    *open_upvalues;
    XSMap      *globals;
    int         main_called;
    Value      *init_inst;
    Value      *spawn_task;
    VMTask      tasks[VM_MAX_TASKS];
    int         n_tasks;
    EffectCont  eff_cont;
#ifdef XSC_ENABLE_TRACER
    XSTracer   *tracer;
#endif
} VM;

VM  *vm_new(void);
void vm_free(VM *vm);
int  vm_run(VM *vm, XSProto *proto);
void upvalue_close_all(Upvalue **list, Value **cutoff);

#endif /* VM_H */
