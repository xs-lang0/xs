#define _POSIX_C_SOURCE 200809L
#include "tracer/tracer.h"
#include "core/xs.h"
#ifdef XSC_ENABLE_VM
#include "vm/bytecode.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define MAX_EVENTS 1048576  /* 1M events */

typedef enum {
    TRACE_CALL   = 0,
    TRACE_RETURN = 1,
    TRACE_STORE  = 2,
    TRACE_IO     = 3,
    TRACE_BRANCH = 4
} TraceEventKind;

typedef struct {
    TraceEventKind kind;
    int64_t timestamp;  /* nanoseconds since start */
    union {
        struct { const char *fn; int line; } call;
        struct { const char *fn; int64_t ival; double fval; const char *sval; int tag; char *deep_json; } ret;
        struct { const char *var; int64_t ival; double fval; const char *sval; int tag; char *deep_json; } store;
        struct { const char *op; char *data; int len; } io;
        struct { int line; int taken; } branch;
    };
} TraceEvent;

struct XSTracer {
    FILE *out;
    char *output_path;
    TraceEvent *events;
    int n_events;
    int capacity;
    int deep_serialize;
    struct timespec start_time;
};

static int64_t elapsed_ns(struct XSTracer *t) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t sec_diff  = (int64_t)(now.tv_sec  - t->start_time.tv_sec);
    int64_t nsec_diff = (int64_t)(now.tv_nsec - t->start_time.tv_nsec);
    return sec_diff * 1000000000LL + nsec_diff;
}

static void ensure_capacity(XSTracer *t) {
    if (t->n_events >= t->capacity) {
        if (t->capacity >= MAX_EVENTS) return; /* TODO: silently drops events, should at least warn once */
        t->capacity = t->capacity * 2;
        if (t->capacity > MAX_EVENTS) t->capacity = MAX_EVENTS;
        void *tmp = realloc(t->events, (size_t)t->capacity * sizeof(TraceEvent));
        if (!tmp) {
            fprintf(stderr, "xs tracer: out of memory\n");
            return;
        }
        t->events = tmp;
    }
}

void tracer_set_deep(XSTracer *t, int deep) {
    if (t) t->deep_serialize = deep;
}


static void json_append(char **buf, int *pos, int *cap, const char *src) {
    int slen = (int)strlen(src);
    while (*pos + slen + 1 > *cap) {
        *cap = (*cap < 64) ? 128 : *cap * 2;
        char *tmp = realloc(*buf, (size_t)*cap);
        if (!tmp) return;
        *buf = tmp;
    }
    memcpy(*buf + *pos, src, (size_t)slen);
    *pos += slen;
    (*buf)[*pos] = '\0';
}

