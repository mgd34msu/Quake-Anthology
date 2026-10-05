#include "internal.h"
#include "reinforcements.h"
#include "medic.h"
#include "qa/game_q2_entities.h"
#include "muzzle_data.h"

static q2_weapon_call monster_weapon(q2m_context *context,
                                     qa_q2_weapon weapon) {
  context->actor->weapon.frame = context->monster->frame;
  q2_weapon_call call = {
      .game = context->game,
      .actor = context->actor,
      .state = &context->actor->weapon,
      .input = {.angles = context->body.angles,
                .view_height = context->monster->view_height,
                .gravity = context->game->services.physics != NULL
                               ? context->game->services.physics->gravity
                               : 800.0f,
                .players_collide = true},
      .definition = qa_q2_weapon_definition_at(context->game, weapon),
      .now_ns = context->game->now_ns,
      .frame_ns = context->game->frame_ns,
      .rerelease = context->game->options.edition == QA_Q2_RERELEASE,
  };
  return call;
}


static bool muzzle(q2m_context *context, int flash, qa_vec3 start,
                   qa_vec3 direction, qa_error *error) {
  return !q2m_alive(context) ||
         q2m_emit(context, QA_BUILTIN_MUZZLE, "q2:monster-muzzle", flash,
                  start, qa_vec_add(start, direction), 1.0f, error);
}

bool q2m_damage_enemy(q2m_context *context, float range, int canonical_mod,
                      uint32_t flags, float damage, float kick, bool *hit,
                      qa_error *error) {
  if (hit != NULL)
    *hit = false;
  if (!q2m_alive(context) || context->monster->enemy.registry == 0)
    return true;
  qa_body_state target;
  qa_combat_state combat;
  qa_error ignored = {0};
  if (!qa_world_body_read(context->game->services.world,
                          context->monster->enemy, &target, &ignored) ||
      !qa_combat_read(context->game->services.combat, context->monster->enemy,
                      &combat, &ignored) ||
      !combat.can_take_damage || combat.health <= 0.0f ||
      q2m_distance(context, context->monster->enemy) > range)
    return true;
  qa_vec3 direction =
      qa_vec_normalize(qa_vec_sub(target.origin, context->body.origin));
  qa_attack attack = {
      .attacker = context->actor->id,
      .inflictor = context->actor->id,
      .combat_provider = context->game->options.owner,
      .cause = qa_q2_damage_cause(context->game->options.edition,
                                  context->game->options.product, canonical_mod,
                                  flags),
  };
  if (!q2_damage(context->game, &attack, context->monster->enemy, damage, kick,
                 direction, target.origin, qa_v3(0, 0, 0), false, error))
    return false;
  if (hit != NULL)
    *hit = true;
  return true;
}

bool q2m_melee(q2m_context *context, float range, float damage, float kick,
               qa_error *error) {
  bool hit;
  bool result = q2m_damage_enemy(context, range, Q2M_MOD_HIT, 8u, damage, kick,
                                 &hit, error);
  if (result && hit && q2m_alive(context))
    result = q2m_sound(context, "weapons/melee2.wav", 1, 1.0f, error);
  return result;
}

bool q2m_hit(q2m_context *context, qa_vec3 aim, float damage, float kick,
              bool *hit, qa_error *error) {
  *hit = false;
  qa_q2_game *game = context->game;
  qa_actor_id enemy = context->monster->enemy;
  if (!q2m_alive(context) || !q2_actor_live(game, enemy))
    return true;
  qa_body_state target_body;
  if (!qa_world_body_read(game->services.world, enemy, &target_body, error))
    return !q2m_alive(context) || !q2_actor_live(game, enemy);
  if (!q2m_alive(context) || !q2_actor_live(game, enemy))
    return true;
  bool rerelease = game->options.edition == QA_Q2_RERELEASE;
  qa_vec3 delta = qa_vec_sub(target_body.origin, context->body.origin);
  float range = q2m_body_distance(game->options.edition, &context->body, &target_body);
  if (range > aim.x)
    return true;
  float side = aim.y;
  if (side > context->body.bounds.mins.x && side < context->body.bounds.maxs.x) {
    if (!rerelease)
      range -= target_body.bounds.maxs.x;
  } else {
    side = side < 0.0f ? target_body.bounds.mins.x : target_body.bounds.maxs.x;
  }
  qa_vec3 point = qa_vec_add(context->body.origin, qa_vec_scale(delta, range));
  if (rerelease) {
    qa_vec3 mins = qa_vec_add(target_body.origin, target_body.bounds.mins);
    qa_vec3 maxs = qa_vec_add(target_body.origin, target_body.bounds.maxs);
    point = qa_v3(fmaxf(mins.x, fminf(maxs.x, context->body.origin.x)),
                  fmaxf(mins.y, fminf(maxs.y, context->body.origin.y)),
                  fmaxf(mins.z, fminf(maxs.z, context->body.origin.z)));
  }
  qa_actor_id target = enemy;
  for (unsigned segment = 0; segment != (rerelease ? 2u : 1u); ++segment) {
    qa_trace_query query = {.start = segment ? point : context->body.origin,
        .end = segment ? target_body.origin : point,
        .pass_actor = context->actor->id,
        .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = rerelease ? Q2_PROJECTILE_MASK : Q2_SHOT_MASK;
    qa_trace_result trace;
    if (!qa_world_trace(game->services.world, &query, &trace, error))
      return false;
    if (!q2m_alive(context) || !q2_actor_live(game, enemy))
      return true;
    if (trace.fraction < 1.0f) {
      if (trace.hit != QA_TRACE_HIT_ACTOR)
        return true;
      qa_combat_state combat;
      qa_error observed = {0};
      bool described = qa_combat_read(game->services.combat, trace.actor, &combat, &observed);
      if (!q2m_alive(context) || !q2_actor_live(game, enemy))
        return true;
      if (!described || !combat.can_take_damage)
        return true;
      qa_builtin_actor_traits traits = {0};
      if (game->services.actor_traits)
        game->services.actor_traits(game->services.context, trace.actor, &traits);
      if (!q2m_alive(context) || !q2_actor_live(game, enemy))
        return true;
      target = traits.monster || traits.player ? enemy : trace.actor;
    }
  }
  qa_vec3 forward, right, up;
  qa_builtin_angle_vectors(context->body.angles, &forward, &right, &up);
  qa_vec3 impact = qa_vec_add(context->body.origin,
      qa_vec_add(qa_vec_scale(forward, range),
          qa_vec_add(qa_vec_scale(right, side), qa_vec_scale(up, aim.z))));
  qa_attack attack = {.attacker = context->actor->id, .inflictor = context->actor->id,
      .combat_provider = game->options.owner,
      .cause = qa_q2_damage_cause(game->options.edition, game->options.product,
                                  Q2M_MOD_HIT, 8u)};
  if (!q2_damage(game, &attack, target, damage, truncf(kick / 2.0f),
                   qa_vec_sub(impact, target_body.origin), impact, qa_v3(0, 0, 0), false, error))
    return false;
  if (!q2m_alive(context))
    return true;
  qa_builtin_actor_traits traits = {0};
  if (game->services.actor_traits && q2_actor_live(game, target))
    game->services.actor_traits(game->services.context, target, &traits);
  if (!q2m_alive(context) || (!traits.monster && !traits.player))
    return true;
  *hit = true;
  if (!q2_actor_live(game, enemy))
    return true;
  qa_body_state current;
  if (!qa_world_body_read(game->services.world, enemy, &current, error))
    return !q2m_alive(context) || !q2_actor_live(game, enemy);
  if (!q2m_alive(context) || !q2_actor_live(game, enemy))
    return true;
  qa_vec3 center = qa_vec_add(current.origin,
      qa_vec_scale(qa_vec_add(current.bounds.mins, current.bounds.maxs), .5f));
  current.velocity = qa_vec_add(current.velocity,
      qa_vec_scale(qa_vec_normalize(qa_vec_sub(center, impact)), kick));
  if (current.velocity.z > 0.0f)
    current.ground = (qa_actor_id){0};
  return qa_world_body_write(game->services.world, enemy, &current, error);
}

static float monster_projectile_speed(const q2m_context *context,
                                      q2m_attack_kind kind) {
  q2m_species species = context->monster->definition->species;
  if (kind == Q2M_ATTACK_BFG && (species == Q2M_JORG || species == Q2M_MAKRON))
    return 300.0f;
  if (kind == Q2M_ATTACK_BLASTER) {
    if (species == Q2M_TANK || species == Q2M_TANK_COMMANDER)
      return 800.0f;
    if (species == Q2M_BOSS2 || species == Q2M_MAKRON ||
        species == Q2M_GUARDIAN)
      return 1000.0f;
  }
  if (kind == Q2M_ATTACK_ROCKET) {
    if (species == Q2M_TANK || species == Q2M_TANK_COMMANDER)
      return 550.0f;
    if (species == Q2M_BOSS2 || species == Q2M_BOSS5 || species == Q2M_CHICK ||
        species == Q2M_CHICK_HEAT || species == Q2M_CARRIER)
      return 500.0f;
    if (species == Q2M_SUPERTANK)
      return context->game->options.edition == QA_Q2_RERELEASE ? 750.0f
                                                               : 500.0f;
  }
  if (kind == Q2M_ATTACK_GRENADE)
    return 600.0f;
  float speed = context->monster->definition->projectile_speed;
  if (speed > 0.0f)
    return speed;
  if (kind == Q2M_ATTACK_ROCKET)
    return 650.0f;
  if (kind == Q2M_ATTACK_BFG)
    return 400.0f;
  return 1000.0f;
}

static float monster_bfg_radius(const q2m_context *context) {
  return context->monster->definition->species == Q2M_JORG     ? 200.0f
         : context->monster->definition->species == Q2M_MAKRON ? 300.0f
                                                               : 1000.0f;
}

qa_vec3 q2m_project_offset(const q2m_context *context, qa_vec3 offset) {
  qa_vec3 forward, right;
  qa_builtin_angle_vectors(context->body.angles, &forward, &right, NULL);
  float scale = context->game->options.edition == QA_Q2_RERELEASE
                    ? context->monster->entity_scale
                    : 1.0f;
  return qa_vec_add(context->body.origin,
                    qa_vec_add(qa_vec_scale(forward, offset.x * scale),
                               qa_vec_add(qa_vec_scale(right, offset.y * scale),
                                          qa_v3(0, 0, offset.z * scale))));
}

bool q2m_muzzle_offset(const q2m_context *context, int flash, qa_vec3 *offset,
                       qa_error *error) {
  const qa_vec3 *offsets;
  size_t count;
  if (context->game->options.edition == QA_Q2_RERELEASE) {
    offsets = q2m_rerelease_muzzle_offsets;
    count = sizeof(q2m_rerelease_muzzle_offsets) /
            sizeof(q2m_rerelease_muzzle_offsets[0]);
  } else {
    offsets = q2m_classic_muzzle_offsets;
    count = sizeof(q2m_classic_muzzle_offsets) /
            sizeof(q2m_classic_muzzle_offsets[0]);
  }
  if (flash < 0 || (size_t)flash >= count) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "monster muzzle flash %d outside edition table", flash);
    return false;
  }
  *offset = offsets[flash];
  return true;
}

bool q2m_project_flash(const q2m_context *context, int flash, qa_vec3 *start,
                       qa_error *error) {
  qa_vec3 offset;
  if (!q2m_muzzle_offset(context, flash, &offset, error))
    return false;
  *start = q2m_project_offset(context, offset);
  return true;
}

static bool target_eye(q2m_context *context, qa_body_state *body,
                       float *view_height, bool *available) {
  *available = false;
  if (context->monster->enemy.registry == 0)
    return true;
  qa_error ignored = {0};
  if (!qa_world_body_read(context->game->services.world,
                          context->monster->enemy, body, &ignored))
    return true;
  *view_height = 22.0f;
  if (context->game->services.actor_traits != NULL) {
    qa_builtin_actor_traits traits = {0};
    if (context->game->services.actor_traits(context->game->services.context,
                                             context->monster->enemy,
                                             &traits))
      *view_height = traits.view_height;
    if (!q2m_alive(context))
      return true;
  }
  *available = true;
  return true;
}

bool q2m_source_shot(q2m_context *context, int flash, float lead,
                     qa_vec3 *start, qa_vec3 *direction, bool *available,
                     qa_error *error) {
  *available = false;
  qa_body_state enemy;
  float view_height;
  if (!target_eye(context, &enemy, &view_height, available) || !*available ||
      !q2m_alive(context))
    return true;
  if (!q2m_project_flash(context, flash, start, error))
    return false;
  qa_vec3 target = qa_vec_add(
      qa_vec_add(enemy.origin, qa_v3(0.0f, 0.0f, view_height)),
      qa_vec_scale(enemy.velocity, lead));
  *direction = qa_vec_normalize(qa_vec_sub(target, *start));
  return true;
}

