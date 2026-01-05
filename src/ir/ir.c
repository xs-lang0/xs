#include "ir/ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void push_instr(IRBlock *b, IRInstr instr) {
    if (b->ninstr >= b->cap_instr) {
        b->cap_instr = b->cap_instr ? b->cap_instr * 2 : 16;
        b->instrs = xs_realloc(b->instrs, b->cap_instr * sizeof(IRInstr));
    }
    b->instrs[b->ninstr++] = instr;
}

static IRInstr instr_new(IROpcode op) {
    IRInstr i;
    memset(&i, 0, sizeof(i));
    i.opcode = op;
    i.dest = -1;
    i.type = IRT_ANY;
    for (int k = 0; k < IR_MAX_OPERANDS; k++)
        i.ops[k] = irop_none();
    i.target_block = -1;
    i.false_block  = -1;
    return i;
}


IRModule *ir_module_new(const char *name) {
    IRModule *m = xs_calloc(1, sizeof(IRModule));
    m->name = xs_strdup(name);
    return m;
}

void ir_module_free(IRModule *m) {
    if (!m) return;
    free(m->name);
    for (int i = 0; i < m->nfunc; i++)
        ir_func_free(m->funcs[i]);
    free(m->funcs);
    for (int i = 0; i < m->nglobal; i++)
        free(m->globals[i].name);
    free(m->globals);
    free(m);
}

void ir_module_add_func(IRModule *m, IRFunc *fn) {
    if (m->nfunc >= m->cap_func) {
        m->cap_func = m->cap_func ? m->cap_func * 2 : 8;
        m->funcs = xs_realloc(m->funcs, m->cap_func * sizeof(IRFunc*));
    }
    m->funcs[m->nfunc++] = fn;
}

/* function */

IRFunc *ir_func_new(const char *name, IRType ret_type) {
    IRFunc *fn = xs_calloc(1, sizeof(IRFunc));
    fn->name = xs_strdup(name);
    fn->ret_type = ret_type;
    return fn;
}

void ir_func_free(IRFunc *fn) {
    if (!fn) return;
    free(fn->name);
    for (int i = 0; i < fn->nparam; i++)
        free(fn->param_names[i]);
    free(fn->param_names);
    free(fn->param_types);
    for (int i = 0; i < fn->nblock; i++)
        ir_block_free(fn->blocks[i]);
    free(fn->blocks);
    free(fn);
}

void ir_func_add_param(IRFunc *fn, const char *name, IRType type) {
    int idx = fn->nparam;
    fn->nparam++;
    fn->param_names = xs_realloc(fn->param_names, fn->nparam * sizeof(char*));
    fn->param_types = xs_realloc(fn->param_types, fn->nparam * sizeof(IRType));
    fn->param_names[idx] = xs_strdup(name);
    fn->param_types[idx] = type;
}

IRBlock *ir_block_new(IRFunc *fn, const char *label) {
    IRBlock *b = xs_calloc(1, sizeof(IRBlock));
    b->label = xs_strdup(label ? label : "bb");
    b->id = fn->nblock;
    if (fn->nblock >= fn->cap_block) {
        fn->cap_block = fn->cap_block ? fn->cap_block * 2 : 8;
        fn->blocks = xs_realloc(fn->blocks, fn->cap_block * sizeof(IRBlock*));
    }
    fn->blocks[fn->nblock++] = b;
    return b;
}

void ir_block_free(IRBlock *b) {
    if (!b) return;
    free(b->label);
    for (int i = 0; i < b->ninstr; i++) {
        IRInstr *instr = &b->instrs[i];
        for (int k = 0; k < IR_MAX_OPERANDS; k++) {
            if (instr->ops[k].kind == IROP_CONST_STR)
                free(instr->ops[k].sval);
        }
        if (instr->extra_ops) {
            for (int k = 0; k < instr->nextra; k++) {
                if (instr->extra_ops[k].kind == IROP_CONST_STR)
                    free(instr->extra_ops[k].sval);
            }
            free(instr->extra_ops);
        }
        if (instr->phi_entries) {
            for (int k = 0; k < instr->nphi; k++) {
                if (instr->phi_entries[k].operand.kind == IROP_CONST_STR)
                    free(instr->phi_entries[k].operand.sval);
            }
            free(instr->phi_entries);
        }
        free(instr->label);
    }
    free(b->instrs);
    free(b->preds);
    free(b->succs);
    free(b);
}

int ir_new_reg(IRFunc *fn) {
    return fn->next_reg++;
}

int ir_block_is_terminated(const IRBlock *b) {
    if (b->ninstr == 0) return 0;
    IROpcode last = b->instrs[b->ninstr - 1].opcode;
    return last == IR_BR || last == IR_BR_IF || last == IR_RET ||
           last == IR_UNREACHABLE;
}

void ir_link_blocks(IRBlock *from, IRBlock *to) {
    if (from->nsucc >= from->cap_succ) {
        from->cap_succ = from->cap_succ ? from->cap_succ * 2 : 4;
        from->succs = xs_realloc(from->succs, from->cap_succ * sizeof(int));
    }
    from->succs[from->nsucc++] = to->id;
    if (to->npred >= to->cap_pred) {
        to->cap_pred = to->cap_pred ? to->cap_pred * 2 : 4;
        to->preds = xs_realloc(to->preds, to->cap_pred * sizeof(int));
    }
    to->preds[to->npred++] = from->id;
}

// emit helpers

int ir_emit_const_int(IRBlock *b, IRFunc *fn, int64_t val) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_CONST);
    i.dest = dest;
    i.type = IRT_I64;
    i.ops[0] = irop_int(val);
    i.nops = 1;
    push_instr(b, i);
    return dest;
}

int ir_emit_const_float(IRBlock *b, IRFunc *fn, double val) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_CONST);
    i.dest = dest;
    i.type = IRT_F64;
    i.ops[0] = irop_float(val);
    i.nops = 1;
    push_instr(b, i);
    return dest;
}

int ir_emit_const_bool(IRBlock *b, IRFunc *fn, int val) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_CONST);
    i.dest = dest;
    i.type = IRT_BOOL;
    i.ops[0] = irop_bool(val);
    i.nops = 1;
    push_instr(b, i);
    return dest;
}

int ir_emit_const_str(IRBlock *b, IRFunc *fn, const char *val) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_CONST);
    i.dest = dest;
    i.type = IRT_STR;
    i.ops[0] = irop_str(val);
    i.nops = 1;
    push_instr(b, i);
    return dest;
}

int ir_emit_const_null(IRBlock *b, IRFunc *fn) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_CONST);
    i.dest = dest;
    i.type = IRT_ANY;
    i.ops[0] = irop_null();
    i.nops = 1;
    push_instr(b, i);
    return dest;
}

int ir_emit_binop(IRBlock *b, IRFunc *fn, IROpcode op, IROperand l, IROperand r, IRType t) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(op);
    i.dest = dest;
    i.type = t;
    i.ops[0] = l;
    i.ops[1] = r;
    i.nops = 2;
    push_instr(b, i);
    return dest;
}

int ir_emit_unary(IRBlock *b, IRFunc *fn, IROpcode op, IROperand src, IRType t) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(op);
    i.dest = dest;
    i.type = t;
    i.ops[0] = src;
    i.nops = 1;
    push_instr(b, i);
    return dest;
}

int ir_emit_load(IRBlock *b, IRFunc *fn, IROperand ptr, IRType t) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_LOAD);
    i.dest = dest;
    i.type = t;
    i.ops[0] = ptr;
    i.nops = 1;
    push_instr(b, i);
    return dest;
}

void ir_emit_store(IRBlock *b, IROperand val, IROperand ptr) {
    IRInstr i = instr_new(IR_STORE);
    i.ops[0] = val;
    i.ops[1] = ptr;
    i.nops = 2;
    push_instr(b, i);
}

int ir_emit_load_global(IRBlock *b, IRFunc *fn, const char *name, IRType t) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_LOAD_GLOBAL);
    i.dest = dest;
    i.type = t;
    i.label = xs_strdup(name);
    push_instr(b, i);
    return dest;
}

int ir_emit_load_field(IRBlock *b, IRFunc *fn, IROperand obj, const char *field, IRType t) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_LOAD_FIELD);
    i.dest = dest;
    i.type = t;
    i.ops[0] = obj;
    i.nops = 1;
    i.label = xs_strdup(field);
    push_instr(b, i);
    return dest;
}

void ir_emit_store_field(IRBlock *b, IROperand obj, const char *field, IROperand val) {
    IRInstr i = instr_new(IR_STORE_FIELD);
    i.ops[0] = obj;
    i.ops[1] = val;
    i.nops = 2;
    i.label = xs_strdup(field);
    push_instr(b, i);
}

int ir_emit_alloc(IRBlock *b, IRFunc *fn, IRType t) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_ALLOC);
    i.dest = dest;
    i.type = t;
    push_instr(b, i);
    return dest;
}

int ir_emit_index(IRBlock *b, IRFunc *fn, IROperand obj, IROperand idx, IRType t) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_INDEX);
    i.dest = dest;
    i.type = t;
    i.ops[0] = obj;
    i.ops[1] = idx;
    i.nops = 2;
    push_instr(b, i);
    return dest;
}

int ir_emit_make_array(IRBlock *b, IRFunc *fn, IROperand *elems, int nelems) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_MAKE_ARRAY);
    i.dest = dest;
    i.type = IRT_ARRAY;
    if (nelems > 0) {
        i.extra_ops = xs_malloc(nelems * sizeof(IROperand));
        memcpy(i.extra_ops, elems, nelems * sizeof(IROperand));
        i.nextra = nelems;
    }
    push_instr(b, i);
    return dest;
}

int ir_emit_make_map(IRBlock *b, IRFunc *fn, IROperand *keys, IROperand *vals, int nentries) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_MAKE_MAP);
    i.dest = dest;
    i.type = IRT_MAP;
    /* keys and vals interleaved */
    if (nentries > 0) {
        i.nextra = nentries * 2;
        i.extra_ops = xs_malloc(i.nextra * sizeof(IROperand));
        for (int k = 0; k < nentries; k++) {
            i.extra_ops[k * 2]     = keys[k];
            i.extra_ops[k * 2 + 1] = vals[k];
        }
    }
    push_instr(b, i);
    return dest;
}

