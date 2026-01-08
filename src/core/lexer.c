#include "core/xs_compat.h"
#include "core/lexer.h"
#include "diagnostic/diagnostic.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

// keyword table
typedef struct { const char *word; TokenKind kind; } KWEntry;
static const KWEntry KEYWORDS[] = {
    {"if",       TK_IF},
    {"else",     TK_ELSE},
    {"elif",     TK_ELIF},
    {"while",    TK_WHILE},
    {"for",      TK_FOR},
    {"in",       TK_IN},
    {"loop",     TK_LOOP},
    {"break",    TK_BREAK},
    {"continue", TK_CONTINUE},
    {"return",   TK_RETURN},
    {"match",    TK_MATCH},
    {"when",     TK_WHEN},
    {"yield",    TK_YIELD},
    {"fn",       TK_FN},
    {"let",      TK_LET},
    {"var",      TK_VAR},
    {"const",    TK_CONST},
    {"type",     TK_TYPE},
    {"class",    TK_CLASS},
    {"struct",   TK_STRUCT},
    {"enum",     TK_ENUM},
    {"trait",    TK_TRAIT},
    {"impl",     TK_IMPL},
    {"module",   TK_MODULE},
    {"import",   TK_IMPORT},
    {"export",   TK_EXPORT},
    {"as",       TK_AS},
    {"from",     TK_FROM},
    {"async",    TK_ASYNC},
    {"await",    TK_AWAIT},
    {"actor",    TK_ACTOR},
    {"spawn",    TK_SPAWN},
    {"nursery",  TK_NURSERY},
    {"macro",    TK_MACRO},
    {"pub",      TK_PUB},
    {"mut",      TK_MUT},
    {"static",   TK_STATIC},
    {"and",      TK_AND},
    {"or",       TK_OR},
    {"not",      TK_NOT},
    {"is",       TK_IS},
    {"try",      TK_TRY},
    {"catch",    TK_CATCH},
    {"finally",  TK_FINALLY},
    {"throw",    TK_THROW},
    {"assert",   TK_ASSERT},
    {"panic",    TK_PANIC},
    {"defer",    TK_DEFER},
    {"self",     TK_SELF},
    {"super",    TK_SUPER},
    {"unsafe",   TK_UNSAFE},
    {"true",     TK_BOOL},
    {"false",    TK_BOOL},
    {"null",     TK_NULL},
    {"effect",   TK_EFFECT},
    {"perform",  TK_PERFORM},
    {"handle",   TK_HANDLE},
    {"resume",   TK_RESUME},
    {NULL,       TK_UNKNOWN},
};

static TokenKind lookup_keyword(const char *s) {
    for (int i = 0; KEYWORDS[i].word; i++)
        if (strcmp(KEYWORDS[i].word, s) == 0)
            return KEYWORDS[i].kind;
    return TK_IDENT;
}

static char lpeek(Lexer *l, int off) {
    int idx = l->pos + off;
    int len = (int)strlen(l->source);
    return idx < len ? l->source[idx] : '\0';
}

static char ladvance(Lexer *l) {
    char ch = l->source[l->pos++];
    if (ch == '\n') { l->line++; l->col = 1; }
    else            { l->col++; }
    return ch;
}

static Span make_span(Lexer *l, int sl, int sc, int sp) {
    Span s;
    s.file     = l->filename;
    s.line     = sl; s.col     = sc;
    s.end_line = l->line; s.end_col = l->col;
    s.offset   = sp; s.length  = l->pos - sp;
    return s;
}

static void cl_push(CommentList *cl, int line, int col, const char *text, int is_block) {
    if (cl->len >= cl->cap) {
        cl->cap = cl->cap ? cl->cap * 2 : 16;
        cl->items = xs_realloc(cl->items, (size_t)cl->cap * sizeof(Comment));
    }
    Comment *c = &cl->items[cl->len++];
    c->line = line;
    c->col  = col;
    c->text = xs_strdup(text);
    c->is_block = is_block;
}

void comment_list_free(CommentList *cl) {
    for (int i = 0; i < cl->len; i++)
        free(cl->items[i].text);
    free(cl->items);
    cl->items = NULL;
    cl->len = cl->cap = 0;
}

/* token array */
static void ta_push(TokenArray *ta, Token t) {
    if (ta->len >= ta->cap) {
        ta->cap = ta->cap ? ta->cap * 2 : 64;
        ta->items = xs_realloc(ta->items, ta->cap * sizeof(Token));
    }
    ta->items[ta->len++] = t;
}

typedef struct { char *buf; int len, cap; } StrBuf;

static void sb_init(StrBuf *sb) { sb->buf = NULL; sb->len = sb->cap = 0; }
static void sb_push(StrBuf *sb, char c) {
    if (sb->len >= sb->cap) {
        sb->cap = sb->cap ? sb->cap * 2 : 16;
        sb->buf = xs_realloc(sb->buf, sb->cap + 1);
    }
    sb->buf[sb->len++] = c;
}
static void sb_push_str(StrBuf *sb, const char *s) {
    while (*s) sb_push(sb, *s++);
}
static char *sb_finish(StrBuf *sb) {
    sb_push(sb, '\0');
    return sb->buf;  /* caller owns it */
}

/* escape sequences */
static char lex_escape(Lexer *l) {
    char esc = ladvance(l);
    switch (esc) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '\\': return '\\';
    case '"': return '"';
    case '\'': return '\'';
    case '0': return '\0';
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'v': return '\v';
    case 'e': return '\033';
    default:  return esc;
    }
}

