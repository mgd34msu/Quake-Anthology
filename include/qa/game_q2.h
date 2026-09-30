#ifndef QA_GAME_Q2_H
#define QA_GAME_Q2_H

#include "qa/builtin.h"

typedef struct qa_q2_game qa_q2_game;
typedef struct qa_command_invocation qa_command_invocation;
bool qa_q2_game_grant_arsenal(qa_q2_game *, qa_actor_id, bool ammo, qa_error *);
bool qa_q2_game_give_item(qa_q2_game *, qa_actor_id, size_t, const char *const *, bool *handled,
                         qa_error *);
bool qa_q2_game_console_command(qa_q2_game *, qa_actor_id, const qa_command_invocation *,
                                 bool *handled, qa_error *);
typedef bool (*qa_q2_actor_fn)(void *, qa_actor_id, qa_error *);
qa_actor_id qa_q2_current_actor(const qa_q2_game *);
bool qa_q2_run_actor(qa_q2_game *, qa_actor_id, qa_q2_actor_fn, void *, qa_error *);
typedef enum qa_q2_edition { QA_Q2_CLASSIC, QA_Q2_RERELEASE } qa_q2_edition;
typedef enum qa_q2_product { QA_Q2_BASE, QA_Q2_XATRIX, QA_Q2_ROGUE, QA_Q2_N64 } qa_q2_product;
typedef enum qa_q2_classic_cause_profile {
    QA_Q2_NATIVE_BASE,
    QA_Q2_NATIVE_XATRIX,
    QA_Q2_NATIVE_ROGUE,
    QA_Q2_NATIVE_CTF
} qa_q2_classic_cause_profile;
qa_damage_cause qa_q2_damage_cause(qa_q2_edition, qa_q2_product, int canonical, uint32_t flags);
typedef enum qa_q2_weapon {
    QA_Q2_WEAPON_NONE,
    QA_Q2_BLASTER,
    QA_Q2_SHOTGUN,
    QA_Q2_SUPERSHOTGUN,
    QA_Q2_MACHINEGUN,
    QA_Q2_CHAINGUN,
    QA_Q2_GRENADES,
    QA_Q2_GRENADELAUNCHER,
    QA_Q2_ROCKETLAUNCHER,
    QA_Q2_HYPERBLASTER,
    QA_Q2_RAILGUN,
    QA_Q2_BFG,
    QA_Q2_TRAP,
    QA_Q2_IONRIPPER,
    QA_Q2_PHALANX,
    QA_Q2_TESLA,
    QA_Q2_PROXLAUNCHER,
    QA_Q2_CHAINFIST,
    QA_Q2_DISINTEGRATOR,
    QA_Q2_ETF_RIFLE,
    QA_Q2_HEATBEAM,
    QA_Q2_GRAPPLE,
    QA_Q2_LMCTF_HOOK,
    QA_Q2_LMCTF_PLASMA,
    QA_Q2_WEAPON_COUNT
} qa_q2_weapon;
typedef enum qa_q2_weapon_phase {
    QA_Q2_ACTIVATING,
    QA_Q2_READY,
    QA_Q2_FIRING,
    QA_Q2_DROPPING
} qa_q2_weapon_phase;
typedef enum qa_q2_handoff {
    QA_Q2_PRIMARY_ACTIVE,
    QA_Q2_PRIMARY_HOLSTERING,
    QA_Q2_PRIMARY_HOLSTERED
} qa_q2_handoff;
typedef enum qa_q2_hand { QA_Q2_RIGHT_HAND, QA_Q2_LEFT_HAND, QA_Q2_CENTER_HAND } qa_q2_hand;
typedef enum qa_q2_selection {
    QA_Q2_SELECTED,
    QA_Q2_CURRENT,
    QA_Q2_NOT_OWNED,
    QA_Q2_NO_AMMO,
    QA_Q2_INSUFFICIENT_AMMO
} qa_q2_selection;
typedef enum qa_q2_weapon_rules {
    QA_Q2_WEAPON_RULES_BASE,
    QA_Q2_WEAPON_RULES_CTF,
    QA_Q2_WEAPON_RULES_LMCTF
} qa_q2_weapon_rules;

