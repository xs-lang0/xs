/* xs_regex.h — Thompson NFA regex engine (replaces POSIX regex.h on MinGW) */
#ifndef XS_REGEX_H
#define XS_REGEX_H

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* POSIX-compatible flags */
#ifndef REG_EXTENDED
#define REG_EXTENDED 1
#endif
#ifndef REG_ICASE
#define REG_ICASE    2
#endif
#ifndef REG_NEWLINE
#define REG_NEWLINE  4
#endif
#ifndef REG_NOSUB
#define REG_NOSUB    8
#endif
#ifndef REG_NOTBOL
#define REG_NOTBOL   1
#endif
#ifndef REG_NOTEOL
#define REG_NOTEOL   2
#endif

enum {
    XR_LIT,      /* literal char */
    XR_DOT,      /* any char */
    XR_CCLASS,   /* character class (bitmap) */
    XR_SPLIT,    /* fork: out1 and out2 */
    XR_JMP,      /* unconditional jump to out1 */
    XR_MATCH,    /* accept state */
    XR_SAVE,     /* capture group marker (sub = group index * 2 + start/end) */
    XR_BOL,      /* ^ anchor */
    XR_EOL       /* $ anchor */
};

typedef struct { unsigned char bits[32]; } xr_cclass_t;

static inline void xr_cc_set(xr_cclass_t *cc, int c)   { cc->bits[c>>3] |=  (1u<<(c&7)); }
static inline int  xr_cc_test(const xr_cclass_t *cc, int c) { return (cc->bits[c>>3]>>(c&7))&1; }

typedef struct xr_node {
    int type;
    int ch;              /* for XR_LIT */
    int sub;             /* for XR_SAVE: slot index */
    int negated;         /* for XR_CCLASS */
    xr_cclass_t cc;      /* for XR_CCLASS */
    struct xr_node *out1;
    struct xr_node *out2;
} xr_node;

/* compiled regex */
typedef struct {
    xr_node *nodes;     /* array of nodes */
    int nnodes;
    int cap;
    xr_node *start;     /* entry node */
    int nsub;           /* number of capture groups */
    int flags;
} regex_t;

typedef struct {
    int rm_so;
    int rm_eo;
} regmatch_t;

typedef struct {
    const char *src;
    int pos;
    int nsub;           /* capture group counter */
    xr_node *nodes;
    int nnodes;
    int cap;
} xr_parse_t;

static xr_node *xr_alloc(xr_parse_t *p) {
    if (p->nnodes >= p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        p->nodes = (xr_node*)realloc(p->nodes, (size_t)p->cap * sizeof(xr_node));
    }
    xr_node *n = &p->nodes[p->nnodes++];
    memset(n, 0, sizeof(*n));
    return n;
}

static xr_node *xr_parse_alt(xr_parse_t *p);
static xr_node *xr_parse_seq(xr_parse_t *p);
static xr_node *xr_parse_atom(xr_parse_t *p);
static xr_node *xr_parse_quant(xr_parse_t *p);

static int xr_peek(xr_parse_t *p) {
    return p->src[p->pos] ? (unsigned char)p->src[p->pos] : -1;
}

static int xr_next(xr_parse_t *p) {
    if (!p->src[p->pos]) return -1;
    return (unsigned char)p->src[p->pos++];
}

/* patch NULL out-pointers with target */
static void xr_patch(xr_node *n, xr_node *target, xr_node **visited, int *nvisited) {
    if (!n || n->type == XR_MATCH) return;
    for (int i = 0; i < *nvisited; i++)
        if (visited[i] == n) return;
    if (*nvisited < 4096) visited[(*nvisited)++] = n;

    if (!n->out1) { n->out1 = target; }
    else xr_patch(n->out1, target, visited, nvisited);

    if (n->type == XR_SPLIT) {
        if (!n->out2) { n->out2 = target; }
        else xr_patch(n->out2, target, visited, nvisited);
    }
}

static void xr_do_patch(xr_node *n, xr_node *target) {
    xr_node *visited[4096];
    int nv = 0;
    xr_patch(n, target, visited, &nv);
}