static char *value_to_json(Value *v, int max_depth) {
    if (!v) return strdup("null");
    if (max_depth <= 0) return strdup("\"<max depth>\"");

    char tmp[128];
    char *buf = NULL;
    int pos = 0, cap = 0;

    switch (v->tag) {
    case XS_NULL:
        return strdup("null");
    case XS_BOOL:
        return strdup(v->i ? "true" : "false");
    case XS_INT:
        snprintf(tmp, sizeof(tmp), "%lld", (long long)v->i);
        return strdup(tmp);
    case XS_FLOAT:
        snprintf(tmp, sizeof(tmp), "%.17g", v->f);
        return strdup(tmp);
    case XS_STR:
    case XS_CHAR: {
        const char *s = v->s ? v->s : "";
        cap = (int)strlen(s) * 2 + 8;
        buf = malloc((size_t)cap);
        pos = 0;
        buf[pos++] = '"';
        for (const char *p = s; *p; p++) {
            if (pos + 8 > cap) { cap *= 2; char *tmp = realloc(buf, (size_t)cap); if (!tmp) { free(buf); return strdup("\"<oom>\""); } buf = tmp; }
            switch (*p) {
            case '"':  buf[pos++] = '\\'; buf[pos++] = '"'; break;
            case '\\': buf[pos++] = '\\'; buf[pos++] = '\\'; break;
            case '\n': buf[pos++] = '\\'; buf[pos++] = 'n'; break;
            case '\t': buf[pos++] = '\\'; buf[pos++] = 't'; break;
            case '\r': buf[pos++] = '\\'; buf[pos++] = 'r'; break;
            default:   buf[pos++] = *p; break;
            }
        }
        buf[pos++] = '"';
        buf[pos] = '\0';
        return buf;
    }
    case XS_ARRAY:
    case XS_TUPLE: {
        cap = 64;
        buf = malloc((size_t)cap);
        pos = 0;
        json_append(&buf, &pos, &cap, "[");
        if (v->arr) {
            for (int i = 0; i < v->arr->len; i++) {
                if (i > 0) json_append(&buf, &pos, &cap, ", ");
                char *elem = value_to_json(v->arr->items[i], max_depth - 1);
                json_append(&buf, &pos, &cap, elem);
                free(elem);
            }
        }
        json_append(&buf, &pos, &cap, "]");
        return buf;
    }
    case XS_MAP:
    case XS_MODULE: {
        cap = 64;
        buf = malloc((size_t)cap);
        pos = 0;
        json_append(&buf, &pos, &cap, "{");
        if (v->map) {
            int first = 1;
            for (int i = 0; i < v->map->cap; i++) {
                if (!v->map->keys[i]) continue;
                if (!first) json_append(&buf, &pos, &cap, ", ");
                first = 0;
                json_append(&buf, &pos, &cap, "\"");
                json_append(&buf, &pos, &cap, v->map->keys[i]);
                json_append(&buf, &pos, &cap, "\": ");
                char *val = value_to_json(v->map->vals[i], max_depth - 1);
                json_append(&buf, &pos, &cap, val);
                free(val);
            }
        }
        json_append(&buf, &pos, &cap, "}");
        return buf;
    }
    case XS_FUNC: {
        const char *name = (v->fn && v->fn->name) ? v->fn->name : "<lambda>";
        int np = v->fn ? v->fn->nparams : 0;
        snprintf(tmp, sizeof(tmp),
                 "{\"type\": \"func\", \"name\": \"%s\", \"params\": %d}",
                 name, np);
        return strdup(tmp);
    }
    case XS_RANGE: {
        if (v->range) {
            snprintf(tmp, sizeof(tmp),
                     "{\"type\": \"range\", \"start\": %lld, \"end\": %lld, \"step\": %lld}",
                     (long long)v->range->start,
                     (long long)v->range->end,
                     (long long)v->range->step);
        } else {
            snprintf(tmp, sizeof(tmp), "{\"type\": \"range\"}");
        }
        return strdup(tmp);
    }
#ifdef XSC_ENABLE_VM
    case XS_CLOSURE: {
        const char *cname = (v->cl && v->cl->proto && v->cl->proto->name)
                            ? v->cl->proto->name : "<closure>";
        int nup = (v->cl && v->cl->proto) ? v->cl->proto->n_upvalues : 0;
        snprintf(tmp, sizeof(tmp),
                 "{\"type\": \"closure\", \"name\": \"%s\", \"upvalues\": %d}",
                 cname, nup);
        return strdup(tmp);
    }
#endif
    case XS_ACTOR:
        return strdup("{\"type\": \"actor\"}");
    default:
        snprintf(tmp, sizeof(tmp), "{\"type\": \"tag_%d\"}", (int)v->tag);
        return strdup(tmp);
    }
}

static void write_string(FILE *f, const char *s) {
    uint32_t len = s ? (uint32_t)strlen(s) : 0;
    fwrite(&len, 4, 1, f);
    if (len > 0) fwrite(s, 1, len, f);
}

XSTracer *tracer_new(const char *output_path) {
    XSTracer *t = calloc(1, sizeof(XSTracer));
    if (!t) return NULL;

    t->output_path = strdup(output_path ? output_path : "trace.xst");
    t->out = fopen(t->output_path, "wb");
    if (!t->out) {
        fprintf(stderr, "xs tracer: cannot open '%s' for writing\n", t->output_path);
        free(t->output_path);
        free(t);
        return NULL;
    }

    t->capacity = 4096;
    t->events = calloc((size_t)t->capacity, sizeof(TraceEvent));
    if (!t->events) {
        fclose(t->out);
        free(t->output_path);
        free(t);
        return NULL;
    }
    t->n_events = 0;

    clock_gettime(CLOCK_MONOTONIC, &t->start_time);
    return t;
}

void tracer_free(XSTracer *t) {
    if (!t) return;
    tracer_flush(t);
    if (t->out) fclose(t->out);
    free(t->events);
    free(t->output_path);
    free(t);
}

void tracer_record_call(XSTracer *t, const char *fn, int line) {
    if (!t || t->n_events >= MAX_EVENTS) return;
    ensure_capacity(t);
    if (t->n_events >= t->capacity) return;

    TraceEvent *e = &t->events[t->n_events++];
    e->kind = TRACE_CALL;
    e->timestamp = elapsed_ns(t);
    e->call.fn = fn;
    e->call.line = line;
}

static void extract_value(Value *v, int *tag, int64_t *ival, double *fval, const char **sval) {
    *ival = 0; *fval = 0.0; *sval = NULL;
    if (!v) { *tag = XS_NULL; return; }
    *tag = (int)v->tag;
    switch (v->tag) {
    case XS_INT:   *ival = v->i;  break;
    case XS_BOOL:  *ival = v->i;  break;
    case XS_FLOAT: *fval = v->f;  break;
    case XS_STR:
    case XS_CHAR:  *sval = v->s;  break;
    case XS_ARRAY:
    case XS_TUPLE:
        if (v->arr) *ival = (int64_t)v->arr->len;
        break;
    case XS_MAP:
    case XS_MODULE:
        if (v->map) *ival = (int64_t)v->map->len;
        break;
    default:       break;
    }
}

