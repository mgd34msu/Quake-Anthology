#ifndef QA_Q3_INTERNAL_H
#define QA_Q3_INTERNAL_H
#include "qa/game_q3.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Q3_MASK_SHOT UINT32_C(0x06000001)
#define Q3_CONTENTS_BODY INT32_C(0x02000000)
#define Q3_CONTENTS_TRIGGER INT32_C(0x40000000)
#define Q3_SURF_NOIMPACT 16
#define Q3_PI 3.14159265358979323846f

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
struct qa_q3_game {
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
    qa_item_id weapon_items[QA_Q3_WEAPON_COUNT], ammo_items[QA_Q3_WEAPON_COUNT];
    qa_item_id item_ids[52];
    int32_t previous_ms, now_ms;
    uint64_t attack_sequence;
    qa_q3_ranking_hit ranking_hit;
    qa_actor_id body_queue[8];
    uint32_t body_queue_index;
    q3_snapshot_frame *snapshot_frames;
    q3_map_runtime *map;
    qa_physics physics;
};
q3_actor *q3_actor_get(qa_q3_game *, qa_actor_id);
const q3_actor *q3_actor_const(const qa_q3_game *, qa_actor_id);
bool q3_fail(qa_error *, const char *);
bool q3_rollback_spawn(qa_q3_game *, qa_actor_id, qa_error *);
q3_snapshot_frame *q3_bounds_snapshot(qa_q3_game *, qa_bounds, qa_collision_role, qa_error *);
bool q3_use_holdable(qa_q3_game *, qa_actor_id, qa_q3_holdable, qa_error *);
bool q3_inventory_holdable_changed(qa_q3_game *, qa_actor_id, qa_q3_holdable before,
                                    qa_q3_holdable after, qa_error *);
bool q3_player_state_valid(const qa_q3_player_state *);
void q3_force_view(qa_q3_player_state *, qa_vec3, int32_t lock_ms);
int32_t q3_entity_number(const qa_q3_game *, qa_actor_id);
bool q3_sound(qa_q3_game *, qa_actor_id, const char *, int32_t channel, qa_error *);
uint32_t q3_rand(qa_q3_game *);
float q3_random(qa_q3_game *);
float q3_crandom(qa_q3_game *);
int32_t q3_add_time(int32_t, int32_t);
int32_t q3_sub_time(int32_t, int32_t);
bool q3_event(qa_q3_game *, qa_actor_id, qa_actor_id, qa_builtin_event_kind, int32_t event,
              int32_t parameter, qa_vec3 origin, qa_vec3 end, qa_vec3 normal, qa_error *);
bool q3_player_event(qa_q3_game *, qa_actor_id, int32_t event, int32_t parameter, qa_error *);
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
