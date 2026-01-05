#ifndef XS_REPLAY_H
#define XS_REPLAY_H

typedef struct XSReplay XSReplay;

XSReplay *replay_new(const char *trace_path);
void      replay_free(XSReplay *r);
int       replay_step_forward(XSReplay *r);
int       replay_step_backward(XSReplay *r);
int       replay_run(const char *trace_path);

#endif
