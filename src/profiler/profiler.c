#define _POSIX_C_SOURCE 200809L
#include "profiler/profiler.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#define MAX_SAMPLES 65536

typedef struct {
    const char *fn_name;
    int  line;
    int  count;
} ProfileSample;

struct XSProfiler {
    ProfileSample samples[MAX_SAMPLES];
    int  n_samples;
    int  running;
    double sample_rate;
    struct timespec start_time;
    struct timespec end_time;
    const char *current_fn;
    int  current_line;
};

static XSProfiler *g_profiler = NULL;

static unsigned int xorshift_state = 1;
static unsigned int xorshift32(void) {
    unsigned int x = xorshift_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    xorshift_state = x;
    return x;
}

static void sigprof_handler(int sig) {
    (void)sig;
    if (g_profiler && g_profiler->running) {
        if (g_profiler->sample_rate > 0.0 && g_profiler->sample_rate < 1.0) {
            double r = (double)(xorshift32() & 0xFFFF) / 65535.0;
            if (r > g_profiler->sample_rate) return;
        }
        profiler_sample(g_profiler, g_profiler->current_fn, g_profiler->current_line);
    }
}

XSProfiler *profiler_new(void) {
    XSProfiler *p = calloc(1, sizeof(XSProfiler));
    return p;
}

void profiler_free(XSProfiler *p) {
    if (!p) return;
    if (p->running) profiler_stop(p);
    if (g_profiler == p) g_profiler = NULL;
    free(p);
}

void profiler_set_sample_rate(XSProfiler *p, double rate) {
    if (!p) return;
    if (rate < 0.0) rate = 0.0;
    if (rate > 1.0) rate = 1.0;
    p->sample_rate = rate;
}

void profiler_start(XSProfiler *p) {
    if (!p) return;

    p->running = 1;
    p->n_samples = 0;
    p->current_fn = "<main>";
    p->current_line = 0;
    clock_gettime(CLOCK_MONOTONIC, &p->start_time);

    g_profiler = p;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigprof_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);

    struct itimerval timer;
    timer.it_interval.tv_sec  = 0;
    timer.it_interval.tv_usec = 1000;
    timer.it_value.tv_sec     = 0;
    timer.it_value.tv_usec    = 1000;
    setitimer(ITIMER_PROF, &timer, NULL);
}

void profiler_stop(XSProfiler *p) {
    if (!p) return;

    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_PROF, &timer, NULL);

    signal(SIGPROF, SIG_DFL);

    p->running = 0;
    clock_gettime(CLOCK_MONOTONIC, &p->end_time);

    if (g_profiler == p) g_profiler = NULL;
}

void profiler_sample(XSProfiler *p, const char *fn, int line) {
    if (!p) return;

    p->current_fn = fn;
    p->current_line = line;

    const char *name = fn ? fn : "<unknown>";

    for (int i = 0; i < p->n_samples; i++) {
        if (p->samples[i].line == line &&
            p->samples[i].fn_name &&
            strcmp(p->samples[i].fn_name, name) == 0) {
            p->samples[i].count++;
            return;
        }
    }

    if (p->n_samples < MAX_SAMPLES) {
        p->samples[p->n_samples].fn_name = name;
        p->samples[p->n_samples].line    = line;
        p->samples[p->n_samples].count   = 1;
        p->n_samples++;
    }
}

static int sample_cmp_desc(const void *a, const void *b) {
    const ProfileSample *sa = (const ProfileSample *)a;
    const ProfileSample *sb = (const ProfileSample *)b;
    return sb->count - sa->count;
}

void profiler_report(XSProfiler *p) {
    if (!p) return;

    double duration = (double)(p->end_time.tv_sec - p->start_time.tv_sec)
                    + (double)(p->end_time.tv_nsec - p->start_time.tv_nsec) / 1e9;

    int total = 0;
    for (int i = 0; i < p->n_samples; i++) {
        total += p->samples[i].count;
    }

    qsort(p->samples, (size_t)p->n_samples, sizeof(ProfileSample), sample_cmp_desc);

    fprintf(stderr, "\n=== XS Profiler Report ===\n");
    fprintf(stderr, "Duration: %.3fs\n", duration);
    fprintf(stderr, "Samples: %d\n\n", total);
    fprintf(stderr, "  %%time    samples  function                line\n");
    fprintf(stderr, "  ------   -------  ----------------------  ----\n");

    int limit = p->n_samples < 20 ? p->n_samples : 20;
    for (int i = 0; i < limit; i++) {
        double pct = total > 0 ? 100.0 * (double)p->samples[i].count / (double)total : 0.0;
        fprintf(stderr, "  %5.1f%%   %7d  %-22s  %d\n",
            pct,
            p->samples[i].count,
            p->samples[i].fn_name ? p->samples[i].fn_name : "<unknown>",
            p->samples[i].line);
    }

    if (p->n_samples > 20) {
        fprintf(stderr, "  ... and %d more entries\n", p->n_samples - 20);
    }
    fprintf(stderr, "\n");
}

/* Folded stack format for FlameGraph / speedscope.
 * Only flat hits -- no nested stacks without a shadow call stack. */
void profiler_report_flamegraph(XSProfiler *p, FILE *out) {
    if (!p || !out) return;

    qsort(p->samples, (size_t)p->n_samples, sizeof(ProfileSample), sample_cmp_desc);

    for (int i = 0; i < p->n_samples; i++) {
        const char *name = p->samples[i].fn_name ? p->samples[i].fn_name : "<unknown>";
        fprintf(out, "%s:%d %d\n", name, p->samples[i].line, p->samples[i].count);
    }
}
