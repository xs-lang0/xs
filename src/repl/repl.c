#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "repl/repl.h"
#include "core/xs_compat.h"
#include "core/value.h"
#include "core/lexer.h"
#include "core/parser.h"
#include "core/env.h"
#include "diagnostic/colorize.h"
#include "runtime/interp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#ifndef __MINGW32__
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#define XS_REPL_VERSION "0.2.0"
#define XS_HISTORY_MAX 1000
#define XS_LINE_MAX    8192

#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_DIM     "\033[2m"

#define DARK_KEYWORD   "\033[34m"
#define DARK_STRING    "\033[32m"
#define DARK_NUMBER    "\033[36m"
#define DARK_COMMENT   "\033[90m"
#define DARK_OPERATOR  "\033[33m"
#define DARK_BUILTIN   "\033[35m"
#define DARK_TYPE      "\033[33;1m"
#define DARK_ERROR     "\033[31m"
#define DARK_RESULT    "\033[32m"
#define DARK_TIMING    "\033[90m"
#define DARK_PROMPT    "\033[36;1m"

#define LIGHT_KEYWORD  "\033[34;1m"
#define LIGHT_STRING   "\033[32;1m"
#define LIGHT_NUMBER   "\033[35m"
#define LIGHT_COMMENT  "\033[37m"
#define LIGHT_OPERATOR "\033[33;1m"
#define LIGHT_BUILTIN  "\033[35;1m"
#define LIGHT_TYPE     "\033[33m"
#define LIGHT_ERROR    "\033[31;1m"
#define LIGHT_RESULT   "\033[32;1m"
#define LIGHT_TIMING   "\033[37m"
#define LIGHT_PROMPT   "\033[34;1m"

typedef struct {
    const char *keyword;
    const char *string;
    const char *number;
    const char *comment;
    const char *operator_;
    const char *builtin;
    const char *type;
    const char *error;
    const char *result;
    const char *timing;
    const char *prompt;
} ColorTheme;

static ColorTheme dark_theme = {
    DARK_KEYWORD, DARK_STRING, DARK_NUMBER, DARK_COMMENT,
    DARK_OPERATOR, DARK_BUILTIN, DARK_TYPE,
    DARK_ERROR, DARK_RESULT, DARK_TIMING, DARK_PROMPT,
};

static ColorTheme light_theme = {
    LIGHT_KEYWORD, LIGHT_STRING, LIGHT_NUMBER, LIGHT_COMMENT,
    LIGHT_OPERATOR, LIGHT_BUILTIN, LIGHT_TYPE,
    LIGHT_ERROR, LIGHT_RESULT, LIGHT_TIMING, LIGHT_PROMPT,
};

static ColorTheme *g_theme = &dark_theme;

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

static const char *xs_builtins[] = {
    "print", "println", "input", "len", "type", "range",
    "keys", "values", "entries", "map", "filter", "reduce",
    "zip", "any", "all", "min", "max", "sum", "sort",
    "reverse", "push", "pop", "slice", "join", "split",
    "contains", "starts_with", "ends_with", "trim",
    "upper", "lower", "replace", "int", "float", "str",
    "bool", "char", "typeof", "assert", "panic",
    NULL
};

static const char *xs_modules[] = {
    "math", "time", "string", "path", "base64", "hash",
    "uuid", "collections", "random", "json", "log", "fmt",
    "csv", "url", "re", "process", "io", "async", "net",
    "crypto", "thread", "buf", "encode", "db", "cli",
    "ffi", "reflect", "gc", "reactive", "os",
    NULL
};



static void colorize_line(const char *line, char *out, size_t outsz) {
    diag_colorize_line(line, out, outsz);
}

/* History */

typedef struct {
    char *lines[XS_HISTORY_MAX];
    int   count;
    char  path[1024];
} History;

static void history_init(History *h) {
    memset(h, 0, sizeof(*h));
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(h->path, sizeof(h->path), "%s/.xs_history", home);

    FILE *f = fopen(h->path, "r");
    if (!f) return;
    char buf[XS_LINE_MAX];
    while (h->count < XS_HISTORY_MAX && fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
        if (len == 0) continue;
        h->lines[h->count++] = xs_strdup(buf);
    }
    fclose(f);
}

static void history_add(History *h, const char *line) {
    if (!line || strlen(line) == 0) return;
    if (h->count > 0 && strcmp(h->lines[h->count - 1], line) == 0) return;

    if (h->count >= XS_HISTORY_MAX) {
        free(h->lines[0]);
        memmove(h->lines, h->lines + 1, (XS_HISTORY_MAX - 1) * sizeof(char *));
        h->count = XS_HISTORY_MAX - 1;
    }
    h->lines[h->count++] = xs_strdup(line);

    FILE *f = fopen(h->path, "a");
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

static void history_free(History *h) {
    for (int i = 0; i < h->count; i++) free(h->lines[i]);
    h->count = 0;
}

static void history_save_quiet(History *h, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < h->count; i++) {
        fprintf(f, "%s\n", h->lines[i]);
    }
    fclose(f);
}

static void history_save_to(History *h, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "%serror: cannot open '%s' for writing%s\n",
                g_theme->error, path, ANSI_RESET);
        return;
    }
    for (int i = 0; i < h->count; i++) {
        fprintf(f, "%s\n", h->lines[i]);
    }
    fclose(f);
    printf("saved %d history entries to '%s'\n", h->count, path);
}