bool q2m_predict_from(q2m_context *context, qa_vec3 start, float speed,
                      bool eye, float offset, qa_vec3 *point,
                      qa_vec3 *direction, bool *available, qa_error *error) {
  *available = false;
  qa_body_state enemy;
  float view_height;
  if (!target_eye(context, &enemy, &view_height, available) || !*available ||
      !q2m_alive(context))
    return true;

  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  qa_vec3 initial_target = enemy.origin;
  if (eye)
    initial_target.z += view_height;
  qa_vec3 initial = qa_vec_sub(initial_target, start);
  if (rerelease) {
    qa_trace_query query = {
        .start = start,
        .end = initial_target,
        .pass_actor = context->actor->id,
        .policy = qa_collision_default_policy(QA_COLLISION_Q2),
    };
    query.policy.contents_mask = Q2M_ATTACK_MASK;
    qa_trace_result trace;
    if (!qa_world_trace(context->game->services.world, &query, &trace, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (trace.hit != QA_TRACE_HIT_ACTOR ||
        !qa_actor_id_equal(trace.actor, context->monster->enemy)) {
      eye = !eye;
      initial_target = enemy.origin;
      if (eye)
        initial_target.z += view_height;
      initial = qa_vec_sub(initial_target, start);
    }
  }

  float travel = rerelease && speed == 0.0f
                     ? 0.0f
                     : speed > 0.0f ? qa_vec_length(initial) / speed : 0.0f;
  qa_vec3 target = qa_vec_add(
      enemy.origin, qa_vec_scale(enemy.velocity, travel - offset));
  if (rerelease) {
    qa_vec3 initial_direction = qa_vec_normalize(initial);
    qa_vec3 predicted_direction =
        qa_vec_normalize(qa_vec_sub(target, start));
    bool reject = qa_vec_dot(initial_direction, predicted_direction) < 0.0f;
    if (!reject) {
      qa_trace_query query = {
          .start = start,
          .end = target,
          .policy = qa_collision_default_policy(QA_COLLISION_Q2),
      };
      query.policy.contents_mask = 3u;
      qa_trace_result trace;
      if (!qa_world_trace(context->game->services.world, &query, &trace,
                          error))
        return false;
      if (!q2m_alive(context))
        return true;
      reject = trace.fraction < 0.9f;
    }
    if (reject)
      target = enemy.origin;
  }
  if (eye)
    target.z += view_height;
  if (point != NULL)
    *point = target;
  *direction = qa_vec_normalize(qa_vec_sub(target, start));
  return true;
}

bool q2m_predict_shot(q2m_context *context, int flash, float speed, bool eye,
                      float offset, qa_vec3 *start, qa_vec3 *direction,
                      bool *available, qa_error *error) {
  if (!q2m_project_flash(context, flash, start, error))
    return false;
  return q2m_predict_from(context, *start, speed, eye, offset, NULL, direction,
                          available, error);
}

static qa_q2_weapon monster_weapon_kind(q2m_attack_kind kind) {
  switch (kind) {
  case Q2M_ATTACK_BULLET:
    return QA_Q2_MACHINEGUN;
  case Q2M_ATTACK_SHOTGUN:
    return QA_Q2_SHOTGUN;
  case Q2M_ATTACK_ROCKET:
  case Q2M_ATTACK_HEAT:
    return QA_Q2_ROCKETLAUNCHER;
  case Q2M_ATTACK_GRENADE:
    return QA_Q2_GRENADELAUNCHER;
  case Q2M_ATTACK_RAIL:
    return QA_Q2_RAILGUN;
  case Q2M_ATTACK_BFG:
    return QA_Q2_BFG;
  case Q2M_ATTACK_BEAM:
    return QA_Q2_HEATBEAM;
  case Q2M_ATTACK_TRACKER:
    return QA_Q2_DISINTEGRATOR;
  case Q2M_ATTACK_ION:
    return QA_Q2_IONRIPPER;
  case Q2M_ATTACK_BLUE_BOLT:
  case Q2M_ATTACK_GREEN_BOLT:
    return QA_Q2_HYPERBLASTER;
  case Q2M_ATTACK_PLASMA:
    return QA_Q2_PHALANX;
  case Q2M_ATTACK_FLECHETTE:
    return QA_Q2_ETF_RIFLE;
  default:
    return QA_Q2_BLASTER;
  }
}

bool q2m_fire(q2m_context *context, const q2m_fire_spec *spec,
              qa_error *error) {
  if (!q2m_alive(context))
    return true;
  q2_weapon_call call =
      monster_weapon(context, monster_weapon_kind(spec->kind));
  call.has_projectile_effects = spec->has_projectile_effects;
  call.projectile_effects = spec->projectile_effects;
  call.has_projectile_enemy = spec->has_projectile_enemy;
  call.projectile_enemy = spec->projectile_enemy;
  call.has_grenade_impulse = spec->has_grenade_impulse;
  call.grenade_right = spec->grenade_right;
  call.grenade_up = spec->grenade_up;
  call.grenade_gravity = spec->grenade_gravity;
  qa_actor_id spawned = {0};
  if (spec->has_turn_fraction)
    call.spawned_projectile = &spawned;

  bool fired;
  switch (spec->kind) {
  case Q2M_ATTACK_BULLET:
  case Q2M_ATTACK_SHOTGUN:
    fired = q2_bullet(&call, spec->start, spec->direction, spec->damage,
                      spec->kick, spec->horizontal_spread,
                      spec->vertical_spread, (int)spec->pellets,
                      spec->direct_mod, spec->kind == Q2M_ATTACK_SHOTGUN, error);
    break;
  case Q2M_ATTACK_RAIL:
    fired = q2_rail(&call, spec->start, spec->direction, spec->damage,
                    spec->kick, spec->direct_mod, 0, error);
    break;
  case Q2M_ATTACK_BEAM:
    fired = q2_heatbeam(&call, spec->start, spec->direction, spec->damage,
                        spec->kick, error);
    break;
  case Q2M_ATTACK_BLASTER:
  case Q2M_ATTACK_ROCKET:
  case Q2M_ATTACK_GRENADE:
  case Q2M_ATTACK_BFG:
  case Q2M_ATTACK_HEAT:
  case Q2M_ATTACK_TRACKER:
  case Q2M_ATTACK_ION:
  case Q2M_ATTACK_BLUE_BOLT:
  case Q2M_ATTACK_GREEN_BOLT:
  case Q2M_ATTACK_PLASMA:
  case Q2M_ATTACK_FLECHETTE: {
    q2_projectile_kind projectile =
        spec->kind == Q2M_ATTACK_BLASTER      ? Q2_BOLT
        : spec->kind == Q2M_ATTACK_ROCKET     ? Q2_ROCKET
        : spec->kind == Q2M_ATTACK_GRENADE    ? Q2_GRENADE
        : spec->kind == Q2M_ATTACK_BFG        ? Q2_BFG_BALL
        : spec->kind == Q2M_ATTACK_HEAT       ? Q2_HEAT_ROCKET
        : spec->kind == Q2M_ATTACK_TRACKER    ? Q2_TRACKER
        : spec->kind == Q2M_ATTACK_ION        ? Q2_ION
        : spec->kind == Q2M_ATTACK_BLUE_BOLT  ? Q2_BLUE_BOLT
        : spec->kind == Q2M_ATTACK_GREEN_BOLT ? Q2_GREEN_BOLT
        : spec->kind == Q2M_ATTACK_PLASMA     ? Q2_PLASMA
                                               : Q2_FLECHETTE;
    fired = q2_projectile_spawn(
        &call, projectile, spec->start, spec->direction, spec->damage,
        spec->kick, spec->speed, spec->radius, spec->radius_damage, spec->fuse,
        spec->direct_mod, spec->splash_mod, false, false, error);
    break;
  }
  default:
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "unsupported exact monster attack kind %d", spec->kind);
    return false;
  }
  if (fired && spec->has_turn_fraction && q2_actor_live(context->game, spawned)) {
    q2_actor *projectile = context->game->actors[spawned.slot];
    if (projectile != NULL && qa_actor_id_equal(projectile->id, spawned) &&
        projectile->projectile.kind == Q2_HEAT_ROCKET)
      projectile->projectile.turn_fraction = spec->turn_fraction;
  }
  return fired &&
         (!q2m_alive(context) || spec->flash < 0 ||
          muzzle(context, spec->flash, spec->start, spec->direction, error));
}

q2m_fire_spec q2m_fire_default(q2m_context *context, q2m_attack_kind kind,
                               float damage, int flash, qa_vec3 start,
                               qa_vec3 direction) {
  float speed = monster_projectile_speed(context, kind);
  q2m_fire_spec spec = {
      .kind = kind,
      .flash = flash,
      .start = start,
      .direction = direction,
      .damage = damage,
      .speed = speed,
      .pellets = 1,
  };
  switch (kind) {
  case Q2M_ATTACK_BULLET:
    spec.kick = 4.0f;
    spec.horizontal_spread = 300.0f;
    spec.vertical_spread = 500.0f;
    spec.direct_mod = Q2M_MOD_MACHINEGUN;
    break;
  case Q2M_ATTACK_SHOTGUN:
    spec.kick = 1.0f;
    spec.horizontal_spread = context->game->options.edition == QA_Q2_RERELEASE
                                 ? 1500.0f
                                 : 1000.0f;
    spec.vertical_spread = context->game->options.edition == QA_Q2_RERELEASE
                               ? 750.0f
                               : 500.0f;
    spec.pellets = context->game->options.edition == QA_Q2_RERELEASE ? 9 : 12;
    spec.direct_mod = Q2M_MOD_SHOTGUN;
    break;
  case Q2M_ATTACK_BLASTER:
    spec.kick = 1.0f;
    spec.fuse = 2.0f;
    spec.direct_mod = Q2M_MOD_BLASTER;
    break;
  case Q2M_ATTACK_ROCKET:
  case Q2M_ATTACK_HEAT:
    spec.radius = 70.0f;
    spec.radius_damage = damage;
    spec.fuse = speed > 0.0f ? 8000.0f / speed : 0.0f;
    spec.direct_mod = Q2M_MOD_ROCKET;
    spec.splash_mod = Q2M_MOD_ROCKET_SPLASH;
    break;
  case Q2M_ATTACK_GRENADE:
    spec.radius = 90.0f;
    spec.radius_damage = damage;
    spec.fuse = 2.5f;
    spec.direct_mod = Q2M_MOD_GRENADE;
    spec.splash_mod = Q2M_MOD_GRENADE_SPLASH;
    break;
  case Q2M_ATTACK_RAIL:
    spec.kick = 100.0f;
    spec.direct_mod = Q2M_MOD_RAILGUN;
    break;
  case Q2M_ATTACK_BFG:
    spec.radius = monster_bfg_radius(context);
    spec.radius_damage = damage;
    spec.direct_mod = Q2M_MOD_BFG_BLAST;
    spec.splash_mod = Q2M_MOD_BFG_BLAST;
    break;
  case Q2M_ATTACK_BEAM:
    spec.kick = 50.0f;
    spec.direct_mod = Q2M_MOD_HEATBEAM;
    break;
  case Q2M_ATTACK_TRACKER:
    spec.kick = damage * 3.0f;
    spec.fuse = 10.0f;
    spec.direct_mod = Q2M_MOD_TRACKER;
    spec.splash_mod = Q2M_MOD_TRACKER;
    spec.has_projectile_enemy = context->monster->enemy.registry != 0;
    spec.projectile_enemy = context->monster->enemy;
    break;
  case Q2M_ATTACK_ION:
    spec.kick = 1.0f;
    spec.radius = 100.0f;
    spec.fuse = 3.0f;
    spec.direct_mod = 34;
    spec.has_projectile_effects = true;
    spec.projectile_effects = UINT64_C(0x100000);
    break;
  case Q2M_ATTACK_BLUE_BOLT:
    spec.kick = 1.0f;
    spec.fuse = 2.0f;
    spec.direct_mod = context->game->options.edition == QA_Q2_RERELEASE ? 58
                                                                         : 1;
    spec.has_projectile_effects = true;
    spec.projectile_effects = UINT64_C(0x400000);
    break;
  case Q2M_ATTACK_GREEN_BOLT:
    spec.kick = 1.0f;
    spec.radius = 128.0f;
    spec.fuse = 2.0f;
    spec.direct_mod = 43;
    break;
  case Q2M_ATTACK_PLASMA:
    spec.radius = 60.0f;
    spec.radius_damage = 60.0f;
    spec.fuse = speed > 0.0f ? 8000.0f / speed : 0.0f;
    spec.direct_mod = 35;
    spec.splash_mod = 35;
    break;
  case Q2M_ATTACK_FLECHETTE:
    spec.kick = 2.0f;
    spec.fuse = speed > 0.0f ? 8000.0f / speed : 0.0f;
    spec.direct_mod = 42;
    break;
  default:
    break;
  }
  return spec;
}

bool q2m_widow_disrupt(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context) || context->monster->enemy.registry == 0)
    return true;
  qa_body_state enemy;
  qa_error ignored = {0};
  if (!qa_world_body_read(context->game->services.world,
                          context->monster->enemy, &enemy, &ignored))
    return true;
  qa_builtin_actor_traits traits = {.view_height = 22.0f};
  if (context->game->services.actor_traits != NULL) {
    qa_builtin_actor_traits shared = {0};
    if (context->game->services.actor_traits(context->game->services.context,
                                             context->monster->enemy, &shared))
      traits = shared;
  }
  if (!q2m_alive(context))
    return true;

  qa_vec3 offset = context->game->options.edition == QA_Q2_RERELEASE
                       ? qa_v3(64.72f, 14.50f, 88.81f)
                       : qa_v3(57.72f, 14.50f, 88.81f);
  qa_vec3 start = q2m_project_offset(context, offset);
  bool locked =
      qa_vec_length(qa_vec_sub(context->monster->saved_attack_position,
                               enemy.origin)) < 30.0f;
  float speed = locked ? 500.0f : 1200.0f;
  qa_vec3 direction;
  if (locked) {
    direction = qa_vec_normalize(
        qa_vec_sub(context->monster->saved_attack_position, start));
  } else {
    bool eye = true;
    qa_vec3 initial = qa_vec_sub(
        qa_vec_add(enemy.origin, qa_v3(0, 0, traits.view_height)), start);
    if (context->game->options.edition == QA_Q2_RERELEASE) {
      qa_trace_query query = {
          .start = start,
          .end = qa_vec_add(start, initial),
          .pass_actor = context->actor->id,
          .policy = qa_collision_default_policy(QA_COLLISION_Q2),
      };
      query.policy.contents_mask = UINT32_C(0x4200001b);
      qa_trace_result trace;
      if (!qa_world_trace(context->game->services.world, &query, &trace, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (trace.hit != QA_TRACE_HIT_ACTOR ||
          !qa_actor_id_equal(trace.actor, context->monster->enemy)) {
        eye = false;
        initial = qa_vec_sub(enemy.origin, start);
      }
    }
    float time = qa_vec_length(initial) / speed;
    qa_vec3 target =
        qa_vec_add(enemy.origin, qa_vec_scale(enemy.velocity, time));
    if (context->game->options.edition == QA_Q2_RERELEASE) {
      qa_vec3 initial_direction = qa_vec_normalize(initial);
      qa_vec3 predicted_direction = qa_vec_normalize(qa_vec_sub(target, start));
      if (qa_vec_dot(initial_direction, predicted_direction) < 0.0f) {
        target = enemy.origin;
      } else {
        qa_trace_query query = {
            .start = start,
            .end = target,
            .policy = qa_collision_default_policy(QA_COLLISION_Q2),
        };
        query.policy.contents_mask = 3u;
        qa_trace_result trace;
        if (!qa_world_trace(context->game->services.world, &query, &trace,
                            error))
          return false;
        if (!q2m_alive(context))
          return true;
        if (trace.fraction < 0.9f)
          target = enemy.origin;
      }
    }
    if (eye)
      target.z += traits.view_height;
    direction = qa_vec_normalize(qa_vec_sub(target, start));
  }

  q2_weapon_call call = monster_weapon(context, QA_Q2_DISINTEGRATOR);
  call.has_projectile_enemy = locked;
  call.projectile_enemy = locked ? context->monster->enemy : (qa_actor_id){0};
  bool result = q2_projectile_spawn(
      &call, Q2_TRACKER, start, direction, 20.0f, 60.0f, speed, 0.0f, 0.0f,
      10.0f, Q2M_MOD_TRACKER, Q2M_MOD_TRACKER, false, false, error);
  return result &&
         (!q2m_alive(context) ||
          q2m_emit(context, QA_BUILTIN_MUZZLE, "q2:monster-muzzle", 148, start,
                   qa_vec_add(start, direction), 1.0f, error));
}


static const char *pain_sound(q2m_species species) {
  switch (species) {
  case Q2M_INFANTRY:
  case Q2M_TURRET_DRIVER:
    return "infantry/infpain1.wav";
  case Q2M_SOLDIER_LIGHT:
  case Q2M_SOLDIER:
  case Q2M_SOLDIER_SS:
  case Q2M_SOLDIER_RIPPER:
  case Q2M_SOLDIER_HYPER:
  case Q2M_SOLDIER_LASER:
    return "soldier/solpain1.wav";
  case Q2M_BERSERK:
    return "berserk/berpain2.wav";
  case Q2M_BRAIN:
    return "brain/brnpain1.wav";
  case Q2M_CHICK:
  case Q2M_CHICK_HEAT:
    return "chick/chkpain1.wav";
  case Q2M_GUNNER:
  case Q2M_GUN_COMMANDER:
    return "gunner/gunpain1.wav";
  case Q2M_TANK:
  case Q2M_TANK_COMMANDER:
    return "tank/tnkpain2.wav";
  case Q2M_MEDIC:
  case Q2M_MEDIC_COMMANDER:
    return "medic/medpain1.wav";
  case Q2M_PARASITE:
    return "parasite/parpain1.wav";
  case Q2M_MUTANT:
    return "mutant/mutpain1.wav";
  case Q2M_GLADIATOR:
  case Q2M_GLADB:
    return "gladiator/gldpain1.wav";
  default:
    return "misc/pain.wav";
  }
}

static const char *death_sound(q2m_species species) {
  switch (species) {
  case Q2M_INFANTRY:
  case Q2M_TURRET_DRIVER:
    return "infantry/infdeth1.wav";
  case Q2M_SOLDIER_LIGHT:
  case Q2M_SOLDIER:
  case Q2M_SOLDIER_SS:
  case Q2M_SOLDIER_RIPPER:
  case Q2M_SOLDIER_HYPER:
  case Q2M_SOLDIER_LASER:
    return "soldier/soldeth1.wav";
  case Q2M_BERSERK:
    return "berserk/berdeth2.wav";
  case Q2M_BRAIN:
    return "brain/brndeth1.wav";
  case Q2M_CHICK:
  case Q2M_CHICK_HEAT:
    return "chick/chkdeth1.wav";
  case Q2M_GUNNER:
  case Q2M_GUN_COMMANDER:
    return "gunner/death1.wav";
  case Q2M_TANK:
  case Q2M_TANK_COMMANDER:
    return "tank/tnkdeth1.wav";
  case Q2M_MEDIC:
  case Q2M_MEDIC_COMMANDER:
    return "medic/meddeth1.wav";
  case Q2M_PARASITE:
    return "parasite/pardeth1.wav";
  case Q2M_MUTANT:
    return "mutant/mutdeth1.wav";
  case Q2M_GLADIATOR:
  case Q2M_GLADB:
    return "gladiator/glddeth2.wav";
  default:
    return "misc/udeath.wav";
  }
}

static bool last_attack_chainfist(const struct qa_q2_monster *monster) {
  return monster->last_attack.cause.kind == QA_CAUSE_Q2 &&
         monster->last_attack.cause.source.q2.means_of_death == 40;
}

static bool reacts_to_pain_cause(const q2m_context *context, bool chainfist) {
  const struct qa_q2_monster *monster = context->monster;
  return !monster->ducked && !monster->combat_point &&
         (context->game->options.skill < 3 || chainfist);
}

static bool reacts_to_pain(const q2m_context *context) {
  return reacts_to_pain_cause(context, last_attack_chainfist(context->monster));
}

static bool stop_loop_sound(q2m_context *context, const char *path, int channel,
                            qa_error *error) {
  if (context->monster->definition->species == Q2M_GUARDIAN)
    context->monster->weapon_sound = 0;
  qa_builtin_event event = {
      .kind = QA_BUILTIN_STOP_SOUND,
      .family = QA_GAME_Q2,
      .provider = context->game->options.owner,
      .actor = context->actor->id,
      .other = context->monster->enemy,
      .time_ns = context->game->now_ns,
      .origin = context->body.origin,
      .volume = 1.0f,
      .attenuation = 1.0f,
      .channel = channel,
      .frame = context->monster->frame,
  };
  if (!qa_builtin_resource(&context->game->services, path, &event.resource,
                           error))
    return false;
  return qa_builtin_emit(&context->game->services, &event, error);
}

static bool pain_brain_mutant(q2m_context *context, bool brain, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  if (context->combat.health < m->max_health * .5f) m->skin = 1;
  else if (rerelease) m->skin = 0;
  if (g->now_ns < m->pain_ns)
    return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  if (!rerelease && g->options.skill == 3)
    return true;
  float choice = rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g);
  unsigned index = choice < .33f ? 0u : choice < .66f ? 1u : 2u;
  static const q2m_move_id brain_moves[] = {
      Q2M_MOVE_brain_move_pain1, Q2M_MOVE_brain_move_pain2, Q2M_MOVE_brain_move_pain3};
  static const q2m_move_id mutant_moves[] = {
      Q2M_MOVE_mutant_move_pain1, Q2M_MOVE_mutant_move_pain2, Q2M_MOVE_mutant_move_pain3};
  const char *sound = brain ? (index == 1 ? "brain/brnpain2.wav" : "brain/brnpain1.wav")
                            : (index == 1 ? "mutant/mutpain2.wav" : "mutant/mutpain1.wav");
  if (!q2m_sound(context, sound, 2, 1, error))
    return false;
  if (!q2m_alive(context) || (rerelease && !reacts_to_pain(context)))
    return true;
  if (!q2m_set_move(context, brain ? brain_moves[index] : mutant_moves[index],
                     rerelease && !brain, error))
    return false;
  return !q2m_alive(context) || !rerelease || !brain || !m->ducked ||
         q2m_callback_run(context, Q2M_CALLBACK_monster_duck_up, error);
}

