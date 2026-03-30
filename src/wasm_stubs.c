/*
 * Stubs for features that genuinely cannot work in WASM.
 * Everything else should be compiled from real source.
 */
#include <stdio.h>
#include <stdlib.h>

/* TLS - no raw sockets in browser */
typedef struct xs_tls_conn xs_tls_conn;
xs_tls_conn *xs_tls_connect(int fd, const char *hostname) { (void)fd; (void)hostname; return NULL; }
int xs_tls_read(xs_tls_conn *conn, void *buf, int len) { (void)conn; (void)buf; (void)len; return -1; }
int xs_tls_write(xs_tls_conn *conn, const void *buf, int len) { (void)conn; (void)buf; (void)len; return -1; }
void xs_tls_close(xs_tls_conn *conn) { (void)conn; }

/* REPL - needs terminal stdin */
int repl_run(void) { fprintf(stderr, "repl not available in wasm\n"); return 1; }

/* JIT - can't mmap executable memory in WASM */
/* (jit.c has #ifdef guards, but stub just in case) */

/* Profiler - uses SIGPROF, no signals in WASM */
#include <signal.h>
/* profiler.c should compile but timer setup is a no-op */

/* LSP/DAP - need stdio/socket communication */
int lsp_run(int argc, char **argv) { (void)argc; (void)argv; return 0; }
int dap_run(int argc, char **argv) { (void)argc; (void)argv; return 0; }

/* g_no_color - defined in main.c normally, but wasm_main.c doesn't set it */
int g_no_color = 1;
