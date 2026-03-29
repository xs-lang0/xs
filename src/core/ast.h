#ifndef AST_H
#define AST_H

#include "core/xs.h"

/* Span (source location) */
typedef struct {
    const char *file;
    int line, col;
    int end_line, end_col;
    int offset, length;
} Span;

static inline Span span_zero(void) {
    Span s = {0};
    s.file = "<unknown>";
    return s;
}

// TypeExpr (parsed type annotation)
typedef struct TypeExpr TypeExpr;
struct TypeExpr {
    enum {
        TEXPR_NAMED,    /* Foo or Foo<T,U>: .name, .args, .nargs */
        TEXPR_ARRAY,    /* [T]: .inner */
        TEXPR_TUPLE,    /* (A, B): .elems, .nelems */
        TEXPR_FN,       /* fn(A,B)->R: .params, .nparams, .ret */
        TEXPR_OPTION,   /* T?: .inner */
        TEXPR_INFER,    /* _ (placeholder) */
    } kind;
    char      *name;
    TypeExpr **args;    int nargs;
    TypeExpr  *inner;
    TypeExpr **elems;   int nelems;
    TypeExpr **params;  int nparams;
    TypeExpr  *ret;
    Span       span;
};
void typeexpr_free(TypeExpr *te);

/* Node tags */
typedef enum {
    NODE_LIT_INT,
    NODE_LIT_BIGINT,
    NODE_LIT_FLOAT,
    NODE_LIT_STRING,
    NODE_LIT_BOOL,
    NODE_LIT_NULL,
    NODE_LIT_CHAR,
    NODE_LIT_ARRAY,
    NODE_LIT_TUPLE,
    NODE_LIT_MAP,

    NODE_IDENT,
    NODE_BINOP,
    NODE_UNARY,
    NODE_ASSIGN,
    NODE_CALL,
    NODE_METHOD_CALL,
    NODE_INDEX,
    NODE_FIELD,
    NODE_SCOPE,       /* A::B::C */
    NODE_IF,
    NODE_MATCH,
    NODE_WHILE,
    NODE_FOR,
    NODE_LOOP,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_RETURN,
    NODE_YIELD,
    NODE_THROW,
    NODE_TRY,
    NODE_DEFER,
    NODE_LAMBDA,
    NODE_BLOCK,
    NODE_CAST,
    NODE_RANGE,
    NODE_STRUCT_INIT,
    NODE_SPREAD,
    NODE_LIST_COMP,
    NODE_MAP_COMP,
    NODE_INTERP_STRING, /* string with interpolation */
    NODE_LIT_REGEX,     /* /pattern/ regex literal */

    NODE_LET,
    NODE_VAR,
    NODE_CONST,
    NODE_EXPR_STMT,

    NODE_FN_DECL,
    NODE_CLASS_DECL,
    NODE_STRUCT_DECL,
    NODE_ENUM_DECL,
    NODE_IMPL_DECL,
    NODE_TRAIT_DECL,
    NODE_IMPORT,
    NODE_USE,
    NODE_MODULE_DECL,
    NODE_TYPE_ALIAS,

    // patterns
    NODE_PAT_WILD,
    NODE_PAT_IDENT,
    NODE_PAT_LIT,
    NODE_PAT_TUPLE,
    NODE_PAT_STRUCT,
    NODE_PAT_ENUM,
    NODE_PAT_OR,
    NODE_PAT_RANGE,
    NODE_PAT_SLICE,
    NODE_PAT_GUARD,
    NODE_PAT_EXPR,
    NODE_PAT_CAPTURE,       /* x @ pattern */
    NODE_PAT_STRING_CONCAT, /* "prefix" ++ rest */
    NODE_PAT_REGEX,         /* r"\d+" regex match */

    // effects
    NODE_EFFECT_DECL,
    NODE_PERFORM,
    NODE_HANDLE,
    NODE_RESUME,

    // async
    NODE_AWAIT,
    NODE_NURSERY,
    NODE_SPAWN,

    // actors
    NODE_ACTOR_DECL,
    NODE_SEND_EXPR,     /* actor ! message */

    NODE_INLINE_C,
    NODE_TAG_DECL,
    NODE_BIND,
    NODE_ADAPT_FN,

    NODE_LIT_DURATION,
    NODE_LIT_COLOR,
    NODE_LIT_DATE,
    NODE_LIT_SIZE,
    NODE_LIT_ANGLE,
    NODE_EVERY,
    NODE_AFTER,
    NODE_TIMEOUT,
    NODE_DEBOUNCE,

    NODE_PROGRAM,
} NodeTag;

