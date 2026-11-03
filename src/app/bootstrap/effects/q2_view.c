/* Port of src/app/bootstrap/effects/q2-view.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/effects/q2_view.h"
#include <math.h>
#include <stdlib.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/view.h"
#include "qa/foundation/js.h"
typedef struct player_effects {
  qa_actor_id actor;
  double ir_until, nuke_until;
  bool has_sphere_event;
  qa_q2mp_player_effect sphere; /* sphere-camera event copy */
} player_effects;
struct qa_app_q2_effect_views {
  player_effects *players;
  size_t count, capacity;
};
qa_app_q2_effect_views *qa_app_q2_effect_views_create(void) {
  qa_app_q2_effect_views *v = calloc(1, sizeof *v);
  if (v == NULL) qa_range_error("Out of memory");
  return v;
}
void qa_app_q2_effect_views_destroy(qa_app_q2_effect_views *views) {
  if (views == NULL) return;
  free(views->players);
  free(views);
}
static player_effects *find_or_add(qa_app_q2_effect_views *v, qa_actor_id actor) {
  for (size_t i = 0; i < v->count; i++)
    if (qa_same_actor(v->players[i].actor, actor)) return &v->players[i];
  if (v->count == v->capacity) {
    size_t cap = v->capacity == 0 ? 8 : v->capacity * 2;
    player_effects *next = realloc(v->players, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    v->players = next; v->capacity = cap;
  }
  player_effects *state = &v->players[v->count++];
  *state = (player_effects){0};
  state->actor = actor;
  return state;
}
void qa_app_q2_effect_views_receive(qa_app_q2_effect_views *views, qa_q2mp_player_effect event) {
  player_effects *state = find_or_add(views, event.actor);
  if (event.kind == QA_Q2MP_EFFECT_IR) state->ir_until = event.until;
  else if (event.kind == QA_Q2MP_EFFECT_NUKE_BLIND) state->nuke_until = event.until;
  else {
    state->has_sphere_event = event.has_sphere;
    if (event.has_sphere) state->sphere = event;
  }
}
qa_app_q2_effect_player_view qa_app_q2_effect_views_frame(qa_app_q2_effect_views *views,
    qa_actor_id actor, qa_scene_camera camera, double seconds,
    bool (*pose)(void *ctx, qa_actor_id actor, qa_vec3 *origin_out), void *pose_ctx) {
  qa_app_q2_effect_player_view out = {camera, false, false, {0, 0, 0, 0}};
  player_effects *state = NULL;
  for (size_t i = 0; i < views->count; i++)
    if (qa_same_actor(views->players[i].actor, actor)) state = &views->players[i];
  if (state == NULL) return out;
  double frames = qa_js_round(state->ir_until * 10) - qa_js_round(seconds * 10);
  bool infrared = frames > 0 && (frames > 30 || ((qa_js_to_int32(frames)) & 4) != 0);
  double nuke_alpha = qa_js_max(0, qa_js_min(1, (state->nuke_until - seconds) / 2));
  double alpha = nuke_alpha + (1 - nuke_alpha) * (infrared ? 0.2 : 0);
  double fraction = alpha > 0 ? nuke_alpha / alpha : 0;
  out.infrared = infrared;
  if (alpha > 0) {
    out.has_blend = true;
    out.blend = (qa_vec4){1, fraction, fraction, alpha};
  }
  if (state->has_sphere_event) {
    double vertical = atan(camera.viewport.height / camera.viewport.width * tan(140 * M_PI / 360)) * 360 / M_PI;
    qa_mat4 lens = qa_scene_perspective_projection(140, vertical, 16384, 4);
    qa_scene_camera selected = camera;
    selected.projection.values[0] = lens.values[0];
    selected.projection.values[5] = lens.values[5];
    qa_vec3 origin = state->sphere.origin;
    qa_vec3 posed;
    if (pose != NULL && pose(pose_ctx, state->sphere.sphere, &posed)) origin = posed;
    selected.origin = origin;
    selected.axis = qa_angles_to_axis(state->sphere.angles);
    out.camera = selected;
  }
  return out;
}
static bool actor_in(const qa_actor_id *actors, size_t count, qa_actor_id actor) {
  for (size_t i = 0; i < count; i++) if (qa_same_actor(actors[i], actor)) return true;
  return false;
}
void qa_app_q2_effect_views_retain(qa_app_q2_effect_views *views, const qa_actor_id *actors, size_t count) {
  size_t kept = 0;
  for (size_t i = 0; i < views->count; i++) {
    player_effects state = views->players[i];
    if (!actor_in(actors, count, state.actor)) continue;
    if (state.has_sphere_event && !actor_in(actors, count, state.sphere.sphere))
      state.has_sphere_event = false;
    views->players[kept++] = state;
  }
  views->count = kept;
}
void qa_app_q2_effect_views_clear(qa_app_q2_effect_views *views) { views->count = 0; }
