/* Port of src/movement/q3/weapon.ts. GPL-2.0-or-later. */
#include "qa/movement/q3/weapon.h"

#include <math.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/movement/q3/constants.h"

static bool flag(double v, int m) { return (qa_int32(v) & m) != 0; }

double qa_q3_weapon_delay(double milliseconds, double persistent, bool haste) {
  if (persistent == QA_Q3_PW_SCOUT) return trunc(milliseconds / 1.5);
  if (persistent == QA_Q3_PW_AMMOREGEN || haste) return trunc(milliseconds / 1.3);
  return milliseconds;
}

bool qa_q3_step_holdable(qa_q3_holdable_state *state, bool pressed,
                         void (*event)(void *ctx, double event), void *ctx) {
  if (pressed) {
    if (!flag(state->pm_flags, QA_Q3_FLAG_USE_ITEM_HELD)) {
      double tag = state->holdable_tag;
      if (tag != QA_Q3_HI_MEDKIT || state->health < state->max_health + 25) {
        state->pm_flags = (double)(qa_int32(state->pm_flags) | QA_Q3_FLAG_USE_ITEM_HELD);
        event(ctx, QA_Q3_EV_USE_ITEM0 + tag);
        state->holdable_item = 0; state->holdable_tag = 0;
      }
      return true;
    }
  } else state->pm_flags = (double)(qa_int32(state->pm_flags) & ~QA_Q3_FLAG_USE_ITEM_HELD);
  return false;
}

typedef struct weapon_step {
  qa_q3_source_weapon_state *state;
  qa_q3_move_command cmd;
  qa_q3_source_weapon_options *options;
} weapon_step;

static void ws_event(weapon_step *w, double e) { w->options->event(w->options->ctx, e); }
static void holdable_event(void *ctx, double e) { ws_event(ctx, e); }
static bool missionpack(weapon_step *w) { return strcmp(w->state->product, "missionpack") == 0; }

static void begin_drop(weapon_step *w) {
  ws_event(w, QA_Q3_EV_CHANGE_WEAPON);
  w->state->weapon_state = QA_Q3_WEAPON_DROPPING;
  w->state->weapon_time += 200;
  w->options->start_torso(w->options->ctx, QA_Q3_ANIM_TORSO_DROP);
}
static void begin_weapon_change(weapon_step *w, double weapon) {
  qa_q3_source_weapon_state *ps = w->state;
  if (weapon <= QA_Q3_WP_NONE || weapon >= (missionpack(w) ? 14 : 11) ||
      !(qa_int32(ps->owned_weapons) & (1 << qa_int32(weapon))) ||
      ps->weapon_state == QA_Q3_WEAPON_DROPPING)
    return;
  begin_drop(w);
}
static void finish_weapon_change(weapon_step *w) {
  qa_q3_source_weapon_state *ps = w->state;
  double requested = w->cmd.weapon, weapon;
  int32_t rw = qa_int32(requested);
  weapon = (rw >= 0 && rw <= 13 && (double)rw == requested) ? requested : QA_Q3_WP_NONE;
  if (weapon >= (missionpack(w) ? 14 : 11)) weapon = QA_Q3_WP_NONE;
  if (!(qa_int32(ps->owned_weapons) & (1 << qa_int32(weapon)))) weapon = QA_Q3_WP_NONE;
  ps->weapon = weapon; ps->weapon_state = QA_Q3_WEAPON_RAISING; ps->weapon_time += 250;
  w->options->start_torso(w->options->ctx, QA_Q3_ANIM_TORSO_RAISE);
}

