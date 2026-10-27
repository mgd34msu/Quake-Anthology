/* Port of src/content/q2/equipment/grapple-weapon.ts. GPL-2.0-or-later.
 * Mirrored frame stepping (TS owner: foundation/weapons/generic-frame.ts). */
#include "qa/content/q2/equipment/grapple_weapon.h"

#include <math.h>
#include <string.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

static const double CTF_PAUSES[] = {10, 18, 27};
static const double CTF_FIRES[] = {6};
static const double LM_PAUSES[] = {14, 18, 26, 30};
static const double LM_FIRES[] = {8, 9, 10, 11};
const qa_q2f_weapon_definition QA_Q2_CTF_GRAPPLE = {
  "grapple", "q2:weapon_grapple", "weapon_grapple", NULL, 0, 0,
  "models/weapons/grapple/tris.md2", "", 12, 5, 9, 31, 36, CTF_PAUSES, 3, CTF_FIRES, 1, false};
const qa_q2f_weapon_definition QA_Q2_RERELEASE_CTF_GRAPPLE = {
  "grapple", "q2:weapon_grapple", "weapon_grapple", NULL, 0, 0,
  "models/weapons/grapple/tris.md2", "", 12, 5, 10, 31, 36, CTF_PAUSES, 3, CTF_FIRES, 1, false};
const qa_q2f_weapon_definition QA_LMCTF_GRAPPLE = {
  "lmctf:hook", "q2:weapon_hook", "weapon_hook", NULL, 0, 0,
  "models/weapons/v_hook/tris.md2", "models/objects/debris2/tris.md2", 11, 9, 13, 34, 38, LM_PAUSES, 4, LM_FIRES, 4, true};

static bool contains(const double *v, size_t n, double x) {
  for (size_t i = 0; i < n; i++) if (v[i] == x) return true;
  return false;
}
static double ms_sum(double time, double seconds) { return (round(time * 1000) + round(seconds * 1000)) / 1000; }

typedef struct frame_hooks {
  void *ctx;
  double (*random)(void *);
  double (*ammo)(void *);
  void (*no_ammo)(void *);
  void (*fire)(void *, bool);
  void (*change_weapon)(void *);
  void (*reverse_animation)(void *);
  void (*attack_animation)(void *);
  void (*powerup_sound)(void *);
  double (*animation_time)(void *);
  void (*prepare_drop)(void *);
} frame_hooks;
typedef struct classic_input { bool attack, change_requested; } classic_input;
typedef struct rerelease_input { bool attack, change_requested, instant_switch, holster, weapon_thunk; double now, frame_seconds; } rerelease_input;

static void step_classic(qa_q2f_generic_frame_state *s, const qa_q2f_weapon_definition *d, classic_input in, frame_hooks h) {
  double idle_first = d->fire_last + 1;
  if (strcmp(s->phase, "dropping") == 0) {
    if (s->frame == d->deactivate_last) { h.change_weapon(h.ctx); return; }
    if (d->deactivate_last - s->frame == 4) h.reverse_animation(h.ctx);
    s->frame++;
    return;
  }
  if (strcmp(s->phase, "activating") == 0) {
    if (s->frame == d->activate_last) { s->phase = "ready"; s->frame = idle_first; }
    else s->frame++;
    return;
  }
  if (in.change_requested && strcmp(s->phase, "firing") != 0) {
    s->phase = "dropping"; s->frame = d->idle_last + 1;
    if (d->deactivate_last - s->frame < 4) h.reverse_animation(h.ctx);
    return;
  }
  if (strcmp(s->phase, "ready") == 0) {
    if (in.attack || s->latched_attack) {
      s->latched_attack = false;
      if (h.ammo(h.ctx) < d->quantity) { h.no_ammo(h.ctx); return; }
      s->frame = d->activate_last + 1; s->phase = "firing";
      h.attack_animation(h.ctx);
    } else {
      if (s->frame == d->idle_last) { s->frame = idle_first; return; }
      if (contains(d->pauses, d->pause_count, s->frame) && floor(h.random(h.ctx) * 16) != 0) return;
      s->frame++;
      return;
    }
  }
  if (strcmp(s->phase, "firing") == 0) {
    if (contains(d->fires, d->fire_count, s->frame)) { s->source_firing = true; h.powerup_sound(h.ctx); h.fire(h.ctx, false); }
    else s->frame++;
    if (s->frame == idle_first + 1) s->phase = "ready";
  }
}