static int needs_continuation(const char *input) {
    size_t len = strlen(input);
    if (len == 0) return 0;
    if (input[len - 1] == '\\') return 1;

    int brace_depth = 0;
    int in_string = 0;
    char str_char = 0;
    for (size_t i = 0; i < len; i++) {
        char c = input[i];
        if (in_string) {
            if (c == '\\' && i + 1 < len) { i++; continue; }
            if (c == str_char) in_string = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = 1;
            str_char = c;
        } else if (c == '{' || c == '(' || c == '[') {
            brace_depth++;
        } else if (c == '}' || c == ')' || c == ']') {
            brace_depth--;
        }
    }
    return brace_depth > 0;
}

/* Tab completion */

#ifndef __MINGW32__
static int get_terminal_width(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}
#else
static int get_terminal_width(void) { return 80; }
#endif

typedef struct {
    char **items;
    int    count;
    int    cap;
} CompletionList;

static void comp_init(CompletionList *cl) {
    cl->items = NULL;
    cl->count = 0;
    cl->cap = 0;
}

static void comp_add(CompletionList *cl, const char *s) {
    if (cl->count >= cl->cap) {
        cl->cap = cl->cap ? cl->cap * 2 : 32;
        char **tmp = realloc(cl->items, (size_t)cl->cap * sizeof(char *));
        if (!tmp) return;
        cl->items = tmp;
    }
    cl->items[cl->count++] = xs_strdup(s);
}

static void comp_free(CompletionList *cl) {
    for (int i = 0; i < cl->count; i++) free(cl->items[i]);
    free(cl->items);
    cl->items = NULL;
    cl->count = 0;
    cl->cap = 0;
}

static void get_completions(const char *prefix, Interp *interp, CompletionList *cl) {
    size_t plen = strlen(prefix);

    for (int i = 0; xs_keywords[i]; i++) {
        if (strncmp(xs_keywords[i], prefix, plen) == 0)
            comp_add(cl, xs_keywords[i]);
    }

    for (int i = 0; xs_builtins[i]; i++) {
        if (strncmp(xs_builtins[i], prefix, plen) == 0)
            comp_add(cl, xs_builtins[i]);
    }

    for (int i = 0; xs_modules[i]; i++) {
        if (strncmp(xs_modules[i], prefix, plen) == 0)
            comp_add(cl, xs_modules[i]);
    }

    if (interp && interp->globals) {
        Env *e = interp->globals;
        while (e) {
            for (int i = 0; i < e->len; i++) {
                Binding *b = &e->bindings[i];
                if (b->name && strncmp(b->name, prefix, plen) == 0)
                    comp_add(cl, b->name);
            }
            e = e->parent;
        }
    }
}

static void display_completions(CompletionList *cl) {
    if (cl->count == 0) return;

    int cols = get_terminal_width();
    int max_len = 0;
    for (int i = 0; i < cl->count; i++) {
        int l = (int)strlen(cl->items[i]);
        if (l > max_len) max_len = l;
    }
    max_len += 2; /* padding */

    int items_per_row = cols / max_len;
    if (items_per_row < 1) items_per_row = 1;

    printf("\n");
    for (int i = 0; i < cl->count; i++) {
        printf("%-*s", max_len, cl->items[i]);
        if ((i + 1) % items_per_row == 0 || i + 1 == cl->count)
            printf("\n");
    }
}

static char *common_prefix(CompletionList *cl) {
    if (cl->count == 0) return xs_strdup("");
    if (cl->count == 1) return xs_strdup(cl->items[0]);

    char *first = cl->items[0];
    size_t plen = strlen(first);

    for (int i = 1; i < cl->count; i++) {
        size_t j = 0;
        while (j < plen && cl->items[i][j] == first[j]) j++;
        plen = j;
    }

    return xs_strndup(first, plen);
}

