/*
 * Port of src/content/q1/foundation/pickups.ts. GPL-2.0-or-later.
 * Mirrors per the header note: foreign Q3 pickups resolve to no
 * definition (the Q3 item tables have no qa_q1_services binding yet);
 * combat armor has no actor-local mirror, so armor takes observe a
 * "none" regular armor and the grant lands only in the host combat
 * store once it is bound; the C pickup-admission mirror passes the
 * weapon item only (its ammo list is granted by the native path);
 * preview mirrors the observation subset with preview acceptance in
 * `eligible`. Backpack cargo follows SharedPickupAdmission's identity
 * profile: weapons, then counters, then selection, always taken.
 */
#include "qa/content/q1/foundation/pickups.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

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
static long flags_of(const qa_q1_actor *entity) { return (long)entity->spawnflags; }
static bool is_live_actor(qa_q1_services *game, qa_actor_id actor) {
  return qa_q1_get_entity(game, &actor) != NULL || qa_q1_is_player(game, &actor);
}
static bool edition_is(qa_q1_services *game, const char *edition) {
  return game->options.edition != NULL && !strcmp(game->options.edition, edition);
}

typedef enum take_kind {
  TAKE_HEALTH, TAKE_ARMOR, TAKE_WEAPON, TAKE_AMMO, TAKE_KEY, TAKE_POWERUP, TAKE_BACKPACK
} take_kind;

typedef struct pickup_def {
  bool has_supply;
  const char *supply_kind; /* weapon|ammo */
  qa_item_id supply_item;
  double supply_amount;              /* ammo offer amount */
  qa_item_id weapon_ammo_item;       /* weapon offer */
  double weapon_ammo_amount;
  bool has_weapon_ammo;
  bool has_cargo;
  char model[80];
  char sound[80];
  const char *bounds; /* box|weapon|artifact */
  double skin, respawn;
  take_kind take;
  /* take parameters */
  bool mega;
  double amount, absorption, points;
  qa_item_id item;
  qa_q1_weapon weapon;
  qa_q1_powerup powerup;
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
    ammo_item_id = "q1:ammo/cells"; ammo_model = "batt"; ammo_amount = big ? 12 : 6;
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
    const char prefix = game->world_type == 0 ? 'w' : game->world_type == 1 ? 'm' : 'b';
    (void)snprintf(def->model, sizeof def->model, "progs/%c_%c_key.mdl", prefix, silver ? 's' : 'g');
    (void)snprintf(def->sound, sizeof def->sound, "%s",
                   game->world_type == 2 ? "misc/basekey.wav"
                   : game->world_type == 1 ? "misc/runekey.wav" : "misc/medkey.wav");
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
    const char *carried = qa_q1_actor_field_get(entity, "weapon");
    if (carried != NULL && strcmp(carried, "") != 0) {
      bool found = false;
      for (size_t i = 0; i < 8; i++)
        if (!strcmp(QA_Q1_WEAPONS[i], carried) || !strcmp(qa_q1_weapon_item(QA_Q1_WEAPONS[i]), carried))
          found = true;
      if (!found) qa_range_error("Unsupported Q1 backpack weapon %s", carried);
    }
    def->has_cargo = true;
    (void)snprintf(def->model, sizeof def->model, "progs/backpack.mdl");
    (void)snprintf(def->sound, sizeof def->sound, "weapons/lock4.wav");
    def->bounds = "artifact";
    def->respawn = -1;
    def->take = TAKE_BACKPACK;
    return true;
  }
  return false; /* foreignQ3Pickup: no definition in this actor model */
}

typedef struct eligibility {
  bool leave, owned;
} eligibility;

