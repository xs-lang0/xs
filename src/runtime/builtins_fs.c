#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "core/xs_compat.h"
#include "runtime/interp.h"
#include "runtime/builtins.h"
#include "core/value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#if !defined(__MINGW32__) && !defined(__wasi__)
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <dirent.h>
#  include <glob.h>
#  if defined(__linux__)
#    include <sys/inotify.h>
#  endif
#  if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
      defined(__OpenBSD__) || defined(__DragonFly__)
#    include <sys/event.h>
#  endif
#elif defined(_WIN32)
#  include <windows.h>
#endif

/* fs module */

static Value *native_fs_read(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_NULL_VAL);
    FILE *f = fopen(a[0]->s, "r");
    if (!f) return value_incref(XS_NULL_VAL);
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = xs_malloc(sz + 1);
    long nr = (long)fread(buf, 1, sz, f); fclose(f); buf[nr] = '\0';
    Value *v = xs_str(buf); free(buf); return v;
}

static Value *native_fs_write(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_STR || VAL_TAG(a[1]) != XS_STR) return value_incref(XS_FALSE_VAL);
    FILE *f = fopen(a[0]->s, "w");
    if (!f) return value_incref(XS_FALSE_VAL);
    fputs(a[1]->s, f); fclose(f);
    return value_incref(XS_TRUE_VAL);
}

static Value *native_fs_append(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_STR || VAL_TAG(a[1]) != XS_STR) return value_incref(XS_FALSE_VAL);
    FILE *f = fopen(a[0]->s, "a");
    if (!f) return value_incref(XS_FALSE_VAL);
    fputs(a[1]->s, f); fclose(f);
    return value_incref(XS_TRUE_VAL);
}

static Value *native_fs_exists(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_FALSE_VAL);
    return (access(a[0]->s, F_OK) == 0) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_remove(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_FALSE_VAL);
    return (unlink(a[0]->s) == 0) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_mkdir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_FALSE_VAL);
    int r = xs_io_mkdirs(a[0]->s);
    return (r == 0 || errno == EEXIST) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_ls(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_array_new();
    DIR *d = opendir(a[0]->s); if (!d) return xs_array_new();
    Value *arr = xs_array_new();
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        array_push(arr->arr, xs_str(ent->d_name));
    }
    closedir(d); return arr;
}

static Value *native_fs_is_dir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_FALSE_VAL);
    struct stat st; if (stat(a[0]->s, &st) != 0) return value_incref(XS_FALSE_VAL);
    return S_ISDIR(st.st_mode) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_is_file(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_FALSE_VAL);
    struct stat st; if (stat(a[0]->s, &st) != 0) return value_incref(XS_FALSE_VAL);
    return S_ISREG(st.st_mode) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_size(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_int(-1);
    struct stat st; if (stat(a[0]->s, &st) != 0) return xs_int(-1);
    return xs_int((int64_t)st.st_size);
}

static Value *native_fs_stat(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_NULL_VAL);
    struct stat st;
    if (stat(a[0]->s, &st) != 0) return value_incref(XS_NULL_VAL);
    XSMap *m = map_new();
    map_take(m, "size", xs_int((int64_t)st.st_size));
    map_set(m, "is_file", S_ISREG(st.st_mode) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL));
    map_set(m, "is_dir", S_ISDIR(st.st_mode) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL));
    map_take(m, "mtime", xs_int((int64_t)st.st_mtime));
    return xs_module(m);
}

static Value *native_fs_read_bytes(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_array_new();
    FILE *f = fopen(a[0]->s, "rb");
    if (!f) return xs_array_new();
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = xs_malloc((size_t)sz);
    long nr = (long)fread(buf, 1, (size_t)sz, f); fclose(f);
    Value *arr = xs_array_new();
    for (long j = 0; j < nr; j++) array_push(arr->arr, xs_int(buf[j]));
    free(buf);
    return arr;
}