static char *read_line_with_completion(const char *prompt, Interp *interp, History *hist) {
    printf("%s", prompt);
    fflush(stdout);

#ifndef __MINGW32__
    struct termios orig, raw;
    int have_term = (tcgetattr(STDIN_FILENO, &orig) == 0);
    if (have_term) {
        raw = orig;
        raw.c_lflag &= ~((tcflag_t)ICANON | (tcflag_t)ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    static char buf[XS_LINE_MAX];
    int pos = 0;
    buf[0] = '\0';
    int hist_idx = hist ? hist->count : 0;
    char saved_line[XS_LINE_MAX] = {0};

    if (!have_term) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig);
        if (!fgets(buf, sizeof(buf), stdin)) return NULL;
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
        return buf;
    }

    while (1) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            tcsetattr(STDIN_FILENO, TCSANOW, &orig);
            return NULL;
        }

        if (c == '\n' || c == '\r') {
            printf("\n");
            break;
        }

        if (c == '\t') {
            int word_start = pos;
            while (word_start > 0 && (isalnum((unsigned char)buf[word_start-1]) || buf[word_start-1] == '_'))
                word_start--;

            char prefix[256];
            int plen = pos - word_start;
            if (plen >= (int)sizeof(prefix)) plen = (int)sizeof(prefix) - 1;
            memcpy(prefix, buf + word_start, (size_t)plen);
            prefix[plen] = '\0';

            if (plen == 0) continue;

            CompletionList cl;
            comp_init(&cl);
            get_completions(prefix, interp, &cl);

            if (cl.count == 1) {
                const char *match = cl.items[0];
                int match_len = (int)strlen(match);
                int to_add = match_len - plen;
                if (pos + to_add < XS_LINE_MAX - 1) {
                    memcpy(buf + pos, match + plen, (size_t)to_add);
                    pos += to_add;
                    buf[pos] = '\0';
                    printf("%s", match + plen);
                    fflush(stdout);
                }
            } else if (cl.count > 1) {
                char *cp = common_prefix(&cl);
                int cp_len = (int)strlen(cp);
                int to_add = cp_len - plen;
                if (to_add > 0 && pos + to_add < XS_LINE_MAX - 1) {
                    memcpy(buf + pos, cp + plen, (size_t)to_add);
                    pos += to_add;
                    buf[pos] = '\0';
                    printf("%s", cp + plen);
                    fflush(stdout);
                }
                free(cp);

                if (to_add == 0) {
                    display_completions(&cl);
                    printf("%s%s", prompt, buf);
                    fflush(stdout);
                }
            }

            comp_free(&cl);
            continue;
        }

        if (c == 127 || c == 8) {
            if (pos > 0) {
                pos--;
                buf[pos] = '\0';
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }

        if (c == 4) { /* ^D */
            if (pos == 0) {
                tcsetattr(STDIN_FILENO, TCSANOW, &orig);
                return NULL;
            }
            continue;
        }

        if (c == 3) {
            printf("^C\n");
            pos = 0;
            buf[0] = '\0';
            printf("%s", prompt);
            fflush(stdout);
            continue;
        }

        if (c == 21) { /* ^U clear line */
            while (pos > 0) {
                printf("\b \b");
                pos--;
            }
            buf[0] = '\0';
            fflush(stdout);
            continue;
        }

        if (c == 27) {
            char seq[8];
            ssize_t nr = read(STDIN_FILENO, seq, 1);
            if (nr <= 0) continue;
            if (seq[0] == '[') {
                nr = read(STDIN_FILENO, seq + 1, 1);
                if (nr <= 0) continue;
                if (seq[1] == 'A' && hist) {
                    if (hist_idx > 0) {
                        if (hist_idx == hist->count) {
                            memcpy(saved_line, buf, sizeof(saved_line) - 1);
                            saved_line[sizeof(saved_line) - 1] = '\0';
                        }
                        hist_idx--;
                        while (pos > 0) { printf("\b \b"); pos--; }
                        strncpy(buf, hist->lines[hist_idx], XS_LINE_MAX - 1);
                        buf[XS_LINE_MAX - 1] = '\0';
                        pos = (int)strlen(buf);
                        printf("%s", buf);
                        fflush(stdout);
                    }
                } else if (seq[1] == 'B' && hist) {
                    if (hist_idx < hist->count) {
                        hist_idx++;
                        while (pos > 0) { printf("\b \b"); pos--; }
                        if (hist_idx < hist->count) {
                            strncpy(buf, hist->lines[hist_idx], XS_LINE_MAX - 1);
                            buf[XS_LINE_MAX - 1] = '\0';
                        } else {
                            strncpy(buf, saved_line, XS_LINE_MAX - 1);
                            buf[XS_LINE_MAX - 1] = '\0';
                        }
                        pos = (int)strlen(buf);
                        printf("%s", buf);
                        fflush(stdout);
                    }
                }
            }
            continue;
        }

        if (pos < XS_LINE_MAX - 1 && c >= 32) {
            buf[pos++] = c;
            buf[pos] = '\0';

            printf("%c", c);
            fflush(stdout);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    return buf;
#else
    static char buf[XS_LINE_MAX];
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;
    size_t len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
    return buf;
#endif
}

static char *read_input(Interp *interp, History *hist) {
    char prompt[128];
    snprintf(prompt, sizeof(prompt), "%sxs> %s", g_theme->prompt, ANSI_RESET);

    char *first = read_line_with_completion(prompt, interp, hist);
    if (!first) return NULL;

    size_t cap = XS_LINE_MAX * 4;
    char *input = malloc(cap);
    if (!input) return NULL;

    size_t flen = strlen(first);
    if (flen > 0 && first[flen - 1] == '\\') {
        memcpy(input, first, flen - 1);
        input[flen - 1] = '\n';
        input[flen] = '\0';
    } else {
        strcpy(input, first);
    }

    while (needs_continuation(input)) {
        char cont_prompt[128];
        snprintf(cont_prompt, sizeof(cont_prompt), "%s... >%s ", g_theme->prompt, ANSI_RESET);

        char *cont = read_line_with_completion(cont_prompt, interp, NULL);
        if (!cont) break;

        size_t ilen = strlen(input);
        size_t clen = strlen(cont);

        int had_backslash = (clen > 0 && cont[clen - 1] == '\\');
        if (had_backslash) clen--;

        if (ilen + clen + 2 >= cap) {
            cap *= 2;
            input = realloc(input, cap);
            if (!input) return NULL;
        }
        input[ilen] = '\n';
        memcpy(input + ilen + 1, cont, clen);
        input[ilen + 1 + clen] = '\0';
    }

    return input;
}

static double time_now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void print_timing(double elapsed) {
    if (elapsed < 0.001) {
        printf(" %s(%.0f\xc2\xb5s)%s", g_theme->timing, elapsed * 1e6, ANSI_RESET);
    } else if (elapsed < 1.0) {
        printf(" %s(%.3fs)%s", g_theme->timing, elapsed, ANSI_RESET);
    } else {
        printf(" %s(%.2fs)%s", g_theme->timing, elapsed, ANSI_RESET);
    }
}


static void cmd_help(void) {
    printf(
        "%sREPL commands:%s\n"
        "  :help             Show this help message\n"
        "  :reset            Reset interpreter state\n"
        "  :env              List all global bindings\n"
        "  :type <expr>      Show type of expression\n"
        "  :doc <name>       Show documentation for name\n"
        "  :load <file>      Load and execute a file\n"
        "  :save <file>      Save REPL history to a file\n"
        "  :time <expr>      Time the evaluation of an expression\n"
        "  :ast <expr>       Show the AST for an expression\n"
        "  :tour             Print a guided language tour\n"
        "  :theme dark|light Switch color theme\n"
        "  :modules          List available standard library modules\n"
        "  :quit / :exit     Exit the REPL\n",
        ANSI_BOLD, ANSI_RESET
    );
}

static void cmd_env(Interp *interp) {
    Env *e = interp->globals;
    if (!e || e->len == 0) {
        printf("%s(no bindings)%s\n", g_theme->timing, ANSI_RESET);
        return;
    }
    for (int i = 0; i < e->len; i++) {
        Binding *b = &e->bindings[i];
        char *r = value_repr(b->value);
        printf("  %s%s%s%s = %s%s%s\n",
               b->mutable ? "mut " : "",
               g_theme->keyword, b->name, ANSI_RESET,
               g_theme->result, r ? r : "(null)", ANSI_RESET);
        free(r);
    }
}

static void cmd_load(Interp *interp, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "%serror: cannot open '%s'%s\n",
                g_theme->error, filename, ANSI_RESET);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc((size_t)(sz + 1));
    if (!src) { fclose(f); return; }
    if (fread(src, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "%serror: read error on '%s'%s\n",
                g_theme->error, filename, ANSI_RESET);
        fclose(f); free(src); return;
    }
    src[sz] = '\0';
    fclose(f);

    Lexer lex;
    lexer_init(&lex, src, filename);
    TokenArray ta = lexer_tokenize(&lex);
    free(src);

    Parser parser;
    parser_init(&parser, &ta, filename);
    Node *program = parser_parse(&parser);
    token_array_free(&ta);

    if (!program || parser.had_error) {
        fprintf(stderr, "%sparse error at %s:%d:%d: %s%s\n",
                g_theme->error, filename, parser.error.span.line,
                parser.error.span.col, parser.error.msg, ANSI_RESET);
        if (program) node_free(program);
        return;
    }

    interp_run(interp, program);
    node_free(program);

    if (interp->cf.signal == CF_ERROR || interp->cf.signal == CF_PANIC) {
        Value *err = interp->cf.value;
        if (err && err->tag == XS_STR) {
            fprintf(stderr, "%serror: %s%s\n", g_theme->error, err->s, ANSI_RESET);
        } else if (err) {
            char *s = value_repr(err);
            if (s) { fprintf(stderr, "%serror: %s%s\n", g_theme->error, s, ANSI_RESET); free(s); }
        }
        if (interp->cf.value) {
            value_decref(interp->cf.value);
            interp->cf.value = NULL;
        }
        interp->cf.signal = 0;
    } else {
        printf("%sloaded '%s'%s\n", g_theme->result, filename, ANSI_RESET);
        if (interp->cf.value) {
            value_decref(interp->cf.value);
            interp->cf.value = NULL;
        }
        interp->cf.signal = 0;
    }
}

