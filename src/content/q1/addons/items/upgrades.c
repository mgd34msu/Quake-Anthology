/* Port of src/content/q1/addons/items/upgrades.ts (quakec_mg3/mg3_upgrades.qc and client.qc). GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/upgrades.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/addons/items/common.h"
#include "qa/content/q1/addons/items/pickups.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

typedef struct {
  const char *type, *parm;
  double base, deathmatch;
  const char *model, *label;
  qa_item_id ammo; /* NULL = TS null */
} upgrade_definition;

static const upgrade_definition UPGRADES[] = {
    {"health", "parm10", 50, 100, "item_h_player", "health", NULL},
    {"shells", "parm11", 50, 100, "backpackshells", "shell", "q1:ammo/shells"},
    {"nails", "parm12", 100, 200, "backpacknails", "nail", "q1:ammo/nails"},
    {"rockets", "parm13", 20, 100, "backpacker", "rocket", "q1:ammo/rockets"},
    {"cells", "parm14", 100, 200, "backpackcells", "cell", "q1:ammo/cells"},
};
#define UPGRADE_COUNT (sizeof UPGRADES / sizeof UPGRADES[0])

static const char *const TRAVEL_PARMS[] = {"parm10", "parm11", "parm12", "parm13", "parm14", "parm15"};
#define TRAVEL_PARM_COUNT (sizeof TRAVEL_PARMS / sizeof TRAVEL_PARMS[0])

uint8_t *qa_q1ad_capture_mg3_upgrade_travel(qa_q1ad_context *context, qa_q1_player_state *player, size_t *out_length) {
  qa_save_value *list = qa_save_value_array();
  if (list == NULL) return NULL;
  for (size_t i = 0; i < TRAVEL_PARM_COUNT; i++) {
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

static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
void qa_q1ad_restore_mg3_upgrade_travel(qa_q1ad_context *context, qa_q1_player_state *player, const uint8_t *bytes,
                                        size_t length) {
  qa_q1_services *game = context->game;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "mg3:upgrade-travel")) qa_save_reader_fail(&reader, "out of memory");
  size_t count = qa_save_reader_list_count(&reader);
  double values[TRAVEL_PARM_COUNT] = {0};
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&reader, i, &item)) qa_save_reader_fail(&reader, "out of memory");
    double number = qa_save_reader_integer_min(&item, 0);
    if (number > 8388607) qa_save_reader_fail(&item, "MG3 source parameter exceeds its flag word");
    if (i < TRAVEL_PARM_COUNT) values[i] = number;
    qa_save_reader_destroy(&item);
  }
  if (count != TRAVEL_PARM_COUNT) qa_save_reader_fail(&reader, "expected six MG3 source travel parameters");
  for (size_t i = 0; i < TRAVEL_PARM_COUNT; i++) qa_q1ad_set_player_number(context, player->actor.id, TRAVEL_PARMS[i], values[i]);
  qa_save_reader_destroy(&reader);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  int32_t flags = qa_js_to_int32(campaign.read_flags(campaign.ctx));
  if ((flags & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0) {
    if (context->services.cvar(context->services.ctx, "skill") != 3)
      campaign.write_flags(campaign.ctx, (double)(flags & ~QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE));
    else {
      qa_item_id hammer_item = qa_q1_services_weapon_item(game, "mg3:mjolnir");
      qa_item_id axe_item = qa_q1_services_weapon_item(game, "axe");
      qa_item_id shotgun_item = qa_q1_services_weapon_item(game, "shotgun");
      qa_item_id super_item = qa_q1_services_weapon_item(game, "supershotgun");
      bool hammer = qa_shared_inventory_table_count(game->host.inventory, player->actor.id, hammer_item) != 0;
      bool bloody_super = (qa_js_to_int32(qa_q1ad_player_number(context, player->actor.id, "parm15")) &
                           QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN) != 0;
      /* for (const entry of entries()): configure replaces counts, not the entry list. */
      size_t entry_count = 0;
      const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host.inventory, player->actor.id,
                                                                            &entry_count);
      for (size_t i = 0; i < entry_count; i++) {
        qa_inventory_entry entry = entries[i];
        if (strncmp(entry.item, "q1:weapon/", 10) != 0) continue;
        bool owned = strcmp(entry.item, axe_item) == 0 || strcmp(entry.item, shotgun_item) == 0 ||
                     (hammer && strcmp(entry.item, hammer_item) == 0) ||
                     (bloody_super && strcmp(entry.item, super_item) == 0);
        entry.count = owned ? 1 : 0;
        qa_shared_inventory_table_configure(game->host.inventory, player->actor, entry, NULL, NULL);
        entries = qa_shared_inventory_table_entries(game->host.inventory, player->actor.id, &entry_count);
      }
      if (bloody_super)
        qa_shared_inventory_table_configure(game->host.inventory, player->actor,
                                            (qa_inventory_entry){.item = super_item, .count = 1, .capacity = 1}, NULL,
                                            NULL);
      if (strcmp(player->weapon, "axe") != 0 && strcmp(player->weapon, "mg3:mjolnir") != 0 &&
          strcmp(player->weapon, "shotgun") != 0)
        player->weapon = "shotgun";
    }
  }
  qa_q1ad_initialize_mg3_capacities(context, player);
  (void)qa_q1_select_weapon(game, player->actor, player->weapon);
}

