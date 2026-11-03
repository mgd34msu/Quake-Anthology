/*
 * Port of src/content/q1/foundation/pickups.ts. GPL-2.0-or-later.
 * Regular armor is read from and written to game->host.combat.
 * Preview returns the acceptance in `eligible`. Backpack cargo follows
 * SharedPickupAdmission's identity profile: weapons, then counters, then
 * selection, always taken.
 */
#include "qa/content/q1/foundation/pickups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/composition/q3_q1_supply.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/world/gameplay/pickups.h"
#include "qa/foundation/js.h"

static qa_vec3 v3(double x, double y, double z) {
  const qa_vec3 result = {x, y, z};
  return result;
}
static void set_string(char **slot, const char *value) {
  if (value == NULL) value = "";
  if (*slot != NULL && strcmp(*slot, value) == 0) return;
  const size_t len = strlen(value) + 1;
  char *copy = malloc(len);
  if (!copy) return;
  memcpy(copy, value, len);
  free(*slot);
  *slot = copy;
}
/* TS `spawnflags & x` applies ToInt32: the int32 view of the number. */
static int32_t flags_of(const qa_q1_actor *entity) { return qa_js_to_int32(entity->spawnflags); }
/* TS game.host.actors.isLive(actor). */
static bool is_live_actor(qa_q1_services *game, qa_actor_id actor) {
  return qa_session_actor_registry_is_live(game->host.actors, actor);
}
static bool edition_is(qa_q1_services *game, const char *edition) {
  return game->options.edition != NULL && !strcmp(game->options.edition, edition);
}

typedef enum take_kind {
  TAKE_HEALTH, TAKE_ARMOR, TAKE_WEAPON, TAKE_AMMO, TAKE_KEY, TAKE_POWERUP, TAKE_BACKPACK,
  TAKE_Q3_HEALTH, TAKE_Q3_ARMOR, TAKE_Q3_AMMO, TAKE_Q3_WEAPON, TAKE_Q3_POWERUP, TAKE_Q3_HOLDABLE, TAKE_REFUSED
} take_kind;

typedef struct qa_q1_pickup_def {
  bool has_supply;
  const char *supply_kind; /* weapon|ammo */
  qa_item_id supply_item;
  double supply_amount;              /* ammo offer amount */
  qa_item_id weapon_ammo_item;       /* weapon offer */
  double weapon_ammo_amount;
  bool has_weapon_ammo;
  bool has_cargo;
  qa_pickup_cargo_entry cargo[5]; /* backpack: four counters, then the carried weapon */
  size_t cargo_count;
  char model[80];
  char sound[80];
  const char *bounds; /* box|weapon|artifact */
  double skin, respawn;
  take_kind take;
  /* take parameters */
  bool mega;
  double amount, absorption, points, mega_cap;
  qa_item_id item;
  qa_q1_weapon weapon;
  qa_q1_powerup powerup;
  /* foreign Q3 parameters */
  double q3_tag, q3_cap, quantity; /* q3_cap: 0 = player's maximum health */
  bool q3_shard, q3_persistent;
} pickup_def;


static void weapon_offer(qa_q1_weapon weapon, pickup_def *def) {
  const qa_item_id ammo = qa_q1_ammo_item(weapon);
  def->has_supply = true;
  def->supply_kind = "weapon";
  def->supply_item = qa_q1_weapon_item(weapon);
  def->has_weapon_ammo = ammo != NULL;
  def->weapon_ammo_item = ammo;
  def->weapon_ammo_amount =
      !strcmp(weapon, "nailgun") || !strcmp(weapon, "supernailgun") ? 30 : !strcmp(weapon, "lightning") ? 15 : 5;
}