typedef struct Node Node;

/* Node arrays */
typedef struct {
    Node  **items;
    int     len, cap;
} NodeList;

NodeList  nodelist_new(void);
void      nodelist_push(NodeList *nl, Node *n);
void      nodelist_free(NodeList *nl);

/* key-value pair (kwargs, struct fields) */
typedef struct {
    char *key;
    Node *val;
} NodePair;

typedef struct {
    NodePair *items;
    int        len, cap;
} NodePairList;

NodePairList nodepairlist_new(void);
void         nodepairlist_push(NodePairList *pl, const char *k, Node *v);
void         nodepairlist_free(NodePairList *pl);

typedef struct {
    Node        *pattern;    /* PatIdent or PatWild typically */
    char        *name;       /* shortcut: non-NULL for simple params */
    Node        *default_val;/* may be NULL */
    int          variadic;
    int          keyword_only;
    TypeExpr    *type_ann;   /* may be NULL */
    Node        *contract;   /* where clause, may be NULL */
    Span         span;
} Param;

typedef struct {
    Param  *items;
    int     len, cap;
} ParamList;

ParamList paramlist_new(void);
void      paramlist_push(ParamList *pl, Param p);
void      paramlist_free(ParamList *pl);

/* Match arm */
typedef struct {
    Node *pattern;
    Node *guard;   /* NULL if none */
    Node *body;    /* Block or expr */
    Span  span;
} MatchArm;

typedef struct {
    char     *effect_name; /* "Log" */
    char     *op_name;     /* "log" */
    ParamList params;      /* handler parameters */
    Node     *body;
    Span      span;
} EffectArm;

typedef struct {
    EffectArm *items;
    int        len, cap;
} EffectArmList;

EffectArmList effectarmlist_new(void);
void          effectarmlist_push(EffectArmList *el, EffectArm arm);
void          effectarmlist_free(EffectArmList *el);

typedef struct {
    MatchArm *items;
    int       len, cap;
} MatchArmList;

MatchArmList  matcharmlist_new(void);
void          matcharmlist_push(MatchArmList *ml, MatchArm arm);
void          matcharmlist_free(MatchArmList *ml);

typedef struct {
    char     *name;
    NodeList  fields;     /* tuple fields or struct fields */
    int       is_struct;  /* 1 = struct-style, 0 = tuple-style */
    NodeList  field_names;/* parallel to fields if is_struct */
    Span      span;
} EnumVariant;

typedef struct {
    EnumVariant *items;
    int          len, cap;
} EnumVariantList;

EnumVariantList enumvariantlist_new(void);
void            enumvariantlist_push(EnumVariantList *el, EnumVariant v);
void            enumvariantlist_free(EnumVariantList *el);

/* The Node union */
struct Node {
    NodeTag tag;
    Span    span;
    union {
        struct { int64_t ival; } lit_int;
        struct { char *bigint_str; } lit_bigint;
        struct { double fval; } lit_float;

        struct {
            char    *sval;       /* full raw string */
            NodeList parts;      /* alternating str/expr for interp */
            int      interpolated;
        } lit_string;

        struct { char *pattern; } lit_regex;

