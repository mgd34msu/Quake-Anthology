/* Port of src/app/bootstrap/simulation/source-strategy.ts: the per-source
 * behavior of a SharedSimulation, chosen once when its source is created
 * (TS createSourceStrategy) and replaced together with the source
 * (TS setSource). One table of function pointers per source class; the class
 * hierarchy LoadingStrategy -> LoadedStrategy -> {Q1, QuakeC, Q2, Q2Native,
 * Q3, Q3Qvm}Strategy is reproduced by each constructor starting from its
 * parent's table and overriding members. The TS SourceStrategyHost is the
 * SharedSimulation itself, so every member takes the simulation.
 * Internal to SharedSimulation (included by shared_simulation_internal.h).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_SOURCE_STRATEGY_H
#define QA_APP_BOOTSTRAP_SIMULATION_SOURCE_STRATEGY_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/mod_client_outputs.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/session.h"
#include "qa/contracts/time.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/compat/qvm/game_combat_binding.h"
#include "qa/core/cvars/index.h"
#include "qa/persistence/decoded.h"
#include "qa/persistence/value.h"

typedef struct qa_app_shared_simulation qa_app_shared_simulation;
typedef struct qa_app_sim_movement_player qa_app_sim_movement_player;
typedef struct qa_movement_result qa_movement_result;
typedef struct qa_q2_lmctf qa_q2_lmctf;
typedef struct qa_q1_character_presentation qa_q1_character_presentation;
struct qa_ss_map;
/* TS checkpointProviders' add(schema, bytes) and providers.push. `add` takes the malloc'd encoded bytes; `push` takes a
 * record whose bytes and schema text are malloc'd. Both keep what they take with the save image. */
typedef struct qa_app_source_checkpoint_sink {
  void *ctx;
  void (*add)(void *ctx, const char *schema, uint8_t *bytes, size_t length);
  void (*push)(void *ctx, qa_provider_checkpoint provider);
} qa_app_source_checkpoint_sink;
/* One nativeClients() entry; the texts are borrowed from the source. */
typedef struct qa_app_source_native_client { double client_slot; const char *phase; const char *userinfo; } qa_app_source_native_client;
/* TS InventoryCoverage: the actors whose inventory the source's own storage owns (kept for the simulation's lifetime). */
typedef struct qa_app_source_inventory_coverage { const qa_owned_actor *actors; size_t actor_count; bool legacy; } qa_app_source_inventory_coverage;
/* TS GuestMonsterProxy; allocate returns false for TS null. */
typedef struct qa_app_source_monster_proxy {
  bool (*allocate)(qa_app_shared_simulation *sim, double *slot);
  void (*bind)(qa_app_shared_simulation *sim, double slot, qa_owned_actor actor);
} qa_app_source_monster_proxy;
/* TS TravelContext: the level change's server flags and landmark (the landmark record stays on the simulation). */
typedef struct qa_app_source_travel_context {
  bool has_server_flags; double server_flags;
  bool has_landmark; double landmark_client_slot;
} qa_app_source_travel_context;
/* One guestTravelPlayers() entry. */
typedef struct qa_app_source_guest_traveler { qa_actor_id actor; qa_client_id client; bool alive; } qa_app_source_guest_traveler;
typedef struct qa_app_sim_quakec_client_ui qa_app_sim_quakec_client_ui;
typedef struct qa_active_powerup_timer qa_active_powerup_timer;
typedef struct qa_app_sim_player_view qa_app_sim_player_view;
typedef struct qa_q1_player_state qa_q1_player_state;
typedef struct qa_app_sim_player_identity qa_app_sim_player_identity;
typedef struct qa_q2_inventory_item qa_q2_inventory_item;
typedef struct qa_pickup_supply_profile qa_pickup_supply_profile;
typedef struct qa_damage_decision qa_damage_decision;
/* TS foreignPowerups result. */
typedef struct qa_app_source_foreign_powerups { double quad_until, double_until, invulnerability_until; } qa_app_source_foreign_powerups;
typedef struct qa_server_settings_owner qa_server_settings_owner;
typedef struct qa_cvar_server_owner qa_cvar_server_owner;
typedef struct qa_app_sim_player_movement_host qa_app_sim_player_movement_host;
typedef struct qa_comp_q1_source_client qa_comp_q1_source_client;
typedef struct qa_inventory_state_binding qa_inventory_state_binding;
typedef struct qa_q2w_input qa_q2w_input;
typedef struct qa_q1_character_source_pose qa_q1_character_source_pose;
typedef struct qa_actor_animation_state qa_actor_animation_state;
/* TS OriginalPlayer: the original player services of a native or QVM primary; `state` is the source's player services
 * (scratch until the next original_player call). */
