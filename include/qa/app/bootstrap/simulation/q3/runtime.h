/* Port of src/app/bootstrap/simulation/q3/runtime.ts (Q3SourceRuntime).
 * qa_app_q3_source_runtime_new(options, host, mode) is the TS constructor: it
 * builds every content/q3 subsystem from the Q3SourceHost (the subsystems
 * owner below) and the runtime methods run on those subsystems directly.
 * The owner and its table stay public for unit fixtures that build the
 * subsystems from partial hosts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_RUNTIME_H
#include "qa/app/bootstrap/simulation/q3/presentation.h"
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/content/q3/base/game/item_lifecycle.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/team_arena/commands.h"
#include "qa/contracts/time.h"
#include "qa/persistence/value.h"

typedef struct qa_app_q3_spawn_outcome {
  const char *kind; /* "dispatched"|"filtered"|"unknown" */
  const char *route, *classname, *reason;
  double slot;
} qa_app_q3_spawn_outcome;
typedef struct qa_app_q3_world_variable { const char *key, *value; } qa_app_q3_world_variable;
typedef struct qa_app_q3_spawn_report {
  qa_app_q3_world_variable *world_variables; /* malloc'd strings owned */
  size_t world_variable_count;
  qa_app_q3_spawn_outcome *outcomes; /* malloc'd; classname owned, other strings static */
  size_t outcome_count;
} qa_app_q3_spawn_report;
void qa_app_q3_spawn_report_destroy(qa_app_q3_spawn_report *report);

typedef struct qa_app_q3_runtime_entity_view {
  bool present, inuse, has_client, never_free, physics_object;
  double slot, e_type, event, event_time, flags, weapon;
  qa_q3_entity_state_record state;
  qa_owned_actor actor;
  qa_vec3 current_origin;
  const char *classname;
  double next_slot; /* teamchain; -1 none */
} qa_app_q3_runtime_entity_view;

typedef struct qa_app_q3_source_subsystems {
  void *ctx;
  /* settings */
  void (*settings_register)(void *ctx, const char *build_date);
  void (*settings_update)(void *ctx);
  double (*settings_integer)(void *ctx, const char *name);
  double (*settings_number)(void *ctx, const char *name);
  const char *(*settings_string)(void *ctx, const char *name);
  double (*settings_modification_count)(void *ctx, const char *name);
  qa_save_value *(*settings_capture)(void *ctx);
  void (*settings_restore)(void *ctx, const qa_save_value *value);
  /* random */
  double (*random_seed)(void *ctx);
  void (*random_reset)(void *ctx, double seed);
  /* records/pool */
  qa_app_q3_runtime_entity_view (*entity_by_actor)(void *ctx, qa_actor_id actor);
  qa_app_q3_runtime_entity_view (*entity_at)(void *ctx, double slot);
  double (*pool_max_clients)(void *ctx);
  double (*pool_num_entities)(void *ctx);
  void (*pool_initialize_clients)(void *ctx, double max_clients);
  void (*records_activate)(void *ctx, double slot);
  void (*records_close)(void *ctx);
  qa_app_q3_runtime_entity_view (*records_attach)(void *ctx, double slot, qa_owned_actor actor);
  const char *(*expire_events)(void *ctx, double slot);
  bool (*linked)(void *ctx, double slot);
  /* graph/level checkpoints keyed by name */
  qa_save_value *(*capture_part)(void *ctx, const char *name);
  void (*restore_part)(void *ctx, const char *name, const qa_save_value *value);
  bool has_personal_portal;
  /* load */
  void (*memory_initialize)(void *ctx);
  double (*sound_index)(void *ctx, const char *path);
  double (*model_index)(void *ctx, const char *path);
  void (*session_initialize_world)(void *ctx);
  void (*process_ip_bans)(void *ctx);
  void (*init_body_queue)(void *ctx);
  void (*registered_items_clear)(void *ctx, double game_type);
  void (*registered_items_save)(void *ctx);
  qa_app_q3_spawn_report (*spawn_entities)(void *ctx);
  void (*find_entity_teams)(void *ctx);
  void (*check_team_items)(void *ctx);
  void (*remap_teams)(void *ctx, double level_time);
  /* frame */
  bool (*missiles_run_owned)(void *ctx, qa_owned_actor actor);
  void (*missiles_run)(void *ctx, double slot);
  void (*missiles_close)(void *ctx);
  void (*run_item)(void *ctx, double slot);
  void (*run_mover)(void *ctx, double slot);
  void (*run_client)(void *ctx, double slot);
  void (*run_think)(void *ctx, double slot, double time);
  void (*client_end_frame)(void *ctx, double slot);
  void (*match_checks)(void *ctx);
  void (*team_check_status)(void *ctx);
  /* clients */
  const char *(*admission_connect)(void *ctx, double client, bool first, bool bot); /* NULL admitted */
  void (*admission_begin)(void *ctx, double client);
  void (*admission_disconnect)(void *ctx, double slot);
  void (*client_think)(void *ctx, double slot, qa_q3_base_user_command command);
  void (*command_dispatch)(void *ctx, double slot, const char *const *argv, size_t argc);
  void (*weapon_fire)(void *ctx, double slot);
  void (*set_entity_weapon)(void *ctx, double slot, double weapon);
  double (*client_weapon)(void *ctx, double slot);
  void (*session_write_world)(void *ctx);
  void (*session_write_client)(void *ctx, double slot);
  bool (*client_connected)(void *ctx, double slot);
  /* combat/misc */
  void (*bridge_before_reaction)(void *ctx, qa_damage_decision decision);
  void (*player_die)(void *ctx, double slot, double damage, double means_of_death);
  double (*observe_supply)(void *ctx, qa_actor_id pickup, qa_actor_id recipient);
  bool (*step_arsenal)(void *ctx, qa_actor_id actor, bool pressed); /* stepHoldable fallback when no arsenal */
  /* presentation pool view */
  qa_q3_entity_pool *(*pool)(void *ctx);
  const char *content;
  /* TS Q3SourceRuntime.records / .admission (network-q3 reads them). NULL when
   * a table does not provide them. */
  struct qa_q3_base_entity_records *(*records)(void *ctx);
  struct qa_q3_ta_client_admission_runtime *(*admission)(void *ctx);
} qa_app_q3_source_subsystems;

