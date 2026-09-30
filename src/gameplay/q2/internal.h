#ifndef QA_Q2_INTERNAL_H
#define QA_Q2_INTERNAL_H
#include "qa/game_q2.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define Q2_NS UINT64_C(1000000000)
#define Q2_MS UINT64_C(1000000)
#define Q2_SHOT_MASK UINT32_C(0x06000003)
#define Q2_PROJECTILE_MASK UINT32_C(0x46004003)
#define Q2_PLAYER_CONTENTS UINT32_C(0x40000000)
#define Q2_WATER_MASK UINT32_C(56)

typedef enum q2_projectile_kind {
    Q2_PROJECTILE_NONE,
    Q2_BOLT,
    Q2_ROCKET,
    Q2_GRENADE,
    Q2_BFG_BALL,
    Q2_ION,
    Q2_PLASMA,
    Q2_FLECHETTE,
    Q2_TRACKER,
    Q2_PROX,
    Q2_TESLA,
    Q2_TRAP,
    Q2_TRACKER_DAEMON,
    Q2_PROX_FIELD,
    Q2_TESLA_FIELD,
    Q2_BAD_AREA,
    Q2_TRAP_GIB,
    Q2_NUKE,
    Q2_GIB,
    Q2_DEBRIS,
    Q2_TRAP_ORBIT_GIB,
    Q2_GREEN_BOLT,
    Q2_BLUE_BOLT,
    Q2_HEAT_ROCKET,
    Q2_CTF_HOOK,
    Q2_LMCTF_HOOK,
    Q2_SPAWN_GROWTH,
    Q2_LMCTF_PLASMA_SPREAD,
    Q2_LMCTF_PLASMA_BOUNCE,
    Q2_PROBOSCIS,
    Q2_PROBOSCIS_SEGMENT,
    Q2_RERELEASE_SPAWN_GROWTH,
    Q2_RERELEASE_SPAWN_BEAM,
    Q2_LOOGIE
} q2_projectile_kind;
typedef enum q2_proboscis_phase {
    Q2_PROBOSCIS_FLYING,
    Q2_PROBOSCIS_ATTACHED,
    Q2_PROBOSCIS_RETRACTING,
    Q2_PROBOSCIS_RETURNED
} q2_proboscis_phase;
struct qa_q2_monster;
struct q2_item_state;
struct q2_power_state;
struct qa_q2_player_state;
struct qa_q2_entity_state;
struct qa_targets;
struct q2_items;
struct q2_players;
struct q2_entities;
typedef struct q2_monsters_runtime q2_monsters_runtime;
typedef struct q2_projectile {
    q2_projectile_kind kind;
    qa_attack attack;
    qa_actor_id owner, enemy, child;
    qa_vec3 movedir;
    float damage, kick, radius_damage, radius, gravity, speed;
    uint64_t born_ns, expire_ns, next_ns, effect_ns;
    int direct_mod, splash_mod, frame, phase, wait;
    float delay, captured_mass, turn_fraction;
    uint64_t effects;
    uint32_t render_flags, gib_flags;
    qa_string_id classname, model, loop_sound;
    int skin;
    float scale, alpha;
    bool hand, held, armed, visible, gekk, dodgeable;
} q2_projectile;
typedef struct q2_actor {
    struct q2_actor *all_next, *free_next;
    struct q2_actor *live_next, *live_previous;
    uint64_t source_order;
    uint64_t extra_effects;
    float alpha;
    bool lmctf_plasma_bounce;
    qa_actor_id id;
    bool weapon_bound, physics_bound;
    qa_q2_weapon_state weapon;
    qa_q2_weapon_input input;
    int silencer;
    q2_projectile projectile;
    qa_physics_properties physics;
    qa_q2_grapple_state grapples[2];
    bool hand_grenade_bound;
    uint64_t hand_revision;
    qa_q2_hand_grenade_state hand_grenade;
    struct qa_q2_monster *monster;
    struct q2_item_state *item;
    struct q2_power_state *powers;
    struct qa_q2_player_state *client;
    struct qa_q2_entity_state *entity;
    qa_q2_game *entity_game;
    struct qa_targets *entity_targets;
    bool restore_definitions, restore_power_inventory, restore_targets;
    qa_inventory_entry restore_hand_ammo;
} q2_actor;
typedef struct q2_trace_frame {
    struct q2_trace_frame *next;
    qa_builtin_actor_snapshot snapshot;
    bool active;
} q2_trace_frame;
typedef struct q2_mt_random {
    uint32_t words[624], index;
    uint64_t draws;
} q2_mt_random;
struct qa_q2_game {
    q2_monsters_runtime *monster_runtime;
    qa_builtin_services services;
    qa_q2_options options;
    qa_q2_hooks hooks;
    qa_q2_grapple_options grapple_options;
    qa_error release_error;
    bool release_failed;
    bool restoring_continuation, continuation_pending, continuation_failed;
    unsigned hand_steps;
    bool lmctf_plasma_quad;
    uint8_t widow_damage_multiplier;
    uint8_t widow_shot_phase;
    qa_q2_weapon_definition definitions[QA_Q2_WEAPON_COUNT];
    qa_item_id items[QA_Q2_WEAPON_COUNT], ammo[QA_Q2_WEAPON_COUNT];
    qa_string_id view_models[QA_Q2_WEAPON_COUNT];
    q2_actor **actors, *all_actors, *retired_actors, *spare_actors;
    q2_actor *first_actor, *last_actor;
    size_t capacity;
    q2_trace_frame *trace_frames;
    struct q2_items *item_runtime;
    struct q2_players *player_runtime;
    struct q2_entities *entity_runtime;
    qa_builtin_random random;
    q2_mt_random rerelease_random;
    qa_actor_id current_actor;
    uint64_t sequence, actor_sequence, now_ns, frame_ns;
};
typedef struct q2_weapon_call {
    qa_q2_game *game;
    q2_actor *actor;
    qa_q2_weapon_state *state;
    qa_q2_weapon_input input;
    const qa_q2_weapon_definition *definition;
    uint64_t now_ns, frame_ns;
    bool rerelease, silenced, equipment;
    bool has_projectile_enemy;
    qa_actor_id projectile_enemy;
    bool has_attack_owner, has_projectile_effects;
    qa_actor_id attack_owner;
    uint64_t projectile_effects;
    qa_actor_id *spawned_projectile;
    bool has_grenade_impulse;
    float grenade_right, grenade_up, grenade_gravity;
} q2_weapon_call;
typedef struct q2_hand_spec {
    qa_vec3 start, direction;
    float speed, fuse;
    bool held;
} q2_hand_spec;

