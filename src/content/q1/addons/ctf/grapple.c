/* Port of src/content/q1/addons/ctf/grapple.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/grapple.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/weapons.h"
#include "qa/foundation/js.h"

static bool same_team(const char *a, const char *b) { return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0); }

static qa_q1_threewave_input grapple_input(void *ctx, qa_actor_id actor) {
  qa_q1ad_ctf_state *state = ctx;
  qa_q1ad_ctf_input input = state->services.input(state->services.ctx, actor);
  return (qa_q1_threewave_input){.held = input.attack, .release = !input.attack && input.grapple_selected,
                                 .jump = input.jump, .view_angles = input.view_angles,
                                 .teleport_until = input.teleport_until};
}
static qa_vec3 grapple_aim(void *ctx, qa_actor_id actor, qa_vec3 forward) {
  qa_q1ad_ctf_state *state = ctx;
  return qa_q1_aim(state->context->game, qa_q1ad_ctf_owner(state, actor), forward);
}
static qa_q1_threewave_anchor grapple_anchor(void *ctx, qa_actor_id actor) {
  qa_q1ad_ctf_state *state = ctx;
  qa_q1_services *game = state->context->game;
  qa_q1_actor *target = qa_q1_get_entity(game, &actor);
  bool player = qa_q1_is_player(game, &actor);
  /* target?.solid !== "none": an absent target compares undefined, so true. */
  return (qa_q1_threewave_anchor){
      .solid = target == NULL || strcmp(target->solid, "none") != 0,
      .centered = player || (target != NULL && strcmp(target->solid, "slidebox") == 0), .player = player};
}
static bool grapple_can_attach(void *ctx, qa_actor_id owner, qa_actor_id target) {
  const char * a;
  const char * b;
  qa_q1ad_ctf_state *state = ctx;
  return !qa_q1_is_player(state->context->game, &target) || qa_q1ad_ctf_teamplay(state) == 0 ||
         !(a = qa_q1ad_ctf_team(state, target), b = qa_q1ad_ctf_last_team(state, owner), same_team(a, b));
}
static bool grapple_can_pulse(void *ctx, qa_actor_id owner, qa_actor_id target) {
  qa_q1ad_ctf_state *state = ctx;
  return !qa_q1_is_player(state->context->game, &target) || qa_q1ad_ctf_teamplay(state) == 0 ||
         !same_team(qa_q1ad_ctf_last_team(state, target), qa_q1ad_ctf_last_team(state, owner));
}
static bool grapple_can_damage(void *ctx, qa_actor_id target, qa_actor_id owner) {
  qa_q1ad_ctf_state *state = ctx;
  return qa_q1_can_damage(state->context->game, target, owner);
}
/* TS ThreewaveGrapple constructor's host.actors.onRelease subscription; the
 * C equipment exposes the handler and leaves the subscription to its owner. */
static void grapple_release(void *ctx, qa_owned_actor actor) {
  qa_q1_threewave_on_release(ctx, actor.id);
}

qa_q1_threewave_grapple *qa_q1ad_ctf_create_grapple(qa_q1ad_ctf_state *state) {
  qa_q1_services *game = state->context->game;
  qa_q1_threewave_grapple *grapple = malloc(sizeof *grapple);
  if (grapple == NULL) return NULL;
  qa_q1_threewave_host host = {.ctx = state, .input = grapple_input, .aim = grapple_aim, .anchor = grapple_anchor,
                               .can_attach = grapple_can_attach, .can_pulse = grapple_can_pulse,
                               .can_damage = grapple_can_damage};
  if (!qa_q1_threewave_grapple_init(grapple, game, host)) {
    qa_q1_threewave_grapple_free(grapple);
    free(grapple);
    return NULL;
  }
  state->grapple_release = qa_session_actor_registry_on_release(game->host.actors, grapple_release, grapple);
  return grapple;
}
void qa_q1ad_ctf_unhook(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if (state->grapple != NULL) qa_q1_threewave_release(state->grapple, actor);
  else if (state->shared_grapple != NULL) state->shared_grapple->release(state->shared_grapple->ctx, actor);
}
void qa_q1ad_ctf_hook_touch(qa_q1ad_ctf_state *state, qa_q1_actor *hook, qa_actor_id other) {
  if (state->grapple != NULL) qa_q1_threewave_touch(state->grapple, hook, other, NULL);
}
bool qa_q1ad_ctf_fire_hook(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if ((qa_js_to_int32(qa_q1ad_ctf_teamplay(state)) & QA_Q1AD_CTF_DISABLE_GRAPPLE) != 0 ||
      state->services.observer(state->services.ctx, actor))
    return false;
  return state->grapple != NULL && qa_q1_threewave_fire(state->grapple, actor);
}
void qa_q1ad_ctf_grapple_trail(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if (state->grapple != NULL) qa_q1_threewave_trail(state->grapple, actor);
}