typedef struct qa_app_q3_source_runtime_mode {
  bool restore;
  const qa_save_value *state;
} qa_app_q3_source_runtime_mode;

typedef struct qa_app_q3_source_runtime qa_app_q3_source_runtime;
typedef struct qa_app_q3_source_subsystems_owner qa_app_q3_source_subsystems_owner;
/* new Q3SourceRuntime(options, host, mode). options and host are copied; the
 * strings, owners and session carry they point to are borrowed and must outlive
 * the runtime. mode.state is borrowed until finish_native_restore. Validation
 * errors abort; NULL only on OOM. */
qa_app_q3_source_runtime *qa_app_q3_source_runtime_new(qa_app_q3_source_options options,
    qa_app_q3_source_host host, qa_app_q3_source_runtime_mode mode);
/* The subsystems the constructor built (borrowed; valid until destroy). */
qa_app_q3_source_subsystems_owner *qa_app_q3_source_runtime_owner(qa_app_q3_source_runtime *runtime);
/* TS Q3SourceRuntime members SharedSimulation reads: spawns.respawn(entity),
 * level.frameNum, level.intermissionTime, commands.cheatsOk(entity), bridge. */
void qa_app_q3_source_runtime_respawn(qa_app_q3_source_runtime *runtime, qa_q3_game_entity *entity);
double qa_app_q3_source_runtime_frame_num(const qa_app_q3_source_runtime *runtime);
double qa_app_q3_source_runtime_intermission_time(const qa_app_q3_source_runtime *runtime);
bool qa_app_q3_source_runtime_cheats_ok(qa_app_q3_source_runtime *runtime, qa_q3_game_entity *entity);
struct qa_q3_base_combat_bridge *qa_app_q3_source_runtime_bridge(qa_app_q3_source_runtime *runtime);
/* level.intermissionQueued, team and world: the members SharedSimulation.createSelectedQ3Arsenal reads. */
double qa_app_q3_source_runtime_intermission_queued(const qa_app_q3_source_runtime *runtime);
struct qa_q3_ta_team_runtime *qa_app_q3_source_runtime_team(qa_app_q3_source_runtime *runtime);
qa_q3_base_server_world qa_app_q3_source_runtime_world(qa_app_q3_source_runtime *runtime);
/* spawns.selectSpawnPoint(origin) and drops.dropItem(entity, item, angle). */
qa_q3_ta_spawn_pose qa_app_q3_source_runtime_select_spawn_point(qa_app_q3_source_runtime *runtime, qa_vec3 origin);
qa_q3_game_entity *qa_app_q3_source_runtime_drop_item(qa_app_q3_source_runtime *runtime, qa_q3_game_entity *entity,
    const qa_q3_base_item_definition *item, double angle);
