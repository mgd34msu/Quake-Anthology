#ifndef QA_FRONTEND_REMOTE_UNIFIED_MEDIA_H
#define QA_FRONTEND_REMOTE_UNIFIED_MEDIA_H
#include "remote_unified.h"
#include "qa/material.h"
#include "qa/font.h"
#include "qa/q3_presentation.h"
#include "qa/cinematic.h"

typedef struct frontend_unified_media frontend_unified_media;
typedef struct frontend_unified_model {
    const qa_resource *resource;
    const qa_vfs_acquisition *opening;
    const qa_model *model;
    qa_scene_model *scene;
    qa_scene_world *brush_world;
    uint32_t inline_model;
    bool is_inline;
} frontend_unified_model;
typedef struct frontend_unified_bank_view {
    const char *content;
    qa_string_id content_name;
    qa_vfs *files;
    const qa_product *product;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_q3_presentation_assets *q3_assets;
    qa_media_library *movies;
    uint32_t cinematic_seat;
    uint64_t cinematic_audio_owner;
} frontend_unified_bank_view;
typedef struct frontend_unified_model_view {
    size_t bank;
    const char *path;
    qa_game_family family;
    const qa_scene_image_options *options;
    const qa_resource *resource;
    const qa_vfs_acquisition *opening;
    const qa_model *model;
    qa_scene_model *scene;
    qa_scene_world *world;
} frontend_unified_model_view;
/* Physical list ordinals are preserved by cold import. Reads are also valid
 * on the isolated empty-bank prefix before shared dictionaries import. */
size_t frontend_unified_media_bank_count(const frontend_unified_media *);
bool frontend_unified_media_bank_read(const frontend_unified_media *, size_t, frontend_unified_bank_view *);
size_t frontend_unified_media_model_count(const frontend_unified_media *);
bool frontend_unified_media_model_read(const frontend_unified_media *, size_t, frontend_unified_model_view *);
qa_executable_recipe *frontend_unified_media_recipe(const frontend_unified_media *);
bool frontend_unified_media_importing(const frontend_unified_media *);
bool frontend_unified_media_create(qa_frontend *, qa_executable_recipe *, uint32_t physical_seat,
    frontend_unified_media **, qa_error *);
/* Each content row borrows the admitted product lookup policy; children are
 * real private media owners, independent of the local Source scene. */
bool frontend_unified_media_bank(frontend_unified_media *, qa_string_id content,
    qa_scene_resources **, qa_material_library **, qa_font_library **,
    qa_audio_bank **, qa_error *);
bool frontend_unified_media_files(frontend_unified_media *, qa_string_id content,
    qa_vfs **, const qa_product **, qa_error *);
bool frontend_unified_media_q3_assets(frontend_unified_media *, qa_string_id content,
    qa_q3_presentation_assets **, qa_error *);
bool frontend_unified_media_q3_assets_read(const frontend_unified_media *, const qa_product *,
    qa_q3_presentation_assets **);
bool frontend_unified_media_model(frontend_unified_media *, qa_string_id content,
    qa_string_id path, qa_game_family, const qa_scene_image_options *,
    frontend_unified_model *, qa_error *);
qa_material_library *frontend_unified_model_materials(const qa_scene_model *);
qa_scene_world *frontend_unified_media_world(const frontend_unified_media *);
void frontend_unified_media_world_scratch(const frontend_unified_media *, qa_scene_world_input *);
bool frontend_unified_media_current(const frontend_unified_media *);
bool frontend_unified_media_ready(const frontend_unified_media *);
bool frontend_unified_media_idle(const frontend_unified_media *);
bool frontend_unified_media_checkpoint_ready(const frontend_unified_media *);
bool frontend_unified_media_visit(const frontend_unified_media *,
    const qa_application_content_visitor *, qa_error *);
bool frontend_unified_media_destroy(frontend_unified_media *, qa_error *);
#endif
