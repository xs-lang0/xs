#include "diagnostic/diagnostic.h"
#include "diagnostic/colorize.h"
#include "core/xs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RST       "\033[0m"
#define BOLD_RED  "\033[31;1m"
#define BOLD_YEL  "\033[33;1m"
#define MAGENTA   "\033[35m"
#define GRAY      "\033[90m"
#define CYAN      "\033[36m"
#define WHITE     "\033[37m"
#define BOLD      "\033[1m"
#define BLUE      "\033[34m"

/* Returns pointer into source for 1-based line_num, sets *line_len. */
static const char *get_line(const char *source, int line_num, int *line_len) {
    if (!source || line_num < 1) return NULL;
    const char *p = source;
    int cur = 1;
    while (cur < line_num) {
        const char *nl = strchr(p, '\n');
        if (!nl) return NULL;
        p = nl + 1;
        cur++;
    }
    const char *end = strchr(p, '\n');
    *line_len = end ? (int)(end - p) : (int)strlen(p);
    return p;
}

static const char *ctx_source_for(DiagContext *ctx, const char *file) {
    if (!ctx || !file) return NULL;
    for (int i = 0; i < ctx->n_sources; i++) {
        if (strcmp(ctx->filenames[i], file) == 0)
            return ctx->sources[i];
    }
    return NULL;
}

static const char *phase_prefix(DiagPhase phase, DiagSeverity sev) {
    if (sev == DIAG_WARNING) {
        switch (phase) {
            case DIAG_PHASE_LEXER:    return "syntax warning";
            case DIAG_PHASE_PARSER:   return "parse warning";
            case DIAG_PHASE_SEMANTIC: return "warning";
            case DIAG_PHASE_RUNTIME:  return "runtime warning";
        }
    } else {
        switch (phase) {
            case DIAG_PHASE_LEXER:    return "syntax error";
            case DIAG_PHASE_PARSER:   return "parse error";
            case DIAG_PHASE_SEMANTIC: return "error";
            case DIAG_PHASE_RUNTIME:  return "runtime error";
        }
    }
    return "error";
}

static void emit_backtick_text(FILE *out, const char *text, const char *text_color) {
    const char *p = text;
    while (*p) {
        if (*p == '`') {
            p++;
            fprintf(out, "%s", DIAG_COLOR(BOLD));
            while (*p && *p != '`')
                fputc(*p++, out);
            fprintf(out, "%s%s", DIAG_COLOR(RST), DIAG_COLOR(text_color));
            if (*p == '`') p++;
        } else {
            fputc(*p++, out);
        }
    }
}

static int gutter_width(int max_line) {
    int w = 1;
    int n = max_line;
    while (n >= 10) { w++; n /= 10; }
    return w < 3 ? 3 : w;
}


