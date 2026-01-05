#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/xs.h"
#include "semantic/exhaust.h"

static int is_catchall(Node *pat) {
    if (!pat) return 0;
    switch (pat->tag) {
        case NODE_PAT_WILD:    return 1;
        case NODE_PAT_IDENT:   return 1;
        case NODE_PAT_CAPTURE: return is_catchall(pat->pat_capture.pattern);
        default:               return 0;
    }
}

/* Returns NULL if exhaustive, or a malloc'd description of the missing case. */
char *exhaust_check(MatchArm *arms, int n_arms,
                    const char **variants, int n_variants) {
    if (!arms || n_arms == 0) return xs_strdup("(no arms)");

    for (int i = 0; i < n_arms; i++) {
        if (is_catchall(arms[i].pattern)) return NULL;
    }

    /* bool coverage */
    {
        int all_bool = 1, has_true = 0, has_false = 0;
        for (int i = 0; i < n_arms; i++) {
            Node *p = arms[i].pattern;
            if (p && p->tag == NODE_PAT_LIT && p->pat_lit.tag == 3) {
                if (p->pat_lit.bval) has_true  = 1;
                else                 has_false = 1;
            } else {
                all_bool = 0;
            }
        }
        if (all_bool) {
            if (!has_true)  return xs_strdup("true");
            if (!has_false) return xs_strdup("false");
            return NULL;
        }
    }

    // --- enum variant coverage
    if (n_variants > 0) {
        for (int v = 0; v < n_variants; v++) {
            int found = 0;
            for (int i = 0; i < n_arms && !found; i++) {
                Node *p = arms[i].pattern;
                if (p && p->tag == NODE_PAT_ENUM && p->pat_enum.path) {
                    const char *path = p->pat_enum.path;
                    if (strcmp(path, variants[v]) == 0) {
                        found = 1;
                    } else {
                        size_t plen = strlen(path);
                        size_t vlen = strlen(variants[v]);
                        if (plen > vlen + 2 &&
                            path[plen - vlen - 2] == ':' &&
                            path[plen - vlen - 1] == ':' &&
                            strcmp(path + plen - vlen, variants[v]) == 0) {
                            found = 1;
                        }
                    }
                }
            }
            if (!found) {
                size_t len = strlen(variants[v]) + 4;
                char *msg = (char *)xs_malloc(len);
                snprintf(msg, len, "%s(_)", variants[v]);
                return msg;
            }
        }
        return NULL;
    }

    /* conservative: no variant info */
    {
        int has_wildcard = 0;
        for (int i = 0; i < n_arms; i++) {
            if (is_catchall(arms[i].pattern)) { has_wildcard = 1; break; }
        }
        if (!has_wildcard) {
            return xs_strdup("_ (exhaustiveness cannot be verified: "
                             "consider adding a wildcard pattern)");
        }
    }
    return NULL;
}