double qa_q1ad_mg3_upgrade_flag(const char *map) {
  if (strcmp(map, "map2b") == 0) return 8192;
  static const char *const MAPS[] = {"map1", "map2", "map3", "map4", "map5", "map6", "map7",
                                     "map8", "secret1", "secret2", "secret3", "secret4", "secret5"};
  for (size_t i = 0; i < sizeof MAPS / sizeof MAPS[0]; i++)
    if (strcmp(map, MAPS[i]) == 0) return (double)(1 << i);
  return strcmp(map, "secret6") == 0 ? 16384 : 0;
}

double qa_q1ad_mg3_upgraded_maximum(double base, double flags) {
  double result = base;
  int32_t bits = qa_js_to_int32(flags);
  for (int bit = 0; bit < 23; bit++)
    if ((bits & (1 << bit)) != 0) result += 10;
  return result;
}

static const upgrade_definition *definition(const char *type) {
  for (size_t i = 0; i < UPGRADE_COUNT; i++)
    if (strcmp(UPGRADES[i].type, type) == 0) return &UPGRADES[i];
  qa_range_error("Unknown MG3 upgrade %s", type);
}

static const upgrade_definition *ammo_upgrade(qa_item_id item) {
  for (size_t i = 0; i < UPGRADE_COUNT; i++)
    if (UPGRADES[i].ammo != NULL && strcmp(UPGRADES[i].ammo, item) == 0) return &UPGRADES[i];
  return NULL;
}

static double mg3_capacity(qa_q1ad_context *context, qa_q1_player_state *player, const upgrade_definition *upgrade) {
  return context->game->options.deathmatch != 0
             ? upgrade->deathmatch
             : qa_q1ad_mg3_upgraded_maximum(upgrade->base, qa_q1ad_player_number(context, player->actor.id, upgrade->parm));
}

void qa_q1ad_initialize_mg3_capacities(qa_q1ad_context *context, qa_q1_player_state *player) {
  qa_q1_services *game = context->game;
  for (size_t i = 0; i < UPGRADE_COUNT; i++) {
    const upgrade_definition *upgrade = &UPGRADES[i];
    double capacity = mg3_capacity(context, player, upgrade);
    if (upgrade->ammo == NULL) {
      player->max_health = capacity;
      if (qa_q1_health(game, player->actor.id) > capacity)
        qa_gameplay_authority_set_health(game->host.combat, player->actor, capacity);
    } else {
      qa_q1ad_set_mg3_inventory_capacity(context, player, upgrade->ammo, capacity);
      double held = qa_shared_inventory_table_count(game->host.inventory, player->actor.id, upgrade->ammo);
      qa_shared_inventory_table_configure(
          game->host.inventory, player->actor,
          (qa_inventory_entry){.item = upgrade->ammo, .capacity = capacity, .count = qa_js_min(capacity, held)}, NULL,
          NULL);
    }
  }
}

