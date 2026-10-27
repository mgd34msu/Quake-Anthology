/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/arsenal_knowledge.h"
#include "qa/bots/behavior/library/weapons.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include <math.h>
#include <string.h>
/* Q3 Weapon enum (owner: content/q3/base/shared/definitions.ts):
 * GAUNTLET=1,MACHINEGUN=2,SHOTGUN=3,GRENADE=4,ROCKET=5,LIGHTNING=6,RAILGUN=7,PLASMA=8,BFG=9,GRAPPLE=10,NAILGUN=11,PROX=12,CHAINGUN=13
 */
int64_t qa_bot_arsenal_personality_role(const qa_bot_weapon_candidate_bots_a *c) {
  if (c->has_role)
    return c->role;
  if (c->melee)
    return 1;
  if (c->projectile_gravity > 0)
    return 4;
  if (c->speed > 0)
    return (c->damage_type & QA_DAMAGE_TYPE_RADIAL) != 0 ? 5 : 8;
  return 2;
}
static double inv(const qa_bot_state_bots_a *s, int64_t i) {
  return (i >= 0 && i < 204) ? s->inventory[i] : 0;
}
bool qa_bot_arsenal_owned(const qa_bot_state_bots_a *s, const qa_bot_weapon_candidate_bots_a *c) {
  return inv(s, c->weapon_inventory_index) > 0;
}
double qa_bot_arsenal_ammunition(const qa_bot_state_bots_a *s,
                                 const qa_bot_weapon_candidate_bots_a *c) {
  if (!c->has_role)
    return c->ammo_amount == 0 ? 999 : inv(s, c->ammo_inventory_index);
  return inv(s, c->ammo_inventory_index);
}
int64_t qa_bot_arsenal_choose_weapon(qa_bot_arsenal_knowledge_bots_a *k, qa_bot_state_bots_a *s,
                                     int64_t handle) {
  float best_w = 0.0f;
  int64_t best = 0, best_role = -1;
  double best_rate = 0;
  bool have_role = false;
  for (size_t i = 0; i < k->candidate_count; i++) {
    const qa_bot_weapon_candidate_bots_a *c = &k->candidates[i];
    int64_t role = qa_bot_arsenal_personality_role(c);
    double weight;
    if (c->has_role) {
      if (!k->evaluate_fight_weapon)
        continue;
      weight = k->evaluate_fight_weapon(k->ctx, handle, role, s->inventory, 204);
    } else {
      if (!qa_bot_arsenal_owned(s, c) || qa_bot_arsenal_ammunition(s, c) < (double)c->ammo_amount)
        continue;
      double dist = hypot(inv(s, QA_INV_ENEMY_HORIZONTAL_DIST), inv(s, QA_INV_ENEMY_HEIGHT));
      if (c->has_max_range && dist > c->max_range)
        continue;
      if (!k->evaluate_fight_weapon)
        continue;
      weight = k->evaluate_fight_weapon(k->ctx, handle, role, s->inventory, 204);
    }
    double rate = c->reload > 0 ? c->damage * c->projectile_count / c->reload : 0;
    if (weight > best_w ||
        (weight > 0 && weight == best_w && have_role && role == best_role && rate > best_rate)) {
      best_w = (float)weight;
      best = c->weapon_number;
      best_role = role;
      have_role = true;
      best_rate = rate;
    }
  }
  if (best == 0) {
    for (size_t i = 0; i < k->candidate_count; i++)
      if (k->candidates[i].valid && k->candidates[i].melee &&
          qa_bot_arsenal_owned(s, &k->candidates[i]))
        return k->candidates[i].weapon_number;
    for (size_t i = 0; i < k->candidate_count; i++)
      if (k->candidates[i].valid && qa_bot_arsenal_owned(s, &k->candidates[i]) &&
          qa_bot_arsenal_ammunition(s, &k->candidates[i]) >= (double)k->candidates[i].ammo_amount)
        return k->candidates[i].weapon_number;
    for (size_t i = 0; i < k->candidate_count; i++)
      if (k->candidates[i].valid)
        return k->candidates[i].weapon_number;
  }
  return best;
}
int64_t qa_bot_arsenal_activation_weapon(qa_bot_arsenal_knowledge_bots_a *k,
                                         qa_bot_state_bots_a *s) {
  static const int64_t base_order[] = {2, 3, 8, 6};
  static const int64_t tail_order[] = {7, 5, 9};
  for (int pass = 0; pass < 3; pass++) {
    const int64_t *ord = pass == 0 ? base_order : pass == 2 ? tail_order : NULL;
    size_t on = pass == 0 ? 4 : pass == 2 ? 3 : 0;
    int64_t mp[2] = {13, 11};
    if (pass == 1) {
      if (!k->missionpack)
        continue;
      ord = mp;
      on = 2;
    }
    for (size_t j = 0; j < on; j++)
      for (size_t i = 0; i < k->candidate_count; i++) {
        const qa_bot_weapon_candidate_bots_a *c = &k->candidates[i];
        if (!c->valid || c->melee || c->projectile_gravity != 0 || !qa_bot_arsenal_owned(s, c))
          continue;
        bool ammo_ok = c->has_role ? qa_bot_arsenal_ammunition(s, c) > 0
                                   : qa_bot_arsenal_ammunition(s, c) >= (double)c->ammo_amount;
        if (ammo_ok && qa_bot_arsenal_personality_role(c) == ord[j])
          return c->weapon_number;
      }
  }
  return -1;
}
double qa_bot_arsenal_aggression(qa_bot_arsenal_knowledge_bots_a *k, qa_bot_state_bots_a *s,
                                 int64_t cur) {
  bool cur_melee = false;
  for (size_t i = 0; i < k->candidate_count; i++)
    if (k->candidates[i].weapon_number == cur)
      cur_melee = k->candidates[i].melee;
  if (inv(s, QA_INV_QUAD) != 0 && (!cur_melee || inv(s, QA_INV_ENEMY_HORIZONTAL_DIST) < 80))
    return 70;
  double health = inv(s, QA_INV_HEALTH), armor = inv(s, QA_INV_ARMOR);
  if (inv(s, QA_INV_ENEMY_HEIGHT) > 200 || health < 60 || (health < 80 && armor < 40))
    return 0;
  return 0;
}
qa_bot_weapon_tactics_bots_a qa_bot_arsenal_tactics(qa_bot_arsenal_knowledge_bots_a *k,
                                                    int64_t weapon) {
  qa_bot_weapon_tactics_bots_a t;
  memset(&t, 0, sizeof t);
  for (size_t i = 0; i < k->candidate_count; i++)
    if (k->candidates[i].weapon_number == weapon) {
      t.melee = k->candidates[i].melee;
      t.has_max_range = k->candidates[i].has_max_range;
      t.max_range = k->candidates[i].max_range;
      t.weakness = k->candidates[i].melee ? 1 : 0;
    }
  return t;
}
