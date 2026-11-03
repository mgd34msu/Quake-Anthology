/* Port of src/app/bootstrap/simulation/runtime.ts, class SharedSimulation:
 * the private state and the method table shared by the shared_simulation_*.c
 * files. The C files split the class by TS region (docs/runtime_layout.md);
 * every TS field is a member below, in TS declaration order, with the TS
 * name in snake case.
 *
 * TS collections: Map/Set become qa_ss_map (insertion ordered, so `for of`
 * iteration keeps the TS order); keys compare by the TS identity rule of the
 * key type (ActorId by value, OwnedActor by object identity
 * qa_same_owned_actor, ClientId by value, strings by content, numbers by
 * SameValueZero, objects by pointer). RevisionMap is a qa_ss_map whose
 * revision counter TS reads. Closures stored in fields are fn+ctx pairs.
 * TS throws become qa_raise("Error", ...) with the TS message (PORTING.md).
 * GPL-2.0-or-later. */
#ifndef QA_SRC_APP_BOOTSTRAP_SIMULATION_SHARED_SIMULATION_INTERNAL_H
#define QA_SRC_APP_BOOTSTRAP_SIMULATION_SHARED_SIMULATION_INTERNAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/app/bootstrap/simulation/actor_execution.h"
#include "qa/app/bootstrap/simulation/arsenal/q1.h"
#include "qa/app/bootstrap/simulation/arsenal/q2.h"
#include "qa/app/bootstrap/simulation/arsenal/q3.h"
#include "qa/app/bootstrap/simulation/arsenal/q3_ammo_regen.h"
#include "qa/app/bootstrap/simulation/arsenal/q3_source.h"
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/app/bootstrap/simulation/classic_guest_world.h"
#include "qa/app/bootstrap/simulation/dropped_pickups.h"
#include "qa/app/bootstrap/simulation/equipment_runtime.h"
#include "qa/app/bootstrap/simulation/events.h"
#include "qa/app/bootstrap/simulation/foreign_q1_world.h"
#include "qa/app/bootstrap/simulation/foreign_q2_world.h"
#include "qa/app/bootstrap/simulation/grapple_checkpoint.h"
#include "qa/app/bootstrap/simulation/grapple_runtime.h"
#include "qa/app/bootstrap/simulation/guest_monsters.h"
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include "qa/compat/q2/classic/combat_profile.h"
#include "qa/compat/qvm/primary_profile.h"
#include "qa/app/bootstrap/simulation/monster_runtime.h"
#include "qa/app/bootstrap/simulation/native_q2_rerelease_save.h"
#include "qa/app/bootstrap/simulation/native_q2_travel.h"
#include "qa/app/bootstrap/simulation/navigation.h"
#include "qa/app/bootstrap/simulation/network.h"
#include "qa/app/bootstrap/simulation/network_q2_guest.h"
#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/app/bootstrap/simulation/q1_punch.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/q2_match_reports.h"
#include "qa/app/bootstrap/presentation_state.h"
#include "qa/content/mounts.h"
#include "qa/app/bootstrap/simulation/q3/guest_artifact.h"
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include "qa/compat/qvm/game_input.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include "qa/app/bootstrap/simulation/source_hosts.h"
#include "qa/app/bootstrap/simulation/weapon_behavior_runtime.h"
#include "qa/app/bootstrap/simulation/native_primary_weapons.h"
#include "qa/app/bootstrap/simulation/native_pmove_contacts.h"
#include "qa/app/bootstrap/simulation/native_commands.h"
#include "qa/app/bootstrap/qvm_grapple_selection.h"
#include "qa/app/bootstrap/simulation/qvm_weapon_behavior.h"
#include "qa/app/bootstrap/simulation/rerelease_weapon_behavior.h"
#include "qa/app/bootstrap/simulation/weapon_slot.h"
#include "qa/app/bootstrap/simulation/weapon_slot_checkpoint.h"
#include "qa/app/bootstrap/simulation/weapon_slot_projection.h"
#include "qa/compat/q2/native_primary.h"
#include "qa/compat/q2/native_primary_commands.h"
#include "qa/compat/q2/native_primary_drop.h"
#include "qa/compat/q2/native_primary_inventory.h"
#include "qa/compat/q2/native_primary_weapons.h"
#include "qa/compat/q2/rerelease/world_text.h"
#include "qa/compat/qvm/game_combat_binding.h"
#include "qa/compat/qvm/game_inventory.h"
#include "qa/compat/qvm/game_pickups.h"
#include "qa/compat/qvm/game_weapons.h"
#include "qa/content/composition/q1/runtime.h"
#include "qa/content/composition/q1_expansion_arsenal.h"
#include "qa/content/composition/q2/index.h"
#include "qa/content/monsters/authored.h"
#include "qa/content/q1/base/player.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/travel.h"
#include "qa/content/q1/quakec/id1_attacks.h"
#include "qa/content/q1/quakec/id1_pickups.h"
#include "qa/content/q1/quakec/id1_projectiles.h"
#include "qa/content/q2/base/player/character.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/content/q3/foundation/character.h"
#include "qa/content/q3/guest_items.h"
#include "qa/contracts/original_pickups.h"
#include "qa/contracts/pickups.h"
#include "qa/core/cvars/index.h"
#include "qa/settings/server/cvars.h"
#include "qa/settings/server/profile.h"
#include "qa/settings/server/selection.h"
#include "qa/debug/world.h"
#include "qa/foundation/event_ring.h"
#include "qa/movement/q1/monsters.h"
#include "qa/persistence/q2_classic_guest.h"
#include "qa/persistence/source_items.h"
#include "qa/persistence/world_state.h"
#include "qa/text/world.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"
#include "qa/world/collision/q3/settings.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/gameplay/original_pickups.h"
#include "qa/world/gameplay/pickups.h"
#include "qa/world/gameplay/q1_client_visibility.h"
#include "qa/world/scheduler.h"
#include "qa/world/session/clocks.h"
#include "qa/world/session/mod_client_applications.h"
#include "qa/world/session/mod_client_outputs.h"
#include "qa/world/session/mod_clients.h"
#include "qa/world/session/mod_match.h"
#include "qa/world/session/mods.h"
#include "qa/app/bootstrap/simulation/checkpoint_values.h"
#include "source_strategy.h"

/* ---- Insertion-ordered Map/Set (shared_simulation_core.c) ---- */
typedef enum qa_ss_key_kind {
  QA_SS_KEY_ACTOR,   /* ActorId: qa_same_actor */
  QA_SS_KEY_OWNED,   /* OwnedActor: object identity, qa_same_owned_actor */
  QA_SS_KEY_CLIENT,  /* ClientId: qa_same_client */
  QA_SS_KEY_NUMBER,  /* number: SameValueZero */
  QA_SS_KEY_STRING,  /* string: content (the map owns a copy) */
  QA_SS_KEY_POINTER  /* object: identity */
} qa_ss_key_kind;
typedef union qa_ss_key {
  qa_actor_id actor; qa_owned_actor owned; qa_client_id client;
  double number; char *string; const void *pointer;
} qa_ss_key;
typedef struct qa_ss_map {
  qa_ss_key_kind key_kind;
  size_t value_size;       /* 0 for a Set */
  qa_ss_key *keys;
  unsigned char *values;
  size_t count, capacity;
  double revision;         /* RevisionMap: bumped by set, delete and clear */
} qa_ss_map;
void qa_ss_map_init(qa_ss_map *map, qa_ss_key_kind kind, size_t value_size);
void qa_ss_map_free(qa_ss_map *map);
/* NULL when absent (TS undefined). */
void *qa_ss_map_get(const qa_ss_map *map, const void *key);
bool qa_ss_map_has(const qa_ss_map *map, const void *key);
/* Inserts at the end or replaces in place (TS Map.set keeps the position);
 * value NULL zero-fills. Returns the stored value. */
void *qa_ss_map_set(qa_ss_map *map, const void *key, const void *value);
bool qa_ss_map_delete(qa_ss_map *map, const void *key);
void qa_ss_map_clear(qa_ss_map *map);
static inline const qa_ss_key *qa_ss_map_key_at(const qa_ss_map *map, size_t index) { return &map->keys[index]; }
static inline void *qa_ss_map_value_at(const qa_ss_map *map, size_t index) { return map->values + index * map->value_size; }

/* ---- TS helper types of the class ---- */
/* TS SourceRuntime union on kind. */
typedef enum qa_ss_source_kind {
  QA_SS_SOURCE_LOADING, QA_SS_SOURCE_QUAKEC, QA_SS_SOURCE_Q1, QA_SS_SOURCE_Q3,
  QA_SS_SOURCE_Q3_QVM, QA_SS_SOURCE_Q2_NATIVE, QA_SS_SOURCE_Q2
} qa_ss_source_kind;
/* TS { client: ClientId -> OwnedActor } of the q2-native source. */
typedef struct qa_ss_source_runtime {
  qa_ss_source_kind kind;
  /* quakec */
  qa_app_sim_quakec_source *quakec;
  /* q1: game is composition.game (TS Q1Foundation, C qa_q1_services). */
  qa_comp_q1_source_composition *q1_composition;
  qa_q1_services *q1_game;
  qa_cvar_registry *q1_cvars;
  /* q3 */
  qa_app_q3_source_runtime *q3;
  qa_app_q3_source_subsystems_owner *q3_owner; /* the Q3SourceRuntime's subsystems (pool, records, admission) */
  qa_app_q3_created_source_host *q3_host;   /* createQ3SourceHost result the runtime's host points into */
  qa_save_value *q3_restore_state;          /* decoded q3:native checkpoint (restore mode), borrowed by q3 */
  /* q3-qvm */
  qa_app_q3_qvm_server_game *qvm;
  qa_q3_guest_catalog *qvm_catalog;
  qa_app_q3_server_state *qvm_state;            /* the guest's Q3ServerState (owned) */
  qa_qvm_combat_bindings *qvm_combat;        /* NULL = TS null */
  const qa_qvm_inventory_profile *qvm_inventory; /* NULL = TS null */
  qa_qvm_primary_weapons *qvm_weapons;        /* NULL = TS null */
  qa_qvm_primary_pickups *qvm_pickups;        /* NULL = TS null */
  /* q2-native: edition "classic" (classic_world, files) or "rerelease". */
  const char *edition;
  qa_app_sim_classic_guest_world *classic_world;
  qa_app_sim_rerelease_guest_world *rerelease_world;
  qa_classic_original_save_files *files;
  qa_ss_map visited;  /* string map -> qa_q2_classic_visited_level | qa_app_sim_q2_rerelease_visited_level */
  qa_ss_map clients;  /* ClientId -> qa_owned_actor */
  /* q2: product plus the Pick<> members TS copies onto the union arm. */
  qa_q2_product_runtime *product;
  qa_q2w_weapons *q2_weapons;
} qa_ss_source_runtime;
/* TS SelectedWeaponSource. */
typedef struct qa_ss_selected_weapon_source {
  const char *kind; /* "q1" | "q2" */
  qa_q1_services *q1_game;
  qa_comp_selected_q1_mission_weapons *mission_weapons;
  qa_q2_game_services *q2_game;
  qa_q2w_weapons *weapons;
  qa_app_sim_source_random *random;
  qa_frame_context frame;
  double map_milliseconds, next_milliseconds, interval_milliseconds;
} qa_ss_selected_weapon_source;
/* TS Q1SelectedArsenal | Q2SelectedArsenal | Q3SelectedArsenal (family). */
typedef struct qa_ss_selected_arsenal {
  const char *family; /* "q1" | "q2" | "q3" */
  qa_app_q1_selected_arsenal *q1;
  qa_app_q2_selected_arsenal *q2;
  qa_app_q3_selected_arsenal *q3;
} qa_ss_selected_arsenal;
/* TS foreignWorld record. */
typedef struct qa_ss_foreign_world {
  qa_provider_reference reference;
  const char *edition; /* "classic" | "rerelease" */
  const char *program; /* "baseq2" | "xatrix" | "rogue" | "mg2" */
  qa_app_sim_foreign_q2_world_modules *modules;
  qa_app_sim_source_random *random;
  struct { qa_frame_context frame; bool advanced; } clock;
} qa_ss_foreign_world;
/* TS foreignQ1World record. */
typedef struct qa_ss_foreign_q1_world {
  qa_provider_reference reference;
  const char *edition;
  const char *program; /* id1 hipnotic rogue dopa mg1 mg3 ctf */
  qa_app_sim_foreign_q1_world_modules *modules;
  qa_app_sim_source_random *random;
  struct { qa_frame_context frame; bool advanced; } clock;
  qa_q1_campaign_state campaign;
  qa_cvar_registry *cvars;
  const char **cvar_names; size_t cvar_name_count;
} qa_ss_foreign_q1_world;
/* TS pendingNativeTravel closure: the captured createSource state. */
typedef struct qa_ss_pending_native_travel {
  const char *edition;
  const qa_app_sim_native_q2_travel *retained;
  qa_app_sim_classic_guest_map map;
  char *map_name, *map_entities;
  qa_app_sim_classic_guest_map_services classic_services;
  qa_app_sim_rerelease_guest_map_services rerelease_services;
  bool has_revisit;
} qa_ss_pending_native_travel;
/* TS sourceRoundSettlement. */
typedef struct qa_ss_round_settlement {
  const char *kind; /* "none" | "ready" | "active" | "failed" */
  qa_app_q3_source_runtime *source;
  double completed;
} qa_ss_round_settlement;
/* TS quakeWorldCommands EventRing record. */
typedef struct qa_ss_quakeworld_command {
  bool action; /* kind "action" vs "move" */
  qa_client_id client;
  qa_qw_user_command *commands; size_t command_count; /* owned */
  double sequence;
  void *action_ctx; void (*run)(void *action_ctx);
} qa_ss_quakeworld_command;
/* TS modClientDrops EventRing record. */
typedef struct qa_ss_mod_client_drop {
  qa_client_id client; qa_actor_id actor; const char *reason; qa_content_id content;
} qa_ss_mod_client_drop;
/* A TS `() => undefined` field. */
typedef struct qa_ss_dispose { void *ctx; void (*dispose)(void *ctx); } qa_ss_dispose;
/* A TS (event: ModClientEvent) => undefined listener. */
typedef struct qa_ss_mod_client_listener { qa_mod_client_listener_fn listener; void *ctx; } qa_ss_mod_client_listener;
/* Map value records. */
typedef struct qa_ss_character_command { bool attack, jump; } qa_ss_character_command;
typedef struct qa_ss_exact_motion { qa_vec3 origin, velocity; } qa_ss_exact_motion;
typedef struct qa_ss_detached_model {
  qa_content_id content; const char *path;
  const char *family; /* NULL | "q1" | "q2" */
  bool has_skin; double skin; bool has_effects; double effects;
} qa_ss_detached_model;
typedef struct qa_ss_presentation_pose { qa_vec3 origin, angles; const char *view_path; double view_frame; } qa_ss_presentation_pose;
typedef struct qa_ss_view_model {
  const char *path; double frame;
  bool has_q2; double q2_skin, q2_rate; qa_vec3 q2_kick_origin, q2_kick_angles;
} qa_ss_view_model;
typedef struct qa_ss_optional_actor { bool has; qa_actor_id actor; } qa_ss_optional_actor;
typedef struct qa_ss_optional_item { bool has; qa_item_id item; } qa_ss_optional_item;
typedef struct qa_ss_horde_frag { double deaths; bool fragged; } qa_ss_horde_frag;
typedef struct qa_ss_lmctf_flag { bool has_actor; qa_owned_actor actor; qa_vec3 home; bool has_carrier; qa_actor_id carrier; bool has_dropped_at; double dropped_at; } qa_ss_lmctf_flag;
typedef struct qa_ss_lmctf_rune { bool has_actor; qa_owned_actor actor; bool has_holder; qa_actor_id holder; } qa_ss_lmctf_rune;
typedef struct qa_ss_qvm_weapon_decision { qa_mod_client_application *application; bool reached; } qa_ss_qvm_weapon_decision;


