#ifndef QA_FRONTEND_MATERIAL_MOVIES_SAVE_H
#define QA_FRONTEND_MATERIAL_MOVIES_SAVE_H
#include "material_movies.h"
#include "qa/cinematic_presentation_save.h"
#include "qa/scene_frame_save.h"
typedef struct frontend_material_movies_refs {
    void *context;
    bool (*asset_encode)(void *, const qa_cinematic_asset *, uint64_t *, qa_error *);
    /* Returns a borrowed asset from this real provider's imported cache and
     * qualified retained path/content binding; it never opens the path. */
    bool (*asset_decode)(void *, uint64_t, const char *, const qa_cinematic_asset **, qa_error *);
    /* The aggregate has already recreated the exact saved global scratch
     * bank/pool and the physical audio namespace. No provider replay occurs. */
    bool (*cinematic_encode)(void *, uint64_t audio_bus, qa_buffer *descriptor, qa_error *);
    bool (*cinematic_decode)(void *, uint32_t saved_seat, qa_bytes bus_descriptor,
        qa_q3_cinematic_handles **, uint32_t *actual_seat, uint64_t *actual_bus, qa_error *);
    qa_cinematic_image_checkpoint_refs images;
    qa_scene_frame_checkpoint_refs frames;
} frontend_material_movies_refs;
bool frontend_material_movies_checkpoint(const frontend_material_movies *,
    const frontend_material_movies_refs *, qa_buffer *, qa_error *);
/* Import the actual image/material/cache owners first. The real candidate
 * wall time already equals the saved anchor. No add, registration, tick or
 * provider initialization occurs while restoring this owner. */
bool frontend_material_movies_restore(const frontend_material_movie_source *,
    const frontend_material_movies_refs *, qa_bytes, frontend_material_movies **, qa_error *);
bool frontend_material_movies_publish_ready(const frontend_material_movies *, qa_error *);
void frontend_material_movies_publish(frontend_material_movies *);
#endif
