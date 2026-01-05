#define _POSIX_C_SOURCE 200809L
#include "tracer/replay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef enum {
    TRACE_CALL   = 0,
    TRACE_RETURN = 1,
    TRACE_STORE  = 2,
    TRACE_IO     = 3,
    TRACE_BRANCH = 4
} TraceEventKind;

typedef struct {
    TraceEventKind kind;
    int64_t timestamp;
    union {
        struct { char *fn; int line; } call;
        struct { char *fn; char *deep_json; } ret;
        struct { char *var; int64_t ival; double fval; char *sval; int tag; char *deep_json; } store;
        struct { char *op; int len; } io;
        struct { int line; int taken; } branch;
    };
} ReplayEvent;

struct XSReplay {
    char *trace_path;
    ReplayEvent *events;
    int n_events;
    int position;
};

static char *read_string(FILE *f) {
    uint32_t len = 0;
    if (fread(&len, 4, 1, f) != 1) return NULL;
    if (len == 0) return NULL;
    char *s = malloc((size_t)len + 1);
    if (!s) return NULL;
    if (fread(s, 1, len, f) != len) { free(s); return NULL; }
    s[len] = '\0';
    return s;
}

static void print_event(int index, ReplayEvent *e) {
    printf("[%d] ", index);
    switch (e->kind) {
    case TRACE_CALL:
        printf("CALL %s line %d", e->call.fn ? e->call.fn : "?", e->call.line);
        break;
    case TRACE_RETURN:
        printf("RETURN %s", e->ret.fn ? e->ret.fn : "?");
        if (e->ret.deep_json) printf(" => %s", e->ret.deep_json);
        break;
    case TRACE_STORE:
        printf("STORE %s = ", e->store.var ? e->store.var : "?");
        if (e->store.deep_json) {
            printf("%s", e->store.deep_json);
        } else if (e->store.sval) {
            printf("\"%s\"", e->store.sval);
        } else if (e->store.fval != 0.0) {
            printf("%g", e->store.fval);
        } else {
            printf("%lld", (long long)e->store.ival);
        }
        break;
    case TRACE_IO:
        printf("IO %s (%d bytes)", e->io.op ? e->io.op : "?", e->io.len);
        break;
    case TRACE_BRANCH:
        printf("BRANCH line %d %s", e->branch.line, e->branch.taken ? "taken" : "not taken");
        break;
    }
    printf("\n");
}

XSReplay *replay_new(const char *trace_path) {
    if (!trace_path) return NULL;

    FILE *f = fopen(trace_path, "rb");
    if (!f) {
        fprintf(stderr, "xs replay: cannot open '%s'\n", trace_path);
        return NULL;
    }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "XST1", 4) != 0) {
        fprintf(stderr, "xs replay: '%s' is not a valid .xst trace file\n", trace_path);
        fclose(f);
        return NULL;
    }

    uint32_t version = 0;
    if (fread(&version, 4, 1, f) != 1 || version != 1) {
        fprintf(stderr, "xs replay: unsupported trace version %u\n", version);
        fclose(f);
        return NULL;
    }

    uint32_t count = 0;
    if (fread(&count, 4, 1, f) != 1) {
        fprintf(stderr, "xs replay: truncated header\n");
        fclose(f);
        return NULL;
    }

    XSReplay *r = calloc(1, sizeof(XSReplay));
    if (!r) { fclose(f); return NULL; }

    r->trace_path = strdup(trace_path);
    r->n_events = (int)count;
    r->events = calloc((size_t)count, sizeof(ReplayEvent));
    r->position = 0;

    if (!r->events) {
        free(r->trace_path);
        free(r);
        fclose(f);
        return NULL;
    }

    for (int i = 0; i < r->n_events; i++) {
        ReplayEvent *e = &r->events[i];
        uint8_t kind = 0;
        if (fread(&kind, 1, 1, f) != 1) { r->n_events = i; break; }
        e->kind = (TraceEventKind)kind;
        if (fread(&e->timestamp, 8, 1, f) != 1) { r->n_events = i; break; }

        switch (e->kind) {
        case TRACE_CALL:
            e->call.fn = read_string(f);
            if (fread(&e->call.line, 4, 1, f) != 1) { r->n_events = i; break; }
            break;
        case TRACE_RETURN: {
            e->ret.fn = read_string(f);
            uint8_t rtag = 0;
            if (fread(&rtag, 1, 1, f) != 1) { r->n_events = i; break; }
            int64_t skip_ival; double skip_fval;
            if (fread(&skip_ival, 8, 1, f) != 1) { r->n_events = i; break; }
            if (fread(&skip_fval, 8, 1, f) != 1) { r->n_events = i; break; }
            char *skip_sval = read_string(f); free(skip_sval);
            e->ret.deep_json = read_string(f);
            break;
        }
        case TRACE_STORE: {
            e->store.var = read_string(f);
            uint8_t tag = 0;
            if (fread(&tag, 1, 1, f) != 1) { r->n_events = i; break; }
            e->store.tag = tag;
            if (fread(&e->store.ival, 8, 1, f) != 1) { r->n_events = i; break; }
            if (fread(&e->store.fval, 8, 1, f) != 1) { r->n_events = i; break; }
            e->store.sval = read_string(f);
            e->store.deep_json = read_string(f);
            break;
        }
        case TRACE_IO:
            e->io.op = read_string(f);
            if (fread(&e->io.len, 4, 1, f) != 1) { r->n_events = i; break; }
            break;
        case TRACE_BRANCH:
            if (fread(&e->branch.line, 4, 1, f) != 1) { r->n_events = i; break; }
            if (fread(&e->branch.taken, 4, 1, f) != 1) { r->n_events = i; break; }
            break;
        }
    }

    fclose(f);
    return r;
}

