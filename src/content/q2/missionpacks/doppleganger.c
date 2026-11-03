/* Port of src/content/q2/missionpacks/doppleganger.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/doppleganger.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <string.h>

#include "missionpacks_internal.h"
#include "qa/content/q2/missionpacks/monsters/spawn.h"
#include "qa/content/q2/missionpacks/projectiles/common.h"

static void timeout_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  qa_q2_entity *body = q2mp_ent(game, entity->has_team_chain, entity->team_chain);
  if (body != NULL) qa_q2mp_explode_default(body, game);
  qa_q2mp_explode_default(entity, game);
}
static void pain_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_pain_reaction reaction) {
  (void)ctx; (void)game;
  if (reaction.attacker != NULL) entity->enemy = *reaction.attacker;
  entity->has_enemy = reaction.attacker != NULL;
}
static void die_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  qa_q2mp_doppleganger *d = ctx;
  qa_body_state_result enemy = entity->has_enemy ? q2mp_bread(game, entity->enemy)
      : (qa_body_state_result){false, {q2mp_zero(), q2mp_zero(), q2mp_zero(), {q2mp_zero(), q2mp_zero()}, NULL}};
  if (enemy.has_value && !(entity->has_team_master && qa_same_actor(entity->enemy, entity->team_master))) {
    qa_q2_entity *sphere = qa_q2mp_spheres_launch(d->spheres, entity, game,
        q2mp_len(q2mp_sub(enemy.value.origin, q2mp_body(game, entity).origin)) > 768 ? QA_Q2MP_SPHERE_HUNTER
                                                                                     : QA_Q2MP_SPHERE_VENGEANCE, true);
    if (sphere->pain.fn != NULL) {
      qa_pain_reaction pain;
      memset(&pain, 0, sizeof pain);
      pain.attack = reaction.pain.attack; pain.self = sphere->actor; pain.attacker = reaction.pain.attacker;
      pain.damage = 0; pain.kick = 0;
      sphere->pain.fn(sphere->pain.ctx, sphere, game, pain);
    }
  }
  timeout_fn(ctx, entity, game);
}
static void body_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_doppleganger *d = ctx;
  qa_vec3 angles = q2mp_body(game, entity).angles;
  double yaw = (double)(qa_js_to_int32(trunc(angles.y * 65536 / 360)) & 65535) * (360.0 / 65536);
  if (fabs(entity->pos1.y - yaw) < 2) {
    if (entity->timestamp < q2mp_now(game) && q2mp_rnd(game) < 0.1) {
      entity->pos1 = q2mp_v3(entity->pos1.x, q2mp_rnd(game) * 350, entity->pos1.z);
      entity->timestamp = q2mp_now(game) + 1;
    }
  } else {
    double move = entity->pos1.y - yaw;
    if (entity->pos1.y > yaw && move >= 180) move -= 360;
    else if (entity->pos1.y <= yaw && move <= -180) move += 360;
    double clamped = move < -entity->speed ? -entity->speed : move > entity->speed ? entity->speed : move;
    qa_q2_body_changes c = q2mp_changes();
    c.has_angles = true;
    c.angles = q2mp_v3(angles.x, fmod(yaw + clamped + 360, 360), angles.z);
    q2mp_move(game, entity, c, false, false);
  }
  if (++entity->frame > 39) entity->frame = 0;
  game->show(game->ctx, entity);
  game->schedule(game->ctx, entity, 0.1, d->body_think);
}

bool qa_q2mp_doppleganger_use(qa_q2mp_doppleganger *d, qa_q2_entity *owner, qa_q2_game_services *game) {
  qa_body_state body = q2mp_body(game, owner);
  const qa_q2w_input *input = qa_q2w_ballistics_input(&d->weapons->base, owner->actor.id);
  qa_vec3 angles = input != NULL ? input->angles : body.angles;
  qa_vec3 forward = q2mp_axes(q2mp_v3(0, angles.y, 0)).forward;
  qa_vec3 point;
  if (!qa_q2mpm_find_rogue_spawn_point(game, q2mp_add(body.origin, q2mp_scale(forward, 48)), body.bounds, 32, &point)) return false;
  if (!qa_q2mpm_check_rogue_ground_spawn_point(game, point, body.bounds, 64, -1)) return false;
  if (!qa_shared_inventory_table_consume(game->host->inventory, owner->actor, "q2:item_doppleganger", 1)) return false;
  (void)qa_q2mpm_rogue_spawn_grow(game, point, 0);
  (void)qa_q2mp_doppleganger_fire(d, owner, game, point, forward);
  return true;
}
qa_q2_entity *qa_q2mp_doppleganger_fire(qa_q2mp_doppleganger *d, qa_q2_entity *owner, qa_q2_game_services *game,
                                         qa_vec3 start, qa_vec3 direction) {
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2mp_doppleganger_callbacks(d));
  qa_q2_entity *base = game->create(game->ctx, "doppleganger", NULL, 0);
  qa_vec3 angles = q2mp_vta(direction);
  base->team_master = owner->actor.id; base->has_team_master = true;
  base->render_flags = 0x8000; base->damageable_target = true; base->pain = d->pain; base->die = d->die;
  qa_q2_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = start;
  c.has_angles = true; c.angles = q2mp_v3(0, angles.y, angles.z);
  c.has_velocity = true; c.velocity = q2mp_zero();
  c.has_bounds = true; c.bounds = (qa_bounds){q2mp_v3(-16, -16, -24), q2mp_v3(16, 16, 32)};
  q2mp_move(game, base, c, false, false);
  game->motion(game->ctx, base, "toss");
  game->solid(game->ctx, base, "box");
  q2mp_combat_create(game, base->actor, 30, 0, true);
  game->schedule(game->ctx, base, 30, d->timeout);
  qa_q2_entity *body = game->create(game->ctx, "doppleganger_body", NULL, 0);
  body->model = owner->model; body->model2 = owner->model2; body->model3 = owner->model3; body->model4 = owner->model4;
  body->frame = owner->frame; body->old_frame = owner->old_frame;
  body->skin = owner->skin; body->effects = owner->effects; body->render_flags = owner->render_flags;
  body->scale = owner->scale;
  body->team_master = base->actor.id; body->has_team_master = true;
  body->speed = 30;
  qa_q2_body_changes bc = q2mp_changes();
  bc.has_origin = true; bc.origin = q2mp_add(start, q2mp_v3(0, 0, 8));
  bc.has_angles = true; bc.angles = q2mp_body(game, owner).angles;
  q2mp_move(game, body, bc, false, false);
  game->show(game->ctx, body);
  game->schedule(game->ctx, body, q2mp_frame(game), d->body_think);
  base->team_chain = body->actor.id; base->has_team_chain = true;
  return base;
}

void qa_q2mp_doppleganger_init(qa_q2mp_doppleganger *d, qa_q2mp_spheres *spheres, qa_q2w_weapons *weapons) {
  memset(d, 0, sizeof *d);
  d->spheres = spheres;
  d->weapons = weapons;
  d->timeout = (qa_q2_think){d, timeout_fn};
  d->body_think = (qa_q2_think){d, body_think_fn};
  d->pain = (qa_q2_pain){d, pain_fn};
  d->die = (qa_q2_die){d, die_fn};
  qa_q2_callback_definitions spawn = qa_q2mpm_rogue_spawn_callbacks();
  size_t i = 0;
  for (; i < spawn.think_count && i < 6; i++) d->cb_think[i] = spawn.think[i];
  d->cb_think[i++] = (qa_q2_named_think){"doppleganger_timeout", d->timeout};
  d->cb_think[i++] = (qa_q2_named_think){"body_think", d->body_think};
  d->cb_think_count = i;
  d->cb_pain[0] = (qa_q2_named_pain){"doppleganger_pain", d->pain};
  d->cb_die[0] = (qa_q2_named_die){"doppleganger_die", d->die};
}
qa_q2_callback_definitions qa_q2mp_doppleganger_callbacks(qa_q2mp_doppleganger *d) {
  qa_q2_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.think = d->cb_think; defs.think_count = d->cb_think_count;
  defs.pain = d->cb_pain; defs.pain_count = 1;
  defs.die = d->cb_die; defs.die_count = 1;
  return defs;
}
