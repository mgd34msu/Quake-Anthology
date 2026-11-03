/* Port of src/app/bootstrap/simulation/grapple-runtime.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/grapple_runtime.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"

static const qa_vec3 zero = {0, 0, 0};
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *grown = realloc(p, size == 0 ? 1 : size);
  if (grown == NULL) qa_range_error("Out of memory");
  return grown;
}

/* One Q2 slot binding: the TS weaponAnimations + q2Weapons entries for
 * an actor. The weapon borrows animation.state; kick writes the
 * animation; the other presentation members forward to the slot host's
 * presentation with its own ctx. */
typedef struct q2_binding {
  qa_actor_id actor;
  qa_app_sim_grapple_weapon_animation animation;
  qa_grapple_weapon_presentation host_presentation;
  qa_q2_grapple_weapon weapon;
} q2_binding;
typedef struct control_entry { qa_actor_id actor; qa_app_sim_grapple_control control; } control_entry;
typedef struct handoff_block {
  qa_app_sim_grapple_runtime *runtime;
  qa_actor_id actor;
  q2_binding *q2; /* the TS closure's captured q2 weapon */
} handoff_block;

struct qa_app_sim_grapple_runtime {
  qa_grapple_selection selection;
  qa_app_sim_grapple_source source;
  qa_app_sim_source_random *random;
  bool has_slot_host;
  qa_app_sim_grapple_slot_host slot_host;
  control_entry *controls; size_t control_count, control_capacity;
  q2_binding **q2; size_t q2_count, q2_capacity;          /* TS weaponAnimations/q2Weapons */
  q2_binding **retired; size_t retired_count, retired_capacity; /* still referenced by TS closures */
  bool has_q1_weapon;
  qa_q1_threewave_weapon q1_weapon;
  qa_actor_id *holstered; size_t holstered_count, holstered_capacity; /* TS q3Holstered */
  handoff_block **handoffs; size_t handoff_count, handoff_capacity;
  int64_t release_subscription;
};

static qa_session_actor_registry *actors_of(const qa_app_sim_grapple_runtime *r) {
  const qa_app_sim_grapple_source *s = &r->source;
  if (is(s->kind, "q1-threewave")) return s->q1_game->host.actors;
  if (is(s->kind, "q3-qvm")) return qa_app_sim_qvm_grapple_source_actors(s->q3_game);
  return qa_q2_entity_services_game(s->q2_game)->host->actors;
}
static qa_shared_body_table *bodies_of(const qa_app_sim_grapple_runtime *r) {
  const qa_app_sim_grapple_source *s = &r->source;
  if (is(s->kind, "q1-threewave")) return s->q1_game->host.bodies;
  if (is(s->kind, "q3-qvm")) return qa_app_sim_qvm_grapple_source_bodies(s->q3_game);
  return qa_q2_entity_services_game(s->q2_game)->host->bodies;
}
static qa_q2_game_services *q2_game(const qa_app_sim_grapple_runtime *r) { return qa_q2_entity_services_game(r->source.q2_game); }
static qa_saved_actor_id saved(qa_actor_id actor) { return qa_saved_actor_id_of(actor); }

