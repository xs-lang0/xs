#ifndef SEMA_H
#define SEMA_H

#include "core/ast.h"
#include "diagnostic/diagnostic.h"
#include "semantic/symtable.h"

typedef struct {
    DiagContext *diag;
    int       lenient;
    int       n_errors;
    SymTab   *st;
} SemaCtx;

void sema_init(SemaCtx *ctx, int lenient);
void sema_free(SemaCtx *ctx);
int  sema_analyze(SemaCtx *ctx, Node *program, const char *filename);

#endif
