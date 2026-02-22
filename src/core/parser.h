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
    int         error_count;     /* total parse errors reported */
    int         max_errors;      /* stop after this many (default 10) */
    int         panic_mode;      /* in panic mode, suppress new errors until sync */
    int         no_arrow_lambda; /* when set, (expr) => is NOT a lambda */
    DiagContext *diag;     /* unified diagnostic engine (may be NULL) */
} Parser;

void  parser_init(Parser *p, TokenArray *ta, const char *filename);
Node *parser_parse(Parser *p);   /* returns Program node, or NULL on fatal error */

/* phase 2: exported parser accessors for plugin system */
Node  *parser_parse_expr(Parser *p, int min_prec);
Node  *parser_parse_block(Parser *p);
Token *parser_peek(Parser *p, int offset);
Token *parser_advance(Parser *p);
int    parser_check(Parser *p, TokenKind kind);
Token *parser_expect(Parser *p, TokenKind kind, const char *msg);

/* phase 2: plugin syntax handler function pointers (set by interp.c) */
extern Node *(*g_plugin_try_syntax_handler)(Parser *p, Token *tok);
extern Node *(*g_plugin_try_syntax_expr_handler)(Parser *p, Token *tok);
extern int (*g_plugin_is_keyword)(const char *word);

/* phase 3: parser override hook (set by interp.c) */
extern Node *(*g_plugin_try_parser_override)(Parser *p, const char *keyword);

/* parse individual constructs (exposed for override chaining) */
Node *parser_parse_if(Parser *p);
Node *parser_parse_for(Parser *p);
Node *parser_parse_while(Parser *p);
Node *parser_parse_match(Parser *p);
Node *parser_parse_fn_decl(Parser *p, int is_pub, int is_async, int is_pure);

#endif /* PARSER_H */
