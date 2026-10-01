#ifndef QA_Q3_PRESENTATION_MEDIA_SAVE_H
#define QA_Q3_PRESENTATION_MEDIA_SAVE_H
#include "qa/q3_presentation.h"
#include "qa/media_save.h"
#include "qa/cinematic_presentation_save.h"
typedef struct qa_q3_movie_checkpoint_refs {
    void *context;
    bool (*asset_encode)(void *, const qa_cinematic_asset *, uint64_t *, qa_error *);
    /* Resolves the real cache owner and qualifies the path's candidate content.
     * Returns one owned reference, including repeated references to an alias. */
    bool (*asset_decode)(void *, uint64_t, const char *path, qa_cinematic_asset **, qa_error *);
    qa_media_checkpoint_refs playback;
    qa_cinematic_image_checkpoint_refs publication;
} qa_q3_movie_checkpoint_refs;
/* Actual local movie slots and prepared source cache. Installed delegated
 * system movies require their external lifetime owner and are rejected by
 * this local owner. Restore keeps the installed heap presentation unchanged
 * until the complete stream, all slots and source aliases qualify. Failure
 * frees unadopted local owners without changing the restored engine queue;
 * success transfers normal movie/stream retirement to the presentation. */
bool qa_q3_presentation_media_checkpoint(const qa_q3_presentation *,
    const qa_q3_movie_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_q3_presentation_media_restore(qa_q3_presentation *,
    const qa_q3_movie_checkpoint_refs *, uint64_t audio_bus, double wall_milliseconds,
    qa_bytes, qa_error *);
#endif
