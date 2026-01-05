// tracer.h -- records calls/stores/IO to .xst binary trace
#ifndef XS_TRACER_H
#define XS_TRACER_H

typedef struct XSTracer XSTracer;

XSTracer *tracer_new(const char *output_path);
void      tracer_free(XSTracer *t);
void      tracer_set_deep(XSTracer *t, int deep);
void      tracer_record_call(XSTracer *t, const char *fn, int line);
void      tracer_record_return(XSTracer *t, const char *fn, void *retval);
void      tracer_record_store(XSTracer *t, const char *var, void *val);
void      tracer_record_io(XSTracer *t, const char *op, void *data, int len);
void      tracer_record_branch(XSTracer *t, int line, int taken);
int       tracer_flush(XSTracer *t);

#endif
