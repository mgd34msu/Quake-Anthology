#ifndef QA_FRONTEND_VISUALS_PRIVATE_H
#define QA_FRONTEND_VISUALS_PRIVATE_H

#include "visual_access.h"
#include "qa/network_q1_nq.h"
#include "qa/network_q1_qw.h"

typedef struct frontend_visual_content {
    size_t references;
    const qa_vfs *files;
    qa_resource *resource, *animation_resource, *scale_resource;
    qa_model model;
    qa_model_animation animation;
} frontend_visual_content;
typedef struct frontend_model {
    struct frontend_model *next;
    qa_resource *resource;
    qa_model owned_model;
    frontend_visual_content *content, *replacement;
    qa_model_replacement replacement_description;
    int64_t source_rank;
    bool source_rank_known;
    const qa_model *model;
    frontend_model_lease *lease;
    qa_scene_model *scene;
    qa_string_id path_name;
    const char *path;
} frontend_model;
typedef struct frontend_brush {
    struct frontend_brush *next;
    qa_resource *resource;
    qa_scene_world *world;
    qa_string_id path_name;
    const char *path;
} frontend_brush;
typedef struct visual_model_recipe {
    char *path;
    const qa_resource *resource;
    int64_t rank;
    bool known;
} visual_model_recipe;
typedef struct frontend_static_model {
    struct frontend_static_model *next;
    frontend_model *model;
    frontend_brush *brush;
    qa_model_transform transform;
    qa_q1_entity baseline;
    uint32_t inline_model;
    bool is_inline;
} frontend_static_model;
typedef struct frontend_flare_image {
    struct frontend_flare_image *next;
    qa_string_id name;
    qa_scene_image *image;
    bool standard;
} frontend_flare_image;
typedef struct frontend_visual_cache {
    qa_arena storage;
    qa_id_index owners;
} frontend_visual_cache;

struct frontend_visual_owner {
    frontend_visual_owner *next;
    frontend_visual_cache *cache;
    qa_arena storage;
    qa_id_index model_index, brush_index, flare_index;
    qa_actor_owner owner;
    qa_game_family family;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_frontend *frontend;
    qa_strings *strings;
    qa_media_library *media;
    frontend_material_movies *shader_movies;
    frontend_model *models;
    frontend_brush *brushes;
    frontend_flare_image *flares;
    frontend_static_model *statics, *static_tail;
    qa_nq_decoder *static_nq;
    qa_qw_decoder *static_qw;
    uint64_t static_revision;
    size_t static_signon;
    visual_model_recipe *recipes;
    visual_model_recipe *brush_recipes;
    size_t recipe_count, attached;
    size_t brush_recipe_count, brushes_attached;
    bool recipes_present, construction_failed;
};
#endif
