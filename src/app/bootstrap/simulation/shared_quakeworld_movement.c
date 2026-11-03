/* Port of src/app/bootstrap/simulation/shared-quakeworld-movement.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/shared_quakeworld_movement.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/movement/body_shape.h"
#include "qa/movement/client_outputs.h"
#include "qa/movement/q1/quakeworld.h"
#include "qa/foundation/js.h"
typedef struct shared_ctx {
  qa_provider_id id; qa_q1_movement_options options; qa_bounds standing;
  qa_app_sim_qw_postures postures;
  void (*publish)(void *ctx, qa_bounds bounds, double view_height); void *publish_ctx;
  /* per-move state (TS closure locals) */
  qa_bounds bounds; double view_height; bool crouched, published;
  qa_movement_input input; qa_movement_services services;
  qa_q1_movement_hooks hooks; qa_q1_movement_options inner_options;
} shared_ctx;
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
typedef struct clear_env { shared_ctx *shared; qa_vec3 origin; } clear_env;
static bool clear_trace(void *ctx, qa_bounds bounds) {
  clear_env *env = ctx; shared_ctx *s = env->shared;
  qa_trace_query q; memset(&q, 0, sizeof q);
  q.start = env->origin; q.end = env->origin;
  q.shape.kind = is(s->input.shape.kind, "capsule") ? "capsule" : "box"; q.shape.bounds = bounds;
  q.target.is_model = false;
  q.policy.kind = "q1"; q.policy.move = "normal"; q.policy.has_hull = false;
  q.numeric = s->input.profile.numeric; q.pass_actor = &s->input.actor.id;
  qa_trace_result t = s->services.scene.trace(s->services.scene.ctx, q);
  return !t.start_solid && !t.all_solid;
}
static void posture(shared_ctx *s, qa_qw_user_command command, qa_qw_movement_state state) {
  qa_user_command wrapped; memset(&wrapped, 0, sizeof wrapped);
  wrapped.kind = QA_USER_COMMAND_QW; wrapped.as.qw = command;
  const qa_mod_client_movement_outputs *outputs = s->input.environment.client_outputs;
  qa_user_command effective = qa_client_stance_command(wrapped, outputs ? outputs->has_stance : false, outputs ? outputs->stance : false);
  if (effective.kind != QA_USER_COMMAND_QW) qa_range_error("Client stance changed movement dialect");
  bool requested = !s->input.environment.flight && effective.as.qw.up_move < 0;
  if (!requested && s->bounds.max.z < s->standing.max.z) {
    clear_env env = {s, state.origin};
    requested = !clear_trace(&env, s->standing);
  }
  s->crouched = requested;
  qa_bounds desired = (outputs && outputs->body_bounds) ? *outputs->body_bounds
      : (s->crouched ? s->postures.crouched.bounds : s->standing);
  clear_env env = {s, state.origin};
  qa_bounds next_bounds = qa_movement_bounds(s->bounds, desired, clear_trace, &env);
  bool same_desired = memcmp(&next_bounds, &desired, sizeof desired) == 0;
  if (!same_desired) s->crouched = s->bounds.max.z < s->standing.max.z;
  double next_height = s->crouched ? s->postures.crouched.view_height : s->postures.standing_view_height;
  if (!s->published || memcmp(&s->bounds, &next_bounds, sizeof next_bounds) != 0 || s->view_height != next_height) {
    s->bounds = next_bounds; s->view_height = next_height; s->published = true;
    if (s->publish) s->publish(s->publish_ctx, s->bounds, s->view_height);
  }
}
static qa_movement_continuation hook_link(void *ctx, qa_owned_actor actor, qa_movement_state state, bool triggers) {
  shared_ctx *s = ctx; const qa_q1_movement_hooks *h = s->options.hooks;
  if (h && h->link) return h->link(h->ctx, actor, state, triggers);
  qa_movement_continuation out; memset(&out, 0, sizeof out); out.is_continue = true; out.state = state; return out;
}
static bool hook_is_bsp(void *ctx, qa_trace_hit hit) {
  shared_ctx *s = ctx; const qa_q1_movement_hooks *h = s->options.hooks;
  if (h && h->is_bsp) return h->is_bsp(h->ctx, hit);
  return hit.kind == QA_TRACE_HIT_WORLD;
}
static qa_trace_shape hook_shape(void *ctx) {
  shared_ctx *s = ctx;
  qa_trace_shape out; out.kind = is(s->input.shape.kind, "capsule") ? "capsule" : "box"; out.bounds = s->bounds; return out;
}
static void hook_body_shape(void *ctx, qa_bounds bounds) {
  shared_ctx *s = ctx; s->bounds = bounds;
  if (s->publish) s->publish(s->publish_ctx, s->bounds, s->view_height);
  const qa_q1_movement_hooks *h = s->options.hooks;
  if (h && h->body_shape) h->body_shape(h->ctx, bounds);
}
static qa_movement_continuation hook_before(void *ctx, qa_q1_player_input input, qa_movement_state state) {
  shared_ctx *s = ctx; const qa_q1_movement_hooks *h = s->options.hooks;
  qa_movement_continuation result;
  if (h && h->before_physics) result = h->before_physics(h->ctx, input, state);
  else { memset(&result, 0, sizeof result); result.is_continue = true; result.state = state; }
  if (result.is_continue && result.state.kind == QA_MOVEMENT_STATE_QW && input.command.kind == QA_USER_COMMAND_QW)
    posture(s, input.command.as.qw, result.state.as.qw);
  return result;
}
static qa_movement_continuation hook_after(void *ctx, qa_q1_player_input input, qa_movement_state state) {
  shared_ctx *s = ctx; const qa_q1_movement_hooks *h = s->options.hooks;
  if (h && h->after_physics) return h->after_physics(h->ctx, input, state);
  qa_movement_continuation out; memset(&out, 0, sizeof out); out.is_continue = true; out.state = state; return out;
}
/* Services share one ctx in the landed contract; the active move's
 * closure state is reached through this pointer (moves do not nest). */