void qa_q3_run_weapon_step(qa_q3_source_weapon_state *state, qa_q3_move_command command,
                           qa_q3_source_weapon_options *options) {
  weapon_step w = {state, command, options};
  qa_q3_source_weapon_state *ps = state;
  if (flag(ps->pm_flags, QA_Q3_FLAG_RESPAWNED) || ps->spectator) return;
  if (ps->health <= 0) { ps->weapon = QA_Q3_WP_NONE; return; }
  if (qa_q3_step_holdable(ps, flag(command.buttons, QA_Q3_BUTTON_USE_HOLDABLE), holdable_event, &w)) return;
  if (ps->weapon_time > 0) ps->weapon_time -= options->msec;
  qa_q3_external_weapon_slot *external = options->external_phase;
  if (external != NULL && *external == QA_Q3_SLOT_HOLSTERED) return;
  if (external != NULL && *external == QA_Q3_SLOT_DROPPING) {
    if (ps->weapon_time <= 0) *external = QA_Q3_SLOT_HOLSTERED;
    return;
  }
  if (external != NULL && *external == QA_Q3_SLOT_RESUME_REQUESTED) {
    if (ps->weapon_time > 0) return;
    finish_weapon_change(&w); *external = QA_Q3_SLOT_ACTIVE; return;
  }
  if (external != NULL && *external == QA_Q3_SLOT_HOLSTER_REQUESTED && ps->weapon_time <= 0 &&
      (ps->weapon_state == QA_Q3_WEAPON_READY || ps->weapon_state == QA_Q3_WEAPON_FIRING)) {
    double requested = command.weapon;
    bool native_switch = requested != ps->weapon && requested > QA_Q3_WP_NONE &&
        requested < (missionpack(&w) ? 14 : 11) && (qa_int32(ps->owned_weapons) & (1 << qa_int32(requested))) != 0;
    if (!native_switch) { begin_drop(&w); *external = QA_Q3_SLOT_DROPPING; return; }
  }
  if (ps->weapon_time <= 0 || ps->weapon_state != QA_Q3_WEAPON_FIRING) {
    if (ps->weapon != command.weapon) begin_weapon_change(&w, command.weapon);
  }
  if (ps->weapon_time > 0) return;
  if (ps->weapon_state == QA_Q3_WEAPON_DROPPING) { finish_weapon_change(&w); return; }
  if (ps->weapon_state == QA_Q3_WEAPON_RAISING) {
    ps->weapon_state = QA_Q3_WEAPON_READY;
    options->start_torso(options->ctx, ps->weapon == QA_Q3_WP_GAUNTLET ? QA_Q3_ANIM_TORSO_STAND2 : QA_Q3_ANIM_TORSO_STAND);
    return;
  }
  if (!flag(command.buttons, QA_Q3_BUTTON_ATTACK) || (ps->weapon == QA_Q3_WP_GAUNTLET && !options->gauntlet_hit)) {
    ps->weapon_time = 0; ps->weapon_state = QA_Q3_WEAPON_READY; return;
  }
  options->start_torso(options->ctx, ps->weapon == QA_Q3_WP_GAUNTLET ? QA_Q3_ANIM_TORSO_ATTACK2 : QA_Q3_ANIM_TORSO_ATTACK);
  ps->weapon_state = QA_Q3_WEAPON_FIRING;
  double ammo = ps->ammo_get(ps->ammo_ctx, ps->weapon);
  if (ammo == 0) { ws_event(&w, QA_Q3_EV_NOAMMO); ps->weapon_time += 500; return; }
  if (ammo != -1) ps->ammo_set(ps->ammo_ctx, ps->weapon, ammo - 1);
  ws_event(&w, QA_Q3_EV_FIRE_WEAPON);
  double add_time;
  switch (ps->weapon == trunc(ps->weapon) ? qa_int32(ps->weapon) : -1) {
    case QA_Q3_WP_LIGHTNING: add_time = 50; break;
    case QA_Q3_WP_SHOTGUN: add_time = 1000; break;
    case QA_Q3_WP_MACHINEGUN: case QA_Q3_WP_PLASMAGUN: add_time = 100; break;
    case QA_Q3_WP_GRENADE_LAUNCHER: case QA_Q3_WP_ROCKET_LAUNCHER: add_time = 800; break;
    case QA_Q3_WP_RAILGUN: add_time = 1500; break;
    case QA_Q3_WP_BFG: add_time = 200; break;
    case QA_Q3_WP_NAILGUN: add_time = missionpack(&w) ? 1000 : 400; break;
    case QA_Q3_WP_PROX_LAUNCHER: add_time = missionpack(&w) ? 800 : 400; break;
    case QA_Q3_WP_CHAINGUN: add_time = missionpack(&w) ? 30 : 400; break;
    default: add_time = 400; break;
  }
  double persistent = missionpack(&w) ? ps->persistent_powerup_tag : 0;
  ps->weapon_time += options->firing_delay != NULL
      ? options->firing_delay(options->ctx, add_time)
      : qa_q3_weapon_delay(add_time, persistent, ps->haste);
}
