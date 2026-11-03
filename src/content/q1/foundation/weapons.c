/*
 * Port of src/content/q1/foundation/weapons.ts. Bodies, combat
 * (canTakeDamage, team) and liveness are the shared world tables of
 * game->host. GPL-2.0-or-later.
 */
#include "qa/content/q1/foundation/weapons.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/world/gameplay/q1_aim.h"

const char *qa_q1_weapon_model(qa_q1_weapon weapon) {
  if (!qa_q1_is_base_weapon(weapon))
    qa_range_error("Use registered Q1 weapon model for %s", weapon);
  if (strcmp(weapon, "axe") == 0) return "progs/v_axe.mdl";
  if (strcmp(weapon, "shotgun") == 0) return "progs/v_shot.mdl";
  if (strcmp(weapon, "supershotgun") == 0) return "progs/v_shot2.mdl";
  if (strcmp(weapon, "nailgun") == 0) return "progs/v_nail.mdl";
  if (strcmp(weapon, "supernailgun") == 0) return "progs/v_nail2.mdl";
  if (strcmp(weapon, "grenadelauncher") == 0) return "progs/v_rock.mdl";
  if (strcmp(weapon, "rocketlauncher") == 0) return "progs/v_rock2.mdl";
  return "progs/v_light.mdl"; /* lightning; TS switch is exhaustive */
}
qa_q1_weapon qa_q1_best_weapon(qa_q1_services *game, qa_owned_actor actor) {
  return qa_q1_best_weapon_counted(game, actor, NULL, NULL);
}
qa_q1_weapon qa_q1_best_weapon_counted(qa_q1_services *game, qa_owned_actor actor,
                                       qa_q1_ammo_count_fn ammo_count, void *ammo_count_ctx) {
  static const qa_q1_weapon fallback_order[] = {"lightning", "supernailgun",
      "supershotgun", "nailgun", "shotgun", "axe"};
  /* TS: game.weaponOrder ?? fallback. The mirror order is present
   * exactly when an owner registered it. */
  const qa_q1_weapon *order = fallback_order;
  size_t count = 6;
  if (game->weapon_order_owner != NULL) {
    order = game->weapon_order;
    count = game->weapon_order_count;
  }
  qa_q1_player_state *player = qa_q1_player(game, actor.id);
  if (player == NULL) return "axe";
  for (size_t i = 0; i < count; i++)
    if (qa_q1_weapon_available_counted(game, player, order[i], "best", ammo_count, ammo_count_ctx)) return order[i];
  return "axe";
}