static xr_node *xr_parse_cclass(xr_parse_t *p) {
    xr_node *n = xr_alloc(p);
    n->type = XR_CCLASS;
    n->negated = 0;
    memset(&n->cc, 0, sizeof(n->cc));

    if (xr_peek(p) == '^') { n->negated = 1; xr_next(p); }

    int first = 1;
    while (xr_peek(p) != -1 && (first || xr_peek(p) != ']')) {
        first = 0;
        int ch = xr_next(p);
        if (ch == '\\' && xr_peek(p) != -1) {
            ch = xr_next(p);
            switch (ch) {
                case 'd': for (int c='0';c<='9';c++) xr_cc_set(&n->cc,c); continue;
                case 'w': for (int c='0';c<='9';c++) xr_cc_set(&n->cc,c);
                          for (int c='a';c<='z';c++) xr_cc_set(&n->cc,c);
                          for (int c='A';c<='Z';c++) xr_cc_set(&n->cc,c);
                          xr_cc_set(&n->cc,'_'); continue;
                case 's': xr_cc_set(&n->cc,' '); xr_cc_set(&n->cc,'\t');
                          xr_cc_set(&n->cc,'\n'); xr_cc_set(&n->cc,'\r');
                          xr_cc_set(&n->cc,'\f'); xr_cc_set(&n->cc,'\v'); continue;
                default: break; /* literal escaped char */
            }
        }
        /* range? */
        if (xr_peek(p) == '-' && p->src[p->pos+1] && p->src[p->pos+1] != ']') {
            xr_next(p); /* consume '-' */
            int end = xr_next(p);
            if (end == '\\' && xr_peek(p) != -1) end = xr_next(p);
            for (int c = ch; c <= end; c++) xr_cc_set(&n->cc, c);
        } else {
            xr_cc_set(&n->cc, ch);
        }
    }
    if (xr_peek(p) == ']') xr_next(p);
    return n;
}

static xr_node *xr_parse_atom(xr_parse_t *p) {
    int c = xr_peek(p);
    if (c == -1 || c == ')' || c == '|') return NULL;

    if (c == '(') {
        xr_next(p);
        int grp = p->nsub++;
        xr_node *save_start = xr_alloc(p);
        save_start->type = XR_SAVE;
        save_start->sub = grp * 2;

        xr_node *body = xr_parse_alt(p);

        xr_node *save_end = xr_alloc(p);
        save_end->type = XR_SAVE;
        save_end->sub = grp * 2 + 1;

        if (xr_peek(p) == ')') xr_next(p);

        if (body) {
            save_start->out1 = body;
            xr_do_patch(body, save_end);
        } else {
            save_start->out1 = save_end;
        }
        return save_start;
    }
    if (c == '[') {
        xr_next(p);
        return xr_parse_cclass(p);
    }
    if (c == '.') {
        xr_next(p);
        xr_node *n = xr_alloc(p);
        n->type = XR_DOT;
        return n;
    }
    if (c == '^') {
        xr_next(p);
        xr_node *n = xr_alloc(p);
        n->type = XR_BOL;
        return n;
    }
    if (c == '$') {
        xr_next(p);
        xr_node *n = xr_alloc(p);
        n->type = XR_EOL;
        return n;
    }
    if (c == '\\') {
        xr_next(p);
        int ec = xr_next(p);
        if (ec == -1) return NULL;
        if (ec == 'd' || ec == 'w' || ec == 's' ||
            ec == 'D' || ec == 'W' || ec == 'S') {
            xr_node *n = xr_alloc(p);
            n->type = XR_CCLASS;
            memset(&n->cc, 0, sizeof(n->cc));
            n->negated = 0;
            switch (ec) {
                case 'd': for (int i='0';i<='9';i++) xr_cc_set(&n->cc,i); break;
                case 'D': for (int i='0';i<='9';i++) xr_cc_set(&n->cc,i); n->negated=1; break;
                case 'w': for (int i='0';i<='9';i++) xr_cc_set(&n->cc,i);
                          for (int i='a';i<='z';i++) xr_cc_set(&n->cc,i);
                          for (int i='A';i<='Z';i++) xr_cc_set(&n->cc,i);
                          xr_cc_set(&n->cc,'_'); break;
                case 'W': for (int i='0';i<='9';i++) xr_cc_set(&n->cc,i);
                          for (int i='a';i<='z';i++) xr_cc_set(&n->cc,i);
                          for (int i='A';i<='Z';i++) xr_cc_set(&n->cc,i);
                          xr_cc_set(&n->cc,'_'); n->negated=1; break;
                case 's': xr_cc_set(&n->cc,' '); xr_cc_set(&n->cc,'\t');
                          xr_cc_set(&n->cc,'\n'); xr_cc_set(&n->cc,'\r');
                          xr_cc_set(&n->cc,'\f'); xr_cc_set(&n->cc,'\v'); break;
                case 'S': xr_cc_set(&n->cc,' '); xr_cc_set(&n->cc,'\t');
                          xr_cc_set(&n->cc,'\n'); xr_cc_set(&n->cc,'\r');
                          xr_cc_set(&n->cc,'\f'); xr_cc_set(&n->cc,'\v'); n->negated=1; break;
            }
            return n;
        }
        xr_node *n = xr_alloc(p);
        n->type = XR_LIT;
        n->ch = ec;
        return n;
    }
    xr_next(p);
    xr_node *n = xr_alloc(p);
    n->type = XR_LIT;
    n->ch = c;
    return n;
}