static bool pain_flipper_flyer(q2m_context *context, bool flyer, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  if (context->combat.health < m->max_health * .5f) m->skin = 1;
  else if (rerelease) m->skin = 0;
  if (g->now_ns < m->pain_ns)
    return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  if (!rerelease && g->options.skill == 3)
    return true;
  unsigned index;
  if (rerelease)
    index = flyer ? q2_random_bounded(g, 3) : (q2_random_bounded(g, 2) == 0 ? 1u : 0u);
  else {
    uint32_t sample = qa_builtin_random_integer(&g->random);
    index = flyer ? sample % 3u : (sample + 1u) % 2u;
  }
  static const q2m_move_id flyer_moves[] = {
      Q2M_MOVE_flyer_move_pain1, Q2M_MOVE_flyer_move_pain2, Q2M_MOVE_flyer_move_pain3};
  const char *sound = flyer ? (index == 1 ? "flyer/flypain2.wav" : "flyer/flypain1.wav")
                            : (index == 1 ? "flipper/flppain2.wav" : "flipper/flppain1.wav");
  if (!q2m_sound(context, sound, 2, 1, error))
    return false;
  if (!q2m_alive(context) || (rerelease && !reacts_to_pain(context)))
    return true;
  if (rerelease && flyer) {
    m->fly_thrusters = false;
    m->fly_acceleration = 15;
    m->fly_speed = 165;
    m->fly_min_distance = 45;
    m->fly_max_distance = 200;
  }
  return q2m_set_move(context, flyer ? flyer_moves[index]
                             : index ? Q2M_MOVE_flipper_move_pain2 : Q2M_MOVE_flipper_move_pain1,
                        rerelease, error);
}

static bool pain_berserk(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  float damage = m->pending_damage;
  if (context->combat.health < m->max_health * .5f) m->skin = 1;
  else if (rerelease) m->skin = 0;
  if (rerelease && m->move &&
      ((m->move->id == Q2M_MOVE_berserk_move_jump) ||
       (m->move->id == Q2M_MOVE_berserk_move_jump2) ||
       (m->move->id == Q2M_MOVE_berserk_move_attack_strike))) return true;
  if (g->now_ns < m->pain_ns) return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  if (!q2m_sound(context, "berserk/berpain2.wav", 2, 1, error)) return false;
  if (!q2m_alive(context) || (rerelease ? !reacts_to_pain(context) : g->options.skill == 3))
    return true;
  if (rerelease || g->options.product == QA_Q2_ROGUE) {
    m->dodging = false;
    if (rerelease && m->attack_state == Q2M_SLIDING) m->attack_state = Q2M_STRAIGHT;
  }
  bool short_pain = rerelease ? damage <= 50 : damage < 20;
  if (!short_pain)
    short_pain = (rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g)) < .5f;
  return q2m_set_move(context, short_pain ? Q2M_MOVE_berserk_move_pain1 : Q2M_MOVE_berserk_move_pain2,
                        false, error);
}

static bool pain_chick(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  bool rogue = g->options.product == QA_Q2_ROGUE;
  float damage = m->pending_damage;
  if (context->combat.health < m->max_health * .5f) {
    if (rerelease) m->skin |= 1;
    else m->skin = 1;
  } else if (rerelease) m->skin &= ~1;
  if (rogue || rerelease) {
    m->dodging = false;
    if (rerelease && m->attack_state == Q2M_SLIDING) m->attack_state = Q2M_STRAIGHT;
  }
  if (g->now_ns < m->pain_ns) return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  float choice = rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g);
  const char *sound = choice < .33f ? "chick/chkpain1.wav" :
                      choice < .66f ? "chick/chkpain2.wav" : "chick/chkpain3.wav";
  if (!q2m_sound(context, sound, 2, 1, error)) return false;
  bool xatrix = g->options.product == QA_Q2_XATRIX || m->definition->species == Q2M_CHICK_HEAT;
  if (!q2m_alive(context) || (rerelease ? !reacts_to_pain(context) :
                             g->options.skill == 3 && !xatrix)) return true;
  if (rogue || rerelease) m->manual_steering = false;
  q2m_move_id move = damage <= 10 ? Q2M_MOVE_chick_move_pain1 :
                     damage <= 25 ? Q2M_MOVE_chick_move_pain2 : Q2M_MOVE_chick_move_pain3;
  if (!q2m_set_move(context, move, rerelease, error)) return false;
  return !q2m_alive(context) || (!rogue && !rerelease) || !m->ducked ||
         q2m_callback_run(context, Q2M_CALLBACK_monster_duck_up, error);
}

static bool pain_gunner(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  bool rogue = g->options.product == QA_Q2_ROGUE;
  float damage = m->pending_damage;
  if (context->combat.health < m->max_health * .5f) m->skin = 1;
  else if (rerelease) m->skin = 0;
  if (rogue || rerelease) {
    m->dodging = false;
    if (rerelease && m->attack_state == Q2M_SLIDING) m->attack_state = Q2M_STRAIGHT;
  }
  if (rerelease) {
    if (m->move && ((m->move->id == Q2M_MOVE_gunner_move_jump) ||
                    (m->move->id == Q2M_MOVE_gunner_move_jump2))) return true;
  } else if (rogue && !context->body.ground.registry) return true;
  if (g->now_ns < m->pain_ns) return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  bool first = rerelease ? q2_random_bounded(g, 2) == 0 :
                          (qa_builtin_random_integer(&g->random) & 1u) != 0;
  if (!q2m_sound(context, first ? "gunner/gunpain2.wav" : "gunner/gunpain1.wav",
                  2, 1, error)) return false;
  if (!q2m_alive(context) || (rerelease ? !reacts_to_pain(context) : g->options.skill == 3))
    return true;
  q2m_move_id move = damage <= 10 ? Q2M_MOVE_gunner_move_pain3 :
                     damage <= 25 ? Q2M_MOVE_gunner_move_pain2 : Q2M_MOVE_gunner_move_pain1;
  if (!q2m_set_move(context, move, false, error)) return false;
  if (rogue || rerelease) m->manual_steering = false;
  return !q2m_alive(context) || (!rogue && !rerelease) || !m->ducked ||
         q2m_callback_run(context, Q2M_CALLBACK_monster_duck_up, error);
}

static bool pain_hover(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  bool rogue = g->options.product == QA_Q2_ROGUE || m->definition->species == Q2M_DAEDALUS;
  float damage = m->pending_damage;
  if (context->combat.health < m->max_health * .5f) {
    if (rogue || rerelease) m->skin |= 1;
    else m->skin = 1;
  } else if (rerelease) m->skin &= ~1;
  if (g->now_ns < m->pain_ns) return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  if (!rerelease && g->options.skill == 3) return true;
  bool first;
  q2m_move_id move;
  if (rerelease) {
    first = q2_rerelease_float(g, 0, 1) < .5f;
    const char *sound = context->combat.mass < 225 ?
        (first ? "hover/hovpain1.wav" : "hover/hovpain2.wav") :
        (first ? "daedalus/daedpain1.wav" : "daedalus/daedpain2.wav");
    if (!q2m_sound(context, sound, 2, 1, error)) return false;
    if (!q2m_alive(context) || !reacts_to_pain(context)) return true;
    float choice = q2_rerelease_float(g, 0, 1);
    move = damage <= 25 ? (choice < .5f ? Q2M_MOVE_hover_move_pain3 : Q2M_MOVE_hover_move_pain2) :
                         (choice < .3f ? Q2M_MOVE_hover_move_pain1 : Q2M_MOVE_hover_move_pain2);
  } else {
    first = damage <= 25 ? q2m_random(g) < .5f :
            !rogue || q2m_random(g) < .45f - .1f * (float)g->options.skill;
    move = first ? (damage <= 25 ? Q2M_MOVE_hover_move_pain3 : Q2M_MOVE_hover_move_pain1) :
                   Q2M_MOVE_hover_move_pain2;
    const char *sound = !rogue || context->combat.mass < 225 ?
        (first ? "hover/hovpain1.wav" : "hover/hovpain2.wav") :
        (first ? "daedalus/daedpain1.wav" : "daedalus/daedpain2.wav");
    if (!q2m_sound(context, sound, 2, 1, error)) return false;
    if (!q2m_alive(context)) return true;
  }
  return q2m_set_move(context, move, rerelease, error);
}

static bool pain_soldier(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  bool heavy = m->definition->species == Q2M_SOLDIER_RIPPER ||
               m->definition->species == Q2M_SOLDIER_HYPER ||
               m->definition->species == Q2M_SOLDIER_LASER;
  bool rogue = g->options.product == QA_Q2_ROGUE && !heavy;
  if (context->combat.health < m->max_health * .5f)
    m->skin |= 1;
  else if (rerelease)
    m->skin &= ~1;
  if (rerelease || rogue) {
    m->dodging = false;
    if (rerelease && m->attack_state == Q2M_SLIDING)
      m->attack_state = Q2M_STRAIGHT;
    m->charging = false;
    m->manual_steering = false;
  }
  q2m_move_id pain1 = heavy && !rerelease ? Q2M_MOVE_soldierh_move_pain1 : Q2M_MOVE_soldier_move_pain1;
  q2m_move_id pain2 = heavy && !rerelease ? Q2M_MOVE_soldierh_move_pain2 : Q2M_MOVE_soldier_move_pain2;
  q2m_move_id pain3 = heavy && !rerelease ? Q2M_MOVE_soldierh_move_pain3 : Q2M_MOVE_soldier_move_pain3;
  q2m_move_id pain4 = heavy && !rerelease ? Q2M_MOVE_soldierh_move_pain4 : Q2M_MOVE_soldier_move_pain4;
  bool airborne = context->body.velocity.z > 100.0f;
  if (g->now_ns < m->pain_ns) {
    if (!airborne || !m->move ||
        (m->move->id != pain1 && m->move->id != pain2 && m->move->id != pain3))
      return true;
  } else {
    m->pain_ns = q2m_after(g->now_ns, 3.0);
    int type = (rerelease ? m->count : m->skin) | 1;
    const char *sound = type == 1 ? "soldier/solpain2.wav"
                        : type == 3 ? "soldier/solpain1.wav"
                                    : "soldier/solpain3.wav";
    if (!q2m_sound(context, sound, 2, 1.0f, error)) return false;
    if (!q2m_alive(context)) return true;
    if (!airborne) {
      if (rerelease ? !reacts_to_pain(context) : g->options.skill == 3)
        return true;
      float draw = q2m_random(g);
      if (!q2m_set_move(context, draw < .33f ? pain1 : draw < .66f ? pain2 : pain3,
                        true, error)) return false;
      if ((rerelease || rogue) && m->ducked &&
          !q2m_callback_run(context, Q2M_CALLBACK_monster_duck_up, error)) return false;
      return !q2m_alive(context) || q2m_soldier_sound_end(context, error);
    }
  }
  if ((rerelease || rogue) && m->ducked &&
      !q2m_callback_run(context, Q2M_CALLBACK_monster_duck_up, error)) return false;
  if (!q2m_alive(context)) return true;
  if (!q2m_set_move(context, pain4, true, error)) return false;
  return q2m_soldier_sound_end(context, error);
}

static bool pain_tank(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  bool chainfist = rerelease && last_attack_chainfist(m);
  float damage = m->pending_damage;
  if (context->combat.health < m->max_health * .5f) m->skin |= 1;
  else if (rerelease) m->skin &= ~1;
  if (!chainfist && damage <= 10) return true;
  if (g->now_ns < m->pain_ns) return true;
  if (!chainfist) {
    if (damage <= 30 &&
        (rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g)) > .2f) return true;
    if ((rerelease || g->options.skill >= 2) &&
        ((m->frame >= 115 && m->frame <= 144) ||
         (m->frame >= 55 && m->frame <= 70))) return true;
  }
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  const char *sound = rerelease && m->count ? "tank/pain.wav" : "tank/tnkpain2.wav";
  if (!q2m_sound(context, sound, 2, 1, error)) return false;
  if (!q2m_alive(context) || (rerelease ? !reacts_to_pain(context) : g->options.skill == 3))
    return true;
  if (rerelease || g->options.product == QA_Q2_ROGUE) m->manual_steering = false;
  q2m_move_id move = damage <= 30 ? Q2M_MOVE_tank_move_pain1 :
                     damage <= 60 ? Q2M_MOVE_tank_move_pain2 : Q2M_MOVE_tank_move_pain3;
  return q2m_set_move(context, move, rerelease, error);
}

static bool infantry_pain_dodge(q2m_context *context, qa_error *error) {
  if (q2_rerelease_float(context->game, 0, 1) >= .33f)
    return true;
  return q2_monster_dodge(context->game, context->actor->id,
                          context->monster->last_attack.attacker,
                          context->elapsed, NULL, false, error);
}

static bool pain_infantry(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  bool rogue = g->options.product == QA_Q2_ROGUE;
  bool ordinary_think = !m->turret_attached;
  if (context->combat.health < m->max_health * .5f) m->skin = 1;
  else if (rerelease) m->skin = 0;
  if (rerelease && ordinary_think && m->move &&
      ((m->move->id == Q2M_MOVE_infantry_move_jump) ||
       (m->move->id == Q2M_MOVE_infantry_move_jump2))) return true;
  if (!rerelease && rogue && !context->body.ground.registry) return true;
  if (rerelease || rogue) {
    m->dodging = false;
    if (rerelease && m->attack_state == Q2M_SLIDING) m->attack_state = Q2M_STRAIGHT;
  }
  if (g->now_ns < m->pain_ns)
    return !rerelease || !ordinary_think || infantry_pain_dodge(context, error);
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  if (!rerelease && g->options.skill == 3) return true;
  unsigned index = rerelease ? (q2_random_bounded(g, 2) == 0 ? 1u : 0u)
                             : qa_builtin_random_integer(&g->random) % 2u;
  q2m_move_id move = index ? Q2M_MOVE_infantry_move_pain2 : Q2M_MOVE_infantry_move_pain1;
  if (!rerelease && !q2m_set_move(context, move, false, error)) return false;
  if (!q2m_sound(context, index ? "infantry/infpain2.wav" : "infantry/infpain1.wav",
                   2, 1, error)) return false;
  if (!q2m_alive(context) || (rerelease && !ordinary_think)) return true;
  if (rerelease) {
    if (!reacts_to_pain(context)) return infantry_pain_dodge(context, error);
    if (!q2m_set_move(context, move, false, error)) return false;
  }
  return !q2m_alive(context) || !(rerelease || rogue) || !m->ducked ||
         q2m_callback_run(context, Q2M_CALLBACK_monster_duck_up, error);
}