typedef struct aim_ctx {
  qa_q1_services *game;
  qa_owned_actor actor;
  const char *team; /* TS combat.read(actor)?.team ?? null */
} aim_ctx;
static qa_body_state_result aim_body(void *ctx, qa_actor_id actor) {
  aim_ctx *aim = ctx;
  return qa_shared_body_table_read(aim->game->host.bodies, actor);
}
static bool aim_eligible(void *ctx, qa_actor_id target) {
  aim_ctx *aim = ctx;
  qa_q1_source_target traits = qa_q1_get_source_target(aim->game, target);
  const qa_combat_state_result state = qa_gameplay_authority_read(aim->game->host.combat, target);
  const double teamplay = qa_q1_option_teamplay(aim->game);
  return !qa_same_actor(target, aim->actor.id) && (traits.aimed_damage || traits.player) &&
         state.has_value && state.value.can_take_damage &&
         !(teamplay != 0 && aim->team != NULL && state.value.team != NULL && !strcmp(aim->team, state.value.team));
}
static bool aim_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_actor_id *out) {
  aim_ctx *aim = ctx;
  qa_q1_trace_request request;
  memset(&request, 0, sizeof request);
  request.start = start;
  request.end = end;
  request.bounds = QA_Q1_POINT;
  request.has_ignore = true;
  request.ignore = aim->actor.id;
  request.monsters = true;
  qa_q1_trace trace = qa_q1_host_trace(aim->game, request);
  if (!trace.has_actor) return false;
  *out = trace.actor;
  return true;
}
qa_vec3 qa_q1_aim(qa_q1_services *game, qa_owned_actor actor, qa_vec3 forward) {
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor.id);
  if (!body.has_value) return forward;
  const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, actor.id);
  size_t count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(game->host.actors, &count);
  qa_actor_id *targets = malloc((count != 0 ? count : 1) * sizeof *targets);
  if (targets == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) targets[i] = observations[i].id;
  aim_ctx aim = {game, actor, combat.has_value ? combat.value.team : NULL};
  qa_q1_aim_targets host = {targets, count, &aim, aim_body, aim_eligible,
                            aim_trace};
  double threshold =
      game->options.has_aim_threshold ? game->options.aim_threshold : 0.93;
  qa_vec3 result = qa_aim_q1(body.value.origin, forward, threshold, host);
  free(targets);
  return result;
}
static bool can_take_damage(qa_q1_services *game, qa_actor_id actor);
void qa_q1_fire_bullets(qa_q1_services *game, qa_owned_actor shooter,
                        qa_vec3 direction, qa_vec3 view_angles, double count,
                        double spread_x, double spread_y, qa_q1_weapon weapon) {
  const qa_body_state_result read = qa_shared_body_table_read(game->host.bodies, shooter.id);
  if (!read.has_value) return;
  const qa_body_state body = read.value;
  qa_q1_basis basis = qa_q1_make_vectors(game, view_angles);
  qa_vec3 source = qa_q1_vadd(body.origin, qa_q1_vscale(basis.forward, 10));
  /* absmin carries SV_LinkEdict's one unit of padding. */
  source.z = (float)(body.origin.z + body.bounds.min.z - 1 +
                     (body.bounds.max.z - body.bounds.min.z) * 0.7);
  bool has_pending = false, holed = false;
  qa_actor_id pending;
  memset(&pending, 0, sizeof pending);
  double total = 0;
  for (double shot = 0; shot < count; shot++) {
    qa_vec3 a = qa_q1_vadd(direction,
                   qa_q1_vscale(game->basis.right,
                                (qa_q1_host_random(game) * 2 - 1) * spread_x));
    qa_vec3 b = qa_q1_vscale(game->basis.up,
                     (qa_q1_host_random(game) * 2 - 1) * spread_y);
    qa_vec3 ray = qa_q1_vadd(
        a, b);
    qa_q1_trace_request request;
    memset(&request, 0, sizeof request);
    request.start = source;
    request.end = qa_q1_vadd(source, qa_q1_vscale(ray, 2048));
    request.bounds = QA_Q1_POINT;
    request.has_ignore = true;
    request.ignore = shooter.id;
    request.monsters = true;
    qa_q1_trace trace = qa_q1_host_trace(game, request);
    if (trace.fraction == 1) continue;
    qa_q1_actor *hit =
        trace.has_actor ? qa_q1_get_entity(game, &trace.actor) : NULL;
    if (!trace.sky &&
        (!trace.has_actor || hit == NULL ||
         hit->solid == NULL || strcmp(hit->solid, "trigger") != 0) &&
        game->host.weapon_impact != NULL)
      game->host.weapon_impact(game->host.ctx, shooter.id, trace.end);
    qa_vec3 impact = qa_q1_vsub(trace.end, qa_q1_vscale(ray, 4));
    if (trace.has_actor && can_take_damage(game, trace.actor)) {
      qa_q1_effect(game, "blood", impact, &trace.actor, 4);
      if (!has_pending || !qa_same_actor(pending, trace.actor)) {
        if (has_pending && qa_session_actor_registry_is_live(game->host.actors, pending))
          qa_q1_damage(game, pending, shooter.id, &shooter.id, total, weapon,
                       "direct", "", NULL);
        total = 0;
        has_pending = true;
        pending = trace.actor;
      }
      total += 4;
    } else {
      qa_q1_effect(game, "gunshot", impact,
                   trace.has_actor ? &trace.actor : NULL, 1);
    }
    if (!holed && game->source_bullet_hole.fn != NULL &&
        (!trace.has_actor || (game->world != NULL && qa_same_actor(trace.actor, game->world->actor.id)))) {
      holed = true;
      game->source_bullet_hole.fn(game->source_bullet_hole.ctx, shooter, trace.end, trace.normal);
    }
  }
  if (has_pending && qa_session_actor_registry_is_live(game->host.actors, pending))
    qa_q1_damage(game, pending, shooter.id, &shooter.id, total, weapon,
                 "direct", "", NULL);
}