int ir_emit_call(IRBlock *b, IRFunc *fn, const char *callee,
                 IROperand *args, int nargs, IRType ret_t) {
    int dest = -1;
    if (ret_t != IRT_VOID)
        dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_CALL);
    i.dest = dest;
    i.type = ret_t;
    i.label = xs_strdup(callee);
    if (nargs > 0) {
        i.extra_ops = xs_malloc(nargs * sizeof(IROperand));
        memcpy(i.extra_ops, args, nargs * sizeof(IROperand));
        i.nextra = nargs;
    }
    push_instr(b, i);
    return dest;
}

int ir_emit_tail_call(IRBlock *b, IRFunc *fn, const char *callee,
                      IROperand *args, int nargs, IRType ret_t) {
    int dest = -1;
    if (ret_t != IRT_VOID)
        dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_TAIL_CALL);
    i.dest = dest;
    i.type = ret_t;
    i.label = xs_strdup(callee);
    if (nargs > 0) {
        i.extra_ops = xs_malloc(nargs * sizeof(IROperand));
        memcpy(i.extra_ops, args, nargs * sizeof(IROperand));
        i.nextra = nargs;
    }
    push_instr(b, i);
    return dest;
}

void ir_emit_br(IRBlock *b, int target_block_id) {
    IRInstr i = instr_new(IR_BR);
    i.target_block = target_block_id;
    push_instr(b, i);
}

void ir_emit_br_if(IRBlock *b, IROperand cond, int true_block, int false_block) {
    IRInstr i = instr_new(IR_BR_IF);
    i.ops[0] = cond;
    i.nops = 1;
    i.target_block = true_block;
    i.false_block  = false_block;
    push_instr(b, i);
}

void ir_emit_ret(IRBlock *b, IROperand val) {
    IRInstr i = instr_new(IR_RET);
    i.ops[0] = val;
    i.nops = 1;
    push_instr(b, i);
}

void ir_emit_ret_void(IRBlock *b) {
    IRInstr i = instr_new(IR_RET);
    i.nops = 0;
    push_instr(b, i);
}

int ir_emit_phi(IRBlock *b, IRFunc *fn, IRType t,
                IRPhiEntry *entries, int nentries) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_PHI);
    i.dest = dest;
    i.type = t;
    if (nentries > 0) {
        i.phi_entries = xs_malloc(nentries * sizeof(IRPhiEntry));
        memcpy(i.phi_entries, entries, nentries * sizeof(IRPhiEntry));
        i.nphi = nentries;
    }
    push_instr(b, i);
    return dest;
}

void ir_emit_print(IRBlock *b, IROperand val) {
    IRInstr i = instr_new(IR_PRINT);
    i.ops[0] = val;
    i.nops = 1;
    push_instr(b, i);
}

int ir_emit_cast(IRBlock *b, IRFunc *fn, IROperand val, IRType target_type) {
    int dest = ir_new_reg(fn);
    IRInstr i = instr_new(IR_CAST);
    i.dest = dest;
    i.type = target_type;
    i.ops[0] = val;
    i.nops = 1;
    push_instr(b, i);
    return dest;
}

void ir_emit_nop(IRBlock *b) {
    IRInstr i = instr_new(IR_NOP);
    push_instr(b, i);
}

void ir_emit_unreachable(IRBlock *b) {
    IRInstr i = instr_new(IR_UNREACHABLE);
    push_instr(b, i);
}

/* printer */

static const char *opcode_name(IROpcode op) {
    switch (op) {
    case IR_CONST:       return "const";
    case IR_UNDEF:       return "undef";
    case IR_ADD:         return "add";
    case IR_SUB:         return "sub";
    case IR_MUL:         return "mul";
    case IR_DIV:         return "div";
    case IR_MOD:         return "mod";
    case IR_POW:         return "pow";
    case IR_NEG:         return "neg";
    case IR_BIT_AND:     return "bit_and";
    case IR_BIT_OR:      return "bit_or";
    case IR_BIT_XOR:     return "bit_xor";
    case IR_BIT_NOT:     return "bit_not";
    case IR_SHL:         return "shl";
    case IR_SHR:         return "shr";
    case IR_EQ:          return "eq";
    case IR_NEQ:         return "neq";
    case IR_LT:          return "lt";
    case IR_LE:          return "le";
    case IR_GT:          return "gt";
    case IR_GE:          return "ge";
    case IR_AND:         return "and";
    case IR_OR:          return "or";
    case IR_NOT:         return "not";
    case IR_ALLOC:       return "alloc";
    case IR_LOAD:        return "load";
    case IR_STORE:       return "store";
    case IR_LOAD_FIELD:  return "load_field";
    case IR_STORE_FIELD: return "store_field";
    case IR_MAKE_ARRAY:  return "make_array";
    case IR_MAKE_MAP:    return "make_map";
    case IR_INDEX:       return "index";
    case IR_BR:          return "br";
    case IR_BR_IF:       return "br_if";
    case IR_RET:         return "ret";
    case IR_CALL:        return "call";
    case IR_TAIL_CALL:   return "tail_call";
    case IR_PHI:         return "phi";
    case IR_STRCAT:      return "strcat";
    case IR_LOAD_GLOBAL: return "load_global";
    case IR_PRINT:       return "print";
    case IR_CAST:        return "cast";
    case IR_NOP:         return "nop";
    case IR_UNREACHABLE: return "unreachable";
    default:             return "???";
    }
}

static const char *type_name(IRType t) {
    switch (t) {
    case IRT_VOID:  return "void";
    case IRT_BOOL:  return "bool";
    case IRT_I64:   return "i64";
    case IRT_F64:   return "f64";
    case IRT_STR:   return "str";
    case IRT_PTR:   return "ptr";
    case IRT_ARRAY: return "array";
    case IRT_MAP:   return "map";
    case IRT_FUNC:  return "func";
    case IRT_ANY:   return "any";
    default:        return "?";
    }
}

static void print_operand(const IROperand *op, FILE *out) {
    switch (op->kind) {
    case IROP_REG:        fprintf(out, "%%%d", op->reg); break;
    case IROP_CONST_INT:  fprintf(out, "%lld", (long long)op->ival); break;
    case IROP_CONST_FLOAT:fprintf(out, "%g", op->fval); break;
    case IROP_CONST_BOOL: fprintf(out, "%s", op->bval ? "true" : "false"); break;
    case IROP_CONST_STR:  fprintf(out, "\"%s\"", op->sval ? op->sval : ""); break;
    case IROP_CONST_NULL: fprintf(out, "null"); break;
    case IROP_NONE:       break;
    }
}

void ir_dump_block(const IRBlock *b, FILE *out) {
    fprintf(out, "  %s:  ; block %d", b->label, b->id);
    if (b->npred > 0) {
        fprintf(out, "  preds=[");
        for (int i = 0; i < b->npred; i++) {
            if (i) fprintf(out, ",");
            fprintf(out, "%d", b->preds[i]);
        }
        fprintf(out, "]");
    }
    fprintf(out, "\n");

    for (int i = 0; i < b->ninstr; i++) {
        const IRInstr *instr = &b->instrs[i];
        fprintf(out, "    ");

        /* dest = */
        if (instr->dest >= 0)
            fprintf(out, "%%%d:%s = ", instr->dest, type_name(instr->type));

        fprintf(out, "%s", opcode_name(instr->opcode));

        /* Print operands */
        switch (instr->opcode) {
        case IR_CONST:
            fprintf(out, " ");
            print_operand(&instr->ops[0], out);
            break;

        case IR_LOAD_GLOBAL:
            fprintf(out, " @%s", instr->label ? instr->label : "?");
            break;

        case IR_BR:
            fprintf(out, " bb%d", instr->target_block);
            break;

        case IR_BR_IF:
            fprintf(out, " ");
            print_operand(&instr->ops[0], out);
            fprintf(out, ", bb%d, bb%d", instr->target_block, instr->false_block);
            break;

        case IR_RET:
            if (instr->nops > 0) {
                fprintf(out, " ");
                print_operand(&instr->ops[0], out);
            }
            break;

        case IR_CALL:
        case IR_TAIL_CALL:
            fprintf(out, " @%s(", instr->label ? instr->label : "?");
            for (int k = 0; k < instr->nextra; k++) {
                if (k) fprintf(out, ", ");
                print_operand(&instr->extra_ops[k], out);
            }
            fprintf(out, ")");
            break;

        case IR_PHI:
            fprintf(out, " ");
            for (int k = 0; k < instr->nphi; k++) {
                if (k) fprintf(out, ", ");
                fprintf(out, "[");
                print_operand(&instr->phi_entries[k].operand, out);
                fprintf(out, ", bb%d]", instr->phi_entries[k].block_id);
            }
            break;

        case IR_LOAD_FIELD:
        case IR_STORE_FIELD:
            for (int k = 0; k < instr->nops; k++) {
                fprintf(out, " ");
                print_operand(&instr->ops[k], out);
            }
            if (instr->label)
                fprintf(out, " .%s", instr->label);
            break;

        case IR_MAKE_ARRAY:
            fprintf(out, " [");
            for (int k = 0; k < instr->nextra; k++) {
                if (k) fprintf(out, ", ");
                print_operand(&instr->extra_ops[k], out);
            }
            fprintf(out, "]");
            break;

        case IR_MAKE_MAP:
            fprintf(out, " {");
            for (int k = 0; k < instr->nextra; k += 2) {
                if (k) fprintf(out, ", ");
                print_operand(&instr->extra_ops[k], out);
                fprintf(out, ": ");
                if (k + 1 < instr->nextra)
                    print_operand(&instr->extra_ops[k + 1], out);
            }
            fprintf(out, "}");
            break;

        default:
            for (int k = 0; k < instr->nops; k++) {
                if (instr->ops[k].kind == IROP_NONE) continue;
                fprintf(out, " ");
                print_operand(&instr->ops[k], out);
                if (k < instr->nops - 1) fprintf(out, ",");
            }
            break;
        }
        fprintf(out, "\n");
    }
}

