#ifndef QA_GAME_Q3_H
#define QA_GAME_Q3_H

#include "qa/builtin.h"
#include "qa/movement.h"
#include "qa/physics.h"
#include "qa/rankings.h"
#include "qa/game_q3_product.h"
#include "qa/game_q3_client_types.h"
#include "qa/game_q3_source_types.h"
#include "qa/game_q3_shader_remap.h"

typedef struct qa_q3_game qa_q3_game;
bool qa_q3_game_random(qa_q3_game *, float *, qa_error *);
bool qa_q3_game_rand(qa_q3_game *, uint32_t *, qa_error *);
bool qa_q3_game_grant_arsenal(qa_q3_game *, qa_actor_id, bool ammo, qa_error *);
bool qa_q3_game_give_item(qa_q3_game *, qa_actor_id, size_t, const char *const *, bool *handled,
                         qa_error *);
typedef struct qa_command_invocation qa_command_invocation;
typedef enum qa_q3_weapon {
    QA_Q3_W_NONE,
    QA_Q3_W_GAUNTLET,
    QA_Q3_W_MACHINEGUN,
    QA_Q3_W_SHOTGUN,
    QA_Q3_W_GRENADE,
    QA_Q3_W_ROCKET,
    QA_Q3_W_LIGHTNING,
    QA_Q3_W_RAIL,
    QA_Q3_W_PLASMA,
    QA_Q3_W_BFG,
    QA_Q3_W_GRAPPLE,
    QA_Q3_W_NAIL,
    QA_Q3_W_PROX,
    QA_Q3_W_CHAINGUN,
    QA_Q3_WEAPON_COUNT
} qa_q3_weapon;
const char *qa_q3_weapon_identity_name(qa_q3_weapon);
typedef enum qa_q3_powerup {
    QA_Q3_P_NONE,
    QA_Q3_P_QUAD,
    QA_Q3_P_BATTLESUIT,
    QA_Q3_P_HASTE,
    QA_Q3_P_INVIS,
    QA_Q3_P_REGEN,
    QA_Q3_P_FLIGHT,
    QA_Q3_P_REDFLAG,
    QA_Q3_P_BLUEFLAG,
    QA_Q3_P_NEUTRALFLAG,
    QA_Q3_P_SCOUT,
    QA_Q3_P_GUARD,
    QA_Q3_P_DOUBLER,
    QA_Q3_P_AMMOREGEN,
    QA_Q3_P_INVULNERABILITY,
    QA_Q3_POWERUP_COUNT
} qa_q3_powerup;
typedef enum qa_q3_holdable {
    QA_Q3_H_NONE,
    QA_Q3_H_TELEPORTER,
    QA_Q3_H_MEDKIT,
    QA_Q3_H_KAMIKAZE,
    QA_Q3_H_PORTAL,
    QA_Q3_H_INVULNERABILITY
} qa_q3_holdable;
typedef enum qa_q3_item_kind {
    QA_Q3_ITEM_BAD,
    QA_Q3_ITEM_WEAPON,
    QA_Q3_ITEM_AMMO,
    QA_Q3_ITEM_ARMOR,
    QA_Q3_ITEM_HEALTH,
    QA_Q3_ITEM_POWERUP,
    QA_Q3_ITEM_HOLDABLE,
    QA_Q3_ITEM_PERSISTENT,
    QA_Q3_ITEM_TEAM
} qa_q3_item_kind;
typedef struct qa_q3_item {
    const char *classname, *name, *model, *secondary_model, *icon, *sound;
    qa_q3_item_kind kind;
    int32_t tag, quantity;
} qa_q3_item;
const qa_q3_item *qa_q3_items(qa_q3_product, size_t *count);
const qa_q3_item *qa_q3_find_item(qa_q3_product, const char *classname, uint32_t *index);

