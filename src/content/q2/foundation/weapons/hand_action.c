/* Port of src/content/q2/foundation/weapons/hand-action.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/hand_action.h"
#include <math.h>

static qa_q2w_hand_action mk(qa_q2w_hand_action_kind k) { qa_q2w_hand_action a = {0}; a.kind = k; return a; }
static double firing_interval(const qa_q2w_hand_action_host *h, double seconds) {
  double r = h->firing_interval ? h->firing_interval(h->ctx, seconds) : seconds;
  if (!isfinite(r) || r < 0) qa_q2f_error("Original hand grenade cadence must produce a finite nonnegative interval");
  return r;
}
static qa_q2w_hand_tempo tempo_of(qa_q2w_hand_action_input in) {
  qa_q2w_hand_tempo t = {in.edition, in.haste, in.quad_fire};
  return t;
}
static qa_q2w_hand_action emit(double expires_at, qa_q2w_hand_action_input in, const qa_q2w_hand_action_host *h, bool held);
static double frame_deadline(qa_q2w_hand_action_input in, const qa_q2w_hand_action_host *h, double from) {
  return (round(from * 1000) + round(firing_interval(h, qa_q2w_hand_frame_seconds(tempo_of(in))) * 1000)) / 1000;
}
static qa_q2w_hand_action emit(double expires_at, qa_q2w_hand_action_input in, const qa_q2w_hand_action_host *h, bool held) {
  h->sound(h->ctx, "cook-stop");
  h->consume(h->ctx);
  qa_q2w_hand_throw_input ti;
  ti.angles = in.throw_angles; ti.damage_multiplier = in.throw_damage_multiplier; ti.gravity = in.throw_gravity;
  ti.project = in.throw_project;
  ti.edition = in.edition; ti.now = in.now;
  ti.alive = in.lifecycle == QA_Q2W_ALIVE || in.lifecycle == QA_Q2W_REMOVING;
  ti.fuse_deadline = in.lifecycle == QA_Q2W_DEAD && in.edition == QA_Q2F_CLASSIC ? in.now : expires_at;
  ti.held = held;
  h->emit(h->ctx, qa_q2w_calculate_hand_throw(ti));
  qa_q2w_hand_action a = mk(QA_Q2W_HA_RECOVERING);
  a.ready_at = qa_q2w_hand_deadline(in.now, firing_interval(h, qa_q2w_hand_recovery_seconds(tempo_of(in))), in.edition);
  a.require_release = held && in.held;
  return a;
}
static qa_q2w_hand_action release(double expires_at, qa_q2w_hand_action_input in, const qa_q2w_hand_action_host *h, double released_at) {
  if (in.edition == QA_Q2F_RERELEASE) return emit(expires_at, in, h, false);
  double throw_at = frame_deadline(in, h, released_at);
  if (in.now < throw_at) {
    qa_q2w_hand_action a = mk(QA_Q2W_HA_RELEASING);
    a.expires_at = expires_at; a.throw_at = throw_at;
    return a;
  }
  return emit(expires_at, in, h, false);
}
static qa_q2w_hand_action cook(qa_q2w_hand_action state, qa_q2w_hand_action_input in, const qa_q2w_hand_action_host *h) {
  if (in.now >= state.expires_at) return emit(state.expires_at, in, h, true);
  return !in.released && in.held ? state : release(state.expires_at, in, h, in.now);
}

qa_q2w_hand_action qa_q2w_step_hand_action(qa_q2w_hand_action state, qa_q2w_hand_action_input in,
                                           const qa_q2w_hand_action_host *h) {
  if (in.lifecycle == QA_Q2W_REMOVED) {
    if (state.kind == QA_Q2W_HA_PREPARING || state.kind == QA_Q2W_HA_COOKING || state.kind == QA_Q2W_HA_RELEASING) h->consume(h->ctx);
    return mk(QA_Q2W_HA_DISARMED);
  }
  if (in.lifecycle == QA_Q2W_DEAD || in.lifecycle == QA_Q2W_REMOVING || !in.enabled) {
    if (state.kind == QA_Q2W_HA_PREPARING) h->refund(h->ctx);
    if (state.kind == QA_Q2W_HA_COOKING || state.kind == QA_Q2W_HA_RELEASING)
      (void)emit(state.expires_at, in, h, in.lifecycle == QA_Q2W_DEAD && in.edition == QA_Q2F_RERELEASE);
    return mk(QA_Q2W_HA_DISARMED);
  }
  switch (state.kind) {
    case QA_Q2W_HA_DISARMED: return in.held ? state : mk(QA_Q2W_HA_IDLE);
    case QA_Q2W_HA_IDLE: {
      if (!in.pressed || !h->reserve(h->ctx)) return state;
      qa_q2w_hand_action a = mk(QA_Q2W_HA_PREPARING);
      a.frame = in.edition == QA_Q2F_CLASSIC ? 1 : 2;
      a.next_at = frame_deadline(in, h, in.now);
      a.release_queued = in.released || !in.held;
      return a;
    }
    case QA_Q2W_HA_PREPARING: {
      double frame = state.frame, next_at = state.next_at;
      while (in.now >= next_at && frame < 11) {
        if (frame == 5) h->sound(h->ctx, "cock");
        frame++;
        next_at = frame_deadline(in, h, next_at);
      }
      if (in.now < next_at) {
        qa_q2w_hand_action a = mk(QA_Q2W_HA_PREPARING);
        a.frame = frame; a.next_at = next_at;
        a.release_queued = state.release_queued || in.released || !in.held;
        return a;
      }
      h->sound(h->ctx, "cook-start");
      qa_q2w_hand_action cooking = mk(QA_Q2W_HA_COOKING);
      cooking.expires_at = qa_q2w_hand_fuse_deadline(next_at, in.edition);
      return state.release_queued ? release(cooking.expires_at, in, h, next_at) : cook(cooking, in, h);
    }
    case QA_Q2W_HA_COOKING: return cook(state, in, h);
    case QA_Q2W_HA_RELEASING: return in.now < state.throw_at ? state : emit(state.expires_at, in, h, false);
    case QA_Q2W_HA_RECOVERING: {
      bool require_release = state.require_release && in.held && !in.released;
      if (in.now < state.ready_at) {
        qa_q2w_hand_action a = state;
        a.require_release = require_release;
        return a;
      }
      return require_release ? mk(QA_Q2W_HA_DISARMED) : mk(QA_Q2W_HA_IDLE);
    }
  }
  return state;
}
