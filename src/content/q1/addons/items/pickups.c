/* Port of src/content/q1/addons/items/pickups.ts (quakec_mg3/mg3_items.qc and items.qc). GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/pickups.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/addons/items/common.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/foundation/js.h"

#define TETTE 512

double qa_q1ad_mg3_weapon_rank(qa_q1_weapon weapon) {
  if (strcmp(weapon, "lightning") == 0) return 1;
  if (strcmp(weapon, "rocketlauncher") == 0) return 2;
  if (strcmp(weapon, "mg3:laser") == 0) return 3;
  if (strcmp(weapon, "supernailgun") == 0) return 4;
  if (strcmp(weapon, "grenadelauncher") == 0) return 5;
  if (strcmp(weapon, "supershotgun") == 0) return 6;
  if (strcmp(weapon, "nailgun") == 0) return 7;
  return 8;
}

static double read_flags(qa_q1ad_context *context) {
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  return campaign.read_flags(campaign.ctx);
}
static void write_flags(qa_q1ad_context *context, double flags) {
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  campaign.write_flags(campaign.ctx, flags);
}
static void configure(qa_q1_services *game, qa_owned_actor actor, qa_item_id item, double count, double capacity) {
  qa_shared_inventory_table_configure(game->host.inventory, actor,
                                      (qa_inventory_entry){.item = item, .count = count, .capacity = capacity}, NULL,
                                      NULL);
}

static void weapon_touch(qa_q1ad_context *context, qa_q1_actor *entity, qa_actor_id other) {
  qa_q1_services *game = context->game;
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (player == NULL) return;
  qa_q1_weapon weapon = strcmp(entity->classname, "weapon_mjolnir") == 0 ? "mg3:mjolnir" : "mg3:laser";
  double deathmatch = game->options.deathmatch;
  bool leave = game->options.coop || deathmatch == 2 || deathmatch == 3 || deathmatch == 5;
  qa_item_id item = qa_q1_services_weapon_item(game, weapon);
  const qa_pickup_admission *admission =
      game->pickup_admission != NULL && game->pickup_admission->maps(game->pickup_admission->ctx, "weapons", item)
          ? game->pickup_admission : NULL;
  bool owned = admission == NULL ? qa_shared_inventory_table_count(game->host.inventory, other, item) != 0
                                 : admission->owns(admission->ctx, other, item);
  if (leave && owned) return;
  bool switching = strcmp(player->auto_switch, "always") == 0 || (strcmp(player->auto_switch, "new") == 0 && !owned);
  if (admission != NULL) {
    qa_pickup_selection selection = switching ? deathmatch == 0 ? QA_PICKUP_SELECTION_ALWAYS : QA_PICKUP_SELECTION_BETTER
                                              : QA_PICKUP_SELECTION_NEVER;
    static const qa_pickup_ammo_grant AMMO[] = {{"q1:ammo/cells", 30}};
    if (!admission->weapon(admission->ctx, player->actor,
                           (qa_pickup_weapon_offer){.item = item, .ammo = AMMO, .ammo_count = 1}, selection))
      return;
  } else {
    (void)qa_shared_inventory_table_give(game->host.inventory, player->actor, "q1:ammo/cells", 30);
    configure(game, player->actor, qa_q1_services_weapon_item(game, weapon), 1, 1);
    if (switching && (deathmatch == 0 || qa_q1ad_mg3_weapon_rank(weapon) < qa_q1ad_mg3_weapon_rank(player->weapon)))
      (void)qa_q1_select_weapon(game, player->actor, weapon);
    (void)qa_q1_select_weapon(game, player->actor, player->weapon);
  }
  qa_q1_msg_arg args[] = {{.text = qa_q1_actor_text(entity, "netname")}};
  qa_q1_message(game, &other, "$qc_got_item", true, args, 1);
  qa_q1_sound(game, player->actor.id, "weapons/pkup.wav", "item", 1, 1);
  qa_q1_effect(game, "pickup", qa_q1_get_body(game, entity).origin, &other, 1);
  entity->has_activator = true;
  entity->activator = other;
  qa_q1_use_targets(game, entity, &other);
  if (!qa_q1_live(game, entity)) return;
  if (leave) { qa_q1ad_set_text(&entity->target, ""); return; }
  qa_q1ad_set_text(&entity->model, "");
  entity->solid = "none";
  qa_q1_link(game, entity);
  double respawn = deathmatch != 0 && deathmatch != 2 ? 30 : entity->wait;
  if (respawn > 0) qa_q1_schedule(game, entity, respawn, qa_q1_callback_registry_action(game->named, entity, "SUB_regen"));
  else qa_q1_cancel(game, entity);
}

static void silent_teleport(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                            const qa_touch_surface *surface) {
  qa_q1_call_touch(game, entity, qa_q1_callback_registry_touch(game->named, entity, "mg3:trigger:silent_teleport"),
                   other, normal, surface);
}

static void ring_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                       const qa_touch_surface *surface) {
  (void)ctx; /* TS handler reads no context state */
  if (!qa_q1_is_player(game, &other)) return;
  silent_teleport(game, entity, other, normal, surface);
  qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, other);
  if (body.has_value) qa_q1_effect(game, "teleport", body.value.origin, NULL, 1);
}

