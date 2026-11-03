/* Port of src/content/q2/equipment/grapple-weapon.ts. GPL-2.0-or-later.
 * Frame stepping is the foundation generic-frame port. */
#include "qa/content/q2/equipment/grapple_weapon.h"

#include <string.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

static const double CTF_PAUSES[] = {10, 18, 27};
static const double CTF_FIRES[] = {6};
static const double LM_PAUSES[] = {14, 18, 26, 30};
static const double LM_FIRES[] = {8, 9, 10, 11};
const qa_q2w_definition QA_Q2_CTF_GRAPPLE = {
  .name = "grapple", .item = "q2:weapon_grapple", .classname = "weapon_grapple", .ammo = NULL, .quantity = 0, .warning = 0,
  .view_model = "models/weapons/grapple/tris.md2", .world_model = "", .player_model = 12,
  .activate_last = 5, .fire_last = 9, .idle_last = 31, .deactivate_last = 36,
  .pauses = CTF_PAUSES, .pause_count = 3, .fires = CTF_FIRES, .fire_count = 1, .repeating = false};
/* TS { ...Q2_CTF_GRAPPLE, fireLast: 10, noHaste: true }. */
const qa_q2w_definition QA_Q2_RERELEASE_CTF_GRAPPLE = {
  .name = "grapple", .item = "q2:weapon_grapple", .classname = "weapon_grapple", .ammo = NULL, .quantity = 0, .warning = 0,
  .view_model = "models/weapons/grapple/tris.md2", .world_model = "", .player_model = 12,
  .activate_last = 5, .fire_last = 10, .idle_last = 31, .deactivate_last = 36,
  .pauses = CTF_PAUSES, .pause_count = 3, .fires = CTF_FIRES, .fire_count = 1, .repeating = false, .no_haste = true};
const qa_q2w_definition QA_LMCTF_GRAPPLE = {
  .name = "lmctf:hook", .item = "q2:weapon_hook", .classname = "weapon_hook", .ammo = NULL, .quantity = 0, .warning = 0,
  .view_model = "models/weapons/v_hook/tris.md2", .world_model = "models/objects/debris2/tris.md2", .player_model = 11,
  .activate_last = 9, .fire_last = 13, .idle_last = 34, .deactivate_last = 38,
  .pauses = LM_PAUSES, .pause_count = 4, .fires = LM_FIRES, .fire_count = 4, .repeating = true};

void qa_step_ctf_grapple_weapon(qa_q2w_state *s, qa_ctf_grapple_state *src, qa_grapple_weapon_input in, qa_q2_edition ed, qa_grapple_step_hooks h) {
  bool rerelease = ed == QA_Q2_EDITION_RERELEASE;
  bool held = in.attack || (rerelease && in.holster);
  if (held && s->phase == QA_Q2W_FIRING && src->has_grapple) s->frame = rerelease ? 6 : 9;
  if (!held && src->has_grapple) { h.reset(h.ctx); if (s->phase == QA_Q2W_FIRING) s->phase = QA_Q2W_READY; }
  if ((in.change_requested || (rerelease && (in.holster || in.latched_holster))) && strcmp(src->grapple_state, "fly") != 0 && s->phase == QA_Q2W_FIRING) {
    h.prepare_drop(h.ctx);
    s->phase = QA_Q2W_DROPPING; s->frame = 32;
  }
  qa_q2w_phase before = s->phase;
  h.generic(h.ctx);
  if (rerelease && held && s->phase == QA_Q2W_FIRING && src->has_grapple) s->frame = 6;
  if (before == QA_Q2W_ACTIVATING && s->phase == QA_Q2W_READY && strcmp(src->grapple_state, "fly") != 0) {
    s->frame = held ? 5 : (rerelease ? 6 : 9);
    s->phase = QA_Q2W_FIRING;
  }
}

void qa_step_lmctf_grapple_weapon(qa_q2w_state *s, qa_lmctf_grapple_state *src, qa_grapple_weapon_input in, qa_grapple_step_hooks h) {
  if (s->phase == QA_Q2W_ACTIVATING) s->frame++;
  if (in.change_requested && s->phase != QA_Q2W_DROPPING) { s->phase = QA_Q2W_DROPPING; s->frame = 36; return; }
  if (!in.attack && !s->latched_attack && !src->hook_held) h.abort(h.ctx);
  h.generic(h.ctx);
}

void qa_fire_ctf_grapple_weapon(qa_actor_id a, qa_q2_game_services *g, qa_q2_ctf_grapple_equipment *core, qa_q2w_state *s, qa_q2_edition ed, qa_grapple_weapon_presentation pres) {
  qa_ctf_grapple_state *st = qa_q2_ctf_grapple_state(core, a);
  if (strcmp(st->grapple_state, "fly") != 0) {
    if (ed == QA_Q2_EDITION_CLASSIC) s->frame++;
    return;
  }
  if (ed == QA_Q2_EDITION_CLASSIC) {
    qa_grapple_hooks hooks = qa_q2_ctf_grapple_hooks(core);
    qa_grapple_pose pose = hooks.pose(hooks.ctx, a, g);
    pres.kick(pres.ctx, qa_q2_scale(qa_math_angle_vectors_rule(pose.angles, QA_ANGLE_RULE_NATIVE).forward, -2), -1);
  }
  qa_q2_ctf_grapple_fire_from_pose(core, a, g);
  if (ed == QA_Q2_EDITION_CLASSIC && qa_session_actor_registry_is_live(g->host->actors, a)) s->frame++;
}

