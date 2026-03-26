#include "dap/dap.h"
#include "core/xs.h"
#include "core/ast.h"
#include "core/lexer.h"
#include "core/parser.h"
#include "core/value.h"
#include "core/env.h"
#include "runtime/interp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DAP_MAX_BREAKPOINTS   256
#define DAP_MAX_STACK_FRAMES  128
#define DAP_MAX_SOURCE_LINES  65536
#define DAP_BUF_SMALL         1024
#define DAP_BUF_MEDIUM        4096
#define DAP_BUF_LARGE         16384



typedef struct {
    int  line;
    char condition[256]; /* empty string = unconditional */
} DapBreakpoint;

typedef struct {
    char          *source_path;
    DapBreakpoint  bps[DAP_MAX_BREAKPOINTS];
    int            n_bps;
} DapBreakpointSet;



typedef struct {
    int   id;
    char  name[256];
    char  source[512];
    int   line;
    int   col;
    Env  *env; /* scope at this frame */
} DapStackFrame;



typedef enum {
    STEP_NONE = 0,
    STEP_CONTINUE,
    STEP_NEXT,
    STEP_IN,
    STEP_OUT,
} StepMode;



typedef struct {
    DapBreakpointSet bp_set;
    char            *program_path;
    char            *source_text;
    int              n_source_lines;

    /* Execution state */
    Interp          *interp;
    Node            *program_ast;
    int              running;
    int              terminated;
    int              current_line;
    int              seq;

    /* Stepping */
    StepMode         step_mode;
    int              step_depth;   /* call depth at time of step command */

    /* Call stack */
    DapStackFrame    frames[DAP_MAX_STACK_FRAMES];
    int              n_frames;

    /* Statement execution index for stepping */
    int              stmt_index;
    int              stop_requested;
    int              stop_on_entry;
} DapState;



static char *dap_json_get_string(const char *json, const char *key) {
    if (!json || !key) return NULL;
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') p++;
    if (*p != '"') return NULL;
    p++;
    const char *start = p;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p+1)) p += 2;
        else p++;
    }
    size_t len = (size_t)(p - start);
    char *result = malloc(len + 1);
    if (!result) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;
            switch (start[i]) {
                case 'n': result[j++] = '\n'; break;
                case 't': result[j++] = '\t'; break;
                case '"': result[j++] = '"'; break;
                case '\\': result[j++] = '\\'; break;
                default: result[j++] = start[i]; break;
            }
        } else {
            result[j++] = start[i];
        }
    }
    result[j] = '\0';
    return result;
}

static int dap_json_get_int(const char *json, const char *key) {
    if (!json || !key) return -1;
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') p++;
    return atoi(p);
}



static int dap_json_get_bool(const char *json, const char *key) {
    if (!json || !key) return 0;
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return 0;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') p++;
    return (strncmp(p, "true", 4) == 0) ? 1 : 0;
}

static void json_escape_into(char *dst, size_t dstsz, const char *src) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 6 < dstsz; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
        case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
        case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
        case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
        case '\r': dst[j++] = '\\'; dst[j++] = 'r';  break;
        case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
        default:
            if (c < 0x20) {
                j += (size_t)snprintf(dst + j, dstsz - j, "\\u%04x", c);
            } else {
                dst[j++] = (char)c;
            }
            break;
        }
    }
    dst[j] = '\0';
}



static char *dap_read_message(void) {
    char header[512];
    int content_length = -1;
    while (fgets(header, sizeof(header), stdin)) {
        size_t len = strlen(header);
        while (len > 0 && (header[len-1] == '\r' || header[len-1] == '\n'))
            header[--len] = '\0';
        if (len == 0) break;
        if (strncmp(header, "Content-Length:", 15) == 0)
            content_length = atoi(header + 15);
    }
    if (content_length <= 0) return NULL;
    char *body = malloc((size_t)content_length + 1);
    if (!body) return NULL;
    size_t nread = fread(body, 1, (size_t)content_length, stdin);
    body[nread] = '\0';
    if ((int)nread < content_length) { free(body); return NULL; }
    return body;
}

static void dap_write_message(const char *json) {
    int len = (int)strlen(json);
    fprintf(stdout, "Content-Length: %d\r\n\r\n%s", len, json);
    fflush(stdout);
}



