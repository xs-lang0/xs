#include "lsp/lsp.h"
#include "core/xs.h"
#include "core/ast.h"
#include "core/lexer.h"
#include "core/parser.h"
#include "core/value.h"
#include "core/env.h"
#include "semantic/sema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define LSP_BUF_SMALL   1024
#define LSP_BUF_MEDIUM  4096
#define LSP_BUF_LARGE   16384
#define LSP_BUF_HUGE    65536
#define LSP_MAX_DOCS    128
#define LSP_MAX_IDENTS  512


static const char *xs_builtin_fns[] = {
    "print", "println", "input", "len", "type", "range",
    "keys", "values", "entries", "map", "filter", "reduce",
    "zip", "any", "all", "min", "max", "sum", "sort",
    "reverse", "push", "pop", "slice", "join", "split",
    "contains", "starts_with", "ends_with", "trim",
    "upper", "lower", "replace", "int", "float", "str",
    "bool", "char", "typeof", "assert", "panic",
    NULL
};

static const char *xs_keywords[] = {
    "fn", "let", "var", "const", "if", "else", "elif",
    "while", "for", "in", "loop", "match", "when",
    "struct", "enum", "trait", "impl", "import", "export",
    "return", "break", "continue", "true", "false", "null",
    "and", "or", "not", "is", "try", "catch", "finally",
    "throw", "defer", "yield", "async", "await",
    "pub", "mut", "static", "self", "super",
    "module", "class", "type", "as", "from",
    "effect", "perform", "handle", "resume",
    "spawn", "nursery", "macro",
    NULL
};

static const char *xs_stdlib_modules[] = {
    "math", "time", "string", "path", "base64", "hash",
    "uuid", "collections", "random", "json", "log", "fmt",
    "csv", "url", "re", "process", "io", "async", "net",
    "crypto", "thread", "buf", "encode", "db", "cli",
    "ffi", "reflect", "gc", "reactive", "os",
    NULL
};


static const char *xs_string_methods[] = {
    "len", "upper", "lower", "trim", "contains", "starts_with",
    "ends_with", "split", "replace", "chars", "bytes", "repeat",
    "join", "slice", "index_of", "parse_int", "parse_float",
    "is_empty", "reverse", "capitalize", "pad_left", "pad_right",
    "count", NULL
};

static const char *xs_array_methods[] = {
    "len", "push", "pop", "first", "last", "contains", "map",
    "filter", "reduce", "sort", "reverse", "join", "slice",
    "index_of", "find", "any", "all", "flatten", "enumerate",
    "sum", "min", "max", "unique", "concat", "insert", "remove",
    NULL
};

static const char *xs_map_methods[] = {
    "keys", "values", "entries", "len", "has", "get", "set",
    "delete", "contains_key", NULL
};

static const char *xs_number_methods[] = {
    "abs", "pow", "sqrt", "floor", "ceil", "round", "clamp",
    "to_str", "is_even", "is_odd", NULL
};


typedef struct {
    char *uri;
    char *text;
    Node *ast;          /* cached parse tree, or NULL */
    int   n_idents;
    char *idents[LSP_MAX_IDENTS]; /* identifiers found in document */
} LspDocument;

static LspDocument g_docs[LSP_MAX_DOCS];
static int         g_ndocs = 0;

static LspDocument *doc_find(const char *uri) {
    if (!uri) return NULL;
    for (int i = 0; i < g_ndocs; i++) {
        if (g_docs[i].uri && strcmp(g_docs[i].uri, uri) == 0)
            return &g_docs[i];
    }
    return NULL;
}

static void doc_clear_idents(LspDocument *doc) {
    for (int i = 0; i < doc->n_idents; i++) free(doc->idents[i]);
    doc->n_idents = 0;
}

static void doc_add_ident(LspDocument *doc, const char *name) {
    if (!name || doc->n_idents >= LSP_MAX_IDENTS) return;
    for (int i = 0; i < doc->n_idents; i++) {
        if (strcmp(doc->idents[i], name) == 0) return;
    }
    doc->idents[doc->n_idents++] = xs_strdup(name);
}

static void doc_free_ast(LspDocument *doc) {
    if (doc->ast) { node_free(doc->ast); doc->ast = NULL; }
}

static LspDocument *doc_open(const char *uri, const char *text) {
    LspDocument *d = doc_find(uri);
    if (!d) {
        if (g_ndocs >= LSP_MAX_DOCS) return NULL;
        d = &g_docs[g_ndocs++];
        memset(d, 0, sizeof(*d));
    } else {
        free(d->text);
        doc_free_ast(d);
        doc_clear_idents(d);
        free(d->uri);
    }
    d->uri = xs_strdup(uri);
    d->text = xs_strdup(text ? text : "");
    d->ast = NULL;
    return d;
}


