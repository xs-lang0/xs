#ifndef BYTECODE_H
#define BYTECODE_H
#include "core/xs.h"
#include <stdint.h>
#include <stddef.h>

typedef uint32_t Instruction;

#define INSTR_OPCODE(i)   ((Opcode)((i) & 0xFF))
#define INSTR_A(i)        (((i) >> 8)  & 0xFF)
#define INSTR_Bx(i)       (((i) >> 16) & 0xFFFF)
#define INSTR_sBx(i)      ((int16_t)(((i) >> 16) & 0xFFFF))
#define INSTR_B(i)        (((i) >> 16) & 0xFF)
#define INSTR_C(i)        (((i) >> 24) & 0xFF)
#define MAKE_A(op,a,bx)   ((Instruction)(op)|((Instruction)(a)<<8)|((Instruction)(bx)<<16))
#define MAKE_B(op,a,b,c)  ((Instruction)(op)|((Instruction)(a)<<8)|((Instruction)(b)<<16)|((Instruction)(c)<<24))

typedef enum {
    OP_NOP = 0,

    OP_PUSH_CONST,    /* Bx=const_idx */
    OP_PUSH_NULL,
    OP_PUSH_TRUE,
    OP_PUSH_FALSE,
    OP_POP,
    OP_DUP,

    OP_LOAD_LOCAL,    /* Bx=slot */
    OP_STORE_LOCAL,
    OP_LOAD_UPVALUE,  /* Bx=idx */
    OP_STORE_UPVALUE,
    OP_LOAD_GLOBAL,   /* Bx=name_const */
    OP_STORE_GLOBAL,

    // --- arithmetic
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_NEG, OP_NOT,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LTE, OP_GTE,
    OP_CONCAT,

    OP_MAKE_ARRAY,    /* C=count */
    OP_MAKE_TUPLE,
    OP_MAKE_MAP,      /* C=n_pairs */
    OP_INDEX_GET,
    OP_INDEX_SET,
    OP_LOAD_FIELD,    /* Bx=name_const */
    OP_STORE_FIELD,

    /* control */
    OP_JUMP,          /* sBx=offset */
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    OP_MAKE_RANGE,    /* A=inclusive */
    OP_ITER_LEN,
    OP_ITER_GET,

    // --- calls
    OP_METHOD_CALL,   /* A=argc Bx=name_const */
    OP_MAKE_CLOSURE,  /* Bx=inner_proto_index_const */
    OP_CALL,          /* C=argc */
    OP_TAIL_CALL,     /* C=argc */
    OP_RETURN,
    OP_SWAP,

    OP_BAND, OP_BOR, OP_BXOR, OP_BNOT, OP_SHL, OP_SHR,

    /* error handling */
    OP_THROW,
    OP_TRY_BEGIN,     /* sBx=offset to catch */
    OP_TRY_END,
    OP_CATCH,

    OP_TRACE_CALL,
    OP_TRACE_RETURN,
    OP_TRACE_STORE,
    OP_TRACE_IO,

    OP_AND, OP_OR,
    OP_SPREAD,
    OP_LOOP,          /* backward jump */

    OP_EFFECT_CALL,   /* A=argc, Bx=effect_op_name */
    OP_EFFECT_RESUME,
    OP_EFFECT_HANDLE, /* Bx=handler_info */

    OP_AWAIT,
    OP_YIELD,
    OP_SPAWN,

    /* OOP */
    OP_MAKE_CLASS,    /* A=nfields Bx=name_const */
    OP_MAKE_ENUM,     /* Bx=name_const */
    OP_MAKE_INST,     /* A=nargs Bx=class_name */
    OP_IMPL_METHOD,
    OP_INHERIT,

    OP_MAKE_MODULE,   /* Bx=name_const */
    OP_END_MODULE,
    OP_IMPORT,        /* Bx=name_const */
    OP_IMPORT_ITEM,   /* A=item_name Bx=module_name */

    OP_DEFER_PUSH,    /* Bx=offset to deferred code */
    OP_DEFER_RUN,

    OP_MAKE_ACTOR,    /* A=n_state_fields Bx=name_const */
    OP_SEND,

    OP_FLOOR_DIV,
    OP_SPACESHIP,     /* <=> */
    OP_OPT_CHAIN,     /* ?. */
    OP_NULL_COALESCE, /* ?? */
    OP_TRY_OP,        /* ? error propagation */
    OP_PIPE,          /* |> */
    OP_IN,
    OP_IS,
    OP_MAP_MERGE,

    OP__MAX
} Opcode;

typedef struct { int is_local; int index; } UVDesc;

typedef struct {
    Instruction *code;
    int          len, cap;
    Value      **consts;
    int          nconsts, cap_consts;
} XSChunk;

typedef struct XSProto XSProto;
struct XSProto {
    char       *name;
    int         arity;
    int         nlocals;
    XSChunk     chunk;
    UVDesc     *uv_descs;
    int         n_upvalues;
    XSProto   **inner;
    int         n_inner, cap_inner;
    int         refcount;
};

XSProto *proto_new(const char *name, int arity);
void     proto_free(XSProto *p);
int      chunk_write(XSChunk *c, Instruction i);
int      chunk_add_const(XSChunk *c, Value *v);
void     proto_dump(XSProto *p);

/* bytecode serialization (.xsc format) */
int      proto_write_file(XSProto *p, const char *path);
XSProto *proto_read_file(const char *path);

#endif /* BYTECODE_H */
