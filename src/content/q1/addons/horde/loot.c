/* Port of src/content/q1/addons/horde/loot.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/horde/loot.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  qa_item_id item;
  const char *classname, *model, *label;
  double amount, capacity;
} ammo_def;
static ammo_def ammo_definition(double type, bool big) {
  if (type == 1) return (ammo_def){"q1:ammo/shells", "item_shells", "shell", "$qc_shells", big ? 40 : 20, 100};
  if (type == 2) return (ammo_def){"q1:ammo/nails", "item_spikes", "nail", "$qc_nails", big ? 50 : 25, 200};
  if (type == 3) return (ammo_def){"q1:ammo/rockets", "item_rockets", "rock", "$qc_rockets", big ? 10 : 5, 100};
  return (ammo_def){"q1:ammo/cells", "item_cells", "batt", "$qc_cells", big ? 12 : 6, 100};
}
static qa_q1ad_horde *horde_of(qa_q1ad_context *context) { return context->slice; }
static bool actor_live(qa_q1ad_context *context, qa_actor_id actor) {
  return context->game.is_live == NULL || context->game.is_live(context->game.ctx, actor);
}
static void taken(qa_q1ad_context *context, qa_q1ad_actor *entity, qa_actor_id player, const char *sound) {
  qa_owned_actor actor;
  if (qa_q1ad_resolve_owned(context, player, &actor)) qa_q1ad_sound_actor(context, actor.id, sound, "item");
  if (!qa_q1ad_live(context, entity) || !actor_live(context, player)) return;
  qa_q1ad_effect(context, "pickup", qa_q1ad_body_of(context, entity).origin, &player, 0);
  if (!qa_q1ad_live(context, entity) || !actor_live(context, player)) return;
  entity->solid = "none";
  entity->model = "";
  qa_q1ad_link(context, entity);
}
static void msg(qa_q1ad_context *context, qa_actor_id player, const char *key,
                const qa_q1ad_msg_arg *args, size_t count) {
  qa_q1ad_message(context, player, key, false, args, count);
}
static void ammo_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  qa_q1ad_horde *horde = horde_of(context);
  if (!qa_q1ad_is_player(context, other) || qa_q1ad_health(context, *other) <= 0) return;
  ammo_def definition = ammo_definition(qa_q1ad_actor_number(entity, "weapon"), false);
  double held = context->game.inventory_count != NULL
      ? context->game.inventory_count(context->game.ctx, *other, definition.item) : 0;
  if (held >= definition.capacity) return;
  qa_q1ad_player *player = qa_q1ad_player_state(context, *other);
  if (player != NULL && context->game.choose_best != NULL) {
    const char *best = context->game.choose_best(context->game.ctx, player->actor);
    if (best != NULL && player->weapon != NULL && strcmp(player->weapon, best) == 0 &&
        context->game.select_weapon != NULL)
      (void)context->game.select_weapon(context->game.ctx, player->actor, best);
  }
  qa_q1ad_msg_arg args[] = {{.text = definition.label}};
  msg(context, *other, "$qc_got_item", args, 1);
  taken(context, entity, *other, "weapons/lock4.wav");
  qa_actor_id living[32];
  size_t count = qa_q1ad_horde_living_players(horde, living, 32);
  for (size_t i = 0; i < count; i++) {
    qa_owned_actor actor;
    if (!qa_q1ad_resolve_owned(context, living[i], &actor)) continue;
    if (context->game.inventory_give != NULL)
      context->game.inventory_give(context->game.ctx, actor, definition.item,
                                   qa_q1ad_actor_number(entity, "aflag"));
  }
  qa_q1ad_actor *owner = entity->has_owner ? qa_q1ad_entity(context, &entity->owner) : NULL;
  if (owner != NULL) qa_q1ad_horde_schedule(horde, owner, "ammo", 20);
  qa_q1ad_remove(context, entity);
}
static void health_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                         const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  qa_q1ad_player *player = qa_q1ad_player_state(context, *other);
  if (player == NULL) return;
  double health = qa_q1ad_health(context, *other);
  if (health <= 0 || health >= player->max_health) return;
  double flags = context->game.campaign_read_flags != NULL
      ? context->game.campaign_read_flags(context->game.ctx) : 0;
  if (qa_q1ad_cvar(context, "horde") != 0 && ((int32_t)flags & 2) != 0)
    qa_q1ad_set_player_number(context, *other, "hunger_time", context->game.time + 10);
  double amount = qa_q1ad_actor_number(entity, "healamount");
  if (context->game.combat_set_health != NULL) {
    double next = health + amount;
    context->game.combat_set_health(context->game.ctx, player->actor,
        next < player->max_health ? next : player->max_health);
  }
  qa_q1ad_msg_arg args[] = {{.is_number = true, .number = amount}};
  msg(context, *other, "$qc_item_health", args, 1);
  taken(context, entity, *other, qa_q1ad_actor_text(entity, "noise"));
  qa_q1ad_actor *owner = entity->has_owner ? qa_q1ad_entity(context, &entity->owner) : NULL;
  if (owner != NULL) owner->wait = 0;
  qa_q1ad_remove(context, entity);
}
typedef struct {
  qa_q1ad_context *context;
  qa_q1ad_actor *entity;
  qa_actor_id other;
  qa_owned_actor actor;
} armor_pickup;
static bool armor_original(void *opaque) {
  armor_pickup *pickup = opaque;
  qa_q1ad_context *context = pickup->context;
  const char *classname = pickup->entity->classname != NULL ? pickup->entity->classname : "";
  double absorption = strcmp(classname, "item_armor1") == 0 ? 0.3
      : strcmp(classname, "item_armor2") == 0 ? 0.6 : 0.8;
  double points = strcmp(classname, "item_armor1") == 0 ? 100
      : strcmp(classname, "item_armor2") == 0 ? 150 : 200;
  qa_q1ad_armor armor = {0};
  bool known = context->game.combat_armor != NULL &&
      context->game.combat_armor(context->game.ctx, pickup->other, &armor);
  const char *kind = known && armor.kind != NULL ? armor.kind : "none";
  if (strcmp(kind, "source") == 0) return false;
  double protection = !known || strcmp(kind, "none") == 0 ? 0
      : armor.points * (strcmp(kind, "q1") == 0 ? armor.absorption
          : strcmp(kind, "q2") == 0 ? armor.normal_protection : armor.protection);
  if (protection >= absorption * points) return false;
  char item[64];
  (void)snprintf(item, sizeof item, "q1:%s", classname);
  qa_q1ad_actor_set(pickup->entity, "horde.armorItem", item);
  if (context->game.combat_set_regular_armor != NULL) {
    qa_q1ad_armor next = {.kind = "q1", .points = points, .absorption = absorption,
                          .item = qa_q1ad_actor_text(pickup->entity, "horde.armorItem")};
    context->game.combat_set_regular_armor(context->game.ctx, pickup->actor, &next);
  }
  return true;
}
static void armor_complete(void *opaque, bool accepted) {
  armor_pickup *pickup = opaque;
  qa_q1ad_context *context = pickup->context;
  if (!accepted) return;
  msg(context, pickup->other, "$qc_item_armor", NULL, 0);
  if (!qa_q1ad_live(context, pickup->entity) || !actor_live(context, pickup->other)) return;
  taken(context, pickup->entity, pickup->other, "items/armor1.wav");
  if (!qa_q1ad_live(context, pickup->entity)) return;
  qa_q1ad_actor *owner = pickup->entity->has_owner
      ? qa_q1ad_entity(context, &pickup->entity->owner) : NULL;
  if (owner != NULL) owner->wait = 0;
  qa_q1ad_remove(context, pickup->entity);
}
static void armor_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                        const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (!qa_q1ad_is_player(context, other) || qa_q1ad_health(context, *other) <= 0) return;
  qa_owned_actor actor;
  if (!qa_q1ad_resolve_owned(context, *other, &actor)) return;
  char item[64];
  (void)snprintf(item, sizeof item, "q1:%s", entity->classname != NULL ? entity->classname : "");
  armor_pickup pickup = {context, entity, *other, actor};
  qa_q1ad_pickup_ops ops = {&pickup, armor_original, armor_complete};
  if (context->game.touch_pickup != NULL)
    context->game.touch_pickup(context->game.ctx, entity, *other, item, &ops);
  else ops.complete(&pickup, ops.original(&pickup));
}
static void key_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                      const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  qa_q1ad_horde *horde = horde_of(context);
  if (!qa_q1ad_is_player(context, other) || qa_q1ad_health(context, *other) <= 0 ||
      (horde->services.is_bot != NULL && horde->services.is_bot(horde->services.ctx, *other))) return;
  qa_q1ad_msg_arg args[] = {{.text = qa_q1ad_actor_text(entity, "netname")}};
  msg(context, *other, "$qc_got_item", args, 1);
  taken(context, entity, *other, qa_q1ad_actor_text(entity, "noise"));
  qa_q1ad_horde_change_keys(horde, strcmp(qa_q1ad_actor_text(entity, "horde.key"), "gold") == 0 ? "gold" : "silver", 1);
  qa_q1ad_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager != NULL && qa_q1ad_actor_number(manager, "key_spawned") != 0) {
    manager->wait = 1;
    qa_q1ad_horde_schedule(horde, manager, "countdown", 0);
  }
  qa_q1ad_remove(context, entity);
}
static qa_q1ad_actor *create_item(qa_q1ad_context *context, const char *classname) {
  return context->game.create != NULL ? context->game.create(context->game.ctx, classname) : NULL;
}
static void ammo_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  bool big = qa_q1ad_random(context) * 4 <= 1;
  double roll = qa_q1ad_random(context) * 20;
  double type = roll <= 7 ? 1 : roll <= 14 ? 2 : roll <= 17 ? 3 : 4;
  ammo_def definition = ammo_definition(type, big);
  qa_q1ad_actor *item = create_item(context, definition.classname);
  if (item == NULL) return;
  double offset = big ? 16 : 12;
  qa_vec3 position = qa_sub3(qa_q1ad_body_of(context, entity).origin, (qa_vec3){offset, offset, 0});
  char model[64];
  (void)snprintf(model, sizeof model, "maps/b_%s%d.bsp", definition.model, big ? 1 : 0);
  qa_q1ad_actor_set(item, "horde.model", model);
  item->model = qa_q1ad_actor_text(item, "horde.model");
  qa_q1ad_actor_set(item, "netname", definition.label);
  qa_q1ad_set_number(context, item, "weapon", type);
  qa_q1ad_set_number(context, item, "aflag", definition.amount);
  item->owner = entity->actor.id;
  item->has_owner = true;
  entity->wait = 1;
  item->movement = "toss";
  item->solid = "trigger";
  item->movement_flags = 256;
  qa_q1ad_bind(item->touch, "mg1:horde:ammo_touch");
  qa_q1ad_body_patch patch = {.has_origin = true, .origin = qa_add3(position, (qa_vec3){0, 0, 1}),
      .has_bounds = true, .bounds = {{0, 0, 0}, {32, 32, 56}}};
  qa_q1ad_set_body(context, item, &patch);
  qa_q1ad_link(context, item);
  qa_q1ad_effect(context, "teleport", qa_add3(position, (qa_vec3){0, 0, 8}), NULL, 0);
}
static void item_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  bool health = qa_q1ad_random(context) * 6 < 5;
  qa_q1ad_actor *item = create_item(context, health ? "item_health" : "item_armor1");
  if (item == NULL) return;
  /* The two independent source if statements replace rotten health with ordinary health. */
  qa_vec3 position = health
      ? qa_sub3(qa_q1ad_body_of(context, entity).origin, (qa_vec3){16, 16, 0})
      : qa_q1ad_body_of(context, entity).origin;
  item->model = health ? "maps/b_bh25.bsp" : "progs/armor.mdl";
  qa_q1ad_actor_set(item, "noise", "items/health1.wav");
  qa_q1ad_set_number(context, item, "healamount", 25);
  qa_q1ad_bind(item->touch, health ? "mg1:horde:health_touch" : "mg1:horde:armor_touch");
  item->owner = entity->actor.id;
  item->has_owner = true;
  item->movement = "toss";
  item->solid = "trigger";
  item->movement_flags = 256;
  entity->wait = 1;
  qa_bounds bounds = health ? (qa_bounds){{0, 0, 0}, {32, 32, 56}}
                             : (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
  qa_q1ad_body_patch patch = {.has_origin = true, .origin = qa_add3(position, (qa_vec3){0, 0, 1}),
                              .has_bounds = true, .bounds = bounds};
  qa_q1ad_set_body(context, item, &patch);
  qa_q1ad_link(context, item);
  qa_q1ad_effect(context, "teleport", qa_add3(position, (qa_vec3){0, 0, 8}), NULL, 0);
}
static void spawn_key(qa_q1ad_context *context, qa_q1ad_actor *entity, bool gold) {
  qa_q1ad_horde *horde = horde_of(context);
  const char *name = gold ? "gold" : "silver";
  bool metal = context->game.world_type == 1, base = context->game.world_type == 2;
  char label[48], netname[64], model[64], announce[96];
  (void)snprintf(label, sizeof label, "%s_%s", name, metal ? "runekey" : base ? "keycard" : "key");
  qa_q1ad_actor *item = create_item(context, gold ? "item_key2" : "item_key1");
  if (item == NULL) return;
  qa_q1ad_actor_set(item, "horde.key", name);
  (void)snprintf(netname, sizeof netname, "$qc_%s", label);
  qa_q1ad_actor_set(item, "netname", netname);
  qa_q1ad_actor_set(item, "noise", metal ? "misc/runekey.wav" : base ? "misc/basekey.wav" : "misc/medkey.wav");
  (void)snprintf(model, sizeof model, "progs/%s_%s_key.mdl", metal ? "m" : base ? "b" : "w", gold ? "g" : "s");
  qa_q1ad_actor_set(item, "horde.model", model);
  item->model = qa_q1ad_actor_text(item, "horde.model");
  item->target = "horde_manager";
  item->movement = "toss";
  item->solid = "trigger";
  item->movement_flags = 256;
  if (!metal) item->effects = 4;
  qa_q1ad_bind(item->touch, "mg1:horde:key_touch");
  qa_q1ad_body_patch patch = {
      .has_origin = true, .origin = qa_add3(qa_q1ad_body_of(context, entity).origin, (qa_vec3){0, 0, 32}),
      .has_velocity = true, .velocity = {0, 0, 255},
      .has_bounds = true, .bounds = {{-16, -16, -25}, {16, 16, 32}}};
  qa_q1ad_set_body(context, item, &patch);
  qa_q1ad_link(context, item);
  (void)snprintf(announce, sizeof announce, "$qc_horde_%s_appears", label);
  qa_q1ad_broadcast(context, announce);
  qa_q1ad_effect(context, "teleport", qa_q1ad_body_of(context, item).origin, NULL, 0);
  qa_q1ad_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager != NULL) qa_q1ad_set_number(context, manager, "key_spawned", 1);
}
static void silver_action(qa_q1ad_context *context, qa_q1ad_actor *entity) { spawn_key(context, entity, false); }
static void gold_action(qa_q1ad_context *context, qa_q1ad_actor *entity) { spawn_key(context, entity, true); }
static void powerup_fade(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  double alpha = qa_q1ad_actor_number(entity, "alpha");
  if (alpha <= 0) { qa_q1ad_remove(context, entity); return; }
  qa_q1ad_alpha(context, entity, (double)(float)(alpha - 0.25 * context->frame_time));
  qa_q1ad_horde_schedule(horde_of(context), entity, "powerup_fade", 0);
}
static void powerup_wait(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_body_of(context, entity).velocity.z < 0) { qa_q1ad_remove(context, entity); return; }
  qa_q1ad_alpha(context, entity, 1);
  qa_q1ad_horde_schedule(horde_of(context), entity, "powerup_fade", 0);
}
static void spawn_ammo_point(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_horde_schedule(horde_of(context), entity, "ammo", 10 + qa_q1ad_random(context) * 3);
}
static void spawn_wait_zero(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  (void)context;
  entity->wait = 0;
}

