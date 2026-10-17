#ifndef QA_FRONTEND_EQUIPMENT_MEDIA_PRIVATE_H
#define QA_FRONTEND_EQUIPMENT_MEDIA_PRIVATE_H

#include "equipment_media.h"
#include "qa/id_index.h"

typedef struct frontend_held_lookup {
    qa_strings *strings;
    qa_vfs *files;
    const char *path;
    uint64_t lookup_generation;
    bool archive_only, probed, found;
} frontend_held_lookup;
typedef struct frontend_q1_wheel_source {
    frontend_visual_owner_view owner;
    qa_resource *resource;
    qa_arena storage;
    frontend_q1_wheel_slot *slots;
    size_t count;
    uint64_t lookup_generation;
} frontend_q1_wheel_source;

struct frontend_equipment_media {
    struct frontend_equipment_media *next;
    qa_actor_owner provider;
    qa_actor_owner gear_namespace;
    uint64_t gear_service_owner;
    qa_game_family family;
    qa_item_id item;
    qa_string_id view_path;
    qa_strings *strings;
    frontend_visual_owner_view owner;
    frontend_visual_model_view view, held_parent;
    frontend_held_declaration declaration;
    frontend_held_model held;
    qa_scene_model *held_scene;
    frontend_model_lease *held_lease;
    size_t users;
    size_t saved_owner, saved_view, saved_parent;
    bool restoring, bound;
    bool source_slot;
    uint64_t source_generation;
    qa_buffer source_held;
    qa_buffer source_icon;
    const qa_material *icon;
    qa_resource *icon_source;
    size_t saved_icon;
    qa_model *source_model;
    qa_frontend *frontend;
    qa_media_library *media;
    frontend_material_movies *movies;
};
struct frontend_equipment {
    frontend_equipment_media *media, *tail;
    qa_arena held_storage;
    qa_id_index held_lookups;
    qa_id_index q1_hud_pictures;
    qa_id_index q1_wheels;
    qa_id_index native_hud_icons;
    const qa_strings *q1_hud_strings;
    bool admitting;
};

bool frontend_equipment_media_dispose(frontend_equipment_media *,qa_error *);
bool frontend_equipment_media_namespace_current(const qa_frontend *, const frontend_equipment_media *);
frontend_material_movie_source frontend_equipment_media_movie_source(frontend_equipment_media *);

#endif