static void spawn_ring(qa_q1ad_context *context, qa_q1_actor *entity, const char *model, const char *label,
                       double height) {
  qa_q1_services *game = context->game;
  char path[64];
  (void)snprintf(path, sizeof path, "progs/%s.mdl", model);
  qa_q1ad_set_text(&entity->model, path);
  (void)qa_q1_actor_field_set(entity, "netname", label);
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, -24}, {16, 16, 32}});
  if (qa_q1_actor_number(entity, "height") == 0) qa_q1ad_set_number(context, entity, "height", height);
  bool teleport = (qa_js_to_int32(entity->spawnflags) & 1) != 0 && entity->target[0] != '\0';
  entity->touch = qa_q1_callback_registry_touch(game->named, entity,
                                                teleport ? "teleport_touch" : QA_Q1AD_MG3_ITEM_PREFIX "ring_touch");
  if ((qa_js_to_int32(entity->spawnflags) & QA_Q1AD_MG3_SPAWNED_ITEM) == 0) qa_q1ad_ring_effect(context, entity, 8);
  qa_q1ad_start_mg3_item(context, entity);
}

static void spawn_insight(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* spawn signature */
  spawn_ring(ctx, entity, "gold_ring", "$mg3_qc_ring_of_insight", -2048);
}
static void spawn_stupor(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* spawn signature */
  spawn_ring(ctx, entity, "onyx_ring", "$mg3_qc_ring_of_oblivion", 2048);
}

typedef struct {
  qa_q1ad_context *context;
  qa_q1_actor *entity;
  qa_actor_id other;
  qa_q1_player_state *player;
} shard_pickup;

static bool shard_original(void *ctx) {
  shard_pickup *pickup = ctx;
  qa_q1_services *game = pickup->context->game;
  qa_combat_state_result state = qa_gameplay_authority_read(game->host.combat, pickup->other);
  bool known = state.has_value;
  qa_regular_armor_state armor = known ? state.value.armor.regular : (qa_regular_armor_state){0};
  if (known && armor.kind == QA_REGULAR_ARMOR_SOURCE) return false;
  if (known && armor.kind == QA_REGULAR_ARMOR_Q1 && armor.absorption < 0.3) {
    qa_regular_armor_state raised = armor;
    raised.absorption = 0.3;
    qa_gameplay_authority_set_regular_armor(game->host.combat, pickup->player->actor, raised);
  }
  double points = !known || armor.kind == QA_REGULAR_ARMOR_NONE ? 0 : armor.points;
  if (points >= 200) return false;
  bool q1 = known && armor.kind == QA_REGULAR_ARMOR_Q1;
  qa_gameplay_authority_set_regular_armor(
      game->host.combat, pickup->player->actor,
      (qa_regular_armor_state){.kind = QA_REGULAR_ARMOR_Q1, .points = qa_js_min(200, points + 5),
                               .absorption = q1 ? qa_js_max(0.3, armor.absorption) : 0.3,
                               .item = q1 ? armor.item : "q1:item_armor1"});
  return true;
}

static void shard_complete(void *ctx, bool taken) {
  shard_pickup *pickup = ctx;
  if (taken)
    qa_q1ad_finish_mg3_pickup(pickup->context, pickup->entity, pickup->other, "$mg3_qc_armor_shard_touch",
                              "items/armor1.wav", NULL, 0);
}

static void shard_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                        const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS handler ignores them */
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (player == NULL || qa_q1_health(game, other) <= 0) return;
  shard_pickup pickup = {ctx, entity, other, player};
  qa_q1_touch_pickup(game, entity, other, "q1:item_armor_shard", "protection",
                     (qa_q1_pickup_continuation){.ctx = &pickup, .original = shard_original,
                                                 .complete = shard_complete});
}

