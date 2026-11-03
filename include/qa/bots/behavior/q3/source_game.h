/* Port of src/bots/behavior/q3/source-game.ts (q3BotGame): the SourceBotGame
 * view over a native Q3 source runtime. Reads detach source state; every
 * mutation is an explicit call to its existing owner.
 *
 * C accommodations:
 *  - TS Q3SourceRuntime is the C subsystem owner (pool, records, world,
 *    level, random, memory, config, missiles, match, arenas, admission, item
 *    lifecycle) plus the runtime host (cvars, configstrings, engine), which
 *    qa_app_q3_source_runtime_host returns. Both are borrowed and must outlive
 *    the game.
 *  - The returned closure object is qa_q3_bot_game_bots_a; `game` is the
 *    SourceBotGame table, whose ctx is the struct itself, so the struct must
 *    not move after init. Engine members other than insert_console_command
 *    forward to host->engine with the host's own ctx; insert_console_command
 *    receives the caller's insert_ctx.
 *  - entity(): the observed player state is a C-owned cell overwritten by the
 *    next entity() call (TS detaches a fresh copy per call).
 *  - The arsenal binding is the subset of TS BotArsenalBinding q3BotGame
 *    reads. Its knowledge leaves receive knowledge->ctx; source_weapon and
 *    source_weapon_state receive ctx.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_SOURCE_GAME_H
#define QA_BOTS_BEHAVIOR_Q3_SOURCE_GAME_H
#include "qa/bots/behavior/q3/arsenal_knowledge.h"
#include "qa/bots/behavior/q3/game_host.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct qa_app_q3_source_subsystems_owner;
struct qa_app_q3_source_host;

typedef struct qa_q3_bot_arsenal_binding_bots_a {
  const qa_bot_arsenal_interface_bots_a *knowledge;
  void *ctx;
  int64_t (*source_weapon)(void *ctx, int64_t client);
  int64_t (*source_weapon_state)(void *ctx, int64_t client);
} qa_q3_bot_arsenal_binding_bots_a;

/* One candidate owns copies of its preview receipt arrays. */
typedef struct qa_q3_bot_pickup_cell_bots_a {
  qa_bot_observed_pickup_bots_a pickup;
  qa_pickup_ammo_receipt *ammo, *weapons;
} qa_q3_bot_pickup_cell_bots_a;

typedef struct qa_q3_bot_game_bots_a {
  qa_source_bot_game_bots_a game;
  struct qa_app_q3_source_subsystems_owner *source;
  const struct qa_app_q3_source_host *host;
  void (*insert_console_command)(void *ctx, const char *text);
  void *insert_ctx;
  const qa_q3_bot_arsenal_binding_bots_a *arsenal; /* NULL is TS undefined */
  qa_q3_config_string_store configstrings;
  qa_bot_pickup_observations_bots_a pickups;
  qa_q3_bot_pickup_cell_bots_a *candidates; /* borrowed by callers until the next candidates() */
  qa_bot_observed_pickup_bots_a *candidate_view;
  size_t candidate_count, candidate_capacity;
  qa_q3_bot_pickup_cell_bots_a inspected; /* inspect() result storage */
  qa_bot_arsenal_knowledge_bots_a source_knowledge; /* TS createBotArsenalKnowledge (no arsenal) */
  qa_bot_arsenal_interface_bots_a arsenal_knowledge; /* TS { ...arsenal.knowledge, updateInventory } */
  bool has_player_state;
  qa_q3_base_player_state player_state;
  char *userinfo; /* last host get_userinfo result */
} qa_q3_bot_game_bots_a;

/* TS q3BotGame(source, insertConsoleCommand, arsenal?). arsenal NULL is the
 * omitted argument. False only on OOM. */
bool qa_q3_bot_game_bots_a_init(qa_q3_bot_game_bots_a *g, struct qa_app_q3_source_subsystems_owner *source,
                                const struct qa_app_q3_source_host *host,
                                void (*insert_console_command)(void *ctx, const char *text),
                                void *insert_ctx, const qa_q3_bot_arsenal_binding_bots_a *arsenal);
void qa_q3_bot_game_bots_a_destroy(qa_q3_bot_game_bots_a *g);
#endif
