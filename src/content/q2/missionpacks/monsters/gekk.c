/* Port of src/content/q2/missionpacks/monsters/gekk.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/gekk.h"

#include <stdio.h>
#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_gekk.h"

typedef struct gekk_bundle {
  qa_q2m_monsters *monsters;
} gekk_bundle;
static void gib_die_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  game->remove(game->ctx, entity);
}
static void loogie_touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  (void)ctx;
  if (entity->has_owner && qa_same_actor(contact.other, entity->owner)) return;
  if (((uint32_t)(contact.surface ? contact.surface->native_flags : 0) & 4u) != 0) {
    game->remove(game->ctx, entity);
    return;
  }
  qa_combat_state_result state = game->host->combat.read(game->host->combat.ctx, contact.other);
  if (state.has_value && state.value.can_take_damage) {
    qa_body_state body = q2mpm_body(game, entity);
    qa_vec3 normal = contact.plane ? contact.plane->normal : q2mpm_zero();
    const qa_actor_id *attacker = entity->has_owner ? &entity->owner : NULL;
    (void)q2mpm_damage(game, contact.other, entity, attacker, entity->damage, 1, body.velocity, body.origin,
                       normal, 38, true, 4);
  }
  game->remove(game->ctx, entity);
}
static qa_q2f_callback_definitions loogie_callbacks(void) {
  static qa_q2f_named_think thinks[1];
  static qa_q2f_named_touch touches[1];
  static bool ready;
  if (!ready) {
    thinks[0].name = "G_FreeEdict";
    thinks[0].cb = qa_q2f_free_entity_think();
    touches[0].name = "loogie_touch";
    touches[0].cb = (qa_q2f_touch){NULL, loogie_touch_fn};
    ready = true;
  }
  qa_q2f_callback_definitions definitions;
  memset(&definitions, 0, sizeof definitions);
  definitions.think = thinks;
  definitions.think_count = 1;
  definitions.touch = touches;
  definitions.touch_count = 1;
  return definitions;
}
qa_q2f_entity *qa_q2mpm_fire_gekk_loogie(qa_q2f_entity *self, qa_q2f_game *game, qa_vec3 start, qa_vec3 direction,
                                         double damage, double speed) {
  game->source_callbacks.register_defs(game->source_callbacks.ctx, loogie_callbacks());
  qa_q2f_entity *projectile = game->create(game->ctx, "loogie", NULL);
  qa_vec3 aim = qa_normalize3_double_hypot(direction);
  projectile->has_owner = true;
  projectile->owner = self->actor.id;
  projectile->model = "models/objects/loogy/tris.md2";
  projectile->effects = q2mpm_or(projectile->effects, 8);
  projectile->damage = damage;
  projectile->clip_mask = QA_Q2M_MASK_SHOT;
  projectile->projectile = true;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = start;
  change.has_angles = true; change.angles = qa_vector_to_angles_with_rule(aim, QA_VECTOR_TO_ANGLES_NATIVE_AI);
  change.has_velocity = true; change.velocity = qa_scale3_double(aim, speed);
  change.has_bounds = true;
  q2mpm_move_body(game, projectile, change, true, false);
  projectile->touch = (qa_q2f_touch){NULL, loogie_touch_fn};
  game->solid(game->ctx, projectile, QA_Q2F_SOLID_BOX);
  game->motion(game->ctx, projectile, QA_Q2F_MOTION_FLY_MISSILE);
  game->schedule(game->ctx, projectile, 2, qa_q2f_free_entity_think());
  game->show(game->ctx, projectile);
  qa_trace_result trace = q2mpm_trace(game, q2mpm_body(game, self).origin, start, NULL, &projectile->actor.id,
                                      QA_Q2M_MASK_SHOT);
  if (trace.fraction < 1) {
    memset(&change, 0, sizeof change);
    change.has_origin = true;
    change.origin = qa_add3_double(start, qa_scale3_double(aim, -10));
    q2mpm_move_body(game, projectile, change, false, false);
    qa_actor_id other;
    if (qa_q2m_trace_ground_actor(trace, game, &other)) {
      qa_touch_contact contact;
      memset(&contact, 0, sizeof contact);
      contact.self = projectile->actor;
      contact.other = other;
      loogie_touch_fn(NULL, projectile, game, contact);
    }
  }
  return projectile;
}
static void acid_gib(qa_q2m_context *c, const char *part, double damage, bool head) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_body_state body = q2mpm_body(game, entity);
  qa_q2f_entity *gib = head ? entity : game->create(game->ctx, "acid_gib", NULL);
  game->source_callbacks.register_defs(game->source_callbacks.ctx, loogie_callbacks());
  {
    static qa_q2f_named_die dies[1];
    static bool dies_ready;
    if (!dies_ready) {
      dies[0].name = "acid_gib_die";
      dies[0].cb = (qa_q2f_die){NULL, gib_die_fn};
      dies_ready = true;
    }
    qa_q2f_callback_definitions definitions;
    memset(&definitions, 0, sizeof definitions);
    definitions.die = dies;
    definitions.die_count = 1;
    game->source_callbacks.register_defs(game->source_callbacks.ctx, definitions);
  }
  qa_vec3 half = qa_scale3_double(qa_sub3_double(body.bounds.max, body.bounds.min), 0.5);
  qa_vec3 center = qa_add3_double(qa_add3_double(body.origin, body.bounds.min),
                                  qa_add3_double(half, q2mpm_v3(-1, -1, -1)));
  qa_vec3 origin = body.origin;
  if (!head)
    origin = qa_add3_double(center, q2mpm_v3((q2mpm_rnd(game) * 2 - 1) * half.x,
                                             (q2mpm_rnd(game) * 2 - 1) * half.y,
                                             (q2mpm_rnd(game) * 2 - 1) * half.z));
  char model[64];
  snprintf(model, sizeof model, "models/objects/gekkgib/%s/tris.md2", part);
  char *model_copy = malloc(strlen(model) + 1);
  if (!model_copy) qa_q2f_error("Out of memory in gekk gib");
  strcpy(model_copy, model);
  gib->model = model_copy;
  gib->clip_mask = QA_Q2M_MASK_SHOT;
  gib->flags = q2mpm_or(gib->flags, 2048);
  gib->damage = 2;
  if (head) {
    gib->skin = 0;
    gib->frame = 0;
    gib->model2 = "";
    gib->effects = (double)(((uint32_t)gib->effects | 0x200000u | 8u) & ~0x4000u);
    gib->server_flags = q2mpm_and(gib->server_flags, ~4);
    qa_q2f_presentation_event e;
    memset(&e, 0, sizeof e);
    e.kind = QA_Q2F_EVENT_SOUND;
    e.actor = &gib->actor.id;
    e.origin = origin;
    e.path = "";
    e.channel = 0; e.volume = 0; e.attenuation = 1; e.loop = "stop";
    game->host->emit(game->host->ctx, e);
  } else {
    gib->effects = (double)((uint32_t)gib->effects | 0x200000u);
    gib->render_flags = q2mpm_or(gib->render_flags, 8);
  }
  qa_vec3 raw = {100 * (q2mpm_rnd(game) * 2 - 1), 100 * (q2mpm_rnd(game) * 2 - 1), 200 + 100 * q2mpm_rnd(game)};
  qa_vec3 impulse = qa_scale3_double(raw, damage < 50 ? 0.7 : 1.2);
  qa_vec3 velocity = qa_add3_double(body.velocity, qa_scale3_double(impulse, head ? 0.5 : 3));
  if (head) {
    gib->angular_velocity = gib->angular_velocity;
    gib->angular_velocity.y = (q2mpm_rnd(game) * 2 - 1) * 600;
  } else {
    gib->angular_velocity = q2mpm_v3(q2mpm_rnd(game) * 600, q2mpm_rnd(game) * 600, q2mpm_rnd(game) * 600);
  }
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  change.has_velocity = true;
  change.velocity = q2mpm_v3(fmax(-300, fmin(300, velocity.x)), fmax(-300, fmin(300, velocity.y)),
                             fmax(200, fmin(500, velocity.z)));
  change.has_bounds = true;
  q2mpm_move_body(game, gib, change, true, false);
  if (!game->host->combat.read(game->host->combat.ctx, gib->actor.id).has_value) {
    qa_combat_state combat;
    memset(&combat, 0, sizeof combat);
    combat.armor.powered.kind = "none";
    combat.can_take_damage = true;
    game->host->combat.create(game->host->combat.ctx, gib->actor, combat);
  } else {
    q2mpm_set_damageable(game, gib, true);
  }
  gib->die = (qa_q2f_die){NULL, gib_die_fn};
  game->motion(game->ctx, gib, QA_Q2F_MOTION_TOSS);
  game->solid(game->ctx, gib, QA_Q2F_SOLID_BOX);
  game->schedule(game->ctx, gib, 10 + q2mpm_rnd(game) * 10, qa_q2f_free_entity_think());
  game->show(game->ctx, gib);
}
static void gibfest(qa_q2m_context *c, double damage) {
  q2mpm_snd(c, "misc/udeath.wav", 2);
  static const char *const parts[] = {"pelvis", "arm", "arm", "torso", "claw", "leg", "leg"};
  for (int i = 0; i < 7; i++) acid_gib(c, parts[i], damage, false);
  acid_gib(c, "head", damage, true);
  c->state->dead = true;
  c->state->gibbed = true;
}
static bool wet_entity(qa_q2m_context *c, qa_q2f_entity *entity) {
  if (entity == c->entity) return c->state->water_level > 0;
  qa_body_state body = q2mpm_body(c->game, entity);
  qa_vec3 probe = body.origin;
  probe.z += body.bounds.min.z + 1;
  return ((uint32_t)c->game->host->point_contents(c->game->host->ctx, probe) & 56u) != 0;
}
static bool wet(qa_q2m_context *c) { return wet_entity(c, c->entity); }
static void water_to_land(qa_q2m_context *c) {
  c->entity->flags = q2mpm_and(c->entity->flags, ~2);
  c->state->locomotion = QA_Q2M_LOCO_WALK;
  c->state->yaw_speed = 20;
  c->entity->view_height = 25;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2mpm_v3(-24, -24, -24);
  change.bounds.max = q2mpm_v3(24, 24, 24);
  q2mpm_move_body(c->game, c->entity, change, true, false);
  q2mpm_set_move(c, "gekk_move_leapatk2");
}
static void land_to_water(qa_q2m_context *c) {
  c->entity->flags = q2mpm_or(c->entity->flags, 2);
  c->state->locomotion = QA_Q2M_LOCO_SWIM;
  c->state->yaw_speed = 10;
  c->entity->view_height = 10;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2mpm_v3(-24, -24, -24);
  change.bounds.max = q2mpm_v3(24, 24, 16);
  q2mpm_move_body(c->game, c->entity, change, true, false);
  q2mpm_set_move(c, "gekk_move_swim_start");
}
static bool check_jump(qa_q2m_context *c) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return false;
  qa_body_state body = q2mpm_body(c->game, c->entity);
  double minimum = enemy.origin.z + enemy.bounds.min.z;
  double size = enemy.bounds.max.z - enemy.bounds.min.z;
  if (body.origin.z + body.bounds.min.z > minimum + 0.75 * size
      || body.origin.z + body.bounds.max.z < minimum + 0.25 * size) return false;
  double distance = hypot(body.origin.x - enemy.origin.x, body.origin.y - enemy.origin.y);
  return !(distance < 100 || (distance > 100 && q2mpm_rnd(c->game) < 0.9));
}
static void run_move(qa_q2m_context *c) {
  q2mpm_set_move(c, wet(c) ? "gekk_move_swim_start" : c->state->stand_ground ? "gekk_move_stand" : "gekk_move_run");
}
static void run_start_move(qa_q2m_context *c) {
  q2mpm_set_move(c, wet(c) ? "gekk_move_swim_start" : "gekk_move_run_start");
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_start_move(c); }
static void run_cb_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static void run_start_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_start_move(c); }
static void melee_move(qa_q2m_context *c) {
  q2mpm_set_move(c, wet(c) ? "gekk_move_attack" : q2mpm_rnd(c->game) > 0.66 ? "gekk_move_attack1" : "gekk_move_attack2");
}
static void melee_fn(void *ctx, qa_q2m_context *c) { (void)ctx; melee_move(c); }
static void hit_move(qa_q2m_context *c, bool right) {
  qa_bounds bounds = q2mpm_body(c->game, c->entity).bounds;
  bool connected = c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game,
                                       q2mpm_v3(80, right ? bounds.max.x : bounds.min.x, 8),
                                       15 + floor(q2mpm_rnd(c->game) * 5), 100);
  q2mpm_snd(c, connected ? (right ? "gek/gk_atck3.wav" : "gek/gk_atck2.wav") : "gek/gk_atck1.wav", 1);
}
static void search_move(qa_q2m_context *c) {
  const char *path = "gek/gk_idle1.wav";
  if (((uint32_t)c->entity->spawnflags & 8u) != 0) {
    double r = q2mpm_rnd(c->game);
    path = r < 0.33 ? "gek/gek_low.wav" : r < 0.66 ? "gek/gek_mid.wav" : "gek/gek_high.wav";
  }
  q2mpm_snd(c, path, 2);
  double hp = fmin(c->entity->max_health,
                     trunc(qa_q2m_health(c->game, &c->entity->actor.id) + 10 + 10 * q2mpm_rnd(c->game)));
  c->game->host->combat.set_health(c->game->host->combat.ctx, c->entity->actor, hp);
  c->entity->skin = hp < c->entity->max_health / 4 ? 2 : hp < c->entity->max_health / 2 ? 1 : 0;
}
static void search_fn(void *ctx, qa_q2m_context *c) { (void)ctx; search_move(c); }
static void jump_touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  gekk_bundle *bundle = ctx;
  qa_q2m_context *context = qa_q2m_monsters_context(bundle->monsters, entity->actor.id);
  if (!context) qa_q2f_error("Gekk jump without its source controller");
  if (qa_q2m_health(game, &entity->actor.id) <= 0) {
    entity->touch = (qa_q2f_touch){NULL, NULL};
    return;
  }
  qa_body_state body = q2mpm_body(game, entity);
  qa_combat_state_result state = game->host->combat.read(game->host->combat.ctx, contact.other);
  if (state.has_value && state.value.can_take_damage && qa_hypot3(body.velocity) > 200) {
    qa_vec3 normal = qa_normalize3_double_hypot(body.velocity);
    double damage = trunc(10 + 10 * q2mpm_rnd(game));
    (void)q2mpm_damage(game, contact.other, entity, &entity->actor.id, damage, damage, body.velocity,
                       qa_add3_double(body.origin, qa_scale3_double(normal, body.bounds.max.x)), normal, 38,
                       false, 0);
  }
  if (!qa_q2m_check_bottom(context, body.origin)) {
    if (body.ground != NULL) {
      context->state->next_frame = qa_q2mpm_xatrix_gekk_frames_value.leapatk_11;
      entity->touch = (qa_q2f_touch){NULL, NULL};
    }
    return;
  }
  entity->touch = (qa_q2f_touch){NULL, NULL};
}
static void takeoff(gekk_bundle *bundle, qa_q2m_context *c, bool from_water) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_body_state body = q2mpm_body(game, entity);
  qa_body_state enemy;
  bool has_enemy = qa_q2m_enemy_body(c, &enemy);
  q2mpm_snd(c, "gek/gk_sght1.wav", 2);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true;
  change.origin = body.origin;
  change.origin.z = from_water ? (has_enemy ? enemy.origin.z : body.origin.z) : body.origin.z + 1;
  q2mpm_move_body(game, entity, change, true, false);
  bool long_jump = check_jump(c);
  memset(&change, 0, sizeof change);
  change.has_velocity = true;
  change.velocity = qa_scale3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward,
                                     from_water ? (long_jump ? 300 : 150) : (long_jump ? 700 : 250));
  change.velocity.z = from_water ? (long_jump ? 250 : 300) : (long_jump ? 250 : 400);
  change.has_ground = true;
  change.ground = NULL;
  q2mpm_move_body(game, entity, change, true, false);
  c->state->ducked = true;
  c->state->attack_finished = q2mpm_now(game) + 3;
  entity->touch = (qa_q2f_touch){bundle, jump_touch_fn};
}
static void stand_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, wet(c) ? "gekk_move_standunderwater" : "gekk_move_stand");
}
static void idle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, wet(c) ? "gekk_move_swim_start" : "gekk_move_idle");
}
static void after_spawn_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (((uint32_t)c->entity->spawnflags & 8u) != 0) q2mpm_set_move(c, "gekk_move_chant");
}
static void ai_stand2_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  if (((uint32_t)c->entity->spawnflags & 8u) == 0) { qa_q2m_run_ai(c, QA_Q2M_AI_STAND, distance); return; }
  qa_q2m_run_ai(c, QA_Q2M_AI_MOVE, distance);
  if (((uint32_t)c->entity->spawnflags & 1u) == 0 && q2mpm_now(c->game) > c->state->idle_time) {
    if (c->state->idle_time != 0) {
      c->idle(c->impl_ctx, c);
      c->state->idle_time = q2mpm_now(c->game) + 15 + q2mpm_rnd(c->game) * 15;
    } else {
      c->state->idle_time = q2mpm_now(c->game) + q2mpm_rnd(c->game) * 15;
    }
  }
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!qa_q2bm_alive_enemy(c)) return false;
  if (qa_q2m_target_distance(c) < 80) { c->state->attack_state = QA_Q2M_ATTACK_MELEE; return true; }
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return false;
  qa_vec3 origin = q2mpm_body(c->game, c->entity).origin;
  bool close = hypot(origin.x - enemy.origin.x, origin.y - enemy.origin.y) >= 100 || origin.z < enemy.origin.z;
  if (check_jump(c) || (close && !wet(c))) { c->state->attack_state = QA_Q2M_ATTACK_MISSILE; return true; }
  return false;
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (((uint32_t)c->entity->flags & 2u) != 0 || wet(c)) return;
  q2mpm_set_move(c, (q2mpm_rnd(c->game) > 0.5 && qa_q2m_target_distance(c) >= 80) || q2mpm_rnd(c->game) > 0.8
                        ? "gekk_move_spit" : "gekk_move_leapatk");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  if (((uint32_t)c->entity->spawnflags & 8u) != 0) {
    c->entity->spawnflags = q2mpm_and(c->entity->spawnflags, ~8);
    return;
  }
  double hp = qa_q2m_health(c->game, &c->entity->actor.id);
  if (hp < c->entity->max_health / 2) c->entity->skin = hp < c->entity->max_health / 4 ? 2 : 1;
  if (q2mpm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2mpm_now(c->game) + 3;
  q2mpm_snd(c, "gek/gk_pain1.wav", 2);
  q2mpm_set_move(c, wet(c) ? "gekk_move_pain" : q2mpm_rnd(c->game) > 0.5 ? "gekk_move_pain1" : "gekk_move_pain2");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  if (qa_q2m_health(c->game, &c->entity->actor.id) <= c->state->gib_health) { gibfest(c, reaction.pain.damage); return; }
  if (c->state->dead) return;
  q2mpm_snd(c, "gek/gk_deth1.wav", 2);
  c->state->dead = true;
  c->state->can_take_damage = true;
  c->entity->skin = 2;
  q2mpm_set_damageable(c->game, c->entity, true);
  if (wet(c)) { q2mpm_set_move(c, "gekk_move_wdeath"); return; }
  double r = q2mpm_rnd(c->game);
  q2mpm_set_move(c, r > 0.66 ? "gekk_move_death1" : r > 0.33 ? "gekk_move_death3" : "gekk_move_death4");
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta, const qa_trace_result *trace,
                     bool gravity) {
  (void)ctx; (void)trace; (void)gravity;
  if (q2mpm_rnd(c->game) > 0.25) return;
  if (!c->entity->has_enemy) { c->entity->has_enemy = true; c->entity->enemy = attacker; }
  if (wet(c)) { q2mpm_set_move(c, "gekk_move_attack"); return; }
  if (c->game->options.skill == 0) {
    q2mpm_set_move(c, q2mpm_rnd(c->game) > 0.5 ? "gekk_move_lduck" : "gekk_move_rduck");
    return;
  }
  c->state->pause_time = q2mpm_now(c->game) + eta + 0.3;
  double r = q2mpm_rnd(c->game);
  if (c->game->options.skill < 3 && r > (c->game->options.skill == 1 ? 0.33 : 0.66)) {
    q2mpm_set_move(c, q2mpm_rnd(c->game) > 0.5 ? "gekk_move_lduck" : "gekk_move_rduck");
    return;
  }
  q2mpm_set_move(c, q2mpm_rnd(c->game) > 0.66 ? "gekk_move_attack1" : "gekk_move_attack2");
}
static void stand_cb_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->stand(c->impl_ctx, c); }
static void swim_loop_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->flags = q2mpm_or(c->entity->flags, 2);
  c->state->locomotion = QA_Q2M_LOCO_SWIM;
  q2mpm_set_move(c, "gekk_move_swim_loop");
}
static void swim_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *enemy = c->entity->has_enemy ? c->game->entity(c->game->ctx, &c->entity->enemy) : NULL;
  if (enemy && !wet_entity(c, enemy) && q2mpm_rnd(c->game) > 0.7) water_to_land(c);
  else q2mpm_set_move(c, "gekk_move_swim_start");
}
static void check_underwater_fn(void *ctx, qa_q2m_context *c) { (void)ctx; if (wet(c)) land_to_water(c); }
static void idle_loop_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2mpm_rnd(c->game) > 0.75 && qa_q2m_health(c->game, &c->entity->actor.id) < c->entity->max_health)
    c->state->next_frame = qa_q2mpm_xatrix_gekk_frames_value.idle_01;
}
static void step_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double n = fmod(floor(q2mpm_rnd(c->game) * 3) + 1, 3);
  char buffer[32];
  snprintf(buffer, sizeof buffer, "gek/gk_step%d.wav", (int)n + 1);
  q2mpm_snd(c, buffer, 2);
}
static void hit_left_fn(void *ctx, qa_q2m_context *c) { (void)ctx; hit_move(c, false); }
static void hit_right_fn(void *ctx, qa_q2m_context *c) { (void)ctx; hit_move(c, true); }
static void bite_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  (void)c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2mpm_v3(80, 0, 0), 5, 0);
}
static void check_refire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_q2mpm_xatrix_gekk_frames *f = &qa_q2mpm_xatrix_gekk_frames_value;
  if (qa_q2bm_alive_enemy(c) && q2mpm_rnd(c->game) < c->game->options.skill * 0.1
      && qa_q2m_target_distance(c) < 80) {
    if (c->entity->frame == f->clawatk3_09) q2mpm_set_move(c, "gekk_move_attack2");
    else if (c->entity->frame == f->clawatk5_09) q2mpm_set_move(c, "gekk_move_attack1");
  }
}
static void loogie_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 eye;
  if (!qa_q2m_enemy_eye(c, &eye) || !qa_q2bm_alive_enemy(c)) return;
  qa_vec3 start = qa_add3_double(qa_q2m_project_flash_default(c, q2mpm_v3(-18, -0.8, 24)),
      qa_scale3_double(qa_math_angle_vectors_rule(q2mpm_body(c->game, c->entity).angles, QA_ANGLE_RULE_NATIVE).up, 2));
  (void)qa_q2mpm_fire_gekk_loogie(c->entity, c->game, start, qa_sub3_double(eye, start), 5, 550);
}
static void reloogie_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2mpm_rnd(c->game) > 0.8 && qa_q2m_health(c->game, &c->entity->actor.id) < c->entity->max_health) {
    q2mpm_set_move(c, "gekk_move_idle2");
    return;
  }
  double distance = qa_q2m_target_distance(c);
  const qa_actor_id *enemy = c->entity->has_enemy ? &c->entity->enemy : NULL;
  if (qa_q2m_health(c->game, enemy) >= 0 && q2mpm_rnd(c->game) > 0.7 && distance >= 80 && distance < 500)
    q2mpm_set_move(c, "gekk_move_spit");
}
static void takeoff1_fn(void *ctx, qa_q2m_context *c) { takeoff(ctx, c, false); }
static void takeoff2_fn(void *ctx, qa_q2m_context *c) { takeoff(ctx, c, true); }
static void stop_skid_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2mpm_body(c->game, c->entity).ground != NULL) {
    qa_q2f_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_velocity = true;
    q2mpm_move_body(c->game, c->entity, change, true, false);
  }
}
static void check_landing_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_q2mpm_xatrix_gekk_frames *f = &qa_q2mpm_xatrix_gekk_frames_value;
  if (q2mpm_body(c->game, c->entity).ground != NULL) {
    q2mpm_snd(c, "mutant/thud1.wav", 1);
    c->state->attack_finished = 0;
    c->state->ducked = false;
    qa_q2f_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_velocity = true;
    q2mpm_move_body(c->game, c->entity, change, true, false);
    return;
  }
  c->state->next_frame = q2mpm_now(c->game) > c->state->attack_finished ? f->leapatk_11 : f->leapatk_12;
}
static void preattack_fn(void *ctx, qa_q2m_context *c) { (void)ctx; (void)c; }
static void isgibfest_fn(void *ctx, qa_q2m_context *c) { (void)ctx; if (q2mpm_rnd(c->game) > 0.9) gibfest(c, 20); }
static void gibfest_fn(void *ctx, qa_q2m_context *c) { (void)ctx; gibfest(c, 20); }
static void dead_fn(void *ctx, qa_q2m_context *c) { (void)ctx; if (!wet(c)) qa_q2bm_finish_corpse(c); }
static void duck_down_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->ducked) return;
  qa_q2m_set_duck(c, true);
  c->state->pause_time = q2mpm_now(c->game) + 1;
}
static void duck_up_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_set_duck(c, false); }
static void duck_hold_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->hold_frame = q2mpm_now(c->game) < c->state->pause_time;
}
static const qa_q2bm_sound_spec sight_spec = {"gek/gk_sght1.wav", 2, 1};
qa_q2m_definition qa_q2mpm_create_gekk_definition(qa_q2m_monsters *monsters) {
  gekk_bundle *bundle = malloc(sizeof *bundle);
  if (!bundle) qa_q2f_error("Out of memory creating gekk definition");
  bundle->monsters = monsters;
  qa_q2m_named_handler *callbacks = malloc(28 * sizeof *callbacks);
  qa_q2m_named_ai *ai = malloc(sizeof *ai);
  qa_q2f_named_touch *touches = malloc(2 * sizeof *touches);
  qa_q2f_named_die *dies = malloc(sizeof *dies);
  qa_q2f_named_think *thinks = malloc(sizeof *thinks);
  if (!callbacks || !ai || !touches || !dies || !thinks) qa_q2f_error("Out of memory creating gekk definition");
  size_t n = 0;
  callbacks[n++] = (qa_q2m_named_handler){"gekk_face", {(void *)"gekk_move_run", qa_q2bm_move_handler_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_chant", {(void *)"gekk_move_chant", qa_q2bm_move_handler_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_run", {NULL, run_cb_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_run_start", {NULL, run_start_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_stand", {NULL, stand_cb_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_search", {NULL, search_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_swim_loop", {NULL, swim_loop_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_swim", {NULL, swim_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_check_underwater", {NULL, check_underwater_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_idle_loop", {NULL, idle_loop_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_step", {NULL, step_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_hit_left", {NULL, hit_left_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_hit_right", {NULL, hit_right_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_bite", {NULL, bite_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_check_refire", {NULL, check_refire_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"loogie", {NULL, loogie_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"reloogie", {NULL, reloogie_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_jump_takeoff", {bundle, takeoff1_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_jump_takeoff2", {bundle, takeoff2_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_stop_skid", {NULL, stop_skid_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_check_landing", {NULL, check_landing_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_preattack", {NULL, preattack_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"isgibfest", {NULL, isgibfest_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_gibfest", {NULL, gibfest_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_dead", {NULL, dead_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_duck_down", {NULL, duck_down_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_duck_up", {NULL, duck_up_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"gekk_duck_hold", {NULL, duck_hold_fn}};
  ai[0] = (qa_q2m_named_ai){"ai_stand2", NULL, ai_stand2_fn};
  touches[0] = (qa_q2f_named_touch){"gekk_jump_touch", {bundle, jump_touch_fn}};
  touches[1] = (qa_q2f_named_touch){"loogie_touch", {NULL, loogie_touch_fn}};
  dies[0] = (qa_q2f_named_die){"acid_gib_die", {NULL, gib_die_fn}};
  thinks[0].name = "G_FreeEdict";
  thinks[0].cb = qa_q2f_free_entity_think();
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_gekk";
  definition.kind = "gekk";
  definition.model = "models/monsters/gekk/tris.md2";
  definition.health = 125;
  definition.gib_health = -30;
  definition.mass = 300;
  definition.bounds.min = q2mpm_v3(-24, -24, -24);
  definition.bounds.max = q2mpm_v3(24, 24, 24);
  definition.scale = 1;
  definition.initial_move = "gekk_move_stand";
  definition.moves = qa_q2mpm_xatrix_gekk_moves;
  definition.move_count = qa_q2mpm_xatrix_gekk_move_count;
  definition.callbacks = callbacks;
  definition.callback_count = n;
  definition.ai = ai;
  definition.ai_count = 1;
  definition.has_source_callbacks = true;
  memset(&definition.source_callbacks, 0, sizeof definition.source_callbacks);
  definition.source_callbacks.touch = touches;
  definition.source_callbacks.touch_count = 2;
  definition.source_callbacks.die = dies;
  definition.source_callbacks.die_count = 1;
  definition.source_callbacks.think = thinks;
  definition.source_callbacks.think_count = 1;
  definition.stand = (qa_q2m_handler){NULL, stand_fn};
  definition.walk = qa_q2bm_move_handler("gekk_move_walk");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.sight = qa_q2bm_sound_handler(&sight_spec);
  definition.search = (qa_q2m_handler){NULL, search_fn};
  definition.idle = (qa_q2m_handler){NULL, idle_fn};
  definition.melee = (qa_q2m_handler){NULL, melee_fn};
  definition.after_spawn = (qa_q2m_handler){NULL, after_spawn_fn};
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.dodge = dodge_fn;
  definition.check_attack = check_attack_fn;
  return definition;
}
