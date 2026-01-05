/* ir.h -- SSA IR for the XS compiler.
 * Three-address form sitting between AST and bytecode. */
#ifndef IR_H
#define IR_H

#include "core/ast.h"

/* IR types */
typedef enum {
    IRT_VOID,
    IRT_BOOL,
    IRT_I64,
    IRT_F64,
    IRT_STR,
    IRT_PTR,
    IRT_ARRAY,
    IRT_MAP,
    IRT_FUNC,
    IRT_ANY,
} IRType;

typedef enum {
    IR_CONST, IR_UNDEF,

    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD, IR_POW, IR_NEG,

    IR_BIT_AND, IR_BIT_OR, IR_BIT_XOR, IR_BIT_NOT, IR_SHL, IR_SHR,

    IR_EQ, IR_NEQ, IR_LT, IR_LE, IR_GT, IR_GE,

    IR_AND, IR_OR, IR_NOT,

    IR_ALLOC, IR_LOAD, IR_STORE, IR_LOAD_FIELD, IR_STORE_FIELD,

    IR_MAKE_ARRAY, IR_MAKE_MAP, IR_INDEX,

    IR_BR, IR_BR_IF, IR_RET,

    IR_CALL, IR_TAIL_CALL,

    IR_PHI,

    IR_STRCAT, IR_LOAD_GLOBAL,

    IR_PRINT, IR_CAST, IR_NOP, IR_UNREACHABLE,

    IR_OPCODE_COUNT
} IROpcode;

/* Operand: virtual register or constant literal */
typedef enum {
    IROP_REG,
    IROP_CONST_INT,
    IROP_CONST_FLOAT,
    IROP_CONST_BOOL,
    IROP_CONST_STR,
    IROP_CONST_NULL,
    IROP_NONE,
} IROperandKind;

typedef struct {
    IROperandKind kind;
    union {
        int     reg;
        int64_t ival;
        double  fval;
        int     bval;
        char   *sval;   /* owned */
    };
} IROperand;

static inline IROperand irop_reg(int r) {
    IROperand o = {IROP_REG, {0}};
    o.reg = r;
    return o;
}
static inline IROperand irop_int(int64_t v) {
    IROperand o = {IROP_CONST_INT, {0}};
    o.ival = v;
    return o;
}
static inline IROperand irop_float(double v) {
    IROperand o = {IROP_CONST_FLOAT, {0}};
    o.fval = v;
    return o;
}
static inline IROperand irop_bool(int v) {
    IROperand o = {IROP_CONST_BOOL, {0}};
    o.bval = v;
    return o;
}
static inline IROperand irop_str(const char *s) {
    IROperand o = {IROP_CONST_STR, {0}};
    o.sval = xs_strdup(s);
    return o;
}
static inline IROperand irop_null(void) {
    IROperand o = {IROP_CONST_NULL, {0}};
    return o;
}
static inline IROperand irop_none(void) {
    IROperand o = {IROP_NONE, {0}};
    return o;
}

typedef struct {
    IROperand operand;
    int       block_id;
} IRPhiEntry;

/* Instruction */
#define IR_MAX_OPERANDS 4

typedef struct {
    IROpcode    opcode;
    int         dest;       /* -1 if none */
    IRType      type;
    IROperand   ops[IR_MAX_OPERANDS];
    int         nops;

    IROperand  *extra_ops;  /* heap-alloc'd for CALL args, MAKE_ARRAY, etc */
    int         nextra;

    IRPhiEntry *phi_entries;
    int         nphi;

    int         target_block;
    int         false_block;

    char       *label;      /* fn name, field name, etc */
    Span        span;
} IRInstr;

typedef struct {
    char      *label;
    int        id;
    IRInstr   *instrs;
    int        ninstr, cap_instr;
    int       *preds;
    int        npred, cap_pred;
    int       *succs;
    int        nsucc, cap_succ;
} IRBlock;