bool qa_q1ad_mg3_inventory_capacity(qa_q1ad_context *context, qa_q1_player_state *player, qa_item_id item, double *out) {
  const upgrade_definition *upgrade = ammo_upgrade(item);
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

void qa_q1ad_set_mg3_inventory_capacity(qa_q1ad_context *context, qa_q1_player_state *player, qa_item_id item,
                                        double capacity) {
  const upgrade_definition *upgrade = ammo_upgrade(item);
  if (upgrade == NULL) qa_range_error("Missing MG3 capacity word for %s", item);
  char field[48];
  (void)snprintf(field, sizeof field, "ammo_%s_max", upgrade->type);
  qa_q1ad_set_player_number(context, player->actor.id, field, capacity);
}

static void upgrade_touch(qa_q1ad_context *context, qa_q1_actor *entity, qa_actor_id other) {
  qa_q1_services *game = context->game;
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (player == NULL) return;
  const upgrade_definition *upgrade = definition(qa_q1_actor_text(entity, "mg3.upgrade"));
  int32_t flag = qa_js_to_int32(qa_q1_actor_number(entity, "upgrade_flag"));
  int32_t flags = qa_js_to_int32(qa_q1ad_player_number(context, other, upgrade->parm));
  bool collected = (flags & flag) != 0;
  double maximum = player->max_health;
  if (!collected) {
    double capacity = upgrade->base;
    if (upgrade->ammo != NULL) {
      size_t entry_count = 0;
      const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host.inventory, other, &entry_count);
      for (size_t i = 0; i < entry_count; i++)
        if (strcmp(entries[i].item, upgrade->ammo) == 0) { capacity = entries[i].capacity; break; }
    }
    qa_q1ad_set_player_number(context, other, upgrade->parm, (double)(flags | flag));
    if (upgrade->ammo == NULL) {
      maximum = player->max_health = (double)(float)(player->max_health + 10);
      double health = qa_q1_health(game, other);
      if (health > 0 && health < maximum)
        qa_gameplay_authority_set_health(game->host.combat, player->actor, qa_js_min(maximum, health + maximum));
    } else {
      maximum = capacity + 10;
      qa_q1ad_set_mg3_inventory_capacity(context, player, upgrade->ammo, maximum);
      qa_shared_inventory_table_configure(
          game->host.inventory, player->actor,
          (qa_inventory_entry){.item = upgrade->ammo, .count = maximum, .capacity = maximum}, NULL, NULL);
    }
  }
  /* GiveUpgradeBonus only bounds existing ammunition; its former bonus is commented out in QC. */
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host.inventory, other, &entry_count);
  for (size_t i = 0; i < entry_count; i++) {
    qa_inventory_entry entry = entries[i];
    if (strncmp(entry.item, "q1:ammo/", 8) != 0 || entry.count <= entry.capacity) continue;
    entry.count = entry.capacity;
    qa_shared_inventory_table_configure(game->host.inventory, player->actor, entry, NULL, NULL);
    entries = qa_shared_inventory_table_entries(game->host.inventory, other, &entry_count);
  }
  (void)qa_q1_select_weapon(game, player->actor, player->weapon);
  char label[64];
  (void)snprintf(label, sizeof label, "$mg3_qc_upgrade_%s", upgrade->label);
  qa_q1_msg_arg args[] = {{.text = label}, {.is_number = true, .number = maximum}};
  qa_q1ad_finish_mg3_pickup(context, entity, other, collected ? "$mg3_qc_upgrade_fail" : "$mg3_qc_upgrade_success",
                            strcmp(upgrade->type, "health") == 0 ? "player/tornoff2.wav" : "weapons/lock4.wav", args,
                            collected ? 1 : 2);
}

static void upgrade_touch_handler(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                                  const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface; /* TS handler ignores them */
  upgrade_touch(ctx, entity, other);
}