void qa_fire_lmctf_grapple_weapon(qa_actor_id a, qa_q2_game_services *g, qa_lmctf_grapple_equipment *core, qa_q2w_state *s, qa_grapple_weapon_presentation pres) {
  s->source_firing = qa_lmctf_grapple_state_of(core, a)->hook_state == 0;
  if (s->source_firing) {
    qa_grapple_hooks hooks = qa_lmctf_grapple_hooks(core);
    qa_grapple_pose pose = hooks.pose(hooks.ctx, a, g);
    pres.kick(pres.ctx, qa_q2_scale(qa_math_angle_vectors_rule(pose.angles, QA_ANGLE_RULE_NATIVE).forward, -2), -1);
  }
  qa_lmctf_grapple_fire(core, a, g);
}

void qa_release_lmctf_grapple_weapon(qa_q2w_state *s) {
  if (s->phase == QA_Q2W_FIRING) s->phase = QA_Q2W_READY;
}
bool qa_ctf_grapple_weapon_should_reset(const qa_q2w_state *s, bool selected, bool change_requested) {
  return selected && !change_requested && s->phase != QA_Q2W_FIRING && s->phase != QA_Q2W_ACTIVATING;
}

qa_grapple_weapon_state qa_create_grapple_weapon_state(void) {
  qa_grapple_weapon_state out;
  memset(&out, 0, sizeof out);
  out.animation.phase = QA_Q2W_ACTIVATING;
  out.animation.frame = 0;
  out.animation.latched_attack = false;
  out.animation.source_firing = false;
  out.animation.think_time = 0;
  out.animation.fire_finished = 0;
  out.animation.fire_buffered = false;
  out.animation.last_firing_time = 0;
  out.handoff = QA_Q2W_HANDOFF_HOLSTERED;
  return out;
}

void qa_q2_grapple_weapon_init(qa_q2_grapple_weapon *w, qa_actor_id a, qa_q2_game_services *g, qa_grapple_weapon_source src, qa_grapple_weapon_state *state, qa_grapple_weapon_presentation pres) {
  w->actor = a; w->game = g; w->source = src; w->state = state; w->presentation = pres;
}
const qa_q2w_definition *qa_q2_grapple_weapon_definition(const qa_q2_grapple_weapon *w) {
  return strcmp(w->source.kind, "lmctf") == 0 ? &QA_LMCTF_GRAPPLE
      : w->source.edition == QA_Q2_EDITION_RERELEASE ? &QA_Q2_RERELEASE_CTF_GRAPPLE : &QA_Q2_CTF_GRAPPLE;
}
void qa_q2_grapple_weapon_holster(qa_q2_grapple_weapon *w) {
  if (w->state->handoff == QA_Q2W_HANDOFF_ACTIVE) w->state->handoff = QA_Q2W_HANDOFF_HOLSTERING;
}
bool qa_q2_grapple_weapon_is_holstered(const qa_q2_grapple_weapon *w) { return w->state->handoff == QA_Q2W_HANDOFF_HOLSTERED; }
void qa_q2_grapple_weapon_released(qa_q2_grapple_weapon *w) {
  if (strcmp(w->source.kind, "lmctf") == 0) qa_release_lmctf_grapple_weapon(&w->state->animation);
}
void qa_q2_grapple_weapon_resume(qa_q2_grapple_weapon *w) {
  w->state->handoff = QA_Q2W_HANDOFF_ACTIVE;
  w->state->animation.phase = QA_Q2W_ACTIVATING;
  w->state->animation.frame = 0;
}
void qa_q2_grapple_weapon_player_frame(qa_q2_grapple_weapon *w) {
  if (strcmp(w->source.kind, "ctf") == 0) {
    if (qa_ctf_grapple_weapon_should_reset(&w->state->animation, w->state->handoff == QA_Q2W_HANDOFF_ACTIVE,
                                           w->state->handoff == QA_Q2W_HANDOFF_HOLSTERING)) {
      qa_q2_ctf_grapple_reset(w->source.ctf, w->actor, w->game);
      return;
    }
    qa_q2_ctf_grapple_player_frame(w->source.ctf, w->actor, w->game, true);
    return;
  }
  if (qa_lmctf_grapple_state_of(w->source.lmctf, w->actor)->hook_state != 0) qa_lmctf_grapple_fire(w->source.lmctf, w->actor, w->game);
}