static void spawn_shard(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_set_text(&entity->model, "progs/armorshard.mdl");
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, QA_Q1AD_MG3_ITEM_PREFIX "shard_touch");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, 0}, {16, 16, 56}});
  qa_q1ad_start_mg3_item(ctx, entity);
}

static void weapon_touch_handler(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                                 const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface; /* TS handler ignores them */
  weapon_touch(ctx, entity, other);
}

static void mjolnir_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                          const qa_vec3 *normal, const qa_touch_surface *surface) {
  weapon_touch(ctx, entity, other);
  silent_teleport(game, entity, other, normal, surface);
}

static void spawn_weapon(qa_q1ad_context *context, qa_q1_actor *entity, const char *model, const char *label,
                         bool mjolnir) {
  qa_q1_services *game = context->game;
  char path[64];
  (void)snprintf(path, sizeof path, "progs/%s.mdl", model);
  qa_q1ad_set_text(&entity->model, path);
  (void)qa_q1_actor_field_set(entity, "netname", label);
  bool teleport = mjolnir && (qa_js_to_int32(entity->spawnflags) & 128) != 0;
  if (teleport && qa_q1_actor_number(entity, "height") == 0) qa_q1ad_set_number(context, entity, "height", -2048);
  entity->touch = qa_q1_callback_registry_touch(
      game->named, entity, teleport ? QA_Q1AD_MG3_ITEM_PREFIX "mjolnir_touch" : QA_Q1AD_MG3_ITEM_PREFIX "weapon_touch");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, 0}, {16, 16, 56}});
  qa_q1ad_start_mg3_item(context, entity);
}

static void spawn_laser_gun(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* spawn signature */
  spawn_weapon(ctx, entity, "g_laserg", "$qc_laser_cannon", false);
}
static void spawn_mjolnir(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* spawn signature */
  spawn_weapon(ctx, entity, "g_hammer", "$mg3_qc_hammer", true);
}

static void head_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                       const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS handler ignores them */
  qa_q1ad_context *context = ctx;
  if (!qa_q1_is_player(game, &other)) return;
  qa_q1_emit(game, (qa_q1_event){.kind = "achievement", .has_player = false, .id = "ACH_FIND_MG3_SECRET"});
  qa_q1ad_broadcast(context, "$mg3_selected_bloody_nightmare");
  write_flags(context, (double)(qa_js_to_int32(read_flags(context)) | QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE |
                                QA_Q1AD_BLOODY_NIGHTMARE_DISCOVERED));
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  campaign.set_skill(campaign.ctx, 3);
  context->services.set_cvar(context->services.ctx, "skill", "3");
  qa_q1ad_finish_mg3_pickup(context, entity, other, "", "player/tornoff2.wav", NULL, 0);
}

static void hug_tette(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                      const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface; /* TS handler ignores them */
  qa_q1ad_context *context = ctx;
  write_flags(context, (double)(qa_js_to_int32(read_flags(context)) | TETTE));
  qa_q1_msg_arg args[] = {{.text = qa_q1_actor_text(entity, "netname")}};
  qa_q1ad_finish_mg3_pickup(context, entity, other, "$qc_got_item", "misc/secret.wav", args, 1);
}

static void spawn_head(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  int32_t flags = qa_js_to_int32(read_flags(context));
  if ((flags & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0 &&
      ((flags & QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME) == 0 || (flags & TETTE) != 0)) {
    qa_q1ad_set_text(&entity->classname, "item_health");
    entity->spawnflags = 2;
    qa_q1_basis basis = qa_q1_vectors(qa_q1_get_body(game, entity).angles);
    qa_vec3 origin = qa_q1_get_body(game, entity).origin;
    qa_q1_set_origin(game, entity,
                     qa_q1_vadd(qa_q1_vadd(origin, qa_q1_vscale(basis.right, 16)), qa_q1_vscale(basis.forward, -16)));
    (void)qa_q1_spawn_pickup(game, entity);
    return;
  }
  bool bunny = (flags & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0;
  qa_q1ad_set_text(&entity->model, bunny ? "progs/g_bunny.mdl" : "progs/item_h_hellkn.mdl");
  (void)qa_q1_actor_field_set(entity, "netname", bunny ? "$mg3_qc_newgameplus_item" : "");
  entity->touch = qa_q1_callback_registry_touch(
      game->named, entity, bunny ? QA_Q1AD_MG3_ITEM_PREFIX "hug_tette" : QA_Q1AD_MG3_ITEM_PREFIX "head_touch");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, -16}, {16, 16, 40}});
  qa_q1ad_start_mg3_item(context, entity);
}