typedef enum qa_q3_selection {
    QA_Q3_CHARACTER = 1u << 0,
    QA_Q3_ARSENAL = 1u << 1,
    QA_Q3_EFFECTS = 1u << 2,
    QA_Q3_COMBAT = 1u << 3,
    QA_Q3_EQUIPMENT = 1u << 4,
    QA_Q3_NATIVE_PLAYER = 15u,
    QA_Q3_ALL_SELECTIONS = QA_Q3_NATIVE_PLAYER | QA_Q3_EQUIPMENT
} qa_q3_selection;
typedef enum qa_q3_weapon_phase {
    QA_Q3_READY,
    QA_Q3_RAISING,
    QA_Q3_DROPPING,
    QA_Q3_FIRING
} qa_q3_weapon_phase;
typedef enum qa_q3_external_slot {
    QA_Q3_SLOT_ACTIVE,
    QA_Q3_SLOT_HOLSTER_REQUESTED,
    QA_Q3_SLOT_DROPPING,
    QA_Q3_SLOT_HOLSTERED,
    QA_Q3_SLOT_RESUME_REQUESTED
} qa_q3_external_slot;
typedef struct qa_q3_cutscene_state {
    qa_vec3 origin, angles, view_offset;
    bool active;
} qa_q3_cutscene_state;
typedef struct qa_q3_player_state {
    uint32_t selections, flags, event_sequence, spawn_count;
    int32_t events[2], event_parameters[2];
    uint32_t entity_event_sequence;
    int32_t external_event, external_event_parameter, external_event_time;
    qa_q3_weapon weapon, requested_weapon;
    qa_q3_weapon_phase weapon_phase;
    qa_q3_external_slot external_slot;
    int32_t weapon_time_ms, max_health, handicap;
    int32_t powerups[QA_Q3_POWERUP_COUNT], ammo_time_ms[QA_Q3_WEAPON_COUNT];
    qa_item_id ammo_regeneration_items[QA_Q3_WEAPON_COUNT];
    qa_q3_powerup persistent;
    qa_q3_holdable holdable;
    int32_t invulnerability_until, respawn_after, time_residual, air_out_time;
    int32_t drowning_damage, pain_after, reward_until, accuracy_shots, accuracy_hits;
    int32_t rail_streak, impressive_count, denied_rewards, player_events;
    int32_t deaths, excellent_count, gauntlet_frag_count, last_kill_ms, dead_yaw;
    int32_t last_killed_client, last_hurt_client, last_hurt_mod;
    int32_t legs_animation, torso_animation, legs_timer_ms, torso_timer_ms;
    int32_t delta_yaw_word, ground_entity_number;
    int32_t delta_pitch_word, delta_roll_word, teleport_lock_ms;
    uint64_t teleport_revision;
    int32_t damage_event, damage_count, damage_pitch, damage_yaw, last_command_ms;
    int32_t command_time_ms, client_number;
    int32_t portal_id;
    int32_t rank, persistent_team, generic1;
    int32_t defend_count, assist_count, captures;
    int32_t fly_sound_after, jumppad_entity, jumppad_frame, pmove_frame_count;
    int32_t last_command_angles[3];
    float damage_blood, damage_armor, damage_knockback;
    qa_vec3 damage_from;
    qa_string_id loop_sound;
    float fractional_weapon_ms, view_height;
    qa_vec3 view_angles, grapple_point;
    qa_q3_cutscene_state cutscene;
    qa_actor_id hook, attached_mine, persistent_item, portal;
    bool spectator, dead, gibbed, respawned, use_item_held, fire_held, grapple_pull;
    bool damage_from_world, noclip, invulnerability_expanded, death_cleanup_done, gauntlet_contact;
    bool no_target;
} qa_q3_player_state;

