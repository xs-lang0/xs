#include "xs_tls.h"
#include "bearssl/bearssl.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#define ssize_t int
#define read(fd, buf, len) recv(fd, buf, len, 0)
#define write(fd, buf, len) send(fd, buf, len, 0)
#else
#include <unistd.h>
#include <sys/socket.h>
#endif

struct xs_tls_conn {
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    int fd;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
};

static int sock_read(void *ctx, unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    ssize_t n = read(fd, buf, len);
    if (n <= 0) return -1;
    return (int)n;
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    ssize_t n = write(fd, buf, len);
    if (n <= 0) return -1;
    return (int)n;
}

/*
 * Insecure x509 wrapper: wraps br_x509_minimal so we get real cert
 * parsing and public key extraction, but always returns 0 (trust)
 * from end_chain. This lets HTTPS work without bundled root CAs.
 */
typedef struct {
    const br_x509_class *vtable;
    br_x509_minimal_context inner;
} insecure_x509_ctx;

static void ix_start_chain(const br_x509_class **ctx, const char *name) {
    insecure_x509_ctx *xc = (insecure_x509_ctx *)ctx;
    xc->inner.vtable->start_chain((const br_x509_class **)&xc->inner, name);
}
static void ix_start_cert(const br_x509_class **ctx, uint32_t length) {
    insecure_x509_ctx *xc = (insecure_x509_ctx *)ctx;
    xc->inner.vtable->start_cert((const br_x509_class **)&xc->inner, length);
}
static void ix_append(const br_x509_class **ctx, const unsigned char *buf, size_t len) {
    insecure_x509_ctx *xc = (insecure_x509_ctx *)ctx;
    xc->inner.vtable->append((const br_x509_class **)&xc->inner, buf, len);
}
static void ix_end_cert(const br_x509_class **ctx) {
    insecure_x509_ctx *xc = (insecure_x509_ctx *)ctx;
    xc->inner.vtable->end_cert((const br_x509_class **)&xc->inner);
}
static unsigned ix_end_chain(const br_x509_class **ctx) {
    insecure_x509_ctx *xc = (insecure_x509_ctx *)ctx;
    xc->inner.vtable->end_chain((const br_x509_class **)&xc->inner);
    return 0; /* always trust */
}
static const br_x509_pkey *ix_get_pkey(const br_x509_class *const *ctx, unsigned *usages) {
    insecure_x509_ctx *xc = (insecure_x509_ctx *)(void *)ctx;
    return xc->inner.vtable->get_pkey((const br_x509_class *const *)&xc->inner, usages);
}

static const br_x509_class insecure_vtable = {
    sizeof(insecure_x509_ctx),
    ix_start_chain,
    ix_start_cert,
    ix_append,
    ix_end_cert,
    ix_end_chain,
    ix_get_pkey
};

static insecure_x509_ctx g_ix509;

xs_tls_conn *xs_tls_connect(int fd, const char *hostname) {
    xs_tls_conn *c = calloc(1, sizeof(xs_tls_conn));
    if (!c) return NULL;
    c->fd = fd;

    br_ssl_client_init_full(&c->sc, &c->xc, NULL, 0);

    /* wrap the x509 minimal engine so we parse certs but skip trust check */
    memset(&g_ix509, 0, sizeof(g_ix509));
    g_ix509.vtable = &insecure_vtable;
    memcpy(&g_ix509.inner, &c->xc, sizeof(br_x509_minimal_context));
    br_ssl_engine_set_x509(&c->sc.eng, (const br_x509_class **)&g_ix509);

    br_ssl_engine_set_buffer(&c->sc.eng, c->iobuf, sizeof(c->iobuf), 1);
    br_ssl_client_reset(&c->sc, hostname, 0);
    br_sslio_init(&c->ioc, &c->sc.eng, sock_read, &c->fd, sock_write, &c->fd);

    br_sslio_flush(&c->ioc);
    if (br_ssl_engine_current_state(&c->sc.eng) == BR_SSL_CLOSED) {
        int err = br_ssl_engine_last_error(&c->sc.eng);
        if (err != 0)
            fprintf(stderr, "xs: tls error %d for %s\n", err, hostname);
        free(c);
        return NULL;
    }

    return c;
}

int xs_tls_read(xs_tls_conn *conn, void *buf, int len) {
    return br_sslio_read(&conn->ioc, buf, len);
}

int xs_tls_write(xs_tls_conn *conn, const void *buf, int len) {
    int r = br_sslio_write_all(&conn->ioc, buf, len);
    if (r < 0) return -1;
    br_sslio_flush(&conn->ioc);
    return len;
}

void xs_tls_close(xs_tls_conn *conn) {
    if (!conn) return;
    br_sslio_close(&conn->ioc);
    close(conn->fd);
    free(conn);
}
