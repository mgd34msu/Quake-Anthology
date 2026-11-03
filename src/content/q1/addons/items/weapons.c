/* Port of src/content/q1/addons/items/weapons.ts (quakec_mg3/mg3_weapons.qc, weapons.qc and player.qc).
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/weapons.h"

#include <math.h>
#include <string.h>

#include "qa/content/q1/addons/items/pickups.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/content/q1/missionpacks/hipnotic_weapons.h"
#include "qa/foundation/js.h"

static double fr(double value) { return (double)(float)value; }

static bool infinite_ammo(qa_q1ad_context *context, qa_q1_player_state *player) {
  return qa_q1ad_player_number(context, player->actor.id, "infiniteammo") != 0;
}

static double inventory_count(qa_q1_services *game, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(game->host.inventory, actor, item);
}

static void emit_weapon(qa_q1_services *game, qa_q1_player_state *player, double punch) {
  qa_q1_emit(game, (qa_q1_event){.kind = "weapon", .has_player = true, .player = player->actor.id,
                                 .weapon = player->weapon,
                                 .view_model = qa_q1_services_weapon_model(game, player->weapon, player),
                                 .frame = player->weapon_frame, .punch = punch});
}

bool qa_q1ad_mg3_hammer_body_frame(qa_q1ad_context *context, qa_q1_player_state *player, double *out) {
  if (strcmp(player->weapon, "mg3:mjolnir") != 0 || player->weapon_animation_at < 0 || player->weapon_frame < 1 ||
      player->weapon_frame > 4)
    return false;
  *out = qa_q1ad_player_number(context, player->actor.id, "mg3.hammerBodyBase") + player->weapon_frame;
  return true;
}

void qa_q1ad_mg3_weapon_frame(qa_q1ad_context *context, qa_q1_player_state *player) {
  qa_q1_services *game = context->game;
  if (qa_q1_health(game, player->actor.id) <= 0) return;
  if (strcmp(player->weapon, "mg3:mjolnir") == 0 &&
      qa_q1ad_player_number(context, player->actor.id, "mg3.hammerGlow") != 0 &&
      qa_q1ad_player_number(context, player->actor.id, "last_mjolnir_hit_time") <= fr(game->time)) {
    qa_q1ad_set_player_number(context, player->actor.id, "mg3.hammerGlow", 0);
    emit_weapon(game, player, 0);
  }
  if (game->time <= player->attack_finished || strcmp(player->weapon, "axe") == 0 ||
      strcmp(player->weapon, "mg3:mjolnir") == 0)
    return;
  qa_item_id ammo = qa_q1_services_weapon_ammo(game, player->weapon);
  if (ammo == NULL || inventory_count(game, player->actor.id, ammo) != 0) return;
  const char *name = strcmp(ammo, "q1:ammo/shells") == 0    ? "item_shells"
                     : strcmp(ammo, "q1:ammo/nails") == 0   ? "item_spikes"
                     : strcmp(ammo, "q1:ammo/rockets") == 0 ? "item_rockets"
                     : strcmp(ammo, "q1:ammo/cells") == 0   ? "item_cells"
                                                            : NULL;
  if (name != NULL)
    for (size_t i = 0; i < game->entity_count; i++) {
      double delay;
      qa_q1_bound_action callback;
      qa_q1_actor *entity = game->entities[i];
      if (strcmp(entity->classname, name) == 0 && (qa_js_to_int32(entity->spawnflags) & 8) != 0 &&
          strcmp(entity->solid, "none") == 0)
        (delay = 0.5 * qa_q1_host_random(game), callback = qa_q1_callback_registry_action(game->named, entity, "SUB_regen"), qa_q1_schedule(game, entity, delay,
                       callback));
    }
  (void)qa_q1_select_weapon(game, player->actor, qa_q1_choose_best(game, player->actor));
}

static bool fire_laser(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  qa_q1ad_context *context = ctx;
  qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (!body.has_value ||
      (!infinite_ammo(context, player) &&
       !qa_shared_inventory_table_consume(game->host.inventory, player->actor, "q1:ammo/cells", 1)))
    return false;
  qa_q1_basis basis = qa_q1_make_vectors(game, player->view_angles);
  qa_vec3 outward = qa_q1_normalize((qa_vec3){basis.forward.x, basis.forward.y, 0});
  qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(body.value.origin, qa_q1_vscale(basis.up, 6)), qa_q1_vscale(outward, 12));
  qa_vec3 direction = qa_q1_aim(game, player->actor, basis.forward);
  bool paired = !player->continuous_firing || player->weapon_frame == 4;
  static const qa_q1mp_hipnotic_laser_profile PROFILE = {"mg3:laser", 15, 20};
  if (paired) {
    double offset = 6 * 0.707;
    qa_vec3 first = qa_q1_vsub(qa_q1_vadd(origin, qa_q1_vscale(basis.right, offset)), qa_q1_vscale(basis.up, offset));
    (void)qa_q1mp_launch_hipnotic_laser(game, player->actor.id, first, direction, false, &PROFILE);
    (void)qa_q1mp_launch_hipnotic_laser(game, player->actor.id, qa_q1_vsub(first, qa_q1_vscale(basis.right, offset * 2)),
                                        direction, false, &PROFILE);
  } else {
    qa_vec3 start = qa_q1_vadd(origin, qa_q1_vscale(basis.up, 6));
    bool light = qa_q1_host_random(game) < 0.1;
    (void)qa_q1mp_launch_hipnotic_laser(game, player->actor.id, start, direction, light, &PROFILE);
  }
  player->continuous_firing = true;
  player->weapon_animation_at = -1;
  player->weapon_frame = paired ? 1 : 4;
  player->attack_finished = player->next_weapon_frame = fr(game->time + qa_q1_weapon_frame_delay(game, player, 0.1));
  player->hostile_until = fr(game->time + 1);
  emit_weapon(game, player, -1);
  qa_q1_effect(game, "muzzleflash", body.value.origin, &player->actor.id, 1);
  return true;
}

static qa_q1_trace point_trace(qa_q1_services *game, qa_vec3 start, qa_vec3 end, qa_actor_id ignore) {
  return qa_q1_host_trace(game, (qa_q1_trace_request){.start = start, .end = end, .bounds = QA_Q1_POINT,
                                                      .has_ignore = true, .ignore = ignore, .monsters = true});
}

static void hammer_strike(void *ctx, qa_q1_services *game, qa_q1_actor *strike) {
  qa_q1ad_context *context = ctx;
  qa_q1_player_state *player = strike->has_owner ? qa_q1_player(game, strike->owner) : NULL;
  qa_body_state_result body = player == NULL ? (qa_body_state_result){0}
                                             : qa_shared_body_table_read(game->host.bodies, player->actor.id);
  if (player == NULL || !body.has_value) { qa_q1_remove(game, strike); return; }
  qa_actor_id self = player->actor.id;
  qa_q1_basis basis = qa_q1_make_vectors(game, player->view_angles);
  qa_vec3 source = qa_q1_vadd(body.value.origin, (qa_vec3){0, 0, 16});
  qa_q1_trace trace = point_trace(game, source, qa_q1_vadd(source, qa_q1_vscale(basis.forward, 64)), self);
  qa_vec3 origin = qa_q1_vsub(trace.end, qa_q1_vscale(basis.forward, 4));
  player->attack_finished = fr(game->time + qa_q1_weapon_attack_delay(game, player, 0.4));
  qa_combat_state_result combat = trace.has_actor ? qa_gameplay_authority_read(game->host.combat, trace.actor)
                                                  : (qa_combat_state_result){0};
  if (trace.has_actor && combat.has_value && combat.value.can_take_damage) {
    qa_actor_id target = trace.actor;
    qa_q1_sound(game, player->actor.id, "hipweap/mjolslap.wav", "weapon", 1, 1);
    qa_q1_effect(game, "blood", origin, &target, 40);
    qa_q1_actor *victim = qa_q1_get_entity(game, &target);
    if (victim != NULL) qa_q1ad_set_number(context, victim, "axhitme", 1);
    const char *classname = game->host.classname(game->host.ctx, target);
    double health = qa_q1_health(game, target);
    double damage = classname != NULL && (strcmp(classname, "monster_zombie") == 0 ||
                                          strcmp(classname, "monster_szombie") == 0)
                        ? 120
                    : health - 40 < 0 ? 80 : 40;
    qa_actor_id last;
    bool has_last = qa_q1ad_player_reference(context, player->actor.id, "last_mjolnir_hit", &last);
    if (qa_q1ad_player_number(context, player->actor.id, "last_mjolnir_hit_time") > fr(game->time)) {
      if (has_last && qa_same_actor(last, target) && player->water_level < 2 &&
          inventory_count(game, player->actor.id, "q1:ammo/cells") >= 15) {
        qa_q1_trace floor =
            point_trace(game, source, qa_q1_vsub(source, qa_q1_vscale(basis.up, player->water_level < 1 ? 30 : 15)), self);
        if (!infinite_ammo(context, player))
          (void)qa_shared_inventory_table_consume(game->host.inventory, player->actor, "q1:ammo/cells", 15);
        qa_q1mp_spawn_hipnotic_hammer_base(game, player, floor.end, "mg3:mjolnir");
      } else
        (void)qa_q1_damage(game, target, self, &self, damage, "mg3:mjolnir", NULL, NULL, NULL);
      qa_q1ad_set_player_number(context, player->actor.id, "last_mjolnir_hit_time", game->time);
      player->attack_finished = fr(game->time + qa_q1_weapon_attack_delay(game, player, 0.5));
    } else {
      qa_q1ad_set_player_number(context, player->actor.id, "last_mjolnir_hit_time", game->time + 0.5);
      qa_q1ad_set_player_reference(context, player->actor.id, "last_mjolnir_hit", &target);
      player->attack_finished = fr(game->time + qa_q1_weapon_attack_delay(game, player, 0.2));
      if (inventory_count(game, player->actor.id, "q1:ammo/cells") >= 15)
        qa_q1ad_set_player_number(context, player->actor.id, "mg3.hammerGlow", 1);
      (void)qa_q1_damage(game, target, self, &self, damage, "mg3:mjolnir", NULL, NULL, NULL);
    }
  } else {
    if (trace.fraction != 1) {
      qa_q1_sound(game, player->actor.id, "hipweap/mjoltink.wav", "weapon", 1, 1);
      qa_q1_effect(game, "gunshot", origin, NULL, 1);
    } else
      qa_q1_sound(game, player->actor.id, "weapons/ax1.wav", "weapon", 1, 1);
    qa_q1ad_set_player_reference(context, player->actor.id, "last_mjolnir_hit", NULL);
  }
  emit_weapon(game, player, 0);
  qa_q1_remove(game, strike);
}

static bool fire_hammer(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  qa_q1ad_context *context = ctx;
  qa_q1_actor *strike = qa_q1_create(game, "mg3_hammer_strike", NULL, NULL);
  strike->has_owner = true;
  strike->owner = player->actor.id;
  qa_q1ad_set_player_number(context, player->actor.id, "mg3.hammerBodyBase",
                            inventory_count(game, player->actor.id, "q1:ammo/cells") < 30 ? 31 : 37);
  qa_q1_schedule(game, strike, 0.2, qa_q1_callback_registry_action(game->named, strike, "mg3:hammer_strike"));
  player->continuous_firing = false;
  player->weapon_animation_at = game->time;
  player->weapon_animation_base = 1;
  player->weapon_frame = 1;
  player->attack_finished = fr(game->time + qa_q1_weapon_attack_delay(game, player, 0.5));
  player->hostile_until = fr(game->time + 1);
  emit_weapon(game, player, 0);
  return true;
}

static bool fire_shotgun(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  qa_q1ad_context *context = ctx;
  double count = inventory_count(game, player->actor.id, "q1:ammo/shells");
  int32_t bloody = qa_js_to_int32(qa_q1ad_player_number(context, player->actor.id, "parm15"));
  if (strcmp(player->weapon, "supershotgun") == 0 && (bloody & QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN) != 0 && count > 1) {
    qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player->actor.id);
    if (!body.has_value) return false;
    if (!infinite_ammo(context, player))
      (void)qa_shared_inventory_table_consume(game->host.inventory, player->actor, "q1:ammo/shells", 2);
    qa_q1_sound(game, player->actor.id, "weapons/shotgn2.wav", "weapon", 1, 1);
    qa_q1_basis basis = qa_q1_make_vectors(game, player->view_angles);
    qa_vec3 direction = qa_q1_aim(game, player->actor, basis.forward);
    qa_q1_fire_bullets(game, player->actor, direction, player->view_angles, 28, 0.3, 0.08, "supershotgun");
    player->continuous_firing = false;
    player->weapon_animation_at = game->time;
    player->weapon_animation_base = 1;
    player->weapon_frame = 1;
    player->attack_finished = fr(game->time + qa_q1_weapon_attack_delay(game, player, 0.7));
    player->hostile_until = fr(game->time + 1);
    emit_weapon(game, player, -4);
    qa_q1_effect(game, "muzzleflash", body.value.origin, &player->actor.id, 1);
    return true;
  }
  bool fired = qa_q1_fire_base_weapon(game, player);
  if (fired && strcmp(player->weapon, "shotgun") == 0 && (bloody & QA_Q1AD_MG3_BLOODY_SHOTGUN) != 0)
    player->attack_finished = fr(game->time + qa_q1_weapon_attack_delay(game, player, 0.28));
  return fired;
}

static bool consume_ammo(void *ctx, qa_q1_services *game, qa_q1_player_state *player, qa_item_id item, double amount) {
  return infinite_ammo(ctx, player) || qa_shared_inventory_table_consume(game->host.inventory, player->actor, item, amount);
}

static const char *hammer_model(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  return qa_q1ad_player_number(ctx, player->actor.id, "mg3.hammerGlow") != 0 &&
                 qa_q1ad_player_number(ctx, player->actor.id, "last_mjolnir_hit_time") > fr(game->time)
             ? "progs/v_hammer_glow.mdl"
             : "progs/v_hammer.mdl";
}

static void hammer_animate(void *ctx, qa_q1_services *game, qa_q1_player_state *player, double seconds) {
  (void)ctx; /* TS closure reads no context state */
  if (player->weapon_animation_at < 0) return;
  double step = floor((seconds - player->weapon_animation_at) / 0.1), frame = step >= 4 ? 0 : step + 1;
  if (frame != player->weapon_frame) {
    player->weapon_frame = frame;
    emit_weapon(game, player, 0);
  }
  if (step >= 4) player->weapon_animation_at = -1;
}

