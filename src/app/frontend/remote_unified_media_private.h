#ifndef QA_FRONTEND_REMOTE_UNIFIED_MEDIA_PRIVATE_H
#define QA_FRONTEND_REMOTE_UNIFIED_MEDIA_PRIVATE_H
#include "internal.h"
#include "remote_unified_media.h"

typedef struct unified_media_bank {
    struct unified_media_bank *next;
    char *content;
    qa_vfs *files;
    const qa_product *product;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_q3_presentation_assets *q3_assets;
} unified_media_bank;
typedef struct unified_media_model {
    struct unified_media_model *next;
    unified_media_bank *bank;
    char *path;
    qa_scene_family family;
    qa_scene_image_options options;
    uint8_t *palette, *translation;
    qa_resource *resource;
    qa_vfs_acquisition opening;
    qa_model decoded;
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
    bool busy;
};
#endif