void tracer_record_return(XSTracer *t, const char *fn, void *retval) {
    if (!t || t->n_events >= MAX_EVENTS) return;
    ensure_capacity(t);
    if (t->n_events >= t->capacity) return;

    TraceEvent *e = &t->events[t->n_events++];
    e->kind = TRACE_RETURN;
    e->timestamp = elapsed_ns(t);
    e->ret.fn = fn;
    e->ret.deep_json = NULL;
    extract_value((Value *)retval, &e->ret.tag, &e->ret.ival, &e->ret.fval, &e->ret.sval);
    if (t->deep_serialize && retval) {
        e->ret.deep_json = value_to_json((Value *)retval, 3);
    }
}

void tracer_record_store(XSTracer *t, const char *var, void *val) {
    if (!t || t->n_events >= MAX_EVENTS) return;
    ensure_capacity(t);
    if (t->n_events >= t->capacity) return;

    TraceEvent *e = &t->events[t->n_events++];
    e->kind = TRACE_STORE;
    e->timestamp = elapsed_ns(t);
    e->store.var = var;
    e->store.deep_json = NULL;
    extract_value((Value *)val, &e->store.tag, &e->store.ival, &e->store.fval, &e->store.sval);
    if (t->deep_serialize && val) {
        e->store.deep_json = value_to_json((Value *)val, 3);
    }
}

void tracer_record_io(XSTracer *t, const char *op, void *data, int len) {
    if (!t || t->n_events >= MAX_EVENTS) return;
    ensure_capacity(t);
    if (t->n_events >= t->capacity) return;

    TraceEvent *e = &t->events[t->n_events++];
    e->kind = TRACE_IO;
    e->timestamp = elapsed_ns(t);
    e->io.op = op;
    e->io.len = len;
    if (data && len > 0) {
        e->io.data = malloc((size_t)len);
        if (e->io.data) {
            memcpy(e->io.data, data, (size_t)len);
        } else {
            e->io.len = 0;
        }
    } else {
        e->io.data = NULL;
    }
}

void tracer_record_branch(XSTracer *t, int line, int taken) {
    if (!t || t->n_events >= MAX_EVENTS) return;
    ensure_capacity(t);
    if (t->n_events >= t->capacity) return;

    TraceEvent *e = &t->events[t->n_events++];
    e->kind = TRACE_BRANCH;
    e->timestamp = elapsed_ns(t);
    e->branch.line = line;
    e->branch.taken = taken;
}

int tracer_flush(XSTracer *t) {
    if (!t || !t->out || t->n_events == 0) return 0;

    fwrite("XST1", 1, 4, t->out);
    uint32_t version = 1;
    fwrite(&version, 4, 1, t->out);
    uint32_t count = (uint32_t)t->n_events;
    fwrite(&count, 4, 1, t->out);

    for (int i = 0; i < t->n_events; i++) {
        TraceEvent *e = &t->events[i];
        uint8_t kind = (uint8_t)e->kind;
        fwrite(&kind, 1, 1, t->out);
        fwrite(&e->timestamp, 8, 1, t->out);

        switch (e->kind) {
        case TRACE_CALL:
            write_string(t->out, e->call.fn);
            fwrite(&e->call.line, 4, 1, t->out);
            break;
        case TRACE_RETURN: {
            write_string(t->out, e->ret.fn);
            uint8_t rtag = (uint8_t)e->ret.tag;
            fwrite(&rtag, 1, 1, t->out);
            fwrite(&e->ret.ival, 8, 1, t->out);
            fwrite(&e->ret.fval, 8, 1, t->out);
            write_string(t->out, e->ret.sval);
            write_string(t->out, e->ret.deep_json);
            break;
        }
        case TRACE_STORE: {
            write_string(t->out, e->store.var);
            uint8_t tag = (uint8_t)e->store.tag;
            fwrite(&tag, 1, 1, t->out);
            fwrite(&e->store.ival, 8, 1, t->out);
            fwrite(&e->store.fval, 8, 1, t->out);
            write_string(t->out, e->store.sval);
            write_string(t->out, e->store.deep_json);
            break;
        }
        case TRACE_IO:
            write_string(t->out, e->io.op);
            fwrite(&e->io.len, 4, 1, t->out);
            if (e->io.data && e->io.len > 0) {
                fwrite(e->io.data, 1, (size_t)e->io.len, t->out);
            }
            break;
        case TRACE_BRANCH:
            fwrite(&e->branch.line, 4, 1, t->out);
            fwrite(&e->branch.taken, 4, 1, t->out);
            break;
        }
    }

    fflush(t->out);

    for (int i = 0; i < t->n_events; i++) {
        TraceEvent *ev = &t->events[i];
        if (ev->kind == TRACE_IO) {
            free(ev->io.data);
            ev->io.data = NULL;
        } else if (ev->kind == TRACE_RETURN) {
            free(ev->ret.deep_json);
            ev->ret.deep_json = NULL;
        } else if (ev->kind == TRACE_STORE) {
            free(ev->store.deep_json);
            ev->store.deep_json = NULL;
        }
    }

    t->n_events = 0;
    return 0;
}