/* --- TS controls Map --- */
static control_entry *find_control(const qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  for (size_t i = 0; i < r->control_count; i++) if (qa_same_actor(r->controls[i].actor, actor)) return &r->controls[i];
  return NULL;
}
static void set_control(qa_app_sim_grapple_runtime *r, qa_actor_id actor, qa_app_sim_grapple_control control) {
  control_entry *entry = find_control(r, actor);
  if (entry == NULL) {
    if (r->control_count == r->control_capacity) {
      r->control_capacity = r->control_capacity ? r->control_capacity * 2 : 4;
      r->controls = xrealloc(r->controls, r->control_capacity * sizeof *r->controls);
    }
    entry = &r->controls[r->control_count++];
    entry->actor = actor;
  }
  entry->control = control;
}
static void delete_control(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  for (size_t i = 0; i < r->control_count; i++)
    if (qa_same_actor(r->controls[i].actor, actor)) {
      memmove(&r->controls[i], &r->controls[i + 1], (r->control_count - i - 1) * sizeof *r->controls);
      r->control_count--;
      return;
    }
}
/* --- TS weaponAnimations/q2Weapons Maps --- */
static void retire(qa_app_sim_grapple_runtime *r, q2_binding *binding) {
  if (r->retired_count == r->retired_capacity) {
    r->retired_capacity = r->retired_capacity ? r->retired_capacity * 2 : 4;
    r->retired = xrealloc(r->retired, r->retired_capacity * sizeof *r->retired);
  }
  r->retired[r->retired_count++] = binding;
}
static q2_binding *find_q2(const qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  for (size_t i = 0; i < r->q2_count; i++) if (qa_same_actor(r->q2[i]->actor, actor)) return r->q2[i];
  return NULL;
}
static void delete_q2(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  for (size_t i = 0; i < r->q2_count; i++)
    if (qa_same_actor(r->q2[i]->actor, actor)) {
      retire(r, r->q2[i]);
      memmove(&r->q2[i], &r->q2[i + 1], (r->q2_count - i - 1) * sizeof *r->q2);
      r->q2_count--;
      return;
    }
}
static void clear_q2(qa_app_sim_grapple_runtime *r) {
  for (size_t i = 0; i < r->q2_count; i++) retire(r, r->q2[i]);
  r->q2_count = 0;
}
/* --- TS q3Holstered Set --- */
static bool holstered_has(const qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  for (size_t i = 0; i < r->holstered_count; i++) if (qa_same_actor(r->holstered[i], actor)) return true;
  return false;
}
static void holstered_add(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  if (holstered_has(r, actor)) return;
  if (r->holstered_count == r->holstered_capacity) {
    r->holstered_capacity = r->holstered_capacity ? r->holstered_capacity * 2 : 4;
    r->holstered = xrealloc(r->holstered, r->holstered_capacity * sizeof *r->holstered);
  }
  r->holstered[r->holstered_count++] = actor;
}
static void holstered_delete(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  for (size_t i = 0; i < r->holstered_count; i++)
    if (qa_same_actor(r->holstered[i], actor)) {
      memmove(&r->holstered[i], &r->holstered[i + 1], (r->holstered_count - i - 1) * sizeof *r->holstered);
      r->holstered_count--;
      return;
    }
}

static void on_release(void *ctx, qa_owned_actor actor) {
  qa_app_sim_grapple_runtime *r = ctx;
  delete_control(r, actor.id);
  delete_q2(r, actor.id);
  holstered_delete(r, actor.id);
}

qa_app_sim_grapple_runtime *qa_app_sim_grapple_runtime_create(qa_grapple_selection selection,
    qa_app_sim_grapple_source source, qa_app_sim_source_random *random, const qa_app_sim_grapple_slot_host *slot_host) {
  if (is(selection.binding, "slot") && slot_host == NULL)
    qa_raise("Error", 0, 0, "Selected grapple slot needs its source weapon host");
  qa_app_sim_grapple_runtime *r = xcalloc(1, sizeof *r);
  r->selection = selection;
  r->source = source;
  r->random = random;
  if (slot_host != NULL) { r->has_slot_host = true; r->slot_host = *slot_host; }
  if (is(source.kind, "q1-threewave") && is(selection.binding, "slot") && slot_host != NULL) {
    qa_q1_threewave_weapon_hooks hooks = {slot_host->ctx, slot_host->selected, slot_host->available, slot_host->frame};
    qa_q1_threewave_weapon_init(&r->q1_weapon, source.q1_core, hooks);
    r->has_q1_weapon = true;
  }
  r->release_subscription = qa_session_actor_registry_on_release(actors_of(r), on_release, r);
  if (is(source.kind, "q2-ctf")) {
    qa_q2_source_callbacks_register(qa_q2_entity_services_callbacks(source.q2_game), qa_q2_ctf_grapple_callbacks(source.ctf_core));
    qa_q2_ctf_grapple_bind(source.ctf_core, q2_game(r));
  } else if (is(source.kind, "q2-lmctf")) {
    qa_q2_source_callbacks_register(qa_q2_entity_services_callbacks(source.q2_game), qa_lmctf_grapple_callbacks(source.lmctf_core));
    qa_lmctf_grapple_bind(source.lmctf_core, q2_game(r));
  }
  return r;
}
void qa_app_sim_grapple_runtime_destroy(qa_app_sim_grapple_runtime *r) {
  if (r == NULL) return;
  qa_session_actor_registry_off_release(actors_of(r), r->release_subscription);
  for (size_t i = 0; i < r->q2_count; i++) free(r->q2[i]);
  for (size_t i = 0; i < r->retired_count; i++) free(r->retired[i]);
  for (size_t i = 0; i < r->handoff_count; i++) free(r->handoffs[i]);
  free(r->q2); free(r->retired); free(r->handoffs); free(r->controls); free(r->holstered);
  free(r);
}
const qa_grapple_selection *qa_app_sim_grapple_runtime_selection(const qa_app_sim_grapple_runtime *r) { return &r->selection; }
const qa_app_sim_grapple_source *qa_app_sim_grapple_runtime_source(const qa_app_sim_grapple_runtime *r) { return &r->source; }