static bool pickup_definition(qa_q1_services *game, qa_q1_actor *entity, pickup_def *def) {
  memset(def, 0, sizeof *def);
  const char *name = entity->classname ? entity->classname : "";
  const bool big = (flags_of(entity) & (!strcmp(name, "item_weapon") ? 8 : 1)) != 0;
  if (!strcmp(name, "item_health")) {
    const bool mega = !big && (flags_of(entity) & 2) != 0;
    def->mega = mega;
    def->amount = big ? 15 : mega ? 100 : 25;
    def->mega_cap = game->mg3_pickups ? 500 : 250;
    (void)snprintf(def->model, sizeof def->model, "maps/b_bh%d.bsp", big ? 10 : mega ? 100 : 25);
    (void)snprintf(def->sound, sizeof def->sound, "%s",
                   big ? "items/r_item1.wav" : mega ? "items/r_item2.wav" : "items/health1.wav");
    def->bounds = "box";
    def->respawn = mega ? 120 : 20;
    def->take = TAKE_HEALTH;
    return true;
  }
  if (!strcmp(name, "item_armor1") || !strcmp(name, "item_armor2") || !strcmp(name, "item_armorInv")) {
    def->absorption = !strcmp(name, "item_armor1") ? 0.3 : !strcmp(name, "item_armor2") ? 0.6 : 0.8;
    def->points = !strcmp(name, "item_armor1") ? 100 : !strcmp(name, "item_armor2") ? 150 : 200;
    def->item = !strcmp(name, "item_armor1") ? "q1:item_armor1"
                : !strcmp(name, "item_armor2") ? "q1:item_armor2" : "q1:item_armorInv";
    (void)snprintf(def->model, sizeof def->model, "progs/armor.mdl");
    (void)snprintf(def->sound, sizeof def->sound, "items/armor1.wav");
    def->bounds = "weapon";
    def->skin = !strcmp(name, "item_armor1") ? 0 : !strcmp(name, "item_armor2") ? 1 : 2;
    def->respawn = 20;
    def->take = TAKE_ARMOR;
    return true;
  }
  for (size_t i = 0; i < 8; i++) {
    char classname[64];
    (void)snprintf(classname, sizeof classname, "weapon_%s", QA_Q1_WEAPONS[i]);
    if (strcmp(name, classname) != 0) continue;
    const qa_q1_weapon weapon = QA_Q1_WEAPONS[i];
    if (!strcmp(weapon, "axe") || !strcmp(weapon, "shotgun")) break;
    const char *model = !strcmp(weapon, "supershotgun") ? "g_shot"
                        : !strcmp(weapon, "nailgun") ? "g_nail"
                        : !strcmp(weapon, "supernailgun") ? "g_nail2"
                        : !strcmp(weapon, "grenadelauncher") ? "g_rock"
                        : !strcmp(weapon, "rocketlauncher") ? "g_rock2" : "g_light";
    weapon_offer(weapon, def);
    (void)snprintf(def->model, sizeof def->model, "progs/%s.mdl", model);
    (void)snprintf(def->sound, sizeof def->sound, "weapons/pkup.wav");
    def->bounds = "weapon";
    def->respawn = 30;
    def->take = TAKE_WEAPON;
    def->weapon = weapon;
    return true;
  }
  const char *ammo_model = NULL;
  qa_item_id ammo_item_id = NULL;
  double ammo_amount = 0;
  if (!strcmp(name, "item_weapon") && (flags_of(entity) & 2) != 0) {
    ammo_item_id = "q1:ammo/rockets"; ammo_model = "rock"; ammo_amount = big ? 10 : 5;
  } else if (!strcmp(name, "item_weapon") && (flags_of(entity) & 4) != 0) {
    ammo_item_id = "q1:ammo/nails"; ammo_model = "nail"; ammo_amount = big ? 40 : 20;
  } else if (!strcmp(name, "item_weapon") && (flags_of(entity) & 1) != 0) {
    ammo_item_id = "q1:ammo/shells"; ammo_model = "shell"; ammo_amount = big ? 40 : 20;
  } else if (!strcmp(name, "item_shells")) {
    ammo_item_id = "q1:ammo/shells"; ammo_model = "shell"; ammo_amount = big ? 40 : 20;
  } else if (!strcmp(name, "item_spikes")) {
    ammo_item_id = "q1:ammo/nails"; ammo_model = "nail"; ammo_amount = big ? 50 : 25;
  } else if (!strcmp(name, "item_rockets")) {
    ammo_item_id = "q1:ammo/rockets"; ammo_model = "rock"; ammo_amount = big ? 10 : 5;
  } else if (!strcmp(name, "item_cells")) {
    ammo_item_id = "q1:ammo/cells"; ammo_model = "batt";
    ammo_amount = game->mg3_pickups ? (big ? 20 : 10) : big ? 12 : 6;
  }
  if (ammo_item_id != NULL) {
    def->has_supply = true;
    def->supply_kind = "ammo";
    def->supply_item = ammo_item_id;
    def->supply_amount = ammo_amount;
    def->item = ammo_item_id;
    def->amount = ammo_amount;
    (void)snprintf(def->model, sizeof def->model, "maps/b_%s%d.bsp", ammo_model, big ? 1 : 0);
    (void)snprintf(def->sound, sizeof def->sound, "weapons/lock4.wav");
    def->bounds = "box";
    def->respawn = game->options.deathmatch == 3 || game->options.deathmatch == 5 ? 15 : 30;
    def->take = TAKE_AMMO;
    return true;
  }
  if (!strcmp(name, "item_key1") || !strcmp(name, "item_key2")) {
    const bool silver = !strcmp(name, "item_key1");
    def->item = silver ? "q1:key/silver" : "q1:key/gold";
    const double authored = game->source_door_keys ? qa_q1_actor_number(entity, "worldtype") : 0;
    const double world_type = authored > 0 && authored <= 3 ? authored - 1 : game->world_type;
    const char prefix = world_type == 0 ? 'w' : world_type == 1 ? 'm' : 'b';
    (void)snprintf(def->model, sizeof def->model, "progs/%c_%c_key.mdl", prefix, silver ? 's' : 'g');
    (void)snprintf(def->sound, sizeof def->sound, "%s",
                   world_type == 2 ? "misc/basekey.wav"
                   : world_type == 1 ? "misc/runekey.wav" : "misc/medkey.wav");
    def->bounds = "weapon";
    def->respawn = -1;
    def->take = TAKE_KEY;
    return true;
  }
  qa_q1_powerup powerup = NULL;
  const char *powerup_model = NULL, *powerup_sound = NULL;
  if (!strcmp(name, "item_artifact_invulnerability")) {
    powerup = "invulnerability"; powerup_model = "invulner"; powerup_sound = "protect";
  } else if (!strcmp(name, "item_artifact_invisibility")) {
    powerup = "invisibility"; powerup_model = "invisibl"; powerup_sound = "inv1";
  } else if (!strcmp(name, "item_artifact_envirosuit")) {
    powerup = "suit"; powerup_model = "suit"; powerup_sound = "suit";
  } else if (!strcmp(name, "item_artifact_super_damage")) {
    powerup = "quad"; powerup_model = "quaddama"; powerup_sound = "damage";
  }
  if (powerup != NULL) {
    def->powerup = powerup;
    (void)snprintf(def->model, sizeof def->model, "progs/%s.mdl", powerup_model);
    (void)snprintf(def->sound, sizeof def->sound, "items/%s.wav", powerup_sound);
    def->bounds = "artifact";
    def->respawn = !strcmp(powerup, "invulnerability") || !strcmp(powerup, "invisibility") ? 300 : 60;
    def->take = TAKE_POWERUP;
    return true;
  }
  if (!strcmp(name, "item_backpack")) {
    static const char *const FIELDS[4] = {"shells", "nails", "rockets", "cells"};
    static const qa_item_id ITEMS[4] = {"q1:ammo/shells", "q1:ammo/nails", "q1:ammo/rockets", "q1:ammo/cells"};
    for (size_t i = 0; i < 4; i++) {
      def->cargo[def->cargo_count].kind = "counter";
      def->cargo[def->cargo_count].item = ITEMS[i];
      def->cargo[def->cargo_count].count = qa_q1_actor_number(entity, FIELDS[i]);
      def->cargo_count++;
    }
    const char *carried = qa_q1_actor_field_get(entity, "weapon");
    if (carried != NULL && strcmp(carried, "") != 0) {
      /* A map-supplied weapon key the game does not know is ignored rather
       * than failing the pickup. */
      for (size_t i = 0; i < 8; i++)
        if (!strcmp(QA_Q1_WEAPONS[i], carried) || !strcmp(qa_q1_weapon_item(QA_Q1_WEAPONS[i]), carried)) {
          def->cargo[def->cargo_count].kind = "weapon";
          def->cargo[def->cargo_count].item = qa_q1_weapon_item(QA_Q1_WEAPONS[i]);
          def->cargo[def->cargo_count].count = 1;
          def->cargo_count++;
          break;
        }
    }
    def->has_cargo = true;
    (void)snprintf(def->model, sizeof def->model, "progs/backpack.mdl");
    (void)snprintf(def->sound, sizeof def->sound, "weapons/lock4.wav");
    def->bounds = "artifact";
    def->respawn = -1;
    def->take = TAKE_BACKPACK;
    return true;
  }
  return game->foreign_pickup.fn != NULL && game->foreign_pickup.fn(game->foreign_pickup.ctx, entity, def);
}

typedef struct eligibility {
  bool leave, owned;
} eligibility;

static const qa_q3_base_item_definition *q3_item_for(const char *classname) {
  size_t count = 0;
  const qa_q3_base_item_definition *items = qa_q3_base_item_list(QA_Q3_BASE_PRODUCT_MISSIONPACK, &count);
  for (size_t i = 0; i < count; i++)
    if (items[i].class_name != NULL && !strcmp(items[i].class_name, classname)) return &items[i];
  return NULL;
}

static qa_q1_powerup q3_powerup_kind(double tag) {
  switch ((int)tag) {
    case QA_Q3_PW_QUAD: case QA_Q3_PW_DOUBLER: return "quad";
    case QA_Q3_PW_BATTLESUIT: return "suit";
    case QA_Q3_PW_INVIS: return "invisibility";
    case QA_Q3_PW_HASTE: case QA_Q3_PW_SCOUT: return "q3:haste";
    case QA_Q3_PW_REGEN: case QA_Q3_PW_GUARD: return "q3:regen";
    case QA_Q3_PW_FLIGHT: return "q3:flight";
    case QA_Q3_PW_INVULNERABILITY: return "invulnerability";
    default: return NULL;
  }
}

/* TS foreignQ3Pickup: Q3-authored items on a Q1 primary. The authored Q3
 * definition drives model, sound, quantity and respawn; grants land in Q1
 * destinations through the shared Q3 to Q1 supply profile, or the host pickup
 * admission when a selected arsenal owns pickups. */