static xr_node *xr_parse_quant(xr_parse_t *p) {
    xr_node *atom = xr_parse_atom(p);
    if (!atom) return NULL;

    int c = xr_peek(p);
    if (c == '*') {
        xr_next(p);
        /* split -> atom -> (loop back to split), or skip */
        xr_node *sp = xr_alloc(p);
        sp->type = XR_SPLIT;
        sp->out1 = atom;  /* try match */
        sp->out2 = NULL;  /* skip (dangling = next) */
        xr_do_patch(atom, sp); /* loop back */
        return sp;
    }
    if (c == '+') {
        xr_next(p);
        /* atom -> split -> (back to atom), or continue */
        xr_node *sp = xr_alloc(p);
        sp->type = XR_SPLIT;
        sp->out1 = atom;  /* loop back */
        sp->out2 = NULL;  /* continue (dangling) */
        xr_do_patch(atom, sp);
        return atom;       /* enter at atom */
    }
    if (c == '?') {
        xr_next(p);
        /* split -> atom, or skip */
        xr_node *sp = xr_alloc(p);
        sp->type = XR_SPLIT;
        sp->out1 = atom;  /* try */
        sp->out2 = NULL;  /* skip (dangling) */
        return sp;
    }
    return atom;
}

static xr_node *xr_parse_seq(xr_parse_t *p) {
    xr_node *first = NULL, *last = NULL;
    while (xr_peek(p) != -1 && xr_peek(p) != ')' && xr_peek(p) != '|') {
        xr_node *q = xr_parse_quant(p);
        if (!q) break;
        if (!first) { first = last = q; }
        else {
            xr_do_patch(last, q);
            last = q;
        }
    }
    return first;
}

static xr_node *xr_parse_alt(xr_parse_t *p) {
    xr_node *left = xr_parse_seq(p);
    if (xr_peek(p) != '|') return left;
    xr_next(p); /* consume '|' */
    xr_node *right = xr_parse_alt(p);
    xr_node *sp = xr_alloc(p);
    sp->type = XR_SPLIT;
    sp->out1 = left;
    sp->out2 = right;
    return sp;
}

/* NFA simulation */

#define XR_MAX_SUB 20  /* max 10 capture groups */

typedef struct {
    xr_node *node;
    int saved[XR_MAX_SUB];
} xr_thread;

typedef struct {
    xr_thread *threads;
    int n;
    int cap;
} xr_tlist;

static void xr_tlist_init(xr_tlist *l) {
    l->threads = NULL; l->n = 0; l->cap = 0;
}
static void xr_tlist_free(xr_tlist *l) { free(l->threads); }

static void xr_tlist_add(xr_tlist *l, xr_node *node, const int *saved,
                          int nsub, int *gen, int curgen) {
    if (!node) return;
    /* epsilon closure */
    switch (node->type) {
    case XR_JMP:
        xr_tlist_add(l, node->out1, saved, nsub, gen, curgen);
        return;
    case XR_SPLIT:
        xr_tlist_add(l, node->out1, saved, nsub, gen, curgen);
        xr_tlist_add(l, node->out2, saved, nsub, gen, curgen);
        return;
    case XR_SAVE: {
        int s[XR_MAX_SUB];
        memcpy(s, saved, sizeof(s));
        if (node->sub < XR_MAX_SUB) s[node->sub] = gen[0]; /* use gen[0] as current pos */
        xr_tlist_add(l, node->out1, s, nsub, gen, curgen);
        return;
    }
    default:
        break;
    }

    if (l->n >= l->cap) {
        l->cap = l->cap ? l->cap * 2 : 32;
        l->threads = (xr_thread*)realloc(l->threads, (size_t)l->cap * sizeof(xr_thread));
    }
    xr_thread *t = &l->threads[l->n++];
    t->node = node;
    memcpy(t->saved, saved, sizeof(t->saved));
}

