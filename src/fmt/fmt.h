#ifndef XS_FMT_H
#define XS_FMT_H

#include "core/ast.h"
#include "core/lexer.h"

/* Format AST to canonical source. Caller frees. Comments from src are preserved. */
char *fmt_format(Node *program, const char *src);
int   fmt_file(const char *path);
int   fmt_file_check(const char *path);

#endif
