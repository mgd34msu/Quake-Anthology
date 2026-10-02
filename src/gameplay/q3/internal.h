#ifndef QA_Q3_INTERNAL_H
#define QA_Q3_INTERNAL_H
#include "qa/game_q3.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_client_types.h"
#include "qa/game_q3_source_types.h"
#include "qa/game_q3_source.h"
#include "source_wire.h"
#include "source_postgame.h"
#include "shader_remap.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Q3_MASK_SHOT UINT32_C(0x06000001)
#define Q3_CONTENTS_BODY INT32_C(0x02000000)
#define Q3_CONTENTS_TRIGGER INT32_C(0x40000000)
#define Q3_SURF_NOIMPACT 16
#define Q3_PI 3.14159265358979323846f

static inline float q3_source_vec_component(qa_vec3 vector, unsigned axis) {
    return axis == 0 ? vector.x : axis == 1 ? vector.y : vector.z;
}

static inline float q3_source_float_multiply(float left, float right) {
    volatile float value = left * right;
    return value;
}

static inline float q3_source_float_add(float left, float right) {
    volatile float value = left + right;
    return value;
}

static inline float q3_source_float_divide(float left, float right) {
    volatile float value = left / right;
    return value;
}

static inline void q3_source_angle_vectors(qa_vec3 angles, qa_vec3 *forward,
                                           qa_vec3 *right, qa_vec3 *up) {
    const float radians = 0.01745329251994329577f;
    float yaw = q3_source_float_multiply(angles.y, radians);
    float pitch = q3_source_float_multiply(angles.x, radians);
    float roll = q3_source_float_multiply(angles.z, radians);
    float sy = (float)sin((double)yaw), cy = (float)cos((double)yaw);
    float sp = (float)sin((double)pitch), cp = (float)cos((double)pitch);
    float sr = (float)sin((double)roll), cr = (float)cos((double)roll);
    if (forward)
        *forward = qa_v3(q3_source_float_multiply(cp, cy), q3_source_float_multiply(cp, sy), -sp);
    if (right) {
        float roll_pitch = q3_source_float_multiply(-sr, sp);
        *right = qa_v3(q3_source_float_add(q3_source_float_multiply(roll_pitch, cy),
                                          q3_source_float_multiply(-cr, -sy)),
                       q3_source_float_add(q3_source_float_multiply(roll_pitch, sy),
                                          q3_source_float_multiply(-cr, cy)),
                       q3_source_float_multiply(-sr, cp));
    }
    if (up) {
        float roll_pitch = q3_source_float_multiply(cr, sp);
        *up = qa_v3(q3_source_float_add(q3_source_float_multiply(roll_pitch, cy),
                                       q3_source_float_multiply(-sr, -sy)),
                    q3_source_float_add(q3_source_float_multiply(roll_pitch, sy),
                                       q3_source_float_multiply(-sr, cy)),
                    q3_source_float_multiply(cr, cp));
    }
}

static inline int32_t q3_source_float_to_int(float value) {
    return isfinite(value) && value >= -2147483648.0f && value < 2147483648.0f
               ? (int32_t)truncf(value)
               : INT32_MIN;
}

static inline int32_t q3_source_float_schedule(int32_t now, float seconds) {
    float milliseconds = q3_source_float_multiply(seconds, 1000.0f);
    return q3_source_float_to_int(q3_source_float_add((float)now, milliseconds));
}

typedef qa_q3_projectile_state q3_missile;
typedef qa_q3_item_state q3_item_state;
typedef qa_q3_actor_state q3_actor;
typedef qa_q3_kamikaze_cooldown q3_kamikaze_cooldown;
typedef struct q3_map_runtime q3_map_runtime;
bool q3_checkpoint_restore_source(qa_q3_game *, const qa_q3_checkpoint *, qa_error *);
bool q3_client_counts_valid(const qa_q3_source_client_counts *, uint32_t max_clients);
bool q3_level_state_valid(const qa_q3_game *, const qa_q3_source_team_state *,
                         const qa_q3_source_match_state *, qa_error *);
