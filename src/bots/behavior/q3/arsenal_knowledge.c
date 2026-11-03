/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/arsenal_knowledge.h"
#include "qa/bots/behavior/library/weapons.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

/** These roles choose learned fuzzy/personality profiles, never gameplay weapon identities. */
static int64_t personality_role(const qa_bot_weapon_knowledge_bots_a *candidate) {
  if (candidate->has_personality_role)
    return candidate->personality_role;
  const qa_weapon_info_bots_a *info = &candidate->info;
  if (candidate->melee)
    return QA_Q3_WP_GAUNTLET;
  if (info->projectile_info.gravity > 0)
    return QA_Q3_WP_GRENADE_LAUNCHER;
  if (info->speed > 0)
    return (info->projectile_info.damage_type & QA_DAMAGE_TYPE_RADIAL) != 0 ? QA_Q3_WP_ROCKET_LAUNCHER
                                                                           : QA_Q3_WP_PLASMAGUN;
  if (info->projectile_count >= 4)
    return QA_Q3_WP_SHOTGUN;
  if (info->horizontal_spread > 0 || info->vertical_spread > 0)
    return QA_Q3_WP_MACHINEGUN;
  return info->reload <= (double)0.2 ? QA_Q3_WP_LIGHTNING : QA_Q3_WP_RAILGUN;
}

static qa_bot_weapon_tactics_bots_a tactics(const qa_bot_weapon_knowledge_bots_a *candidate) {
  int64_t role = candidate == NULL ? QA_Q3_WP_NONE : personality_role(candidate);
  qa_bot_weapon_tactics_bots_a t;
  memset(&t, 0, sizeof t);
  t.melee = candidate != NULL && candidate->melee;
  t.has_max_range = candidate != NULL && candidate->has_maximum_range;
  t.max_range = t.has_max_range ? candidate->maximum_range : 0;
  t.weakness = role == QA_Q3_WP_MACHINEGUN ? 90 : 0;
  t.close_range_aim_penalty =
      candidate == NULL ? false
      : !candidate->has_personality_role ? candidate->info.speed == 0 && !candidate->melee
      : role == QA_Q3_WP_MACHINEGUN || role == QA_Q3_WP_SHOTGUN || role == QA_Q3_WP_LIGHTNING || role == QA_Q3_WP_RAILGUN;
  t.predict_occluded_splash = role == QA_Q3_WP_BFG || role == QA_Q3_WP_ROCKET_LAUNCHER ||
                              role == QA_Q3_WP_GRENADE_LAUNCHER;
  t.has_aim_accuracy = true;
  switch (role) {
  case QA_Q3_WP_MACHINEGUN:
    t.aim_accuracy = QA_BCHAR_AIM_ACCURACY_MACHINEGUN;
    break;
  case QA_Q3_WP_SHOTGUN:
    t.aim_accuracy = QA_BCHAR_AIM_ACCURACY_SHOTGUN;
    break;
  case QA_Q3_WP_GRENADE_LAUNCHER:
    t.aim_accuracy = QA_BCHAR_AIM_ACCURACY_GRENADELAUNCHER;
    break;
  case QA_Q3_WP_ROCKET_LAUNCHER:
    t.aim_accuracy = QA_BCHAR_AIM_ACCURACY_ROCKETLAUNCHER;
    break;
  case QA_Q3_WP_LIGHTNING:
    t.aim_accuracy = QA_BCHAR_AIM_ACCURACY_LIGHTNING;
    break;
  case QA_Q3_WP_RAILGUN:
    t.aim_accuracy = QA_BCHAR_AIM_ACCURACY_RAILGUN;
    break;
  case QA_Q3_WP_PLASMAGUN:
    t.aim_accuracy = QA_BCHAR_AIM_ACCURACY_PLASMAGUN;
    break;
  case QA_Q3_WP_BFG:
    t.aim_accuracy = QA_BCHAR_AIM_ACCURACY_BFG10K;
    break;
  default:
    t.has_aim_accuracy = false;
    break;
  }
  t.has_aim_skill = true;
  switch (role) {
  case QA_Q3_WP_GRENADE_LAUNCHER:
    t.aim_skill = QA_BCHAR_AIM_SKILL_GRENADELAUNCHER;
    break;
  case QA_Q3_WP_ROCKET_LAUNCHER:
    t.aim_skill = QA_BCHAR_AIM_SKILL_ROCKETLAUNCHER;
    break;
  case QA_Q3_WP_PLASMAGUN:
    t.aim_skill = QA_BCHAR_AIM_SKILL_PLASMAGUN;
    break;
  case QA_Q3_WP_BFG:
    t.aim_skill = QA_BCHAR_AIM_SKILL_BFG10K;
    break;
  default:
    t.has_aim_skill = false;
    break;
  }
  return t;
}