static Value *native_fs_write_bytes(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_STR || VAL_TAG(a[1]) != XS_ARRAY) return value_incref(XS_FALSE_VAL);
    FILE *f = fopen(a[0]->s, "wb");
    if (!f) return value_incref(XS_FALSE_VAL);
    XSArray *arr = a[1]->arr;
    for (int j = 0; j < arr->len; j++) {
        uint8_t b = (VAL_TAG(arr->items[j]) == XS_INT) ? (uint8_t)VAL_INT(arr->items[j]) : 0;
        fwrite(&b, 1, 1, f);
    }
    fclose(f);
    return value_incref(XS_TRUE_VAL);
}

static Value *native_fs_mkdir_p(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_FALSE_VAL);
    int r = xs_io_mkdirs(a[0]->s);
    return (r == 0 || errno == EEXIST) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_rmdir(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_FALSE_VAL);
    return (rmdir(a[0]->s) == 0) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_rename(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_STR || VAL_TAG(a[1]) != XS_STR) return value_incref(XS_FALSE_VAL);
    return (rename(a[0]->s, a[1]->s) == 0) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

static Value *native_fs_copy(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_STR || VAL_TAG(a[1]) != XS_STR) return value_incref(XS_FALSE_VAL);
    FILE *src = fopen(a[0]->s, "rb"); if (!src) return value_incref(XS_FALSE_VAL);
    FILE *dst = fopen(a[1]->s, "wb"); if (!dst) { fclose(src); return value_incref(XS_FALSE_VAL); }
    char buf[4096]; size_t nr;
    while ((nr = fread(buf, 1, sizeof buf, src)) > 0) fwrite(buf, 1, nr, dst);
    fclose(src); fclose(dst);
    return value_incref(XS_TRUE_VAL);
}

static Value *native_fs_join(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1) return xs_str("");
    int total = 0;
    for (int j = 0; j < n; j++) {
        if (VAL_TAG(a[j]) != XS_STR) continue;
        total += (int)strlen(a[j]->s) + 1;
    }
    char *res = xs_malloc(total + 1);
    char *p = res;
    for (int j = 0; j < n; j++) {
        if (VAL_TAG(a[j]) != XS_STR) continue;
        if (p > res && p[-1] != '/') *p++ = '/';
        size_t slen = strlen(a[j]->s);
        memcpy(p, a[j]->s, slen);
        p += slen;
    }
    *p = '\0';
    Value *v = xs_str(res); free(res); return v;
}

static Value *native_fs_basename(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_str("");
    const char *s = a[0]->s;
    const char *last = strrchr(s, '/');
    return xs_str(last ? last + 1 : s);
}

static Value *native_fs_dirname(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_str(".");
    const char *s = a[0]->s;
    const char *last = strrchr(s, '/');
    if (!last) return xs_str(".");
    if (last == s) return xs_str("/");
    return xs_str_n(s, last - s);
}

static Value *native_fs_ext(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_str("");
    const char *s = a[0]->s;
    const char *base = strrchr(s, '/');
    const char *dot = strrchr(base ? base : s, '.');
    return dot ? xs_str(dot) : xs_str("");
}

static Value *native_fs_abs(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_str("");
    char buf[4096];
#ifdef _WIN32
    if (_fullpath(buf, a[0]->s, sizeof(buf))) return xs_str(buf);
#else
    if (realpath(a[0]->s, buf)) return xs_str(buf);
#endif
    return value_incref((Value*)a[0]);
}

static Value *native_fs_temp_dir(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
    const char *t = getenv("TMPDIR");
#ifdef _WIN32
    if (!t) t = getenv("TEMP");
    if (!t) t = getenv("TMP");
#endif
    if (!t) t = "/tmp";
    char buf[4096];
#ifdef _WIN32
    if (_fullpath(buf, t, sizeof(buf))) return xs_str(buf);
#else
    if (realpath(t, buf)) return xs_str(buf);
#endif
    return xs_str(t);
}

