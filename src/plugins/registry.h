#ifndef XS_PLUGIN_REGISTRY_H
#define XS_PLUGIN_REGISTRY_H
#include "plugins/plugin_api.h"

typedef struct {
    char *name;
    char *version;
    char *author;
    char *description;
    char **dependencies;
    int   ndeps;
    int   active;
} XSPluginMeta;

typedef struct {
    XSHookType type;
    XSHookFn   fn;
    void      *ctx;
} PluginHookEntry;

typedef struct XSPluginRegistry XSPluginRegistry;

XSPluginRegistry *registry_new(void);
void              registry_free(XSPluginRegistry *r);

int  registry_add(XSPluginRegistry *r, const char *name,
                  xs_plugin_init_fn init_fn);
int  registry_add_full(XSPluginRegistry *r, const XSPluginMeta *meta,
                       xs_plugin_init_fn init_fn);
int  registry_remove(XSPluginRegistry *r, const char *name);
xs_plugin_init_fn registry_get(XSPluginRegistry *r, const char *name);
const XSPluginMeta *registry_get_meta(XSPluginRegistry *r, const char *name);
char **registry_list(XSPluginRegistry *r, int *count_out);  /* caller frees */
int  registry_count(XSPluginRegistry *r);

/* hooks */
int  registry_add_hook(XSPluginRegistry *r, const char *plugin_name,
                       XSHookType type, XSHookFn fn, void *ctx);
int  registry_dispatch_hooks(XSPluginRegistry *r, XSHookType type,
                             Interp *interp, void *data, void **result);

/* deps */
int  registry_check_deps(XSPluginRegistry *r, const char *plugin_name);
char **registry_missing_deps(XSPluginRegistry *r, const char *plugin_name,
                             int *count_out);
int  registry_activate(XSPluginRegistry *r, const char *name);
int  registry_deactivate(XSPluginRegistry *r, const char *name);

#endif