static void cmd_time(Interp *interp, const char *expr_str) {
    Lexer lex;
    lexer_init(&lex, expr_str, "<repl>");
    TokenArray ta = lexer_tokenize(&lex);
    Parser parser;
    parser_init(&parser, &ta, "<repl>");
    Node *prog = parser_parse(&parser);
    token_array_free(&ta);

    if (!prog || parser.had_error) {
        fprintf(stderr, "%sparse error at %d:%d: %s%s\n",
                g_theme->error, parser.error.span.line,
                parser.error.span.col, parser.error.msg, ANSI_RESET);
        if (prog) node_free(prog);
        return;
    }

    double t0 = time_now_sec();
    interp_run(interp, prog);
    double elapsed = time_now_sec() - t0;
    node_free(prog);

    if (interp->cf.signal == CF_ERROR || interp->cf.signal == CF_PANIC) {
        Value *err = interp->cf.value;
        if (err && err->tag == XS_STR) {
            fprintf(stderr, "%serror: %s%s\n", g_theme->error, err->s, ANSI_RESET);
        }
        if (interp->cf.value) { value_decref(interp->cf.value); interp->cf.value = NULL; }
        interp->cf.signal = 0;
    } else {
        Value *result = interp->cf.value;
        if (result && result->tag != XS_NULL) {
            char *s = value_repr(result);
            if (s) {
                printf("%s=> %s%s", g_theme->result, s, ANSI_RESET);
                free(s);
            }
        }
        print_timing(elapsed);
        printf("\n");
        if (interp->cf.value) { value_decref(interp->cf.value); interp->cf.value = NULL; }
        interp->cf.signal = 0;
    }
}