q2_actor *q2_actor_get(qa_q2_game *, qa_actor_id, bool create, qa_error *);
void q2_actor_publish_prepared(qa_q2_game *, q2_actor *, qa_actor_id, bool new_storage);
void q2_actor_order(qa_q2_game *, q2_actor *, uint64_t);
bool q2_actor_live(qa_q2_game *, qa_actor_id);
float q2_random(qa_q2_game *);
bool q2_monster_timed_invulnerability(const q2_actor *, uint64_t now_ns);
float q2_crandom(qa_q2_game *);
void q2_rerelease_seed(qa_q2_game *, uint32_t);
uint32_t q2_rerelease_word(qa_q2_game *);
uint32_t q2_random_bounded(qa_q2_game *, uint32_t bound);
float q2_rerelease_float(qa_q2_game *, float minimum, float maximum);
int64_t q2_rerelease_time_ms(qa_q2_game *, int64_t minimum, int64_t maximum);
bool q2_count(qa_q2_game *, qa_actor_id, qa_item_id, int *, qa_error *);
bool q2_ammo(q2_weapon_call *, int *, qa_error *);
bool q2_consume(q2_weapon_call *, int, bool honor_infinite, qa_error *);
bool q2_sound(q2_weapon_call *, const char *, int, float attenuation, qa_error *);
bool q2_loop(q2_weapon_call *, const char *, qa_error *);
bool q2_event(q2_weapon_call *, qa_builtin_event_kind, int, qa_vec3, qa_vec3, qa_error *);
bool q2_noise(q2_weapon_call *, qa_vec3, qa_error *);
bool q2_animation(q2_weapon_call *, int priority, int first, int last, qa_error *);
bool q2_attack_animation(q2_weapon_call *, int offset, qa_error *);
bool q2_reverse_animation(q2_weapon_call *, qa_error *);
bool q2_power_sound(q2_weapon_call *, qa_error *);
bool q2_no_ammo(q2_weapon_call *, bool sound, qa_error *);
bool q2_change_weapon(q2_weapon_call *, qa_error *);
bool q2_weapon_validate(qa_q2_game *, const qa_q2_weapon_state *, qa_error *);
bool q2_weapon_powerups(q2_weapon_call *, qa_error *);
bool q2_generic(q2_weapon_call *, qa_error *);
bool q2_generic_classic(q2_weapon_call *, qa_error *);
bool q2_fire(q2_weapon_call *, bool buffered, qa_error *);
bool q2_throw_frame(q2_weapon_call *, qa_error *);
bool q2_throw(q2_weapon_call *, bool held, qa_error *);
bool q2_hand_calculate(q2_weapon_call *, uint64_t expires_ns, bool alive, bool held,
                       qa_q2_hand_projection_fn, void *, q2_hand_spec *, qa_error *);