/* Lex a string body. Keeps {expr} interpolation markers in the raw
   output; the parser splits them out later. */
static char *lex_string_body(Lexer *l, char quote, int *out_interp) {
    StrBuf sb; sb_init(&sb);
    int interp_depth = 0;
    *out_interp = 0;
    int start_line = l->line, start_col = l->col - 1; /* -1 for the opening quote */

    /* Check for triple-quote */
    int triple = 0;
    if (lpeek(l, 0) == quote && lpeek(l, 1) == quote) {
        ladvance(l); ladvance(l);
        triple = 1;
    }

    int terminated = 0;
    while (l->pos < (int)strlen(l->source)) {
        if (interp_depth == 0) {
            if (triple) {
                if (lpeek(l,0)==quote && lpeek(l,1)==quote && lpeek(l,2)==quote) {
                    ladvance(l); ladvance(l); ladvance(l);
                    terminated = 1;
                    break;
                }
            } else {
                if (lpeek(l,0) == quote) { ladvance(l); terminated = 1; break; }
                if (lpeek(l,0) == '\n') {
                    Span espan = {0};
                    espan.file = l->filename;
                    espan.line = start_line; espan.col = start_col;
                    espan.end_line = l->line; espan.end_col = l->col;
                    Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_LEXER,
                        "L0001", "unterminated string literal");
                    diag_annotate(d, espan, 1,
                        "string starts here but never closes");
                    diag_hint(d, "add a closing quote, or use triple quotes for multi-line strings");
                    diag_render_one(d, l->source, l->filename);
                    diag_free(d);
                    break;
                }
            }
        }

        char c = ladvance(l);

        if (interp_depth > 0) {
            sb_push(&sb, c);
            if (c == '{') interp_depth++;
            else if (c == '}') { interp_depth--; }
            else if (c == '"' || c == '\'') {
                /* consume inner string verbatim */
                char iq = c;
                while (l->pos < (int)strlen(l->source)) {
                    char nc = ladvance(l);
                    sb_push(&sb, nc);
                    if (nc == '\\') { sb_push(&sb, ladvance(l)); }
                    else if (nc == iq) break;
                }
            }
        } else {
            if (c == '{') {
                *out_interp = 1;
                interp_depth++;
                sb_push(&sb, c);
            } else if (c == '\\') {
                char esc = lpeek(l, 0);
                if (esc == '{') {
                    ladvance(l);
                    /* literal brace — use sentinel \x01{ so parser can detect */
                    sb_push(&sb, '\x01'); sb_push(&sb, '{');
                } else if (esc == '}') {
                    ladvance(l);
                    sb_push(&sb, '\x01'); sb_push(&sb, '}');
                } else {
                    sb_push(&sb, lex_escape(l));
                }
            } else {
                sb_push(&sb, c);
            }
        }
    }
    if (!terminated && triple) {
        Span espan = {0};
        espan.file = l->filename;
        espan.line = start_line; espan.col = start_col;
        espan.end_line = l->line; espan.end_col = l->col;
        Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_LEXER,
            "L0001", "unterminated triple-quoted string");
        diag_annotate(d, espan, 1,
            "string starts here but never closes");
        diag_hint(d, "add a closing triple-quote %c%c%c", quote, quote, quote);
        diag_render_one(d, l->source, l->filename);
        diag_free(d);
    }
    char *result = sb_finish(&sb);

    /* Triple-quote dedent: strip common leading whitespace */
    if (triple) {
        char *raw = result;
        int rlen = (int)strlen(raw);

        /* Find indentation of the closing """ (last line's leading whitespace) */
        int indent = 0;
        int i2 = rlen - 1;
        /* Walk back to find the start of the last line */
        while (i2 > 0 && raw[i2-1] != '\n') i2--;
        /* Count spaces/tabs on that last line */
        while (i2 < rlen && (raw[i2]==' ' || raw[i2]=='\t')) { indent++; i2++; }

        /* Build dedented string */
        StrBuf db; sb_init(&db);
        int pos2 = 0;
        /* Skip leading newline right after opening """ */
        if (pos2 < rlen && raw[pos2] == '\n') pos2++;

        while (pos2 < rlen) {
            /* Strip up to 'indent' whitespace chars at start of each line */
            int stripped = 0;
            while (stripped < indent && pos2 < rlen &&
                   (raw[pos2]==' ' || raw[pos2]=='\t')) {
                pos2++; stripped++;
            }
            /* Copy rest of line */
            while (pos2 < rlen && raw[pos2] != '\n') {
                sb_push(&db, raw[pos2++]);
            }
            if (pos2 < rlen && raw[pos2] == '\n') {
                sb_push(&db, '\n');
                pos2++;
            }
        }
        /* Remove trailing whitespace-only last line (the line before closing """) */
        char *dedented = sb_finish(&db);
        int dlen = (int)strlen(dedented);
        /* If it ends with \n followed by nothing (we already stripped indent), keep the \n */
        /* Actually: the last line (with closing """) should be removed entirely if it's whitespace-only */
        if (dlen > 0 && dedented[dlen-1] == '\n') {
            /* Check if the line before this \n is empty or the string ends with \n */
            /* The trailing \n from the last content line should remain */
        }
        free(raw);
        return dedented;
    }

    return result;
}