static const int64_t aggression_profiles[][3] = {
    {QA_Q3_WP_BFG, 7, 100},          {QA_Q3_WP_RAILGUN, 5, 95},    {QA_Q3_WP_LIGHTNING, 50, 90},
    {QA_Q3_WP_ROCKET_LAUNCHER, 5, 90}, {QA_Q3_WP_PLASMAGUN, 40, 85}, {QA_Q3_WP_GRENADE_LAUNCHER, 10, 80},
    {QA_Q3_WP_SHOTGUN, 10, 50},
};

/* TS profileInventory: false is null; ammo -1 is the null ammo slot. */
static bool profile_inventory(int64_t role, int64_t *weapon, int64_t *ammo) {
  switch (role) {
  case QA_Q3_WP_GAUNTLET: *weapon = QA_INV_GAUNTLET; *ammo = -1; return true;
  case QA_Q3_WP_MACHINEGUN: *weapon = QA_INV_MACHINEGUN; *ammo = QA_INV_BULLETS; return true;
  case QA_Q3_WP_SHOTGUN: *weapon = QA_INV_SHOTGUN; *ammo = QA_INV_SHELLS; return true;
  case QA_Q3_WP_GRENADE_LAUNCHER: *weapon = QA_INV_GRENADELAUNCHER; *ammo = QA_INV_GRENADES; return true;
  case QA_Q3_WP_ROCKET_LAUNCHER: *weapon = QA_INV_ROCKETLAUNCHER; *ammo = QA_INV_ROCKETS; return true;
  case QA_Q3_WP_LIGHTNING: *weapon = QA_INV_LIGHTNING; *ammo = QA_INV_LIGHTNINGAMMO; return true;
  case QA_Q3_WP_RAILGUN: *weapon = QA_INV_RAILGUN; *ammo = QA_INV_SLUGS; return true;
  case QA_Q3_WP_PLASMAGUN: *weapon = QA_INV_PLASMAGUN; *ammo = QA_INV_CELLS; return true;
  case QA_Q3_WP_BFG: *weapon = QA_INV_BFG10K; *ammo = QA_INV_BFGAMMO; return true;
  case QA_Q3_WP_GRAPPLING_HOOK: *weapon = QA_INV_GRAPPLINGHOOK; *ammo = -1; return true;
  case QA_Q3_WP_NAILGUN: *weapon = QA_INV_NAILGUN; *ammo = QA_INV_NAILS; return true;
  case QA_Q3_WP_PROX_LAUNCHER: *weapon = QA_INV_PROXLAUNCHER; *ammo = QA_INV_MINES; return true;
  case QA_Q3_WP_CHAINGUN: *weapon = QA_INV_CHAINGUN; *ammo = QA_INV_BELT; return true;
  default: return false;
  }
}

/* state.inventory[index] ?? 0 */
static double inventory(const qa_bot_state_bots_a *state, int64_t index) {
  return index >= 0 && index < QA_Q3_BASE_MAX_ITEMS ? state->src->inventory[index] : 0;
}

static bool owned(const qa_bot_state_bots_a *state, const qa_bot_weapon_knowledge_bots_a *candidate) {
  int64_t weapon, ammo;
  if (!candidate->has_personality_role)
    return inventory(state, candidate->info.weapon_inventory_index) > 0;
  if (!profile_inventory(candidate->personality_role, &weapon, &ammo))
    return false;
  return inventory(state, weapon) > 0;
}

static double ammunition(const qa_bot_state_bots_a *state,
                         const qa_bot_weapon_knowledge_bots_a *candidate) {
  if (!candidate->has_personality_role)
    return candidate->info.ammo_amount == 0 ? 999
                                            : inventory(state, candidate->info.ammo_inventory_index);
  int64_t weapon, ammo;
  if (!profile_inventory(candidate->personality_role, &weapon, &ammo))
    return 0;
  return ammo < 0 ? 999 : inventory(state, ammo);
}