static eligibility weapon_eligibility(qa_q1_services *game, qa_q1_player_state *player, qa_item_id item) {
  eligibility result;
  const qa_q1_pickup_rules *rules = game->pickup_rules;
  if (rules != NULL && rules->weapon_leave) result.leave = rules->weapon_leave(rules->ctx, game);
  else
    result.leave = game->options.coop || game->options.deathmatch == 2 || game->options.deathmatch == 3 ||
                   game->options.deathmatch == 5;
  if (game->pickup_admission != NULL && game->pickup_admission->owns)
    result.owned = game->pickup_admission->owns(game->pickup_admission->ctx, player->actor.id, item);
  else
    result.owned = qa_q1_inventory_count(game, player->actor.id, item) > 0;
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
  if (selection != NULL && !strcmp(selection, "always"))
    (void)qa_q1_select_weapon(game, player->actor, weapon);
  else if (selection != NULL && !strcmp(selection, "better") &&
           weapon_rank(game, weapon) < weapon_rank(game, player->weapon))
    (void)qa_q1_select_weapon(game, player->actor, weapon);
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
    const bool accepted = game->pickup_admission->weapon != NULL &&
                          game->pickup_admission->weapon(game->pickup_admission->ctx, player->actor,
                                                         offer.supply_item, selection);
    return !accepted ? 0 : elig.leave ? 2 : 1;
  }
  (void)qa_q1_inventory_give(game, player->actor, offer.supply_item, 1);
  const qa_q1_weapon selected =
      rules != NULL && rules->weapon_granted ? rules->weapon_granted(rules->ctx, game, player, weapon) : weapon;
  if (offer.has_weapon_ammo) {
    const double amount = rules != NULL && rules->weapon_ammo_grant
                              ? rules->weapon_ammo_grant(rules->ctx, game, player, weapon, offer.weapon_ammo_amount)
                              : offer.weapon_ammo_amount;
    (void)qa_q1_inventory_give(game, player->actor, offer.weapon_ammo_item, amount);
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

static int take_pickup(qa_q1_services *game, qa_q1_actor *entity, const pickup_def *def,
                       qa_q1_player_state *player) {
  switch (def->take) {
    case TAKE_HEALTH: {
      const double health = qa_q1_health(game, player->actor.id);
      const double limit = def->mega ? 250 : player->max_health;
      if (health <= 0 || health >= limit) return 0;
      qa_q1_set_health(game, player->actor, fmin(limit, health + def->amount));
      return 1;
    }
    case TAKE_ARMOR: {
      /* No regular-armor mirror on the actor (see file note): current
       * reads as "none", so the TS refusal cannot fire here. */
      const double current = 0;
      if (current >= def->absorption * def->points) return 0;
      return 1;
    }
    case TAKE_WEAPON:
      return take_weapon(game, player, def->weapon);
    case TAKE_AMMO: {
      if (game->pickup_admission != NULL)
        return game->pickup_admission->ammo != NULL &&
                       game->pickup_admission->ammo(game->pickup_admission->ctx, player->actor,
                                                     def->supply_item, def->supply_amount)
                   ? 1 : 0;
      const qa_q1_weapon best = qa_q1_choose_best(game, player->actor);
      if (qa_q1_inventory_give(game, player->actor, def->supply_item, def->supply_amount) == 0) return 0;
      qa_q1_ammo_pickup_selection(game, player, best,
                                  player->auto_switch == NULL || strcmp(player->auto_switch, "never") != 0);
      return 1;
    }
    case TAKE_KEY: {
      if (qa_q1_inventory_give(game, player->actor, def->item, 1) == 0) return 0;
      return game->options.coop ? 2 : 1;
    }
    case TAKE_POWERUP:
      qa_q1_give_powerup(game, player, def->powerup, 30);
      return 1;
    case TAKE_BACKPACK: {
      static const char *const FIELDS[4] = {"shells", "nails", "rockets", "cells"};
      static const qa_item_id ITEMS[4] = {"q1:ammo/shells", "q1:ammo/nails", "q1:ammo/rockets", "q1:ammo/cells"};
      const char *selection = game->options.deathmatch == 0 ? "always" : "better";
      if (game->pickup_admission != NULL)
        return game->pickup_admission->cargo_taken != NULL &&
                       game->pickup_admission->cargo_taken(game->pickup_admission->ctx, player->actor, selection)
                   ? 1 : 0;
      qa_q1_weapon carried_weapon = NULL;
      const char *carried = qa_q1_actor_field_get(entity, "weapon");
      if (carried != NULL && strcmp(carried, "") != 0) {
        for (size_t i = 0; i < 8; i++)
          if (!strcmp(QA_Q1_WEAPONS[i], carried) || !strcmp(qa_q1_weapon_item(QA_Q1_WEAPONS[i]), carried))
            carried_weapon = QA_Q1_WEAPONS[i];
        if (carried_weapon == NULL) qa_range_error("Unsupported Q1 backpack weapon %s", carried);
      }
      if (carried_weapon != NULL) (void)qa_q1_inventory_give(game, player->actor, qa_q1_weapon_item(carried_weapon), 1);
      for (size_t i = 0; i < 4; i++)
        (void)qa_q1_inventory_give(game, player->actor, ITEMS[i], qa_q1_actor_number(entity, FIELDS[i]));
      if (carried_weapon != NULL) qa_q1_weapon_pickup_selection(game, player, carried_weapon, selection);
      return 1;
    }
  }
  return 0;
}

bool qa_q1_observe_supply(qa_q1_services *game, qa_actor_id pickup, qa_actor_id other,
                          qa_q1_supply_observation *out) {
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
  out->kind = def.supply_kind;
  out->item = def.supply_item;
  out->amount = !strcmp(def.supply_kind, "ammo") ? def.supply_amount : 1;
  if (entity->solid != NULL && !strcmp(entity->solid, "trigger")) {
    qa_q1_player_state *player = pickup_player(game, other);
    bool blocked = false;
    if (player != NULL && !strcmp(def.supply_kind, "weapon")) {
      const eligibility elig = weapon_eligibility(game, player, def.supply_item);
      blocked = elig.leave && elig.owned;
    }
    out->availability = "ready";
    out->eligible = player != NULL && !blocked;
    return true;
  }
  const bool regenerating = entity->next_think >= 0 && entity->think.fn != NULL &&
                            entity->think.name != NULL && !strcmp(entity->think.name, "SUB_regen");
  out->availability = regenerating ? "respawning" : "inactive";
  out->at_seconds = regenerating ? entity->next_think : 0;
  out->eligible = false;
  return true;
}

bool qa_q1_preview_supply(qa_q1_services *game, qa_actor_id pickup, qa_actor_id recipient,
                          qa_q1_supply_observation *out) {
  if (!qa_q1_observe_supply(game, pickup, recipient, out)) return false;
  if (game->pickup_admission != NULL) return true; /* admission preview: subset */
  if (!strcmp(out->kind, "weapon")) {
    out->eligible = true; /* TS weapon preview is always accepted */
    return true;
  }
  /* TS ammo preview with "nonzero" acceptance over inventory entries. */
  double given = out->amount;
  double capacity = 0;
  if (qa_q1_inventory_capacity(game, recipient, out->item, &capacity)) {
    const double current = qa_q1_inventory_count(game, recipient, out->item);
    given = fmin(out->amount, fmax(0, capacity - current));
  }
  out->eligible = given != 0;
  return true;
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
    patch.velocity = v3(-100 + qa_q1_host_random(game) * 200, -100 + qa_q1_host_random(game) * 200, 300);
    qa_q1_set_body_patch(game, entity, &patch, false, false, true, false, false);
    qa_q1_link(game, entity);
    return true;
  }
  qa_q1_schedule(game, entity, 0.2, qa_q1_callback_registry_action(game->named, entity, "PlaceItem"));
  return true;
}

