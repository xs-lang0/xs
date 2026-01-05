// Bidirectional type checker for XS
#ifndef TYPECHECK_H
#define TYPECHECK_H

#include "core/ast.h"
#include "types/types.h"
#include "semantic/sema.h"
#include "semantic/symtable.h"

XsType *tc_synth(Node *n, SymTab *st, SemaCtx *ctx);
int     tc_check(Node *n, XsType *expected, SymTab *st, SemaCtx *ctx);
void    tc_program(Node *prog, SymTab *st, SemaCtx *ctx);

#endif