/* TS NativeSliceContact (native_pmove_contacts.h): touch (B3) pushes heap entries,
 * the native input binding (B1) drains and frees them. */

struct qa_app_shared_simulation {
  /* readonly options (TS constructor parameter property). */
  qa_app_simulation_options options;
  qa_decoded_world world; /* options.world decoded view (TS ApplicationWorld) */
  const char *world_kind; /* "q1-bsp" | "q2-bsp" | "q3-bsp" */
  const char *world_entities;
  const qa_q1_map *q1_map; /* world kind q1-bsp */
  qa_session_id session;
  qa_executable_recipe recipe;
  qa_session_actor_registry *actors;
  qa_actor_callback_table *callbacks;
  qa_shared_scene_queries *scene;
  qa_app_sim_shared_physics *physics;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_shared_original_pickup_admission *original_pickups;
  qa_frame_scheduler *scheduler;
  qa_scheduler_clock *scheduler_clocks; size_t scheduler_clock_count;
  qa_provider_id *scheduler_providers; /* mixed ordering copy */
  qa_app_sim_source_random *random;
  qa_app_sim_bot_services bot_services;
  /* Q3 horde wave state. */
  double horde_wave, horde_pending_wave;
  qa_ss_map horde_fragged; /* ActorId -> qa_ss_horde_frag */
  double horde_next_wave_at, horde_admission_at, horde_admission_players;
  /* Runtime Tag state. */
  bool has_tag_token; qa_owned_actor tag_token;
  bool has_tag_owner; qa_actor_id tag_owner;
  double tag_count;
  bool has_tag_dropped_at; double tag_dropped_at;
  qa_ss_map tag_health; /* ActorId -> double */
  bool has_tag_spawns; qa_vec3 *tag_spawns; size_t tag_spawn_count;
  /* Runtime LMCTF state. */
  qa_ss_map lmctf_teams;  /* ActorId -> double (1|2) */
  qa_ss_map lmctf_flags;  /* number (1|2) -> qa_ss_lmctf_flag */
  qa_ss_map lmctf_runes;  /* string -> qa_ss_lmctf_rune */
  qa_ss_map lmctf_health; /* ActorId -> double */
  qa_ss_map lmctf_regen_at; /* ActorId -> double */
  bool has_lmctf_spots; qa_ss_map lmctf_spot_flags; /* string -> qa_vec3 */ qa_vec3 *lmctf_spot_health; size_t lmctf_spot_health_count;
  bool lmctf_prepared;
  /* Runtime DeathBall state. */
  bool has_deathball; qa_owned_actor deathball;
  qa_ss_map deathball_teams; /* ActorId -> double */
  bool has_deathball_spots; bool has_deathball_ball; qa_vec3 deathball_ball; qa_ss_map deathball_goals; /* number -> qa_vec3 */
  bool deathball_prepared;
  bool has_deathball_reset_at; double deathball_reset_at;
  qa_source_clock clock;
  qa_app_sim_simulation_events *events;
  qa_app_presentation_state *presentation; /* SimulationEvents' PresentationState super */
  const char **accepted_contents; size_t accepted_content_count; /* super fog options */
  qa_ss_map actor_executions;  /* ActorId -> qa_app_sim_actor_execution */
  qa_ss_map player_states;     /* RevisionMap OwnedActor -> qa_app_sim_movement_player * */
  bool has_ordered_players; double ordered_players_revision; qa_actor_id *ordered_players; size_t ordered_player_count;
  qa_ss_map native_movement_players;   /* OwnedActor -> qa_app_sim_movement_player * */
  qa_ss_map native_character_commands; /* ActorId -> qa_ss_character_command */
  qa_ss_map native_character_health;   /* ActorId -> double */
  qa_ss_map guest_monster_plans;       /* string -> qa_app_sim_guest_monster_plan * (NULL = TS null) */
  bool guest_monsters_admitted;
  char *pending_guest_monster_report;  /* NULL = TS null */
  qa_ss_map shared_travel_applied;     /* Set<ActorId> */
  qa_ss_map native_mirrored_combat;    /* Set<ActorId> */
  qa_ss_map native_mirrored_health;    /* ActorId -> double */
  qa_ss_map native_mirror_write_reported; /* Set<ActorId> */
  bool has_native_slice_contacts; qa_app_sim_native_slice_contact **native_slice_contacts; size_t native_slice_contact_count;
  qa_ss_map native_ground_planes;      /* ActorId -> qa_plane */
  qa_ss_map native_move_carry;         /* ActorId -> double */
  qa_ss_map native_exact_motions;      /* ActorId -> qa_ss_exact_motion */
  qa_app_sim_q1_player_punch *q1_punch;
  qa_ss_map q2_characters;   /* OwnedActor -> qa_q2_character_actor * */
  qa_ss_map character_ticks; /* OwnedActor -> double */
  qa_ss_map entry_carry;     /* OwnedActor -> qa_q1_travel_state */
  char *start_items;
  char *initial_spawn_point;
  qa_ss_map pending_start_items; /* Set<ActorId> */
  qa_shared_world_restore_completion *pending_shared_restore; /* NULL = TS null */
  qa_ss_map detached_models; /* OwnedActor -> qa_ss_detached_model */
  qa_ss_map q1_characters;   /* OwnedActor -> qa_q1_character_actor * */
  qa_q1_services *q1_character_foundation;
  qa_ss_map q1_character_adjuncts; /* Set<qa_q1_services *> */
  qa_ss_map q2_views;        /* ActorId -> qa_q2_base_player_view */
  qa_q1_campaign_state q1_campaign;
  qa_ss_map characters;      /* OwnedActor -> qa_q3_character_actor * */
  qa_ss_map character_starts; /* OwnedActor -> qa_ss_optional_actor */
  qa_app_sim_grapple_runtime *grapple;
  bool has_pending_qvm_grapple_restore;
  qa_app_sim_grapple_runtime_checkpoint pending_qvm_grapple_restore_grapple;
  qa_app_sim_saved_weapon_slot *pending_qvm_grapple_restore_slots; size_t pending_qvm_grapple_restore_slot_count;
  qa_source_item_restore *source_items_restore; /* NULL = TS null */
  qa_ss_map weapon_slots;    /* ActorId -> qa_app_sim_weapon_slot * */
  qa_ss_map native_equipment_players;  /* Set<ActorId> */
  qa_ss_map native_equipment_alive;    /* ActorId -> bool */
  qa_ss_map native_weapon_requests;    /* ActorId -> double */
  qa_ss_map native_equipment_velocity; /* ActorId -> qa_vec3 */
  qa_frame_context grapple_frame;
  qa_app_sim_hand_grenade_runtime *hand_grenades;
  qa_frame_context equipment_frame;
  qa_app_q3_selected_source *selected_q3_source;
  qa_app_q3_mapped_ammo_regeneration *selected_ammo_timers;
  double selected_q3_time, selected_q3_next;
  qa_ss_map selected_q3_strings; /* number -> char * (malloc'd, owned; freed by qa_app_shared_sim_arsenal_destroy) */
  qa_app_sim_simulation_weapon_behaviors *weapon_behavior;
  qa_app_sim_application_bot_navigation *native_navigation;
  double selected_milliseconds;
  bool has_selected_arsenal; qa_ss_selected_arsenal selected_arsenal;
  bool has_selected_weapon_source; qa_ss_selected_weapon_source selected_weapon_source;
  qa_ss_map primary_command_blocks; /* Set<ActorId> */
  qa_shared_pickup_admission *selected_supply;
  qa_app_sim_source_pickup_cargo *dropped_pickups;
  const qa_pickup_supply_profile *selected_original_supply;
  bool has_selected_original_weapons; qa_ss_map selected_original_weapons; /* string -> qa_ss_optional_item */
  const qa_q1qc_pickup_policy *selected_pickup_policy;
  qa_original_pickup_rule *selected_original_pickups; size_t selected_original_pickup_count;
  qa_ss_map q3_arsenals; /* OwnedActor -> qa_q3_arsenal_runtime_state */
  qa_ss_map q3_commands; /* OwnedActor -> qa_actor_command */
  qa_q3_death_animation_sequence death_animations;
  qa_ss_map source_models; /* ActorId -> qa_q2_presentation_event (kind model) */
  /* ActorId -> bool: sourceModels records the checkpoint reader rebuilt ({ ..., renderFlags, alpha }: alpha last);
   * the next "model" event replaces the record with the emitted literal's order. C-only key order. */
  qa_ss_map source_model_read_order;
  qa_ss_map presentation_snapshot; /* ActorId -> qa_ss_presentation_pose */
  bool has_presentation_tick_milliseconds; double presentation_tick_milliseconds;
  bool presentation_ahead;
  qa_ss_map view_models; /* ActorId -> qa_ss_view_model */
  qa_event_ring transitions; /* TransitionIntent, capacity 1024 */
  qa_event_cursor transitions_cursor;
  bool has_level_change; char *level_change_map; bool has_level_change_landmark; qa_q2_landmark_carry level_change_landmark; double level_change_server_flags;
  qa_provider_reference weapon_provider;
  qa_q1_monster_movement q1_movement;
  qa_app_sim_selected_monsters *selected_monsters;
  qa_ss_map monster_sources; /* string ProviderId -> qa_app_sim_selected_monster_source * */
  bool has_foreign_world; qa_ss_foreign_world foreign_world;
  bool has_foreign_q1_world; qa_ss_foreign_q1_world foreign_q1_world;
  qa_ss_map monster_missions; /* ActorId -> qa_monster_mission */
  qa_cvar_registry *q2_server_registry;
  qa_world_debug_line_store debug_line_store;
  double debug_line_frame;
  const qa_debug_line *debug_line_snapshot; size_t debug_line_snapshot_count;
  qa_world_text_store world_text_store;
  double world_text_frame;
  const qa_world_text *const *world_text_snapshot; size_t world_text_snapshot_count;
  qa_ss_source_runtime source;
  /* TS strategy: per-source behavior, replaced together with `source` (qa_ss_set_source_strategy). */
  const qa_app_source_strategy *strategy; /* TS this.strategy: the source class's shared table */
  bool has_pending_native_travel; qa_ss_pending_native_travel pending_native_travel;
  bool has_dispose_source_combat; int64_t dispose_source_combat; /* qa_gameplay_authority_unregister id */
  qa_frame_context source_frame;
  qa_ss_map settled_q3_sources; /* WeakSet<Q3SourceRuntime> */
  double admission_settle_milliseconds;
  double host_milliseconds;
  bool q1_pause_state;
  double source_scheduling_milliseconds;
  bool closed;
  qa_session_mods *mod_owner;
  qa_ss_mod_client_listener *mod_client_listeners; size_t mod_client_listener_count, mod_client_listener_capacity;
  qa_ss_map mod_client_admissions; /* Set<ActorId> */
  qa_ss_map mod_client_commands;   /* ActorId -> qa_mod_client_command_receipt */
  bool has_dispose_native_pickup_supply; qa_ss_dispose dispose_native_pickup_supply;
  qa_compat_q2_native_primary_commands *native_primary_commands;
  qa_compat_q2_native_primary_inventory *native_primary_inventory;
  qa_compat_q2_native_primary_drop *native_primary_drop;
  qa_ss_map restored_native_inventory_selections; /* ActorId -> qa_ss_optional_item */
  qa_compat_q2_native_primary_weapons *native_primary_weapons;
  qa_mod_client_application **native_weapon_applications; size_t native_weapon_application_count, native_weapon_application_capacity;
  qa_ss_map native_travel_consumed; /* Set<ClientId> */
  qa_ss_map native_weapon_times;    /* ActorId -> double */
  bool has_native_weapon_turn; qa_actor_id native_weapon_turn_actor; bool native_weapon_turn_attack;
  qa_mod_client_application *current_qvm_arsenal_application;
  qa_ss_qvm_weapon_decision *qvm_weapon_decisions; size_t qvm_weapon_decision_count, qvm_weapon_decision_capacity;
  qa_ss_map selected_pickup_bindings; /* ActorId -> qa_ss_dispose */
  qa_mod_client_applications *mod_client_applications;
  qa_event_ring mod_client_drops; /* qa_ss_mod_client_drop, capacity 1024 */
  qa_event_cursor mod_client_drops_cursor;
  qa_mod_client_outputs *mod_client_outputs;
  qa_mod_client_services mod_clients;
  qa_mod_match_state *source_match;
  bool stepping;
  bool checkpoint_in_progress;
  qa_ss_round_settlement source_round_settlement;
  bool checking_q2_rules;
  qa_q1_client_visibility *q1_client_visibility;
  double attack_sequence;
  bool q1_restart;
  qa_ss_map last_attack;  /* OwnedActor -> qa_attack_provenance */
  qa_ss_map q2_entity_attacks; /* C: ActorId -> qa_attack_provenance * borrowed by Q2 entity.lastAttack */
  qa_ss_map area_portals; /* number -> bool */
  qa_event_ring quake_world_commands; /* qa_ss_quakeworld_command, capacity 8192 */
  qa_event_cursor quake_world_cursor;
  bool has_quake_world_touched; qa_ss_map quake_world_touched; /* Set<number> */
  qa_collision_map_settings *source_collision_settings; /* NULL = TS null */

