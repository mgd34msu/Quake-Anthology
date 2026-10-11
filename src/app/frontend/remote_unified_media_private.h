#ifndef QA_FRONTEND_REMOTE_UNIFIED_MEDIA_PRIVATE_H
#define QA_FRONTEND_REMOTE_UNIFIED_MEDIA_PRIVATE_H
#include "internal.h"
#include "remote_unified_media.h"
#include "model_inventory.h"

typedef struct unified_media_bank {
    struct unified_media_bank *next;
    char *content;
    qa_vfs *files;
    const qa_product *product;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_media_library *media;
    struct frontend_material_movies *shader_movies;
    uint64_t cinematic_audio_owner;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_q3_presentation_assets *q3_assets;
    qa_buffer saved_assets;
    bool saved_map, assets_restored, constructing, construction_failed;
} unified_media_bank;
typedef struct unified_media_model {
    struct unified_media_model *next;
    unified_media_bank *bank;
    qa_string_id path;
    qa_game_family family;
    qa_scene_image_options options;
    uint8_t *palette, *translation;
    qa_resource *resource;
    qa_vfs_acquisition opening;
    qa_model decoded;
    const qa_model *source;
    frontend_model_lease *source_lease;
    uint64_t saved_model, saved_scene, saved_world;
    qa_scene_model *scene;
    qa_scene_world *world;
} unified_media_model;
struct frontend_unified_media {
    qa_frontend *frontend;
    qa_executable_recipe *recipe;
    unified_media_bank *banks;
    unified_media_model *models;
    qa_scene_world *world;
    unified_media_bank *world_bank;
    frontend_world_scratch world_scratch;
    uint64_t saved_world;
    uint32_t physical_seat;
    bool busy, importing, roots_attached;
};
#endif
