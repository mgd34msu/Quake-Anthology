/* Port of src/content/q1/addons/horde/loot.ts (quakec_mg1/horde.qc item and key functions). GPL-2.0-or-later. */
#include "qa/content/q1/addons/horde/loot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/pickups.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

typedef struct {
  qa_item_id item;
  const char *classname, *model, *label;
  double amount, capacity;
} horde_ammo;

static horde_ammo ammo_definition(double type, bool big) {
  if (type == 1) return (horde_ammo){"q1:ammo/shells", "item_shells", "shell", "$qc_shells", big ? 40 : 20, 100};
  if (type == 2) return (horde_ammo){"q1:ammo/nails", "item_spikes", "nail", "$qc_nails", big ? 50 : 25, 200};
  if (type == 3) return (horde_ammo){"q1:ammo/rockets", "item_rockets", "rock", "$qc_rockets", big ? 10 : 5, 100};
  return (horde_ammo){"q1:ammo/cells", "item_cells", "batt", "$qc_cells", big ? 12 : 6, 100};
}

static bool actor_live(qa_q1_services *game, qa_actor_id actor) {
  return qa_session_actor_registry_is_live(game->host.actors, actor);
}

static void taken(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id player, const char *sound) {
  qa_owned_actor actor;
  if (qa_session_actor_registry_resolve_owned(game->host.actors, player, &actor))
    qa_q1_sound(game, actor.id, sound, "item", 1, 1);
  if (!qa_q1_live(game, entity) || !actor_live(game, player)) return;
  qa_q1_effect(game, "pickup", qa_q1_get_body(game, entity).origin, &player, 1);
  if (!qa_q1_live(game, entity) || !actor_live(game, player)) return;
  entity->solid = "none";
  qa_q1ad_set_text(&entity->model, "");
  qa_q1_link(game, entity);
}

static qa_q1_actor *owner_of(qa_q1_services *game, qa_q1_actor *entity) {
  return qa_q1_get_entity(game, entity->has_owner ? &entity->owner : NULL);
}

static void ammo_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS handler ignores them */
  qa_q1ad_horde *horde = ctx;
  if (!qa_q1_is_player(game, &other) || qa_q1_health(game, other) <= 0) return;
  horde_ammo definition = ammo_definition(qa_q1_actor_number(entity, "weapon"), false);
  if (qa_shared_inventory_table_count(game->host.inventory, other, definition.item) >= definition.capacity) return;
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (player != NULL && strcmp(player->weapon, qa_q1_choose_best(game, player->actor)) == 0)
    (void)qa_q1_select_weapon(game, player->actor, qa_q1_choose_best(game, player->actor));
  qa_q1_msg_arg args[] = {{.text = definition.label}};
  qa_q1_message(game, &other, "$qc_got_item", false, args, 1);
  taken(game, entity, other, "weapons/lock4.wav");
  size_t count = qa_q1ad_horde_living_players(horde, NULL, 0);
  qa_actor_id *living = malloc((count == 0 ? 1 : count) * sizeof *living);
  if (living == NULL) qa_range_error("out of memory");
  (void)qa_q1ad_horde_living_players(horde, living, count);
  for (size_t i = 0; i < count; i++) {
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_owned(game->host.actors, living[i], &actor)) continue;
    (void)qa_shared_inventory_table_give(game->host.inventory, actor, definition.item,
                                         qa_q1_actor_number(entity, "aflag"));
  }
  free(living);
  qa_q1_actor *owner = owner_of(game, entity);
  if (owner != NULL) qa_q1ad_horde_schedule(horde, owner, "ammo", 20);
  qa_q1_remove(game, entity);
}

static void health_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS handler ignores them */
  qa_q1ad_horde *horde = ctx;
  qa_q1ad_context *context = horde->context;
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (player == NULL) return;
  double health = qa_q1_health(game, other);
  if (health <= 0 || health >= player->max_health) return;
  if (context->services.cvar(context->services.ctx, "horde") != 0) {
    qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
    if ((qa_js_to_int32(campaign.read_flags(campaign.ctx)) & 2) != 0)
      qa_q1ad_set_player_number(context, other, "hunger_time", game->time + 10);
  }
  double amount = qa_q1_actor_number(entity, "healamount");
  qa_gameplay_authority_set_health(game->host.combat, player->actor, qa_js_min(health + amount, player->max_health));
  qa_q1_msg_arg args[] = {{.is_number = true, .number = amount}};
  qa_q1_message(game, &other, "$qc_item_health", false, args, 1);
  taken(game, entity, other, qa_q1_actor_text(entity, "noise"));
  qa_q1_actor *owner = owner_of(game, entity);
  if (owner != NULL) owner->wait = 0;
  qa_q1_remove(game, entity);
}