bool q2_hand_validate(const qa_q2_hand_grenade_state *, qa_error *);
bool q2_present(q2_weapon_call *, qa_error *);
uint64_t q2_animation_time(q2_weapon_call *);
uint64_t q2_interval(q2_weapon_call *, uint64_t);
float q2_multiplier(q2_weapon_call *);
void q2_kick(q2_weapon_call *, qa_vec3, qa_vec3, float);
void q2_recoil(q2_weapon_call *, qa_vec3 *origin, qa_vec3 *angles);
bool q2_project(q2_weapon_call *, qa_vec3 angles, qa_vec3 offset, qa_vec3 *, qa_vec3 *, qa_error *);
qa_attack q2_attack(q2_weapon_call *, int mod, uint32_t flags);
bool q2_bullet(q2_weapon_call *, qa_vec3, qa_vec3, float damage, float kick, float hs, float vs,
               int count, int mod, qa_error *);
bool q2_rail(q2_weapon_call *, qa_vec3, qa_vec3, float, float, int mod, uint32_t flags, qa_error *);
bool q2_heatbeam(q2_weapon_call *, qa_vec3, qa_vec3, float, float, qa_error *);
bool q2_fire_chainfist(q2_weapon_call *, qa_error *);
bool q2_projectile_spawn(q2_weapon_call *, q2_projectile_kind, qa_vec3, qa_vec3, float damage,
                         float kick, float speed, float radius, float radius_damage, float fuse,
                         int direct_mod, int splash_mod, bool hand, bool held, qa_error *);
bool q2_projectile_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_proboscis_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_proboscis_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_proboscis_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_tracker_target(q2_weapon_call *, qa_vec3, qa_vec3, qa_actor_id *, qa_error *);
bool q2_launch_behavior(qa_q2_game *, q2_actor *, qa_builtin_projectile_role, bool *changed,
                        qa_error *);
bool q2_target_damageable(qa_q2_game *, qa_actor_id);
bool q2_target_creature(qa_q2_game *, qa_actor_id, bool *creature, bool *player, qa_error *);
bool q2_projectile_event(qa_q2_game *, qa_actor_id, qa_builtin_event_kind, const char *, int,
                         qa_vec3, qa_vec3, qa_error *);
bool q2_projectile_loop(qa_q2_game *, q2_actor *, const char *, bool stop_previous, qa_error *);
qa_attack q2_projectile_attack(qa_q2_game *, qa_actor_id, const q2_projectile *, int, uint32_t);
bool q2_projectile_noise(qa_q2_game *, const q2_projectile *, qa_vec3, qa_error *);
bool q2_projectile_radius(qa_q2_game *, qa_actor_id, const q2_projectile *, qa_vec3, qa_actor_id,
                          float, float, int, uint32_t, qa_error *);