static shared_ctx *active_shared;
static qa_animation_step_result anim_step(void *ctx, qa_animation_step_input input) {
  shared_ctx *s = active_shared;
  if (s && s->crouched) input.locomotion = "crouch";
  return s->services.animation_step(ctx, input);
}
static qa_movement_result shared_move(void *ctx, qa_movement_input input, qa_movement_services services) {
  shared_ctx *s = ctx;
  if (input.environment.pose != NULL || input.state.as.qw.dead || input.state.as.qw.spectator != 0) {
    qa_movement_provider inner = qa_create_qw_movement_provider(s->id, s->options);
    return inner.move(inner.ctx, input, services);
  }
  s->input = input; s->services = services;
  s->bounds = input.current_bounds ? *input.current_bounds : (is(input.shape.kind, "point") ? s->standing : input.shape.bounds);
  s->view_height = s->options.has_view_height ? s->options.view_height : s->postures.standing_view_height;
  s->crouched = s->bounds.max.z < s->standing.max.z; s->published = false;
  posture(s, input.command.as.qw, input.state.as.qw);
  qa_qw_user_command command = input.command.as.qw;
  if (command.up_move > 0) command.buttons = qa_js_to_int32(command.buttons) | 2;
  const qa_q1_movement_hooks *orig = s->options.hooks;
  s->hooks = orig ? *orig : (qa_q1_movement_hooks){0};
  s->hooks.ctx = s;
  s->hooks.link = hook_link; s->hooks.is_bsp = hook_is_bsp; s->hooks.shape = hook_shape;
  s->hooks.body_shape = hook_body_shape; s->hooks.before_physics = hook_before; s->hooks.after_physics = hook_after;
  if (!(orig && orig->think)) s->hooks.think = NULL;
  if (!(orig && orig->qw_state)) s->hooks.qw_state = NULL;
  if (!(orig && orig->sound)) s->hooks.sound = NULL;
  if (!(orig && orig->player_action)) s->hooks.player_action = NULL;
  s->inner_options = s->options;
  s->inner_options.has_view_height = true; s->inner_options.view_height = s->view_height;
  s->inner_options.hooks = &s->hooks;
  qa_movement_provider inner = qa_create_qw_movement_provider(s->id, s->inner_options);
  qa_movement_input next = input;
  next.command.kind = QA_USER_COMMAND_QW; next.command.as.qw = command;
  next.current_bounds = &s->bounds;
  next.shape.kind = is(input.shape.kind, "capsule") ? "capsule" : "box"; next.shape.bounds = s->bounds;
  qa_movement_services wrapped = services;
  wrapped.animation_step = anim_step;
  shared_ctx *previous = active_shared; active_shared = s;
  qa_movement_result result = inner.move(inner.ctx, next, wrapped);
  /* viewHeight is a TS getter over the closure; refresh for callers
   * that reuse options is unnecessary since options are per-move. */
  active_shared = previous;
  return result;
}
qa_movement_provider qa_app_sim_create_shared_quakeworld_movement(qa_provider_id id, qa_q1_movement_options options,
    qa_bounds standing_bounds, qa_app_sim_qw_postures postures,
    void (*publish_posture)(void *ctx, qa_bounds bounds, double view_height), void *publish_ctx) {
  shared_ctx *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  s->id = id; s->options = options; s->standing = standing_bounds; s->postures = postures;
  s->publish = publish_posture; s->publish_ctx = publish_ctx;
  qa_movement_provider out; out.kind = "q1-quakeworld"; out.id = id; out.ctx = s; out.move = shared_move;
  return out;
}
void qa_app_sim_shared_quakeworld_movement_destroy(qa_movement_provider provider) { free(provider.ctx); }
