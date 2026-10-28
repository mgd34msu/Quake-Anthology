/* Port of src/app/bootstrap/team-arena-results.ts over the landed
 * native UI controller. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_TEAM_ARENA_RESULTS_H
#define QA_APP_BOOTSTRAP_TEAM_ARENA_RESULTS_H
#include <stdbool.h>
#include "qa/ui/common/controller.h"
typedef struct qa_app_team_arena_result {
  const char *title; bool won; double score, opponent, points; const char *time;
} qa_app_team_arena_result;
typedef struct qa_app_team_arena_result_service {
  void *ctx;
  bool (*read)(void *ctx, qa_app_team_arena_result *out); /* false = TS null */
  bool has_demo;
  bool (*demo_available)(void *ctx);
  void (*demo_play)(void *ctx);
  void (*next)(void *ctx);
  void (*retry)(void *ctx);
  void (*quit)(void *ctx);
} qa_app_team_arena_result_service;
typedef struct qa_app_team_arena_results qa_app_team_arena_results;
qa_app_team_arena_results *qa_app_team_arena_results_create(qa_native_ui_controller *controller, qa_app_team_arena_result_service service);
void qa_app_team_arena_results_destroy(qa_app_team_arena_results *r);
bool qa_app_team_arena_results_active(const qa_app_team_arena_results *r);
void qa_app_team_arena_results_update(qa_app_team_arena_results *r);
void qa_app_team_arena_results_close(qa_app_team_arena_results *r);
#endif
