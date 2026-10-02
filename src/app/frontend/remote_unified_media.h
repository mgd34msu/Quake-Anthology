#ifndef QA_FRONTEND_REMOTE_UNIFIED_MEDIA_H
#define QA_FRONTEND_REMOTE_UNIFIED_MEDIA_H
#include "remote_unified.h"
#include "qa/material.h"
#include "qa/font.h"
#include "qa/q3_presentation.h"

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
bool frontend_unified_media_create(qa_frontend *, qa_executable_recipe *,
    frontend_unified_media **, qa_error *);
/* Each content row borrows the admitted product lookup policy; children are
 * real private media owners, independent of the local Source scene. */
bool frontend_unified_media_bank(frontend_unified_media *, const char *content,
    qa_scene_resources **, qa_material_library **, qa_font_library **,
    qa_audio_bank **, qa_error *);
bool frontend_unified_media_files(frontend_unified_media *, const char *content,
    qa_vfs **, const qa_product **, qa_error *);
bool frontend_unified_media_q3_assets(frontend_unified_media *, const char *content,
    qa_q3_presentation_assets **, qa_error *);
bool frontend_unified_media_q3_assets_read(const frontend_unified_media *, const char *content,
    qa_q3_presentation_assets **);
bool frontend_unified_media_model(frontend_unified_media *, const char *content,
    const char *path, qa_scene_family, const qa_scene_image_options *,
    frontend_unified_model *, qa_error *);
qa_scene_world *frontend_unified_media_world(const frontend_unified_media *);
bool frontend_unified_media_current(const frontend_unified_media *);
bool frontend_unified_media_visit(const frontend_unified_media *,
    const qa_application_content_visitor *, qa_error *);
bool frontend_unified_media_destroy(frontend_unified_media *, qa_error *);
#endif
