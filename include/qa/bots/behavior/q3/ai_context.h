/* Port of src/bots/behavior/q3/ai-context.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_CONTEXT_H
#define QA_BOTS_BEHAVIOR_Q3_AI_CONTEXT_H
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/bots/behavior/q3/game_host.h"
#include "qa/bots/behavior/q3/observations.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_bot_cvar_bots_a {
  char name[64];
  char value[256];
  double numeric_value;
  int64_t integer_value, modification_count;
  bool registered;
} qa_bot_cvar_bots_a;
void qa_bot_cvar_register_bots_a(qa_bot_cvar_bots_a *c, const char *name, const char *def,
                                 const char *(*lookup)(void *ctx, const char *name),
                                 void *lookup_ctx);
void qa_bot_cvar_update_bots_a(qa_bot_cvar_bots_a *c,
                               const char *(*lookup)(void *ctx, const char *name),
                               void *lookup_ctx);
void qa_bot_cvar_write_integer(qa_bot_cvar_bots_a *c, int64_t value);
typedef struct qa_game_ai_context_bots_a {
  qa_bot_entity_observations observations;
  qa_bot_state_store_bots_a states;
  char *node_switches[50];
  size_t node_switch_count;
  double time, regular_update_time;
  int64_t num_bots;
  bool interbreed;
  int64_t interbreed_match_count;
  int64_t gametype, maxclients;
  qa_source_bot_game_bots_a game;
  bool has_game;
  void *host_ctx;
  void (*print)(void *ctx, const char *text);
  void (*say)(void *ctx, int64_t client, const char *text);
  void (*say_team)(void *ctx, int64_t client, const char *text);
  void (*console_command)(void *ctx, const char *text);
  int64_t (*cvar_int)(void *ctx, const char *name);
  double (*random_unit)(void *ctx);
  double (*crandom)(void *ctx);
  double (*character_float)(void *ctx, int64_t character, int64_t index);
  double (*character_bounded)(void *ctx, int64_t character, int64_t index, double minv,
                              double maxv);
  void (*reset_state_hook)(void *ctx, qa_bot_state_bots_a *state);
} qa_game_ai_context_bots_a;
bool qa_game_ai_context_bots_a_init(qa_game_ai_context_bots_a *c, int64_t max_clients);
void qa_game_ai_context_bots_a_destroy(qa_game_ai_context_bots_a *c);
double qa_game_ai_context_random(qa_game_ai_context_bots_a *c);
void qa_game_ai_context_reset_state(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *state);
void qa_bot_reset_state_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *state);
#endif