bool qa_q1_foreign_q3_pickup(void *ctx, const qa_q1_actor *entity, struct qa_q1_pickup_def *def) {
  (void)ctx; /* TS foreignQ3Pickup(entity) */
  const char *name = entity->classname ? entity->classname : "";
  const qa_q3_base_item_definition *item = q3_item_for(name);
  if (item == NULL || (int)item->type == QA_Q3_BASE_IT_BAD) return false;
  (void)snprintf(def->model, sizeof def->model, "%s", item->world_models[0] ? item->world_models[0] : "");
  (void)snprintf(def->sound, sizeof def->sound, "%s", item->pickup_sound ? item->pickup_sound : "items/health1.wav");
  def->bounds = "weapon";
  def->skin = 0;
  def->q3_tag = item->tag;
  def->quantity = entity->count != 0 ? entity->count : item->quantity;
  switch ((int)item->type) {
    case QA_Q3_BASE_IT_HEALTH:
      def->q3_cap = !strcmp(name, "item_health_mega") || !strcmp(name, "item_health_small") ? 200 : 0;
      def->respawn = 35;
      def->take = TAKE_Q3_HEALTH;
      return true;
    case QA_Q3_BASE_IT_ARMOR:
      def->q3_shard = !strcmp(name, "item_armor_shard");
      def->respawn = 25;
      def->take = TAKE_Q3_ARMOR;
      return true;
    case QA_Q3_BASE_IT_AMMO: {
      const qa_q3_weapon_item *weapon = qa_q3_get_weapon_item(item->tag);
      if (weapon == NULL || weapon->ammo == NULL) return false;
      def->has_supply = true;
      def->supply_kind = "ammo";
      def->supply_item = weapon->ammo;
      def->supply_amount = def->quantity;
      def->respawn = 40;
      def->take = TAKE_Q3_AMMO;
      return true;
    }
    case QA_Q3_BASE_IT_WEAPON: {
      const qa_q3_weapon_item *weapon = qa_q3_get_weapon_item(item->tag);
      if (weapon == NULL) return false;
      def->has_supply = true;
      def->supply_kind = "weapon";
      def->supply_item = weapon->item;
      def->has_weapon_ammo = weapon->ammo != NULL;
      def->weapon_ammo_item = weapon->ammo;
      def->weapon_ammo_amount = def->quantity;
      def->respawn = 30;
      def->take = TAKE_Q3_WEAPON;
      return true;
    }
    case QA_Q3_BASE_IT_POWERUP:
    case QA_Q3_BASE_IT_PERSISTANT_POWERUP: {
      const qa_q1_powerup kind = q3_powerup_kind(item->tag);
      if (kind == NULL && (int)item->tag != QA_Q3_PW_AMMOREGEN) return false;
      def->q3_persistent = (int)item->type == QA_Q3_BASE_IT_PERSISTANT_POWERUP;
      def->powerup = kind;
      def->bounds = "artifact";
      def->respawn = def->q3_persistent ? -1 : 120;
      def->take = TAKE_Q3_POWERUP;
      return true;
    }
    case QA_Q3_BASE_IT_HOLDABLE:
      def->respawn = 60;
      def->take = TAKE_Q3_HOLDABLE;
      return true;
    case QA_Q3_BASE_IT_TEAM:
      /* Team objective markers have no objective lifecycle on a non-team Q1
       * primary; they stay visible in place. */
      def->respawn = -1;
      def->take = TAKE_REFUSED;
      return true;
    default:
      return false;
  }
}

/* TS host.combat.read(actor)?.armor.regular; false = undefined. */
static bool read_regular_armor(qa_q1_services *game, qa_actor_id actor, qa_regular_armor_state *out) {
  const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, actor);
  if (!combat.has_value) return false;
  *out = combat.value.armor.regular;
  return true;
}

static eligibility weapon_eligibility(qa_q1_services *game, qa_q1_player_state *player, qa_item_id item) {
  eligibility result;
  const qa_q1_pickup_rules *rules = game->pickup_rules;
  if (rules != NULL && rules->weapon_leave) result.leave = rules->weapon_leave(rules->ctx, game);
  else
    result.leave = game->options.coop || game->options.deathmatch == 2 || game->options.deathmatch == 3 ||
                   game->options.deathmatch == 5;
  if (game->pickup_admission != NULL)
    result.owned = game->pickup_admission->owns(game->pickup_admission->ctx, player->actor.id, item);
  else
    result.owned = qa_shared_inventory_table_count(game->host.inventory, player->actor.id, item) > 0;
  return result;
}

static qa_q1_player_state *pickup_player(qa_q1_services *game, qa_actor_id other) {
  return qa_q1_health(game, other) <= 0 ? NULL : qa_q1_player(game, other);
}

static double weapon_rank(qa_q1_services *game, qa_q1_weapon weapon) {
  const qa_q1_pickup_rules *rules = game->pickup_rules;
  if (rules != NULL && rules->weapon_rank) return rules->weapon_rank(rules->ctx, weapon);
  static const qa_q1_weapon RANK[8] = {"lightning", "rocketlauncher", "supernailgun", "grenadelauncher",
                                       "supershotgun", "nailgun", "shotgun", "axe"};
  for (size_t i = 0; i < 8; i++)
    if (!strcmp(RANK[i], weapon)) return (double)i;
  return -1; /* TS indexOf */
}

void qa_q1_ammo_pickup_selection(qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon before,
                                 bool auto_switch) {
  if (auto_switch && player->weapon != NULL && before != NULL && !strcmp(player->weapon, before))
    (void)qa_q1_select_weapon(game, player->actor, qa_q1_choose_best(game, player->actor));
}

void qa_q1_weapon_pickup_selection(qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon weapon,
                                   const char *selection) {
  double left;
  double right;
  if (selection != NULL && !strcmp(selection, "always"))
    (void)qa_q1_select_weapon(game, player->actor, weapon);
  else if (selection != NULL && !strcmp(selection, "better") &&
           (left = weapon_rank(game, weapon), right = weapon_rank(game, player->weapon), left < right))
    (void)qa_q1_select_weapon(game, player->actor, weapon);
}

static qa_pickup_selection selection_of(const char *selection) {
  return !strcmp(selection, "always") ? QA_PICKUP_SELECTION_ALWAYS
         : !strcmp(selection, "better") ? QA_PICKUP_SELECTION_BETTER : QA_PICKUP_SELECTION_NEVER;
}
/* TS game.pickupAdmission.weapon(actor, {item, ammo}, selection). */
static bool admission_weapon(qa_q1_services *game, qa_owned_actor actor, const pickup_def *offer, const char *selection) {
  const qa_pickup_admission *admission = game->pickup_admission;
  const qa_pickup_ammo_grant grant = {offer->weapon_ammo_item, offer->weapon_ammo_amount};
  const qa_pickup_weapon_offer weapon = {offer->supply_item, offer->has_weapon_ammo ? &grant : NULL,
                                         offer->has_weapon_ammo ? 1 : 0};
  return admission->weapon(admission->ctx, actor, weapon, selection_of(selection));
}
/* TS game.pickupAdmission.ammo(actor, {item, amount}). */
static bool admission_ammo(qa_q1_services *game, qa_owned_actor actor, qa_item_id item, double amount) {
  const qa_pickup_ammo_grant grant = {item, amount};
  return game->pickup_admission->ammo(game->pickup_admission->ctx, actor, grant, false, false);
}

static const char *selection_name(qa_pickup_selection selection) {
  return selection == QA_PICKUP_SELECTION_ALWAYS ? "always"
         : selection == QA_PICKUP_SELECTION_BETTER ? "better" : "never";
}

/* TS item_backpack take without a host pickup admission: a SharedPickupAdmission
 * over the identity "q1:source-backpack" profile, whose weaponGranted runs the
 * Q1 weapon pickup selection. */