/* GrappleWeaponPresentation with the runtime's kick and the host's other members. */
static void p_kick(void *ctx, qa_vec3 origin, double pitch) {
  q2_binding *b = ctx;
  b->animation.kick_origin = origin;
  b->animation.kick_pitch = pitch;
}
static void p_attack(void *ctx) {
  q2_binding *b = ctx;
  if (b->host_presentation.attack_animation != NULL) b->host_presentation.attack_animation(b->host_presentation.ctx);
}
static void p_reverse(void *ctx) {
  q2_binding *b = ctx;
  if (b->host_presentation.reverse_animation != NULL) b->host_presentation.reverse_animation(b->host_presentation.ctx);
}
static void p_powerup(void *ctx) {
  q2_binding *b = ctx;
  if (b->host_presentation.powerup_sound != NULL) b->host_presentation.powerup_sound(b->host_presentation.ctx);
}
static double p_time(void *ctx, qa_q2w_state *state) {
  q2_binding *b = ctx;
  return b->host_presentation.animation_time(b->host_presentation.ctx, state);
}
static qa_q2_edition edition_of(const char *edition) { return is(edition, "rerelease") ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC; }
static void bind_weapon(qa_app_sim_grapple_runtime *r, qa_actor_id actor, qa_app_sim_grapple_weapon_animation animation) {
  if (is(r->source.kind, "q1-threewave") || is(r->source.kind, "q3-qvm") || !r->has_slot_host)
    qa_raise("Error", 0, 0, "Q2 grapple animation requires its selected slot host");
  q2_binding *b = xcalloc(1, sizeof *b);
  b->actor = actor;
  b->animation = animation;
  /* TS sets weaponAnimations before reading the host presentation. */
  q2_binding *previous = NULL;
  size_t at = r->q2_count;
  for (size_t i = 0; i < r->q2_count; i++) if (qa_same_actor(r->q2[i]->actor, actor)) { previous = r->q2[i]; at = i; }
  if (previous != NULL) { retire(r, previous); r->q2[at] = b; }
  else {
    if (r->q2_count == r->q2_capacity) {
      r->q2_capacity = r->q2_capacity ? r->q2_capacity * 2 : 4;
      r->q2 = xrealloc(r->q2, r->q2_capacity * sizeof *r->q2);
    }
    r->q2[r->q2_count++] = b;
  }
  b->host_presentation = r->slot_host.presentation(r->slot_host.ctx, actor);
  qa_grapple_weapon_presentation presentation = {b, p_kick, p_attack, p_reverse, p_powerup,
                                                 b->host_presentation.animation_time != NULL ? p_time : NULL};
  qa_grapple_weapon_source source;
  memset(&source, 0, sizeof source);
  if (is(r->source.kind, "q2-ctf")) {
    source.kind = "ctf"; source.ctf = r->source.ctf_core; source.edition = edition_of(r->selection.edition);
  } else {
    source.kind = "lmctf"; source.lmctf = r->source.lmctf_core;
  }
  qa_q2_grapple_weapon_init(&b->weapon, actor, q2_game(r), source, &b->animation.state, presentation);
}

void qa_app_sim_grapple_runtime_admit(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  if (!qa_session_actor_registry_is_live(actors_of(r), actor) || !qa_shared_body_table_read(bodies_of(r), actor).has_value)
    qa_raise("Error", 0, 0, "Grapple needs an admitted actor and body");
  if (is(r->source.kind, "q3-qvm")) qa_app_sim_qvm_grapple_source_admit(r->source.q3_game, actor);
  qa_app_sim_grapple_control control;
  memset(&control, 0, sizeof control);
  set_control(r, actor, control);
  if (is(r->selection.binding, "slot")) {
    if (is(r->source.kind, "q1-threewave")) (void)qa_q1_threewave_state_for(r->source.q1_core, actor);
    else if (is(r->source.kind, "q3-qvm")) holstered_add(r, actor);
    else {
      qa_app_sim_grapple_weapon_animation animation;
      memset(&animation, 0, sizeof animation);
      animation.state = qa_create_grapple_weapon_state();
      bind_weapon(r, actor, animation);
    }
  }
}

