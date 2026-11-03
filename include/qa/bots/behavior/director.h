/*
 * Port of src/bots/behavior/director.ts (SourceBotDirector): source arena/AI
 * scheduling generates commands for the existing shared player pipeline.
 *
 * C accommodations:
 *  - The director is heap allocated and must not move: the library, BSP
 *    entities, AI and catalog bind to it. Options and the host are borrowed.
 *  - TS BotNavigation & GoalNavigation is the pair qa_source_bot_navigation;
 *    the navigation factory fills it. Both tables are borrowed by the round.
 *  - TS errors raised for lifecycle misuse abort (qa_range_error). Failures
 *    that depend on data or environment (missing behavior files, mounted
 *    assets, navigation readiness, saved orchestration, arena rosters) return
 *    false with the TS message in qa_source_bot_director_error.
 *  - OwnedActor identity (TS Map keys and ===) is id, owner and token equality.
 *  - Arrays returned by frame, roster and the source pickup goals are
 *    director-owned and valid until the next call of the same function.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_DIRECTOR_H
#define QA_BOTS_BEHAVIOR_DIRECTOR_H
#include "qa/bots/behavior/library/bsp_entities.h"
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/orders.h"
#include "qa/bots/behavior/q3/ai_main.h"
#include "qa/bots/behavior/q3/catalog.h"
#include "qa/bots/behavior/q3/game_host.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/core/common_parse.h"
#include "qa/foundation/event_ring.h"
#include "qa/persistence/value.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* TS Pick<ActorCommand, "command" | "arsenal">. */
typedef struct qa_source_bot_encoded_command {
  qa_user_command command;
  bool has_arsenal;
  qa_arsenal_intent arsenal;
} qa_source_bot_encoded_command;

/* TS SourceBotDirectorHost. Every member receives ctx. */
typedef struct qa_source_bot_director_host {
  void *ctx;
  qa_source_bot_game_bots_a *game;
  qa_provider_id provider;
  /* Allocate the real session player and source client binding, without starting movement. */
  int64_t (*allocate_client)(void *ctx);
  /* false is TS null */
  bool (*actor)(void *ctx, int64_t client, qa_owned_actor *out);
  void (*encode_command)(void *ctx, int64_t client, const qa_q3_base_user_command *command,
                         qa_source_bot_encoded_command *out);
  int64_t (*snapshot_entity)(void *ctx, int64_t client, int64_t sequence);
  /* NULL is TS null; borrowed until the next call */
  const char *(*console_message)(void *ctx, int64_t client);
  int64_t (*point_contents)(void *ctx, qa_vec3 point);
} qa_source_bot_director_host;

/* TS BotNavigation & GoalNavigation. */
typedef struct qa_source_bot_navigation {
  qa_bot_navigation_bots_a bot;
  qa_goal_navigation goal;
} qa_source_bot_navigation;
/* TS options.navigation(library, bspEntities); bsp_entities is the director's,
 * borrowed for the round. */
typedef void (*qa_source_bot_navigation_fn)(void *ctx, qa_bot_library_bots_a *library,
                                            qa_aas_bsp_entities *bsp_entities,
                                            qa_source_bot_navigation *out);

/* TS BotSourceFiles: read/list as the catalog takes them; provenance NULL is
 * the absent member and receives files.ctx. legacy_provenance (TS
 * legacyProvenance?, the digest version 1 bot saves recorded) likewise. */
typedef struct qa_source_bot_files {
  qa_bot_catalog_files_bots_a files;
  const char *(*provenance)(void *ctx);
  const char *(*legacy_provenance)(void *ctx);
} qa_source_bot_files;

typedef struct qa_source_bot_director_options {
  const qa_source_bot_director_host *host;
  qa_source_bot_library_options_bots_a library;
  qa_source_bot_files files;
  const char *entities;
  bool restoring;
  qa_source_bot_navigation_fn navigation;
  void *navigation_ctx;
} qa_source_bot_director_options;

typedef struct qa_source_bot_roster_entry {
  qa_owned_actor actor;
  int64_t source_client;
  qa_bot_settings_bots_a settings;
  qa_bot_state_bots_a *state;
} qa_source_bot_roster_entry;

typedef struct qa_source_bot_arena_entry {
  char *name, *info;
} qa_source_bot_arena_entry;

/* One round: AI, catalog and navigation have their own lifetime. */
typedef struct qa_source_bot_round {
  struct qa_source_bot_director *director;
  const qa_source_bot_director_host *host;
  qa_source_bot_navigation navigation;
  qa_game_ai_host_bots_a ai_host;
  qa_game_ai_bots_a ai;
  qa_common_parse_state parser;
  qa_game_bot_catalog_bots_a catalog;
} qa_source_bot_round;

typedef enum qa_source_bot_round_phase {
  QA_SOURCE_BOT_ROUND_ACTIVE,
  QA_SOURCE_BOT_ROUND_DETACHED,
  QA_SOURCE_BOT_ROUND_BOUND
} qa_source_bot_round_phase;

typedef struct qa_source_bot_sequence {
  qa_owned_actor actor;
  int64_t next;
} qa_source_bot_sequence;

/* Ring record: ActorCommand with its arsenal held by value. */
typedef struct qa_source_bot_generated_command {
  qa_actor_id actor;
  qa_provider_id provider;
  double sequence;
  qa_source_bot_encoded_command encoded;
} qa_source_bot_generated_command;