  /* C-only owned storage behind the members above. */
  int64_t release_subscription;      /* actors.onRelease */
  qa_app_sim_q1_actor_host_env *q1_host_envs; size_t q1_host_env_count;
  qa_app_sim_q2_actor_host_env **q2_host_envs; size_t q2_host_env_count;
  void **allocations; size_t allocation_count, allocation_capacity; /* closure records freed by destroy */
  /* Owned records with their own release, run by destroy (last kept first) before the allocations are freed. */
  struct qa_ss_owned { void *value; void (*release)(void *value); } *owned; size_t owned_count, owned_capacity;
  qa_actor_id damage_scratch[2];     /* QuakeC damageRequest attacker/inflictor storage */
  int64_t *mod_client_subscriptions; size_t mod_client_subscription_count;
  /* C-only B1 storage (shared_simulation_worlds.c; freed by qa_app_shared_sim_worlds_destroy). */
  void **b1_match_players; size_t b1_match_player_count, b1_match_player_capacity; /* primaryMatchPlayer closure records */
  bool has_b1_restart_carry; qa_app_q3_captured_session b1_restart_carry; /* sessionCarry the restarted Q3 runtime borrows */
  void *b1_foreign_q2, *b1_foreign_q1; /* adjunct host records (provider timing storage) */
  void *b1_guest_monsters; qa_app_sim_source_random *b1_guest_random; /* prepareGuestMonsters adjunct (wl_selected_behavior) */
  /* C-only B2 storage (shared_simulation_arsenal.c; freed by qa_app_shared_sim_arsenal_destroy). */
  qa_app_sim_native_primary_weapon_context b2_native_host; /* context of the transient nativePrimaryWeaponHost views */
  qa_ss_dispose *b2_owned; size_t b2_owned_count, b2_owned_capacity; /* owners the B2 constructions created, released LIFO */
  qa_ss_map b2_player_ammo; /* ActorId -> qa_inventory_entry *: the selected read adopted by player->arsenal */
};

