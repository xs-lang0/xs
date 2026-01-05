#ifndef XS_ERROR_H
#define XS_ERROR_H

#include "core/value.h"
#include "core/ast.h"

/* error values are XS_MAPs with "kind", "message", optional "cause" */
Value *xs_error_new(const char *kind, const char *message, Value *cause);
Value *xs_error_from_str(const char *message);
const char *xs_error_kind(Value *err);
const char *xs_error_message(Value *err);
Value      *xs_error_cause(Value *err);

void xs_runtime_error(Span span, const char *label, const char *hint,
                      const char *fmt, ...);
void xs_error_set_source(const char *source);

#endif