void qa_q1ad_register_horde_loot(qa_q1ad_horde *horde) {
  qa_q1ad_context *context = horde->context;
  qa_q1ad_register_named(context, "mg1:horde:ammo_touch", (qa_q1ad_handlers){.touch = ammo_touch});
  qa_q1ad_register_named(context, "mg1:horde:health_touch", (qa_q1ad_handlers){.touch = health_touch});
  qa_q1ad_register_named(context, "mg1:horde:armor_touch", (qa_q1ad_handlers){.touch = armor_touch});
  qa_q1ad_register_named(context, "mg1:horde:key_touch", (qa_q1ad_handlers){.touch = key_touch});
  qa_q1ad_register_named(context, "mg1:horde:ammo", (qa_q1ad_handlers){.action = ammo_action});
  qa_q1ad_register_named(context, "mg1:horde:item", (qa_q1ad_handlers){.action = item_action});
  qa_q1ad_register_named(context, "mg1:horde:silver", (qa_q1ad_handlers){.action = silver_action});
  qa_q1ad_register_named(context, "mg1:horde:gold", (qa_q1ad_handlers){.action = gold_action});
  qa_q1ad_register_named(context, "mg1:horde:powerup_fade", (qa_q1ad_handlers){.action = powerup_fade});
  qa_q1ad_register_named(context, "mg1:horde:powerup_wait", (qa_q1ad_handlers){.action = powerup_wait});
  qa_q1ad_register_spawn(context, "info_horde_ammo", false, spawn_ammo_point);
  qa_q1ad_register_spawn(context, "info_horde_item", false, spawn_wait_zero);
  qa_q1ad_register_spawn(context, "info_horde_key", false, spawn_wait_zero);
}

