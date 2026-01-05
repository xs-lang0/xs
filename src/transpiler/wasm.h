#ifndef XS_WASM_H
#define XS_WASM_H

#include "core/ast.h"

int transpile_wasm(Node *program, const char *filename, const char *out_path);

#endif /* XS_WASM_H */
