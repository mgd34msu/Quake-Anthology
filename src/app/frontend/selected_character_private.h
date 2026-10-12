#ifndef QA_FRONTEND_SELECTED_CHARACTER_PRIVATE_H
#define QA_FRONTEND_SELECTED_CHARACTER_PRIVATE_H
#include "selected_character.h"
#include "../../presentation/q3_native/pose.h"
#include "qa/actors.h"
#include "qa/pool.h"
#include "qa/q3_assets_save.h"
#include <stdlib.h>
#include <string.h>

struct frontend_selected_character_pose {
    frontend_selected_character *owner;
    qa_actor_id actor;
    q3n_player_pose pose;
    size_t users;
    bool reset;
};
struct frontend_selected_character {
    frontend_selected_character *next;
    qa_frontend *frontend;
    frontend_selected_character_view view;
    qa_resource *resources[8];
    qa_vfs_acquisition receipts[8];
    qa_player_animation_config animation;
    qa_launch_instance_lease *appearance_lease;
    qa_arena pose_storage;
    qa_pool pose_records;
    qa_actor_index *pose_indexes;
    uint32_t pose_seats;
    bool admitting, restoring;
};
struct frontend_selected_character_output {
    frontend_selected_character_pose *pose;
    qa_application_selected_q3_character source;
    qa_q3_ref_entity parts[3];
    size_t count;
};
void frontend_selected_character_dispose(frontend_selected_character *);

#endif