/* ---- TS free helpers (runtime.ts 270-280) ---- */
extern const qa_vec3 qa_ss_zero;
qa_vec3 qa_ss_add(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_ss_subtract(qa_vec3 a, qa_vec3 b);
double qa_ss_seconds(qa_source_time time);
bool qa_ss_is_q1_barrel(const char *classname);
/* TS sourceModel: false = TS null. */
bool qa_ss_source_model(const char *path, double *out);
/* TS requestedPath.replace(/^maps\//, "").replace(/\.bsp$/, ""), malloc'd. */
char *qa_ss_map_name(const char *requested_path);
bool qa_ss_is(const char *a, const char *b);
char *qa_ss_strdup(const char *text);
/* TS numeric String(n) of the cvar setters. */
const char *qa_ss_number_text(double value, char buffer[32]);

/* ---- Phase A: implemented in shared_simulation_core.c ---- */
qa_mod_client_services qa_app_shared_sim_create_mod_clients(qa_app_shared_simulation *sim);
qa_mod_client_player_view qa_app_shared_sim_source_client_view(qa_app_shared_simulation *sim, qa_actor_id actor);
/* false = TS null. */
bool qa_app_shared_sim_active_client_outputs(qa_app_shared_simulation *sim, qa_actor_id actor, qa_mod_client_movement_outputs *out);
void qa_app_shared_sim_assert_open(const qa_app_shared_simulation *sim);
void qa_app_shared_sim_schedule(qa_app_shared_simulation *sim, qa_owned_actor actor, double due);
bool qa_app_shared_sim_execution_provider(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id *out);
double qa_app_shared_sim_source_order(qa_app_shared_simulation *sim, qa_actor_id a, qa_actor_id b);
void qa_app_shared_sim_source_position(qa_app_shared_simulation *sim, qa_actor_id actor, double out[2]);
bool qa_app_shared_sim_collision(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_shared_solid *out);
void qa_app_shared_sim_register_actor_execution(qa_app_shared_simulation *sim, qa_app_sim_actor_execution entry);
double qa_app_shared_sim_contents(qa_app_shared_simulation *sim, qa_vec3 point, const char *family);
qa_app_sim_movement_player *qa_app_shared_sim_movement_player_of(qa_app_shared_simulation *sim, qa_owned_actor actor);
const qa_client_id *qa_app_shared_sim_client_identities(qa_app_shared_simulation *sim, size_t *out_count);
bool qa_app_shared_sim_world_actor(qa_app_shared_simulation *sim, qa_actor_id *out);
qa_app_sim_movement_player *qa_app_shared_sim_player(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_sim_movement_player *qa_app_shared_sim_require_player(qa_app_shared_simulation *sim, qa_actor_id actor);
const char *qa_app_shared_sim_classname(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_initialize_server_settings(qa_app_shared_simulation *sim, qa_cvar_registry *cvars);
const char *qa_app_shared_sim_q1_physics_edition(const qa_app_shared_simulation *sim);
/* The playerStates entry of an OwnedActor (NULL = TS undefined). */
qa_app_sim_movement_player *qa_app_shared_sim_player_state(qa_app_shared_simulation *sim, qa_owned_actor actor);
/* TS this.bodies.read(actor): false = null. */
bool qa_app_shared_sim_body(qa_app_shared_simulation *sim, qa_actor_id actor, qa_body_state *out);
/* TS this.actors.resolveOwned(actor) !== null. */
bool qa_app_shared_sim_resolve_owned(qa_app_shared_simulation *sim, qa_actor_id actor, qa_owned_actor *out);
/* TS this.events.message({ kind: "print", level: 2, text }, actor?). */
void qa_app_shared_sim_print(qa_app_shared_simulation *sim, const char *text, const qa_actor_id *actor);
/* TS options.identity.owns(client). */
bool qa_app_shared_sim_owns_client(const qa_app_shared_simulation *sim, qa_client_id client);
/* TS options.q2Guest?.prepared / q3Guest typed views. */
const qa_app_sim_native_q2_guest_options *qa_app_shared_sim_q2_guest(const qa_app_shared_simulation *sim);
/* TS this.mod clients notification (listeners set). */
void qa_app_shared_sim_add_mod_client_listener(qa_app_shared_simulation *sim, qa_mod_client_listener_fn listener, void *ctx);
/* modClients.subscribe: adds the listener and returns its delete closure (shared_simulation_support.c). */
qa_mod_client_unsubscribe qa_app_shared_sim_subscribe_listener(qa_app_shared_simulation *sim, qa_mod_client_listener_fn listener, void *ctx);
void qa_app_shared_sim_unsubscribe_listener(void *ctx);

/* ---- Phase A: shared_simulation_source.c ---- */
void qa_app_shared_sim_create_source(qa_app_shared_simulation *sim, bool loading, qa_ss_source_runtime *out);
void qa_app_shared_sim_source_runtime_destroy(qa_app_shared_simulation *sim, qa_ss_source_runtime *source);
/* TS q1ActorHost(source, runtime, role?) / q2ActorHost(source, runtime, monster, role?). role NULL = undefined. */
qa_q1_host qa_app_shared_sim_q1_actor_host(qa_app_shared_simulation *sim, qa_provider_reference source,
    qa_app_sim_actor_host_runtime runtime, const char *role);
typedef qa_q2m_state *(*qa_ss_q2_monster_fn)(void *ctx, qa_actor_id actor);
qa_q2_foundation_host qa_app_shared_sim_q2_actor_host(qa_app_shared_simulation *sim, qa_provider_reference source,
    qa_app_sim_actor_host_runtime runtime, qa_ss_q2_monster_fn monster, void *monster_ctx, const char *role);

/* ---- Phase A: shared_simulation_step.c ---- */
void qa_app_shared_sim_begin_qvm_selected_frame(qa_app_shared_simulation *sim);
void qa_app_shared_sim_run_qvm_selected_actors(qa_app_shared_simulation *sim);
qa_world_snapshot qa_app_shared_sim_snapshot(qa_app_shared_simulation *sim);
void qa_app_shared_sim_begin_native_q2_step(qa_app_shared_simulation *sim, qa_input_batch input);
qa_simulation_output qa_app_shared_sim_step_native_q2(qa_app_shared_simulation *sim, qa_input_batch input);
qa_simulation_output qa_app_shared_sim_step_native_q2_async(qa_app_shared_simulation *sim, qa_input_batch input,
    qa_app_shared_sim_progress_fn progress, void *progress_ctx);
/* TS *nativeQ2Frames generator: frame(frame_ms) runs after each yield. */
void qa_app_shared_sim_native_q2_frames(qa_app_shared_simulation *sim, qa_input_batch input,
    bool (*frame)(void *ctx, double frame_milliseconds), void *frame_ctx);
/* Step planning, exposed for tests (runtime.ts step 5244-5296). */
typedef struct qa_ss_boundary { double milliseconds; bool map, q2, q3; } qa_ss_boundary;
typedef struct qa_ss_step_plan {
  bool has_fixed; double fixed;
  bool map_run; double elapsed;
  double map_start_milliseconds, map_end_milliseconds;
  qa_ss_boundary *boundaries; size_t boundary_count;
} qa_ss_step_plan;
qa_ss_step_plan qa_app_shared_sim_plan_step(qa_app_shared_simulation *sim, qa_input_batch input, bool paused);
void qa_ss_step_plan_free(qa_ss_step_plan *plan);

/* ---- Phase B: declared for the phase A callers (docs/runtime_layout.md) ----
 * B1 worlds and rules (shared_simulation_worlds.c). */
bool qa_app_shared_sim_q1_punch_owner(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_q1_punch_owner *out);
void qa_app_shared_sim_emit_q1_addon(qa_app_shared_simulation *sim, qa_content_id content, const qa_q1ad_event *event, qa_source_time time);
void qa_app_shared_sim_advance_q1_punch(qa_app_shared_simulation *sim);
bool qa_app_shared_sim_ts_primary(qa_app_shared_simulation *sim);
bool qa_app_shared_sim_foreign_q2_identity(qa_app_shared_simulation *sim, qa_provider_reference *out);
/* TS foreignAdjunctIdentity: false = null; family "q1"|"q2", edition "classic"|"rerelease". */
bool qa_app_shared_sim_foreign_adjunct_identity(qa_app_shared_simulation *sim, qa_provider_reference *reference,
    const char **family, const char **edition);
void qa_app_shared_sim_create_foreign_world(qa_app_shared_simulation *sim);
void qa_app_shared_sim_spawn_foreign_world(qa_app_shared_simulation *sim, const double *ordinals, size_t ordinal_count, const char *entities);
void qa_app_shared_sim_prepare_selected_monsters(qa_app_shared_simulation *sim);
/* TS guestMonsterEntities(text, edition): borrowed (cached plan) result. */
const char *qa_app_shared_sim_guest_monster_entities(qa_app_shared_simulation *sim, const char *text, const char *edition);
void qa_app_shared_sim_prepare_guest_monsters(qa_app_shared_simulation *sim);
void qa_app_shared_sim_admit_guest_monsters(qa_app_shared_simulation *sim);
void qa_app_shared_sim_flush_guest_monster_report(qa_app_shared_simulation *sim);
void qa_app_shared_sim_run_guest_monsters(qa_app_shared_simulation *sim);
void qa_app_shared_sim_begin_foreign_q1_world_frame(qa_app_shared_simulation *sim, bool map);
void qa_app_shared_sim_begin_foreign_world_frame(qa_app_shared_simulation *sim, double milliseconds, bool map);
void qa_app_shared_sim_begin_monster_frames(qa_app_shared_simulation *sim, double milliseconds, bool map);
void qa_app_shared_sim_create_monster_movement(qa_app_shared_simulation *sim, qa_numeric_profile numeric,
    qa_app_sim_source_random *random, qa_q1_monster_movement *out);
const char *qa_app_shared_sim_guest_map_edition(qa_app_shared_simulation *sim);
void qa_app_shared_sim_q2_monster_dodge(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_actor_id *attacker,
    double eta, const qa_trace_result *trace);
/* TS monsterTarget(actor): false = null. */
bool qa_app_shared_sim_monster_target_notarget(qa_app_shared_simulation *sim, qa_actor_id actor);
/* TS monsterTarget(actor) as the Q1/Q2 host observation (has_value false = null). */
qa_q1_monster_target_result qa_app_shared_sim_monster_target_q1(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_q2_weapon_target_result qa_app_shared_sim_q2_weapon_target(qa_app_shared_simulation *sim, qa_actor_id actor);
bool qa_app_shared_sim_q1_visibility_eye(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q1_client_eye *out);
void qa_app_shared_sim_tick_native_characters(qa_app_shared_simulation *sim);
void qa_app_shared_sim_bind_native_input(qa_app_shared_simulation *sim);
/* TS nativeQ2Client(actor): the admitted client. */
qa_client_id qa_app_shared_sim_native_q2_client(qa_app_shared_simulation *sim, qa_actor_id actor);
bool qa_app_shared_sim_q3_guest_retired(qa_app_shared_simulation *sim);
void qa_app_shared_sim_shutdown_q3_guest(qa_app_shared_simulation *sim);
qa_q2_product_runtime *qa_app_shared_sim_q2_source(qa_app_shared_simulation *sim);
void qa_app_shared_sim_install_rerelease_navigation(qa_app_shared_simulation *sim, qa_app_sim_application_bot_navigation *navigation);
/* TS nativeModContext(): false = undefined. */
bool qa_app_shared_sim_native_mod_context(qa_app_shared_simulation *sim, void *out_context);
qa_compat_q2_rerelease_navigation_services qa_app_shared_sim_rerelease_navigation_services(qa_app_shared_simulation *sim);
void qa_app_shared_sim_tick_horde_wave(qa_app_shared_simulation *sim);
bool qa_app_shared_sim_foreign_tag(qa_app_shared_simulation *sim);
void qa_app_shared_sim_tick_tag(qa_app_shared_simulation *sim);
bool qa_app_shared_sim_foreign_lmctf(qa_app_shared_simulation *sim);
/* TS lmctfQ1HasteRules(): registered on the Q1 source game. */
void qa_app_shared_sim_register_lmctf_q1_haste_rules(qa_app_shared_simulation *sim, qa_q1_services *game);
void qa_app_shared_sim_tick_lmctf(qa_app_shared_simulation *sim);
void qa_app_shared_sim_tick_deathball(qa_app_shared_simulation *sim);
qa_victim_armor_context qa_app_shared_sim_victim_armor_context(qa_app_shared_simulation *sim, qa_damage_request request);
void qa_app_shared_sim_register_combat(qa_app_shared_simulation *sim);
/* TS canonicalTeam(actor, original): borrowed text, NULL = TS null. */
const char *qa_app_shared_sim_canonical_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *original);
bool qa_app_shared_sim_team_game(qa_app_shared_simulation *sim);
qa_source_match_player_result qa_app_shared_sim_primary_match_player(qa_app_shared_simulation *sim, qa_actor_id actor);
/* B1 members without a phase A caller (docs/runtime_layout.md names; TS getters as functions). */
void qa_app_shared_sim_worlds_destroy(qa_app_shared_simulation *sim);
bool qa_app_shared_sim_foreign_q2_identity_full(qa_app_shared_simulation *sim, qa_provider_reference *reference,
    const char **edition, const char **program);
bool qa_app_shared_sim_foreign_q1_identity(qa_app_shared_simulation *sim, qa_provider_reference *reference,
    const char **edition, const char **program);
void qa_app_shared_sim_request_horde_bot(qa_app_shared_simulation *sim, double player_count);
const qa_vec3 *qa_app_shared_sim_tag_spawn_origins(qa_app_shared_simulation *sim, size_t *out_count);
/* false = TS null. */
bool qa_app_shared_sim_farthest_deathmatch_spawn(qa_app_shared_simulation *sim, qa_vec3 *out);
/* from NULL = TS null. */
void qa_app_shared_sim_spawn_tag_token(qa_app_shared_simulation *sim, const qa_actor_id *from);
void qa_app_shared_sim_tag_token_touched(qa_app_shared_simulation *sim, qa_touch_contact contact);
void qa_app_shared_sim_add_tag_score(qa_app_shared_simulation *sim, qa_actor_id actor, double amount);
void qa_app_shared_sim_give_tag_inventory(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_take_tag_inventory(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_tag_bonus(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_grant_tag_quad(qa_app_shared_simulation *sim, qa_actor_id actor);
bool qa_app_shared_sim_foreign_ctf(qa_app_shared_simulation *sim);
/* get flagMode: "lmctf" | "ctf" | NULL (TS null). */
const char *qa_app_shared_sim_flag_mode(qa_app_shared_simulation *sim);
/* The held rune kind, NULL = TS null. */
const char *qa_app_shared_sim_lmctf_rune_held(qa_app_shared_simulation *sim, qa_actor_id actor);
bool qa_app_shared_sim_lmctf_haste(qa_app_shared_simulation *sim, qa_actor_id actor);
bool qa_app_shared_sim_lmctf_flag_home(qa_app_shared_simulation *sim, double team, qa_vec3 *out);
void qa_app_shared_sim_spawn_lmctf_flag(qa_app_shared_simulation *sim, double team, qa_vec3 origin);
void qa_app_shared_sim_lmctf_flag_touched(qa_app_shared_simulation *sim, double team, qa_touch_contact contact);
void qa_app_shared_sim_lmctf_take_flag(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_spawn_lmctf_rune(qa_app_shared_simulation *sim, const char *kind, qa_vec3 origin);
void qa_app_shared_sim_lmctf_rune_touched(qa_app_shared_simulation *sim, const char *kind, qa_touch_contact contact);
void qa_app_shared_sim_prepare_lmctf(qa_app_shared_simulation *sim);
bool qa_app_shared_sim_foreign_deathball(qa_app_shared_simulation *sim);
/* deathballOrigins(): ball (false = null); goals land in sim->deathball_goals. */
bool qa_app_shared_sim_deathball_origins(qa_app_shared_simulation *sim, qa_vec3 *ball);
void qa_app_shared_sim_spawn_deathball(qa_app_shared_simulation *sim, qa_vec3 origin);
void qa_app_shared_sim_prepare_deathball(qa_app_shared_simulation *sim);
/* serverSettings(): bindings over owner storage freed with _bound_settings_free. */
typedef struct qa_app_shared_sim_bound_settings { qa_bound_server_setting *bindings; size_t count; void *storage; } qa_app_shared_sim_bound_settings;
qa_app_shared_sim_bound_settings qa_app_shared_sim_server_settings(qa_app_shared_simulation *sim);
void qa_app_shared_sim_bound_settings_free(qa_app_shared_sim_bound_settings *settings);
void qa_app_shared_sim_server_profile(qa_app_shared_simulation *sim, qa_server_profile *out);
qa_cvar_registry *qa_app_shared_sim_q2_server_cvars(qa_app_shared_simulation *sim);
qa_app_sim_quakec_source *qa_app_shared_sim_quakec_source(qa_app_shared_simulation *sim);
const qa_ss_source_runtime *qa_app_shared_sim_q1_source(qa_app_shared_simulation *sim);
/* TS q2Native() !== null; classic / rerelease may each be NULL (not written). */
bool qa_app_shared_sim_q2_native(qa_app_shared_simulation *sim, qa_app_sim_classic_guest_world **classic, qa_app_sim_rerelease_guest_world **rerelease);
qa_app_q3_qvm_server_game *qa_app_shared_sim_q3_guest(qa_app_shared_simulation *sim);
qa_app_q3_source_runtime *qa_app_shared_sim_q3_source(qa_app_shared_simulation *sim);
typedef struct qa_app_shared_sim_q3_source_state {
  qa_content_id content; qa_app_q3_selected_source *source; qa_app_q3_source_presentation_state *state; /* state owned by the caller */
} qa_app_shared_sim_q3_source_state;
bool qa_app_shared_sim_selected_q3_source_state(qa_app_shared_simulation *sim, qa_app_shared_sim_q3_source_state *out);
/* NULL = TS null. */
const char *qa_app_shared_sim_pending_match_map(qa_app_shared_simulation *sim);
/* Returns the restarted clients (storage reused by the next call). */
const qa_client_id *qa_app_shared_sim_restart_source_round(qa_app_shared_simulation *sim, size_t *out_count);
void qa_app_shared_sim_begin_source_round_settlement(qa_app_shared_simulation *sim);
void qa_app_shared_sim_complete_source_round_settlement(qa_app_shared_simulation *sim);
/* B2 members B1 calls (shared_simulation_arsenal.c). */
qa_compat_q2_native_primary_player qa_app_shared_sim_selected_native_player(qa_app_shared_simulation *sim);
qa_qvm_primary_weapons *qa_app_shared_sim_require_qvm_weapons(qa_app_shared_simulation *sim);
qa_q1_services *qa_app_shared_sim_q1_combat_source(qa_app_shared_simulation *sim);
qa_app_sim_player_ui qa_app_shared_sim_native_player_ui(qa_app_shared_simulation *sim, qa_actor_id actor);
/* B3 members B1 calls (shared_simulation_players.c). */
qa_app_sim_movement_player *qa_app_shared_sim_create_player(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_client_id client,
    qa_vec3 origin, qa_vec3 angles, qa_arsenal_state arsenal);
/* createPlayer(actor, client, origin, angles, arsenal, characterDefinition?, characterAppearance?): NULL = TS undefined. */
qa_app_sim_movement_player *qa_app_shared_sim_create_player_with(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_client_id client,
    qa_vec3 origin, qa_vec3 angles, qa_arsenal_state arsenal, const qa_provider_reference *character_definition,
    const qa_provider_reference *character_appearance);
qa_q1_services *qa_app_shared_sim_q1_character_source(qa_app_shared_simulation *sim);
/* B1 native bridge members. */
qa_owned_actor qa_app_shared_sim_register_q2_native_client_mode(qa_app_shared_simulation *sim, qa_client_id client, bool live);
qa_app_sim_player_admission qa_app_shared_sim_admit_q2_native_player(qa_app_shared_simulation *sim, qa_client_id client, const char *userinfo, bool begin);
void qa_app_shared_sim_attach_native_character(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
qa_app_sim_movement_player *qa_app_shared_sim_native_character_host(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_sim_movement_player *const *qa_app_shared_sim_presentation_players(qa_app_shared_simulation *sim, size_t *out_count);
void qa_app_shared_sim_sync_native_movement_combat(qa_app_shared_simulation *sim, qa_owned_actor owned);
double qa_app_shared_sim_native_water_contents(qa_app_shared_simulation *sim, const qa_app_sim_movement_player *player);
qa_app_sim_application_bot_navigation *qa_app_shared_sim_rerelease_navigation(qa_app_shared_simulation *sim);
qa_app_sim_selected_monster_source *qa_app_shared_sim_monster_source_for(qa_app_shared_simulation *sim, qa_monster_definition_reference definition);
void qa_app_shared_sim_sync_guest_monster_proxies(qa_app_shared_simulation *sim);
/* captureSelectedMonsters() / captureForeignQ1World(): the TS record as a
 * checkpoint-value tree (a null value is TS null); caller frees
 * (shared_simulation_checkpoint.c). */
qa_save_value *qa_app_shared_sim_capture_selected_monsters(qa_app_shared_simulation *sim);
void qa_app_shared_sim_restore_selected_monsters(qa_app_shared_simulation *sim, const qa_app_sim_selected_monsters_checkpoint *saved);
qa_save_value *qa_app_shared_sim_capture_foreign_q1_world(qa_app_shared_simulation *sim);
void qa_app_shared_sim_restore_foreign_q1_world(qa_app_shared_simulation *sim, const qa_app_sim_foreign_q1_world_checkpoint *saved);
/* captureForeignWorld(): false = TS null; free with _foreign_world_checkpoint_free. */
bool qa_app_shared_sim_capture_foreign_world(qa_app_shared_simulation *sim, qa_app_sim_foreign_q2_world_checkpoint *out);
void qa_app_shared_sim_foreign_world_checkpoint_free(qa_app_sim_foreign_q2_world_checkpoint *checkpoint);
void qa_app_shared_sim_restore_foreign_world(qa_app_shared_simulation *sim, const qa_app_sim_foreign_q2_world_checkpoint *saved);
void qa_app_shared_sim_attach_monster(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_monster_definition_reference definition,
    const qa_monster_mission *mission);
/* q2MatchDamageEffects(): storage holds the closures' captures. */
qa_q2_damage_source_effects qa_app_shared_sim_q2_match_damage_effects(qa_app_shared_simulation *sim, qa_q2_product_match_effects_binding *storage);
bool qa_app_shared_sim_guest_monster_projection(qa_app_shared_simulation *sim, const char *edition);
/* guestMonsterPlan(text, edition): cached plan owned by the simulation; NULL = TS null. */
const qa_app_sim_guest_monster_plan *qa_app_shared_sim_guest_monster_plan(qa_app_shared_simulation *sim, const char *text, const char *edition);
bool qa_app_shared_sim_foreign_native_movement(qa_app_shared_simulation *sim);
bool qa_app_shared_sim_foreign_native_character(qa_app_shared_simulation *sim);
/* TS SelectionConsumptionEntry (contracts/session.ts; no C contract type yet). detail owned. */
typedef struct qa_app_shared_sim_selection_consumption_entry { const char *axis, *requested, *outcome; char *detail; } qa_app_shared_sim_selection_consumption_entry;
typedef struct qa_app_shared_sim_consumption { qa_app_shared_sim_selection_consumption_entry *entries; size_t count; } qa_app_shared_sim_consumption;
qa_app_shared_sim_consumption qa_app_shared_sim_selection_consumption(qa_app_shared_simulation *sim);
void qa_app_shared_sim_selection_consumption_free(qa_app_shared_sim_consumption *consumption);

/* B2 arsenal, equipment and presentation (shared_simulation_arsenal.c). */
/* TS SimulationTravel.players[].selectedArsenal: the record that
 * qa_app_sim_travel_player.selected_arsenal points at (kind "q1" | "q2" | "q3"). */
typedef struct qa_app_sim_selected_arsenal_travel {
  const char *kind;
  qa_app_q1_selected_arsenal_travel q1;                                   /* q1: state */
  qa_item_id q2_weapon; const qa_inventory_entry *q2_inventory; size_t q2_inventory_count; /* q2 */
  qa_app_q3_selected_arsenal_checkpoint q3; double q3_milliseconds;     /* q3: state, milliseconds */
} qa_ss_selected_arsenal_travel;
void qa_ss_selected_arsenal_travel_free(qa_ss_selected_arsenal_travel *travel);
/* arsenalObservation's { actor, viewAngles, viewHeight, waterLevel }. */
typedef struct qa_ss_arsenal_observation { qa_owned_actor actor; qa_vec3 view_angles; double view_height, water_level; } qa_ss_arsenal_observation;
typedef struct qa_ss_presentation_fraction { double fraction, back_lerp; } qa_ss_presentation_fraction;
typedef struct qa_ss_interpolated_pose { qa_vec3 origin, angles, previous_origin; double back_lerp; } qa_ss_interpolated_pose;
typedef struct qa_ss_visual_fields { double alpha, scale; } qa_ss_visual_fields;
/* replacedSupplyItems(profile): malloc'd (free()); items borrow the profile's. */
qa_item_id *qa_ss_replaced_supply_items(const qa_pickup_supply_profile *profile, size_t *out_count);
/* this.inventory as the selected arsenals' inventory surface. */
qa_app_inventory_access qa_app_shared_sim_inventory_access(qa_app_shared_simulation *sim);
qa_q1_services *qa_app_shared_sim_q1_combat_source(qa_app_shared_simulation *sim);
qa_qvm_primary_weapons *qa_app_shared_sim_require_qvm_weapons(qa_app_shared_simulation *sim);
qa_compat_q2_native_primary_weapons *qa_app_shared_sim_require_native_weapons(qa_app_shared_simulation *sim);
void qa_app_shared_sim_refresh_arsenal_cache(qa_app_shared_simulation *sim, qa_actor_id actor);
/* false = TS null. */
bool qa_app_shared_sim_arsenal_observation(qa_app_shared_simulation *sim, qa_actor_id actor, qa_ss_arsenal_observation *out);
void qa_app_shared_sim_sync_selected_q1_health_limit(qa_app_shared_simulation *sim, qa_actor_id actor);
/* false = TS undefined; free the record with qa_ss_selected_arsenal_travel_free. */
bool qa_app_shared_sim_capture_selected_arsenal_travel(qa_app_shared_simulation *sim, qa_actor_id actor, qa_ss_selected_arsenal_travel *out);
void qa_app_shared_sim_restore_selected_arsenal_travel(qa_app_shared_simulation *sim, qa_owned_actor actor, double maximum,
    const qa_ss_selected_arsenal_travel *carry);
/* arsenalState(actor): false = TS null; *out_owned: the ammo array is malloc'd. */
bool qa_app_shared_sim_arsenal_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_arsenal_state *out, bool *out_owned);
bool qa_app_shared_sim_native_dropped_item(qa_app_shared_simulation *sim, qa_actor_id actor, qa_actor_id pickup, qa_item_id item, double count);
/* weaponPresentationClock(): false = TS null. */
bool qa_app_shared_sim_weapon_presentation_clock(qa_app_shared_simulation *sim, qa_content_id *out_content, double *out_milliseconds);
void qa_app_shared_sim_register_resource(qa_app_shared_simulation *sim, qa_content_id content, const char *path,
    qa_resolved_resource_reference resource);
void qa_app_shared_sim_begin_presentation_frame(qa_app_shared_simulation *sim, double frame);
const qa_debug_line *qa_app_shared_sim_debug_lines(qa_app_shared_simulation *sim, size_t *out_count);
const qa_app_sim_presentation_event *const *qa_app_shared_sim_drain_presentation_events(qa_app_shared_simulation *sim, size_t *out_count);
qa_ss_presentation_fraction qa_app_shared_sim_presentation_fraction(qa_app_shared_simulation *sim);
qa_ss_interpolated_pose qa_app_shared_sim_interpolated_pose(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin,
    qa_vec3 angles, double fraction);
qa_ss_visual_fields qa_app_shared_sim_q1_visual_fields(double alpha, double scale);
/* grappleState(actor): false = TS null. */
typedef struct qa_ss_grapple_state { const char *mechanic; bool has_hook; qa_actor_id hook; bool pulling; double gravity_scale; bool prediction_suppressed; } qa_ss_grapple_state;
bool qa_app_shared_sim_grapple_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_ss_grapple_state *out);
void qa_app_shared_sim_set_grapple_input(qa_app_shared_simulation *sim, qa_actor_id actor, bool held);
void qa_app_shared_sim_set_hand_grenade_input(qa_app_shared_simulation *sim, qa_actor_id actor, bool held);
/* handGrenadeState(actor): false = TS null. */
bool qa_app_shared_sim_hand_grenade_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_hand_grenade_equipment_state *out);
/* bindEquipmentInventory(actor, initial = []). */
void qa_app_shared_sim_bind_equipment_inventory(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_inventory_entry *initial,
    size_t initial_count);
/* primaryHandoff(actor): the closure record is kept until the simulation is destroyed. */
qa_app_primary_weapon_handoff qa_app_shared_sim_primary_handoff(qa_app_shared_simulation *sim, qa_actor_id actor);
/* bindWeaponSlot(actor, state?): state NULL is TS undefined. */
void qa_app_shared_sim_bind_weapon_slot_state(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_weapon_slot_restore_state *state);
bool qa_app_shared_sim_has_weapon_slot(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_q2w_input qa_app_shared_sim_equipment_weapon_input(qa_app_shared_simulation *sim, qa_actor_id actor);
/* priority "attack" | "reverse". */
void qa_app_shared_sim_grapple_character_animation(qa_app_shared_simulation *sim, qa_actor_id actor, const char *priority, bool melee);
void qa_app_shared_sim_weapon_character_animation(qa_app_shared_simulation *sim, qa_actor_id actor, const char *priority, bool melee,
    qa_content_id content, bool reset_time);
qa_app_sim_grapple_slot_host qa_app_shared_sim_grapple_slot_host(qa_app_shared_simulation *sim);
double qa_app_shared_sim_hand_grenade_interval(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds);
qa_app_sim_player_ui qa_app_shared_sim_native_player_ui(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_sim_player_ui qa_app_shared_sim_primary_ui(qa_app_shared_simulation *sim, qa_actor_id actor);
double qa_app_shared_sim_selected_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds);
/* q2SourceDamageHooks(): quadMultiplier / sourceDamageMultiplier. */
double qa_app_shared_sim_q2_quad_multiplier(qa_app_shared_simulation *sim);
double qa_app_shared_sim_q2_source_damage_multiplier(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_frame_context qa_app_shared_sim_selected_q1_frame(qa_app_shared_simulation *sim);
void qa_app_shared_sim_qvm_client_spawned(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_qvm_weapon_step(qa_app_shared_simulation *sim, qa_mod_client_application *application, bool reached);
void qa_app_shared_sim_create_selected_q3_arsenal(qa_app_shared_simulation *sim, qa_ss_selected_arsenal *out);
void qa_app_shared_sim_create_selected_q2_arsenal(qa_app_shared_simulation *sim, qa_ss_selected_arsenal *out);
void qa_app_shared_sim_create_selected_q1_arsenal(qa_app_shared_simulation *sim, qa_ss_selected_arsenal *out);
qa_q1_services *qa_app_shared_sim_q1_weapon_source(qa_app_shared_simulation *sim);
/* TS q2ItemWeaponSource(): false = null. */
bool qa_app_shared_sim_q2_item_weapon_source(qa_app_shared_simulation *sim, qa_q2_game_services **out_game, qa_q2w_weapons **out_weapons);
/* selectedAmmo(actor): false = TS null; the timers are borrowed from the regeneration owner. */
bool qa_app_shared_sim_selected_ammo(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_q3_ta_mapped_ammo_timer **out,
    size_t *out_count);
const qa_owned_actor *qa_app_shared_sim_selected_ammo_actors(qa_app_shared_simulation *sim, size_t *out_count);
void qa_app_shared_sim_bind_selected_ammo(qa_app_shared_simulation *sim, const qa_pickup_supply_profile *profile);
void qa_app_shared_sim_bind_selected_original_pickups(qa_app_shared_simulation *sim, qa_owned_actor actor);
/* prepareSelectedOriginalPickups(profile): the profile is borrowed for the simulation's lifetime. */
void qa_app_shared_sim_prepare_selected_original_pickups(qa_app_shared_simulation *sim, const qa_pickup_supply_profile *profile);
qa_q2w_input qa_app_shared_sim_native_q2_weapon_input(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_q2w_input qa_app_shared_sim_qvm_q2_weapon_input(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_native_client_spawned(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_native_weapon_step(qa_app_shared_simulation *sim, qa_actor_id actor, bool reached);
/* giveNativeSelectedAmmo(actor, source, { kind: "set" | "add", amount }). */
void qa_app_shared_sim_give_native_selected_ammo(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id source, const char *kind,
    double amount);
/* slotProjection(actor, model): model NULL is TS null; the result (and ui.items) stays valid for the next three projections. */
qa_app_sim_weapon_slot_projection qa_app_shared_sim_slot_projection(qa_app_shared_simulation *sim, qa_actor_id actor,
    const qa_app_simulation_presentation *model);
qa_app_sim_weapon_slot_projection qa_app_shared_sim_weapon_slot(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_use_selected_inventory_item(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item);
bool qa_app_shared_sim_selected_weapon_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name,
    const char *const *args, size_t arg_count);
bool qa_app_shared_sim_source_item_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name,
    const char *const *args, size_t arg_count);
qa_item_id qa_app_shared_sim_native_drop_ammo_source(qa_app_shared_simulation *sim, qa_item_id item);
/* primaryPresentations(): scratch storage valid until the next call. */
const qa_app_simulation_presentation *qa_app_shared_sim_primary_presentations(qa_app_shared_simulation *sim, size_t *out_count);
/* previewSelectedQ3Pickup / admitSelectedQ3Pickup: the preview's offer ammo grant is thread-local storage valid until
 * the next preview; its receipt arrays are malloc'd (qa_free_pickup_supply_preview). */
qa_q3_source_pickup_preview qa_app_shared_sim_preview_selected_q3_pickup(qa_app_shared_simulation *sim, qa_q3_source_pickup_descriptor pickup);
qa_q3_source_pickup_admission qa_app_shared_sim_admit_selected_q3_pickup(qa_app_shared_simulation *sim, qa_q3_source_pickup_descriptor pickup);
/* selectedQ3Spawn / dropSelectedQ3Objectives / selectedQ3ClientChanged (the Q3SelectedSource host members). */
void qa_app_shared_sim_selected_q3_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_app_shared_sim_drop_selected_q3_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor);
void qa_app_shared_sim_selected_q3_client_changed(qa_app_shared_simulation *sim, qa_owned_actor actor,
    qa_app_q3_selected_client_effects before, qa_app_q3_selected_client_effects after);
/* nativeDropProtection(actor, kind "debit" | "consume", execute): execute's result, NULL also for TS undefined. */
void *qa_app_shared_sim_native_drop_protection(qa_app_shared_simulation *sim, qa_actor_id actor, const char *kind,
    void *(*execute)(void *ctx), void *ctx);
qa_compat_q2_native_drop_projection qa_app_shared_sim_native_drop_projection(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item);
/* nativeInventoryRows(actor): NULL = TS null; rows are thread-local storage valid until the next call. */
const qa_compat_q2_native_inventory_row *qa_app_shared_sim_native_inventory_rows(qa_app_shared_simulation *sim, qa_actor_id actor,
    size_t *out_count);
/* Stores a selected read into player->arsenal, adopting its malloc'd ammo array (the previous adopted array is freed). */
void qa_app_shared_sim_store_player_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, qa_arsenal_state arsenal);
/* C-only: releases the B2 owners (selected arsenals, equipment runtimes and their games). */
void qa_app_shared_sim_arsenal_destroy(qa_app_shared_simulation *sim);
bool qa_app_shared_sim_require_equipment_player(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_bind_source_inventory(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_inventory_state_binding binding,
    const qa_inventory_entry *saved, size_t saved_count);
bool qa_app_shared_sim_equipment_player_available(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_begin_native_equipment_frame(qa_app_shared_simulation *sim);
const qa_shared_grapple_control *qa_app_shared_sim_shared_grapple(qa_app_shared_simulation *sim);
void qa_app_shared_sim_admit_grapple(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_bind_weapon_slot(qa_app_shared_simulation *sim, qa_actor_id actor);
bool qa_app_shared_sim_request_weapon(qa_app_shared_simulation *sim, qa_actor_id actor, qa_weapon_reference weapon);
qa_q1_character_source_pose qa_app_shared_sim_q1_character_pose(qa_app_shared_simulation *sim, qa_actor_id actor);
/* TS grappleAnchor(actor): "world" | "none" | "player" | "corpse" | "brush" | "box". */
const char *qa_app_shared_sim_grapple_anchor(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_powerup(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q1_powerup powerup, double expires);
/* selectedArsenal.select/has/remove/admit over the family union. */
bool qa_app_shared_sim_selected_arsenal_select(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item);
bool qa_app_shared_sim_selected_arsenal_has(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_selected_arsenal_remove(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_arsenal_state qa_app_shared_sim_selected_arsenal_admit(qa_app_shared_simulation *sim, qa_owned_actor actor, double health, bool restored);
qa_app_sim_grapple_runtime *qa_app_shared_sim_create_grapple(qa_app_shared_simulation *sim);
void qa_app_shared_sim_initialize_qvm_grapple(qa_app_shared_simulation *sim);
qa_app_sim_hand_grenade_runtime *qa_app_shared_sim_create_hand_grenades(qa_app_shared_simulation *sim);
/* kind: NULL (TS undefined) | "alive" | "dead" | "removing". */
void qa_app_shared_sim_step_hand_grenade(qa_app_shared_simulation *sim, qa_actor_id actor, const char *kind);
qa_app_sim_qvm_weapon_behavior_source *qa_app_shared_sim_create_qvm_weapon_behavior(qa_app_shared_simulation *sim, qa_app_prepared_weapon_behavior entry);
qa_app_sim_rerelease_weapon_behavior_source *qa_app_shared_sim_create_native_weapon_behavior(qa_app_shared_simulation *sim,
    qa_app_prepared_weapon_behavior entry, void (*next_frame)(void *frame_ctx), void *frame_ctx);
void qa_app_shared_sim_weapon_event(qa_app_shared_simulation *sim, qa_content_id content, const qa_q2w_event *event);
qa_arsenal_state qa_app_shared_sim_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
qa_weapon_step_result qa_app_shared_sim_weapon_step(qa_app_shared_simulation *sim, qa_weapon_step_input input);
void qa_app_shared_sim_frame_selected_q2_weapon(qa_app_shared_simulation *sim, qa_owned_actor actor);
void qa_app_shared_sim_player_weapon(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
qa_q2w_input qa_app_shared_sim_q2_weapon_input(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
void qa_app_shared_sim_project_quake_c_attack(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_arsenal_state before);
qa_actor_command qa_app_shared_sim_prepare_arsenal_command(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player,
    qa_actor_command command, bool paused);
qa_app_sim_player_ui qa_app_shared_sim_player_ui(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_restore_native_inventory_cursors(qa_app_shared_simulation *sim, const qa_actor_id *actor);
bool qa_app_shared_sim_grant_selected_arsenal(qa_app_shared_simulation *sim, qa_actor_id actor, const char *category);
bool qa_app_shared_sim_give_selected_item(qa_app_shared_simulation *sim, qa_actor_id actor, const char *const *args, size_t arg_count);
void qa_app_shared_sim_give_q2_start_items(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_run_quake_world_new_missile(qa_app_shared_simulation *sim);
void qa_app_shared_sim_capture_presentation_snapshot(qa_app_shared_simulation *sim);
void qa_app_shared_sim_submit_debug_shapes(qa_app_shared_simulation *sim, qa_compat_q2_rerelease_debug_shapes_event event);
qa_app_sim_qc_weapon_target *qa_app_shared_sim_weapon_behavior_targets(qa_app_shared_simulation *sim, size_t *out_count);

/* B3 players, admission, travel, checkpoint and restore (shared_simulation_players.c). */
void qa_app_shared_sim_set_source_view_roll(qa_app_shared_simulation *sim, qa_actor_id actor, double roll);
const qa_actor_id *qa_app_shared_sim_players(qa_app_shared_simulation *sim, size_t *out_count);
/* TS playerClient(actor): false = null. */
bool qa_app_shared_sim_player_client(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id *out);
void qa_app_shared_sim_quake_c_client_spawned(qa_app_shared_simulation *sim, qa_owned_actor actor);
void qa_app_shared_sim_sync_quake_c_client_view(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
void qa_app_shared_sim_sync_q3_player(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
void qa_app_shared_sim_spawn_q3_player(qa_app_shared_simulation *sim, qa_q3_game_entity *entity, qa_q3_ta_spawn_pose pose);
void qa_app_shared_sim_select_q3_spawn_weapon(qa_app_shared_simulation *sim, qa_q3_game_entity *entity);
qa_q3_ta_client_movement_result qa_app_shared_sim_move_q3_client(qa_app_shared_simulation *sim, qa_q3_game_entity *entity,
    qa_q3_base_user_command command, qa_q3_ta_client_movement_options options);
void qa_app_shared_sim_place_q1_player(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, qa_q1_actor *spot,
    const qa_q1_travel_state *travel);
void qa_app_shared_sim_set_player_movement(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q2_player_movement_change change);
qa_animation_step_result qa_app_shared_sim_quake_c_character_step(qa_app_shared_simulation *sim, qa_animation_step_input input);
void qa_app_shared_sim_run_quake_world_commands(qa_app_shared_simulation *sim);
void qa_app_shared_sim_notify_client_event(qa_app_shared_simulation *sim, const char *kind, qa_actor_id actor);
void qa_app_shared_sim_observe_client_command(qa_app_shared_simulation *sim, const qa_actor_command *input);
qa_actor_id qa_app_shared_sim_require_mod_client(qa_app_shared_simulation *sim, qa_client_id client);
void qa_app_shared_sim_store_player_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *value);
const char *qa_app_shared_sim_source_player_name(qa_app_shared_simulation *sim, qa_actor_id actor);
/* NULL = TS null. */
const char *qa_app_shared_sim_source_player_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_sim_player_view qa_app_shared_sim_player_view(qa_app_shared_simulation *sim, qa_actor_id actor);
/* TS sourcePlayerView(actor): false = null. */
bool qa_app_shared_sim_source_player_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_player_view *out);
void qa_app_shared_sim_set_world_gravity(qa_app_shared_simulation *sim, double gravity);
/* TS q2Userinfo(source): ordered Map, first key wins (shared_simulation_players.c). */
typedef struct qa_ss_info { qa_app_sim_cvar_name_value *items; size_t count, capacity; } qa_ss_info;
qa_ss_info qa_ss_q2_userinfo(const char *source);
void qa_ss_info_set(qa_ss_info *info, const char *name, const char *value);
void qa_ss_info_free(qa_ss_info *info);
const char *qa_ss_info_get(const qa_ss_info *info, const char *name);
/* "\\name\\value..." of an ordered map (malloc'd). */
char *qa_ss_info_text(const qa_app_sim_cvar_name_value *items, size_t count);
/* previous.size === values.size && [...values].every(([n, v]) => previous.get(n) === v). */
bool qa_ss_info_same(const qa_app_sim_cvar_name_value *previous, size_t previous_count, const qa_ss_info *values);
void qa_app_shared_sim_userinfo_changed(qa_app_shared_simulation *sim, qa_actor_id actor, const char *previous);
/* Host members the source strategies call (TS SourceStrategyHost; shared_simulation_players.c). */
qa_app_sim_player_admission qa_app_shared_sim_admit_quake_c_player(qa_app_shared_simulation *sim, qa_client_id client,
    const qa_app_simulation_travel *travel, const char *userinfo);
void qa_app_shared_sim_spawn_q1_player(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player);
void qa_app_shared_sim_set_entry_carry(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q1_travel_state state);
bool qa_app_shared_sim_bot_slot_available(qa_app_shared_simulation *sim, qa_client_id client);
/* Strategy guest UI members (shared_simulation_arsenal.c). */
qa_app_sim_player_ui qa_ss_q3_qvm_guest_ui(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_sim_player_ui qa_ss_q2_native_guest_ui(qa_app_shared_simulation *sim, qa_actor_id actor);
/* Strategy command members implemented beside the command helpers (shared_simulation_players.c). */
bool qa_ss_q2_native_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count);
bool qa_ss_quakec_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count);
bool qa_ss_q2_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count);
bool qa_ss_q3_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count);
bool qa_ss_q1_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count);
void qa_ss_q3_after_command(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player, const char *name, const char *const *args, size_t arg_count);
void qa_ss_q1_after_command(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player, const char *name, const char *const *args, size_t arg_count);
void qa_ss_q2_after_command(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player, const char *name, const char *const *args, size_t arg_count);
/* TS setSource(source): the strategy follows the source (call after sim->source changes kind). */
static inline void qa_ss_set_source_strategy(qa_app_shared_simulation *sim) { sim->strategy = qa_app_create_source_strategy(sim); }
void qa_app_shared_sim_control_player(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 view_offset);
void qa_app_shared_sim_apply_guest_travel_carries(qa_app_shared_simulation *sim);
void qa_app_shared_sim_restore_campaign_travel(qa_app_shared_simulation *sim, const qa_app_simulation_travel *travel);
void qa_app_shared_sim_assert_bot_restore_ready(qa_app_shared_simulation *sim);
/* Save and restore SourceStrategy members defined beside their helpers in shared_simulation_checkpoint.c. */
void qa_ss_q1_checkpoint_providers(qa_app_shared_simulation *sim, const qa_app_source_checkpoint_sink *sink);
void qa_ss_q2_checkpoint_providers(qa_app_shared_simulation *sim, const qa_app_source_checkpoint_sink *sink);
void qa_ss_q3_checkpoint_providers(qa_app_shared_simulation *sim, const qa_app_source_checkpoint_sink *sink);
void qa_ss_q2_native_checkpoint_extras(qa_app_shared_simulation *sim, qa_save_value *record);
void qa_ss_q3_qvm_checkpoint_extras(qa_app_shared_simulation *sim, qa_save_value *record);
void qa_ss_q2_native_restore_loading(qa_app_shared_simulation *sim, const qa_save_image *save, qa_app_shared_sim_next_frame_fn next_frame,
    void *frame_ctx);
void qa_ss_q2_native_restore_guest(qa_app_shared_simulation *sim, const qa_save_image *save, bool native_restored);
void qa_ss_q3_restore_continuation(qa_app_shared_simulation *sim, const qa_save_image *save);
void qa_ss_q1_restore_foundation(qa_app_shared_simulation *sim, const qa_save_image *save, const qa_save_reader *reader);
void qa_ss_q2_restore_foundation(qa_app_shared_simulation *sim, const qa_save_image *save, const qa_save_reader *reader);
/* TS source.game.services.hasSourceInventory of a native Q2 source. */
bool qa_app_shared_sim_native_has_source_inventory(qa_app_shared_simulation *sim);
/* TS host.qvmDeclaration(): options.q3Guest?.prepared.primary.declaration?.digest ?? null. */
const char *qa_app_shared_sim_qvm_declaration(qa_app_shared_simulation *sim);
void qa_app_shared_sim_restore(qa_app_shared_simulation *sim, const qa_save_image *save, bool native_loaded);
/* captureTravel(spawnPoint) / captureOriginalSave(format, comment) / checkpointModsForTravel() (shared_simulation_players.c).
 * captureOriginalSave: game_directories NULL for format version 5; false only on OOM. checkpointModsForTravel: false is
 * TS undefined. */
qa_app_simulation_travel qa_app_shared_sim_capture_travel(qa_app_shared_simulation *sim, const char *spawn_point);
bool qa_app_shared_sim_capture_original_save(qa_app_shared_simulation *sim, int format_version, const char *game_directories,
    const char *comment, qa_q1_save_data *out);
/* restoreOriginalSave(save) (shared_simulation_players.c): out_unknown receives the unknown fields of the QuakeC restore,
 * which TS discards (free with qa_q1_unknown_save_fields_free). */
void qa_app_shared_sim_restore_original_save(qa_app_shared_simulation *sim, const qa_q1_save_data *save, qa_q1_unknown_save_fields *out_unknown);
bool qa_app_shared_sim_checkpoint_mods_for_travel(qa_app_shared_simulation *sim, qa_mod_travel_entry **out_mods, size_t *out_count);
/* new Q3CharacterActor(actor, provider, product, {...source-game services}) and characters.set (shared_simulation_players.c). */
qa_q3_character_actor *qa_app_shared_sim_new_q3_character(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_provider_id provider,
    const char *product, qa_content_id content);
void qa_app_shared_sim_set_q3_character(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q3_character_actor *character);
/* checkpoint / checkpointLoading / checkpointFromSource (shared_simulation_checkpoint.c).
 * The image borrows the recipe from the simulation; every other array and
 * byte buffer is owned by the save (release with qa_app_shared_sim_save_free). */
typedef struct qa_app_shared_sim_save {
  qa_save_image image;
  qa_persist_decoded owner;
  qa_body_checkpoint *bodies; size_t body_count; /* captureSharedBodies storage */
} qa_app_shared_sim_save;
void qa_app_shared_sim_checkpoint(qa_app_shared_simulation *sim, qa_app_shared_sim_save *out);
/* checkpointFromSource(rereleaseSave?, weaponBehaviors?, mods?): NULL = TS undefined. */
void qa_app_shared_sim_checkpoint_from_source(qa_app_shared_simulation *sim, const qa_app_sim_rerelease_guest_save *rerelease_save,
    const qa_save_value *weapon_behaviors, const qa_mod_session_checkpoint *mods, qa_app_shared_sim_save *out);
/* checkpointLoading(nextFrame): TS async, pumped synchronously. */
void qa_app_shared_sim_checkpoint_loading(qa_app_shared_simulation *sim, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx,
    qa_app_shared_sim_save *out);
void qa_app_shared_sim_save_free(qa_app_shared_sim_save *save);
/* checkpointFromSource's nativeOriginal: the q2-native original-save provider
 * record (encodeQ2RereleaseNativeSave / encodeQ2ClassicOriginalSave over
 * source.files.capture); bytes kept in owner. Not yet ported. */
void qa_app_shared_sim_native_original_checkpoint(qa_app_shared_simulation *sim, const qa_app_sim_rerelease_guest_save *rerelease_save,
    qa_persist_decoded *owner, qa_provider_checkpoint *out);
/* The plain records the world:simulation record embeds are built by
 * app/bootstrap/simulation/checkpoint_values.h; SourcePickupCargo.capture
 * and restore are in dropped_pickups.h. */
/* selectedWeaponSource.game.capture() for the Q2 arm: qa_q2_game_services_capture
 * (content/q2/foundation/entity_services.h). */
/* q3/guest-artifact.ts PreparedQ3Game.primary members are declared in q3/guest_artifact.h. */
/* B3 members without a phase A caller (docs/runtime_layout.md names). */
/* sharedCarryOf / sharedTravelState record: inventory borrowed. */
typedef struct qa_ss_shared_carry { double health; qa_armor_state armor; const qa_inventory_entry *inventory; size_t inventory_count; } qa_ss_shared_carry;
/* TS levelChange record: map owned by the holder. */
typedef struct qa_ss_level_change { char *map; double server_flags; bool has_landmark; qa_q2_landmark_carry landmark; } qa_ss_level_change;
const qa_active_mod_client_presentation *qa_app_shared_sim_mod_client_presentation_sources(qa_app_shared_simulation *sim, size_t *out_count);
const qa_active_mod_presentation *qa_app_shared_sim_mod_presentation_sources(qa_app_shared_simulation *sim, size_t *out_count);
/* takeModClientDrops(): malloc'd (free()). */
qa_ss_mod_client_drop *qa_app_shared_sim_take_mod_client_drops(qa_app_shared_simulation *sim, size_t *out_count);
void qa_app_shared_sim_remember_mod_client_command(qa_app_shared_simulation *sim, const qa_actor_command *input);
void qa_app_shared_sim_update_player_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *userinfo);
void qa_app_shared_sim_set_player_movement_link(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q2_player_movement_change change, bool link);
void qa_app_shared_sim_kill_box(qa_app_shared_simulation *sim, qa_owned_actor actor);
const qa_transition_intent *qa_app_shared_sim_take_transitions(qa_app_shared_simulation *sim, size_t *out_count);
bool qa_app_shared_sim_take_level_change(qa_app_shared_simulation *sim, qa_ss_level_change *out);
void qa_app_shared_sim_assert_checkpoint_ready(qa_app_shared_simulation *sim);
bool qa_app_shared_sim_shared_travel_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_ss_shared_carry *out);
bool qa_app_shared_sim_shared_carry_of(const qa_app_sim_travel_player *carried, qa_ss_shared_carry *out);
void qa_app_shared_sim_apply_shared_travel_carry(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_app_sim_travel_player *carried);
void qa_app_shared_sim_apply_shared_travel_once(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_client_id client);
void qa_app_shared_sim_restore_selected_travel(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player,
    const qa_app_sim_travel_player *carried_player);
/* resumeQ2Presentation(actor = null): actor NULL is TS null. */
void qa_app_shared_sim_resume_q2_presentation(qa_app_shared_simulation *sim, const qa_actor_id *actor);
qa_q1_services *qa_app_shared_sim_q1_character_source(qa_app_shared_simulation *sim);
qa_q1_character_actor *qa_app_shared_sim_attach_q1_character(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
void qa_app_shared_sim_attach_q2_character(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
/* spawnCharacterGib(gib, player?): player NULL is TS undefined. */
void qa_app_shared_sim_spawn_character_gib(qa_app_shared_simulation *sim, qa_q2_character_gib gib, qa_app_sim_movement_player *player);
void qa_app_shared_sim_drop_player_inventory(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
void qa_app_shared_sim_respawn_player(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
/* C-only B3 storage: the release handler and destroy free the character actors, entry carries and the
 * Q1 character foundation (shared_simulation_players.c). */
void qa_app_shared_sim_players_release(qa_app_shared_simulation *sim, qa_owned_actor actor);
/* SimulationEvents capture()/restore(): the PresentationState super plus its own `sequence` (events.ts 87-92). */
qa_save_value *qa_app_shared_sim_events_capture(qa_app_shared_simulation *sim);
void qa_app_shared_sim_events_restore(qa_app_shared_simulation *sim, const qa_save_reader *reader,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id saved), void *reference_ctx);
void qa_app_shared_sim_players_destroy(qa_app_shared_simulation *sim);
void qa_app_shared_sim_set_player_field_of_view(qa_app_shared_simulation *sim, qa_actor_id actor, double field_of_view, const char *mode);
qa_animation_step_result qa_app_shared_sim_animation_step(qa_app_shared_simulation *sim, qa_animation_step_input input);
qa_movement_continuation qa_app_shared_sim_touch(qa_app_shared_simulation *sim, qa_movement_touch_contact contact, qa_movement_state state);
/* action "jump" | "swim". */
void qa_app_shared_sim_jump(qa_app_shared_simulation *sim, qa_owned_actor actor, const char *action);
/* reserveNetQuakeClient(client, userinfo?): userinfo NULL is TS undefined. */
qa_actor_id qa_app_shared_sim_reserve_net_quake_client_with(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_sim_quakec_info *userinfo);
qa_owned_actor qa_app_shared_sim_prepare_q3_client(qa_app_shared_simulation *sim, qa_client_id client, qa_app_q3_source_runtime *source, bool bot);
/* SourceStrategy members defined beside their helpers in shared_simulation_arsenal.c (weaponDelay, equipmentDelay,
 * weaponDamageFactor), and the host members they reach. */
double qa_app_shared_sim_native_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds);
double qa_app_shared_sim_native_equipment_delay(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds);
double qa_ss_loaded_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds);
double qa_ss_q2_native_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds);
double qa_ss_q3_qvm_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds);
double qa_ss_q3_weapon_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double seconds);
double qa_ss_loaded_equipment_delay(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds);
double qa_ss_q3_qvm_equipment_delay(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds);
double qa_ss_q2_native_equipment_delay(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id provider, double seconds);
bool qa_ss_loaded_weapon_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out);
bool qa_ss_quakec_weapon_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out);
bool qa_ss_q2_native_weapon_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out);
bool qa_ss_q3_qvm_weapon_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out);
bool qa_ss_q3_weapon_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor, bool quad, double *out);
/* SourceStrategy handoff members (shared_simulation_arsenal.c). */
qa_app_primary_weapon_handoff qa_ss_loaded_handoff(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_primary_weapon_handoff qa_ss_native_handoff(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_primary_weapon_handoff qa_ss_quakec_handoff(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_primary_weapon_handoff qa_ss_q1_handoff(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_primary_weapon_handoff qa_ss_q2_handoff(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_primary_weapon_handoff qa_ss_q3_handoff(qa_app_shared_simulation *sim, qa_actor_id actor);
/* SourceStrategy selected-arsenal members defined beside their helpers (shared_simulation_arsenal.c), and the host
 * members they reach (TS damageModifierOption, selectedQ1Powerup). */
bool qa_ss_quakec_damage_modifier(qa_app_shared_simulation *sim, qa_source_damage_modifier *out);
const qa_source_damage_modifier *qa_app_shared_sim_damage_modifier_option(qa_app_shared_simulation *sim, qa_source_damage_modifier *storage);
double qa_app_shared_sim_selected_q1_powerup(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup);
double qa_ss_q3_q1_powerup_expires(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup);
double qa_ss_q3_equipment_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor);
double qa_ss_q3_equipment_firing_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double milliseconds, double persistent);
void qa_ss_q3_check_hurt_carrier(qa_app_shared_simulation *sim, qa_actor_id target, qa_actor_id attacker);
bool qa_ss_q3_check_obelisk_attack(qa_app_shared_simulation *sim, qa_actor_id target, qa_actor_id attacker);
void qa_ss_q3_return_pickup(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_selected_player_stats qa_ss_loading_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_selected_player_stats qa_ss_q1_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_selected_player_stats qa_ss_quakec_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_selected_player_stats qa_ss_q2_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_selected_player_stats qa_ss_q2_native_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_selected_player_stats qa_ss_q3_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_selected_player_stats qa_ss_q3_qvm_selected_player(qa_app_shared_simulation *sim, qa_actor_id actor);
/* SourceStrategy teleportSpawn / dropObjectives / originalPlayer / setMaxHealth and equipment members (shared_simulation_arsenal.c). */
void qa_ss_loading_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_ss_q1_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_ss_quakec_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_ss_q2_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_ss_q2_native_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_ss_q3_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_ss_q3_qvm_teleport_spawn(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_ss_loading_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor);
void qa_ss_q1_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor);
void qa_ss_quakec_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor);
void qa_ss_q2_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor);
void qa_ss_q2_native_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor);
void qa_ss_q3_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor);
void qa_ss_q3_qvm_drop_objectives(qa_app_shared_simulation *sim, qa_owned_actor actor);
void qa_ss_q1_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health);
void qa_ss_quakec_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health);
void qa_ss_q2_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health);
void qa_ss_q3_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health);
void qa_ss_q3_qvm_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health);
bool qa_ss_q2_native_original_player(qa_app_shared_simulation *sim, qa_app_source_original_player *out);
bool qa_ss_q3_qvm_original_player(qa_app_shared_simulation *sim, qa_app_source_original_player *out);
bool qa_ss_q2_native_source_inventory(qa_app_shared_simulation *sim, qa_actor_id actor, qa_inventory_state_binding *out);
bool qa_ss_q2_native_teleport_bits(qa_app_shared_simulation *sim, qa_actor_id actor, double *out);
bool qa_ss_q3_qvm_teleport_bits(qa_app_shared_simulation *sim, qa_actor_id actor, double *out);
bool qa_ss_q3_qvm_player_available(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out);
bool qa_ss_q2_native_player_available(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out);
bool qa_app_shared_sim_native_available(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_equipment_powerups qa_ss_q3_equipment_powerups(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_equipment_powerups qa_ss_q3_qvm_equipment_powerups(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_weapon_facts qa_ss_q3_weapon_facts(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_ss_q3_write_character_animation(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_actor_animation_state *animation);
/* SourceStrategy arsenal-step members (shared_simulation_arsenal.c) and the host members they reach (TS ownsQ3Equipment). */
qa_arsenal_state qa_ss_q1_base_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
qa_arsenal_state qa_ss_q2_base_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
void qa_ss_q1_player_weapon(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player);
bool qa_ss_q3_step_selected_holdable(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_weapon_step_input *input);
bool qa_ss_q3_primary_weapon_step(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_weapon_step_input *input,
    qa_weapon_step_result *out);
bool qa_app_shared_sim_owns_q3_equipment(qa_app_shared_simulation *sim);
/* SourceStrategy roster members (shared_simulation_players.c, shared_simulation_worlds.c). */
bool qa_ss_q2_native_disconnect(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player);
bool qa_ss_quakec_disconnect(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player);
qa_server_settings_owner qa_ss_q3_server_settings_owner(qa_app_shared_simulation *sim, qa_cvar_server_owner *owner);
/* SourceStrategy client-output, reaction and powerup members (shared_simulation_core.c, shared_simulation_worlds.c). */
void qa_ss_loading_check_output_claim(qa_app_shared_simulation *sim, const qa_mod_client_output_channel *channels, size_t channel_count, bool has_movement, qa_clock_profile_kind movement);
void qa_ss_quakec_check_output_claim(qa_app_shared_simulation *sim, const qa_mod_client_output_channel *channels, size_t channel_count, bool has_movement, qa_clock_profile_kind movement);
void qa_ss_q2_native_check_output_claim(qa_app_shared_simulation *sim, const qa_mod_client_output_channel *channels, size_t channel_count, bool has_movement, qa_clock_profile_kind movement);
void qa_ss_q3_qvm_check_output_claim(qa_app_shared_simulation *sim, const qa_mod_client_output_channel *channels, size_t channel_count, bool has_movement, qa_clock_profile_kind movement);
bool qa_ss_q2_native_guest_grounded(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out);
bool qa_ss_q3_qvm_guest_grounded(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out);
bool qa_ss_q2_native_guest_client_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 *out_view_offset, bool *out_crouched);
bool qa_ss_q3_qvm_guest_client_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 *out_view_offset, bool *out_crouched);
bool qa_ss_quakec_outputs_suspended(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player);
bool qa_ss_q2_outputs_suspended(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player);
bool qa_ss_q2_native_outputs_suspended(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player);
bool qa_ss_q3_qvm_outputs_suspended(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player);
void qa_ss_q2_reaction_end(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision);
void qa_ss_quakec_damage_committed(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision);
void qa_ss_q2_damage_committed(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision);
void qa_ss_q3_grant_quad(qa_app_shared_simulation *sim, qa_actor_id actor);
qa_app_source_foreign_powerups qa_ss_q3_foreign_powerups(qa_app_shared_simulation *sim, qa_actor_id actor);
/* SourceStrategy match/team members (shared_simulation_worlds.c). */
const char *qa_ss_q1_canonical_team(qa_app_shared_simulation *sim, const char *original);
const char *qa_ss_quakec_canonical_team(qa_app_shared_simulation *sim, const char *original);
const char *qa_ss_q2_canonical_team(qa_app_shared_simulation *sim, const char *original);
const char *qa_ss_q2_native_canonical_team(qa_app_shared_simulation *sim, const char *original);
const char *qa_ss_q3_canonical_team(qa_app_shared_simulation *sim, const char *original);
const char *qa_ss_q3_qvm_canonical_team(qa_app_shared_simulation *sim, const char *original);
double qa_ss_q1_match_score(qa_app_shared_simulation *sim, qa_actor_id actor);
double qa_ss_quakec_match_score(qa_app_shared_simulation *sim, qa_actor_id actor);
double qa_ss_q2_match_score(qa_app_shared_simulation *sim, qa_actor_id actor);
double qa_ss_q2_native_match_score(qa_app_shared_simulation *sim, qa_actor_id actor);
double qa_ss_q3_match_score(qa_app_shared_simulation *sim, qa_actor_id actor);
double qa_ss_q3_qvm_match_score(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_ss_q1_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value);
void qa_ss_quakec_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value);
void qa_ss_q2_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value);
void qa_ss_q2_native_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value);
void qa_ss_q3_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value);
void qa_ss_q3_qvm_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value);
void qa_ss_q1_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested);
void qa_ss_quakec_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested);
void qa_ss_q2_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested);
void qa_ss_q2_native_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested);
void qa_ss_q3_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested);
void qa_ss_q3_qvm_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested);
bool qa_ss_q2_native_declares_match(qa_app_shared_simulation *sim);
bool qa_ss_q3_qvm_declares_match(qa_app_shared_simulation *sim);
/* SourceStrategy identity, field-of-view, command and grant members (shared_simulation_players.c, shared_simulation_arsenal.c). */
void qa_ss_q1_set_identity(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_identity *identity);
void qa_ss_q2_set_identity(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_identity *identity);
bool qa_ss_q2_native_set_field_of_view(qa_app_shared_simulation *sim, qa_actor_id actor, double field_of_view, const char *mode);
bool qa_ss_q2_set_field_of_view(qa_app_shared_simulation *sim, qa_actor_id actor, double field_of_view, const char *mode);
bool qa_ss_q2_native_command_movement(qa_app_shared_simulation *sim, qa_client_id client, qa_movement_state *out);
const qa_pickup_supply_profile *qa_ss_q1_grant_profile(qa_app_shared_simulation *sim);
const qa_pickup_supply_profile *qa_ss_q2_grant_profile(qa_app_shared_simulation *sim);
const qa_pickup_supply_profile *qa_ss_q3_grant_profile(qa_app_shared_simulation *sim);
void qa_ss_q2_granted_weapon(qa_app_shared_simulation *sim, qa_owned_actor owner, const char *requested, const qa_item_id *destinations,
    size_t destination_count, const qa_pickup_supply_profile *profile);
