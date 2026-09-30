#ifndef QA_HUD_WHEEL_SAVE_H
#define QA_HUD_WHEEL_SAVE_H
#include "qa/hud_wheel.h"
#include "qa/source_save.h"
typedef struct qa_hud_wheel_checkpoint_refs {
    void *context;
    /* Resolve actual source item identities, including retired provenance.
     * The source key is not necessarily a shared inventory numeric ID. */
    bool (*encode)(void *, qa_hud_wheel_mode, uint64_t, uint64_t *, qa_error *);
    bool (*decode)(void *, qa_hud_wheel_mode, uint64_t, uint64_t *, qa_error *);
} qa_hud_wheel_checkpoint_refs;
/* Item arrays are operation-scoped scratch: every later item read first calls
 * the installed observation service. Continuation preserves the actual wheel
 * and carousel selectors, clocks, analog/pointer and fade state without an
 * observation, selection or changed callback. */
bool qa_hud_wheel_checkpoint(const qa_hud_wheel *, const qa_hud_wheel_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_hud_wheel_restore(qa_hud_wheel *, const qa_hud_wheel_checkpoint_refs *, qa_bytes, qa_error *);
#endif