        struct { int bval; } lit_bool;
        struct { char cval; } lit_char;
        struct { NodeList elems; Node *repeat_val; int64_t repeat_cnt; } lit_array;
        struct { NodeList keys; NodeList vals; } lit_map;
        struct { char *name; } ident;
        struct { char op[8]; Node *left; Node *right; } binop;
        struct { char op[4]; Node *expr; int prefix; } unary;
        struct { char op[4]; Node *target; Node *value; } assign;

        struct {
            Node        *callee;
            NodeList     args;
            NodePairList kwargs;
        } call;

        struct {
            Node        *obj;
            char        *method;
            NodeList     args;
            NodePairList kwargs;
            int          optional;
        } method_call;

        struct { Node *obj; Node *index; } index;
        struct { Node *obj; char *name; int optional; } field;
        struct { char **parts; int nparts; } scope;

        struct {
            Node        *cond;
            Node        *then;       /* always a Block */
            NodeList     elif_conds; /* parallel */
            NodeList     elif_thens;
            Node        *else_branch; /* NULL or Block */
        } if_expr;

        struct {
            Node        *subject;
            MatchArmList arms;
        } match;

        struct { Node *cond; Node *body; char *label; } while_loop;
        struct { Node *pattern; Node *iter; Node *body; char *label; } for_loop;
        struct { Node *body; char *label; } loop;
        struct { char *label; Node *value; } brk;
        struct { char *label; } cont;
        struct { Node *value; } ret;
        struct { Node *value; } yield_;
        struct { Node *value; } throw_;
        struct { Node *body; } defer_;

        struct {
            Node        *body;
            MatchArmList catch_arms;
            Node        *finally_block;
        } try_;

        struct {
            ParamList params;
            Node     *body;
            int       is_generator; /* 1 if fn* lambda */
        } lambda;

        struct {
            NodeList stmts;
            Node    *expr;        /* trailing expression */
            int      has_decls;   /* cached: -1=unknown, 0=no, 1=yes */
            int      is_unsafe;   /* 1 if from 'unsafe { }' */
        } block;

        struct { Node *expr; char *type_name; } cast;
        struct { Node *start; Node *end; int inclusive; } range;

        struct {
            char        *path;     /* "Point" or "Shape::Circle" */
            NodePairList fields;
            Node        *rest;     /* ..base spread */
        } struct_init;

        struct { Node *expr; } spread;

        struct {
            Node    *element;
            NodeList clause_pats;  /* parallel arrays */
            NodeList clause_iters;
            NodeList clause_conds;
        } list_comp;

        struct {
            Node    *key;
            Node    *value;
            NodeList clause_pats;
            NodeList clause_iters;
            NodeList clause_conds;
        } map_comp;

        struct {
            Node        *pattern;
            char        *name;
            Node        *value;
            int          mutable;
            TypeExpr    *type_ann;
            Node        *contract;   /* where clause, may be NULL */
        } let;

        struct { char *name; Node *value; TypeExpr *type_ann; Node *contract; } const_;
        struct { Node *expr; int has_semicolon; } expr_stmt;

        struct {
            char      *name;
            ParamList  params;
            Node      *body;
            int        is_async;
            int        is_pub;
            int        is_generator;
            int        is_pure;
            int        is_test;
            int        is_static;
            char      *deprecated_msg; /* NULL = not deprecated */
            TypeExpr  *ret_type;
            char     **type_params;
            TypeExpr **type_bounds;
            int        n_type_params;
        } fn_decl;

        struct {
            char    *name;
            char   **bases;
            int      nbases;
            NodeList members;
        } class_decl;

        struct {
            char        *name;
            NodePairList fields;
            TypeExpr   **field_types;  /* parallel to fields, NULL entries for untyped */
            int          n_field_types;
            char       **type_params;
            int          n_type_params;
            char       **derives;
            int          n_derives;
        } struct_decl;

        struct {
            char           *name;
            EnumVariantList variants;
            char          **type_params;
            int             n_type_params;
        } enum_decl;

        struct {
            char    *type_name;
            char    *trait_name;
            NodeList members;
        } impl_decl;