/* SourceStrategy UI, view and presentation members (shared_simulation_arsenal.c, shared_simulation_players.c). */
qa_actor_id qa_ss_q2_ui_subject(qa_app_shared_simulation *sim, qa_actor_id actor);
size_t qa_ss_q1_ui_powerups(qa_app_shared_simulation *sim, qa_actor_id actor, qa_active_powerup_timer *out, size_t capacity);
size_t qa_ss_q2_ui_powerups(qa_app_shared_simulation *sim, qa_actor_id actor, qa_active_powerup_timer *out, size_t capacity);
size_t qa_ss_q3_ui_powerups(qa_app_shared_simulation *sim, qa_actor_id actor, qa_active_powerup_timer *out, size_t capacity);
size_t qa_ss_quakec_ui_powerups(qa_app_shared_simulation *sim, qa_actor_id actor, qa_active_powerup_timer *out, size_t capacity);
void qa_ss_loading_arsenal_ui(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_arsenal_state *arsenal, qa_app_sim_player_ui *ui, void *ui_storage);
void qa_ss_q1_arsenal_ui(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_arsenal_state *arsenal, qa_app_sim_player_ui *ui, void *ui_storage);
void qa_ss_q2_arsenal_ui(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_arsenal_state *arsenal, qa_app_sim_player_ui *ui, void *ui_storage);
void qa_ss_q3_arsenal_ui(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_arsenal_state *arsenal, qa_app_sim_player_ui *ui, void *ui_storage);
bool qa_ss_q2_native_guest_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_player_view *out);
bool qa_ss_q3_qvm_guest_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_player_view *out);
bool qa_ss_q2_chasing(qa_app_shared_simulation *sim, qa_actor_id actor);
bool qa_ss_quakec_override_view(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_view *view, bool intermission, double health,
    qa_app_sim_player_view *out);