static void upgrade_start(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  int32_t left;
  int32_t right;
  qa_q1ad_context *context = ctx;
  const upgrade_definition *upgrade = definition(qa_q1_actor_text(entity, "mg3.upgrade"));
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (((left = qa_js_to_int32(qa_q1ad_player_number(context, players[i], upgrade->parm)), right = qa_js_to_int32(qa_q1_actor_number(entity, "upgrade_flag")), left &
         right)) != 0) {
      qa_q1ad_alpha(context, entity, 0.6);
      break;
    }
  entity->solid = "trigger";
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, QA_Q1AD_MG3_ITEM_PREFIX "upgrade_touch");
  qa_q1ad_start_mg3_item(context, entity);
}

static void spawn_upgrade(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  const upgrade_definition *upgrade = NULL;
  for (size_t i = 0; i < UPGRADE_COUNT && upgrade == NULL; i++) {
    char classname[48];
    (void)snprintf(classname, sizeof classname, "item_upgrade_%s", UPGRADES[i].type);
    if (strcmp(entity->classname, classname) == 0) upgrade = &UPGRADES[i];
  }
  if (upgrade == NULL) qa_range_error("Unknown MG3 upgrade spawn %s", entity->classname);
  if (qa_q1_actor_number(entity, "upgrade_flag") == 0)
    qa_q1ad_set_number(context, entity, "upgrade_flag", qa_q1ad_mg3_upgrade_flag(game->map_name != NULL ? game->map_name : ""));
  (void)qa_q1_actor_field_set(entity, "mg3.upgrade", upgrade->type);
  char netname[64], model[64];
  (void)snprintf(netname, sizeof netname, "$mg3_qc_upgrade_%s", upgrade->label);
  (void)qa_q1_actor_field_set(entity, "netname", netname);
  (void)snprintf(model, sizeof model, "progs/%s.mdl", upgrade->model);
  qa_q1ad_set_text(&entity->model, model);
  bool health = strcmp(upgrade->type, "health") == 0;
  if (health) qa_q1_set_origin(game, entity, qa_q1_vadd(qa_q1_get_body(game, entity).origin, (qa_vec3){0, 0, 8}));
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, health ? -8 : 0}, {16, 16, health ? 48 : 56}});
  qa_q1_schedule(game, entity, 0.5,
                 qa_q1_callback_registry_action(game->named, entity, QA_Q1AD_MG3_ITEM_PREFIX "upgrade_start"));
}

void qa_q1ad_register_mg3_upgrades(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_callback_registry_register(game->named, QA_Q1AD_MG3_ITEM_PREFIX "upgrade_touch",
                                   (qa_q1_callback_handlers){.ctx = context, .touch = upgrade_touch_handler});
  qa_q1_callback_registry_register(game->named, QA_Q1AD_MG3_ITEM_PREFIX "upgrade_start",
                                   (qa_q1_callback_handlers){.ctx = context, .action = upgrade_start});
  for (size_t i = 0; i < UPGRADE_COUNT; i++) {
    char classname[48];
    (void)snprintf(classname, sizeof classname, "item_upgrade_%s", UPGRADES[i].type);
    qa_q1_register_spawn(game, classname, spawn_upgrade, context);
  }
}

bool qa_q1ad_give_next_mg3_upgrade(qa_q1ad_context *context, const char *type, qa_q1_player_state *player) {
  const upgrade_definition *upgrade = definition(type);
  int32_t flags = qa_js_to_int32(qa_q1ad_player_number(context, player->actor.id, upgrade->parm));
  for (int32_t bit = 1; bit <= 16384; bit *= 2)
    if ((flags & bit) == 0) {
      char classname[48];
      (void)snprintf(classname, sizeof classname, "item_upgrade_%s", type);
      qa_q1_actor *entity = qa_q1_create(context->game, classname, NULL, NULL);
      (void)qa_q1_actor_field_set(entity, "mg3.upgrade", type);
      qa_q1ad_set_number(context, entity, "upgrade_flag", bit);
      upgrade_touch(context, entity, player->actor.id);
      return true;
    }
  return false;
}
