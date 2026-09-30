#ifndef QA_FONT_WORLD_SAVE_H
#define QA_FONT_WORLD_SAVE_H
#include "qa/font.h"

typedef struct qa_font_world_checkpoint_refs {
    void *context;
    bool (*content_encode)(void *, uint64_t, uint64_t *, qa_error *);
    bool (*content_decode)(void *, uint64_t, uint64_t *, qa_error *);
} qa_font_world_checkpoint_refs;
/* Capture actual retained rows without pruning expiration, marking one-frame
 * entries observed, drawing or consulting a clock. Content identities are
 * qualified by the enclosing provider/presentation owner. Restore prepares all
 * rows before replacing the store, with no submission/snapshot callbacks. */
bool qa_font_world_store_checkpoint(const qa_font_world_store *,
    const qa_font_world_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_font_world_store_restore(qa_font_world_store *, qa_bytes,
    const qa_font_world_checkpoint_refs *, qa_error *);
#endif
