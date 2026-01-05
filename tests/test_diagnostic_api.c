/* test_diagnostic_api.c -- unit tests for the diagnostic engine */
#include "diagnostic/diagnostic.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int g_no_color = 1; /* suppress colors in tests */

int main(void) {
    /* Test diag_new */
    Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0001", "mismatched types");
    assert(d != NULL);
    assert(d->severity == DIAG_ERROR);
    assert(d->original_severity == DIAG_ERROR);
    assert(strcmp(d->code, "T0001") == 0);
    assert(strcmp(d->message, "mismatched types") == 0);
    assert(d->group_id == -1);
    assert(d->n_annotations == 0);
    assert(d->n_hints == 0);
    assert(d->n_notes == 0);
    assert(d->n_stack_frames == 0);

    /* Test diag_new with format string */
    Diagnostic *d_fmt = diag_new(DIAG_ERROR, DIAG_PHASE_PARSER, "P0001",
                                  "unexpected token '%s'", "foo");
    assert(strcmp(d_fmt->message, "unexpected token 'foo'") == 0);
    diag_free(d_fmt);

    /* Test diag_new with NULL code (runtime) */
    Diagnostic *d_rt = diag_new(DIAG_ERROR, DIAG_PHASE_RUNTIME, NULL, "index out of bounds");
    assert(d_rt->code == NULL);
    assert(d_rt->phase == DIAG_PHASE_RUNTIME);
    diag_free(d_rt);

    /* Test diag_annotate */
    Span s = { .file = "test.xs", .line = 5, .col = 10, .end_line = 5, .end_col = 15 };
    diag_annotate(d, s, 1, "expected %s, found %s", "i32", "str");
    assert(d->n_annotations == 1);
    assert(d->annotations[0].is_primary == 1);
    assert(strcmp(d->annotations[0].span.file, "test.xs") == 0);
    assert(d->annotations[0].span.line == 5);
    assert(d->annotations[0].span.col == 10);
    assert(d->annotations[0].span.end_line == 5);
    assert(d->annotations[0].span.end_col == 15);
    assert(strcmp(d->annotations[0].label, "expected i32, found str") == 0);
    /* DiagSpan.file is an owned copy, not the same pointer */
    assert(d->annotations[0].span.file != s.file);

    /* Test secondary annotation */
    Span s2 = { .file = "test.xs", .line = 3, .col = 5, .end_line = 3, .end_col = 8 };
    diag_annotate(d, s2, 0, "declared i32 here");
    assert(d->n_annotations == 2);
    assert(d->annotations[1].is_primary == 0);

    /* Test annotation with NULL label */
    Span s3 = { .file = "test.xs", .line = 7, .col = 1, .end_line = 7, .end_col = 5 };
    diag_annotate(d, s3, 1, NULL);
    assert(d->n_annotations == 3);
    assert(d->annotations[2].label == NULL);

    /* Test hints */
    diag_hint(d, "try `msg.to_int()`");
    assert(d->n_hints == 1);
    assert(strcmp(d->hints[0], "try `msg.to_int()`") == 0);

    diag_hint(d, "another hint with %d values", 3);
    assert(d->n_hints == 2);
    assert(strcmp(d->hints[1], "another hint with 3 values") == 0);

    /* Test notes */
    diag_note(d, "declared on line 3");
    assert(d->n_notes == 1);
    assert(strcmp(d->notes[0], "declared on line 3") == 0);

    /* Test stack frames */
    diag_push_frame(d, "test.xs", 15, 12, "get_item");
    diag_push_frame(d, "test.xs", 28, 5, "process");
    diag_push_frame(d, "test.xs", 35, 1, NULL);
    assert(d->n_stack_frames == 3);
    assert(strcmp(d->stack_frames[0].func_name, "get_item") == 0);
    assert(d->stack_frames[2].func_name == NULL);

    /* Test group_id */
    diag_set_group(d, 42);
    assert(d->group_id == 42);

    /* Free the diagnostic */
    diag_free(d);

    /* Test context lifecycle */
    DiagContext *ctx = diag_context_new();
    assert(ctx != NULL);
    assert(ctx->n_items == 0);
    assert(ctx->n_sources == 0);

    /* Test add source */
    diag_context_add_source(ctx, "main.xs", "let x = 42\nlet y = 10\n");
    assert(ctx->n_sources == 1);
    assert(strcmp(ctx->filenames[0], "main.xs") == 0);

    /* Test counts - initially zero */
    assert(diag_context_error_count(ctx) == 0);
    assert(diag_context_warning_count(ctx) == 0);
    assert(diag_context_downgraded_count(ctx) == 0);

    /* Emit an error */
    Diagnostic *d2 = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0002", "undefined name");
    diag_emit(ctx, d2);
    assert(d2 == NULL);  /* macro nulled it */
    assert(diag_context_error_count(ctx) == 1);
    assert(diag_context_warning_count(ctx) == 0);

    /* Emit a warning */
    Diagnostic *d_warn = diag_new(DIAG_WARNING, DIAG_PHASE_SEMANTIC, "T0003", "unused var");
    diag_emit(ctx, d_warn);
    assert(diag_context_error_count(ctx) == 1);
    assert(diag_context_warning_count(ctx) == 1);

    /* Test lenient mode */
    diag_context_set_lenient(ctx, 1);
    Diagnostic *d3 = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0003", "unused var");
    diag_emit(ctx, d3);
    assert(diag_context_error_count(ctx) == 1);    /* still 1 - d3 downgraded */
    assert(diag_context_warning_count(ctx) == 2);   /* d_warn + d3 now a warning */
    assert(diag_context_downgraded_count(ctx) == 1);

    /* Emit another error in lenient mode */
    Diagnostic *d4 = diag_new(DIAG_ERROR, DIAG_PHASE_SEMANTIC, "T0004", "mutability violation");
    diag_emit(ctx, d4);
    assert(diag_context_error_count(ctx) == 1);
    assert(diag_context_warning_count(ctx) == 3);
    assert(diag_context_downgraded_count(ctx) == 2);

    /* Test render_all doesn't crash (just verifying no segfault) */
    diag_render_all(ctx);

    diag_context_free(ctx);

    /* Test render_one doesn't crash */
    Diagnostic *d5 = diag_new(DIAG_ERROR, DIAG_PHASE_LEXER, "L0001",
                               "unterminated string literal");
    Span s5 = { .file = "test.xs", .line = 7, .col = 12, .end_line = 7, .end_col = 13 };
    diag_annotate(d5, s5, 1, "string starts here but never closes");
    diag_hint(d5, "add a closing `\"`");
    diag_render_one(d5, "let x = 42\nlet y = 10\nlet z = 20\n"
                         "let a = 1\nlet b = 2\nlet c = 3\n"
                         "let name = \"hello\n", "test.xs");
    diag_free(d5);

    /* Test empty context */
    DiagContext *empty = diag_context_new();
    diag_render_all(empty);  /* should be no-op */
    assert(diag_context_error_count(empty) == 0);
    diag_context_free(empty);

    /* Test NULL handling */
    diag_free(NULL);  /* should not crash */
    diag_annotate(NULL, s, 1, "test");  /* should not crash */
    diag_hint(NULL, "test");  /* should not crash */
    diag_note(NULL, "test");  /* should not crash */
    diag_push_frame(NULL, "f", 1, 1, "fn");  /* should not crash */
    diag_set_group(NULL, 1);  /* should not crash */

    printf("All diagnostic API tests passed!\n");
    return 0;
}