typedef struct {
    char     *name;
    char    **param_names;
    IRType   *param_types;
    int       nparam;
    IRType    ret_type;
    IRBlock **blocks;
    int       nblock, cap_block;
    int       next_reg;
    int       is_external;
} IRFunc;

typedef struct {
    char     *name;
    IRFunc  **funcs;
    int       nfunc, cap_func;
    struct {
        char   *name;
        IRType  type;
        int     is_const;
    } *globals;
    int  nglobal, cap_global;
} IRModule;

/* Builder API */

IRModule *ir_module_new(const char *name);
void      ir_module_free(IRModule *m);

IRFunc   *ir_func_new(const char *name, IRType ret_type);
void      ir_func_free(IRFunc *fn);
void      ir_module_add_func(IRModule *m, IRFunc *fn);
void      ir_func_add_param(IRFunc *fn, const char *name, IRType type);

IRBlock  *ir_block_new(IRFunc *fn, const char *label);
void      ir_block_free(IRBlock *b);

int       ir_new_reg(IRFunc *fn);

int       ir_emit_const_int(IRBlock *b, IRFunc *fn, int64_t val);
int       ir_emit_const_float(IRBlock *b, IRFunc *fn, double val);
int       ir_emit_const_bool(IRBlock *b, IRFunc *fn, int val);
int       ir_emit_const_str(IRBlock *b, IRFunc *fn, const char *val);
int       ir_emit_const_null(IRBlock *b, IRFunc *fn);

int       ir_emit_binop(IRBlock *b, IRFunc *fn, IROpcode op, IROperand l, IROperand r, IRType t);
int       ir_emit_unary(IRBlock *b, IRFunc *fn, IROpcode op, IROperand src, IRType t);

int       ir_emit_load(IRBlock *b, IRFunc *fn, IROperand ptr, IRType t);
int       ir_emit_load_global(IRBlock *b, IRFunc *fn, const char *name, IRType t);
void      ir_emit_store(IRBlock *b, IROperand val, IROperand ptr);

int       ir_emit_load_field(IRBlock *b, IRFunc *fn, IROperand obj, const char *field, IRType t);
void      ir_emit_store_field(IRBlock *b, IROperand obj, const char *field, IROperand val);

int       ir_emit_alloc(IRBlock *b, IRFunc *fn, IRType t);
int       ir_emit_index(IRBlock *b, IRFunc *fn, IROperand obj, IROperand idx, IRType t);

int       ir_emit_make_array(IRBlock *b, IRFunc *fn, IROperand *elems, int nelems);
int       ir_emit_make_map(IRBlock *b, IRFunc *fn, IROperand *keys, IROperand *vals, int nentries);

int       ir_emit_call(IRBlock *b, IRFunc *fn, const char *callee, IROperand *args, int nargs, IRType ret_t);
int       ir_emit_tail_call(IRBlock *b, IRFunc *fn, const char *callee, IROperand *args, int nargs, IRType ret_t);

void      ir_emit_br(IRBlock *b, int target_block_id);
void      ir_emit_br_if(IRBlock *b, IROperand cond, int true_block, int false_block);
void      ir_emit_ret(IRBlock *b, IROperand val);
void      ir_emit_ret_void(IRBlock *b);

int       ir_emit_phi(IRBlock *b, IRFunc *fn, IRType t, IRPhiEntry *entries, int nentries);

void      ir_emit_print(IRBlock *b, IROperand val);
int       ir_emit_cast(IRBlock *b, IRFunc *fn, IROperand val, IRType target_type);
void      ir_emit_nop(IRBlock *b);
void      ir_emit_unreachable(IRBlock *b);

int       ir_block_is_terminated(const IRBlock *b);
void      ir_link_blocks(IRBlock *from, IRBlock *to);

void      ir_dump(const IRModule *m, FILE *out);
void      ir_dump_func(const IRFunc *fn, FILE *out);
void      ir_dump_block(const IRBlock *b, FILE *out);

IRModule *ir_lower(Node *program);

#endif /* IR_H */