static double fr(double value) { return (float)value; }
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
/* TS game.host.bodies.read(actor); false = null. */
static bool body_of(qa_q1_services *game, qa_actor_id actor, qa_q1_body *out) {
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
  if (!body.has_value) return false;
  *out = qa_q1_body_from_state(body.value);
  return true;
}
/* TS game.host.combat.read(actor)?.canTakeDamage. */
static bool can_take_damage(qa_q1_services *game, qa_actor_id actor) {
  const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, actor);
  return combat.has_value && combat.value.can_take_damage;
}
static const char *classname_of(qa_q1_services *game, qa_actor_id actor) {
  if (game->host.classname) {
    const char *name = game->host.classname(game->host.ctx, actor);
    if (name) return name;
  }
  qa_q1_actor *entity = qa_q1_get_entity(game, &actor);
  return entity ? entity->classname : NULL;
}
static void weapon_impact_at(qa_q1_services *game, qa_actor_id owner, qa_vec3 origin) {
  if (game->host.weapon_impact) game->host.weapon_impact(game->host.ctx, owner, origin);
}

static qa_q1_actor *projectile(qa_q1_services *game, qa_q1_player_state *player, const char *kind,
                               qa_vec3 velocity, qa_vec3 origin) {
  qa_q1_actor *entity = qa_q1_create(game, !strcmp(kind, "rocket") ? "missile" : kind, NULL, NULL);
  if (entity == NULL) return NULL;
  entity->projectile = kind;
  entity->projectile_weapon = player->weapon;
  entity->has_owner = true;
  entity->owner = player->actor.id;
  entity->movement = !strcmp(kind, "grenade") ? "bounce" : "flymissile";
  entity->solid = "bbox";
  set_string(&entity->model, !strcmp(kind, "rocket") ? "progs/missile.mdl"
                             : !strcmp(kind, "grenade") ? "progs/grenade.mdl"
                             : !strcmp(kind, "superspike") ? "progs/s_spike.mdl"
                                                           : "progs/spike.mdl");
  if (!strcmp(kind, "grenade")) entity->angular_velocity = v3(300, 300, 300);
  /* weapons.qc orients rockets and spikes along their velocity
   * (vectoangles); unified body angles use view convention. */
  const qa_vec3 angles =
      v3(-atan2(velocity.z, qa_js_hypot2(velocity.x, velocity.y)) * 180 / 3.141592653589793,
         qa_q1_yaw_for(velocity), 0);
  qa_q1_body patch = qa_q1_get_body(game, entity);
  patch.origin = origin;
  patch.velocity = velocity;
  patch.angles = angles;
  qa_q1_set_body_patch(game, entity, &patch, true, true, true, false, false);
  qa_q1_link(game, entity);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "projectile_touch");
  if (!strcmp(kind, "grenade") && game->source_grenade_fix)
    qa_q1_schedule(game, entity, 0.05, qa_q1_callback_registry_action(game->named, entity, "GrenadeThink"));
  else
    qa_q1_schedule(game, entity, !strcmp(kind, "grenade") ? 2.5 : !strcmp(kind, "rocket") ? 5 : 6,
                   qa_q1_callback_registry_action(game->named, entity,
                                                    !strcmp(kind, "grenade") ? "GrenadeExplode" : "SUB_Remove"));
  qa_q1_launch_projectile_behavior(game, entity, player->actor.id, player->weapon,
                                   !strcmp(kind, "rocket") ? "rocket"
                                   : !strcmp(kind, "grenade") ? "grenade"
                                                              : "nail");
  return entity;
}

static void explode(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *direct, bool rocket) {
  const qa_vec3 impact_origin = qa_q1_get_body(game, entity).origin;
  if (entity->has_owner) weapon_impact_at(game, entity->owner, impact_origin);
  if (rocket && direct != NULL && qa_q1_health(game, *direct) != 0) {
    double amount = fr(100 + qa_q1_host_random(game) * 20);
    const char *classname = classname_of(game, *direct);
    if (classname != NULL && (!strcmp(classname, "monster_shambler") ||
                              (game->source_bloody_nightmare.fn != NULL && !strcmp(classname, "monster_super_shambler"))))
      amount *= 0.5;
    (void)qa_q1_damage(game, *direct, entity->actor.id, entity->has_owner ? &entity->owner : NULL,
                       amount, entity->projectile_weapon, "direct", "", NULL);
  }
  qa_q1_radius_damage(game, entity->actor.id, entity->has_owner ? &entity->owner : NULL, 120, direct,
                      entity->projectile_weapon, "");
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 origin = rocket ? qa_q1_vsub(body.origin, qa_q1_vscale(qa_q1_normalize(body.velocity), 8))
                                : body.origin;
  qa_q1_effect(game, "explosion", origin, NULL, 1);
  qa_q1_remove(game, entity);
}