bool q2_mine_spawn(q2_weapon_call *, q2_projectile_kind, qa_vec3, qa_vec3, float, float, float,
                   float, float, bool, qa_error *);
bool q2_mine_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_mine_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_damage(qa_q2_game *, const qa_attack *, qa_actor_id, float, float, qa_vec3, qa_vec3,
               qa_vec3, bool, qa_error *);
bool q2_prepare_damage(void *, qa_damage_request *, bool *allowed, qa_error *);
bool q2_prepare_radius_damage(void *, qa_damage_request *, bool *allowed, qa_error *);
bool q2_radius_damage(qa_q2_game *, const qa_builtin_radius *, size_t *, qa_error *);
bool q2_definitions(qa_q2_game *, qa_error *);
bool q2_monsters_init(qa_q2_game *, qa_error *);
void q2_monsters_close(qa_q2_game *);
void q2_monsters_begin_map(qa_q2_game *);
void q2_monsters_reclaim(qa_q2_game *);
void q2_monsters_release_actor(qa_q2_game *, qa_actor_id);
bool q2_monster_tick(qa_q2_game *, q2_actor *, qa_error *);
void q2_monster_release_state(q2_actor *);
bool q2_monster_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool q2_monster_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_monster_traits(qa_q2_game *, qa_actor_id, qa_builtin_actor_traits *);
bool q2_monster_pain_advance(qa_q2_game *, qa_actor_id, uint64_t amount_ns);
bool q2_monster_dodge(qa_q2_game *, qa_actor_id target, qa_actor_id attacker, float eta_seconds,
                      const qa_trace_result *, bool gravity, qa_error *);
bool q2_items_init(qa_q2_game *, qa_error *);
bool q2_players_init(qa_q2_game *, qa_error *);
bool q2_entities_init(qa_q2_game *, qa_error *);
void q2_items_close(qa_q2_game *);
void q2_players_close(qa_q2_game *);
void q2_entities_close(qa_q2_game *);
void q2_items_release_state(q2_actor *);
void q2_client_release_state(q2_actor *);
void q2_entity_release_state(q2_actor *);
void q2_entity_unbind(qa_q2_game *, q2_actor *);
bool q2_save_reference(qa_q2_game *, qa_actor_id, qa_q2_saved_reference *, qa_error *);
bool q2_resolve_reference(qa_q2_game *, qa_q2_saved_reference, qa_actor_id *, qa_error *);
bool q2_checkpoint_idle(qa_q2_game *, qa_error *);
bool q2_item_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_client_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_entity_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_entity_prethink(qa_q2_game *, q2_actor *, qa_error *);
bool q2_actor_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_actor_physics(qa_q2_game *, q2_actor *, qa_error *);
bool q2_item_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_item_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool q2_item_traits(qa_q2_game *, qa_actor_id, qa_builtin_actor_traits *);
bool q2_client_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_entity_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_client_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool q2_entity_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool q2_client_traits(qa_q2_game *, qa_actor_id, qa_builtin_actor_traits *);
bool q2_entity_traits(qa_q2_game *, qa_actor_id, qa_builtin_actor_traits *);
bool q2_player_noise(qa_q2_game *, qa_actor_id, qa_vec3, bool secondary, qa_error *);
bool q2_player_tracker_pain(qa_q2_game *, qa_actor_id, uint64_t until_ns, qa_error *);
bool q2_player_invisibility_reveal(qa_q2_game *, qa_actor_id, uint64_t until_ns, qa_error *);
bool q2_player_nuke_blind(qa_q2_game *, qa_actor_id, uint64_t until_ns, bool inside, qa_error *);
bool q2_player_damage_view(qa_q2_game *, qa_actor_id, float pitch, float roll, uint64_t until_ns,
                           qa_error *);
bool q2_item_food_cube(qa_q2_game *, qa_actor_id source, qa_vec3, float scale, int health,
                       qa_vec3 velocity, qa_error *);
bool q2_fire_nuke(qa_q2_game *, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction, float speed,
                  float multiplier, qa_error *);