typedef struct qa_source_bot_director {
  qa_bot_library_bots_a library;
  qa_aas_bsp_entities bsp_entities;
  qa_source_bot_director_options options;
  qa_source_bot_round *round;
  qa_source_bot_round_phase round_phase;
  qa_source_bot_sequence *sequences; /* TS Map insertion order */
  size_t sequence_count, sequence_capacity;
  qa_event_ring generated;
  qa_event_cursor generated_cursor;
  bool loaded, closed, running;
  qa_source_bot_generated_command *taken;
  qa_actor_command *commands;
  size_t command_count;
  qa_source_bot_roster_entry *roster;
  size_t roster_count, roster_capacity;
  qa_source_pickup_goal *pickup_goals;
  size_t pickup_goal_capacity;
  char error[512];
} qa_source_bot_director;

/* TS constructor. NULL only on OOM. */
qa_source_bot_director *qa_source_bot_director_create(const qa_source_bot_director_options *options);
/* Releases C storage; close() first for the TS shutdown. */
void qa_source_bot_director_destroy(qa_source_bot_director *d);
const char *qa_source_bot_director_error(const qa_source_bot_director *d);

/* TS getters. */
qa_source_bot_navigation *qa_source_bot_director_navigation(qa_source_bot_director *d);
qa_game_ai_bots_a *qa_source_bot_director_ai(qa_source_bot_director *d);
qa_game_bot_catalog_bots_a *qa_source_bot_director_catalog(qa_source_bot_director *d);
const qa_source_bot_director_options *qa_source_bot_director_options_of(const qa_source_bot_director *d);

void qa_source_bot_director_begin_round_restart(qa_source_bot_director *d);
bool qa_source_bot_director_bind_restarted_round(qa_source_bot_director *d,
                                                 const qa_source_bot_director_host *host,
                                                 qa_source_bot_navigation_fn navigation,
                                                 void *navigation_ctx);
void qa_source_bot_director_resume_round_bots(qa_source_bot_director *d);

/* TS captureSaveState: owned tree, NULL when asset provenance is missing. */
qa_save_value *qa_source_bot_director_capture_save_state(qa_source_bot_director *d);
typedef bool (*qa_source_bot_resolve_owned_fn)(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out);
typedef struct qa_source_bot_restore_hooks {
  qa_goal_restore_actor_fn resolve_actor;
  void *resolve_actor_ctx;
  qa_source_bot_resolve_owned_fn resolve_owned; /* false is TS null */
  void *resolve_owned_ctx;
  qa_bot_move_edge_fn edge;
  void *edge_ctx;
  qa_bot_remap_observation_fn remap_observation;
  void *remap_observation_ctx;
} qa_source_bot_restore_hooks;
bool qa_source_bot_director_restore_save_state(qa_source_bot_director *d, const qa_save_value *value,
                                               const qa_source_bot_restore_hooks *hooks);

bool qa_source_bot_director_load(qa_source_bot_director *d, bool restart);
/* Called once by the session's source frame phase; it never runs actor physics itself. */
const qa_actor_command *qa_source_bot_director_frame(qa_source_bot_director *d, double milliseconds,
                                                     size_t *out_count);
qa_save_value *qa_source_bot_director_checkpoint_orchestration(qa_source_bot_director *d);
/* Call after restoring the child owners and transport; this never initializes them. */
bool qa_source_bot_director_restore_orchestration(qa_source_bot_director *d, const qa_save_value *value,
                                                  qa_source_bot_resolve_owned_fn resolve, void *resolve_ctx);
bool qa_source_bot_director_connect(qa_source_bot_director *d, int64_t client, bool restart);
void qa_source_bot_director_shutdown_client(qa_source_bot_director *d, int64_t client, bool restart);
void qa_source_bot_director_remove_queued_begin(qa_source_bot_director *d, int64_t client);
void qa_source_bot_director_console_command(qa_source_bot_director *d, const char *const *argv,
                                            size_t argc);
bool qa_source_bot_director_add_match_bot(qa_source_bot_director *d);
void qa_source_bot_director_test_aas(qa_source_bot_director *d, qa_vec3 origin);
void qa_source_bot_director_interbreed_end_match(qa_source_bot_director *d);
const qa_source_bot_roster_entry *qa_source_bot_director_roster(qa_source_bot_director *d,
                                                                size_t *out_count);
qa_bot_goal_status qa_source_bot_director_request_move_to_point(qa_source_bot_director *d,
                                                                int64_t client, qa_vec3 point);
qa_bot_goal_status qa_source_bot_director_request_follow_entity(qa_source_bot_director *d,
                                                                int64_t client, int64_t number);
void qa_source_bot_director_clear_goal(qa_source_bot_director *d, int64_t client);
qa_bot_goal_status qa_source_bot_director_goal_status(qa_source_bot_director *d, int64_t client);
/* TS arenaRoster: malloc'd entries (free with qa_source_bot_arena_roster_free);
 * false with the TS error when the arena names an undefined bot. */
bool qa_source_bot_director_arena_roster(qa_source_bot_director *d, const char *map,
                                         qa_source_bot_arena_entry **out, size_t *out_count);
void qa_source_bot_arena_roster_free(qa_source_bot_arena_entry *entries, size_t count);
void qa_source_bot_director_close(qa_source_bot_director *d, bool restart);
#endif