bool qa_ss_q2_native_weapon_visible(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out);
double qa_ss_q3_qvm_guest_bob_cycle(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_ss_q2_native_presentations(qa_app_shared_simulation *sim, double fraction, double back_lerp, void *list);
void qa_ss_q3_presentations(qa_app_shared_simulation *sim, double fraction, double back_lerp, void *list);
/* SourceStrategy travel members (shared_simulation_players.c). */
void qa_ss_loading_travel_source(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel);
void qa_ss_q1_travel_source(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel);
void qa_ss_quakec_travel_source(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel);
void qa_ss_shared_travel_source(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel);
void qa_ss_q2_travel_source(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel);
void qa_ss_loading_travel_player_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_travel_player *entry);
void qa_ss_shared_travel_player_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_travel_player *entry);
void qa_ss_q1_travel_player_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_travel_player *entry);
void qa_ss_q2_travel_player_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_travel_player *entry);
bool qa_ss_q3_qvm_travelers(qa_app_shared_simulation *sim, qa_client_id *clients, qa_actor_id *actors, size_t capacity, size_t *out_count);
bool qa_ss_q3_qvm_guest_health(qa_app_shared_simulation *sim, qa_actor_id actor, double *out);
void qa_ss_q3_qvm_set_guest_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health);
size_t qa_ss_q3_qvm_guest_travel_players(qa_app_shared_simulation *sim, qa_app_source_guest_traveler *out, size_t capacity);
/* SourceStrategy.movementBindings members (shared_simulation_players.c). */
void qa_ss_q1_movement_bindings(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_clock_profile_kind selected_clock, void *player_host,
    qa_app_sim_player_movement_host *host);
