#ifndef RESOLVE_H
#define RESOLVE_H

#include "core/ast.h"
#include "semantic/sema.h"
#include "semantic/symtable.h"

void resolve_program(Node *prog, SymTab *st, SemaCtx *ctx);

#endif
