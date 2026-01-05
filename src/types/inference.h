#ifndef XS_INFERENCE_H
#define XS_INFERENCE_H

/* HM type inference for XS. */

#include "core/ast.h"
#include "types/types.h"

typedef struct InferResult InferResult;

InferResult *infer_types(Node *program);
const char  *infer_type_at(InferResult *r, int node_id);
int          infer_error_count(InferResult *r);
const char  *infer_error_at(InferResult *r, int idx);
void         infer_result_free(InferResult *r);

/* legacy */
typedef struct InferCtx InferCtx;

InferCtx *infer_new(void);
void      infer_free(InferCtx *ctx);
XsType   *infer_expr(InferCtx *ctx, Node *expr);
int       infer_check(InferCtx *ctx, Node *expr, XsType *expected);

#endif /* XS_INFERENCE_H */