void replay_free(XSReplay *r) {
    if (!r) return;
    for (int i = 0; i < r->n_events; i++) {
        ReplayEvent *e = &r->events[i];
        switch (e->kind) {
        case TRACE_CALL:   free(e->call.fn); break;
        case TRACE_RETURN: free(e->ret.fn); free(e->ret.deep_json); break;
        case TRACE_STORE:  free(e->store.var); free(e->store.sval); free(e->store.deep_json); break;
        case TRACE_IO:     free(e->io.op); break;
        case TRACE_BRANCH: break;
        }
    }
    free(r->events);
    free(r->trace_path);
    free(r);
}

int replay_step_forward(XSReplay *r) {
    if (!r || r->position >= r->n_events) return 0;
    print_event(r->position, &r->events[r->position]);
    r->position++;
    return (r->position < r->n_events) ? 1 : 0;
}

int replay_step_backward(XSReplay *r) {
    if (!r || r->position <= 0) return 0;
    r->position--;
    print_event(r->position, &r->events[r->position]);
    return 1;
}

int replay_run(const char *trace_path) {
    XSReplay *r = replay_new(trace_path);
    if (!r) return 1;

    printf("XS Replay -- %s (%d events)\n", trace_path, r->n_events);
    printf("Commands: n(ext), p(rev), c(ontinue), q(uit), g <n> (goto event)\n\n");

    if (r->n_events > 0) {
        print_event(r->position, &r->events[r->position]);
    }

    char line[256];
    for (;;) {
        printf("replay> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (len > 0 && line[len-1] == '\r') line[--len] = '\0';

        if (len == 0) continue;

        if (line[0] == 'q') {
            break;
        } else if (line[0] == 'n') {
            if (!replay_step_forward(r)) {
                printf("(end of trace)\n");
            }
        } else if (line[0] == 'p') {
            if (!replay_step_backward(r)) {
                printf("(beginning of trace)\n");
            }
        } else if (line[0] == 'c') {
            while (replay_step_forward(r)) {}
            printf("(end of trace)\n");
        } else if (line[0] == 'g') {
            int target = 0;
            if (sscanf(line + 1, "%d", &target) == 1) {
                if (target < 0) target = 0;
                if (target >= r->n_events) target = r->n_events - 1;
                r->position = target;
                print_event(r->position, &r->events[r->position]);
            } else {
                printf("Usage: g <event_number>\n");
            }
        } else {
            printf("Unknown command. Use n(ext), p(rev), c(ontinue), q(uit), g <n>\n");
        }
    }

    replay_free(r);
    return 0;
}
