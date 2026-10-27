/* Port of src/bots/behavior/q3/source-game.ts. Q3SourceRuntime (owner:
 * app/bootstrap, other slices) is mirrored as host fn+ctx filling the
 * game-host vtable. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_SOURCE_GAME_H
#define QA_BOTS_BEHAVIOR_Q3_SOURCE_GAME_H
#include "qa/bots/behavior/q3/game_host.h"
typedef struct qa_q3_source_runtime_bots_a {
  void *ctx;
  qa_bot_observed_entity_bots_a (*get_entity)(void *ctx, int64_t number);
  int64_t entity_count, max_clients, game_type;
  int64_t (*model_index)(void *ctx, const char *name);
  int64_t (*choose_team)(void *ctx, int64_t client);
  void (*activate_bot)(void *ctx, int64_t client);
  void (*exit_level)(void *ctx);
  void (*reset_podium)(void *ctx);
  void (*userinfo_changed)(void *ctx, int64_t client);
  const char *(*connect)(void *ctx, int64_t client, bool first, bool is_bot);
  void (*begin)(void *ctx, int64_t client);
} qa_q3_source_runtime_bots_a;
qa_source_bot_game_bots_a qa_q3_bot_game_bots_a(qa_q3_source_runtime_bots_a source,
                                                void (*insert_console_command)(void *ctx,
                                                                               const char *text),
                                                void *insert_ctx);
#endif
