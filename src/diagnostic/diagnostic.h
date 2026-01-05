#ifndef XS_DIAGNOSTIC_H
#define XS_DIAGNOSTIC_H

#include "core/ast.h"

extern int g_no_color;
#define DIAG_COLOR(code) (g_no_color ? "" : (code))

typedef enum { DIAG_ERROR, DIAG_WARNING } DiagSeverity;

typedef enum {
    DIAG_PHASE_LEXER,
    DIAG_PHASE_PARSER,
    DIAG_PHASE_SEMANTIC,
    DIAG_PHASE_RUNTIME
} DiagPhase;

/* Span that owns its file string */
typedef struct {
    char *file;
    int line, col;
    int end_line, end_col;
} DiagSpan;

typedef struct {
    DiagSpan span;
    char *label;
    int is_primary;         /* 1 = ^^^ red, 0 = --- cyan */
} DiagAnnotation;

typedef struct {
    char *file;
    int line, col;
    char *func_name;
} DiagStackFrame;

typedef struct {
    DiagSeverity severity;
    DiagSeverity original_severity;
    DiagPhase phase;
    char *code;             /* e.g. "T0001", NULL for runtime */
    char *message;
    int group_id;           /* -1 = standalone */

    DiagAnnotation *annotations;
    int n_annotations, cap_annotations;

    char **hints;
    int n_hints;

    char **notes;
    int n_notes;

    DiagStackFrame *stack_frames;
    int n_stack_frames;
} Diagnostic;

typedef struct {
    Diagnostic *items;
    int n_items, cap_items;

    char **filenames;
    char **sources;
    int n_sources, cap_sources;

    int lenient;
} DiagContext;

/* Builder */
Diagnostic *diag_new(DiagSeverity sev, DiagPhase phase,
                     const char *code, const char *fmt, ...);
void diag_annotate(Diagnostic *d, Span span, int is_primary,
                   const char *fmt, ...);
void diag_hint(Diagnostic *d, const char *fmt, ...);
void diag_note(Diagnostic *d, const char *fmt, ...);
void diag_push_frame(Diagnostic *d, const char *file, int line,
                     int col, const char *func_name);
void diag_set_group(Diagnostic *d, int group_id);
void diag_free(Diagnostic *d);

/* Moves ownership into ctx; macro nulls caller's pointer */
void diag_emit_(DiagContext *ctx, Diagnostic *d);
#define diag_emit(ctx, d) (diag_emit_((ctx), (d)), (d) = NULL)

/* Context */
DiagContext *diag_context_new(void);
void diag_context_add_source(DiagContext *ctx, const char *filename,
                             const char *source);
void diag_context_free(DiagContext *ctx);
void diag_context_set_lenient(DiagContext *ctx, int lenient);
int  diag_context_error_count(DiagContext *ctx);
int  diag_context_warning_count(DiagContext *ctx);
int  diag_context_downgraded_count(DiagContext *ctx);

void diag_render_one(Diagnostic *d, const char *source,
                     const char *filename);
void diag_render_all(DiagContext *ctx);

int diag_explain(const char *code);

#endif
