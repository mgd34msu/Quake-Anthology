/* Port of src/content/q2/missionpacks/monsters/fixbot.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/fixbot.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/missionpacks/monsters/dabeam.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_fixbot.h"

typedef struct before_entry {
  qa_owned_actor actor;
  qa_vec3 origin;
} before_entry;
typedef struct fixbot_bundle {
  qa_q2m_monsters *monsters;
  before_entry *before; size_t before_count, before_cap;
} fixbot_bundle;
static void run_move(qa_q2m_context *c) {
  q2mpm_set_move(c, c->state->stand_ground ? "fixbot_move_stand" : "fixbot_move_run");
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static qa_q2f_entity *goal_of(qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity->has_goal ? c->game->entity(c->game->ctx, &c->entity->goal) : NULL;
  if (!entity) qa_q2f_error("Fixbot source movement requires its bot goal");
  return entity;
}
static double turn_toward(qa_q2m_context *c) {
  qa_vec3 direction = qa_sub3_double(q2mpm_body(c->game, goal_of(c)).origin, q2mpm_body(c->game, c->entity).origin);
  c->state->ideal_yaw = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  qa_q2m_change_yaw(c);
  return trunc(qa_hypot3(direction));
}
static void leave_goal(qa_q2m_context *c) {
  qa_q2f_entity *target = goal_of(c);
  c->game->schedule(c->game->ctx, target, 0.1, qa_q2f_free_entity_think());
  c->entity->has_goal = false;
  c->entity->has_enemy = false;
  q2mpm_set_move(c, "fixbot_move_stand");
}
static void vertical_goal(qa_q2m_context *c, bool landing) {
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_entity *target = game->create(game->ctx, "bot_goal", NULL);
  qa_body_state body = q2mpm_body(game, entity);
  target->has_owner = true;
  target->owner = entity->actor.id;
  game->solid(game->ctx, target, QA_Q2F_SOLID_BOX);
  qa_bounds bounds = {{-32, -32, -24}, {32, 32, 24}};
  qa_vec3 end = qa_add3_double(body.origin,
      qa_scale3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).up, landing ? -8096 : 128));
  qa_trace_result trace = q2mpm_trace(game, body.origin, end, &bounds, &entity->actor.id,
                                      qa_q2m_monster_solid_mask(game));
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = trace.end;
  change.has_bounds = true; change.bounds = bounds;
  q2mpm_move_body(game, target, change, true, false);
  entity->has_goal = true; entity->goal = target->actor.id;
  entity->has_enemy = true; entity->enemy = target->actor.id;
  q2mpm_set_move(c, landing ? "fixbot_move_landing" : "fixbot_move_takeoff");
}
static bool search_dead(fixbot_bundle *bundle, qa_q2m_context *c) {
  if (c->entity->has_goal) return false;
  qa_q2f_entity *best = NULL;
  size_t count = 0;
  const qa_actor_id *nearby = c->game->host->nearby(c->game->host->ctx, q2mpm_body(c->game, c->entity).origin, 1024, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = nearby[i];
    qa_q2f_entity *candidate = c->game->entity(c->game->ctx, &actor);
    qa_q2m_context *candidate_context = qa_q2m_monsters_context(bundle->monsters, actor);
    if (!candidate || candidate == c->entity || ((uint32_t)candidate->server_flags & 4u) == 0
        || (candidate_context && candidate_context->state->good_guy) || candidate->has_owner
        || qa_q2m_health(c->game, &actor) > 0 || candidate->has_next_think || !qa_q2m_visible(c, &actor)) continue;
    if (!best || candidate->max_health > best->max_health) best = candidate;
  }
  if (!best) return false;
  c->state->has_old_enemy = c->entity->has_enemy;
  c->state->old_enemy = c->entity->enemy;
  c->entity->has_enemy = true;
  c->entity->enemy = best->actor.id;
  best->has_owner = true;
  best->owner = c->entity->actor.id;
  c->state->medic = true;
  qa_q2m_monsters_found_target(bundle->monsters, c);
  return true;
}
static void attack_move(fixbot_bundle *bundle, qa_q2m_context *c) {
  (void)bundle;
  if (c->state->medic) {
    if (!qa_q2m_visible(c, c->entity->has_goal ? &c->entity->goal : NULL)) return;
    qa_body_state_result enemy;
    memset(&enemy, 0, sizeof enemy);
    if (c->entity->has_enemy) enemy = c->game->host->bodies.read(c->game->host->bodies.ctx, c->entity->enemy);
    if (!enemy.has_value
        || qa_hypot3(qa_sub3_double(q2mpm_body(c->game, c->entity).origin, enemy.value.origin)) > 128) return;
    q2mpm_set_move(c, "fixbot_move_laserattack");
    return;
  }
  q2mpm_set_move(c, "fixbot_move_attack2");
}
static void attack_fn(void *ctx, qa_q2m_context *c) { attack_move(ctx, c); }
static void walk_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *target = c->entity->has_goal ? c->game->entity(c->game->ctx, &c->entity->goal) : NULL;
  bool weld = target && strcmp(target->classname, "object_repair") == 0
      && qa_hypot3(qa_sub3_double(q2mpm_body(c->game, c->entity).origin, q2mpm_body(c->game, target).origin)) < 32;
  q2mpm_set_move(c, weld ? "fixbot_move_weld_start" : "fixbot_move_walk");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  if (q2mpm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2mpm_now(c->game) + 3;
  q2mpm_snd(c, "flyer/flypain1.wav", 2);
  q2mpm_set_move(c, reaction.damage <= 10 ? "fixbot_move_pain3" : reaction.damage <= 25 ? "fixbot_move_painb"
                                                                                         : "fixbot_move_paina");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_explode(c, "flyer/flydeth1.wav");
}
static void ai_move2_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  if (distance != 0) qa_q2m_run_ai(c, QA_Q2M_AI_MOVE, distance);
  (void)turn_toward(c);
}
static void ai_movetogoal_fn(void *ctx, qa_q2m_context *c, double distance) {
  fixbot_bundle *bundle = ctx;
  bool found = false;
  for (size_t i = 0; i < bundle->before_count; i++) {
    if (bundle->before[i].actor.token == c->entity->actor.token
        && qa_same_actor(bundle->before[i].actor.id, c->entity->actor.id)) {
      bundle->before[i].origin = q2mpm_body(c->game, c->entity).origin;
      found = true;
    }
  }
  if (!found) {
    if (bundle->before_count == bundle->before_cap) {
      size_t cap = bundle->before_cap ? bundle->before_cap * 2 : 8;
      before_entry *grown = realloc(bundle->before, cap * sizeof *grown);
      if (!grown) qa_q2f_error("Out of memory in fixbot movement");
      bundle->before = grown;
      bundle->before_cap = cap;
    }
    bundle->before[bundle->before_count].actor = c->entity->actor;
    bundle->before[bundle->before_count].origin = q2mpm_body(c->game, c->entity).origin;
    bundle->before_count++;
  }
  (void)c->move_to_goal(c->impl_ctx, c, distance);
}
static void ai_facing_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx; (void)distance;
  if (qa_q2m_in_front(c, goal_of(c)->actor.id)) { q2mpm_set_move(c, "fixbot_move_forward"); return; }
  (void)turn_toward(c);
}
static void change_to_roam_fn(void *ctx, qa_q2m_context *c) {
  fixbot_bundle *bundle = ctx;
  if (search_dead(bundle, c)) return;
  q2mpm_set_move(c, "fixbot_move_roamgoal");
  if (((uint32_t)c->entity->spawnflags & 16u) != 0) { vertical_goal(c, true); c->entity->spawnflags = 32; }
  if (((uint32_t)c->entity->spawnflags & 8u) != 0) { vertical_goal(c, false); c->entity->spawnflags = 32; }
  if (((uint32_t)c->entity->spawnflags & 4u) != 0) { q2mpm_set_move(c, "fixbot_move_roamgoal"); c->entity->spawnflags = 32; }
  if (c->entity->spawnflags == 0) q2mpm_set_move(c, "fixbot_move_stand2");
}
static void roam_goal_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_body_state body = q2mpm_body(game, entity);
  qa_q2f_entity *target = game->create(game->ctx, "bot_goal", NULL);
  target->has_owner = true;
  target->owner = entity->actor.id;
  game->solid(game->ctx, target, QA_Q2F_SOLID_BOX);
  double distance = 0;
  qa_vec3 chosen = q2mpm_zero();
  for (int i = 0; i < 12; i++) {
    qa_vec3 angles = body.angles;
    angles.y += i < 6 ? i * 30 : -(i - 6) * 30;
    qa_vec3 end = qa_add3_double(body.origin,
        qa_scale3_double(qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE).forward, 8192));
    qa_trace_result trace = q2mpm_trace(game, body.origin, end, NULL, &entity->actor.id, QA_Q2M_MASK_SHOT);
    double size = trunc(qa_hypot3(qa_sub3_double(body.origin, trace.end)));
    if (size > distance) { distance = size; chosen = trace.end; }
  }
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = chosen;
  q2mpm_move_body(game, target, change, true, false);
  entity->has_goal = true; entity->goal = target->actor.id;
  entity->has_enemy = true; entity->enemy = target->actor.id;
  q2mpm_set_move(c, "fixbot_move_turn");
}
static void fly_vertical2_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (turn_toward(c) < 32) leave_goal(c);
}
static void fly_vertical_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_q2mpm_xatrix_fixbot_frames *f = &qa_q2mpm_xatrix_fixbot_frames_value;
  (void)turn_toward(c);
  if (c->entity->frame == f->landing_58 || c->entity->frame == f->takeoff_16) leave_goal(c);
  qa_body_state body = q2mpm_body(c->game, c->entity);
  qa_vec3 tilted = body.angles;
  tilted.x += 90;
  qa_vec3 direction = qa_math_angle_vectors_rule(tilted, QA_ANGLE_RULE_NATIVE).forward;
  double spread = 500 + c->entity->frame - f->takeoff_01;
  for (int i = 0; i < 10; i++)
    c->weapons.fire_shotgun(c->weapons.ctx, c->entity, c->game, body.origin, direction, 2, 1, spread, spread, 1, 37);
}
static void use_scanner_fn(void *ctx, qa_q2m_context *c) {
  fixbot_bundle *bundle = ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  size_t count = 0;
  const qa_actor_id *nearby = game->host->nearby(game->host->ctx, q2mpm_body(game, entity).origin, 1024, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = nearby[i];
    qa_q2f_entity *candidate = game->entity(game->ctx, &actor);
    if (!candidate || qa_q2m_health(game, &actor) < 100 || strcmp(candidate->classname, "object_repair") != 0
        || !qa_q2m_visible(c, &actor)) continue;
    qa_q2f_entity *old = goal_of(c);
    if (strcmp(old->classname, "bot_goal") == 0) game->schedule(game->ctx, old, 0.1, qa_q2f_free_entity_think());
    entity->has_goal = true; entity->goal = actor;
    entity->has_enemy = true; entity->enemy = actor;
    if (qa_hypot3(qa_sub3_double(q2mpm_body(game, entity).origin, q2mpm_body(game, candidate).origin)) < 32)
      q2mpm_set_move(c, "fixbot_move_weld_start");
    return;
  }
  qa_q2f_entity *target = goal_of(c);
  if (qa_hypot3(qa_sub3_double(q2mpm_body(game, entity).origin, q2mpm_body(game, target).origin)) < 32) {
    if (strcmp(target->classname, "object_repair") == 0) q2mpm_set_move(c, "fixbot_move_weld_start");
    else leave_goal(c);
    return;
  }
  qa_vec3 previous;
  bool found = false;
  for (size_t i = 0; i < bundle->before_count; i++) {
    if (bundle->before[i].actor.token == entity->actor.token && qa_same_actor(bundle->before[i].actor.id, entity->actor.id)) {
      previous = bundle->before[i].origin;
      found = true;
    }
  }
  if (!found) qa_q2f_error("Fixbot scanner must follow source movement in the same frame");
  if (trunc(qa_hypot3(qa_sub3_double(q2mpm_body(game, entity).origin, previous))) == 0) {
    if (strcmp(target->classname, "object_repair") == 0) q2mpm_set_move(c, "fixbot_move_stand");
    else leave_goal(c);
  }
}
static void weldstate_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_q2mpm_xatrix_fixbot_frames *f = &qa_q2mpm_xatrix_fixbot_frames_value;
  if (c->entity->frame == f->weldstart_10) { q2mpm_set_move(c, "fixbot_move_weld"); return; }
  if (c->entity->frame == f->weldmiddle_07) {
    qa_q2f_entity *target = goal_of(c);
    double hp = qa_q2m_health(c->game, &target->actor.id);
    if (hp < 0) {
      qa_q2f_entity *enemy = c->entity->has_enemy ? c->game->entity(c->game->ctx, &c->entity->enemy) : NULL;
      if (enemy) enemy->has_owner = false;
      q2mpm_set_move(c, "fixbot_move_weld_end");
      return;
    }
    c->game->host->combat.set_health(c->game->host->combat.ctx, target->actor, hp - 10);
    return;
  }
  c->entity->has_goal = false;
  c->entity->has_enemy = false;
  q2mpm_set_move(c, "fixbot_move_stand");
}
static void fire_welder_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!c->entity->has_enemy) return;
  double color = 0xe0 + floor(q2mpm_rnd(c->game) * 8);
  q2mpm_emit_effect(c->game, "q2:welding-sparks",
                    qa_q2m_project_flash_default(c, q2mpm_v3(24, -0.8, -10)), q2mpm_zero(), 10, color);
  if (q2mpm_rnd(c->game) > 0.8) {
    double r = q2mpm_rnd(c->game);
    q2mpm_snd4(c, r < 0.33 ? "misc/welder1.wav" : r < 0.66 ? "misc/welder2.wav" : "misc/welder3.wav", 2, 1, 2);
  }
}
static void fire_blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!qa_q2m_visible_enemy(c)) q2mpm_set_move(c, "fixbot_move_run");
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 58, &aim)) return;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 15, 1000, 8,
                                false, false, 0);
  qa_q2bm_muzzle(c, 58, aim.direction, aim.start);
}
static void fire_laser_fn(void *ctx, qa_q2m_context *c) {
  fixbot_bundle *bundle = ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *target = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  if (!target) return;
  qa_q2m_context *patient = qa_q2m_monsters_context(bundle->monsters, target->actor.id);
  if (!patient) return;
  if (qa_q2m_health(game, &target->actor.id) <= patient->state->gib_health) {
    c->state->medic = false;
    q2mpm_set_move(c, "fixbot_move_stand");
    return;
  }
  q2mpm_snd4(c, "misc/lasfly.wav", 0, 1, 3);
  qa_body_state body = q2mpm_body(game, entity);
  qa_body_state target_body = q2mpm_body(game, target);
  qa_vec3 angles = qa_vector_to_angles_with_rule(qa_sub3_double(target_body.origin, body.origin),
                                                 QA_VECTOR_TO_ANGLES_NATIVE_AI);
  qa_vec3 origin = qa_add3_double(body.origin,
      qa_scale3_double(qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE).forward, 16));
  (void)qa_q2mpm_monster_dabeam(entity, game, &target->actor.id, origin, angles, -1, true);
  qa_combat_state_result combat = game->host->combat.read(game->host->combat.ctx, target->actor.id);
  double mass = combat.has_value ? combat.value.mass : 0;
  if (qa_q2m_health(game, &target->actor.id) <= mass / 10) {
    patient->state->resurrecting = true;
    return;
  }
  qa_vec3 up_end = qa_scale3_double(qa_math_angle_vectors_rule(target_body.angles, QA_ANGLE_RULE_NATIVE).up, 48);
  qa_trace_result trace = q2mpm_trace(game, target_body.origin, up_end, &target_body.bounds, &entity->actor.id,
                                      qa_q2m_monster_solid_mask(game));
  if (trace.hit.kind == QA_TRACE_HIT_ACTOR) {
    qa_combat_state_result victim = game->host->combat.read(game->host->combat.ctx, trace.hit.actor);
    if (victim.has_value && victim.value.can_take_damage) {
      qa_q2f_owned_result owned = game->host->actors.resolve_owned(game->host->actors.ctx, trace.hit.actor);
      if (owned.has_value) game->host->combat.set_health(game->host->combat.ctx, owned.value, -1000);
      return;
    }
  }
  target->spawnflags = 0;
  target->target = target->targetname = target->combat_target = target->death_target = "";
  target->has_owner = true;
  target->owner = entity->actor.id;
  qa_q2m_context *restored = qa_q2m_monsters_respawn(bundle->monsters, target, game);
  target->has_owner = false;
  restored->state->resurrecting = false;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true;
  change.origin = body.origin;
  change.origin.z += 1;
  q2mpm_move_body(game, entity, change, true, false);
  c->state->medic = false;
  q2mpm_set_move(c, "fixbot_move_stand");
}
qa_q2m_definition qa_q2mpm_create_fixbot_definition(qa_q2m_monsters *monsters) {
  fixbot_bundle *bundle = malloc(sizeof *bundle);
  if (!bundle) qa_q2f_error("Out of memory creating fixbot definition");
  memset(bundle, 0, sizeof *bundle);
  bundle->monsters = monsters;
  qa_q2m_named_handler *callbacks = malloc(12 * sizeof *callbacks);
  qa_q2m_named_ai *ai = malloc(3 * sizeof *ai);
  qa_q2f_named_think *thinks = malloc(sizeof *thinks);
  if (!callbacks || !ai || !thinks) qa_q2f_error("Out of memory creating fixbot definition");
  size_t n = 0;
  callbacks[n++] = (qa_q2m_named_handler){"fixbot_run", {NULL, run_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"fixbot_dead", {NULL, qa_q2bm_finish_corpse_handler}};
  callbacks[n++] = (qa_q2m_named_handler){"fixbot_attack", {bundle, attack_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"change_to_roam", {bundle, change_to_roam_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"roam_goal", {NULL, roam_goal_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"fly_vertical2", {NULL, fly_vertical2_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"fly_vertical", {NULL, fly_vertical_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"use_scanner", {bundle, use_scanner_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"weldstate", {NULL, weldstate_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"fixbot_fire_welder", {NULL, fire_welder_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"fixbot_fire_blaster", {NULL, fire_blaster_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"fixbot_fire_laser", {bundle, fire_laser_fn}};
  ai[0] = (qa_q2m_named_ai){"ai_move2", NULL, ai_move2_fn};
  ai[1] = (qa_q2m_named_ai){"ai_movetogoal", bundle, ai_movetogoal_fn};
  ai[2] = (qa_q2m_named_ai){"ai_facing", NULL, ai_facing_fn};
  thinks[0].name = "G_FreeEdict";
  thinks[0].cb = qa_q2f_free_entity_think();
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_fixbot";
  definition.kind = "fixbot";
  definition.model = "models/monsters/fixbot/tris.md2";
  definition.health = 150;
  definition.gib_health = 0;
  definition.mass = 150;
  definition.bounds.min = q2mpm_v3(-32, -32, -24);
  definition.bounds.max = q2mpm_v3(32, 32, 24);
  definition.scale = 1;
  definition.has_locomotion = true;
  definition.locomotion = QA_Q2M_LOCO_FLY;
  definition.initial_move = "fixbot_move_stand";
  definition.moves = qa_q2mpm_xatrix_fixbot_moves;
  definition.move_count = qa_q2mpm_xatrix_fixbot_move_count;
  definition.callbacks = callbacks;
  definition.callback_count = n;
  definition.ai = ai;
  definition.ai_count = 3;
  definition.has_source_callbacks = true;
  memset(&definition.source_callbacks, 0, sizeof definition.source_callbacks);
  definition.source_callbacks.think = thinks;
  definition.source_callbacks.think_count = 1;
  definition.stand = qa_q2bm_move_handler("fixbot_move_stand");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.walk = (qa_q2m_handler){NULL, walk_fn};
  definition.attack = (qa_q2m_handler){bundle, attack_fn};
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  return definition;
}
