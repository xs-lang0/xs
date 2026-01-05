#include "plugins/plugin_api.h"
#include "core/xs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef XSC_ENABLE_PLUGINS
#include <dlfcn.h>
#endif

#define MAX_HOOKS_PER_TYPE 64

typedef struct {
    char     *name;
    XSHookFn  fn;
    void     *ctx;
} HookEntry;

static struct {
    HookEntry entries[MAX_HOOKS_PER_TYPE];
    int       count;
} g_hooks[XS_HOOK_COUNT];

static int g_hooks_initialized = 0;

static void hooks_ensure_init(void) {
    if (g_hooks_initialized) return;
    for (int t = 0; t < XS_HOOK_COUNT; t++) {
        g_hooks[t].count = 0;
    }
    g_hooks_initialized = 1;
}

#define MAX_LOADED_PLUGINS 128

typedef struct {
    char              *name;
    char              *path;
    void              *dl_handle;
    xs_plugin_fini_fn  fini_fn;
} LoadedPlugin;

static LoadedPlugin g_loaded[MAX_LOADED_PLUGINS];
static int          g_loaded_count = 0;


int plugin_load(Interp *i, const char *path) {
#ifdef XSC_ENABLE_PLUGINS
    hooks_ensure_init();

    void *dl = dlopen(path, RTLD_NOW);
    if (!dl) {
        fprintf(stderr, "xs: plugin_load: dlopen('%s') failed: %s\n",
                path, dlerror());
        return 1;
    }

    xs_plugin_init_fn init_fn =
        (xs_plugin_init_fn)dlsym(dl, "xs_plugin_init");
    if (!init_fn) {
        fprintf(stderr, "xs: plugin_load: '%s' missing xs_plugin_init: %s\n",
                path, dlerror());
        dlclose(dl);
        return 1;
    }

    int rc = init_fn(i, XS_PLUGIN_API_VERSION);
    if (rc != 0) {
        fprintf(stderr, "xs: plugin_load: '%s' init returned %d\n", path, rc);
        dlclose(dl);
        return 1;
    }

    if (g_loaded_count < MAX_LOADED_PLUGINS) {
        LoadedPlugin *lp = &g_loaded[g_loaded_count++];
        lp->path      = xs_strdup(path);
        lp->dl_handle = dl;

        const char **name_ptr = (const char **)dlsym(dl, "xs_plugin_name");
        lp->name = xs_strdup(name_ptr ? *name_ptr : path);

        lp->fini_fn = (xs_plugin_fini_fn)dlsym(dl, "xs_plugin_fini");
    }

    return 0;
#else
    (void)i; (void)path;
    fprintf(stderr, "xs: plugin loading not available "
            "(rebuild with XSC_ENABLE_PLUGINS=1)\n");
    return 1;
#endif
}

int plugin_unload(Interp *i, const char *name) {
#ifdef XSC_ENABLE_PLUGINS
    for (int j = 0; j < g_loaded_count; j++) {
        if (g_loaded[j].name && strcmp(g_loaded[j].name, name) == 0) {
            LoadedPlugin *lp = &g_loaded[j];

            if (lp->fini_fn) {
                lp->fini_fn(i);
            }

            for (int t = 0; t < XS_HOOK_COUNT; t++) {
                int dst = 0;
                for (int k = 0; k < g_hooks[t].count; k++) {
                    if (g_hooks[t].entries[k].name &&
                        strcmp(g_hooks[t].entries[k].name, name) == 0) {
                        free(g_hooks[t].entries[k].name);
                    } else {
                        if (dst != k) {
                            g_hooks[t].entries[dst] = g_hooks[t].entries[k];
                        }
                        dst++;
                    }
                }
                g_hooks[t].count = dst;
            }

            if (lp->dl_handle) {
                dlclose(lp->dl_handle);
            }
            free(lp->name);
            free(lp->path);

            for (int k = j; k < g_loaded_count - 1; k++) {
                g_loaded[k] = g_loaded[k + 1];
            }
            g_loaded_count--;
            return 0;
        }
    }
    fprintf(stderr, "xs: plugin_unload: '%s' not found\n", name);
    return 1;
#else
    (void)i; (void)name;
    fprintf(stderr, "xs: plugin unloading not available "
            "(rebuild with XSC_ENABLE_PLUGINS=1)\n");
    return 1;
#endif
}