static void print_ast_node(Node *n, int indent) {
    if (!n) { printf("%*s(null)\n", indent * 2, ""); return; }

    const char *tag_name = "unknown";
    switch (n->tag) {
    case NODE_LIT_INT:    tag_name = "LitInt"; break;
    case NODE_LIT_BIGINT: tag_name = "LitBigInt"; break;
    case NODE_LIT_FLOAT:  tag_name = "LitFloat"; break;
    case NODE_LIT_STRING: tag_name = "LitString"; break;
    case NODE_LIT_BOOL:   tag_name = "LitBool"; break;
    case NODE_LIT_NULL:   tag_name = "LitNull"; break;
    case NODE_LIT_CHAR:   tag_name = "LitChar"; break;
    case NODE_LIT_ARRAY:  tag_name = "LitArray"; break;
    case NODE_LIT_TUPLE:  tag_name = "LitTuple"; break;
    case NODE_LIT_MAP:    tag_name = "LitMap"; break;
    case NODE_IDENT:      tag_name = "Ident"; break;
    case NODE_BINOP:      tag_name = "BinOp"; break;
    case NODE_UNARY:      tag_name = "Unary"; break;
    case NODE_ASSIGN:     tag_name = "Assign"; break;
    case NODE_CALL:       tag_name = "Call"; break;
    case NODE_METHOD_CALL: tag_name = "MethodCall"; break;
    case NODE_INDEX:      tag_name = "Index"; break;
    case NODE_FIELD:      tag_name = "Field"; break;
    case NODE_IF:         tag_name = "If"; break;
    case NODE_MATCH:      tag_name = "Match"; break;
    case NODE_WHILE:      tag_name = "While"; break;
    case NODE_FOR:        tag_name = "For"; break;
    case NODE_RETURN:     tag_name = "Return"; break;
    case NODE_BREAK:      tag_name = "Break"; break;
    case NODE_CONTINUE:   tag_name = "Continue"; break;
    case NODE_LAMBDA:     tag_name = "Lambda"; break;
    case NODE_BLOCK:      tag_name = "Block"; break;
    case NODE_LET:        tag_name = "Let"; break;
    case NODE_VAR:        tag_name = "Var"; break;
    case NODE_CONST:      tag_name = "Const"; break;
    case NODE_FN_DECL:    tag_name = "FnDecl"; break;
    case NODE_STRUCT_DECL: tag_name = "StructDecl"; break;
    case NODE_ENUM_DECL:  tag_name = "EnumDecl"; break;
    case NODE_IMPORT:     tag_name = "Import"; break;
    case NODE_EXPR_STMT:  tag_name = "ExprStmt"; break;
    case NODE_PROGRAM:    tag_name = "Program"; break;
    case NODE_RANGE:      tag_name = "Range"; break;
    case NODE_THROW:      tag_name = "Throw"; break;
    case NODE_TRY:        tag_name = "Try"; break;
    case NODE_YIELD:      tag_name = "Yield"; break;
    case NODE_DEFER:      tag_name = "Defer"; break;
    case NODE_CAST:       tag_name = "Cast"; break;
    default: break;
    }

    printf("%*s%s%s%s", indent * 2, "", g_theme->keyword, tag_name, ANSI_RESET);

    switch (n->tag) {
    case NODE_LIT_INT:
        printf(" %s%lld%s", g_theme->number, (long long)n->lit_int.ival, ANSI_RESET);
        break;
    case NODE_LIT_BIGINT:
        printf(" %s%s%s", g_theme->number, n->lit_bigint.bigint_str, ANSI_RESET);
        break;
    case NODE_LIT_FLOAT:
        printf(" %s%g%s", g_theme->number, n->lit_float.fval, ANSI_RESET);
        break;
    case NODE_LIT_STRING:
        printf(" %s\"%s\"%s", g_theme->string,
               n->lit_string.sval ? n->lit_string.sval : "", ANSI_RESET);
        break;
    case NODE_LIT_BOOL:
        printf(" %s%s%s", g_theme->keyword,
               n->lit_bool.bval ? "true" : "false", ANSI_RESET);
        break;
    case NODE_IDENT:
        printf(" %s", n->ident.name ? n->ident.name : "?");
        break;
    case NODE_BINOP:
        printf(" op='%s'", n->binop.op);
        break;
    case NODE_UNARY:
        printf(" op='%s'", n->unary.op);
        break;
    case NODE_FN_DECL:
        printf(" %s%s%s", g_theme->builtin,
               n->fn_decl.name ? n->fn_decl.name : "<lambda>", ANSI_RESET);
        break;
    case NODE_LET: case NODE_VAR:
        printf(" %s", n->let.name ? n->let.name : "?");
        break;
    default: break;
    }
    printf("\n");

    switch (n->tag) {
    case NODE_BINOP:
        print_ast_node(n->binop.left, indent + 1);
        print_ast_node(n->binop.right, indent + 1);
        break;
    case NODE_UNARY:
        print_ast_node(n->unary.expr, indent + 1);
        break;
    case NODE_CALL:
        print_ast_node(n->call.callee, indent + 1);
        for (int i = 0; i < n->call.args.len; i++)
            print_ast_node(n->call.args.items[i], indent + 1);
        break;
    case NODE_EXPR_STMT:
        print_ast_node(n->expr_stmt.expr, indent + 1);
        break;
    case NODE_PROGRAM:
        for (int i = 0; i < n->program.stmts.len; i++)
            print_ast_node(n->program.stmts.items[i], indent + 1);
        break;
    case NODE_BLOCK:
        for (int i = 0; i < n->block.stmts.len; i++)
            print_ast_node(n->block.stmts.items[i], indent + 1);
        break;
    case NODE_IF:
        print_ast_node(n->if_expr.cond, indent + 1);
        print_ast_node(n->if_expr.then, indent + 1);
        if (n->if_expr.else_branch) print_ast_node(n->if_expr.else_branch, indent + 1);
        break;
    case NODE_LET: case NODE_VAR:
        if (n->let.value) print_ast_node(n->let.value, indent + 1);
        break;
    case NODE_ASSIGN:
        print_ast_node(n->assign.target, indent + 1);
        print_ast_node(n->assign.value, indent + 1);
        break;
    case NODE_RETURN:
        if (n->ret.value) print_ast_node(n->ret.value, indent + 1);
        break;
    case NODE_FN_DECL:
        if (n->fn_decl.body) print_ast_node(n->fn_decl.body, indent + 1);
        break;
    case NODE_LAMBDA:
        if (n->lambda.body) print_ast_node(n->lambda.body, indent + 1);
        break;
    case NODE_INDEX:
        print_ast_node(n->index.obj, indent + 1);
        print_ast_node(n->index.index, indent + 1);
        break;
    case NODE_FIELD:
        print_ast_node(n->field.obj, indent + 1);
        break;
    case NODE_LIT_ARRAY:
        for (int i = 0; i < n->lit_array.elems.len; i++)
            print_ast_node(n->lit_array.elems.items[i], indent + 1);
        break;
    default: break;
    }
}