static void step_rerelease(qa_q2f_generic_frame_state *s, const qa_q2f_weapon_definition *d, rerelease_input in, frame_hooks h) {
  double now = in.now;
  double idle_first = d->fire_last + 1;
  double idle_last = strcmp(d->name, "bfg") == 0 ? 54 : d->idle_last;
  if (strcmp(s->phase, "dropping") == 0) {
    if (s->think_time <= now) {
      if (s->frame == d->deactivate_last) { h.change_weapon(h.ctx); return; }
      if (d->deactivate_last - s->frame == 4) h.reverse_animation(h.ctx);
      s->frame++;
      s->think_time = ms_sum(now, h.animation_time(h.ctx));
    }
    return;
  }
  if (strcmp(s->phase, "activating") == 0 && (s->think_time <= now || in.instant_switch)) {
    s->think_time = ms_sum(now, h.animation_time(h.ctx));
    if (s->frame == d->activate_last || in.instant_switch) {
      s->phase = "ready"; s->frame = idle_first; s->fire_buffered = false;
      s->fire_finished = in.instant_switch ? 0 : ms_sum(now, h.animation_time(h.ctx));
    } else s->frame++;
    return;
  }
  if ((in.change_requested || (!in.instant_switch && in.holster)) && strcmp(s->phase, "firing") != 0) {
    if (in.instant_switch || s->think_time <= now) {
      h.prepare_drop(h.ctx);
      s->phase = "dropping";
      if (in.instant_switch) { h.change_weapon(h.ctx); return; }
      s->frame = idle_last + 1;
      if (d->deactivate_last - s->frame < 4) h.reverse_animation(h.ctx);
      s->think_time = ms_sum(now, h.animation_time(h.ctx));
    }
    return;
  }
  if (strcmp(s->phase, "ready") == 0) {
    if ((s->fire_buffered || s->latched_attack || in.attack) && s->fire_finished <= now) {
      s->latched_attack = false; s->think_time = now;
      if (h.ammo(h.ctx) < d->quantity) { h.no_ammo(h.ctx); return; }
      s->phase = "firing"; s->last_firing_time = ms_sum(now, 2.5);
      if (!d->repeating) {
        s->frame = d->activate_last + 1; s->fire_buffered = false;
        s->think_time = ms_sum(s->think_time, (in.weapon_thunk ? in.frame_seconds : 0) + h.animation_time(h.ctx));
        s->fire_finished = ms_sum(now, h.animation_time(h.ctx));
        if (contains(d->fires, d->fire_count, s->frame)) { h.powerup_sound(h.ctx); h.fire(h.ctx, false); }
        h.attack_animation(h.ctx);
        return;
      }
    } else if (s->think_time <= now) {
      s->think_time = ms_sum(now, h.animation_time(h.ctx));
      if (s->frame == idle_last) { s->frame = idle_first; return; }
      if (!contains(d->pauses, d->pause_count, s->frame) || floor(h.random(h.ctx) * 16) == 0) s->frame++;
      return;
    }
  }
  if (strcmp(s->phase, "firing") == 0 && s->think_time <= now) {
    s->last_firing_time = ms_sum(now, 2.5);
    if (!d->repeating) s->frame++;
    s->fire_finished = ms_sum(now, h.animation_time(h.ctx));
    bool buffered = s->fire_buffered;
    s->fire_buffered = false;
    if (d->repeating) h.fire(h.ctx, buffered);
    else if (contains(d->fires, d->fire_count, s->frame) && !(strcmp(d->name, "shotgun") == 0 && s->frame == 9)) { h.powerup_sound(h.ctx); h.fire(h.ctx, buffered); }
    if (s->frame == idle_first) { s->phase = "ready"; s->fire_buffered = false; }
    s->think_time = ms_sum(now, h.animation_time(h.ctx) + (d->repeating && in.weapon_thunk ? in.frame_seconds : 0));
  }
}