void qa_q1_touch_pickup(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, qa_item_id item,
                        const char *resource_kind, qa_q1_pickup_continuation continuation) {
  (void)item;
  (void)resource_kind;
  /* The qa_q1_host mirror has no originalPickups seam, so the TS
   * first branch always runs: eligible && live -> complete(original()). */
  if (!qa_q1_live(game, entity) || !is_live_actor(game, other)) return;
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
  bool leave, original_ran;
} touch_ctx;

static bool touch_original(void *ctx) {
  touch_ctx *touch = ctx;
  touch->original_ran = true;
  const int result = take_pickup(touch->game, touch->entity, touch->def, touch->player);
  touch->leave = result == 2;
  return result != 0;
}
static void touch_complete(void *ctx, bool taken) {
  touch_ctx *touch = ctx;
  qa_q1_services *game = touch->game;
  qa_q1_actor *entity = touch->entity;
  const pickup_def *def = touch->def;
  qa_q1_player_state *player = touch->player;
  if (!taken) return;
  if (!touch->original_ran) {
    if (def->has_supply && !strcmp(def->supply_kind, "weapon")) {
      const qa_q1_pickup_rules *rules = game->pickup_rules;
      touch->leave = rules != NULL && rules->weapon_leave ? rules->weapon_leave(rules->ctx, game)
                                                          : weapon_eligibility(game, player, def->supply_item).leave;
    } else {
      touch->leave = touch->key != NULL && game->options.coop;
    }
  }
  if (!qa_q1_live(game, entity) || !is_live_actor(game, touch->other)) return;
  if (edition_is(game, "rerelease") && !strcmp(entity->classname, "item_health") &&
      (flags_of(entity) & 3) == 2)
    player->mega_rot_at = game->time + 5;
  qa_q1_sound(game, player->actor.id, def->sound, "item", 1, 1);
  if (!qa_q1_live(game, entity) || !is_live_actor(game, touch->other)) return;
  qa_q1_effect(game, "pickup", qa_q1_get_body(game, entity).origin, &player->actor.id, 1);
  if (!qa_q1_live(game, entity) || !is_live_actor(game, touch->other)) return;
  if (def->has_cargo) {
    qa_q1_remove(game, entity);
    return;
  }
  if (touch->leave) {
    if (strncmp(entity->classname, "weapon_", 7) != 0) qa_q1_use_targets(game, entity, &touch->other);
    return;
  }
  entity->solid = "none";
  set_string(&entity->model, "");
  qa_q1_link(game, entity);
  if (!qa_q1_live(game, entity) || !is_live_actor(game, touch->other)) return;
  const qa_q1_pickup_rules *rules = game->pickup_rules;
  const double respawn = rules != NULL && rules->respawn
                             ? rules->respawn(rules->ctx, game, entity, def->respawn)
                             : def->respawn;
  if (!qa_q1_live(game, entity) || !is_live_actor(game, touch->other)) return;
  const bool respawns = game->options.deathmatch != 0 && respawn > 0 &&
                        (game->options.deathmatch != 2 || strncmp(entity->classname, "item_artifact_", 14) == 0);
  if (edition_is(game, "classic") && !strcmp(entity->classname, "item_health") && (flags_of(entity) & 3) == 2) {
    entity->has_owner = true;
    entity->owner = player->actor.id;
    qa_q1_schedule(game, entity, 5, qa_q1_callback_registry_action(game->named, entity, "health_rot"));
  } else if (respawns) {
    qa_q1_schedule(game, entity, respawn, qa_q1_callback_registry_action(game->named, entity, "SUB_regen"));
  } else {
    qa_q1_cancel(game, entity);
  }
  if (qa_q1_live(game, entity) && is_live_actor(game, touch->other))
    qa_q1_use_targets(game, entity, &touch->other);
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
  char fallback_item[96];
  qa_item_id item = def.has_supply ? def.supply_item : key;
  if (item == NULL) {
    (void)snprintf(fallback_item, sizeof fallback_item, "q1:%s", entity->classname);
    item = fallback_item;
  }
  const bool armor = !strcmp(entity->classname, "item_armor1") || !strcmp(entity->classname, "item_armor2") ||
                     !strcmp(entity->classname, "item_armorInv");
  const char *resource = armor ? "protection" : (def.has_supply || key != NULL) ? "inventory" : NULL;
  touch_ctx ctx = {game, entity, other, player, &def, key, false, false};
  const qa_q1_pickup_continuation continuation = {&ctx, touch_original, touch_complete, NULL};
  qa_q1_touch_pickup(game, entity, other, item, resource, continuation);
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
    qa_q1_set_health(game, player->actor, qa_q1_health(game, player->actor.id) - 1);
    qa_q1_schedule(game, entity, 1, qa_q1_callback_registry_action(game->named, entity, "health_rot"));
    return;
  }
  if (game->options.deathmatch == 1)
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