/* r"..." raw string */
static char *lex_raw_string_dquote(Lexer *l) {
    StrBuf sb; sb_init(&sb);
    int triple = 0;
    if (lpeek(l,0)=='"' && lpeek(l,1)=='"') {
        ladvance(l); ladvance(l); triple=1;
        /* Skip leading newline after opening r""" */
        if (lpeek(l,0)=='\n') ladvance(l);
    }
    while (l->pos < (int)strlen(l->source)) {
        if (triple) {
            if (lpeek(l,0)=='"' && lpeek(l,1)=='"' && lpeek(l,2)=='"') {
                ladvance(l); ladvance(l); ladvance(l); break;
            }
        } else {
            if (lpeek(l,0)=='"') { ladvance(l); break; }
            if (lpeek(l,0)=='\n') break;
        }
        sb_push(&sb, ladvance(l));
    }
    /* For triple raw strings, strip trailing whitespace-only last line */
    if (triple) {
        char *result = sb_finish(&sb);
        int rlen = (int)strlen(result);
        /* Find last newline */
        int last_nl = -1;
        for (int i3 = rlen-1; i3 >= 0; i3--) {
            if (result[i3] == '\n') { last_nl = i3; break; }
        }
        if (last_nl >= 0) {
            /* Check if everything after last \n is whitespace */
            int all_ws = 1;
            for (int i3 = last_nl+1; i3 < rlen; i3++) {
                if (result[i3] != ' ' && result[i3] != '\t') { all_ws = 0; break; }
            }
            if (all_ws) result[last_nl+1] = '\0';
        }
        return result;
    }
    return sb_finish(&sb);
}

/* backtick raw string */
static char *lex_raw_string(Lexer *l) {
    StrBuf sb; sb_init(&sb);
    int triple = 0;
    if (lpeek(l,0)=='`' && lpeek(l,1)=='`') {
        ladvance(l); ladvance(l); triple=1;
        /* Skip leading newline after opening ``` */
        if (lpeek(l,0)=='\n') ladvance(l);
    }
    while (l->pos < (int)strlen(l->source)) {
        if (triple) {
            if (lpeek(l,0)=='`' && lpeek(l,1)=='`' && lpeek(l,2)=='`') {
                ladvance(l); ladvance(l); ladvance(l); break;
            }
        } else {
            if (lpeek(l,0)=='`') { ladvance(l); break; }
        }
        sb_push(&sb, ladvance(l));
    }
    /* For triple raw strings, apply dedent (same as triple-quoted strings) */
    if (triple) {
        char *raw = sb_finish(&sb);
        int rlen = (int)strlen(raw);

        /* Find indentation of the closing ``` (last line's leading whitespace) */
        int indent = 0;
        int i2 = rlen - 1;
        while (i2 > 0 && raw[i2-1] != '\n') i2--;
        while (i2 < rlen && (raw[i2]==' ' || raw[i2]=='\t')) { indent++; i2++; }

        /* Build dedented string */
        StrBuf db; sb_init(&db);
        int pos2 = 0;

        while (pos2 < rlen) {
            /* Strip up to 'indent' whitespace chars at start of each line */
            int stripped = 0;
            while (stripped < indent && pos2 < rlen &&
                   (raw[pos2]==' ' || raw[pos2]=='\t')) {
                pos2++; stripped++;
            }
            /* Copy rest of line */
            while (pos2 < rlen && raw[pos2] != '\n') {
                sb_push(&db, raw[pos2++]);
            }
            if (pos2 < rlen && raw[pos2] == '\n') {
                sb_push(&db, '\n');
                pos2++;
            }
        }
        /* Remove trailing whitespace-only last line */
        char *dedented = sb_finish(&db);
        int dlen = (int)strlen(dedented);
        /* Find last newline */
        int last_nl = -1;
        for (int i3 = dlen-1; i3 >= 0; i3--) {
            if (dedented[i3] == '\n') { last_nl = i3; break; }
        }
        if (last_nl >= 0) {
            int all_ws = 1;
            for (int i3 = last_nl+1; i3 < dlen; i3++) {
                if (dedented[i3] != ' ' && dedented[i3] != '\t') { all_ws = 0; break; }
            }
            if (all_ws) dedented[last_nl+1] = '\0';
        }
        free(raw);
        return dedented;
    }
    return sb_finish(&sb);
}

