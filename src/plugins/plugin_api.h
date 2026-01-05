#ifndef XS_PLUGIN_API_H
#define XS_PLUGIN_API_H
/* Shared-library plugin interface. Plugins export xs_plugin_init(). */
#include "core/value.h"
#include "runtime/interp.h"

#define XS_PLUGIN_API_VERSION 1

typedef enum {
    XS_HOOK_LEXER  = 0,
    XS_HOOK_PARSER = 1,
    XS_HOOK_EVAL   = 2,
    XS_HOOK_TYPE   = 3,
    XS_HOOK_COUNT  = 4
} XSHookType;

/* Returns 0 to continue, 1 if hook consumed the event. */
typedef int (*XSHookFn)(Interp *interp, void *data, void **result, void *ctx);

typedef struct {
    const char *name;
    const char *version;
    const char *description;
    const char *author;
    const char **dependencies;
    int         ndeps;
} XSPluginInfo;

typedef int  (*xs_plugin_init_fn)(Interp *i, int api_version);
typedef void (*xs_plugin_fini_fn)(Interp *i);

int plugin_load(Interp *i, const char *path);
int plugin_unload(Interp *i, const char *name);

/* hooks */
int plugin_register_hook(const char *name, XSHookType type,
                         XSHookFn fn, void *ctx);
int plugin_dispatch_hooks(XSHookType type, Interp *interp,
                          void *data, void **result);

int plugin_register_builtin(Interp *i, const char *name, NativeFn fn);
int plugin_register_type(Interp *i, const char *type_name, void *type_info);

#endif