static void render_diagnostic(Diagnostic *d, const char *source,
                              int n_instances, FILE *out) {
    const char *sev_color = (d->severity == DIAG_ERROR) ?
        DIAG_COLOR(BOLD_RED) : DIAG_COLOR(BOLD_YEL);
    const char *rst = DIAG_COLOR(RST);
    const char *gray = DIAG_COLOR(GRAY);
    const char *mag = DIAG_COLOR(MAGENTA);

    fprintf(out, "%s%s", sev_color, phase_prefix(d->phase, d->severity));
    if (d->code) {
        fprintf(out, "%s[%s%s%s]", gray, mag, d->code, gray);
    }
    fprintf(out, "%s: %s%s", sev_color, rst, d->message);
    if (n_instances > 1)
        fprintf(out, " (%d instances)", n_instances);
    fprintf(out, "\n");

    DiagAnnotation *primary = NULL;
    for (int i = 0; i < d->n_annotations; i++) {
        if (d->annotations[i].is_primary) { primary = &d->annotations[i]; break; }
    }
    if (primary) {
        fprintf(out, " %s-->%s %s%s%s:%d:%d\n",
                gray, rst,
                mag, primary->span.file, rst,
                primary->span.line, primary->span.col);
    }

    if (source && d->n_annotations > 0) {
        /* Sort annotations by line */
        for (int i = 1; i < d->n_annotations; i++) {
            DiagAnnotation tmp = d->annotations[i];
            int j = i - 1;
            while (j >= 0 && d->annotations[j].span.line > tmp.span.line) {
                d->annotations[j + 1] = d->annotations[j];
                j--;
            }
            d->annotations[j + 1] = tmp;
        }

        int max_ln = 0;
        for (int i = 0; i < d->n_annotations; i++) {
            int el = d->annotations[i].span.end_line;
            if (el < d->annotations[i].span.line) el = d->annotations[i].span.line;
            if (el > max_ln) max_ln = el;
        }
        int gw = gutter_width(max_ln);

        fprintf(out, "\n");

        int last_rendered_line = 0;

        for (int ai = 0; ai < d->n_annotations; ) {
            int group_start = ai;
            int group_end = ai;
            while (group_end + 1 < d->n_annotations &&
                   d->annotations[group_end + 1].span.line -
                   d->annotations[group_end].span.line <= 3) {
                group_end++;
            }

            int block_first_line = d->annotations[group_start].span.line;
            int block_last_line = d->annotations[group_end].span.line;
            for (int k = group_start; k <= group_end; k++) {
                int el = d->annotations[k].span.end_line;
                if (el > block_last_line) block_last_line = el;
            }

            if (last_rendered_line > 0 &&
                block_first_line - last_rendered_line > 3) {
                fprintf(out, " %s%*s...%s\n", gray, gw - 2, "", rst);
            }

            for (int ln = block_first_line; ln <= block_last_line; ln++) {
                int line_len = 0;
                const char *line_ptr = get_line(source, ln, &line_len);
                if (!line_ptr) continue;

                char linebuf[1024];
                int copylen = line_len < (int)sizeof(linebuf) - 1 ? line_len : (int)sizeof(linebuf) - 1;
                memcpy(linebuf, line_ptr, (size_t)copylen);
                linebuf[copylen] = '\0';

                char colored[4096];
                diag_colorize_line(linebuf, colored, sizeof colored);

                fprintf(out, " %s%*d |%s %s\n",
                        gray, gw, ln, rst, colored);

                for (int k = group_start; k <= group_end; k++) {
                    DiagAnnotation *a = &d->annotations[k];
                    if (a->span.line != ln && a->span.end_line != ln) {
                        if (ln > a->span.line && ln < a->span.end_line) {
                            const char *ul_color = a->is_primary ?
                                DIAG_COLOR(BOLD_RED) : DIAG_COLOR(CYAN);
                            char ul_char = a->is_primary ? '^' : '-';
                            fprintf(out, " %s%*s |%s ", gray, gw, "", rst);
                            fprintf(out, "%s", ul_color);
                            for (int c = 0; c < copylen; c++)
                                fputc(ul_char, out);
                            fprintf(out, "%s\n", rst);
                        }
                        continue;
                    }

                    int ul_start, ul_len;
                    if (a->span.line == a->span.end_line || a->span.end_line == 0) {
                        ul_start = a->span.col;
                        ul_len = a->span.end_col - a->span.col;
                        if (ul_len < 1) ul_len = 1;
                    } else if (ln == a->span.line) {
                        ul_start = a->span.col;
                        ul_len = copylen - ul_start + 1;
                        if (ul_len < 1) ul_len = 1;
                    } else if (ln == a->span.end_line) {
                        ul_start = 1;
                        ul_len = a->span.end_col;
                        if (ul_len < 1) ul_len = 1;
                    } else {
                        continue;
                    }

                    const char *ul_color = a->is_primary ?
                        DIAG_COLOR(BOLD_RED) : DIAG_COLOR(CYAN);
                    char ul_char = a->is_primary ? '^' : '-';

                    fprintf(out, " %s%*s |%s ", gray, gw, "", rst);
                    for (int c = 1; c < ul_start; c++)
                        fputc(' ', out);
                    fprintf(out, "%s", ul_color);
                    for (int c = 0; c < ul_len; c++)
                        fputc(ul_char, out);

                    if (a->label) {
                        fprintf(out, " %s", a->label);
                    }
                    fprintf(out, "%s\n", rst);

                    /* Pipe connector for secondary labels */
                    if (!a->is_primary && a->label && ln == a->span.line &&
                        a->span.line == a->span.end_line) {
                        /* Show pipe connector for secondary label */
                        fprintf(out, " %s%*s |%s ", gray, gw, "", rst);
                        for (int c = 1; c < ul_start; c++) fputc(' ', out);
                        fprintf(out, "%s|%s\n", ul_color, rst);
                        fprintf(out, " %s%*s |%s ", gray, gw, "", rst);
                        for (int c = 1; c < ul_start; c++) fputc(' ', out);
                        fprintf(out, "%s%s%s\n", ul_color, a->label, rst);
                    }
                }
                last_rendered_line = ln;
            }

            ai = group_end + 1;
        }

        fprintf(out, "\n");
    }

    if (d->n_stack_frames > 0) {
        fprintf(out, " %sstack trace:%s\n", DIAG_COLOR(BLUE), rst);
        for (int i = 0; i < d->n_stack_frames; i++) {
            DiagStackFrame *f = &d->stack_frames[i];
            fprintf(out, "   %s%s%s:%d:%d  in %s%s%s\n",
                    mag, f->file, rst,
                    f->line, f->col,
                    DIAG_COLOR(BOLD),
                    f->func_name ? f->func_name : "<main>",
                    rst);
        }
        fprintf(out, "\n");
    }

    for (int i = 0; i < d->n_hints; i++) {
        fprintf(out, " %shint:%s %s", DIAG_COLOR(BOLD_YEL), DIAG_COLOR(WHITE), "");
        emit_backtick_text(out, d->hints[i], WHITE);
        fprintf(out, "%s\n", rst);
    }

    for (int i = 0; i < d->n_notes; i++) {
        fprintf(out, " %snote:%s %s", DIAG_COLOR(GRAY), DIAG_COLOR(WHITE), "");
        emit_backtick_text(out, d->notes[i], WHITE);
        fprintf(out, "%s\n", rst);
    }

    int n_primary = 0;
    for (int i = 0; i < d->n_annotations; i++)
        if (d->annotations[i].is_primary) n_primary++;

    if (n_primary >= 2) {
        fprintf(out, "\n");
        int idx = 1;
        for (int i = 0; i < d->n_annotations; i++) {
            if (d->annotations[i].is_primary) {
                fprintf(out, " %s(e%d):%s %s%s%s:%d:%d\n",
                        gray, idx, rst,
                        mag, d->annotations[i].span.file, rst,
                        d->annotations[i].span.line, d->annotations[i].span.col);
                idx++;
            }
        }
    }

    if (d->code) {
        int is_repl = 0;
        for (int i = 0; i < d->n_annotations; i++) {
            const char *f = d->annotations[i].span.file;
            if (f && (strcmp(f, "<repl>") == 0 || strcmp(f, "<eval>") == 0)) {
                is_repl = 1;
                break;
            }
        }
        if (!is_repl) {
            fprintf(out, "\n %sfor more info:%s xs --explain %s\n",
                    DIAG_COLOR(BLUE), rst, d->code);
        }
    }

    fprintf(out, "\n");
}

