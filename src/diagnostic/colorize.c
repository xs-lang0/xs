#include "diagnostic/colorize.h"
#include "core/xs.h"

#include <string.h>
#include <ctype.h>

#define ANSI_RESET   "\033[0m"
#define COL_KEYWORD  "\033[34m"
#define COL_STRING   "\033[32m"
#define COL_NUMBER   "\033[36m"
#define COL_COMMENT  "\033[90m"
#define COL_OPERATOR "\033[33m"
#define COL_BUILTIN  "\033[35m"
#define COL_TYPE     "\033[33;1m"

static const char *diag_keywords[] = {
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

static const char *diag_builtins[] = {
    "print", "println", "input", "len", "type", "range",
    "keys", "values", "entries", "map", "filter", "reduce",
    "zip", "any", "all", "min", "max", "sum", "sort",
    "reverse", "push", "pop", "slice", "join", "split",
    "contains", "starts_with", "ends_with", "trim",
    "upper", "lower", "replace", "int", "float", "str",
    "bool", "char", "typeof", "assert", "panic",
    NULL
};

static int diag_is_keyword(const char *word) {
    for (int i = 0; diag_keywords[i]; i++)
        if (strcmp(diag_keywords[i], word) == 0) return 1;
    return 0;
}

static int diag_is_builtin(const char *word) {
    for (int i = 0; diag_builtins[i]; i++)
        if (strcmp(diag_builtins[i], word) == 0) return 1;
    return 0;
}

void diag_colorize_line(const char *line, char *out, size_t outsz) {
    if (g_no_color) {
        size_t len = strlen(line);
        if (len >= outsz) len = outsz - 1;
        memcpy(out, line, len);
        out[len] = '\0';
        return;
    }

    size_t j = 0;
    size_t len = strlen(line);

    #define EMIT(s) do { \
        const char *_s = (s); \
        while (*_s && j + 1 < outsz) out[j++] = *_s++; \
    } while(0)

    #define EMIT_CHAR(c) do { if (j + 1 < outsz) out[j++] = (c); } while(0)

    for (size_t i = 0; i < len; ) {
        if (line[i] == '-' && i + 1 < len && line[i+1] == '-') {
            EMIT(COL_COMMENT);
            while (i < len) EMIT_CHAR(line[i++]);
            EMIT(ANSI_RESET);
            break;
        }

        /* {- ... -} nestable block comments */
        if (line[i] == '{' && i + 1 < len && line[i+1] == '-') {
            EMIT(COL_COMMENT);
            EMIT_CHAR(line[i++]); EMIT_CHAR(line[i++]);
            int depth = 1;
            while (i < len && depth > 0) {
                if (line[i] == '{' && i + 1 < len && line[i+1] == '-') {
                    EMIT_CHAR(line[i++]); EMIT_CHAR(line[i++]); depth++;
                } else if (line[i] == '-' && i + 1 < len && line[i+1] == '}') {
                    EMIT_CHAR(line[i++]); EMIT_CHAR(line[i++]); depth--;
                } else {
                    EMIT_CHAR(line[i++]);
                }
            }
            EMIT(ANSI_RESET);
            continue;
        }

        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            EMIT(COL_STRING);
            EMIT_CHAR(line[i++]);
            while (i < len && line[i] != quote) {
                if (line[i] == '\\' && i + 1 < len) {
                    EMIT_CHAR(line[i++]);
                }
                EMIT_CHAR(line[i++]);
            }
            if (i < len) EMIT_CHAR(line[i++]);
            EMIT(ANSI_RESET);
            continue;
        }

        if (isdigit((unsigned char)line[i]) ||
            (line[i] == '.' && i + 1 < len && isdigit((unsigned char)line[i+1]))) {
            if (i == 0 || !isalnum((unsigned char)line[i-1])) {
                EMIT(COL_NUMBER);
                if (line[i] == '0' && i + 1 < len && (line[i+1] == 'x' || line[i+1] == 'X')) {
                    EMIT_CHAR(line[i++]);
                    EMIT_CHAR(line[i++]);
                    while (i < len && (isxdigit((unsigned char)line[i]) || line[i] == '_'))
                        EMIT_CHAR(line[i++]);
                } else {
                    while (i < len && (isdigit((unsigned char)line[i]) || line[i] == '.' || line[i] == '_' ||
                                       line[i] == 'e' || line[i] == 'E'))
                        EMIT_CHAR(line[i++]);
                }
                EMIT(ANSI_RESET);
                continue;
            }
        }

        if (isalpha((unsigned char)line[i]) || line[i] == '_') {
            size_t start = i;
            while (i < len && (isalnum((unsigned char)line[i]) || line[i] == '_'))
                i++;
            size_t wlen = i - start;
            char word[128];
            if (wlen >= sizeof(word)) wlen = sizeof(word) - 1;
            memcpy(word, line + start, wlen);
            word[wlen] = '\0';

            if (diag_is_keyword(word)) {
                EMIT(COL_KEYWORD);
                for (size_t k = start; k < start + wlen; k++) EMIT_CHAR(line[k]);
                EMIT(ANSI_RESET);
            } else if (diag_is_builtin(word)) {
                EMIT(COL_BUILTIN);
                for (size_t k = start; k < start + wlen; k++) EMIT_CHAR(line[k]);
                EMIT(ANSI_RESET);
            } else if (wlen > 0 && isupper((unsigned char)word[0])) {
                EMIT(COL_TYPE);
                for (size_t k = start; k < start + wlen; k++) EMIT_CHAR(line[k]);
                EMIT(ANSI_RESET);
            } else {
                for (size_t k = start; k < start + wlen; k++) EMIT_CHAR(line[k]);
            }
            continue;
        }

        if (strchr("+-*/%=<>!&|^~?:@", line[i])) {
            EMIT(COL_OPERATOR);
            EMIT_CHAR(line[i++]);
            if (i < len && strchr("=>&|+-<>.*?", line[i])) {
                EMIT_CHAR(line[i++]);
                if (i < len && (line[i] == '=' || line[i] == '>'))
                    EMIT_CHAR(line[i++]);
            }
            EMIT(ANSI_RESET);
            continue;
        }

        EMIT_CHAR(line[i++]);
    }

    out[j] = '\0';
    #undef EMIT
    #undef EMIT_CHAR
}
