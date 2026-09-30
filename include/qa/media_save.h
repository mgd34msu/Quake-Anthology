#ifndef QA_MEDIA_SAVE_H
#define QA_MEDIA_SAVE_H
#include "qa/cinematic.h"

typedef struct qa_media_checkpoint_refs {
    void *context;
    bool (*material_encode)(void *, uint64_t, qa_buffer *, qa_error *);
    bool (*material_decode)(void *, qa_bytes, uint64_t *, qa_error *);
} qa_media_checkpoint_refs;
/* These records own decoded buffers and source text. Decode does not open a
 * movie or dispatch output; the qualified cinematic constructor validates the
 * actual candidate content and reconnects its clock, audio and target owners. */
bool qa_cinematic_checkpoint_encode(const qa_cinematic_checkpoint *, const qa_media_checkpoint_refs *,
                                     qa_buffer *, qa_error *);
bool qa_cinematic_checkpoint_decode(qa_bytes, const qa_media_checkpoint_refs *,
                                     qa_cinematic_checkpoint *, qa_error *);
#endif