static void cmd_ast(const char *expr_str) {
    Lexer lex;
    lexer_init(&lex, expr_str, "<repl>");
    TokenArray ta = lexer_tokenize(&lex);
    Parser parser;
    parser_init(&parser, &ta, "<repl>");
    Node *prog = parser_parse(&parser);
    token_array_free(&ta);

    if (!prog || parser.had_error) {
        fprintf(stderr, "%sparse error at %d:%d: %s%s\n",
                g_theme->error, parser.error.span.line,
                parser.error.span.col, parser.error.msg, ANSI_RESET);
        if (prog) node_free(prog);
        return;
    }

    print_ast_node(prog, 0);
    node_free(prog);
}

static void cmd_tour(void) {
    printf("%s--- XS Language Tour ---%s\n\n", ANSI_BOLD, ANSI_RESET);

    char colored[XS_LINE_MAX];

    const char *examples[] = {
        "# 1. Variables and Types",
        "let name = \"world\"",
        "var count = 0",
        "let pi = 3.14159",
        "",
        "# 2. Functions",
        "fn greet(name) {",
        "    println(\"Hello, {name}!\")",
        "}",
        "",
        "# 3. Control Flow",
        "if count == 0 {",
        "    println(\"zero\")",
        "} else {",
        "    println(\"nonzero\")",
        "}",
        "",
        "# 4. Pattern Matching",
        "match value {",
        "    0 => println(\"zero\")",
        "    1..10 => println(\"small\")",
        "    _ => println(\"other\")",
        "}",
        "",
        "# 5. Collections",
        "let nums = [1, 2, 3, 4, 5]",
        "let doubled = nums.map((x) => x * 2)",
        "let evens = nums.filter((x) => x % 2 == 0)",
        "",
        "# 6. Structs and Enums",
        "struct Point { x, y }",
        "enum Shape { Circle(r), Rect(w, h) }",
        "",
        "# 7. Error Handling",
        "try { risky_operation() }",
        "catch e { println(\"Error: {e}\") }",
        "",
        "# 8. Modules",
        "import math",
        "let result = math.sqrt(16.0)",
        "",
        "# 9. Closures & Higher-Order",
        "let add = (a, b) => a + b",
        "let apply = (f, x, y) => f(x, y)",
        "",
        "# 10. Algebraic Effects",
        "effect Log { fn log(msg) }",
        "handle expr {",
        "    Log.log(msg) => { println(msg); resume null }",
        "}",
        NULL
    };

    for (int i = 0; examples[i]; i++) {
        if (strlen(examples[i]) == 0) {
            printf("\n");
        } else {
            colorize_line(examples[i], colored, sizeof(colored));
            printf("  %s\n", colored);
        }
    }
    printf("\n%sTry these examples in the REPL!%s\n", g_theme->timing, ANSI_RESET);
}

static void cmd_modules(void) {
    printf("%sAvailable standard library modules:%s\n", ANSI_BOLD, ANSI_RESET);
    int cols = get_terminal_width();
    int col = 2;
    printf("  ");
    for (int i = 0; xs_modules[i]; i++) {
        int len = (int)strlen(xs_modules[i]);
        if (col + len + 2 > cols) {
            printf("\n  ");
            col = 2;
        }
        printf("%s%s%s", g_theme->builtin, xs_modules[i], ANSI_RESET);
        if (xs_modules[i + 1]) printf(", ");
        col += len + 2;
    }
    printf("\n\nUse %simport <module>%s to load a module.\n",
           g_theme->keyword, ANSI_RESET);
}