static void bloody_start(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if ((qa_js_to_int32(read_flags(context)) & QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME) == 0) qa_q1_remove(game, entity);
  else qa_q1ad_start_mg3_item(context, entity);
}

static void bloody_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                         const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS handler ignores them */
  qa_q1ad_context *context = ctx;
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (player == NULL) return;
  int32_t flag = strcmp(entity->classname, "weapon_bloody_sg") == 0 ? QA_Q1AD_MG3_BLOODY_SHOTGUN
                                                                     : QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN;
  qa_q1ad_set_player_number(context, other, "parm15",
                            (double)(qa_js_to_int32(qa_q1ad_player_number(context, other, "parm15")) | flag));
  (void)qa_shared_inventory_table_give(game->host.inventory, player->actor, "q1:ammo/shells", 30);
  qa_q1_weapon weapon = flag == QA_Q1AD_MG3_BLOODY_SHOTGUN ? "shotgun" : "supershotgun";
  configure(game, player->actor, qa_q1_services_weapon_item(game, weapon), 1, 1);
  (void)qa_q1_select_weapon(game, player->actor, weapon);
  qa_q1ad_finish_mg3_pickup(context, entity, other, "$mg3_map2_secret_weapon", "weapons/pkup.wav", NULL, 0);
}

static void spawn_bloody(qa_q1_services *game, qa_q1_actor *entity, const char *model) {
  char path[64];
  (void)snprintf(path, sizeof path, "progs/%s.mdl", model);
  qa_q1ad_set_text(&entity->model, path);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, QA_Q1AD_MG3_ITEM_PREFIX "bloody_touch");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, 0}, {16, 16, 56}});
  qa_q1_schedule(game, entity, 0.5,
                 qa_q1_callback_registry_action(game->named, entity, QA_Q1AD_MG3_ITEM_PREFIX "bloody_start"));
}
static void spawn_bloody_sg(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; /* the handler reads no context state */
  spawn_bloody(game, entity, "g_bloodshot");
}
static void spawn_bloody_ssg(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; /* the handler reads no context state */
  spawn_bloody(game, entity, "g_bloodshot2");
}

static void register_touch(qa_q1ad_context *context, const char *name, qa_q1_touch_fn touch) {
  qa_q1_callback_registry_register(context->game->named, name,
                                   (qa_q1_callback_handlers){.ctx = context, .touch = touch});
}

void qa_q1ad_register_mg3_pickups(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  register_touch(context, QA_Q1AD_MG3_ITEM_PREFIX "ring_touch", ring_touch);
  qa_q1_register_spawn(game, "item_draught_insight", spawn_insight, context);
  qa_q1_register_spawn(game, "item_draught_stupor", spawn_stupor, context);
  register_touch(context, QA_Q1AD_MG3_ITEM_PREFIX "shard_touch", shard_touch);
  qa_q1_register_spawn(game, "item_armor_shard", spawn_shard, context);
  register_touch(context, QA_Q1AD_MG3_ITEM_PREFIX "weapon_touch", weapon_touch_handler);
  register_touch(context, QA_Q1AD_MG3_ITEM_PREFIX "mjolnir_touch", mjolnir_touch);
  qa_q1_register_spawn(game, "weapon_laser_gun", spawn_laser_gun, context);
  qa_q1_register_spawn(game, "weapon_mjolnir", spawn_mjolnir, context);
  register_touch(context, QA_Q1AD_MG3_ITEM_PREFIX "head_touch", head_touch);
  register_touch(context, QA_Q1AD_MG3_ITEM_PREFIX "hug_tette", hug_tette);
  qa_q1_register_spawn(game, "item_head_hellknight", spawn_head, context);
  qa_q1_callback_registry_register(game->named, QA_Q1AD_MG3_ITEM_PREFIX "bloody_start",
                                   (qa_q1_callback_handlers){.ctx = context, .action = bloody_start});
  register_touch(context, QA_Q1AD_MG3_ITEM_PREFIX "bloody_touch", bloody_touch);
  qa_q1_register_spawn(game, "weapon_bloody_sg", spawn_bloody_sg, context);
  qa_q1_register_spawn(game, "weapon_bloody_ssg", spawn_bloody_ssg, context);
}
