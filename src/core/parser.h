#ifndef PARSER_H
#define PARSER_H

#include "core/lexer.h"
#include "core/ast.h"
#include "diagnostic/diagnostic.h"

typedef struct {
    char  msg[256];
    Span  span;
} ParseError;

typedef struct {
    Token      *tokens;
    int         ntokens;
    int         pos;
    const char *filename;
    ParseError  error;
    int         had_error;
    int         no_arrow_lambda; /* when set, (expr) => is NOT a lambda */
    DiagContext *diag;     /* unified diagnostic engine (may be NULL) */
} Parser;

void  parser_init(Parser *p, TokenArray *ta, const char *filename);
Node *parser_parse(Parser *p);   /* returns Program node, or NULL on fatal error */

#endif /* PARSER_H */