void qa_q1ad_spawn_horde_powerup(qa_q1ad_horde *horde, qa_q1ad_actor *dead) {
  qa_q1ad_context *context = horde->context;
  qa_q1ad_actor *manager = qa_q1ad_horde_manager(horde);
  if (manager == NULL) return;
  double chance = qa_q1ad_actor_number(manager, "powerup_chance");
  if (chance == 0) chance = 0.025;
  if (qa_q1ad_random(context) >= chance) {
    qa_q1ad_set_number(context, manager, "powerup_chance", chance + 0.025);
    return;
  }
  qa_q1ad_set_number(context, manager, "powerup_chance", 0.025);
  bool invulnerable = qa_q1ad_random(context) < 0.25;
  qa_q1ad_actor *powerup = create_item(context,
      invulnerable ? "item_artifact_invulnerability" : "item_artifact_super_damage");
  if (powerup == NULL) return;
  if (context->game.spawn_entity != NULL) context->game.spawn_entity(context->game.ctx, powerup);
  powerup->movement_flags = 256;
  powerup->solid = "trigger";
  powerup->movement = "bounce";
  qa_q1ad_body_patch patch = {.has_origin = true, .origin = qa_q1ad_body_of(context, dead).origin,
      .has_velocity = true, .velocity = {0, 0, 300},
      .has_bounds = true, .bounds = {{-12, -12, -12}, {12, 12, 12}}};
  qa_q1ad_set_body(context, powerup, &patch);
  qa_q1ad_link(context, powerup);
  qa_q1ad_horde_schedule(horde, powerup, "powerup_wait", 10);
}