static Token lex_number(Lexer *l, int sl, int sc, int sp) {
    StrBuf sb; sb_init(&sb);
    int is_float = 0;
    Token t; t.span = make_span(l,sl,sc,sp); t.sval = NULL; t.ival = 0;

    if (lpeek(l,0) == '0') {
        sb_push(&sb, ladvance(l));
        char pfx = tolower(lpeek(l,0));
        if (pfx == 'x') {
            sb_push(&sb, ladvance(l));
            while (isxdigit(lpeek(l,0)) || lpeek(l,0)=='_') {
                char c = ladvance(l); if (c!='_') sb_push(&sb,c);
            }
            char *s = sb_finish(&sb);
            errno = 0;
            t.ival = strtoll(s, NULL, 16);
            if (errno == ERANGE) { t.kind = TK_BIGINT; t.sval = xs_strdup(s); }
            else { t.kind = TK_INT; }
            free(s); t.span = make_span(l,sl,sc,sp); return t;
        } else if (pfx == 'b') {
            sb_push(&sb, ladvance(l));
            while (lpeek(l,0)=='0'||lpeek(l,0)=='1'||lpeek(l,0)=='_') {
                char c=ladvance(l); if(c!='_') sb_push(&sb,c);
            }
            char *s=sb_finish(&sb);
            /* skip "0b" prefix since strtoll base-2 doesn't handle it in C11 */
            errno = 0;
            t.ival = strtoll(s+2, NULL, 2);
            if (errno == ERANGE) { t.kind = TK_BIGINT; t.sval = xs_strdup(s); }
            else { t.kind = TK_INT; }
            free(s); t.span=make_span(l,sl,sc,sp); return t;
        } else if (pfx == 'o') {
            sb_push(&sb, ladvance(l));
            while ((lpeek(l,0)>='0'&&lpeek(l,0)<='7')||lpeek(l,0)=='_') {
                char c=ladvance(l); if(c!='_') sb_push(&sb,c);
            }
            char *s=sb_finish(&sb);
            errno = 0;
            t.ival = strtoll(s, NULL, 8);
            if (errno == ERANGE) { t.kind = TK_BIGINT; t.sval = xs_strdup(s); }
            else { t.kind = TK_INT; }
            free(s); t.span=make_span(l,sl,sc,sp); return t;
        }
    }

    while (isdigit(lpeek(l,0)) || lpeek(l,0)=='_') {
        char c=ladvance(l); if(c!='_') sb_push(&sb,c);
    }
    /* Allow fractional part only if previous token is NOT a dot (tuple field context) */
    {
        int prev_is_dot = (l->tokens.len > 0 && l->tokens.items[l->tokens.len-1].kind == TK_DOT);
        if (!prev_is_dot && lpeek(l,0)=='.' && isdigit(lpeek(l,1))) {
            is_float=1; sb_push(&sb,ladvance(l));
            while (isdigit(lpeek(l,0))||lpeek(l,0)=='_') {
                char c=ladvance(l); if(c!='_') sb_push(&sb,c);
            }
        }
    }
    if (tolower(lpeek(l,0))=='e') {
        is_float=1; sb_push(&sb,ladvance(l));
        if (lpeek(l,0)=='+'||lpeek(l,0)=='-') sb_push(&sb,ladvance(l));
        while (isdigit(lpeek(l,0))) sb_push(&sb,ladvance(l));
    }
    /* consume optional suffix like i32, u64, f64 */
    if (strchr("iuf",lpeek(l,0))) {
        while (isalnum(lpeek(l,0))) ladvance(l);
    }

    char *s = sb_finish(&sb);
    if (is_float) { t.kind=TK_FLOAT; t.fval=atof(s); }
    else {
        /* Check if the integer overflows i64 */
        errno = 0;
        char *endp;
        long long val = strtoll(s, &endp, 10);
        if (errno == ERANGE || (strlen(s) > 18 && strcmp(s, "9223372036854775807") > 0)) {
            /* Overflows i64 — store as bigint string */
            t.kind = TK_BIGINT;
            t.sval = xs_strdup(s);
        } else {
            t.kind = TK_INT;
            t.ival = val;
        }
    }
    free(s);
    t.span = make_span(l,sl,sc,sp);
    return t;
}