typedef struct qa_q2_weapon_definition {
    qa_q2_weapon weapon;
    const char *name, *item, *classname, *ammo, *view_model, *world_model;
    int quantity, warning, player_model;
    int activate_last, fire_last, idle_last, deactivate_last;
    uint64_t pauses, fires;
    bool repeating;
} qa_q2_weapon_definition;

typedef struct qa_q2_weapon_input {
    qa_vec3 angles;
    qa_q2_hand hand;
    qa_q2_weapon_rules source_rules;
    float view_height, gravity;
    bool attack, latched_attack, holster, latched_holster, ducked, spectator, notarget;
    bool animate_player, haste, no_stack_double, instant_switch, quick_switch;
    bool infinite_ammo, players_collide, weapon_thunk;
    bool rune_damage;
    uint64_t quad_until_ns, double_until_ns, quad_fire_until_ns;
} qa_q2_weapon_input;

typedef struct qa_q2_weapon_state {
    qa_q2_weapon weapon, last_weapon, pending;
    qa_q2_weapon_phase phase;
    qa_q2_handoff handoff;
    int frame, machinegun_shots, view_skin;
    uint64_t think_ns, fire_finished_ns, empty_sound_ns, last_firing_ns;
    uint64_t grenade_ns, grenade_finished_ns, kick_ns, kick_until_ns;
    bool fire_buffered, latched_attack, source_firing, grenade_blew_up;
    enum { QA_Q2_HAND_UNRESERVED, QA_Q2_HAND_FINITE, QA_Q2_HAND_INFINITE } hand_reservation;
    qa_vec3 kick_origin, kick_angles;
    float kick_seconds, gun_rate;
    qa_string_id loop_sound, view_model;
} qa_q2_weapon_state;

typedef struct qa_q2_weapon_presentation {
    qa_actor_id actor;
    qa_q2_weapon weapon;
    qa_string_id model;
    int player_model, frame, skin;
    float rate;
    qa_vec3 kick_origin, kick_angles;
} qa_q2_weapon_presentation;

typedef enum qa_q2_grapple_kind { QA_Q2_CTF_GRAPPLE, QA_Q2_LMCTF_GRAPPLE } qa_q2_grapple_kind;
typedef enum qa_q2_grapple_phase {
    QA_Q2_GRAPPLE_FLY,
    QA_Q2_GRAPPLE_PULL,
    QA_Q2_GRAPPLE_HANG
} qa_q2_grapple_phase;
typedef struct qa_q2_grapple_pose {
    qa_vec3 angles, gravity_direction, previous_velocity;
    qa_q2_hand hand;
    float view_height, gravity_scale;
} qa_q2_grapple_pose;
typedef struct qa_q2_grapple_motion {
    qa_vec3 previous_velocity;
    bool set_previous_velocity, set_prediction, prediction_suppressed;
} qa_q2_grapple_motion;
typedef struct qa_q2_grapple_options {
    float fly_speed, pull_speed, damage;
    bool players_collide;
} qa_q2_grapple_options;
typedef struct qa_q2_grapple_state {
    qa_actor_id hook;
    qa_q2_grapple_phase phase;
    uint64_t release_ns;
    int hook_state, hook_length;
    bool hook_held, saved_no_knockback, has_saved_no_knockback, equipment_bound;
    qa_q2_weapon_state equipment;
} qa_q2_grapple_state;

