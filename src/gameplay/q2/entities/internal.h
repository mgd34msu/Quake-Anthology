#ifndef QA_Q2_ENTITIES_INTERNAL_H
#define QA_Q2_ENTITIES_INTERNAL_H
#include "../player/internal.h"
#include <errno.h>

typedef qa_q2_entity_state q2_entity_state;
bool q2_player_map_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_player_map_think(qa_q2_game *, q2_actor *, q2_entity_think, qa_error *);
typedef struct q2_entities {
    qa_q2_entity_services services;
    qa_actor_id poi, poi_dynamic;
    qa_string_id poi_image, story;
    int poi_stage, steam_id, total_secrets, found_secrets, total_goals, found_goals;
    uint64_t last_autosave_ns;
    q2_healthbar bars[2];
    qa_q2_fog world_fog;
    qa_string_id sky, goals, primary, secondary;
    qa_vec3 sky_axis;
    float sky_rotation;
    uint32_t primary_changes, secondary_changes;
    unsigned goal_number;
    bool sky_auto, has_goals;
    q2_wind_time *wind;
    size_t wind_count, wind_capacity;
} q2_entities;

q2_actor *q2_ent(qa_q2_game *, qa_actor_id);
qa_string_id q2_actor_field(qa_q2_game *, qa_actor_id, const char *);
float q2_actor_field_float(qa_q2_game *, qa_actor_id, const char *, float);
uint32_t q2_actor_field_flags(qa_q2_game *, qa_actor_id, const char *);
bool q2_entity_bind(qa_q2_game *, q2_actor *, qa_error *);
const char *q2_field_text(qa_q2_game *, const q2_entity_state *, const char *);
qa_string_id q2_field_id(qa_q2_game *, const q2_entity_state *, const char *);
float q2_field_float(qa_q2_game *, const q2_entity_state *, const char *, float);
qa_vec3 q2_field_vec(qa_q2_game *, const q2_entity_state *, const char *, qa_vec3);
bool q2_entity_schedule(qa_q2_game *, q2_actor *, q2_entity_think, float);
bool q2_entity_body(qa_q2_game *, q2_actor *, const qa_body_state *, bool, qa_error *);
bool q2_entity_solid(qa_q2_game *, q2_actor *, qa_physics_solid, qa_error *);
bool q2_entity_show(qa_q2_game *, q2_actor *, qa_error *);
bool q2_entity_sound(qa_q2_game *, q2_actor *, const char *, int, float, float, int, qa_error *);
bool q2_entity_targets(qa_q2_game *, q2_actor *, qa_actor_id, bool, qa_error *);
bool q2_entity_damage(qa_q2_game *, q2_actor *, qa_actor_id, qa_actor_id, float, float, int,
                      uint32_t, qa_error *);
bool q2_entity_radius(qa_q2_game *, q2_actor *, qa_actor_id, float, float, int, qa_error *);
bool q2_entity_message(qa_q2_game *, q2_actor *, qa_actor_id, const char *, qa_error *);
bool q2_entity_pick(qa_q2_game *, qa_string_id, qa_actor_id *);
bool q2_entity_init_trigger(qa_q2_game *, q2_actor *, qa_error *);
bool q2_entity_clip(qa_q2_game *, q2_actor *, qa_actor_id, bool *, qa_error *);
bool q2_entity_flags(qa_q2_game *, bool, bool, uint32_t *, qa_error *);
bool q2_entity_native_spawn(qa_q2_game *, const char *, const qa_body_state *, q2_entity_kind,
                            q2_actor **, qa_error *);