typedef struct backpack_grant {
  qa_q1_services *game;
  qa_q1_player_state *player;
} backpack_grant;
static void backpack_ammo_granted(void *ctx, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants,
                                  size_t grant_count, bool auto_switch) {
  (void)ctx; (void)actor; (void)grants; (void)grant_count; (void)auto_switch; /* TS () => undefined */
}
static void backpack_weapon_granted(void *ctx, qa_owned_actor actor, const qa_item_id *weapons, size_t weapon_count,
                                    qa_pickup_selection selection) {
  backpack_grant *grant = ctx;
  (void)actor;
  for (size_t i = 0; i < weapon_count; i++) {
    qa_q1_weapon weapon = NULL;
    for (size_t w = 0; w < 8 && weapon == NULL; w++)
      if (!strcmp(qa_q1_weapon_item(QA_Q1_WEAPONS[w]), weapons[i])) weapon = QA_Q1_WEAPONS[w];
    if (weapon == NULL) qa_range_error("Q1 backpack selected a foreign weapon");
    qa_q1_weapon_pickup_selection(grant->game, grant->player, weapon, selection_name(selection));
  }
}
static bool source_backpack_cargo(qa_q1_services *game, qa_q1_player_state *player,
                                  const qa_pickup_cargo_entry *cargo, size_t cargo_count,
                                  qa_pickup_selection selection) {
  static const qa_item_id AMMO[4] = {"q1:ammo/shells", "q1:ammo/nails", "q1:ammo/rockets", "q1:ammo/cells"};
  static const qa_item_id WEAPON_ITEMS[8] = {"q1:weapon/axe", "q1:weapon/shotgun", "q1:weapon/supershotgun",
                                             "q1:weapon/nailgun", "q1:weapon/supernailgun",
                                             "q1:weapon/grenadelauncher", "q1:weapon/rocketlauncher",
                                             "q1:weapon/lightning"};
  qa_pickup_supply_mapping ammo[4], weapons[8];
  for (size_t i = 0; i < 4; i++) ammo[i] = (qa_pickup_supply_mapping){AMMO[i], &AMMO[i], 1};
  for (size_t i = 0; i < 8; i++) weapons[i] = (qa_pickup_supply_mapping){WEAPON_ITEMS[i], &WEAPON_ITEMS[i], 1};
  backpack_grant grant = {game, player};
  qa_shared_pickup_admission_options options;
  memset(&options, 0, sizeof options);
  options.inventory = game->host.inventory;
  options.profile.id = "q1:source-backpack";
  options.profile.ammo = ammo;
  options.profile.ammo_count = 4;
  options.profile.weapons = weapons;
  options.profile.weapon_count = 8;
  options.ctx = &grant;
  options.ammo_granted = backpack_ammo_granted;
  options.weapon_granted = backpack_weapon_granted;
  qa_shared_pickup_admission *admission = qa_shared_pickup_admission_create(options);
  if (admission == NULL) qa_range_error("Out of memory");
  const bool taken = qa_shared_pickup_admission_cargo(admission, player->actor, cargo, cargo_count, selection);
  qa_shared_pickup_admission_destroy(admission);
  return taken;
}

/* Returns 0 refused, 1 taken, 2 leave. */
static int take_weapon(qa_q1_services *game, qa_q1_player_state *player, qa_q1_weapon weapon) {
  pickup_def offer;
  memset(&offer, 0, sizeof offer);
  weapon_offer(weapon, &offer);
  const eligibility elig = weapon_eligibility(game, player, offer.supply_item);
  if (elig.leave && elig.owned) return 0;
  const qa_q1_pickup_rules *rules = game->pickup_rules;
  if (game->pickup_admission != NULL) {
    bool auto_switch;
    if (rules != NULL && rules->auto_switch) auto_switch = rules->auto_switch(rules->ctx, game, player, elig.owned);
    else
      auto_switch = player->auto_switch != NULL &&
                    (!strcmp(player->auto_switch, "always") ||
                     (!strcmp(player->auto_switch, "new") && !elig.owned));
    const char *selection = !auto_switch ? "never" : game->options.deathmatch == 0 ? "always" : "better";
    const bool accepted = admission_weapon(game, player->actor, &offer, selection);
    return !accepted ? 0 : elig.leave ? 2 : 1;
  }
  (void)qa_shared_inventory_table_give(game->host.inventory, player->actor, offer.supply_item, 1);
  const qa_q1_weapon selected =
      rules != NULL && rules->weapon_granted ? rules->weapon_granted(rules->ctx, game, player, weapon) : weapon;
  if (offer.has_weapon_ammo) {
    const double amount = rules != NULL && rules->weapon_ammo_grant
                              ? rules->weapon_ammo_grant(rules->ctx, game, player, weapon, offer.weapon_ammo_amount)
                              : offer.weapon_ammo_amount;
    (void)qa_shared_inventory_table_give(game->host.inventory, player->actor, offer.weapon_ammo_item, amount);
  }
  bool auto_switch;
  if (rules != NULL && rules->auto_switch) auto_switch = rules->auto_switch(rules->ctx, game, player, elig.owned);
  else
    auto_switch = player->auto_switch != NULL &&
                  (!strcmp(player->auto_switch, "always") ||
                   (!strcmp(player->auto_switch, "new") && !elig.owned));
  if (auto_switch)
    qa_q1_weapon_pickup_selection(game, player, selected, game->options.deathmatch == 0 ? "always" : "better");
  return elig.leave ? 2 : 1;
}

