#ifndef QA_MEDIA_LIBRARY_SAVE_H
#define QA_MEDIA_LIBRARY_SAVE_H
#include "qa/cinematic.h"

typedef struct qa_media_library_checkpoint_refs {
    void *context;
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *, qa_error *);
    bool (*resource_decode)(void *, uint64_t, const qa_resource **, qa_error *);
    bool (*image_encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    bool (*image_decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
} qa_media_library_checkpoint_refs;
/* Resolvers qualify actual retained content/image versions in the enclosing
 * candidate owner graph. Restore preserves installed asset addresses, rebuilds
 * only immutable CIN/RoQ/OGV headers/tables from exact retained source bytes,
 * and binds PCX to an already-restored image. Playback remains a separate owner.
 * Reference counts are reconstructed by actual library/playback/source holders. */
bool qa_media_library_checkpoint(const qa_media_library *, const qa_media_library_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_media_library_restore(qa_media_library *, qa_bytes, const qa_media_library_checkpoint_refs *, qa_error *);
size_t qa_media_library_record_count(const qa_media_library *);
const qa_cinematic_asset *qa_media_library_record_at(const qa_media_library *, size_t);
qa_scene_resources *qa_media_library_resource_owner(const qa_media_library *);
#endif