static void dap_send_response(DapState *st, int request_seq, const char *command, const char *body_json) {
    size_t blen = body_json ? strlen(body_json) : 2;
    char *buf = malloc(blen + 512);
    if (!buf) return;
    st->seq++;
    snprintf(buf, blen + 512,
        "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,"
        "\"success\":true,\"command\":\"%s\",\"body\":%s}",
        st->seq, request_seq, command, body_json ? body_json : "{}");
    dap_write_message(buf);
    free(buf);
}

static void dap_send_error_response(DapState *st, int request_seq, const char *command, const char *message) {
    char escaped[512];
    json_escape_into(escaped, sizeof(escaped), message);
    char buf[DAP_BUF_MEDIUM];
    st->seq++;
    snprintf(buf, sizeof(buf),
        "{\"seq\":%d,\"type\":\"response\",\"request_seq\":%d,"
        "\"success\":false,\"command\":\"%s\",\"message\":\"%s\"}",
        st->seq, request_seq, command, escaped);
    dap_write_message(buf);
}

static void dap_send_event(DapState *st, const char *event, const char *body_json) {
    size_t blen = body_json ? strlen(body_json) : 2;
    char *buf = malloc(blen + 256);
    if (!buf) return;
    st->seq++;
    sprintf(buf,
        "{\"seq\":%d,\"type\":\"event\",\"event\":\"%s\",\"body\":%s}",
        st->seq, event, body_json ? body_json : "{}");
    dap_write_message(buf);
    free(buf);
}



static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)(sz + 1));
    if (!buf) { fclose(f); return NULL; }
    size_t nr = fread(buf, 1, (size_t)sz, f);
    buf[nr] = '\0';
    fclose(f);
    return buf;
}

static int count_lines(const char *text) {
    if (!text) return 0;
    int n = 1;
    for (const char *p = text; *p; p++) {
        if (*p == '\n') n++;
    }
    return n;
}

static int load_program(DapState *st) {
    if (!st->program_path) return -1;

    free(st->source_text);
    st->source_text = read_file(st->program_path);
    if (!st->source_text) return -1;

    st->n_source_lines = count_lines(st->source_text);

    Lexer lex;
    lexer_init(&lex, st->source_text, st->program_path);
    TokenArray ta = lexer_tokenize(&lex);

    Parser parser;
    parser_init(&parser, &ta, st->program_path);
    Node *prog = parser_parse(&parser);
    token_array_free(&ta);

    if (!prog || parser.had_error) {
        if (prog) node_free(prog);
        return -1;
    }

    if (st->program_ast) node_free(st->program_ast);
    st->program_ast = prog;

    if (st->interp) interp_free(st->interp);
    st->interp = interp_new(st->program_path);

    return 0;
}



static void push_frame(DapState *st, const char *name, const char *source, int line, int col, Env *env) {
    if (st->n_frames >= DAP_MAX_STACK_FRAMES) return;
    DapStackFrame *f = &st->frames[st->n_frames];
    f->id = st->n_frames + 1;
    snprintf(f->name, sizeof(f->name), "%s", name ? name : "<anonymous>");
    snprintf(f->source, sizeof(f->source), "%s", source ? source : "<unknown>");
    f->line = line;
    f->col = col;
    f->env = env;
    st->n_frames++;
}

static void pop_frame(DapState *st) {
    if (st->n_frames > 0) st->n_frames--;
}

static void update_top_frame(DapState *st, int line, int col) {
    if (st->n_frames > 0) {
        st->frames[st->n_frames - 1].line = line;
        st->frames[st->n_frames - 1].col = col;
    }
}



static int check_breakpoint(DapState *st, int line) {
    for (int i = 0; i < st->bp_set.n_bps; i++) {
        if (st->bp_set.bps[i].line == line) {
            if (st->bp_set.bps[i].condition[0] != '\0' && st->interp) {
                Lexer lex;
                lexer_init(&lex, st->bp_set.bps[i].condition, "<cond>");
                TokenArray ta = lexer_tokenize(&lex);
                Parser p;
                parser_init(&p, &ta, "<cond>");
                Node *prog = parser_parse(&p);
                token_array_free(&ta);

                if (prog && !p.had_error) {
                    interp_run(st->interp, prog);
                    Value *v = st->interp->cf.value;
                    int truthy = v ? value_truthy(v) : 0;
                    if (st->interp->cf.value) {
                        value_decref(st->interp->cf.value);
                        st->interp->cf.value = NULL;
                    }
                    st->interp->cf.signal = 0;
                    node_free(prog);
                    if (!truthy) continue; /* condition false, skip */
                } else {
                    if (prog) node_free(prog);
                }
            }
            return 1;
        }
    }
    return 0;
}