static void refresh(qa_bot_arsenal_knowledge_bots_a *k, qa_bot_library_bots_a *library,
                    int64_t handle) {
  size_t count = 0;
  qa_bot_weapon_knowledge_bots_a *next = k->data.candidates(k->data.ctx, library, handle, &count);
  free(k->candidates);
  k->candidates = next;
  k->candidate_count = next == NULL ? 0 : count;
}

static const qa_bot_weapon_knowledge_bots_a *find_number(const qa_bot_arsenal_knowledge_bots_a *k,
                                                         int64_t number) {
  for (size_t i = 0; i < k->candidate_count; i++)
    if (k->candidates[i].info.number == number)
      return &k->candidates[i];
  return NULL;
}

static void update_inventory(void *ctx, struct qa_bot_state_bots_a *state) {
  qa_bot_arsenal_knowledge_bots_a *k = ctx;
  k->data.update_inventory(k->data.ctx, state);
}

static bool same_item(qa_item_id a, qa_item_id b) { return strcmp(a, b) == 0; }

static double pickup_utility(void *ctx, struct qa_bot_library_bots_a *library,
                             struct qa_bot_state_bots_a *state,
                             const qa_pickup_supply_preview *preview) {
  qa_bot_arsenal_knowledge_bots_a *k = ctx;
  if (!preview->accepted)
    return 0;
  refresh(k, library, state->src->ws);
  size_t capacity = k->candidate_count + preview->weapon_count + preview->ammo_count + 1;
  const qa_bot_weapon_supply_bots_a **supplies = calloc(capacity, sizeof *supplies);
  qa_item_id *new_weapons = calloc(capacity, sizeof *new_weapons);
  qa_item_id *cost_items = calloc(capacity, sizeof *cost_items);
  double *costs = calloc(capacity, sizeof *costs);
  qa_item_id *given_items = calloc(capacity, sizeof *given_items);
  double *given = calloc(capacity, sizeof *given);
  if (!supplies || !new_weapons || !cost_items || !costs || !given_items || !given)
    qa_range_error("Bot pickup utility allocation failed");
  size_t supply_count = 0, new_count = 0, cost_count = 0, given_count = 0;
  for (size_t i = 0; i < k->candidate_count; i++)
    if (k->candidates[i].info.valid && k->candidates[i].has_supply)
      supplies[supply_count++] = &k->candidates[i].supply;
  for (size_t r = 0; r < preview->weapon_count; r++) {
    const qa_pickup_ammo_receipt *receipt = &preview->weapons[r];
    if (!(receipt->before <= 0 && receipt->given > 0))
      continue;
    bool supplied = false;
    for (size_t s = 0; s < supply_count && !supplied; s++)
      supplied = same_item(supplies[s]->weapon, receipt->item);
    if (!supplied)
      continue;
    bool present = false;
    for (size_t n = 0; n < new_count && !present; n++)
      present = same_item(new_weapons[n], receipt->item);
    if (!present)
      new_weapons[new_count++] = receipt->item;
  }
  for (size_t s = 0; s < supply_count; s++) {
    const qa_bot_weapon_supply_bots_a *supply = supplies[s];
    bool is_new = false;
    for (size_t n = 0; n < new_count && !is_new; n++)
      is_new = same_item(new_weapons[n], supply->weapon);
    if ((!supply->owned && !is_new) || !supply->has_ammo || supply->per_shot <= 0)
      continue;
    size_t at = 0;
    while (at < cost_count && !same_item(cost_items[at], supply->ammo_item))
      at++;
    if (at == cost_count) {
      cost_items[cost_count] = supply->ammo_item;
      costs[cost_count++] = INFINITY;
    }
    costs[at] = qa_js_min(costs[at], supply->per_shot);
  }
  for (size_t r = 0; r < preview->ammo_count; r++) {
    const qa_pickup_ammo_receipt *receipt = &preview->ammo[r];
    if (receipt->given <= 0)
      continue;
    size_t at = 0;
    while (at < given_count && !same_item(given_items[at], receipt->item))
      at++;
    if (at == given_count) {
      given_items[given_count] = receipt->item;
      given[given_count++] = 0;
    }
    given[at] = given[at] + receipt->given;
  }
  double utility = (double)new_count * 100;
  for (size_t g = 0; g < given_count; g++)
    for (size_t c = 0; c < cost_count; c++)
      if (same_item(cost_items[c], given_items[g])) {
        utility += given[g] / costs[c];
        break;
      }
  free(supplies);
  free(new_weapons);
  free(cost_items);
  free(costs);
  free(given_items);
  free(given);
  return utility;
}

