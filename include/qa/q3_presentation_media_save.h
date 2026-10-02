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
    /* The external fullscreen owner encodes its actual retained source lease
     * and decoder. Decode returns one owned cold handle; it remains unpublished
     * until the enclosing frontend candidate is admitted. Discard releases only
     * an unadopted handle, without commands or candidate audio queue mutation. */
    bool (*system_encode)(void *, const qa_q3_system_movie *, uint32_t flags, qa_buffer *, qa_error *);
    bool (*system_decode)(void *, qa_bytes, uint32_t flags, qa_q3_system_movie *, qa_error *);
    void (*system_discard)(void *, qa_q3_system_movie *);
} qa_q3_movie_checkpoint_refs;
/* Actual movie slots and prepared source cache. Delegated system slots require
 * their external lifetime owner's qualified codec. Restore keeps the installed heap presentation unchanged
 * until the complete stream, all slots and source aliases qualify. Failure
 * frees unadopted local owners without changing the restored engine queue;
 * success transfers normal movie/stream retirement to the presentation. */
bool qa_q3_presentation_media_checkpoint(const qa_q3_presentation *,
    const qa_q3_movie_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_q3_presentation_media_checkpoint_schema(const qa_q3_presentation *,
    const qa_q3_movie_checkpoint_refs *,uint32_t schema,qa_buffer *,qa_error *);
bool qa_q3_presentation_media_restore(qa_q3_presentation *,
    const qa_q3_movie_checkpoint_refs *, uint64_t audio_bus, double wall_milliseconds,
    qa_bytes, qa_error *);
/* Reads only the actual codec header's source binding mode. Full slot/cache
 * admission remains the media restore owner's responsibility. */
bool qa_q3_presentation_media_binding_read(qa_bytes,bool *shared,qa_error *);
bool qa_q3_presentation_media_binding_version_read(qa_bytes,uint32_t *schema,bool *shared,qa_error *);
#endif
