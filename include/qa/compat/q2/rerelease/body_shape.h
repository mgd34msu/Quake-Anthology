/* Port of src/compat/q2/rerelease/body-shape.ts. GPL-2.0-or-later.
 * The dimensions entry binding uses the runner mirror's bind_entry
 * with the movement-global check as the accepts predicate. */
#ifndef QA_COMPAT_Q2_RERELEASE_BODY_SHAPE_H
#define QA_COMPAT_Q2_RERELEASE_BODY_SHAPE_H
#include "qa/compat/q2/rerelease/module.h"
#include "qa/movement/body_shape.h"

/* body NULL is TS undefined; run is the TS () => undefined. */
void qa_compat_q2_with_rerelease_body_shape(qa_compat_q2_rerelease_guest_module *module, qa_guest_address address,
                                            const qa_movement_body_shape *body, void *run_ctx, void (*run)(void *));
#endif
