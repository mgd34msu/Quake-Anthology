#ifndef QA_FRONTEND_EQUIPMENT_MEDIA_SAVE_H
#define QA_FRONTEND_EQUIPMENT_MEDIA_SAVE_H

#include "equipment_media.h"

/* The constructor prefix retains exact resources and physical cache ordinals.
 * Actual decoded visual caches precede bind; actual scene/model inventories
 * precede attach. None of these operations runs acquisition or model builders. */
bool frontend_equipment_topology_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_equipment_prepare_restored(qa_frontend *, qa_bytes, qa_error *);
bool frontend_equipment_media_bind_restored(qa_frontend *, qa_error *);
/* Success consumes the scene and retains an independent parsed-holder token.
 * The caller transfers the matching genuine equipment root exactly once. */
bool frontend_equipment_media_attach_restored(qa_frontend *, size_t ordinal,
    const qa_model *, qa_scene_model *, frontend_model_inventory *, qa_error *);
bool frontend_equipment_topology_ready(const qa_frontend *, qa_error *);

#endif
