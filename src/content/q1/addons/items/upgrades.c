/* Port of src/content/q1/addons/items/upgrades.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/upgrades.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/addons/items/common.h"
#include "qa/content/q1/addons/items/pickups.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

typedef struct {
  const char *type, *parm;
  double base, deathmatch;
  const char *model, *label;
  qa_item_id ammo;
} upgrade_def;
static const upgrade_def UPGRADES[] = {
  {"health", "parm10", 50, 100, "item_h_player", "health", NULL},
  {"shells", "parm11", 50, 100, "backpackshells", "shell", "q1:ammo/shells"},
  {"nails", "parm12", 100, 200, "backpacknails", "nail", "q1:ammo/nails"},
  {"rockets", "parm13", 20, 100, "backpacker", "rocket", "q1:ammo/rockets"},
  {"cells", "parm14", 100, 200, "backpackcells", "cell", "q1:ammo/cells"},
};
#define UPGRADE_COUNT (sizeof UPGRADES / sizeof UPGRADES[0])
static const char *const TRAVEL_PARMS[] = {"parm10", "parm11", "parm12", "parm13", "parm14", "parm15"};

uint8_t *qa_q1ad_capture_mg3_upgrade_travel(qa_q1ad_context *context, qa_q1ad_player *player, size_t *out_length) {
  qa_save_value *list = qa_save_value_array();
  if (list == NULL) return NULL;
  for (size_t i = 0; i < 6; i++) {
    qa_save_value *value = qa_save_value_number(qa_q1ad_player_number(context, player->actor.id, TRAVEL_PARMS[i]));
    if (value == NULL || !qa_save_value_push(list, value)) {
      qa_save_value_free(value);
      qa_save_value_free(list);
      return NULL;
    }
  }
  uint8_t *bytes = qa_encode_checkpoint_value(list, out_length);
  qa_save_value_free(list);
  return bytes;
}

void qa_q1ad_restore_mg3_upgrade_travel(qa_q1ad_context *context, qa_q1ad_player *player,
                                        const uint8_t *bytes, size_t length) {
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "mg3:upgrade-travel"))
    qa_save_reader_fail(&reader, "out of memory");
  size_t count = qa_save_reader_list_count(&reader);
  double values[6];
  for (size_t i = 0; i < count && i < 6; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&reader, i, &item)) qa_save_reader_fail(&reader, "out of memory");
    double number = qa_save_reader_integer_min(&item, 0);
    if (number > 8388607) qa_save_reader_fail(&item, "MG3 source parameter exceeds its flag word");
    values[i] = number;
    qa_save_reader_destroy(&item);
  }
  if (count != 6) qa_save_reader_fail(&reader, "expected six MG3 source travel parameters");
  for (size_t i = 0; i < 6; i++)
    qa_q1ad_set_player_number(context, player->actor.id, TRAVEL_PARMS[i], values[i]);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(decoded);
  double flags = context->game.campaign_read_flags != NULL
      ? context->game.campaign_read_flags(context->game.ctx) : 0;
  if (((int32_t)flags & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0) {
    if (qa_q1ad_cvar(context, "skill") != 3) {
      if (context->game.campaign_write_flags != NULL)
        context->game.campaign_write_flags(context->game.ctx,
            (double)(int32_t)((int32_t)flags & ~QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE));
    } else {
      char hammer_item[64], axe_item[64], shotgun_item[64], super_item[64];
      qa_q1ad_weapon_item("mg3:mjolnir", hammer_item);
      qa_q1ad_weapon_item("axe", axe_item);
      qa_q1ad_weapon_item("shotgun", shotgun_item);
      qa_q1ad_weapon_item("supershotgun", super_item);
      double hammer_count = context->game.inventory_count != NULL
          ? context->game.inventory_count(context->game.ctx, player->actor.id, hammer_item) : 0;
      bool hammer = hammer_count != 0;
      bool bloody_super = ((int32_t)qa_q1ad_player_number(context, player->actor.id, "parm15") &
          QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN) != 0;
      qa_q1ad_inventory_entry entries[32];
      size_t entry_count = context->game.inventory_entries != NULL
          ? context->game.inventory_entries(context->game.ctx, player->actor.id, entries, 32) : 0;
      for (size_t i = 0; i < entry_count; i++) {
        if (strncmp(entries[i].item, "q1:weapon/", 10) != 0) continue;
        bool owned = strcmp(entries[i].item, axe_item) == 0 || strcmp(entries[i].item, shotgun_item) == 0 ||
            (hammer && strcmp(entries[i].item, hammer_item) == 0) ||
            (bloody_super && strcmp(entries[i].item, super_item) == 0);
        if (context->game.inventory_configure != NULL)
          context->game.inventory_configure(context->game.ctx, player->actor,
              entries[i].item, owned ? 1 : 0, entries[i].capacity);
      }
      if (bloody_super && context->game.inventory_configure != NULL)
        context->game.inventory_configure(context->game.ctx, player->actor, super_item, 1, 1);
      const char *weapon = player->weapon != NULL ? player->weapon : "";
      if (strcmp(weapon, "axe") != 0 && strcmp(weapon, "mg3:mjolnir") != 0 &&
          strcmp(weapon, "shotgun") != 0) player->weapon = "shotgun";
    }
  }
  qa_q1ad_initialize_mg3_capacities(context, player);
  if (context->game.select_weapon != NULL)
    (void)context->game.select_weapon(context->game.ctx, player->actor, player->weapon);
}

double qa_q1ad_mg3_upgrade_flag(const char *map) {
  if (strcmp(map, "map2b") == 0) return 8192;
  static const char *const MAPS[] = {"map1", "map2", "map3", "map4", "map5", "map6", "map7", "map8",
      "secret1", "secret2", "secret3", "secret4", "secret5"};
  for (size_t i = 0; i < sizeof MAPS / sizeof MAPS[0]; i++)
    if (strcmp(map, MAPS[i]) == 0) return (double)(1 << (int)i);
  return strcmp(map, "secret6") == 0 ? 16384 : 0;
}
double qa_q1ad_mg3_upgraded_maximum(double base, double flags) {
  double result = base;
  for (int bit = 0; bit < 23; bit++) if (((int32_t)flags & (1 << bit)) != 0) result += 10;
  return result;
}
static const upgrade_def *definition(const char *type) {
  for (size_t i = 0; i < UPGRADE_COUNT; i++)
    if (strcmp(UPGRADES[i].type, type) == 0) return &UPGRADES[i];
  qa_range_error("Unknown MG3 upgrade %s", type);
}
static double mg3_capacity(qa_q1ad_context *context, qa_q1ad_player *player, const upgrade_def *upgrade) {
  return context->game.deathmatch != 0 ? upgrade->deathmatch
      : qa_q1ad_mg3_upgraded_maximum(upgrade->base,
          qa_q1ad_player_number(context, player->actor.id, upgrade->parm));
}
void qa_q1ad_initialize_mg3_capacities(qa_q1ad_context *context, qa_q1ad_player *player) {
  for (size_t i = 0; i < UPGRADE_COUNT; i++) {
    const upgrade_def *upgrade = &UPGRADES[i];
    double capacity = mg3_capacity(context, player, upgrade);
    if (upgrade->ammo == NULL) {
      player->max_health = capacity;
      if (qa_q1ad_health(context, player->actor.id) > capacity &&
          context->game.combat_set_health != NULL)
        context->game.combat_set_health(context->game.ctx, player->actor, capacity);
    } else {
      qa_q1ad_set_mg3_inventory_capacity(context, player, upgrade->ammo, capacity);
      double held = context->game.inventory_count != NULL
          ? context->game.inventory_count(context->game.ctx, player->actor.id, upgrade->ammo) : 0;
      if (context->game.inventory_configure != NULL)
        context->game.inventory_configure(context->game.ctx, player->actor, upgrade->ammo,
            held < capacity ? held : capacity, capacity);
    }
  }
}
bool qa_q1ad_mg3_inventory_capacity(qa_q1ad_context *context, qa_q1ad_player *player, qa_item_id item, double *out) {
  const upgrade_def *upgrade = NULL;
  for (size_t i = 0; i < UPGRADE_COUNT; i++)
    if (UPGRADES[i].ammo != NULL && strcmp(UPGRADES[i].ammo, item) == 0) upgrade = &UPGRADES[i];
  if (upgrade == NULL) return false;
  char field[48];
  (void)snprintf(field, sizeof field, "ammo_%s_max", upgrade->type);
  double saved;
  if (!qa_q1ad_player_word(context, player->actor.id, field, &saved)) {
    *out = mg3_capacity(context, player, upgrade);
    return true;
  }
  if (!isfinite(saved) || saved <= 0) qa_range_error("Invalid MG3 source ammo maximum");
  *out = saved;
  return true;
}
void qa_q1ad_set_mg3_inventory_capacity(qa_q1ad_context *context, qa_q1ad_player *player, qa_item_id item, double capacity) {
  const upgrade_def *upgrade = NULL;
  for (size_t i = 0; i < UPGRADE_COUNT; i++)
    if (UPGRADES[i].ammo != NULL && strcmp(UPGRADES[i].ammo, item) == 0) upgrade = &UPGRADES[i];
  if (upgrade == NULL) qa_range_error("Missing MG3 capacity word for %s", item);
  char field[48];
  (void)snprintf(field, sizeof field, "ammo_%s_max", upgrade->type);
  qa_q1ad_set_player_number(context, player->actor.id, field, capacity);
}
static void upgrade_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                          const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  qa_q1ad_player *player = qa_q1ad_player_state(context, *other);
  if (player == NULL) return;
  const upgrade_def *upgrade = definition(qa_q1ad_actor_text(entity, "mg3.upgrade"));
  int32_t flag = (int32_t)qa_q1ad_actor_number(entity, "upgrade_flag");
  int32_t flags = (int32_t)qa_q1ad_player_number(context, *other, upgrade->parm);
  bool collected = (flags & flag) != 0;
  double maximum = player->max_health;
  if (!collected) {
    double capacity = upgrade->base;
    bool has_capacity = true;
    if (upgrade->ammo != NULL) {
      qa_q1ad_inventory_entry entries[32];
      size_t count = context->game.inventory_entries != NULL
          ? context->game.inventory_entries(context->game.ctx, *other, entries, 32) : 0;
      has_capacity = false;
      for (size_t i = 0; i < count; i++)
        if (strcmp(entries[i].item, upgrade->ammo) == 0) { capacity = entries[i].capacity; has_capacity = true; }
      if (!has_capacity) capacity = upgrade->base;
    }
    qa_q1ad_set_player_number(context, *other, upgrade->parm, (double)(flags | flag));
    if (upgrade->ammo == NULL) {
      maximum = player->max_health = (double)(float)(player->max_health + 10);
      double health = qa_q1ad_health(context, *other);
      if (health > 0 && health < maximum && context->game.combat_set_health != NULL) {
        double next = health + maximum;
        context->game.combat_set_health(context->game.ctx, player->actor, next < maximum ? next : maximum);
      }
    } else {
      maximum = capacity + 10;
      qa_q1ad_set_mg3_inventory_capacity(context, player, upgrade->ammo, maximum);
      if (context->game.inventory_configure != NULL)
        context->game.inventory_configure(context->game.ctx, player->actor, upgrade->ammo, maximum, maximum);
    }
  }
  /* GiveUpgradeBonus only bounds existing ammunition; its former bonus is commented out in QC. */
  qa_q1ad_inventory_entry entries[32];
  size_t count = context->game.inventory_entries != NULL
      ? context->game.inventory_entries(context->game.ctx, *other, entries, 32) : 0;
  for (size_t i = 0; i < count; i++)
    if (strncmp(entries[i].item, "q1:ammo/", 8) == 0 && entries[i].count > entries[i].capacity &&
        context->game.inventory_configure != NULL)
      context->game.inventory_configure(context->game.ctx, player->actor,
          entries[i].item, entries[i].capacity, entries[i].capacity);
  if (context->game.select_weapon != NULL)
    (void)context->game.select_weapon(context->game.ctx, player->actor, player->weapon);
  char label[48];
  (void)snprintf(label, sizeof label, "$mg3_qc_upgrade_%s", upgrade->label);
  qa_q1ad_actor_set(entity, "upgrade.label", label);
  qa_q1ad_msg_arg fail_args[] = {{.text = qa_q1ad_actor_text(entity, "upgrade.label")}};
  qa_q1ad_msg_arg ok_args[] = {{.text = qa_q1ad_actor_text(entity, "upgrade.label")},
                               {.is_number = true, .number = maximum}};
  qa_q1ad_finish_mg3_pickup(context, entity, *other,
      collected ? "$mg3_qc_upgrade_fail" : "$mg3_qc_upgrade_success",
      strcmp(upgrade->type, "health") == 0 ? "player/tornoff2.wav" : "weapons/lock4.wav",
      collected ? fail_args : ok_args, collected ? 1 : 2);
}
static void upgrade_start(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  const upgrade_def *upgrade = definition(qa_q1ad_actor_text(entity, "mg3.upgrade"));
  qa_actor_id players[32];
  size_t count = qa_q1ad_players(context, players, 32);
  int32_t flag = (int32_t)qa_q1ad_actor_number(entity, "upgrade_flag");
  for (size_t i = 0; i < count; i++)
    if (((int32_t)qa_q1ad_player_number(context, players[i], upgrade->parm) & flag) != 0) {
      qa_q1ad_alpha(context, entity, 0.6);
      break;
    }
  entity->solid = "trigger";
  qa_q1ad_bind(entity->touch, QA_Q1AD_MG3_ITEM_PREFIX "upgrade_touch");
  qa_q1ad_start_mg3_item(context, entity);
}
static const upgrade_def *spawn_upgrade_of(qa_q1ad_actor *entity) {
  const char *classname = entity->classname != NULL ? entity->classname : "";
  for (size_t i = 0; i < UPGRADE_COUNT; i++) {
    char want[48];
    (void)snprintf(want, sizeof want, "item_upgrade_%s", UPGRADES[i].type);
    if (strcmp(classname, want) == 0) return &UPGRADES[i];
  }
  return &UPGRADES[0];
}
static void spawn_upgrade(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  const upgrade_def *upgrade = spawn_upgrade_of(entity);
  if (qa_q1ad_actor_number(entity, "upgrade_flag") == 0)
    qa_q1ad_set_number(context, entity, "upgrade_flag",
        qa_q1ad_mg3_upgrade_flag(context->game.map_name != NULL ? context->game.map_name : ""));
  qa_q1ad_actor_set(entity, "mg3.upgrade", upgrade->type);
  char netname[48], model[64];
  (void)snprintf(netname, sizeof netname, "$mg3_qc_upgrade_%s", upgrade->label);
  qa_q1ad_actor_set(entity, "netname", netname);
  (void)snprintf(model, sizeof model, "progs/%s.mdl", upgrade->model);
  qa_q1ad_actor_set(entity, "upgrade.model", model);
  entity->model = qa_q1ad_actor_text(entity, "upgrade.model");
  bool health = strcmp(upgrade->type, "health") == 0;
  if (health)
    qa_q1ad_set_origin(context, entity,
        qa_add3(qa_q1ad_body_of(context, entity).origin, (qa_vec3){0, 0, 8}));
  qa_q1ad_set_bounds(context, entity,
      (qa_bounds){{-16, -16, health ? -8 : 0}, {16, 16, health ? 48 : 56}});
  qa_q1ad_schedule(context, entity, 0.5, QA_Q1AD_MG3_ITEM_PREFIX "upgrade_start");
}