void ir_dump_func(const IRFunc *fn, FILE *out) {
    fprintf(out, "fn @%s(", fn->name);
    for (int i = 0; i < fn->nparam; i++) {
        if (i) fprintf(out, ", ");
        fprintf(out, "%%%s:%s", fn->param_names[i], type_name(fn->param_types[i]));
    }
    fprintf(out, ") -> %s {\n", type_name(fn->ret_type));

    for (int i = 0; i < fn->nblock; i++)
        ir_dump_block(fn->blocks[i], out);

    fprintf(out, "}\n\n");
}

void ir_dump(const IRModule *m, FILE *out) {
    fprintf(out, "; module: %s\n", m->name);
    for (int i = 0; i < m->nglobal; i++) {
        fprintf(out, "@%s : %s%s\n",
                m->globals[i].name,
                type_name(m->globals[i].type),
                m->globals[i].is_const ? " (const)" : "");
    }
    if (m->nglobal > 0) fprintf(out, "\n");
    for (int i = 0; i < m->nfunc; i++)
        ir_dump_func(m->funcs[i], out);
}

/* AST -> IR lowering */

typedef struct {
    IRModule *module;
    IRFunc   *func;         /* current function being lowered */
    IRBlock  *block;        /* current block being emitted into */

    struct {
        char *name;
        int   reg;
    } *locals;
    int nlocal, cap_local;

    int break_block;
    int continue_block;
} LowerCtx;

static void ctx_init(LowerCtx *ctx, IRModule *m) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->module = m;
    ctx->break_block = -1;
    ctx->continue_block = -1;
}

static void ctx_push_local(LowerCtx *ctx, const char *name, int reg) {
    if (ctx->nlocal >= ctx->cap_local) {
        ctx->cap_local = ctx->cap_local ? ctx->cap_local * 2 : 16;
        ctx->locals = xs_realloc(ctx->locals,
            ctx->cap_local * sizeof(ctx->locals[0]));
    }
    ctx->locals[ctx->nlocal].name = xs_strdup(name);
    ctx->locals[ctx->nlocal].reg  = reg;
    ctx->nlocal++;
}

static int ctx_lookup_local(LowerCtx *ctx, const char *name) {
    for (int i = ctx->nlocal - 1; i >= 0; i--) {
        if (strcmp(ctx->locals[i].name, name) == 0)
            return ctx->locals[i].reg;
    }
    return -1;
}

static void ctx_free_locals(LowerCtx *ctx) {
    for (int i = 0; i < ctx->nlocal; i++)
        free(ctx->locals[i].name);
    free(ctx->locals);
    ctx->locals = NULL;
    ctx->nlocal = ctx->cap_local = 0;
}

static int lower_expr(LowerCtx *ctx, Node *node);
static void lower_stmt(LowerCtx *ctx, Node *node);

static IROpcode binop_to_ir(const char *op) {
    if (strcmp(op, "+")  == 0) return IR_ADD;
    if (strcmp(op, "-")  == 0) return IR_SUB;
    if (strcmp(op, "*")  == 0) return IR_MUL;
    if (strcmp(op, "/")  == 0) return IR_DIV;
    if (strcmp(op, "%")  == 0) return IR_MOD;
    if (strcmp(op, "**") == 0) return IR_POW;
    if (strcmp(op, "&")  == 0) return IR_BIT_AND;
    if (strcmp(op, "|")  == 0) return IR_BIT_OR;
    if (strcmp(op, "^")  == 0) return IR_BIT_XOR;
    if (strcmp(op, "<<") == 0) return IR_SHL;
    if (strcmp(op, ">>") == 0) return IR_SHR;
    if (strcmp(op, "==") == 0) return IR_EQ;
    if (strcmp(op, "!=") == 0) return IR_NEQ;
    if (strcmp(op, "<")  == 0) return IR_LT;
    if (strcmp(op, "<=") == 0) return IR_LE;
    if (strcmp(op, ">")  == 0) return IR_GT;
    if (strcmp(op, ">=") == 0) return IR_GE;
    if (strcmp(op, "&&") == 0) return IR_AND;
    if (strcmp(op, "||") == 0) return IR_OR;
    if (strcmp(op, "++") == 0) return IR_STRCAT;
    return IR_NOP;
}

static IRType binop_result_type(IROpcode op) {
    switch (op) {
    case IR_EQ: case IR_NEQ: case IR_LT: case IR_LE:
    case IR_GT: case IR_GE:  case IR_AND: case IR_OR:
        return IRT_BOOL;
    case IR_STRCAT:
        return IRT_STR;
    default:
        return IRT_ANY;
    }
}

