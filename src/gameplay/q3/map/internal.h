#ifndef QA_Q3_MAP_INTERNAL_H
#define QA_Q3_MAP_INTERNAL_H

#include "../internal.h"
#include "qa/game_q3_map.h"
#include "qa/text.h"

#include <float.h>

struct q3_map_runtime {
    qa_q3_map_options options;
    qa_q3_map_actor_state *actors;
    uint32_t capacity;
    int32_t loaded_game_type;
    uint64_t registered_items;
    bool world_spawned, post_spawned, locations_linked;
    qa_actor_id location_head;
};

static inline int32_t q3_map_float_to_int(float value) {
    return q3_source_float_to_int(value);
}

static inline int32_t q3_map_source_schedule(int32_t now, float seconds) {
    return q3_source_float_schedule(now, seconds);
}

static inline int32_t q3_map_random_schedule(int32_t now, float wait, float random,
                                             float crandom) {
    float seconds = q3_source_float_add(wait, q3_source_float_multiply(random, crandom));
    return q3_source_float_schedule(now, seconds);
}

bool q3_map_fail(qa_error *, const char *);
bool q3_map_item_respawn(qa_q3_game *, qa_actor_id, bool *handled, qa_error *);
bool q3_maps_round_reset(qa_q3_game *, const qa_q3_map_options *, qa_error *);
bool q3_configstring_event(qa_q3_game *, const qa_q3_map_event *, qa_error *);
qa_q3_map_actor_state *q3_map_get(qa_q3_game *, qa_actor_id);
const qa_q3_map_actor_state *q3_map_const(const qa_q3_game *, qa_actor_id);
bool q3_map_text(const qa_q3_game *, qa_string_id);
const char *q3_map_cstr(const qa_q3_game *, qa_string_id);
bool q3_map_intern(qa_q3_game *, qa_bytes, qa_string_id *, qa_error *);
bool q3_map_intern_fold(qa_q3_game *, qa_bytes, qa_string_id *, qa_error *);
bool q3_map_intern_cstr(qa_q3_game *, const char *, qa_string_id *, qa_error *);
bool q3_map_property(const qa_q3_map_fields *, const char *, qa_bytes *);
bool q3_map_number(const qa_q3_map_fields *, const char *, double, double *, qa_error *);
bool q3_map_integer(const qa_q3_map_fields *, const char *, int32_t, int32_t *);
bool q3_map_vector(const qa_q3_map_fields *, const char *, qa_vec3, qa_vec3 *, qa_error *);
bool q3_map_parse_state(qa_q3_game *, const qa_q3_map_fields *, qa_q3_map_actor_state *,
                        qa_error *);
bool q3_map_allocate(qa_q3_game *, qa_q3_map_actor_state *, const qa_actor_collision *, bool link,
                     qa_error *);
bool q3_map_bind_target(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);
bool q3_map_target_binding(qa_q3_game *, qa_actor_id, qa_target_binding *);
bool q3_map_checkpoint_restore_source(qa_q3_game *, const qa_q3_map_checkpoint *, qa_error *);
bool q3_map_register_item(qa_q3_game *, uint32_t, qa_error *);
bool q3_map_use_targets(qa_q3_game *, qa_q3_map_actor_state *, qa_actor_id, qa_error *);
bool q3_map_schedule(qa_q3_game *, qa_q3_map_actor_state *, int32_t delay_ms,
                     qa_q3_map_think);
bool q3_map_emit(qa_q3_game *, const qa_q3_map_event *, qa_error *);
void q3_map_warn(qa_q3_game *, qa_actor_id, const char *);
qa_vec3 q3_map_direction(qa_vec3);
bool q3_map_is_player(qa_q3_game *, qa_actor_id);
bool q3_map_player_launchable(qa_q3_game *, qa_actor_id, q3_actor **);
int32_t q3_map_team(qa_q3_game *, qa_actor_id);
bool q3_map_set_velocity(qa_q3_game *, qa_actor_id, qa_vec3, qa_error *);
bool q3_map_target_pose(qa_q3_game *, qa_actor_id, qa_vec3 *, qa_vec3 *, qa_error *);
bool q3_map_pick(qa_q3_game *, qa_string_id, qa_actor_id *, qa_error *);
bool q3_map_aim(qa_q3_game *, qa_q3_map_actor_state *, qa_vec3 origin, qa_error *);
bool q3_map_spawn_target(qa_q3_game *, const qa_q3_map_fields *,
                         qa_q3_map_actor_state *, qa_error *);
bool q3_map_spawn_trigger(qa_q3_game *, const qa_q3_map_fields *,
                          qa_q3_map_actor_state *, qa_error *);
bool q3_map_spawn_misc(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);
bool q3_map_spawn_item(qa_q3_game *, const qa_q3_map_fields *, qa_q3_map_actor_state *,
                       uint32_t, qa_error *);
bool q3_map_spawn_mover(qa_q3_game *, const qa_q3_map_fields *, qa_q3_map_actor_state *,
                        qa_error *);
bool q3_map_target_use(qa_q3_game *, qa_q3_map_actor_state *, qa_actor_id, qa_actor_id,
                       qa_error *);
bool q3_map_trigger_use(qa_q3_game *, qa_q3_map_actor_state *, qa_actor_id, qa_actor_id,
                        qa_error *);
bool q3_map_misc_use(qa_q3_game *, qa_q3_map_actor_state *, qa_actor_id, qa_actor_id,
                     qa_error *);
bool q3_map_trigger_touch(qa_q3_game *, qa_q3_map_actor_state *, const qa_touch_contact *,
                          qa_error *);
bool q3_map_target_think(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);
bool q3_map_trigger_think(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);
bool q3_map_misc_think(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);
bool q3_map_item_think(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);
bool q3_map_item_use(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);
bool q3_map_mover_touch(qa_q3_game *, qa_q3_map_actor_state *, const qa_touch_contact *,
                        qa_error *);
bool q3_map_mover_think(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);
bool q3_map_mover_post_spawn(qa_q3_game *, qa_error *);
bool q3_map_mover_action(qa_q3_game *, qa_q3_mover_action, qa_actor_id, qa_actor_id, int32_t,
                         bool *, qa_error *);
bool q3_map_mover_used(qa_q3_game *, qa_actor_id, int32_t, int32_t, qa_error *);
bool q3_map_mover_sync_state(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);
bool q3_map_mover_sync_admission(qa_q3_game *, qa_q3_map_actor_state *, qa_error *);
void q3_map_mover_presentation(const qa_q3_game *, qa_actor_id, const char **,
                               const char **, uint32_t *);

#endif
