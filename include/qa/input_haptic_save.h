#ifndef QA_INPUT_HAPTIC_SAVE_H
#define QA_INPUT_HAPTIC_SAVE_H
#include "qa/input.h"
#include "qa/vfs.h"

typedef struct qa_haptic_checkpoint_refs {
    void *context;
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *, uint64_t *, qa_error *);
    bool (*resource_decode)(void *, uint64_t, uint64_t, const qa_resource **, qa_error *);
} qa_haptic_checkpoint_refs;
/* Captures the actual cache and all of its installed players as one holder
 * graph. External pattern holders require their own coordinated owner. Restore
 * retains qualified content and preserves installed player sinks/contexts;
 * it never plays, stops, updates, or emits rumble. */
bool qa_haptic_checkpoint(const qa_haptic_cache *, qa_haptic_player *const *, size_t,
    const qa_haptic_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_haptic_restore(qa_haptic_cache *, qa_haptic_player *const *, size_t,
    const qa_haptic_checkpoint_refs *, qa_bytes, qa_error *);
#endif
