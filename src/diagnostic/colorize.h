#ifndef XS_DIAG_COLORIZE_H
#define XS_DIAG_COLORIZE_H

#include <stddef.h>

/* ANSI-highlight one line of XS source into 'out'. Respects g_no_color. */
void diag_colorize_line(const char *line, char *out, size_t outsz);

#endif