typedef struct qa_q3_rules {
    int32_t game_type, proximity_timeout_ms, force_respawn_seconds, dmflags;
    float quad_factor, knockback, weapon_respawn_seconds, team_weapon_respawn_seconds;
    float gravity;
    bool friendly_fire, blood, intermission;
} qa_q3_rules;
typedef enum qa_q3_source_award {
    QA_Q3_AWARD_IMPRESSIVE = 9,
    QA_Q3_AWARD_EXCELLENT = 10,
    QA_Q3_AWARD_DEFEND = 11,
    QA_Q3_AWARD_ASSIST = 12,
    QA_Q3_AWARD_GAUNTLET = 13,
    QA_Q3_AWARD_CAPTURE = 14
} qa_q3_source_award;
enum {
    QA_Q3_SOURCE_PM_COMMAND = 1, QA_Q3_SOURCE_PM_EVENTS = 2,
    QA_Q3_SOURCE_PM_FRAME = 4, QA_Q3_SOURCE_PM_JUMPPAD = 8,
    QA_Q3_SOURCE_PM_DELTAS = 16, QA_Q3_SOURCE_PM_VIEW = 32,
    QA_Q3_SOURCE_PM_ALL = 63
};
typedef enum qa_q3_obelisk_think {
    QA_Q3_OBELISK_NONE, QA_Q3_OBELISK_REGEN, QA_Q3_OBELISK_RESPAWN
} qa_q3_obelisk_think;
typedef enum qa_q3_obelisk_die_stage {
    QA_Q3_OBELISK_DIE_TEAM_SCORE, QA_Q3_OBELISK_DIE_PLAYER_SCORE
} qa_q3_obelisk_die_stage;
typedef struct qa_q3_obelisk_state {
    qa_actor_id model;
    int32_t team, next_think_ms;
    qa_q3_obelisk_think think;
} qa_q3_obelisk_state;
typedef struct qa_q3_obelisk_continuation {
    qa_actor_id model;
    int32_t next_think_ms;
    qa_q3_obelisk_think think;
} qa_q3_obelisk_continuation;
typedef struct qa_q3_obelisk_settings {
    int32_t health, regen_period_seconds, regen_amount, respawn_delay_seconds;
} qa_q3_obelisk_settings;
typedef struct qa_q3_hooks {
    void *context;
    bool (*source_settings_update)(void *, const qa_source_frame *, qa_error *);
    bool (*source_world_init)(void *, qa_error *);
    bool (*source_team_items)(void *, qa_error *);
    bool (*source_client_run)(void *, qa_actor_id, const qa_source_frame *, qa_error *);
    bool (*source_client_end)(void *, qa_actor_id, const qa_source_frame *, qa_error *);
    bool (*source_end_frame)(void *, const qa_source_frame *, qa_error *);
    bool (*source_movement_state)(void *, qa_actor_id, const qa_q3_player_state *,
                                   uint32_t fields, qa_error *);
    /* Match and map owners handle their own obligations; selection does not
     * give this provider authority over the session's mode or target graph. */
    bool (*objective_pickup)(void *, qa_actor_id item, qa_actor_id player, uint32_t item_index,
                             bool *accepted, qa_error *);
    bool (*death)(void *, qa_actor_id victim, const qa_damage_request *, qa_error *);
    bool (*respawn)(void *, qa_actor_id, qa_error *);
    bool (*teleport_destination)(void *, qa_actor_id, qa_vec3 *, qa_vec3 *, qa_error *);
    bool (*primary_attack_allowed)(void *, qa_actor_id);
    int32_t (*source_team)(void *, qa_actor_id);
    int32_t (*entity_number)(void *, qa_actor_id);
    bool (*objective_drop)(void *, qa_actor_id player, qa_error *);
    bool (*objective_dropped)(void *, qa_actor_id item, uint32_t item_index, qa_error *);
    bool (*objective_admitted)(void *, qa_actor_id item, uint32_t item_index,
                               bool finished, qa_error *);
    bool (*objective_expired)(void *, qa_actor_id item, uint32_t item_index, qa_error *);
    bool (*objective_nodrop)(void *, qa_actor_id item, uint32_t item_index, qa_error *);
    bool (*source_flags_cleared)(void *, qa_actor_id player, qa_error *);
    bool (*source_obelisk_settings)(void *, qa_q3_obelisk_settings *, qa_error *);
    bool (*objective_obelisk_admitted)(void *, qa_actor_id, qa_actor_id, int32_t, qa_error *);
    bool (*objective_obelisk_touch)(void *, qa_actor_id, qa_actor_id, qa_error *);
    bool (*objective_obelisk_die)(void *, qa_actor_id, qa_actor_id,
                                  qa_q3_obelisk_die_stage, qa_error *);
    bool (*objective_obelisk_pain)(void *, qa_actor_id, qa_actor_id, int32_t, qa_error *);
    bool (*foreign_mover_read)(void *, qa_actor_id, qa_q3_mover_state *);
    bool (*foreign_mover_write)(void *, qa_actor_id, const qa_q3_mover_state *, qa_error *);
    qa_actor_owner (*combat_provider)(void *, qa_actor_id target, qa_actor_owner fallback);
    bool (*mover_action)(void *, qa_q3_mover_action, qa_actor_id, qa_actor_id, int32_t, qa_error *);
    /* Queue reports and copy borrowed strings. These callbacks may inspect,
     * but must not mutate or destroy the provider. A missing sink skips reports. */
    bool (*ranking_report)(void *, const qa_ranking_source_report *, qa_error *);
    bool (*ranking_warmup)(void *);
    bool (*cheats_enabled)(void *);
    bool (*console_print)(void *, const char *, qa_error *);
    bool (*source_log)(void *, const char *, qa_error *);
    bool (*postgame_cvar_integer)(void *, const char *, int32_t *, qa_error *);
    bool (*memory_debug_integer)(void *, int32_t *, qa_error *);
    bool (*source_hurt_carrier)(void *, qa_actor_id target, qa_actor_id attacker, qa_error *);
    bool (*source_frag_bonuses)(void *, qa_actor_id target, qa_actor_id attacker, qa_error *);
    bool (*source_client_death)(void *, qa_actor_id, qa_error *);
    bool (*source_death_score)(void *, qa_actor_id target, qa_actor_id attacker, qa_error *);
    bool (*client_print)(void *, qa_actor_id, const char *, qa_error *);
    bool (*server_command)(void *, int32_t source_slot_or_minus_one, const char *, qa_error *);
    /* Called after the actual source slot commits. Text is borrowed only for
     * this call; a failing notification leaves the source mutation committed. */
    bool (*configstring_changed)(void *, uint32_t index, const char *, qa_error *);
    bool (*console_motion)(void *, qa_actor_id, bool noclip, qa_error *);
    bool (*grant_arsenal)(void *, qa_actor_id, bool ammo, bool *handled, qa_error *);
    bool (*give_item)(void *, qa_actor_id, size_t, const char *const *, bool *handled, qa_error *);
    bool (*suicide)(void *, qa_actor_id, qa_error *);
    bool (*award)(void *, qa_actor_id, qa_q3_source_award, qa_error *);
} qa_q3_hooks;
typedef struct qa_q3_options {
    qa_builtin_services services;
    qa_actor_owner owner;
    qa_q3_product product;
    qa_q3_rules rules;
    qa_q3_hooks hooks;
    uint32_t random_seed, max_clients;
} qa_q3_options;
qa_q3_rules qa_q3_default_rules(void);
bool qa_q3_create(const qa_q3_options *, qa_q3_game **, qa_error *);
/* Component and combat policy descriptors borrow the game. Their owner must
 * detach both before destroying the game. */
