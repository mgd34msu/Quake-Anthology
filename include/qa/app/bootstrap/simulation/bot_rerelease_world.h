/* Port of src/app/bootstrap/simulation/bot-rerelease-world.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_RERELEASE_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_RERELEASE_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/bots/behavior/rerelease/data/knowledge.h"
#include "qa/bots/behavior/rerelease/nav.h"
#include "qa/bots/behavior/rerelease/world.h"
/* TS RereleaseBotObjectives; every member receives ctx. mode's result is
 * released with qa_bot_game_mode_destroy. goal false is TS null. */
typedef struct qa_app_sim_rerelease_bot_objectives {
  void *ctx;
  void (*admit)(void *ctx, qa_actor_id actor);
  qa_bot_game_mode (*mode)(void *ctx);
  int64_t (*team)(void *ctx, qa_actor_id actor);
  bool (*carrying)(void *ctx, qa_actor_id actor);
  bool (*goal)(void *ctx, qa_actor_id actor, qa_vec3 *out);
} qa_app_sim_rerelease_bot_objectives;
/* TS RereleaseFrameSnapshot: nothing the simulation exposes to bots changes
 * while their commands are being collected, so within one frame the entity
 * list is the same for every bot and a bot's own state is the same for every
 * call of its think. The first asker builds each; outside a frame nothing is
 * retained. list borrows the building world's storage, which that world does
 * not rebuild while the frame holds it. */
typedef struct qa_app_sim_rerelease_frame_snapshot {
  bool open;
  int64_t serial;
  bool has_list; const qa_bot_entity *list; size_t list_count;
} qa_app_sim_rerelease_frame_snapshot;
void qa_app_sim_rerelease_frame_snapshot_begin(qa_app_sim_rerelease_frame_snapshot *snapshot);
void qa_app_sim_rerelease_frame_snapshot_end(qa_app_sim_rerelease_frame_snapshot *snapshot);
/* TS RereleaseWorldOptions; callbacks receive ctx; all members borrowed. */
typedef struct qa_app_sim_rerelease_world_options {
  const qa_app_sim_bot_simulation *simulation;
  qa_app_sim_bot_navigation *navigation;
  const qa_bot_knowledge *knowledge;
  const qa_app_sim_rerelease_bot_objectives *objectives;
  void *ctx;
  bool (*is_bot)(void *ctx, qa_actor_id actor);
  int64_t (*identify)(void *ctx, qa_actor_id actor);
  double (*elapsed)(void *ctx);
  const qa_bot_sound *(*sounds)(void *ctx, size_t *out_count);
  qa_app_sim_rerelease_frame_snapshot *snapshot;
} qa_app_sim_rerelease_world_options;
/* TS nativeWeaponItem; NULL is TS null. */
qa_item_id qa_app_sim_native_weapon_item(const qa_app_sim_bot_simulation *simulation, qa_actor_id actor,
    const qa_bot_knowledge *knowledge, double number);
/* TS createRereleaseBotWorld(options, actor): world is the BotWorldT (ctx is
 * this struct, which must not move). Arrays returned by world members are
 * borrowed until the next call of the same member. */
typedef struct qa_app_sim_rerelease_bot_world {
  qa_app_sim_rerelease_world_options options;
  qa_actor_id actor;
  qa_numeric_profile numeric;
  qa_source_rerelease_navigation nav_source;
  qa_bot_navigation_iface nav;
  qa_bot_world world;
  qa_bot_ammo_entry *ammo; size_t ammo_count, ammo_capacity;
  qa_bot_entity *entities; size_t entity_count, entity_capacity;
  char **classnames; size_t classname_count, classname_capacity;
  bool has_remembered; int64_t remembered_frame; qa_bot_self remembered; /* TS remembered */
} qa_app_sim_rerelease_bot_world;
/* TS errors raise. NULL only on OOM. */
qa_app_sim_rerelease_bot_world *qa_app_sim_create_rerelease_bot_world(const qa_app_sim_rerelease_world_options *options, qa_actor_id actor);
void qa_app_sim_rerelease_bot_world_destroy(qa_app_sim_rerelease_bot_world *world);
#endif