bool q2_noise_for_actor(qa_q2_game *, qa_actor_id, qa_vec3, bool secondary, qa_error *);
bool q2_nuke_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_nuke_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
q2_trace_frame *q2_scratch_acquire(qa_q2_game *, qa_error *);
q2_trace_frame *q2_nearby(qa_q2_game *, qa_vec3 origin, float radius, qa_error *);
q2_trace_frame *q2_player_roster(qa_q2_game *, qa_error *);
enum {
    Q2_GIB_HEAD = 1u, Q2_GIB_METALLIC = 2u, Q2_GIB_SKINNED = 4u, Q2_GIB_UPRIGHT = 8u,
    Q2_GIB_WIDOW = 16u, Q2_GIB_WIDOW_SIZED = 32u, Q2_GIB_WIDOW_HIT_SOUND = 64u,
    Q2_GIB_WIDOW_LEGS = 128u
};
bool q2_widow_legs_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_widow_gib_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_spawn_gib(qa_q2_game *, qa_actor_id source, const char *model, float damage, uint32_t flags,
                  int skin, float scale, qa_error *);
bool q2_spawn_debris(qa_q2_game *, qa_actor_id source, qa_error *);
bool q2_spawn_model_debris(qa_q2_game *, qa_actor_id source, const char *model, float speed,
                           qa_vec3 origin, qa_error *);
bool q2_spawn_growth(qa_q2_game *, qa_vec3 origin, unsigned size, qa_error *);
bool q2_gib_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_gib_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_gib_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_trap_capture_gibs(qa_q2_game *, q2_actor *, qa_error *);
bool q2_fire_actor_bolt(qa_q2_game *, qa_actor_id source, qa_actor_id credited_owner, qa_vec3 start,
                        qa_vec3 direction, float damage, float speed, uint64_t effects,
                        int means_of_death, bool green, qa_error *);
bool q2_fire_actor_loogie(qa_q2_game *, qa_actor_id source, qa_vec3 start, qa_vec3 direction,
                          qa_error *);
bool q2_fire_actor_rocket(qa_q2_game *, qa_actor_id source, qa_actor_id credited_owner,
                          qa_vec3 start, qa_vec3 direction, float damage, float speed,
                          float splash_damage, float radius, int direct_mod, int splash_mod,
                          qa_error *);
bool q2_green_touch(qa_q2_game *, q2_actor *, const qa_touch_contact *, qa_error *);
bool q2_heat_rocket_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_grapple_weapon(q2_weapon_call *, qa_error *);
bool q2_grapple_fire(q2_weapon_call *, qa_error *);
bool q2_grapple_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_grapple_think(qa_q2_game *, q2_actor *, qa_error *);
bool q2_grapple_reaction(qa_q2_game *, q2_actor *, const qa_damage_outcome *, qa_error *);
bool q2_grapple_released(qa_q2_game *, q2_actor *, qa_error *);
bool q2_lmctf_plasma_weapon(q2_weapon_call *, qa_error *);
bool q2_lmctf_plasma_fire(q2_weapon_call *, qa_error *);
bool q2_lmctf_plasma_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_lmctf_plasma_mode(qa_q2_game *, q2_actor *, qa_error *);
static inline bool q2_frame_bit(uint64_t bits, int frame) {
    return frame >= 0 && frame < 64 && (bits & (UINT64_C(1) << (unsigned)frame)) != 0;
}
static inline bool q2_continues(const q2_weapon_call *c) {
    return c->state->handoff == QA_Q2_PRIMARY_ACTIVE && c->input.attack;
}
static inline uint64_t q2_deadline(uint64_t now, uint64_t duration) {
    return duration > UINT64_MAX - now ? UINT64_MAX : now + duration;
}
static inline uint64_t q2_duration(double seconds) {
    if (seconds <= 0)
        return 0;
    double ns = seconds * 1e9;
    return !isfinite(ns) || ns >= (double)UINT64_MAX ? UINT64_MAX : (uint64_t)(ns + 0.5);
}
#endif
