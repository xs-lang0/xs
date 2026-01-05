/* optimizer.h -- AST optimization passes. */
#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "core/ast.h"

typedef struct {
    int constants_folded;
    int dead_code_removed;
    int functions_inlined;
    int strengths_reduced;
    int cses_eliminated;
    int algebraic_simplified;
    int constants_propagated;
    int loop_invariants_hoisted;
    int unused_vars_eliminated;
} OptStats;

Node *optimize(Node *program, OptStats *stats);

Node *opt_constant_fold(Node *node, int *count);
Node *opt_dead_code_elim(Node *node, int *count);
Node *opt_strength_reduce(Node *node, int *count);
Node *opt_inline_expand(Node *program, int *count);
Node *opt_cse(Node *node, int *count);
Node *opt_algebraic_simplify(Node *node, int *count);
Node *opt_constant_propagate(Node *node, int *count);
Node *opt_loop_invariant_motion(Node *node, int *count);
Node *opt_unused_var_elim(Node *node, int *count);

#endif /* OPTIMIZER_H */