static int lower_expr(LowerCtx *ctx, Node *node) {
    if (!node) {
        return ir_emit_const_null(ctx->block, ctx->func);
    }

    switch (node->tag) {
    case NODE_LIT_INT:
        return ir_emit_const_int(ctx->block, ctx->func, node->lit_int.ival);

    case NODE_LIT_BIGINT:
        return ir_emit_const_int(ctx->block, ctx->func, strtoll(node->lit_bigint.bigint_str, NULL, 10));

    case NODE_LIT_FLOAT:
        return ir_emit_const_float(ctx->block, ctx->func, node->lit_float.fval);

    case NODE_LIT_BOOL:
        return ir_emit_const_bool(ctx->block, ctx->func, node->lit_bool.bval);

    case NODE_LIT_STRING:
        return ir_emit_const_str(ctx->block, ctx->func,
                                 node->lit_string.sval ? node->lit_string.sval : "");

    case NODE_LIT_NULL:
        return ir_emit_const_null(ctx->block, ctx->func);

    case NODE_LIT_CHAR: {
        char buf[2] = { node->lit_char.cval, '\0' };
        return ir_emit_const_str(ctx->block, ctx->func, buf);
    }

    case NODE_IDENT: {
        int reg = ctx_lookup_local(ctx, node->ident.name);
        if (reg >= 0) {
            return ir_emit_load(ctx->block, ctx->func, irop_reg(reg), IRT_ANY);
        }
        return ir_emit_load_global(ctx->block, ctx->func, node->ident.name, IRT_ANY);
    }

    case NODE_BINOP: {
        if (strcmp(node->binop.op, "&&") == 0) {
            int lreg = lower_expr(ctx, node->binop.left);
            IRBlock *rhs_block = ir_block_new(ctx->func, "and_rhs");
            IRBlock *merge_block = ir_block_new(ctx->func, "and_merge");
            IRBlock *orig_block = ctx->block;
            ir_emit_br_if(ctx->block, irop_reg(lreg), rhs_block->id, merge_block->id);
            ir_link_blocks(ctx->block, rhs_block);
            ir_link_blocks(ctx->block, merge_block);

            ctx->block = rhs_block;
            int rreg = lower_expr(ctx, node->binop.right);
            IRBlock *rhs_end = ctx->block;
            if (!ir_block_is_terminated(rhs_end)) {
                ir_emit_br(rhs_end, merge_block->id);
                ir_link_blocks(rhs_end, merge_block);
            }

            ctx->block = merge_block;
            IRPhiEntry entries[2];
            entries[0].operand = irop_bool(0);
            entries[0].block_id = orig_block->id;
            entries[1].operand = irop_reg(rreg);
            entries[1].block_id = rhs_end->id;
            return ir_emit_phi(merge_block, ctx->func, IRT_BOOL, entries, 2);
        }
        if (strcmp(node->binop.op, "||") == 0) {
            int lreg = lower_expr(ctx, node->binop.left);
            IRBlock *rhs_block = ir_block_new(ctx->func, "or_rhs");
            IRBlock *merge_block = ir_block_new(ctx->func, "or_merge");
            IRBlock *orig_block = ctx->block;
            ir_emit_br_if(ctx->block, irop_reg(lreg), merge_block->id, rhs_block->id);
            ir_link_blocks(ctx->block, merge_block);
            ir_link_blocks(ctx->block, rhs_block);

            ctx->block = rhs_block;
            int rreg = lower_expr(ctx, node->binop.right);
            IRBlock *rhs_end = ctx->block;
            if (!ir_block_is_terminated(rhs_end)) {
                ir_emit_br(rhs_end, merge_block->id);
                ir_link_blocks(rhs_end, merge_block);
            }

            ctx->block = merge_block;
            IRPhiEntry entries[2];
            entries[0].operand = irop_bool(1);
            entries[0].block_id = orig_block->id;
            entries[1].operand = irop_reg(rreg);
            entries[1].block_id = rhs_end->id;
            return ir_emit_phi(merge_block, ctx->func, IRT_BOOL, entries, 2);
        }

        int lreg = lower_expr(ctx, node->binop.left);
        int rreg = lower_expr(ctx, node->binop.right);
        IROpcode op = binop_to_ir(node->binop.op);
        IRType rt = binop_result_type(op);
        return ir_emit_binop(ctx->block, ctx->func, op,
                             irop_reg(lreg), irop_reg(rreg), rt);
    }

    case NODE_UNARY: {
        int src = lower_expr(ctx, node->unary.expr);
        if (strcmp(node->unary.op, "-") == 0)
            return ir_emit_unary(ctx->block, ctx->func, IR_NEG, irop_reg(src), IRT_ANY);
        if (strcmp(node->unary.op, "!") == 0)
            return ir_emit_unary(ctx->block, ctx->func, IR_NOT, irop_reg(src), IRT_BOOL);
        if (strcmp(node->unary.op, "~") == 0)
            return ir_emit_unary(ctx->block, ctx->func, IR_BIT_NOT, irop_reg(src), IRT_I64);
        return src;
    }

    case NODE_CALL: {
        int nargs = node->call.args.len;
        IROperand *args = NULL;
        if (nargs > 0)
            args = xs_malloc(nargs * sizeof(IROperand));
        for (int i = 0; i < nargs; i++) {
            int r = lower_expr(ctx, node->call.args.items[i]);
            args[i] = irop_reg(r);
        }
        const char *callee = "?";
        if (node->call.callee && node->call.callee->tag == NODE_IDENT)
            callee = node->call.callee->ident.name;
        int dest = ir_emit_call(ctx->block, ctx->func, callee, args, nargs, IRT_ANY);
        free(args);
        return dest;
    }

    case NODE_METHOD_CALL: {
        int obj = lower_expr(ctx, node->method_call.obj);
        int nargs = node->method_call.args.len;
        IROperand *args = xs_malloc((nargs + 1) * sizeof(IROperand));
        args[0] = irop_reg(obj);
        for (int i = 0; i < nargs; i++) {
            int r = lower_expr(ctx, node->method_call.args.items[i]);
            args[i + 1] = irop_reg(r);
        }
        char buf[256];
        snprintf(buf, sizeof(buf), ".%s", node->method_call.method);
        int dest = ir_emit_call(ctx->block, ctx->func, buf, args, nargs + 1, IRT_ANY);
        free(args);
        return dest;
    }

    case NODE_INDEX: {
        int obj = lower_expr(ctx, node->index.obj);
        int idx = lower_expr(ctx, node->index.index);
        return ir_emit_index(ctx->block, ctx->func,
                             irop_reg(obj), irop_reg(idx), IRT_ANY);
    }

    case NODE_FIELD: {
        int obj = lower_expr(ctx, node->field.obj);
        return ir_emit_load_field(ctx->block, ctx->func,
                                  irop_reg(obj), node->field.name, IRT_ANY);
    }

    case NODE_IF: {
        int cond_reg = lower_expr(ctx, node->if_expr.cond);
        IRBlock *then_block = ir_block_new(ctx->func, "if_then");
        IRBlock *else_block = ir_block_new(ctx->func, "if_else");
        IRBlock *merge_block = ir_block_new(ctx->func, "if_merge");

        ir_emit_br_if(ctx->block, irop_reg(cond_reg),
                       then_block->id, else_block->id);
        ir_link_blocks(ctx->block, then_block);
        ir_link_blocks(ctx->block, else_block);

        /* Then */
        ctx->block = then_block;
        int then_val = lower_expr(ctx, node->if_expr.then);
        IRBlock *then_end = ctx->block;
        if (!ir_block_is_terminated(then_end)) {
            ir_emit_br(then_end, merge_block->id);
            ir_link_blocks(then_end, merge_block);
        }

        /* Else */
        ctx->block = else_block;
        int else_val;
        if (node->if_expr.else_branch)
            else_val = lower_expr(ctx, node->if_expr.else_branch);
        else
            else_val = ir_emit_const_null(ctx->block, ctx->func);
        IRBlock *else_end = ctx->block;
        if (!ir_block_is_terminated(else_end)) {
            ir_emit_br(else_end, merge_block->id);
            ir_link_blocks(else_end, merge_block);
        }

        ctx->block = merge_block;
        IRPhiEntry entries[2];
        entries[0].operand = irop_reg(then_val);
        entries[0].block_id = then_end->id;
        entries[1].operand = irop_reg(else_val);
        entries[1].block_id = else_end->id;
        return ir_emit_phi(merge_block, ctx->func, IRT_ANY, entries, 2);
    }

    case NODE_BLOCK: {
        int last = -1;
        for (int i = 0; i < node->block.stmts.len; i++) {
            lower_stmt(ctx, node->block.stmts.items[i]);
            if (ir_block_is_terminated(ctx->block)) break;
        }
        if (node->block.expr && !ir_block_is_terminated(ctx->block))
            last = lower_expr(ctx, node->block.expr);
        else if (!ir_block_is_terminated(ctx->block))
            last = ir_emit_const_null(ctx->block, ctx->func);
        else
            last = ir_emit_const_null(ctx->block, ctx->func);
        return last;
    }

    case NODE_LAMBDA: {
        char name[64];
        snprintf(name, sizeof(name), "__lambda_%d", ctx->func->next_reg);
        IRFunc *lam = ir_func_new(name, IRT_ANY);
        for (int i = 0; i < node->lambda.params.len; i++) {
            const char *pname = node->lambda.params.items[i].name;
            ir_func_add_param(lam, pname ? pname : "_", IRT_ANY);
        }
        ir_module_add_func(ctx->module, lam);

        IRFunc  *save_fn    = ctx->func;
        IRBlock *save_block = ctx->block;
        int      save_nlocal = ctx->nlocal;

        ctx->func = lam;
        IRBlock *entry = ir_block_new(lam, "entry");
        ctx->block = entry;

        for (int i = 0; i < lam->nparam; i++) {
            int r = ir_emit_alloc(entry, lam, IRT_ANY);
            ir_emit_store(entry, irop_reg(i), irop_reg(r));
            ctx_push_local(ctx, lam->param_names[i], r);
        }

        int body_val = lower_expr(ctx, node->lambda.body);
        if (!ir_block_is_terminated(ctx->block))
            ir_emit_ret(ctx->block, irop_reg(body_val));

        ctx->func = save_fn;
        ctx->block = save_block;
        for (int i = ctx->nlocal - 1; i >= save_nlocal; i--)
            free(ctx->locals[i].name);
        ctx->nlocal = save_nlocal;

        return ir_emit_const_str(ctx->block, ctx->func, name);
    }

    case NODE_CAST: {
        int val = lower_expr(ctx, node->cast.expr);
        IRType target = IRT_ANY;
        if (node->cast.type_name) {
            if (strcmp(node->cast.type_name, "i64") == 0 || strcmp(node->cast.type_name, "int") == 0)
                target = IRT_I64;
            else if (strcmp(node->cast.type_name, "f64") == 0 || strcmp(node->cast.type_name, "float") == 0)
                target = IRT_F64;
            else if (strcmp(node->cast.type_name, "bool") == 0)
                target = IRT_BOOL;
            else if (strcmp(node->cast.type_name, "str") == 0 || strcmp(node->cast.type_name, "string") == 0)
                target = IRT_STR;
        }
        return ir_emit_cast(ctx->block, ctx->func, irop_reg(val), target);
    }

    case NODE_LIT_ARRAY: {
        int n = node->lit_array.elems.len;
        IROperand *elems = NULL;
        if (n > 0)
            elems = xs_malloc(n * sizeof(IROperand));
        for (int i = 0; i < n; i++) {
            int r = lower_expr(ctx, node->lit_array.elems.items[i]);
            elems[i] = irop_reg(r);
        }
        int dest = ir_emit_make_array(ctx->block, ctx->func, elems, n);
        free(elems);
        return dest;
    }

    case NODE_LIT_MAP: {
        int n = node->lit_map.keys.len;
        IROperand *keys = NULL, *vals = NULL;
        if (n > 0) {
            keys = xs_malloc(n * sizeof(IROperand));
            vals = xs_malloc(n * sizeof(IROperand));
        }
        for (int i = 0; i < n; i++) {
            int kr = lower_expr(ctx, node->lit_map.keys.items[i]);
            int vr = lower_expr(ctx, node->lit_map.vals.items[i]);
            keys[i] = irop_reg(kr);
            vals[i] = irop_reg(vr);
        }
        int dest = ir_emit_make_map(ctx->block, ctx->func, keys, vals, n);
        free(keys);
        free(vals);
        return dest;
    }

    case NODE_LIT_TUPLE: {
        int n = node->lit_array.elems.len;
        IROperand *elems = NULL;
        if (n > 0)
            elems = xs_malloc(n * sizeof(IROperand));
        for (int i = 0; i < n; i++) {
            int r = lower_expr(ctx, node->lit_array.elems.items[i]);
            elems[i] = irop_reg(r);
        }
        int dest = ir_emit_make_array(ctx->block, ctx->func, elems, n);
        free(elems);
        return dest;
    }

    case NODE_MATCH: {
        int subject = lower_expr(ctx, node->match.subject);
        IRBlock *merge_block = ir_block_new(ctx->func, "match_end");
        int narms = node->match.arms.len;
        int *arm_vals = xs_malloc(narms * sizeof(int));
        int *arm_blk_ids = xs_malloc(narms * sizeof(int));
        int nphis = 0;

        IRBlock *current_test = ctx->block;
        for (int i = 0; i < narms; i++) {
            MatchArm *arm = &node->match.arms.items[i];
            IRBlock *arm_body  = ir_block_new(ctx->func, "match_arm");
            IRBlock *next_test = (i + 1 < narms) ?
                                 ir_block_new(ctx->func, "match_test") : merge_block;

            ctx->block = current_test;
            if (arm->pattern && arm->pattern->tag == NODE_PAT_WILD) {
                ir_emit_br(ctx->block, arm_body->id);
                ir_link_blocks(ctx->block, arm_body);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT) {
                int pat_val;
                if (arm->pattern->pat_lit.tag == 0)
                    pat_val = ir_emit_const_int(ctx->block, ctx->func, arm->pattern->pat_lit.ival);
                else if (arm->pattern->pat_lit.tag == 1)
                    pat_val = ir_emit_const_float(ctx->block, ctx->func, arm->pattern->pat_lit.fval);
                else if (arm->pattern->pat_lit.tag == 2)
                    pat_val = ir_emit_const_str(ctx->block, ctx->func,
                                                 arm->pattern->pat_lit.sval ? arm->pattern->pat_lit.sval : "");
                else if (arm->pattern->pat_lit.tag == 3)
                    pat_val = ir_emit_const_bool(ctx->block, ctx->func, arm->pattern->pat_lit.bval);
                else
                    pat_val = ir_emit_const_null(ctx->block, ctx->func);
                int cmp = ir_emit_binop(ctx->block, ctx->func, IR_EQ,
                                         irop_reg(subject), irop_reg(pat_val), IRT_BOOL);
                ir_emit_br_if(ctx->block, irop_reg(cmp), arm_body->id, next_test->id);
                ir_link_blocks(ctx->block, arm_body);
                ir_link_blocks(ctx->block, next_test);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT) {
                int slot = ir_emit_alloc(ctx->block, ctx->func, IRT_ANY);
                ir_emit_store(ctx->block, irop_reg(subject), irop_reg(slot));
                ctx_push_local(ctx, arm->pattern->pat_ident.name, slot);
                ir_emit_br(ctx->block, arm_body->id);
                ir_link_blocks(ctx->block, arm_body);
            } else {
                ir_emit_br(ctx->block, arm_body->id);
                ir_link_blocks(ctx->block, arm_body);
            }

            ctx->block = arm_body;
            int val = arm->body ? lower_expr(ctx, arm->body)
                                : ir_emit_const_null(ctx->block, ctx->func);
            IRBlock *arm_end = ctx->block;
            if (!ir_block_is_terminated(arm_end)) {
                arm_vals[nphis] = val;
                arm_blk_ids[nphis] = arm_end->id;
                nphis++;
                ir_emit_br(arm_end, merge_block->id);
                ir_link_blocks(arm_end, merge_block);
            }

            current_test = next_test;
        }

        if (current_test && current_test != merge_block && !ir_block_is_terminated(current_test)) {
            ctx->block = current_test;
            int null_val = ir_emit_const_null(ctx->block, ctx->func);
            arm_vals[nphis] = null_val;
            arm_blk_ids[nphis] = current_test->id;
            nphis++;
            ir_emit_br(current_test, merge_block->id);
            ir_link_blocks(current_test, merge_block);
        }

        ctx->block = merge_block;
        int result;
        if (nphis > 0) {
            IRPhiEntry *entries = xs_malloc(nphis * sizeof(IRPhiEntry));
            for (int i = 0; i < nphis; i++) {
                entries[i].operand = irop_reg(arm_vals[i]);
                entries[i].block_id = arm_blk_ids[i];
            }
            result = ir_emit_phi(merge_block, ctx->func, IRT_ANY, entries, nphis);
            free(entries);
        } else {
            result = ir_emit_const_null(ctx->block, ctx->func);
        }
        free(arm_vals);
        free(arm_blk_ids);
        return result;
    }

    case NODE_RANGE: {
        int start_reg = lower_expr(ctx, node->range.start);
        int end_reg   = lower_expr(ctx, node->range.end);
        int incl_reg  = ir_emit_const_bool(ctx->block, ctx->func, node->range.inclusive);
        IROperand args[3] = { irop_reg(start_reg), irop_reg(end_reg), irop_reg(incl_reg) };
        return ir_emit_call(ctx->block, ctx->func, "__range", args, 3, IRT_ARRAY);
    }

    case NODE_INTERP_STRING: {
        int nparts = node->lit_string.parts.len;
        if (nparts == 0) {
            return ir_emit_const_str(ctx->block, ctx->func,
                                     node->lit_string.sval ? node->lit_string.sval : "");
        }
        int result = lower_expr(ctx, node->lit_string.parts.items[0]);
        for (int i = 1; i < nparts; i++) {
            int part = lower_expr(ctx, node->lit_string.parts.items[i]);
            result = ir_emit_binop(ctx->block, ctx->func, IR_STRCAT,
                                   irop_reg(result), irop_reg(part), IRT_STR);
        }
        return result;
    }

    case NODE_STRUCT_INIT: {
        int n = node->struct_init.fields.len;
        int name_reg = ir_emit_const_str(ctx->block, ctx->func,
                                          node->struct_init.path ? node->struct_init.path : "?");
        IROperand alloc_args[1] = { irop_reg(name_reg) };
        int obj = ir_emit_call(ctx->block, ctx->func, "__struct_new", alloc_args, 1, IRT_ANY);
        for (int i = 0; i < n; i++) {
            int val = lower_expr(ctx, node->struct_init.fields.items[i].val);
            ir_emit_store_field(ctx->block, irop_reg(obj),
                                node->struct_init.fields.items[i].key, irop_reg(val));
        }
        if (node->struct_init.rest) {
            int rest_val = lower_expr(ctx, node->struct_init.rest);
            IROperand spread_args[2] = { irop_reg(obj), irop_reg(rest_val) };
            ir_emit_call(ctx->block, ctx->func, "__struct_spread", spread_args, 2, IRT_VOID);
        }
        return obj;
    }

    case NODE_LIST_COMP: {
        /* [element for pat in iter if cond]
         * Lower as: arr = []; for pat in iter { if cond { arr.push(element) } } */
        int arr = ir_emit_make_array(ctx->block, ctx->func, NULL, 0);

        /* Only handle first clause for simplicity (most common case) */
        int nclauses = node->list_comp.clause_iters.len;
        for (int ci = 0; ci < nclauses; ci++) {
            int iter_reg = lower_expr(ctx, node->list_comp.clause_iters.items[ci]);

            IRBlock *cond_block  = ir_block_new(ctx->func, "lc_cond");
            IRBlock *body_block  = ir_block_new(ctx->func, "lc_body");
            IRBlock *merge_block = ir_block_new(ctx->func, "lc_end");

            /* Index variable */
            int idx_slot = ir_emit_alloc(ctx->block, ctx->func, IRT_I64);
            int zero_reg = ir_emit_const_int(ctx->block, ctx->func, 0);
            ir_emit_store(ctx->block, irop_reg(zero_reg), irop_reg(idx_slot));

            ir_emit_br(ctx->block, cond_block->id);
            ir_link_blocks(ctx->block, cond_block);

            /* Condition: index < len(iter) */
            ctx->block = cond_block;
            int idx_val = ir_emit_load(cond_block, ctx->func, irop_reg(idx_slot), IRT_I64);
            IROperand len_args[1] = { irop_reg(iter_reg) };
            int len_reg = ir_emit_call(cond_block, ctx->func, "__len", len_args, 1, IRT_I64);
            int cmp = ir_emit_binop(cond_block, ctx->func, IR_LT,
                                     irop_reg(idx_val), irop_reg(len_reg), IRT_BOOL);
            ir_emit_br_if(cond_block, irop_reg(cmp), body_block->id, merge_block->id);
            ir_link_blocks(cond_block, body_block);
            ir_link_blocks(cond_block, merge_block);

            /* Body */
            ctx->block = body_block;
            int elem_reg = ir_emit_index(body_block, ctx->func,
                                          irop_reg(iter_reg), irop_reg(idx_val), IRT_ANY);
            /* Bind pattern */
            int elem_slot = ir_emit_alloc(body_block, ctx->func, IRT_ANY);
            ir_emit_store(body_block, irop_reg(elem_reg), irop_reg(elem_slot));
            Node *pat = node->list_comp.clause_pats.items[ci];
            if (pat && pat->tag == NODE_PAT_IDENT)
                ctx_push_local(ctx, pat->pat_ident.name, elem_slot);
            else if (pat && pat->tag == NODE_IDENT)
                ctx_push_local(ctx, pat->ident.name, elem_slot);

            /* Optional guard condition */
            Node *guard = (ci < node->list_comp.clause_conds.len) ?
                          node->list_comp.clause_conds.items[ci] : NULL;
            IRBlock *push_block = body_block;
            IRBlock *skip_block = NULL;
            if (guard) {
                int guard_val = lower_expr(ctx, guard);
                push_block = ir_block_new(ctx->func, "lc_push");
                skip_block = ir_block_new(ctx->func, "lc_skip");
                ir_emit_br_if(ctx->block, irop_reg(guard_val), push_block->id, skip_block->id);
                ir_link_blocks(ctx->block, push_block);
                ir_link_blocks(ctx->block, skip_block);
                ctx->block = push_block;
            }

            /* Evaluate element expression and push to array */
            int val = lower_expr(ctx, node->list_comp.element);
            IROperand push_args[2] = { irop_reg(arr), irop_reg(val) };
            ir_emit_call(ctx->block, ctx->func, "__array_push", push_args, 2, IRT_VOID);

            /* Increment index and loop back */
            if (!ir_block_is_terminated(ctx->block)) {
                int cur_idx = ir_emit_load(ctx->block, ctx->func, irop_reg(idx_slot), IRT_I64);
                int one_reg = ir_emit_const_int(ctx->block, ctx->func, 1);
                int new_idx = ir_emit_binop(ctx->block, ctx->func, IR_ADD,
                                             irop_reg(cur_idx), irop_reg(one_reg), IRT_I64);
                ir_emit_store(ctx->block, irop_reg(new_idx), irop_reg(idx_slot));
                ir_emit_br(ctx->block, cond_block->id);
                ir_link_blocks(ctx->block, cond_block);
            }

            if (skip_block) {
                /* Skip block also increments and loops */
                ctx->block = skip_block;
                int cur_idx2 = ir_emit_load(ctx->block, ctx->func, irop_reg(idx_slot), IRT_I64);
                int one_reg2 = ir_emit_const_int(ctx->block, ctx->func, 1);
                int new_idx2 = ir_emit_binop(ctx->block, ctx->func, IR_ADD,
                                              irop_reg(cur_idx2), irop_reg(one_reg2), IRT_I64);
                ir_emit_store(ctx->block, irop_reg(new_idx2), irop_reg(idx_slot));
                ir_emit_br(ctx->block, cond_block->id);
                ir_link_blocks(ctx->block, cond_block);
            }

            ctx->block = merge_block;
        }
        return arr;
    }

    case NODE_MAP_COMP: {
        /* {key: value for pat in iter if cond} */
        IROperand no_args[1];
        (void)no_args;
        int map = ir_emit_make_map(ctx->block, ctx->func, NULL, NULL, 0);

        int nclauses = node->map_comp.clause_iters.len;
        for (int ci = 0; ci < nclauses; ci++) {
            int iter_reg = lower_expr(ctx, node->map_comp.clause_iters.items[ci]);

            IRBlock *cond_block  = ir_block_new(ctx->func, "mc_cond");
            IRBlock *body_block  = ir_block_new(ctx->func, "mc_body");
            IRBlock *merge_block = ir_block_new(ctx->func, "mc_end");

            int idx_slot = ir_emit_alloc(ctx->block, ctx->func, IRT_I64);
            int zero_reg = ir_emit_const_int(ctx->block, ctx->func, 0);
            ir_emit_store(ctx->block, irop_reg(zero_reg), irop_reg(idx_slot));

            ir_emit_br(ctx->block, cond_block->id);
            ir_link_blocks(ctx->block, cond_block);

            ctx->block = cond_block;
            int idx_val = ir_emit_load(cond_block, ctx->func, irop_reg(idx_slot), IRT_I64);
            IROperand len_args[1] = { irop_reg(iter_reg) };
            int len_reg = ir_emit_call(cond_block, ctx->func, "__len", len_args, 1, IRT_I64);
            int cmp = ir_emit_binop(cond_block, ctx->func, IR_LT,
                                     irop_reg(idx_val), irop_reg(len_reg), IRT_BOOL);
            ir_emit_br_if(cond_block, irop_reg(cmp), body_block->id, merge_block->id);
            ir_link_blocks(cond_block, body_block);
            ir_link_blocks(cond_block, merge_block);

            ctx->block = body_block;
            int elem_reg = ir_emit_index(body_block, ctx->func,
                                          irop_reg(iter_reg), irop_reg(idx_val), IRT_ANY);
            int elem_slot = ir_emit_alloc(body_block, ctx->func, IRT_ANY);
            ir_emit_store(body_block, irop_reg(elem_reg), irop_reg(elem_slot));
            Node *pat = node->map_comp.clause_pats.items[ci];
            if (pat && pat->tag == NODE_PAT_IDENT)
                ctx_push_local(ctx, pat->pat_ident.name, elem_slot);
            else if (pat && pat->tag == NODE_IDENT)
                ctx_push_local(ctx, pat->ident.name, elem_slot);

            Node *guard = (ci < node->map_comp.clause_conds.len) ?
                          node->map_comp.clause_conds.items[ci] : NULL;
            IRBlock *insert_block = body_block;
            IRBlock *skip_block = NULL;
            if (guard) {
                int guard_val = lower_expr(ctx, guard);
                insert_block = ir_block_new(ctx->func, "mc_ins");
                skip_block = ir_block_new(ctx->func, "mc_skip");
                ir_emit_br_if(ctx->block, irop_reg(guard_val), insert_block->id, skip_block->id);
                ir_link_blocks(ctx->block, insert_block);
                ir_link_blocks(ctx->block, skip_block);
                ctx->block = insert_block;
            }

            int key_val = lower_expr(ctx, node->map_comp.key);
            int val_val = lower_expr(ctx, node->map_comp.value);
            IROperand set_args[3] = { irop_reg(map), irop_reg(key_val), irop_reg(val_val) };
            ir_emit_call(ctx->block, ctx->func, "__map_set", set_args, 3, IRT_VOID);

            if (!ir_block_is_terminated(ctx->block)) {
                int cur_idx = ir_emit_load(ctx->block, ctx->func, irop_reg(idx_slot), IRT_I64);
                int one_reg = ir_emit_const_int(ctx->block, ctx->func, 1);
                int new_idx = ir_emit_binop(ctx->block, ctx->func, IR_ADD,
                                             irop_reg(cur_idx), irop_reg(one_reg), IRT_I64);
                ir_emit_store(ctx->block, irop_reg(new_idx), irop_reg(idx_slot));
                ir_emit_br(ctx->block, cond_block->id);
                ir_link_blocks(ctx->block, cond_block);
            }

            if (skip_block) {
                ctx->block = skip_block;
                int cur_idx2 = ir_emit_load(ctx->block, ctx->func, irop_reg(idx_slot), IRT_I64);
                int one_reg2 = ir_emit_const_int(ctx->block, ctx->func, 1);
                int new_idx2 = ir_emit_binop(ctx->block, ctx->func, IR_ADD,
                                              irop_reg(cur_idx2), irop_reg(one_reg2), IRT_I64);
                ir_emit_store(ctx->block, irop_reg(new_idx2), irop_reg(idx_slot));
                ir_emit_br(ctx->block, cond_block->id);
                ir_link_blocks(ctx->block, cond_block);
            }

            ctx->block = merge_block;
        }
        return map;
    }

    case NODE_SPREAD: {
        /* Emit a runtime helper call: __spread(expr) */
        int val = lower_expr(ctx, node->spread.expr);
        IROperand args[1] = { irop_reg(val) };
        return ir_emit_call(ctx->block, ctx->func, "__spread", args, 1, IRT_ANY);
    }

    case NODE_TRY: {
        /* Lower try as: try body, branch to catch on error, then finally.
         * Simplified: emit body in a try block, catch arms as conditional branches. */
        IRBlock *try_block    = ir_block_new(ctx->func, "try_body");
        IRBlock *catch_block  = ir_block_new(ctx->func, "try_catch");
        IRBlock *finally_block_ir = node->try_.finally_block ?
                                    ir_block_new(ctx->func, "try_finally") : NULL;
        IRBlock *merge_block  = ir_block_new(ctx->func, "try_end");

        /* Call __try_begin to set up exception handler */
        IROperand begin_args[1] = { irop_int(catch_block->id) };
        ir_emit_call(ctx->block, ctx->func, "__try_begin", begin_args, 1, IRT_VOID);

        ir_emit_br(ctx->block, try_block->id);
        ir_link_blocks(ctx->block, try_block);

        /* Try body */
        ctx->block = try_block;
        int try_val = lower_expr(ctx, node->try_.body);
        IRBlock *try_end = ctx->block;
        if (!ir_block_is_terminated(try_end)) {
            ir_emit_call(try_end, ctx->func, "__try_end", NULL, 0, IRT_VOID);
            int target = finally_block_ir ? finally_block_ir->id : merge_block->id;
            ir_emit_br(try_end, target);
            ir_link_blocks(try_end, finally_block_ir ? finally_block_ir : merge_block);
        }

        /* Catch block — get the caught exception */
        ctx->block = catch_block;
        IROperand no_args2[1]; (void)no_args2;
        int exc_reg = ir_emit_call(catch_block, ctx->func, "__get_exception", NULL, 0, IRT_ANY);

        /* Lower catch arms as if-else chain */
        int ncatch = node->try_.catch_arms.len;
        for (int i = 0; i < ncatch; i++) {
            MatchArm *arm = &node->try_.catch_arms.items[i];
            IRBlock *arm_body = ir_block_new(ctx->func, "catch_arm");
            IRBlock *next_arm = (i + 1 < ncatch) ?
                                ir_block_new(ctx->func, "catch_test") :
                                (finally_block_ir ? finally_block_ir : merge_block);

            if (arm->pattern && arm->pattern->tag == NODE_PAT_WILD) {
                ir_emit_br(ctx->block, arm_body->id);
                ir_link_blocks(ctx->block, arm_body);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT) {
                int slot = ir_emit_alloc(ctx->block, ctx->func, IRT_ANY);
                ir_emit_store(ctx->block, irop_reg(exc_reg), irop_reg(slot));
                ctx_push_local(ctx, arm->pattern->pat_ident.name, slot);
                ir_emit_br(ctx->block, arm_body->id);
                ir_link_blocks(ctx->block, arm_body);
            } else {
                ir_emit_br(ctx->block, arm_body->id);
                ir_link_blocks(ctx->block, arm_body);
            }

            ctx->block = arm_body;
            if (arm->body)
                lower_expr(ctx, arm->body);
            if (!ir_block_is_terminated(ctx->block)) {
                int target = finally_block_ir ? finally_block_ir->id : merge_block->id;
                ir_emit_br(ctx->block, target);
                ir_link_blocks(ctx->block, finally_block_ir ? finally_block_ir : merge_block);
            }
            if (i + 1 < ncatch)
                ctx->block = next_arm;
        }
        if (ncatch == 0 && !ir_block_is_terminated(catch_block)) {
            int target = finally_block_ir ? finally_block_ir->id : merge_block->id;
            ir_emit_br(catch_block, target);
            ir_link_blocks(catch_block, finally_block_ir ? finally_block_ir : merge_block);
        }

        /* Finally block */
        if (finally_block_ir) {
            ctx->block = finally_block_ir;
            if (node->try_.finally_block)
                lower_expr(ctx, node->try_.finally_block);
            if (!ir_block_is_terminated(ctx->block)) {
                ir_emit_br(ctx->block, merge_block->id);
                ir_link_blocks(ctx->block, merge_block);
            }
        }

        ctx->block = merge_block;
        return try_val;
    }

    case NODE_PERFORM: {
        /* perform Effect.op(args) => __perform(effect_name, op_name, args...) */
        int nargs = node->perform.args.len;
        int total = nargs + 2;
        IROperand *args = xs_malloc(total * sizeof(IROperand));
        args[0] = irop_str(node->perform.effect_name ? node->perform.effect_name : "?");
        args[1] = irop_str(node->perform.op_name ? node->perform.op_name : "?");
        for (int i = 0; i < nargs; i++) {
            int r = lower_expr(ctx, node->perform.args.items[i]);
            args[i + 2] = irop_reg(r);
        }
        int dest = ir_emit_call(ctx->block, ctx->func, "__perform", args, total, IRT_ANY);
        free(args);
        return dest;
    }

    case NODE_HANDLE: {
        /* handle expr { Effect.op(params) => body, ... }
         * Lower as: __handle_begin(); result = expr; __handle_end()
         * with handler arms registered */
        /* Register each handler arm as a nested function */
        for (int i = 0; i < node->handle.arms.len; i++) {
            EffectArm *earm = &node->handle.arms.items[i];
            char handler_name[128];
            snprintf(handler_name, sizeof(handler_name), "__handler_%s_%s_%d",
                     earm->effect_name ? earm->effect_name : "?",
                     earm->op_name ? earm->op_name : "?", i);

            IRFunc *hfn = ir_func_new(handler_name, IRT_ANY);
            for (int p = 0; p < earm->params.len; p++) {
                const char *pname = earm->params.items[p].name;
                ir_func_add_param(hfn, pname ? pname : "_", IRT_ANY);
            }
            ir_module_add_func(ctx->module, hfn);

            /* Lower handler body */
            IRFunc  *save_fn = ctx->func;
            IRBlock *save_block = ctx->block;
            int save_nlocal = ctx->nlocal;

            ctx->func = hfn;
            IRBlock *hentry = ir_block_new(hfn, "entry");
            ctx->block = hentry;
            for (int p = 0; p < hfn->nparam; p++) {
                int slot = ir_emit_alloc(hentry, hfn, IRT_ANY);
                ir_emit_store(hentry, irop_reg(p), irop_reg(slot));
                ctx_push_local(ctx, hfn->param_names[p], slot);
            }
            int hbody_val = lower_expr(ctx, earm->body);
            if (!ir_block_is_terminated(ctx->block))
                ir_emit_ret(ctx->block, irop_reg(hbody_val));

            ctx->func = save_fn;
            ctx->block = save_block;
            for (int k = ctx->nlocal - 1; k >= save_nlocal; k--)
                free(ctx->locals[k].name);
            ctx->nlocal = save_nlocal;

            /* Register the handler */
            int ename_reg = ir_emit_const_str(ctx->block, ctx->func,
                                               earm->effect_name ? earm->effect_name : "?");
            int oname_reg = ir_emit_const_str(ctx->block, ctx->func,
                                               earm->op_name ? earm->op_name : "?");
            int hname_reg = ir_emit_const_str(ctx->block, ctx->func, handler_name);
            IROperand reg_args[3] = { irop_reg(ename_reg), irop_reg(oname_reg), irop_reg(hname_reg) };
            ir_emit_call(ctx->block, ctx->func, "__handle_register", reg_args, 3, IRT_VOID);
        }

        /* Lower the handled expression */
        int result = lower_expr(ctx, node->handle.expr);
        ir_emit_call(ctx->block, ctx->func, "__handle_end", NULL, 0, IRT_VOID);
        return result;
    }

    case NODE_RESUME: {
        /* resume val => __resume(val) */
        int val = node->resume_.value ? lower_expr(ctx, node->resume_.value)
                                      : ir_emit_const_null(ctx->block, ctx->func);
        IROperand args[1] = { irop_reg(val) };
        return ir_emit_call(ctx->block, ctx->func, "__resume", args, 1, IRT_ANY);
    }

    case NODE_AWAIT: {
        /* await expr => __await(expr) */
        int val = lower_expr(ctx, node->await_.expr);
        IROperand args[1] = { irop_reg(val) };
        return ir_emit_call(ctx->block, ctx->func, "__await", args, 1, IRT_ANY);
    }

    case NODE_YIELD: {
        /* yield value => __yield(value) */
        int val = node->yield_.value ? lower_expr(ctx, node->yield_.value)
                                     : ir_emit_const_null(ctx->block, ctx->func);
        IROperand args[1] = { irop_reg(val) };
        return ir_emit_call(ctx->block, ctx->func, "__yield", args, 1, IRT_ANY);
    }

    case NODE_SPAWN: {
        /* spawn expr => __spawn(expr) */
        int val = lower_expr(ctx, node->spawn_.expr);
        IROperand args[1] = { irop_reg(val) };
        return ir_emit_call(ctx->block, ctx->func, "__spawn", args, 1, IRT_ANY);
    }

    case NODE_NURSERY: {
        /* nursery { body } => __nursery_begin(); body; __nursery_end() */
        ir_emit_call(ctx->block, ctx->func, "__nursery_begin", NULL, 0, IRT_VOID);
        int body_val = lower_expr(ctx, node->nursery_.body);
        ir_emit_call(ctx->block, ctx->func, "__nursery_end", NULL, 0, IRT_VOID);
        return body_val;
    }

    default:
        /* Unsupported expression: emit null */
        return ir_emit_const_null(ctx->block, ctx->func);
    }
}

