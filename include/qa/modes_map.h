#ifndef QA_MODES_MAP_H
#define QA_MODES_MAP_H

#include "qa/horde.h"

typedef enum qa_mode_map_role {
    QA_MODE_MAP_UNCLAIMED,
    QA_MODE_MAP_OBJECT,
    QA_MODE_MAP_SPAWN,
    QA_MODE_MAP_HORDE_MANAGER,
    QA_MODE_MAP_HORDE_POINT
} qa_mode_map_role;
typedef struct qa_mode_map_entity {
    const char *classname;
    qa_actor_id actor;
    qa_vec3 origin, angles;
    qa_bounds bounds;
    qa_string_id target, message;
    uint32_t spawnflags;
    float speed, wait;
    int32_t count;
    bool inline_model, no_bots, no_humans;
} qa_mode_map_entity;
typedef struct qa_mode_map_admission {
    qa_mode_map_role role;
    qa_mode_object_spec object;
    qa_mode_spawnpoint spawn;
    qa_horde_point horde;
} qa_mode_map_admission;
enum qa_mode_map_requirement {
    QA_MODE_MAP_RED = 1u,
    QA_MODE_MAP_BLUE = 2u,
    QA_MODE_MAP_NEUTRAL = 4u,
    QA_MODE_MAP_BALL = 8u,
    QA_MODE_MAP_RED_GOAL = 16u,
    QA_MODE_MAP_BLUE_GOAL = 32u,
    QA_MODE_MAP_PLAYER_SPAWN = 64u,
    QA_MODE_MAP_HORDE_CONTROLLER = 128u,
    QA_MODE_MAP_MONSTER_SPAWN = 256u,
    QA_MODE_MAP_BALL_SPAWN = 512u,
    QA_MODE_MAP_TAG = 1024u
};
bool qa_modes_classify_entity(qa_modes *, qa_mode_id, const qa_mode_map_entity *,
                              qa_mode_map_admission *, qa_error *);
/* Outputs only missing roles using authored spawn anchors. The map coordinator
 * admits each returned record, then supplies the combined spawn/point records.
 * Unresolved requirements remain explicit when a map has no usable anchors.
 * NULL output with zero capacity measures the required record count. */
bool qa_modes_plan_map(qa_modes *, qa_mode_id, const qa_mode_map_admission *, size_t, bool generate,
                       qa_mode_map_admission *, size_t capacity, size_t *count, uint32_t *missing,
                       qa_error *);
bool qa_modes_start_relics(qa_modes *, qa_mode_id, qa_error *);

#endif
