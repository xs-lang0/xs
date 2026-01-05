#ifndef EXHAUST_H
#define EXHAUST_H

#include "core/ast.h"

/* Returns NULL if exhaustive, or malloc'd missing-pattern string. */
char *exhaust_check(MatchArm *arms, int n_arms,
                    const char **enum_variants, int n_variants);

#endif
