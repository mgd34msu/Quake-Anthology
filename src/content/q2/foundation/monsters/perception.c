/* Port of src/content/q2/foundation/monsters/perception.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/perception.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "monsters_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static bool grow(void **arr, size_t *cap, size_t count, size_t size) {
  if (count < *cap) return true;
  size_t next = *cap ? *cap * 2 : 8;
  void *p = realloc(*arr, next * size);
  if (!p) return false;
  *arr = p; *cap = next; return true;
}
static double health_actor(qa_q2f_game *g, qa_actor_id a) { return qa_q2m_health(g, &a); }

bool qa_q2m_facing_ideal(qa_q2m_context *c) {
  double delta = fmod(fmod(q2m_body(c->game, c->entity).angles.y - c->state->ideal_yaw, 360) + 360, 360);
  return delta <= 45 || delta >= 315;
}
static const qa_q2m_attack_chance_profile normal_chances = {0.7, 0.4, 0.25, 0.06, 0, 1};

bool qa_q2m_default_check_attack(qa_q2m_context *c, const qa_q2m_attack_chance_profile *profile_arg) {
  const qa_q2m_attack_chance_profile *profile = profile_arg ? profile_arg : &normal_chances;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  qa_q2f_entity *entity = c->entity;
  qa_vec3 target;
  if (!qa_q2m_enemy_eye(c, &target)) return false;
  bool rr = q2m_rr(game);
  bool rogue = c->source_combat_rules(c->impl_ctx, c) == QA_Q2M_RULES_ROGUE;
  qa_body_state body = q2m_body(game, entity);
  qa_vec3 start = body.origin;
  start.z = body.origin.z + entity->view_height;
  if (qa_q2m_health(game, entity->has_enemy ? &entity->enemy : NULL) > 0) {
    qa_trace_result trace = q2m_trace(game, start, target, NULL, &entity->actor.id, qa_q2m_attack_trace_mask(game));
    bool direct = trace.hit.kind == QA_TRACE_HIT_ACTOR
      && ((entity->has_enemy && qa_same_actor(trace.hit.actor, entity->enemy))
          || (rr && game->host->is_player(game->host->ctx, trace.hit.actor)));
    if (!direct) {
      qa_q2f_entity *target_entity = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
      if ((!rr && !rogue) || !target_entity || target_entity->solid != QA_Q2F_SOLID_NONE || trace.fraction < 1) {
        if ((rr || rogue) && state->blind_fire && (!rr || state->had_visibility) && state->blind_fire_delay <= 20
            && !qa_q2m_visible_enemy(c)
            && !(trace.hit.kind == QA_TRACE_HIT_ACTOR && game->host->is_monster(game->host->ctx, trace.hit.actor))
            && q2m_now(game) >= state->attack_finished
            && q2m_now(game) >= state->trail_time + state->blind_fire_delay) {
          qa_trace_result blind = q2m_trace(game, start, state->blind_fire_target, NULL, &entity->actor.id, 0x2000000);
          if (!blind.all_solid && !blind.start_solid
              && (blind.fraction == 1 || (blind.hit.kind == QA_TRACE_HIT_ACTOR && entity->has_enemy
                                           && qa_same_actor(blind.hit.actor, entity->enemy)))) {
            state->attack_state = QA_Q2M_ATTACK_BLIND;
            return true;
          }
        }
        return false;
      }
    }
  }
  double distance = qa_q2m_target_distance(c);
  if (rr ? distance <= 20 : distance < 80) {
    if (!rr && game->options.skill == 0 && floor(q2m_rnd(game) * 4) != 0) {
      if (rogue) state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
      return false;
    }
    state->attack_state = state->has_melee && (!rr || state->melee_time <= q2m_now(game))
      ? QA_Q2M_ATTACK_MELEE : QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  if (rr && state->attack_state == QA_Q2M_ATTACK_MELEE && state->melee_time > q2m_now(game))
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
  if (!state->has_ranged_attack) {
    if (rr || rogue) state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    return false;
  }
  if (q2m_now(game) < state->attack_finished || (!rr && distance >= 1000)) return false;
  double chance = rr ? (state->stand_ground ? profile->stand_ground : distance <= 20 ? profile->melee
                        : distance <= 440 ? profile->near : distance <= 940 ? profile->mid : profile->far)
                     : (state->stand_ground ? 0.4 : distance < 500 ? 0.1 : distance < 1000 ? 0.02 : 0);
  if (!rr) chance *= game->options.skill == 0 ? 0.5 : game->options.skill >= 2 ? 2 : 1;
  qa_q2f_entity *enemy_entity = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  bool non_solid_enemy = enemy_entity && enemy_entity->solid == QA_Q2F_SOLID_NONE;
  bool take = rr ? ((entity->has_enemy && !game->host->is_player(game->host->ctx, entity->enemy) && non_solid_enemy)
                    || q2m_rnd(game) < chance)
                 : (q2m_rnd(game) < chance || (rogue && non_solid_enemy));
  if (take) {
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    state->attack_finished = q2m_now(game) + (rr ? 0 : 2 * q2m_rnd(game));
    return true;
  }
  if (state->locomotion == QA_Q2M_LOCO_FLY && (!rr || state->strafe_time <= q2m_now(game))) {
    double strafe_chance = (rr || rogue) ? (strcmp(entity->classname, "monster_daedalus") == 0 ? 0.8 : 0.6) : 0.3;
    const char *enemy_class = enemy_entity ? enemy_entity->classname : "";
    if ((rr || rogue) && (strcmp(enemy_class, "tesla") == 0 || (rr && strcmp(enemy_class, "tesla_mine") == 0)))
      strafe_chance = 0;
    else if (rr) strafe_chance *= profile->strafe_scalar;
    if (rr && strafe_chance == 0) return false;
    qa_q2m_attack_state next = q2m_rnd(game) < strafe_chance ? QA_Q2M_ATTACK_SLIDING : QA_Q2M_ATTACK_STRAIGHT;
    if (rr && next != state->attack_state) {
      qa_q2_rerelease_random *random = game->host->rerelease_random;
      if (!random) qa_q2f_error("Rerelease monster strafe timing requires the shared source RNG");
      state->strafe_time = q2m_now(game) + qa_q2_rerelease_random_time_milliseconds(random, 1000, 3000) / 1000.0;
    }
    state->attack_state = next;
  } else if (rr && state->locomotion != QA_Q2M_LOCO_FLY && !state->alt.has_pathing)
    state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
  return false;
}

/* ---- perception maps ---- */
bool qa_q2m_perception_init(qa_q2m_perception *p, qa_q2m_context_lookup contexts) {
  memset(p, 0, sizeof *p);
  p->contexts = contexts;
  p->last_frame = -INFINITY;
  return true;
}
void qa_q2m_perception_destroy(qa_q2m_perception *p) {
  free(p->alerted); free(p->noises); free(p->player_origins); free(p->hostile);
  for (size_t i = 0; i < p->trail_count; i++) free(p->trails[i].points);
  free(p->trails);
  memset(p, 0, sizeof *p);
}
void qa_q2m_perception_set_source_combat_rules(qa_q2m_perception *p, qa_q2m_combat_rules rules,
                                               const qa_q2m_source_combat_hooks *hooks) {
  p->source_combat_rules = rules; p->source_combat_hooks = hooks;
}
void qa_q2m_perception_set_hint_paths(qa_q2m_perception *p, const qa_q2m_hint_hooks *hooks) { p->hint_hooks = hooks; }
bool qa_q2m_perception_current_sight_client(qa_q2m_perception *p, qa_actor_id *out) {
  if (!p->has_sight_client) return false;
  *out = p->sight_client;
  return true;
}
void qa_q2m_perception_bind(qa_q2m_perception *p, qa_q2f_game *game) { p->services = game; }