/* The authority keeps the regular-armor item id by reference. */
static qa_item_id armor_item(const char *classname) {
  if (strcmp(classname, "item_armor1") == 0) return "q1:item_armor1";
  if (strcmp(classname, "item_armor2") == 0) return "q1:item_armor2";
  if (strcmp(classname, "item_armorInv") == 0) return "q1:item_armorInv";
  return NULL;
}

typedef struct {
  qa_q1_services *game;
  qa_q1_actor *entity;
  qa_actor_id other;
  qa_owned_actor actor;
  qa_item_id item;
} armor_pickup;

static bool armor_original(void *ctx) {
  armor_pickup *pickup = ctx;
  qa_q1_services *game = pickup->game;
  const char *classname = pickup->entity->classname;
  double absorption = strcmp(classname, "item_armor1") == 0 ? 0.3 : strcmp(classname, "item_armor2") == 0 ? 0.6 : 0.8;
  double points = strcmp(classname, "item_armor1") == 0 ? 100 : strcmp(classname, "item_armor2") == 0 ? 150 : 200;
  qa_combat_state_result state = qa_gameplay_authority_read(game->host.combat, pickup->other);
  const qa_regular_armor_state *armor = state.has_value ? &state.value.armor.regular : NULL;
  if (armor != NULL && armor->kind == QA_REGULAR_ARMOR_SOURCE) return false;
  double protection = armor == NULL || armor->kind == QA_REGULAR_ARMOR_NONE ? 0
      : armor->points * (armor->kind == QA_REGULAR_ARMOR_Q1 ? armor->absorption
                         : armor->kind == QA_REGULAR_ARMOR_Q2 ? armor->normal_protection : armor->protection);
  if (protection >= absorption * points) return false;
  qa_gameplay_authority_set_regular_armor(
      game->host.combat, pickup->actor,
      (qa_regular_armor_state){.kind = QA_REGULAR_ARMOR_Q1, .points = points, .absorption = absorption,
                               .item = pickup->item});
  return true;
}

static void armor_complete(void *ctx, bool accepted) {
  armor_pickup *pickup = ctx;
  qa_q1_services *game = pickup->game;
  if (!accepted) return;
  qa_q1_message(game, &pickup->other, "$qc_item_armor", false, NULL, 0);
  if (!qa_q1_live(game, pickup->entity) || !actor_live(game, pickup->other)) return;
  taken(game, pickup->entity, pickup->other, "items/armor1.wav");
  if (!qa_q1_live(game, pickup->entity)) return;
  qa_q1_actor *owner = owner_of(game, pickup->entity);
  if (owner != NULL) owner->wait = 0;
  qa_q1_remove(game, pickup->entity);
}

static void armor_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                        const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS handler ignores them */
  if (!qa_q1_is_player(game, &other) || qa_q1_health(game, other) <= 0) return;
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_owned(game->host.actors, other, &actor)) return;
  qa_item_id item = armor_item(entity->classname);
  if (item == NULL) { /* `q1:${entity.classname}` for a foreign classname: kept on the entity */
    char text[96];
    (void)snprintf(text, sizeof text, "q1:%s", entity->classname);
    (void)qa_q1_actor_field_set(entity, "horde.armorItem", text);
    item = qa_q1_actor_text(entity, "horde.armorItem");
  }
  armor_pickup pickup = {game, entity, other, actor, item};
  qa_q1_touch_pickup(game, entity, other, item, "protection",
                     (qa_q1_pickup_continuation){.ctx = &pickup, .original = armor_original,
                                                 .complete = armor_complete});
}

static void key_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                      const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS handler ignores them */
  qa_q1ad_horde *horde = ctx;
  if (!qa_q1_is_player(game, &other) || qa_q1_health(game, other) <= 0 ||
      horde->services.is_bot(horde->services.ctx, other))
    return;
  qa_q1_msg_arg args[] = {{.text = qa_q1_actor_text(entity, "netname")}};
  qa_q1_message(game, &other, "$qc_got_item", false, args, 1);
  taken(game, entity, other, qa_q1_actor_text(entity, "noise"));
  qa_q1ad_horde_change_keys(horde, strcmp(qa_q1_actor_text(entity, "horde.key"), "gold") == 0 ? "gold" : "silver", 1);
  qa_q1_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager != NULL && qa_q1_actor_number(manager, "key_spawned") != 0) {
    manager->wait = 1;
    qa_q1ad_horde_schedule(horde, manager, "countdown", 0);
  }
  qa_q1_remove(game, entity);
}

