#ifndef QA_FRONTEND_EQUIPMENT_MEDIA_PRIVATE_H
#define QA_FRONTEND_EQUIPMENT_MEDIA_PRIVATE_H

#include "equipment_media.h"

struct frontend_equipment_media {
    struct frontend_equipment_media *next;
    qa_actor_owner provider;
    qa_game_family family;
    qa_item_id item;
    char *view_path, *saved_parent_path;
    frontend_visual_owner_view owner;
    frontend_visual_model_view view, held_parent;
    frontend_held_declaration declaration;
    frontend_held_model held;
    qa_scene_model *held_scene;
    frontend_model_lease *held_lease;
    size_t users;
    size_t saved_owner, saved_view, saved_parent;
    bool restoring, bound;
};
struct frontend_equipment {
    frontend_equipment_media *media, *tail;
    bool admitting;
};

void frontend_equipment_media_dispose(frontend_equipment_media *);

#endif
