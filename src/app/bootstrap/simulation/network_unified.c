/* Port of src/app/bootstrap/simulation/network-unified.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_unified.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static char *xstrdup(const char *s) { size_t n = strlen(s); char *o = malloc(n + 1); if (!o) qa_range_error("Out of memory"); memcpy(o, s, n + 1); return o; }
char *qa_app_sim_unified_wire_model_path(const char *family, const char *path) {
  char *out = malloc(strlen(family) + strlen(path) + 64);
  if (!out) qa_range_error("Out of memory");
  sprintf(out, "unified:%s/%s", family, path);
  return out;
}
char *qa_app_sim_unified_source_model_path(const char *family, const char *path) {
  char prefix[256];
  snprintf(prefix, sizeof prefix, "unified:%s/", family);
  if (strncmp(path, prefix, strlen(prefix)) == 0) return xstrdup(path + strlen(prefix));
  return xstrdup(path);
}
static void append(char **buf, size_t *len, size_t *cap, const char *text) {
  size_t n = strlen(text);
  if (*len + n + 1 > *cap) { *cap = (*len + n + 1) * 2; *buf = realloc(*buf, *cap); if (!*buf) qa_range_error("Out of memory"); }
  memcpy(*buf + *len, text, n + 1); *len += n;
}
static void append_number(char **buf, size_t *len, size_t *cap, double value) {
  char text[32];
  /* JS String(number) for integers used here. */
  if (value == (double)(long long)value) snprintf(text, sizeof text, "%lld", (long long)value);
  else snprintf(text, sizeof text, "%.17g", value);
  append(buf, len, cap, text);
}
static char *presentation_content(qa_app_simulation_presentation p) {
  const char *family = p.family == QA_GAME_FAMILY_Q1 ? "q1" : p.family == QA_GAME_FAMILY_Q2 ? "q2" : "q3";
  char *out = malloc(64); size_t len = 0, cap = 64; out[0] = '\0';
  append(&out, &len, &cap, "unified:model/"); append(&out, &len, &cap, family); append(&out, &len, &cap, "/");
  /* encodeURIComponent-ish: keep alphanumerics and -_.!~*'(), %XX rest. */
  for (const unsigned char *s = (const unsigned char *)p.path; *s; s++) {
    char c = (char)*s;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || strchr("-_.!~*'()", c)) { char t[2] = {c, 0}; append(&out, &len, &cap, t); }
    else { char t[4]; snprintf(t, sizeof t, "%%%02X", *s); append(&out, &len, &cap, t); }
  }
  append(&out, &len, &cap, "?frame="); append_number(&out, &len, &cap, p.frame);
  append(&out, &len, &cap, "&old="); append_number(&out, &len, &cap, p.old_frame);
  if (p.has_back_lerp) { append(&out, &len, &cap, "&lerp="); append_number(&out, &len, &cap, p.back_lerp); }
  append(&out, &len, &cap, "&skin="); append_number(&out, &len, &cap, p.skin);
  append(&out, &len, &cap, "&effects="); append_number(&out, &len, &cap, p.effects);
  append(&out, &len, &cap, "&flags="); append_number(&out, &len, &cap, p.render_flags);
  append(&out, &len, &cap, "&scale="); append_number(&out, &len, &cap, p.scale);
  if (p.has_alpha) { append(&out, &len, &cap, "&alpha="); append_number(&out, &len, &cap, p.alpha); }
  return out;
}
struct qa_app_sim_unified_presentation {
  qa_app_simulation_presentation *models; size_t count;
  char **content_ids, **paths;
  bool has_view; qa_app_sim_unified_player_view view;
};
static qa_app_sim_unified_presentation *build(qa_app_sim_unified_snapshot snap,
    const qa_app_simulation_presentation *presentations, size_t count) {
  qa_app_sim_unified_presentation *out = calloc(1, sizeof *out);
  if (!out) qa_range_error("Out of memory");
  (void)snap;
  out->count = count;
  out->models = count ? malloc(count * sizeof *out->models) : NULL;
  out->content_ids = count ? malloc(count * sizeof *out->content_ids) : NULL;
  out->paths = count ? malloc(count * sizeof *out->paths) : NULL;
  for (size_t i = 0; i < count; i++) {
    out->models[i] = presentations[i];
    out->content_ids[i] = presentation_content(presentations[i]);
    const char *family = presentations[i].family == QA_GAME_FAMILY_Q1 ? "q1" : presentations[i].family == QA_GAME_FAMILY_Q2 ? "q2" : "q3";
    out->paths[i] = qa_app_sim_unified_wire_model_path(family, presentations[i].path);
    out->models[i].content = out->content_ids[i];
    out->models[i].path = out->paths[i];
  }
  return out;
}
qa_app_sim_unified_presentation *qa_app_sim_unified_presentation_for(qa_app_sim_unified_snapshot snapshot, const char *recipient_kind, qa_client_id recipient_client) {
  qa_app_sim_unified_presentation *out = build(snapshot, snapshot.presentations, snapshot.presentation_count);
  const qa_app_sim_unified_participant *recipient = NULL;
  for (size_t i = 0; i < snapshot.participant_count; i++)
    if (qa_same_client(snapshot.participants[i].client, recipient_client)) recipient = &snapshot.participants[i];
  if (recipient && snapshot.player_view) {
    qa_app_sim_unified_player_view source;
    if (snapshot.player_view(snapshot.ctx, recipient->actor, &source)) {
      qa_body_state body; bool has_body = snapshot.body_read && snapshot.body_read(snapshot.ctx, recipient->actor, &body);
      double health = 0; bool has_health = snapshot.combat_health && snapshot.combat_health(snapshot.ctx, recipient->actor, &health);
      out->has_view = true;
      out->view = source;
      if (has_body) out->view.view.origin = body.origin;
      out->view.has_health = has_health; out->view.health = health;
      (void)recipient_kind;
    }
  }
  return out;
}
void qa_app_sim_unified_presentation_for_free(qa_app_sim_unified_presentation *presentation) {
  if (!presentation) return;
  for (size_t i = 0; i < presentation->count; i++) { free(presentation->content_ids[i]); free(presentation->paths[i]); }
  free(presentation->content_ids); free(presentation->paths); free(presentation->models);
  free(presentation);
}
size_t qa_app_sim_unified_presentation_model_count(qa_app_sim_unified_presentation *presentation) { return presentation->count; }
const qa_app_simulation_presentation *qa_app_sim_unified_presentation_models(qa_app_sim_unified_presentation *presentation) { return presentation->models; }
bool qa_app_sim_unified_presentation_view(qa_app_sim_unified_presentation *presentation, qa_app_sim_unified_player_view *out) {
  if (!presentation->has_view) return false;
  *out = presentation->view; return true;
}
void qa_app_sim_unified_presentation_set_view(qa_app_sim_unified_presentation *presentation, qa_app_sim_unified_player_view *value) {
  presentation->has_view = value != NULL;
  if (value) presentation->view = *value;
}
qa_app_sim_unified_model_presentations qa_app_sim_unified_model_presentations_for(qa_app_sim_unified_snapshot snapshot) {
  qa_app_sim_unified_presentation *built = build(snapshot, snapshot.presentations, snapshot.presentation_count);
  qa_app_sim_unified_model_presentations out;
  out.presentations = built->models; out.count = built->count;
  out.content_ids = (qa_content_id *)built->content_ids; out.paths = built->paths;
  /* ownership moved out; free only the shell */
  built->models = NULL; built->content_ids = NULL; built->paths = NULL; built->count = 0;
  free(built);
  return out;
}
void qa_app_sim_unified_model_presentations_free(qa_app_sim_unified_model_presentations *presentations) {
  for (size_t i = 0; i < presentations->count; i++) { free((void *)presentations->content_ids[i]); free(presentations->paths[i]); }
  free(presentations->content_ids); free(presentations->paths); free(presentations->presentations);
  memset(presentations, 0, sizeof *presentations);
}