void qa_step_ctf_grapple_weapon(qa_q2f_generic_frame_state *s, qa_ctf_grapple_state *src, qa_grapple_weapon_input in, qa_q2_edition ed, qa_grapple_step_hooks h) {
  bool rerelease = ed == QA_Q2_EDITION_RERELEASE;
  bool held = in.attack || (rerelease && in.holster);
  if (held && strcmp(s->phase, "firing") == 0 && src->has_grapple) s->frame = rerelease ? 6 : 9;
  if (!held && src->has_grapple) { h.reset(h.ctx); if (strcmp(s->phase, "firing") == 0) s->phase = "ready"; }
  if ((in.change_requested || (rerelease && (in.holster || in.latched_holster))) && strcmp(src->grapple_state, "fly") != 0 && strcmp(s->phase, "firing") == 0) {
    h.prepare_drop(h.ctx);
    s->phase = "dropping"; s->frame = 32;
  }
  const char *before = s->phase;
  h.generic(h.ctx);
  if (rerelease && held && strcmp(s->phase, "firing") == 0 && src->has_grapple) s->frame = 6;
  if (strcmp(before, "activating") == 0 && strcmp(s->phase, "ready") == 0 && strcmp(src->grapple_state, "fly") != 0) {
    s->frame = held ? 5 : (rerelease ? 6 : 9);
    s->phase = "firing";
  }
}

void qa_step_lmctf_grapple_weapon(qa_q2f_generic_frame_state *s, qa_lmctf_grapple_state *src, qa_grapple_weapon_input in, qa_grapple_step_hooks h) {
  if (strcmp(s->phase, "activating") == 0) s->frame++;
  if (in.change_requested && strcmp(s->phase, "dropping") != 0) { s->phase = "dropping"; s->frame = 36; return; }
  if (!in.attack && !s->latched_attack && !src->hook_held) h.abort(h.ctx);
  h.generic(h.ctx);
}

