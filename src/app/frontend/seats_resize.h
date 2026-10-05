#ifndef QA_FRONTEND_SEATS_RESIZE_H
#define QA_FRONTEND_SEATS_RESIZE_H
#include "qa/frontend.h"
#include "qa/input_platform.h"

typedef struct frontend_seats_resize_state {
    unsigned previous;
    bool started, composed;
    qa_error failure;
} frontend_seats_resize_state;

/* The current startup request owns this bookkeeping, never another queue or
 * physical release pointer. Returned input WAIT retains the old seat count. */
frontend_seats_resize_state *frontend_startup_launch_resize_state(qa_frontend *, unsigned next);
bool frontend_seats_resize(qa_frontend *, unsigned next, bool *complete, qa_error *);
/* Preserve logical players while replacing only changed dense physical slots.
 * old_slots is the current request's actual new-slot to old-slot mapping. */
bool frontend_seats_recompose(qa_frontend *,unsigned next,unsigned first,
    const int old_slots[QA_INPUT_LOCAL_SEATS],bool *complete,qa_error *);
#endif
