#ifndef QA_FRONTEND_MATERIAL_MOVIES_H
#define QA_FRONTEND_MATERIAL_MOVIES_H
#include "qa/frontend.h"
#include "qa/cinematic.h"
#include "qa/material.h"

typedef struct frontend_material_movies frontend_material_movies;
typedef struct frontend_material_movie_source {
    qa_frontend *frontend;
    qa_vfs *files;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_media_library *media;
    void *context;
    /* Reads the genuine provider's retained constructor/live/candidate tuple.
     * It neither dispatches a source callback nor grants teardown authority. */
    bool (*current)(void *, const struct frontend_material_movie_source *);
} frontend_material_movie_source;
typedef struct frontend_material_movie_view {
    const char *path;
    const qa_cinematic_asset *asset;
    const qa_scene_image *initial_image;
    const qa_cinematic *playback;
    const qa_scene_frame *initial_publication;
    uint64_t target;
    bool failed;
    const qa_error *failure;
} frontend_material_movie_view;

/* Install before material registration. The parent keeps the exact provider,
 * shared media cache and frontend wall-clock owner alive until destruction. */
bool frontend_material_movies_create(const frontend_material_movie_source *,
    frontend_material_movies **, qa_error *);
bool frontend_material_movies_destroy(frontend_material_movies **, qa_error *);
bool frontend_material_movies_idle(const frontend_material_movies *);
bool frontend_material_movies_current(const frontend_material_movies *);
bool frontend_material_movies_library_owner(const qa_material_library *,
    frontend_material_movies **, qa_error *);
bool frontend_material_movies_roster_count(const qa_frontend *, size_t *, qa_error *);
bool frontend_material_movies_roster_at(const qa_frontend *, size_t,
    frontend_material_movies **, qa_error *);
bool frontend_material_movies_source_read(const frontend_material_movies *,
    frontend_material_movie_source *, qa_error *);
/* The real retained renderer capsule prepares its destination slot before
 * the retiring provider transfers custody. Both tuples keep the same resource
 * heaps, media cache, frontend clock and reached-stage playback owner. */
bool frontend_material_movies_transfer(frontend_material_movies **source_slot,
    const frontend_material_movie_source *expected_source,
    const frontend_material_movie_source *destination,
    frontend_material_movies **destination_slot, qa_error *);
const qa_scene_image *frontend_material_movies_start(void *, const char *, qa_error *);
/* Frame admission only binds the true submitted frame. Playback advances at
 * a reached video stage, using the frontend monotonic media wall clock. */
bool frontend_material_movies_frame(frontend_material_movies *, qa_scene_frame *, qa_error *);
const qa_scene_image *frontend_material_movies_resolve(void *, uint64_t, double, qa_error *);
/* Mixed world/model draws route a reached initial image through the actual
 * frontend's retained provider owners. The context is that exact frontend. */
const qa_scene_image *frontend_material_movies_frontend_resolve(void *, uint64_t, double, qa_error *);
size_t frontend_material_movies_count(const frontend_material_movies *);
/* Includes genuine failed path receipts; those have no playback/image/asset. */
bool frontend_material_movies_read(const frontend_material_movies *, size_t,
    frontend_material_movie_view *);
#endif