static bool shotgun_best_available(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)ctx; (void)game; (void)player; /* weapon === "shotgun" */
  return true;
}
static bool supershotgun_best_available(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)ctx; /* TS closure reads no context state */
  return inventory_count(game, player->actor.id, "q1:ammo/shells") >= 2;
}
static const char *shotgun_model(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)game; /* model choice reads only the player word */
  return (qa_js_to_int32(qa_q1ad_player_number(ctx, player->actor.id, "parm15")) & QA_Q1AD_MG3_BLOODY_SHOTGUN) != 0
             ? "progs/v_bloodshot.mdl"
             : "progs/v_shot.mdl";
}
static const char *supershotgun_model(void *ctx, qa_q1_services *game, qa_q1_player_state *player) {
  (void)game; /* model choice reads only the player word */
  return (qa_js_to_int32(qa_q1ad_player_number(ctx, player->actor.id, "parm15")) & QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN) != 0
             ? "progs/v_bloodshot2.mdl"
             : "progs/v_shot2.mdl";
}

void qa_q1ad_register_mg3_weapons(qa_q1ad_context *context, qa_q1ad_mg3_weapon_tables *tables) {
  qa_q1_services *game = context->game;
  tables->ammunition = (qa_q1_weapon_rules){.ctx = context, .id = "q1:mg3:ammunition", .consume_ammo = consume_ammo};
  qa_q1_register_weapon_rules(game, &tables->ammunition);
  qa_q1mp_register_hipnotic_laser_callbacks(game);
  qa_q1mp_register_hipnotic_hammer_callbacks(game);
  qa_q1_callback_registry_register(game->named, "mg3:hammer_strike",
                                   (qa_q1_callback_handlers){.ctx = context, .action = hammer_strike});
  tables->laser = (qa_q1_weapon_definition){.ctx = context, .id = "mg3:laser", .ammo = "q1:ammo/cells",
                                            .model = "progs/v_laserg.mdl", .rank = 3, .fire = fire_laser};
  qa_q1_register_weapon(game, &tables->laser);
  tables->mjolnir = (qa_q1_weapon_definition){
      .ctx = context, .id = "mg3:mjolnir", .ammo = "q1:ammo/cells", .has_ammo_per_shot = true, .ammo_per_shot = 0,
      .model = "progs/v_hammer.mdl", .rank = 8, .model_for = hammer_model, .fire = fire_hammer,
      .animate = hammer_animate};
  qa_q1_register_weapon(game, &tables->mjolnir);
  tables->shotgun = (qa_q1_weapon_definition){
      .ctx = context, .id = "shotgun", .ammo = "q1:ammo/shells", .has_ammo_per_shot = true, .ammo_per_shot = 1,
      .model = "progs/v_shot.mdl", .rank = 8, .best_available = shotgun_best_available, .model_for = shotgun_model,
      .fire = fire_shotgun};
  qa_q1_replace_weapon(game, &tables->shotgun);
  tables->supershotgun = (qa_q1_weapon_definition){
      .ctx = context, .id = "supershotgun", .ammo = "q1:ammo/shells", .has_ammo_per_shot = true, .ammo_per_shot = 1,
      .model = "progs/v_shot2.mdl", .rank = 6, .best_available = supershotgun_best_available,
      .model_for = supershotgun_model, .fire = fire_shotgun};
  qa_q1_replace_weapon(game, &tables->supershotgun);
  /* The MG3 QC intentionally omits laser/rocket/grenade launchers from W_BestWeapon. */
  static const qa_q1_weapon ORDER[] = {"lightning", "supernailgun", "supershotgun", "nailgun",
                                       "shotgun", "mg3:mjolnir", "axe"};
  qa_q1_register_weapon_order(game, "q1:mg3", ORDER, sizeof ORDER / sizeof ORDER[0]);
}