qa_weapon_reference qa_app_sim_grapple_runtime_weapon(const qa_app_sim_grapple_runtime *r) {
  const char *mechanic = r->selection.mechanic;
  qa_weapon_reference out = {r->selection.source.provider,
      is(mechanic, "q1-threewave") ? "q1:ctf/weapon/grapple" : is(mechanic, "q2-ctf") ? "q2:weapon_grapple"
      : is(mechanic, "q3-qvm") ? "q3:weapon_grapplinghook" : "q2:weapon_hook"};
  return out;
}

/* --- TS handoff closures --- */
static void h_q3_holster(void *ctx) {
  handoff_block *h = ctx;
  holstered_add(h->runtime, h->actor);
  qa_app_sim_grapple_runtime_release(h->runtime, h->actor);
}
static bool h_q3_is_holstered(void *ctx) { handoff_block *h = ctx; return holstered_has(h->runtime, h->actor); }
static void h_q3_resume(void *ctx) { handoff_block *h = ctx; holstered_delete(h->runtime, h->actor); }
static void h_q1_holster(void *ctx) { handoff_block *h = ctx; qa_q1_threewave_weapon_holster(&h->runtime->q1_weapon, h->actor); }
static bool h_q1_is_holstered(void *ctx) { handoff_block *h = ctx; return qa_q1_threewave_weapon_is_holstered(&h->runtime->q1_weapon); }
static void h_q1_resume(void *ctx) { handoff_block *h = ctx; qa_q1_threewave_weapon_resume(&h->runtime->q1_weapon, h->actor); }
static void h_q2_holster(void *ctx) { handoff_block *h = ctx; qa_q2_grapple_weapon_holster(&h->q2->weapon); }
static bool h_q2_is_holstered(void *ctx) { handoff_block *h = ctx; return qa_q2_grapple_weapon_is_holstered(&h->q2->weapon); }
static void h_q2_resume(void *ctx) {
  handoff_block *h = ctx;
  qa_q2_grapple_weapon_resume(&h->q2->weapon);
  q2_binding *animation = find_q2(h->runtime, h->actor);
  if (animation != NULL) {
    qa_q2_game_services *game = h->q2->weapon.game;
    animation->animation.next_frame_at = qa_js_round((game->host->now(game->host->ctx) + 0.1) * 1000) / 1000;
  }
}
qa_app_sim_equipment_weapon_handoff qa_app_sim_grapple_runtime_handoff(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  if (!is(r->selection.binding, "slot")) qa_raise("Error", 0, 0, "Offhand grapple has no weapon slot");
  qa_app_sim_equipment_weapon_handoff out;
  memset(&out, 0, sizeof out);
  out.weapon = qa_app_sim_grapple_runtime_weapon(r);
  q2_binding *q2 = NULL;
  if (!is(r->source.kind, "q3-qvm") && !r->has_q1_weapon) {
    q2 = find_q2(r, actor);
    if (q2 == NULL) qa_raise("Error", 0, 0, "Missing grapple weapon animation owner");
  }
  handoff_block *h = xcalloc(1, sizeof *h);
  h->runtime = r; h->actor = actor; h->q2 = q2;
  if (r->handoff_count == r->handoff_capacity) {
    r->handoff_capacity = r->handoff_capacity ? r->handoff_capacity * 2 : 4;
    r->handoffs = xrealloc(r->handoffs, r->handoff_capacity * sizeof *r->handoffs);
  }
  r->handoffs[r->handoff_count++] = h;
  out.ctx = h;
  if (is(r->source.kind, "q3-qvm")) { out.holster = h_q3_holster; out.is_holstered = h_q3_is_holstered; out.resume = h_q3_resume; }
  else if (r->has_q1_weapon) { out.holster = h_q1_holster; out.is_holstered = h_q1_is_holstered; out.resume = h_q1_resume; }
  else { out.holster = h_q2_holster; out.is_holstered = h_q2_is_holstered; out.resume = h_q2_resume; }
  return out;
}

bool qa_app_sim_grapple_runtime_weapon_view(qa_app_sim_grapple_runtime *r, qa_actor_id actor,
                                            qa_app_sim_grapple_weapon_view *out) {
  memset(out, 0, sizeof *out);
  if (!is(r->selection.binding, "slot")) return false;
  if (is(r->source.kind, "q1-threewave")) {
    out->path = "progs/v_star.mdl";
    out->frame = qa_q1_threewave_state_for(r->source.q1_core, actor)->weapon_frame;
    return true;
  }
  if (is(r->source.kind, "q3-qvm")) { *out = qa_app_sim_qvm_grapple_source_weapon_view(r->source.q3_game, actor); return true; }
  q2_binding *b = find_q2(r, actor);
  if (b == NULL) return false;
  out->path = qa_q2_grapple_weapon_definition(&b->weapon)->view_model;
  out->frame = b->animation.state.animation.frame;
  out->kick_origin = b->animation.kick_origin;
  out->kick_pitch = b->animation.kick_pitch;
  return true;
}
void qa_app_sim_grapple_runtime_released(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  q2_binding *b = find_q2(r, actor);
  if (b != NULL) qa_q2_grapple_weapon_released(&b->weapon);
}