static bool pain_supertank(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  bool boss5 = m->definition->species == Q2M_BOSS5;
  float damage = m->pending_damage;
  if (context->combat.health < m->max_health * .5f) {
    if (rerelease) m->skin |= 1;
    else m->skin = 1;
  } else if (rerelease) m->skin &= ~1;
  if (g->now_ns < m->pain_ns) return true;
  if (!(rerelease && last_attack_chainfist(m))) {
    if (damage <= 25 && (rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g)) < .2f)
      return true;
    if ((rerelease || g->options.skill >= 2) && m->frame >= 20 && m->frame <= 33)
      return true;
  }
  if (!rerelease) {
    m->pain_ns = q2m_after(g->now_ns, 3.0);
    if (!boss5 && g->options.skill == 3) return true;
  }
  const char *sound = damage <= 10 ? "bosstank/btkpain1.wav" :
                      damage <= 25 ? "bosstank/btkpain3.wav" : "bosstank/btkpain2.wav";
  if (!q2m_sound(context, sound, 2, 1, error)) return false;
  if (!q2m_alive(context)) return true;
  if (rerelease) {
    m->pain_ns = q2m_after(g->now_ns, 3.0);
    if (!reacts_to_pain(context)) return true;
  }
  q2m_move_id move = boss5 ? (damage <= 10 ? Q2M_MOVE_boss5_move_pain1 :
                               damage <= 25 ? Q2M_MOVE_boss5_move_pain2 : Q2M_MOVE_boss5_move_pain3) :
                            (damage <= 10 ? Q2M_MOVE_supertank_move_pain1 :
                               damage <= 25 ? Q2M_MOVE_supertank_move_pain2 : Q2M_MOVE_supertank_move_pain3);
  return q2m_set_move(context, move, rerelease, error);
}

static bool pain_classic_makron(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  float damage = m->pending_damage;
  if (context->combat.health < m->max_health * .5f) m->skin = 1;
  if (g->now_ns < m->pain_ns || (damage <= 25 && q2m_random(g) < .2f)) return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  if (g->options.skill == 3) return true;
  q2m_move_id move; const char *sound;
  if (damage <= 40) {
    move = Q2M_MOVE_makron_move_pain4; sound = "makron/pain3.wav";
  } else if (damage <= 110) {
    move = Q2M_MOVE_makron_move_pain5; sound = "makron/pain2.wav";
  } else {
    if (q2m_random(g) > (damage <= 150 ? .45f : .35f)) return true;
    move = Q2M_MOVE_makron_move_pain6; sound = "makron/pain1.wav";
  }
  if (!q2m_sound(context, sound, 2, 0, error)) return false;
  return !q2m_alive(context) || q2m_set_move(context, move, false, error);
}

static bool pain_jorg(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  float damage = m->pending_damage;
  if (context->combat.health < m->max_health * .5f) m->skin = 1;
  else if (rerelease) m->skin = 0;
  if (!rerelease && !q2m_weapon_sound(context, NULL, error)) return false;
  if (!q2m_alive(context) || g->now_ns < m->pain_ns) return true;
  if (!(rerelease && last_attack_chainfist(m))) {
    if (damage <= 40 && (rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g)) <= .6f)
      return true;
    if (m->frame >= 0 && m->frame <= 7 &&
        (rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g)) <= .005f) return true;
    if (m->frame >= 8 && m->frame <= 13 &&
        (rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g)) <= .00005f) return true;
    if (m->frame >= 18 && m->frame <= 25 &&
        (rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g)) <= .005f) return true;
  }
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  if (!rerelease && g->options.skill == 3) return true;
  q2m_move_id move = Q2M_MOVE_NONE; const char *sound = NULL;
  if (damage <= 50) {
    move = Q2M_MOVE_jorg_move_pain1;
    if (!rerelease) sound = "boss3/bs3pain1.wav";
  } else if (damage <= 100) {
    move = Q2M_MOVE_jorg_move_pain2; sound = "boss3/bs3pain2.wav";
  } else if ((rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g)) <= .3f) {
    move = Q2M_MOVE_jorg_move_pain3; sound = "boss3/bs3pain3.wav";
  }
  if (sound && !q2m_sound(context, sound, 2, 1, error)) return false;
  if (!q2m_alive(context)) return true;
  if (rerelease) {
    if (!reacts_to_pain(context)) return true;
    if (!q2m_jorg_sound_end(context, error)) return false;
  }
  return !q2m_alive(context) || !move || q2m_set_move(context, move, rerelease, error);
}

static bool pain_floater(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  if (context->combat.health < monster->max_health * .5f) monster->skin = 1;
  else if (rerelease) monster->skin = 0;
  if (context->game->now_ns < monster->pain_ns)
    return true;
  if (context->game->options.edition == QA_Q2_RERELEASE &&
      monster->move != NULL &&
      ((monster->move->id == Q2M_MOVE_floater_move_disguise) ||
       (monster->move->id == Q2M_MOVE_floater_move_pop)))
    return true;
  if (!rerelease) {
    monster->pain_ns = q2m_after(context->game->now_ns, 3.0);
    if (context->game->options.skill == 3) return true;
  }
  bool first = rerelease
                   ? q2_random_bounded(context->game, 3) == 0
                   : ((uint64_t)qa_builtin_random_integer(&context->game->random) + 1u) % 3u == 0;
  if (!q2m_sound(context,
                 first ? "floater/fltpain1.wav" : "floater/fltpain2.wav", 2,
                 1.0f, error))
    return false;
  if (!q2m_alive(context)) return true;
  if (rerelease) monster->pain_ns = q2m_after(context->game->now_ns, 3.0);
  if (rerelease && !reacts_to_pain(context))
    return true;
  return q2m_set_move(context,
                      first ? Q2M_MOVE_floater_move_pain1 : Q2M_MOVE_floater_move_pain2,
                      true, error);
}

static bool pain_boss2(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  if (context->combat.health < m->max_health * .5f) m->skin = 1;
  else if (rerelease) m->skin = 0;
  if (g->now_ns < m->pain_ns) return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  float damage = m->pending_damage;
  const char *sound = damage < 10 ? "bosshovr/bhvpain3.wav" :
                      damage < 30 ? "bosshovr/bhvpain1.wav" : "bosshovr/bhvpain2.wav";
  if (!q2m_sound(context, sound, 2, 0, error)) return false;
  return !q2m_alive(context) || (rerelease && !reacts_to_pain(context)) ||
         q2m_set_move(context, damage < 30 ? Q2M_MOVE_boss2_move_pain_light :
                                             Q2M_MOVE_boss2_move_pain_heavy, rerelease, error);
}

static bool pain_classic_parasite(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  if (context->combat.health < m->max_health * .5f) m->skin = 1;
  if (g->now_ns < m->pain_ns) return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  if (g->options.skill == 3) return true;
  if (!q2m_sound(context, q2m_random(g) < .5f ? "parasite/parpain1.wav" :
                                               "parasite/parpain2.wav", 2, 1, error)) return false;
  return !q2m_alive(context) || q2m_set_move(context, Q2M_MOVE_parasite_move_pain1, false, error);
}

static bool pain_gekk(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  if (rerelease)
    m->skin = context->combat.health < m->max_health * .25f ? 2 :
              context->combat.health < m->max_health * .5f ? 1 : 0;
  if (m->spawnflags & 8u) {
    m->spawnflags &= ~8u;
    return true;
  }
  if (!rerelease) {
    if (context->combat.health < m->max_health * .25f) m->skin = 2;
    else if (context->combat.health < m->max_health * .5f) m->skin = 1;
  }
  if (g->now_ns < m->pain_ns) return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  if (!q2m_sound(context, "gek/gk_pain1.wav", 2, 1, error)) return false;
  if (!q2m_alive(context)) return true;
  bool water = m->water_level >= (rerelease ? 2 : 1);
  if (rerelease && water && !(context->actor->physics.flags & QA_PHYSICS_SWIMMING)) {
    context->actor->physics.flags |= QA_PHYSICS_SWIMMING;
    m->alternate_fly = true;
  }
  if (rerelease && !reacts_to_pain(context)) return true;
  q2m_move_id move = Q2M_MOVE_gekk_move_pain;
  if (!water) {
    float draw = rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g);
    move = draw > .5f ? Q2M_MOVE_gekk_move_pain1 : Q2M_MOVE_gekk_move_pain2;
  }
  return q2m_set_move(context, move, rerelease, error);
}

static bool pain_insane(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  if (g->now_ns < m->pain_ns) return true;
  m->pain_ns = q2m_after(g->now_ns, 3.0);
  unsigned variant = 1u + (rerelease ? (q2_random_bounded(g, 2) == 0 ? 1u : 0u) :
                                      qa_builtin_random_integer(&g->random) & 1u);
  int band = context->combat.health < 25 ? 25 : context->combat.health < 50 ? 50 :
             context->combat.health < 75 ? 75 : 100;
  char sound[sizeof("player/male/pain100_2.wav")];
  snprintf(sound, sizeof(sound), "player/male/pain%d_%u.wav", band, variant);
  if (!q2m_sound(context, sound, 2, 2, error)) return false;
  if (!q2m_alive(context) || (!rerelease && g->options.skill == 3)) return true;
  q2m_move_id move = m->spawnflags & 8u ? Q2M_MOVE_insane_move_struggle_cross :
                    ((m->frame >= 227 && m->frame <= 235) ||
                     (m->frame >= 98 && m->frame <= 159) ||
                     (rerelease && m->frame >= 0 && m->frame <= 39)) ?
                    Q2M_MOVE_insane_move_crawl_pain : Q2M_MOVE_insane_move_stand_pain;
  return q2m_set_move(context, move, rerelease, error);
}

static bool pain_gladiator(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  const bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  const bool chainfist = last_attack_chainfist(monster);
  if (context->combat.health < monster->max_health * .5f) {
    if (rerelease) monster->skin |= 1;
    else monster->skin = 1;
  } else if (rerelease) monster->skin &= ~1;
  const bool gladb = monster->definition->species == Q2M_GLADB;
  const bool airborne = context->body.velocity.z > 100.0f;
  q2m_move_id ground_move = gladb ? Q2M_MOVE_gladb_move_pain : Q2M_MOVE_gladiator_move_pain;
  q2m_move_id air_move =
      gladb ? Q2M_MOVE_gladb_move_pain_air : Q2M_MOVE_gladiator_move_pain_air;
  if (context->game->now_ns < monster->pain_ns) {
    if (airborne && monster->move != NULL &&
        monster->move->id == ground_move)
      return q2m_set_move(context, air_move, true, error);
    return true;
  }
  monster->pain_ns = q2m_after(context->game->now_ns, 3.0);
  const char *sound = q2m_random(context->game) < 0.5f
                          ? "gladiator/pain.wav"
                          : "gladiator/gldpain2.wav";
  if (!q2m_sound(context, sound, 2, 1.0f, error))
    return false;
  if (!q2m_alive(context))
    return true;
  bool animates = rerelease
                      ? reacts_to_pain_cause(context, chainfist)
                      : gladb || context->game->options.skill != 3;
  if (!animates) return true;
  qa_body_state current;
  if (!qa_world_body_read(context->game->services.world, context->actor->id,
                          &current, error)) return false;
  return !q2m_alive(context) ||
         q2m_set_move(context, current.velocity.z > 100 ? air_move : ground_move,
                      true, error);
}

static bool pain_fixbot(q2m_context *context, float damage, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (context->game->now_ns < monster->pain_ns)
    return true;
  const bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  if (rerelease) {
    monster->fly_position_ns = 0;
    monster->fly_acceleration = 5;
    monster->fly_speed = 110;
    monster->fly_buzzard = false;
    monster->fly_min_distance = 300;
    monster->fly_max_distance = 500;
  }
  monster->pain_ns = q2m_after(context->game->now_ns, 3.0);
  if (!q2m_sound(context, "flyer/flypain1.wav", 2, 1.0f, error))
    return false;
  if (!q2m_alive(context))
    return true;
  q2m_move_id move = damage <= 10.0f   ? Q2M_MOVE_fixbot_move_pain3
                     : damage <= 25.0f ? Q2M_MOVE_fixbot_move_painb
                                       : Q2M_MOVE_fixbot_move_paina;
  if (!q2m_set_move(context, move, true, error)) return false;
  return !rerelease || q2m_medic_abort(context, false, false, false, error);
}

static bool pain_arachnid(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  bool chainfist = last_attack_chainfist(m);
  if (g->now_ns < m->pain_ns) return true;
  m->pain_ns = q2m_after(g->now_ns, 3);
  if (!q2m_sound(context, "arachnid/pain.wav", 2, 1, error)) return false;
  if (!q2m_alive(context) || (rerelease ? !reacts_to_pain_cause(context, chainfist) :
      m->ducked || m->combat_point || g->options.skill >= 3)) return true;
  float choice = rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g);
  return q2m_set_move(context, choice < .5f ? Q2M_MOVE_arachnid_move_pain1 :
                                           Q2M_MOVE_arachnid_move_pain2, true, error);
}

static bool pain_shambler(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  float damage = m->pending_damage;
  bool chainfist = last_attack_chainfist(m);
  if (g->now_ns < m->timestamp_ns) return true;
  m->timestamp_ns = q2_deadline(g->now_ns, rerelease ? UINT64_C(1000000) : g->frame_ns);
  if (!q2m_sound(context, "shambler/shurt2.wav", 0, 1, error)) return false;
  if (!q2m_alive(context)) return true;
  if (!(rerelease && chainfist) && damage <= 30 &&
      (rerelease ? q2_rerelease_float(g, 0, 1) : q2m_random(g)) > .2f) return true;
  if (g->options.skill >= 2 && m->frame >= 35 && m->frame <= 64) return true;
  if (rerelease ? !reacts_to_pain_cause(context, chainfist) :
      m->ducked || m->combat_point || g->options.skill >= 3) return true;
  if (g->now_ns < m->pain_ns) return true;
  m->pain_ns = q2m_after(g->now_ns, 2);
  return q2m_set_move(context, Q2M_MOVE_shambler_move_pain, true, error);
}

static bool commander_pain_dodge(q2m_context *context, qa_actor_id attacker,
                                 qa_error *error) {
  qa_q2_game *g = context->game;
  float choice = g->options.edition == QA_Q2_RERELEASE
                     ? q2_rerelease_float(g, 0, 1) : q2m_random(g);
  return choice >= .3f || q2_monster_dodge(g, context->actor->id,
      attacker, g->options.edition == QA_Q2_RERELEASE ? context->elapsed : .1f,
      NULL, false, error);
}

static bool pain_gun_commander(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *g = context->game;
  bool rerelease = g->options.edition == QA_Q2_RERELEASE;
  float damage = m->pending_damage;
  qa_actor_id attacker = m->last_attack.attacker;
  bool chainfist = last_attack_chainfist(m);
  if (rerelease)
    m->skin = context->combat.health < m->max_health * .5f ? m->skin | 1 : m->skin & ~1;
  m->dodging = false;
  if (rerelease && m->attack_state == Q2M_SLIDING) m->attack_state = Q2M_STRAIGHT;
  if ((m->move->id == Q2M_MOVE_guncmdr_move_jump) ||
      (m->move->id == Q2M_MOVE_guncmdr_move_jump2) ||
      (m->move->id == Q2M_MOVE_guncmdr_move_duck_attack)) return true;
  if (g->now_ns < m->pain_ns) return commander_pain_dodge(context, attacker, error);
  m->pain_ns = q2m_after(g->now_ns, 3);
  bool first = rerelease ? q2_random_bounded(g, 2) == 0 : q2m_random(g) < .5f;
  if (!q2m_sound(context, first ? "guncmdr/gcdrpain2.wav" :
                               "guncmdr/gcdrpain1.wav", 2, 1, error)) return false;
  if (!q2m_alive(context)) return true;
  if (rerelease ? !reacts_to_pain_cause(context, chainfist) :
      m->ducked || m->combat_point || g->options.skill >= 3)
    return commander_pain_dodge(context, attacker, error);
  if (!attacker.registry && g->services.physics) attacker = g->services.physics->world_actor;
  if (!q2_actor_live(g, attacker)) return true;
  qa_body_state other;
  if (!qa_world_body_read(g->services.world, context->actor->id, &context->body, error)) return false;
  if (!q2m_alive(context)) return true;
  if (!qa_world_body_read(g->services.world, attacker, &other, error)) return false;
  if (!q2m_alive(context) || !q2_actor_live(g, attacker)) return true;
  qa_vec3 forward, difference = qa_vec_sub(other.origin, context->body.origin);
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
  difference.z = 0;
  q2m_move_id move;
  if (damage < 35) {
    unsigned choice = rerelease ? q2_random_bounded(g, 4) :
                                 (unsigned)(q2m_random(g) * 4);
    move = choice == 0 ? Q2M_MOVE_guncmdr_move_pain3 : choice == 1 ? Q2M_MOVE_guncmdr_move_pain2 :
           choice == 2 ? Q2M_MOVE_guncmdr_move_pain1 : Q2M_MOVE_guncmdr_move_pain7;
  } else {
    if (qa_vec_dot(qa_vec_normalize(difference), forward) < -.4f)
      move = Q2M_MOVE_guncmdr_move_pain6;
    else {
      first = rerelease ? q2_random_bounded(g, 2) == 0 : q2m_random(g) < .5f;
      move = first ? Q2M_MOVE_guncmdr_move_pain4 : Q2M_MOVE_guncmdr_move_pain5;
    }
    m->pain_ns = q2m_after(m->pain_ns, 1.5);
  }
  if (!q2m_set_move(context, move, false, error)) return false;
  m->manual_steering = false;
  return !m->ducked || q2m_callback_run(context, Q2M_CALLBACK_monster_duck_up, error);
}

