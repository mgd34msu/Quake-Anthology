#ifndef QA_MATERIAL_MOVIES_SAVE_H
#define QA_MATERIAL_MOVIES_SAVE_H
#include "qa/cinematic.h"
typedef struct qa_material_movie_record {
    uint64_t initial;
    qa_cinematic *playback;
    bool enabled;
} qa_material_movie_record;
bool qa_material_movies_idle(const qa_material_movies *);
size_t qa_material_movies_count(const qa_material_movies *);
qa_scene_resources *qa_material_movies_resource_owner(const qa_material_movies *);
bool qa_material_movies_read(const qa_material_movies *, size_t, qa_material_movie_record *);
bool qa_material_movies_completed_ready(const qa_material_movies *, qa_error *);
#endif