bool q2_entity_multi(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_target_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_target_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_actor_id, bool *, qa_error *);
bool q2_target_think(qa_q2_game *, q2_actor *, q2_entity_think, bool *, qa_error *);
bool q2_trigger_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_trigger_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_actor_id, bool *, qa_error *);
bool q2_trigger_touch(qa_q2_game *, q2_actor *, const qa_touch_contact *, bool *, qa_error *);
bool q2_trigger_think(qa_q2_game *, q2_actor *, q2_entity_think, bool *, qa_error *);
bool q2_mover_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_mover_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_actor_id, bool *, qa_error *);
bool q2_mover_think(qa_q2_game *, q2_actor *, q2_entity_think, bool *, qa_error *);
bool q2_mover_touch(qa_q2_game *, q2_actor *, const qa_touch_contact *, bool *, qa_error *);
bool q2_mover_blocked(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_mover_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_move_start(qa_q2_game *, q2_actor *, qa_vec3, bool, q2_move_done, qa_error *);
bool q2_move_tick(qa_q2_game *, q2_actor *, q2_entity_think, qa_error *);
bool q2_move_finished(qa_q2_game *, q2_actor *, q2_move_done, qa_error *);
bool q2_mover_portals(qa_q2_game *, q2_actor *, bool, qa_error *);
bool q2_door_down(qa_q2_game *, q2_actor *, qa_error *);
bool q2_door_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_door_finished(qa_q2_game *, q2_actor *, bool, qa_error *);
bool q2_door_prepare(qa_q2_game *, q2_actor *, qa_error *);
bool q2_door_smart_water(qa_q2_game *, q2_actor *, qa_error *);
bool q2_door_spawn(qa_q2_game *, q2_actor *, qa_error *);
bool q2_door_touch(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_door_blocked(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_door_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_train_spawn(qa_q2_game *, q2_actor *, qa_error *);
bool q2_train_find(qa_q2_game *, q2_actor *, qa_error *);
bool q2_train_next(qa_q2_game *, q2_actor *, qa_error *);
bool q2_train_wait(qa_q2_game *, q2_actor *, qa_error *);
bool q2_train_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_train_resume(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_route_touch(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_brush_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_brush_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_actor_id, bool *, qa_error *);
bool q2_brush_think(qa_q2_game *, q2_actor *, q2_entity_think, bool *, qa_error *);
bool q2_brush_touch(qa_q2_game *, q2_actor *, const qa_touch_contact *, bool *, qa_error *);
bool q2_brush_blocked(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_brush_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_brush_finished(qa_q2_game *, q2_actor *, q2_move_done, qa_error *);
bool q2_rerelease_entity_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_rerelease_entity_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_actor_id, bool *,
                             qa_error *);
bool q2_rerelease_entity_think(qa_q2_game *, q2_actor *, q2_entity_think, bool *, qa_error *);
bool q2_rerelease_entity_touch(qa_q2_game *, q2_actor *, const qa_touch_contact *, bool *,
                               qa_error *);
bool q2_scenery_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_scenery_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_actor_id, bool *, qa_error *);
bool q2_scenery_think(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_scenery_touch(qa_q2_game *, q2_actor *, const qa_touch_contact *, bool *, qa_error *);
bool q2_scenery_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
static inline qa_vec3 q2_movedir(qa_vec3 v) {
    if (v.x == 0 && v.y == -1 && v.z == 0)
        return qa_v3(0, 0, 1);
    if (v.x == 0 && v.y == -2 && v.z == 0)
        return qa_v3(0, 0, -1);
    qa_vec3 forward;
    qa_builtin_angle_vectors(v, &forward, NULL, NULL);
    return forward;
}
bool q2_target_extra_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_target_extra_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_actor_id, bool *, qa_error *);
bool q2_target_extra_think(qa_q2_game *, q2_actor *, q2_entity_think, bool *, qa_error *);
bool q2_entity_teleport(qa_q2_game *, q2_actor *, qa_actor_id, const qa_body_state *, bool ctf,
                        bool pad, qa_error *);
bool q2_scenery_prethink(qa_q2_game *, q2_actor *, qa_error *);
bool q2_laser_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_rerelease_poi(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_rerelease_goal_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_rerelease_notify(qa_q2_game *, q2_actor *, qa_error *);
uint32_t q2_entity_color(const char *);
bool q2_light_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_light_use(qa_q2_game *, q2_actor *, qa_error *);
bool q2_light_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_turret_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_turret_think(qa_q2_game *, q2_actor *, q2_entity_think, qa_error *);
bool q2_turret_blocked(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_q64_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_q64_use(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_q64_think(qa_q2_game *, q2_actor *, q2_entity_think, qa_error *);
#endif