static Token lex_ident(Lexer *l, int sl, int sc, int sp) {
    StrBuf sb; sb_init(&sb);
    while (isalnum(lpeek(l,0)) || lpeek(l,0)=='_')
        sb_push(&sb, ladvance(l));
    char *name = sb_finish(&sb);
    Span span = make_span(l,sl,sc,sp);

    Token t; t.span = span; t.sval = NULL; t.ival = 0;
    TokenKind kw = lookup_keyword(name);
    t.kind = kw;
    if (kw == TK_BOOL) {
        t.ival = (name[0]=='t') ? 1 : 0;
        free(name);
    } else if (kw == TK_NULL) {
        t.ival = 0; free(name);
    } else {
        /* r"..." raw string literal */
        if (kw == TK_IDENT && name[0]=='r' && name[1]=='\0' && lpeek(l,0)=='"') {
            ladvance(l); /* consume opening " */
            char *s = lex_raw_string_dquote(l);
            free(name);
            t.kind = TK_RAW_STRING;
            t.sval = s;
            return t;
        }
        /* c"..." colored string literal: lex as string, emit with ANSI wrapping */
        /* Spec format: c"style;style;...;text" — styles first, text after last ';' */
        /* Also supports interpolation in the text portion */
        if (kw == TK_IDENT && name[0]=='c' && name[1]=='\0' && lpeek(l,0)=='"') {
            ladvance(l); /* consume opening " */
            int has_interp = 0;
            char *raw = lex_string_body(l, '"', &has_interp);
            /* Find the last semicolon to split styles from text */
            char *last_semi = NULL;
            { /* find last ';' that's not inside {} interpolation */
                int depth = 0;
                for (char *p = raw; *p; p++) {
                    if (*p == '{' && (p == raw || *(p-1) != '\x01')) depth++;
                    else if (*p == '}' && depth > 0) depth--;
                    else if (*p == ';' && depth == 0) last_semi = p;
                }
            }
            char ansi_buf[256] = "";
            char reset_buf[8] = "";
            char *text_start = raw;
            if (last_semi) {
                *last_semi = '\0';
                text_start = last_semi + 1;
                /* Skip leading space in text portion */
                while (*text_start == ' ') text_start++;
                char *styles_str = raw;
                /* collect ANSI codes from semicolon-separated style names */
                char codes[256] = "\033[";
                int codes_pos = 2; /* strlen("\033[") */
                int first_code = 1;
                char styles_copy[256];
                int style_len = (int)strlen(styles_str);
                if (style_len > 250) style_len = 250;
                memcpy(styles_copy, styles_str, style_len); styles_copy[style_len] = '\0';
                char *tok2 = strtok(styles_copy, ";");
                while (tok2) {
                    /* trim leading whitespace */
                    while (*tok2 == ' ') tok2++;
                    int code = -1;
                    int handled = 0;
                    /* styles */
                    if (strcmp(tok2,"bold")==0) code=1;
                    else if (strcmp(tok2,"dim")==0) code=2;
                    else if (strcmp(tok2,"italic")==0) code=3;
                    else if (strcmp(tok2,"underline")==0) code=4;
                    else if (strcmp(tok2,"blink")==0) code=5;
                    else if (strcmp(tok2,"reverse")==0) code=7;
                    else if (strcmp(tok2,"hidden")==0) code=8;
                    else if (strcmp(tok2,"strikethrough")==0) code=9;
                    /* foreground colors */
                    else if (strcmp(tok2,"black")==0) code=30;
                    else if (strcmp(tok2,"red")==0) code=31;
                    else if (strcmp(tok2,"green")==0) code=32;
                    else if (strcmp(tok2,"yellow")==0) code=33;
                    else if (strcmp(tok2,"blue")==0) code=34;
                    else if (strcmp(tok2,"magenta")==0) code=35;
                    else if (strcmp(tok2,"cyan")==0) code=36;
                    else if (strcmp(tok2,"white")==0) code=37;
                    /* bright foreground colors */
                    else if (strcmp(tok2,"bright_black")==0) code=90;
                    else if (strcmp(tok2,"bright_red")==0) code=91;
                    else if (strcmp(tok2,"bright_green")==0) code=92;
                    else if (strcmp(tok2,"bright_yellow")==0) code=93;
                    else if (strcmp(tok2,"bright_blue")==0) code=94;
                    else if (strcmp(tok2,"bright_magenta")==0) code=95;
                    else if (strcmp(tok2,"bright_cyan")==0) code=96;
                    else if (strcmp(tok2,"bright_white")==0) code=97;
                    /* background colors */
                    else if (strcmp(tok2,"bg_black")==0) code=40;
                    else if (strcmp(tok2,"bg_red")==0) code=41;
                    else if (strcmp(tok2,"bg_green")==0) code=42;
                    else if (strcmp(tok2,"bg_yellow")==0) code=43;
                    else if (strcmp(tok2,"bg_blue")==0) code=44;
                    else if (strcmp(tok2,"bg_magenta")==0) code=45;
                    else if (strcmp(tok2,"bg_cyan")==0) code=46;
                    else if (strcmp(tok2,"bg_white")==0) code=47;
                    /* bright background colors */
                    else if (strcmp(tok2,"bg_bright_black")==0) code=100;
                    else if (strcmp(tok2,"bg_bright_red")==0) code=101;
                    else if (strcmp(tok2,"bg_bright_green")==0) code=102;
                    else if (strcmp(tok2,"bg_bright_yellow")==0) code=103;
                    else if (strcmp(tok2,"bg_bright_blue")==0) code=104;
                    else if (strcmp(tok2,"bg_bright_magenta")==0) code=105;
                    else if (strcmp(tok2,"bg_bright_cyan")==0) code=106;
                    else if (strcmp(tok2,"bg_bright_white")==0) code=107;
                    /* fg256,N — 256-color foreground */
                    else if (strncmp(tok2,"fg256,",6)==0) {
                        int n = atoi(tok2+6);
                        if (!first_code) codes_pos += snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, ";");
                        codes_pos += snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, "38;5;%d",n);
                        first_code = 0; handled = 1;
                    }
                    /* bg256,N — 256-color background */
                    else if (strncmp(tok2,"bg256,",6)==0) {
                        int n = atoi(tok2+6);
                        if (!first_code) codes_pos += snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, ";");
                        codes_pos += snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, "48;5;%d",n);
                        first_code = 0; handled = 1;
                    }
                    /* rgb,R,G,B — truecolor foreground */
                    else if (strncmp(tok2,"rgb,",4)==0) {
                        int r2=0,g=0,b=0;
                        sscanf(tok2+4,"%d,%d,%d",&r2,&g,&b);
                        if (!first_code) codes_pos += snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, ";");
                        codes_pos += snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, "38;2;%d;%d;%d",r2,g,b);
                        first_code = 0; handled = 1;
                    }
                    /* bgrgb,R,G,B — truecolor background */
                    else if (strncmp(tok2,"bgrgb,",6)==0) {
                        int r2=0,g=0,b=0;
                        sscanf(tok2+6,"%d,%d,%d",&r2,&g,&b);
                        if (!first_code) codes_pos += snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, ";");
                        codes_pos += snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, "48;2;%d;%d;%d",r2,g,b);
                        first_code = 0; handled = 1;
                    }
                    if (code >= 0 && !handled) {
                        if (!first_code) codes_pos += snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, ";");
                        codes_pos += snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, "%d",code);
                        first_code = 0;
                    }
                    tok2 = strtok(NULL, ";");
                }
                if (!first_code) {
                    snprintf(codes+codes_pos, sizeof(codes)-(size_t)codes_pos, "m");
                    snprintf(ansi_buf, sizeof(ansi_buf), "%s", codes);
                    snprintf(reset_buf, sizeof(reset_buf), "\033[0m");
                }
            }
            /* Build final string: ansi_buf + text + reset_buf */
            /* If text has interpolation markers, we need to wrap with ANSI
               but still emit as TK_STRING so the parser can handle interpolation */
            char *text_dup = xs_strdup(text_start);
            size_t total = strlen(ansi_buf) + strlen(text_dup) + strlen(reset_buf) + 1;
            char *final_str = xs_malloc(total);
            snprintf(final_str, total, "%s%s%s", ansi_buf, text_dup, reset_buf);
            free(text_dup);
            free(raw); free(name);
            t.kind = TK_STRING;
            t.sval = final_str;
            return t;
        }
        /* Check for macro: name! (not !=) */
        if (lpeek(l,0)=='!' && lpeek(l,1)!='=') {
            ladvance(l);
            t.kind = TK_MACRO_BANG;
        }
        t.sval = name; /* owns it */
    }
    return t;
}

