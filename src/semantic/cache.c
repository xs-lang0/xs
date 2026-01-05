#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core/xs.h"
#include "semantic/cache.h"

static uint64_t fnv1a(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
    return h;
}

typedef struct CacheEntry {
    char    *filename;
    uint64_t src_hash;
    int      n_errors;
    struct CacheEntry *next;
} CacheEntry;

#define CACHE_BUCKETS 128
struct SemaCache {
    CacheEntry *buckets[CACHE_BUCKETS];
};

SemaCache *cache_new(void) {
    SemaCache *c = xs_malloc(sizeof *c);
    memset(c->buckets, 0, sizeof c->buckets);
    return c;
}

void cache_free(SemaCache *c) {
    for (int i = 0; i < CACHE_BUCKETS; i++) {
        CacheEntry *e = c->buckets[i];
        while (e) {
            CacheEntry *nx = e->next;
            free(e->filename);
            free(e);
            e = nx;
        }
    }
    free(c);
}

int cache_lookup(SemaCache *c, const char *filename,
                 const char *src, int *out_errors) {
    uint64_t fh = fnv1a(filename) % CACHE_BUCKETS;
    uint64_t sh = fnv1a(src);
    for (CacheEntry *e = c->buckets[fh]; e; e = e->next) {
        if (strcmp(e->filename, filename) == 0 && e->src_hash == sh) {
            *out_errors = e->n_errors;
            return 1;
        }
    }
    return 0;
}

void cache_store(SemaCache *c, const char *filename,
                 const char *src, int n_errors) {
    uint64_t fh = fnv1a(filename) % CACHE_BUCKETS;
    uint64_t sh = fnv1a(src);
    for (CacheEntry *e = c->buckets[fh]; e; e = e->next) {
        if (strcmp(e->filename, filename) == 0) {
            e->src_hash = sh;
            e->n_errors = n_errors;
            return;
        }
    }
    CacheEntry *e = xs_malloc(sizeof *e);
    e->filename = xs_strdup(filename);
    e->src_hash = sh;
    e->n_errors = n_errors;
    e->next = c->buckets[fh];
    c->buckets[fh] = e;
}