static int handle_command(const char *line, Interp **interp_ptr, History *hist) {
    if (strcmp(line, ":help") == 0) {
        cmd_help();
        return 1;
    }
    if (strcmp(line, ":quit") == 0 || strcmp(line, ":exit") == 0) {
        return -1;
    }
    if (strcmp(line, ":reset") == 0) {
        interp_free(*interp_ptr);
        *interp_ptr = interp_new("<repl>");
        printf("%sinterpreter reset%s\n", g_theme->result, ANSI_RESET);
        return 1;
    }
    if (strcmp(line, ":env") == 0) {
        cmd_env(*interp_ptr);
        return 1;
    }
    if (strcmp(line, ":tour") == 0) {
        cmd_tour();
        return 1;
    }
    if (strcmp(line, ":modules") == 0) {
        cmd_modules();
        return 1;
    }
    if (strncmp(line, ":theme ", 7) == 0) {
        const char *theme_name = line + 7;
        while (*theme_name == ' ') theme_name++;
        if (strcmp(theme_name, "dark") == 0) {
            g_theme = &dark_theme;
            printf("switched to %sdark%s theme\n", ANSI_BOLD, ANSI_RESET);
        } else if (strcmp(theme_name, "light") == 0) {
            g_theme = &light_theme;
            printf("switched to %slight%s theme\n", ANSI_BOLD, ANSI_RESET);
        } else {
            fprintf(stderr, "%susage: :theme dark|light%s\n",
                    g_theme->error, ANSI_RESET);
        }
        return 1;
    }
    if (strncmp(line, ":save ", 6) == 0) {
        const char *path = line + 6;
        while (*path == ' ') path++;
        if (*path) {
            history_save_to(hist, path);
        } else {
            fprintf(stderr, "%susage: :save <file>%s\n",
                    g_theme->error, ANSI_RESET);
        }
        return 1;
    }
    if (strncmp(line, ":time ", 6) == 0) {
        const char *expr_str = line + 6;
        while (*expr_str == ' ') expr_str++;
        if (!*expr_str) {
            fprintf(stderr, "%susage: :time <expr>%s\n",
                    g_theme->error, ANSI_RESET);
            return 1;
        }
        cmd_time(*interp_ptr, expr_str);
        return 1;
    }
    if (strncmp(line, ":ast ", 5) == 0) {
        const char *expr_str = line + 5;
        while (*expr_str == ' ') expr_str++;
        if (!*expr_str) {
            fprintf(stderr, "%susage: :ast <expr>%s\n",
                    g_theme->error, ANSI_RESET);
            return 1;
        }
        cmd_ast(expr_str);
        return 1;
    }
    if (strncmp(line, ":type ", 6) == 0) {
        const char *expr_str = line + 6;
        while (*expr_str == ' ') expr_str++;
        if (!*expr_str) { printf("usage: :type <expr>\n"); return 1; }
        Lexer tlex; lexer_init(&tlex, expr_str, "<repl>");
        TokenArray tta = lexer_tokenize(&tlex);
        Parser tparser; parser_init(&tparser, &tta, "<repl>");
        Node *tprog = parser_parse(&tparser);
        token_array_free(&tta);
        if (!tprog || tparser.had_error) {
            fprintf(stderr, "%sparse error at %d:%d: %s%s\n",
                    g_theme->error, tparser.error.span.line,
                    tparser.error.span.col, tparser.error.msg, ANSI_RESET);
            if (tprog) node_free(tprog);
        } else {
            Node *target = tprog;
            if (tprog->tag == NODE_PROGRAM && tprog->program.stmts.len > 0) {
                target = tprog->program.stmts.items[0];
                if (target->tag == NODE_EXPR_STMT) target = target->expr_stmt.expr;
            }
            const char *type_name = "unknown";
            switch (target->tag) {
                case NODE_LIT_INT:    type_name = "Int"; break;
                case NODE_LIT_BIGINT: type_name = "Int"; break;
                case NODE_LIT_FLOAT:  type_name = "Float"; break;
                case NODE_LIT_STRING: type_name = "String"; break;
                case NODE_LIT_BOOL:   type_name = "Bool"; break;
                case NODE_LIT_NULL:   type_name = "Null"; break;
                case NODE_LIT_CHAR:   type_name = "Char"; break;
                case NODE_LIT_ARRAY:  type_name = "Array"; break;
                case NODE_LIT_TUPLE:  type_name = "Tuple"; break;
                case NODE_LIT_MAP:    type_name = "Map"; break;
                case NODE_LAMBDA:     type_name = "Fn"; break;
                case NODE_IDENT: {
                    Value *v = env_get((*interp_ptr)->globals, target->ident.name);
                    if (v) {
                        switch (v->tag) {
                            case XS_INT:   type_name = "Int"; break;
                            case XS_FLOAT: type_name = "Float"; break;
                            case XS_STR:   type_name = "String"; break;
                            case XS_BOOL:  type_name = "Bool"; break;
                            case XS_FUNC: case XS_NATIVE: type_name = "Fn"; break;
                            case XS_ARRAY: type_name = "Array"; break;
                            case XS_MAP:   type_name = "Map"; break;
                            case XS_NULL:  type_name = "Null"; break;
                            default: break;
                        }
                    }
                    break;
                }
                default: break;
            }
            printf("%s%s%s\n", g_theme->type, type_name, ANSI_RESET);
            node_free(tprog);
        }
        return 1;
    }
    if (strncmp(line, ":doc ", 5) == 0) {
        const char *name = line + 5;
        while (*name == ' ') name++;
        if (!*name) { printf("usage: :doc <name>\n"); return 1; }
        Value *v = env_get((*interp_ptr)->globals, name);
        if (!v) {
            printf("%sno documentation found for '%s'%s\n",
                   g_theme->timing, name, ANSI_RESET);
        } else if (v->tag == XS_FUNC) {
            printf("%sfunction%s '%s%s%s' (%d params)\n",
                   g_theme->keyword, ANSI_RESET,
                   g_theme->builtin, name, ANSI_RESET,
                   v->fn->nparams);
        } else if (v->tag == XS_NATIVE) {
            printf("%sbuiltin function%s '%s%s%s'\n",
                   g_theme->keyword, ANSI_RESET,
                   g_theme->builtin, name, ANSI_RESET);
        } else if (v->tag == XS_MAP || v->tag == XS_MODULE) {
            printf("%smodule%s with %d entries\n",
                   g_theme->keyword, ANSI_RESET,
                   v->map ? v->map->len : 0);
        } else {
            char *repr = value_repr(v);
            printf("%s: %s%s%s\n", name,
                   g_theme->result, repr ? repr : "(unknown)", ANSI_RESET);
            free(repr);
        }
        return 1;
    }
    if (strncmp(line, ":load ", 6) == 0) {
        const char *path = line + 6;
        while (*path == ' ') path++;
        if (*path) {
            cmd_load(*interp_ptr, path);
        } else {
            fprintf(stderr, "%susage: :load <file>%s\n",
                    g_theme->error, ANSI_RESET);
        }
        return 1;
    }
    fprintf(stderr, "%sunknown command: %s (:help for list)%s\n",
            g_theme->error, line, ANSI_RESET);
    return 1;
}