static Value *native_fs_temp_file(Interp *ig, Value **a, int n) {
    (void)ig; (void)a; (void)n;
#ifdef __MINGW32__
    /* mingw: use mkstemp from POSIX layer */
    const char *t = getenv("TMPDIR");
    if (!t) t = getenv("TEMP");
    if (!t) t = getenv("TMP");
    if (!t) t = "/tmp";
    char tmpl[4096];
    snprintf(tmpl, sizeof(tmpl), "%.*s/xs_tmp_XXXXXX",
             (int)(sizeof(tmpl) - 24), t);
    int fd = mkstemp(tmpl);
    if (fd < 0) return xs_str("");
    close(fd);
    return xs_str(tmpl);
#elif defined(_WIN32)
    const char *t = getenv("TEMP");
    if (!t) t = getenv("TMP");
    if (!t) t = ".";
    char tmpl[4096];
    snprintf(tmpl, sizeof(tmpl), "%.*s/xs_tmp_XXXXXX",
             (int)(sizeof(tmpl) - 24), t);
    if (_mktemp(tmpl)) { FILE *f = fopen(tmpl, "w"); if (f) fclose(f); }
    else return xs_str("");
    return xs_str(tmpl);
#else
    const char *t = getenv("TMPDIR");
    if (!t) t = "/tmp";
    char resolved[4096];
    if (realpath(t, resolved)) t = resolved;
    char tmpl[4096];
    snprintf(tmpl, sizeof(tmpl), "%.*s/xs_tmp_XXXXXX",
             (int)(sizeof(tmpl) - 24), t);
    int fd = mkstemp(tmpl);
    if (fd < 0) return xs_str("");
    close(fd);
    return xs_str(tmpl);
#endif
}

/* fs.read_stream(path) - returns a reader map with read/read_line/read_all/close */
static Value *native_fs_reader_read(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE)) return value_incref(XS_NULL_VAL);
    Value *fdv = map_get(a[0]->map, "_fd");
    if (!fdv || VAL_TAG(fdv) != XS_INT) return value_incref(XS_NULL_VAL);
    FILE *f = (FILE*)(uintptr_t)VAL_INT(fdv);
    if (!f) return value_incref(XS_NULL_VAL);
    int count = 4096;
    if (n >= 2 && VAL_TAG(a[1]) == XS_INT) count = (int)VAL_INT(a[1]);
    if (count <= 0) count = 1;
    if (count > 1048576) count = 1048576;
    char *buf = xs_malloc((size_t)count + 1);
    size_t nr = fread(buf, 1, (size_t)count, f);
    if (nr == 0) { free(buf); return value_incref(XS_NULL_VAL); }
    buf[nr] = '\0';
    Value *v = xs_str_n(buf, nr); free(buf); return v;
}

static Value *native_fs_reader_read_line(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE)) return value_incref(XS_NULL_VAL);
    Value *fdv = map_get(a[0]->map, "_fd");
    if (!fdv || VAL_TAG(fdv) != XS_INT) return value_incref(XS_NULL_VAL);
    FILE *f = (FILE*)(uintptr_t)VAL_INT(fdv);
    if (!f) return value_incref(XS_NULL_VAL);
    int cap = 256; char *buf = xs_malloc(cap); int pos = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') break;
        if (pos + 1 >= cap) { cap *= 2; buf = xs_realloc(buf, cap); }
        buf[pos++] = (char)c;
    }
    if (pos == 0 && c == EOF) { free(buf); return value_incref(XS_NULL_VAL); }
    if (pos > 0 && buf[pos-1] == '\r') pos--;
    buf[pos] = '\0';
    Value *v = xs_str(buf); free(buf); return v;
}

static Value *native_fs_reader_read_all(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE)) return value_incref(XS_NULL_VAL);
    Value *fdv = map_get(a[0]->map, "_fd");
    if (!fdv || VAL_TAG(fdv) != XS_INT) return value_incref(XS_NULL_VAL);
    FILE *f = (FILE*)(uintptr_t)VAL_INT(fdv);
    if (!f) return value_incref(XS_NULL_VAL);
    int cap = 4096; char *buf = xs_malloc(cap); int pos = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (pos + 1 >= cap) { cap *= 2; buf = xs_realloc(buf, cap); }
        buf[pos++] = (char)c;
    }
    buf[pos] = '\0';
    Value *v = xs_str(buf); free(buf); return v;
}