static int take_pickup(qa_q1_services *game, const pickup_def *def,
                       qa_q1_player_state *player) {
  switch (def->take) {
    case TAKE_HEALTH: {
      const double health = qa_q1_health(game, player->actor.id);
      const double limit = def->mega ? def->mega_cap : player->max_health;
      if (health <= 0 || health >= limit) return 0;
      qa_gameplay_authority_set_health(game->host.combat, player->actor, qa_js_min(limit, health + def->amount));
      return 1;
    }
    case TAKE_ARMOR: {
      qa_regular_armor_state armor;
      if (!read_regular_armor(game, player->actor.id, &armor)) armor.kind = QA_REGULAR_ARMOR_NONE;
      if (armor.kind == QA_REGULAR_ARMOR_SOURCE) return 0;
      const double factor = armor.kind == QA_REGULAR_ARMOR_Q1   ? armor.absorption
                            : armor.kind == QA_REGULAR_ARMOR_Q2 ? armor.normal_protection
                                                                : armor.protection;
      const double current = armor.kind == QA_REGULAR_ARMOR_NONE ? 0 : armor.points * factor;
      if (current >= def->absorption * def->points) return 0;
      qa_regular_armor_state next;
      memset(&next, 0, sizeof next);
      next.kind = QA_REGULAR_ARMOR_Q1;
      next.points = def->points;
      next.absorption = def->absorption;
      next.item = def->item;
      qa_gameplay_authority_set_regular_armor(game->host.combat, player->actor, next);
      return 1;
    }
    case TAKE_WEAPON:
      return take_weapon(game, player, def->weapon);
    case TAKE_AMMO: {
      if (game->pickup_admission != NULL)
        return admission_ammo(game, player->actor, def->supply_item, def->supply_amount) ? 1 : 0;
      const qa_q1_weapon best = qa_q1_choose_best(game, player->actor);
      if (qa_shared_inventory_table_give(game->host.inventory, player->actor, def->supply_item, def->supply_amount) == 0) return 0;
      qa_q1_ammo_pickup_selection(game, player, best,
                                  player->auto_switch == NULL || strcmp(player->auto_switch, "never") != 0);
      return 1;
    }
    case TAKE_KEY: {
      if (qa_shared_inventory_table_give(game->host.inventory, player->actor, def->item, 1) == 0) return 0;
      return game->options.coop ? 2 : 1;
    }
    case TAKE_POWERUP: {
      /* MG1 items.qc: with a horde manager present, quad and pentagram last 5 seconds. */
      bool horde = false;
      if (!strcmp(def->powerup, "quad") || !strcmp(def->powerup, "invulnerability"))
        for (size_t i = 0; i < game->entity_count && !horde; i++)
          horde = game->entities[i]->classname != NULL && !strcmp(game->entities[i]->classname, "horde_manager");
      qa_q1_give_powerup(game, player, def->powerup, horde ? 5 : 30);
      return 1;
    }
    case TAKE_BACKPACK: {
      const qa_pickup_selection selection =
          game->options.deathmatch == 0 ? QA_PICKUP_SELECTION_ALWAYS : QA_PICKUP_SELECTION_BETTER;
      if (game->pickup_admission != NULL)
        return game->pickup_admission->cargo(game->pickup_admission->ctx, player->actor, def->cargo, def->cargo_count,
                                             selection) ? 1 : 0;
      return source_backpack_cargo(game, player, def->cargo, def->cargo_count, selection) ? 1 : 0;
    }
    case TAKE_Q3_HEALTH: {
      const double health = qa_q1_health(game, player->actor.id);
      const double limit = def->q3_cap != 0 ? def->q3_cap : player->max_health;
      if (health <= 0 || health >= limit) return 0;
      qa_gameplay_authority_set_health(game->host.combat, player->actor, qa_js_min(limit, health + def->quantity));
      return 1;
    }
    case TAKE_Q3_ARMOR: {
      qa_regular_armor_state armor;
      if (!read_regular_armor(game, player->actor.id, &armor)) armor.kind = QA_REGULAR_ARMOR_NONE;
      if (armor.kind == QA_REGULAR_ARMOR_SOURCE) return 0;
      const double current = armor.kind == QA_REGULAR_ARMOR_NONE ? 0 : armor.points;
      const double points = def->q3_shard ? qa_js_min(200, current + def->quantity) : qa_js_max(current, def->quantity);
      if (points <= current) return 0;
      qa_regular_armor_state next;
      memset(&next, 0, sizeof next);
      next.kind = QA_REGULAR_ARMOR_Q3;
      next.points = points;
      next.protection = 2.0 / 3.0;
      qa_gameplay_authority_set_regular_armor(game->host.combat, player->actor, next);
      return 1;
    }
    case TAKE_Q3_AMMO: {
      if (game->pickup_admission != NULL)
        return admission_ammo(game, player->actor, def->supply_item, def->supply_amount) ? 1 : 0;
      qa_item_id destination = def->supply_item;
      for (size_t i = 0; i < QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE.ammo_count; i++) {
        const qa_pickup_supply_mapping *entry = &QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE.ammo[i];
        if (!strcmp(entry->source, def->supply_item)) {
          if (entry->destination_count > 0) destination = entry->destinations[0];
          break;
        }
      }
      const qa_q1_weapon best = qa_q1_choose_best(game, player->actor);
      if (qa_shared_inventory_table_give(game->host.inventory, player->actor, destination, def->supply_amount) == 0) return 0;
      qa_q1_ammo_pickup_selection(game, player, best,
                                  player->auto_switch == NULL || strcmp(player->auto_switch, "never") != 0);
      return 1;
    }
    case TAKE_Q3_WEAPON: {
      const eligibility elig = weapon_eligibility(game, player, def->supply_item);
      if (elig.leave && elig.owned) return 0;
      if (game->pickup_admission != NULL) {
        const char *selection = player->auto_switch != NULL && !strcmp(player->auto_switch, "never") ? "never"
                                : game->options.deathmatch == 0 ? "always" : "better";
        return !admission_weapon(game, player->actor, def, selection) ? 0 : elig.leave ? 2 : 1;
      }
      const char *mapped = NULL;
      for (size_t i = 0; i < QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE.weapon_count; i++) {
        const qa_pickup_supply_mapping *entry = &QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE.weapons[i];
        if (!strcmp(entry->source, def->supply_item)) {
          if (entry->destination_count > 0 && !strncmp(entry->destinations[0], "q1:weapon/", 10))
            mapped = entry->destinations[0] + 10;
          break;
        }
      }
      if (mapped == NULL) return 0;
      for (size_t i = 0; i < 8; i++)
        if (!strcmp(QA_Q1_WEAPONS[i], mapped)) return take_weapon(game, player, QA_Q1_WEAPONS[i]);
      return 0;
    }
    case TAKE_Q3_POWERUP: {
      if ((int)def->q3_tag == QA_Q3_PW_AMMOREGEN) {
        static const qa_item_id AMMOS[4] = {"q1:ammo/shells", "q1:ammo/nails", "q1:ammo/rockets", "q1:ammo/cells"};
        for (size_t i = 0; i < 4; i++) (void)qa_shared_inventory_table_give(game->host.inventory, player->actor, AMMOS[i], 999);
        return 1;
      }
      if (def->powerup == NULL) return 0;
      qa_q1_give_powerup(game, player, def->powerup, def->q3_persistent ? 3600 : 30);
      if (!strcmp(def->powerup, "q3:flight") && game->host.set_gravity != NULL)
        qa_q1_set_gravity(game, player->actor.id, 0.1);
      if ((int)def->q3_tag == QA_Q3_PW_GUARD)
        qa_gameplay_authority_set_health(game->host.combat, player->actor, qa_js_max(qa_q1_health(game, player->actor.id), 100));
      return 1;
    }
    case TAKE_Q3_HOLDABLE:
      switch ((int)def->q3_tag) {
        case QA_Q3_HI_MEDKIT: {
          const double health = qa_q1_health(game, player->actor.id);
          if (health <= 0 || health >= player->max_health) return 0;
          qa_gameplay_authority_set_health(game->host.combat, player->actor, player->max_health);
          return 1;
        }
        case QA_Q3_HI_INVULNERABILITY:
          qa_q1_give_powerup(game, player, "invulnerability", 30);
          return 1;
        case QA_Q3_HI_KAMIKAZE:
          qa_q1_radius_damage(game, player->actor.id, &player->actor.id, 300, NULL, NULL, "");
          (void)qa_q1_damage(game, player->actor.id, player->actor.id, &player->actor.id, 10000, NULL, "direct", "kamikaze", NULL);
          return 1;
        case QA_Q3_HI_TELEPORTER:
        case QA_Q3_HI_PORTAL: {
          size_t spots = 0;
          for (size_t i = 0; i < game->entity_count; i++) {
            const char *classname = game->entities[i]->classname;
            if (classname != NULL && (!strcmp(classname, "info_player_deathmatch") || !strcmp(classname, "info_player_start")))
              spots++;
          }
          const double index = floor(qa_q1_host_random(game) * (double)spots);
          qa_q1_actor *spot = NULL;
          size_t seen = 0;
          for (size_t i = 0; i < game->entity_count && spot == NULL; i++) {
            const char *classname = game->entities[i]->classname;
            if (classname != NULL && (!strcmp(classname, "info_player_deathmatch") || !strcmp(classname, "info_player_start"))) {
              if ((double)seen == index) spot = game->entities[i];
              seen++;
            }
          }
          const qa_body_state_result read = qa_shared_body_table_read(game->host.bodies, player->actor.id);
          if (spot == NULL || !read.has_value) return 0;
          qa_q1_body patch = qa_q1_body_from_state(read.value);
          patch.origin = qa_q1_get_body(game, spot).origin;
          patch.velocity = QA_Q1_ZERO;
          patch.has_ground = false;
          qa_shared_body_table_write(game->host.bodies, player->actor, qa_q1_body_to_state(&patch));
          qa_shared_body_table_link(game->host.bodies, player->actor, NULL);
          return 1;
        }
        default:
          return 0;
      }
    case TAKE_REFUSED:
      return 0;
  }
  return 0;
}

bool qa_q1_observe_supply(qa_q1_services *game, qa_actor_id pickup, qa_actor_id other,
                          qa_pickup_supply_observation *out) {
  qa_q1_actor *entity = qa_q1_get_entity(game, &pickup);
  if (entity == NULL) return false;
  pickup_def def;
  if (!pickup_definition(game, entity, &def) || !def.has_supply) return false;
  if (entity->touch.fn == NULL || entity->touch.name == NULL || strcmp(entity->touch.name, "item_touch") != 0)
    return false;
  const qa_q1_pickup_rules *rules = game->pickup_rules;
  if (!strcmp(def.supply_kind, "weapon") && game->pickup_admission == NULL && rules != NULL &&
      (rules->weapon_granted != NULL || rules->weapon_ammo_grant != NULL))
    return false;
  memset(out, 0, sizeof *out);
  out->actor = pickup;
  /* TS offer: ammo {item, amount} or weapon {item, ammo}. */
  if (!strcmp(def.supply_kind, "ammo")) {
    out->offer.kind = QA_PICKUP_SUPPLY_AMMO;
    out->offer.grant.item = def.supply_item;
    out->offer.grant.amount = def.supply_amount;
  } else {
    /* TS weapon offer {item, ammo: [] | [{item, amount}]}; the ammo grant
     * lives in game->observed_supply_ammo until the next observation. */
    out->offer.kind = QA_PICKUP_SUPPLY_WEAPON;
    out->offer.item = def.supply_item;
    if (def.has_weapon_ammo) {
      game->observed_supply_ammo.item = def.weapon_ammo_item;
      game->observed_supply_ammo.amount = def.weapon_ammo_amount;
      out->offer.ammo = &game->observed_supply_ammo;
      out->offer.ammo_count = 1;
    }
  }
  if (entity->solid != NULL && !strcmp(entity->solid, "trigger")) {
    qa_q1_player_state *player = pickup_player(game, other);
    bool blocked = false;
    if (player != NULL && !strcmp(def.supply_kind, "weapon")) {
      const eligibility elig = weapon_eligibility(game, player, def.supply_item);
      blocked = elig.leave && elig.owned;
    }
    out->availability_kind = "ready";
    out->eligible = player != NULL && !blocked;
    return true;
  }
  const bool regenerating = entity->next_think >= 0 && entity->think.fn != NULL &&
                            entity->think.name != NULL && !strcmp(entity->think.name, "SUB_regen");
  out->availability_kind = regenerating ? "respawning" : "inactive";
  out->at_seconds = regenerating ? entity->next_think : 0;
  out->eligible = false;
  return true;
}