static double hostile_get(qa_q2m_perception *p, qa_actor_id a) {
  for (size_t i = 0; i < p->hostile_count; i++) if (qa_same_actor(p->hostile[i].actor, a)) return p->hostile[i].time;
  return -1;
}
static void hostile_set(qa_q2m_perception *p, qa_actor_id a, double time) {
  for (size_t i = 0; i < p->hostile_count; i++)
    if (qa_same_actor(p->hostile[i].actor, a)) { p->hostile[i].time = time; return; }
  if (!grow((void **)&p->hostile, &p->hostile_cap, p->hostile_count, sizeof *p->hostile)) return;
  p->hostile[p->hostile_count++] = (qa_q2m_actor_time){a, time};
}
static void alerted_set(qa_q2m_perception *p, qa_actor_id a, qa_q2m_sighting s) {
  for (size_t i = 0; i < p->alerted_count; i++)
    if (qa_same_actor(p->alerted[i].actor, a)) { p->alerted[i].sighting = s; return; }
  if (!grow((void **)&p->alerted, &p->alerted_cap, p->alerted_count, sizeof *p->alerted)) return;
  p->alerted[p->alerted_count++] = (qa_q2m_sighting_entry){a, s};
}
static qa_q2m_trail *trail_of(qa_q2m_perception *p, qa_actor_id a, bool create) {
  for (size_t i = 0; i < p->trail_count; i++) if (qa_same_actor(p->trails[i].actor, a)) return &p->trails[i];
  if (!create || !grow((void **)&p->trails, &p->trail_cap, p->trail_count, sizeof *p->trails)) return NULL;
  qa_q2m_trail *t = &p->trails[p->trail_count++];
  memset(t, 0, sizeof *t);
  t->actor = a;
  return t;
}
static bool origin_get(qa_q2m_perception *p, qa_actor_id a, qa_vec3 *out) {
  for (size_t i = 0; i < p->origin_count; i++)
    if (qa_same_actor(p->player_origins[i].actor, a)) { *out = p->player_origins[i].origin; return true; }
  return false;
}
static void origin_set(qa_q2m_perception *p, qa_actor_id a, qa_vec3 origin) {
  for (size_t i = 0; i < p->origin_count; i++)
    if (qa_same_actor(p->player_origins[i].actor, a)) { p->player_origins[i].origin = origin; return; }
  if (!grow((void **)&p->player_origins, &p->origin_cap, p->origin_count, sizeof *p->player_origins)) return;
  p->player_origins[p->origin_count++] = (qa_q2m_actor_vec){a, origin};
}