/* hooks */

int plugin_register_hook(const char *name, XSHookType type,
                         XSHookFn fn, void *ctx) {
    hooks_ensure_init();

    if (type < 0 || type >= XS_HOOK_COUNT) {
        fprintf(stderr, "xs: plugin_register_hook: invalid hook type %d\n",
                (int)type);
        return 1;
    }
    if (g_hooks[type].count >= MAX_HOOKS_PER_TYPE) {
        fprintf(stderr, "xs: plugin_register_hook: too many hooks for type %d\n",
                (int)type);
        return 1;
    }

    HookEntry *he = &g_hooks[type].entries[g_hooks[type].count++];
    he->name = xs_strdup(name);
    he->fn   = fn;
    he->ctx  = ctx;
    return 0;
}

int plugin_dispatch_hooks(XSHookType type, Interp *interp,
                          void *data, void **result) {
    hooks_ensure_init();

    if (type < 0 || type >= XS_HOOK_COUNT) return 0;

    for (int j = 0; j < g_hooks[type].count; j++) {
        HookEntry *he = &g_hooks[type].entries[j];
        if (he->fn) {
            int handled = he->fn(interp, data, result, he->ctx);
            if (handled) {
                return 1;
            }
        }
    }
    return 0;
}

/* type registry */

#define MAX_PLUGIN_TYPES 256

typedef struct {
    char *type_name;
    void *type_info;
} PluginTypeEntry;

static PluginTypeEntry g_plugin_types[MAX_PLUGIN_TYPES];
static int             g_plugin_types_count = 0;

static void *plugin_type_lookup(const char *type_name) {
    if (!type_name) return NULL;
    for (int j = 0; j < g_plugin_types_count; j++) {
        if (g_plugin_types[j].type_name &&
            strcmp(g_plugin_types[j].type_name, type_name) == 0) {
            return g_plugin_types[j].type_info;
        }
    }
    return NULL;
}

static Value *native_plugin_has_type(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || !args[0] || args[0]->tag != XS_STR)
        return value_incref(XS_FALSE_VAL);
    void *info = plugin_type_lookup(args[0]->s);
    return value_incref(info ? XS_TRUE_VAL : XS_FALSE_VAL);
}


int plugin_register_builtin(Interp *i, const char *name, NativeFn fn) {
    if (!i || !name || !fn) return 1;
    interp_define_native(i, name, fn);
    return 0;
}

int plugin_register_type(Interp *i, const char *type_name, void *type_info) {
    if (!i || !type_name) return 1;

    for (int j = 0; j < g_plugin_types_count; j++) {
        if (g_plugin_types[j].type_name &&
            strcmp(g_plugin_types[j].type_name, type_name) == 0) {
            g_plugin_types[j].type_info = type_info;
            return 0;
        }
    }

    if (g_plugin_types_count >= MAX_PLUGIN_TYPES) {
        fprintf(stderr, "xs: plugin_register_type: type registry full "
                "(max %d types)\n", MAX_PLUGIN_TYPES);
        return 1;
    }

    PluginTypeEntry *entry = &g_plugin_types[g_plugin_types_count++];
    entry->type_name = xs_strdup(type_name);
    entry->type_info = type_info;

    Value *type_map = xs_map_new();
    map_set(type_map->map, "__plugin_type", value_incref(xs_str(type_name)));
    map_set(type_map->map, "name", value_incref(xs_str(type_name)));

    env_define(i->globals, type_name, type_map, 0);
    static int helper_registered = 0;
    if (!helper_registered) {
        interp_define_native(i, "__plugin_has_type", native_plugin_has_type);
        helper_registered = 1;
    }

    return 0;
}