static bool pain_carrier(q2m_context *context, float damage, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  const bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  const bool chainfist = last_attack_chainfist(monster);
  if (context->combat.health < monster->max_health * .5f)
    monster->skin = 1;
  else if (rerelease)
    monster->skin = 0;
  if ((!rerelease && context->game->options.skill == 3) ||
      context->game->now_ns < monster->pain_ns)
    return true;
  monster->pain_ns = q2m_after(context->game->now_ns, 5.0);
  const char *sound = damage < 10 ? "carrier/pain_sm.wav"
                      : damage < 30 ? "carrier/pain_md.wav"
                                    : "carrier/pain_lg.wav";
  if (!q2m_sound(context, sound, 2, 0, error))
    return false;
  if (!q2m_alive(context) ||
      (rerelease && !reacts_to_pain_cause(context, chainfist)))
    return true;
  if (rerelease) monster->weapon_sound = 0;
  if (damage < 10 ||
      (damage < 30 && !(rerelease && chainfist) &&
       q2m_random(context->game) >= .5f))
    return true;
  if (!q2m_set_move(context, damage < 30 ? Q2M_MOVE_carrier_move_pain_light
                                        : Q2M_MOVE_carrier_move_pain_heavy,
                     true, error))
    return false;
  monster->hold_frame = false;
  monster->manual_steering = false;
  monster->yaw_speed = 15.0f;
  return true;
}