// main tokenizer
static void lex_next(Lexer *l) {
    int sl = l->line, sc = l->col, sp = l->pos;
    char ch = lpeek(l, 0);

    if (ch==' '||ch=='\t'||ch=='\r') {
        while (lpeek(l,0)==' '||lpeek(l,0)=='\t'||lpeek(l,0)=='\r')
            ladvance(l);
        return;
    }

    if (ch=='\n') {
        ladvance(l);
        return;
    }

    if (ch=='#') {
        if (lpeek(l,1)=='[') {
            ladvance(l); ladvance(l);
            Token t; t.kind=TK_HASH_BRACKET; t.sval=NULL;
            t.span=make_span(l,sl,sc,sp);
            ta_push(&l->tokens, t); return;
        }
        if (lpeek(l,1)=='{') {
            ladvance(l); /* skip '#', let next iteration handle '{' */
            return;
        }
        if (lpeek(l,1)=='!') {
            if (lpeek(l,2)=='[') {
                ladvance(l); ladvance(l); ladvance(l);
                Token t; t.kind=TK_UNKNOWN; t.sval=xs_strdup("#![");
                t.span=make_span(l,sl,sc,sp);
                ta_push(&l->tokens, t); return;
            }
            while (lpeek(l,0)!='\n' && lpeek(l,0)!='\0') ladvance(l);
            return;
        }
        ladvance(l);
        return;
    }

    /* {- block comment -} — nestable */
    if (ch=='{' && lpeek(l,1)=='-') {
        int cline = l->line, ccol = l->col;
        StrBuf csb; sb_init(&csb);
        sb_push_str(&csb, "{-");
        ladvance(l); ladvance(l); /* consume {- */
        int depth=1;
        while (depth>0 && lpeek(l,0)!='\0') {
            char c=ladvance(l);
            sb_push(&csb, c);
            if (c=='{' && lpeek(l,0)=='-') { sb_push(&csb, ladvance(l)); depth++; }
            else if (c=='-' && lpeek(l,0)=='}') { sb_push(&csb, ladvance(l)); depth--; }
        }
        if (depth > 0) {
            Span espan = {0};
            espan.file = l->filename;
            espan.line = cline; espan.col = ccol;
            espan.end_line = l->line; espan.end_col = l->col;
            Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_LEXER,
                "L0002", "unterminated block comment");
            diag_annotate(d, espan, 1,
                "block comment starts here but never closes");
            diag_hint(d, "add closing `-}` to match the opening `{-`");
            if (depth > 1)
                diag_note(d, "nesting depth is %d — %d closing `-}` delimiters needed",
                          depth, depth);
            diag_render_one(d, l->source, l->filename);
            diag_free(d);
        }
        char *ctxt = sb_finish(&csb);
        cl_push(&l->comments, cline, ccol, ctxt, 1);
        free(ctxt);
        return;
    }

    if (ch=='"' || ch=='\'') {
        ladvance(l);
        int interp=0;
        char *s = lex_string_body(l, ch, &interp);
        Token t; t.span=make_span(l,sl,sc,sp);
        t.kind = TK_STRING;
        t.sval = s;
        ta_push(&l->tokens, t); return;
    }
    if (ch=='`') {
        ladvance(l);
        char *s = lex_raw_string(l);
        Token t; t.kind=TK_RAW_STRING; t.sval=s;
        t.span=make_span(l,sl,sc,sp);
        ta_push(&l->tokens, t); return;
    }
    if (ch=='\'' && lpeek(l,1)!='\\' && lpeek(l,2)=='\'') {
        ladvance(l);
        char c = ladvance(l);
        ladvance(l); /* closing ' */
        char *s = xs_malloc(2); s[0]=c; s[1]='\0';
        Token t; t.kind=TK_CHAR; t.sval=s;
        t.span=make_span(l,sl,sc,sp);
        ta_push(&l->tokens, t); return;
    }

    /* numbers */
    if (isdigit(ch)) {
        Token t = lex_number(l, sl, sc, sp);
        ta_push(&l->tokens, t); return;
    }
    if (ch=='.' && isdigit(lpeek(l,1))) {
        int is_field = 0;
        if (l->tokens.len > 0) {
            TokenKind prev = l->tokens.items[l->tokens.len-1].kind;
            if (prev == TK_IDENT || prev == TK_RPAREN || prev == TK_RBRACKET
                || prev == TK_INT || prev == TK_FLOAT || prev == TK_STRING)
                is_field = 1;
        }
        if (!is_field) {
            Token t = lex_number(l, sl, sc, sp);
            ta_push(&l->tokens, t); return;
        }
        /* fall through to operators where '.' will be lexed as TK_DOT */
    }

    if (isalpha(ch) || ch=='_') {
        Token t = lex_ident(l, sl, sc, sp);
        ta_push(&l->tokens, t); return;
    }

    ladvance(l);
    char nx = lpeek(l, 0);
    Token t; t.span=make_span(l,sl,sc,sp); t.sval=NULL;