static void collect_idents(LspDocument *doc, Node *n) {
    if (!n) return;
    switch (n->tag) {
    case NODE_IDENT:
        doc_add_ident(doc, n->ident.name);
        break;
    case NODE_FN_DECL:
        if (n->fn_decl.name) doc_add_ident(doc, n->fn_decl.name);
        if (n->fn_decl.body) collect_idents(doc, n->fn_decl.body);
        for (int i = 0; i < n->fn_decl.params.len; i++) {
            if (n->fn_decl.params.items[i].name)
                doc_add_ident(doc, n->fn_decl.params.items[i].name);
        }
        break;
    case NODE_LET: case NODE_VAR:
        if (n->let.name) doc_add_ident(doc, n->let.name);
        if (n->let.value) collect_idents(doc, n->let.value);
        break;
    case NODE_CONST:
        if (n->const_.name) doc_add_ident(doc, n->const_.name);
        if (n->const_.value) collect_idents(doc, n->const_.value);
        break;
    case NODE_STRUCT_DECL:
        if (n->struct_decl.name) doc_add_ident(doc, n->struct_decl.name);
        break;
    case NODE_ENUM_DECL:
        if (n->enum_decl.name) doc_add_ident(doc, n->enum_decl.name);
        break;
    case NODE_TRAIT_DECL:
        if (n->trait_decl.name) doc_add_ident(doc, n->trait_decl.name);
        break;
    case NODE_PROGRAM:
        for (int i = 0; i < n->program.stmts.len; i++)
            collect_idents(doc, n->program.stmts.items[i]);
        break;
    case NODE_BLOCK:
        for (int i = 0; i < n->block.stmts.len; i++)
            collect_idents(doc, n->block.stmts.items[i]);
        if (n->block.expr) collect_idents(doc, n->block.expr);
        break;
    case NODE_EXPR_STMT:
        collect_idents(doc, n->expr_stmt.expr);
        break;
    case NODE_IF:
        collect_idents(doc, n->if_expr.cond);
        collect_idents(doc, n->if_expr.then);
        for (int i = 0; i < n->if_expr.elif_conds.len; i++) {
            collect_idents(doc, n->if_expr.elif_conds.items[i]);
            collect_idents(doc, n->if_expr.elif_thens.items[i]);
        }
        if (n->if_expr.else_branch) collect_idents(doc, n->if_expr.else_branch);
        break;
    case NODE_WHILE:
        collect_idents(doc, n->while_loop.cond);
        collect_idents(doc, n->while_loop.body);
        break;
    case NODE_FOR:
        collect_idents(doc, n->for_loop.pattern);
        collect_idents(doc, n->for_loop.iter);
        collect_idents(doc, n->for_loop.body);
        break;
    case NODE_CALL:
        collect_idents(doc, n->call.callee);
        for (int i = 0; i < n->call.args.len; i++)
            collect_idents(doc, n->call.args.items[i]);
        break;
    case NODE_BINOP:
        collect_idents(doc, n->binop.left);
        collect_idents(doc, n->binop.right);
        break;
    case NODE_UNARY:
        collect_idents(doc, n->unary.expr);
        break;
    case NODE_ASSIGN:
        collect_idents(doc, n->assign.target);
        collect_idents(doc, n->assign.value);
        break;
    case NODE_RETURN:
        if (n->ret.value) collect_idents(doc, n->ret.value);
        break;
    case NODE_LAMBDA:
        if (n->lambda.body) collect_idents(doc, n->lambda.body);
        break;
    case NODE_IMPORT:
        break;
    case NODE_MATCH:
        collect_idents(doc, n->match.subject);
        for (int i = 0; i < n->match.arms.len; i++) {
            collect_idents(doc, n->match.arms.items[i].pattern);
            collect_idents(doc, n->match.arms.items[i].body);
        }
        break;
    case NODE_INDEX:
        collect_idents(doc, n->index.obj);
        collect_idents(doc, n->index.index);
        break;
    case NODE_FIELD:
        collect_idents(doc, n->field.obj);
        break;
    case NODE_METHOD_CALL:
        collect_idents(doc, n->method_call.obj);
        for (int i = 0; i < n->method_call.args.len; i++)
            collect_idents(doc, n->method_call.args.items[i]);
        break;
    case NODE_LIT_ARRAY:
        for (int i = 0; i < n->lit_array.elems.len; i++)
            collect_idents(doc, n->lit_array.elems.items[i]);
        break;
    case NODE_LIT_MAP:
        for (int i = 0; i < n->lit_map.keys.len; i++) {
            collect_idents(doc, n->lit_map.keys.items[i]);
            collect_idents(doc, n->lit_map.vals.items[i]);
        }
        break;
    case NODE_IMPL_DECL:
        for (int i = 0; i < n->impl_decl.members.len; i++)
            collect_idents(doc, n->impl_decl.members.items[i]);
        break;
    case NODE_MODULE_DECL:
        if (n->module_decl.name) doc_add_ident(doc, n->module_decl.name);
        for (int i = 0; i < n->module_decl.body.len; i++)
            collect_idents(doc, n->module_decl.body.items[i]);
        break;
    case NODE_TRY:
        collect_idents(doc, n->try_.body);
        if (n->try_.finally_block) collect_idents(doc, n->try_.finally_block);
        break;
    default:
        break;
    }
}

/* JSON helpers */

static char *json_get_string(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') p++;
    if (*p != '"') return NULL;
    p++;
    const char *start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) p += 2;
        else p++;
    }
    size_t len = (size_t)(p - start);
    char *result = malloc(len + 1);
    if (!result) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;
            switch (start[i]) {
                case 'n': result[j++] = '\n'; break;
                case 't': result[j++] = '\t'; break;
                case 'r': result[j++] = '\r'; break;
                case '"': result[j++] = '"';  break;
                case '\\': result[j++] = '\\'; break;
                default: result[j++] = start[i]; break;
            }
        } else {
            result[j++] = start[i];
        }
    }
    result[j] = '\0';
    return result;
}

static int json_get_int(const char *json, const char *key) {
    if (!json || !key) return -1;
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') p++;
    return atoi(p);
}


static void json_escape_into(char *dst, size_t dstsz, const char *src) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 6 < dstsz; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
        case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
        case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
        case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
        case '\r': dst[j++] = '\\'; dst[j++] = 'r';  break;
        case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
        default:
            if (c < 0x20) {
                j += (size_t)snprintf(dst + j, dstsz - j, "\\u%04x", c);
            } else {
                dst[j++] = (char)c;
            }
            break;
        }
    }
    dst[j] = '\0';
}


static char *lsp_read_message(void) {
    char header[512];
    int content_length = -1;
    while (fgets(header, sizeof(header), stdin)) {
        size_t len = strlen(header);
        while (len > 0 && (header[len-1] == '\r' || header[len-1] == '\n'))
            header[--len] = '\0';
        if (len == 0) break;
        if (strncmp(header, "Content-Length:", 15) == 0)
            content_length = atoi(header + 15);
    }
    if (content_length <= 0) return NULL;
    char *body = malloc((size_t)content_length + 1);
    if (!body) return NULL;
    size_t nread = fread(body, 1, (size_t)content_length, stdin);
    body[nread] = '\0';
    if ((int)nread < content_length) { free(body); return NULL; }
    return body;
}

