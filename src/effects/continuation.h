#ifndef XS_CONTINUATION_H
#define XS_CONTINUATION_H

/* One-shot delimited continuations (setjmp/longjmp). */

#include <setjmp.h>

typedef struct XSContinuation XSContinuation;

#define CONT_STATE_FRESH    0
#define CONT_STATE_SAVED    1
#define CONT_STATE_RESUMED  2

struct XSContinuation {
    jmp_buf  perform_site;
    jmp_buf  handler_site;
    void    *resume_value;
    int      state;
    int      one_shot;
};

XSContinuation *continuation_new(void);
void continuation_free(XSContinuation *k);
int  continuation_save(XSContinuation *k);
void continuation_resume(XSContinuation *k, void *value);
void *continuation_get_value(XSContinuation *k);
int  continuation_is_spent(XSContinuation *k);
void continuation_reset(XSContinuation *k);
void continuation_set_one_shot(XSContinuation *k, int one_shot);

XSContinuation *continuation_capture(void);

#endif /* XS_CONTINUATION_H */