/* TS observeSupply: false is TS null. */
bool qa_app_q3_source_runtime_observe_supply_value(qa_app_q3_source_runtime *runtime, qa_actor_id pickup,
    qa_actor_id recipient, qa_q3_supply_observation *out);
void qa_app_q3_source_runtime_destroy(qa_app_q3_source_runtime *runtime);
const qa_app_q3_source_options *qa_app_q3_source_runtime_options(const qa_app_q3_source_runtime *runtime);
qa_app_q3_source_host *qa_app_q3_source_runtime_host(qa_app_q3_source_runtime *runtime);
double qa_app_q3_source_runtime_level_time(const qa_app_q3_source_runtime *runtime);
qa_save_value *qa_app_q3_source_runtime_capture_native_state(qa_app_q3_source_runtime *runtime);
#include <stdint.h>
uint8_t *qa_app_q3_source_runtime_capture_native_bytes(qa_app_q3_source_runtime *runtime, size_t *out_length);
void qa_app_q3_source_runtime_finish_native_restore(qa_app_q3_source_runtime *runtime);
double qa_app_q3_source_runtime_game_type(qa_app_q3_source_runtime *runtime);
/* TS Q3SourceRuntime.pool (the subsystem table's entity pool). */
qa_q3_entity_pool *qa_app_q3_source_runtime_pool(qa_app_q3_source_runtime *runtime);
/* TS sourceState(): owned state, NULL only on OOM. */
qa_app_q3_source_presentation_state *qa_app_q3_source_runtime_source_state(qa_app_q3_source_runtime *runtime);
size_t qa_app_q3_source_runtime_presentations(qa_app_q3_source_runtime *runtime, qa_app_simulation_presentation *out, size_t capacity);
const qa_app_q3_spawn_report *qa_app_q3_source_runtime_spawn_report(qa_app_q3_source_runtime *runtime);
double qa_app_q3_source_runtime_observe_supply(qa_app_q3_source_runtime *runtime, qa_actor_id pickup, qa_actor_id recipient);
double qa_app_q3_source_runtime_quad_damage_factor(qa_app_q3_source_runtime *runtime);
bool qa_app_q3_source_runtime_step_holdable(qa_app_q3_source_runtime *runtime, qa_actor_id actor, bool pressed);
const char *qa_app_q3_source_runtime_restart_compatibility(qa_app_q3_source_runtime *runtime);
void qa_app_q3_source_runtime_close(qa_app_q3_source_runtime *runtime);
const qa_app_q3_spawn_report *qa_app_q3_source_runtime_load(qa_app_q3_source_runtime *runtime);
typedef struct qa_app_q3_foreign_spawn { double ordinal; const char *classname; } qa_app_q3_foreign_spawn;
size_t qa_app_q3_source_runtime_foreign_spawns(qa_app_q3_source_runtime *runtime, qa_app_q3_foreign_spawn *out, size_t capacity);
void qa_app_q3_source_runtime_begin_frame(qa_app_q3_source_runtime *runtime, qa_frame_context frame);
void qa_app_q3_source_runtime_run_actor(qa_app_q3_source_runtime *runtime, qa_owned_actor actor);
void qa_app_q3_source_runtime_end_frame(qa_app_q3_source_runtime *runtime);
qa_app_q3_runtime_entity_view qa_app_q3_source_runtime_prepare_client(qa_app_q3_source_runtime *runtime,
    qa_owned_actor actor, double client);
/* Denial raises QA_APP_Q3_CLIENT_ADMISSION_DENIED (TS Q3ClientAdmissionDenied)
 * with the admission's rejection text, which callers catch with qa_try. */
#define QA_APP_Q3_CLIENT_ADMISSION_DENIED "Q3ClientAdmissionDenied"
qa_app_q3_runtime_entity_view qa_app_q3_source_runtime_admit_player(qa_app_q3_source_runtime *runtime,
    qa_owned_actor actor, double client);