bool qa_q1_preview_supply(qa_q1_services *game, qa_actor_id pickup, qa_actor_id recipient,
                          qa_pickup_supply_preview *out) {
  qa_pickup_supply_observation observation;
  if (!qa_q1_observe_supply(game, pickup, recipient, &observation)) return false;
  const qa_pickup_supply_offer offer = observation.offer;
  if (game->pickup_admission != NULL) {
    *out = game->pickup_admission->preview(game->pickup_admission->ctx, recipient, offer);
    return true;
  }
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host.inventory, recipient, &entry_count);
  qa_pickup_grant_plan plan;
  memset(&plan, 0, sizeof plan);
  if (offer.kind == QA_PICKUP_SUPPLY_WEAPON) {
    const qa_pickup_ammo_grant weapon = {offer.item, 1};
    plan.is_weapon = true;
    plan.weapons = &weapon;
    plan.weapon_count = 1;
    plan.ammo = offer.ammo;
    plan.ammo_count = offer.ammo_count;
    *out = qa_preview_pickup_grants(entries, entry_count, plan);
    return true;
  }
  if (offer.kind == QA_PICKUP_SUPPLY_AMMO) {
    plan.is_weapon = false;
    plan.acceptance = "nonzero";
    plan.ammo = &offer.grant;
    plan.ammo_count = 1;
    *out = qa_preview_pickup_grants(entries, entry_count, plan);
    return true;
  }
  return false;
}

bool qa_q1_spawn_pickup(qa_q1_services *game, qa_q1_actor *entity) {
  pickup_def def;
  if (!pickup_definition(game, entity, &def)) return false;
  const char *name = entity->classname ? entity->classname : "";
  if (qa_q1_services_uses_id1(game)) {
    if (!strcmp(name, "item_weapon")) {
      const int size = (flags_of(entity) & 8) != 0 ? 1 : 0;
      char model[32];
      if ((flags_of(entity) & 1) != 0) {
        (void)snprintf(model, sizeof model, "maps/b_shell%d.bsp", size);
        (void)qa_q1_services_precache_model(game, model);
      }
      if ((flags_of(entity) & 4) != 0) {
        (void)snprintf(model, sizeof model, "maps/b_nail%d.bsp", size);
        (void)qa_q1_services_precache_model(game, model);
      }
      if ((flags_of(entity) & 2) != 0) {
        (void)snprintf(model, sizeof model, "maps/b_rock%d.bsp", size);
        (void)qa_q1_services_precache_model(game, model);
      }
    } else if (strcmp(name, "item_backpack") != 0) {
      (void)qa_q1_services_precache_model(game, def.model);
    }
    if (!strcmp(name, "item_health") || !strcmp(name, "item_key1") || !strcmp(name, "item_key2")) {
      (void)qa_q1_services_precache_sound(game, def.sound);
    } else if (!strcmp(name, "item_artifact_invulnerability")) {
      (void)qa_q1_services_precache_sound(game, "items/protect.wav");
      (void)qa_q1_services_precache_sound(game, "items/protect2.wav");
      (void)qa_q1_services_precache_sound(game, "items/protect3.wav");
    } else if (!strcmp(name, "item_artifact_envirosuit")) {
      (void)qa_q1_services_precache_sound(game, "items/suit.wav");
      (void)qa_q1_services_precache_sound(game, "items/suit2.wav");
    } else if (!strcmp(name, "item_artifact_invisibility")) {
      (void)qa_q1_services_precache_sound(game, "items/inv1.wav");
      (void)qa_q1_services_precache_sound(game, "items/inv2.wav");
      (void)qa_q1_services_precache_sound(game, "items/inv3.wav");
    } else if (!strcmp(name, "item_artifact_super_damage")) {
      (void)qa_q1_services_precache_sound(game, "items/damage.wav");
      (void)qa_q1_services_precache_sound(game, "items/damage2.wav");
      (void)qa_q1_services_precache_sound(game, "items/damage3.wav");
    }
  }
  set_string(&entity->model, def.model);
  set_string(&entity->original_model, entity->model);
  entity->skin = def.skin;
  entity->solid = "none";
  entity->movement = "none";
  const qa_bounds bounds = !strcmp(def.bounds, "box")
                               ? (qa_bounds){QA_Q1_ZERO, v3(32, 32, 56)}
                               : !strcmp(def.bounds, "weapon")
                                     ? (qa_bounds){v3(-16, -16, 0), v3(16, 16, 56)}
                                     : (qa_bounds){v3(-16, -16, -24), v3(16, 16, 32)};
  qa_q1_set_bounds(game, entity, bounds);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "item_touch");
  if (!strcmp(name, "item_backpack")) {
    entity->solid = "trigger";
    entity->movement = "toss";
    entity->movement_flags = 256;
    const qa_bounds pack_bounds = {v3(-16, -16, 0), v3(16, 16, 56)};
    qa_q1_set_bounds(game, entity, pack_bounds);
    qa_q1_body patch = qa_q1_get_body(game, entity);
    double x = -100 + qa_q1_host_random(game) * 200;
    double y = -100 + qa_q1_host_random(game) * 200;
    patch.velocity = v3(x, y, 300);
    qa_q1_set_body_patch(game, entity, &patch, false, false, true, false, false);
    qa_q1_link(game, entity);
    return true;
  }
  /* StartItem: the MG programs remove coop- and rune-gated items and start
   * SPAWNED ones hidden until used. */
  const char *place = "PlaceItem", *use = NULL;
  if (game->source_item_start.fn != NULL &&
      !game->source_item_start.fn(game->source_item_start.ctx, entity, &place, &use))
    return true;
  if (use != NULL) entity->use = qa_q1_callback_registry_use(game->named, entity, use);
  /* PlaceItem waits until all brush entities exist, lifts six units, then drops 256 units. */
  qa_q1_schedule(game, entity, 0.2, qa_q1_callback_registry_action(game->named, entity, place));
  return true;
}

/* TS touchQ1Pickup's `complete` wrapper: settle only while both actors live. */
typedef struct guarded_touch {
  qa_q1_services *game;
  qa_q1_actor *entity;
  qa_actor_id other;
  qa_q1_pickup_continuation continuation;
} guarded_touch;
static bool guarded_eligible(void *ctx) {
  guarded_touch *guard = ctx;
  return guard->continuation.eligible == NULL || guard->continuation.eligible(guard->continuation.ctx);
}
static bool guarded_original(void *ctx) {
  guarded_touch *guard = ctx;
  return guard->continuation.original(guard->continuation.ctx);
}
static void guarded_complete(void *ctx, bool taken) {
  guarded_touch *guard = ctx;
  if (qa_q1_live(guard->game, guard->entity) && is_live_actor(guard->game, guard->other))
    guard->continuation.complete(guard->continuation.ctx, taken);
}