typedef struct qa_q2_hand_grenade_options {
    bool enabled, infinite_ammo;
    int initial_ammo, capacity;
} qa_q2_hand_grenade_options;
typedef enum qa_q2_hand_action_kind {
    QA_Q2_HAND_IDLE,
    QA_Q2_HAND_DISARMED,
    QA_Q2_HAND_PREPARING,
    QA_Q2_HAND_COOKING,
    QA_Q2_HAND_RELEASING,
    QA_Q2_HAND_RECOVERING
} qa_q2_hand_action_kind;
typedef enum qa_q2_hand_lifecycle {
    QA_Q2_HAND_ALIVE,
    QA_Q2_HAND_DEAD,
    QA_Q2_HAND_REMOVING,
    QA_Q2_HAND_REMOVED
} qa_q2_hand_lifecycle;
typedef struct qa_q2_hand_action {
    qa_q2_hand_action_kind kind;
    union {
        struct {
            int frame;
            uint64_t next_ns;
            bool release_queued;
        } preparing;
        struct {
            uint64_t expires_ns;
        } cooking;
        struct {
            uint64_t expires_ns, throw_ns;
        } releasing;
        struct {
            uint64_t ready_ns;
            bool require_release;
        } recovering;
    } state;
} qa_q2_hand_action;
typedef struct qa_q2_hand_grenade_state {
    qa_q2_hand_grenade_options options;
    qa_q2_hand_action action;
} qa_q2_hand_grenade_state;
typedef bool (*qa_q2_hand_projection_fn)(void *, qa_actor_id, qa_vec3 angles, qa_vec3 offset,
                                         qa_vec3 *start, qa_vec3 *direction, qa_error *);
typedef struct qa_q2_hand_grenade_input {
    qa_q2_weapon_input weapon;
    bool pressed, held, released;
    qa_q2_hand_lifecycle lifecycle;
    void *project_context;
    qa_q2_hand_projection_fn project;
} qa_q2_hand_grenade_input;

typedef struct qa_q2_options {
    qa_actor_owner owner;
    qa_q2_edition edition;
    qa_q2_product product;
    bool deathmatch, cooperative;
    uint32_t deathmatch_flags;
    int skill;
    uint64_t seed;
    uint64_t frame_ns;
} qa_q2_options;
typedef struct qa_q2_monster_spawn_options {
    const char *classname;
    uint32_t spawnflags;
    float scale, health_multiplier;
    qa_actor_id enemy, commander;
    bool summoned, triggered;
} qa_q2_monster_spawn_options;
typedef enum qa_q2_monster_action_kind {
    QA_Q2_MONSTER_USE,
    QA_Q2_MONSTER_TOUCH,
    QA_Q2_MONSTER_BLOCKED,
    QA_Q2_MONSTER_DODGE,
    QA_Q2_MONSTER_SET_ENEMY,
    QA_Q2_MONSTER_FOUND_TARGET
} qa_q2_monster_action_kind;
bool qa_q2_monster_spawn(qa_q2_game *, qa_actor_id, const qa_q2_monster_spawn_options *,
                         qa_error *);
bool qa_q2_monster_action(qa_q2_game *, qa_actor_id, qa_q2_monster_action_kind, qa_actor_id other,
                          float value, qa_error *);

typedef struct qa_q2_hooks {
    void *context;
    bool (*weapon_view)(void *, const qa_q2_weapon_presentation *, qa_error *);
    bool (*noise)(void *, qa_actor_id, qa_vec3, bool secondary, qa_error *);
    bool (*ammo_changed)(void *, qa_actor_id, qa_item_id, qa_error *);
    float (*quad_multiplier)(void *, qa_actor_id);
    float (*damage_multiplier)(void *, qa_actor_id);
    uint64_t (*firing_interval)(void *, qa_actor_id, uint64_t);
    bool (*can_target)(void *, qa_actor_id, qa_actor_id);
    bool (*tracker_pain)(void *, qa_actor_id, uint64_t until_ns, qa_error *);
    bool (*invisibility_reveal)(void *, qa_actor_id, uint64_t until_ns, qa_error *);
    bool (*food_cube)(void *, qa_actor_id source, qa_vec3 origin, float scale, int health,
                      qa_vec3 velocity, qa_error *);
    bool (*nuke_blind)(void *, qa_actor_id, uint64_t until_ns, bool inside_radius, qa_error *);
    bool (*ctf_strength_sound)(void *, qa_actor_id, bool *handled, qa_error *);
    bool (*ctf_haste_sound)(void *, qa_actor_id, qa_error *);
    bool (*grapple_pose)(void *, qa_actor_id, qa_q2_grapple_kind, qa_q2_grapple_pose *, qa_error *);
    bool (*grapple_motion)(void *, qa_actor_id, const qa_q2_grapple_motion *, qa_error *);
    bool (*grapple_can_attach)(void *, qa_actor_id owner, qa_actor_id target, qa_q2_grapple_kind);
    bool (*grapple_can_damage)(void *, qa_actor_id owner, qa_actor_id target, qa_q2_grapple_kind);
    bool (*grapple_player_hit)(void *, qa_actor_id);
    /* A successful begin must be paired with end, including failure paths. */
    bool (*lag_begin)(void *, qa_actor_id, qa_vec3, qa_vec3, qa_error *);
    bool (*lag_end)(void *, qa_error *);
} qa_q2_hooks;