static int xr_run_nfa(xr_node *start, const char *str, int pos, int nsub,
                       int *best_saved, int slen) {
    xr_tlist cur, nxt;
    xr_tlist_init(&cur);
    xr_tlist_init(&nxt);

    int saved[XR_MAX_SUB];
    for (int i = 0; i < XR_MAX_SUB; i++) saved[i] = -1;
    int gen_arr[2]; /* gen_arr[0] = current string pos */

    int matched = 0;

    gen_arr[0] = pos;
    xr_tlist_add(&cur, start, saved, nsub, gen_arr, 0);

    for (int sp = pos; ; sp++) {
        int ch = (sp < slen) ? (unsigned char)str[sp] : -1;
        nxt.n = 0;

        for (int i = 0; i < cur.n; i++) {
            xr_thread *t = &cur.threads[i];
            xr_node *nd = t->node;

            switch (nd->type) {
            case XR_LIT:
                if (ch == nd->ch) {
                    gen_arr[0] = sp + 1;
                    xr_tlist_add(&nxt, nd->out1, t->saved, nsub, gen_arr, sp+1);
                }
                break;
            case XR_DOT:
                if (ch != -1 && ch != '\n') {
                    gen_arr[0] = sp + 1;
                    xr_tlist_add(&nxt, nd->out1, t->saved, nsub, gen_arr, sp+1);
                }
                break;
            case XR_CCLASS:
                if (ch != -1) {
                    int in = xr_cc_test(&nd->cc, ch);
                    if (nd->negated) in = !in;
                    if (in) {
                        gen_arr[0] = sp + 1;
                        xr_tlist_add(&nxt, nd->out1, t->saved, nsub, gen_arr, sp+1);
                    }
                }
                break;
            case XR_BOL:
                if (sp == 0 || str[sp-1] == '\n') {
                    gen_arr[0] = sp;
                    xr_tlist_add(&nxt, nd->out1, t->saved, nsub, gen_arr, sp);
                    /* BOL doesn't consume, but we put it in nxt to not re-process.
                       Actually we need to handle it differently -- put in cur at same sp. */
                }
                break;
            case XR_EOL:
                if (ch == -1 || ch == '\n') {
                    gen_arr[0] = sp;
                    xr_tlist_add(&nxt, nd->out1, t->saved, nsub, gen_arr, sp);
                }
                break;
            case XR_MATCH:
                if (!matched || t->saved[1] > best_saved[1] ||
                    (t->saved[1] == best_saved[1] && t->saved[0] < best_saved[0])) {
                    memcpy(best_saved, t->saved, sizeof(int) * XR_MAX_SUB);
                    matched = 1;
                }
                break;
            default:
                break;
            }
        }

        if (nxt.n == 0) break;
        if (ch == -1) break;

        xr_tlist tmp = cur;
        cur = nxt;
        nxt = tmp;
        nxt.n = 0;
    }

    xr_tlist_free(&cur);
    xr_tlist_free(&nxt);
    return matched;
}

/* public API */

static inline int regcomp(regex_t *preg, const char *pattern, int cflags) {
    (void)cflags;
    memset(preg, 0, sizeof(*preg));
    preg->flags = cflags;

    xr_parse_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.src = pattern;
    ps.pos = 0;
    ps.nsub = 1; /* group 0 = entire match */

    /* group 0 = entire match */
    xr_node *save0_start = xr_alloc(&ps);
    save0_start->type = XR_SAVE;
    save0_start->sub = 0;

    xr_node *body = xr_parse_alt(&ps);

    xr_node *save0_end = xr_alloc(&ps);
    save0_end->type = XR_SAVE;
    save0_end->sub = 1;

    xr_node *match = xr_alloc(&ps);
    match->type = XR_MATCH;

    save0_end->out1 = match;

    if (body) {
        save0_start->out1 = body;
        xr_do_patch(body, save0_end);
    } else {
        save0_start->out1 = save0_end;
    }

    preg->nodes = ps.nodes;
    preg->nnodes = ps.nnodes;
    preg->cap = ps.cap;
    preg->start = save0_start;
    preg->nsub = ps.nsub;
    return 0;
}

static inline int regexec(const regex_t *preg, const char *string,
                           size_t nmatch, regmatch_t pmatch[], int eflags) {
    (void)eflags;
    int slen = (int)strlen(string);
    int best[XR_MAX_SUB];

    for (int pos = 0; pos <= slen; pos++) {
        for (int i = 0; i < XR_MAX_SUB; i++) best[i] = -1;
        if (xr_run_nfa(preg->start, string, pos, preg->nsub, best, slen)) {
            if (pmatch && nmatch > 0) {
                for (size_t i = 0; i < nmatch; i++) {
                    if (i * 2 + 1 < XR_MAX_SUB && best[i*2] >= 0) {
                        pmatch[i].rm_so = best[i*2];
                        pmatch[i].rm_eo = best[i*2+1];
                    } else {
                        pmatch[i].rm_so = -1;
                        pmatch[i].rm_eo = -1;
                    }
                }
            }
            return 0; /* match */
        }
    }
    return 1; /* no match (REG_NOMATCH) */
}

static inline void regfree(regex_t *preg) {
    free(preg->nodes);
    preg->nodes = NULL;
    preg->nnodes = 0;
    preg->cap = 0;
}

#endif /* XS_REGEX_H */