static bool pain_widow(q2m_context *context, float damage, bool sequel,
                       qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  const bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  const bool chainfist = last_attack_chainfist(monster);
  const int skill = context->game->options.skill;
  if (context->combat.health < monster->max_health * .5f)
    monster->skin = 1;
  else if (rerelease)
    monster->skin = 0;
  if ((!rerelease && skill == 3) || context->game->now_ns < monster->pain_ns)
    return true;
  if (!rerelease && !sequel && monster->pause_ns == UINT64_MAX)
    monster->pause_ns = 0;
  monster->pain_ns = q2m_after(context->game->now_ns, 5.0);
  const char *sound = sequel
                          ? damage < 15.0f   ? "widow/bw2pain1.wav"
                            : damage < 75.0f ? "widow/bw2pain2.wav"
                                             : "widow/bw2pain3.wav"
                          : damage < 15.0f   ? "widow/bw1pain1.wav"
                            : damage < 75.0f ? "widow/bw1pain2.wav"
                                             : "widow/bw1pain3.wav";
  if ((rerelease || sequel) && !q2m_sound(context, sound, 2, 0, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (rerelease) {
    if (!reacts_to_pain_cause(context, chainfist)) return true;
    if (!sequel) monster->pause_ns = 0;
  }
  if (damage >= 15.0f && skill < 3) {
    float chance = damage < 75.0f ? 0.6f - 0.2f * (float)skill
                                  : 0.75f - 0.1f * (float)skill;
    if (q2m_random(context->game) < chance) {
      if (sequel) monster->manual_steering = false;
      q2m_move_id move = sequel ? Q2M_MOVE_widow2_move_pain
                                : damage < 75.0f
                                      ? Q2M_MOVE_widow_move_pain_light
                                      : Q2M_MOVE_widow_move_pain_heavy;
      if (!q2m_set_move(context, move, true, error))
        return false;
      if (!sequel) monster->manual_steering = false;
    }
  }
  return rerelease || sequel || !q2m_alive(context) ||
         q2m_sound(context, sound, 2, 0, error);
}

static bool pain_guardian(q2m_context *context, float damage, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  const bool chainfist = last_attack_chainfist(monster);
  if ((!chainfist && damage <= 10.0f) ||
      context->game->now_ns < monster->pain_ns)
    return true;
  if (!chainfist && damage <= 75.0f && q2m_random(context->game) > 0.2f)
    return true;
  if ((monster->frame >= 162 && monster->frame <= 176) ||
      (monster->frame >= 177 && monster->frame <= 180) ||
      (monster->frame >= 125 && monster->frame <= 137))
    return true;
  monster->pain_ns = q2m_after(context->game->now_ns, 3.0);
  if (!reacts_to_pain(context))
    return true;
  if (!q2m_set_move(context, Q2M_MOVE_guardian_move_pain1, true, error))
    return false;
  return stop_loop_sound(context, "weapons/hyprbl1a.wav", 0, error);
}

static bool pain_rerelease_makron(q2m_context *context, float damage,
                                  qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  const bool chainfist = last_attack_chainfist(monster);
  if ((monster->move != Q2M_MOVE_NONE &&
       (monster->move->id == Q2M_MOVE_makron_move_sight)) ||
      context->game->now_ns < monster->pain_ns ||
      (!chainfist && damage <= 25.0f && q2m_random(context->game) < 0.2f))
    return true;
  monster->pain_ns = q2m_after(context->game->now_ns, 3.0);
  const char *sound = NULL;
  q2m_move_id move = Q2M_MOVE_NONE;
  if (damage <= 40.0f) {
    sound = "makron/pain3.wav";
    move = Q2M_MOVE_makron_move_pain4;
  } else if (damage <= 110.0f) {
    sound = "makron/pain2.wav";
    move = Q2M_MOVE_makron_move_pain5;
  } else if (q2m_random(context->game) <=
             (damage <= 150.0f ? 0.45f : 0.35f)) {
    sound = "makron/pain1.wav";
    move = Q2M_MOVE_makron_move_pain6;
  }
  if (sound != NULL && !q2m_sound(context, sound, 2, 1.0f, error))
    return false;
  return !q2m_alive(context) || !reacts_to_pain(context) || move == Q2M_MOVE_NONE ||
         q2m_set_move(context, move, true, error);
}

static bool pain_rerelease_parasite(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  monster->skin = context->combat.health < monster->base_health * 0.5f ? 1 : 0;
  if (context->game->now_ns < monster->pain_ns)
    return true;
  if (!q2m_parasite_interrupt(context, false, error))
    return false;
  if (!q2m_alive(context))
    return true;
  monster->pain_ns = q2m_after(context->game->now_ns, 3.0);
  const char *sound = q2m_random(context->game) < 0.5f ? "parasite/parpain1.wav"
                                                     : "parasite/parpain2.wav";
  if (!q2m_sound(context, sound, 2, 1.0f, error))
    return false;
  return !q2m_alive(context) || !reacts_to_pain(context) ||
         q2m_set_move(context, Q2M_MOVE_parasite_move_pain1, false, error);
}

static bool pain_actor(q2m_context *context, qa_error *error) {
  qa_q2_game *game = context->game;
  struct qa_q2_monster *monster = context->monster;
  bool rerelease = game->options.edition == QA_Q2_RERELEASE;
  bool wounded = context->combat.health < monster->max_health * .5f;
  if (wounded || rerelease)
    monster->skin = wounded ? 1 : 0;
  if (game->now_ns < monster->pain_ns)
    return true;
  monster->pain_ns = q2m_after(game->now_ns, 3);

  qa_actor_id attacker = monster->last_attack.attacker;
  qa_builtin_actor_traits traits = {0};
  bool player = game->services.actor_traits && q2_actor_live(game, attacker) &&
                game->services.actor_traits(game->services.context, attacker, &traits) &&
                traits.player;
  if (!q2m_alive(context))
    return true;
  if (player && q2_actor_live(game, attacker) &&
      (rerelease ? q2_rerelease_float(game, 0, 1) : q2m_random(game)) < .4f) {
    qa_body_state other, self;
    if (!qa_world_body_read(game->services.world, attacker, &other, error))
      return false;
    if (!q2m_alive(context) || !q2_actor_live(game, attacker))
      return true;
    if (!qa_world_body_read(game->services.world, context->actor->id, &self, error))
      return false;
    if (!q2m_alive(context) || !q2_actor_live(game, attacker))
      return true;
    qa_vec3 direction = qa_vec_sub(other.origin, self.origin);
    double yaw;
    if (direction.x == 0)
      yaw = direction.y == 0 ? 0 : direction.y > 0 ? 90 : rerelease ? 270 : -90;
    else {
      double angle = atan2(direction.y, direction.x);
      yaw = rerelease ? angle * (180.0 / (double)0x1.921fb6p1f)
                      : trunc(angle * 180.0 / 0x1.921fb54442d18p1);
      if (yaw < 0)
        yaw += 360;
    }
    monster->ideal_yaw = (float)yaw;
    float draw = rerelease ? q2_rerelease_float(game, 0, 1) : q2m_random(game);
    if (!q2m_set_move(context, draw < .5f ? Q2M_MOVE_actor_move_flipoff : Q2M_MOVE_actor_move_taunt,
                       true, error))
      return false;
    const qa_actor_record *record =
        qa_actors_get(qa_session_actors(game->services.session), context->actor->id);
    if (!record || !record->has_source) {
      qa_error_set(error, QA_ERROR_ARGUMENT, context->actor->id.slot,
                   "Q2 actor taunt requires its actual source entity number");
      return false;
    }
    static const char *const names[] = {
        "Hellrot", "Tokay", "Killme", "Disruptor", "Adrianator", "Rambear", "Titus", "Bitterman"};
    static const char *const messages[] = {"Watch it", "#$@*&", "Idiot", "Check your targets"};
    unsigned message = rerelease ? q2_random_bounded(game, 4)
                                 : qa_builtin_random_integer(&game->random) % 3u;
    char text[64];
    snprintf(text, sizeof(text), "%s: %s!\n", names[record->source_slot % 8u], messages[message]);
    return q2_player_print(game, attacker, 3, text, error);
  }
  static const q2m_move_id moves[] = {
      Q2M_MOVE_actor_move_pain1, Q2M_MOVE_actor_move_pain2, Q2M_MOVE_actor_move_pain3};
  unsigned choice = rerelease ? q2_random_bounded(game, 3)
                              : qa_builtin_random_integer(&game->random) % 3u;
  return q2m_set_move(context, moves[choice], true, error);
}

static bool pain_medic(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  bool rogue = rerelease || context->game->options.product != QA_Q2_XATRIX ||
               m->definition->species == Q2M_MEDIC_COMMANDER;
  bool commander = context->combat.mass > 400;
  const float damage = m->pending_damage;
  const bool chainfist = last_attack_chainfist(m);
  if (rogue) {
    m->dodging = false;
    if (rerelease && m->attack_state == Q2M_SLIDING)
      m->attack_state = Q2M_STRAIGHT;
  }
  if (rerelease)
    m->skin = (m->skin & ~1) | (context->combat.health < m->max_health * .5f);
  else if (context->combat.health < m->max_health * .5f)
    m->skin = rogue && commander ? 3 : 1;
  if (context->game->now_ns < m->pain_ns)
    return true;
  m->pain_ns = q2m_after(context->game->now_ns, 3);
  if (!rerelease && (context->game->options.skill == 3 || (rogue && m->medic)))
    return true;
  float roll = 0;
  bool pain2;
  if (rerelease)
    roll = q2_rerelease_float(context->game, 0, 1);
  if (rogue && commander) {
    if (damage < 35) {
      if (!q2m_sound(context, "medic_commander/medpain1.wav", 2, 1, error))
        return false;
      if (!q2m_alive(context) || !rerelease || !chainfist)
        return true;
    }
    if (!rerelease)
      m->manual_steering = m->hold_frame = false;
    if (!q2m_sound(context, "medic_commander/medpain2.wav", 2, 1, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!rerelease)
      roll = q2m_random(context->game);
    pain2 = roll < fmin((double)damage * .005, .5);
  } else {
    if (!rerelease)
      roll = q2m_random(context->game);
    pain2 = roll >= .5f;
    if (!rerelease && !q2m_set_move(context,
        pain2 ? Q2M_MOVE_medic_move_pain2 : Q2M_MOVE_medic_move_pain1, false, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!q2m_sound(context, pain2 ? "medic/medpain2.wav" : "medic/medpain1.wav", 2, 1, error))
      return false;
    if (!q2m_alive(context))
      return true;
  }
  if (rerelease) {
    if (!reacts_to_pain_cause(context, chainfist) || (!chainfist && m->medic))
      return true;
    if (commander)
      m->manual_steering = m->hold_frame = false;
  }
  if ((rerelease || (rogue && commander)) &&
      !q2m_set_move(context, pain2 ? Q2M_MOVE_medic_move_pain2 : Q2M_MOVE_medic_move_pain1, true, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (rogue && m->ducked && !q2m_callback_run(context, Q2M_CALLBACK_monster_duck_up, error))
    return false;
  return !q2m_alive(context) || !rerelease || q2m_medic_abort(context, false, false, false, error);
}

bool q2m_pain(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context) || context->monster->dead)
    return true;
  struct qa_q2_monster *monster = context->monster;
  q2m_species species = monster->definition->species;
  if (species == Q2M_ACTOR)
    return pain_actor(context, error);
  if (species == Q2M_TURRET)
    return true;
  if (species == Q2M_KAMIKAZE ||
      (species == Q2M_FLYER && context->combat.mass != 50 &&
       (context->game->options.edition == QA_Q2_RERELEASE ||
        context->game->options.product == QA_Q2_ROGUE)))
    return true;
  if (species == Q2M_MEDIC || species == Q2M_MEDIC_COMMANDER)
    return pain_medic(context, error);
  if (species == Q2M_BRAIN || species == Q2M_MUTANT)
    return pain_brain_mutant(context, species == Q2M_BRAIN, error);
  if (species == Q2M_FLIPPER || species == Q2M_FLYER)
    return pain_flipper_flyer(context, species == Q2M_FLYER, error);
  if (species == Q2M_BERSERK) return pain_berserk(context, error);
  if (species == Q2M_CHICK || species == Q2M_CHICK_HEAT) return pain_chick(context, error);
  if (species == Q2M_GUNNER) return pain_gunner(context, error);
  if (species == Q2M_GUN_COMMANDER) return pain_gun_commander(context, error);
  if (species == Q2M_ARACHNID) return pain_arachnid(context, error);
  if (species == Q2M_SHAMBLER) return pain_shambler(context, error);
  if (species == Q2M_HOVER || species == Q2M_DAEDALUS) return pain_hover(context, error);
  if (species == Q2M_TANK || species == Q2M_TANK_COMMANDER) return pain_tank(context, error);
  if (species == Q2M_INFANTRY || species == Q2M_TURRET_DRIVER)
    return pain_infantry(context, error);
  if (species == Q2M_SUPERTANK || species == Q2M_BOSS5)
    return pain_supertank(context, error);
  if (species == Q2M_JORG) return pain_jorg(context, error);
  if (species == Q2M_FLOATER) return pain_floater(context, error);
  if (species == Q2M_BOSS2) return pain_boss2(context, error);
  if (species == Q2M_GEKK) return pain_gekk(context, error);
  if (species == Q2M_INSANE) return pain_insane(context, error);
  if (species == Q2M_GLADIATOR || species == Q2M_GLADB) return pain_gladiator(context, error);
  if (species == Q2M_FIXBOT) return pain_fixbot(context, monster->pending_damage, error);
  if (species == Q2M_CARRIER) return pain_carrier(context, monster->pending_damage, error);
  if (species == Q2M_WIDOW || species == Q2M_WIDOW2)
    return pain_widow(context, monster->pending_damage, species == Q2M_WIDOW2, error);
  if (species == Q2M_MAKRON && context->game->options.edition != QA_Q2_RERELEASE)
    return pain_classic_makron(context, error);
  if (species == Q2M_PARASITE && context->game->options.edition != QA_Q2_RERELEASE)
    return pain_classic_parasite(context, error);
  if (species == Q2M_SOLDIER_LIGHT || species == Q2M_SOLDIER ||
      species == Q2M_SOLDIER_SS || species == Q2M_SOLDIER_RIPPER ||
      species == Q2M_SOLDIER_HYPER || species == Q2M_SOLDIER_LASER)
    return pain_soldier(context, error);
  float damage = monster->pending_damage;
  if (context->combat.health < monster->base_health * 0.5f)
    monster->skin |= 1;
  else if (context->game->options.edition == QA_Q2_RERELEASE)
    monster->skin &= ~1;
  switch (species) {
  case Q2M_GUARDIAN:
    return pain_guardian(context, damage, error);
  case Q2M_STALKER:
    return q2m_stalker_pain(context, reacts_to_pain(context),
                            last_attack_chainfist(monster), error);
  case Q2M_MAKRON:
    if (context->game->options.edition == QA_Q2_RERELEASE)
      return pain_rerelease_makron(context, damage, error);
    break;
  case Q2M_PARASITE:
    if (context->game->options.edition == QA_Q2_RERELEASE)
      return pain_rerelease_parasite(context, error);
    break;
  default:
    break;
  }
  if (context->game->now_ns < monster->pain_ns)
    return true;
  if (species == Q2M_CARRIER && context->game->options.skill == 3)
    return true;

  double delay = species == Q2M_CARRIER ? 5.0 : 3.0;
  monster->pain_ns = q2m_after(context->game->now_ns, delay);

  q2m_move_id move = monster->definition->pain1_move;
  const char *sound = pain_sound(species);
  float random = q2m_random(context->game);
  switch (species) {
  case Q2M_GLADIATOR:
  case Q2M_GLADB:
    move = context->body.velocity.z > 100.0f ? species == Q2M_GLADB
                                                   ? Q2M_MOVE_gladb_move_pain_air
                                                   : Q2M_MOVE_gladiator_move_pain_air
           : species == Q2M_GLADB            ? Q2M_MOVE_gladb_move_pain
                                             : Q2M_MOVE_gladiator_move_pain;
    sound = random < 0.5f ? "gladiator/pain.wav" : "gladiator/gldpain2.wav";
    break;
  case Q2M_CARRIER:
    if (damage < 10.0f) {
      move = Q2M_MOVE_NONE;
      sound = "carrier/pain_sm.wav";
    } else if (damage < 30.0f) {
      move = random < 0.5f ? Q2M_MOVE_carrier_move_pain_light : Q2M_MOVE_NONE;
      sound = "carrier/pain_md.wav";
    } else {
      move = Q2M_MOVE_carrier_move_pain_heavy;
      sound = "carrier/pain_lg.wav";
    }
    break;
  case Q2M_FIXBOT:
    move = damage <= 10.0f   ? Q2M_MOVE_fixbot_move_pain3
           : damage <= 25.0f ? Q2M_MOVE_fixbot_move_painb
                             : Q2M_MOVE_fixbot_move_paina;
    sound = "flyer/flypain1.wav";
    break;
  default:
    if (monster->definition->pain3_move != Q2M_MOVE_NONE && random > 0.66f)
      move = monster->definition->pain3_move;
    else if (monster->definition->pain2_move != Q2M_MOVE_NONE && random > 0.33f)
      move = monster->definition->pain2_move;
    break;
  }
  bool nightmare_blocks =
      context->game->options.skill == 3 && species != Q2M_BOSS2;
  if (sound != NULL && !q2m_sound(context, sound, 2, 1.0f, error))
    return false;
  if (!q2m_alive(context) || nightmare_blocks || monster->combat_point)
    return true;
  monster->dodging = false;
  monster->ducked = false;
  monster->charging = false;
  monster->manual_steering = false;
  return move == Q2M_MOVE_NONE || q2m_set_move(context, move, true, error);
}

typedef struct q2m_gib_piece {
  const char *model;
  uint8_t count;
  uint8_t flags;
  float scale;
} q2m_gib_piece;

#define GIB(model_, count_, flags_)                                           \
  { model_, count_, flags_, 1.0f }
#define SCALED_GIB(model_, count_, flags_, scale_)                            \
  { model_, count_, flags_, scale_ }

static const q2m_gib_piece classic_standard_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 4, 0),
    GIB("models/objects/gibs/head2/tris.md2", 1, Q2_GIB_HEAD),
};
static const q2m_gib_piece classic_small_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 1, Q2_GIB_HEAD),
};
static const q2m_gib_piece classic_tank_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 1, 0),
    GIB("models/objects/gibs/sm_metal/tris.md2", 4, Q2_GIB_METALLIC),
    GIB("models/objects/gibs/chest/tris.md2", 1, 0),
    GIB("models/objects/gibs/gear/tris.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_METALLIC),
};
static const q2m_gib_piece classic_makron_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 1, 0),
    GIB("models/objects/gibs/sm_metal/tris.md2", 4, Q2_GIB_METALLIC),
    GIB("models/objects/gibs/gear/tris.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_METALLIC),
};
static const q2m_gib_piece classic_boss_final_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 4, 0),
    GIB("models/objects/gibs/sm_metal/tris.md2", 8, Q2_GIB_METALLIC),
    GIB("models/objects/gibs/chest/tris.md2", 1, 0),
    GIB("models/objects/gibs/gear/tris.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_METALLIC),
};
static const q2m_gib_piece expansion_soldier_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 3, 0),
    GIB("models/objects/gibs/chest/tris.md2", 1, 0),
    GIB("models/objects/gibs/head2/tris.md2", 1, Q2_GIB_HEAD),
};
static const q2m_gib_piece rerelease_soldier_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 3, 0),
    GIB("models/objects/gibs/bone2/tris.md2", 1, 0),
    GIB("models/objects/gibs/bone/tris.md2", 1, 0),
    GIB("models/monsters/soldier/gibs/arm.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/soldier/gibs/gun.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/soldier/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/soldier/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_infantry_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 1, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 3, 0),
    GIB("models/monsters/infantry/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/infantry/gibs/gun.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/infantry/gibs/foot.md2", 2, Q2_GIB_SKINNED),
    GIB("models/monsters/infantry/gibs/arm.md2", 2, Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_berserk_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 3, 0),
    GIB("models/objects/gibs/gear/tris.md2", 1, 0),
    GIB("models/monsters/berserk/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/berserk/gibs/hammer.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/berserk/gibs/thigh.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/berserk/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_brain_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 1, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/monsters/brain/gibs/arm.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/brain/gibs/boot.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/brain/gibs/door.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/brain/gibs/pelvis.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/brain/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/brain/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_chick_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 3, 0),
    GIB("models/monsters/bitch/gibs/arm.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/bitch/gibs/foot.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/bitch/gibs/tube.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/bitch/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/bitch/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_flipper_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/objects/gibs/head2/tris.md2", 1, Q2_GIB_HEAD),
};
static const q2m_gib_piece rerelease_floater_gibs[] = {
    GIB("models/objects/gibs/sm_metal/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 3, 0),
    GIB("models/monsters/float/gibs/piece.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/float/gibs/gun.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/float/gibs/base.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/float/gibs/jar.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_flyer_gibs[] = {
    GIB("models/objects/gibs/sm_metal/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/monsters/flyer/gibs/base.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/flyer/gibs/gun.md2", 2, Q2_GIB_SKINNED),
    GIB("models/monsters/flyer/gibs/wing.md2", 2, Q2_GIB_SKINNED),
    GIB("models/monsters/flyer/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_gladiator_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/monsters/gladiatr/gibs/thigh.md2", 2, Q2_GIB_SKINNED),
    GIB("models/monsters/gladiatr/gibs/larm.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/gladiatr/gibs/rarm.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/gladiatr/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/gladiatr/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_gunner_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/monsters/gunner/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/gunner/gibs/garm.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/gunner/gibs/gun.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/gunner/gibs/foot.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/gunner/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_hover_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_metal/tris.md2", 2, Q2_GIB_METALLIC),
    GIB("models/monsters/hover/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/hover/gibs/ring.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_METALLIC),
    GIB("models/monsters/hover/gibs/foot.md2", 2, Q2_GIB_SKINNED),
    GIB("models/monsters/hover/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_medic_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 1, 0),
    GIB("models/objects/gibs/sm_metal/tris.md2", 1, Q2_GIB_METALLIC),
    GIB("models/monsters/medic/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/medic/gibs/leg.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/medic/gibs/hook.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/medic/gibs/gun.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/medic/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_mutant_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 4, 0),
    GIB("models/monsters/mutant/gibs/hand.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/mutant/gibs/foot.md2", 2, Q2_GIB_SKINNED),
    GIB("models/monsters/mutant/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/mutant/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_parasite_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 1, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 3, 0),
    GIB("models/monsters/parasite/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/parasite/gibs/bleg.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/parasite/gibs/fleg.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/parasite/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_tank_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 1, 0),
    GIB("models/objects/gibs/sm_metal/tris.md2", 3, Q2_GIB_METALLIC),
    GIB("models/objects/gibs/gear/tris.md2", 1, Q2_GIB_METALLIC),
    GIB("models/monsters/tank/gibs/foot.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_METALLIC),
    GIB("models/monsters/tank/gibs/thigh.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_METALLIC),
    GIB("models/monsters/tank/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/tank/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_shambler_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 1, 0),
    GIB("models/objects/gibs/chest/tris.md2", 1, 0),
    GIB("models/objects/gibs/head2/tris.md2", 1, Q2_GIB_HEAD),
};
static const q2m_gib_piece rerelease_guncmdr_gibs[] = {
    GIB("models/objects/gibs/bone/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/objects/gibs/gear/tris.md2", 1, 0),
    GIB("models/monsters/gunner/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/gunner/gibs/garm.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/gunner/gibs/gun.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/gunner/gibs/foot.md2", 1, Q2_GIB_SKINNED),
};
static const q2m_gib_piece rerelease_supertank_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_metal/tris.md2", 2, Q2_GIB_METALLIC),
    GIB("models/monsters/boss1/gibs/cgun.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_METALLIC),
    GIB("models/monsters/boss1/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/boss1/gibs/core.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/boss1/gibs/ltread.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/boss1/gibs/rgun.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/boss1/gibs/rtread.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/boss1/gibs/tube.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/boss1/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED | Q2_GIB_METALLIC),
};
static const q2m_gib_piece rerelease_boss2_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_metal/tris.md2", 2, Q2_GIB_METALLIC),
    GIB("models/monsters/boss2/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/boss2/gibs/engine.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/boss2/gibs/spine.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/boss2/gibs/chaingun.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/boss2/gibs/cpu.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/boss2/gibs/rocket.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/boss2/gibs/wing.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    SCALED_GIB("models/monsters/boss2/gibs/larm.md2", 1,
               Q2_GIB_SKINNED | Q2_GIB_UPRIGHT, 1.0f),
    SCALED_GIB("models/monsters/boss2/gibs/rarm.md2", 1,
               Q2_GIB_SKINNED | Q2_GIB_UPRIGHT, 1.0f),
    SCALED_GIB("models/monsters/boss2/gibs/larm.md2", 1,
               Q2_GIB_SKINNED | Q2_GIB_UPRIGHT, 2.0f),
    SCALED_GIB("models/monsters/boss2/gibs/rarm.md2", 1,
               Q2_GIB_SKINNED | Q2_GIB_UPRIGHT, 2.0f),
    SCALED_GIB("models/monsters/boss2/gibs/larm.md2", 1,
               Q2_GIB_SKINNED | Q2_GIB_UPRIGHT, 1.35f),
    SCALED_GIB("models/monsters/boss2/gibs/rarm.md2", 1,
               Q2_GIB_SKINNED | Q2_GIB_UPRIGHT, 1.35f),
    GIB("models/monsters/boss2/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED | Q2_GIB_METALLIC),
};
static const q2m_gib_piece rerelease_jorg_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_metal/tris.md2", 2, Q2_GIB_METALLIC),
    GIB("models/monsters/boss3/jorg/gibs/chest.md2", 1, Q2_GIB_SKINNED),
    GIB("models/monsters/boss3/jorg/gibs/foot.md2", 2, Q2_GIB_SKINNED),
    GIB("models/monsters/boss3/jorg/gibs/tube.md2", 4, Q2_GIB_SKINNED),
    GIB("models/monsters/boss3/jorg/gibs/spike.md2", 6, Q2_GIB_SKINNED),
    GIB("models/monsters/boss3/jorg/gibs/gun.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/boss3/jorg/gibs/thigh.md2", 2,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/boss3/jorg/gibs/spine.md2", 1,
        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT),
    GIB("models/monsters/boss3/jorg/gibs/head.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_SKINNED | Q2_GIB_METALLIC),
};
static const q2m_gib_piece rerelease_guardian_gibs[] = {
    GIB("models/objects/gibs/sm_meat/tris.md2", 2, 0),
    GIB("models/objects/gibs/sm_metal/tris.md2", 4, Q2_GIB_METALLIC),
    GIB("models/monsters/guardian/gib1.md2", 2, Q2_GIB_METALLIC),
    GIB("models/monsters/guardian/gib2.md2", 2, Q2_GIB_METALLIC),
    GIB("models/monsters/guardian/gib3.md2", 2, Q2_GIB_METALLIC),
    GIB("models/monsters/guardian/gib4.md2", 2, Q2_GIB_METALLIC),
    GIB("models/monsters/guardian/gib5.md2", 2, Q2_GIB_METALLIC),
    GIB("models/monsters/guardian/gib6.md2", 2, Q2_GIB_METALLIC),
    GIB("models/monsters/guardian/gib7.md2", 1,
        Q2_GIB_HEAD | Q2_GIB_METALLIC),
};

#undef GIB
#undef SCALED_GIB

static bool spawn_gib_recipe(q2m_context *context,
                             const q2m_gib_piece *pieces, size_t piece_count,
                             float damage, qa_error *error) {
  const qa_actor_id source = context->actor->id;
  const int skin = context->monster->skin;
  const float scale = context->monster->entity_scale;
  for (size_t piece_index = 0; piece_index < piece_count; ++piece_index) {
    const q2m_gib_piece *piece = &pieces[piece_index];
    for (uint8_t copy = 0; copy < piece->count; ++copy)
      if (!q2_spawn_gib(context->game, source, piece->model, damage,
                        piece->flags, skin, scale * piece->scale, error))
        return false;
  }
  return true;
}

#define SPAWN_RECIPE(name_, damage_)                                         \
  spawn_gib_recipe(context, name_, sizeof(name_) / sizeof((name_)[0]),       \
                   damage_, error)

static bool spawn_death_gibs(q2m_context *context, float damage,
                             qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  q2m_species species = monster->definition->species;
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  if (!rerelease) {
    if (species == Q2M_TANK || species == Q2M_TANK_COMMANDER)
      return SPAWN_RECIPE(classic_tank_gibs, damage);
    if (species == Q2M_MAKRON)
      return SPAWN_RECIPE(classic_makron_gibs, damage);
    if (species == Q2M_FLIPPER || species == Q2M_HOVER ||
        species == Q2M_DAEDALUS)
      return SPAWN_RECIPE(classic_small_gibs, damage);
    if (species == Q2M_SOLDIER_RIPPER || species == Q2M_SOLDIER_HYPER ||
        species == Q2M_SOLDIER_LASER)
      return SPAWN_RECIPE(expansion_soldier_gibs, damage);
    return SPAWN_RECIPE(classic_standard_gibs, damage);
  }

  switch (species) {
  case Q2M_SOLDIER_LIGHT:
  case Q2M_SOLDIER:
  case Q2M_SOLDIER_SS:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_soldier_gibs, damage);
  case Q2M_INFANTRY:
  case Q2M_TURRET_DRIVER: {
    monster->skin /= 2;
    if (!SPAWN_RECIPE(rerelease_infantry_gibs, damage))
      return false;
    const char *head = monster->move != NULL &&
                               (monster->move->id == Q2M_MOVE_infantry_move_death3)
                           ? "models/monsters/infantry/gibs/head.md2"
                           : "models/objects/gibs/sm_meat/tris.md2";
    return q2_spawn_gib(context->game, context->actor->id, head, damage,
                        Q2_GIB_HEAD | Q2_GIB_SKINNED, monster->skin,
                        monster->entity_scale, error);
  }
  case Q2M_BERSERK:
    return SPAWN_RECIPE(rerelease_berserk_gibs, damage);
  case Q2M_BRAIN:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_brain_gibs, damage);
  case Q2M_CHICK:
  case Q2M_CHICK_HEAT:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_chick_gibs, damage);
  case Q2M_FLIPPER:
    return SPAWN_RECIPE(rerelease_flipper_gibs, damage);
  case Q2M_FLOATER:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_floater_gibs, damage);
  case Q2M_FLYER:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_flyer_gibs, damage);
  case Q2M_GLADIATOR:
  case Q2M_GLADB:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_gladiator_gibs, damage);
  case Q2M_GUNNER:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_gunner_gibs, damage);
  case Q2M_HOVER:
  case Q2M_DAEDALUS:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_hover_gibs, 150.0f);
  case Q2M_MAKRON:
    return SPAWN_RECIPE(classic_makron_gibs, damage);
  case Q2M_MEDIC:
  case Q2M_MEDIC_COMMANDER:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_medic_gibs, damage);
  case Q2M_MUTANT:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_mutant_gibs, damage);
  case Q2M_PARASITE:
    monster->skin /= 2;
    return SPAWN_RECIPE(rerelease_parasite_gibs, damage);
  case Q2M_TANK:
  case Q2M_TANK_COMMANDER:
    monster->skin /= 2;
    if (!SPAWN_RECIPE(rerelease_tank_gibs, damage))
      return false;
    return monster->style != 0 ||
           q2_spawn_gib(context->game, context->actor->id,
                        "models/monsters/tank/gibs/barm.md2", damage,
                        Q2_GIB_SKINNED | Q2_GIB_UPRIGHT, monster->skin,
                        monster->entity_scale, error);
  case Q2M_GUN_COMMANDER: {
    monster->skin /= 2;
    if (!SPAWN_RECIPE(rerelease_guncmdr_gibs, damage))
      return false;
    const char *head = monster->move != NULL &&
                               (monster->move->id == Q2M_MOVE_guncmdr_move_death5)
                           ? "models/monsters/gunner/gibs/head.md2"
                           : "models/objects/gibs/sm_meat/tris.md2";
    return q2_spawn_gib(context->game, context->actor->id, head, damage,
                        Q2_GIB_HEAD | Q2_GIB_SKINNED, monster->skin,
                        monster->entity_scale, error);
  }
  case Q2M_SHAMBLER:
    return SPAWN_RECIPE(rerelease_shambler_gibs, damage);
  case Q2M_ARACHNID:
  case Q2M_ACTOR:
  case Q2M_INSANE:
    return SPAWN_RECIPE(classic_standard_gibs, damage);
  default:
    return SPAWN_RECIPE(classic_standard_gibs, damage);
  }
}

#undef SPAWN_RECIPE

static bool emit_explosion(q2m_context *context, const char *effect,
                           qa_vec3 origin, int code, qa_error *error) {
  return q2m_emit(context, QA_BUILTIN_EXPLOSION, effect, code, origin, origin,
                  1.0f, error);
}

static qa_vec3 random_body_point(q2m_context *context) {
  qa_vec3 span = qa_vec_sub(context->body.bounds.maxs,
                            context->body.bounds.mins);
  return qa_vec_add(
      qa_vec_add(context->body.origin, context->body.bounds.mins),
      qa_v3(q2m_random(context->game) * span.x,
            q2m_random(context->game) * span.y,
            q2m_random(context->game) * span.z));
}

static bool classic_boss_explosion(q2m_context *context, qa_error *error) {
  static const qa_vec3 offsets[] = {
      {-24.0f, -24.0f, 0.0f}, {24.0f, 24.0f, 0.0f},
      {24.0f, -24.0f, 0.0f},  {-24.0f, 24.0f, 0.0f},
      {-48.0f, -48.0f, 0.0f}, {48.0f, 48.0f, 0.0f},
      {-48.0f, 48.0f, 0.0f},  {48.0f, -48.0f, 0.0f},
  };
  if (!q2m_alive(context))
    return true;
  struct qa_q2_monster *monster = context->monster;
  int index = monster->count++;
  if (index == (int)(sizeof(offsets) / sizeof(offsets[0]))) {
    monster->death_ns = 0;
    monster->dead = true;
    monster->gibbed = true;
    monster->can_take_damage = false;
    return spawn_gib_recipe(
        context, classic_boss_final_gibs,
        sizeof(classic_boss_final_gibs) / sizeof(classic_boss_final_gibs[0]),
        500.0f, error);
  }
  qa_vec3 offset = offsets[index];
  offset.z = 24.0f + floorf(q2m_random(context->game) * 16.0f);
  if (!emit_explosion(context, "q2:explosion1",
                      qa_vec_add(context->body.origin, offset), index, error))
    return false;
  if (q2m_alive(context))
    monster->death_ns = q2m_after(context->game->now_ns, 0.1);
  return true;
}

bool q2m_start_boss_explosion(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  if (context->game->options.edition == QA_Q2_RERELEASE) {
    if ((context->monster->spawnflags & UINT32_C(65536)) != 0)
      return true;
    return q2m_spawn_boss_exploder(context, error);
  }
  if (context->monster->death_ns != 0)
    return true;
  return classic_boss_explosion(context, error);
}

bool q2m_boss_explosion_tick(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context) || context->monster->death_ns == 0 ||
      context->game->now_ns < context->monster->death_ns)
    return true;
  return classic_boss_explosion(context, error);
}