typedef struct qa_app_source_original_player {
  void *state;
  void (*set_max_health)(qa_app_shared_simulation *sim, const struct qa_app_source_original_player *self, qa_actor_id actor, double health);
  void (*teleport)(qa_app_shared_simulation *sim, const struct qa_app_source_original_player *self, qa_actor_id actor, qa_vec3 origin,
      qa_vec3 velocity, qa_vec3 angles, double hold);
} qa_app_source_original_player;
/* TS EquipmentSuppression. */
typedef struct qa_app_source_equipment_suppression { bool quad, quad_fire, haste; } qa_app_source_equipment_suppression;
/* TS equipmentPowerups result; has_quad_until false = TS quadUntil null. */
typedef struct qa_app_source_equipment_powerups { bool has_quad_until; double quad_until; double haste_until; } qa_app_source_equipment_powerups;
/* TS WeaponFacts; q3_client is the native Q3 client whose ps TS q3Ps names (NULL = undefined). */
typedef struct qa_app_source_weapon_facts {
  struct qa_q3_game_client *q3_client; bool spectator; const char *hand; double quad_until; bool infinite_ammo;
} qa_app_source_weapon_facts;
typedef struct qa_pickup_admission qa_pickup_admission;
typedef struct qa_source_damage_modifier qa_source_damage_modifier;
/* TS SelectedPlayerStats; has_team false = TS team null. */
typedef struct qa_app_source_selected_player_stats {
  double max_health; bool has_team; double team; double quad_until; double haste_until;
} qa_app_source_selected_player_stats;
typedef struct qa_q2w_event qa_q2w_event;
typedef struct qa_q2_damage_source_effects qa_q2_damage_source_effects;
typedef struct qa_q2_product_match_effects_binding qa_q2_product_match_effects_binding;
typedef struct qa_q1_services qa_q1_services;
typedef struct qa_q2_entity qa_q2_entity;
typedef struct qa_app_simulation_travel qa_app_simulation_travel;
typedef struct qa_app_sim_travel_player qa_app_sim_travel_player;
typedef struct qa_app_sim_player_admission qa_app_sim_player_admission;
typedef struct qa_q1_travel_state qa_q1_travel_state;
typedef struct qa_weapon_state qa_weapon_state;
typedef struct qa_q3_game_client qa_q3_game_client;
typedef struct qa_cvar_registry qa_cvar_registry;
typedef struct qa_app_sim_player_ui qa_app_sim_player_ui;
typedef struct qa_app_primary_weapon_handoff qa_app_primary_weapon_handoff;

/* TS stepRoute(): how a step is driven. */
typedef enum qa_app_source_step_route {
  QA_APP_SOURCE_STEP_SHARED, QA_APP_SOURCE_STEP_NATIVE, QA_APP_SOURCE_STEP_AWAITED
} qa_app_source_step_route;

/* TS PreFrameInput; players are the simulation's playerStates. */
typedef struct qa_app_source_pre_frame_input {
  double elapsed;
  const qa_actor_command *commands; size_t command_count;
  bool deferred;
  struct qa_ss_map *pending; /* Map<MovementPlayer, number> | null: MovementPlayer pointer -> sequence */
} qa_app_source_pre_frame_input;

