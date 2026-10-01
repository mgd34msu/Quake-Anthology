#ifndef QA_FRONTEND_WHEELS_SAVE_H
#define QA_FRONTEND_WHEELS_SAVE_H
#include "internal.h"
#include "qa/hud_wheel_save.h"

/* The actual frontend wheel producer uses shared inventory item strings as
 * keys. Their foundation ordinals survive actor/provider retirement. Decode
 * only qualifies those saved strings; it observes no items or selection. */
qa_hud_wheel_checkpoint_refs frontend_wheel_refs(frontend_seat *);
#endif