#define TOK1(k)    do { t.kind=(k); ta_push(&l->tokens,t); return; } while(0)
#define TOK2(k)    do { ladvance(l); t.kind=(k); t.span=make_span(l,sl,sc,sp); ta_push(&l->tokens,t); return; } while(0)
#define TOK3(k)    do { ladvance(l); ladvance(l); t.kind=(k); t.span=make_span(l,sl,sc,sp); ta_push(&l->tokens,t); return; } while(0)

    switch (ch) {
    case '+':
        if (nx=='=') TOK2(TK_PLUS_ASSIGN);
        if (nx=='+') TOK2(TK_CONCAT);
        TOK1(TK_PLUS);
    case '-':
        if (nx=='=') TOK2(TK_MINUS_ASSIGN);
        if (nx=='>') TOK2(TK_ARROW);
        if (nx=='-') {
            /* -- line comment: capture text (first '-' already consumed) */
            int cline = l->line, ccol = l->col - 1;
            StrBuf csb; sb_init(&csb);
            sb_push_str(&csb, "--");
            ladvance(l); /* consume second dash (first was consumed before switch) */
            while (lpeek(l,0)!='\n' && lpeek(l,0)!='\0') {
                sb_push(&csb, ladvance(l));
            }
            char *ctxt = sb_finish(&csb);
            cl_push(&l->comments, cline, ccol, ctxt, 0);
            free(ctxt);
            return;
        }
        TOK1(TK_MINUS);
    case '*':
        if (nx=='*') {
            ladvance(l);
            if (lpeek(l,0)=='=') TOK2(TK_POWER_ASSIGN);
            t.kind=TK_POWER; t.span=make_span(l,sl,sc,sp); ta_push(&l->tokens,t); return;
        }
        if (nx=='=') TOK2(TK_STAR_ASSIGN);
        TOK1(TK_STAR);
    case '/':
        if (nx=='=') TOK2(TK_SLASH_ASSIGN);
        if (nx=='/') TOK2(TK_FLOORDIV);
        TOK1(TK_SLASH);
    case '%':
        if (nx=='=') TOK2(TK_PERCENT_ASSIGN);
        TOK1(TK_PERCENT);
    case '=':
        if (nx=='=') TOK2(TK_EQ);
        if (nx=='>') TOK2(TK_FAT_ARROW);
        TOK1(TK_ASSIGN);
    case '!':
        if (nx=='=') TOK2(TK_NEQ);
        TOK1(TK_OP_NOT);
    case '<':
        if (nx=='=') {
            ladvance(l);
            if (lpeek(l,0)=='>') TOK2(TK_SPACESHIP);
            t.kind=TK_LE; t.span=make_span(l,sl,sc,sp); ta_push(&l->tokens,t); return;
        }
        if (nx=='<') {
            ladvance(l);
            if (lpeek(l,0)=='=') TOK2(TK_SHL_ASSIGN);
            t.kind=TK_SHL; t.span=make_span(l,sl,sc,sp); ta_push(&l->tokens,t); return;
        }
        TOK1(TK_LT);
    case '>':
        if (nx=='=') TOK2(TK_GE);
        if (nx=='>') {
            ladvance(l);
            if (lpeek(l,0)=='=') TOK2(TK_SHR_ASSIGN);
            t.kind=TK_SHR; t.span=make_span(l,sl,sc,sp); ta_push(&l->tokens,t); return;
        }
        TOK1(TK_GT);
    case '&':
        if (nx=='&') TOK2(TK_OP_AND);
        if (nx=='=') TOK2(TK_AND_ASSIGN);
        TOK1(TK_AMP);
    case '|':
        if (nx=='|') TOK2(TK_OP_OR);
        if (nx=='=') TOK2(TK_OR_ASSIGN);
        if (nx=='>') TOK2(TK_PIPE_ARROW);
        TOK1(TK_PIPE);
    case '^':
        if (nx=='=') TOK2(TK_XOR_ASSIGN);
        TOK1(TK_CARET);
    case '~': TOK1(TK_TILDE);
    case '.':
        if (nx=='.') {
            ladvance(l);
            if (lpeek(l,0)=='.') TOK2(TK_DOTDOTDOT);
            if (lpeek(l,0)=='=') TOK2(TK_DOTDOTEQ);
            t.kind=TK_DOTDOT; t.span=make_span(l,sl,sc,sp); ta_push(&l->tokens,t); return;
        }
        TOK1(TK_DOT);
    case ':':
        if (nx==':') TOK2(TK_COLON_COLON);
        TOK1(TK_COLON);
    case '?':
        if (nx=='?') TOK2(TK_NULL_COALESCE);
        if (nx=='.') TOK2(TK_QUESTION_DOT);
        TOK1(TK_QUESTION);
    case '@': TOK1(TK_AT);
    case '(':
        l->paren_depth++;
        TOK1(TK_LPAREN);
    case ')':
        if (l->paren_depth>0) l->paren_depth--;
        TOK1(TK_RPAREN);
    case '{':
        l->paren_depth++;
        TOK1(TK_LBRACE);
    case '}':
        if (l->paren_depth>0) l->paren_depth--;
        TOK1(TK_RBRACE);
    case '[':
        l->paren_depth++;
        TOK1(TK_LBRACKET);
    case ']':
        if (l->paren_depth>0) l->paren_depth--;
        TOK1(TK_RBRACKET);
    case ',': TOK1(TK_COMMA);
    case ';': TOK1(TK_SEMICOLON);
    default:
        t.kind=TK_UNKNOWN;
        t.sval=xs_malloc(2); t.sval[0]=ch; t.sval[1]='\0';
        ta_push(&l->tokens, t); return;
    }