/* step-aware execution */

/*
 * Execute a single statement, checking breakpoints and step mode before.
 * Returns 1 if execution should stop (hit breakpoint or step complete).
 */
static int exec_stmt_with_debug(DapState *st, Node *stmt) {
    if (!stmt) return 0;

    int line = stmt->span.line;
    int col  = stmt->span.col > 0 ? stmt->span.col : 1;

    st->current_line = line;
    update_top_frame(st, line, col);

    int should_stop = 0;

    if (check_breakpoint(st, line)) {
        should_stop = 1;
    }

    switch (st->step_mode) {
    case STEP_NEXT:
        if (st->n_frames <= st->step_depth) {
            should_stop = 1;
        }
        break;
    case STEP_IN:
        should_stop = 1;
        break;
    case STEP_OUT:
        if (st->n_frames < st->step_depth) {
            should_stop = 1;
        }
        break;
    default:
        break;
    }

    if (should_stop) {
        st->stop_requested = 1;
        st->step_mode = STEP_NONE;
        return 1;
    }

    /* Execute the statement */
    interp_exec(st->interp, stmt);

    return 0;
}

/*
 * Run program statements with debug support.
 * Returns the stop reason string, or NULL if completed.
 */
static const char *run_program_debug(DapState *st, int from_stmt) {
    if (!st->program_ast || st->program_ast->tag != NODE_PROGRAM) return NULL;

    NodeList *stmts = &st->program_ast->program.stmts;

    for (int i = from_stmt; i < stmts->len; i++) {
        Node *stmt = stmts->items[i];
        st->stmt_index = i;

        if (exec_stmt_with_debug(st, stmt)) {
            return st->stop_requested ? "breakpoint" : "step";
        }

        /* Check for runtime errors */
        if (st->interp->cf.signal == CF_ERROR || st->interp->cf.signal == CF_PANIC) {
            /* Report the error as an exception event */
            Value *err = st->interp->cf.value;
            {
                char body[DAP_BUF_MEDIUM];
                char escaped[DAP_BUF_SMALL];
                const char *emsg = (err && err->tag == XS_STR) ? err->s : "runtime error";
                json_escape_into(escaped, sizeof(escaped), emsg);
                snprintf(body, sizeof(body),
                    "{\"reason\":\"exception\",\"description\":\"%s\","
                    "\"text\":\"%s\","
                    "\"threadId\":1,\"allThreadsStopped\":true}",
                    escaped, escaped);
                dap_send_event(st, "stopped", body);
            }
            if (st->interp->cf.value) {
                value_decref(st->interp->cf.value);
                st->interp->cf.value = NULL;
            }
            st->interp->cf.signal = 0;
            return "exception";
        }

        /* Check for return/break at top level (end of program) */
        if (st->interp->cf.signal == CF_RETURN) {
            if (st->interp->cf.value) {
                value_decref(st->interp->cf.value);
                st->interp->cf.value = NULL;
            }
            st->interp->cf.signal = 0;
            return NULL; /* program done */
        }
    }

    return NULL; /* completed */
}



static void dap_parse_breakpoints(DapState *st, const char *msg) {
    st->bp_set.n_bps = 0;

    const char *bp = strstr(msg, "\"breakpoints\"");
    if (!bp) return;

    const char *p = bp;
    while (*p && st->bp_set.n_bps < DAP_MAX_BREAKPOINTS) {
        const char *line_key = strstr(p, "\"line\"");
        if (!line_key) break;
        line_key += 6;
        while (*line_key == ' ' || *line_key == ':' || *line_key == '\t') line_key++;
        int line = atoi(line_key);

        if (line > 0) {
            DapBreakpoint *b = &st->bp_set.bps[st->bp_set.n_bps];
            b->line = line;
            b->condition[0] = '\0';

            /* Look for a "condition" field nearby (within this breakpoint object) */
            const char *cond_key = strstr(line_key, "\"condition\"");
            const char *next_line = strstr(line_key + 1, "\"line\"");
            /* Only use this condition if it appears before the next breakpoint */
            if (cond_key && (!next_line || cond_key < next_line)) {
                const char *q = cond_key + 11;
                while (*q == ' ' || *q == ':' || *q == '\t') q++;
                if (*q == '"') {
                    q++;
                    const char *cs = q;
                    while (*q && *q != '"') {
                        if (*q == '\\' && *(q+1)) q += 2;
                        else q++;
                    }
                    size_t clen = (size_t)(q - cs);
                    if (clen >= sizeof(b->condition)) clen = sizeof(b->condition) - 1;
                    memcpy(b->condition, cs, clen);
                    b->condition[clen] = '\0';
                }
            }

            st->bp_set.n_bps++;
        }
        p = line_key + 1;
    }
}