static Value *native_fs_reader_close(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE)) return value_incref(XS_NULL_VAL);
    Value *fdv = map_get(a[0]->map, "_fd");
    if (!fdv || VAL_TAG(fdv) != XS_INT) return value_incref(XS_NULL_VAL);
    FILE *f = (FILE*)(uintptr_t)VAL_INT(fdv);
    if (f) { fclose(f); map_take(a[0]->map, "_fd", xs_int(0)); }
    return value_incref(XS_NULL_VAL);
}

static Value *native_fs_read_stream(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_NULL_VAL);
    FILE *f = fopen(a[0]->s, "r");
    if (!f) return value_incref(XS_NULL_VAL);
    XSMap *m = map_new();
    map_take(m, "_fd", xs_int((int64_t)(uintptr_t)f));
    map_take(m, "read", xs_native(native_fs_reader_read));
    map_take(m, "read_line", xs_native(native_fs_reader_read_line));
    map_take(m, "read_all", xs_native(native_fs_reader_read_all));
    map_take(m, "close", xs_native(native_fs_reader_close));
    return xs_module(m);
}

/* fs.write_stream(path) - returns a writer map with write/flush/close */
static Value *native_fs_writer_write(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE)) return value_incref(XS_FALSE_VAL);
    Value *fdv = map_get(a[0]->map, "_fd");
    if (!fdv || VAL_TAG(fdv) != XS_INT) return value_incref(XS_FALSE_VAL);
    FILE *f = (FILE*)(uintptr_t)VAL_INT(fdv);
    if (!f) return value_incref(XS_FALSE_VAL);
    if (VAL_TAG(a[1]) == XS_STR) fputs(a[1]->s, f);
    return value_incref(XS_TRUE_VAL);
}

static Value *native_fs_writer_flush(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || (VAL_TAG(a[0]) != XS_MAP && VAL_TAG(a[0]) != XS_MODULE)) return value_incref(XS_NULL_VAL);
    Value *fdv = map_get(a[0]->map, "_fd");
    if (!fdv || VAL_TAG(fdv) != XS_INT) return value_incref(XS_NULL_VAL);
    FILE *f = (FILE*)(uintptr_t)VAL_INT(fdv);
    if (f) fflush(f);
    return value_incref(XS_NULL_VAL);
}

static Value *native_fs_write_stream(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_NULL_VAL);
    FILE *f = fopen(a[0]->s, "w");
    if (!f) return value_incref(XS_NULL_VAL);
    XSMap *m = map_new();
    map_take(m, "_fd", xs_int((int64_t)(uintptr_t)f));
    map_take(m, "write", xs_native(native_fs_writer_write));
    map_take(m, "flush", xs_native(native_fs_writer_flush));
    map_take(m, "close", xs_native(native_fs_reader_close));
    return xs_module(m);
}

/* fs.read_lines(path) - returns array of lines */
static Value *native_fs_read_lines(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_array_new();
    FILE *f = fopen(a[0]->s, "r");
    if (!f) return xs_array_new();
    Value *arr = xs_array_new();
    char buf[8192];
    while (fgets(buf, sizeof(buf), f)) {
        int len = (int)strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
        array_push(arr->arr, xs_str(buf));
    }
    fclose(f);
    return arr;
}

/* fs.walk(path) - recursive directory walker, returns array of entry maps */
static void fs_walk_recurse(const char *dir, Value *arr) {
#if !defined(__wasi__)
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char fullpath[4096];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, ent->d_name);
        struct stat st;
        if (stat(fullpath, &st) != 0) continue;
        Value *entry = xs_map_new();
        Value *pv = xs_str(fullpath); map_set(entry->map, "path", pv); value_decref(pv);
        Value *nv = xs_str(ent->d_name); map_set(entry->map, "name", nv); value_decref(nv);
        int isdir = S_ISDIR(st.st_mode);
        int isfile = S_ISREG(st.st_mode);
        map_set(entry->map, "is_file", isfile ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL));
        map_set(entry->map, "is_dir", isdir ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL));
        Value *sv = xs_int((int64_t)st.st_size); map_set(entry->map, "size", sv); value_decref(sv);
        array_push(arr->arr, entry);
        value_decref(entry);
        if (isdir) fs_walk_recurse(fullpath, arr);
    }
    closedir(d);
