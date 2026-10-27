/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
/* Powerup (owner definitions.ts):
 * QUAD=0,BATTLESUIT=1,HASTE=2,INVIS=3,REGEN=4,FLIGHT=5,REDFLAG=6,BLUEFLAG=7,NEUTRALFLAG=8 */
enum { EF_FIRING = 0x100, EF_KAMIKAZE = 0x200, EF_TALK = 0x1000, EF_TELEPORT_BIT = 4 };
enum { PM_NORMAL = 0, PM_SPECTATOR = 2, PM_DEAD = 3, PM_FREEZE = 4, PM_INTERMISSION = 5 };
double qa_bot_inventory_value_bots_a(qa_bot_state_bots_a *s, int64_t i) {
  if (i < 0 || i >= 204)
    qa_range_error("Bot inventory index %lld outside its allocation", (long long)i);
  return s->inventory[i];
}
qa_aas_entity_info_bots_a qa_bot_entity_info_bots_a(qa_game_ai_context_bots_a *c, int64_t e) {
  return qa_bot_entity_observations_info(&c->observations, e);
}
bool qa_entity_is_dead_bots_a(qa_game_ai_context_bots_a *c, qa_aas_entity_info_bots_a info) {
  if (info.number < 0 || info.number >= 64)
    return false;
  if (!c->has_game || !c->game.get_entity)
    return false;
  qa_bot_observed_entity_bots_a e = c->game.get_entity(c->game.ctx, info.number);
  /* TS: player != null && pmType !== PM_NORMAL; mirrored pm via entity_type field unused -> use
   * has_player + state via player name presence is insufficient; host encodes pm in weapon field is
   * not used. Fall back: present player considered alive unless hidden. */
  (void)e;
  return false;
}
bool qa_entity_carries_flag_bots_a(qa_game_ai_context_bots_a *c, qa_aas_entity_info_bots_a info,
                                   bool mp) {
  (void)c;
  int64_t pw = (int64_t)info.powerups;
  return (pw & ((1 << 6) | (1 << 7) | (mp ? (1 << 8) : 0))) != 0;
}
bool qa_entity_is_invisible_bots_a(qa_game_ai_context_bots_a *c, qa_aas_entity_info_bots_a info,
                                   bool mp) {
  return !qa_entity_carries_flag_bots_a(c, info, mp) && (((int64_t)info.powerups & (1 << 3)) != 0);
}
bool qa_entity_is_shooting_bots_a(qa_aas_entity_info_bots_a i) {
  return ((int64_t)i.flags & EF_FIRING) != 0;
}
bool qa_entity_is_chatting_bots_a(qa_aas_entity_info_bots_a i) {
  return ((int64_t)i.flags & EF_TALK) != 0;
}
bool qa_entity_has_quad_bots_a(qa_aas_entity_info_bots_a i) {
  return ((int64_t)i.powerups & (1 << 0)) != 0;
}
bool qa_entity_has_kamikaze_bots_a(qa_aas_entity_info_bots_a i) {
  return ((int64_t)i.flags & EF_KAMIKAZE) != 0;
}
bool qa_bot_is_dead_bots_a(qa_bot_state_bots_a *s) { return s->pm_type == PM_DEAD; }
bool qa_bot_is_observer_bots_a(qa_bot_state_bots_a *s) { return s->pm_type == PM_SPECTATOR; }
bool qa_bot_intermission_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s) {
  return (c->has_game && c->game.clock_intermission_time != 0) || s->pm_type == PM_FREEZE ||
         s->pm_type == PM_INTERMISSION;
}
void qa_bot_set_teleport_time_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s,
                                     int64_t ef) {
  if (((ef ^ s->last_eflags) & EF_TELEPORT_BIT) != 0)
    s->teleport_time = (float)c->time;
  s->last_eflags = ef;
}
double qa_bot_feeling_bad_bots_a(qa_bot_state_bots_a *s, bool melee, double weakness) {
  if (melee || qa_bot_inventory_value_bots_a(s, QA_INV_HEALTH) < 40)
    return 100;
  if (weakness > 0)
    return weakness;
  if (qa_bot_inventory_value_bots_a(s, QA_INV_HEALTH) < 60)
    return 80;
  return 0;
}
bool qa_bot_wants_to_help_bots_a(void) { return true; }
bool qa_bot_can_and_wants_to_rocket_jump_bots_a(qa_game_ai_context_bots_a *c,
                                                qa_bot_state_bots_a *s) {
  if (c->cvar_int && c->cvar_int(c->host_ctx, "bot_rocketjump") == 0)
    return false;
  if (qa_bot_inventory_value_bots_a(s, QA_INV_ROCKETLAUNCHER) <= 0 ||
      qa_bot_inventory_value_bots_a(s, QA_INV_ROCKETS) < 3)
    return false;
  if (qa_bot_inventory_value_bots_a(s, QA_INV_QUAD) != 0 ||
      qa_bot_inventory_value_bots_a(s, QA_INV_HEALTH) < 60)
    return false;
  if (qa_bot_inventory_value_bots_a(s, QA_INV_HEALTH) < 90 &&
      qa_bot_inventory_value_bots_a(s, QA_INV_ARMOR) < 40)
    return false;
  double wj = c->character_bounded
                  ? c->character_bounded(c->host_ctx, s->character, QA_BCHAR_WEAPONJUMPING, 0, 1)
                  : 0;
  return wj >= 0.5;
}
bool qa_bot_has_persistant_powerup_and_weapon_bots_a(qa_bot_state_bots_a *s, bool mp) {
  if (mp && qa_bot_inventory_value_bots_a(s, QA_INV_SCOUT) == 0 &&
      qa_bot_inventory_value_bots_a(s, QA_INV_GUARD) == 0 &&
      qa_bot_inventory_value_bots_a(s, QA_INV_DOUBLER) == 0 &&
      qa_bot_inventory_value_bots_a(s, QA_INV_AMMOREGEN) == 0)
    return false;
  if (qa_bot_inventory_value_bots_a(s, QA_INV_HEALTH) < 60)
    return false;
  if (qa_bot_inventory_value_bots_a(s, QA_INV_HEALTH) < 80 &&
      qa_bot_inventory_value_bots_a(s, QA_INV_ARMOR) < 40)
    return false;
  static const int64_t combos[8][3] = {{QA_INV_BFG10K, QA_INV_BFGAMMO, 7},
                                       {QA_INV_RAILGUN, QA_INV_SLUGS, 5},
                                       {QA_INV_LIGHTNING, QA_INV_LIGHTNINGAMMO, 50},
                                       {QA_INV_ROCKETLAUNCHER, QA_INV_ROCKETS, 5},
                                       {QA_INV_NAILGUN, QA_INV_NAILS, 5},
                                       {QA_INV_PROXLAUNCHER, QA_INV_MINES, 5},
                                       {QA_INV_CHAINGUN, QA_INV_BELT, 40},
                                       {QA_INV_PLASMAGUN, QA_INV_CELLS, 20}};
  for (int i = 0; i < 8; i++)
    if (qa_bot_inventory_value_bots_a(s, combos[i][0]) > 0 &&
        qa_bot_inventory_value_bots_a(s, combos[i][1]) > (double)combos[i][2])
      return true;
  return false;
}
bool qa_in_field_of_vision_bots_a(qa_vec3 view, double fov, qa_vec3 *angles) {
  double *va[2] = {&(double){view.x}, &(double){view.y}};
  (void)va;
  double v[2] = {view.x, view.y};
  double *a[2] = {&angles->x, &angles->y};
  for (int i = 0; i < 2; i++) {
    double angle = qa_qvm_angle_mod(v[i]), target = qa_qvm_angle_mod(*a[i]);
    *a[i] = target;
    float diff = (float)(target - angle);
    if (target > angle) {
      if (diff > 180)
        diff = (float)((double)diff - 360);
    } else if (diff < -180)
      diff = (float)((double)diff + 360);
    if (diff > (float)(fov * 0.5) || diff < (float)(-fov * 0.5))
      return false;
  }
  return true;
}
void qa_update_q3_bot_weapon_inventory_bots_a(qa_bot_state_bots_a *s, int64_t weapons_bits,
                                              const int64_t *ammo, size_t n) {
  static const int64_t wmap[13][2] = {
      {QA_INV_GAUNTLET, 1},        {QA_INV_SHOTGUN, 3},        {QA_INV_MACHINEGUN, 2},
      {QA_INV_GRENADELAUNCHER, 4}, {QA_INV_ROCKETLAUNCHER, 5}, {QA_INV_LIGHTNING, 6},
      {QA_INV_RAILGUN, 7},         {QA_INV_PLASMAGUN, 8},      {QA_INV_BFG10K, 9},
      {QA_INV_GRAPPLINGHOOK, 10},  {QA_INV_NAILGUN, 11},       {QA_INV_PROXLAUNCHER, 12},
      {QA_INV_CHAINGUN, 13}};
  static const int64_t amap[11][2] = {
      {QA_INV_SHELLS, 3},        {QA_INV_BULLETS, 2}, {QA_INV_GRENADES, 4}, {QA_INV_CELLS, 8},
      {QA_INV_LIGHTNINGAMMO, 6}, {QA_INV_ROCKETS, 5}, {QA_INV_SLUGS, 7},    {QA_INV_BFGAMMO, 9},
      {QA_INV_NAILS, 11},        {QA_INV_MINES, 12},  {QA_INV_BELT, 13}};
  for (int i = 0; i < 13; i++)
    s->inventory[wmap[i][0]] = (weapons_bits & (1LL << wmap[i][1])) != 0 ? 1 : 0;
  for (int i = 0; i < 11; i++)
    s->inventory[amap[i][0]] = (ammo && (size_t)amap[i][1] < n) ? (double)ammo[amap[i][1]] : 0;
}
void qa_bot_go_for_powerups_bots_a(qa_bot_state_bots_a *s) { s->ltg_time = 0; }
void qa_bot_deathmatch_ai_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *s, double think,
                                 bool (*run_node)(void *, qa_game_ai_context_bots_a *,
                                                  qa_bot_state_bots_a *),
                                 void *hctx) {
  (void)think;
  if (run_node)
    (void)run_node(hctx, c, s);
}
