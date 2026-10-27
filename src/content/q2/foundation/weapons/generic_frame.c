/* Port of src/content/q2/foundation/weapons/generic-frame.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/generic_frame.h"
#include <math.h>
#include <string.h>

double qa_q2w_millisecond_sum(double time, double seconds) {
  return (round(time * 1000) + round(seconds * 1000)) / 1000;
}
static bool includes(const double *values, size_t count, double frame) {
  for (size_t i = 0; i < count; i++) if (values[i] == frame) return true;
  return false;
}

void qa_q2w_step_classic_frame(qa_q2w_state *s, const qa_q2w_definition *d,
                               qa_q2w_classic_frame_input in, const qa_q2w_frame_hooks *h) {
  double idle_first = d->fire_last + 1;
  if (s->phase == QA_Q2W_DROPPING) {
    if (s->frame == d->deactivate_last) { h->change_weapon(h->ctx); return; }
    if (d->deactivate_last - s->frame == 4) h->reverse_animation(h->ctx);
    s->frame++; return;
  }
  if (s->phase == QA_Q2W_ACTIVATING) {
    if (s->frame == d->activate_last) { s->phase = QA_Q2W_READY; s->frame = idle_first; }
    else s->frame++;
    return;
  }
  if (in.change_requested && s->phase != QA_Q2W_FIRING) {
    s->phase = QA_Q2W_DROPPING; s->frame = d->idle_last + 1;
    if (d->deactivate_last - s->frame < 4) h->reverse_animation(h->ctx);
    return;
  }
  if (s->phase == QA_Q2W_READY) {
    if (in.attack || s->latched_attack) {
      s->latched_attack = false;
      if (h->ammo(h->ctx) < d->quantity) { h->no_ammo(h->ctx); return; }
      s->frame = d->activate_last + 1; s->phase = QA_Q2W_FIRING;
      h->attack_animation(h->ctx);
    } else {
      if (s->frame == d->idle_last) { s->frame = idle_first; return; }
      if (includes(d->pauses, d->pause_count, s->frame) && floor(h->random(h->ctx) * 16) != 0) return;
      s->frame++; return;
    }
  }
  if (s->phase == QA_Q2W_FIRING) {
    if (includes(d->fires, d->fire_count, s->frame)) { s->source_firing = true; h->powerup_sound(h->ctx); h->fire(h->ctx, false); }
    else s->frame++;
    if (s->frame == idle_first + 1) s->phase = QA_Q2W_READY;
  }
}

void qa_q2w_step_rerelease_frame(qa_q2w_state *s, const qa_q2w_definition *d,
                                 qa_q2w_rerelease_frame_input in, const qa_q2w_frame_hooks *h) {
  double now = in.now;
  double idle_first = d->fire_last + 1;
  double idle_last = strcmp(d->name, "bfg") == 0 ? 54 : d->idle_last;
  if (s->phase == QA_Q2W_DROPPING) {
    if (s->think_time <= now) {
      if (s->frame == d->deactivate_last) { h->change_weapon(h->ctx); return; }
      if (d->deactivate_last - s->frame == 4) h->reverse_animation(h->ctx);
      s->frame++; s->think_time = qa_q2w_millisecond_sum(now, h->animation_time(h->ctx));
    }
    return;
  }
  if (s->phase == QA_Q2W_ACTIVATING) {
    if (s->think_time <= now || in.instant_switch) {
      s->think_time = qa_q2w_millisecond_sum(now, h->animation_time(h->ctx));
      if (s->frame == d->activate_last || in.instant_switch) {
        s->phase = QA_Q2W_READY; s->frame = idle_first; s->fire_buffered = false;
        s->fire_finished = in.instant_switch ? 0 : qa_q2w_millisecond_sum(now, h->animation_time(h->ctx));
      } else s->frame++;
      return;
    }
  }
  if ((in.change_requested || (!in.instant_switch && in.holster)) && s->phase != QA_Q2W_FIRING) {
    if (in.instant_switch || s->think_time <= now) {
      h->prepare_drop(h->ctx);
      s->phase = QA_Q2W_DROPPING;
      if (in.instant_switch) { h->change_weapon(h->ctx); return; }
      s->frame = idle_last + 1;
      if (d->deactivate_last - s->frame < 4) h->reverse_animation(h->ctx);
      s->think_time = qa_q2w_millisecond_sum(now, h->animation_time(h->ctx));
    }
    return;
  }
  if (s->phase == QA_Q2W_READY) {
    if ((s->fire_buffered || s->latched_attack || in.attack) && s->fire_finished <= now) {
      s->latched_attack = false; s->think_time = now;
      if (h->ammo(h->ctx) < d->quantity) { h->no_ammo(h->ctx); return; }
      s->phase = QA_Q2W_FIRING; s->last_firing_time = qa_q2w_millisecond_sum(now, 2.5);
      if (!d->repeating) {
        s->frame = d->activate_last + 1; s->fire_buffered = false;
        s->think_time = qa_q2w_millisecond_sum(s->think_time, (in.weapon_thunk ? in.frame_seconds : 0) + h->animation_time(h->ctx));
        s->fire_finished = qa_q2w_millisecond_sum(now, h->animation_time(h->ctx));
        if (includes(d->fires, d->fire_count, s->frame)) { h->powerup_sound(h->ctx); h->fire(h->ctx, false); }
        h->attack_animation(h->ctx);
        return;
      }
    } else if (s->think_time <= now) {
      s->think_time = qa_q2w_millisecond_sum(now, h->animation_time(h->ctx));
      if (s->frame == idle_last) { s->frame = idle_first; return; }
      if (!includes(d->pauses, d->pause_count, s->frame) || floor(h->random(h->ctx) * 16) == 0) s->frame++;
      return;
    }
  }
  if (s->phase == QA_Q2W_FIRING && s->think_time <= now) {
    s->last_firing_time = qa_q2w_millisecond_sum(now, 2.5);
    if (!d->repeating) s->frame++;
    s->fire_finished = qa_q2w_millisecond_sum(now, h->animation_time(h->ctx));
    bool buffered = s->fire_buffered;
    s->fire_buffered = false;
    if (d->repeating) h->fire(h->ctx, buffered);
    else if (includes(d->fires, d->fire_count, s->frame) && !(strcmp(d->name, "shotgun") == 0 && s->frame == 9)) {
      h->powerup_sound(h->ctx); h->fire(h->ctx, buffered);
    }
    if (s->frame == idle_first) { s->phase = QA_Q2W_READY; s->fire_buffered = false; }
    s->think_time = qa_q2w_millisecond_sum(now, h->animation_time(h->ctx) + (d->repeating && in.weapon_thunk ? in.frame_seconds : 0));
  }
}