static void ammo_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  bool big = qa_q1_host_random(game) * 4 <= 1;
  double roll = qa_q1_host_random(game) * 20;
  double type = roll <= 7 ? 1 : roll <= 14 ? 2 : roll <= 17 ? 3 : 4;
  horde_ammo definition = ammo_definition(type, big);
  qa_q1_actor *item = qa_q1_create(game, definition.classname, NULL, NULL);
  double offset = big ? 16 : 12;
  qa_vec3 position = qa_q1_vsub(qa_q1_get_body(game, entity).origin, (qa_vec3){offset, offset, 0});
  char model[64];
  (void)snprintf(model, sizeof model, "maps/b_%s%d.bsp", definition.model, big ? 1 : 0);
  qa_q1ad_set_text(&item->model, model);
  (void)qa_q1_actor_field_set(item, "netname", definition.label);
  qa_q1ad_set_number(horde->context, item, "weapon", type);
  qa_q1ad_set_number(horde->context, item, "aflag", definition.amount);
  item->has_owner = true;
  item->owner = entity->actor.id;
  entity->wait = 1;
  item->movement = "toss";
  item->solid = "trigger";
  item->movement_flags = 256;
  item->touch = qa_q1_callback_registry_touch(game->named, item, "mg1:horde:ammo_touch");
  qa_q1_body_patch patch = {.has_origin = true, .origin = qa_q1_vadd(position, (qa_vec3){0, 0, 1}),
                            .has_bounds = true, .bounds = {{0, 0, 0}, {32, 32, 56}}};
  qa_q1_set_body(game, item, &patch);
  qa_q1_link(game, item);
  qa_q1_effect(game, "teleport", qa_q1_vadd(position, (qa_vec3){0, 0, 8}), NULL, 1);
}

static void item_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  double roll = qa_q1_host_random(game) * 6;
  bool health = roll < 5;
  qa_q1_actor *item = qa_q1_create(game, health ? "item_health" : "item_armor1", NULL, NULL);
  /* The two independent source if statements replace rotten health with ordinary health. */
  qa_vec3 position = health ? qa_q1_vsub(qa_q1_get_body(game, entity).origin, (qa_vec3){16, 16, 0})
                            : qa_q1_get_body(game, entity).origin;
  qa_q1ad_set_text(&item->model, health ? "maps/b_bh25.bsp" : "progs/armor.mdl");
  (void)qa_q1_actor_field_set(item, "noise", "items/health1.wav");
  qa_q1ad_set_number(horde->context, item, "healamount", 25);
  item->touch = qa_q1_callback_registry_touch(game->named, item,
                                              health ? "mg1:horde:health_touch" : "mg1:horde:armor_touch");
  item->has_owner = true;
  item->owner = entity->actor.id;
  item->movement = "toss";
  item->solid = "trigger";
  item->movement_flags = 256;
  entity->wait = 1;
  qa_q1_body_patch patch = {.has_origin = true, .origin = qa_q1_vadd(position, (qa_vec3){0, 0, 1}), .has_bounds = true,
                            .bounds = health ? (qa_bounds){{0, 0, 0}, {32, 32, 56}}
                                             : (qa_bounds){{-16, -16, 0}, {16, 16, 56}}};
  qa_q1_set_body(game, item, &patch);
  qa_q1_link(game, item);
  qa_q1_effect(game, "teleport", qa_q1_vadd(position, (qa_vec3){0, 0, 8}), NULL, 1);
}

static void spawn_key(qa_q1ad_horde *horde, qa_q1_services *game, qa_q1_actor *entity, bool gold) {
  const char *name = gold ? "gold" : "silver";
  bool metal = game->world_type == 1, base = game->world_type == 2;
  char label[48], netname[64], model[64], announce[96];
  (void)snprintf(label, sizeof label, "%s_%s", name, metal ? "runekey" : base ? "keycard" : "key");
  qa_q1_actor *item = qa_q1_create(game, gold ? "item_key2" : "item_key1", NULL, NULL);
  (void)qa_q1_actor_field_set(item, "horde.key", name);
  (void)snprintf(netname, sizeof netname, "$qc_%s", label);
  (void)qa_q1_actor_field_set(item, "netname", netname);
  (void)qa_q1_actor_field_set(item, "noise", metal ? "misc/runekey.wav" : base ? "misc/basekey.wav" : "misc/medkey.wav");
  (void)snprintf(model, sizeof model, "progs/%s_%s_key.mdl", metal ? "m" : base ? "b" : "w", gold ? "g" : "s");
  qa_q1ad_set_text(&item->model, model);
  qa_q1ad_set_text(&item->target, "horde_manager");
  item->movement = "toss";
  item->solid = "trigger";
  item->movement_flags = 256;
  if (!metal) item->effects = 4;
  item->touch = qa_q1_callback_registry_touch(game->named, item, "mg1:horde:key_touch");
  qa_q1_body_patch patch = {.has_origin = true,
                            .origin = qa_q1_vadd(qa_q1_get_body(game, entity).origin, (qa_vec3){0, 0, 32}),
                            .has_velocity = true, .velocity = {0, 0, 255},
                            .has_bounds = true, .bounds = {{-16, -16, -25}, {16, 16, 32}}};
  qa_q1_set_body(game, item, &patch);
  qa_q1_link(game, item);
  (void)snprintf(announce, sizeof announce, "$qc_horde_%s_appears", label);
  qa_q1ad_broadcast(horde->context, announce);
  qa_q1_effect(game, "teleport", qa_q1_get_body(game, item).origin, NULL, 1);
  qa_q1_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager != NULL) qa_q1ad_set_number(horde->context, manager, "key_spawned", 1);
}

