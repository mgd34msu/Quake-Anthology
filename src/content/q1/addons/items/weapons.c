/* Port of src/content/q1/addons/items/weapons.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/items/weapons.h"

#include <math.h>
#include <string.h>

#include "qa/content/q1/addons/items/pickups.h"

static double fr(double value) { return (double)(float)value; }
static bool infinite_ammo(qa_q1ad_context *context, qa_q1ad_player *player) {
  return qa_q1ad_player_number(context, player->actor.id, "infiniteammo") != 0;
}
static double inventory_count(qa_q1ad_context *context, qa_actor_id actor, qa_item_id item) {
  return context->game.inventory_count != NULL
      ? context->game.inventory_count(context->game.ctx, actor, item) : 0;
}
static bool inventory_consume(qa_q1ad_context *context, qa_owned_actor actor, qa_item_id item, double amount) {
  return context->game.inventory_consume != NULL &&
      context->game.inventory_consume(context->game.ctx, actor, item, amount);
}
static void emit_weapon(qa_q1ad_context *context, qa_q1ad_player *player, double punch) {
  qa_q1ad_host_event event = {.kind = "weapon", .player = player->actor.id, .has_player = true,
      .weapon = player->weapon,
      .view_model = context->game.weapon_model != NULL
          ? context->game.weapon_model(context->game.ctx, player->weapon, player) : "",
      .frame = player->weapon_frame, .punch = punch};
  qa_q1ad_host_emit(context, &event);
}
static qa_angle_vectors basis_of(qa_q1ad_context *context, qa_vec3 angles) {
  qa_angle_vectors basis;
  if (context->game.make_vectors != NULL &&
      context->game.make_vectors(context->game.ctx, angles, &basis)) return basis;
  return qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_Q1);
}
static double frame_delay(qa_q1ad_context *context, qa_q1ad_player *player, double delay) {
  return context->game.weapon_frame_delay != NULL
      ? context->game.weapon_frame_delay(context->game.ctx, player, delay) : delay;
}
static double attack_delay(qa_q1ad_context *context, qa_q1ad_player *player, double delay) {
  return context->game.weapon_attack_delay != NULL
      ? context->game.weapon_attack_delay(context->game.ctx, player, delay) : delay;
}
static qa_vec3 aim_at(qa_q1ad_context *context, qa_owned_actor actor, qa_vec3 forward) {
  return context->game.aim != NULL ? context->game.aim(context->game.ctx, actor, forward) : forward;
}

bool qa_q1ad_mg3_hammer_body_frame(qa_q1ad_context *context, qa_q1ad_player *player, double *out) {
  const char *weapon = player->weapon != NULL ? player->weapon : "";
  if (strcmp(weapon, "mg3:mjolnir") != 0 || player->weapon_animation_at < 0 ||
      player->weapon_frame < 1 || player->weapon_frame > 4) return false;
  *out = qa_q1ad_player_number(context, player->actor.id, "mg3.hammerBodyBase") + player->weapon_frame;
  return true;
}

void qa_q1ad_mg3_weapon_frame(qa_q1ad_context *context, qa_q1ad_player *player) {
  if (qa_q1ad_health(context, player->actor.id) <= 0) return;
  const char *weapon = player->weapon != NULL ? player->weapon : "";
  if (strcmp(weapon, "mg3:mjolnir") == 0 &&
      qa_q1ad_player_number(context, player->actor.id, "mg3.hammerGlow") != 0 &&
      qa_q1ad_player_number(context, player->actor.id, "last_mjolnir_hit_time") <= fr(context->game.time)) {
    qa_q1ad_set_player_number(context, player->actor.id, "mg3.hammerGlow", 0);
    emit_weapon(context, player, 0);
  }
  if (context->game.time <= player->attack_finished || strcmp(weapon, "axe") == 0 ||
      strcmp(weapon, "mg3:mjolnir") == 0) return;
  const char *ammo = context->game.weapon_ammo != NULL
      ? context->game.weapon_ammo(context->game.ctx, weapon) : NULL;
  if (ammo == NULL || inventory_count(context, player->actor.id, ammo) != 0) return;
  const char *name = strcmp(ammo, "q1:ammo/shells") == 0 ? "item_shells"
      : strcmp(ammo, "q1:ammo/nails") == 0 ? "item_spikes"
      : strcmp(ammo, "q1:ammo/rockets") == 0 ? "item_rockets"
      : strcmp(ammo, "q1:ammo/cells") == 0 ? "item_cells" : NULL;
  if (name != NULL) {
    qa_q1ad_actor *entities[512];
    size_t count = qa_q1ad_entities(context, entities, 512);
    for (size_t i = 0; i < count; i++)
      if (entities[i]->classname != NULL && strcmp(entities[i]->classname, name) == 0 &&
          ((int32_t)entities[i]->spawnflags & 8) != 0 && entities[i]->solid != NULL &&
          strcmp(entities[i]->solid, "none") == 0)
        qa_q1ad_schedule(context, entities[i], 0.5 * qa_q1ad_random(context), "SUB_regen");
  }
  if (context->game.choose_best != NULL && context->game.select_weapon != NULL) {
    const char *best = context->game.choose_best(context->game.ctx, player->actor);
    if (best != NULL) (void)context->game.select_weapon(context->game.ctx, player->actor, best);
  }
}

bool qa_q1ad_mg3_fire_laser(qa_q1ad_context *context, qa_q1ad_player *player) {
  qa_q1ad_body body;
  if (!qa_q1ad_body_by_id(context, player->actor.id, &body)) return false;
  if (!infinite_ammo(context, player) &&
      !inventory_consume(context, player->actor, "q1:ammo/cells", 1)) return false;
  qa_angle_vectors basis = basis_of(context, player->view_angles);
  qa_vec3 flat_forward = basis.forward;
  flat_forward.z = 0;
  qa_vec3 outward = qa_normalize3_or_zero(flat_forward);
  qa_vec3 origin = qa_add3(qa_add3(body.origin, qa_scale3(basis.up, 6)), qa_scale3(outward, 12));
  qa_vec3 direction = aim_at(context, player->actor, basis.forward);
  bool paired = !player->continuous_firing || player->weapon_frame == 4;
  if (paired) {
    double offset = 6 * 0.707;
    qa_vec3 first = qa_sub3(qa_add3(origin, qa_scale3(basis.right, offset)), qa_scale3(basis.up, offset));
    if (context->game.launch_laser != NULL) {
      context->game.launch_laser(context->game.ctx, player->actor.id, first, direction, false);
      context->game.launch_laser(context->game.ctx, player->actor.id,
          qa_sub3(first, qa_scale3(basis.right, offset * 2)), direction, false);
    }
  } else if (context->game.launch_laser != NULL)
    context->game.launch_laser(context->game.ctx, player->actor.id,
        qa_add3(origin, qa_scale3(basis.up, 6)), direction, qa_q1ad_random(context) < 0.1);
  player->continuous_firing = true;
  player->weapon_animation_at = -1;
  player->weapon_frame = paired ? 1 : 4;
  player->attack_finished = player->next_weapon_frame =
      fr(context->game.time + frame_delay(context, player, 0.1));
  player->hostile_until = fr(context->game.time + 1);
  emit_weapon(context, player, -1);
  qa_q1ad_effect(context, "muzzleflash", body.origin, &player->actor.id, 0);
  return true;
}

static void hammer_strike(qa_q1ad_context *context, qa_q1ad_actor *strike) {
  qa_q1ad_player *player = strike->has_owner ? qa_q1ad_player_state(context, strike->owner) : NULL;
  qa_q1ad_body body;
  bool has_body = player != NULL && qa_q1ad_body_by_id(context, player->actor.id, &body);
  if (player == NULL || !has_body) { qa_q1ad_remove(context, strike); return; }
  qa_angle_vectors basis = basis_of(context, player->view_angles);
  qa_vec3 source = qa_add3(body.origin, (qa_vec3){0, 0, 16});
  qa_actor_id self = player->actor.id;
  qa_q1ad_trace trace = {.fraction = 1, .end = qa_add3(source, qa_scale3(basis.forward, 64))};
  if (context->game.trace != NULL)
    (void)context->game.trace(context->game.ctx, source, trace.end,
        (qa_bounds){{0, 0, 0}, {0, 0, 0}}, &self, true, &trace);
  qa_vec3 origin = qa_sub3(trace.end, qa_scale3(basis.forward, 4));
  player->attack_finished = fr(context->game.time + attack_delay(context, player, 0.4));
  bool can_take = false;
  bool known = trace.has_actor && context->game.combat_can_take_damage != NULL &&
      context->game.combat_can_take_damage(context->game.ctx, trace.actor, &can_take);
  if (trace.has_actor && known && can_take) {
    qa_actor_id target = trace.actor;
    qa_q1ad_sound_actor(context, player->actor.id, "hipweap/mjolslap.wav", "weapon");
    qa_q1ad_effect(context, "blood", origin, &target, 40);
    qa_q1ad_actor *victim = qa_q1ad_entity(context, &target);
    if (victim != NULL) qa_q1ad_set_number(context, victim, "axhitme", 1);
    const char *classname = context->game.classname_of != NULL
        ? context->game.classname_of(context->game.ctx, target) : "";
    if (classname == NULL) classname = "";
    double health = qa_q1ad_health(context, target);
    double damage = strcmp(classname, "monster_zombie") == 0 ||
        strcmp(classname, "monster_szombie") == 0 ? 120 : health - 40 < 0 ? 80 : 40;
    qa_actor_id last;
    bool has_last = qa_q1ad_player_reference(context, player->actor.id, "last_mjolnir_hit", &last);
    if (qa_q1ad_player_number(context, player->actor.id, "last_mjolnir_hit_time") > fr(context->game.time)) {
      if (has_last && qa_same_actor(last, target) && player->water_level < 2 &&
          inventory_count(context, player->actor.id, "q1:ammo/cells") >= 15) {
        qa_q1ad_trace floor_trace = {.fraction = 1,
            .end = qa_sub3(source, qa_scale3(basis.up, player->water_level < 1 ? 30 : 15))};
        if (context->game.trace != NULL)
          (void)context->game.trace(context->game.ctx, source, floor_trace.end,
              (qa_bounds){{0, 0, 0}, {0, 0, 0}}, &self, true, &floor_trace);
        if (!infinite_ammo(context, player))
          (void)inventory_consume(context, player->actor, "q1:ammo/cells", 15);
        if (context->game.spawn_hammer_base != NULL)
          context->game.spawn_hammer_base(context->game.ctx, player, floor_trace.end, "mg3:mjolnir");
      } else
        qa_q1ad_damage(context, target, &self, &self, damage, "mg3:mjolnir", NULL, NULL);
      qa_q1ad_set_player_number(context, player->actor.id, "last_mjolnir_hit_time", context->game.time);
      player->attack_finished = fr(context->game.time + attack_delay(context, player, 0.5));
    } else {
      qa_q1ad_set_player_number(context, player->actor.id, "last_mjolnir_hit_time", context->game.time + 0.5);
      qa_q1ad_set_player_reference(context, player->actor.id, "last_mjolnir_hit", &target);
      player->attack_finished = fr(context->game.time + attack_delay(context, player, 0.2));
      if (inventory_count(context, player->actor.id, "q1:ammo/cells") >= 15)
        qa_q1ad_set_player_number(context, player->actor.id, "mg3.hammerGlow", 1);
      qa_q1ad_damage(context, target, &self, &self, damage, "mg3:mjolnir", NULL, NULL);
    }
  } else {
    if (trace.fraction != 1) {
      qa_q1ad_sound_actor(context, player->actor.id, "hipweap/mjoltink.wav", "weapon");
      qa_q1ad_effect(context, "gunshot", origin, NULL, 0);
    } else qa_q1ad_sound_actor(context, player->actor.id, "weapons/ax1.wav", "weapon");
    qa_q1ad_set_player_reference(context, player->actor.id, "last_mjolnir_hit", NULL);
  }
  emit_weapon(context, player, 0);
  qa_q1ad_remove(context, strike);
}

bool qa_q1ad_mg3_fire_hammer(qa_q1ad_context *context, qa_q1ad_player *player) {
  if (context->game.create == NULL) return false;
  qa_q1ad_actor *strike = context->game.create(context->game.ctx, "mg3_hammer_strike");
  if (strike == NULL) return false;
  strike->owner = player->actor.id;
  strike->has_owner = true;
  qa_q1ad_set_player_number(context, player->actor.id, "mg3.hammerBodyBase",
      inventory_count(context, player->actor.id, "q1:ammo/cells") < 30 ? 31 : 37);
  qa_q1ad_schedule(context, strike, 0.2, "mg3:hammer_strike");
  player->continuous_firing = false;
  player->weapon_animation_at = context->game.time;
  player->weapon_animation_base = 1;
  player->weapon_frame = 1;
  player->attack_finished = fr(context->game.time + attack_delay(context, player, 0.5));
  player->hostile_until = fr(context->game.time + 1);
  emit_weapon(context, player, 0);
  return true;
}

bool qa_q1ad_mg3_fire_shotgun(qa_q1ad_context *context, qa_q1ad_player *player) {
  double shells = inventory_count(context, player->actor.id, "q1:ammo/shells");
  int32_t bloody = (int32_t)qa_q1ad_player_number(context, player->actor.id, "parm15");
  const char *weapon = player->weapon != NULL ? player->weapon : "";
  if (strcmp(weapon, "supershotgun") == 0 && (bloody & QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN) != 0 && shells > 1) {
    qa_q1ad_body body;
    if (!qa_q1ad_body_by_id(context, player->actor.id, &body)) return false;
    if (!infinite_ammo(context, player)) (void)inventory_consume(context, player->actor, "q1:ammo/shells", 2);
    qa_q1ad_sound_actor(context, player->actor.id, "weapons/shotgn2.wav", "weapon");
    qa_angle_vectors basis = basis_of(context, player->view_angles);
    if (context->game.fire_bullets != NULL)
      context->game.fire_bullets(context->game.ctx, player->actor,
          aim_at(context, player->actor, basis.forward), player->view_angles, 28, 0.3, 0.08, "supershotgun");
    player->continuous_firing = false;
    player->weapon_animation_at = context->game.time;
    player->weapon_animation_base = 1;
    player->weapon_frame = 1;
    player->attack_finished = fr(context->game.time + attack_delay(context, player, 0.7));
    player->hostile_until = fr(context->game.time + 1);
    emit_weapon(context, player, -4);
    qa_q1ad_effect(context, "muzzleflash", body.origin, &player->actor.id, 0);
    return true;
  }
  bool fired = context->game.fire_base_weapon != NULL &&
      context->game.fire_base_weapon(context->game.ctx, player);
  if (fired && strcmp(weapon, "shotgun") == 0 && (bloody & QA_Q1AD_MG3_BLOODY_SHOTGUN) != 0)
    player->attack_finished = fr(context->game.time + attack_delay(context, player, 0.28));
  return fired;
}

bool qa_q1ad_mg3_consume_ammo(qa_q1ad_context *context, qa_q1ad_player *player, qa_item_id item, double amount) {
  return infinite_ammo(context, player) || inventory_consume(context, player->actor, item, amount);
}
const char *qa_q1ad_mg3_hammer_model(qa_q1ad_context *context, qa_q1ad_player *player) {
  return qa_q1ad_player_number(context, player->actor.id, "mg3.hammerGlow") != 0 &&
          qa_q1ad_player_number(context, player->actor.id, "last_mjolnir_hit_time") > fr(context->game.time)
      ? "progs/v_hammer_glow.mdl" : "progs/v_hammer.mdl";
}
const char *qa_q1ad_mg3_shotgun_model(qa_q1ad_context *context, qa_q1ad_player *player, const char *weapon) {
  bool shotgun = strcmp(weapon, "shotgun") == 0;
  int32_t bloody = (int32_t)qa_q1ad_player_number(context, player->actor.id, "parm15");
  bool active = (bloody & (shotgun ? QA_Q1AD_MG3_BLOODY_SHOTGUN : QA_Q1AD_MG3_BLOODY_SUPER_SHOTGUN)) != 0;
  return active ? shotgun ? "progs/v_bloodshot.mdl" : "progs/v_bloodshot2.mdl"
                : shotgun ? "progs/v_shot.mdl" : "progs/v_shot2.mdl";
}
bool qa_q1ad_mg3_shotgun_best_available(qa_q1ad_context *context, qa_q1ad_player *player, const char *weapon) {
  return strcmp(weapon, "shotgun") == 0 ||
      inventory_count(context, player->actor.id, "q1:ammo/shells") >= 2;
}
void qa_q1ad_mg3_hammer_animate(qa_q1ad_context *context, qa_q1ad_player *player, double seconds) {
  if (player->weapon_animation_at < 0) return;
  double step = floor((seconds - player->weapon_animation_at) / 0.1);
  double frame = step >= 4 ? 0 : step + 1;
  if (frame != player->weapon_frame) {
    player->weapon_frame = frame;
    emit_weapon(context, player, 0);
  }
  if (step >= 4) player->weapon_animation_at = -1;
}

void qa_q1ad_register_mg3_weapons(qa_q1ad_context *context) {
  if (context->game.register_rule != NULL) {
    context->game.register_rule(context->game.ctx, "q1:mg3:ammunition");
    context->game.register_rule(context->game.ctx, "hipnotic:laser-callbacks");
    context->game.register_rule(context->game.ctx, "hipnotic:hammer-callbacks");
    context->game.register_rule(context->game.ctx, "weapon:mg3:laser");
    context->game.register_rule(context->game.ctx, "weapon:mg3:mjolnir");
    context->game.register_rule(context->game.ctx, "weapon:shotgun");
    context->game.register_rule(context->game.ctx, "weapon:supershotgun");
    /* The MG3 QC intentionally omits laser/rocket/grenade launchers from W_BestWeapon. */
    context->game.register_rule(context->game.ctx, "q1:mg3:weapon-order");
  }
  qa_q1ad_register_named(context, "mg3:hammer_strike", (qa_q1ad_handlers){.action = hammer_strike});
}