static int build_variables_json(Env *env, char *buf, size_t bufsz, int ref) {
    int off = 0;
    off += snprintf(buf + off, bufsz - (size_t)off, "{\"variables\":[");

    if (env) {
        int first = 1;
        /* For locals (ref=1), show current scope only.
         * For globals (ref=2), show all in globals chain. */
        Env *e = env;
        int max_depth = (ref == 2) ? 100 : 1; /* locals = 1 scope, globals = all */
        int depth = 0;

        while (e && depth < max_depth) {
            for (int i = 0; i < e->len; i++) {
                Binding *b = &e->bindings[i];
                if (!b->name) continue;

                if (!first) off += snprintf(buf + off, bufsz - (size_t)off, ",");
                first = 0;

                char escaped_name[256];
                json_escape_into(escaped_name, sizeof(escaped_name), b->name);

                char *repr = b->value ? value_repr(b->value) : NULL;
                char escaped_val[DAP_BUF_SMALL];
                json_escape_into(escaped_val, sizeof(escaped_val),
                                 repr ? repr : "null");
                free(repr);

                /* Determine type string */
                const char *type_str = "unknown";
                if (b->value) {
                    switch (b->value->tag) {
                    case XS_NULL:   type_str = "null";   break;
                    case XS_BOOL:   type_str = "Bool";   break;
                    case XS_INT:    type_str = "Int";    break;
                    case XS_FLOAT:  type_str = "Float";  break;
                    case XS_STR:    type_str = "String"; break;
                    case XS_CHAR:   type_str = "Char";   break;
                    case XS_ARRAY:  type_str = "Array";  break;
                    case XS_MAP:    type_str = "Map";    break;
                    case XS_TUPLE:  type_str = "Tuple";  break;
                    case XS_FUNC:   type_str = "Fn";     break;
                    case XS_NATIVE: type_str = "NativeFn"; break;
                    case XS_STRUCT_VAL: type_str = "Struct"; break;
                    case XS_ENUM_VAL:   type_str = "Enum";   break;
                    case XS_RANGE:  type_str = "Range";  break;
                    case XS_MODULE: type_str = "Module"; break;
                    default: break;
                    }
                }

                /* Determine variablesReference for compound types */
                int var_ref = 0;
                if (b->value && (b->value->tag == XS_ARRAY || b->value->tag == XS_MAP ||
                                 b->value->tag == XS_TUPLE || b->value->tag == XS_STRUCT_VAL)) {
                    var_ref = 100 + i + depth * 1000; /* unique-ish ref */
                }

                off += snprintf(buf + off, bufsz - (size_t)off,
                    "{\"name\":\"%s\",\"value\":\"%s\",\"type\":\"%s\","
                    "\"variablesReference\":%d}",
                    escaped_name, escaped_val, type_str, var_ref);

                if (off + 256 >= (int)bufsz) break;
            }
            e = e->parent;
            depth++;
        }
    }

    off += snprintf(buf + off, bufsz - (size_t)off, "]}");
    return off;
}

/* handlers */

static void dap_handle_initialize(DapState *st, int req_seq) {
    const char *caps =
        "{"
        "\"supportsConfigurationDoneRequest\":true,"
        "\"supportsFunctionBreakpoints\":false,"
        "\"supportsConditionalBreakpoints\":true,"
        "\"supportsStepBack\":false,"
        "\"supportsSetVariable\":false,"
        "\"supportsRestartFrame\":false,"
        "\"supportsGotoTargetsRequest\":false,"
        "\"supportsStepInTargetsRequest\":false,"
        "\"supportsCompletionsRequest\":false,"
        "\"supportsModulesRequest\":false,"
        "\"supportsExceptionOptions\":false,"
        "\"supportsEvaluateForHovers\":true,"
        "\"supportsTerminateRequest\":true"
        "}";
    dap_send_response(st, req_seq, "initialize", caps);
    dap_send_event(st, "initialized", "{}");
}

