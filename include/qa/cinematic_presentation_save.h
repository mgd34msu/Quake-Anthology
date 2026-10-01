#ifndef QA_CINEMATIC_PRESENTATION_SAVE_H
#define QA_CINEMATIC_PRESENTATION_SAVE_H
#include "qa/cinematic.h"
#include "qa/source_save.h"
typedef struct qa_cinematic_image_checkpoint_refs {
    void *context;
    bool (*encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    /* Returns a borrowed immutable version in the qualified image inventory. */
    bool (*decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
} qa_cinematic_image_checkpoint_refs;
/* Playback/decoder state is restored separately. These fields preserve the
 * actual retained publication version and frame binding without emitting a
 * command or constructing a replacement image. Restore requires the genuine
 * unadopted owner from qa_cinematic_restore_qualified; commit only after the
 * enclosing complete stream qualifies. Both codecs hold actual owner activity
 * through reference resolution and publication. */
bool qa_cinematic_presentation_checkpoint(const qa_cinematic *, const qa_scene_frame *,
    const qa_cinematic_image_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_cinematic_presentation_restore(qa_cinematic *, const qa_scene_frame *,
    const qa_cinematic_image_checkpoint_refs *, qa_bytes, qa_error *);
#endif
