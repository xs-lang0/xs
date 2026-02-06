#ifndef XS_TLS_H
#define XS_TLS_H

/* minimal TLS client wrapper around BearSSL */

typedef struct xs_tls_conn xs_tls_conn;

/* connect TLS over an existing socket fd. returns NULL on failure */
xs_tls_conn *xs_tls_connect(int fd, const char *hostname);

/* read/write through TLS. returns bytes transferred or -1 */
int xs_tls_read(xs_tls_conn *conn, void *buf, int len);
int xs_tls_write(xs_tls_conn *conn, const void *buf, int len);

/* close and free */
void xs_tls_close(xs_tls_conn *conn);

#endif