#undef TOK1
#undef TOK2
#undef TOK3
}

/* public API */
void lexer_init(Lexer *l, const char *source, const char *filename) {
    l->source    = source;
    l->filename  = filename ? filename : "<stdin>";
    l->pos       = 0;
    l->line      = 1;
    l->col       = 1;
    l->paren_depth = 0;
    l->tokens.items = NULL;
    l->tokens.len   = 0;
    l->tokens.cap   = 0;
    l->comments.items = NULL;
    l->comments.len   = 0;
    l->comments.cap   = 0;
}

TokenArray lexer_tokenize(Lexer *l) {
    int srclen = (int)strlen(l->source);
    while (l->pos < srclen)
        lex_next(l);

    /* EOF */
    Token eof;
    eof.kind   = TK_EOF;
    eof.sval   = NULL;
    eof.span   = make_span(l, l->line, l->col, l->pos);
    ta_push(&l->tokens, eof);

    return l->tokens;
}

void lexer_free(Lexer *l) {
    token_array_free(&l->tokens);
    comment_list_free(&l->comments);
}

void token_array_free(TokenArray *ta) {
    for (int i = 0; i < ta->len; i++) {
        Token *t = &ta->items[i];
        if (t->kind == TK_STRING || t->kind == TK_RAW_STRING ||
            t->kind == TK_CHAR   || t->kind == TK_IDENT      ||
            t->kind == TK_BIGINT ||
            t->kind == TK_MACRO_BANG || t->kind == TK_UNKNOWN)
            free(t->sval);
    }
    free(ta->items);
    ta->items = NULL; ta->len = ta->cap = 0;
}

const char *token_kind_name(TokenKind k) {
    switch (k) {
    case TK_INT: return "INT"; case TK_BIGINT: return "BIGINT";
    case TK_FLOAT: return "FLOAT";
    case TK_STRING: return "STRING"; case TK_CHAR: return "CHAR";
    case TK_BOOL: return "BOOL"; case TK_NULL: return "NULL";
    case TK_IDENT: return "IDENT";
    case TK_IF: return "if"; case TK_ELSE: return "else";
    case TK_ELIF: return "elif"; case TK_WHILE: return "while";
    case TK_FOR: return "for"; case TK_IN: return "in";
    case TK_LOOP: return "loop"; case TK_BREAK: return "break";
    case TK_CONTINUE: return "continue"; case TK_RETURN: return "return";
    case TK_MATCH: return "match"; case TK_FN: return "fn";
    case TK_LET: return "let"; case TK_VAR: return "var";
    case TK_CONST: return "const"; case TK_CLASS: return "class";
    case TK_STRUCT: return "struct"; case TK_ENUM: return "enum";
    case TK_IMPL: return "impl"; case TK_IMPORT: return "import";
    case TK_PLUS: return "+"; case TK_MINUS: return "-";
    case TK_STAR: return "*"; case TK_SLASH: return "/";
    case TK_PERCENT: return "%"; case TK_POWER: return "**";
    case TK_EQ: return "=="; case TK_NEQ: return "!=";
    case TK_LT: return "<"; case TK_GT: return ">";
    case TK_LE: return "<="; case TK_GE: return ">=";
    case TK_ASSIGN: return "="; case TK_DOT: return ".";
    case TK_DOTDOT: return ".."; case TK_DOTDOTEQ: return "..=";
    case TK_DOTDOTDOT: return "..."; case TK_COMMA: return ",";
    case TK_SEMICOLON: return ";"; case TK_COLON: return ":";
    case TK_COLON_COLON: return "::"; case TK_LPAREN: return "(";
    case TK_RPAREN: return ")"; case TK_LBRACE: return "{";
    case TK_RBRACE: return "}"; case TK_LBRACKET: return "[";
    case TK_RBRACKET: return "]"; case TK_ARROW: return "->";
    case TK_FAT_ARROW: return "=>"; case TK_PIPE: return "|";
    case TK_OP_AND: return "&&"; case TK_OP_OR: return "||";
    case TK_OP_NOT: return "!"; case TK_NULL_COALESCE: return "??";
    case TK_EOF: return "EOF";
    case TK_EFFECT: return "effect"; case TK_PERFORM: return "perform";
    case TK_HANDLE: return "handle"; case TK_RESUME: return "resume";
    case TK_ASYNC: return "async"; case TK_AWAIT: return "await";
    case TK_SPAWN: return "spawn"; case TK_NURSERY: return "nursery";
    case TK_ACTOR: return "actor";
    case TK_HASH_BRACKET: return "#[";
    case TK_UNSAFE: return "unsafe";
    default: return "?";
    }
}