static void dap_handle_launch(DapState *st, int req_seq, const char *msg) {
    char *program = dap_json_get_string(msg, "program");
    if (program) {
        free(st->program_path);
        st->program_path = program;
    }

    st->running = 0;
    st->terminated = 0;
    st->current_line = 1;
    st->stmt_index = 0;
    st->step_mode = STEP_NONE;
    st->n_frames = 0;
    st->stop_on_entry = dap_json_get_bool(msg, "stopOnEntry");

    if (load_program(st) != 0) {
        dap_send_error_response(st, req_seq, "launch",
            st->program_path ? "Failed to load program" : "No program specified");
        return;
    }

    /* Push initial frame */
    push_frame(st, "main", st->program_path, 1, 1,
               st->interp ? st->interp->globals : NULL);

    dap_send_response(st, req_seq, "launch", "{}");
}

static void dap_handle_set_breakpoints(DapState *st, int req_seq, const char *msg) {
    char *source_path = NULL;
    const char *src = strstr(msg, "\"source\"");
    if (src) source_path = dap_json_get_string(src, "path");

    free(st->bp_set.source_path);
    st->bp_set.source_path = source_path;
    dap_parse_breakpoints(st, msg);

    /* Build verified breakpoints response */
    char buf[DAP_BUF_MEDIUM];
    int off = 0;
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "{\"breakpoints\":[");
    for (int i = 0; i < st->bp_set.n_bps; i++) {
        if (i > 0) off += snprintf(buf + off, sizeof(buf) - (size_t)off, ",");
        off += snprintf(buf + off, sizeof(buf) - (size_t)off,
            "{\"id\":%d,\"verified\":true,\"line\":%d}",
            i + 1, st->bp_set.bps[i].line);
    }
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "]}");
    dap_send_response(st, req_seq, "setBreakpoints", buf);
}

static void dap_handle_configuration_done(DapState *st, int req_seq) {
    st->running = 1;
    st->stop_requested = 0;
    dap_send_response(st, req_seq, "configurationDone", "{}");

    if (st->stop_on_entry) {
        /* Stop immediately on first line */
        st->step_mode = STEP_NEXT;
        st->step_depth = 0;
        dap_send_event(st, "stopped",
            "{\"reason\":\"entry\",\"threadId\":1,\"allThreadsStopped\":true}");
        st->running = 0;
        return;
    }

    /* Execute the program with debug support */
    st->step_mode = STEP_CONTINUE;
    const char *reason = run_program_debug(st, 0);

    if (st->stop_requested) {
        char body[DAP_BUF_SMALL];
        snprintf(body, sizeof(body),
            "{\"reason\":\"%s\",\"threadId\":1,\"allThreadsStopped\":true}",
            reason ? reason : "breakpoint");
        dap_send_event(st, "stopped", body);
        st->running = 0;
    } else if (!reason || strcmp(reason, "exception") != 0) {
        /* Program completed normally */
        dap_send_event(st, "terminated", "{}");
        st->terminated = 1;
        st->running = 0;
    }
}

static void dap_handle_threads(DapState *st, int req_seq) {
    dap_send_response(st, req_seq, "threads",
        "{\"threads\":[{\"id\":1,\"name\":\"main\"}]}");
}