/* TS Q3SourceRuntime members network-q3.ts reads besides the pool:
 * records.byActor (NULL is TS null) and admission.userinfoChanged. They abort
 * when the subsystem table provides no such owner. */
qa_q3_game_entity *qa_app_q3_source_runtime_record_by_actor(qa_app_q3_source_runtime *runtime, qa_actor_id actor);
/* TS Q3SourceRuntime.records (records.nativeByActor and friends); aborts like record_by_actor. */
struct qa_q3_base_entity_records *qa_app_q3_source_runtime_records(qa_app_q3_source_runtime *runtime);
/* TS Q3SourceRuntime.settings.string(name) (= settings.snapshot(name).value). */
const char *qa_app_q3_source_runtime_setting_string(qa_app_q3_source_runtime *runtime, const char *name);
void qa_app_q3_source_runtime_userinfo_changed(qa_app_q3_source_runtime *runtime, double client);
typedef struct qa_app_q3_captured_session {
  qa_app_q3_source_session_carry carry;
  qa_app_q3_source_session_carry_client *clients; /* owned strings */
} qa_app_q3_captured_session;
qa_app_q3_captured_session qa_app_q3_source_runtime_capture_session(qa_app_q3_source_runtime *runtime);
void qa_app_q3_captured_session_destroy(qa_app_q3_captured_session *session);
void qa_app_q3_source_runtime_disconnect_player(qa_app_q3_source_runtime *runtime, qa_actor_id actor);
void qa_app_q3_source_runtime_player_think(qa_app_q3_source_runtime *runtime, qa_actor_command input);
void qa_app_q3_source_runtime_player_command(qa_app_q3_source_runtime *runtime, qa_actor_id actor,
    const char *name, const char *const *args, size_t argc);
void qa_app_q3_source_runtime_player_weapon(qa_app_q3_source_runtime *runtime, qa_actor_id actor);
void qa_app_q3_source_runtime_before_reaction(qa_app_q3_source_runtime *runtime, qa_owned_actor actor,
    qa_damage_decision decision);
void qa_app_q3_source_runtime_adjust_area_portal_state(qa_app_q3_source_runtime *runtime, double slot, bool open);

/* ---- Bootstrap construction (TS runtime.ts itemLifecycle 160-169,
 * bindItemSaveCallbacks 173, createCommands 439-452, load spawnItem
 * 553-554). All services borrowed; every service fn gets services.ctx. --- */
typedef struct qa_app_q3_item_lifecycle_services {
  void *ctx;
  /* world: ServerWorld & Pick<ActorSpatialQueries,"traceActor"> */
  qa_q3_base_server_trace_result (*trace)(void *ctx, qa_q3_base_server_trace_query query);
  qa_q3_base_actor_trace_result (*trace_actor)(void *ctx, qa_q3_base_actor_trace_query query);
  /* (item, player) => team.pickupTeam / useTargets(targets(), ...) */
  double (*team_pickup)(void *ctx, qa_q3_game_entity *item, qa_q3_game_entity *player);
  void (*use_targets)(void *ctx, qa_q3_game_entity *item, qa_q3_game_entity *activator);
  double (*sound_index)(void *ctx, const char *path); /* config.soundIndex */
  double (*rand_value)(void *ctx);                   /* random.rand */
  double (*random)(void *ctx);                       /* random.random */
  void (*log)(void *ctx, const char *message);       /* engine.log */
  void (*warn)(void *ctx, const char *message);      /* engine.print */
  char *(*get_userinfo)(void *ctx, double client);   /* malloc'd; engine.getUserinfo */
  /* host.previewPickup/admitPickup (types.h opaque): NULL = TS undefined,
   * yielding {kind:"native"}. When set, descriptor points to
   * qa_q3_source_pickup_descriptor and out to the matching preview or
   * admission struct; returning false also yields native. */
  bool (*preview_pickup)(void *ctx, const void *descriptor, void *out_preview);
  bool (*admit_pickup)(void *ctx, const void *descriptor, void *out_admission);
  bool has_original_pickups; /* ...(host.originalPickups === undefined ? {} : ...) */
  qa_original_pickup_admission original_pickups;
  /* settings.integer; refreshes gameType/g_weaponrespawn/
   * g_weaponTeamRespawn (TS getters). NULL leaves zeroed fields. */
  double (*settings_integer)(void *ctx, const char *name);
} qa_app_q3_item_lifecycle_services;

