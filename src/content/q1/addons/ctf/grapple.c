/* Port of src/content/q1/addons/ctf/grapple.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/grapple.h"

#include <string.h>

qa_q1ad_grapple_input qa_q1ad_ctf_grapple_input(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_ctf_input input = state->services.input != NULL
      ? state->services.input(state->services.ctx, actor) : (qa_q1ad_ctf_input){0};
  return (qa_q1ad_grapple_input){.held = input.attack,
      .release = !input.attack && input.grapple_selected, .jump = input.jump,
      .view_angles = input.view_angles, .teleport_until = input.teleport_until};
}
qa_vec3 qa_q1ad_ctf_grapple_aim(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_vec3 forward) {
  qa_q1ad_context *context = state->context;
  return context->game.aim != NULL
      ? context->game.aim(context->game.ctx, qa_q1ad_ctf_owner(state, actor), forward) : forward;
}
qa_q1ad_grapple_anchor qa_q1ad_ctf_grapple_anchor(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_actor *target = qa_q1ad_entity(state->context, &actor);
  bool player = qa_q1ad_is_player(state->context, &actor);
  const char *solid = target != NULL ? target->solid : NULL;
  return (qa_q1ad_grapple_anchor){
      .solid = solid == NULL || strcmp(solid, "none") != 0,
      .centered = player || (solid != NULL && strcmp(solid, "slidebox") == 0), .player = player};
}
static bool same_team_text(const char *a, const char *b) {
  return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0);
}
bool qa_q1ad_ctf_grapple_can_attach(qa_q1ad_ctf_state *state, qa_actor_id owner, qa_actor_id target) {
  return !qa_q1ad_is_player(state->context, &target) || qa_q1ad_ctf_teamplay(state) == 0 ||
      !same_team_text(qa_q1ad_ctf_team(state, target), qa_q1ad_ctf_last_team(state, owner));
}
bool qa_q1ad_ctf_grapple_can_pulse(qa_q1ad_ctf_state *state, qa_actor_id owner, qa_actor_id target) {
  return !qa_q1ad_is_player(state->context, &target) || qa_q1ad_ctf_teamplay(state) == 0 ||
      !same_team_text(qa_q1ad_ctf_last_team(state, target), qa_q1ad_ctf_last_team(state, owner));
}
bool qa_q1ad_ctf_grapple_can_damage(qa_q1ad_ctf_state *state, qa_actor_id target, qa_actor_id owner) {
  return state->context->game.can_damage != NULL &&
      state->context->game.can_damage(state->context->game.ctx, target, owner);
}
qa_q1ad_threewave_grapple qa_q1ad_ctf_create_grapple(qa_q1ad_ctf_state *state) {
  if (state->grapple_factory != NULL) return state->grapple_factory(state->factory_ctx, state);
  /* Equipment constructor not yet ported: present, inert ops. */
  return (qa_q1ad_threewave_grapple){.present = true};
}
void qa_q1ad_ctf_unhook(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if (state->grapple.present) {
    if (state->grapple.release != NULL) state->grapple.release(state->grapple.ctx, actor);
  } else if (state->shared_grapple != NULL && state->shared_grapple->release != NULL)
    state->shared_grapple->release(state->shared_grapple->ctx, actor);
}
void qa_q1ad_ctf_hook_touch(qa_q1ad_ctf_state *state, qa_q1ad_actor *hook, qa_actor_id other) {
  if (state->grapple.present && state->grapple.touch != NULL)
    state->grapple.touch(state->grapple.ctx, hook, other);
}
bool qa_q1ad_ctf_fire_hook(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if (((int32_t)qa_q1ad_ctf_teamplay(state) & QA_Q1AD_CTF_DISABLE_GRAPPLE) != 0 ||
      (state->services.observer != NULL && state->services.observer(state->services.ctx, actor)))
    return false;
  return state->grapple.present && state->grapple.fire != NULL &&
      state->grapple.fire(state->grapple.ctx, actor);
}
void qa_q1ad_ctf_grapple_trail(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  if (state->grapple.present && state->grapple.trail != NULL)
    state->grapple.trail(state->grapple.ctx, actor);
}