static void dap_handle_stack_trace(DapState *st, int req_seq) {
    char *buf = malloc(DAP_BUF_LARGE);
    if (!buf) { dap_send_response(st, req_seq, "stackTrace", "{\"stackFrames\":[],\"totalFrames\":0}"); return; }

    int off = 0;
    off += snprintf(buf + off, DAP_BUF_LARGE - (size_t)off, "{\"stackFrames\":[");

    /* Output frames from top (most recent) to bottom */
    for (int i = st->n_frames - 1; i >= 0; i--) {
        DapStackFrame *f = &st->frames[i];
        if (i < st->n_frames - 1) off += snprintf(buf + off, DAP_BUF_LARGE - (size_t)off, ",");

        char escaped_name[512];
        json_escape_into(escaped_name, sizeof(escaped_name), f->name);
        char escaped_source[1024];
        json_escape_into(escaped_source, sizeof(escaped_source), f->source);

        /* Extract just the filename for display */
        const char *basename = f->source;
        const char *slash = strrchr(f->source, '/');
        if (slash) basename = slash + 1;
        char escaped_basename[256];
        json_escape_into(escaped_basename, sizeof(escaped_basename), basename);

        off += snprintf(buf + off, DAP_BUF_LARGE - (size_t)off,
            "{"
            "\"id\":%d,"
            "\"name\":\"%s\","
            "\"source\":{\"name\":\"%s\",\"path\":\"%s\"},"
            "\"line\":%d,"
            "\"column\":%d"
            "}",
            f->id, escaped_name,
            escaped_basename, escaped_source,
            f->line > 0 ? f->line : 1,
            f->col > 0 ? f->col : 1);
    }

    off += snprintf(buf + off, DAP_BUF_LARGE - (size_t)off,
        "],\"totalFrames\":%d}", st->n_frames);

    dap_send_response(st, req_seq, "stackTrace", buf);
    free(buf);
}

static void dap_handle_scopes(DapState *st, int req_seq, const char *msg) {
    int frame_id = dap_json_get_int(msg, "frameId");

    /* Encode frame_id into variablesReference so that the variables request
       can look up the correct frame's environment.
       Scheme: locals = frame_id * 10 + 1, globals = frame_id * 10 + 2. */
    int locals_ref  = frame_id * 10 + 1;
    int globals_ref = frame_id * 10 + 2;

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"scopes\":["
        "{\"name\":\"Locals\",\"variablesReference\":%d,\"expensive\":false},"
        "{\"name\":\"Globals\",\"variablesReference\":%d,\"expensive\":false}"
        "]}", locals_ref, globals_ref);
    dap_send_response(st, req_seq, "scopes", buf);
}

static void dap_handle_variables(DapState *st, int req_seq, const char *msg) {
    int var_ref = dap_json_get_int(msg, "variablesReference");

    char *buf = malloc(DAP_BUF_LARGE);
    if (!buf) { dap_send_response(st, req_seq, "variables", "{\"variables\":[]}"); return; }

    if (!st->interp) {
        snprintf(buf, DAP_BUF_LARGE, "{\"variables\":[]}");
    } else {
        /* Decode variablesReference: frame_id * 10 + kind
           where kind=1 is locals, kind=2 is globals. */
        int kind     = var_ref % 10;
        int frame_id = var_ref / 10;

        if (kind == 2) {
            /* Globals are the same across all frames */
            build_variables_json(st->interp->globals, buf, DAP_BUF_LARGE, var_ref);
        } else if (kind == 1) {
            /* Look up the frame's environment by frame_id.
               Frame IDs are 1-based (see push_frame). */
            Env *frame_env = NULL;
            for (int i = 0; i < st->n_frames; i++) {
                if (st->frames[i].id == frame_id) {
                    frame_env = st->frames[i].env;
                    break;
                }
            }
            if (frame_env) {
                build_variables_json(frame_env, buf, DAP_BUF_LARGE, var_ref);
            } else {
                /* Fallback to current scope if frame not found */
                build_variables_json(st->interp->env, buf, DAP_BUF_LARGE, var_ref);
            }
        } else {
            /* Nested variable reference (array/map elements) - simplified */
            snprintf(buf, DAP_BUF_LARGE, "{\"variables\":[]}");
        }
    }

    dap_send_response(st, req_seq, "variables", buf);
    free(buf);
}

static void dap_handle_continue(DapState *st, int req_seq) {
    dap_send_response(st, req_seq, "continue", "{\"allThreadsContinued\":true}");

    if (st->terminated) {
        dap_send_event(st, "terminated", "{}");
        return;
    }

    st->stop_requested = 0;
    st->step_mode = STEP_CONTINUE;
    const char *reason = run_program_debug(st, st->stmt_index + 1);

    if (st->stop_requested) {
        char body[DAP_BUF_SMALL];
        snprintf(body, sizeof(body),
            "{\"reason\":\"%s\",\"threadId\":1,\"allThreadsStopped\":true}",
            reason ? reason : "breakpoint");
        dap_send_event(st, "stopped", body);
    } else if (!reason || strcmp(reason, "exception") != 0) {
        dap_send_event(st, "terminated", "{}");
        st->terminated = 1;
    }
}