/* Construction retains private state without admitting a session component.
 * The application owns the instance throughout component admission/retirement. */
bool qa_q2_create(const qa_builtin_services *, const qa_q2_options *, const qa_q2_hooks *,
                  qa_q2_game **, qa_error *);
/* Borrowed component; removal never frees the instance. */
qa_component qa_q2_component(qa_q2_game *);
/* Requires a detached instance at a session safe point. Remove an admitted
 * component before destroying its instance; keep the shared services alive. */
bool qa_q2_destroy(qa_q2_game *, qa_error *);
/* After shared world retirement and source clock reset, before actor admission.
 * Registry must be empty. Names are interned before publication; zero spawn
 * point means empty. Connection identity and player carry stay with the host. */
bool qa_q2_begin_map(qa_q2_game *, qa_string_id map_name, qa_string_id spawn_point, qa_error *);
/* Provider-owned temporary protection, without reading composed combat state. */
bool qa_q2_timed_invulnerability(qa_q2_game *, qa_actor_id);
bool qa_q2_powerups_present(qa_q2_game *, qa_actor_id);
const qa_q2_weapon_definition *qa_q2_weapon_definition_at(const qa_q2_game *, qa_q2_weapon);
qa_q2_weapon qa_q2_weapon_from_classname(const qa_q2_game *, const char *);
bool qa_q2_weapon_bind(qa_q2_game *, qa_actor_id, qa_q2_weapon, qa_error *);
bool qa_q2_weapon_read(qa_q2_game *, qa_actor_id, qa_q2_weapon_state *, qa_error *);
bool qa_q2_weapon_restore(qa_q2_game *, qa_actor_id, const qa_q2_weapon_state *, qa_error *);
bool qa_q2_weapon_select(qa_q2_game *, qa_actor_id, qa_q2_weapon, bool allow_empty,
                         qa_q2_selection *, qa_error *);
