#ifndef QA_FRONTEND_MATERIAL_MOVIES_PRIVATE_H
#define QA_FRONTEND_MATERIAL_MOVIES_PRIVATE_H
#include "material_movies.h"
#include "internal.h"
#include "qa/cinematic_restore.h"
#include "qa/media_library_save.h"
#include "qa/material_library_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_movies_save.h"
#include "qa/media_library_prepare.h"

typedef struct frontend_material_movie_row {
    char *path;
    qa_cinematic_asset *asset;
    qa_cinematic *playback;
    const qa_scene_image *initial;
    qa_scene_frame publication;
    uint64_t target;
    bool failed;
    qa_error failure;
} frontend_material_movie_row;
typedef struct frontend_material_movie_cinematic_receipt {
    char *path;
    const qa_scene_image *image;
    int32_t handle;
} frontend_material_movie_cinematic_receipt;
struct frontend_material_movies {
    frontend_material_movie_source source;
    struct frontend_material_movies *next;
    frontend_material_movie_row **rows;
    size_t count, capacity;
    uint64_t next_target;
    qa_material_movies *registry;
    qa_q3_cinematic_source *cinematic_source;
    uint32_t cinematic_seat;
    uint64_t cinematic_bus;
    frontend_material_movie_cinematic_receipt *cinematic_receipts;
    size_t cinematic_count, cinematic_capacity;
    struct frontend_material_movies_policy *pending;
    bool busy, linked, cinematic_mode;
};
bool frontend_material_movie_source_valid(const frontend_material_movie_source *);
bool frontend_material_movie_link(frontend_material_movies *, qa_error *);
bool frontend_material_movie_unlink(frontend_material_movies *, qa_error *);
size_t frontend_material_movie_live_count(const frontend_material_movies *);
const qa_scene_image *frontend_material_movie_cached(const frontend_material_movie_row *, qa_error *);
double frontend_material_movie_clock(void *);
char *frontend_material_movie_path(const char *, qa_error *);
bool frontend_material_movie_path_valid(const char *);
qa_cinematic_options frontend_material_movie_options(frontend_material_movies *, uint64_t);
bool frontend_material_movie_row_create(frontend_material_movies *, qa_media_library *,
    qa_material_movies *, const char *, uint64_t, frontend_material_movie_row **, qa_error *);
void frontend_material_movie_row_free(frontend_material_movie_row *);
bool frontend_material_movie_rows_reserve(frontend_material_movie_row ***, size_t *, size_t, qa_error *);
bool frontend_material_movie_cinematic_reserve(frontend_material_movie_cinematic_receipt **,
    size_t *, size_t, qa_error *);
bool frontend_material_movie_cinematic_receipt_make(const char *,
    frontend_material_movie_cinematic_receipt *, qa_error *);
void frontend_material_movie_cinematic_receipt_free(frontend_material_movie_cinematic_receipt *);
#endif