void qa_q1_projectile_touch(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                            qa_vec3 normal, const qa_touch_surface *surface) {
  (void)normal;
  if (other != NULL && entity->has_owner && qa_same_actor(*other, entity->owner)) return;
  if (other != NULL) {
    qa_q1_actor *other_entity = qa_q1_get_entity(game, other);
    if (other_entity != NULL && other_entity->solid != NULL && !strcmp(other_entity->solid, "trigger"))
      return;
  }
  const bool is_grenade = entity->projectile != NULL && !strcmp(entity->projectile, "grenade");
  if (!is_grenade &&
      ((surface != NULL && (qa_js_to_int32(surface->native_flags) & 4) != 0) ||
       !strcmp(game->host.contents(game->host.ctx, qa_q1_get_body(game, entity).origin), "sky"))) {
    qa_q1_remove(game, entity);
    return;
  }
  if (entity->projectile == NULL) return;
  if (!strcmp(entity->projectile, "rocket")) {
    explode(game, entity, other, true);
  } else if (is_grenade) {
    if (other != NULL && game->source_grenade_fix && qa_q1_get_source_target(game, *other).aimed_damage) {
      const char *classname = classname_of(game, *other);
      if (classname != NULL && (!strcmp(classname, "monster_boss") || !strcmp(classname, "monster_oldone_new"))) {
        (void)qa_q1_damage(game, *other, entity->actor.id, entity->has_owner ? &entity->owner : NULL, 120,
                           entity->projectile_weapon, "direct", "", NULL);
        explode(game, entity, other, false);
        return;
      }
    }
    if (other != NULL) {
      const qa_q1_source_target traits = qa_q1_get_source_target(game, *other);
      if (traits.aimed_damage || traits.player) {
        explode(game, entity, NULL, false);
        return;
      }
    }
    if (game->source_grenade_fix) {
      if (entity->attack_finished >= game->time) return;
      entity->attack_finished = game->time + 0.1;
    }
    qa_q1_sound(game, entity->actor.id, "weapons/bounce.wav", "weapon", 1, 1);
  } else if (!strcmp(entity->projectile, "spike") || !strcmp(entity->projectile, "superspike")) {
    const qa_vec3 origin = qa_q1_get_body(game, entity).origin;
    if (entity->has_owner) weapon_impact_at(game, entity->owner, origin);
    const double amount = !strcmp(entity->projectile, "spike") ? 9 : 18;
    if (other != NULL && can_take_damage(game, *other)) {
      qa_q1_effect(game, "blood", origin, other, amount);
      (void)qa_q1_damage(game, *other, entity->actor.id, entity->has_owner ? &entity->owner : NULL,
                         amount, entity->projectile_weapon, "direct", "", NULL);
    } else {
      qa_q1_effect(game,
                   !strcmp(entity->classname, "wizard_spike") ? "wizard-spike"
                   : !strcmp(entity->classname, "knight_spike") ? "knight-spike"
                                                                 : entity->projectile,
                   origin, NULL, 1);
    }
    qa_q1_remove(game, entity);
  }
}

