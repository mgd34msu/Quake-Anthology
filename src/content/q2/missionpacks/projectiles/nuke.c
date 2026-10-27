/* Port of src/content/q2/missionpacks/projectiles/nuke.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/projectiles/nuke.h"

#include <stdlib.h>
#include <string.h>

#include "../missionpacks_internal.h"
#include "qa/content/q2/missionpacks/projectiles/common.h"

static void nuke_bounce_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  (void)ctx; (void)contact;
  q2mp_sound(game, entity, q2mp_rnd(game) > 0.5 ? "weapons/hgrenb1a.wav" : "weapons/hgrenb2a.wav", true, 2, false, 0, false, 0);
}
static void player_effect(qa_q2mp_nuke *n, qa_actor_id actor, double until) {
  qa_q2mp_player_effect fx;
  memset(&fx, 0, sizeof fx);
  fx.kind = QA_Q2MP_EFFECT_NUKE_BLIND; fx.actor = actor; fx.until = until;
  n->base.base.hooks.player_effect(n->base.base.hooks.ctx, fx);
}
static void nuke_quake_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game);
static void nuke_explode(qa_q2mp_nuke *n, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_vec3 origin = q2mp_body(game, entity).origin;
  if (entity->has_team_master)
    qa_q2w_ballistics_player_noise_for_actor(n->base.base.hooks.base, entity->team_master, game, origin, "impact");
  qa_actor_id *blinded = NULL;
  size_t blinded_count = 0, blinded_cap = 0;
  size_t count = 0;
  const qa_actor_id *actors = game->host->nearby(game->host->ctx, origin, entity->damage_radius * 2, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    if (qa_same_actor(actor, entity->actor.id) || !q2mp_live(game, actor) || !q2mp_can_hurt(game, actor)) continue;
    qa_q2f_entity *target = q2mp_ent(game, true, actor);
    qa_body_state_result body = q2mp_bread(game, actor);
    if (!body.has_value) continue;
    if (!game->host->is_player(game->host->ctx, actor) && !game->host->is_monster(game->host->ctx, actor) &&
        !(target != NULL && target->damageable_target)) continue;
    qa_vec3 center = q2mp_add(body.value.origin, q2mp_scale(q2mp_add(body.value.bounds.min, body.value.bounds.max), 0.5));
    double distance = q2mp_len(q2mp_sub(origin, center));
    double points = distance <= entity->damage_radius ? 10000
        : entity->damage / entity->damage_radius * (2 * entity->damage_radius - distance);
    if (points <= 0) continue;
    if (game->host->is_player(game->host->ctx, actor)) {
      if (distance <= entity->damage_radius && target != NULL)
        target->flags = (double)(q2mp_u32(target->flags) | 0x10000u) + (target->flags >= 4294967296.0 ? 4294967296.0 : 0);
      player_effect(n, actor, q2mp_now(game) + 2);
      if (blinded_count == blinded_cap) {
        size_t next = blinded_cap ? blinded_cap * 2 : 8;
        qa_actor_id *grown = realloc(blinded, next * sizeof *grown);
        if (grown != NULL) { blinded = grown; blinded_cap = next; }
      }
      if (blinded_count < blinded_cap) blinded[blinded_count++] = actor;
    }
    q2mp_damage(game, actor, entity, entity->actor.id, entity->has_team_master ? &entity->team_master : NULL,
                trunc(points), trunc(points), q2mp_sub(body.value.origin, origin), origin, q2mp_zero(), QA_Q2MP_MOD_NUKE,
                true, 1, "q2:ammo_nuke");
  }
  size_t players = 0;
  const qa_actor_id *list = game->host->players(game->host->ctx, &players);
  for (size_t i = 0; i < players; i++) {
    qa_actor_id actor = list[i];
    bool seen = false;
    for (size_t j = 0; j < blinded_count; j++) if (qa_same_actor(blinded[j], actor)) seen = true;
    if (seen) continue;
    qa_body_state_result body = q2mp_bread(game, actor);
    if (!body.has_value) continue;
    qa_trace_result trace = q2mp_trace(game, origin, body.value.origin, NULL, &entity->actor.id, 3);
    double duration = trace.fraction == 1 ? 2 : q2mp_len(q2mp_sub(body.value.origin, origin)) < 2048 ? 1.5 : 1;
    player_effect(n, actor, q2mp_now(game) + duration);
  }
  free(blinded);
  if (entity->damage > 400) q2mp_sound(game, entity, "items/damage3.wav", true, 3, false, 0, false, 0);
  q2mp_sound(game, entity, "weapons/grenlx1a.wav", true, 10, true, 1, true, 0);
  qa_q2mp_effect(entity, game, "explosion1_big", q2mp_zero(), 1, 0);
  qa_q2mp_effect(entity, game, "nukeblast", q2mp_zero(), 1, 0);
  entity->visible = false; entity->speed = 100; entity->timestamp = q2mp_now(game) + 3; entity->delay = 0;
  game->show(game->ctx, entity);
  game->schedule(game->ctx, entity, q2mp_frame(game), n->nuke_quake);
}
static void nuke_die_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_death_reaction reaction) {
  qa_q2mp_nuke *n = ctx;
  q2mp_traits_hurt(game, entity->actor, false);
  qa_q2f_entity *attacker = q2mp_ent(game, reaction.pain.attacker != NULL,
                                      reaction.pain.attacker ? *reaction.pain.attacker : entity->actor.id);
  if (attacker != NULL && strcmp(attacker->classname, "nuke") == 0) game->remove(game->ctx, entity);
  else nuke_explode(n, entity, game);
}
static void nuke_quake_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_nuke *n = ctx;
  if (entity->delay < q2mp_now(game)) {
    q2mp_sound(game, entity, "world/rumble.wav", true, 0, true, 0.75, true, 0);
    entity->delay = q2mp_now(game) + 0.5;
  }
  size_t players = 0;
  const qa_actor_id *list = game->host->players(game->host->ctx, &players);
  for (size_t i = 0; i < players; i++) {
    qa_body_state_result body = q2mp_bread(game, list[i]);
    if (!body.has_value || body.value.ground == NULL) continue;
    qa_combat_state_result combat = q2mp_cread(game, list[i]);
    double mass = combat.has_value ? combat.value.mass : 200;
    qa_q2mp_velocity(game, list[i], q2mp_v3(body.value.velocity.x + (q2mp_rnd(game) * 2 - 1) * 150,
                                            body.value.velocity.y + (q2mp_rnd(game) * 2 - 1) * 150,
                                            entity->speed * 100 / mass), true);
  }
  if (q2mp_now(game) < entity->timestamp) game->schedule(game->ctx, entity, q2mp_frame(game), n->nuke_quake);
  else game->remove(game->ctx, entity);
}
static void nuke_think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_nuke *n = ctx;
  double multiplier = entity->damage / 400;
  double divisor = multiplier == 1 ? 1.4 : multiplier == 2 ? 2 : multiplier == 4 ? 3 : multiplier == 8 ? 5 : 1;
  double flash = multiplier == 2 ? 37 : multiplier == 4 ? 38 : multiplier == 8 ? 39 : 36;
  if (entity->wait < q2mp_now(game)) { nuke_explode(n, entity, game); return; }
  if (q2mp_now(game) >= entity->wait - 6) {
    if (++entity->frame > 11) entity->frame = 6;
    if (((q2mp_u32(game->host->point_contents(game->host->ctx, q2mp_body(game, entity).origin)) & 24) != 0)) {
      nuke_explode(n, entity, game);
      return;
    }
    game->host->combat.set_health(game->host->combat.ctx, entity->actor, 1);
    entity->has_owner = false;
    game->motion(game->ctx, entity, entity->motion);
    game->show(game->ctx, entity);
    qa_q2w_event ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = QA_Q2W_EVENT_MUZZLEFLASH; ev.actor = &entity->actor.id; ev.flash = flash; ev.silenced = false;
    n->base.base.hooks.base->hooks.emit(n->base.base.hooks.base->hooks.ctx, ev);
    if (entity->timestamp <= q2mp_now(game)) {
      q2mp_sound(game, entity, "weapons/nukewarn2.wav", true, 10, true, 1, true, 1.8 / divisor);
      entity->timestamp = q2mp_now(game) + (entity->wait - q2mp_now(game) <= 3 ? 0.3 : 0.5);
    }
    game->schedule(game->ctx, entity, 0.1, n->nuke_think);
    return;
  }
  if (entity->timestamp <= q2mp_now(game)) {
    q2mp_sound(game, entity, "weapons/nukewarn2.wav", true, 10, true, 1, true, 1.8 / divisor);
    entity->timestamp = q2mp_now(game) + 1;
  }
  game->schedule(game->ctx, entity, q2mp_frame(game), n->nuke_think);
}
qa_q2f_entity *qa_q2mp_nuke_fire_nuke(qa_q2mp_nuke *n, qa_owned_actor self, qa_q2f_game *game, qa_vec3 start,
                                      qa_vec3 direction, double speed, double multiplier) {
  /* TS throwMine registers the subclass (virtual) callback set. */
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2mp_nuke_callbacks(n));
  qa_q2f_entity *bomb = qa_q2mp_mines_throw_mine(&n->base, self, game, "nuke", start, direction, speed);
  qa_q2f_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_zero();
  c.has_bounds = true; c.bounds = (qa_bounds){q2mp_v3(-8, -8, 0), q2mp_v3(8, 8, 16)};
  q2mp_move(game, bomb, c, true, false);
  bomb->damage = 400 * multiplier;
  bomb->damage_radius = multiplier == 1 ? 512 : 512 + 128 * multiplier;
  bomb->wait = q2mp_now(game) + 10;
  bomb->die = n->nuke_die;
  bomb->touch = n->nuke_bounce;
  q2mp_combat_create(game, bomb->actor, 10000, 0, true);
  game->schedule(game->ctx, bomb, q2mp_frame(game), n->nuke_think);
  qa_q2mp_publish_projectile(bomb, game, "", true, (qa_q2mp_projectile_behavior){"q2:ammo_nuke", "grenade"});
  return bomb;
}