void diag_render_one(Diagnostic *d, const char *source,
                     const char *filename) {
    if (!d) return;
    (void)filename;
    render_diagnostic(d, source, 1, stderr);
}

void diag_render_all(DiagContext *ctx) {
    if (!ctx || ctx->n_items == 0) return;

    int *rendered = (int *)xs_calloc((size_t)ctx->n_items, sizeof(int));

    int errors_shown = 0;
    int warnings_shown = 0;
    int errors_remaining = 0;
    int warnings_remaining = 0;


    /* Errors first (capped at 10), then warnings (capped at 4) */
    for (int i = 0; i < ctx->n_items; i++) {
        if (rendered[i] || ctx->items[i].severity != DIAG_ERROR) continue;

        Diagnostic *d = &ctx->items[i];
        int group_id = d->group_id;

        if (errors_shown >= 10) {
            errors_remaining++;
            rendered[i] = 1;
            if (group_id >= 0) {
                for (int j = i + 1; j < ctx->n_items; j++) {
                    if (!rendered[j] && ctx->items[j].group_id == group_id) {
                        rendered[j] = 1;
                    }
                }
            }
            continue;
        }

        const char *src = NULL;
        if (d->n_annotations > 0) {
            src = ctx_source_for(ctx, d->annotations[0].span.file);
        }

        int n_instances = 1;
        if (group_id >= 0) {
            for (int j = i + 1; j < ctx->n_items; j++) {
                if (rendered[j] || ctx->items[j].group_id != group_id) continue;
                rendered[j] = 1;
                n_instances++;

                Diagnostic *other = &ctx->items[j];
                for (int k = 0; k < other->n_annotations; k++) {
                    if (d->n_annotations >= d->cap_annotations) {
                        int nc = d->cap_annotations ? d->cap_annotations * 2 : 8;
                        d->annotations = (DiagAnnotation *)xs_realloc(
                            d->annotations, (size_t)nc * sizeof(DiagAnnotation));
                        d->cap_annotations = nc;
                    }
                    DiagAnnotation *a = &d->annotations[d->n_annotations++];
                    a->span.file = xs_strdup(other->annotations[k].span.file);
                    a->span.line = other->annotations[k].span.line;
                    a->span.col = other->annotations[k].span.col;
                    a->span.end_line = other->annotations[k].span.end_line;
                    a->span.end_col = other->annotations[k].span.end_col;
                    a->label = other->annotations[k].label ? xs_strdup(other->annotations[k].label) : NULL;
                    a->is_primary = other->annotations[k].is_primary;
                }
                for (int k = 0; k < other->n_hints; k++) {
                    int dup = 0;
                    for (int h = 0; h < d->n_hints; h++) {
                        if (strcmp(d->hints[h], other->hints[k]) == 0) { dup = 1; break; }
                    }
                    if (!dup) {
                        d->hints = (char **)xs_realloc(d->hints, (size_t)(d->n_hints + 1) * sizeof(char *));
                        d->hints[d->n_hints++] = xs_strdup(other->hints[k]);
                    }
                }
                for (int k = 0; k < other->n_notes; k++) {
                    int dup = 0;
                    for (int h = 0; h < d->n_notes; h++) {
                        if (strcmp(d->notes[h], other->notes[k]) == 0) { dup = 1; break; }
                    }
                    if (!dup) {
                        d->notes = (char **)xs_realloc(d->notes, (size_t)(d->n_notes + 1) * sizeof(char *));
                        d->notes[d->n_notes++] = xs_strdup(other->notes[k]);
                    }
                }
            }
        }

        rendered[i] = 1;
        render_diagnostic(d, src, n_instances, stderr);
        errors_shown++;
    }

    if (errors_remaining > 0) {
        fprintf(stderr, "%s... and %d more error%s (fix the above first)%s\n\n",
                DIAG_COLOR(BOLD_RED),
                errors_remaining,
                errors_remaining == 1 ? "" : "s",
                DIAG_COLOR(RST));
    }

    for (int i = 0; i < ctx->n_items; i++) {
        if (rendered[i] || ctx->items[i].severity != DIAG_WARNING) continue;

        Diagnostic *d = &ctx->items[i];
        int group_id = d->group_id;

        if (warnings_shown >= 4) {
            warnings_remaining++;
            rendered[i] = 1;
            if (group_id >= 0) {
                for (int j = i + 1; j < ctx->n_items; j++) {
                    if (!rendered[j] && ctx->items[j].group_id == group_id)
                        rendered[j] = 1;
                }
            }
            continue;
        }

        const char *src = NULL;
        if (d->n_annotations > 0) {
            src = ctx_source_for(ctx, d->annotations[0].span.file);
        }

        int n_instances = 1;
        if (group_id >= 0) {
            for (int j = i + 1; j < ctx->n_items; j++) {
                if (rendered[j] || ctx->items[j].group_id != group_id) continue;
                rendered[j] = 1;
                n_instances++;
            }
        }

        rendered[i] = 1;
        render_diagnostic(d, src, n_instances, stderr);
        warnings_shown++;
    }

    if (warnings_remaining > 0) {
        fprintf(stderr, "%s... and %d more warning%s%s\n\n",
                DIAG_COLOR(BOLD_YEL),
                warnings_remaining,
                warnings_remaining == 1 ? "" : "s",
                DIAG_COLOR(RST));
    }

    free(rendered);
}