/* Owns the filled context plus the registry's registered table.
 * context.ctx points at this host: storage must not move after init. */
typedef struct qa_app_q3_item_lifecycle_host {
  qa_q3_item_lifecycle_context context;
  qa_app_q3_item_lifecycle_services services;
  char *handicap; /* owned scratch for handicapForClient */
} qa_app_q3_item_lifecycle_host;
/* ItemRegistry(product) over itemList(product). Registers
 * q3.item.touch/q3.item.respawn pool callbacks and calls
 * qa_q3_bind_item_save_callbacks. */
void qa_app_q3_item_lifecycle_host_init(qa_app_q3_item_lifecycle_host *host,
    qa_q3_entity_pool *entities, qa_q3_base_product product,
    qa_app_q3_item_lifecycle_services services);
void qa_app_q3_item_lifecycle_host_destroy(qa_app_q3_item_lifecycle_host *host);
/* Refreshes the settings-derived fields, then returns the context. */
qa_q3_item_lifecycle_context qa_app_q3_item_lifecycle_host_context(qa_app_q3_item_lifecycle_host *host);

/* TS load() spawnItem closure: spawnItem(entity, item, variables,
 * () => gameAtoi(engineCvar(`disable_${item.className}`)) !== 0,
 * itemLifecycle); engineCvar is host.cvars.variableString. */
typedef struct qa_app_q3_spawn_item_host {
  qa_app_q3_item_lifecycle_host *items; /* borrowed */
  qa_cvar_registry *cvars;              /* borrowed */
} qa_app_q3_spawn_item_host;
/* Sets spawn->ctx/spawn->spawn_item so spawn.c reaches qa_q3_spawn_item. */
void qa_app_q3_wire_spawn_item(qa_q3_spawn_context *spawn, qa_app_q3_spawn_item_host *host);

/* TS createCommands host object. grant/give are host.grantSelectedArsenal/
 * host.giveSelectedItem (NULL = TS ?. ?? false); player_die is
 * death.playerDie. items is the constructed itemLifecycle above. */
