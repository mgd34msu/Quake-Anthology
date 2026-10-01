#ifndef QA_INPUT_PLATFORM_SAVE_H
#define QA_INPUT_PLATFORM_SAVE_H
#include "qa/input_platform.h"
#include "qa/input_haptic_save.h"
typedef struct qa_input_platform_restore_guard qa_input_platform_restore_guard;
typedef struct qa_input_platform_checkpoint_refs {
    void *context;
    bool (*seat_encode)(void *, const qa_input_seat *, uint64_t *, qa_error *);
    bool (*seat_decode)(void *, uint64_t, qa_input_seat **, qa_error *);
    qa_haptic_checkpoint_refs haptics;
} qa_input_platform_checkpoint_refs;
/* Actual isolated owner allocation. Settings, seats and callback contexts must
 * already exist. It creates no native endpoint, changes no SDL global state,
 * and invokes no output/routing/input callbacks. Keep it detached until the
 * complete saved platform state and native handoff are qualified. */
qa_input_platform *qa_input_platform_create_detached(const qa_input_platform_options *, qa_error *);
bool qa_input_platform_checkpoint(const qa_input_platform *, const qa_input_platform_checkpoint_refs *,
    qa_buffer *, qa_error *);
/* Decode only into a detached owner. The active platform keeps sole native
 * ownership; the guard borrows both heaps through the final publication cut.
 * Exact native endpoint identity and current SDL mode/sensor state must match.
 * No rescanning, MIDI reads, input release, routing or output callbacks run. */
bool qa_input_platform_restore(qa_input_platform *detached, const qa_input_platform *active,
    const qa_input_platform_checkpoint_refs *, qa_bytes,
    qa_input_platform_restore_guard **, qa_error *);
bool qa_input_platform_handoff_ready(const qa_input_platform_restore_guard *, qa_error *);
/* Recapture the genuine detached candidate before publication. Its guard must
 * still qualify the active native endpoints; only that retained native cut is
 * encoded alongside the candidate's actual private state and haptic holders. */
bool qa_input_platform_restore_checkpoint(const qa_input_platform_restore_guard *,
    const qa_input_platform_checkpoint_refs *, qa_buffer *, qa_error *);
void qa_input_platform_handoff(qa_input_platform_restore_guard *);
void qa_input_platform_restore_guard_destroy(qa_input_platform_restore_guard *);
bool qa_input_platform_context_rebind_ready(const qa_input_platform *, const void *current, qa_error *);
void qa_input_platform_context_rebind(qa_input_platform *, void *destination);
#endif