static bool weapon_info(void *ctx, struct qa_bot_library_bots_a *library, int64_t handle,
                        int64_t weapon, qa_weapon_info_bots_a *out) {
  qa_bot_arsenal_knowledge_bots_a *k = ctx;
  refresh(k, library, handle);
  const qa_bot_weapon_knowledge_bots_a *candidate = find_number(k, weapon);
  if (candidate == NULL)
    return false;
  *out = candidate->info;
  return true;
}

static qa_bot_weapon_tactics_bots_a tactics_of(void *ctx, int64_t weapon) {
  return tactics(find_number(ctx, weapon));
}

static int64_t activation_weapon(void *ctx, struct qa_bot_library_bots_a *library,
                                 struct qa_bot_state_bots_a *state) {
  qa_bot_arsenal_knowledge_bots_a *k = ctx;
  refresh(k, library, state->src->ws);
  static const int64_t base[] = {QA_Q3_WP_MACHINEGUN, QA_Q3_WP_SHOTGUN, QA_Q3_WP_PLASMAGUN,
                                 QA_Q3_WP_LIGHTNING};
  static const int64_t missionpack[] = {QA_Q3_WP_CHAINGUN, QA_Q3_WP_NAILGUN};
  static const int64_t tail[] = {QA_Q3_WP_RAILGUN, QA_Q3_WP_ROCKET_LAUNCHER, QA_Q3_WP_BFG};
  int64_t order[9];
  size_t order_count = 0;
  for (size_t i = 0; i < 4; i++)
    order[order_count++] = base[i];
  if (state->product == QA_Q3_BASE_PRODUCT_MISSIONPACK)
    for (size_t i = 0; i < 2; i++)
      order[order_count++] = missionpack[i];
  for (size_t i = 0; i < 3; i++)
    order[order_count++] = tail[i];
  bool *available = calloc(k->candidate_count + 1, sizeof *available);
  if (available == NULL)
    qa_range_error("Bot activation weapon allocation failed");
  for (size_t i = 0; i < k->candidate_count; i++) {
    const qa_bot_weapon_knowledge_bots_a *c = &k->candidates[i];
    available[i] = c->info.valid && !c->melee && c->info.projectile_info.gravity == 0 &&
                   owned(state, c) &&
                   (!c->has_personality_role ? ammunition(state, c) >= c->info.ammo_amount
                                             : ammunition(state, c) > 0);
  }
  int64_t result = -1;
  for (size_t o = 0; o < order_count && result < 0; o++)
    for (size_t i = 0; i < k->candidate_count; i++)
      if (available[i] && personality_role(&k->candidates[i]) == order[o]) {
        result = k->candidates[i].info.number;
        break;
      }
  free(available);
  return result;
}

typedef struct best_choice {
  float weight;
  int64_t weapon;
  bool has_role;
  int64_t role;
  double rate;
} best_choice;

static void consider(best_choice *best, const qa_bot_weapon_knowledge_bots_a *candidate, int64_t role,
                     float weight) {
  const qa_weapon_info_bots_a *info = &candidate->info;
  double rate = info->reload > 0 ? (double)info->projectile_info.damage *
                                       (double)info->projectile_count / (double)info->reload
                                 : 0;
  if (weight > best->weight || (weight > 0 && weight == best->weight && best->has_role &&
                                role == best->role && rate > best->rate)) {
    best->weight = weight;
    best->weapon = info->number;
    best->has_role = true;
    best->role = role;
    best->rate = rate;
  }
}

