#ifndef QA_FRONTEND_RENDERER_MATERIALS_H
#define QA_FRONTEND_RENDERER_MATERIALS_H
#include "internal.h"
#include "qa/persistence_content.h"
#include "material_movies.h"
typedef struct frontend_renderer_materials frontend_renderer_materials;
typedef struct frontend_renderer_materials_view {
    qa_material_library *library;
    qa_scene_resources *images;
    qa_vfs *mounts;
    qa_scene_resources *lightmap_images;
    qa_vfs *lightmap_mounts;
    qa_media_library *media;
    frontend_material_movies *movies;
    bool has_cinematics;
    uint32_t cinematic_seat;
    uint64_t cinematic_bus;
} frontend_renderer_materials_view;
/* Retained shader and lightmap parents without an ordinary owner are present.
 * Existing provider banks and libraries keep their original namespace. */
bool frontend_renderer_materials_count(const qa_frontend *,size_t *,qa_error *);
bool frontend_renderer_materials_read_at(const qa_frontend *,size_t,frontend_renderer_materials_view *,qa_error *);
bool frontend_renderer_materials_movie_count(const qa_frontend *,size_t *,qa_error *);
bool frontend_renderer_materials_movie_source_at(qa_frontend *,size_t,frontend_material_movie_source *,qa_error *);
bool frontend_renderer_materials_prune(qa_frontend *,qa_error *);
bool frontend_renderer_materials_destroy(frontend_renderer_materials **,qa_error *);
bool frontend_renderer_materials_idle(const frontend_renderer_materials *);
bool frontend_renderer_materials_checkpoint(qa_frontend *,qa_buffer *,qa_error *);
bool frontend_renderer_materials_prepare_restored(qa_frontend *,qa_bytes,qa_error *);
bool frontend_renderer_materials_bind_restored(qa_frontend *,qa_error *);
bool frontend_renderer_materials_adopt_movies(qa_frontend *,const frontend_material_movie_source *,
    frontend_material_movies **,qa_media_library **,qa_error *);
#endif
