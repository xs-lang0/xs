#include "diagnostic/diagnostic.h"
#include "core/xs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static char *diag_vasprintf(const char *fmt, va_list ap) {
    char buf[256];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n < 0) { va_end(ap2); return xs_strdup("(format error)"); }
    if (n < (int)sizeof buf) { va_end(ap2); return xs_strdup(buf); }
    char *heap = (char *)xs_malloc((size_t)n + 1);
    vsnprintf(heap, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return heap;
}

static DiagSpan diag_span_from_span(Span s) {
    DiagSpan ds;
    ds.file = xs_strdup(s.file ? s.file : "<unknown>");
    ds.line = s.line;
    ds.col = s.col;
    ds.end_line = s.end_line;
    ds.end_col = s.end_col;
    return ds;
}


Diagnostic *diag_new(DiagSeverity sev, DiagPhase phase,
                     const char *code, const char *fmt, ...) {
    Diagnostic *d = (Diagnostic *)xs_calloc(1, sizeof(Diagnostic));
    d->severity = sev;
    d->original_severity = sev;
    d->phase = phase;
    d->code = code ? xs_strdup(code) : NULL;
    d->group_id = -1;

    va_list ap;
    va_start(ap, fmt);
    d->message = diag_vasprintf(fmt, ap);
    va_end(ap);

    return d;
}

void diag_annotate(Diagnostic *d, Span span, int is_primary,
                   const char *fmt, ...) {
    if (!d) return;
    if (d->n_annotations >= d->cap_annotations) {
        int newcap = d->cap_annotations ? d->cap_annotations * 2 : 4;
        d->annotations = (DiagAnnotation *)xs_realloc(
            d->annotations, (size_t)newcap * sizeof(DiagAnnotation));
        d->cap_annotations = newcap;
    }
    DiagAnnotation *a = &d->annotations[d->n_annotations++];
    a->span = diag_span_from_span(span);
    a->is_primary = is_primary;
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        a->label = diag_vasprintf(fmt, ap);
        va_end(ap);
    } else {
        a->label = NULL;
    }
}

void diag_hint(Diagnostic *d, const char *fmt, ...) {
    if (!d) return;
    d->hints = (char **)xs_realloc(d->hints, (size_t)(d->n_hints + 1) * sizeof(char *));
    va_list ap;
    va_start(ap, fmt);
    d->hints[d->n_hints++] = diag_vasprintf(fmt, ap);
    va_end(ap);
}

void diag_note(Diagnostic *d, const char *fmt, ...) {
    if (!d) return;
    d->notes = (char **)xs_realloc(d->notes, (size_t)(d->n_notes + 1) * sizeof(char *));
    va_list ap;
    va_start(ap, fmt);
    d->notes[d->n_notes++] = diag_vasprintf(fmt, ap);
    va_end(ap);
}

void diag_push_frame(Diagnostic *d, const char *file, int line,
                     int col, const char *func_name) {
    if (!d) return;
    d->stack_frames = (DiagStackFrame *)xs_realloc(
        d->stack_frames, (size_t)(d->n_stack_frames + 1) * sizeof(DiagStackFrame));
    DiagStackFrame *f = &d->stack_frames[d->n_stack_frames++];
    f->file = xs_strdup(file ? file : "<unknown>");
    f->line = line;
    f->col = col;
    f->func_name = func_name ? xs_strdup(func_name) : NULL;
}

void diag_set_group(Diagnostic *d, int group_id) {
    if (d) d->group_id = group_id;
}

void diag_free(Diagnostic *d) {
    if (!d) return;
    free(d->code);
    free(d->message);
    for (int i = 0; i < d->n_annotations; i++) {
        free(d->annotations[i].span.file);
        free(d->annotations[i].label);
    }
    free(d->annotations);
    for (int i = 0; i < d->n_hints; i++)
        free(d->hints[i]);
    free(d->hints);
    for (int i = 0; i < d->n_notes; i++)
        free(d->notes[i]);
    free(d->notes);
    for (int i = 0; i < d->n_stack_frames; i++) {
        free(d->stack_frames[i].file);
        free(d->stack_frames[i].func_name);
    }
    free(d->stack_frames);
    free(d);
}