static void lightning(qa_q1_services *game, qa_q1_player_state *player) {
  qa_q1_body body;
  if (!body_of(game, player->actor.id, &body)) return;
  const double cells = qa_shared_inventory_table_count(game->host.inventory, player->actor.id, "q1:ammo/cells");
  if (player->water_level > 1) {
    weapon_impact_at(game, player->actor.id, body.origin);
    (void)qa_q1_consume_weapon_ammo(game, player, "q1:ammo/cells", cells);
    qa_q1_radius_damage(game, player->actor.id, &player->actor.id, 35 * cells, NULL, "lightning", "discharge");
    return;
  }
  (void)qa_q1_consume_weapon_ammo(game, player, "q1:ammo/cells", 1);
  const qa_vec3 forward = qa_q1_make_vectors(game, player->view_angles).forward;
  const qa_vec3 start = qa_q1_vadd(body.origin, v3(0, 0, 16));
  qa_q1_trace_request wall_request = {start, qa_q1_vadd(start, qa_q1_vscale(forward, 600)),
                                      QA_Q1_POINT, true, player->actor.id, false, false, false};
  const qa_q1_trace wall = qa_q1_host_trace(game, wall_request);
  if (wall.fraction < 1 && !wall.sky) weapon_impact_at(game, player->actor.id, wall.end);
  qa_q1_event beam = {0};
  beam.kind = "beam";
  beam.beam_style = "lightning2";
  beam.has_actor = true;
  beam.actor = player->actor.id;
  beam.start = start;
  beam.end = wall.end;
  qa_q1_emit(game, beam);
  const qa_vec3 end = qa_q1_vadd(wall.end, qa_q1_vscale(forward, 4));
  const qa_vec3 delta = qa_q1_vsub(end, body.origin);
  const qa_vec3 side = v3(-delta.y * 16, -delta.y * 16, 0);
  const qa_vec3 offsets[3] = {QA_Q1_ZERO, side, qa_q1_vscale(side, -1)};
  qa_actor_id hit[3];
  size_t hit_count = 0;
  for (size_t i = 0; i < 3; i++) {
    qa_q1_trace_request request = {qa_q1_vadd(body.origin, offsets[i]), qa_q1_vadd(end, offsets[i]),
                                   QA_Q1_POINT, true, player->actor.id, true, false, false};
    const qa_q1_trace trace = qa_q1_host_trace(game, request);
    if (!trace.has_actor) continue;
    bool seen = false;
    for (size_t j = 0; j < hit_count; j++)
      if (qa_same_actor(hit[j], trace.actor)) seen = true;
    if (seen || !can_take_damage(game, trace.actor)) continue;
    qa_q1_actor *hit_entity = qa_q1_get_entity(game, &trace.actor);
    if (!trace.sky && (hit_entity == NULL || hit_entity->solid == NULL ||
                       strcmp(hit_entity->solid, "trigger") != 0))
      weapon_impact_at(game, player->actor.id, trace.end);
    hit[hit_count++] = trace.actor;
    qa_q1_event particles = {0};
    particles.kind = "particles";
    particles.has_origin = true;
    particles.origin = trace.end;
    particles.direction = v3(0, 0, 100);
    particles.color = game->mg3_pickups ? 246 : 225;
    particles.count = 120;
    qa_q1_emit(game, particles);
    (void)qa_q1_damage(game, trace.actor, player->actor.id, &player->actor.id, 30, "lightning", "direct", "", NULL);
  }
}

static bool registered_weapon(qa_q1_services *game, qa_q1_weapon weapon) {
  for (size_t i = 0; i < game->registered_weapon_count; i++)
    if (game->registered_weapons[i].def != NULL && game->registered_weapons[i].def->id != NULL &&
        !strcmp(game->registered_weapons[i].def->id, weapon))
      return true;
  return false;
}

bool qa_q1_fire_weapon(qa_q1_services *game, qa_q1_player_state *player) {
  if (player->primary_holstered) return false;
  if (registered_weapon(game, player->weapon) || !qa_q1_is_base_weapon(player->weapon))
    return qa_q1_fire_registered_weapon(game, player);
  return qa_q1_fire_base_weapon(game, player);
}

