/* Port of src/app/bootstrap/q2-match-reports.ts. SharedSimulation
 * (simulation/runtime.ts, other slice) is the host view below.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q2_MATCH_REPORTS_H
#define QA_APP_BOOTSTRAP_Q2_MATCH_REPORTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/player_progress.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/protocol.h"
#include "qa/core/cvars/index.h"
/* TS NativeQ2MapTransition. */
typedef struct qa_app_native_q2_map_transition {
  const char *kind; /* "game-module-map-transition" */
  qa_module_identity module;
} qa_app_native_q2_map_transition;
/* Score or false (TS null). Stats are the player view's stats. */
bool qa_app_classic_match_score(const qa_q2_player_state *state, double *out);
bool qa_app_rerelease_match_score(const qa_q2_rerelease_player_state *state, double *out);
typedef struct qa_app_q2_match_simulation {
  void *ctx;
  const char *mode; /* options.mode */
  const char *map_path; /* recipe.map.geometry.requestedPath */
  bool (*has_q2_source)(void *ctx);
  bool (*has_q2_native)(void *ctx);
  qa_cvar_registry *(*q2_server_cvars)(void *ctx); /* NULL = TS null */
  /* Source players intermission: kind + started (double as text id). */
  const char *(*intermission_kind)(void *ctx);
  double (*intermission_started)(void *ctx);
  bool (*source_player)(void *ctx, qa_actor_id actor, bool *spectator_out, double *score_out);
  /* Native world. */
  const char *(*native_edition)(void *ctx);
  qa_module_identity (*native_module)(void *ctx);
  size_t (*native_players)(void *ctx, const double **slots_out);
  bool (*player_client_slot)(void *ctx, qa_actor_id actor, double *slot_out);
  const qa_q2_player_state *(*classic_player_state)(void *ctx, double entity_number);
  const qa_q2_rerelease_player_state *(*rerelease_player_state)(void *ctx, double entity_number);
  /* randomUUID source. */
  const char *(*random_uuid)(void *ctx);
} qa_app_q2_match_simulation;
/* Aborts when prepared without source cvars (TS Error). */
void qa_app_prepare_q2_match_reports(const qa_app_q2_match_simulation *simulation, bool restoring);
/* transition NULL = TS undefined. Aborts on a foreign transition. */
bool qa_app_q2_match_completed(const qa_app_q2_match_simulation *simulation,
    const qa_app_native_q2_map_transition *transition);
typedef struct qa_app_q2_match_local { qa_actor_id actor; qa_seat_id seat; } qa_app_q2_match_local;
/* Owned event rows (strings owned by the result scratch). */
typedef struct qa_app_q2_match_report_rows {
  qa_app_player_progress_event *items; size_t count;
  char **strings; size_t string_count;
} qa_app_q2_match_report_rows;
void qa_app_q2_match_report_rows_destroy(qa_app_q2_match_report_rows *rows);
qa_app_q2_match_report_rows qa_app_q2_match_reports(const qa_app_q2_match_simulation *simulation,
    const qa_app_q2_match_local *locals, size_t local_count,
    const qa_app_native_q2_map_transition *transition);
#endif
