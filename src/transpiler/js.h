#ifndef XS_JS_H
#define XS_JS_H

#include "core/ast.h"

char *transpile_js(Node *program, const char *filename);

#endif /* XS_JS_H */