static void lsp_write_message(const char *json) {
    int len = (int)strlen(json);
    fprintf(stdout, "Content-Length: %d\r\n\r\n%s", len, json);
    fflush(stdout);
}


static void lsp_send_response(int id, const char *result_json) {
    char *buf = malloc(strlen(result_json) + 128);
    if (!buf) return;
    sprintf(buf, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":%s}", id, result_json);
    lsp_write_message(buf);
    free(buf);
}

static void lsp_send_notification(const char *method, const char *params_json) {
    char *buf = malloc(strlen(method) + strlen(params_json) + 128);
    if (!buf) return;
    sprintf(buf, "{\"jsonrpc\":\"2.0\",\"method\":\"%s\",\"params\":%s}", method, params_json);
    lsp_write_message(buf);
    free(buf);
}

static void lsp_send_error(int id, int code, const char *message) {
    char buf[LSP_BUF_MEDIUM];
    char escaped[LSP_BUF_SMALL];
    json_escape_into(escaped, sizeof(escaped), message);
    snprintf(buf, sizeof(buf),
        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":{\"code\":%d,\"message\":\"%s\"}}",
        id, code, escaped);
    lsp_write_message(buf);
}


typedef struct {
    char msg[512];
    int  line;
    int  col;
    int  end_line;
    int  end_col;
    int  severity; /* 1=Error, 2=Warning, 3=Info, 4=Hint */
} LspDiag;

#define LSP_MAX_DIAGS 256

static int parse_and_analyze(LspDocument *doc, LspDiag *diags, int max_diags) {
    int n_diags = 0;

    doc_free_ast(doc);
    doc_clear_idents(doc);

    if (!doc->text || strlen(doc->text) == 0) return 0;

    Lexer lex;
    lexer_init(&lex, doc->text, doc->uri ? doc->uri : "<lsp>");
    TokenArray ta = lexer_tokenize(&lex);

    Parser parser;
    parser_init(&parser, &ta, doc->uri ? doc->uri : "<lsp>");
    Node *program = parser_parse(&parser);

    if (!program || parser.had_error) {
            if (n_diags < max_diags) {
            LspDiag *d = &diags[n_diags++];
            snprintf(d->msg, sizeof(d->msg), "%s",
                     parser.error.msg[0] ? parser.error.msg : "parse error");
            d->line     = parser.error.span.line > 0 ? parser.error.span.line - 1 : 0;
            d->col      = parser.error.span.col  > 0 ? parser.error.span.col  - 1 : 0;
            d->end_line = d->line;
            d->end_col  = d->col + 1;
            d->severity = 1; /* Error */
        }
        if (program) node_free(program);
        token_array_free(&ta);
        return n_diags;
    }

    token_array_free(&ta);

    doc->ast = program;
    collect_idents(doc, program);

    SemaCtx sema;
    sema_init(&sema, 1, 0); /* lenient mode */
    sema_analyze(&sema, program, doc->uri ? doc->uri : "<lsp>");

    if (sema.diag) {
        for (int i = 0; i < sema.diag->n_items && n_diags < max_diags; i++) {
            Diagnostic *di = &sema.diag->items[i];
            LspDiag *d = &diags[n_diags++];
            snprintf(d->msg, sizeof(d->msg), "%s", di->message ? di->message : "semantic error");
            int src_line = 0, src_col = 0;
            if (di->n_annotations > 0) {
                src_line = di->annotations[0].span.line;
                src_col  = di->annotations[0].span.col;
            }
            d->line     = src_line > 0 ? src_line - 1 : 0;
            d->col      = src_col  > 0 ? src_col  - 1 : 0;
            d->end_line = d->line;
            d->end_col  = d->col + 1;
            d->severity = (di->severity == DIAG_WARNING) ? 2 : 1;
        }
    }

    sema_free(&sema);

    return n_diags;
}


static void lsp_send_diagnostics(const char *uri, const char *source_text) {
    LspDocument *doc = doc_open(uri, source_text);
    if (!doc) {
        char buf[LSP_BUF_MEDIUM];
        snprintf(buf, sizeof(buf), "{\"uri\":\"%s\",\"diagnostics\":[]}", uri ? uri : "");
        lsp_send_notification("textDocument/publishDiagnostics", buf);
        return;
    }

    LspDiag diags[LSP_MAX_DIAGS];
    int n = parse_and_analyze(doc, diags, LSP_MAX_DIAGS);

    size_t bufsz = LSP_BUF_HUGE;
    char *buf = malloc(bufsz);
    if (!buf) return;

    int off = 0;
    off += snprintf(buf + off, bufsz - (size_t)off,
        "{\"uri\":\"%s\",\"diagnostics\":[", uri ? uri : "");

    for (int i = 0; i < n; i++) {
        if (i > 0) off += snprintf(buf + off, bufsz - (size_t)off, ",");

        char escaped_msg[1024];
        json_escape_into(escaped_msg, sizeof(escaped_msg), diags[i].msg);

        off += snprintf(buf + off, bufsz - (size_t)off,
            "{"
            "\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
            "\"end\":{\"line\":%d,\"character\":%d}},"
            "\"severity\":%d,"
            "\"source\":\"xs\","
            "\"message\":\"%s\""
            "}",
            diags[i].line, diags[i].col,
            diags[i].end_line, diags[i].end_col,
            diags[i].severity,
            escaped_msg);
    }

    off += snprintf(buf + off, bufsz - (size_t)off, "]}");
    lsp_send_notification("textDocument/publishDiagnostics", buf);
    free(buf);
}


