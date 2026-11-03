/* Port of src/app/bootstrap/base-arena-postgame.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/base_arena_postgame.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/game_numeric.h"
#include "qa/foundation/js.h"
static double number_at(const char *const *args, size_t count, size_t index) {
  const char *text = index < count && args[index] ? args[index] : "";
  /* TS splits on NUL (C string end) and slices 1023 chars. */
  char buf[1024];
  size_t len = strlen(text);
  if (len > 1023) len = 1023;
  memcpy(buf, text, len);
  buf[len] = '\0';
  return qa_game_atoi(buf);
}
qa_app_arena_postgame qa_app_parse_arena_postgame(const char *const *args, size_t arg_count, double level, double skill) {
  double count = qa_js_min(8, qa_js_max(0, number_at(args, arg_count, 0)));
  double player_client = number_at(args, arg_count, 1);
  qa_app_arena_postgame game = {0};
  game.player_client = player_client;
  game.players = calloc(count > 0 ? (size_t)count : 1, sizeof *game.players);
  double rank = 8;
  for (double n = 0; n < count; n++) {
    size_t i = (size_t)n;
    double client = number_at(args, arg_count, 8 + i * 3);
    double player_rank = (double)(int32_t)((qa_js_to_int32(number_at(args, arg_count, 9 + i * 3)) & ~64) + 1);
    game.players[game.player_count++] = (qa_app_arena_podium_player){client, player_rank, number_at(args, arg_count, 10 + i * 3)};
    if (client == player_client) rank = player_rank;
  }
  game.result = (qa_app_arena_result){level, skill, rank, number_at(args, arg_count, 2), number_at(args, arg_count, 3),
      number_at(args, arg_count, 4), number_at(args, arg_count, 5), number_at(args, arg_count, 6), number_at(args, arg_count, 7) != 0};
  return game;
}
static const char *const controls[] = {"retry", "next", "main"};
qa_app_arena_postgame_presentation qa_app_arena_postgame_presentation_make(const qa_app_arena_postgame *game, qa_app_arena_progression_result result) {
  qa_app_arena_postgame_presentation out;
  out.result = result;
  out.podium = game->players;
  out.podium_count = game->player_count < 3 ? game->player_count : 3;
  out.music_command = result.rank == 1 ? "music music/win" : "music music/loss";
  out.winner_announcement_after_milliseconds = 1000;
  out.controls = controls;
  out.control_count = 3;
  return out;
}
