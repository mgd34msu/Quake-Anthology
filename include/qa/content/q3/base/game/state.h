/* Port of src/content/q3/base/game/state.ts. GPL-2.0-or-later.
 * GameEntity.r is the shared/entity-shared.ts EntityShared port and
 * GameClient.ps the shared/player-state.ts PlayerState port; body, health
 * and takedamage live in the binding's owners, as in TS. GameEntity.s is
 * the TS EntityState (network/q3/state/entity.ts) with number storage. */
#ifndef QA_CONTENT_Q3_BASE_GAME_STATE_H
#define QA_CONTENT_Q3_BASE_GAME_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/world.h"
enum { QA_Q3_MAX_CLIENTS = 64, QA_Q3_MAX_GENTITIES = 1024 };
typedef enum qa_q3_connection_state { QA_Q3_CONN_DISCONNECTED = 0, QA_Q3_CONN_CONNECTING = 1, QA_Q3_CONN_CONNECTED = 2 } qa_q3_connection_state;
typedef enum qa_q3_spectator_state { QA_Q3_SPEC_NOT = 0, QA_Q3_SPEC_FREE = 1, QA_Q3_SPEC_FOLLOW = 2, QA_Q3_SPEC_SCOREBOARD = 3 } qa_q3_spectator_state;
typedef enum qa_q3_team_state { QA_Q3_TEAM_STATE_BEGIN = 0, QA_Q3_TEAM_STATE_ACTIVE = 1 } qa_q3_team_state;
typedef enum qa_q3_mover_state { QA_Q3_MOVER_POS1 = 0, QA_Q3_MOVER_POS2 = 1, QA_Q3_MOVER_ONE_TO_TWO = 2, QA_Q3_MOVER_TWO_TO_ONE = 3 } qa_q3_mover_state;
enum { QA_Q3_GAME_FLAG_GODMODE = 0x10, QA_Q3_GAME_FLAG_NOTARGET = 0x20, QA_Q3_GAME_FLAG_TEAMSLAVE = 0x400,
  QA_Q3_GAME_FLAG_NO_KNOCKBACK = 0x800, QA_Q3_GAME_FLAG_DROPPED_ITEM = 0x1000, QA_Q3_GAME_FLAG_NO_BOTS = 0x2000,
  QA_Q3_GAME_FLAG_NO_HUMANS = 0x4000, QA_Q3_GAME_FLAG_FORCE_GESTURE = 0x8000 };
/* GameEntity.s is TS EntityState (network/q3/state/entity.ts), the one
 * qa_q3_entity_state_record type. */
typedef struct qa_q3_game_entity qa_q3_game_entity;
typedef struct qa_q3_game_client qa_q3_game_client;
/* TS GameEntityBinding as fn+ctx; body is the shared EntityBodyBinding. */
typedef struct qa_q3_game_entity_binding {
  void *ctx; qa_q3_base_entity_body_binding body;
  qa_owned_actor (*actor)(void *ctx); bool (*active)(void *ctx);
  double (*health)(void *ctx); void (*set_health)(void *ctx, double value);
  bool (*takedamage)(void *ctx); void (*set_takedamage)(void *ctx, bool value);
  void (*schedule)(void *ctx, double nextthink); void (*run_think)(void *ctx, double time_ms);
} qa_q3_game_entity_binding;
typedef struct qa_q3_player_team_state {
  double state, location, captures, base_defense, carrier_defense, flag_recovery, frag_carrier, assists;
  double last_hurt_carrier, last_returned_flag, flag_since, last_fragged_carrier;
} qa_q3_player_team_state;
void qa_q3_player_team_state_init(qa_q3_player_team_state *state);
typedef struct qa_q3_client_session {
  double session_team, spectator_time, spectator_state, spectator_client, wins, losses, team_leader;
} qa_q3_client_session;
void qa_q3_client_session_init(qa_q3_client_session *session);
typedef struct qa_q3_client_persistant {
  double connected; qa_q3_base_user_command cmd; bool local_client, initial_spawn, predict_item_pickup, pmove_fixed;
  char *netname; /* owned, may be NULL for "" */ double max_health, enter_time; qa_q3_player_team_state team_state;
  double vote_count, team_vote_count; bool team_info;
} qa_q3_client_persistant;
void qa_q3_client_persistant_init(qa_q3_client_persistant *pers);
void qa_q3_client_persistant_destroy(qa_q3_client_persistant *pers);
struct qa_q3_game_client {
  qa_q3_base_player_state ps; qa_q3_client_persistant pers; qa_q3_client_session sess;
  bool ready_to_exit, noclip; double last_cmd_time, buttons, old_buttons, latched_buttons; qa_vec3 old_origin;
  double damage_armor, damage_blood, damage_knockback; qa_vec3 damage_from; bool damage_from_world;
  double accurate_count, accuracy_shots, accuracy_hits, last_killed_client, last_hurt_client, last_hurt_mod;
  double respawn_time, inactivity_time; bool inactivity_warning; double reward_time, air_out_time, last_kill_time;
  bool fire_held; qa_q3_game_entity *hook; double switch_team_time, time_residual;
  qa_q3_game_entity *persistant_powerup; double portal_id; qa_q3_base_player_state_slots ammo_times;
  double invulnerability_time; uint8_t *areabits; size_t areabits_length;
};
/* TS DamageParticipant: GameEntity* (entity!=NULL) or {kind:"shared-actor"}. */
typedef struct qa_q3_damage_participant {
  qa_q3_game_entity *entity; qa_actor_id actor; void *origin_ctx;
  bool (*origin)(void *ctx, qa_vec3 *out); /* false = TS null; used only for shared-actor */
} qa_q3_damage_participant;
typedef qa_q3_damage_participant qa_q3_use_participant;
typedef qa_q3_damage_participant qa_q3_damage_inflictor;
qa_q3_damage_participant qa_q3_participant_entity(qa_q3_game_entity *entity);
typedef void (*qa_q3_entity_think_fn)(void *ctx, qa_q3_game_entity *self);
typedef void (*qa_q3_entity_blocked_fn)(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant other);
typedef void (*qa_q3_entity_touch_fn)(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant other, qa_touch_contact contact);
typedef void (*qa_q3_entity_use_fn)(void *ctx, qa_q3_game_entity *self, const qa_q3_use_participant *other, const qa_q3_use_participant *activator);
typedef void (*qa_q3_entity_pain_fn)(void *ctx, qa_q3_game_entity *self, qa_q3_damage_participant attacker, double damage);
typedef void (*qa_q3_entity_die_fn)(void *ctx, qa_q3_game_entity *self, qa_q3_damage_inflictor inflictor,
    qa_q3_damage_participant attacker, double damage, double method_of_death);
