#ifndef QA_FRONTEND_SEATS_RESIZE_H
#define QA_FRONTEND_SEATS_RESIZE_H
#include "qa/frontend.h"

typedef struct frontend_seats_resize_state {
    unsigned previous;
    bool started;
    qa_error failure;
} frontend_seats_resize_state;

/* The current startup request owns this bookkeeping, never another queue or
 * physical release pointer. Returned input WAIT retains the old seat count. */
frontend_seats_resize_state *frontend_startup_launch_resize_state(qa_frontend *, unsigned next);
bool frontend_seats_resize(qa_frontend *, unsigned next, bool *complete, qa_error *);
#endif