bool qa_q1_fire_base_weapon(qa_q1_services *game, qa_q1_player_state *player) {
  if (player->primary_holstered) return false;
  if (!qa_q1_is_base_weapon(player->weapon)) qa_range_error("Base Q1 attack requires a base weapon");
  const bool repeating = player->continuous_firing;
  if (qa_q1_health(game, player->actor.id) <= 0 ||
      game->time < (repeating ? player->next_weapon_frame : player->attack_finished))
    return false;
  const qa_item_id ammo = qa_q1_ammo_item(player->weapon);
  if (ammo != NULL && qa_shared_inventory_table_count(game->host.inventory, player->actor.id, ammo) < 1) {
    (void)qa_q1_select_weapon(game, player->actor, qa_q1_best_weapon(game, player->actor));
    return false;
  }
  qa_q1_body body;
  if (!body_of(game, player->actor.id, &body)) return false;
  const double volume = game->host.weapon_volume ? game->host.weapon_volume(game->host.ctx, player->actor.id) : 1;
  if (!registered_weapon(game, player->weapon)) qa_q1_weapon_before_fire(game, player);
  const qa_q1_basis basis = qa_q1_make_vectors(game, player->view_angles);
  const qa_q1_weapon weapon = player->weapon;
  double delay = 0.1, punch = -2;
  qa_q1_character_attack attack = {0};
  attack.has = true;
  attack.kind = !strcmp(weapon, "shotgun") || !strcmp(weapon, "supershotgun") ? "shotgun"
                : !strcmp(weapon, "lightning") ? "lightning"
                : !strcmp(weapon, "nailgun") || !strcmp(weapon, "supernailgun") ? "nail"
                                                                                   : "rocket";
  player->continuous_firing = !strcmp(weapon, "nailgun") || !strcmp(weapon, "supernailgun") ||
                              !strcmp(weapon, "lightning");
  player->next_weapon_frame = fr(game->time + qa_q1_weapon_frame_delay(game, player, 0.1));
  if (!player->continuous_firing) {
    player->weapon_animation_at = game->time;
    player->weapon_animation_base = 1;
  }
  player->hostile_until = game->time + 1;
  if (!strcmp(weapon, "axe")) {
    delay = 0.5;
    punch = 0;
    qa_q1_sound(game, player->actor.id, "weapons/ax1.wav", "weapon", 1, volume);
    const double animation = qa_q1_host_random(game);
    attack.kind = "axe";
    attack.variant = animation < 0.25 ? 0 : animation < 0.5 ? 1 : animation < 0.75 ? 2 : 3;
    player->weapon_animation_base = (animation >= 0.25 && animation < 0.5) || animation >= 0.75 ? 5 : 1;
    qa_q1_actor *strike = qa_q1_create(game, "axe_strike", NULL, NULL);
    if (strike != NULL) {
      strike->has_owner = true;
      strike->owner = player->actor.id;
      qa_q1_schedule(game, strike, 0.2, qa_q1_callback_registry_action(game->named, strike, "player_axe3"));
    }
  } else if (!strcmp(weapon, "shotgun") || !strcmp(weapon, "supershotgun")) {
    const bool super_shot = !strcmp(weapon, "supershotgun") &&
                            qa_shared_inventory_table_count(game->host.inventory, player->actor.id, "q1:ammo/shells") > 1;
    (void)qa_q1_consume_weapon_ammo(game, player, "q1:ammo/shells", super_shot ? 2 : 1);
    delay = !strcmp(weapon, "supershotgun") ? 0.7 : 0.5;
    punch = super_shot ? -4 : -2;
    qa_q1_sound(game, player->actor.id, super_shot ? "weapons/shotgn2.wav" : "weapons/guncock.wav",
                "weapon", 1, volume);
    qa_q1_fire_bullets(game, player->actor, qa_q1_aim(game, player->actor, basis.forward),
                       player->view_angles, super_shot ? 14 : 6, super_shot ? 0.14 : 0.04,
                       super_shot ? 0.08 : 0.04, weapon);
  } else if (!strcmp(weapon, "nailgun") || !strcmp(weapon, "supernailgun")) {
    qa_vec3 a3;
    double s;
    const bool super_nail = !strcmp(weapon, "supernailgun") &&
                            qa_shared_inventory_table_count(game->host.inventory, player->actor.id, "q1:ammo/nails") >= 2;
    delay = 0.2;
    (void)qa_q1_consume_weapon_ammo(game, player, "q1:ammo/nails", super_nail ? 2 : 1);
    qa_q1_sound(game, player->actor.id, super_nail ? "weapons/spike2.wav" : "weapons/rocket1i.wav",
                "weapon", 1, volume);
    const qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(body.origin, v3(0, 0, 16)),
                                      qa_q1_vscale(basis.right, super_nail ? 0 : player->nail_side * 4));
    (void)projectile(game, player, super_nail ? "superspike" : "spike",
                     (a3 = qa_q1_aim(game, player->actor, basis.forward), s = qa_q1_nail_speed(game, player, 1000), qa_q1_vscale(a3,
                                  s)),
                     origin);
    player->nail_side *= -1;
  } else if (!strcmp(weapon, "grenadelauncher")) {
    (void)qa_q1_consume_weapon_ammo(game, player, "q1:ammo/rockets", 1);
    delay = 0.6;
    qa_q1_sound(game, player->actor.id, "weapons/grenade.wav", "weapon", 1, volume);
    qa_vec3 velocity;
    if (player->view_angles.x == 0) {
      velocity = qa_q1_vscale(qa_q1_aim(game, player->actor, basis.forward), 600);
      velocity.z = 200;
    } else {
      qa_vec3 a2 = qa_q1_vadd(qa_q1_vadd(qa_q1_vscale(basis.forward, 600), qa_q1_vscale(basis.up, 200)),
                     qa_q1_vscale(basis.right, (qa_q1_host_random(game) * 2 - 1) * 10));
      qa_vec3 b = qa_q1_vscale(basis.up, (qa_q1_host_random(game) * 2 - 1) * 10);
      velocity = qa_q1_vadd(
          a2, b);
    }
    (void)projectile(game, player, "grenade", velocity, body.origin);
  } else if (!strcmp(weapon, "rocketlauncher")) {
    (void)qa_q1_consume_weapon_ammo(game, player, "q1:ammo/rockets", 1);
    delay = 0.8;
    qa_q1_sound(game, player->actor.id, "weapons/sgun1.wav", "weapon", 1, volume);
    (void)projectile(game, player, "rocket",
                     qa_q1_vscale(qa_q1_aim(game, player->actor, basis.forward), 1000),
                     qa_q1_vadd(qa_q1_vadd(body.origin, qa_q1_vscale(basis.forward, 8)), v3(0, 0, 16)));
  } else { /* lightning */
    delay = repeating ? 0.2 : 0.1;
    if (player->lightning_sound_at < game->time) {
      qa_q1_sound(game, player->actor.id, "weapons/lhit.wav", "weapon", 1, volume);
      player->lightning_sound_at = game->time + 0.6;
    }
    lightning(game, player);
    if (!repeating) qa_q1_sound(game, player->actor.id, "weapons/lstart.wav", "auto", 1, volume);
  }
  /* SuperDamageSound, limited by super_sound to once a second. */
  if (strcmp(weapon, "axe") != 0 && qa_q1_powerup_expires(game, player->actor.id, "quad") > game->time &&
      qa_q1_player_timer_get(game->quad_sounds, game->quad_sound_count, player) < game->time) {
    qa_q1_player_timer_set(&game->quad_sounds, &game->quad_sound_count, &game->quad_sound_capacity, player,
                           game->time + 1);
    qa_q1_sound(game, player->actor.id, "items/damage3.wav", "body", 1, 1);
  }
  player->attack_finished = fr(game->time + qa_q1_weapon_attack_delay(game, player, delay));
  player->weapon_frame = player->continuous_firing
                             ? fmod(player->weapon_frame, !strcmp(weapon, "lightning") ? 4 : 8) + 1
                             : player->weapon_animation_base;
  qa_q1_weapon_punch(game, player, punch);
  qa_q1_event event = {0};
  event.kind = "weapon";
  event.has_player = true;
  event.player = player->actor.id;
  event.weapon = weapon;
  event.view_model = qa_q1_services_weapon_model(game, weapon, player);
  event.frame = player->weapon_frame;
  event.punch = punch;
  event.attack = attack;
  qa_q1_emit(game, event);
  qa_q1_effect(game, "muzzleflash", body.origin, &player->actor.id, 1);
  return true;
}

