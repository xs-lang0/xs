#ifndef XS_LINT_H
#define XS_LINT_H

#include "core/ast.h"

typedef struct XSLint XSLint;

XSLint *lint_new(int auto_fix);
void    lint_free(XSLint *l);
int     lint_program(XSLint *l, Node *program, const char *filename);
void    lint_report(XSLint *l);
int     lint_file(const char *path, int auto_fix);

#endif
