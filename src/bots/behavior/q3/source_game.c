/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/source_game.h"
#include <string.h>
qa_source_bot_game_bots_a qa_q3_bot_game_bots_a(qa_q3_source_runtime_bots_a src,
                                                void (*insert)(void *ctx, const char *text),
                                                void *insert_ctx) {
  (void)insert;
  (void)insert_ctx;
  qa_source_bot_game_bots_a g;
  memset(&g, 0, sizeof g);
  g.ctx = src.ctx;
  g.entity_count = src.entity_count;
  g.max_clients = src.max_clients;
  g.game_type = src.game_type;
  g.get_entity = src.get_entity;
  g.model_index = src.model_index;
  g.choose_team = src.choose_team;
  g.activate_bot = src.activate_bot;
  g.exit_level = src.exit_level;
  g.reset_podium = src.reset_podium;
  g.client_userinfo_changed = src.userinfo_changed;
  g.client_connect = src.connect;
  g.client_begin = src.begin;
  return g;
}
