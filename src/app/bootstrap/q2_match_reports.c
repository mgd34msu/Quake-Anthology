/* Port of src/app/bootstrap/q2-match-reports.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_match_reports.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/q2/state.h"
static const char round_identity[] = "qts_matchRoundIdentity";
static double read_stat(qa_double_view stats, int64_t index) {
  return qa_q2_read_element_f64(stats.values, stats.count, index);
}
bool qa_app_classic_match_score(const qa_q2_player_state *state, double *out) {
  if (read_stat(state->view.stats, 17) != 0) return false;
  *out = read_stat(state->view.stats, 14);
  return true;
}
bool qa_app_rerelease_match_score(const qa_q2_rerelease_player_state *state, double *out) {
  if ((((int64_t)read_stat(state->view.stats, 13)) & 8) == 0 || read_stat(state->view.stats, 17) != 0) return false;
  *out = read_stat(state->view.stats, 14);
  return true;
}
void qa_app_prepare_q2_match_reports(const qa_app_q2_match_simulation *simulation, bool restoring) {
  if ((!simulation->has_q2_source(simulation->ctx) && !simulation->has_q2_native(simulation->ctx))
      || strcmp(simulation->mode, "deathmatch") != 0) return;
  qa_cvar_registry *cvars = simulation->q2_server_cvars(simulation->ctx);
  if (cvars == NULL) qa_range_error("Q2 match reporting has no source cvars");
  qa_cvar_snapshot snap;
  /* Q2CvarFlag.ReadOnly. */
  if (qa_cvar_registry_register(cvars, round_identity, "", QA_Q2_CVAR_FLAG_READ_ONLY, &snap)) qa_cvar_read_free(&snap);
  char *current = qa_cvar_registry_variable_string(cvars, round_identity);
  bool empty = current == NULL || current[0] == '\0';
  free(current);
  if (!restoring || empty) {
    const char *uuid = simulation->random_uuid(simulation->ctx);
    if (qa_cvar_registry_set(cvars, round_identity, uuid, true, &snap)) qa_cvar_read_free(&snap);
  }
}
bool qa_app_q2_match_completed(const qa_app_q2_match_simulation *simulation,
    const qa_app_native_q2_map_transition *transition) {
  if (strcmp(simulation->mode, "deathmatch") != 0) return false;
  if (simulation->has_q2_source(simulation->ctx))
    return strcmp(simulation->intermission_kind(simulation->ctx), "playing") != 0;
  bool native = simulation->has_q2_native(simulation->ctx);
  if (transition != NULL && native) {
    qa_module_identity module = simulation->native_module(simulation->ctx);
    const qa_module_identity *producer = &transition->module;
    if (strcmp(module.id, producer->id) != 0 || strcmp(module.digest, producer->digest) != 0
        || strcmp(module.revision, producer->revision) != 0
        || strcmp(module.artifact_path, producer->artifact_path) != 0)
      qa_range_error("Native match transition belongs to another module");
    return true;
  }
  if (!native || strcmp(simulation->native_edition(simulation->ctx), "rerelease") != 0) return false;
  const double *slots = NULL;
  size_t count = simulation->native_players(simulation->ctx, &slots);
  for (size_t i = 0; i < count; i++) {
    const qa_q2_rerelease_player_state *state =
        simulation->rerelease_player_state(simulation->ctx, slots[i] + 1);
    if (state != NULL && (((int64_t)read_stat(state->view.stats, 13)) & 8) != 0) return true;
  }
  return false;
}
static char *track(qa_app_q2_match_report_rows *rows, char *text) {
  char **next = realloc(rows->strings, (rows->string_count + 1) * sizeof *next);
  if (next == NULL) qa_range_error("Out of memory");
  rows->strings = next;
  rows->strings[rows->string_count++] = text;
  return text;
}
static char *format_text(qa_app_q2_match_report_rows *rows, const char *format, const char *a) {
  size_t size = strlen(format) + strlen(a) + 64;
  char *text = malloc(size);
  if (text == NULL) qa_range_error("Out of memory");
  snprintf(text, size, format, a);
  return track(rows, text);
}
void qa_app_q2_match_report_rows_destroy(qa_app_q2_match_report_rows *rows) {
  if (rows == NULL) return;
  free(rows->items);
  for (size_t i = 0; i < rows->string_count; i++) free(rows->strings[i]);
  free(rows->strings);
  memset(rows, 0, sizeof *rows);
}
qa_app_q2_match_report_rows qa_app_q2_match_reports(const qa_app_q2_match_simulation *simulation,
    const qa_app_q2_match_local *locals, size_t local_count,
    const qa_app_native_q2_map_transition *transition) {
  qa_app_q2_match_report_rows rows = {0};
  if (!qa_app_q2_match_completed(simulation, transition)) return rows;
  qa_cvar_registry *cvars = simulation->q2_server_cvars(simulation->ctx);
  char *identity = cvars != NULL ? qa_cvar_registry_variable_string(cvars, round_identity) : NULL;
  if (identity == NULL || identity[0] == '\0') {
    free(identity);
    qa_range_error("Q2 match reporting was not prepared");
  }
  const char *map = simulation->map_path;
  rows.items = calloc(local_count == 0 ? 1 : local_count, sizeof *rows.items);
  if (rows.items == NULL) qa_range_error("Out of memory");
  bool source_reports = simulation->has_q2_source(simulation->ctx)
      && strcmp(simulation->intermission_kind(simulation->ctx), "playing") != 0;
  if (source_reports) {
    double started = simulation->intermission_started(simulation->ctx);
    for (size_t i = 0; i < local_count; i++) {
      bool spectator = false;
      double score = 0;
      if (!simulation->source_player(simulation->ctx, locals[i].actor, &spectator, &score) || spectator) continue;
      qa_app_player_progress_event *event = &rows.items[rows.count++];
      event->kind = "match-completed"; event->source = "q2";
      char participant[64];
      snprintf(participant, sizeof participant, "local-seat:%lld", (long long)locals[i].seat.index);
      event->participant = format_text(&rows, "%s", participant);
      char event_text[512];
      if (started == (double)(long long)started)
        snprintf(event_text, sizeof event_text, "match:%s:%lld", identity, (long long)started);
      else snprintf(event_text, sizeof event_text, "match:%s:%g", identity, started);
      event->event = format_text(&rows, "%s", event_text);
      event->map = map;
      event->score = score;
    }
    free(identity);
    return rows;
  }
  if (!simulation->has_q2_native(simulation->ctx)) { free(identity); return rows; }
  bool classic = strcmp(simulation->native_edition(simulation->ctx), "classic") == 0 || transition != NULL;
  for (size_t i = 0; i < local_count; i++) {
    double slot = 0;
    if (!simulation->player_client_slot(simulation->ctx, locals[i].actor, &slot)) continue;
    double score = 0;
    bool has_score;
    if (classic) {
      const qa_q2_player_state *state = simulation->classic_player_state(simulation->ctx, slot + 1);
      has_score = state != NULL && qa_app_classic_match_score(state, &score);
    } else {
      const qa_q2_rerelease_player_state *state = simulation->rerelease_player_state(simulation->ctx, slot + 1);
      has_score = state != NULL && qa_app_rerelease_match_score(state, &score);
    }
    if (!has_score) continue;
    qa_app_player_progress_event *event = &rows.items[rows.count++];
    event->kind = "match-completed"; event->source = "q2";
    char participant[64];
    snprintf(participant, sizeof participant, "local-seat:%lld", (long long)locals[i].seat.index);
    event->participant = format_text(&rows, "%s", participant);
    event->event = format_text(&rows, "match:%s", identity);
    event->map = map;
    event->score = score;
  }
  free(identity);
  return rows;
}
