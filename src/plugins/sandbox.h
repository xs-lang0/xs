#ifndef XS_PLUGIN_SANDBOX_H
#define XS_PLUGIN_SANDBOX_H
#include "runtime/interp.h"

typedef struct XSPluginSandbox XSPluginSandbox;

/* capability flags */
#define XS_PLUGIN_CAP_NONE      0x00
#define XS_PLUGIN_CAP_FS_READ   0x01
#define XS_PLUGIN_CAP_FS_WRITE  0x02
#define XS_PLUGIN_CAP_NET       0x04
#define XS_PLUGIN_CAP_PROC      0x08
#define XS_PLUGIN_CAP_ENV       0x10
#define XS_PLUGIN_CAP_FFI       0x20
#define XS_PLUGIN_CAP_UNSAFE    0x40
#define XS_PLUGIN_CAP_ALL       0xFF
#define XS_PLUGIN_CAP_FS        (XS_PLUGIN_CAP_FS_READ | XS_PLUGIN_CAP_FS_WRITE)
#define XS_PLUGIN_CAP_SAFE      (XS_PLUGIN_CAP_NONE)

typedef struct {
    size_t max_memory;
    size_t current_memory;
    double max_cpu_seconds;
    double cpu_seconds_used;
    int    max_open_files;
    int    open_files;
} XSResourceLimits;

XSPluginSandbox *sandbox_new(int capabilities);
XSPluginSandbox *sandbox_create(int capabilities,
                                const XSResourceLimits *limits);
void sandbox_free(XSPluginSandbox *s);

int  sandbox_check(XSPluginSandbox *s, int capability);
int  sandbox_check_all(XSPluginSandbox *s, int required);
void sandbox_grant(XSPluginSandbox *s, int capability);
void sandbox_revoke(XSPluginSandbox *s, int capability);
int  sandbox_capabilities(XSPluginSandbox *s);

/* returns 0=ok, 1=limit exceeded */
int  sandbox_track_alloc(XSPluginSandbox *s, size_t bytes);
void sandbox_track_free(XSPluginSandbox *s, size_t bytes);
int  sandbox_track_cpu(XSPluginSandbox *s, double seconds);
int  sandbox_track_file_open(XSPluginSandbox *s);
void sandbox_track_file_close(XSPluginSandbox *s);
XSResourceLimits sandbox_get_usage(XSPluginSandbox *s);

void  sandbox_apply(XSPluginSandbox *s, Interp *i);
char *sandbox_describe(XSPluginSandbox *s);  /* caller frees */

#endif
