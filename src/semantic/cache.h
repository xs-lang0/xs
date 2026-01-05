// Per-module incremental analysis cache (FNV-1a keyed).
#ifndef CACHE_H
#define CACHE_H

typedef struct SemaCache SemaCache;

SemaCache *cache_new(void);
void       cache_free(SemaCache *c);

/* Returns 1 on hit (sets *out_errors), 0 on miss. */
int  cache_lookup(SemaCache *c, const char *filename,
                  const char *src, int *out_errors);
void cache_store(SemaCache *c, const char *filename,
                 const char *src, int n_errors);

#endif
