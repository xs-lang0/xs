#ifndef XS_STRBUF_H
#define XS_STRBUF_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

typedef struct { char *data; int len, cap; } SB;

static inline void sb_init(SB *s) { s->data = NULL; s->len = s->cap = 0; }

static inline void sb_ensure(SB *s, int need) {
    if (s->len + need + 1 <= s->cap) return;
    s->cap = (s->len + need + 1) * 2;
    char *p = (char *)realloc(s->data, (size_t)s->cap);
    if (!p) { fprintf(stderr, "out of memory\n"); abort(); }
    s->data = p;
}

static inline void sb_add(SB *s, const char *t) {
    int n = (int)strlen(t);
    sb_ensure(s, n);
    memcpy(s->data + s->len, t, (size_t)n);
    s->len += n;
    s->data[s->len] = '\0';
}

static inline void sb_addc(SB *s, char c) {
    sb_ensure(s, 1);
    s->data[s->len++] = c;
    s->data[s->len] = '\0';
}

static inline void sb_addn(SB *s, const char *t, int n) {
    sb_ensure(s, n);
    memcpy(s->data + s->len, t, (size_t)n);
    s->len += n;
    s->data[s->len] = '\0';
}

static inline void sb_indent(SB *s, int depth) {
    for (int i = 0; i < depth; i++) sb_add(s, "    ");
}

/* sb_printf: callers shouldn't exceed 1k in a single call */
static inline void sb_printf(SB *s, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    sb_add(s, buf);
}

static inline char *sb_finish(SB *s) {
    if (!s->data) { sb_ensure(s, 0); s->data[0] = '\0'; }
    return s->data;
}

static inline void sb_free(SB *s) { free(s->data); sb_init(s); }

#endif