bool q2m_finish_boss_death(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  struct qa_q2_monster *monster = context->monster;
  q2m_species species = monster->definition->species;
  if (context->game->options.edition != QA_Q2_RERELEASE)
    return species == Q2M_JORG ? true : q2m_corpse(context, error);

  if ((species == Q2M_SUPERTANK || species == Q2M_BOSS5 ||
       species == Q2M_BOSS2) &&
      (monster->spawnflags & UINT32_C(65536)) != 0) {
    monster->dead = false;
    monster->can_take_damage = true;
    return q2m_damageable(context, true, error);
  }

  const q2m_gib_piece *pieces = NULL;
  size_t piece_count = 0;
  float damage = 500.0f;
  switch (species) {
  case Q2M_SUPERTANK:
  case Q2M_BOSS5:
    pieces = rerelease_supertank_gibs;
    piece_count = sizeof(rerelease_supertank_gibs) /
                  sizeof(rerelease_supertank_gibs[0]);
    monster->skin /= 2;
    break;
  case Q2M_BOSS2:
    pieces = rerelease_boss2_gibs;
    piece_count = sizeof(rerelease_boss2_gibs) /
                  sizeof(rerelease_boss2_gibs[0]);
    monster->skin /= 2;
    context->actor->physics.gravity_direction = qa_v3(0, 0, -1);
    break;
  case Q2M_JORG:
    pieces = rerelease_jorg_gibs;
    piece_count = sizeof(rerelease_jorg_gibs) /
                  sizeof(rerelease_jorg_gibs[0]);
    monster->skin /= 2;
    break;
  case Q2M_GUARDIAN:
    for (int explosion = 0; explosion < 3; ++explosion) {
      if (!emit_explosion(context, "q2:explosion1-big",
                          random_body_point(context), explosion, error))
        return false;
      if (!q2m_alive(context))
        return true;
    }
    pieces = rerelease_guardian_gibs;
    piece_count = sizeof(rerelease_guardian_gibs) /
                  sizeof(rerelease_guardian_gibs[0]);
    damage = 125.0f;
    break;
  default:
    return q2m_corpse(context, error);
  }
  if (species != Q2M_GUARDIAN &&
      !emit_explosion(context, "q2:explosion1-big", context->body.origin, 0,
                      error))
    return false;
  if (!q2m_alive(context))
    return true;
  monster->dead = true;
  monster->gibbed = true;
  monster->can_take_damage = false;
  return spawn_gib_recipe(context, pieces, piece_count, damage, error);
}

static bool rerelease_flying_explosion(q2m_context *context,
                                       qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  const bool floater = monster->definition->species == Q2M_FLOATER;
  monster->dead = true;
  monster->gibbed = true;
  monster->can_take_damage = false;
  monster->touch_active = false;
  if (!q2m_damageable(context, false, error) ||
      !q2m_sound(context,
                 floater ? "floater/fltdeth1.wav" : "flyer/flydeth1.wav", 2,
                 1.0f, error) ||
      !emit_explosion(context, "q2:explosion1", context->body.origin, 0,
                      error))
    return false;
  if (!q2m_alive(context))
    return true;
  monster->skin /= 2;
  return spawn_gib_recipe(
      context, floater ? rerelease_floater_gibs : rerelease_flyer_gibs,
      floater ? sizeof(rerelease_floater_gibs) /
                    sizeof(rerelease_floater_gibs[0])
              : sizeof(rerelease_flyer_gibs) /
                    sizeof(rerelease_flyer_gibs[0]),
      55.0f, error);
}

static bool kill_widow2_stalkers(q2m_context *context, qa_error *error) {
  qa_builtin_snapshot_frame *scratch = q2_scratch_acquire(context->game, error);
  if (scratch == NULL)
    return false;
  if (scratch->snapshot.capacity < context->game->capacity &&
      !qa_builtin_snapshot_reserve(&scratch->snapshot, context->game->capacity,
                                   error)) {
    qa_builtin_snapshot_release(scratch);
    return false;
  }
  scratch->snapshot.count = 0;
  for (q2_actor *actor = context->game->first_actor; actor != NULL;
       actor = actor->live_next)
    scratch->snapshot.ids[scratch->snapshot.count++] = actor->id;

  qa_vec3 point = context->body.origin;
  qa_body_state enemy_body;
  qa_error ignored = {0};
  if (context->monster->enemy.registry != 0 &&
      qa_world_body_read(context->game->services.world,
                         context->monster->enemy, &enemy_body, &ignored))
    point = enemy_body.origin;

  if (!q2m_alive(context)) {
    qa_builtin_snapshot_release(scratch);
    return true;
  }

  bool result = true;
  for (size_t index = 0; index < scratch->snapshot.count; ++index) {
    qa_actor_id id = scratch->snapshot.ids[index];
    if (id.slot >= context->game->capacity ||
        !q2_actor_live(context->game, id))
      continue;
    q2_actor *actor = context->game->actors[id.slot];
    if (actor == NULL || actor->monster == NULL ||
        actor->projectile.kind != Q2_PROJECTILE_NONE ||
        actor->monster->definition == NULL ||
        actor->monster->definition->species != Q2M_STALKER)
      continue;
    qa_combat_state combat;
    if (!qa_combat_read(context->game->services.combat, id, &combat, error)) {
      result = false;
      break;
    }
    if (!q2m_alive(context))
      break;
    if (!q2_actor_live(context->game, id) || combat.health <= 0.0f)
      continue;
    qa_attack attack = {
        .attacker = context->actor->id,
        .inflictor = context->actor->id,
        .combat_provider = context->game->options.owner,
        .cause = qa_q2_damage_cause(context->game->options.edition,
                                    context->game->options.product, 0, 8),
    };
    if (!q2_damage(context->game, &attack, id, combat.health + 1.0f, 0.0f,
                   qa_v3(0, 0, 0), point, qa_v3(0, 0, 0), false, error)) {
      result = false;
      break;
    }
    if (!q2m_alive(context))
      break;
  }
  qa_builtin_snapshot_release(scratch);
  return result;
}