        struct {
            char    *name;
            char   **assoc_types;
            int      n_assoc_types;
            char   **method_names;
            int      n_methods;
            char    *super_trait;
            NodeList methods;
        } trait_decl;

        struct {
            char **path;
            int    nparts;
            char  *alias;
            char **items;
            int    nitems;
        } import;

        struct {
            char  *path;        /* file path from the string literal */
            char  *alias;       /* from 'as name', or derived from filename */
            char **names;       /* selective imports: original names */
            char **name_aliases;/* parallel: renamed name (or same as names[i]) */
            int    nnames;
            int    import_all;  /* 1 = namespace import, 0 = selective */
            int    is_plugin;   /* 1 = use plugin "path" */
            int    sandbox_flags; /* bitfield: 1=inject_only, 2=no_override, 4=no_eval_hook */
        } use_;

        struct {
            char    *name;
            NodeList body;
        } module_decl;

        struct { char *name; char *target; } type_alias;

        struct {
            char     *name;
            NodeList  ops;
        } effect_decl;

        struct {
            char    *effect_name;
            char    *op_name;
            NodeList args;
        } perform;

        struct {
            Node         *expr;
            EffectArmList arms;
        } handle;

        struct { Node *value; } resume_;
        struct { Node *expr; } await_;
        struct { Node *body; } nursery_;
        struct { Node *expr; } spawn_;

        struct {
            char     *name;
            NodePairList state_fields;
            NodeList methods;
        } actor_decl;

        struct {
            Node *target;
            Node *message;
        } send_expr;

        struct { int dummy; } pat_wild;

        struct {
            char *name;
            int   mutable;
        } pat_ident;

        struct {
            int64_t  ival;
            double   fval;
            char    *sval;
            int      tag;  /* 0=int,1=float,2=str,3=bool,4=null */
            int      bval;
        } pat_lit;

        struct { NodeList elems; } pat_tuple;

        struct {
            char        *path;
            NodePairList fields;  /* name -> sub-pattern */
            NodeList     defaults; /* parallel to fields: default expr or NULL */
            int          rest;
        } pat_struct;

        struct {
            char    *path;
            NodeList args;
        } pat_enum;

        struct { Node *left; Node *right; } pat_or;

        struct {
            Node *start; Node *end;
            int   inclusive;
        } pat_range;

        struct {
            NodeList elems;
            char    *rest; /* ..name or NULL */
        } pat_slice;

        struct {
            Node *pattern;
            Node *guard;
        } pat_guard;
        struct { Node *expr; } pat_expr;

        struct {
            char *name;
            Node *pattern;
        } pat_capture;

        struct {
            char *prefix;
            Node *rest;
        } pat_str_concat;

        struct {
            char *pattern;  /* regex pattern string */
        } pat_regex;

        struct { char *code; } inline_c;

        struct {
            char      *name;
            ParamList  params;
            Node      *body;
            int        is_pub;
        } tag_decl;

        struct {
            char *name;
            Node *expr;
        } bind_decl;

        struct {
            char      *name;
            ParamList  params;
            TypeExpr  *ret_type;
            int        is_pub;
            char     **targets;     /* "native", "js", "wasm" */
            Node     **bodies;      /* parallel block nodes */
            int        nbranches;
        } adapt_fn;

        struct { double ms; } lit_duration;     /* stored as milliseconds */
        struct { int r, g, b, a; } lit_color;   /* RGBA 0-255 */
        struct { char *value; } lit_date;       /* ISO string */
        struct { double bytes; } lit_size;      /* stored as bytes */
        struct { double radians; } lit_angle;   /* stored as radians */

        struct { Node *interval; Node *body; } every_;
        struct { Node *delay; Node *body; } after_;
        struct { Node *duration; Node *body; Node *fallback; } timeout_;
        struct { Node *delay; Node *body; } debounce_;

        struct { NodeList stmts; } program;
    };
};

Node *node_new(NodeTag tag, Span span);
void  node_free(Node *n);
Node *program_new(NodeList stmts, Span span);

#endif /* AST_H */