static void axe_strike(void *ctx, qa_q1_services *game, qa_q1_actor *strike) {
  (void)ctx;
  qa_q1_player_state *player = strike->has_owner ? qa_q1_player(game, strike->owner) : NULL;
  if (player == NULL) {
    qa_q1_remove(game, strike);
    return;
  }
  /* In QC the hit runs in a player frame that death and intermission replace. */
  if (qa_q1_health(game, player->actor.id) <= 0 || game->has_intermission) {
    qa_q1_remove(game, strike);
    return;
  }
  qa_q1_body current;
  if (!body_of(game, player->actor.id, &current)) {
    qa_q1_remove(game, strike);
    return;
  }
  const qa_vec3 start = qa_q1_vadd(current.origin, v3(0, 0, 16));
  const qa_vec3 forward = qa_q1_make_vectors(game, player->view_angles).forward;
  qa_q1_trace_request request = {start, qa_q1_vadd(start, qa_q1_vscale(forward, 64)),
                                 QA_Q1_POINT, true, player->actor.id, true, false, false};
  const qa_q1_trace trace = qa_q1_host_trace(game, request);
  if (trace.fraction < 1) {
    qa_q1_actor *hit_entity = trace.has_actor ? qa_q1_get_entity(game, &trace.actor) : NULL;
    if (!trace.sky && (!trace.has_actor || hit_entity == NULL || hit_entity->solid == NULL ||
                       strcmp(hit_entity->solid, "trigger") != 0))
      weapon_impact_at(game, player->actor.id, trace.end);
    const qa_vec3 org = qa_q1_vsub(trace.end, qa_q1_vscale(forward, 4));
    if (trace.has_actor && can_take_damage(game, trace.actor)) {
      qa_q1_effect(game, "blood", org, &trace.actor, 20);
      (void)qa_q1_damage(game, trace.actor, player->actor.id, &player->actor.id, 20, "axe", "direct", "", NULL);
    } else {
      qa_q1_sound(game, player->actor.id, "player/axhit2.wav", "weapon", 1, 1);
      qa_q1_effect(game, "gunshot", org, NULL, 3);
    }
  }
  qa_q1_remove(game, strike);
}