void qa_ss_quakec_movement_bindings(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_clock_profile_kind selected_clock, void *player_host,
    qa_app_sim_player_movement_host *host);
qa_app_sim_player_admission qa_app_shared_sim_admit_q3_player(qa_app_shared_simulation *sim, qa_client_id client, qa_app_q3_source_runtime *source);
/* admitPlayer(client, travel = options.travel, userinfo?): travel/userinfo NULL are TS undefined. */
qa_app_sim_player_admission qa_app_shared_sim_admit_player_with(qa_app_shared_simulation *sim, qa_client_id client,
    const qa_app_simulation_travel *travel, const char *userinfo);
qa_app_sim_player_admission qa_app_shared_sim_admit_travel(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_simulation_travel *travel);

/* ---- Access structs (shared_simulation_access.c) ---- */
qa_app_sim_network_simulation qa_app_shared_sim_network_simulation(qa_app_shared_simulation *sim);
qa_app_sim_network_content qa_app_shared_sim_network_content(qa_app_shared_simulation *sim, qa_app_loaded_content *content);
qa_app_sim_q2_guest_world qa_app_shared_sim_q2_guest_world(qa_app_shared_simulation *sim);
qa_app_sim_bot_simulation qa_app_shared_sim_bot_simulation(qa_app_shared_simulation *sim);
/* The SharedSimulation members q2-match-reports.ts reads (ctx is the simulation).
 * Borrowed results (player states, slots, uuid) live until the next call on this thread. */
