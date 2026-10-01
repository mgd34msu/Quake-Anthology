#ifndef QA_FRONTEND_VISUAL_RESTORE_H
#define QA_FRONTEND_VISUAL_RESTORE_H
#include "internal.h"
#include "model_inventory.h"
typedef struct frontend_visual_owner_view {
    qa_actor_owner owner;
    qa_scene_family family;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
} frontend_visual_owner_view;
typedef struct frontend_visual_model_view {
    const char *path;
    const qa_resource *resource;
    const qa_model *model;
    qa_scene_model *scene;
} frontend_visual_model_view;
size_t frontend_visual_owner_count(const qa_frontend *);
bool frontend_visual_owner_read(const qa_frontend *, size_t, frontend_visual_owner_view *);
size_t frontend_visual_model_count(const qa_frontend *, size_t owner);
bool frontend_visual_model_read(const qa_frontend *, size_t owner, size_t ordinal, frontend_visual_model_view *);
bool frontend_visuals_idle(const qa_frontend *);
/* Transfers scene ownership on success and retains the exact resource and a
 * stable parsed-holder token from the restored immutable inventory.
 * Call in saved physical cache order, before candidate activation. */
bool frontend_visual_model_attach_restored(qa_frontend *, size_t owner, const char *path,
    const qa_resource *, const qa_model *, qa_scene_model *, frontend_model_inventory *, qa_error *);
/* Constructor metadata only. Real resource/material/model private state is
 * imported separately before activation. Provider factories need not exist
 * yet; keys resolve in the restored foundation string table without interning. */
bool frontend_visual_topology_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_visual_prepare_restored(qa_frontend *, qa_bytes, qa_error *);
bool frontend_visual_topology_ready(const qa_frontend *, qa_error *);
#endif
