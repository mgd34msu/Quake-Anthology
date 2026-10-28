/* Port of src/app/bootstrap/base-arena-postgame.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_BASE_ARENA_POSTGAME_H
#define QA_APP_BOOTSTRAP_BASE_ARENA_POSTGAME_H
#include <stddef.h>
#include "qa/app/bootstrap/base_arena_progression.h"
typedef struct qa_app_arena_podium_player { double client, rank, score; } qa_app_arena_podium_player;
typedef struct qa_app_arena_postgame {
  qa_app_arena_result result;
  qa_app_arena_podium_player *players; size_t player_count; /* owned */
  double player_client;
} qa_app_arena_postgame;
typedef struct qa_app_arena_postgame_presentation {
  qa_app_arena_progression_result result; /* borrowed copy kept by caller */
  const qa_app_arena_podium_player *podium; size_t podium_count;
  const char *music_command; /* static */
  double winner_announcement_after_milliseconds; /* 1000 */
  const char *const *controls; size_t control_count; /* static */
} qa_app_arena_postgame_presentation;
/* Args exclude spPostgame. Result players owned; free with free(). */
qa_app_arena_postgame qa_app_parse_arena_postgame(const char *const *args, size_t arg_count, double level, double skill);
qa_app_arena_postgame_presentation qa_app_arena_postgame_presentation_make(const qa_app_arena_postgame *game, qa_app_arena_progression_result result);
#endif
