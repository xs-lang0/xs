#include <stdlib.h>
#include <string.h>

#include "core/xs.h"
#include "semantic/symtable.h"

#define NBUCKETS 64

static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
    while (*s) h = h * 33 ^ (unsigned char)*s++;
    return h;
}

static Scope *scope_new(Scope *parent) {
    Scope *sc = xs_malloc(sizeof *sc);
    sc->n_buckets = NBUCKETS;
    sc->buckets   = xs_malloc(sizeof(Symbol*) * NBUCKETS);
    memset(sc->buckets, 0, sizeof(Symbol*) * NBUCKETS);
    sc->parent = parent;
    return sc;
}

static void scope_free_one(Scope *sc) {
    for (int i = 0; i < sc->n_buckets; i++) {
        Symbol *s = sc->buckets[i];
        while (s) {
            Symbol *nx = s->next;
            free(s->name);
            free(s);
            s = nx;
        }
    }
    free(sc->buckets);
    free(sc);
}

SymTab *symtab_new(void) {
    SymTab *st = xs_malloc(sizeof *st);
    st->current = scope_new(NULL);
    return st;
}

void symtab_free(SymTab *st) {
    if (!st) return;
    while (st->current) {
        Scope *parent = st->current->parent;
        scope_free_one(st->current);
        st->current = parent;
    }
    free(st);
}

void scope_push(SymTab *st) {
    st->current = scope_new(st->current);
}

void scope_pop(SymTab *st) {
    Scope *sc = st->current;
    if (!sc || !sc->parent) return;
    st->current = sc->parent;
    scope_free_one(sc);
}

int sym_define(SymTab *st, const char *name, SymKind kind,
               XsType *type, Node *decl, int mutable) {
    if (!name || !*name) return -1;
    Scope *sc = st->current;
    unsigned int h = hash_str(name) % (unsigned)sc->n_buckets;
    for (Symbol *s = sc->buckets[h]; s; s = s->next)
        if (strcmp(s->name, name) == 0) return -1;
    Symbol *sym = xs_malloc(sizeof *sym);
    sym->name    = xs_strdup(name);
    sym->kind    = kind;
    sym->type    = type;
    sym->decl    = decl;
    sym->is_used = 0;
    sym->mutable = mutable;
    sym->next    = sc->buckets[h];
    sc->buckets[h] = sym;
    return 0;
}

Symbol *sym_lookup(SymTab *st, const char *name) {
    if (!name || !*name) return NULL;
    for (Scope *sc = st->current; sc; sc = sc->parent) {
        unsigned int h = hash_str(name) % (unsigned)sc->n_buckets;
        for (Symbol *s = sc->buckets[h]; s; s = s->next)
            if (strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

void sym_mark_used(SymTab *st, const char *name) {
    Symbol *s = sym_lookup(st, name);
    if (s) s->is_used = 1;
}

Symbol **scope_unused_locals(SymTab *st, int *out_n) {
    Scope *sc = st->current;
    int n = 0;
    for (int i = 0; i < sc->n_buckets; i++)
        for (Symbol *s = sc->buckets[i]; s; s = s->next)
            if ((s->kind == SYM_LOCAL) && !s->is_used) n++;
    if (n == 0) { *out_n = 0; return NULL; }
    Symbol **arr = xs_malloc(sizeof(Symbol*) * n);
    int idx = 0;
    for (int i = 0; i < sc->n_buckets; i++)
        for (Symbol *s = sc->buckets[i]; s; s = s->next)
            if ((s->kind == SYM_LOCAL) && !s->is_used) arr[idx++] = s;
    *out_n = n;
    return arr;
}