qa_component qa_q3_component(qa_q3_game *);
bool qa_q3_combat_policy(qa_q3_game *, qa_combat_policy *, qa_error *);
bool qa_q3_destroy(qa_q3_game *, qa_error *);
bool qa_q3_destroy_ready(const qa_q3_game *);
bool qa_q3_pickups_rebind(qa_q3_game *, qa_error *);
bool qa_q3_inventory_admit(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_inventory_rebind(qa_q3_game *, qa_error *);
bool qa_q3_rules_read(const qa_q3_game *, qa_q3_rules *, qa_error *);
bool qa_q3_source_clock(const qa_q3_game *, int32_t *source_time_ms, qa_error *);
bool qa_q3_set_rules(qa_q3_game *, const qa_q3_rules *, qa_error *);
/* G_UpdateCvars writes the source numerical values without construction-time
 * range restrictions. Call from the real source settings phase. */
bool qa_q3_set_source_rules(qa_q3_game *, const qa_q3_rules *, qa_error *);
bool qa_q3_game_console_command(qa_q3_game *, qa_actor_id, const qa_command_invocation *,
                                bool *handled, qa_error *);
qa_item_id qa_q3_weapon_item(const qa_q3_game *, qa_q3_weapon, bool ammo);
qa_item_id qa_q3_item_identity(const qa_q3_game *, uint32_t item_index);
bool qa_q3_bind_player(qa_q3_game *, qa_actor_id, uint32_t selections, int32_t handicap,
                       qa_error *);
typedef struct qa_q3_player_binding {
    qa_actor_id actor;
    uint64_t token;
    uint32_t prior_selections, selections;
    int32_t handicap;
    bool created;
} qa_q3_player_binding;
/* Zero-initialize the transaction. Begin reserves this actor/generation but
 * does not publish selections; commit is the only mutation and rollback only
 * releases that reservation. Competing binds/restores fail while it is open. */
bool qa_q3_bind_player_begin(qa_q3_game *, qa_actor_id, uint32_t selections, int32_t handicap,
                             qa_q3_player_binding *, qa_error *);
bool qa_q3_bind_player_validate(qa_q3_game *, const qa_q3_player_binding *, qa_error *);
bool qa_q3_bind_player_commit(qa_q3_game *, qa_q3_player_binding *, qa_error *);
bool qa_q3_bind_player_rollback(qa_q3_game *, qa_q3_player_binding *, qa_error *);
bool qa_q3_player_read(const qa_q3_game *, qa_actor_id, qa_q3_player_state *);
bool qa_q3_player_notarget(qa_q3_game *, qa_actor_id, bool *enabled, qa_error *);
bool qa_q3_player_set_view(qa_q3_game *, qa_actor_id, qa_vec3 angles, float view_height,
                           qa_error *);
/* Private character presentation and input suppression only. The application
 * mutates the independently selected movement family plus the shared body,
 * combat traits, link and motion discontinuity. A player spawn clears this. */
bool qa_q3_character_cutscene(qa_q3_game *, qa_actor_id, qa_vec3 origin, qa_vec3 angles,
                              qa_vec3 view_offset, qa_error *);
bool qa_q3_character_cutscene_clear(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_actor_traits(const qa_q3_game *, qa_actor_id, qa_builtin_actor_traits *);
bool qa_q3_player_restore(qa_q3_game *, qa_actor_id, const qa_q3_player_state *, qa_error *);
bool qa_q3_player_request_weapon(qa_q3_game *, qa_actor_id, qa_q3_weapon, qa_error *);
/* Replace the native quad deadline using this source's current clock. */
bool qa_q3_player_quad(qa_q3_game *, qa_actor_id, uint64_t duration_ns, qa_error *);
/* Extend an active native quad deadline; otherwise start from this source's clock. */
bool qa_q3_player_quad_stack(qa_q3_game *, qa_actor_id, uint64_t duration_ns, qa_error *);
bool qa_q3_spawn_player(qa_q3_game *, qa_actor_id, const qa_body_state *, qa_team_id, qa_error *);
typedef struct qa_q3_controls {
    bool attack, use_holdable, prediction;
    bool gauntlet_contact_known, gauntlet_contact;
    qa_q3_weapon requested_weapon;
    /* Independent equipment owns hook press/release while primary fire is idle.
     */
    bool grapple_independent;
} qa_q3_controls;
/* Foreign input dialects provide semantic controls here; their button words
 * are never interpreted as Q3 commands. Prediction consumes the same weapon
 * state machine but does not create server missiles, damage, or holdables. */
bool qa_q3_arsenal_step(qa_q3_game *, qa_actor_id, const qa_q3_controls *, float elapsed_ms,
                        qa_error *);
bool qa_q3_player_command(qa_q3_game *, qa_actor_id, const qa_movement_command *, float elapsed_ms,
                          qa_error *);
bool qa_q3_player_timers(qa_q3_game *, qa_actor_id, int32_t elapsed_ms, qa_error *);
bool qa_q3_player_effects(qa_q3_game *, qa_actor_id, int32_t elapsed_ms, int32_t water_level,
                          int32_t water_type, bool noclip, qa_error *);
bool qa_q3_before_reaction(qa_q3_game *, const qa_damage_outcome *, qa_error *);
bool qa_q3_timed_invulnerability(const qa_q3_game *, qa_actor_id);
bool qa_q3_damage_allowed(const qa_q3_game *, const qa_damage_request *);
bool qa_q3_player_end_frame(qa_q3_game *, qa_actor_id, int32_t water_level, int32_t water_type,
                            qa_error *);
bool qa_q3_damage_reaction(qa_q3_game *, const qa_damage_outcome *, qa_error *);
bool qa_q3_player_death_cleanup(qa_q3_game *, qa_actor_id, qa_error *);
/* A foreign character's later corpse-gib callback cancels the attached Q3
 * death effect, just as native Q3 body_die does. */
bool qa_q3_cancel_death_effects(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_set_weapon_slot(qa_q3_game *, qa_actor_id, bool holster, qa_error *);
bool qa_q3_map_ammo_regeneration(qa_q3_game *, qa_actor_id, qa_q3_weapon source_weapon,
                                 qa_item_id selected_ammo, qa_error *);
bool qa_q3_fire_weapon(qa_q3_game *, qa_actor_id, qa_q3_weapon, qa_error *);
typedef struct qa_q3_grapple_state {
    qa_actor_id hook;
    qa_vec3 point;
    bool active, fire_held;
} qa_q3_grapple_state;
/* This is Q3 grapple intent for the selected movement owner. Native Q3 pulls
 * toward point - forward*16 at distance*10 within 100 units, otherwise 800;
 * its ground plane clears before the ordinary air step. */
bool qa_q3_grapple_read(const qa_q3_game *, qa_actor_id, qa_q3_grapple_state *);
bool qa_q3_release_grapple(qa_q3_game *, qa_actor_id, qa_error *);
/* Consume the expected retained holdable and emit its source predictable event.
 * Prediction consumes its private state without applying server-side effects.
 */
bool qa_q3_activate_holdable(qa_q3_game *, qa_actor_id, qa_q3_holdable expected, bool prediction,
                             qa_error *);
bool qa_q3_movement_environment(qa_q3_game *, qa_actor_id, qa_movement_environment *, qa_error *);
bool qa_q3_prepare_movement(qa_q3_game *, qa_actor_id, qa_movement_input *, qa_error *);
qa_movement_control qa_q3_movement_phase(void *, qa_movement_phase, qa_movement_call *, qa_error *);
/* Shared hosts choose the current arsenal owner independently of character,
 * movement and effect projections. The standalone phase keeps source defaults. */
qa_movement_control qa_q3_movement_phase_selected(void *, qa_movement_phase, qa_movement_call *,
                                                 bool arsenal_selected, qa_error *);
qa_movement_control qa_q3_movement_effect(void *, const qa_movement_effect *, qa_movement_call *,
                                          qa_error *);

typedef struct qa_q3_item_spawn {
    uint32_t item_index;
    int32_t count, team_restriction;
    float wait_seconds, random_seconds;
    bool dropped, suspended;
    qa_vec3 origin, velocity;
    qa_string_id target;
} qa_q3_item_spawn;
bool qa_q3_spawn_item(qa_q3_game *, const qa_q3_item_spawn *, qa_actor_id *, qa_error *);
bool qa_q3_source_item_adopt(qa_q3_game *, qa_actor_id, const qa_q3_item_spawn *,
                           bool available, qa_error *);
bool qa_q3_source_item_spawn_read(const qa_q3_game *, qa_actor_id, qa_q3_item_spawn *,
                                  bool *finished, qa_error *);
bool qa_q3_source_item_respawn(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_source_drop_item(qa_q3_game *, qa_actor_id source_player, uint32_t item_index,
                            float angle, qa_actor_id *, qa_error *);
bool qa_q3_touch_item(qa_q3_game *, qa_actor_id item, qa_actor_id recipient, bool *accepted,
                      qa_error *);
bool qa_q3_touch(qa_q3_game *, const qa_touch_contact *, qa_error *);
bool qa_q3_teleport(qa_q3_game *, qa_actor_id, qa_vec3 origin, qa_vec3 angles, qa_error *);

typedef enum qa_q3_mover_blocked {
    QA_Q3_MOVER_BLOCKED_NONE, QA_Q3_MOVER_BLOCKED_DOOR
} qa_q3_mover_blocked;
typedef struct qa_q3_mover_definition {
    qa_q3_mover_state state;
    qa_vec3 first, second;
    int32_t state_index, damage, next_think_ms;
    float wait_ms;
    qa_q3_mover_blocked blocked;
    qa_actor_id team_leader;
    qa_actor_id activator;
    qa_string_id target;
    qa_string_id loop_sound;
    bool crusher, map_controlled;
} qa_q3_mover_definition;
bool qa_q3_bind_mover(qa_q3_game *, qa_actor_id, const qa_q3_mover_definition *, qa_error *);
bool qa_q3_use_mover(qa_q3_game *, qa_actor_id, qa_actor_id activator, qa_error *);
qa_q3_mover_services qa_q3_mover_adapter(qa_q3_game *);
typedef enum qa_q3_actor_kind {
    Q3_ACTOR_NONE,
    Q3_ACTOR_PLAYER,
    Q3_ACTOR_MISSILE,
    Q3_ACTOR_ITEM,
    Q3_ACTOR_MOVER,
    Q3_ACTOR_PROX_TRIGGER,
    Q3_ACTOR_KAMIKAZE,
    Q3_ACTOR_PORTAL,
    Q3_ACTOR_CORPSE,
    Q3_ACTOR_KAMIKAZE_TIMER,
    Q3_ACTOR_TEMPORARY,
    Q3_ACTOR_OBELISK,
    Q3_ACTOR_PODIUM,
    Q3_ACTOR_VICTORY_MODEL
} qa_q3_actor_kind;
typedef enum qa_q3_projectile_phase {
    Q3_MISSILE_FLIGHT,
    Q3_MISSILE_EVENT,
    Q3_MISSILE_HOOK,
    Q3_MISSILE_PROX_ARMING,
    Q3_MISSILE_PROX_ARMED,
    Q3_MISSILE_PROX_TRIGGERED,
    Q3_MISSILE_PROX_PLAYER,
    Q3_MISSILE_PROX_DISCARD
} qa_q3_projectile_phase;
typedef struct qa_q3_projectile_state {
    qa_q3_weapon weapon;
    qa_trajectory trajectory;
    qa_actor_id owner, pass, attached, trigger;
    qa_team_id team;
    qa_vec3 normal, damage_point;
    float damage, splash, radius;
    int32_t method, splash_method, think_at, event_at;
    uint32_t flags;
    qa_string_id loop_sound;
    qa_q3_projectile_phase phase;
    bool left_owner;
} qa_q3_projectile_state;
typedef struct qa_q3_item_state {
    qa_q3_item_spawn spawn;
    qa_trajectory trajectory;
    int32_t respawn_at, expire_at, ground_entity_number;
    float bounce;
    bool hidden, on_ground;
} qa_q3_item_state;
bool qa_q3_item_read(const qa_q3_game *, qa_actor_id, qa_q3_item_state *);
bool qa_q3_item_availability(qa_q3_game *, qa_actor_id, bool available, int32_t respawn_at_ms,
                             int32_t expire_at_ms, qa_error *);
typedef struct qa_q3_actor_state {
    qa_actor_id actor;
    qa_actor_id enemy;
    uint32_t enemy_source_slot;
    bool enemy_source_present;
    bool force_gesture;
    int32_t spawnflags;
    int32_t water_level, water_type;
    qa_q3_actor_kind kind;
    float alpha;
    union {
        qa_q3_player_state player;
        qa_q3_projectile_state missile;
        qa_q3_item_state item;
        qa_q3_mover_definition mover;
        qa_q3_obelisk_continuation obelisk;
        struct {
            qa_actor_id parent;
        } trigger;
        struct {
            qa_actor_id attacker;
            int32_t start, next, elapsed;
            qa_vec3 angles;
        } kamikaze;
        struct {
            qa_actor_id destination, owner;
            int32_t expire_at, activate_at;
            qa_vec3 angles, fallback;
            bool source;
            int32_t sequence;
            bool enabled;
        } portal;
        struct {
            qa_actor_id player;
            qa_trajectory trajectory;
            int32_t animation, timestamp, next_sink;
            uint32_t flags;
            bool physics_object;
        } corpse;
        struct {
            qa_q3_entity entity;
            int32_t event_time_ms;
        } temporary;
        struct {
            qa_q3_entity entity;
            int32_t timestamp, count, event_time_ms;
            float physics_bounce;
            bool physics_object;
        } postgame;
    } state;
} qa_q3_actor_state;
bool qa_q3_source_obelisk_spawn(qa_q3_game *, qa_actor_id model, int32_t team,
                                 uint32_t authored_flags, qa_actor_id *, qa_error *);
bool qa_q3_source_obelisk_read(const qa_q3_game *, qa_actor_id,
                                qa_q3_obelisk_state *, qa_error *);
bool qa_q3_source_obelisk_reaction(qa_q3_game *, const qa_damage_outcome *,
                                    bool *handled, qa_error *);
typedef struct qa_q3_kamikaze_cooldown {
    qa_actor_id actor;
    int32_t damage_after, shock_after;
} qa_q3_kamikaze_cooldown;
typedef struct qa_q3_ranking_hit {
    int32_t frame, self, attacker, method;
    bool valid;
} qa_q3_ranking_hit;
typedef struct qa_q3_saved_configstring {
    uint32_t index;
    char *text;
} qa_q3_saved_configstring;
typedef struct qa_q3_checkpoint {
    qa_q3_source_memory memory;
    uint32_t version, random_state, death_animation, body_queue_index;
    uint32_t max_clients;
    uint32_t source_count;
    bool new_session;
    int32_t fry_sound_index;
    int32_t portal_sequence;
    int32_t last_team_location_time;
    qa_q3_source_client_counts client_counts;
    qa_q3_source_team_state team_state;
    qa_q3_source_match_state match_state;
    qa_q3_shader_remap_state shader_remaps;
    qa_buffer wire_state;
    qa_q3_source_binding source_entities[QA_Q3_SOURCE_ENTITIES];
    qa_q3_native_client clients[QA_Q3_NATIVE_CLIENTS];
    qa_q3_actor_state source_clients[QA_Q3_NATIVE_CLIENTS];
    qa_q3_product product;
    qa_q3_rules rules;
    int32_t previous_ms, now_ms;
    uint64_t attack_sequence;
    qa_q3_ranking_hit ranking_hit;
    qa_actor_id body_queue[8];
    uint32_t podium_players[3];
    qa_q3_actor_state *actors;
    size_t actor_count;
    qa_q3_kamikaze_cooldown *kamikaze_cooldowns;
    size_t cooldown_count;
    qa_q3_saved_configstring *configstrings;
    size_t configstring_count;
} qa_q3_checkpoint;
/* Typed checkpoint memory is separate from file/network encoding. The save
 * owner encodes fields explicitly and remaps all actor and string IDs before
 * restore. Shared body, movement, combat and inventory stores are saved once.
 */
bool qa_q3_checkpoint_capture(const qa_q3_game *, qa_q3_checkpoint *, qa_error *);
bool qa_q3_checkpoint_restore(qa_q3_game *, const qa_q3_checkpoint *, qa_error *);
void qa_q3_checkpoint_free(qa_q3_checkpoint *);
bool qa_q3_ranking_capture(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_ranking_flag_pickup(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_ranking_team_name(qa_q3_game *, qa_actor_id, const char *, qa_error *);
bool qa_q3_ranking_weapon_time(qa_q3_game *, qa_actor_id, qa_q3_weapon, int32_t, qa_error *);
/* The shared damage owner submits each committed outcome once, independent of
 * the selected character/effects providers. Damage precedes death reporting. */
bool qa_q3_ranking_damage(qa_q3_game *, const qa_damage_outcome *, qa_error *);
bool qa_q3_ranking_death(qa_q3_game *, const qa_damage_outcome *, qa_error *);
bool qa_q3_projectile_read(const qa_q3_game *, qa_actor_id, qa_q3_projectile_state *);
bool qa_q3_projectile_steer(qa_q3_game *, qa_actor_id, qa_vec3 velocity, qa_error *);
typedef enum qa_q3_entity_kind {
    QA_Q3_ENTITY_HIDDEN,
    QA_Q3_ENTITY_PLAYER,
    QA_Q3_ENTITY_ITEM,
    QA_Q3_ENTITY_MISSILE,
    QA_Q3_ENTITY_MOVER,
    QA_Q3_ENTITY_GRAPPLE,
    QA_Q3_ENTITY_PORTAL,
    QA_Q3_ENTITY_CORPSE,
    QA_Q3_ENTITY_KAMIKAZE,
    QA_Q3_ENTITY_TEAM
} qa_q3_entity_kind;
typedef struct qa_q3_entity_view {
    qa_actor_id actor, owner, attachment;
    qa_q3_entity_kind kind;
    qa_body_state body;
    qa_trajectory position, angular;
    uint32_t flags, selections, powerups, item_index, constant_light;
    qa_q3_weapon weapon;
    int32_t legs_animation, torso_animation, time_ms, expire_ms, source_number, source_client;
    qa_string_id loop_sound;
    const char *model, *secondary_model;
    float alpha;
    qa_q3_entity source_entity;
    bool has_source_entity;
} qa_q3_entity_view;
bool qa_q3_entity_read(const qa_q3_game *, qa_actor_id, qa_q3_entity_view *, qa_error *);
/* Unified presentation extension; finite authored fade overshoot is retained. */
bool qa_q3_alpha_read(const qa_q3_game *, qa_actor_id, float *, qa_error *);
bool qa_q3_alpha(qa_q3_game *, qa_actor_id, float, qa_error *);
bool qa_q3_frame(qa_q3_game *, int32_t previous_ms, int32_t now_ms, qa_error *);
bool qa_q3_actor_frame(qa_q3_game *, qa_actor_id, qa_error *);
void qa_q3_actor_released(qa_q3_game *, qa_actor_record);

#endif