qa_app_q2_match_simulation qa_app_shared_sim_q2_match_simulation(qa_app_shared_simulation *sim);
/* The SharedSimulation members navigation.ts reads. recipe is simulation.recipe as the
 * caller identifies it: C keeps a value copy of the content's recipe, so the caller passes
 * the content's recipe pointer when this simulation was built from it (TS identity). */
qa_app_sim_navigation_simulation qa_app_shared_sim_navigation_simulation(qa_app_shared_simulation *sim, const qa_executable_recipe *recipe);
/* rereleaseNavigation() as the bot shells' navigation; false = TS null. */
bool qa_app_shared_sim_bot_navigation(qa_app_shared_simulation *sim, qa_app_sim_bot_navigation *out);
/* Public TS members the access structs forward to (phase B unless noted). */
bool qa_app_shared_sim_q2_movement_config(qa_app_shared_simulation *sim, qa_app_sim_q2_movement_config *out);
bool qa_app_shared_sim_q2_player_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q2_base_player_view *out);
qa_owned_actor qa_app_shared_sim_register_q2_native_client(qa_app_shared_simulation *sim, qa_client_id client);
size_t qa_app_shared_sim_q2_native_players(qa_app_shared_simulation *sim, const qa_client_id **out_clients, const qa_actor_id **out_actors);
const qa_q3_character_view *qa_app_shared_sim_character_views(qa_app_shared_simulation *sim, size_t *out_count);
const qa_world_text *qa_app_shared_sim_world_text(qa_app_shared_simulation *sim, size_t *out_count);
const char *qa_app_shared_sim_toggle_q1_pause(qa_app_shared_simulation *sim, const qa_actor_id *actor);
const qa_app_sim_presentation_event *const *qa_app_shared_sim_persistent_events(qa_app_shared_simulation *sim, size_t *out_count);
qa_save_value *const *qa_app_shared_sim_persistent_presentation(qa_app_shared_simulation *sim, size_t *out_count);
bool qa_app_shared_sim_event_resource(qa_app_shared_simulation *sim, qa_resource_id id, qa_resolved_resource_reference *out);
qa_app_sim_network_restart_plan qa_app_shared_sim_source_restart_plan(qa_app_shared_simulation *sim);
void qa_app_shared_sim_queue_quake_world_action(qa_app_shared_simulation *sim, qa_client_id client, void (*action)(void *ctx), void *action_ctx);
void qa_app_shared_sim_queue_quake_world_commands(qa_app_shared_simulation *sim, qa_client_id client, const qa_qw_user_command *commands,
    size_t count, double sequence);
qa_actor_id qa_app_shared_sim_reserve_net_quake_client(qa_app_shared_simulation *sim, qa_client_id client);
bool qa_app_shared_sim_bot_entity(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_bot_entity *out);
qa_owned_actor qa_app_shared_sim_prepare_bot_client(qa_app_shared_simulation *sim, qa_client_id client);
void qa_app_shared_sim_set_source_player_identity(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_identity *identity);
const qa_app_sim_source_objective *qa_app_shared_sim_source_objectives(qa_app_shared_simulation *sim, size_t *out_count);
bool qa_app_shared_sim_q2_weapon_source(qa_app_shared_simulation *sim, qa_q2_game_services **out_game, qa_q2w_weapons **out_weapons);
qa_app_q3_selected_arsenal *qa_app_shared_sim_selected_q3_weapon_source(qa_app_shared_simulation *sim);

/* ---- Phase A helpers shared by the region files (shared_simulation_core.c) ---- */
/* Keeps a closure record (calloc'd) alive until the simulation is destroyed. */
void *qa_app_shared_sim_keep(qa_app_shared_simulation *sim, size_t size);
/* Keeps an owned record until the simulation is destroyed, where release(value) runs. */
void qa_app_shared_sim_keep_owned(qa_app_shared_simulation *sim, void *value, void (*release)(void *value));
/* SimulationEvents super: binds the PresentationState members into the
 * events options and releases them on destroy. */
void qa_app_shared_sim_bind_presentation_super(qa_app_shared_simulation *sim, qa_app_sim_simulation_events_options *events);
void qa_app_shared_sim_release_presentation_super(qa_app_shared_simulation *sim);
/* TS this.events.retire(actor) / finishOwnerRestore() (PresentationState super). */
void qa_app_shared_sim_events_retire(qa_app_shared_simulation *sim, qa_actor_id actor);
void qa_app_shared_sim_events_finish_owner_restore(qa_app_shared_simulation *sim);
/* TS this.events.emit(content, source, time?, recipient?). */
void qa_app_shared_sim_emit(qa_app_shared_simulation *sim, qa_content_id content, qa_app_sim_source_event_input source,
    const qa_source_time *time, const qa_actor_id *recipient);
/* createSource branches in shared_simulation_source.c. */
void qa_app_shared_sim_create_q3_qvm_source(qa_app_shared_simulation *sim, qa_ss_source_runtime *out);
void qa_app_shared_sim_create_q3_source(qa_app_shared_simulation *sim, qa_ss_source_runtime *out);
qa_app_sim_classic_guest_world *qa_app_shared_sim_classic_travel_revisit(qa_app_shared_simulation *sim, void *native_ctx,
    qa_app_sim_classic_guest_world *world, qa_app_sim_classic_guest_map map, const qa_app_sim_classic_guest_map_services *binding,
    bool loading, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx);
/* A string kept alive for the simulation's lifetime (TS template literals stored in records). */
const char *qa_app_shared_sim_intern(qa_app_shared_simulation *sim, const char *text);
/* Constructor 822-841: game.bindInput(definition, {...}) for the Q3 QVM source (shared_simulation_source.c). */
void qa_app_shared_sim_bind_qvm_input(qa_app_shared_simulation *sim);
/* load 969-976: run the pendingNativeTravel closure (shared_simulation_source.c). */
void qa_app_shared_sim_run_pending_native_travel(qa_app_shared_simulation *sim, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx);
/* load 984-1027: SessionMods.open over the simulation services (phase B3, shared_simulation_players.c). */
void qa_app_shared_sim_open_mods(qa_app_shared_simulation *sim, const qa_mod_session_checkpoint *mod_state,
    qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx);
/* TS nativePrimary(): options.q2Guest?.prepared native primary (B2). NULL = TS null. */
const qa_compat_q2_native_primary_profile *qa_app_shared_sim_native_primary(qa_app_shared_simulation *sim);
/* options.enabledMods: SimulationOptions.enabled_mods points at this list. */
typedef struct qa_app_sim_mod_selections { const qa_mod_selection *items; size_t count; } qa_app_sim_mod_selections;
/* Releases ctx with dispose when the simulation is destroyed (LIFO with the B2 owners). */
void qa_app_shared_sim_own(qa_app_shared_simulation *sim, void (*dispose)(void *ctx), void *ctx);
/* options.enabledMods / modTravel lengths (types.h keeps them opaque). */
size_t qa_app_sim_enabled_mod_count(const void *enabled_mods);
size_t qa_app_sim_mod_travel_count(const void *mod_travel);

/* ---- Dependencies other modules do not export yet ----
 * presentation-state.ts: the PresentationState super of SimulationEvents
 * (emitOwned, resourceFor, registerResource, lightStyle, retire,
 * finishOwnerRestore, assertOutputConsumed). qa_app_presentation_state
 * (presentation_state.h) ports only owner lifecycle and capacity. */
typedef struct qa_app_presentation_fog_options {
  qa_content_id content; const char *const *accepted_contents; size_t accepted_content_count;
  const char *entities; void *ctx; bool (*alive)(void *ctx, qa_owned_actor actor);
} qa_app_presentation_fog_options;
qa_app_presentation_state *qa_app_presentation_state_create_source(qa_source_time (*now)(void *ctx),
    bool (*source_slot)(void *ctx, qa_actor_id actor, double *out), void *ctx, const qa_app_presentation_fog_options *fog);
void qa_app_presentation_state_emit_owned(qa_app_presentation_state *state, bool has_owner, qa_presentation_owner owner,
    qa_content_id content, qa_app_sim_source_event_input source, qa_source_time time, bool has_recipient, qa_actor_id recipient,
    qa_app_sim_presentation_event *out);
bool qa_app_presentation_state_resource_for(qa_app_presentation_state *state, qa_content_id content, const char *path, qa_resource_id *out);
void qa_app_presentation_state_register_resource(qa_app_presentation_state *state, qa_content_id content, const char *path,
    qa_resolved_resource_reference resource);
const char *qa_app_presentation_state_light_style(qa_app_presentation_state *state, double style);
void qa_app_presentation_state_assert_output_consumed(qa_app_presentation_state *state);
void qa_app_presentation_state_retire_actor(qa_app_presentation_state *state, qa_actor_id actor);
void qa_app_presentation_state_finish_owner_restore(qa_app_presentation_state *state);
/* presentation-state.ts lightStyles(seconds) (SimulationEvents super). */
const qa_scene_light_style *qa_app_shared_sim_light_styles(qa_app_shared_simulation *sim, double seconds, size_t *out_count);
/* The selected Q1 arsenal's game (TS selectedArsenal.game, a Q1EntityServices): B2. */
qa_q1_services *qa_app_shared_sim_selected_q1_game(qa_app_shared_simulation *sim);
/* equipment-runtime.ts HandGrenadeRuntime.independent?.game (NULL = undefined). */
qa_q2_entity_services *qa_app_sim_hand_grenade_runtime_independent_game(const qa_app_sim_hand_grenade_runtime *runtime);
/* presentation-state.ts takePresentation(): the drained events (SimulationEvents super). */
/* q3/guest-runtime.ts Q3QvmServerGame.game is qa_app_q3_qvm_server_game_game (q3/guest_runtime.h). */
/* selectedArsenal.ui(actor, weaponProvider) over the family union (B2, playerCommand 7886). */
qa_app_sim_player_ui qa_app_shared_sim_selected_arsenal_ui(qa_app_shared_simulation *sim, qa_actor_id actor);
/* selectedArsenal.catalog() (thread-local storage valid until the next call) and nativePrimaryWeaponHost(source.game,
 * this.actors) (B2, read by bindNativeInput). */
const qa_selected_pickup_weapon *qa_app_shared_sim_selected_arsenal_catalog(qa_app_shared_simulation *sim, size_t *out_count);
qa_compat_q2_native_primary_weapon_host qa_app_shared_sim_native_primary_weapon_host(qa_app_shared_simulation *sim);
/* TS captureNativeQ2Travel(newUnit, spawnPoint, nextFrame) (B1, shared_simulation_worlds.c): the record and every array
 * it holds are owned by the result; free with qa_app_shared_sim_native_q2_travel_free. */
qa_app_sim_native_q2_travel *qa_app_shared_sim_capture_native_q2_travel(qa_app_shared_simulation *sim, bool new_unit,
    const char *spawn_point, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx);
void qa_app_shared_sim_native_q2_travel_free(qa_app_sim_native_q2_travel *travel);
const qa_app_sim_presentation_event *const *qa_app_presentation_state_take_presentation(qa_app_presentation_state *state, size_t *out_count);
/* q3/runtime.ts new Q3SourceRuntime(options, host, mode) and the members SharedSimulation reads
 * (respawn, frameNum, intermissionTime, cheatsOk, bridge, spawns.selectSpawnPoint, drops.dropItem)
 * are in q3/runtime.h. */
#endif