/* Lower a statement node. */
static void lower_stmt(LowerCtx *ctx, Node *node) {
    if (!node) return;
    if (ir_block_is_terminated(ctx->block)) return;

    switch (node->tag) {
    case NODE_LET:
    case NODE_VAR: {
        int val_reg;
        if (node->let.value)
            val_reg = lower_expr(ctx, node->let.value);
        else
            val_reg = ir_emit_const_null(ctx->block, ctx->func);
        int slot = ir_emit_alloc(ctx->block, ctx->func, IRT_ANY);
        ir_emit_store(ctx->block, irop_reg(val_reg), irop_reg(slot));
        const char *name = node->let.name;
        if (name)
            ctx_push_local(ctx, name, slot);
        break;
    }

    case NODE_CONST: {
        int val_reg = lower_expr(ctx, node->const_.value);
        int slot = ir_emit_alloc(ctx->block, ctx->func, IRT_ANY);
        ir_emit_store(ctx->block, irop_reg(val_reg), irop_reg(slot));
        if (node->const_.name)
            ctx_push_local(ctx, node->const_.name, slot);
        break;
    }

    case NODE_EXPR_STMT: {
        lower_expr(ctx, node->expr_stmt.expr);
        break;
    }

    case NODE_RETURN: {
        if (node->ret.value) {
            int val = lower_expr(ctx, node->ret.value);
            ir_emit_ret(ctx->block, irop_reg(val));
        } else {
            ir_emit_ret_void(ctx->block);
        }
        break;
    }

    case NODE_BREAK: {
        if (ctx->break_block >= 0)
            ir_emit_br(ctx->block, ctx->break_block);
        break;
    }

    case NODE_CONTINUE: {
        if (ctx->continue_block >= 0)
            ir_emit_br(ctx->block, ctx->continue_block);
        break;
    }

    case NODE_IF: {
        int cond_reg = lower_expr(ctx, node->if_expr.cond);
        IRBlock *then_block = ir_block_new(ctx->func, "if_then");
        IRBlock *else_block = ir_block_new(ctx->func, "if_else");
        IRBlock *merge_block = ir_block_new(ctx->func, "if_merge");

        ir_emit_br_if(ctx->block, irop_reg(cond_reg),
                       then_block->id, else_block->id);
        ir_link_blocks(ctx->block, then_block);
        ir_link_blocks(ctx->block, else_block);

        /* Then */
        ctx->block = then_block;
        if (node->if_expr.then)
            lower_expr(ctx, node->if_expr.then);
        if (!ir_block_is_terminated(ctx->block)) {
            ir_emit_br(ctx->block, merge_block->id);
            ir_link_blocks(ctx->block, merge_block);
        }

        /* Elif chains */
        IRBlock *current_else = else_block;
        for (int i = 0; i < node->if_expr.elif_conds.len; i++) {
            ctx->block = current_else;
            int elif_cond = lower_expr(ctx, node->if_expr.elif_conds.items[i]);
            IRBlock *elif_then = ir_block_new(ctx->func, "elif_then");
            IRBlock *elif_else = ir_block_new(ctx->func, "elif_else");
            ir_emit_br_if(ctx->block, irop_reg(elif_cond),
                           elif_then->id, elif_else->id);
            ir_link_blocks(ctx->block, elif_then);
            ir_link_blocks(ctx->block, elif_else);

            ctx->block = elif_then;
            if (node->if_expr.elif_thens.items[i])
                lower_expr(ctx, node->if_expr.elif_thens.items[i]);
            if (!ir_block_is_terminated(ctx->block)) {
                ir_emit_br(ctx->block, merge_block->id);
                ir_link_blocks(ctx->block, merge_block);
            }
            current_else = elif_else;
        }

        /* Else */
        ctx->block = current_else;
        if (node->if_expr.else_branch)
            lower_expr(ctx, node->if_expr.else_branch);
        if (!ir_block_is_terminated(ctx->block)) {
            ir_emit_br(ctx->block, merge_block->id);
            ir_link_blocks(ctx->block, merge_block);
        }

        ctx->block = merge_block;
        break;
    }

    case NODE_WHILE: {
        IRBlock *cond_block  = ir_block_new(ctx->func, "while_cond");
        IRBlock *body_block  = ir_block_new(ctx->func, "while_body");
        IRBlock *merge_block = ir_block_new(ctx->func, "while_end");

        ir_emit_br(ctx->block, cond_block->id);
        ir_link_blocks(ctx->block, cond_block);

        /* Condition */
        ctx->block = cond_block;
        int cond_reg = lower_expr(ctx, node->while_loop.cond);
        ir_emit_br_if(ctx->block, irop_reg(cond_reg),
                       body_block->id, merge_block->id);
        ir_link_blocks(ctx->block, body_block);
        ir_link_blocks(ctx->block, merge_block);

        /* Body */
        int save_break = ctx->break_block;
        int save_cont  = ctx->continue_block;
        ctx->break_block    = merge_block->id;
        ctx->continue_block = cond_block->id;

        ctx->block = body_block;
        if (node->while_loop.body)
            lower_expr(ctx, node->while_loop.body);
        if (!ir_block_is_terminated(ctx->block)) {
            ir_emit_br(ctx->block, cond_block->id);
            ir_link_blocks(ctx->block, cond_block);
        }

        ctx->break_block    = save_break;
        ctx->continue_block = save_cont;

        ctx->block = merge_block;
        break;
    }

    case NODE_FOR: {
        /* Lower for loop: for pat in iter { body } */
        int iter_reg = lower_expr(ctx, node->for_loop.iter);

        IRBlock *cond_block  = ir_block_new(ctx->func, "for_cond");
        IRBlock *body_block  = ir_block_new(ctx->func, "for_body");
        IRBlock *merge_block = ir_block_new(ctx->func, "for_end");

        /* Index variable */
        int idx_slot = ir_emit_alloc(ctx->block, ctx->func, IRT_I64);
        int zero_reg = ir_emit_const_int(ctx->block, ctx->func, 0);
        ir_emit_store(ctx->block, irop_reg(zero_reg), irop_reg(idx_slot));

        ir_emit_br(ctx->block, cond_block->id);
        ir_link_blocks(ctx->block, cond_block);

        /* Condition: index < len(iter) */
        ctx->block = cond_block;
        int idx_val = ir_emit_load(cond_block, ctx->func, irop_reg(idx_slot), IRT_I64);
        IROperand len_args[1] = { irop_reg(iter_reg) };
        int len_reg = ir_emit_call(cond_block, ctx->func, "__len", len_args, 1, IRT_I64);
        int cond_reg = ir_emit_binop(cond_block, ctx->func, IR_LT,
                                      irop_reg(idx_val), irop_reg(len_reg), IRT_BOOL);
        ir_emit_br_if(cond_block, irop_reg(cond_reg), body_block->id, merge_block->id);
        ir_link_blocks(cond_block, body_block);
        ir_link_blocks(cond_block, merge_block);

        /* Body */
        int save_break = ctx->break_block;
        int save_cont  = ctx->continue_block;
        ctx->break_block    = merge_block->id;
        ctx->continue_block = cond_block->id;

        ctx->block = body_block;
        /* Bind loop variable */
        int elem_reg = ir_emit_index(body_block, ctx->func,
                                      irop_reg(iter_reg), irop_reg(idx_val), IRT_ANY);
        int elem_slot = ir_emit_alloc(body_block, ctx->func, IRT_ANY);
        ir_emit_store(body_block, irop_reg(elem_reg), irop_reg(elem_slot));
        /* Bind pattern name */
        if (node->for_loop.pattern && node->for_loop.pattern->tag == NODE_PAT_IDENT)
            ctx_push_local(ctx, node->for_loop.pattern->pat_ident.name, elem_slot);
        else if (node->for_loop.pattern && node->for_loop.pattern->tag == NODE_IDENT)
            ctx_push_local(ctx, node->for_loop.pattern->ident.name, elem_slot);

        if (node->for_loop.body)
            lower_expr(ctx, node->for_loop.body);

        /* Increment index */
        if (!ir_block_is_terminated(ctx->block)) {
            int cur_idx = ir_emit_load(ctx->block, ctx->func, irop_reg(idx_slot), IRT_I64);
            int one_reg = ir_emit_const_int(ctx->block, ctx->func, 1);
            int new_idx = ir_emit_binop(ctx->block, ctx->func, IR_ADD,
                                         irop_reg(cur_idx), irop_reg(one_reg), IRT_I64);
            ir_emit_store(ctx->block, irop_reg(new_idx), irop_reg(idx_slot));
            ir_emit_br(ctx->block, cond_block->id);
            ir_link_blocks(ctx->block, cond_block);
        }

        ctx->break_block    = save_break;
        ctx->continue_block = save_cont;

        ctx->block = merge_block;
        break;
    }

    case NODE_LOOP: {
        IRBlock *body_block  = ir_block_new(ctx->func, "loop_body");
        IRBlock *merge_block = ir_block_new(ctx->func, "loop_end");

        ir_emit_br(ctx->block, body_block->id);
        ir_link_blocks(ctx->block, body_block);

        int save_break = ctx->break_block;
        int save_cont  = ctx->continue_block;
        ctx->break_block    = merge_block->id;
        ctx->continue_block = body_block->id;

        ctx->block = body_block;
        if (node->loop.body)
            lower_expr(ctx, node->loop.body);
        if (!ir_block_is_terminated(ctx->block)) {
            ir_emit_br(ctx->block, body_block->id);
            ir_link_blocks(ctx->block, body_block);
        }

        ctx->break_block    = save_break;
        ctx->continue_block = save_cont;

        ctx->block = merge_block;
        break;
    }

    case NODE_ASSIGN: {
        int val_reg = lower_expr(ctx, node->assign.value);
        if (node->assign.target && node->assign.target->tag == NODE_IDENT) {
            int slot = ctx_lookup_local(ctx, node->assign.target->ident.name);
            if (slot >= 0)
                ir_emit_store(ctx->block, irop_reg(val_reg), irop_reg(slot));
        } else if (node->assign.target && node->assign.target->tag == NODE_FIELD) {
            int obj = lower_expr(ctx, node->assign.target->field.obj);
            ir_emit_store_field(ctx->block, irop_reg(obj),
                                node->assign.target->field.name, irop_reg(val_reg));
        } else if (node->assign.target && node->assign.target->tag == NODE_INDEX) {
            int obj = lower_expr(ctx, node->assign.target->index.obj);
            int idx = lower_expr(ctx, node->assign.target->index.index);
            IRInstr inst = instr_new(IR_STORE);
            inst.ops[0] = irop_reg(val_reg);
            inst.ops[1] = irop_reg(obj);
            inst.ops[2] = irop_reg(idx);
            inst.nops = 3;
            push_instr(ctx->block, inst);
        }
        break;
    }

    case NODE_FN_DECL: {
        /* Create a new IR function */
        IRFunc *fn = ir_func_new(node->fn_decl.name, IRT_ANY);
        for (int i = 0; i < node->fn_decl.params.len; i++) {
            const char *pname = node->fn_decl.params.items[i].name;
            ir_func_add_param(fn, pname ? pname : "_", IRT_ANY);
        }
        ir_module_add_func(ctx->module, fn);

        IRFunc  *save_fn    = ctx->func;
        IRBlock *save_block = ctx->block;
        int      save_nlocal = ctx->nlocal;

        ctx->func = fn;
        IRBlock *entry = ir_block_new(fn, "entry");
        ctx->block = entry;

        /* Bind params as locals */
        for (int i = 0; i < fn->nparam; i++) {
            int slot = ir_emit_alloc(entry, fn, IRT_ANY);
            ir_emit_store(entry, irop_reg(i), irop_reg(slot));
            ctx_push_local(ctx, fn->param_names[i], slot);
        }

        /* Lower body */
        if (node->fn_decl.body) {
            if (node->fn_decl.body->tag == NODE_BLOCK) {
                /* Block body */
                for (int i = 0; i < node->fn_decl.body->block.stmts.len; i++) {
                    lower_stmt(ctx, node->fn_decl.body->block.stmts.items[i]);
                    if (ir_block_is_terminated(ctx->block)) break;
                }
                if (!ir_block_is_terminated(ctx->block)) {
                    if (node->fn_decl.body->block.expr) {
                        int val = lower_expr(ctx, node->fn_decl.body->block.expr);
                        ir_emit_ret(ctx->block, irop_reg(val));
                    } else {
                        ir_emit_ret_void(ctx->block);
                    }
                }
            } else {
                /* Expression body */
                int val = lower_expr(ctx, node->fn_decl.body);
                if (!ir_block_is_terminated(ctx->block))
                    ir_emit_ret(ctx->block, irop_reg(val));
            }
        } else {
            ir_emit_ret_void(ctx->block);
        }

        /* Restore context */
        ctx->func  = save_fn;
        ctx->block = save_block;
        for (int i = ctx->nlocal - 1; i >= save_nlocal; i--)
            free(ctx->locals[i].name);
        ctx->nlocal = save_nlocal;
        break;
    }

    case NODE_BLOCK: {
        for (int i = 0; i < node->block.stmts.len; i++) {
            lower_stmt(ctx, node->block.stmts.items[i]);
            if (ir_block_is_terminated(ctx->block)) break;
        }
        break;
    }

    case NODE_MATCH: {
        int subject = lower_expr(ctx, node->match.subject);
        IRBlock *merge_block = ir_block_new(ctx->func, "match_end");

        IRBlock *current_test = NULL;
        for (int i = 0; i < node->match.arms.len; i++) {
            MatchArm *arm = &node->match.arms.items[i];
            IRBlock *arm_body  = ir_block_new(ctx->func, "match_arm");
            IRBlock *next_test = (i + 1 < node->match.arms.len) ?
                                 ir_block_new(ctx->func, "match_test") : merge_block;

            if (current_test == NULL) {
                current_test = ctx->block;
            }

            ctx->block = current_test;
            if (arm->pattern && arm->pattern->tag == NODE_PAT_WILD) {
                ir_emit_br(ctx->block, arm_body->id);
                ir_link_blocks(ctx->block, arm_body);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_LIT) {
                int pat_val;
                if (arm->pattern->pat_lit.tag == 0)
                    pat_val = ir_emit_const_int(ctx->block, ctx->func, arm->pattern->pat_lit.ival);
                else if (arm->pattern->pat_lit.tag == 1)
                    pat_val = ir_emit_const_float(ctx->block, ctx->func, arm->pattern->pat_lit.fval);
                else if (arm->pattern->pat_lit.tag == 2)
                    pat_val = ir_emit_const_str(ctx->block, ctx->func,
                                                 arm->pattern->pat_lit.sval ? arm->pattern->pat_lit.sval : "");
                else if (arm->pattern->pat_lit.tag == 3)
                    pat_val = ir_emit_const_bool(ctx->block, ctx->func, arm->pattern->pat_lit.bval);
                else
                    pat_val = ir_emit_const_null(ctx->block, ctx->func);
                int cmp = ir_emit_binop(ctx->block, ctx->func, IR_EQ,
                                         irop_reg(subject), irop_reg(pat_val), IRT_BOOL);
                ir_emit_br_if(ctx->block, irop_reg(cmp), arm_body->id, next_test->id);
                ir_link_blocks(ctx->block, arm_body);
                ir_link_blocks(ctx->block, next_test);
            } else if (arm->pattern && arm->pattern->tag == NODE_PAT_IDENT) {
                int slot = ir_emit_alloc(ctx->block, ctx->func, IRT_ANY);
                ir_emit_store(ctx->block, irop_reg(subject), irop_reg(slot));
                ctx_push_local(ctx, arm->pattern->pat_ident.name, slot);
                ir_emit_br(ctx->block, arm_body->id);
                ir_link_blocks(ctx->block, arm_body);
            } else {
                ir_emit_br(ctx->block, arm_body->id);
                ir_link_blocks(ctx->block, arm_body);
            }

            /* Arm body */
            ctx->block = arm_body;
            if (arm->body)
                lower_expr(ctx, arm->body);
            if (!ir_block_is_terminated(ctx->block)) {
                ir_emit_br(ctx->block, merge_block->id);
                ir_link_blocks(ctx->block, merge_block);
            }

            current_test = next_test;
        }

        if (current_test && current_test != merge_block && !ir_block_is_terminated(current_test)) {
            ctx->block = current_test;
            ir_emit_br(current_test, merge_block->id);
            ir_link_blocks(current_test, merge_block);
        }

        ctx->block = merge_block;
        break;
    }

    default:
        /* Treat as expression statement */
        lower_expr(ctx, node);
        break;
    }
}

/* entry point */

IRModule *ir_lower(Node *program) {
    if (!program) return NULL;

    IRModule *module = ir_module_new("main");

    /* Create a top-level __main function for statements outside fn decls */
    IRFunc *main_fn = ir_func_new("__main", IRT_VOID);
    ir_module_add_func(module, main_fn);

    LowerCtx ctx;
    ctx_init(&ctx, module);
    ctx.func = main_fn;

    IRBlock *entry = ir_block_new(main_fn, "entry");
    ctx.block = entry;

    if (program->tag == NODE_PROGRAM) {
        for (int i = 0; i < program->program.stmts.len; i++) {
            Node *stmt = program->program.stmts.items[i];
            lower_stmt(&ctx, stmt);
            if (ir_block_is_terminated(ctx.block)) break;
        }
    } else {
        lower_stmt(&ctx, program);
    }

    /* Ensure __main is terminated */
    if (!ir_block_is_terminated(ctx.block))
        ir_emit_ret_void(ctx.block);

    ctx_free_locals(&ctx);
    return module;
}