void qa_q2mp_nuke_init(qa_q2mp_nuke *n, qa_q2mp_projectile_hooks hooks) {
  qa_q2mp_mines_init(&n->base, hooks);
  n->nuke_think = (qa_q2f_think){n, nuke_think_fn};
  n->nuke_quake = (qa_q2f_think){n, nuke_quake_fn};
  n->nuke_bounce = (qa_q2f_touch){n, nuke_bounce_fn};
  n->nuke_die = (qa_q2f_die){n, nuke_die_fn};
  memcpy(n->cb_think, n->base.cb_think, sizeof n->base.cb_think);
  n->cb_think[13] = (qa_q2f_named_think){"Nuke_Think", n->nuke_think};
  n->cb_think[14] = (qa_q2f_named_think){"Nuke_Quake", n->nuke_quake};
  memcpy(n->cb_touch, n->base.cb_touch, sizeof n->base.cb_touch);
  n->cb_touch[9] = (qa_q2f_named_touch){"nuke_bounce", n->nuke_bounce};
  memcpy(n->cb_die, n->base.cb_die, sizeof n->base.cb_die);
  n->cb_die[2] = (qa_q2f_named_die){"nuke_die", n->nuke_die};
}
qa_q2f_callback_definitions qa_q2mp_nuke_callbacks(qa_q2mp_nuke *n) {
  qa_q2f_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.trajectory = n->base.base.cb_traj; defs.trajectory_count = 4;
  defs.think = n->cb_think; defs.think_count = 15;
  defs.touch = n->cb_touch; defs.touch_count = 10;
  defs.die = n->cb_die; defs.die_count = 3;
  return defs;
}
