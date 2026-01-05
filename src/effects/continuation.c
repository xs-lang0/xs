#include "effects/continuation.h"

#include <stdlib.h>
#include <string.h>

XSContinuation *continuation_new(void) {
    XSContinuation *k = calloc(1, sizeof(XSContinuation));
    if (!k) return NULL;
    k->state    = CONT_STATE_FRESH;
    k->one_shot = 1;
    return k;
}

void continuation_free(XSContinuation *k) {
    free(k);
}

int continuation_save(XSContinuation *k) {
    if (!k) return -1;
    k->state = CONT_STATE_SAVED;
    return setjmp(k->perform_site);
}

void continuation_resume(XSContinuation *k, void *value) {
    if (!k) return;
    if (k->state != CONT_STATE_SAVED) {
        if (k->one_shot && k->state == CONT_STATE_RESUMED) return;
        if (k->state == CONT_STATE_FRESH) return;
    }
    k->state = CONT_STATE_RESUMED;
    k->resume_value = value;
    longjmp(k->perform_site, 1);
}

void *continuation_get_value(XSContinuation *k) {
    if (!k) return NULL;
    return k->resume_value;
}

int continuation_is_spent(XSContinuation *k) {
    if (!k) return 1;
    return k->state == CONT_STATE_RESUMED;
}

void continuation_reset(XSContinuation *k) {
    if (!k) return;
    k->state = CONT_STATE_FRESH;
    k->resume_value = NULL;
}

void continuation_set_one_shot(XSContinuation *k, int one_shot) {
    if (!k) return;
    k->one_shot = one_shot;
}

XSContinuation *continuation_capture(void) {
    return continuation_new();
}
