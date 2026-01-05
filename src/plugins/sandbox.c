#include "plugins/sandbox.h"
#include "core/env.h"
#include "core/xs.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct XSPluginSandbox {
    int              capabilities;
    XSResourceLimits limits;
};


XSPluginSandbox *sandbox_new(int capabilities) {
    XSPluginSandbox *s = calloc(1, sizeof *s);
    if (s) {
        s->capabilities = capabilities;
        s->limits.max_memory     = 0;
        s->limits.current_memory = 0;
        s->limits.max_cpu_seconds = 0.0;
        s->limits.cpu_seconds_used = 0.0;
        s->limits.max_open_files = 0;
        s->limits.open_files     = 0;
    }
    return s;
}

XSPluginSandbox *sandbox_create(int capabilities,
                                const XSResourceLimits *limits) {
    XSPluginSandbox *s = sandbox_new(capabilities);
    if (s && limits) {
        s->limits.max_memory      = limits->max_memory;
        s->limits.max_cpu_seconds = limits->max_cpu_seconds;
        s->limits.max_open_files  = limits->max_open_files;
    }
    return s;
}

void sandbox_free(XSPluginSandbox *s) {
    free(s);
}


int sandbox_check(XSPluginSandbox *s, int capability) {
    if (!s) return 0;
    return (s->capabilities & capability) != 0;
}

int sandbox_check_all(XSPluginSandbox *s, int required) {
    if (!s) return 0;
    return (s->capabilities & required) == required;
}

void sandbox_grant(XSPluginSandbox *s, int capability) {
    if (!s) return;
    s->capabilities |= capability;
}

void sandbox_revoke(XSPluginSandbox *s, int capability) {
    if (!s) return;
    s->capabilities &= ~capability;
}

int sandbox_capabilities(XSPluginSandbox *s) {
    return s ? s->capabilities : 0;
}

// resource tracking

int sandbox_track_alloc(XSPluginSandbox *s, size_t bytes) {
    if (!s) return 1;
    if (s->limits.max_memory > 0) {
        if (s->limits.current_memory + bytes > s->limits.max_memory) {
            fprintf(stderr, "xs: sandbox: memory limit exceeded "
                    "(current=%zu, requested=%zu, limit=%zu)\n",
                    s->limits.current_memory, bytes, s->limits.max_memory);
            return 1;
        }
    }
    s->limits.current_memory += bytes;
    return 0;
}

void sandbox_track_free(XSPluginSandbox *s, size_t bytes) {
    if (!s) return;
    if (bytes > s->limits.current_memory) {
        s->limits.current_memory = 0;
    } else {
        s->limits.current_memory -= bytes;
    }
}

int sandbox_track_cpu(XSPluginSandbox *s, double seconds) {
    if (!s) return 1;
    s->limits.cpu_seconds_used += seconds;
    if (s->limits.max_cpu_seconds > 0.0 &&
        s->limits.cpu_seconds_used > s->limits.max_cpu_seconds) {
        fprintf(stderr, "xs: sandbox: CPU time limit exceeded "
                "(used=%.3f, limit=%.3f)\n",
                s->limits.cpu_seconds_used, s->limits.max_cpu_seconds);
        return 1;
    }
    return 0;
}

int sandbox_track_file_open(XSPluginSandbox *s) {
    if (!s) return 1;
    if (s->limits.max_open_files > 0 &&
        s->limits.open_files >= s->limits.max_open_files) {
        fprintf(stderr, "xs: sandbox: open file limit exceeded (limit=%d)\n",
                s->limits.max_open_files);
        return 1;
    }
    s->limits.open_files++;
    return 0;
}

void sandbox_track_file_close(XSPluginSandbox *s) {
    if (!s) return;
    if (s->limits.open_files > 0) {
        s->limits.open_files--;
    }
}

XSResourceLimits sandbox_get_usage(XSPluginSandbox *s) {
    XSResourceLimits empty;
    memset(&empty, 0, sizeof(empty));
    if (!s) return empty;
    return s->limits;
}


void sandbox_apply(XSPluginSandbox *s, Interp *i) {
    if (!s || !i) return;

    if (!(s->capabilities & XS_PLUGIN_CAP_FS_READ) &&
        !(s->capabilities & XS_PLUGIN_CAP_FS_WRITE)) {
        env_define(i->globals, "io", XS_NULL_VAL, 0);
        env_define(i->globals, "fs", XS_NULL_VAL, 0);
        env_define(i->globals, "File", XS_NULL_VAL, 0);
    }

    if (!(s->capabilities & XS_PLUGIN_CAP_NET)) {
        env_define(i->globals, "net", XS_NULL_VAL, 0);
        env_define(i->globals, "http", XS_NULL_VAL, 0);
        env_define(i->globals, "socket", XS_NULL_VAL, 0);
    }

    if (!(s->capabilities & XS_PLUGIN_CAP_PROC)) {
        env_define(i->globals, "exec", XS_NULL_VAL, 0);
        env_define(i->globals, "process", XS_NULL_VAL, 0);
        env_define(i->globals, "spawn", XS_NULL_VAL, 0);
    }

    if (!(s->capabilities & XS_PLUGIN_CAP_ENV)) {
        env_define(i->globals, "env", XS_NULL_VAL, 0);
        env_define(i->globals, "getenv", XS_NULL_VAL, 0);
        env_define(i->globals, "setenv", XS_NULL_VAL, 0);
    }

    if (!(s->capabilities & XS_PLUGIN_CAP_FFI)) {
        env_define(i->globals, "ffi", XS_NULL_VAL, 0);
        env_define(i->globals, "dlopen", XS_NULL_VAL, 0);
    }
}


char *sandbox_describe(XSPluginSandbox *s) {
    if (!s) return xs_strdup("(null sandbox)");

    char buf[512];
    int  pos = 0;

    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                    "capabilities: 0x%02x [", s->capabilities);

    int first = 1;
    struct { int flag; const char *name; } caps[] = {
        { XS_PLUGIN_CAP_FS_READ,  "fs_read"  },
        { XS_PLUGIN_CAP_FS_WRITE, "fs_write" },
        { XS_PLUGIN_CAP_NET,      "net"      },
        { XS_PLUGIN_CAP_PROC,     "proc"     },
        { XS_PLUGIN_CAP_ENV,      "env"      },
        { XS_PLUGIN_CAP_FFI,      "ffi"      },
        { XS_PLUGIN_CAP_UNSAFE,   "unsafe"   },
        { 0, NULL }
    };

    for (int j = 0; caps[j].name; j++) {
        if (s->capabilities & caps[j].flag) {
            if (!first) {
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ", ");
            }
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                            "%s", caps[j].name);
            first = 0;
        }
    }

    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "]");

    if (s->limits.max_memory > 0) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        ", mem=%zu/%zu", s->limits.current_memory,
                        s->limits.max_memory);
    }
    if (s->limits.max_cpu_seconds > 0.0) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        ", cpu=%.3f/%.3f",
                        s->limits.cpu_seconds_used,
                        s->limits.max_cpu_seconds);
    }
    if (s->limits.max_open_files > 0) {
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        ", files=%d/%d",
                        s->limits.open_files,
                        s->limits.max_open_files);
    }

    return xs_strdup(buf);
}