/* MG GrenadeThink: counts 50 ticks to the explosion and reflects a grenade
 * that stopped against a floor it was falling onto. */
static void grenade_think(void *ctx, qa_q1_services *game, qa_q1_actor *grenade) {
  (void)ctx;
  grenade->count++;
  if (grenade->count == 50) {
    explode(game, grenade, NULL, false);
    return;
  }
  const qa_vec3 previous = qa_q1_actor_vector(grenade, "oldorigin");
  const qa_q1_body body = qa_q1_get_body(game, grenade);
  if (qa_q1_length(body.velocity) == 0 && qa_q1_length(previous) > 120 && previous.z < 0) {
    qa_q1_trace_request request = {qa_q1_vadd(body.origin, v3(0, 0, 4)), qa_q1_vsub(body.origin, v3(0, 0, 8)),
                                   QA_Q1_POINT, true, grenade->actor.id, false, false, false};
    const qa_q1_trace trace = qa_q1_host_trace(game, request);
    if (trace.fraction < 1) {
      const qa_vec3 reflected =
          qa_q1_vsub(previous, qa_q1_vscale(trace.normal, 2 * qa_q1_dot(previous, trace.normal)));
      qa_q1_body_patch patch;
      memset(&patch, 0, sizeof patch);
      patch.has_velocity = true;
      patch.velocity = qa_q1_vscale(reflected, 0.6);
      patch.has_ground = true;
      patch.ground = NULL;
      qa_q1_set_body(game, grenade, &patch);
      grenade->movement_flags = (double)(qa_js_to_int32(grenade->movement_flags) & ~512);
      grenade->angular_velocity = v3(400, 400, 400);
    }
  }
  const qa_vec3 velocity = qa_q1_get_body(game, grenade).velocity;
  char text[128];
  (void)snprintf(text, sizeof text, "%s %s %s", QA_JS_NUMBER(velocity.x), QA_JS_NUMBER(velocity.y),
                 QA_JS_NUMBER(velocity.z));
  (void)qa_q1_actor_field_set(grenade, "oldorigin", text);
  qa_q1_schedule(game, grenade, 0.05, qa_q1_callback_registry_action(game->named, grenade, "GrenadeThink"));
}

static void touch_projectile(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                             const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx;
  qa_q1_projectile_touch(game, entity, &other, normal ? *normal : QA_Q1_ZERO, surface);
}
static void grenade_explode(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  explode(game, entity, NULL, false);
}

void qa_q1_register_weapon_callbacks(qa_q1_services *game) {
  qa_q1_callback_handlers touch = {0};
  touch.touch = touch_projectile;
  qa_q1_callback_registry_register(game->named, "projectile_touch", touch);
  qa_q1_callback_handlers explode_action = {0};
  explode_action.action = grenade_explode;
  qa_q1_callback_registry_register(game->named, "GrenadeExplode", explode_action);
  qa_q1_callback_handlers think = {0};
  think.action = grenade_think;
  qa_q1_callback_registry_register(game->named, "GrenadeThink", think);
  qa_q1_callback_handlers axe = {0};
  axe.action = axe_strike;
  qa_q1_callback_registry_register(game->named, "player_axe3", axe);
}