void qa_q1_touch_pickup(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, qa_item_id item,
                        const char *resource_kind, qa_q1_pickup_continuation continuation) {
  qa_q1_touch_pickup_cargo(game, entity, other, item, resource_kind, continuation, NULL, 0);
}
void qa_q1_touch_pickup_cargo(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, qa_item_id item,
                              const char *resource_kind, qa_q1_pickup_continuation continuation,
                              const qa_pickup_cargo_entry *cargo, size_t cargo_count) {
  if (!qa_q1_live(game, entity) || !is_live_actor(game, other)) return;
  if (game->host.original_pickups != NULL) {
    qa_pickup_resource resource;
    memset(&resource, 0, sizeof resource);
    const bool has_resource = resource_kind != NULL;
    if (has_resource && !strcmp(resource_kind, "protection")) {
      resource.kind = QA_PICKUP_RESOURCE_PROTECTION;
      resource.channel = QA_PROTECTION_REGULAR;
    } else if (has_resource) {
      resource.kind = QA_PICKUP_RESOURCE_INVENTORY;
      resource.item = item;
    }
    qa_original_pickup_offer offer;
    memset(&offer, 0, sizeof offer);
    offer.recipient = other;
    offer.pickup = entity->actor.id;
    offer.source = entity->actor.owner;
    offer.item = item;
    offer.default_resource = has_resource ? &resource : NULL;
    offer.count.is_override = entity->count != 0;
    offer.count.amount = entity->count;
    offer.dropped = entity->classname != NULL && !strcmp(entity->classname, "item_backpack");
    offer.time.kind = QA_SOURCE_TIME_SECONDS;
    offer.time.value = game->time;
    offer.cargo = cargo; /* NULL = TS cargo undefined */
    offer.cargo_count = cargo_count;
    guarded_touch guard = {game, entity, other, continuation};
    const qa_original_pickup_continuation wrapped = {&guard, guarded_eligible, guarded_original, guarded_complete};
    const qa_original_pickup_admission *admission = game->host.original_pickups;
    (void)admission->touch(admission->ctx, offer, wrapped);
    return;
  }
  if (continuation.eligible != NULL && !continuation.eligible(continuation.ctx)) return;
  if (!qa_q1_live(game, entity) || !is_live_actor(game, other)) return;
  const bool taken = continuation.original(continuation.ctx);
  if (qa_q1_live(game, entity) && is_live_actor(game, other))
    continuation.complete(continuation.ctx, taken);
}

typedef struct touch_ctx {
  qa_q1_services *game;
  qa_q1_actor *entity;
  qa_actor_id other;
  qa_q1_player_state *player;
  const pickup_def *def;
  qa_item_id key;
  bool shared;
  bool leave, original_ran;
} touch_ctx;

static const char *pickup_item_name(const char *name) {
  static const char *const NAMES[][2] = {
      {"weapon_supershotgun", "$qc_double_shotgun"}, {"weapon_nailgun", "$qc_nailgun"},
      {"weapon_supernailgun", "$qc_super_nailgun"}, {"weapon_grenadelauncher", "$qc_grenade_launcher"},
      {"weapon_rocketlauncher", "$qc_rocket_launcher"}, {"weapon_lightning", "$qc_thunderbolt"},
      {"item_shells", "$qc_shells"}, {"item_spikes", "$qc_nails"}, {"item_rockets", "$qc_rockets"},
      {"item_cells", "$qc_cells"}, {"item_artifact_invulnerability", "$qc_pentagram_of_protection"},
      {"item_artifact_invisibility", "$qc_ring_of_shadows"}, {"item_artifact_envirosuit", "$qc_biosuit"},
      {"item_artifact_super_damage", "$qc_quad_damage"}};
  for (size_t i = 0; i < sizeof NAMES / sizeof *NAMES; i++)
    if (!strcmp(NAMES[i][0], name)) return NAMES[i][1];
  return NULL;
}
/* items.qc prints one line per accepted pickup. */
static void send_pickup_message(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  const char *name = entity->classname != NULL ? entity->classname : "";
  const bool big = (flags_of(entity) & 1) != 0;
  if (!strcmp(name, "item_health")) {
    const qa_q1_msg_arg amount = {true, big ? 15 : (flags_of(entity) & 2) != 0 ? 100 : 25, NULL};
    qa_q1_message(game, &other, "$qc_item_health", false, &amount, 1);
    return;
  }
  if (!strcmp(name, "item_armor1") || !strcmp(name, "item_armor2") || !strcmp(name, "item_armorInv")) {
    qa_q1_message(game, &other, "$qc_item_armor", false, NULL, 0);
    return;
  }
  if (!strcmp(name, "item_key1") || !strcmp(name, "item_key2")) {
    char key[48];
    (void)snprintf(key, sizeof key, "$qc_%s_%s", !strcmp(name, "item_key1") ? "silver" : "gold",
                   game->world_type == 2 ? "keycard" : game->world_type == 1 ? "runekey" : "key");
    const qa_q1_msg_arg arg = {false, 0, key};
    qa_q1_message(game, &other, "$qc_got_item", false, &arg, 1);
    return;
  }
  const char *item = pickup_item_name(name);
  if (item == NULL) return;
  const qa_q1_msg_arg arg = {false, 0, item};
  qa_q1_message(game, &other, "$qc_got_item", false, &arg, 1);
}

static bool touch_live(touch_ctx *touch) {
  return qa_q1_live(touch->game, touch->entity) && is_live_actor(touch->game, touch->other);
}
static void schedule_named(qa_q1_services *game, qa_q1_actor *entity, double delay, const char *name) {
  qa_q1_schedule(game, entity, delay, qa_q1_callback_registry_action(game->named, entity, name));
}

static bool touch_original(void *ctx) {
  touch_ctx *touch = ctx;
  touch->original_ran = true;
  const int result = take_pickup(touch->game, touch->def, touch->player);
  touch->leave = result == 2;
  return result != 0;
}
static void touch_complete(void *ctx, bool taken) {
  touch_ctx *touch = ctx;
  qa_q1_services *game = touch->game;
  qa_q1_actor *entity = touch->entity;
  const pickup_def *def = touch->def;
  qa_q1_player_state *player = touch->player;
  const bool item_start = game->source_item_start.fn != NULL;
  if (!taken) return;
  if (touch->shared) (void)qa_q1_actor_reference_set(entity, "enemy", &touch->other);
  if (!touch->original_ran) {
    if (def->has_supply && !strcmp(def->supply_kind, "weapon")) {
      const qa_q1_pickup_rules *rules = game->pickup_rules;
      touch->leave = rules != NULL && rules->weapon_leave
                         ? rules->weapon_leave(rules->ctx, game)
                         : game->options.coop || game->options.deathmatch == 2 || game->options.deathmatch == 3 ||
                               game->options.deathmatch == 5;
    } else {
      touch->leave = touch->key != NULL && game->options.coop;
    }
  }
  if (!touch_live(touch)) return;
  send_pickup_message(game, entity, touch->other);
  if (!touch_live(touch)) return;
  if (edition_is(game, "rerelease") && !strcmp(entity->classname, "item_health") &&
      (flags_of(entity) & 3) == 2 && !game->source_no_mega_rot)
    player->mega_rot_at = game->time + 5;
  qa_q1_sound(game, player->actor.id, def->sound, "item", 1, 1);
  if (!touch_live(touch)) return;
  qa_q1_effect(game, "pickup", qa_q1_get_body(game, entity).origin, &player->actor.id, 1);
  if (!touch_live(touch)) return;
  if (def->has_cargo) {
    qa_q1_remove(game, entity);
    return;
  }
  if (touch->leave) {
    /* MG weapon_touch fires the targets before the leave return, once. */
    if (strncmp(entity->classname, "weapon_", 7) != 0) {
      qa_q1_use_targets(game, entity, &touch->other);
      if (touch->key != NULL && item_start) set_string(&entity->target, "");
    } else if (item_start) {
      qa_q1_use_targets(game, entity, &touch->other);
      set_string(&entity->target, "");
    }
    return;
  }
  entity->solid = "none";
  set_string(&entity->model, "");
  qa_q1_link(game, entity);
  if (!touch_live(touch)) return;
  const qa_q1_pickup_rules *rules = game->pickup_rules;
  const double respawn = rules != NULL && rules->respawn
                             ? rules->respawn(rules->ctx, game, entity, def->respawn)
                             : def->respawn;
  if (!touch_live(touch)) return;
  const bool respawns = game->options.deathmatch != 0 && respawn > 0 &&
                        (game->options.deathmatch != 2 || strncmp(entity->classname, "item_artifact_", 14) == 0);
  if ((edition_is(game, "classic") || item_start) && !strcmp(entity->classname, "item_health") &&
      (flags_of(entity) & 3) == 2) {
    entity->has_owner = true;
    entity->owner = player->actor.id;
    schedule_named(game, entity, 5, "health_rot");
  } else if (item_start && game->options.coop && !strcmp(entity->classname, "item_artifact_envirosuit")) {
    if (touch_live(touch)) qa_q1_use_targets(game, entity, &touch->other);
    set_string(&entity->target, "");
    schedule_named(game, entity, 2.5, "SUB_regen");
    return;
  } else if (respawns) {
    schedule_named(game, entity, respawn, "SUB_regen");
  } else if (touch->shared && entity->wait <= 0) {
    entity->count++;
    size_t player_count = 0;
    (void)game->host.players(game->host.ctx, &player_count);
    if (entity->count < (double)player_count) {
      schedule_named(game, entity, 1, "SUB_regen");
    } else {
      (void)qa_q1_actor_reference_set(entity, "enemy", NULL);
      entity->count = 0;
      schedule_named(game, entity, 6 * 60, "SUB_regen");
    }
  } else if (item_start && entity->wait > 0) {
    (void)qa_q1_actor_reference_set(entity, "enemy", NULL);
    schedule_named(game, entity, entity->wait, "SUB_regen");
  } else {
    qa_q1_cancel(game, entity);
  }
  if (touch_live(touch)) qa_q1_use_targets(game, entity, &touch->other);
}

