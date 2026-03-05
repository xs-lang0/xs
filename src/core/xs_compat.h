/* xs_compat.h — platform shims */
#ifndef XS_COMPAT_H
#define XS_COMPAT_H

#ifdef __MINGW32__
#ifndef __USE_MINGW_ANSI_STDIO
#  define __USE_MINGW_ANSI_STDIO 1
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS 1
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VC_EXTRA_LEAN
#include <windows.h>
#include <time.h>

#ifndef CLOCK_REALTIME
#  define CLOCK_REALTIME  0
#endif
#ifndef CLOCK_MONOTONIC
#  define CLOCK_MONOTONIC 1
#endif

#ifndef XS_CLOCK_GETTIME_DEFINED
#define XS_CLOCK_GETTIME_DEFINED
static inline int xs_clock_gettime(int clk_id, struct timespec *ts) {
    if (clk_id == CLOCK_MONOTONIC) {
        LARGE_INTEGER freq, count;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&count);
        ts->tv_sec  = (time_t)(count.QuadPart / freq.QuadPart);
        ts->tv_nsec = (long)(((count.QuadPart % freq.QuadPart) * 1000000000LL) / freq.QuadPart);
    } else {
        /* CLOCK_REALTIME */
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        unsigned long long ns100 =
            ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        /* Windows epoch (1601) → Unix epoch (1970) */
        ns100 -= 116444736000000000ULL;
        ts->tv_sec  = (time_t)(ns100 / 10000000ULL);
        ts->tv_nsec = (long)((ns100 % 10000000ULL) * 100);
    }
    return 0;
}
#undef  clock_gettime
#define clock_gettime xs_clock_gettime
#endif /* XS_CLOCK_GETTIME_DEFINED */

#include <dirent.h>

#include <stdio.h>
#include <process.h>
#ifndef getpid
#  define getpid _getpid
#endif
#ifndef getppid
#  define getppid() ((int)-1)
#endif

#ifndef XS_SETENV_DEFINED
#define XS_SETENV_DEFINED
static inline int xs_setenv(const char *name, const char *val, int overwrite) {
    (void)overwrite;
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s=%s", name, val);
    return _putenv(buf);
}
#define setenv(n,v,o) xs_setenv(n,v,o)
#define unsetenv(n)   _putenv_s(n,"")
#endif

#ifndef usleep
#  define usleep(us) Sleep((DWORD)((us)/1000))
#endif

#ifndef XS_NANOSLEEP_DEFINED
#define XS_NANOSLEEP_DEFINED
static inline int xs_nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    DWORD ms = (DWORD)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
    Sleep(ms);
    return 0;
}
#define nanosleep xs_nanosleep
#endif

/* glob stub via FindFirstFile */
#ifndef XS_GLOB_STUB_DEFINED
#define XS_GLOB_STUB_DEFINED
typedef struct {
    size_t    gl_pathc;
    char    **gl_pathv;
    size_t    gl_offs;
} glob_t;
#define GLOB_NOSORT 0
static inline int xs_glob(const char *pattern, int flags, void *unused,
                           glob_t *pglob) {
    (void)flags; (void)unused;
    pglob->gl_pathc = 0;
    pglob->gl_pathv = NULL;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    size_t cap = 8;
    pglob->gl_pathv = (char**)malloc(cap * sizeof(char*));
    do {
        if (pglob->gl_pathc >= cap) {
            cap *= 2;
            pglob->gl_pathv = (char**)realloc(pglob->gl_pathv, cap*sizeof(char*));
        }
        pglob->gl_pathv[pglob->gl_pathc++] = _strdup(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}
static inline void xs_globfree(glob_t *pglob) {
    for (size_t j = 0; j < pglob->gl_pathc; j++) free(pglob->gl_pathv[j]);
    free(pglob->gl_pathv);
    pglob->gl_pathv = NULL; pglob->gl_pathc = 0;
}
#define glob(p,f,e,g)  xs_glob(p,f,e,g)
#define globfree(g)    xs_globfree(g)
#endif /* XS_GLOB_STUB_DEFINED */

#ifndef _SC_NPROCESSORS_ONLN
#include <sysinfoapi.h>
static inline long xs_sysconf_nproc(void) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    return (long)si.dwNumberOfProcessors;
}
#define sysconf(x) xs_sysconf_nproc()
#define _SC_NPROCESSORS_ONLN 84
#endif

#include <direct.h>
#ifndef XS_MKDIR_DEFINED
#define XS_MKDIR_DEFINED
static inline int xs_mkdir(const char *path, int mode) {
    (void)mode;
    return _mkdir(path);
}
#define mkdir(p,m) xs_mkdir(p,m)
#endif

/* strptime shim */
#ifndef XS_STRPTIME_DEFINED
#define XS_STRPTIME_DEFINED
static inline char *xs_strptime(const char *s, const char *fmt, struct tm *tm) {
    const char *sp = s;
    const char *fp = fmt;
    int val, consumed;
    while (*fp) {
        if (*fp == '%') {
            fp++;
            switch (*fp) {
            case 'Y':
                if (sscanf(sp, "%4d%n", &val, &consumed) != 1) return NULL;
                tm->tm_year = val - 1900;
                sp += consumed;
                break;
            case 'm':
                if (sscanf(sp, "%2d%n", &val, &consumed) != 1) return NULL;
                if (val < 1 || val > 12) return NULL;
                tm->tm_mon = val - 1;
                sp += consumed;
                break;
            case 'd':
                if (sscanf(sp, "%2d%n", &val, &consumed) != 1) return NULL;
                if (val < 1 || val > 31) return NULL;
                tm->tm_mday = val;
                sp += consumed;
                break;
            case 'H':
                if (sscanf(sp, "%2d%n", &val, &consumed) != 1) return NULL;
                if (val < 0 || val > 23) return NULL;
                tm->tm_hour = val;
                sp += consumed;
                break;
            case 'M':
                if (sscanf(sp, "%2d%n", &val, &consumed) != 1) return NULL;
                if (val < 0 || val > 59) return NULL;
                tm->tm_min = val;
                sp += consumed;
                break;
            case 'S':
                if (sscanf(sp, "%2d%n", &val, &consumed) != 1) return NULL;
                if (val < 0 || val > 59) return NULL;
                tm->tm_sec = val;
                sp += consumed;
                break;
            case '%':
                if (*sp != '%') return NULL;
                sp++;
                break;
            default:
                return NULL;
            }
            fp++;
        } else {
            if (*sp != *fp) return NULL;
            sp++;
            fp++;
        }
    }
    return (char *)sp;
}
#define strptime xs_strptime
#endif

#else /* POSIX */
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <sys/stat.h>
#endif /* __MINGW32__ */

#endif /* XS_COMPAT_H */
