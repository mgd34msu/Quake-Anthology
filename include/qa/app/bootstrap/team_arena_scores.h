/* Port of src/app/bootstrap/team-arena-scores.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_TEAM_ARENA_SCORES_H
#define QA_APP_BOOTSTRAP_TEAM_ARENA_SCORES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_app_team_arena_postgame {
  double accuracy, impressives, excellents, defends, assists, gauntlets;
  double base_score, perfects, red_score, blue_score, end_time, captures;
} qa_app_team_arena_postgame;
typedef struct qa_app_team_arena_score {
  double accuracy, impressives, excellents, defends, assists, gauntlets;
  double base_score, perfects, red_score, blue_score, captures;
  double score, time, time_bonus, shutout_bonus, skill_bonus;
} qa_app_team_arena_score;
typedef struct qa_app_team_arena_score_input {
  qa_app_team_arena_postgame stats; double match_start_time, skill, time_to_beat;
} qa_app_team_arena_score_input;
typedef struct qa_app_team_arena_score_result {
  qa_app_team_arena_score score, previous; bool new_high_score, new_best_time, won;
} qa_app_team_arena_score_result;
/* TS TeamArenaScoreFiles async -> synchronous fn+ctx. read returns malloc'd bytes or NULL. */
typedef struct qa_app_team_arena_score_files {
  void *ctx;
  uint8_t *(*read_file)(void *ctx, const char *path, size_t *out_length);
  void (*write_file)(void *ctx, const char *path, const uint8_t *bytes, size_t length);
} qa_app_team_arena_score_files;
qa_app_team_arena_postgame qa_app_parse_team_arena_postgame(const char *const *args, size_t count);
qa_app_team_arena_score_result qa_app_calculate_team_arena_score(qa_app_team_arena_score_input input, qa_app_team_arena_score previous);
char *qa_app_team_arena_score_path(const char *map, double game_type); /* malloc'd */
void qa_app_encode_team_arena_score(qa_app_team_arena_score info, uint8_t out[68]);
qa_app_team_arena_score qa_app_decode_team_arena_score(const uint8_t *bytes, size_t length); /* bytes NULL = TS null */
qa_app_team_arena_score_result qa_app_record_team_arena_score(qa_app_team_arena_score_input input, const char *map, double game_type, qa_app_team_arena_score_files files);
#endif