/* TS `q1:${classname}` is a value the admission host may keep; the C id is
 * interned for the process instead of living in the touch frame. */
static qa_item_id interned_class_item(const char *classname) {
  static char **table = NULL;
  static size_t count = 0, capacity = 0;
  char text[160];
  (void)snprintf(text, sizeof text, "q1:%s", classname);
  for (size_t i = 0; i < count; i++)
    if (strcmp(table[i], text) == 0) return table[i];
  if (count == capacity) {
    size_t next = capacity == 0 ? 16 : capacity * 2;
    char **grown = (char **)realloc(table, next * sizeof *grown);
    if (grown == NULL) qa_range_error("out of memory");
    table = grown;
    capacity = next;
  }
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy == NULL) qa_range_error("out of memory");
  memcpy(copy, text, length);
  table[count++] = copy;
  return copy;
}

static void pickup_touch(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  pickup_def def;
  if (!pickup_definition(game, entity, &def))
    qa_range_error("Unknown saved Q1 pickup: %s", entity->classname ? entity->classname : "");
  if (entity->solid == NULL || strcmp(entity->solid, "trigger") != 0) return;
  qa_q1_player_state *player = pickup_player(game, other);
  if (player == NULL) return;
  qa_item_id key = !strcmp(entity->classname, "item_key1") ? "q1:key/silver"
                   : !strcmp(entity->classname, "item_key2") ? "q1:key/gold" : NULL;
  qa_item_id item = def.has_supply ? def.supply_item : key;
  if (item == NULL) item = interned_class_item(entity->classname);
  const bool armor = !strcmp(entity->classname, "item_armor1") || !strcmp(entity->classname, "item_armor2") ||
                     !strcmp(entity->classname, "item_armorInv");
  const char *resource = armor ? "protection" : (def.has_supply || key != NULL) ? "inventory" : NULL;
  /* COOP_RESPAWN_ITEMS_FOR_PLAYERS: one player cannot collect every respawn of a coop item. */
  const bool shared = game->source_item_start.fn != NULL && game->options.coop &&
                      strncmp(entity->classname, "weapon_", 7) != 0 &&
                      strncmp(entity->classname, "item_key", 8) != 0 &&
                      strcmp(entity->classname, "item_backpack") != 0;
  qa_actor_id previous;
  if (shared && entity->wait <= 0 && qa_q1_actor_reference_get(entity, "enemy", &previous) &&
      qa_same_actor(previous, other))
    return;
  touch_ctx ctx = {game, entity, other, player, &def, key, shared, false, false};
  const qa_q1_pickup_continuation continuation = {&ctx, touch_original, touch_complete, NULL};
  qa_q1_touch_pickup_cargo(game, entity, other, item, resource, continuation,
                           def.has_cargo ? def.cargo : NULL, def.has_cargo ? def.cargo_count : 0);
}

bool qa_q1_give_pickup(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other) {
  pickup_def def;
  if (!pickup_definition(game, entity, &def)) return false;
  entity->solid = "trigger";
  pickup_touch(game, entity, other);
  return true;
}

static void item_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  pickup_touch(game, entity, other);
}
static void place_item(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 start = qa_q1_vadd(body.origin, v3(0, 0, 6));
  if (game->source_item_start.fn != NULL && (flags_of(entity) & 65536) != 0) {
    /* DROPTOFLOOR_DISABLE: the item keeps its authored height plus the
     * six-unit lift and never drops or falls out of the level. */
    entity->solid = "trigger";
    entity->movement = "none";
    entity->movement_flags = 256;
    qa_q1_body patch = body;
    patch.origin = start;
    patch.velocity = QA_Q1_ZERO;
    qa_q1_set_body_patch(game, entity, &patch, true, false, true, false, false);
    qa_q1_link(game, entity);
    return;
  }
  qa_q1_trace_request request = {start, qa_q1_vadd(start, v3(0, 0, -256)), body.bounds,
                                 true, entity->actor.id, true, false, false};
  const qa_q1_trace trace = qa_q1_host_trace(game, request);
  if (trace.all_solid || trace.fraction == 1) {
    qa_q1_remove(game, entity);
    return;
  }
  entity->solid = "trigger";
  entity->movement = "toss";
  entity->movement_flags = 256 | 512;
  qa_q1_body patch = body;
  patch.origin = trace.end;
  patch.velocity = QA_Q1_ZERO;
  patch.has_ground = trace.has_actor;
  if (trace.has_actor) patch.ground = trace.actor;
  qa_q1_set_body_patch(game, entity, &patch, true, false, true, false, true);
  qa_q1_link(game, entity);
}
static void sub_regen(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->solid = "trigger";
  set_string(&entity->model, entity->original_model ? entity->original_model : "");
  qa_q1_sound_defaults(game, entity->actor.id, "items/itembk2.wav");
  qa_q1_link(game, entity);
}
static void health_rot(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_player_state *player = entity->has_owner ? qa_q1_player(game, entity->owner) : NULL;
  if (player != NULL && qa_q1_health(game, player->actor.id) > player->max_health) {
    qa_gameplay_authority_set_health(game->host.combat, player->actor, qa_q1_health(game, player->actor.id) - 1);
    qa_q1_schedule(game, entity, 1, qa_q1_callback_registry_action(game->named, entity, "health_rot"));
    return;
  }
  const bool regenerates = game->source_item_start.fn != NULL
                               ? game->options.deathmatch != 0 && game->options.deathmatch != 2
                               : game->options.deathmatch == 1;
  if (regenerates)
    qa_q1_schedule(game, entity, 20, qa_q1_callback_registry_action(game->named, entity, "SUB_regen"));
}

void qa_q1_register_pickup_callbacks(qa_q1_services *game) {
  qa_q1_callback_handlers h = {0};
  h.touch = item_touch;
  qa_q1_callback_registry_register(game->named, "item_touch", h);
  h = (qa_q1_callback_handlers){0}; h.action = place_item;
  qa_q1_callback_registry_register(game->named, "PlaceItem", h);
  h = (qa_q1_callback_handlers){0}; h.action = sub_regen;
  qa_q1_callback_registry_register(game->named, "SUB_regen", h);
  h = (qa_q1_callback_handlers){0}; h.action = health_rot;
  qa_q1_callback_registry_register(game->named, "health_rot", h);
}