static char *get_word_at(const char *text, int line, int character) {
    if (!text) return NULL;

    const char *p = text;
    for (int l = 0; l < line && *p; l++) {
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    const char *line_start = p;
    const char *line_end = p;
    while (*line_end && *line_end != '\n') line_end++;

    int line_len = (int)(line_end - line_start);
    if (character < 0 || character >= line_len) return NULL;

    int pos = character;
    int start = pos;
    int end = pos;

    while (start > 0 && (isalnum((unsigned char)line_start[start - 1]) || line_start[start - 1] == '_'))
        start--;
    while (end < line_len && (isalnum((unsigned char)line_start[end]) || line_start[end] == '_'))
        end++;

    if (start == end) return NULL;

    int wlen = end - start;
    char *word = malloc((size_t)wlen + 1);
    memcpy(word, line_start + start, (size_t)wlen);
    word[wlen] = '\0';
    return word;
}


static const char *node_tag_name(NodeTag tag) {
    switch (tag) {
    case NODE_FN_DECL:     return "function";
    case NODE_STRUCT_DECL: return "struct";
    case NODE_ENUM_DECL:   return "enum";
    case NODE_TRAIT_DECL:  return "trait";
    case NODE_LET:         return "let binding";
    case NODE_VAR:         return "var binding";
    case NODE_CONST:       return "const binding";
    case NODE_IMPORT:      return "import";
    case NODE_MODULE_DECL: return "module";
    case NODE_IMPL_DECL:   return "impl block";
    case NODE_LAMBDA:      return "lambda";
    default:               return NULL;
    }
}

/* Search for a declaration of `name` in an AST node, return the node */
static Node *find_decl_in_ast(Node *n, const char *name) {
    if (!n || !name) return NULL;

    switch (n->tag) {
    case NODE_FN_DECL:
        if (n->fn_decl.name && strcmp(n->fn_decl.name, name) == 0) return n;
            return find_decl_in_ast(n->fn_decl.body, name);
    case NODE_LET: case NODE_VAR:
        if (n->let.name && strcmp(n->let.name, name) == 0) return n;
        return NULL;
    case NODE_CONST:
        if (n->const_.name && strcmp(n->const_.name, name) == 0) return n;
        return NULL;
    case NODE_STRUCT_DECL:
        if (n->struct_decl.name && strcmp(n->struct_decl.name, name) == 0) return n;
        return NULL;
    case NODE_ENUM_DECL:
        if (n->enum_decl.name && strcmp(n->enum_decl.name, name) == 0) return n;
        return NULL;
    case NODE_TRAIT_DECL:
        if (n->trait_decl.name && strcmp(n->trait_decl.name, name) == 0) return n;
        return NULL;
    case NODE_MODULE_DECL:
        if (n->module_decl.name && strcmp(n->module_decl.name, name) == 0) return n;
        for (int i = 0; i < n->module_decl.body.len; i++) {
            Node *r = find_decl_in_ast(n->module_decl.body.items[i], name);
            if (r) return r;
        }
        return NULL;
    case NODE_IMPL_DECL:
        for (int i = 0; i < n->impl_decl.members.len; i++) {
            Node *r = find_decl_in_ast(n->impl_decl.members.items[i], name);
            if (r) return r;
        }
        return NULL;
    case NODE_PROGRAM:
        for (int i = 0; i < n->program.stmts.len; i++) {
            Node *r = find_decl_in_ast(n->program.stmts.items[i], name);
            if (r) return r;
        }
        return NULL;
    case NODE_BLOCK:
        for (int i = 0; i < n->block.stmts.len; i++) {
            Node *r = find_decl_in_ast(n->block.stmts.items[i], name);
            if (r) return r;
        }
        return NULL;
    case NODE_IF:
        {
            Node *r = find_decl_in_ast(n->if_expr.then, name);
            if (r) return r;
            if (n->if_expr.else_branch) {
                r = find_decl_in_ast(n->if_expr.else_branch, name);
                if (r) return r;
            }
        }
        return NULL;
    case NODE_WHILE:
        return find_decl_in_ast(n->while_loop.body, name);
    case NODE_FOR:
        return find_decl_in_ast(n->for_loop.body, name);
    case NODE_EXPR_STMT:
        return NULL;
    default:
        return NULL;
    }
}

static void build_hover_text(Node *decl, const char *name, char *out, size_t outsz) {
    if (!decl) {
        for (int i = 0; xs_keywords[i]; i++) {
            if (strcmp(xs_keywords[i], name) == 0) {
                snprintf(out, outsz, "(keyword) %s", name);
                return;
            }
        }
        for (int i = 0; xs_builtin_fns[i]; i++) {
            if (strcmp(xs_builtin_fns[i], name) == 0) {
                snprintf(out, outsz, "(builtin) fn %s(...)", name);
                return;
            }
        }
        snprintf(out, outsz, "%s", name);
        return;
    }

    const char *kind = node_tag_name(decl->tag);

    switch (decl->tag) {
    case NODE_FN_DECL: {
        int off = 0;
        off += snprintf(out + off, outsz - (size_t)off, "fn %s(", name);
        for (int i = 0; i < decl->fn_decl.params.len; i++) {
            if (i > 0) off += snprintf(out + off, outsz - (size_t)off, ", ");
            Param *pm = &decl->fn_decl.params.items[i];
            if (pm->variadic) off += snprintf(out + off, outsz - (size_t)off, "...");
            off += snprintf(out + off, outsz - (size_t)off, "%s",
                            pm->name ? pm->name : "_");
            if (pm->type_ann && pm->type_ann->name)
                off += snprintf(out + off, outsz - (size_t)off, ": %s", pm->type_ann->name);
        }
        off += snprintf(out + off, outsz - (size_t)off, ")");
        if (decl->fn_decl.ret_type && decl->fn_decl.ret_type->name)
            snprintf(out + off, outsz - (size_t)off, " -> %s", decl->fn_decl.ret_type->name);
        break;
    }
    case NODE_STRUCT_DECL:
        snprintf(out, outsz, "struct %s { %d field(s) }", name, decl->struct_decl.fields.len);
        break;
    case NODE_ENUM_DECL:
        snprintf(out, outsz, "enum %s { %d variant(s) }", name, decl->enum_decl.variants.len);
        break;
    case NODE_TRAIT_DECL:
        snprintf(out, outsz, "trait %s { %d method(s) }", name, decl->trait_decl.n_methods);
        break;
    case NODE_LET: case NODE_VAR:
        snprintf(out, outsz, "%s %s%s",
                 decl->let.mutable ? "var" : "let",
                 name,
                 decl->let.type_ann && decl->let.type_ann->name
                     ? decl->let.type_ann->name : "");
        break;
    case NODE_CONST:
        snprintf(out, outsz, "const %s", name);
        break;
    default:
        snprintf(out, outsz, "(%s) %s", kind ? kind : "symbol", name);
        break;
    }
}


static void lsp_handle_initialize(int id) {
    const char *caps =
        "{"
        "\"capabilities\":{"
            "\"textDocumentSync\":1,"
            "\"hoverProvider\":true,"
            "\"completionProvider\":{\"triggerCharacters\":[\".\",\":\"]},"
            "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\"]},"
            "\"definitionProvider\":true,"
            "\"documentSymbolProvider\":true"
        "},"
        "\"serverInfo\":{\"name\":\"xs-lsp\",\"version\":\"0.2.0\"}"
        "}";
    lsp_send_response(id, caps);
}


static void lsp_handle_hover(int id, const char *json) {
    int line = -1, character = -1;
    const char *pos = strstr(json, "\"position\"");
    if (pos) {
        line = json_get_int(pos, "line");
        character = json_get_int(pos, "character");
    }

    char *uri = NULL;
    const char *td = strstr(json, "\"textDocument\"");
    if (td) uri = json_get_string(td, "uri");

    LspDocument *doc = doc_find(uri);
    free(uri);

    char *word = NULL;
    if (doc && doc->text) {
        word = get_word_at(doc->text, line, character);
    }

    if (!word || strlen(word) == 0) {
        lsp_send_response(id, "null");
        free(word);
        return;
    }

    char hover_text[1024];

    Node *decl = NULL;
    if (doc && doc->ast) {
        decl = find_decl_in_ast(doc->ast, word);
    }

    build_hover_text(decl, word, hover_text, sizeof(hover_text));

    char escaped[2048];
    json_escape_into(escaped, sizeof(escaped), hover_text);

    char result[LSP_BUF_MEDIUM];
    snprintf(result, sizeof(result),
        "{\"contents\":{\"kind\":\"markdown\",\"value\":\"```xs\\n%s\\n```\"}}",
        escaped);
    lsp_send_response(id, result);
    free(word);
}

/* Helper: append method completion items from a NULL-terminated list.
 * Returns updated values of off and first via pointers. */
static void append_method_items(char *buf, size_t bufsz, int *off, int *first,
                                const char *const *methods, const char *type_detail) {
    for (int i = 0; methods[i]; i++) {
        if (!*first) *off += snprintf(buf + *off, bufsz - (size_t)*off, ",");
        *first = 0;
        *off += snprintf(buf + *off, bufsz - (size_t)*off,
            "{\"label\":\"%s\",\"kind\":2,\"detail\":\"%s method\"}",
            methods[i], type_detail);
    }
}

/* Detect the type of expression before the dot by scanning backwards in the
 * line text.  Returns 's' for string, 'a' for array, 'm' for map,
 * 'n' for number, or 0 for unknown / offer all. */
static int detect_dot_type(const char *line, int col) {
    if (col <= 0 || !line) return 0;
    int pos = col - 1; /* character just before the dot */

    /* skip trailing whitespace */
    while (pos >= 0 && (line[pos] == ' ' || line[pos] == '\t')) pos--;
    if (pos < 0) return 0;

    char ch = line[pos];

    /* String literal: ends with '"' */
    if (ch == '"') return 's';

    /* Array literal: ends with ']' */
    if (ch == ']') return 'a';

    /* Map literal: ends with '}' */
    if (ch == '}') return 'm';

    /* Number literal: digit or '.' (float) just before the member dot */
    if (ch >= '0' && ch <= '9') return 'n';

    /* Could be an identifier — no way to know the type without full
     * type inference, so return 0 to offer all methods. */
    return 0;
}

static void lsp_handle_completions(int id, const char *json) {
    char *uri = NULL;
    const char *td = strstr(json, "\"textDocument\"");
    if (td) uri = json_get_string(td, "uri");

    LspDocument *doc = doc_find(uri);
    free(uri);

    size_t bufsz = LSP_BUF_HUGE;
    char *buf = malloc(bufsz);
    if (!buf) { lsp_send_response(id, "{\"isIncomplete\":false,\"items\":[]}"); return; }

    int off = 0;
    off += snprintf(buf + off, bufsz - (size_t)off, "{\"isIncomplete\":false,\"items\":[");
    int first = 1;

    int is_dot_trigger = 0;
    {
        /* Check context.triggerCharacter */
        const char *ctx = strstr(json, "\"context\"");
        if (ctx) {
            char *tch = json_get_string(ctx, "triggerCharacter");
            if (tch && strcmp(tch, ".") == 0) is_dot_trigger = 1;
            free(tch);
        }

        /* Fallback: check the character before cursor in the document text */
        if (!is_dot_trigger && doc && doc->text) {
            const char *pos = strstr(json, "\"position\"");
            if (pos) {
                int line = json_get_int(pos, "line");
                int character = json_get_int(pos, "character");
                const char *p = doc->text;
                for (int l = 0; l < line && *p; l++) {
                    while (*p && *p != '\n') p++;
                    if (*p == '\n') p++;
                }
                /* p now points to start of the target line */
                if (character > 0) {
                    int line_len = 0;
                    while (p[line_len] && p[line_len] != '\n') line_len++;
                    if (character - 1 < line_len && p[character - 1] == '.') {
                        is_dot_trigger = 1;
                    }
                }
            }
        }
    }

    if (is_dot_trigger) {
        /* dot-completion */
        int dtype = 0;
        if (doc && doc->text) {
            const char *pos = strstr(json, "\"position\"");
            if (pos) {
                int line = json_get_int(pos, "line");
                int character = json_get_int(pos, "character");
                const char *p = doc->text;
                for (int l = 0; l < line && *p; l++) {
                    while (*p && *p != '\n') p++;
                    if (*p == '\n') p++;
                }
                /* p = start of target line; character-1 is the dot itself,
                 * so we pass character-1 to detect_dot_type which looks at
                 * the char before the dot. */
                dtype = detect_dot_type(p, character - 1);
            }
        }

        switch (dtype) {
        case 's':
            append_method_items(buf, bufsz, &off, &first,
                                xs_string_methods, "str");
            break;
        case 'a':
            append_method_items(buf, bufsz, &off, &first,
                                xs_array_methods, "array");
            break;
        case 'm':
            append_method_items(buf, bufsz, &off, &first,
                                xs_map_methods, "map");
            break;
        case 'n':
            append_method_items(buf, bufsz, &off, &first,
                                xs_number_methods, "number");
            break;
        default:
            /* Unknown type — offer all method sets */
            append_method_items(buf, bufsz, &off, &first,
                                xs_string_methods, "str");
            append_method_items(buf, bufsz, &off, &first,
                                xs_array_methods, "array");
            append_method_items(buf, bufsz, &off, &first,
                                xs_map_methods, "map");
            append_method_items(buf, bufsz, &off, &first,
                                xs_number_methods, "number");
            break;
        }
    } else {

        /* Keywords (kind 14 = Keyword) */
        for (int i = 0; xs_keywords[i]; i++) {
            if (!first) off += snprintf(buf + off, bufsz - (size_t)off, ",");
            first = 0;
            off += snprintf(buf + off, bufsz - (size_t)off,
                "{\"label\":\"%s\",\"kind\":14}", xs_keywords[i]);
        }

        /* Builtin functions (kind 3 = Function) */
        for (int i = 0; xs_builtin_fns[i]; i++) {
            if (!first) off += snprintf(buf + off, bufsz - (size_t)off, ",");
            first = 0;
            off += snprintf(buf + off, bufsz - (size_t)off,
                "{\"label\":\"%s\",\"kind\":3,\"detail\":\"builtin\"}", xs_builtin_fns[i]);
        }

        /* Standard library modules (kind 9 = Module) */
        for (int i = 0; xs_stdlib_modules[i]; i++) {
            if (!first) off += snprintf(buf + off, bufsz - (size_t)off, ",");
            first = 0;
            off += snprintf(buf + off, bufsz - (size_t)off,
                "{\"label\":\"%s\",\"kind\":9,\"detail\":\"module\"}", xs_stdlib_modules[i]);
        }

        /* Document identifiers (kind 6 = Variable) */
        if (doc) {
            for (int i = 0; i < doc->n_idents; i++) {
                if (!first) off += snprintf(buf + off, bufsz - (size_t)off, ",");
                first = 0;

                /* Determine kind from AST if possible */
                int kind = 6; /* Variable */
                if (doc->ast) {
                    Node *decl = find_decl_in_ast(doc->ast, doc->idents[i]);
                    if (decl) {
                        switch (decl->tag) {
                        case NODE_FN_DECL: kind = 3; break;
                        case NODE_STRUCT_DECL: kind = 22; break; /* Struct */
                        case NODE_ENUM_DECL: kind = 13; break;   /* Enum */
                        case NODE_TRAIT_DECL: kind = 8; break;    /* Interface */
                        case NODE_MODULE_DECL: kind = 9; break;
                        default: break;
                        }
                    }
                }

                char escaped[256];
                json_escape_into(escaped, sizeof(escaped), doc->idents[i]);
                off += snprintf(buf + off, bufsz - (size_t)off,
                    "{\"label\":\"%s\",\"kind\":%d}", escaped, kind);
            }
        }
    }

    off += snprintf(buf + off, bufsz - (size_t)off, "]}");
    lsp_send_response(id, buf);
    free(buf);
}


static void lsp_handle_signature_help(int id, const char *json) {
    int line = -1, character = -1;
    const char *pos = strstr(json, "\"position\"");
    if (pos) {
        line = json_get_int(pos, "line");
        character = json_get_int(pos, "character");
    }

    char *uri = NULL;
    const char *td = strstr(json, "\"textDocument\"");
    if (td) uri = json_get_string(td, "uri");

    LspDocument *doc = doc_find(uri);
    free(uri);

    if (!doc || !doc->text) {
        lsp_send_response(id, "{\"signatures\":[]}");
        return;
    }

    /* Walk backwards from cursor position to find the function name before '(' */
    const char *p = doc->text;
    for (int l = 0; l < line && *p; l++) {
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    const char *line_start = p;
    const char *line_end = p;
    while (*line_end && *line_end != '\n') line_end++;

    int line_len = (int)(line_end - line_start);
    if (character < 0 || character > line_len) {
        lsp_send_response(id, "{\"signatures\":[]}");
        return;
    }

    /* Count commas before cursor to determine active parameter */
    int active_param = 0;
    int paren_depth = 0;
    int paren_pos = -1;

    for (int i = character - 1; i >= 0; i--) {
        char c = line_start[i];
        if (c == ')') paren_depth++;
        else if (c == '(') {
            if (paren_depth == 0) { paren_pos = i; break; }
            paren_depth--;
        } else if (c == ',' && paren_depth == 0) {
            active_param++;
        }
    }

    if (paren_pos < 0) {
        lsp_send_response(id, "{\"signatures\":[]}");
        return;
    }

    /* Extract function name before the paren */
    int name_end = paren_pos;
    while (name_end > 0 && line_start[name_end - 1] == ' ') name_end--;
    int name_start = name_end;
    while (name_start > 0 && (isalnum((unsigned char)line_start[name_start - 1]) || line_start[name_start - 1] == '_'))
        name_start--;

    if (name_start == name_end) {
        lsp_send_response(id, "{\"signatures\":[]}");
        return;
    }

    int nlen = name_end - name_start;
    char fn_name[256];
    if (nlen >= (int)sizeof(fn_name)) nlen = (int)sizeof(fn_name) - 1;
    memcpy(fn_name, line_start + name_start, (size_t)nlen);
    fn_name[nlen] = '\0';

    /* Look up in AST */
    Node *decl = NULL;
    if (doc->ast) decl = find_decl_in_ast(doc->ast, fn_name);

    char sig_label[1024];
    char params_json[2048];

    if (decl && decl->tag == NODE_FN_DECL) {
        int soff = 0;
        soff += snprintf(sig_label + soff, sizeof(sig_label) - (size_t)soff, "fn %s(", fn_name);
        int poff = 0;
        poff += snprintf(params_json + poff, sizeof(params_json) - (size_t)poff, "[");

        for (int i = 0; i < decl->fn_decl.params.len; i++) {
            Param *pm = &decl->fn_decl.params.items[i];
            const char *pname = pm->name ? pm->name : "_";

            if (i > 0) {
                soff += snprintf(sig_label + soff, sizeof(sig_label) - (size_t)soff, ", ");
                poff += snprintf(params_json + poff, sizeof(params_json) - (size_t)poff, ",");
            }

            int label_start = soff;
            if (pm->variadic) soff += snprintf(sig_label + soff, sizeof(sig_label) - (size_t)soff, "...");
            soff += snprintf(sig_label + soff, sizeof(sig_label) - (size_t)soff, "%s", pname);
            if (pm->type_ann && pm->type_ann->name)
                soff += snprintf(sig_label + soff, sizeof(sig_label) - (size_t)soff, ": %s", pm->type_ann->name);
            int label_end = soff;

            poff += snprintf(params_json + poff, sizeof(params_json) - (size_t)poff,
                "{\"label\":[%d,%d]}", label_start, label_end);
        }

        soff += snprintf(sig_label + soff, sizeof(sig_label) - (size_t)soff, ")");
        if (decl->fn_decl.ret_type && decl->fn_decl.ret_type->name)
            snprintf(sig_label + soff, sizeof(sig_label) - (size_t)soff,
                     " -> %s", decl->fn_decl.ret_type->name);

        poff += snprintf(params_json + poff, sizeof(params_json) - (size_t)poff, "]");
    } else {
        /* Check if it's a known builtin */
        int is_builtin = 0;
        for (int i = 0; xs_builtin_fns[i]; i++) {
            if (strcmp(xs_builtin_fns[i], fn_name) == 0) { is_builtin = 1; break; }
        }
        if (is_builtin) {
            snprintf(sig_label, sizeof(sig_label), "fn %s(...args)", fn_name);
            snprintf(params_json, sizeof(params_json), "[{\"label\":\"...args\"}]");
        } else {
            lsp_send_response(id, "{\"signatures\":[]}");
            return;
        }
    }

    char escaped_label[2048];
    json_escape_into(escaped_label, sizeof(escaped_label), sig_label);

    char result[LSP_BUF_LARGE];
    snprintf(result, sizeof(result),
        "{\"signatures\":[{"
        "\"label\":\"%s\","
        "\"parameters\":%s"
        "}],"
        "\"activeSignature\":0,"
        "\"activeParameter\":%d"
        "}",
        escaped_label, params_json, active_param);

    lsp_send_response(id, result);
}


static void lsp_handle_definition(int id, const char *json) {
    int line = -1, character = -1;
    const char *pos = strstr(json, "\"position\"");
    if (pos) {
        line = json_get_int(pos, "line");
        character = json_get_int(pos, "character");
    }

    char *uri = NULL;
    const char *td = strstr(json, "\"textDocument\"");
    if (td) uri = json_get_string(td, "uri");

    LspDocument *doc = doc_find(uri);
    if (!doc || !doc->text || !doc->ast) {
        lsp_send_response(id, "null");
        free(uri);
        return;
    }

    char *word = get_word_at(doc->text, line, character);
    if (!word || strlen(word) == 0) {
        lsp_send_response(id, "null");
        free(word);
        free(uri);
        return;
    }

    Node *decl = find_decl_in_ast(doc->ast, word);
    free(word);

    if (!decl) {
        lsp_send_response(id, "null");
        free(uri);
        return;
    }

    int def_line = decl->span.line > 0 ? decl->span.line - 1 : 0;
    int def_col  = decl->span.col  > 0 ? decl->span.col  - 1 : 0;

    char result[LSP_BUF_MEDIUM];
    snprintf(result, sizeof(result),
        "{\"uri\":\"%s\","
        "\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
        "\"end\":{\"line\":%d,\"character\":%d}}}",
        uri ? uri : "",
        def_line, def_col,
        def_line, def_col + 1);

    lsp_send_response(id, result);
    free(uri);
}


static void emit_symbol(char *buf, size_t bufsz, int *off, int *first,
                        const char *name, int kind, Span span) {
    if (!name || *off + 256 >= (int)bufsz) return;
    if (!*first) *off += snprintf(buf + *off, bufsz - (size_t)*off, ",");
    *first = 0;

    int sl = span.line > 0 ? span.line - 1 : 0;
    int sc = span.col  > 0 ? span.col  - 1 : 0;
    int el = span.end_line > 0 ? span.end_line - 1 : sl;
    int ec = span.end_col  > 0 ? span.end_col  - 1 : sc + (int)strlen(name);

    char escaped[256];
    json_escape_into(escaped, sizeof(escaped), name);

    *off += snprintf(buf + *off, bufsz - (size_t)*off,
        "{"
        "\"name\":\"%s\","
        "\"kind\":%d,"
        "\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
        "\"end\":{\"line\":%d,\"character\":%d}},"
        "\"selectionRange\":{\"start\":{\"line\":%d,\"character\":%d},"
        "\"end\":{\"line\":%d,\"character\":%d}}"
        "}",
        escaped, kind,
        sl, sc, el, ec,
        sl, sc, sl, sc + (int)strlen(name));
}

static void collect_symbols_json(Node *n, char *buf, size_t bufsz, int *off, int *first, const char *uri) {
    if (!n) return;
    (void)uri;

    switch (n->tag) {
    case NODE_PROGRAM:
        for (int i = 0; i < n->program.stmts.len; i++)
            collect_symbols_json(n->program.stmts.items[i], buf, bufsz, off, first, uri);
        break;
    case NODE_FN_DECL:
        /* SymbolKind: Function = 12 */
        if (n->fn_decl.name)
            emit_symbol(buf, bufsz, off, first, n->fn_decl.name, 12, n->span);
        break;
    case NODE_STRUCT_DECL:
        /* SymbolKind: Struct = 23 */
        if (n->struct_decl.name)
            emit_symbol(buf, bufsz, off, first, n->struct_decl.name, 23, n->span);
        break;
    case NODE_ENUM_DECL:
        /* SymbolKind: Enum = 10 */
        if (n->enum_decl.name)
            emit_symbol(buf, bufsz, off, first, n->enum_decl.name, 10, n->span);
        break;
    case NODE_TRAIT_DECL:
        /* SymbolKind: Interface = 11 */
        if (n->trait_decl.name)
            emit_symbol(buf, bufsz, off, first, n->trait_decl.name, 11, n->span);
        break;
    case NODE_LET: case NODE_VAR:
        /* SymbolKind: Variable = 13 */
        if (n->let.name)
            emit_symbol(buf, bufsz, off, first, n->let.name, 13, n->span);
        break;
    case NODE_CONST:
        /* SymbolKind: Constant = 14 */
        if (n->const_.name)
            emit_symbol(buf, bufsz, off, first, n->const_.name, 14, n->span);
        break;
    case NODE_MODULE_DECL:
        /* SymbolKind: Module = 2 */
        if (n->module_decl.name)
            emit_symbol(buf, bufsz, off, first, n->module_decl.name, 2, n->span);
        for (int i = 0; i < n->module_decl.body.len; i++)
            collect_symbols_json(n->module_decl.body.items[i], buf, bufsz, off, first, uri);
        break;
    case NODE_IMPL_DECL:
        /* SymbolKind: Class = 5 (for impl blocks) */
        if (n->impl_decl.type_name)
            emit_symbol(buf, bufsz, off, first, n->impl_decl.type_name, 5, n->span);
        for (int i = 0; i < n->impl_decl.members.len; i++)
            collect_symbols_json(n->impl_decl.members.items[i], buf, bufsz, off, first, uri);
        break;
    case NODE_EFFECT_DECL:
        /* SymbolKind: Event = 24 */
        if (n->effect_decl.name)
            emit_symbol(buf, bufsz, off, first, n->effect_decl.name, 24, n->span);
        break;
    case NODE_TYPE_ALIAS:
        /* SymbolKind: TypeParameter = 26 */
        if (n->type_alias.name)
            emit_symbol(buf, bufsz, off, first, n->type_alias.name, 26, n->span);
        break;
    default:
        break;
    }
}

static void lsp_handle_document_symbols(int id, const char *json) {
    char *uri = NULL;
    const char *td = strstr(json, "\"textDocument\"");
    if (td) uri = json_get_string(td, "uri");

    LspDocument *doc = doc_find(uri);
    free(uri);

    if (!doc || !doc->ast) {
        lsp_send_response(id, "[]");
        return;
    }

    size_t bufsz = LSP_BUF_HUGE;
    char *buf = malloc(bufsz);
    if (!buf) { lsp_send_response(id, "[]"); return; }

    int off = 0;
    int first = 1;
    off += snprintf(buf + off, bufsz - (size_t)off, "[");
    collect_symbols_json(doc->ast, buf, bufsz, &off, &first, NULL);
    off += snprintf(buf + off, bufsz - (size_t)off, "]");

    lsp_send_response(id, buf);
    free(buf);
}


int lsp_run(void) {
    int shutdown_requested = 0;

    fprintf(stderr, "xs-lsp: starting LSP server v0.2.0\n");

    while (1) {
        char *msg = lsp_read_message();
        if (!msg) break;

        char *method = json_get_string(msg, "method");
        int id = json_get_int(msg, "id");

        if (!method) {
            free(msg);
            continue;
        }

        if (strcmp(method, "initialize") == 0) {
            lsp_handle_initialize(id);

        } else if (strcmp(method, "initialized") == 0) {
            /* no-op */

        } else if (strcmp(method, "textDocument/didOpen") == 0) {
            char *uri = NULL;
            char *text = NULL;
            const char *td = strstr(msg, "\"textDocument\"");
            if (td) {
                uri = json_get_string(td, "uri");
                text = json_get_string(td, "text");
            }
            lsp_send_diagnostics(uri, text);
            free(uri);
            free(text);

        } else if (strcmp(method, "textDocument/didChange") == 0) {
            char *uri = NULL;
            const char *td = strstr(msg, "\"textDocument\"");
            if (td) uri = json_get_string(td, "uri");
            char *text = NULL;
            const char *cc = strstr(msg, "\"contentChanges\"");
            if (cc) text = json_get_string(cc, "text");
            lsp_send_diagnostics(uri, text);
            free(uri);
            free(text);

        } else if (strcmp(method, "textDocument/hover") == 0) {
            lsp_handle_hover(id, msg);

        } else if (strcmp(method, "textDocument/completion") == 0) {
            lsp_handle_completions(id, msg);

        } else if (strcmp(method, "textDocument/signatureHelp") == 0) {
            lsp_handle_signature_help(id, msg);

        } else if (strcmp(method, "textDocument/definition") == 0) {
            lsp_handle_definition(id, msg);

        } else if (strcmp(method, "textDocument/documentSymbol") == 0) {
            lsp_handle_document_symbols(id, msg);

        } else if (strcmp(method, "shutdown") == 0) {
            shutdown_requested = 1;
            lsp_send_response(id, "null");

        } else if (strcmp(method, "exit") == 0) {
            free(method);
            free(msg);
            return shutdown_requested ? 0 : 1;

        } else {
            if (id >= 0) {
                lsp_send_error(id, -32601, method);
            }
        }

        free(method);
        free(msg);
    }

    for (int i = 0; i < g_ndocs; i++) {
        free(g_docs[i].uri);
        free(g_docs[i].text);
        doc_free_ast(&g_docs[i]);
        doc_clear_idents(&g_docs[i]);
    }
    g_ndocs = 0;

    return 0;
}