#else
    (void)dir; (void)arr;
#endif
}

static Value *native_fs_walk(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_array_new();
    Value *arr = xs_array_new();
    fs_walk_recurse(a[0]->s, arr);
    return arr;
}

/* fs.glob(pattern) - match files by glob pattern */
static Value *native_fs_glob(Interp *ig, Value **a, int n) {
    (void)ig;
#if !defined(__wasi__)
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_array_new();
    glob_t g;
    memset(&g, 0, sizeof(g));
    int rc = glob(a[0]->s, GLOB_NOSORT, NULL, &g);
    Value *arr = xs_array_new();
    if (rc == 0) {
        for (size_t j = 0; j < g.gl_pathc; j++)
            array_push(arr->arr, xs_str(g.gl_pathv[j]));
    }
    globfree(&g);
    return arr;
#else
    (void)a; (void)n;
    return xs_array_new();
#endif
}

/* fs.chmod(path, mode) */
static Value *native_fs_chmod(Interp *ig, Value **a, int n) {
    (void)ig;
#if !defined(__MINGW32__) && !defined(__wasi__)
    if (n < 2 || VAL_TAG(a[0]) != XS_STR || VAL_TAG(a[1]) != XS_INT) return value_incref(XS_FALSE_VAL);
    return (chmod(a[0]->s, (mode_t)VAL_INT(a[1])) == 0) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
#else
    (void)a; (void)n;
    return value_incref(XS_FALSE_VAL);
#endif
}

/* fs.symlink(target, link_path) */
static Value *native_fs_symlink(Interp *ig, Value **a, int n) {
    (void)ig;
#if !defined(__MINGW32__) && !defined(__wasi__)
    if (n < 2 || VAL_TAG(a[0]) != XS_STR || VAL_TAG(a[1]) != XS_STR) return value_incref(XS_FALSE_VAL);
    return (symlink(a[0]->s, a[1]->s) == 0) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
#else
    (void)a; (void)n;
    return value_incref(XS_FALSE_VAL);
#endif
}

/* fs.readlink(path) */
static Value *native_fs_readlink(Interp *ig, Value **a, int n) {
    (void)ig;
#if !defined(__MINGW32__) && !defined(__wasi__)
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_NULL_VAL);
    char buf[4096];
    ssize_t len = readlink(a[0]->s, buf, sizeof(buf) - 1);
    if (len < 0) return value_incref(XS_NULL_VAL);
    buf[len] = '\0';
    return xs_str(buf);
#else
    (void)a; (void)n;
    return value_incref(XS_NULL_VAL);
#endif
}

/* fs.realpath(path) */
static Value *native_fs_realpath(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return value_incref(XS_NULL_VAL);
    char buf[4096];
#ifdef _WIN32
    if (_fullpath(buf, a[0]->s, sizeof(buf))) return xs_str(buf);
#else
    if (realpath(a[0]->s, buf)) return xs_str(buf);
#endif
    return value_incref(XS_NULL_VAL);
}

/* fs.watch(path, callback) - single-shot watcher that blocks until the
   first filesystem event, calls callback({type, name}), then returns.
   Linux uses inotify, macOS/BSD use kqueue/EVFILT_VNODE, Windows uses
   ReadDirectoryChangesW. Other platforms return false. */
static Value *native_fs_watch(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_STR ||
        (VAL_TAG(a[1]) != XS_FUNC && VAL_TAG(a[1]) != XS_NATIVE))
        return value_incref(XS_FALSE_VAL);