typedef struct qa_q3_entity_callback { void *fn; void *ctx; } qa_q3_entity_callback;
struct qa_q3_game_entity {
  qa_q3_entity_state_record s; qa_q3_base_entity_shared r; qa_q3_game_client *client;
  double slot; qa_q3_game_entity_binding binding;
  char *classname; qa_q3_game_client *classname_client; /* owned string; classname_client selects client-name kind */
  double spawnflags; bool never_free; double flags; char *model, *model2; double freetime, event_time;
  bool free_after_event, unlink_after_event, physics_object; double physics_bounce, clipmask;
  double mover_state, sound_pos1, sound_1to2, sound_2to1, sound_pos2, sound_loop;
  qa_q3_game_entity *parent, *next_train, *prev_train; qa_vec3 pos1, pos2; char *message;
  double timestamp, angle; char *target, *targetname, *team, *target_shader_name, *target_shader_new_name;
  qa_q3_game_entity *target_ent; double speed; qa_vec3 movedir; double nextthink;
  qa_q3_entity_callback think, reached, blocked, touch, use, pain, die;
  double pain_debounce_time, fly_sound_debounce_time, last_move_time;
  double damage, splash_damage, splash_radius, method_of_death, splash_method_of_death, count;
  qa_q3_game_entity *chain, *enemy, *activator; qa_q3_use_participant activation; bool has_activation;
  qa_q3_game_entity *teamchain, *teammaster; double kamikaze_time, kamikaze_shock_time, watertype, waterlevel;
  double noise_index, wait, random; const qa_q3_base_item_definition *item;
};
bool qa_q3_game_entity_inuse(const qa_q3_game_entity *entity);
qa_owned_actor qa_q3_game_entity_actor(const qa_q3_game_entity *entity);
const char *qa_q3_game_entity_classname(const qa_q3_game_entity *entity);
void qa_q3_game_entity_set_classname(qa_q3_game_entity *entity, const char *value);
void qa_q3_game_entity_bind_client_name(qa_q3_game_entity *entity, qa_q3_game_client *client);
/* TS captureClassname: kind 0=value (value may be NULL), 1=client-name. */
typedef struct qa_q3_game_classname_capture { int kind; const char *value; qa_q3_game_client *client; } qa_q3_game_classname_capture;
qa_q3_game_classname_capture qa_q3_game_entity_capture_classname(const qa_q3_game_entity *entity);
double qa_q3_game_entity_health(const qa_q3_game_entity *entity);
void qa_q3_game_entity_set_health(qa_q3_game_entity *entity, double value);
bool qa_q3_game_entity_takedamage(const qa_q3_game_entity *entity);
void qa_q3_game_entity_set_takedamage(qa_q3_game_entity *entity, bool value);
void qa_q3_game_entity_set_nextthink(qa_q3_game_entity *entity, double value);
void qa_q3_game_entity_restore_next_think(qa_q3_game_entity *entity, double value);
/* TS new GameClient(product, authority = null, ammoTimerStored?): authority
 * is borrowed and must outlive the client; ammo_timer_stored may be NULL.
 * false only on allocation failure. */
bool qa_q3_game_client_init_bound(qa_q3_game_client *client, qa_q3_base_product product,
    const qa_q3_base_player_authority_binding *authority,
    void *ammo_timer_ctx, void (*ammo_timer_stored)(void *ctx, double index, double value));
/* TS createGameClient(product). */
bool qa_q3_game_client_init(qa_q3_game_client *client, qa_q3_base_product product);
void qa_q3_game_client_destroy(qa_q3_game_client *client);
/* TS ps.copyFrom(new GameClient(ps.product).ps, stores): source memset of
 * playerState_t that keeps the record and its authority. */
void qa_q3_game_player_state_reset(qa_q3_base_player_state *ps, qa_q3_base_copy_stores stores);
/* shared/snapshot-state.ts convertPlayerState into the game EntityState. */
void qa_q3_game_player_state_to_entity_state(qa_q3_base_player_state *ps, qa_q3_entity_state_record *s,
    bool snap, bool has_extrapolation, double extrapolation_time);
void qa_q3_game_entity_init(qa_q3_game_entity *entity, double slot, qa_q3_game_entity_binding binding);
void qa_q3_game_entity_destroy(qa_q3_game_entity *entity);
#endif
