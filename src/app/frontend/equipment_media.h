#ifndef QA_FRONTEND_EQUIPMENT_MEDIA_H
#define QA_FRONTEND_EQUIPMENT_MEDIA_H

#include "visual_access.h"
#include "equipment_held.h"
#include "qa/application_equipment.h"

typedef struct frontend_equipment_media frontend_equipment_media;
typedef struct frontend_equipment_media_view {
    qa_actor_owner provider;
    qa_game_family family;
    qa_item_id item;
    const char *view_path;
    frontend_visual_owner_view owner;
    frontend_visual_model_view view, held_parent;
    const frontend_held_declaration *declaration;
    const frontend_held_model *held;
    qa_scene_model *held_scene;
} frontend_equipment_media_view;

/* Live admission creates actual retained media only. Import attaches saved
 * physical inventories separately; it never calls this constructor. */
bool frontend_equipment_media_prepare(qa_frontend *, const qa_application_equipment_view *,
    frontend_equipment_media **, qa_error *);
bool frontend_equipment_media_read(const frontend_equipment_media *, frontend_equipment_media_view *);
bool frontend_equipment_idle(const qa_frontend *);
bool frontend_equipment_retire(qa_frontend *, qa_error *);
void frontend_equipment_destroy(qa_frontend *);
size_t frontend_equipment_media_count(const qa_frontend *);
bool frontend_equipment_media_at(const qa_frontend *, size_t, frontend_equipment_media_view *);

#endif
