#ifndef XS_DOCGEN_H
#define XS_DOCGEN_H
#include "core/ast.h"

/* format: "html" | "markdown" | "json".  Caller frees result. */
char *docgen_generate(Node *program, const char *filename, const char *format);

#endif