static void step_slot(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  if (!r->has_slot_host) qa_raise("Error", 0, 0, "Missing grapple slot host");
  const qa_app_sim_grapple_slot_host *host = &r->slot_host;
  control_entry *found = find_control(r, actor);
  bool has_input = found != NULL;
  qa_app_sim_grapple_control input;
  memset(&input, 0, sizeof input);
  if (has_input) {
    input = found->control;
    qa_app_sim_grapple_control next = input;
    next.pressed = false; next.released = false;
    set_control(r, actor, next);
  }
  if (is(r->source.kind, "q3-qvm")) {
    if ((has_input && input.released) || !host->selected(host->ctx, actor))
      qa_app_sim_qvm_grapple_source_release(r->source.q3_game, actor);
    else if (host->selected(host->ctx, actor) && host->available(host->ctx, actor) && has_input && input.pressed
             && !holstered_has(r, actor))
      qa_app_sim_qvm_grapple_source_fire(r->source.q3_game, actor);
    return;
  }
  if (is(r->source.kind, "q1-threewave")) {
    if (host->selected(host->ctx, actor) && qa_app_sim_grapple_runtime_held(r, actor) && r->has_q1_weapon)
      (void)qa_q1_threewave_weapon_attack(&r->q1_weapon, actor);
    if (r->has_q1_weapon) qa_q1_threewave_weapon_animate(&r->q1_weapon, actor);
    qa_q1_threewave_trail(r->source.q1_core, actor);
    return;
  }
  q2_binding *b = find_q2(r, actor);
  if (b == NULL) qa_raise("Error", 0, 0, "Missing Q2 slot animation");
  if (has_input && input.pressed && host->selected(host->ctx, actor)) b->animation.state.animation.latched_attack = true;
  qa_q2_game_services *game = q2_game(r);
  double now = game->host->now(game->host->ctx);
  if (is(r->selection.edition, "rerelease") || now + 0.000001 >= b->animation.next_frame_at) {
    b->animation.next_frame_at = qa_js_round((b->animation.next_frame_at + 0.1) * 1000) / 1000;
    qa_grapple_weapon_step_input step;
    memset(&step, 0, sizeof step);
    step.attack = qa_app_sim_grapple_runtime_held(r, actor);
    step.now = now;
    step.frame_seconds = game->host->frame_seconds(game->host->ctx);
    qa_q2_grapple_weapon_step(&b->weapon, step);
  }
  qa_q2_grapple_weapon_player_frame(&b->weapon);
}

