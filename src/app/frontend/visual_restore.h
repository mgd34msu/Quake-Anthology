#ifndef QA_FRONTEND_VISUAL_RESTORE_H
#define QA_FRONTEND_VISUAL_RESTORE_H
#include "internal.h"
#include "model_inventory.h"
#include "material_movies.h"
typedef struct frontend_visual_owner_view {
    qa_actor_owner owner;
    qa_game_family family;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_media_library *media;
    struct frontend_material_movies *shader_movies;
} frontend_visual_owner_view;
typedef struct frontend_visual_model_view {
    const char *path;
    const qa_resource *resource;
    const qa_model *model;
    qa_scene_model *scene;
} frontend_visual_model_view;
typedef struct frontend_visual_brush_view {
    const char *path;
    const qa_resource *resource;
    qa_scene_world *world;
} frontend_visual_brush_view;
size_t frontend_visual_owner_count(const qa_frontend *);
bool frontend_visual_owner_read(const qa_frontend *, size_t, frontend_visual_owner_view *);
bool frontend_visual_movie_source_read(qa_frontend *, size_t, frontend_material_movie_source *, qa_error *);
size_t frontend_visual_model_count(const qa_frontend *, size_t owner);
bool frontend_visual_model_read(const qa_frontend *, size_t owner, size_t ordinal, frontend_visual_model_view *);
size_t frontend_visual_brush_count(const qa_frontend *, size_t owner);
bool frontend_visual_brush_read(const qa_frontend *, size_t owner, size_t ordinal, frontend_visual_brush_view *);
bool frontend_visuals_idle(const qa_frontend *);
#endif
