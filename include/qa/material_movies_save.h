#ifndef QA_MATERIAL_MOVIES_SAVE_H
#define QA_MATERIAL_MOVIES_SAVE_H
#include "qa/cinematic.h"
typedef struct qa_material_movie_record {
    uint64_t initial;
    qa_cinematic *playback;
    bool enabled;
} qa_material_movie_record;
typedef struct qa_material_movies_checkpoint_refs {
    void *context;
    bool (*movie_encode)(void *, const qa_cinematic *, uint64_t *, qa_error *);
    /* Returns an already-qualified unadopted movie; ownership transfers only
     * when the complete registry import succeeds. */
    bool (*movie_decode)(void *, uint64_t, qa_cinematic **, qa_error *);
    bool (*initial_encode)(void *, uint64_t, uint64_t *, qa_error *);
    bool (*initial_decode)(void *, uint64_t, uint64_t *, qa_error *);
    bool (*frame_encode)(void *, const qa_scene_frame *, uint64_t *, qa_error *);
    bool (*frame_decode)(void *, uint64_t, qa_scene_frame **, qa_error *);
} qa_material_movies_checkpoint_refs;
bool qa_material_movies_idle(const qa_material_movies *);
size_t qa_material_movies_count(const qa_material_movies *);
qa_scene_resources *qa_material_movies_resource_owner(const qa_material_movies *);
bool qa_material_movies_read(const qa_material_movies *, size_t, qa_material_movie_record *);
bool qa_material_movies_checkpoint(const qa_material_movies *,
    const qa_material_movies_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_material_movies_restore(qa_scene_resources *, qa_bytes,
    const qa_material_movies_checkpoint_refs *, qa_material_movies **, qa_error *);
bool qa_material_movies_publish_ready(const qa_material_movies *, qa_error *);
bool qa_material_movies_completed_ready(const qa_material_movies *, qa_error *);
void qa_material_movies_publish(qa_material_movies *);
#endif
