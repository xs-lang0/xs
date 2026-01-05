#ifndef LEXER_H
#define LEXER_H

#include "core/xs.h"
#include "core/ast.h"

typedef enum {
    TK_INT,
    TK_BIGINT,
    TK_FLOAT,
    TK_STRING,
    TK_RAW_STRING,
    TK_CHAR,
    TK_BOOL,
    TK_NULL,

    TK_IDENT,

    TK_IF, TK_ELSE, TK_ELIF, TK_WHILE, TK_FOR, TK_IN,
    TK_LOOP, TK_BREAK, TK_CONTINUE, TK_RETURN, TK_MATCH, TK_WHEN,
    TK_YIELD,

    TK_FN, TK_LET, TK_VAR, TK_CONST, TK_TYPE, TK_CLASS,
    TK_STRUCT, TK_ENUM, TK_TRAIT, TK_IMPL,
    TK_MODULE, TK_IMPORT, TK_EXPORT, TK_AS, TK_FROM,
    TK_ASYNC, TK_AWAIT, TK_ACTOR, TK_SPAWN, TK_NURSERY, TK_MACRO,

    TK_PUB, TK_MUT, TK_STATIC,
    TK_AND, TK_OR, TK_NOT, TK_IS, TK_TRY, TK_CATCH, TK_FINALLY,
    TK_THROW, TK_ASSERT, TK_PANIC, TK_DEFER,
    TK_SELF, TK_SUPER, TK_UNSAFE,

    TK_EFFECT, TK_PERFORM, TK_HANDLE, TK_RESUME,

    // operators
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_POWER, TK_FLOORDIV, TK_CONCAT,
    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LE, TK_GE, TK_SPACESHIP,
    TK_OP_AND, TK_OP_OR, TK_OP_NOT,
    TK_AMP, TK_PIPE, TK_CARET, TK_TILDE, TK_SHL, TK_SHR,

    TK_ASSIGN,
    TK_PLUS_ASSIGN, TK_MINUS_ASSIGN, TK_STAR_ASSIGN,
    TK_SLASH_ASSIGN, TK_PERCENT_ASSIGN,
    TK_AND_ASSIGN, TK_OR_ASSIGN, TK_XOR_ASSIGN,
    TK_SHL_ASSIGN, TK_SHR_ASSIGN, TK_POWER_ASSIGN,

    TK_ARROW, TK_FAT_ARROW,
    TK_DOT, TK_DOTDOT, TK_DOTDOTDOT, TK_DOTDOTEQ,
    TK_QUESTION, TK_QUESTION_DOT, TK_NULL_COALESCE,
    TK_PIPE_ARROW, TK_AT,

    TK_LPAREN, TK_RPAREN,
    TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET,

    TK_COMMA, TK_SEMICOLON, TK_COLON, TK_COLON_COLON,
    TK_MACRO_BANG, TK_HASH_BRACKET,
    TK_NEWLINE,

    TK_EOF,
    TK_UNKNOWN,
} TokenKind;

/* Token */
typedef struct {
    TokenKind kind;
    Span      span;
    union {
        int64_t  ival;   /* TK_INT, TK_BOOL */
        double   fval;   /* TK_FLOAT */
        char    *sval;   /* TK_STRING, TK_IDENT, TK_CHAR, etc. */
    };
} Token;

typedef struct {
    Token *items;
    int    len, cap;
} TokenArray;

typedef struct {
    int   line, col;
    char *text;       /* includes delimiters */
    int   is_block;
} Comment;

typedef struct {
    Comment *items;
    int      len, cap;
} CommentList;

void comment_list_free(CommentList *cl);

typedef struct {
    const char *source;
    const char *filename;
    int         pos;
    int         line, col;
    int         paren_depth;
    TokenArray  tokens;
    CommentList comments;
} Lexer;

void       lexer_init(Lexer *l, const char *source, const char *filename);
TokenArray lexer_tokenize(Lexer *l);
void       lexer_free(Lexer *l);

void       token_array_free(TokenArray *ta);
const char *token_kind_name(TokenKind k);

#endif /* LEXER_H */
