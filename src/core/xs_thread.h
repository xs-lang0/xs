/* xs_thread.h: cross-platform threads (Win32 / pthreads) */
#ifndef XS_THREAD_H
#define XS_THREAD_H

#include <stdlib.h>

#if defined(_WIN32) || defined(__MINGW32__)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef HANDLE          xs_thread_t;
typedef CRITICAL_SECTION xs_mutex_t;

/* bridge POSIX-style callback to Win32 thread entry */
typedef struct {
    void *(*fn)(void *);
    void  *arg;
} xs_thread_trampoline_t;

static DWORD WINAPI xs_thread_trampoline_entry(LPVOID param) {
    xs_thread_trampoline_t *tramp = (xs_thread_trampoline_t *)param;
    void *(*fn)(void *) = tramp->fn;
    void *arg           = tramp->arg;
    free(tramp);
    fn(arg);
    return 0;
}

static inline int xs_thread_create(xs_thread_t *t, void *(*fn)(void *), void *arg) {
    xs_thread_trampoline_t *tramp = (xs_thread_trampoline_t *)malloc(sizeof(*tramp));
    if (!tramp) return -1;
    tramp->fn  = fn;
    tramp->arg = arg;
    HANDLE h = CreateThread(NULL, 0, xs_thread_trampoline_entry, tramp, 0, NULL);
    if (!h) {
        free(tramp);
        return -1;
    }
    *t = h;
    return 0;
}

static inline int xs_thread_join(xs_thread_t t, void **retval) {
    (void)retval;
    DWORD r = WaitForSingleObject(t, INFINITE);
    if (r != WAIT_OBJECT_0) return -1;
    CloseHandle(t);
    return 0;
}

static inline int xs_thread_detach(xs_thread_t t) {
    return CloseHandle(t) ? 0 : -1;
}

static inline unsigned long xs_thread_self_id(void) {
    return (unsigned long)GetCurrentThreadId();
}


static inline int xs_mutex_init(xs_mutex_t *m) {
    InitializeCriticalSection(m);
    return 0;
}

static inline int xs_mutex_lock(xs_mutex_t *m) {
    EnterCriticalSection(m);
    return 0;
}

static inline int xs_mutex_unlock(xs_mutex_t *m) {
    LeaveCriticalSection(m);
    return 0;
}

static inline int xs_mutex_trylock(xs_mutex_t *m) {
    return TryEnterCriticalSection(m) ? 0 : -1;
}

static inline int xs_mutex_destroy(xs_mutex_t *m) {
    DeleteCriticalSection(m);
    return 0;
}

static inline void xs_thread_sleep_ns(double secs) {
    DWORD ms = (DWORD)(secs * 1000.0);
    if (ms == 0 && secs > 0) ms = 1;
    Sleep(ms);
}

#else /* POSIX */

#include <pthread.h>
#include <time.h>

typedef pthread_t       xs_thread_t;
typedef pthread_mutex_t xs_mutex_t;

static inline int xs_thread_create(xs_thread_t *t, void *(*fn)(void *), void *arg) {
    return pthread_create(t, NULL, fn, arg);
}

static inline int xs_thread_join(xs_thread_t t, void **retval) {
    return pthread_join(t, retval);
}

static inline int xs_thread_detach(xs_thread_t t) {
    return pthread_detach(t);
}

static inline unsigned long xs_thread_self_id(void) {
    return (unsigned long)(uintptr_t)pthread_self();
}


static inline int xs_mutex_init(xs_mutex_t *m) {
    return pthread_mutex_init(m, NULL);
}

static inline int xs_mutex_lock(xs_mutex_t *m) {
    return pthread_mutex_lock(m);
}

static inline int xs_mutex_unlock(xs_mutex_t *m) {
    return pthread_mutex_unlock(m);
}

static inline int xs_mutex_trylock(xs_mutex_t *m) {
    return pthread_mutex_trylock(m);
}

static inline int xs_mutex_destroy(xs_mutex_t *m) {
    return pthread_mutex_destroy(m);
}

static inline void xs_thread_sleep_ns(double secs) {
    struct timespec ts;
    ts.tv_sec  = (time_t)secs;
    ts.tv_nsec = (long)((secs - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

#endif /* _WIN32 || __MINGW32__ */

#define XS_HAS_THREADS 1

#endif /* XS_THREAD_H */
