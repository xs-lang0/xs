#ifndef XS_C_GEN_H
#define XS_C_GEN_H

#include "core/ast.h"

char *transpile_c(Node *program, const char *filename);

#endif /* XS_C_GEN_H */
