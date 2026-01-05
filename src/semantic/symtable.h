// Scoped symbol table
#ifndef SYMTABLE_H
#define SYMTABLE_H

#include "core/ast.h"
#include "types/types.h"

typedef enum {
    SYM_FN,
    SYM_STRUCT,
    SYM_ENUM,
    SYM_TRAIT,
    SYM_CONST,
    SYM_LOCAL,
    SYM_PARAM,
    SYM_GENERIC,
    SYM_EFFECT,
    SYM_IMPL,
    SYM_ALIAS,
    SYM_MODULE_ITEM,
} SymKind;

typedef struct Symbol {
    char          *name;
    SymKind        kind;
    XsType        *type;
    Node          *decl;
    int            is_used;
    int            mutable;
    struct Symbol *next;
} Symbol;

typedef struct Scope {
    Symbol       **buckets;
    int            n_buckets;
    struct Scope  *parent;
} Scope;

typedef struct {
    Scope *current;
} SymTab;

SymTab  *symtab_new(void);
void     symtab_free(SymTab *st);

void     scope_push(SymTab *st);
void     scope_pop(SymTab *st);

int      sym_define(SymTab *st, const char *name, SymKind kind,
                    XsType *type, Node *decl, int mutable);
Symbol  *sym_lookup(SymTab *st, const char *name);
void     sym_mark_used(SymTab *st, const char *name);
Symbol **scope_unused_locals(SymTab *st, int *out_n);

#endif
