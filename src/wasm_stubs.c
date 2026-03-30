#include <stdio.h>
#include <stdlib.h>

typedef struct xs_tls_conn xs_tls_conn;
xs_tls_conn *xs_tls_connect(int fd, const char *hostname) { (void)fd; (void)hostname; return NULL; }
int xs_tls_read(xs_tls_conn *conn, void *buf, int len) { (void)conn; (void)buf; (void)len; return -1; }
int xs_tls_write(xs_tls_conn *conn, const void *buf, int len) { (void)conn; (void)buf; (void)len; return -1; }
void xs_tls_close(xs_tls_conn *conn) { (void)conn; }

int repl_run(void) { return 0; }
int lint_file(const char *path, int auto_fix) { (void)path; (void)auto_fix; return 0; }
void coverage_record_line(void *ctx, int line) { (void)ctx; (void)line; }
void coverage_record_branch(void *ctx, int line, int taken) { (void)ctx; (void)line; (void)taken; }

int plugin_load(void *i, const char *path) { (void)i; (void)path; return -1; }

void profiler_start(void) {}
void profiler_stop(void) {}
void profiler_report(void) {}

int pkg_new(const char *name) { (void)name; return 0; }
int pkg_install(const char *name) { (void)name; return 0; }
int pkg_remove(const char *name) { (void)name; return 0; }
int pkg_update(const char *name) { (void)name; return 0; }
int pkg_list(void) { return 0; }
int pkg_publish(void) { return 0; }

typedef struct { int dummy; } IRModule;
typedef struct { int f,d,i,s,c; } OptStats;
void *ir_lower(void *ast) { (void)ast; return NULL; }
void ir_dump(void *ir, void *f) { (void)ir; (void)f; }
void ir_module_free(void *ir) { (void)ir; }
void *optimize(void *program, void *stats) { (void)stats; return program; }

int jit_compile(void *ir) { (void)ir; return 0; }

void tracer_init(void *i) { (void)i; }
void tracer_record(void *i, void *n) { (void)i; (void)n; }
void tracer_save(void *i, const char *path) { (void)i; (void)path; }
void tracer_replay(const char *path) { (void)path; }

int lsp_run(int argc, char **argv) { (void)argc; (void)argv; return 0; }
int dap_run(int argc, char **argv) { (void)argc; (void)argv; return 0; }
int g_no_color = 1;
