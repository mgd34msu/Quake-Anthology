/* Port of src/content/q2/foundation/monsters/types.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/network/common/js_number.h"

#include <math.h>

#include "qa/foundation/support.h"

const char *qa_q2m_ai_name(qa_q2m_ai_kind kind) {
  switch (kind) {
    case QA_Q2M_AI_STAND: return "stand";
    case QA_Q2M_AI_WALK: return "walk";
    case QA_Q2M_AI_RUN: return "run";
    case QA_Q2M_AI_CHARGE: return "charge";
    case QA_Q2M_AI_MOVE: return "move";
    case QA_Q2M_AI_SOLDIER_MOVE: return "soldier_move";
    case QA_Q2M_AI_TURN: return "turn";
    case QA_Q2M_AI_NONE: return "none";
    case QA_Q2M_AI_SOURCE: return "source";
  }
  return "none";
}
const char *qa_q2m_weapon_kind_name(qa_q2m_weapon_kind kind) {
  switch (kind) {
    case QA_Q2M_WEAPON_BLASTER: return "blaster";
    case QA_Q2M_WEAPON_SHOTGUN: return "shotgun";
    case QA_Q2M_WEAPON_MACHINEGUN: return "machinegun";
  }
  return "blaster";
}
const char *qa_q2m_locomotion_name(qa_q2m_locomotion kind) {
  switch (kind) {
    case QA_Q2M_LOCO_WALK: return "walk";
    case QA_Q2M_LOCO_FLY: return "fly";
    case QA_Q2M_LOCO_SWIM: return "swim";
    case QA_Q2M_LOCO_STATIONARY: return "stationary";
  }
  return "walk";
}
const char *qa_q2m_attack_state_name(qa_q2m_attack_state state) {
  switch (state) {
    case QA_Q2M_ATTACK_STRAIGHT: return "straight";
    case QA_Q2M_ATTACK_SLIDING: return "sliding";
    case QA_Q2M_ATTACK_MELEE: return "melee";
    case QA_Q2M_ATTACK_MISSILE: return "missile";
    case QA_Q2M_ATTACK_BLIND: return "blind";
  }
  return "straight";
}

size_t qa_q2m_record_at(size_t count, double index) {
  if (!(index == trunc(index)) || index < 0 || index >= (double)count)
    qa_range_error("Monster source table index %s outside %zu", QA_JS_NUMBER(index), count);
  return (size_t)index;
}

static void mw_bullet(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                      double damage, double kick, double hspread, double vspread, double mod) {
  qa_q2w_ballistics_fire_bullet(ctx, self->actor, game, start, direction, damage, kick, hspread, vspread, mod);
}
static void mw_shotgun(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                       double damage, double kick, double hspread, double vspread, double count, double mod) {
  qa_q2w_ballistics_fire_shotgun(ctx, self->actor, game, start, direction, damage, kick, hspread, vspread, count, mod);
}
static qa_q2_entity *mw_blaster(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                 double damage, double speed, double effects, bool hyper, bool has_mod, double mod) {
  return has_mod ? qa_q2w_ballistics_fire_blaster(ctx, self->actor, game, start, direction, damage, speed, effects, hyper, mod)
                 : qa_q2w_ballistics_fire_blaster_default(ctx, self->actor, game, start, direction, damage, speed, effects, hyper);
}
static bool mw_hit(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 aim, double damage, double kick) {
  return qa_q2w_ballistics_fire_hit(ctx, self, game, aim, damage, kick);
}
static qa_q2_entity *mw_rocket(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                double damage, double speed, double radius, double radius_damage) {
  return qa_q2w_ballistics_fire_rocket(ctx, self->actor, game, start, direction, damage, speed, radius, radius_damage);
}
static qa_q2_entity *mw_grenade(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                 double damage, double speed, double timer, double radius, bool hand, bool held, bool monster,
                                 const qa_q2w_grenade_adjustment *adjustment) {
  return qa_q2w_ballistics_fire_grenade(ctx, self->actor, game, start, direction, damage, speed, timer, radius, hand, held, monster, adjustment);
}
static void mw_rail(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction, double damage, double kick) {
  qa_q2w_ballistics_fire_rail(ctx, self->actor, game, start, direction, damage, kick);
}
static qa_q2_entity *mw_bfg(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                             double damage, double speed, double radius) {
  return qa_q2w_ballistics_fire_bfg(ctx, self->actor, game, start, direction, damage, speed, radius);
}
qa_q2m_weapons qa_q2m_weapons_from_ballistics(qa_q2w_ballistics *ballistics) {
  qa_q2m_weapons w;
  w.ctx = ballistics;
  w.fire_bullet = mw_bullet; w.fire_shotgun = mw_shotgun; w.fire_blaster = mw_blaster;
  w.fire_hit = mw_hit; w.fire_rocket = mw_rocket; w.fire_grenade = mw_grenade;
  w.fire_rail = mw_rail; w.fire_bfg = mw_bfg;
  return w;
}
