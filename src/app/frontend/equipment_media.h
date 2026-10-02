#ifndef QA_FRONTEND_EQUIPMENT_MEDIA_H
#define QA_FRONTEND_EQUIPMENT_MEDIA_H

#include "visual_access.h"
#include "equipment_held.h"
#include "qa/application_equipment.h"
#include "material_movies_save.h"

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
    qa_actor_owner gear_namespace;
    uint64_t gear_service_owner;
    bool source_slot;
    uint64_t source_generation;
    qa_bytes source_held;
    qa_bytes source_icon;
    const qa_material *icon;
    qa_media_library *media;
    frontend_material_movies *movies;
} frontend_equipment_media_view;

/* Live admission creates actual retained media only. Import attaches saved
 * physical inventories separately; it never calls this constructor. */
bool frontend_equipment_media_prepare(qa_frontend *, const qa_application_equipment_view *,
    frontend_equipment_media **, qa_error *);
/* A genuine authored Q3 declaration, including none, precedes native held
 * presentation. Absence leaves authored=false and creates no media row. */
bool frontend_equipment_media_prepare_q3_held(qa_frontend *, const qa_application_equipment_view *,
    frontend_equipment_media **, bool *authored, qa_error *);
bool frontend_equipment_media_prepare_source_held(qa_frontend *,const qa_application_equipment_view *,
    frontend_equipment_media **,bool *authored,qa_error *);
bool frontend_equipment_media_prepare_source_icon(qa_frontend *,const qa_application_equipment_view *,
    frontend_equipment_media **,const qa_material **,qa_error *);
bool frontend_equipment_media_source_icon_read(const qa_frontend *,const qa_application_equipment_view *,
    const qa_material **,qa_error *);
bool frontend_equipment_media_read(const frontend_equipment_media *, frontend_equipment_media_view *);
bool frontend_equipment_media_retain(frontend_equipment_media *, qa_error *);
void frontend_equipment_media_release(frontend_equipment_media *);
bool frontend_equipment_idle(const qa_frontend *);
bool frontend_equipment_retire(qa_frontend *, qa_error *);
bool frontend_equipment_media_prune(qa_frontend *,qa_error *);
void frontend_equipment_destroy(qa_frontend *);
size_t frontend_equipment_media_count(const qa_frontend *);
bool frontend_equipment_media_at(const qa_frontend *, size_t, frontend_equipment_media_view *);
size_t frontend_equipment_movie_count(const qa_frontend *);
bool frontend_equipment_movie_at(const qa_frontend *,size_t,size_t *physical_ordinal);
bool frontend_equipment_movie_source_read(qa_frontend *,size_t,
    frontend_material_movie_source *,qa_error *);
bool frontend_equipment_movies_restore(qa_frontend *,size_t,
    const frontend_material_movies_refs *,qa_bytes,qa_error *);

#endif