void qa_app_sim_grapple_runtime_observe_teleport(qa_app_sim_grapple_runtime *r, qa_actor_id actor, double bit) {
  control_entry *state = find_control(r, actor);
  if (state == NULL) return;
  if (state->control.has_teleport_bit && state->control.teleport_bit != bit) qa_app_sim_grapple_runtime_release(r, actor);
  control_entry *current = find_control(r, actor);
  if (current != NULL) { current->control.has_teleport_bit = true; current->control.teleport_bit = bit; }
}
bool qa_app_sim_grapple_runtime_jump(const qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  control_entry *state = find_control(r, actor);
  return state != NULL && state->control.jump;
}
void qa_app_sim_grapple_runtime_set_jump(qa_app_sim_grapple_runtime *r, qa_actor_id actor, bool jump) {
  control_entry *state = find_control(r, actor);
  if (state != NULL) state->control.jump = jump;
}
bool qa_app_sim_grapple_runtime_held(const qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  control_entry *state = find_control(r, actor);
  return state != NULL && state->control.held;
}
void qa_app_sim_grapple_runtime_input(qa_app_sim_grapple_runtime *r, qa_actor_id actor, bool held) {
  control_entry *previous = find_control(r, actor);
  if (previous == NULL) qa_raise("Error", 0, 0, "Actor has no selected offhand grapple");
  qa_app_sim_grapple_control next = previous->control;
  next.held = held;
  next.pressed = previous->control.pressed || (held && !previous->control.held);
  next.released = previous->control.released || (!held && previous->control.held);
  previous->control = next;
}
qa_vec3 qa_app_sim_grapple_runtime_previous_velocity(const qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  control_entry *state = find_control(r, actor);
  return state == NULL ? zero : state->control.previous_velocity;
}
void qa_app_sim_grapple_runtime_set_previous_velocity(qa_app_sim_grapple_runtime *r, qa_actor_id actor, qa_vec3 velocity) {
  control_entry *state = find_control(r, actor);
  if (state != NULL) state->control.previous_velocity = velocity;
}
bool qa_app_sim_grapple_runtime_prediction(const qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  control_entry *state = find_control(r, actor);
  return state != NULL && state->control.prediction_suppressed;
}
void qa_app_sim_grapple_runtime_set_prediction(qa_app_sim_grapple_runtime *r, qa_actor_id actor, bool suppressed) {
  control_entry *state = find_control(r, actor);
  if (state != NULL) state->control.prediction_suppressed = suppressed;
}
bool qa_app_sim_grapple_runtime_hook(qa_app_sim_grapple_runtime *r, qa_actor_id actor, qa_actor_id *out) {
  const qa_app_sim_grapple_source *s = &r->source;
  if (is(s->kind, "q1-threewave")) {
    qa_q1_actor *hook = qa_q1_threewave_hook(s->q1_core, actor);
    if (hook == NULL) return false;
    *out = hook->actor.id;
    return true;
  }
  if (is(s->kind, "q2-ctf")) {
    qa_ctf_grapple_state *state = qa_q2_ctf_grapple_find_state(s->ctf_core, actor);
    if (state == NULL || !state->has_grapple) return false;
    *out = state->grapple;
    return true;
  }
  if (is(s->kind, "q2-lmctf")) {
    qa_lmctf_grapple_state *state = qa_lmctf_grapple_find_state(s->lmctf_core, actor);
    if (state == NULL || !state->has_hook) return false;
    *out = state->hook;
    return true;
  }
  return qa_app_sim_qvm_grapple_source_hook(s->q3_game, actor, out);
}
bool qa_app_sim_grapple_runtime_pulling(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  const qa_app_sim_grapple_source *s = &r->source;
  if (is(s->kind, "q1-threewave")) return qa_q1_threewave_pulling(s->q1_core, actor);
  if (is(s->kind, "q2-ctf")) {
    qa_ctf_grapple_state *state = qa_q2_ctf_grapple_find_state(s->ctf_core, actor);
    return state != NULL && !is(state->grapple_state, "fly");
  }
  if (is(s->kind, "q2-lmctf")) {
    qa_lmctf_grapple_state *state = qa_lmctf_grapple_find_state(s->lmctf_core, actor);
    return state != NULL && state->hook_state == 2;
  }
  return qa_app_sim_qvm_grapple_source_pulling(s->q3_game, actor);
}
double qa_app_sim_grapple_runtime_gravity_scale(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  return is(r->source.kind, "q2-lmctf") ? qa_lmctf_grapple_gravity_scale(r->source.lmctf_core, actor) : 1;
}
void qa_app_sim_grapple_runtime_release(qa_app_sim_grapple_runtime *r, qa_actor_id actor) {
  control_entry *state = find_control(r, actor);
  if (state != NULL) {
    state->control.held = false; state->control.pressed = false; state->control.released = false;
    state->control.prediction_suppressed = false;
  }
  qa_actor_id hook;
  if (!qa_app_sim_grapple_runtime_hook(r, actor, &hook) && !is(r->source.kind, "q1-threewave")) return;
  const qa_app_sim_grapple_source *s = &r->source;
  if (is(s->kind, "q1-threewave")) qa_q1_threewave_release(s->q1_core, actor);
  else if (is(s->kind, "q2-ctf")) qa_q2_ctf_grapple_reset(s->ctf_core, actor, q2_game(r));
  else if (is(s->kind, "q2-lmctf")) qa_lmctf_grapple_abort(s->lmctf_core, actor, q2_game(r));
  else qa_qvm_grapple_provider_release(s->q3_core, actor, true);
}
void qa_app_sim_grapple_runtime_step(qa_app_sim_grapple_runtime *r, qa_actor_id actor, bool enabled) {
  control_entry *found = find_control(r, actor);
  if (found == NULL) return;
  qa_app_sim_grapple_control state = found->control;
  if (!enabled) { qa_app_sim_grapple_runtime_release(r, actor); return; }
  if (is(r->selection.binding, "slot")) { step_slot(r, actor); return; }
  const qa_app_sim_grapple_source *s = &r->source;
  if (state.released && is(s->kind, "q3-qvm")) {
    qa_app_sim_grapple_control next = state;
    next.pressed = false; next.released = false;
    set_control(r, actor, next);
    qa_app_sim_qvm_grapple_source_release(s->q3_game, actor);
    return;
  }
  if (state.released) { qa_app_sim_grapple_runtime_release(r, actor); return; }
  if (!state.held) return;
  qa_app_sim_grapple_control next = state;
  next.pressed = false; next.released = false;
  set_control(r, actor, next);
  if (is(s->kind, "q1-threewave")) {
    if (state.pressed) (void)qa_q1_threewave_fire(s->q1_core, actor);
    qa_q1_threewave_trail(s->q1_core, actor);
  } else if (is(s->kind, "q2-ctf")) {
    if (state.pressed) qa_q2_ctf_grapple_offhand(s->ctf_core, actor, q2_game(r), true);
    if (!qa_session_actor_registry_is_live(actors_of(r), actor)) return;
    qa_q2_ctf_grapple_player_frame(s->ctf_core, actor, q2_game(r), true);
  } else if (is(s->kind, "q2-lmctf")) {
    qa_lmctf_grapple_state *current = qa_lmctf_grapple_find_state(s->lmctf_core, actor);
    if (state.pressed || (current != NULL && current->has_hook)) qa_lmctf_grapple_fire(s->lmctf_core, actor, q2_game(r));
  } else if (state.pressed) qa_app_sim_qvm_grapple_source_fire(s->q3_game, actor);
}

