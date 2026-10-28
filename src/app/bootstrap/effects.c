/* Port of src/app/bootstrap/effects.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/effects.h"
#include <stdlib.h>
#include <string.h>
struct qa_app_application_effects {
  qa_app_effects_host host;
  const void *assets, *queries;
  double seed;
  qa_app_unhandled_effect *unhandled; size_t unhandled_count, unhandled_cap;
  bool closed;
};
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
qa_app_application_effects *qa_app_application_effects_create(qa_app_effects_host host, const void *assets, const void *queries, double seed) {
  qa_app_application_effects *effects = calloc(1, sizeof *effects);
  if (!effects) return NULL;
  effects->host = host;
  effects->assets = assets;
  effects->queries = queries;
  effects->seed = seed;
  return effects;
}
void qa_app_application_effects_destroy(qa_app_application_effects *effects) {
  if (!effects) return;
  qa_app_application_effects_close(effects);
  for (size_t i = 0; i < effects->unhandled_count; i++) free(effects->unhandled[i].reason);
  free(effects->unhandled);
  free(effects);
}
void qa_app_application_effects_bind_renderer_hardware(qa_app_application_effects *effects) { effects->host.bind_hardware(effects->host.ctx); }
void qa_app_application_effects_receive(qa_app_application_effects *effects, const void *const *events, size_t count) {
  if (count == 0) return;
  const char **reasons = calloc(count, sizeof *reasons);
  effects->host.receive(effects->host.ctx, events, count, reasons);
  for (size_t i = 0; i < count; i++) {
    if (!reasons[i]) continue;
    if (effects->unhandled_count == effects->unhandled_cap) {
      effects->unhandled_cap = effects->unhandled_cap ? effects->unhandled_cap * 2 : 16;
      effects->unhandled = realloc(effects->unhandled, effects->unhandled_cap * sizeof *effects->unhandled);
    }
    effects->unhandled[effects->unhandled_count++] = (qa_app_unhandled_effect){events[i], dup(reasons[i])};
  }
  free(reasons);
}
qa_app_unhandled_effect *qa_app_application_effects_drain_unhandled(qa_app_application_effects *effects, size_t *out_count) {
  qa_app_unhandled_effect *rows = effects->unhandled;
  *out_count = effects->unhandled_count;
  effects->unhandled = NULL;
  effects->unhandled_count = 0;
  effects->unhandled_cap = 0;
  return rows;
}
size_t qa_app_application_effects_drain_sounds(qa_app_application_effects *effects, const void **out) {
  return effects->host.drain_sounds(effects->host.ctx, out);
}
const char *qa_app_application_effects_preload_transient_resources(qa_app_application_effects *effects, char **out_errors_json) {
  return effects->host.preload_transient(effects->host.ctx, out_errors_json);
}
const char *qa_app_application_effects_preload_model(qa_app_application_effects *effects, qa_content_id content, const char *path) {
  return effects->host.preload_model(effects->host.ctx, content, path);
}
const char *qa_app_application_effects_prepare(qa_app_application_effects *effects, const void *snapshot) {
  return effects->host.prepare(effects->host.ctx, snapshot);
}
qa_app_effect_frame qa_app_application_effects_frame(qa_app_application_effects *effects, qa_scene_camera camera, bool has_viewer, qa_actor_id viewer) {
  return effects->host.frame(effects->host.ctx, camera, viewer, has_viewer);
}
const void *qa_app_application_effects_shadow_scene_lights(qa_app_application_effects *effects, qa_scene_camera camera, size_t *out_count) {
  return effects->host.shadow_lights(effects->host.ctx, camera, out_count);
}
void qa_app_application_effects_reset_round(qa_app_application_effects *effects) { effects->host.reset_round(effects->host.ctx); }
void qa_app_application_effects_close(qa_app_application_effects *effects) {
  if (!effects || effects->closed) return;
  effects->closed = true;
  effects->host.close_effects(effects->host.ctx);
}