typedef struct qa_app_q3_game_command_host_parts {
  qa_q3_entity_pool *pool;
  qa_q3_ta_match_state *state;
  qa_q3_base_player_state_slots *team_scores;
  const qa_q3_ta_command_settings *settings;
  qa_q3_ta_game_command_imports imports;
  qa_q3_ta_team_runtime *team;
  qa_q3_ta_client_spawn_runtime *spawn;
  qa_q3_ta_client_admission_runtime *admission;
  qa_q3_ta_match_runtime *match;
  qa_app_q3_item_lifecycle_host *items;
  qa_q3_teleport_context teleport; /* { combat, world } */
  void *ctx;
  bool (*grant_selected_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  bool (*give_selected_item)(void *ctx, qa_actor_id actor, const char *const *args, size_t arg_count);
  void (*player_die)(void *ctx, qa_q3_game_entity *self, qa_q3_damage_inflictor inflictor,
      qa_q3_damage_participant attacker, double damage, double method_of_death);
} qa_app_q3_game_command_host_parts;
/* The result's spawn_item field stays NULL: TS GameCommandHost has none
 * (commands.ts calls spawnItem directly through host.items). */
qa_q3_ta_game_command_host qa_app_q3_build_game_command_host(qa_app_q3_game_command_host_parts parts);

/* ---- Subsystems aggregate (TS Q3SourceRuntime constructor fields,
 * runtime.ts 73-231 + create* helpers). The owner constructs every
 * landed content/q3 subsystem in TS order and cross-wires the internal
 * pointers/getters between them; parts carry only the external leaf
 * services (session tables, engine imports, item table, spawn text).
 * Snapshot-valued host fields (TS live getters flattened to values in
 * the landed hosts) are refreshed by the table trampolines. Settings
 * reads before settings_register/settings_restore return 0: TS only
 * evaluates those getters after register(), while several C hosts
 * sample them eagerly at construction. ------------------------------- */
#include "qa/content/q3/base/combat_bridge.h"
#include "qa/content/q3/base/game/death.h"
#include "qa/content/q3/base/game/item_motion.h"
#include "qa/content/q3/base/game/level.h"
#include "qa/content/q3/base/game/memory.h"
#include "qa/content/q3/base/game/missile.h"
#include "qa/content/q3/base/game/mover.h"
#include "qa/content/q3/base/game/mover_spawn.h"
#include "qa/content/q3/base/game/numeric.h"
#include "qa/content/q3/base/game/personal_portal.h"
#include "qa/content/q3/base/game/shader_remaps.h"
#include "qa/content/q3/base/game/spawn.h"
#include "qa/content/q3/base/game/targets.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/content/q3/base/game/weapon.h"
#include "qa/content/q3/base/map_spawns.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/settings.h"
#include "qa/content/q3/base/world_adapter.h"
#include "qa/content/q3/team_arena/arenas.h"
#include "qa/content/q3/team_arena/client_admission.h"
#include "qa/content/q3/team_arena/client_spawn.h"
#include "qa/content/q3/team_arena/client_think.h"
#include "qa/content/q3/team_arena/match.h"
#include "qa/content/q3/team_arena/server_commands.h"
#include "qa/content/q3/team_arena/session.h"
#include "qa/content/q3/team_arena/team.h"

typedef struct qa_app_q3_source_subsystems_parts {
  /* The Q3SourceHost the TS constructor closes over (borrowed). When set, every
   * leaf service below that is NULL is filled from it and from the owned
   * subsystems exactly as runtime.ts wires them; NULL keeps the unit-fixture
   * behavior (unset leaves stay unset). */
  const qa_app_q3_source_host *host;
  bool foreign_world; /* options.foreignWorld: adjustAreaPortalState is a no-op */
  qa_q3_base_product product;
  double max_clients;
  qa_provider_id map_provider; /* options.recipe.map.entities.provider */
  qa_q3_base_settings_host settings_host;       /* remap_teams overwritten */
  qa_q3_base_record_host records_host;          /* damage_call/admit_damage overwritten */
  qa_q3_base_world_adapter_host world_host;     /* curves/player_curve_clip overwritten when cvars set */
  qa_q3_entity_pool_options pool_options;       /* records/product/time/link/unlink overwritten */
  qa_q3_config_string_store config_store;       /* host.configstrings */
  double (*memory_debug_integer)(void *ctx);    /* NULL: settings g_debugAlloc */
  void *memory_ctx;
  qa_app_q3_item_lifecycle_services item_services; /* trace/team/use/sound/random/settings overwritten */
  qa_q3_base_combat_bridge_host bridge_host;    /* internal pointers + TS closures overwritten */
  qa_q3_missile_host missile_host;
  qa_q3_weapon_host weapon_host;                /* combat/world/random/unlink overwritten */
  qa_q3_ta_team_host team_host;                 /* internal pointers + score/ranks/respawn overwritten */
  qa_q3_death_host death_host;                  /* internal pointers + frame/drops/team overwritten */
  qa_q3_ta_client_think_host think_host;        /* pool/world/effects/frame/settings overwritten */
  qa_q3_ta_client_spawn_host client_spawn_host; /* pool/world/random/die/match overwritten */
  qa_q3_ta_session_services session_services;   /* broadcast_team_change overwritten */
  qa_q3_ta_match_host match_host;               /* internal pointers + TS closures overwritten */
  qa_q3_mover_host mover_host;
  qa_q3_mover_spawn_host mover_spawn_host;      /* movers/pool/world/gravity/sound/time overwritten */
  qa_q3_personal_portal_host portal_host;       /* missionpack only */
  qa_q3_ta_client_admission_host admission_host; /* internal pointers + TS closures overwritten */
  qa_app_q3_game_command_host_parts command_parts; /* internal pointers overwritten */
  qa_q3_ta_game_server_command_host server_command_host; /* set_team/memory/podium overwritten */
  qa_q3_kill_box_host kill_box;                 /* spawns.killBox + portal/triggers */
  qa_q3_misc_spawn_host misc_spawn_host;        /* spawnHandlers() misc */
  qa_q3_trigger_host trigger_host;              /* spawnHandlers() triggers; shared fields overwritten */
  qa_q3_target_runtime target_runtime;          /* spawnHandlers() targets; shared fields overwritten */
  const char *spawn_text;                       /* options.entities */
  qa_q3_worldspawn_context worldspawn;          /* pool/start_time overwritten; motd/cvars fns external */
  const char *content;                          /* subsystems.content for presentation */
} qa_app_q3_source_subsystems_parts;

/* Heap owner: game memory alone embeds the 256K pool. Storage must not
 * move after create (subsystem hosts borrow owner addresses). */
/* NULL only on OOM (settings/records creation); invalid pool options abort. */
qa_app_q3_source_subsystems_owner *qa_app_q3_source_subsystems_owner_create(
    qa_app_q3_source_subsystems_parts parts);
void qa_app_q3_source_subsystems_owner_destroy(qa_app_q3_source_subsystems_owner *owner);
/* The qa_app_q3_source_subsystems table bound to the owned subsystems.
 * Entries with no landed owner API stay NULL (see runtime.c note). */
qa_app_q3_source_subsystems qa_app_q3_source_subsystems_owner_table(
    qa_app_q3_source_subsystems_owner *owner);
/* Refresh the flattened snapshot fields from level + settings (the TS
 * live getters). Table trampolines call this; exposed for hosts that
 * read owner subsystems directly. */
void qa_app_q3_source_subsystems_owner_refresh(qa_app_q3_source_subsystems_owner *owner);
/* Owned subsystem accessors (borrowed; valid until destroy). */
qa_q3_game_level *qa_app_q3_source_subsystems_owner_level(qa_app_q3_source_subsystems_owner *owner);
qa_q3_entity_pool *qa_app_q3_source_subsystems_owner_pool(qa_app_q3_source_subsystems_owner *owner);
qa_q3_base_entity_records *qa_app_q3_source_subsystems_owner_records(qa_app_q3_source_subsystems_owner *owner);
qa_q3_base_world_adapter *qa_app_q3_source_subsystems_owner_world(qa_app_q3_source_subsystems_owner *owner);
qa_q3_base_game_settings *qa_app_q3_source_subsystems_owner_settings(qa_app_q3_source_subsystems_owner *owner);
qa_q3_game_combat_context *qa_app_q3_source_subsystems_owner_combat(qa_app_q3_source_subsystems_owner *owner);
qa_q3_ta_game_command_runtime *qa_app_q3_source_subsystems_owner_commands(qa_app_q3_source_subsystems_owner *owner);
/* TS Q3SourceRuntime.serverCommands and .spawns (application.ts reads them). */
struct qa_q3_ta_game_server_command_runtime *qa_app_q3_source_subsystems_owner_server_commands(qa_app_q3_source_subsystems_owner *owner);
struct qa_q3_ta_client_spawn_runtime *qa_app_q3_source_subsystems_owner_spawns(qa_app_q3_source_subsystems_owner *owner);
qa_q3_ta_client_admission_runtime *qa_app_q3_source_subsystems_owner_admission(qa_app_q3_source_subsystems_owner *owner);
qa_q3_ta_team_runtime *qa_app_q3_source_subsystems_owner_team(qa_app_q3_source_subsystems_owner *owner);
qa_q3_ta_match_runtime *qa_app_q3_source_subsystems_owner_match(qa_app_q3_source_subsystems_owner *owner);
qa_app_q3_item_lifecycle_host *qa_app_q3_source_subsystems_owner_items(qa_app_q3_source_subsystems_owner *owner);
/* TS Q3SourceRuntime fields read by q3BotGame (bots/behavior/q3/source-game.ts). */
qa_q3_base_product qa_app_q3_source_subsystems_owner_product(qa_app_q3_source_subsystems_owner *owner);
/* TS gameType getter: settings g_gametype (0 before settings register/restore). */
double qa_app_q3_source_subsystems_owner_game_type(qa_app_q3_source_subsystems_owner *owner);
qa_game_random *qa_app_q3_source_subsystems_owner_random(qa_app_q3_source_subsystems_owner *owner);
qa_q3_game_memory *qa_app_q3_source_subsystems_owner_memory(qa_app_q3_source_subsystems_owner *owner);
qa_q3_config_string_registry *qa_app_q3_source_subsystems_owner_config(qa_app_q3_source_subsystems_owner *owner);
qa_q3_missile_runtime *qa_app_q3_source_subsystems_owner_missiles(qa_app_q3_source_subsystems_owner *owner);
qa_q3_ta_arena_runtime *qa_app_q3_source_subsystems_owner_arenas(qa_app_q3_source_subsystems_owner *owner);
#endif
