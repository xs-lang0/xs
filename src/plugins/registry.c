#include "plugins/registry.h"
#include "core/xs.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#define MAX_HOOKS_PER_PLUGIN 32

typedef struct RegEntry {
    /* Metadata */
    XSPluginMeta       meta;
    xs_plugin_init_fn  init_fn;

    /* Per-plugin hooks */
    PluginHookEntry    hooks[MAX_HOOKS_PER_PLUGIN];
    int                nhooks;

    /* Linked list */
    struct RegEntry   *next;
} RegEntry;

struct XSPluginRegistry {
    RegEntry *head;
    int       count;
};


static void meta_init(XSPluginMeta *m) {
    memset(m, 0, sizeof(*m));
}

static void meta_free_contents(XSPluginMeta *m) {
    free(m->name);
    free(m->version);
    free(m->author);
    free(m->description);
    for (int j = 0; j < m->ndeps; j++) {
        free(m->dependencies[j]);
    }
    free(m->dependencies);
}

static void meta_copy(XSPluginMeta *dst, const XSPluginMeta *src) {
    dst->name        = xs_strdup(src->name);
    dst->version     = xs_strdup(src->version);
    dst->author      = xs_strdup(src->author);
    dst->description = xs_strdup(src->description);
    dst->active      = src->active;
    dst->ndeps       = src->ndeps;
    if (src->ndeps > 0 && src->dependencies) {
        dst->dependencies = malloc(sizeof(char *) * (size_t)src->ndeps);
        for (int j = 0; j < src->ndeps; j++) {
            dst->dependencies[j] = xs_strdup(src->dependencies[j]);
        }
    } else {
        dst->dependencies = NULL;
    }
}

static RegEntry *find_entry(XSPluginRegistry *r, const char *name) {
    for (RegEntry *e = r->head; e; e = e->next) {
        if (e->meta.name && strcmp(e->meta.name, name) == 0) {
            return e;
        }
    }
    return NULL;
}


XSPluginRegistry *registry_new(void) {
    XSPluginRegistry *r = calloc(1, sizeof *r);
    return r;
}

void registry_free(XSPluginRegistry *r) {
    if (!r) return;
    RegEntry *e = r->head;
    while (e) {
        RegEntry *next = e->next;
        meta_free_contents(&e->meta);
        free(e);
        e = next;
    }
    free(r);
}


int registry_add(XSPluginRegistry *r, const char *name,
                 xs_plugin_init_fn init_fn) {
    if (!r || !name || !init_fn) return 1;

    RegEntry *existing = find_entry(r, name);
    if (existing) {
        existing->init_fn = init_fn;
        return 0;
    }

    RegEntry *entry = calloc(1, sizeof *entry);
    if (!entry) return 1;

    meta_init(&entry->meta);
    entry->meta.name   = xs_strdup(name);
    entry->meta.active = 0;
    entry->init_fn     = init_fn;
    entry->nhooks      = 0;

    entry->next = r->head;
    r->head     = entry;
    r->count++;
    return 0;
}

int registry_add_full(XSPluginRegistry *r, const XSPluginMeta *meta,
                      xs_plugin_init_fn init_fn) {
    if (!r || !meta || !meta->name) return 1;

    RegEntry *existing = find_entry(r, meta->name);
    if (existing) {
        meta_free_contents(&existing->meta);
        meta_copy(&existing->meta, meta);
        existing->init_fn = init_fn;
        return 0;
    }

    RegEntry *entry = calloc(1, sizeof *entry);
    if (!entry) return 1;

    meta_copy(&entry->meta, meta);
    entry->init_fn = init_fn;
    entry->nhooks  = 0;

    entry->next = r->head;
    r->head     = entry;
    r->count++;
    return 0;
}

int registry_remove(XSPluginRegistry *r, const char *name) {
    if (!r || !name) return 1;

    RegEntry *prev = NULL;
    for (RegEntry *e = r->head; e; e = e->next) {
        if (e->meta.name && strcmp(e->meta.name, name) == 0) {
            if (prev) {
                prev->next = e->next;
            } else {
                r->head = e->next;
            }
            meta_free_contents(&e->meta);
            free(e);
            r->count--;
            return 0;
        }
        prev = e;
    }
    return 1;
}

xs_plugin_init_fn registry_get(XSPluginRegistry *r, const char *name) {
    if (!r || !name) return NULL;
    RegEntry *e = find_entry(r, name);
    return e ? e->init_fn : NULL;
}

const XSPluginMeta *registry_get_meta(XSPluginRegistry *r, const char *name) {
    if (!r || !name) return NULL;
    RegEntry *e = find_entry(r, name);
    return e ? &e->meta : NULL;
}

