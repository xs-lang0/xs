#include "xs_tls.h"
#include "bearssl/bearssl.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

struct xs_tls_conn {
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    int fd;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
};

/* low-level socket I/O callbacks for BearSSL */
static int sock_read(void *ctx, unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    ssize_t n;
    for (;;) {
        n = read(fd, buf, len);
        if (n <= 0) return -1;
        return (int)n;
    }
}

static int sock_write(void *ctx, const unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    ssize_t n;
    for (;;) {
        n = write(fd, buf, len);
        if (n <= 0) return -1;
        return (int)n;
    }
}

xs_tls_conn *xs_tls_connect(int fd, const char *hostname) {
    xs_tls_conn *c = calloc(1, sizeof(xs_tls_conn));
    if (!c) return NULL;
    c->fd = fd;

    br_ssl_client_init_full(&c->sc, &c->xc, NULL, 0);
    br_ssl_engine_set_buffer(&c->sc.eng, c->iobuf, sizeof(c->iobuf), 1);
    br_ssl_client_reset(&c->sc, hostname, 0);
    br_sslio_init(&c->ioc, &c->sc.eng, sock_read, &c->fd, sock_write, &c->fd);

    /* force the handshake */
    br_sslio_flush(&c->ioc);
    if (br_ssl_engine_current_state(&c->sc.eng) == BR_SSL_CLOSED) {
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