void qa_app_sim_grapple_runtime_checkpoint_destroy(qa_app_sim_grapple_runtime_checkpoint *c) {
  free(c->weapon_animations); free(c->controls);
  qa_app_sim_grapple_source_checkpoint *s = &c->source;
  if (is(s->kind, "q1-threewave")) {
    if (s->has_q1_saved) qa_q1_foundation_checkpoint_free(&s->q1_saved);
    else qa_q1_checkpoint_free(&s->q1_entities);
  }
  qa_persist_decoded_free(&s->q2_owner);
  free(s->ctf_states); free(s->lmctf_states); free(s->holstered);
  if (is(s->kind, "q3-qvm")) qa_app_sim_qvm_grapple_source_checkpoint_destroy(&s->component);
  memset(c, 0, sizeof *c);
}

qa_app_sim_grapple_runtime_checkpoint qa_app_sim_grapple_runtime_capture(qa_app_sim_grapple_runtime *r) {
  qa_app_sim_grapple_runtime_checkpoint out;
  memset(&out, 0, sizeof out);
  out.version = 2;
  out.weapon_animations = xcalloc(r->q2_count, sizeof *out.weapon_animations);
  out.weapon_animation_count = r->q2_count;
  for (size_t i = 0; i < r->q2_count; i++) {
    out.weapon_animations[i].actor = saved(r->q2[i]->actor);
    out.weapon_animations[i].animation = r->q2[i]->animation;
  }
  out.controls = xcalloc(r->control_count, sizeof *out.controls);
  out.control_count = r->control_count;
  for (size_t i = 0; i < r->control_count; i++) {
    out.controls[i].actor = saved(r->controls[i].actor);
    out.controls[i].control = r->controls[i].control;
  }
  out.random = qa_app_sim_source_random_checkpoint(r->random);
  const qa_app_sim_grapple_source *s = &r->source;
  qa_app_sim_grapple_source_checkpoint *saved_source = &out.source;
  saved_source->kind = s->kind;
  if (is(s->kind, "q1-threewave")) {
    if (!qa_q1_services_capture(s->q1_game, &saved_source->q1_entities)) qa_range_error("Out of memory");
  } else if (is(s->kind, "q2-ctf")) {
    saved_source->q2_entities = qa_q2_entity_services_capture(s->q2_game);
    size_t count = qa_q2_ctf_grapple_state_count(s->ctf_core);
    saved_source->ctf_states = xcalloc(count, sizeof *saved_source->ctf_states);
    saved_source->ctf_state_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_actor_id actor;
      qa_ctf_grapple_state *state = qa_q2_ctf_grapple_state_at(s->ctf_core, i, &actor);
      saved_source->ctf_states[i].actor = saved(actor);
      saved_source->ctf_states[i].state = qa_capture_ctf_grapple(*state);
    }
  } else if (is(s->kind, "q2-lmctf")) {
    saved_source->q2_entities = qa_q2_entity_services_capture(s->q2_game);
    size_t count = qa_lmctf_grapple_state_count(s->lmctf_core);
    saved_source->lmctf_states = xcalloc(count, sizeof *saved_source->lmctf_states);
    saved_source->lmctf_state_count = count;
    for (size_t i = 0; i < count; i++) {
      qa_actor_id actor;
      qa_lmctf_grapple_state *state = qa_lmctf_grapple_state_at(s->lmctf_core, i, &actor);
      saved_source->lmctf_states[i].actor = saved(actor);
      saved_source->lmctf_states[i].state = qa_capture_lmctf_grapple(*state);
    }
  } else {
    saved_source->component = qa_app_sim_qvm_grapple_source_capture(s->q3_game);
    saved_source->holstered = xcalloc(r->holstered_count, sizeof *saved_source->holstered);
    saved_source->holstered_count = r->holstered_count;
    for (size_t i = 0; i < r->holstered_count; i++) saved_source->holstered[i] = saved(r->holstered[i]);
  }
  return out;
}