char **registry_list(XSPluginRegistry *r, int *count_out) {
    if (!r || r->count == 0) {
        if (count_out) *count_out = 0;
        return NULL;
    }
    char **names = malloc(sizeof(char *) * (size_t)r->count);
    if (!names) {
        if (count_out) *count_out = 0;
        return NULL;
    }
    int idx = 0;
    for (RegEntry *e = r->head; e; e = e->next) {
        names[idx++] = xs_strdup(e->meta.name);
    }
    if (count_out) *count_out = idx;
    return names;
}

int registry_count(XSPluginRegistry *r) {
    return r ? r->count : 0;
}

/* hooks */

int registry_add_hook(XSPluginRegistry *r, const char *plugin_name,
                      XSHookType type, XSHookFn fn, void *ctx) {
    if (!r || !plugin_name || !fn) return 1;
    if (type < 0 || type >= XS_HOOK_COUNT) return 1;

    RegEntry *e = find_entry(r, plugin_name);
    if (!e) return 1;

    if (e->nhooks >= MAX_HOOKS_PER_PLUGIN) {
        fprintf(stderr, "xs: registry: too many hooks for plugin '%s'\n",
                plugin_name);
        return 1;
    }

    PluginHookEntry *he = &e->hooks[e->nhooks++];
    he->type = type;
    he->fn   = fn;
    he->ctx  = ctx;

    plugin_register_hook(plugin_name, type, fn, ctx);
    return 0;
}

int registry_dispatch_hooks(XSPluginRegistry *r, XSHookType type,
                            Interp *interp, void *data, void **result) {
    if (!r) return 0;
    if (type < 0 || type >= XS_HOOK_COUNT) return 0;

    for (RegEntry *e = r->head; e; e = e->next) {
        if (!e->meta.active) continue;

        for (int j = 0; j < e->nhooks; j++) {
            PluginHookEntry *he = &e->hooks[j];
            if (he->type == type && he->fn) {
                int handled = he->fn(interp, data, result, he->ctx);
                if (handled) return 1;
            }
        }
    }
    return 0;
}

// dependency checking

int registry_check_deps(XSPluginRegistry *r, const char *plugin_name) {
    if (!r || !plugin_name) return -1;

    RegEntry *e = find_entry(r, plugin_name);
    if (!e) return -1;

    int missing = 0;
    for (int j = 0; j < e->meta.ndeps; j++) {
        const char *dep = e->meta.dependencies[j];
        RegEntry *dep_entry = find_entry(r, dep);
        if (!dep_entry || !dep_entry->meta.active) {
            missing++;
        }
    }
    return missing;
}

char **registry_missing_deps(XSPluginRegistry *r, const char *plugin_name,
                             int *count_out) {
    if (count_out) *count_out = 0;
    if (!r || !plugin_name) return NULL;

    RegEntry *e = find_entry(r, plugin_name);
    if (!e) return NULL;

    int missing = 0;
    for (int j = 0; j < e->meta.ndeps; j++) {
        const char *dep = e->meta.dependencies[j];
        RegEntry *dep_entry = find_entry(r, dep);
        if (!dep_entry || !dep_entry->meta.active) {
            missing++;
        }
    }

    if (missing == 0) return NULL;

    char **names = malloc(sizeof(char *) * (size_t)missing);
    if (!names) return NULL;

    int idx = 0;
    for (int j = 0; j < e->meta.ndeps; j++) {
        const char *dep = e->meta.dependencies[j];
        RegEntry *dep_entry = find_entry(r, dep);
        if (!dep_entry || !dep_entry->meta.active) {
            names[idx++] = xs_strdup(dep);
        }
    }

    if (count_out) *count_out = idx;
    return names;
}

int registry_activate(XSPluginRegistry *r, const char *name) {
    if (!r || !name) return 1;

    RegEntry *e = find_entry(r, name);
    if (!e) return 1;

    int missing = registry_check_deps(r, name);
    if (missing > 0) {
        fprintf(stderr, "xs: cannot activate plugin '%s': "
                "%d unmet dependencies\n", name, missing);

        int dep_count = 0;
        char **deps = registry_missing_deps(r, name, &dep_count);
        if (deps) {
            for (int j = 0; j < dep_count; j++) {
                fprintf(stderr, "  missing: %s\n", deps[j]);
                free(deps[j]);
            }
            free(deps);
        }
        return 1;
    }

    e->meta.active = 1;
    return 0;
}

int registry_deactivate(XSPluginRegistry *r, const char *name) {
    if (!r || !name) return 1;

    RegEntry *e = find_entry(r, name);
    if (!e) return 1;

    e->meta.active = 0;
    return 0;
}