static void dap_handle_next(DapState *st, int req_seq) {
    dap_send_response(st, req_seq, "next", "{}");

    if (st->terminated) {
        dap_send_event(st, "terminated", "{}");
        return;
    }

    st->stop_requested = 0;
    st->step_mode = STEP_NEXT;
    st->step_depth = st->n_frames;
    const char *reason = run_program_debug(st, st->stmt_index + 1);

    if (st->stop_requested) {
        dap_send_event(st, "stopped",
            "{\"reason\":\"step\",\"threadId\":1,\"allThreadsStopped\":true}");
    } else if (!reason || strcmp(reason, "exception") != 0) {
        dap_send_event(st, "terminated", "{}");
        st->terminated = 1;
    }
}

static void dap_handle_step_in(DapState *st, int req_seq) {
    dap_send_response(st, req_seq, "stepIn", "{}");

    if (st->terminated) {
        dap_send_event(st, "terminated", "{}");
        return;
    }

    st->stop_requested = 0;
    st->step_mode = STEP_IN;
    st->step_depth = st->n_frames;
    const char *reason = run_program_debug(st, st->stmt_index + 1);

    if (st->stop_requested) {
        dap_send_event(st, "stopped",
            "{\"reason\":\"step\",\"threadId\":1,\"allThreadsStopped\":true}");
    } else if (!reason || strcmp(reason, "exception") != 0) {
        dap_send_event(st, "terminated", "{}");
        st->terminated = 1;
    }
}

static void dap_handle_step_out(DapState *st, int req_seq) {
    dap_send_response(st, req_seq, "stepOut", "{}");

    if (st->terminated) {
        pop_frame(st);
        dap_send_event(st, "terminated", "{}");
        return;
    }

    st->stop_requested = 0;
    st->step_mode = STEP_OUT;
    st->step_depth = st->n_frames;
    const char *reason = run_program_debug(st, st->stmt_index + 1);

    if (st->stop_requested) {
        dap_send_event(st, "stopped",
            "{\"reason\":\"step\",\"threadId\":1,\"allThreadsStopped\":true}");
    } else if (!reason || strcmp(reason, "exception") != 0) {
        dap_send_event(st, "terminated", "{}");
        st->terminated = 1;
    }
}



static void dap_handle_evaluate(DapState *st, int req_seq, const char *msg) {
    char *expression = dap_json_get_string(msg, "expression");
    if (!expression || strlen(expression) == 0) {
        dap_send_error_response(st, req_seq, "evaluate", "Empty expression");
        free(expression);
        return;
    }

    if (!st->interp) {
        dap_send_error_response(st, req_seq, "evaluate", "No interpreter active");
        free(expression);
        return;
    }

    /* Parse the expression */
    Lexer lex;
    lexer_init(&lex, expression, "<eval>");
    TokenArray ta = lexer_tokenize(&lex);

    Parser parser;
    parser_init(&parser, &ta, "<eval>");
    Node *prog = parser_parse(&parser);
    token_array_free(&ta);

    if (!prog || parser.had_error) {
        char err_buf[512];
        snprintf(err_buf, sizeof(err_buf), "Parse error: %s",
                 parser.error.msg[0] ? parser.error.msg : "syntax error");
        dap_send_error_response(st, req_seq, "evaluate", err_buf);
        if (prog) node_free(prog);
        free(expression);
        return;
    }

    /* Evaluate — use interp_eval for expressions, interp_exec for statements */
    Value *result = NULL;
    if (prog->tag == NODE_PROGRAM && prog->program.stmts.len == 1) {
        Node *stmt = prog->program.stmts.items[0];
        if (stmt->tag == NODE_EXPR_STMT && stmt->expr_stmt.expr) {
            result = interp_eval(st->interp, stmt->expr_stmt.expr);
            if (result) value_incref(result);
        } else {
            interp_exec(st->interp, stmt);
        }
    } else {
        interp_run(st->interp, prog);
    }
    node_free(prog);

    if (st->interp->cf.signal == CF_ERROR || st->interp->cf.signal == CF_PANIC) {
        Value *err = st->interp->cf.value;
        char err_msg[DAP_BUF_SMALL];
        if (err && err->tag == XS_STR) {
            snprintf(err_msg, sizeof(err_msg), "Error: %s", err->s);
        } else {
            char *repr = err ? value_repr(err) : NULL;
            snprintf(err_msg, sizeof(err_msg), "Error: %s", repr ? repr : "unknown");
            free(repr);
        }
        if (st->interp->cf.value) {
            value_decref(st->interp->cf.value);
            st->interp->cf.value = NULL;
        }
        st->interp->cf.signal = 0;
        dap_send_error_response(st, req_seq, "evaluate", err_msg);
    } else {
        if (!result) result = st->interp->cf.value;
        char *repr = result ? value_repr(result) : NULL;
        char escaped[DAP_BUF_SMALL];
        json_escape_into(escaped, sizeof(escaped), repr ? repr : "null");
        free(repr);

        /* Determine type */
        const char *type_str = "null";
        if (result) {
            switch (result->tag) {
            case XS_NULL:   type_str = "null";   break;
            case XS_BOOL:   type_str = "Bool";   break;
            case XS_INT:    type_str = "Int";    break;
            case XS_FLOAT:  type_str = "Float";  break;
            case XS_STR:    type_str = "String"; break;
            case XS_ARRAY:  type_str = "Array";  break;
            case XS_MAP:    type_str = "Map";    break;
            case XS_FUNC:   type_str = "Fn";     break;
            default:        type_str = "value";  break;
            }
        }

        char body[DAP_BUF_MEDIUM];
        snprintf(body, sizeof(body),
            "{\"result\":\"%s\",\"type\":\"%s\",\"variablesReference\":0}",
            escaped, type_str);
        dap_send_response(st, req_seq, "evaluate", body);

        if (result && result != st->interp->cf.value) value_decref(result);
        if (st->interp->cf.value) {
            value_decref(st->interp->cf.value);
            st->interp->cf.value = NULL;
        }
        st->interp->cf.signal = 0;
    }

    free(expression);
}