/* Emit: move ownership into context */
void diag_emit_(DiagContext *ctx, Diagnostic *d) {
    if (!ctx || !d) return;

    if (ctx->lenient && d->severity == DIAG_ERROR) {
        d->original_severity = d->severity;
        d->severity = DIAG_WARNING;
    }

    if (ctx->n_items >= ctx->cap_items) {
        int newcap = ctx->cap_items ? ctx->cap_items * 2 : 8;
        ctx->items = (Diagnostic *)xs_realloc(
            ctx->items, (size_t)newcap * sizeof(Diagnostic));
        ctx->cap_items = newcap;
    }
    ctx->items[ctx->n_items++] = *d;
    free(d); /* shell only; owned data lives in ctx->items now */
}

DiagContext *diag_context_new(void) {
    return (DiagContext *)xs_calloc(1, sizeof(DiagContext));
}

void diag_context_add_source(DiagContext *ctx, const char *filename,
                             const char *source) {
    if (!ctx) return;
    if (ctx->n_sources >= ctx->cap_sources) {
        int newcap = ctx->cap_sources ? ctx->cap_sources * 2 : 4;
        ctx->filenames = (char **)xs_realloc(
            ctx->filenames, (size_t)newcap * sizeof(char *));
        ctx->sources = (char **)xs_realloc(
            ctx->sources, (size_t)newcap * sizeof(char *));
        ctx->cap_sources = newcap;
    }
    ctx->filenames[ctx->n_sources] = xs_strdup(filename ? filename : "<unknown>");
    ctx->sources[ctx->n_sources] = xs_strdup(source ? source : "");
    ctx->n_sources++;
}

void diag_context_free(DiagContext *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->n_items; i++) {
        Diagnostic *d = &ctx->items[i];
        free(d->code);
        free(d->message);
        for (int j = 0; j < d->n_annotations; j++) {
            free(d->annotations[j].span.file);
            free(d->annotations[j].label);
        }
        free(d->annotations);
        for (int j = 0; j < d->n_hints; j++)
            free(d->hints[j]);
        free(d->hints);
        for (int j = 0; j < d->n_notes; j++)
            free(d->notes[j]);
        free(d->notes);
        for (int j = 0; j < d->n_stack_frames; j++) {
            free(d->stack_frames[j].file);
            free(d->stack_frames[j].func_name);
        }
        free(d->stack_frames);
    }
    free(ctx->items);
    for (int i = 0; i < ctx->n_sources; i++) {
        free(ctx->filenames[i]);
        free(ctx->sources[i]);
    }
    free(ctx->filenames);
    free(ctx->sources);
    free(ctx);
}

void diag_context_set_lenient(DiagContext *ctx, int lenient) {
    if (ctx) ctx->lenient = lenient;
}

int diag_context_error_count(DiagContext *ctx) {
    if (!ctx) return 0;
    int count = 0;
    for (int i = 0; i < ctx->n_items; i++)
        if (ctx->items[i].severity == DIAG_ERROR) count++;
    return count;
}

int diag_context_warning_count(DiagContext *ctx) {
    if (!ctx) return 0;
    int count = 0;
    for (int i = 0; i < ctx->n_items; i++)
        if (ctx->items[i].severity == DIAG_WARNING) count++;
    return count;
}

int diag_context_downgraded_count(DiagContext *ctx) {
    if (!ctx) return 0;
    int count = 0;
    for (int i = 0; i < ctx->n_items; i++)
        if (ctx->items[i].severity == DIAG_WARNING &&
            ctx->items[i].original_severity == DIAG_ERROR) count++;
    return count;
}
