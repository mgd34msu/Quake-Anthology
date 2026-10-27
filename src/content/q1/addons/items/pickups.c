/* Port of src/content/q1/addons/items/pickups.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/pickups.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/addons/items/common.h"

#define TETTE 512

double qa_q1ad_mg3_weapon_rank(const char *weapon) {
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
  return context->game.campaign_read_flags != NULL
      ? context->game.campaign_read_flags(context->game.ctx) : 0;
}
static void write_flags(qa_q1ad_context *context, double flags) {
  if (context->game.campaign_write_flags != NULL)
    context->game.campaign_write_flags(context->game.ctx, flags);
}
static void weapon_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                         const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  qa_q1ad_player *player = qa_q1ad_player_state(context, *other);
  if (player == NULL) return;
  const char *classname = entity->classname != NULL ? entity->classname : "";
  const char *weapon = strcmp(classname, "weapon_mjolnir") == 0 ? "mg3:mjolnir" : "mg3:laser";
  double dm = context->game.deathmatch;
  bool leave = context->game.coop || dm == 2 || dm == 3 || dm == 5;
  char item[64];
  qa_q1ad_weapon_item(weapon, item);
  bool admission = context->game.has_pickup_admission && context->game.admission_maps != NULL &&
      context->game.admission_maps(context->game.ctx, "weapons", item);
  bool owned = admission
      ? context->game.admission_owns != NULL && context->game.admission_owns(context->game.ctx, *other, item)
      : context->game.inventory_count != NULL &&
            context->game.inventory_count(context->game.ctx, *other, item) != 0;
  if (leave && owned) return;
  const char *auto_switch = player->auto_switch != NULL ? player->auto_switch : "";
  if (admission) {
    const char *selection = strcmp(auto_switch, "always") == 0 ||
            (strcmp(auto_switch, "new") == 0 && !owned)
        ? dm == 0 ? "always" : "better" : "never";
    if (context->game.admission_weapon == NULL ||
        !context->game.admission_weapon(context->game.ctx, player->actor, item, selection)) return;
  } else {
    if (context->game.inventory_give != NULL)
      context->game.inventory_give(context->game.ctx, player->actor, "q1:ammo/cells", 30);
    if (context->game.inventory_configure != NULL)
      context->game.inventory_configure(context->game.ctx, player->actor, item, 1, 1);
    if (strcmp(auto_switch, "always") == 0 || (strcmp(auto_switch, "new") == 0 && !owned)) {
      if ((dm == 0 || qa_q1ad_mg3_weapon_rank(weapon) <
              qa_q1ad_mg3_weapon_rank(player->weapon != NULL ? player->weapon : "")) &&
          context->game.select_weapon != NULL)
        (void)context->game.select_weapon(context->game.ctx, player->actor, weapon);
    }
    if (context->game.select_weapon != NULL)
      (void)context->game.select_weapon(context->game.ctx, player->actor, player->weapon);
  }
  qa_q1ad_msg_arg args[] = {{.text = qa_q1ad_actor_text(entity, "netname")}};
  qa_q1ad_message(context, *other, "$qc_got_item", true, args, 1);
  qa_q1ad_sound_actor(context, player->actor.id, "weapons/pkup.wav", "item");
  qa_q1ad_effect(context, "pickup", qa_q1ad_body_of(context, entity).origin, other, 0);
  entity->activator = *other;
  entity->has_activator = true;
  qa_q1ad_use_targets(context, entity, other);
  if (!qa_q1ad_live(context, entity)) return;
  if (leave) { entity->target = ""; return; }
  entity->model = "";
  entity->solid = "none";
  qa_q1ad_link(context, entity);
  double respawn = dm != 0 && dm != 2 ? 30 : entity->wait;
  if (respawn > 0) qa_q1ad_schedule(context, entity, respawn, "SUB_regen");
  else qa_q1ad_cancel(context, entity);
}
static void ring_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  if (!qa_q1ad_is_player(context, other)) return;
  qa_q1ad_do_touch(context, entity, "mg3:trigger:silent_teleport", other, normal);
  qa_q1ad_body body;
  if (qa_q1ad_body_by_id(context, *other, &body))
    qa_q1ad_effect(context, "teleport", body.origin, NULL, 0);
}
static void spawn_ring_kind(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *model,
                            const char *label, double height) {
  char path[64];
  (void)snprintf(path, sizeof path, "progs/%s.mdl", model);
  qa_q1ad_actor_set(entity, "ring.model", path);
  entity->model = qa_q1ad_actor_text(entity, "ring.model");
  qa_q1ad_actor_set(entity, "netname", label);
  qa_q1ad_set_bounds(context, entity, (qa_bounds){{-16, -16, -24}, {16, 16, 32}});
  if (qa_q1ad_actor_number(entity, "height") == 0) qa_q1ad_set_number(context, entity, "height", height);
  bool teleport = ((int32_t)entity->spawnflags & 1) != 0 &&
      entity->target != NULL && entity->target[0] != '\0';
  qa_q1ad_bind(entity->touch, teleport ? "teleport_touch" : QA_Q1AD_MG3_ITEM_PREFIX "ring_touch");
  if (((int32_t)entity->spawnflags & QA_Q1AD_MG3_SPAWNED_ITEM) == 0 && entity->count == 0) {
    entity->count = 1;
    qa_vec3 origin = qa_add3(qa_q1ad_body_of(context, entity).origin, (qa_vec3){0, 0, 8});
    qa_q1ad_host_event event = {.kind = "ambient", .origin = origin, .path = "ambience/hum1.wav",
                                .volume = 0.7, .attenuation = 3};
    qa_q1ad_host_emit(context, &event);
    if (context->game.create != NULL) {
      qa_q1ad_actor *particles = context->game.create(context->game.ctx, "particle_tele");
      if (particles != NULL) {
        qa_q1ad_set_number(context, particles, "distance", 48);
        qa_q1ad_set_origin(context, particles, origin);
        if (context->game.spawn_entity != NULL) context->game.spawn_entity(context->game.ctx, particles);
      }
    }
  }
  qa_q1ad_start_mg3_item(context, entity);
}
static void spawn_insight(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  spawn_ring_kind(context, entity, "gold_ring", "$mg3_qc_ring_of_insight", -2048);
}
static void spawn_stupor(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  spawn_ring_kind(context, entity, "onyx_ring", "$mg3_qc_ring_of_oblivion", 2048);
}
typedef struct {
  qa_q1ad_context *context;
  qa_q1ad_actor *entity;
  qa_actor_id other;
  qa_q1ad_player *player;
} shard_pickup;
static bool shard_original(void *opaque) {
  shard_pickup *pickup = opaque;
  qa_q1ad_context *context = pickup->context;
  qa_q1ad_armor armor = {0};
  bool known = context->game.combat_armor != NULL &&
      context->game.combat_armor(context->game.ctx, pickup->other, &armor);
  const char *kind = known && armor.kind != NULL ? armor.kind : "none";
  if (strcmp(kind, "source") == 0) return false;
  if (strcmp(kind, "q1") == 0 && armor.absorption < 0.3 &&
      context->game.combat_set_regular_armor != NULL) {
    armor.absorption = 0.3;
    context->game.combat_set_regular_armor(context->game.ctx, pickup->player->actor, &armor);
  }
  double points = !known || strcmp(kind, "none") == 0 ? 0 : armor.points;
  if (points >= 200) return false;
  double next_points = points + 5;
  if (next_points > 200) next_points = 200;
  qa_q1ad_armor next = {.kind = "q1", .points = next_points,
      .absorption = strcmp(kind, "q1") == 0 ? (armor.absorption > 0.3 ? armor.absorption : 0.3) : 0.3,
      .item = strcmp(kind, "q1") == 0 && armor.item != NULL ? armor.item : "q1:item_armor1"};
  if (context->game.combat_set_regular_armor != NULL)
    context->game.combat_set_regular_armor(context->game.ctx, pickup->player->actor, &next);
  return true;
}
static void shard_complete(void *opaque, bool taken) {
  shard_pickup *pickup = opaque;
  if (taken) qa_q1ad_finish_mg3_pickup(pickup->context, pickup->entity, pickup->other,
      "$mg3_qc_armor_shard_touch", "items/armor1.wav", NULL, 0);
}
static void shard_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                        const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  qa_q1ad_player *player = qa_q1ad_player_state(context, *other);
  if (player == NULL || qa_q1ad_health(context, *other) <= 0) return;
  shard_pickup pickup = {context, entity, *other, player};
  qa_q1ad_pickup_ops ops = {&pickup, shard_original, shard_complete};
  if (context->game.touch_pickup != NULL)
    context->game.touch_pickup(context->game.ctx, entity, *other, "q1:item_armor_shard", &ops);
  else ops.complete(&pickup, ops.original(&pickup));
}
static void spawn_shard(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->model = "progs/armorshard.mdl";
  qa_q1ad_bind(entity->touch, QA_Q1AD_MG3_ITEM_PREFIX "shard_touch");
  qa_q1ad_set_bounds(context, entity, (qa_bounds){{-16, -16, 0}, {16, 16, 56}});
  qa_q1ad_start_mg3_item(context, entity);
}
static void mjolnir_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                          const qa_actor_id *other, const qa_vec3 *normal) {
  weapon_touch(context, entity, other, normal);
  qa_q1ad_do_touch(context, entity, "mg3:trigger:silent_teleport", other, normal);
}
static void spawn_weapon_kind(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *model,
                              const char *label, bool mjolnir) {
  char path[64];
  (void)snprintf(path, sizeof path, "progs/%s.mdl", model);
  qa_q1ad_actor_set(entity, "weapon.model", path);
  entity->model = qa_q1ad_actor_text(entity, "weapon.model");
  qa_q1ad_actor_set(entity, "netname", label);
  bool teleport = mjolnir && ((int32_t)entity->spawnflags & 128) != 0;
  if (teleport && qa_q1ad_actor_number(entity, "height") == 0)
    qa_q1ad_set_number(context, entity, "height", -2048);
  qa_q1ad_bind(entity->touch, teleport ? QA_Q1AD_MG3_ITEM_PREFIX "mjolnir_touch"
                                       : QA_Q1AD_MG3_ITEM_PREFIX "weapon_touch");
  qa_q1ad_set_bounds(context, entity, (qa_bounds){{-16, -16, 0}, {16, 16, 56}});
  qa_q1ad_start_mg3_item(context, entity);
}
static void spawn_laser_gun(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  spawn_weapon_kind(context, entity, "g_laserg", "$qc_laser_cannon", false);
}
static void spawn_mjolnir(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  spawn_weapon_kind(context, entity, "g_hammer", "$mg3_qc_hammer", true);
}
static void head_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (!qa_q1ad_is_player(context, other)) return;
  qa_q1ad_host_event event = {.kind = "achievement", .player_null = true, .text = "ACH_FIND_MG3_SECRET"};
  qa_q1ad_host_emit(context, &event);
  qa_q1ad_broadcast(context, "$mg3_selected_bloody_nightmare");
  write_flags(context, (double)(int32_t)((int32_t)read_flags(context) |
      QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE | QA_Q1AD_BLOODY_NIGHTMARE_DISCOVERED));
  if (context->game.campaign_set_skill != NULL) context->game.campaign_set_skill(context->game.ctx, 3);
  if (context->services.set_cvar != NULL) context->services.set_cvar(context->services.ctx, "skill", "3");
  qa_q1ad_finish_mg3_pickup(context, entity, *other, "", "player/tornoff2.wav", NULL, 0);
}
static void hug_tette(qa_q1ad_context *context, qa_q1ad_actor *entity,
                      const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  write_flags(context, (double)(int32_t)((int32_t)read_flags(context) | TETTE));
  qa_q1ad_msg_arg args[] = {{.text = qa_q1ad_actor_text(entity, "netname")}};
  qa_q1ad_finish_mg3_pickup(context, entity, *other, "$qc_got_item", "misc/secret.wav", args, 1);
}
static void spawn_head(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  int32_t flags = (int32_t)read_flags(context);
  if ((flags & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0 &&
      ((flags & QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME) == 0 || (flags & TETTE) != 0)) {
    entity->classname = "item_health";
    entity->spawnflags = 2;
    qa_angle_vectors basis = qa_math_angle_vectors_rule(
        qa_q1ad_body_of(context, entity).angles, QA_ANGLE_RULE_Q1);
    if (context->game.make_vectors != NULL)
      (void)context->game.make_vectors(context->game.ctx, qa_q1ad_body_of(context, entity).angles, &basis);
    qa_vec3 origin = qa_q1ad_body_of(context, entity).origin;
    qa_q1ad_set_origin(context, entity, qa_add3(qa_add3(origin, qa_scale3(basis.right, 16)),
                                               qa_scale3(basis.forward, -16)));
    if (context->game.spawn_pickup != NULL) context->game.spawn_pickup(context->game.ctx, entity);
    return;
  }
  bool bunny = (flags & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0;
  entity->model = bunny ? "progs/g_bunny.mdl" : "progs/item_h_hellkn.mdl";
  qa_q1ad_actor_set(entity, "netname", bunny ? "$mg3_qc_newgameplus_item" : "");
  qa_q1ad_bind(entity->touch, bunny ? QA_Q1AD_MG3_ITEM_PREFIX "hug_tette"
                                    : QA_Q1AD_MG3_ITEM_PREFIX "head_touch");
  qa_q1ad_set_bounds(context, entity, (qa_bounds){{-16, -16, -16}, {16, 16, 40}});
  qa_q1ad_start_mg3_item(context, entity);
}
static void bloody_start(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (((int32_t)read_flags(context) & QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME) == 0)
    qa_q1ad_remove(context, entity);
  else qa_q1ad_start_mg3_item(context, entity);
}
static void bloody_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                         const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  qa_q1ad_player *player = qa_q1ad_player_state(context, *other);
  if (player == NULL) return;
  const char *classname = entity->classname != NULL ? entity->classname : "";
  int flag = strcmp(classname, "weapon_bloody_sg") == 0
      ? QA_Q1AD_MG3_BLOODY_SHOTGUN : QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN;
  qa_q1ad_set_player_number(context, *other, "parm15",
      (double)(int32_t)((int32_t)qa_q1ad_player_number(context, *other, "parm15") | flag));
  if (context->game.inventory_give != NULL)
    context->game.inventory_give(context->game.ctx, player->actor, "q1:ammo/shells", 30);
  const char *weapon = flag == QA_Q1AD_MG3_BLOODY_SHOTGUN ? "shotgun" : "supershotgun";
  char item[64];
  qa_q1ad_weapon_item(weapon, item);
  if (context->game.inventory_configure != NULL)
    context->game.inventory_configure(context->game.ctx, player->actor, item, 1, 1);
  if (context->game.select_weapon != NULL)
    (void)context->game.select_weapon(context->game.ctx, player->actor, weapon);
  qa_q1ad_finish_mg3_pickup(context, entity, *other, "$mg3_map2_secret_weapon", "weapons/pkup.wav", NULL, 0);
}
static void spawn_bloody_kind(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *model) {
  char path[64];
  (void)snprintf(path, sizeof path, "progs/%s.mdl", model);
  qa_q1ad_actor_set(entity, "weapon.model", path);
  entity->model = qa_q1ad_actor_text(entity, "weapon.model");
  qa_q1ad_bind(entity->touch, QA_Q1AD_MG3_ITEM_PREFIX "bloody_touch");
  qa_q1ad_set_bounds(context, entity, (qa_bounds){{-16, -16, 0}, {16, 16, 56}});
  qa_q1ad_schedule(context, entity, 0.5, QA_Q1AD_MG3_ITEM_PREFIX "bloody_start");
}
static void spawn_bloody_sg(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  spawn_bloody_kind(context, entity, "g_bloodshot");
}
static void spawn_bloody_ssg(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  spawn_bloody_kind(context, entity, "g_bloodshot2");
}

void qa_q1ad_register_mg3_pickups(qa_q1ad_context *context) {
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "ring_touch",
      (qa_q1ad_handlers){.touch = ring_touch});
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "shard_touch",
      (qa_q1ad_handlers){.touch = shard_touch});
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "weapon_touch",
      (qa_q1ad_handlers){.touch = weapon_touch});
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "mjolnir_touch",
      (qa_q1ad_handlers){.touch = mjolnir_touch});
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "head_touch",
      (qa_q1ad_handlers){.touch = head_touch});
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "hug_tette",
      (qa_q1ad_handlers){.touch = hug_tette});
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "bloody_start",
      (qa_q1ad_handlers){.action = bloody_start});
  qa_q1ad_register_named(context, QA_Q1AD_MG3_ITEM_PREFIX "bloody_touch",
      (qa_q1ad_handlers){.touch = bloody_touch});
  qa_q1ad_register_spawn(context, "item_draught_insight", false, spawn_insight);
  qa_q1ad_register_spawn(context, "item_draught_stupor", false, spawn_stupor);
  qa_q1ad_register_spawn(context, "item_armor_shard", false, spawn_shard);
  qa_q1ad_register_spawn(context, "weapon_laser_gun", false, spawn_laser_gun);
  qa_q1ad_register_spawn(context, "weapon_mjolnir", false, spawn_mjolnir);
  qa_q1ad_register_spawn(context, "item_head_hellknight", false, spawn_head);
  qa_q1ad_register_spawn(context, "weapon_bloody_sg", false, spawn_bloody_sg);
  qa_q1ad_register_spawn(context, "weapon_bloody_ssg", false, spawn_bloody_ssg);
}