static int64_t choose_weapon(void *ctx, struct qa_bot_library_bots_a *library,
                             struct qa_bot_state_bots_a *state) {
  qa_bot_arsenal_knowledge_bots_a *k = ctx;
  refresh(k, library, state->src->ws);
  best_choice best = {0, 0, false, 0, 0};
  double values[QA_Q3_BASE_MAX_ITEMS];
  for (size_t i = 0; i < k->candidate_count; i++) {
    const qa_bot_weapon_knowledge_bots_a *candidate = &k->candidates[i];
    int64_t role = personality_role(candidate);
    for (int index = 0; index < QA_Q3_BASE_MAX_ITEMS; index++)
      values[index] = state->src->inventory[index];
    qa_weight_inventory weights = {values, QA_Q3_BASE_MAX_ITEMS, NULL, NULL};
    float weight;
    if (candidate->has_personality_role) {
      if (qa_weapon_ai_evaluate_fight_weapon(&library->weapons, state->src->ws, role, weights,
                                             &weight))
        consider(&best, candidate, role, weight);
      continue;
    }
    if (!owned(state, candidate) || ammunition(state, candidate) < candidate->info.ammo_amount)
      continue;
    double horizontal = inventory(state, QA_INV_ENEMY_HORIZONTAL_DIST);
    double height = inventory(state, QA_INV_ENEMY_HEIGHT);
    double distance = hypot(horizontal, height);
    if (candidate->has_maximum_range && distance > candidate->maximum_range)
      continue;
    int64_t weapon_slot, ammo_slot;
    if (!profile_inventory(role, &weapon_slot, &ammo_slot))
      continue;
    values[weapon_slot] = 1;
    if (ammo_slot >= 0)
      values[ammo_slot] = ammunition(state, candidate);
    if (qa_weapon_ai_evaluate_fight_weapon(&library->weapons, state->src->ws, role, weights, &weight))
      consider(&best, candidate, role, weight);
  }
  if (best.weapon == 0) {
    /* Source BotChooseFightWeapon defaults to the gauntlet; weapon 0 has no weapon info. */
    const qa_bot_weapon_knowledge_bots_a *fallback = NULL;
    for (size_t i = 0; i < k->candidate_count && fallback == NULL; i++)
      if (k->candidates[i].info.valid && k->candidates[i].melee && owned(state, &k->candidates[i]))
        fallback = &k->candidates[i];
    for (size_t i = 0; i < k->candidate_count && fallback == NULL; i++)
      if (k->candidates[i].info.valid && owned(state, &k->candidates[i]) &&
          ammunition(state, &k->candidates[i]) >= k->candidates[i].info.ammo_amount)
        fallback = &k->candidates[i];
    for (size_t i = 0; i < k->candidate_count && fallback == NULL; i++)
      if (k->candidates[i].info.valid)
        fallback = &k->candidates[i];
    if (fallback != NULL)
      best.weapon = fallback->info.number;
  }
  return best.weapon;
}

static double aggression(void *ctx, struct qa_bot_state_bots_a *state) {
  qa_bot_arsenal_knowledge_bots_a *k = ctx;
  const qa_bot_weapon_knowledge_bots_a *current = find_number(k, state->src->weapon_num);
  if (inventory(state, QA_INV_QUAD) != 0 &&
      (current == NULL || !current->melee || inventory(state, QA_INV_ENEMY_HORIZONTAL_DIST) < 80))
    return 70;
  double health = inventory(state, QA_INV_HEALTH), armor = inventory(state, QA_INV_ARMOR);
  if (inventory(state, QA_INV_ENEMY_HEIGHT) > 200 || health < 60 || (health < 80 && armor < 40))
    return 0;
  for (size_t p = 0; p < sizeof aggression_profiles / sizeof aggression_profiles[0]; p++)
    for (size_t i = 0; i < k->candidate_count; i++)
      if (personality_role(&k->candidates[i]) == aggression_profiles[p][0] &&
          owned(state, &k->candidates[i]) &&
          ammunition(state, &k->candidates[i]) > aggression_profiles[p][1])
        return (double)aggression_profiles[p][2];
  return 0;
}

/** One decision implementation consumes native and foreign source facts through the same fuzzy profiles. */
void qa_create_bot_arsenal_knowledge_bots_a(qa_bot_arsenal_knowledge_bots_a *k,
                                            const qa_bot_arsenal_data_bots_a *data) {
  memset(k, 0, sizeof *k);
  k->data = *data;
  k->knowledge.ctx = k;
  k->knowledge.pickup_utility = pickup_utility;
  k->knowledge.choose_weapon = choose_weapon;
  k->knowledge.activation_weapon = activation_weapon;
  k->knowledge.tactics = tactics_of;
  k->knowledge.aggression = aggression;
  k->knowledge.update_inventory = update_inventory;
  k->knowledge.weapon_info = weapon_info;
}

void qa_bot_arsenal_knowledge_bots_a_destroy(qa_bot_arsenal_knowledge_bots_a *k) {
  free(k->candidates);
  k->candidates = NULL;
  k->candidate_count = 0;
}
