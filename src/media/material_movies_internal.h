#ifndef QA_MATERIAL_MOVIES_INTERNAL_H
#define QA_MATERIAL_MOVIES_INTERNAL_H
#include "cinematic_internal.h"
#include "qa/id_index.h"

typedef struct material_movie {
    uint64_t initial;
    qa_cinematic *playback;
    bool enabled;
} material_movie;
struct qa_material_movies {
    qa_scene_resources *resources;
    /* Physical registration order survives imported image identity rebasing. */
    material_movie *movies;
    size_t count, capacity;
    qa_id_index images;
    qa_scene_frame *prepared;
    uint64_t sequence;
    struct qa_material_movies_stage *pending;
    bool busy, stage_sealed;
};
bool material_movies_index_build(qa_id_index *, const material_movie *, size_t, qa_error *);
#endif