static void silver_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { spawn_key(ctx, game, entity, false); }
static void gold_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) { spawn_key(ctx, game, entity, true); }

static void powerup_fade(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  double alpha = qa_q1_actor_number(entity, "alpha");
  if (alpha <= 0) { qa_q1_remove(game, entity); return; }
  qa_q1ad_alpha(horde->context, entity, (double)(float)(alpha - 0.25 * horde->context->frame_time));
  qa_q1ad_horde_schedule(horde, entity, "powerup_fade", 0);
}

static void powerup_wait(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde *horde = ctx;
  if (qa_q1_get_body(game, entity).velocity.z < 0) { qa_q1_remove(game, entity); return; }
  qa_q1ad_alpha(horde->context, entity, 1);
  qa_q1ad_horde_schedule(horde, entity, "powerup_fade", 0);
}

static void spawn_ammo_point(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_horde_schedule(ctx, entity, "ammo", 10 + qa_q1_host_random(game) * 3);
}

static void spawn_wait_zero(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; (void)game; /* TS handler reads neither */
  entity->wait = 0;
}

static void register_touch(qa_q1_services *game, qa_q1ad_horde *horde, const char *name, qa_q1_touch_fn touch) {
  qa_q1_callback_registry_register(game->named, name, (qa_q1_callback_handlers){.ctx = horde, .touch = touch});
}
static void register_action(qa_q1_services *game, qa_q1ad_horde *horde, const char *name, qa_q1_action_fn action) {
  qa_q1_callback_registry_register(game->named, name, (qa_q1_callback_handlers){.ctx = horde, .action = action});
}

void qa_q1ad_register_horde_loot(qa_q1ad_horde *horde) {
  qa_q1_services *game = horde->context->game;
  register_touch(game, horde, "mg1:horde:ammo_touch", ammo_touch);
  register_touch(game, horde, "mg1:horde:health_touch", health_touch);
  register_touch(game, horde, "mg1:horde:armor_touch", armor_touch);
  register_touch(game, horde, "mg1:horde:key_touch", key_touch);
  register_action(game, horde, "mg1:horde:ammo", ammo_action);
  register_action(game, horde, "mg1:horde:item", item_action);
  register_action(game, horde, "mg1:horde:silver", silver_action);
  register_action(game, horde, "mg1:horde:gold", gold_action);
  register_action(game, horde, "mg1:horde:powerup_fade", powerup_fade);
  register_action(game, horde, "mg1:horde:powerup_wait", powerup_wait);
  qa_q1_register_spawn(game, "info_horde_ammo", spawn_ammo_point, horde);
  qa_q1_register_spawn(game, "info_horde_item", spawn_wait_zero, horde);
  qa_q1_register_spawn(game, "info_horde_key", spawn_wait_zero, horde);
}

void qa_q1ad_spawn_horde_powerup(qa_q1ad_horde *horde, qa_q1_actor *dead) {
  qa_q1ad_context *context = horde->context;
  qa_q1_services *game = context->game;
  qa_q1_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager == NULL) return;
  double chance = qa_q1_actor_number(manager, "powerup_chance");
  if (chance == 0) chance = 0.025; /* `|| 0.025`: number() is finite */
  if (qa_q1_host_random(game) >= chance) {
    qa_q1ad_set_number(context, manager, "powerup_chance", chance + 0.025);
    return;
  }
  qa_q1ad_set_number(context, manager, "powerup_chance", 0.025);
  bool invulnerable = qa_q1_host_random(game) < 0.25;
  qa_q1_actor *powerup =
      qa_q1_create(game, invulnerable ? "item_artifact_invulnerability" : "item_artifact_super_damage", NULL, NULL);
  qa_q1_spawn_entity(game, powerup);
  powerup->movement_flags = 256;
  powerup->solid = "trigger";
  powerup->movement = "bounce";
  qa_q1_body_patch patch = {.has_origin = true, .origin = qa_q1_get_body(game, dead).origin,
                            .has_velocity = true, .velocity = {0, 0, 300},
                            .has_bounds = true, .bounds = {{-12, -12, -12}, {12, 12, 12}}};
  qa_q1_set_body(game, powerup, &patch);
  qa_q1_link(game, powerup);
  qa_q1ad_horde_schedule(horde, powerup, "powerup_wait", 10);
}