/* TS step(): frameInput is fixed at entry ({ ...input, changeRequested }),
 * so a prepareDrop inside the step does not change it. */
typedef struct step_ctx { qa_q2_grapple_weapon *w; qa_q2w_rerelease_frame_input frame; } step_ctx;
static double fw_random(void *c) { step_ctx *x = c; return x->w->game->host->random(x->w->game->host->ctx); }
static double fw_ammo(void *c) { (void)c; return 0; }
static void fw_no_ammo(void *c) { (void)c; qa_raise("Error", 0, 0, "An ammunition-free grapple cannot run out of ammo"); }
static void fw_fire(void *c, bool buffered) {
  (void)buffered; /* TS fire: () => this.fire() ignores the buffered flag. */
  step_ctx *x = c;
  qa_q2_grapple_weapon *w = x->w;
  if (strcmp(w->source.kind, "ctf") == 0)
    qa_fire_ctf_grapple_weapon(w->actor, w->game, w->source.ctf, &w->state->animation, w->source.edition, w->presentation);
  else qa_fire_lmctf_grapple_weapon(w->actor, w->game, w->source.lmctf, &w->state->animation, w->presentation);
}
static void fw_change(void *c) { step_ctx *x = c; x->w->state->handoff = QA_Q2W_HANDOFF_HOLSTERED; }
static void fw_reverse(void *c) { step_ctx *x = c; x->w->presentation.reverse_animation(x->w->presentation.ctx); }
static void fw_attack(void *c) { step_ctx *x = c; x->w->presentation.attack_animation(x->w->presentation.ctx); }
static void fw_powerup(void *c) { step_ctx *x = c; x->w->presentation.powerup_sound(x->w->presentation.ctx); }
static double fw_anim_time(void *c) { step_ctx *x = c; return x->w->presentation.animation_time(x->w->presentation.ctx, &x->w->state->animation); }
static void fw_prepare_drop(void *c) { step_ctx *x = c; qa_q2_grapple_weapon_holster(x->w); }

static void generic_step(void *c) {
  step_ctx *x = c;
  qa_q2_grapple_weapon *w = x->w;
  qa_q2w_state *s = &w->state->animation;
  const qa_q2w_frame_hooks hooks = {x, fw_random, fw_ammo, fw_no_ammo, fw_fire, fw_change, fw_reverse, fw_attack,
                                     fw_powerup, fw_anim_time, fw_prepare_drop};
  const qa_q2w_definition *definition = qa_q2_grapple_weapon_definition(w);
  if (strcmp(w->source.kind, "ctf") == 0 && w->source.edition == QA_Q2_EDITION_RERELEASE) {
    qa_q2w_step_rerelease_frame(s, definition, x->frame, &hooks);
    return;
  }
  qa_q2w_phase before = s->phase;
  if (strcmp(w->source.kind, "lmctf") == 0) s->source_firing = false;
  qa_q2w_classic_frame_input classic = {x->frame.attack, x->frame.change_requested};
  qa_q2w_step_classic_frame(s, definition, classic, &hooks);
  if (strcmp(w->source.kind, "ctf") == 0 && s->phase != QA_Q2W_FIRING && before == s->phase && !qa_q2_grapple_weapon_is_holstered(w))
    qa_q2w_step_classic_frame(s, definition, classic, &hooks);
}
static void reset_cb(void *c) {
  step_ctx *x = c;
  qa_q2_ctf_grapple_reset(x->w->source.ctf, x->w->actor, x->w->game);
}
static void abort_cb(void *c) {
  step_ctx *x = c;
  qa_lmctf_grapple_abort(x->w->source.lmctf, x->w->actor, x->w->game);
}

void qa_q2_grapple_weapon_step(qa_q2_grapple_weapon *w, qa_grapple_weapon_step_input in) {
  if (qa_q2_grapple_weapon_is_holstered(w)) return;
  step_ctx x = {w, {.attack = in.attack, .change_requested = w->state->handoff == QA_Q2W_HANDOFF_HOLSTERING,
                    .now = in.now, .frame_seconds = in.frame_seconds, .instant_switch = in.instant_switch,
                    .holster = in.holster, .weapon_thunk = in.weapon_thunk}};
  qa_grapple_weapon_input frame_input = {in.attack, x.frame.change_requested, in.holster, in.latched_holster};
  if (strcmp(w->source.kind, "ctf") == 0) {
    qa_step_ctf_grapple_weapon(&w->state->animation, qa_q2_ctf_grapple_state(w->source.ctf, w->actor), frame_input, w->source.edition,
        (qa_grapple_step_hooks){.ctx = &x, .reset = reset_cb, .prepare_drop = fw_prepare_drop, .generic = generic_step});
    return;
  }
  qa_step_lmctf_grapple_weapon(&w->state->animation, qa_lmctf_grapple_state_of(w->source.lmctf, w->actor), frame_input,
      (qa_grapple_step_hooks){.ctx = &x, .abort = abort_cb, .generic = generic_step});
}