void qa_q1ad_register_mg3_upgrades(qa_q1ad_context *context) {
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "upgrade_touch",
      (qa_q1ad_handlers){.touch = upgrade_touch});
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "upgrade_start",
      (qa_q1ad_handlers){.action = upgrade_start});
  for (size_t i = 0; i < UPGRADE_COUNT; i++) {
    char classname[48];
    (void)snprintf(classname, sizeof classname, "item_upgrade_%s", UPGRADES[i].type);
    qa_q1ad_register_spawn(context, classname, false, spawn_upgrade);
  }
}

bool qa_q1ad_give_next_mg3_upgrade(qa_q1ad_context *context, const char *type, qa_q1ad_player *player) {
  const upgrade_def *upgrade = definition(type);
  int32_t flags = (int32_t)qa_q1ad_player_number(context, player->actor.id, upgrade->parm);
  for (int bit = 1; bit <= 16384; bit *= 2)
    if ((flags & bit) == 0) {
      char classname[48];
      (void)snprintf(classname, sizeof classname, "item_upgrade_%s", type);
      if (context->game.create == NULL) return false;
      qa_q1ad_actor *entity = context->game.create(context->game.ctx, classname);
      if (entity == NULL) return false;
      qa_q1ad_actor_set(entity, "mg3.upgrade", type);
      qa_q1ad_set_number(context, entity, "upgrade_flag", bit);
      upgrade_touch(context, entity, &player->actor.id, NULL);
      return true;
    }
  return false;
}