void qa_app_sim_grapple_runtime_restore(qa_app_sim_grapple_runtime *r, const qa_app_sim_grapple_runtime_checkpoint *checkpoint) {
  qa_app_sim_source_random_restore(r->random, checkpoint->random);
  const qa_app_sim_grapple_source *s = &r->source;
  const qa_app_sim_grapple_source_checkpoint *saved_source = &checkpoint->source;
  qa_session_actor_registry *actors = actors_of(r);
  if (is(s->kind, "q1-threewave") && is(saved_source->kind, "q1-threewave")) {
    /* The persistence reader still decodes its own Q1FoundationCheckpoint
     * copy (persistence/q1_foundation.h) with no conversion to the content
     * type, so a read Q1 checkpoint cannot restore the source game. */
    if (saved_source->has_q1_saved)
      qa_raise("Error", 0, 0, "Q1 foundation checkpoint from a save has no content conversion");
    qa_q1_services_restore(s->q1_game, &saved_source->q1_entities, false);
  } else if (is(s->kind, "q2-ctf") && is(saved_source->kind, "q2-ctf")) {
    qa_q2_entity_services_restore(s->q2_game, saved_source->q2_entities);
    qa_q2_ctf_grapple_clear_states(s->ctf_core);
    for (size_t i = 0; i < saved_source->ctf_state_count; i++) {
      qa_actor_id actor = qa_session_actor_registry_reference_saved(actors, saved_source->ctf_states[i].actor, "checkpoint");
      qa_ctf_grapple_state state = qa_restore_ctf_grapple(saved_source->ctf_states[i].state, q2_game(r));
      *qa_q2_ctf_grapple_state(s->ctf_core, actor) = state;
    }
  } else if (is(s->kind, "q2-lmctf") && is(saved_source->kind, "q2-lmctf")) {
    qa_q2_entity_services_restore(s->q2_game, saved_source->q2_entities);
    qa_lmctf_grapple_clear_states(s->lmctf_core);
    for (size_t i = 0; i < saved_source->lmctf_state_count; i++) {
      qa_actor_id actor = qa_session_actor_registry_reference_saved(actors, saved_source->lmctf_states[i].actor, "checkpoint");
      qa_lmctf_grapple_state state = qa_restore_lmctf_grapple(saved_source->lmctf_states[i].state, q2_game(r));
      *qa_lmctf_grapple_state_of(s->lmctf_core, actor) = state;
    }
  } else if (is(s->kind, "q3-qvm") && is(saved_source->kind, "q3-qvm")) {
    qa_app_sim_qvm_grapple_source_restore(s->q3_game, &saved_source->component);
    r->holstered_count = 0;
    for (size_t i = 0; i < saved_source->holstered_count; i++)
      holstered_add(r, qa_session_actor_registry_reference_saved(actors, saved_source->holstered[i], "checkpoint"));
  } else qa_raise("Error", 0, 0, "Saved grapple source differs from selected equipment");
  clear_q2(r);
  for (size_t i = 0; i < checkpoint->weapon_animation_count; i++) {
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(actors, checkpoint->weapon_animations[i].actor, &actor))
      qa_raise("Error", 0, 0, "Missing grapple animation owner");
    bind_weapon(r, actor.id, checkpoint->weapon_animations[i].animation);
  }
  r->control_count = 0;
  for (size_t i = 0; i < checkpoint->control_count; i++) {
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(actors, checkpoint->controls[i].actor, &actor) || find_control(r, actor.id) != NULL)
      qa_raise("Error", 0, 0, "Invalid saved grapple input owner");
    set_control(r, actor.id, checkpoint->controls[i].control);
  }
}