#if defined(__linux__) && !defined(__wasi__)
    int ifd = inotify_init();
    if (ifd < 0) return value_incref(XS_FALSE_VAL);
    int wd = inotify_add_watch(ifd, a[0]->s,
        IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVE);
    if (wd < 0) { close(ifd); return value_incref(XS_FALSE_VAL); }
    char evbuf[4096];
    ssize_t len = read(ifd, evbuf, sizeof(evbuf));
    if (len > 0) {
        struct inotify_event *ev = (struct inotify_event *)evbuf;
        Value *info = xs_map_new();
        const char *etype = "unknown";
        if (ev->mask & IN_CREATE) etype = "create";
        else if (ev->mask & IN_DELETE) etype = "delete";
        else if (ev->mask & IN_MODIFY) etype = "modify";
        else if (ev->mask & IN_MOVE) etype = "move";
        Value *tv = xs_str(etype); map_set(info->map, "type", tv); value_decref(tv);
        if (ev->len > 0) {
            Value *nv = xs_str(ev->name);
            map_set(info->map, "name", nv); value_decref(nv);
        }
        Value *cb_args[1] = { info };
        Value *r = call_value(ig, a[1], cb_args, 1, "fs.watch");
        if (r) value_decref(r);
        value_decref(info);
    }
    inotify_rm_watch(ifd, wd);
    close(ifd);
    return value_incref(XS_TRUE_VAL);

#elif (defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
       defined(__OpenBSD__) || defined(__DragonFly__)) && !defined(__wasi__)
    /* O_EVTONLY is darwin-only and gated behind _DARWIN_C_SOURCE; on
     * the BSDs (and on darwin without that feature macro) plain
     * O_RDONLY does the right thing for kqueue's EVFILT_VNODE. */
    #ifndef O_EVTONLY
    #define O_EVTONLY O_RDONLY
    #endif
    int kq = kqueue();
    if (kq < 0) return value_incref(XS_FALSE_VAL);
    int fd = open(a[0]->s, O_EVTONLY);
    if (fd < 0) { close(kq); return value_incref(XS_FALSE_VAL); }
    struct kevent reg, out;
    EV_SET(&reg, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_DELETE | NOTE_EXTEND | NOTE_RENAME | NOTE_ATTRIB,
           0, NULL);
    if (kevent(kq, &reg, 1, NULL, 0, NULL) < 0) {
        close(fd); close(kq); return value_incref(XS_FALSE_VAL);
    }
    if (kevent(kq, NULL, 0, &out, 1, NULL) > 0) {
        const char *etype = "unknown";
        if (out.fflags & NOTE_DELETE) etype = "delete";
        else if (out.fflags & NOTE_RENAME) etype = "move";
        else if (out.fflags & (NOTE_WRITE | NOTE_EXTEND)) etype = "modify";
        else if (out.fflags & NOTE_ATTRIB) etype = "modify";
        Value *info = xs_map_new();
        Value *tv = xs_str(etype); map_set(info->map, "type", tv); value_decref(tv);
        /* kqueue doesn't report filenames inside a watched directory */
        Value *cb_args[1] = { info };
        Value *r = call_value(ig, a[1], cb_args, 1, "fs.watch");
        if (r) value_decref(r);
        value_decref(info);
    }
    close(fd); close(kq);
    return value_incref(XS_TRUE_VAL);