void qa_fire_ctf_grapple_weapon(qa_actor_id a, qa_q2_game_services *g, qa_q2_ctf_grapple_equipment *core, qa_q2f_generic_frame_state *s, qa_q2_edition ed, qa_grapple_weapon_presentation pres) {
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

void qa_fire_lmctf_grapple_weapon(qa_actor_id a, qa_q2_game_services *g, qa_lmctf_grapple_equipment *core, qa_q2f_generic_frame_state *s, qa_grapple_weapon_presentation pres) {
  s->source_firing = qa_lmctf_grapple_state_of(core, a)->hook_state == 0;
  if (s->source_firing) {
    qa_grapple_hooks hooks = qa_lmctf_grapple_hooks(core);
    qa_grapple_pose pose = hooks.pose(hooks.ctx, a, g);
    pres.kick(pres.ctx, qa_q2_scale(qa_math_angle_vectors_rule(pose.angles, QA_ANGLE_RULE_NATIVE).forward, -2), -1);
  }
  qa_lmctf_grapple_fire(core, a, g);
}

void qa_release_lmctf_grapple_weapon(qa_q2f_generic_frame_state *s) {
  if (strcmp(s->phase, "firing") == 0) s->phase = "ready";
}
bool qa_ctf_grapple_weapon_should_reset(qa_q2f_generic_frame_state *s, bool selected, bool change_requested) {
  return selected && !change_requested && strcmp(s->phase, "firing") != 0 && strcmp(s->phase, "activating") != 0;
}

qa_grapple_weapon_state qa_create_grapple_weapon_state(void) {
  return (qa_grapple_weapon_state){.animation = {.phase = "activating", .frame = 0, .latched_attack = false,
      .source_firing = false, .fire_buffered = false, .think_time = 0, .fire_finished = 0, .last_firing_time = 0},
    .handoff = "holstered"};
}

void qa_q2_grapple_weapon_init(qa_q2_grapple_weapon *w, qa_actor_id a, qa_q2_game_services *g, qa_grapple_weapon_source src, qa_grapple_weapon_state *state, qa_grapple_weapon_presentation pres) {
  w->actor = a; w->game = g; w->source = src; w->state = state; w->presentation = pres;
}
const qa_q2f_weapon_definition *qa_q2_grapple_weapon_definition(const qa_q2_grapple_weapon *w) {
  return strcmp(w->source.kind, "lmctf") == 0 ? &QA_LMCTF_GRAPPLE
      : w->source.edition == QA_Q2_EDITION_RERELEASE ? &QA_Q2_RERELEASE_CTF_GRAPPLE : &QA_Q2_CTF_GRAPPLE;
}
void qa_q2_grapple_weapon_holster(qa_q2_grapple_weapon *w) {
  if (strcmp(w->state->handoff, "active") == 0) w->state->handoff = "holstering";
}
bool qa_q2_grapple_weapon_is_holstered(const qa_q2_grapple_weapon *w) { return strcmp(w->state->handoff, "holstered") == 0; }
void qa_q2_grapple_weapon_released(qa_q2_grapple_weapon *w) {
  if (strcmp(w->source.kind, "lmctf") == 0) qa_release_lmctf_grapple_weapon(&w->state->animation);
}
void qa_q2_grapple_weapon_resume(qa_q2_grapple_weapon *w) {
  w->state->handoff = "active";
  w->state->animation.phase = "activating";
  w->state->animation.frame = 0;
}
void qa_q2_grapple_weapon_player_frame(qa_q2_grapple_weapon *w) {
  if (strcmp(w->source.kind, "ctf") == 0) {
    if (qa_ctf_grapple_weapon_should_reset(&w->state->animation, strcmp(w->state->handoff, "active") == 0, strcmp(w->state->handoff, "holstering") == 0)) {
      qa_q2_ctf_grapple_reset(w->source.ctf, w->actor, w->game);
      return;
    }
    qa_q2_ctf_grapple_player_frame(w->source.ctf, w->actor, w->game, true);
    return;
  }
  if (qa_lmctf_grapple_state_of(w->source.lmctf, w->actor)->hook_state != 0) qa_lmctf_grapple_fire(w->source.lmctf, w->actor, w->game);
}

typedef struct weapon_ctx { qa_q2_grapple_weapon *w; } weapon_ctx;
static double fw_random(void *c) { weapon_ctx *x = c; return x->w->game->host->random(x->w->game->host->ctx); }
static double fw_ammo(void *c) { (void)c; return 0; }
static void fw_no_ammo(void *c) { (void)c; qa_range_error("An ammunition-free grapple cannot run out of ammo"); }
static void fw_fire_cb(void *c, bool buffered) {
  (void)buffered;
  weapon_ctx *x = c;
  qa_q2_grapple_weapon *w = x->w;
  if (strcmp(w->source.kind, "ctf") == 0)
    qa_fire_ctf_grapple_weapon(w->actor, w->game, w->source.ctf, &w->state->animation, w->source.edition, w->presentation);
  else qa_fire_lmctf_grapple_weapon(w->actor, w->game, w->source.lmctf, &w->state->animation, w->presentation);
}
static void fw_change(void *c) { weapon_ctx *x = c; x->w->state->handoff = "holstered"; }
static void fw_reverse(void *c) { weapon_ctx *x = c; x->w->presentation.reverse_animation(x->w->presentation.ctx); }
static void fw_attack(void *c) { weapon_ctx *x = c; x->w->presentation.attack_animation(x->w->presentation.ctx); }
static void fw_powerup(void *c) { weapon_ctx *x = c; x->w->presentation.powerup_sound(x->w->presentation.ctx); }
static double fw_anim_time(void *c) { weapon_ctx *x = c; return x->w->presentation.animation_time(x->w->presentation.ctx, &x->w->state->animation); }
static void fw_prepare_drop(void *c) { weapon_ctx *x = c; qa_q2_grapple_weapon_holster(x->w); }

typedef struct generic_ctx { qa_q2_grapple_weapon *w; qa_grapple_weapon_step_input in; } generic_ctx;
static void generic_step(void *c) {
  generic_ctx *gc = c;
  qa_q2_grapple_weapon *w = gc->w;
  qa_q2f_generic_frame_state *s = &w->state->animation;
  weapon_ctx wc = {w};
  frame_hooks h = {&wc, fw_random, fw_ammo, fw_no_ammo, fw_fire_cb, fw_change, fw_reverse, fw_attack, fw_powerup, fw_anim_time, fw_prepare_drop};
  bool change = strcmp(w->state->handoff, "holstering") == 0;
  if (strcmp(w->source.kind, "ctf") == 0 && w->source.edition == QA_Q2_EDITION_RERELEASE) {
    rerelease_input in = {gc->in.attack, change, gc->in.instant_switch, gc->in.holster, gc->in.weapon_thunk, gc->in.now, gc->in.frame_seconds};
    step_rerelease(s, qa_q2_grapple_weapon_definition(w), in, h);
    return;
  }
  const char *before = s->phase;
  if (strcmp(w->source.kind, "lmctf") == 0) s->source_firing = false;
  classic_input in = {gc->in.attack, change};
  step_classic(s, qa_q2_grapple_weapon_definition(w), in, h);
  if (strcmp(w->source.kind, "ctf") == 0 && strcmp(s->phase, "firing") != 0 && before == s->phase && !qa_q2_grapple_weapon_is_holstered(w))
    step_classic(s, qa_q2_grapple_weapon_definition(w), in, h);
}
static void reset_cb(void *c) {
  generic_ctx *gc = c;
  qa_q2_ctf_grapple_reset(gc->w->source.ctf, gc->w->actor, gc->w->game);
}
static void abort_cb(void *c) {
  generic_ctx *gc = c;
  qa_lmctf_grapple_abort(gc->w->source.lmctf, gc->w->actor, gc->w->game);
}

static void prepare_drop_cb(void *c) {
  generic_ctx *gc = c;
  qa_q2_grapple_weapon_holster(gc->w);
}

void qa_q2_grapple_weapon_step(qa_q2_grapple_weapon *w, qa_grapple_weapon_step_input in) {
  if (qa_q2_grapple_weapon_is_holstered(w)) return;
  generic_ctx gc = {w, in};
  qa_grapple_weapon_input frame_input = {in.attack, strcmp(w->state->handoff, "holstering") == 0, in.holster, in.latched_holster};
  if (strcmp(w->source.kind, "ctf") == 0) {
    qa_step_ctf_grapple_weapon(&w->state->animation, qa_q2_ctf_grapple_state(w->source.ctf, w->actor), frame_input, w->source.edition,
        (qa_grapple_step_hooks){.ctx = &gc, .reset = reset_cb, .prepare_drop = prepare_drop_cb, .generic = generic_step});
    return;
  }
  qa_step_lmctf_grapple_weapon(&w->state->animation, qa_lmctf_grapple_state_of(w->source.lmctf, w->actor), frame_input,
      (qa_grapple_step_hooks){.ctx = &gc, .abort = abort_cb, .generic = generic_step});
}