int dap_run(void) {
    DapState state;
    memset(&state, 0, sizeof(state));
    state.seq = 0;
    state.current_line = 1;

    fprintf(stderr, "xs-dap: starting DAP server v0.2.0\n");

    while (1) {
        char *msg = dap_read_message();
        if (!msg) break;

        char *command = dap_json_get_string(msg, "command");
        int req_seq = dap_json_get_int(msg, "seq");

        if (!command) {
            free(msg);
            continue;
        }

        if (strcmp(command, "initialize") == 0) {
            dap_handle_initialize(&state, req_seq);
        } else if (strcmp(command, "launch") == 0) {
            dap_handle_launch(&state, req_seq, msg);
        } else if (strcmp(command, "setBreakpoints") == 0) {
            dap_handle_set_breakpoints(&state, req_seq, msg);
        } else if (strcmp(command, "configurationDone") == 0) {
            dap_handle_configuration_done(&state, req_seq);
        } else if (strcmp(command, "threads") == 0) {
            dap_handle_threads(&state, req_seq);
        } else if (strcmp(command, "stackTrace") == 0) {
            dap_handle_stack_trace(&state, req_seq);
        } else if (strcmp(command, "scopes") == 0) {
            dap_handle_scopes(&state, req_seq, msg);
        } else if (strcmp(command, "variables") == 0) {
            dap_handle_variables(&state, req_seq, msg);
        } else if (strcmp(command, "continue") == 0) {
            dap_handle_continue(&state, req_seq);
        } else if (strcmp(command, "next") == 0) {
            dap_handle_next(&state, req_seq);
        } else if (strcmp(command, "stepIn") == 0) {
            dap_handle_step_in(&state, req_seq);
        } else if (strcmp(command, "stepOut") == 0) {
            dap_handle_step_out(&state, req_seq);
        } else if (strcmp(command, "evaluate") == 0) {
            dap_handle_evaluate(&state, req_seq, msg);
        } else if (strcmp(command, "disconnect") == 0 || strcmp(command, "terminate") == 0) {
            dap_send_response(&state, req_seq, command, "{}");
            free(command);
            free(msg);
            break;
        } else {
            /* Unknown command */
            char err_msg[256];
            snprintf(err_msg, sizeof(err_msg), "Unknown command: %s", command);
            dap_send_error_response(&state, req_seq, command, err_msg);
        }

        free(command);
        free(msg);
    }

    /* Cleanup */
    free(state.program_path);
    free(state.source_text);
    free(state.bp_set.source_path);
    if (state.program_ast) node_free(state.program_ast);
    if (state.interp) interp_free(state.interp);

    return 0;
}