#elif defined(_WIN32) && !defined(__wasi__)
    HANDLE dir = CreateFileA(a[0]->s, FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (dir == INVALID_HANDLE_VALUE) return value_incref(XS_FALSE_VAL);
    /* align on DWORD per ReadDirectoryChangesW requirements */
    unsigned char buf[4096] __attribute__((aligned(sizeof(DWORD))));
    DWORD bytes = 0;
    BOOL ok = ReadDirectoryChangesW(dir, buf, sizeof(buf), FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
        &bytes, NULL, NULL);
    if (ok && bytes > 0) {
        FILE_NOTIFY_INFORMATION *fni = (FILE_NOTIFY_INFORMATION *)buf;
        const char *etype = "unknown";
        switch (fni->Action) {
            case FILE_ACTION_ADDED:
            case FILE_ACTION_RENAMED_NEW_NAME: etype = "create"; break;
            case FILE_ACTION_REMOVED:
            case FILE_ACTION_RENAMED_OLD_NAME: etype = "delete"; break;
            case FILE_ACTION_MODIFIED:         etype = "modify"; break;
        }
        /* convert UTF-16 filename to UTF-8 */
        int wlen = (int)(fni->FileNameLength / sizeof(WCHAR));
        int need = WideCharToMultiByte(CP_UTF8, 0, fni->FileName, wlen,
                                       NULL, 0, NULL, NULL);
        char *name = (char *)malloc((size_t)need + 1);
        if (name) {
            WideCharToMultiByte(CP_UTF8, 0, fni->FileName, wlen,
                                name, need, NULL, NULL);
            name[need] = '\0';
        }
        Value *info = xs_map_new();
        Value *tv = xs_str(etype); map_set(info->map, "type", tv); value_decref(tv);
        if (name) {
            Value *nv = xs_str(name); map_set(info->map, "name", nv); value_decref(nv);
            free(name);
        }
        Value *cb_args[1] = { info };
        Value *r = call_value(ig, a[1], cb_args, 1, "fs.watch");
        if (r) value_decref(r);
        value_decref(info);
    }
    CloseHandle(dir);
    return value_incref(XS_TRUE_VAL);

#else
    (void)a; (void)n;
    return value_incref(XS_FALSE_VAL);
#endif
}


extern Value *http_do_request(const char *method, const char *url, XSMap *extra_headers, const char *body, size_t body_len);

Value *make_fs_module(void) {
    XSMap *m = map_new();
    map_take(m, "read",         xs_native(native_fs_read));
    map_take(m, "read_bytes",   xs_native(native_fs_read_bytes));
    map_take(m, "write",        xs_native(native_fs_write));
    map_take(m, "write_bytes",  xs_native(native_fs_write_bytes));
    map_take(m, "append",       xs_native(native_fs_append));
    map_take(m, "exists",       xs_native(native_fs_exists));
    map_take(m, "remove",       xs_native(native_fs_remove));
    map_take(m, "mkdir",        xs_native(native_fs_mkdir));
    map_take(m, "mkdir_p",      xs_native(native_fs_mkdir_p));
    map_take(m, "rmdir",        xs_native(native_fs_rmdir));
    map_take(m, "list",         xs_native(native_fs_ls));
    map_take(m, "ls",           xs_native(native_fs_ls));
    map_take(m, "is_dir",       xs_native(native_fs_is_dir));
    map_take(m, "is_file",      xs_native(native_fs_is_file));
    map_take(m, "size",         xs_native(native_fs_size));
    map_take(m, "stat",         xs_native(native_fs_stat));
    map_take(m, "rename",       xs_native(native_fs_rename));
    map_take(m, "copy",         xs_native(native_fs_copy));
    map_take(m, "join",         xs_native(native_fs_join));
    map_take(m, "basename",     xs_native(native_fs_basename));
    map_take(m, "dirname",      xs_native(native_fs_dirname));
    map_take(m, "ext",          xs_native(native_fs_ext));
    map_take(m, "abs",          xs_native(native_fs_abs));
    map_take(m, "temp_dir",     xs_native(native_fs_temp_dir));
    map_take(m, "temp_file",    xs_native(native_fs_temp_file));
    map_take(m, "read_stream",  xs_native(native_fs_read_stream));
    map_take(m, "write_stream", xs_native(native_fs_write_stream));
    map_take(m, "read_lines",   xs_native(native_fs_read_lines));
    map_take(m, "walk",         xs_native(native_fs_walk));
    map_take(m, "glob",         xs_native(native_fs_glob));
    map_take(m, "chmod",        xs_native(native_fs_chmod));
    map_take(m, "symlink",      xs_native(native_fs_symlink));
    map_take(m, "readlink",     xs_native(native_fs_readlink));
    map_take(m, "realpath",     xs_native(native_fs_realpath));
    map_take(m, "watch",        xs_native(native_fs_watch));
    return xs_module(m);
}