bool qa_q2m_perception_capture(qa_q2m_perception *p, qa_q2m_perception_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->sight_client.has_value = p->has_sight_client && qa_q2f_save_actor(&p->sight_client, &out->sight_client.value);
  out->sight.has_value = p->has_sight;
  if (p->has_sight) { (void)qa_q2f_save_actor(&p->sight.actor, &out->sight.value.actor); out->sight.value.time = p->sight.time; }
  qa_q2m_alerted_entry *alerted = p->alerted_count ? malloc(p->alerted_count * sizeof *alerted) : NULL;
  if (p->alerted_count && !alerted) return false;
  for (size_t i = 0; i < p->alerted_count; i++) {
    (void)qa_q2f_save_actor(&p->alerted[i].actor, &alerted[i].actor);
    (void)qa_q2f_save_actor(&p->alerted[i].sighting.actor, &alerted[i].sighting.actor);
    alerted[i].sighting.time = p->alerted[i].sighting.time;
  }
  out->alerted = alerted; out->alerted_count = p->alerted_count;
  out->primary.has_value = p->has_primary;
  if (p->has_primary) {
    (void)qa_q2f_save_actor(&p->primary.actor, &out->primary.value.actor);
    (void)qa_q2f_save_actor(&p->primary.owner, &out->primary.value.owner);
    out->primary.value.origin = p->primary.origin; out->primary.value.time = p->primary.time;
  }
  out->secondary.has_value = p->has_secondary;
  if (p->has_secondary) {
    (void)qa_q2f_save_actor(&p->secondary.actor, &out->secondary.value.actor);
    (void)qa_q2f_save_actor(&p->secondary.owner, &out->secondary.value.owner);
    out->secondary.value.origin = p->secondary.origin; out->secondary.value.time = p->secondary.time;
  }
  qa_q2m_noise_pair_entry *noises = p->noise_count ? malloc(p->noise_count * sizeof *noises) : NULL;
  if (p->noise_count && !noises) return false;
  for (size_t i = 0; i < p->noise_count; i++) {
    (void)qa_q2f_save_actor(&p->noises[i].actor, &noises[i].actor);
    (void)qa_q2f_save_actor(&p->noises[i].primary, &noises[i].primary);
    (void)qa_q2f_save_actor(&p->noises[i].secondary, &noises[i].secondary);
  }
  out->noises = noises; out->noise_count = p->noise_count;
  qa_q2m_trail_entry *trails = p->trail_count ? malloc(p->trail_count * sizeof *trails) : NULL;
  if (p->trail_count && !trails) return false;
  for (size_t i = 0; i < p->trail_count; i++) {
    (void)qa_q2f_save_actor(&p->trails[i].actor, &trails[i].actor);
    trails[i].point_count = p->trails[i].count;
    trails[i].points = NULL;
    if (p->trails[i].count) {
      qa_q2m_trail_point *points = malloc(p->trails[i].count * sizeof *points);
      if (!points) return false;
      memcpy(points, p->trails[i].points, p->trails[i].count * sizeof *points);
      trails[i].points = points;
    }
  }
  out->trails = trails; out->trail_count = p->trail_count;
  qa_q2m_origin_entry *origins = p->origin_count ? malloc(p->origin_count * sizeof *origins) : NULL;
  if (p->origin_count && !origins) return false;
  for (size_t i = 0; i < p->origin_count; i++) {
    (void)qa_q2f_save_actor(&p->player_origins[i].actor, &origins[i].actor);
    origins[i].origin = p->player_origins[i].origin;
  }
  out->player_origins = origins; out->player_origin_count = p->origin_count;
  qa_q2m_hostile_entry *hostile = p->hostile_count ? malloc(p->hostile_count * sizeof *hostile) : NULL;
  if (p->hostile_count && !hostile) return false;
  for (size_t i = 0; i < p->hostile_count; i++) {
    (void)qa_q2f_save_actor(&p->hostile[i].actor, &hostile[i].actor);
    hostile[i].time = p->hostile[i].time;
  }
  out->hostile = hostile; out->hostile_count = p->hostile_count;
  out->has_last_frame = isfinite(p->last_frame);
  out->last_frame = p->last_frame;
  return true;
}
void qa_q2m_perception_checkpoint_free(qa_q2m_perception_checkpoint *cp) {
  free((void *)cp->alerted); free((void *)cp->noises);
  for (size_t i = 0; i < cp->trail_count; i++) free((void *)cp->trails[i].points);
  free((void *)cp->trails); free((void *)cp->player_origins); free((void *)cp->hostile);
  memset(cp, 0, sizeof *cp);
}
void qa_q2m_perception_restore(qa_q2m_perception *p, qa_q2f_game *game, const qa_q2m_perception_checkpoint *cp) {
  p->services = game;
  p->has_sight_client = cp->sight_client.has_value;
  if (cp->sight_client.has_value) p->sight_client = game->host->actors.reference_saved(game->host->actors.ctx, cp->sight_client.value);
  p->has_sight = cp->sight.has_value;
  if (cp->sight.has_value) p->sight = (qa_q2m_sighting){game->host->actors.reference_saved(game->host->actors.ctx, cp->sight.value.actor), cp->sight.value.time};
  p->has_primary = cp->primary.has_value;
  if (cp->primary.has_value) p->primary = (qa_q2m_noise_sighting){
    game->host->actors.reference_saved(game->host->actors.ctx, cp->primary.value.actor),
    game->host->actors.reference_saved(game->host->actors.ctx, cp->primary.value.owner),
    cp->primary.value.origin, cp->primary.value.time};
  p->has_secondary = cp->secondary.has_value;
  if (cp->secondary.has_value) p->secondary = (qa_q2m_noise_sighting){
    game->host->actors.reference_saved(game->host->actors.ctx, cp->secondary.value.actor),
    game->host->actors.reference_saved(game->host->actors.ctx, cp->secondary.value.owner),
    cp->secondary.value.origin, cp->secondary.value.time};
  p->alerted_count = 0; p->noise_count = 0;
  for (size_t i = 0; i < p->trail_count; i++) free(p->trails[i].points);
  p->trail_count = 0; p->origin_count = 0; p->hostile_count = 0;
  for (size_t i = 0; i < cp->alerted_count; i++)
    alerted_set(p, game->host->actors.reference_saved(game->host->actors.ctx, cp->alerted[i].actor),
                (qa_q2m_sighting){game->host->actors.reference_saved(game->host->actors.ctx, cp->alerted[i].sighting.actor), cp->alerted[i].sighting.time});
  for (size_t i = 0; i < cp->noise_count; i++) {
    if (!grow((void **)&p->noises, &p->noise_cap, p->noise_count, sizeof *p->noises)) return;
    p->noises[p->noise_count++] = (qa_q2m_noise_pair){
      game->host->actors.reference_saved(game->host->actors.ctx, cp->noises[i].actor),
      game->host->actors.reference_saved(game->host->actors.ctx, cp->noises[i].primary),
      game->host->actors.reference_saved(game->host->actors.ctx, cp->noises[i].secondary)};
  }
  for (size_t i = 0; i < cp->trail_count; i++) {
    qa_q2m_trail *t = trail_of(p, game->host->actors.reference_saved(game->host->actors.ctx, cp->trails[i].actor), true);
    if (!t) return;
    for (size_t j = 0; j < cp->trails[i].point_count; j++) {
      if (!grow((void **)&t->points, &t->cap, t->count, sizeof *t->points)) return;
      t->points[t->count++] = cp->trails[i].points[j];
    }
  }
  for (size_t i = 0; i < cp->player_origin_count; i++)
    origin_set(p, game->host->actors.reference_saved(game->host->actors.ctx, cp->player_origins[i].actor), cp->player_origins[i].origin);
  for (size_t i = 0; i < cp->hostile_count; i++)
    hostile_set(p, game->host->actors.reference_saved(game->host->actors.ctx, cp->hostile[i].actor), cp->hostile[i].time);
  p->last_frame = cp->has_last_frame ? cp->last_frame : -INFINITY;
}
void qa_q2m_perception_release(qa_q2m_perception *p, qa_actor_id actor) {
  for (size_t i = 0; i < p->noise_count; i++)
    if (qa_same_actor(p->noises[i].actor, actor)) { p->noises[i] = p->noises[--p->noise_count]; break; }
  for (size_t i = 0; i < p->alerted_count; i++)
    if (qa_same_actor(p->alerted[i].actor, actor)) { p->alerted[i] = p->alerted[--p->alerted_count]; break; }
  for (size_t i = 0; i < p->trail_count; i++)
    if (qa_same_actor(p->trails[i].actor, actor)) { free(p->trails[i].points); p->trails[i] = p->trails[--p->trail_count]; break; }
  for (size_t i = 0; i < p->origin_count; i++)
    if (qa_same_actor(p->player_origins[i].actor, actor)) { p->player_origins[i] = p->player_origins[--p->origin_count]; break; }
  for (size_t i = 0; i < p->hostile_count; i++)
    if (qa_same_actor(p->hostile[i].actor, actor)) { p->hostile[i] = p->hostile[--p->hostile_count]; break; }
}
static bool targetable(qa_q2f_game *game, qa_actor_id actor) {
  qa_q2f_target_obs_result observed = game->monster_target(game->ctx, &actor);
  return q2m_live(game, actor) && health_actor(game, actor) > 0 && observed.has_value && !observed.value.notarget;
}
void qa_q2m_perception_begin_frame(qa_q2m_perception *p, qa_q2f_game *game) {
  p->services = game;
  if (p->last_frame == q2m_now(game)) return;
  p->last_frame = q2m_now(game);
  size_t player_count = 0;
  const qa_actor_id *players = game->host->players(game->host->ctx, &player_count);
  double current = 0;
  if (p->has_sight_client) {
    current = -1;
    for (size_t i = 0; i < player_count; i++)
      if (qa_same_actor(players[i], p->sight_client)) { current = (double)i; break; }
  }
  p->has_sight_client = false;
  for (size_t i = 1; i <= player_count; i++) {
    qa_actor_id candidate = players[(size_t)fmod(current + (double)i, (double)player_count)];
    bool rogue_block = p->source_combat_rules == QA_Q2M_RULES_ROGUE
      && ((uint32_t)(game->entity(game->ctx, &candidate) ? game->entity(game->ctx, &candidate)->flags : 0) & 0x8000u) != 0;
    if (targetable(game, candidate) && !rogue_block) { p->sight_client = candidate; p->has_sight_client = true; break; }
  }
  for (size_t i = 0; i < player_count; i++) {
    qa_actor_id player = players[i];
    qa_body_state_result body = game->host->bodies.read(game->host->bodies.ctx, player);
    if (!body.has_value || health_actor(game, player) <= 0) continue;
    qa_q2f_target_obs_result observed = game->monster_target(game->ctx, &player);
    if (!observed.has_value) continue;
    qa_vec3 player_eye = body.value.origin;
    player_eye.z = body.value.origin.z + observed.value.view_height;
    qa_q2m_trail *trail = trail_of(p, player, false);
    bool has_previous = trail && trail->count > 0;
    bool push = !has_previous
      || q2m_trace(game, player_eye, trail->points[trail->count - 1].origin, NULL, &player, QA_Q2M_MASK_OPAQUE).fraction != 1;
    if (push) {
      qa_vec3 old_origin;
      if (!origin_get(p, player, &old_origin)) old_origin = body.value.origin;
      double yaw = !has_previous ? body.value.angles.y
        : qa_vector_to_angles_with_rule(qa_sub3_double(old_origin, trail->points[trail->count - 1].origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
      if (!trail) trail = trail_of(p, player, true);
      if (trail && grow((void **)&trail->points, &trail->cap, trail->count, sizeof *trail->points)) {
        trail->points[trail->count++] = (qa_q2m_trail_point){old_origin, q2m_now(game), yaw};
        if (trail->count > 8) {
          memmove(trail->points, trail->points + 1, (trail->count - 1) * sizeof *trail->points);
          trail->count--;
        }
      }
    }
    origin_set(p, player, body.value.origin);
  }
}
void qa_q2m_perception_report_noise(qa_q2m_perception *p, qa_actor_id actor, qa_vec3 origin, bool secondary) {
  qa_q2f_game *game = p->services;
  if (!game || !q2m_live(game, actor) || strcmp(game->options.mode, "deathmatch") == 0) return;
  qa_q2f_entity *actor_entity = game->entity(game->ctx, &actor);
  if (((uint32_t)(actor_entity ? actor_entity->flags : 0) & (uint32_t)QA_Q2M_FL_NOTARGET) != 0) return;
  qa_q2m_noise_pair *pair = NULL;
  for (size_t i = 0; i < p->noise_count; i++) if (qa_same_actor(p->noises[i].actor, actor)) pair = &p->noises[i];
  if (!pair) {
    qa_q2f_entity *primary = game->create(game->ctx, "player_noise", NULL);
    qa_q2f_entity *other = game->create(game->ctx, "player_noise", NULL);
    qa_q2f_entity *both[2] = {primary, other};
    for (int i = 0; i < 2; i++) {
      both[i]->owner = actor; both[i]->has_owner = true;
      both[i]->visible = false;
      both[i]->server_flags = (double)((uint32_t)both[i]->server_flags | 1u);
      qa_q2f_body_changes c; memset(&c, 0, sizeof c);
      c.has_bounds = true; c.bounds = (qa_bounds){q2m_v3(-8, -8, -8), q2m_v3(8, 8, 8)};
      q2m_move(game, both[i], c, true, false);
    }
    if (!grow((void **)&p->noises, &p->noise_cap, p->noise_count, sizeof *p->noises)) return;
    pair = &p->noises[p->noise_count++];
    *pair = (qa_q2m_noise_pair){actor, primary->actor.id, other->actor.id};
  }
  qa_actor_id noise_id = secondary ? pair->secondary : pair->primary;
  qa_q2f_entity *noise = game->entity(game->ctx, &noise_id);
  if (!noise) return;
  qa_q2f_body_changes c; memset(&c, 0, sizeof c);
  c.has_origin = true; c.origin = origin;
  q2m_move(game, noise, c, false, false);
  qa_q2m_noise_sighting record = {noise->actor.id, actor, origin, q2m_now(game)};
  if (secondary) { p->secondary = record; p->has_secondary = true; }
  else { p->primary = record; p->has_primary = true; }
}

static bool close_enough(qa_q2m_context *c, qa_vec3 origin, double distance, const qa_actor_id *actor) {
  qa_body_state body = q2m_body(c->game, c->entity);
  qa_bounds bounds = {q2m_zero(), q2m_zero()};
  if (actor) {
    qa_body_state_result target = c->game->host->bodies.read(c->game->host->bodies.ctx, *actor);
    if (target.has_value) bounds = target.value.bounds;
  }
  return origin.x + bounds.min.x <= body.origin.x + body.bounds.max.x + distance
      && origin.x + bounds.max.x >= body.origin.x + body.bounds.min.x - distance
      && origin.y + bounds.min.y <= body.origin.y + body.bounds.max.y + distance
      && origin.y + bounds.max.y >= body.origin.y + body.bounds.min.y - distance
      && origin.z + bounds.min.z <= body.origin.z + body.bounds.max.z + distance
      && origin.z + bounds.max.z >= body.origin.z + body.bounds.min.z - distance;
}
static bool visual_candidate(qa_q2m_perception *p, qa_q2m_context *c, qa_actor_id actor) {
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  qa_q2f_entity *entity = c->entity;
  qa_body_state_result target = game->host->bodies.read(game->host->bodies.ctx, actor);
  if (!target.has_value || !q2m_live(game, actor)) return false;
  bool had_enemy = entity->has_enemy;
  qa_actor_id saved = entity->enemy;
  entity->enemy = actor; entity->has_enemy = true;
  double distance = qa_q2m_target_distance(c);
  entity->enemy = saved; entity->has_enemy = had_enemy;
  if (q2m_rr(game)) {
    if (distance > 940) return false;
    return (distance <= 440 && hostile_get(p, actor) >= q2m_now(game) && ((uint32_t)entity->spawnflags & 1u) == 0)
        || (qa_q2m_visible(c, &actor) && (distance <= 20 || qa_q2m_in_front(c, actor)));
  }
  qa_q2f_target_obs_result observed = game->monster_target(game->ctx, &actor);
  if (!observed.has_value || distance >= 1000
      || (observed.value.has_light_level && observed.value.light_level <= 5)
      || !qa_q2m_visible(c, &actor)) return false;
  double hostile_until = -1;
  qa_q2m_context *actor_context = p->contexts.get ? p->contexts.get(p->contexts.ctx, actor) : NULL;
  hostile_until = actor_context ? actor_context->state->show_hostile : hostile_get(p, actor);
  if (distance >= 80 && distance < 500 && hostile_until < q2m_now(game) && !qa_q2m_in_front(c, actor)) return false;
  if (distance >= 500 && !qa_q2m_in_front(c, actor)) return false;
  return !state->good_guy;
}

bool qa_q2m_perception_find_target(qa_q2m_perception *p, qa_q2m_context *c) {
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  if (state->good_guy || state->combat_point || state->dead) return false;
  double now = q2m_now(game);
  double window = !q2m_rr(game) ? 0.1 : game->host->frame_seconds(game->host->ctx);
  bool has_candidate = false;
  qa_actor_id candidate;
  memset(&candidate, 0, sizeof candidate);
  bool has_noise = false;
  qa_q2m_noise_sighting noise;
  memset(&noise, 0, sizeof noise);
  if (q2m_rr(game)) {
    size_t player_count = 0;
    const qa_actor_id *players = game->host->players(game->host->ctx, &player_count);
    size_t candidate_count = 0;
    for (size_t i = 0; i < player_count; i++) if (targetable(game, players[i])) {
      qa_body_state_result target = game->host->bodies.read(game->host->bodies.ctx, players[i]);
      if (target.has_value && (close_enough(c, target.value.origin, 0, &players[i])
          || (qa_q2m_in_front(c, players[i]) && qa_q2m_visible(c, &players[i])))) candidate_count++;
    }
    if (candidate_count > 0) {
      size_t pick = (size_t)floor(q2m_rnd(game) * (double)candidate_count);
      size_t seen = 0;
      for (size_t i = 0; i < player_count; i++) if (targetable(game, players[i])) {
        qa_body_state_result target = game->host->bodies.read(game->host->bodies.ctx, players[i]);
        if (target.has_value && (close_enough(c, target.value.origin, 0, &players[i])
            || (qa_q2m_in_front(c, players[i]) && qa_q2m_visible(c, &players[i])))) {
          if (seen++ == pick) { candidate = players[i]; has_candidate = true; }
        }
      }
    }
    if (has_candidate && entity->has_enemy && qa_same_actor(candidate, entity->enemy) && !state->has_sound_target) return false;
    if (!has_candidate && ((uint32_t)entity->spawnflags & 1u) == 0) {
      for (size_t i = 0; i < p->alerted_count; i++) {
        qa_q2m_sighting sight = p->alerted[i].sighting;
        if (sight.time >= now - window && visual_candidate(p, c, sight.actor)) { candidate = sight.actor; has_candidate = true; break; }
      }
    }
  } else if (p->has_sight && p->sight.time >= now - window && ((uint32_t)entity->spawnflags & 1u) == 0) {
    candidate = p->sight.actor; has_candidate = true;
    qa_q2f_entity *candidate_entity = game->entity(game->ctx, &candidate);
    if (candidate_entity && candidate_entity->has_enemy && entity->has_enemy
        && qa_same_actor(candidate_entity->enemy, entity->enemy)) return false;
  }
  if (!has_candidate) {
    if (p->has_primary && p->primary.time >= now - window) { noise = p->primary; has_noise = true; }
    else if (!entity->has_enemy && ((uint32_t)entity->spawnflags & 1u) == 0
             && p->has_secondary && p->secondary.time >= now - window) { noise = p->secondary; has_noise = true; }
    if (has_noise) { candidate = noise.actor; has_candidate = true; }
    else if (!q2m_rr(game) && p->has_sight_client) { candidate = p->sight_client; has_candidate = true; }
  }
  if (!has_candidate || !q2m_live(game, candidate)) return false;
  if (state->alt.hint_path && strcmp(game->options.mode, "coop") == 0) has_noise = false;
  if (entity->has_enemy && qa_same_actor(candidate, entity->enemy)
      && !(q2m_rr(game) && has_noise && state->has_sound_target)) return true;
  qa_q2f_entity *candidate_entity = game->entity(game->ctx, &candidate);
  if (has_noise) {
    qa_q2f_entity *owner_entity = game->entity(game->ctx, &noise.owner);
    if (((uint32_t)(owner_entity ? owner_entity->flags : 0) & (uint32_t)QA_Q2M_FL_NOTARGET) != 0) return false;
    qa_vec3 origin = q2m_body(game, entity).origin;
    if (((uint32_t)entity->spawnflags & 1u) != 0 ? !qa_q2m_visible(c, &candidate)
                                                  : !game->host->in_phs(game->host->ctx, origin, noise.origin)) return false;
    if (qa_hypot3(qa_sub3_double(noise.origin, origin)) > 1000 || !game->host->areas_connected(game->host->ctx, origin, noise.origin)) return false;
    state->ideal_yaw = qa_vector_to_angles_with_rule(qa_sub3_double(noise.origin, origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
    if (!state->manual_steering) qa_q2m_change_yaw(c);
    if (state->temporary_stand_ground) { state->stand_ground = false; state->temporary_stand_ground = false; }
    state->has_sound_target = true;
    state->sound_target = (qa_q2m_sound_target){noise.actor, noise.owner, noise.origin, noise.time};
    entity->enemy = candidate; entity->has_enemy = true;
  } else {
    if (game->host->is_player(game->host->ctx, candidate)) {
      if (!targetable(game, candidate)) return false;
    } else if (game->host->is_monster(game->host->ctx, candidate)) {
      if (!candidate_entity || !candidate_entity->has_enemy || !targetable(game, candidate_entity->enemy)) return false;
    } else return false;
    if (!visual_candidate(p, c, candidate)) return false;
    state->has_sound_target = false;
    if (game->host->is_player(game->host->ctx, candidate)) { entity->enemy = candidate; entity->has_enemy = true; }
    else if (candidate_entity && candidate_entity->has_enemy) { entity->enemy = candidate_entity->enemy; entity->has_enemy = true; }
    else entity->has_enemy = false;
    if (!entity->has_enemy || !game->host->is_player(game->host->ctx, entity->enemy)) { entity->has_enemy = false; return false; }
  }
  if (state->alt.hint_path && p->hint_hooks) { p->hint_hooks->stop(p->hint_hooks->ctx, c); return true; }
  qa_q2m_perception_found_target(p, c);
  if (!state->has_sound_target) {
    if (!q2m_rr(game) || !state->close_sight_tripped) c->dispatch(c->impl_ctx, c, "$sight");
    state->close_sight_tripped = true;
  }
  return true;
}
void qa_q2m_perception_hunt_target(qa_q2m_perception *p, qa_q2m_context *c) {
  (void)p;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  c->entity->goal = c->entity->enemy; c->entity->has_goal = c->entity->has_enemy;
  if (c->state->stand_ground) c->stand(c->impl_ctx, c); else c->run(c->impl_ctx, c);
  c->state->ideal_yaw = qa_vector_to_angles_with_rule(
      qa_sub3_double(enemy.origin, q2m_body(c->game, c->entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  if (!q2m_rr(c->game) && !c->state->stand_ground) c->state->attack_finished = q2m_now(c->game) + 1;
}
void qa_q2m_perception_found_target(qa_q2m_perception *p, qa_q2m_context *c) {
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  if (entity->has_enemy && game->host->is_player(game->host->ctx, entity->enemy)) {
    qa_q2f_entity *player = game->entity(game->ctx, &entity->enemy);
    if (player && p->source_combat_rules == QA_Q2M_RULES_ROGUE)
      player->flags = (double)((uint32_t)player->flags & ~0x8000u);
    qa_q2m_sighting record = {entity->actor.id, q2m_now(game)};
    p->sight = record; p->has_sight = true;
    alerted_set(p, entity->enemy, record);
    hostile_set(p, entity->enemy, q2m_now(game) + 1);
  }
  state->show_hostile = q2m_now(game) + 1;
  if (q2m_rr(game)) {
    if (state->trail_time == 0) state->attack_finished = q2m_now(game) + 0.6;
    state->attack_finished += game->options.skill == 0 ? 0.4 : game->options.skill == 1 ? 0.2 : 0;
    state->has_saved_goal = true; state->saved_goal = enemy.origin;
    state->blind_fire_target = qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity, -0.1));
    state->blind_fire_delay = 0;
  }
  state->last_sighting = enemy.origin; state->trail_time = q2m_now(game);
  if (p->source_combat_rules == QA_Q2M_RULES_ROGUE && !q2m_rr(game)) state->blind_fire_target = enemy.origin;
  const qa_monster_mission *mission = p->contexts.mission ? p->contexts.mission(p->contexts.ctx, entity->actor.id) : NULL;
  if (mission) {
    mission->found_target(mission->ctx);
    qa_monster_combat_route route = mission->combat_route(mission->ctx);
    if (route.goal) {
      state->combat_point = true;
      state->has_move_target = true; state->move_target = *route.goal;
      entity->goal = *route.goal; entity->has_goal = true;
      state->pause_time = 0;
      c->run(c->impl_ctx, c);
      return;
    }
  }
  if (state->combat_point) return;
  if (strlen(state->combat_target) == 0) { qa_q2m_perception_hunt_target(p, c); return; }
  qa_q2f_entity *target = game->pick_target(game->ctx, state->combat_target);
  if (!target) {
    char message[512];
    (void)snprintf(message, sizeof message, "%s: combattarget %s not found", entity->classname, state->combat_target);
    game->host->diagnostic(game->host->ctx, message);
    qa_q2m_perception_hunt_target(p, c);
    return;
  }
  state->combat_target = ""; entity->combat_target = "";
  state->combat_point = true;
  state->has_move_target = true; state->move_target = target->actor.id;
  entity->goal = target->actor.id; entity->has_goal = true;
  if (!q2m_rr(game)) target->targetname = "";
  state->pause_time = 0;
  c->run(c->impl_ctx, c);
}
void qa_q2m_perception_react_to_damage(qa_q2m_perception *p, qa_q2m_context *c, const qa_actor_id *attacker_ptr) {
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  if (!attacker_ptr) return;
  qa_actor_id attacker = *attacker_ptr;
  if (!game->host->is_player(game->host->ctx, attacker) && !game->host->is_monster(game->host->ctx, attacker)) return;
  if (p->source_combat_hooks && p->source_combat_hooks->before_react
      && p->source_combat_hooks->before_react(p->source_combat_hooks->ctx, c, attacker)) return;
  if (qa_same_actor(attacker, entity->actor.id) || (entity->has_enemy && qa_same_actor(attacker, entity->enemy))) return;
  qa_q2f_entity *other = game->entity(game->ctx, &attacker);
  qa_q2m_context *attacker_context = p->contexts.get ? p->contexts.get(p->contexts.ctx, attacker) : NULL;
  if (state->good_guy && (game->host->is_player(game->host->ctx, attacker)
      || (attacker_context && attacker_context->state->good_guy)
      || (other && p->source_combat_hooks && p->source_combat_hooks->is_good_guy
          && p->source_combat_hooks->is_good_guy(p->source_combat_hooks->ctx, other)))) return;
  if (game->host->is_player(game->host->ctx, attacker)) {
    state->has_sound_target = false;
    if (entity->has_enemy && game->host->is_player(game->host->ctx, entity->enemy)) {
      if (qa_q2m_visible_enemy(c)) { state->has_old_enemy = true; state->old_enemy = attacker; return; }
      state->has_old_enemy = true; state->old_enemy = entity->enemy;
    }
    entity->enemy = attacker; entity->has_enemy = true;
  } else if (other) {
    bool ignore_shots;
    if (p->source_combat_rules == QA_Q2M_RULES_ROGUE || q2m_rr(game))
      ignore_shots = state->ignore_shots || (attacker_context && attacker_context->state->ignore_shots);
    else ignore_shots = strcmp(other->classname, "monster_tank") == 0 || strcmp(other->classname, "monster_supertank") == 0
                     || strcmp(other->classname, "monster_makron") == 0 || strcmp(other->classname, "monster_jorg") == 0;
    bool retaliate = (((uint32_t)entity->flags & 3u) == ((uint32_t)other->flags & 3u)
                     && strcmp(entity->classname, other->classname) != 0 && !ignore_shots)
                  || (other->has_enemy && qa_same_actor(other->enemy, entity->actor.id));
    if (entity->has_enemy && game->host->is_player(game->host->ctx, entity->enemy)) {
      state->has_old_enemy = true; state->old_enemy = entity->enemy;
    }
    if (retaliate) { entity->enemy = attacker; entity->has_enemy = true; }
    else if (other->has_enemy && !qa_same_actor(other->enemy, entity->actor.id)) {
      entity->enemy = other->enemy; entity->has_enemy = true;
    } else return;
  } else { entity->enemy = attacker; entity->has_enemy = true; }
  if (!state->ducked) qa_q2m_perception_found_target(p, c);
}

bool qa_q2m_perception_check_attack(qa_q2m_perception *p, qa_q2m_context *c, qa_q2m_check_attack_fn check) {
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  if (state->combat_point) return false;
  if (state->has_sound_target) {
    qa_q2m_sound_target fresh = state->sound_target;
    if (p->has_primary && qa_same_actor(p->primary.actor, state->sound_target.actor))
      fresh = (qa_q2m_sound_target){p->primary.actor, p->primary.owner, p->primary.origin, p->primary.time};
    else if (p->has_secondary && qa_same_actor(p->secondary.actor, state->sound_target.actor))
      fresh = (qa_q2m_sound_target){p->secondary.actor, p->secondary.owner, p->secondary.origin, p->secondary.time};
    state->sound_target = fresh;
    if (q2m_now(game) - fresh.time <= 5) { state->show_hostile = q2m_now(game) + 1; return false; }
    if (entity->has_goal && entity->has_enemy && qa_same_actor(entity->goal, entity->enemy)) {
      entity->has_goal = state->has_move_target;
      if (state->has_move_target) entity->goal = state->move_target;
    }
    state->has_sound_target = false;
    if (state->temporary_stand_ground) { state->stand_ground = false; state->temporary_stand_ground = false; }
  }
  qa_body_state enemy_body;
  bool has_enemy_body = qa_q2m_enemy_body(c, &enemy_body);
  double enemy_health = qa_q2m_health(game, entity->has_enemy ? &entity->enemy : NULL);
  bool enemy_gone = !has_enemy_body || (state->medic ? enemy_health > 0
                                       : state->brutal ? (!q2m_rr(game) && enemy_health <= -80) : enemy_health <= 0);
  if (enemy_gone) {
    if (p->source_combat_rules == QA_Q2M_RULES_ROGUE) state->medic = false;
    entity->has_enemy = false; state->close_sight_tripped = false;
    if (q2m_rr(game)) entity->has_goal = false;
    if (state->has_old_enemy && qa_q2m_health(game, &state->old_enemy) > 0) {
      entity->enemy = state->old_enemy; entity->has_enemy = true; state->has_old_enemy = false;
      qa_q2m_perception_hunt_target(p, c);
      has_enemy_body = qa_q2m_enemy_body(c, &enemy_body);
    } else if (p->source_combat_rules == QA_Q2M_RULES_ROGUE && p->source_combat_hooks) {
      qa_actor_id recovered;
      bool has_recovered = p->source_combat_hooks->recover_enemy
        && p->source_combat_hooks->recover_enemy(p->source_combat_hooks->ctx, c, &recovered);
      if (has_recovered) {
        entity->enemy = recovered; entity->has_enemy = true; state->has_old_enemy = false;
        qa_q2m_perception_hunt_target(p, c);
        has_enemy_body = qa_q2m_enemy_body(c, &enemy_body);
      } else {
        if (state->has_move_target) { entity->goal = state->move_target; entity->has_goal = true; c->walk(c->impl_ctx, c); }
        else { state->pause_time = q2m_now(game) + 100000000; c->stand(c->impl_ctx, c); }
        return true;
      }
    } else {
      if (state->has_move_target && (!q2m_rr(game) || !state->stand_ground)) {
        entity->goal = state->move_target; entity->has_goal = true; c->walk(c->impl_ctx, c);
      } else { state->pause_time = q2m_now(game) + 100000000; c->stand(c->impl_ctx, c); }
      return true;
    }
  }
  if (!has_enemy_body) return false;
  bool enemy_visible = qa_q2m_visible_enemy(c);
  if (enemy_visible) {
    state->search_time = q2m_now(game) + 5; state->last_sighting = enemy_body.origin;
    if (p->source_combat_rules == QA_Q2M_RULES_ROGUE && !q2m_rr(game)) {
      state->lost_sight = false; state->trail_time = q2m_now(game);
      state->blind_fire_target = enemy_body.origin; state->blind_fire_delay = 0;
    }
    if (q2m_rr(game)) {
      state->had_visibility = true; state->lost_sight = false;
      state->has_saved_goal = true; state->saved_goal = enemy_body.origin;
      state->trail_time = q2m_now(game);
      state->blind_fire_target = qa_add3_double(enemy_body.origin, qa_scale3_double(enemy_body.velocity, -0.1));
      state->blind_fire_delay = 0;
      if (entity->has_enemy) hostile_set(p, entity->enemy, q2m_now(game) + 1);
    }
  }
  bool rr = q2m_rr(game);
  if (!rr && p->source_combat_rules == QA_Q2M_RULES_ROGUE) {
    bool selected = check.fn(check.ctx, c);
    if (!selected) return false;
    if (state->attack_state == QA_Q2M_ATTACK_MISSILE || state->attack_state == QA_Q2M_ATTACK_MELEE
        || state->attack_state == QA_Q2M_ATTACK_BLIND) {
      state->ideal_yaw = qa_vector_to_angles_with_rule(
          qa_sub3_double(enemy_body.origin, q2m_body(game, entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
      if (!state->manual_steering) qa_q2m_change_yaw(c);
      if (qa_q2m_facing_ideal(c)) {
        if (state->attack_state == QA_Q2M_ATTACK_MELEE) { c->melee(c->impl_ctx, c); state->attack_state = QA_Q2M_ATTACK_STRAIGHT; }
        else {
          c->attack(c->impl_ctx, c);
          if (state->attack_state == QA_Q2M_ATTACK_MISSILE || state->attack_state == QA_Q2M_ATTACK_BLIND)
            state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
        }
      }
      return true;
    }
    return enemy_visible;
  }
  bool selected = false;
  if (rr && state->check_attack_time <= q2m_now(game)) {
    state->check_attack_time = q2m_now(game) + 0.1;
    selected = check.fn(check.ctx, c);
  }
  if (state->attack_state == QA_Q2M_ATTACK_MISSILE || state->attack_state == QA_Q2M_ATTACK_MELEE
      || (rr && state->attack_state == QA_Q2M_ATTACK_BLIND)) {
    state->ideal_yaw = qa_vector_to_angles_with_rule(
        qa_sub3_double(enemy_body.origin, q2m_body(game, entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
    if (!state->manual_steering) qa_q2m_change_yaw(c);
    if (qa_q2m_facing_ideal(c)) {
      if (state->attack_state == QA_Q2M_ATTACK_MELEE) c->melee(c->impl_ctx, c);
      else {
        c->attack(c->impl_ctx, c);
        if (rr) state->attack_finished = q2m_now(game) + 1 + q2m_rnd(game);
      }
      if (!rr || state->attack_state == QA_Q2M_ATTACK_MISSILE || state->attack_state == QA_Q2M_ATTACK_BLIND
          || state->attack_state == QA_Q2M_ATTACK_MELEE)
        state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    }
    return true;
  }
  if (rr) return selected;
  return enemy_visible && check.fn(check.ctx, c);
}

static qa_vec3 pursuit_goal(qa_q2m_perception *p, qa_q2m_context *c, double distance) {
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state body = q2m_body(game, entity);
  bool new_goal = false;
  if (!state->lost_sight) {
    state->lost_sight = true; state->pursuit_last_seen = true;
    state->pursue_next = false; state->pursue_temporary = false; new_goal = true;
  }
  if (state->pursue_next) {
    state->pursue_next = false; state->search_time = q2m_now(game) + 5;
    if (state->pursue_temporary && state->has_saved_goal) {
      state->pursue_temporary = false; state->last_sighting = state->saved_goal; new_goal = true;
    } else {
      qa_q2m_trail *trail = entity->has_enemy ? trail_of(p, entity->enemy, false) : NULL;
      const qa_q2m_trail_point *marker = NULL;
      size_t marker_index = 0;
      if (trail) for (size_t i = 0; i < trail->count; i++)
        if (trail->points[i].time > state->trail_time) { marker = &trail->points[i]; marker_index = i; break; }
      if (state->pursuit_last_seen && marker) {
        const qa_q2m_trail_point *prior = marker_index > 0 ? &trail->points[marker_index - 1] : NULL;
        qa_trace_result trace = q2m_trace(game, body.origin, marker->origin, NULL, &entity->actor.id, QA_Q2M_MASK_OPAQUE);
        if (trace.fraction != 1 && prior
            && q2m_trace(game, body.origin, prior->origin, NULL, &entity->actor.id, QA_Q2M_MASK_OPAQUE).fraction == 1)
          marker = prior;
      }
      state->pursuit_last_seen = false;
      if (marker) {
        state->last_sighting = marker->origin; state->trail_time = marker->time; state->ideal_yaw = marker->yaw;
        qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
        changes.has_angles = true; changes.angles = body.angles; changes.angles.y = marker->yaw;
        q2m_move(game, entity, changes, true, false);
        new_goal = true;
      }
    }
  }
  double d1 = qa_hypot3(qa_sub3_double(state->last_sighting, body.origin));
  if (d1 <= distance) state->pursue_next = true;
  if (new_goal && d1 > 0) {
    qa_trace_result center = q2m_trace(game, body.origin, state->last_sighting, &body.bounds, &entity->actor.id,
                                       qa_q2m_monster_solid_mask(game));
    if (center.fraction < 1) {
      double d2 = d1 * (center.fraction + 1) * 0.5;
      state->ideal_yaw = qa_vector_to_angles_with_rule(qa_sub3_double(state->last_sighting, body.origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
      qa_vec3 basis_angles = body.angles; basis_angles.y = state->ideal_yaw;
      qa_angle_vectors basis = qa_math_angle_vectors_rule(basis_angles, QA_ANGLE_RULE_NATIVE);
      qa_vec3 left_end = qa_add3_double(body.origin, qa_add3_double(qa_scale3_double(basis.forward, d2), qa_scale3_double(basis.right, -16)));
      qa_vec3 right_end = qa_add3_double(body.origin, qa_add3_double(qa_scale3_double(basis.forward, d2), qa_scale3_double(basis.right, 16)));
      qa_trace_result left = q2m_trace(game, body.origin, left_end, &body.bounds, &entity->actor.id, qa_q2m_monster_solid_mask(game));
      qa_trace_result right = q2m_trace(game, body.origin, right_end, &body.bounds, &entity->actor.id, qa_q2m_monster_solid_mask(game));
      double center_fraction = d1 * center.fraction / d2;
      double side = left.fraction >= center_fraction && left.fraction > right.fraction ? -16
                  : right.fraction >= center_fraction && right.fraction > left.fraction ? 16 : 0;
      if (side != 0) {
        double fraction = side < 0 ? left.fraction : right.fraction;
        state->has_saved_goal = true; state->saved_goal = state->last_sighting;
        state->pursue_temporary = true;
        double forward = fraction < 1 ? d2 * fraction * 0.5 : d2;
        state->last_sighting = qa_add3_double(body.origin, qa_add3_double(qa_scale3_double(basis.forward, forward), qa_scale3_double(basis.right, side)));
        state->ideal_yaw = qa_vector_to_angles_with_rule(qa_sub3_double(state->last_sighting, body.origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
      }
      qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_angles = true; changes.angles = body.angles; changes.angles.y = state->ideal_yaw;
      q2m_move(game, entity, changes, true, false);
    }
  }
  return state->last_sighting;
}
bool qa_q2m_perception_move_to_goal(qa_q2m_perception *p, qa_q2m_context *c, double distance) {
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  if (state->locomotion == QA_Q2M_LOCO_STATIONARY
      || (q2m_body(game, entity).ground == NULL && state->locomotion == QA_Q2M_LOCO_WALK)) return false;
  qa_body_state enemy;
  bool has_enemy = qa_q2m_enemy_body(c, &enemy);
  bool has_goal = false;
  qa_vec3 goal;
  if (entity->has_goal) {
    qa_body_state_result goal_r = game->host->bodies.read(game->host->bodies.ctx, entity->goal);
    if (goal_r.has_value) { goal = goal_r.value.origin; has_goal = true; }
  }
  if (!state->alt.hint_path && !state->combat_point && !state->has_sound_target && has_enemy && !qa_q2m_visible_enemy(c)) {
    goal = pursuit_goal(p, c, distance); has_goal = true;
  } else if (!state->alt.hint_path && !state->combat_point && !state->has_sound_target && has_enemy) {
    state->lost_sight = false; state->last_sighting = enemy.origin; state->trail_time = q2m_now(game);
  }
  if (!has_goal) return false;
  if (!state->alt.hint_path && has_enemy && !state->combat_point && !state->has_sound_target
      && close_enough(c, enemy.origin, distance, entity->has_enemy ? &entity->enemy : NULL)) return true;
  if ((floor(q2m_rnd(game) * 4) != 1 || (p->source_combat_rules == QA_Q2M_RULES_ROGUE && state->charging))
      && qa_q2m_step_direction(c, state->ideal_yaw, distance)) return true;
  if (c->consume_source_blocked(c->impl_ctx, c)) return false;
  if (!q2m_live(game, entity->actor.id)) return false;
  if (q2m_rr(game) && p->source_combat_rules != QA_Q2M_RULES_ROGUE && c->blocked(c->impl_ctx, c, distance)) return true;
  return qa_q2m_chase_direction(c, goal, distance);
}