bool qa_q2_lmctf_plasma_mode(qa_q2_game *, qa_actor_id, bool *bounce, qa_error *);
bool qa_q2_weapon_holster(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_weapon_resume(qa_q2_game *, qa_actor_id, const qa_q2_weapon_input *, qa_q2_weapon,
                         qa_error *);
bool qa_q2_weapon_can_drop(qa_q2_game *, qa_actor_id, qa_q2_weapon, bool *, qa_error *);
/* Retains held controls and attack edges without advancing the arsenal. The
 * existing weapon checkpoint owns both the controls and the pending edge. */
bool qa_q2_weapon_controls(qa_q2_game *, qa_actor_id, const qa_q2_weapon_input *, qa_error *);
bool qa_q2_weapon_controls_read(qa_q2_game *, qa_actor_id, qa_q2_weapon_input *, qa_error *);
/* Called in the owning actor's source turn, regardless of its character family.
 * Exact integer source times avoid per-frame conversion and drifting deadlines. */
bool qa_q2_weapon_tick(qa_q2_game *, qa_actor_id, const qa_q2_weapon_input *, uint64_t now_ns,
                       uint64_t frame_ns, qa_error *);
bool qa_q2_weapon_silencer(qa_q2_game *, qa_actor_id, int charges, qa_error *);
/* Clears owned input latches only, without callbacks, allocation, weapon
 * advancement, or changes to cooked grenades and reserved ammunition. */
bool qa_q2_clear_input(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_actor_released(qa_q2_game *, qa_actor_record, qa_error *);
/* Release native tracker continuations attached to this generation, including
 * targets whose character is supplied by another provider. */
bool qa_q2_clear_trackers(qa_q2_game *, qa_actor_id target, qa_error *);
bool qa_q2_physics_read(qa_q2_game *, qa_actor_id, qa_physics_properties *);
bool qa_q2_physics_write(qa_q2_game *, qa_actor_id, const qa_physics_properties *, qa_error *);
bool qa_q2_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool qa_q2_projectile_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool qa_q2_damage_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool qa_q2_item_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool qa_q2_actor_traits(qa_q2_game *, qa_actor_id, qa_builtin_actor_traits *);
uint64_t qa_q2_actor_extra_effects(qa_q2_game *, qa_actor_id);
bool qa_q2_actor_tick(qa_q2_game *, qa_actor_id, uint64_t now_ns, uint64_t frame_ns, qa_error *);
typedef struct qa_q2_projectile_view {
    qa_string_id model, loop_sound;
    uint64_t effects;
    uint32_t render_flags;
    int frame, skin;
    float scale, alpha;
    qa_vec3 beam_end;
    bool visible, beam;
} qa_q2_projectile_view;
bool qa_q2_projectile_read(qa_q2_game *, qa_actor_id, qa_q2_projectile_view *);
typedef struct qa_q2_saved_reference {
    qa_saved_actor_id actor;
    bool present;
} qa_q2_saved_reference;
typedef struct qa_q2_runtime_checkpoint {
    uint32_t version;
    qa_q2_edition edition;
    qa_q2_product product;
    qa_builtin_random random;
    uint32_t rerelease_words[624], rerelease_index;
    uint64_t rerelease_draws, sequence, actor_sequence, now_ns, frame_ns;
    qa_q2_grapple_options grapple_options;
    bool lmctf_plasma_quad;
    uint8_t widow_damage_multiplier;
    uint8_t widow_shot_phase;
} qa_q2_runtime_checkpoint;
typedef struct qa_q2_projectile_checkpoint {
    uint32_t kind;
    qa_attack attack;
    qa_q2_saved_reference attacker, inflictor, projectile, owner, enemy, child;
    qa_vec3 movedir;
    float damage, kick, radius_damage, radius, gravity, speed, delay, captured_mass, turn_fraction;
    uint64_t born_ns, expire_ns, next_ns, effect_ns, effects;
    uint32_t render_flags, gib_flags;
    qa_string_id classname, model, loop_sound;
    int direct_mod, splash_mod, frame, phase, wait, skin;
    float scale, alpha;
    bool hand, held, armed, visible, gekk, dodgeable;
} qa_q2_projectile_checkpoint;
typedef struct qa_q2_actor_checkpoint {
    uint32_t version;
    uint64_t source_order;
    uint64_t extra_effects;
    bool lmctf_plasma_bounce;
    bool weapon_bound, physics_bound;
    qa_q2_weapon_state weapon;
    qa_q2_weapon_input input;
    int silencer;
    qa_physics_properties physics;
    qa_q2_saved_reference physics_enemy, physics_goal;
    qa_q2_projectile_checkpoint projectile;
    qa_q2_grapple_state grapples[2];
    qa_q2_saved_reference grapple_hooks[2];
    bool hand_grenade_bound;
    qa_q2_hand_grenade_state hand_grenade;
} qa_q2_actor_checkpoint;
/* Value snapshots; the save layer encodes fields and remaps resource IDs.
 * Embedded attack/body authority references are cleared and stored above as
 * saved references. Restore requires the shared actors and resources first. */
bool qa_q2_runtime_capture(qa_q2_game *, qa_q2_runtime_checkpoint *, qa_error *);
bool qa_q2_runtime_restore(qa_q2_game *, const qa_q2_runtime_checkpoint *, qa_error *);
bool qa_q2_actor_capture(qa_q2_game *, qa_actor_id, qa_q2_actor_checkpoint *, qa_error *);
bool qa_q2_actor_restore(qa_q2_game *, qa_actor_id, const qa_q2_actor_checkpoint *, qa_error *);
bool qa_q2_bad_area(qa_q2_game *, qa_actor_id actor, qa_vec3 origin, qa_actor_id *hazard,
                    qa_error *);
bool qa_q2_accept_ground(qa_q2_game *, qa_actor_id, qa_vec3 origin, bool *accepted, qa_error *);
bool qa_q2_before_monster_step(qa_q2_game *, qa_actor_id, qa_vec3 *displacement,
                               bool *handled, qa_error *);
bool qa_q2_spawn_bad_area(qa_q2_game *, qa_bounds absolute, uint64_t lifespan_ns, qa_actor_id owner,
                          qa_actor_id *area, qa_error *);
bool qa_q2_mark_tesla_area(qa_q2_game *, qa_actor_id observer, qa_actor_id tesla, bool *created,
                           qa_error *);

bool qa_q2_grapple_configure(qa_q2_game *, const qa_q2_grapple_options *, qa_error *);
bool qa_q2_grapple_read(qa_q2_game *, qa_actor_id, qa_q2_grapple_kind, qa_q2_grapple_state *,
                        qa_error *);
bool qa_q2_grapple_reset(qa_q2_game *, qa_actor_id, qa_q2_grapple_kind, qa_error *);
bool qa_q2_grapple_offhand(qa_q2_game *, qa_actor_id, qa_q2_grapple_kind, bool pressed, qa_error *);
bool qa_q2_grapple_hold(qa_q2_game *, qa_actor_id, bool pressed, qa_error *);
/* Independent equipment slots reuse the native weapon frame interpreter. */
bool qa_q2_grapple_equipment_resume(qa_q2_game *, qa_actor_id, qa_q2_grapple_kind, qa_error *);
bool qa_q2_grapple_equipment_holster(qa_q2_game *, qa_actor_id, qa_q2_grapple_kind, qa_error *);
bool qa_q2_grapple_equipment_tick(qa_q2_game *, qa_actor_id, qa_q2_grapple_kind,
                                  const qa_q2_weapon_input *, uint64_t now_ns, uint64_t frame_ns,
                                  qa_error *);
/* Once after selected movement. damage_pulse follows the classic 10 Hz source
 * damage cadence independently of rerelease command/movement cadence. */
bool qa_q2_grapple_after_movement(qa_q2_game *, qa_actor_id, bool damage_pulse, uint64_t now_ns,
                                  uint64_t frame_ns, qa_error *);
float qa_q2_grapple_gravity_scale(qa_q2_game *, qa_actor_id);
/* Independent action reservations debit the canonical shared grenade entry
 * once. Send REMOVING while the last actor/body pose is still available. */
bool qa_q2_hand_grenade_configure(qa_q2_game *, qa_actor_id, const qa_q2_hand_grenade_options *,
                                  qa_error *);
typedef struct qa_q2_hand_grenade_admission {
    qa_q2_game *game;
    qa_actor_id actor;
    qa_q2_hand_grenade_options options;
    qa_inventory_entry initial_ammo;
    uint64_t revision, body_serial;
    void *expected_actor, *prepared_actor;
    bool missing_ammo, active;
} qa_q2_hand_grenade_admission;
/* The successful prepare output has one owner and must not be copied. It owns
 * only its unattached allocation. Prepare canonical initial_ammo even when an
 * entry exists: inventory admission preserves its current count. Validate all
 * participants, then commit without intervening callbacks. Abort consumes any
 * remaining preparation, including after a failed or successful commit. */
bool qa_q2_hand_grenade_prepare(qa_q2_game *, qa_actor_id, const qa_q2_hand_grenade_options *,
                                qa_q2_hand_grenade_admission *, qa_error *);
bool qa_q2_hand_grenade_validate(const qa_q2_hand_grenade_admission *, qa_error *);
bool qa_q2_hand_grenade_commit(qa_q2_hand_grenade_admission *, qa_error *);
void qa_q2_hand_grenade_abort(qa_q2_hand_grenade_admission *);
bool qa_q2_hand_grenade_step(qa_q2_game *, qa_actor_id, const qa_q2_hand_grenade_input *,
                             uint64_t now_ns, uint64_t frame_ns, qa_error *);
bool qa_q2_hand_grenade_read(qa_q2_game *, qa_actor_id, qa_q2_hand_grenade_state *, bool *bound,
                             qa_error *);
bool qa_q2_hand_grenade_restore(qa_q2_game *, qa_actor_id, const qa_q2_hand_grenade_state *,
                                qa_error *);

#include "qa/game_q2_monsters.h"

#endif