int repl_run(void) {
    printf("%s%sXS %s%s — interactive REPL\n",
           ANSI_BOLD, g_theme->prompt, XS_REPL_VERSION, ANSI_RESET);
    printf("Type '%s:help%s' for commands, '%s:quit%s' to exit.\n\n",
           g_theme->keyword, ANSI_RESET,
           g_theme->keyword, ANSI_RESET);

    Interp *interp = interp_new("<repl>");
    History hist;
    history_init(&hist);

    for (;;) {
        char *input = read_input(interp, &hist);
        if (!input) {
            printf("\n");
            break;
        }

        if (strlen(input) == 0) {
            free(input);
            continue;
        }

        history_add(&hist, input);

        if (input[0] == ':') {
            int rc = handle_command(input, &interp, &hist);
            free(input);
            if (rc == -1) break;
            continue;
        }

        if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            free(input);
            break;
        }

        Lexer lex;
        lexer_init(&lex, input, "<repl>");
        TokenArray ta = lexer_tokenize(&lex);

        Parser parser;
        parser_init(&parser, &ta, "<repl>");
        Node *program = parser_parse(&parser);
        token_array_free(&ta);

        if (!program || parser.had_error) {
            fprintf(stderr, "%sparse error at %d:%d: %s%s\n",
                    g_theme->error, parser.error.span.line,
                    parser.error.span.col, parser.error.msg, ANSI_RESET);
            if (program) node_free(program);
            free(input);
            continue;
        }

        double t0 = time_now_sec();
        interp_run(interp, program);
        double elapsed = time_now_sec() - t0;
        node_free(program);
        free(input);

        if (interp->cf.signal == CF_ERROR || interp->cf.signal == CF_PANIC) {
            Value *err = interp->cf.value;
            if (err && err->tag == XS_STR) {
                fprintf(stderr, "%serror: %s%s\n",
                        g_theme->error, err->s, ANSI_RESET);
            } else if (err) {
                char *s = value_repr(err);
                if (s) {
                    fprintf(stderr, "%serror: %s%s\n",
                            g_theme->error, s, ANSI_RESET);
                    free(s);
                }
            } else {
                fprintf(stderr, "%serror: (unknown)%s\n",
                        g_theme->error, ANSI_RESET);
            }
            if (interp->cf.value) {
                value_decref(interp->cf.value);
                interp->cf.value = NULL;
            }
            interp->cf.signal = 0;
        } else if (interp->cf.signal == CF_RETURN) {
            Value *result = interp->cf.value;
            if (result && result->tag != XS_NULL) {
                char *s = value_repr(result);
                if (s) {
                    printf("%s=> %s%s", g_theme->result, s, ANSI_RESET);
                    print_timing(elapsed);
                    printf("\n");
                    free(s);
                }
            }
            if (interp->cf.value) {
                value_decref(interp->cf.value);
                interp->cf.value = NULL;
            }
            interp->cf.signal = 0;
        } else {
            if (interp->cf.value && interp->cf.value->tag != XS_NULL) {
                char *s = value_repr(interp->cf.value);
                if (s) {
                    printf("%s=> %s%s", g_theme->result, s, ANSI_RESET);
                    print_timing(elapsed);
                    printf("\n");
                    free(s);
                }
            }
            if (interp->cf.value) {
                value_decref(interp->cf.value);
                interp->cf.value = NULL;
            }
            interp->cf.signal = 0;
        }
    }

    history_save_quiet(&hist, hist.path);
    history_free(&hist);
    interp_free(interp);
    return 0;
}
