#ifndef QA_PLAYER_STATE_H
#define QA_PLAYER_STATE_H

#include "qa/movement.h"

/* Current selected player state. The control owner stores one instance per
 * live actor; reads return temporary snapshots. Body/link publication and
 * completed command history remain separate domains. */
typedef struct qa_player_state {
    qa_actor_id actor;
    qa_movement_state state;
    qa_movement_profile profile;
    qa_bounds standing_bounds, bounds;
    qa_movement_ground ground;
    qa_vec3 view_angles, command_angles, view_offset;
    uint64_t command_sequence, command_angle_revision;
    uint32_t buttons, previous_buttons;
    int32_t water_level, water_type;
    float view_height, gravity_multiplier;
    qa_movement_mode player_mode;
    bool flight, cutscene, player_mode_set;
} qa_player_state;

#endif