static bool widow_death(q2m_context *context, bool sequel, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (sequel && context->combat.health <= monster->gib_health) {
    monster->dead = true;
    monster->gibbed = true;
    monster->can_take_damage = false;
    context->combat.can_take_damage = false;
    float damage = fminf(monster->pending_damage, 100.0f);
    if (!q2m_sound(context, "misc/udeath.wav", 2, 1.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
    return q2m_widow_death_gibs(context, damage, error);
  }
  if (monster->dead)
    return true;

  q2m_widow_clear_powerups(context);
  monster->dead = true;
  monster->touch_active = false;
  monster->can_take_damage = false;
  monster->count = 0;
  context->actor->physics.flags |= QA_PHYSICS_DEAD;
  if (!q2m_damageable(context, false, error))
    return false;
  if (sequel && !q2m_sound(context, "widow/death.wav", 2, 1.0f, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (sequel && !kill_widow2_stalkers(context, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!q2m_emit(context, QA_BUILTIN_DEATH, NULL, 0, context->body.origin,
                context->body.origin, monster->pending_damage, error))
    return false;
  return !q2m_alive(context) ||
         q2m_set_move(context,
                      sequel ? Q2M_MOVE_widow2_move_death : Q2M_MOVE_widow_move_death, true,
                      error);
}

bool q2m_die(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  struct qa_q2_monster *monster = context->monster;
  if (monster->gibbed)
    return true;
  if ((monster->definition->species == Q2M_SHAMBLER ||
       monster->definition->species == Q2M_TURRET) &&
      !q2m_source_visuals_release(context, error)) return false;
  if (!q2m_alive(context)) return true;
  if (monster->definition->species == Q2M_SOLDIER_LIGHT ||
      monster->definition->species == Q2M_SOLDIER ||
      monster->definition->species == Q2M_SOLDIER_SS ||
      monster->definition->species == Q2M_SOLDIER_RIPPER ||
      monster->definition->species == Q2M_SOLDIER_HYPER ||
      monster->definition->species == Q2M_SOLDIER_LASER) {
    if (!q2m_soldier_sound_end(context, error)) return false;
    if (!q2m_alive(context)) return true;
  }

  if (!q2m_medic_died(context, error))
    return false;
  if (!q2m_alive(context))
    return true;

  if (!q2m_lifecycle_killed(context, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (monster->definition->species == Q2M_PARASITE &&
      context->game->options.edition == QA_Q2_RERELEASE) {
    if (!q2m_parasite_interrupt(context, true, error))
      return false;
    if (!q2m_alive(context))
      return true;
  }

  if (context->game->options.edition == QA_Q2_RERELEASE &&
      (monster->definition->species == Q2M_FLOATER ||
       monster->definition->species == Q2M_FLYER))
    return rerelease_flying_explosion(context, error);

  if (monster->definition->species == Q2M_TURRET_DRIVER &&
      monster->turret_attached) {
    if (!qa_q2_turret_driver_detach(context->game, context->actor->id, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!q2m_refresh(context, error))
      return false;
    monster = context->monster;
  }

  bool crushed = monster->last_attack.cause.kind == QA_CAUSE_Q2 &&
                 monster->last_attack.cause.source.q2.means_of_death == 20;
  q2m_species species = monster->definition->species;
  if (species == Q2M_JORG) {
    if (!q2m_jorg_sound_end(context, error)) return false;
    if (!q2m_alive(context)) return true;
  }
  if (species == Q2M_GUARDIAN) {
    if (!stop_loop_sound(context, "weapons/hyprbl1a.wav", 0, error)) return false;
    if (!q2m_alive(context)) return true;
    monster->dead = true;
    context->actor->physics.flags |= QA_PHYSICS_DEAD;
    if (!q2m_damageable(context, true, error)) return false;
    return !q2m_alive(context) || q2m_set_move(context, Q2M_MOVE_guardian_move_death, true, error);
  }
  if (species == Q2M_WIDOW || species == Q2M_WIDOW2)
    return widow_death(context, species == Q2M_WIDOW2, error);
  bool scripted_boss =
      species == Q2M_SUPERTANK || species == Q2M_BOSS5 ||
      species == Q2M_BOSS2 || species == Q2M_JORG || species == Q2M_CARRIER;
  bool special_boss_gib =
      context->game->options.edition == QA_Q2_RERELEASE &&
      (monster->spawnflags & UINT32_C(65536)) != 0 &&
      (species == Q2M_SUPERTANK || species == Q2M_BOSS5 ||
       species == Q2M_BOSS2);
  bool gib = (monster->definition->flags & Q2M_NO_GIB) == 0 &&
             (!scripted_boss || special_boss_gib) &&
             (context->combat.health <= monster->gib_health ||
              (context->game->options.edition == QA_Q2_RERELEASE &&
               monster->dead && crushed));
  if (gib) {
    monster->dead = true;
    monster->gibbed = true;
    monster->can_take_damage = false;
    if (!q2m_sound(context, "misc/udeath.wav", 2, 1.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (special_boss_gib) {
      if (!emit_explosion(context, "q2:explosion1-big", context->body.origin,
                          0, error))
        return false;
      if (!q2m_alive(context))
        return true;
      monster->skin /= 2;
      const q2m_gib_piece *pieces = species == Q2M_BOSS2
                                        ? rerelease_boss2_gibs
                                        : rerelease_supertank_gibs;
      size_t count = species == Q2M_BOSS2
                         ? sizeof(rerelease_boss2_gibs) /
                               sizeof(rerelease_boss2_gibs[0])
                         : sizeof(rerelease_supertank_gibs) /
                               sizeof(rerelease_supertank_gibs[0]);
      return spawn_gib_recipe(context, pieces, count, 500.0f, error);
    }
    return spawn_death_gibs(context,
                            fmaxf(1.0f, monster->pending_damage), error);
  }
  if (monster->dead)
    return true;

  if (species == Q2M_MEDIC || species == Q2M_MEDIC_COMMANDER) {
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    bool commander = (rerelease || context->game->options.product == QA_Q2_ROGUE ||
                      species == Q2M_MEDIC_COMMANDER) &&
                     context->combat.mass != 400;
    if (!q2m_sound(context, commander ? "medic_commander/meddeth.wav"
                                     : "medic/meddeth1.wav", 2, 1, error))
      return false;
    if (!q2m_alive(context))
      return true;
    monster->dead = true;
    monster->touch_active = false;
    context->actor->physics.flags |= QA_PHYSICS_DEAD;
    if (!q2m_damageable(context, true, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!q2m_emit(context, QA_BUILTIN_DEATH, NULL, 0, context->body.origin,
                  context->body.origin, monster->pending_damage, error))
      return false;
    return !q2m_alive(context) ||
           q2m_set_move(context, Q2M_MOVE_medic_move_death, true, error);
  }

  bool damageable_corpse = species != Q2M_SUPERTANK && species != Q2M_BOSS5 &&
                           species != Q2M_BOSS2 && species != Q2M_CARRIER &&
                           species != Q2M_JORG && species != Q2M_WIDOW &&
                           species != Q2M_WIDOW2 && species != Q2M_GUARDIAN;
  monster->dead = true;
  monster->touch_active = false;
  monster->can_take_damage = damageable_corpse;
  monster->enemy = monster->last_attack.attacker;
  context->actor->physics.flags |= QA_PHYSICS_DEAD;
  context->combat.can_take_damage = damageable_corpse;
  q2m_move_id move = monster->definition->death1_move;
  const char *sound = death_sound(species);
  char death_path[sizeof("player/male/death4.wav")];
  switch (species) {
  case Q2M_BERSERK:
    move = monster->pending_damage >= 50.0f ? Q2M_MOVE_berserk_move_death1
                                            : Q2M_MOVE_berserk_move_death2;
    break;
  case Q2M_BRAIN:
    move = q2m_random(context->game) <= 0.5f ? Q2M_MOVE_brain_move_death1
                                             : Q2M_MOVE_brain_move_death2;
    break;
  case Q2M_CHICK:
  case Q2M_CHICK_HEAT:
    if (q2m_random(context->game) < 0.5f) {
      move = Q2M_MOVE_chick_move_death1;
      sound = "chick/chkdeth1.wav";
    } else {
      move = Q2M_MOVE_chick_move_death2;
      sound = "chick/chkdeth2.wav";
    }
    break;
  case Q2M_FLOATER:
    sound = "floater/fltdeth1.wav";
    break;
  case Q2M_FLYER:
  case Q2M_KAMIKAZE:
    sound = "flyer/flydeth1.wav";
    break;
  case Q2M_HOVER:
  case Q2M_DAEDALUS:
    sound = q2m_random(context->game) < 0.5f ? "hover/hovdeth1.wav"
                                             : "hover/hovdeth2.wav";
    break;
  case Q2M_JORG:
    sound = "boss3/bs3deth1.wav";
    break;
  case Q2M_MAKRON:
    sound = "makron/death.wav";
    break;
  case Q2M_MUTANT:
    monster->skin = 1;
    move = q2m_random(context->game) < 0.5f ? Q2M_MOVE_mutant_move_death1
                                            : Q2M_MOVE_mutant_move_death2;
    break;
  case Q2M_SUPERTANK:
  case Q2M_BOSS5:
    sound = "bosstank/btkdeth1.wav";
    break;
  case Q2M_TANK:
  case Q2M_TANK_COMMANDER:
    sound = "tank/death.wav";
    break;
  case Q2M_BOSS2:
    sound = "bosshovr/bhvdeth1.wav";
    break;
  case Q2M_CARRIER:
    sound = "carrier/death.wav";
    break;
  case Q2M_ACTOR:
    move = q2m_random(context->game) < 0.5f ? Q2M_MOVE_actor_move_death1
                                            : Q2M_MOVE_actor_move_death2;
    sound = NULL;
    break;
  case Q2M_INSANE: {
    move = (monster->frame >= 99 && monster->frame <= 159) ||
                   (monster->frame >= 227 && monster->frame <= 235)
               ? Q2M_MOVE_insane_move_crawl_death
               : Q2M_MOVE_insane_move_stand_death;
    unsigned variant = 1u + (unsigned)floorf(q2m_random(context->game) * 4.0f);
    if (variant > 4u)
      variant = 4u;
    snprintf(death_path, sizeof(death_path), "player/male/death%u.wav",
             variant);
    sound = death_path;
    break;
  }
  case Q2M_GEKK:
    monster->skin = 2;
    if (monster->water_level > 0)
      move = Q2M_MOVE_gekk_move_wdeath;
    else {
      float choice = q2m_random(context->game);
      move = choice > 0.66f   ? Q2M_MOVE_gekk_move_death1
             : choice > 0.33f ? Q2M_MOVE_gekk_move_death3
             : q2m_move_find(monster, Q2M_MOVE_gekk_move_death4) != NULL
                 ? Q2M_MOVE_gekk_move_death4
                 : Q2M_MOVE_gekk_move_death3;
    }
    sound = "gek/gk_deth1.wav";
    break;
  default:
    if (monster->definition->death2_move != Q2M_MOVE_NONE &&
        q2m_random(context->game) < 0.5f)
      move = monster->definition->death2_move;
    break;
  }
  if (!q2m_damageable(context, damageable_corpse, error) ||
      (sound != NULL && !q2m_sound(context, sound, 2, 1.0f, error)) ||
      !q2m_emit(context, QA_BUILTIN_DEATH, NULL, 0, context->body.origin,
                context->body.origin, monster->pending_damage, error))
    return false;
  if (!q2m_alive(context))
    return true;

  if (species == Q2M_FLOATER || species == Q2M_FLYER || species == Q2M_FIXBOT ||
      species == Q2M_KAMIKAZE) {
    if (!q2m_emit(context, QA_BUILTIN_EXPLOSION, "q2:explosion1", 0,
                  context->body.origin, context->body.origin, 1.0f, error))
      return false;
    return q2m_release(context, error);
  }

  return move == Q2M_MOVE_NONE ? q2m_corpse(context, error)
                      : q2m_set_move(context, move, true, error);
}

bool q2m_hover_explode(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  if (!emit_explosion(context, "q2:explosion1", context->body.origin, 0, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (context->game->options.edition != QA_Q2_RERELEASE ||
      context->monster->definition->species == Q2M_DAEDALUS)
    return q2m_release(context, error);
  if (!spawn_death_gibs(context, 150, error))
    return false;
  if (q2m_alive(context)) {
    context->monster->dead = true;
    context->monster->gibbed = true;
    context->monster->corpse_phase = Q2M_CORPSE_IDLE;
    context->monster->corpse_due_ns = 0;
  }
  return true;
}

bool q2m_release(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  return qa_session_release(context->game->services.session, context->actor->id,
                            error);
}

static qa_attack environmental_attack(q2m_context *context, int means,
                                      qa_hazard hazard) {
  qa_actor_id world = context->game->services.physics != NULL
                          ? context->game->services.physics->world_actor
                          : (qa_actor_id){0};
  qa_attack attack = {
      .sequence = 0,
      .time_ns = context->game->now_ns,
      .attacker = world,
      .inflictor = world,
      .combat_provider = context->game->options.owner,
  };
  attack.cause.kind =
      means == Q2M_MOD_WATER || means == Q2M_MOD_SLIME || means == Q2M_MOD_LAVA
          ? QA_CAUSE_Q2
          : QA_CAUSE_ENVIRONMENT;
  if (attack.cause.kind == QA_CAUSE_Q2)
    attack.cause = qa_q2_damage_cause(context->game->options.edition,
                                      context->game->options.product, means, 0);
  else
    attack.cause.source.hazard = hazard;
  return attack;
}

bool q2m_world_effects(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  struct qa_q2_monster *monster = context->monster;
  int water = context->actor->physics.water_level;
  int contents = context->actor->physics.water_type;
  if (context->combat.health > 0.0f) {
    bool can_breathe =
        monster->definition->locomotion == Q2M_SWIM ? water > 0 : water < 3;
    if (can_breathe)
      monster->air_ns =
          q2m_after(context->game->now_ns,
                    monster->definition->locomotion == Q2M_SWIM ? 9.0 : 12.0);
    else if (context->game->now_ns >= monster->air_ns &&
             context->game->now_ns >= monster->pain_ns) {
      float elapsed =
          (float)((context->game->now_ns - monster->air_ns) / Q2M_SECOND);
      float damage = fminf(15.0f, 2.0f + 2.0f * floorf(elapsed));
      qa_attack attack =
          environmental_attack(context, Q2M_MOD_WATER, QA_HAZARD_DROWN);
      monster->pain_ns = q2m_after(context->game->now_ns, 1.0);
      if (!q2_damage(context->game, &attack, context->actor->id, damage, 0.0f,
                     qa_v3(0, 0, 0), context->body.origin, qa_v3(0, 0, 0),
                     false, error))
        return false;
    }
  }
  if (!q2m_alive(context) || water == 0 ||
      context->game->now_ns < monster->environment_ns)
    return true;
  int means = 0;
  float damage = 0.0f;
  if (((uint32_t)contents & 8u) != 0) {
    means = Q2M_MOD_LAVA;
    damage = 10.0f * (float)water;
    monster->environment_ns = q2m_after(context->game->now_ns, 0.2);
  } else if (((uint32_t)contents & 16u) != 0) {
    means = Q2M_MOD_SLIME;
    damage = 4.0f * (float)water;
    monster->environment_ns = q2m_after(context->game->now_ns, 1.0);
  }
  if (means == 0)
    return true;
  qa_attack attack = environmental_attack(
      context, means, means == Q2M_MOD_LAVA ? QA_HAZARD_LAVA : QA_HAZARD_SLIME);
  return q2_damage(context->game, &attack, context->actor->id, damage, 0.0f,
                   qa_v3(0, 0, 0), context->body.origin, qa_v3(0, 0, 0), false,
                   error);
}

bool q2m_kamikaze(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  q2_weapon_call call = monster_weapon(context, QA_Q2_ROCKETLAUNCHER);
  qa_attack attack = q2_attack(&call, Q2M_MOD_ROCKET_SPLASH, 1u);
  qa_builtin_radius radius = {
      .attack = attack,
      .origin = context->body.origin,
      .radius = 150.0f,
      .damage = 150.0f,
      .distance_scale = 1.0f,
      .self_scale = 0.0f,
      .knockback_scale = 1.0f,
      .ignore = context->actor->id,
      .visibility_pass = context->actor->id,
      .trace = qa_collision_default_policy(QA_COLLISION_Q2),
      .distance = QA_RADIUS_CENTER,
      .check_visibility = true,
      .context = context->game,
      .prepare = q2_prepare_radius_damage,
  };
  size_t damaged = 0;
  if (!q2_radius_damage(context->game, &radius, &damaged, error) ||
      !q2m_emit(context, QA_BUILTIN_EXPLOSION, "q2:explosion1", 0,
                context->body.origin, context->body.origin, 1.0f, error))
    return false;
  return q2m_alive(context) ? q2m_release(context, error) : true;
}

static bool touch_damage(q2m_context *context, qa_actor_id target,
                         int canonical_mod, float damage, float kick,
                         qa_vec3 direction, qa_vec3 point, qa_vec3 normal,
                         bool *hit, qa_error *error) {
  if (hit != NULL)
    *hit = false;
  qa_combat_state combat;
  qa_error ignored = {0};
  if (!qa_combat_read(context->game->services.combat, target, &combat,
                      &ignored) ||
      !combat.can_take_damage)
    return true;
  qa_attack attack = {
      .attacker = context->actor->id,
      .inflictor = context->actor->id,
      .combat_provider = context->game->options.owner,
      .cause =
          qa_q2_damage_cause(context->game->options.edition,
                             context->game->options.product, canonical_mod, 0),
  };
  if (!q2_damage(context->game, &attack, target, damage, kick, direction, point,
                 normal, false, error))
    return false;
  if (hit != NULL)
    *hit = true;
  return true;
}

static bool berserk_slam_damage(q2m_context *context, qa_vec3 point,
                                qa_error *error) {
  qa_builtin_snapshot_frame *nearby =
      q2_nearby(context->game, context->body.origin, 330.0f, error);
  if (nearby == NULL)
    return false;
  bool result = true;
  for (size_t index = 0; index < nearby->snapshot.count; ++index) {
    qa_actor_id target = nearby->snapshot.ids[index];
    if (qa_actor_id_equal(target, context->actor->id))
      continue;
    qa_combat_state combat;
    qa_body_state body;
    qa_error ignored = {0};
    if (!qa_combat_read(context->game->services.combat, target, &combat,
                        &ignored) ||
        !combat.can_take_damage ||
        !qa_world_body_read(context->game->services.world, target, &body,
                            &ignored))
      continue;
    qa_builtin_actor_traits traits = {0};
    if (context->game->services.actor_traits != NULL)
      context->game->services.actor_traits(context->game->services.context,
                                           target, &traits);
    if (!q2m_alive(context))
      break;
    if (traits.player && body.ground.registry == 0)
      continue;

    qa_trace_policy visibility = qa_collision_default_policy(QA_COLLISION_Q2);
    visibility.contents_mask = 1u;
    bool visible = false;
    if (!qa_builtin_can_damage(&context->game->services, context->body.origin,
                               target, context->actor->id, visibility, false,
                               &visible, error)) {
      result = false;
      break;
    }
    if (!q2m_alive(context))
      break;
    if (!visible)
      continue;

    qa_vec3 minimum = qa_vec_add(body.origin, body.bounds.mins);
    qa_vec3 maximum = qa_vec_add(body.origin, body.bounds.maxs);
    qa_vec3 closest = qa_v3(fmaxf(minimum.x, fminf(maximum.x, point.x)),
                            fmaxf(minimum.y, fminf(maximum.y, point.y)),
                            fmaxf(minimum.z, fminf(maximum.z, point.z)));
    float amount =
        fminf(1.0f, 1.0f - qa_vec_length(qa_vec_sub(closest, point)) / 165.0f);
    if (amount <= 0.0f)
      continue;
    qa_vec3 direction = qa_vec_normalize(qa_vec_sub(body.origin, point));
    point.z = minimum.z;
    float damage = truncf(fmaxf(1.0f, 8.0f * amount * amount));
    float kick = truncf(300.0f * amount * amount);
    if (!touch_damage(context, target, Q2M_MOD_UNKNOWN, damage, kick, direction,
                      point, direction, NULL, error)) {
      result = false;
      break;
    }
    if (!q2m_alive(context))
      break;
    if (traits.player &&
        qa_world_body_read(context->game->services.world, target, &body,
                           &ignored) &&
        body.velocity.z < 270.0f) {
      body.velocity.z = 270.0f;
      if (!qa_world_body_write(context->game->services.world, target, &body,
                               error)) {
        result = false;
        break;
      }
    }
  }
  qa_builtin_snapshot_release(nearby);
  return result;
}

bool q2m_berserk_land(q2m_context *context, qa_error *error) {
  qa_vec3 forward, right, up;
  qa_builtin_angle_vectors(context->body.angles, &forward, &right, &up);
  qa_vec3 impact = qa_vec_add(context->body.origin,
                              qa_vec_add(qa_vec_scale(forward, 20.0f),
                                         qa_vec_add(qa_vec_scale(right, -14.3f),
                                                    qa_vec_scale(up, -21.0f))));
  qa_trace_query query = {
      .start = context->body.origin,
      .end = impact,
      .pass_actor = context->actor->id,
      .policy = qa_collision_default_policy(QA_COLLISION_Q2),
  };
  query.policy.contents_mask = 3u;
  qa_trace_result trace;
  if (!qa_world_trace(context->game->services.world, &query, &trace, error))
    return false;
  context->monster->touch_active = false;
  context->monster->ducked = false;
  context->monster->jump_ns = 0;
  context->monster->frame = 163;
  context->actor->physics.gravity_scale = 1.0f;
  context->body.velocity = qa_v3(0, 0, 0);
  if (!q2m_write_body(context, true, error) ||
      !q2m_sound(context, "mutant/thud1.wav", 1, 1.0f, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!q2m_sound(context, "world/explod2.wav", 0, 0.75f, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!q2m_emit(context, QA_BUILTIN_EXPLOSION, "q2:berserk-slam", 0, trace.end,
                qa_vec_add(trace.end, qa_v3(0, 0, 1)), 1.0f, error))
    return false;
  return !q2m_alive(context) || berserk_slam_damage(context, trace.end, error);
}

bool q2m_touch(q2m_context *context, const qa_touch_contact *contact,
               qa_error *error) {
  if (!q2m_alive(context) || contact == NULL || contact->other.registry == 0 ||
      context->monster->dead)
    return true;
  struct qa_q2_monster *monster = context->monster;
  if (monster->definition->species == Q2M_KAMIKAZE)
    return q2m_kamikaze(context, error);
  if (!monster->touch_active)
    return true;

  q2m_species species = monster->definition->species;
  if (species == Q2M_BERSERK) {
    if (context->game->options.edition == QA_Q2_RERELEASE &&
        context->body.ground.registry != 0)
      return q2m_berserk_land(context, error);
    return true;
  }
  if (species != Q2M_MUTANT && species != Q2M_GEKK)
    return true;

  float speed = qa_vec_length(context->body.velocity);
  float threshold = species == Q2M_GEKK ? 200.0f
                    : context->game->options.edition == QA_Q2_RERELEASE
                        ? 30.0f
                        : 400.0f;
  if (speed > threshold && (species != Q2M_MUTANT ||
                            context->game->options.edition != QA_Q2_RERELEASE ||
                            monster->style == 1)) {
    qa_vec3 normal = qa_vec_normalize(context->body.velocity);
    qa_vec3 point =
        qa_vec_add(context->body.origin,
                   qa_vec_scale(normal, context->body.bounds.maxs.x));
    float damage = truncf((species == Q2M_GEKK ? 10.0f : 40.0f) +
                          10.0f * q2m_random(context->game));
    bool hit;
    if (!touch_damage(
            context, contact->other, species == Q2M_GEKK ? 38 : Q2M_MOD_UNKNOWN,
            damage, damage, context->body.velocity, point, normal, &hit, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (hit && species == Q2M_MUTANT &&
        context->game->options.edition == QA_Q2_RERELEASE)
      monster->style = 0;
  }

  bool supported = false;
  if (!qa_physics_check_bottom(context->game->services.physics,
                               context->actor->id, context->body.origin,
                               &supported, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!supported) {
    if (context->body.ground.registry != 0) {
      monster->next_frame = species == Q2M_GEKK ? 81 : 1;
      monster->touch_active = false;
    }
    return true;
  }
  monster->touch_active = false;
  return true;
}