struct qa_ss_source_runtime;
typedef struct qa_app_source_strategy {
  /* TS readonly kind: "loading" | "quakec" | "q1" | "q3" | "q3-qvm" | "q2-native" | "q2". */
  const char *kind;

  /* Typed access to the source this strategy wraps; NULL for every other source. */
  struct qa_ss_source_runtime *(*as_q1)(qa_app_shared_simulation *sim);
  struct qa_ss_source_runtime *(*as_q2)(qa_app_shared_simulation *sim);
  struct qa_ss_source_runtime *(*as_quakec)(qa_app_shared_simulation *sim);
  struct qa_ss_source_runtime *(*as_q3)(qa_app_shared_simulation *sim);
  struct qa_ss_source_runtime *(*as_q3_qvm)(qa_app_shared_simulation *sim);
  struct qa_ss_source_runtime *(*as_q2_native)(qa_app_shared_simulation *sim);

  /* The actor's userinfo string as the source stores it, or NULL when the source has none (scratch until the next call). */
  const char *(*userinfo)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* Replaces the stored userinfo without running the source's change callbacks. */
  void (*store_userinfo)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, const char *userinfo);
  /* Applies a userinfo change through the source's own path; previous NULL = TS null. */
  void (*update_userinfo)(qa_app_shared_simulation *sim, qa_actor_id actor, const char *userinfo, const char *previous);
  /* Pause settings (TS PauseControl): false when the source cannot pause (TS null). */
  bool (*pause)(qa_app_shared_simulation *sim, qa_cvar_registry **cvars);
  bool (*pause_is_spectator)(qa_app_shared_simulation *sim, qa_actor_id actor);

  /* Spawn. */
  /* Admits through the source's own path (true, *out filled), or false when the shared Q1/Q2 admission applies (TS null). */
  bool (*admit_client)(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_simulation_travel *travel, const char *userinfo,
      qa_app_sim_player_admission *out);
  qa_owned_actor (*prepare_bot)(qa_app_shared_simulation *sim, qa_client_id client);
  qa_weapon_state (*initial_arsenal_state)(qa_app_shared_simulation *sim);
  /* Attaches the source entity of a new player; NULL when the source has none. */
  qa_q2_entity *(*attach_entity)(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player);
  void (*configure_admitted)(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_client_id client, qa_app_sim_movement_player *player,
      qa_q2_entity *entity, const char *userinfo, const qa_app_simulation_travel *travel);
  /* Attaches the Q2 character presentation a Q1 source gives its Q2-dressed players. */
  void (*attach_foreign_character)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
  /* Lands the source's own carry (TS carried = carried_player?.state); true when it carried the player. */
  bool (*admit_carry)(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q2_entity *entity, const qa_app_sim_travel_player *carried_player);
  void (*enter_world)(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player, bool fresh);
  /* false = TS null. */
  bool (*capture_entry_carry)(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q1_travel_state *out);

  bool (*q2_gibbed)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* The native Q3 client of the actor: false when the source is not Q3 (TS null), else true with *out NULL when it has no record (TS undefined). */
  bool (*q3_client)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q3_game_client **out);
  /* Whether the source's own gamecode supplies the punch angles. */
  bool (*supplies_punch)(qa_app_shared_simulation *sim);
  bool (*skips_trigger_touch)(qa_app_shared_simulation *sim, qa_actor_id actor);

  /* Whether the source hosts guest monsters projected onto the shared world. */
  bool (*admits_guest_monsters)(qa_app_shared_simulation *sim);
  /* Whether the source projects selected monsters into its own guest records (TS projectsMonsters). */
  bool (*projects_monsters)(qa_app_shared_simulation *sim);
  /* Whether selected monsters run as guest monsters whose authored actors restore from the source (TS usesGuestMonsters). */
  bool (*uses_guest_monsters)(qa_app_shared_simulation *sim);
  /* The guest edition of a native Q2 source ("classic" | "rerelease"), NULL otherwise (TS guestEdition). */
  const char *(*guest_edition)(qa_app_shared_simulation *sim);

  /* Equipment, combat and movement policy. */
  /* The primary weapon handoff of the source's own arsenal; raises "Primary source is not ready" before a source exists. */
  qa_app_primary_weapon_handoff (*handoff)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* The source's own weapon damage factor; false = TS null. */
  bool (*weapon_damage_factor)(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out);
  double (*weapon_delay)(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds);
  double (*equipment_delay)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds);
  bool (*curves_enabled)(qa_app_shared_simulation *sim);
  bool (*player_curve_clip)(qa_app_shared_simulation *sim);
  bool (*friendly_fire)(qa_app_shared_simulation *sim);
  double (*knockback)(qa_app_shared_simulation *sim);
  double (*intermission_queued)(qa_app_shared_simulation *sim);
  double (*quad_factor)(qa_app_shared_simulation *sim);
  double (*proximity_timeout)(qa_app_shared_simulation *sim);
  bool (*runs_q3_powerups)(qa_app_shared_simulation *sim);
  bool (*steps_hand_grenades)(qa_app_shared_simulation *sim);
  /* Team match policy; raises "Team match policy requires an initialized source" before a source exists. */
  bool (*team_game)(qa_app_shared_simulation *sim);
  /* Whether a reset movement state keeps its delta angle words (Q3 sources). */
  bool (*keeps_angle_deltas)(qa_app_shared_simulation *sim);
  double (*stop_speed)(qa_app_shared_simulation *sim);
  /* The family a legacy Q3 arsenal migrates to on this source ("q1" | "q2"), NULL when it has none. */
  const char *(*legacy_q3_family)(qa_app_shared_simulation *sim);

  /* Presentation. */
  bool (*uses_q2_views)(qa_app_shared_simulation *sim);
  bool (*renders_own_intermission)(qa_app_shared_simulation *sim);

  /* Whether the source draws its players' UI and view from guest records instead of the shared player state. */
  bool (*has_guest_ui)(qa_app_shared_simulation *sim);
  qa_app_sim_player_ui (*guest_ui)(qa_app_shared_simulation *sim, qa_actor_id actor);

  /* Commands. */
  /* Whether the source answers every player command itself. */
  bool (*forwards_commands)(qa_app_shared_simulation *sim);
  /* Runs a command only this source owns; true when it consumed the command. */
  bool (*pre_command)(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count);
  /* The weapon and item commands the source handles after the shared ones decline. */
  void (*after_command)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player, const char *name,
      const char *const *args, size_t arg_count);

  /* Step. */
  qa_q2_lmctf *(*lmctf)(qa_app_shared_simulation *sim); /* NULL = TS null */
  bool (*prepares_q1_clients)(qa_app_shared_simulation *sim);
  bool (*intermission_fading)(qa_app_shared_simulation *sim);
  void (*fade_intermission)(qa_app_shared_simulation *sim);
  void (*begin_timed_frame)(qa_app_shared_simulation *sim, double elapsed);
  void (*begin_guest_frame)(qa_app_shared_simulation *sim, qa_frame_context frame);
  /* Starts the source's turn over its actors for one boundary. */
  void (*begin_actor_turns)(qa_app_shared_simulation *sim, qa_frame_context frame);
  bool (*intermission_active)(qa_app_shared_simulation *sim);
  void (*request_intermission_exit)(qa_app_shared_simulation *sim, qa_actor_id actor, double buttons);
  void (*pre_frame)(qa_app_shared_simulation *sim, const qa_app_source_pre_frame_input *input);
  /* Runs the deferred pre-think of a Q1 player; false when the player did not survive it. */
  bool (*pre_think)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_actor_command *command);
  /* Runs the player's command through the source's own think; false when the shared movement applies. */
  bool (*player_think)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_actor_command *command);
  /* Whether the source tracks per-player gravity in the movement state. */
  bool (*tracks_gravity)(qa_app_shared_simulation *sim);
  double (*gravity_scale)(qa_app_shared_simulation *sim, qa_actor_id actor);
  void (*movement_applied)(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_movement_result *moved);
  void (*after_player_command)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, bool paused);
  void (*before_client_actor)(qa_app_shared_simulation *sim, qa_owned_actor actor);
  void (*retouch)(qa_app_shared_simulation *sim, qa_owned_actor actor);
  bool (*spectating)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* Runs the actor through the source's own player turn; false when it has none. */
  bool (*run_player_actor)(qa_app_shared_simulation *sim, qa_owned_actor actor);
  bool (*watches_transitions)(qa_app_shared_simulation *sim);

  /* Frame route and conclusion. */
  qa_app_source_step_route (*step_route)(qa_app_shared_simulation *sim);
  /* Whether a round settlement (its Q3 game) belongs to this source. */
  bool (*settles)(qa_app_shared_simulation *sim, const void *game);
  /* Pulls the source's own gravity setting into the shared world before a step. */
  void (*sync_gravity)(qa_app_shared_simulation *sim);
  void (*after_step)(qa_app_shared_simulation *sim);
  /* The source's end-of-frame work after its actors have run. */
  void (*finish_frame)(qa_app_shared_simulation *sim, bool run);
  void (*conclude_frame)(qa_app_shared_simulation *sim);
  /* TS q1Powerups(actor)?.get(powerup) ?? 0. */
  double (*q1_powerup)(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup);
  void (*q1_character_frame)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q1_character_presentation *frame);

  /* Combat and monsters. */
  /* Whether the source marks the actor notarget; false when the source keeps no such flag for it (TS null). */
  bool (*notarget)(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out);
  /* false = TS null. */
  bool (*cloak_alpha)(qa_app_shared_simulation *sim, qa_actor_id actor, double *out);
  bool (*god_mode)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* Runs the source's own handling of a Q2 weapon event; false when the source has none. */
  bool (*weapon_event)(qa_app_shared_simulation *sim, const qa_q2w_event *event);
  bool (*has_enemy)(qa_app_shared_simulation *sim, qa_actor_id target);
  bool (*team_damage_enabled)(qa_app_shared_simulation *sim, const char *match_provider);
  /* The Q2 match's damage effects, bound in *storage. */
  qa_q2_damage_source_effects (*match_damage_effects)(qa_app_shared_simulation *sim, qa_q2_product_match_effects_binding *storage);
  /* Reports weapon noise to the monsters; raises `message` when the source has none yet. */
  void (*required_monster_noise)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, bool secondary, const char *message);
  /* Raises `message` unless the source is a Q2 game. */
  void (*require_q2)(qa_app_shared_simulation *sim, const char *message);
  void (*pushed)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 velocity);
  void (*set_entity_gravity)(qa_app_shared_simulation *sim, qa_actor_id actor, double gravity);
  /* player NULL = TS null. */
  bool (*monsters_searching)(qa_app_shared_simulation *sim, const qa_actor_id *player);
  /* The Q1 game that owns weapon selection when no selected arsenal does; NULL = TS null. */
  qa_q1_services *(*q1_game)(qa_app_shared_simulation *sim);
  bool (*select_weapon)(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_item_id item);
  /* The source's connected clients in its own order; NULL when the shared session order applies (scratch until the next call). */
  const qa_client_id *(*connected_clients)(qa_app_shared_simulation *sim, size_t *out_count);
  /* The source's player actors in its own order; NULL when the shared session order applies (scratch until the next call). */
  const qa_actor_id *(*player_actors)(qa_app_shared_simulation *sim, size_t *out_count);

  /* Selected arsenals and combat. */
  qa_app_source_selected_player_stats (*selected_player)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* The Q3 game type the source runs; false when the recipe's match decides (TS null). */
  bool (*q3_game_type)(qa_app_shared_simulation *sim, double *out);
  /* The source's Q3 server info (malloc'd; the caller frees). */
  char *(*q3_server_info)(qa_app_shared_simulation *sim);
  /* Whether the source runs the Team Arena equipment natively. */
  bool (*primary_equipment)(qa_app_shared_simulation *sim);
  /* Whether the source supplies the selected Q3 weapon effects. */
  bool (*has_equipment_effects)(qa_app_shared_simulation *sim);
  double (*equipment_damage_factor)(qa_app_shared_simulation *sim, qa_actor_id actor);
  double (*equipment_firing_delay)(qa_app_shared_simulation *sim, qa_actor_id actor, double milliseconds, double persistent);
  void (*check_hurt_carrier)(qa_app_shared_simulation *sim, qa_actor_id target, qa_actor_id attacker);
  bool (*check_obelisk_attack)(qa_app_shared_simulation *sim, qa_actor_id target, qa_actor_id attacker);
  void (*return_pickup)(qa_app_shared_simulation *sim, qa_actor_id actor);
  void (*install_pickup_admission)(qa_app_shared_simulation *sim, const qa_pickup_admission *admission);
  /* The weapon family the source's own arsenal belongs to ("q1" | "q2" | "q3"). */
  const char *(*supply_family)(qa_app_shared_simulation *sim);
  /* Whether the source gamecode owns the arsenal's pickup supply. */
  bool (*owns_supply)(qa_app_shared_simulation *sim);
  /* Whether the source's supply is its own Q3 weapon set unchanged. */
  bool (*identity_supply)(qa_app_shared_simulation *sim);
  const qa_item_id *(*extra_replaced_items)(qa_app_shared_simulation *sim, size_t *out_count);
  bool (*animates_weapon_fire)(qa_app_shared_simulation *sim);
  /* item NULL = TS null. */
  void (*note_fired)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item);
  /* kind: "weapon" | "impact". */
  void (*weapon_noise)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, const char *kind);
  bool (*allows_auto_switch)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* The source's own server cvars; NULL when the shared server settings registry applies. */
  qa_cvar_registry *(*cvars)(qa_app_shared_simulation *sim);
  double (*q1_powerup_expires)(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup);
  /* false = TS null. */
  bool (*damage_modifier)(qa_app_shared_simulation *sim, qa_source_damage_modifier *out);
  void (*monster_noise)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, bool secondary);

  /* Teleport spawns, objectives and the original player. */
  void (*teleport_spawn)(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
  void (*drop_objectives)(qa_app_shared_simulation *sim, qa_owned_actor actor);
  /* The original player services of a native or QVM primary; false when the source has none (TS null). */
  bool (*original_player)(qa_app_shared_simulation *sim, qa_app_source_original_player *out);
  void (*set_max_health)(qa_app_shared_simulation *sim, qa_actor_id actor, double health);

  /* Equipment. */
  qa_app_source_equipment_suppression (*equipment_suppression)(qa_app_shared_simulation *sim);
  /* Whether the source's native player owns the grapple and offhand equipment state. */
  bool (*owns_equipment_players)(qa_app_shared_simulation *sim);
  /* false = TS null. */
  bool (*source_inventory)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_inventory_state_binding *out);
  /* Whether the equipment owner counts as alive; false when the shared player rule applies (TS null). */
  bool (*player_available)(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out);
  /* The source's teleport toggle bits for the actor; false when it keeps none (TS null). */
  bool (*teleport_bits)(qa_app_shared_simulation *sim, qa_actor_id actor, double *out);
  qa_app_source_equipment_powerups (*equipment_powerups)(qa_app_shared_simulation *sim, qa_actor_id actor);
  qa_q2w_input (*refine_weapon_input)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q2w_input input);
  /* Plays an equipment attack animation through the source's own player (priority "attack" | "reverse"); true when it did. */
  bool (*guest_weapon_animation)(qa_app_shared_simulation *sim, qa_actor_id actor, const char *priority, bool melee);
  void (*write_character_animation)(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_actor_animation_state *animation);
  /* false = TS null. */
  bool (*character_pose)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q1_character_source_pose *out);
  /* The source's weapon-frame facts for the Q2 weapon engine. */
  qa_app_source_weapon_facts (*weapon_facts)(qa_app_shared_simulation *sim, qa_actor_id actor);

  /* Arsenal step. */
  /* The arsenal the source reports before any selected arsenal replaces it (the ammo array follows qa_app_shared_sim_arsenal's ownership). */
  qa_arsenal_state (*base_arsenal)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
  /* NULL = TS null. */
  qa_comp_q1_source_client *(*q1_client)(qa_app_shared_simulation *sim, qa_actor_id actor);
  void (*q1_impulse)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* Whether a match pause freezes the selected arsenal. */
  bool (*weapons_paused)(qa_app_shared_simulation *sim);
  /* Steps a source holdable before the selected arsenal; true when it consumed the step. */
  bool (*step_selected_holdable)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_weapon_step_input *input);
  /* Steps the source's own arsenal; false leaves the shared weapon projection to answer (TS null). */
  bool (*primary_weapon_step)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_weapon_step_input *input,
      qa_weapon_step_result *out);
  void (*player_weapon)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
  /* Whether source impulses are consumed by the source itself rather than the shared arsenal. */
  bool (*drops_impulse)(qa_app_shared_simulation *sim);
  /* Whether an arsenal command without a weapon slot goes straight to the weapon request. */
  bool (*requests_weapon_directly)(qa_app_shared_simulation *sim);
  /* Whether such a request is made even for the weapon already held. */
  bool (*always_requests_weapon)(qa_app_shared_simulation *sim);
  /* Whether a movement contact is handled by the source's own physics. */
  bool (*owns_contacts)(qa_app_shared_simulation *sim, qa_actor_id actor);
  bool (*quake_world_contacts)(qa_app_shared_simulation *sim);
  /* Voices the jump through the source's own player; true when it did. */
  bool (*jump_voice)(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player);
  /* false = TS null. */
  bool (*animation)(qa_app_shared_simulation *sim, const qa_animation_step_input *input, qa_animation_step_result *out);
  /* The arsenal after a client's attack, for sources that project it onto a foreign character; false = TS null. */
  bool (*attack_arsenal)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_arsenal_state *out);
  /* TS movementBindings(actor, selectedClock): fills the binding members of `host`; `player_host` is the player's
   * host record (shared_simulation_players.c), whose bindings live as long as the player. */
  void (*movement_bindings)(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_clock_profile_kind selected_clock, void *player_host,
      qa_app_sim_player_movement_host *host);

  /* Server settings, roster and lifecycle. */
  /* The source's server cvars (TS ServerCvarRegistry); NULL = TS null. */
  qa_cvar_registry *(*server_cvars)(qa_app_shared_simulation *sim);
  /* TS serverSettingsOwner(owner): `owner` is the cvar owner over server_cvars(); a source that refines it reads its own
   * storage beside `owner` (shared_simulation_worlds.c allocates it). */
  qa_server_settings_owner (*server_settings_owner)(qa_app_shared_simulation *sim, qa_cvar_server_owner *owner);
  /* The client a player belongs to when the source keeps its own roster: 1 found, 0 TS null, -1 TS undefined
   * (defer to the shared players). */
  int (*client_of)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id *out);
  /* Removes a player from the source; true = "release" (the caller releases the shared actor), false = "done". player NULL = TS null. */
  bool (*disconnect)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player);
  /* Drops the player's inventory on death; only Q1 sources drop a backpack. */
  void (*drop_inventory)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
  void (*respawn)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
  /* TS telefragCause(): true = { kind: "q1", deathType: "telefrag" }, false = { kind: "q2", meansOfDeath: 21, damageFlags: 32 }. */
  bool (*telefrag_cause_q1)(qa_app_shared_simulation *sim);
  void (*cutscene_control)(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, qa_vec3 angles);
  /* Gives the player a thirty second quad where the source has one. */
  void (*grant_quad)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* The player's powerup expiry times as foreign monsters read them; guest worlds keep no public timers. */
  qa_app_source_foreign_powerups (*foreign_powerups)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* Raises when the source cannot honor a mod client output claim; has_movement false = TS movement undefined. */
  void (*check_output_claim)(qa_app_shared_simulation *sim, const qa_mod_client_output_channel *channels, size_t channel_count, bool has_movement,
      qa_clock_profile_kind movement);
  /* false = TS null. */
  bool (*guest_grounded)(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out);
  /* false = TS null. */
  bool (*guest_client_view)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 *out_view_offset, bool *out_crouched);
  /* Whether the source's own intermission or camera hides a component's client outputs; player NULL = TS null. */
  bool (*outputs_suspended)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player);
  /* Source reactions before the shared selected-source damage reaction, after the shared death handling, and after a commit. */
  void (*reaction_start)(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision);
  void (*reaction_end)(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision);
  void (*damage_committed)(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision);

  /* Match and team. */
  /* Maps the source's own team name onto the shared team ids (NULL = TS null). */
  const char *(*canonical_team)(qa_app_shared_simulation *sim, const char *original);
  /* Whether the source declares a match score and team command for its players. */
  bool (*declares_match)(qa_app_shared_simulation *sim);
  double (*match_score)(qa_app_shared_simulation *sim, qa_actor_id actor);
  void (*set_match_score)(qa_app_shared_simulation *sim, qa_actor_id actor, double value);
  void (*set_match_team)(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested);

  /* Identity, field of view, command angles and grants. */
  void (*set_identity)(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_identity *identity);
  /* Applies a field-of-view change the source owns (mode "change" | "restore"); false leaves it to the shared character. */
  bool (*set_field_of_view)(qa_app_shared_simulation *sim, qa_actor_id actor, double field_of_view, const char *mode);
  /* The source's authoritative angle delta for a Q3 command; false when it keeps none (TS undefined). */
  bool (*angle_delta)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, double out[3]);
  /* The movement state a component command's angles are relative to; false defers to the shared player (TS undefined). */
  bool (*command_movement)(qa_app_shared_simulation *sim, qa_client_id client, qa_movement_state *out);
  /* Items the guest's own ammo rows keep out of a shared grant; NULL = TS null (scratch until the next call). */
  const qa_item_id *(*guest_grant_exclusions)(qa_app_shared_simulation *sim, size_t *out_count);
  /* TS grantProfile(this.selectedArsenal); NULL = TS null. */
  const qa_pickup_supply_profile *(*grant_profile)(qa_app_shared_simulation *sim);
  /* false = TS null. */
  bool (*source_item)(qa_app_shared_simulation *sim, const char *requested, qa_q2_inventory_item *out);
  /* Gives the weapon's own ammo and selects it after a mapped weapon grant (TS arsenal = this.selectedArsenal). */
  void (*granted_weapon)(qa_app_shared_simulation *sim, qa_owned_actor owner, const char *requested, const qa_item_id *destinations,
      size_t destination_count, const qa_pickup_supply_profile *profile);

  /* UI, views and presentations. */
  /* The player whose state the UI shows: a spectating Q2 observer follows its chase target. */
  qa_actor_id (*ui_subject)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* false = TS null; the caller frees out->weapon.items. */
  bool (*client_ui)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_quakec_client_ui *out);
  /* The UI powerup timers, written into out (at most capacity); returns the count. */
  size_t (*ui_powerups)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_active_powerup_timer *out, size_t capacity);
  /* TS arsenalUi(actor, arsenal): writes items, ammo, weaponStatus and arsenalWarning into ui (ui_storage is the
   * primaryUi record of shared_simulation_arsenal.c that keeps them). */
  void (*arsenal_ui)(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_arsenal_state *arsenal, qa_app_sim_player_ui *ui, void *ui_storage);
  /* The view built from the source's guest records; false when the shared player state draws it (TS null). */
  bool (*guest_view)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_player_view *out);
  /* Whether the actor is a Q2 observer following a chase target. */
  bool (*chasing)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* TS cutsceneFov(fov): has_fov false = TS undefined. */
  double (*cutscene_fov)(qa_app_shared_simulation *sim, bool has_fov, double fov);
  /* A view the source overrides after the shared one is built; false keeps it (TS null). */
  bool (*override_view)(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_view *view, bool intermission, double health,
      qa_app_sim_player_view *out);
  /* Whether the source draws a foreign character's death view and body from its own player. */
  bool (*foreign_character_death)(qa_app_shared_simulation *sim);
  bool (*rerelease_view_height)(qa_app_shared_simulation *sim);
  /* Appends the source's own presentations to `list` (the primaryPresentations list of shared_simulation_arsenal.c). */
  void (*presentations)(qa_app_shared_simulation *sim, double fraction, double back_lerp, void *list);
  /* Whether a first-person weapon model may exist for an actor without shared player state. */
  bool (*guest_weapon_views)(qa_app_shared_simulation *sim);
  qa_q2_entity *(*q2_player_entity)(qa_app_shared_simulation *sim, qa_actor_id actor); /* NULL = TS null */
  qa_q1_player_state *(*q1_player)(qa_app_shared_simulation *sim, qa_actor_id actor); /* NULL = TS null */
  /* The Q1 client colors (TS { shirt, pants }); NULL = TS null or undefined. */
  qa_comp_q1_source_client *(*q1_colors)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* The first-person weapon model's visibility when the source decides it; false for the health rule (TS null). */
  bool (*weapon_visible)(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out);
  double (*guest_bob_cycle)(qa_app_shared_simulation *sim, qa_actor_id actor);

  /* Travel. */
  /* TS travelSource(context): writes the travel's source record (source_kind and its fields) into *travel. */
  void (*travel_source)(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel);
  /* The clients that carry through a campaign travel in the source's own order; false for the shared player order (TS null). */
  bool (*travelers)(qa_app_shared_simulation *sim, qa_client_id *clients, qa_actor_id *actors, size_t capacity, size_t *out_count);
  /* TS travelPlayerState(actor): writes the carried state (state_kind and its fields) into *entry. */
  void (*travel_player_state)(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_travel_player *entry);
  /* false = TS null. */
  bool (*guest_health)(qa_app_shared_simulation *sim, qa_actor_id actor, double *out);
  void (*set_guest_health)(qa_app_shared_simulation *sim, qa_actor_id actor, double health);
  /* Guest players whose pending travel carry may land (at most capacity); returns the count. */
  size_t (*guest_travel_players)(qa_app_shared_simulation *sim, qa_app_source_guest_traveler *out, size_t capacity);

  /* Saves and restore. */
  /* Raises `message` while the source world is still loading. */
  void (*require_loaded)(qa_app_shared_simulation *sim, const char *message);
  /* Raises unless the source can save at this frame. */
  void (*assert_save_ready)(qa_app_shared_simulation *sim);
  /* Whether the source gamecode rebuilds the actor itself on restore. */
  bool (*reconstructs_actor)(qa_app_shared_simulation *sim, qa_actor_id actor);
  /* Adds the source's own checkpoint providers. */
  void (*checkpoint_providers)(qa_app_shared_simulation *sim, const qa_app_source_checkpoint_sink *sink);
  /* The source's guest checkpoints (0 or 1, borrowed from the source). */
  const qa_guest_checkpoint *(*checkpoint_guests)(qa_app_shared_simulation *sim, size_t *out_count);
  /* Sets the source-specific fields of the simulation checkpoint record, where TS spreads checkpointExtras(). */
  void (*checkpoint_extras)(qa_app_shared_simulation *sim, qa_save_value *record);
  /* false = TS null; otherwise *out is a malloc'd array the caller frees. */
  bool (*native_clients)(qa_app_shared_simulation *sim, qa_app_source_native_client **out, size_t *out_count);
  /* Malloc'd snapshots (the caller frees each with qa_cvar_read_free, then the array); NULL with count 0 for none. */
  qa_cvar_snapshot *(*source_cvar_snapshots)(qa_app_shared_simulation *sim, size_t *out_count);
  /* The primary QVM interface digest a save must match; NULL when the source has none. */
  const char *(*expected_declaration)(qa_app_shared_simulation *sim);
  qa_qvm_combat_bindings *(*legacy_combat)(qa_app_shared_simulation *sim); /* NULL = TS null */
  /* Raises unless the source has the storage a saved inventory projection of `kind` ("qvm" | "native") needs. */
  void (*require_inventory_projection)(qa_app_shared_simulation *sim, const char *kind);
  /* The actors whose inventory the source's own storage owns; false when the source keeps none (TS null). */
  bool (*inventory_coverage)(qa_app_shared_simulation *sim, bool native, bool qvm, qa_app_source_inventory_coverage *out);
  /* Refuses an original save the source cannot represent. */
  void (*assert_savable)(qa_app_shared_simulation *sim);
  /* The source's own save taken a frame at a time; false when it has none (TS undefined). The caller frees its parts. */
  bool (*capture_guest_save)(qa_app_shared_simulation *sim, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx,
      qa_app_sim_rerelease_guest_save *out);
  /* The source's own original save as a checkpoint provider whose bytes `owner` keeps; false when it keeps none (TS null). */
  bool (*original_save)(qa_app_shared_simulation *sim, const qa_app_sim_rerelease_guest_save *guest_save, qa_persist_decoded *owner,
      qa_provider_checkpoint *out);
  /* Restores the source's own original save a frame at a time. */
  void (*restore_loading)(qa_app_shared_simulation *sim, const qa_save_image *save, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx);
  /* Restores the source's own original save and reconnects its saved clients. */
  void (*restore_guest)(qa_app_shared_simulation *sim, const qa_save_image *save, bool native_restored);
  /* The guest stand-in entities shared monsters need to be addressed by guest slot; false when the guest needs none (TS null). */
  bool (*monster_proxy)(qa_app_shared_simulation *sim, qa_app_source_monster_proxy *out);
  /* Restores the source's own per-player continuation from a saved image. */
  void (*restore_continuation)(qa_app_shared_simulation *sim, const qa_save_image *save);
  /* Restores the source's foundation checkpoint and server variables. */
  void (*restore_foundation)(qa_app_shared_simulation *sim, const qa_save_image *save, const qa_save_reader *reader);
  /* Re-registers the world gravity a saved Q2 server variable set omitted. */
  void (*restore_gravity)(qa_app_shared_simulation *sim, const qa_save_image *save);
  /* The source's saved ammo timer for a weapon; false when the actor has no source client (TS null). */
  bool (*ammo_timer)(qa_app_shared_simulation *sim, qa_actor_id actor, double weapon, double *out);
} qa_app_source_strategy;

/* TS createSourceStrategy(source, host): the strategy of the simulation's current source. The tables are built once
 * per class and shared (the members take the simulation, as the TS methods take this.host and this.source). */
const qa_app_source_strategy *qa_app_create_source_strategy(const qa_app_shared_simulation *sim);
#endif
