/*
 * Stubs for features that cannot work in WASM/WASI.
 * Provides no-op implementations for POSIX APIs not available in wasi-libc.
 */
#ifdef __wasi__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TLS - no raw sockets in browser */
typedef struct xs_tls_conn xs_tls_conn;
xs_tls_conn *xs_tls_connect(int fd, const char *hostname) { (void)fd; (void)hostname; return NULL; }
int xs_tls_read(xs_tls_conn *conn, void *buf, int len) { (void)conn; (void)buf; (void)len; return -1; }
int xs_tls_write(xs_tls_conn *conn, const void *buf, int len) { (void)conn; (void)buf; (void)len; return -1; }
void xs_tls_close(xs_tls_conn *conn) { (void)conn; }

/* REPL - needs terminal stdin */
int repl_run(void) { fprintf(stderr, "repl not available in wasm\n"); return 1; }

/* LSP/DAP - need stdio/socket communication */
int lsp_run(int argc, char **argv) { (void)argc; (void)argv; return 0; }
int dap_run(int argc, char **argv) { (void)argc; (void)argv; return 0; }

/* g_no_color - defined in main.c normally */
int g_no_color = 1;

/* popen/pclose - no subprocesses in WASI */
FILE *popen(const char *command, const char *type) {
    (void)command; (void)type;
    return NULL;
}
int pclose(FILE *stream) {
    (void)stream;
    return -1;
}

/* temp file/dir creation */
int mkstemp(char *tmpl) {
    (void)tmpl;
    return -1;
}
int mkstemps(char *tmpl, int suffixlen) {
    (void)tmpl; (void)suffixlen;
    return -1;
}
char *mkdtemp(char *tmpl) {
    (void)tmpl;
    return NULL;
}

/* process info */
int getppid(void) { return 0; }

/* profiler stubs - sigaction/setitimer not in WASI */
/* profiler.c has __wasi__ guards, but signal.h types still needed */

/* networking stubs - no sockets in WASI */
int socket(int domain, int type, int protocol) { (void)domain; (void)type; (void)protocol; return -1; }
int connect(int fd, const void *addr, unsigned int len) { (void)fd; (void)addr; (void)len; return -1; }
int bind(int fd, const void *addr, unsigned int len) { (void)fd; (void)addr; (void)len; return -1; }
int listen(int fd, int backlog) { (void)fd; (void)backlog; return -1; }
int accept(int fd, void *addr, void *len) { (void)fd; (void)addr; (void)len; return -1; }
int setsockopt(int fd, int level, int name, const void *val, unsigned int len) { (void)fd; (void)level; (void)name; (void)val; (void)len; return -1; }
int getsockname(int fd, void *addr, void *len) { (void)fd; (void)addr; (void)len; return -1; }
int getaddrinfo(const char *node, const char *service, const void *hints, void **res) { (void)node; (void)service; (void)hints; *res = NULL; return -1; }
void freeaddrinfo(void *res) { (void)res; }
int select(int nfds, void *r, void *w, void *e, void *tv) { (void)nfds; (void)r; (void)w; (void)e; (void)tv; return -1; }
int fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; return -1; }
int sendto(int fd, const void *buf, unsigned long len, int flags, const void *addr, unsigned int alen) { (void)fd; (void)buf; (void)len; (void)flags; (void)addr; (void)alen; return -1; }
int recvfrom(int fd, void *buf, unsigned long len, int flags, void *addr, void *alen) { (void)fd; (void)buf; (void)len; (void)flags; (void)addr; (void)alen; return -1; }

/* directory/glob stubs */
typedef struct { void **gl_pathv; int gl_pathc; } glob_t;
int glob(const char *pat, int flags, void *errfunc, glob_t *g) { (void)pat; (void)flags; (void)errfunc; g->gl_pathc = 0; g->gl_pathv = NULL; return -1; }
void globfree(glob_t *g) { (void)g; }

/* system/exec stubs */
int system(const char *cmd) { (void)cmd; return -1; }
int pipe(int fds[2]) { (void)fds; return -1; }
int fork(void) { return -1; }
int waitpid(int pid, int *status, int opts) { (void)pid; (void)status; (void)opts; return -1; }
int kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
/* sleep via host-provided import (Atomics.wait in browser, busy-wait fallback) */
#include <time.h>
extern void __xs_sleep_ms(int ms) __attribute__((import_module("env"), import_name("__xs_sleep_ms")));

unsigned int sleep(unsigned int secs) {
    __xs_sleep_ms((int)(secs * 1000));
    return 0;
}
int usleep(unsigned int usec) {
    __xs_sleep_ms((int)(usec / 1000));
    return 0;
}
int nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    int ms = (int)(req->tv_sec * 1000) + (int)(req->tv_nsec / 1000000);
    __xs_sleep_ms(ms);
    return 0;
}

/* chown - not in wasi-libc (readlink, chmod are provided by wasi-libc) */
int chown(const char *path, unsigned int uid, unsigned int gid) { (void)path; (void)uid; (void)gid; return -1; }

/* process */
int getpid(void) { return 1; }

/* mktemp */
char *_mktemp(char *tmpl) { (void)tmpl; return NULL; }

/* dynamic loading - no dlopen in WASI */
void *dlopen(const char *path, int flags) { (void)path; (void)flags; return NULL; }
void *dlsym(void *handle, const char *symbol) { (void)handle; (void)symbol; return NULL; }
int dlclose(void *handle) { (void)handle; return -1; }
char *dlerror(void) { return "dlopen not supported in WASI"; }

#endif /* __wasi__ */
