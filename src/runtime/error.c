#include "runtime/error.h"
#include "runtime/interp.h"
#include "diagnostic/diagnostic.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static const char *g_current_source = NULL;

void xs_error_set_source(const char *source) {
    g_current_source = source;
}

extern Interp *g_current_interp;

Value *xs_error_new(const char *kind, const char *message, Value *cause) {
    Value *m = xs_map_new();
    Value *k   = xs_str(kind    ? kind    : "Error");
    Value *msg = xs_str(message ? message : "");
    map_set(m->map, "kind",    k);   value_decref(k);
    map_set(m->map, "message", msg); value_decref(msg);
    if (cause) map_set(m->map, "cause", cause);
    return m;
}

Value *xs_error_from_str(const char *message) {
    return xs_error_new("Error", message, NULL);
}

const char *xs_error_kind(Value *err) {
    if (!err || err->tag != XS_MAP) return "Error";
    Value *v = map_get(err->map, "kind");
    return (v && v->tag == XS_STR) ? v->s : "Error";
}

const char *xs_error_message(Value *err) {
    if (!err || err->tag != XS_MAP) return "";
    Value *v = map_get(err->map, "message");
    return (v && v->tag == XS_STR) ? v->s : "";
}

Value *xs_error_cause(Value *err) {
    if (!err || err->tag != XS_MAP) return NULL;
    return map_get(err->map, "cause");
}

/* runtime error reporting */

void xs_runtime_error(Span span, const char *label, const char *hint,
                      const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    Diagnostic *d = diag_new(DIAG_ERROR, DIAG_PHASE_RUNTIME, NULL, "%s", buf);
    diag_annotate(d, span, 1, "%s", label ? label : buf);
    if (hint) diag_hint(d, "%s", hint);

    if (g_current_interp && g_current_interp->call_stack_len > 0) {
        int n = g_current_interp->call_stack_len;
        #define MAX_FRAMES 10
        if (n <= MAX_FRAMES) {
            for (int j = n - 1; j >= 0; j--) {
                InterpFrame *f = &g_current_interp->call_stack[j];
                diag_push_frame(d,
                    f->call_span.file ? f->call_span.file : "<unknown>",
                    f->call_span.line, f->call_span.col, f->func_name);
            }
        } else {
            /* show top 5, elide middle, show bottom 5 */
            for (int j = n - 1; j >= n - 5; j--) {
                InterpFrame *f = &g_current_interp->call_stack[j];
                diag_push_frame(d,
                    f->call_span.file ? f->call_span.file : "<unknown>",
                    f->call_span.line, f->call_span.col, f->func_name);
            }
            char elide[64];
            snprintf(elide, sizeof elide, "... %d frames omitted ...", n - MAX_FRAMES);
            diag_push_frame(d, "", 0, 0, elide);
            for (int j = 4; j >= 0; j--) {
                InterpFrame *f = &g_current_interp->call_stack[j];
                diag_push_frame(d,
                    f->call_span.file ? f->call_span.file : "<unknown>",
                    f->call_span.line, f->call_span.col, f->func_name);
            }
        }
        #undef MAX_FRAMES
    }

    diag_render_one(d, g_current_source, span.file);
    diag_free(d);
}
