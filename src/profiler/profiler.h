#ifndef XS_PROFILER_H
#define XS_PROFILER_H
#include <stdio.h>

typedef struct XSProfiler XSProfiler;

XSProfiler *profiler_new(void);
void        profiler_free(XSProfiler *p);
void        profiler_set_sample_rate(XSProfiler *p, double rate);
void        profiler_start(XSProfiler *p);
void        profiler_stop(XSProfiler *p);
void        profiler_sample(XSProfiler *p, const char *fn, int line);
void        profiler_report(XSProfiler *p);
void        profiler_report_flamegraph(XSProfiler *p, FILE *out);

#endif