bool q3_followed_player_valid(const qa_q3_game *, const qa_q3_player *);
bool q3_followed_player_saved_valid(const qa_q3_game *, const qa_q3_player *);
qa_q3_player *q3_client_follow_player(qa_q3_game *, uint32_t slot);
bool q3_source_movement_write(qa_q3_game *, qa_actor_id, uint32_t fields, qa_error *);
bool q3_source_client_pointer(const qa_q3_game *, qa_actor_id, uint32_t *);
bool q3_source_row_body_ensure(qa_q3_game *, uint32_t, qa_actor_id *, qa_error *);
bool q3_spawn_raw_actor(qa_q3_game *, qa_string_id, qa_actor_id *, qa_error *);
bool q3_source_client_body_ensure(qa_q3_game *, uint32_t, qa_actor_id *, qa_error *);
int32_t q3_source_team(qa_q3_game *, qa_actor_id);
bool q3_obelisk_step(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_obelisk_touch(qa_q3_game *, qa_actor_id, qa_actor_id, qa_error *);
bool q3_obelisk_reconnect(qa_q3_game *, qa_actor_id, qa_error *);
typedef struct q3_inventory_owner {
    qa_q3_game *game;
    qa_actor_id actor;
    qa_inventory_lease weapons, holdables;
    uint32_t selections;
} q3_inventory_owner;
struct qa_q3_map_actor_state;
typedef struct q3_snapshot_frame {
    struct q3_snapshot_frame *next;
    qa_builtin_actor_snapshot snapshot;
    bool active;
} q3_snapshot_frame;
typedef struct q3_death_continuation {
    qa_actor_id actor;
    uint64_t sequence, time_ns;
    qa_actor_owner weapon_provider;
} q3_death_continuation;
typedef struct q3_current_origin {
    qa_actor_id actor;
    qa_vec3 origin;
    bool active;
} q3_current_origin;
struct qa_q3_game {
    qa_q3_source_memory memory;
    qa_q3_options options;
    q3_actor *actors;
    q3_kamikaze_cooldown *kamikaze_cooldowns;
    uint64_t *player_binding_tokens;
    uint64_t player_binding_serial;
    qa_pickup_lease *item_observations;
    q3_inventory_owner *inventory_owners;
    size_t observation_depth;
    bool source_restored;
    uint32_t capacity, rng, death_animation;
    qa_string_id source_noclass, source_freed;
    qa_item_id weapon_items[QA_Q3_WEAPON_COUNT], ammo_items[QA_Q3_WEAPON_COUNT];
    qa_item_id item_ids[52];
    int32_t previous_ms, now_ms;
    uint64_t attack_sequence;
    qa_q3_ranking_hit ranking_hit;
    qa_actor_id body_queue[8];
    uint32_t podium_players[3];
    uint32_t body_queue_index;
    q3_snapshot_frame *snapshot_frames;
    q3_map_runtime *map;
    q3_wire_state *wire;
    qa_q3_shader_remap_state shader_remaps;
    char *configstrings[QA_Q3_NATIVE_CONFIGSTRINGS];
    uint64_t configstring_revisions[QA_Q3_NATIVE_CONFIGSTRINGS];
    qa_q3_native_client clients[QA_Q3_NATIVE_CLIENTS];
    q3_actor client_actors[QA_Q3_NATIVE_CLIENTS];
    qa_q3_source_binding source_entities[QA_Q3_SOURCE_ENTITIES];
    uint16_t *source_numbers;
    uint32_t source_count;
    bool new_session;
    int32_t fry_sound_index;
    int32_t portal_sequence;
    int32_t last_team_location_time;
    qa_q3_source_client_counts client_counts;
    qa_q3_source_team_state team_state;
    qa_q3_source_match_state match_state;
    bool source_actor_ran[QA_Q3_SOURCE_ENTITIES];
    q3_death_continuation death_continuations[QA_Q3_SOURCE_CLIENTS];
    q3_current_origin current_origins[QA_Q3_SOURCE_CLIENTS];
    qa_physics physics;
};
qa_q3_native_client *q3_client_at(qa_q3_game *, uint32_t);
const qa_q3_native_client *q3_client_const(const qa_q3_game *, uint32_t);
q3_actor *q3_actor_storage(qa_q3_game *, qa_actor_id);
q3_actor *q3_actor_at(qa_q3_game *, uint32_t);
const q3_actor *q3_actor_at_const(const qa_q3_game *, uint32_t);
void q3_source_state_reset(qa_q3_game *);
bool q3_source_level_init(qa_q3_game *, qa_error *);
bool q3_spawn_actor(qa_q3_game *, const qa_builtin_spawn *, qa_actor_id *, qa_error *);
bool q3_teleport_event_at(qa_q3_game *, qa_actor_id, qa_vec3, bool entering, qa_error *);
bool q3_source_body_read(qa_q3_game *, qa_actor_id, qa_body_state *, qa_error *);
void q3_source_origin_written(qa_q3_game *, qa_actor_id, qa_vec3);
bool q3_source_origins_idle(const qa_q3_game *);
void q3_source_actor_released(qa_q3_game *, qa_actor_id);
bool q3_source_prepare(qa_q3_game *, const qa_q3_checkpoint *, uint16_t **, qa_error *);
void q3_source_commit(qa_q3_game *, const qa_q3_checkpoint *, uint16_t *);
q3_actor *q3_actor_get(qa_q3_game *, qa_actor_id);
const q3_actor *q3_actor_const(const qa_q3_game *, qa_actor_id);
float q3_initial_alpha(const qa_q3_game *, qa_actor_id);
bool q3_fail(qa_error *, const char *);
void q3_configstrings_clear(qa_q3_game *);
bool q3_configstrings_capture(const qa_q3_game *, qa_q3_checkpoint *, qa_error *);
bool q3_configstrings_prepare(const qa_q3_checkpoint *, char ***, qa_error *);
void q3_configstrings_discard(char **);
void q3_configstrings_commit(qa_q3_game *, char **);
bool q3_rollback_spawn(qa_q3_game *, qa_actor_id, qa_error *);
q3_snapshot_frame *q3_bounds_snapshot(qa_q3_game *, qa_bounds, qa_collision_role, qa_error *);
bool q3_use_holdable(qa_q3_game *, qa_actor_id, qa_q3_holdable, qa_error *);
qa_actor_id q3_portal_destination(qa_q3_game *, int32_t sequence);
bool q3_inventory_holdable_changed(qa_q3_game *, qa_actor_id, qa_q3_holdable before,
                                    qa_q3_holdable after, qa_error *);
bool q3_player_state_valid(const qa_q3_player_state *);
bool q3_player_state_valid_source_client(const qa_q3_player_state *, const qa_q3_native_client *);
void q3_force_view(qa_q3_player_state *, qa_vec3, int32_t lock_ms);
int32_t q3_entity_number(const qa_q3_game *, qa_actor_id);
bool q3_sound(qa_q3_game *, qa_actor_id, const char *, int32_t channel, qa_error *);
bool q3_sound_report(qa_q3_game *, qa_actor_id, const char *, int32_t channel, qa_error *);
uint32_t q3_rand(qa_q3_game *);
float q3_random(qa_q3_game *);
float q3_crandom(qa_q3_game *);
int32_t q3_add_time(int32_t, int32_t);
float q3_source_atof(qa_bytes);
int32_t q3_sub_time(int32_t, int32_t);
bool q3_event(qa_q3_game *, qa_actor_id, qa_actor_id, qa_builtin_event_kind, int32_t event,
              int32_t parameter, qa_vec3 origin, qa_vec3 end, qa_vec3 normal, qa_error *);
bool q3_player_event(qa_q3_game *, qa_actor_id, int32_t event, int32_t parameter, qa_error *);
bool q3_add_event(qa_q3_game *, qa_actor_id, int32_t event, int32_t parameter, qa_error *);
bool q3_source_initial_death(qa_q3_game *, const qa_damage_outcome *, bool *admitted, qa_error *);
bool q3_source_death_effects(qa_q3_game *, const qa_damage_outcome *, qa_error *);
bool q3_trace(qa_q3_game *, qa_vec3, qa_vec3, qa_actor_id, uint32_t, qa_trace_result *, qa_error *);
bool q3_damage(qa_q3_game *, qa_actor_id target, qa_actor_id attacker, qa_actor_id inflictor,
               qa_q3_weapon, int32_t method, uint32_t flags, float amount, qa_vec3 direction,
               qa_vec3 point, bool radius, qa_damage_outcome *, qa_error *);
bool q3_radius(qa_q3_game *, qa_actor_id inflictor, qa_actor_id attacker, qa_q3_weapon,
               int32_t method, qa_vec3, float damage, float radius, qa_actor_id ignore,
               bool *accuracy, qa_error *);
bool q3_accuracy(qa_q3_game *, qa_actor_id target, qa_actor_id attacker);
bool q3_is_player(qa_q3_game *, qa_actor_id);
bool q3_ranking_fire(qa_q3_game *, qa_actor_id, qa_q3_weapon, qa_error *);
bool q3_ranking_pickup(qa_q3_game *, qa_actor_id, const qa_q3_item *, int32_t, qa_error *);
bool q3_ranking_holdable(qa_q3_game *, qa_actor_id, qa_q3_holdable, qa_error *);
bool q3_ranking_reward(qa_q3_game *, qa_actor_id, uint32_t, qa_error *);
void q3_credit_accuracy(qa_q3_game *, qa_actor_id);
bool q3_invulnerability(qa_q3_game *, qa_actor_id, qa_vec3 direction, qa_vec3 point,
                        qa_vec3 *impact, qa_vec3 *normal, bool *hit, qa_error *);
float q3_damage_factor(qa_q3_game *, const qa_q3_player_state *);
bool q3_launch(qa_q3_game *, qa_actor_id, qa_q3_weapon, qa_vec3, qa_vec3, qa_vec3 right, qa_vec3 up,
               float factor, qa_actor_id *, qa_error *);
bool q3_missile_step(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_missile_explode(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_missile_trigger(qa_q3_game *, qa_actor_id mine, qa_actor_id player, qa_error *);
bool q3_item_step(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_item_bind_existing(qa_q3_game *, qa_actor_id, const qa_q3_item_spawn *, bool available,
                           bool initial_powerup_delay, bool *placed, qa_error *);
bool q3_item_touch(qa_q3_game *, qa_actor_id item, qa_actor_id recipient,
                   bool allow_hidden, bool *accepted, qa_error *);
bool q3_item_observation_bind(qa_q3_game *, qa_actor_id, qa_pickup_lease *, qa_error *);
bool q3_item_observations_prepare(qa_q3_game *, const q3_actor *, qa_pickup_lease **, qa_error *);
void q3_item_observations_abort(qa_q3_game *, qa_pickup_lease *);
void q3_item_observations_commit(qa_q3_game *, qa_pickup_lease *);
bool q3_kamikaze_step(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_portal_step(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_combat_describe(void *, const qa_damage_request *, const qa_combat_state *,
                        const qa_combat_state *, qa_combat_context *, qa_error *);
bool q3_ammo_read(qa_q3_game *, qa_actor_id, qa_q3_weapon, int32_t *, qa_error *);
bool q3_ammo_timer_store(qa_q3_game *, qa_actor_id, qa_q3_weapon, int32_t, qa_error *);
bool q3_add_ammo(qa_q3_game *, qa_actor_id, qa_q3_weapon, int32_t, qa_error *);
bool q3_killbox(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_owns_weapon(qa_q3_game *, qa_actor_id, qa_q3_weapon);
bool q3_gauntlet(qa_q3_game *, qa_actor_id, bool *hit, qa_error *);
bool q3_item_register(qa_q3_game *, qa_error *);
bool q3_drop_player_items(qa_q3_game *, qa_actor_id, bool no_drop, qa_error *);
bool q3_copy_corpse(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_corpse_step(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_start_kamikaze(qa_q3_game *, qa_actor_id source, qa_actor_id attacker, bool kill,
                       qa_error *);
bool q3_cancel_kamikaze_timers(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_schedule_kamikaze(qa_q3_game *, qa_actor_id, qa_vec3 origin, qa_error *);
bool q3_death_rewards(qa_q3_game *, qa_actor_id, const qa_damage_request *, qa_error *);
bool q3_mover_set_state(qa_q3_game *, qa_actor_id, int32_t, int32_t, qa_error *);
bool q3_mover_match_team(qa_q3_game *, qa_actor_id, int32_t, int32_t, qa_error *);
bool q3_map_frame_begin(qa_q3_game *, qa_error *);
bool q3_map_actor_frame(qa_q3_game *, qa_actor_id, bool *handled, qa_error *);
bool q3_map_touch(qa_q3_game *, const qa_touch_contact *, bool *handled, qa_error *);
bool q3_map_item_picked(qa_q3_game *, qa_actor_id, int32_t respawn_at, int32_t expire_at,
                        bool *handled, qa_error *);
void q3_map_actor_released(qa_q3_game *, qa_actor_record);
void q3_map_destroy(qa_q3_game *);
bool q3_map_mover_action(qa_q3_game *, qa_q3_mover_action, qa_actor_id, qa_actor_id, int32_t,
                         bool *, qa_error *);
bool q3_map_mover_used(qa_q3_game *, qa_actor_id, int32_t, int32_t, qa_error *);
bool q3_map_mover_sync_state(qa_q3_game *, struct qa_q3_map_actor_state *, qa_error *);
void q3_map_mover_presentation(const qa_q3_game *, qa_actor_id, const char **,
                               const char **, uint32_t *);
#endif
