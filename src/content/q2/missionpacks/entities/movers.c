/* Port of src/content/q2/missionpacks/entities/movers.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/entities/movers.h"
#include "qa/foundation/js.h"

#include <stdlib.h>
#include <string.h>

#include "../missionpacks_internal.h"
#include "qa/content/q2/missionpacks/projectiles/common.h"
#include "qa/content/q2/missionpacks/types.h"

const char *qa_q2mp_platform_phase_name(qa_q2mp_platform_phase phase) {
  return phase == QA_Q2MP_PLATFORM_TOP ? "top" : phase == QA_Q2MP_PLATFORM_BOTTOM ? "bottom"
         : phase == QA_Q2MP_PLATFORM_UP ? "up" : "down";
}
bool qa_q2mp_rogue_movers_platform_state(qa_q2mp_rogue_movers *m, qa_q2_entity *entity, qa_q2mp_platform_state *out) {
  (void)m;
  if (strcmp(entity->classname, "func_plat2") != 0) return false;
  out->top = entity->pos1;
  out->bottom = entity->pos2;
  out->phase = entity->style == 0 ? QA_Q2MP_PLATFORM_TOP : entity->style == 1 ? QA_Q2MP_PLATFORM_BOTTOM
               : entity->style == 2 ? QA_Q2MP_PLATFORM_UP : QA_Q2MP_PLATFORM_DOWN;
  return true;
}

static void plat_sound(qa_q2_entity *entity, qa_q2_game_services *game, bool start) {
  if ((q2mp_u32(entity->flags) & 1024u) != 0) return;
  q2mp_sound(game, entity, start ? "plats/pt1_strt.wav" : "plats/pt1_end.wav", true, 10, true, 1, true, 3);
  entity->sound = start ? "plats/pt1_mid.wav" : "";
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "sound"; e.has_actor = true; e.actor = entity->actor.id; e.origin = q2mp_body(game, entity).origin;
  e.path = "plats/pt1_mid.wav"; e.channel = 0; e.volume = 1; e.attenuation = 3; e.reliable = false;
  e.loop = start ? "start" : "stop";
  game->host->emit(game->host->ctx, e);
}
static void down_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
static void up_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
static void hit(qa_q2mp_rogue_movers *m, qa_q2_entity *entity, qa_q2_game_services *game, bool top) {
  plat_sound(entity, game, false);
  entity->style = top ? 0 : 1;
  qa_q2_think returning = top ? m->down : m->up;
  if (((q2mp_u32(entity->count) & 1u) != 0)) {
    entity->count = 4;
    if ((q2mp_u32(entity->spawnflags) & 2u) == 0) game->schedule(game->ctx, entity, 5, returning);
    double left = q2mp_now(game);
    double right = (q2mp_dm(game) ? 1 : 2);
    entity->timestamp = left - right;
  } else {
    entity->count = 0;
    entity->timestamp = q2mp_now(game);
    if ((((q2mp_u32(entity->spawnflags) & 4u) != 0) != top) && ((q2mp_u32(entity->spawnflags) & 2u) == 0))
      game->schedule(game->ctx, entity, 2, returning);
  }
  if (!top) {
    size_t count = 0;
    qa_q2_entity **areas = q2mp_entity_snapshot(game, &count);
    for (size_t i = 0; i < count; i++) {
      qa_q2_entity *area = areas[i];
      if (!q2mp_entity_present(game, area)) continue;
      if (strcmp(area->classname, "bad_area") == 0 && area->has_owner && qa_same_actor(area->owner, entity->actor.id))
        game->remove(game->ctx, area);
    }
    free(areas);
  }
  qa_q2_game_use_targets_entity(game, entity, &entity->actor.id, false);
}
static void top_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) { hit(ctx, entity, game, true); }
static void bottom_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) { hit(ctx, entity, game, false); }
static void down_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_rogue_movers *m = ctx;
  plat_sound(entity, game, true);
  entity->style = 3;
  entity->count = (double)(qa_js_to_int32(q2mp_u32(entity->count) | 2u));
  qa_q2_linear_motion_move_to(qa_q2_mover_module_linear(m->hooks.movers), entity, game, entity->pos2, m->bottom);
}
static void up_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_rogue_movers *m = ctx;
  plat_sound(entity, game, true);
  entity->style = 2;
  entity->count = (double)(qa_js_to_int32(q2mp_u32(entity->count) | 2u));
  qa_bounds bounds = q2mp_body(game, entity).bounds;
  (void)qa_q2mp_mines_spawn_bad_area(m->hooks.projectiles, game, bounds.min,
                                     q2mp_v3(bounds.max.x, bounds.max.y, bounds.min.z + 64), 0, true, entity->actor.id);
  qa_q2_linear_motion_move_to(qa_q2_mover_module_linear(m->hooks.movers), entity, game, entity->pos1, m->top);
}
static void operate(qa_q2mp_rogue_movers *m, qa_q2_entity *trigger, qa_q2_game_services *game, qa_actor_id actor) {
  qa_q2_entity *entity = q2mp_ent(game, trigger->has_enemy, trigger->enemy);
  qa_body_state_result other = q2mp_bread(game, actor);
  if (entity == NULL || !other.has_value || ((q2mp_u32(entity->count) & 2u) != 0) || entity->timestamp + 2 > q2mp_now(game)) return;
  qa_body_state bounds = q2mp_body(game, trigger);
  qa_vec3 min = q2mp_add(bounds.origin, bounds.bounds.min), max = q2mp_add(bounds.origin, bounds.bounds.max);
  double center = (min.z + max.z) / 2;
  double other_state = entity->style == 0
      ? ((((q2mp_u32(entity->spawnflags) & 32u) != 0) ? center : max.z) > other.value.origin.z ? 1 : 0)
      : (other.value.origin.z > center ? 0 : 1);
  entity->count = 2;
  double pause = q2mp_dm(game) ? 0.3 : 0.5;
  if (entity->style != other_state) {
    entity->count = (double)(qa_js_to_int32(q2mp_u32(entity->count) | 1u));
    pause = 0.1;
  }
  entity->timestamp = q2mp_now(game);
  game->schedule(game->ctx, entity, pause, entity->style == 1 ? m->up : m->down);
}
static void plat_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  qa_q2mp_rogue_movers *m = ctx;
  if (q2mp_health(game, contact.other) <= 0 ||
      (!game->host->is_player(game->host->ctx, contact.other) && !game->host->is_monster(game->host->ctx, contact.other))) return;
  operate(m, entity, game, contact.other);
}
static void plat_use_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)other;
  qa_q2mp_rogue_movers *m = ctx;
  if (entity->style > 1 || entity->timestamp + 2 > q2mp_now(game) || activator == NULL) return;
  for (size_t i = 0; i < qa_q2_game_entity_count(game); i++) {
    qa_q2_entity *trigger = qa_q2_game_entity_at(game, i);
    if (q2mp_touch_eq(trigger->touch, m->plat_touch) && trigger->has_enemy && qa_same_actor(trigger->enemy, entity->actor.id)) {
      operate(m, trigger, game, *activator);
      return;
    }
  }
}
static void make_trigger(qa_q2mp_rogue_movers *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_bounds bounds = q2mp_body(game, entity).bounds;
  double lip = qa_q2_number_field(&entity->spawn, "lip", 0);
  double min_x = bounds.min.x + 25, min_y = bounds.min.y + 25, max_x = bounds.max.x - 25, max_y = bounds.max.y - 25;
  double min_z = bounds.max.z + 8 - (entity->pos1.z - entity->pos2.z + lip);
  double max_z = ((q2mp_u32(entity->spawnflags) & 1u) != 0) ? min_z + 8 : bounds.max.z + 8;
  if (max_x - min_x <= 0) { min_x = (bounds.min.x + bounds.max.x) * 0.5; max_x = min_x + 1; }
  if (max_y - min_y <= 0) { min_y = (bounds.min.y + bounds.max.y) * 0.5; max_y = min_y + 1; }
  qa_q2_entity *trigger = game->create(game->ctx, "plat2_trigger", NULL, 0);
  trigger->enemy = entity->actor.id; trigger->has_enemy = true;
  trigger->touch = m->plat_touch;
  trigger->visible = false;
  qa_q2_body_changes c = q2mp_changes();
  c.has_bounds = true;
  c.bounds = (qa_bounds){q2mp_v3(min_x - 10, min_y - 10, min_z), q2mp_v3(max_x + 10, max_y + 10, max_z)};
  q2mp_move(game, trigger, c, false, false);
  game->solid(game->ctx, trigger, "trigger");
}
static void activate_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q2mp_rogue_movers *m = ctx;
  entity->use = m->plat_use;
  make_trigger(m, entity, game);
  down_fn(ctx, entity, game);
}
static void plat_blocked_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id other) {
  qa_body_state_result body = q2mp_bread(game, other);
  if (!body.has_value) return;
  if (!game->host->is_monster(game->host->ctx, other) && !game->host->is_player(game->host->ctx, other)) {
    q2mp_damage(game, other, entity, entity->actor.id, &entity->actor.id, 100000, 1, q2mp_zero(), body.value.origin,
                q2mp_zero(), 20, false, 0, NULL);
    qa_q2_entity *target = q2mp_ent(game, true, other);
    if (target != NULL) qa_q2mp_explode_default(target, game);
    return;
  }
  if (q2mp_health(game, other) < 1)
    q2mp_damage(game, other, entity, entity->actor.id, &entity->actor.id, 100, 1, q2mp_zero(), body.value.origin,
                q2mp_zero(), 20, false, 0, NULL);
  q2mp_damage(game, other, entity, entity->actor.id, &entity->actor.id, entity->damage, 1, q2mp_zero(), body.value.origin,
              q2mp_zero(), 20, false, 0, NULL);
  if (entity->style == 2) down_fn(ctx, entity, game);
  else if (entity->style == 3) up_fn(ctx, entity, game);
}
static void spawn_platform(qa_q2mp_rogue_movers *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_zero();
  q2mp_move(game, entity, c, false, false);
  game->solid(game->ctx, entity, "brush");
  game->motion(game->ctx, entity, "push");
  entity->blocked = m->plat_blocked;
  double multiplier = q2mp_dm(game) ? 2 : 1;
  entity->speed = (entity->speed == 0 ? 20 : entity->speed * 0.1) * multiplier;
  entity->accel = (entity->accel == 0 ? 5 : entity->accel * 0.1) * multiplier;
  entity->decel = (entity->decel == 0 ? 5 : entity->decel * 0.1) * multiplier;
  if (entity->damage == 0) entity->damage = 2;
  qa_body_state body = q2mp_body(game, entity);
  double lip = qa_q2_number_field(&entity->spawn, "lip", 0);
  double height = qa_q2_number_field(&entity->spawn, "height", 0);
  if (height == 0) height = body.bounds.max.z - body.bounds.min.z;
  entity->pos1 = body.origin;
  entity->pos2 = q2mp_v3(body.origin.x, body.origin.y, body.origin.z - height + lip);
  entity->style = 0;
  entity->count = 0;
  if (strcmp(entity->targetname, "") != 0 && !(q2mp_rr(game) && ((q2mp_u32(entity->spawnflags) & 8u) != 0)))
    entity->use = m->activate;
  else {
    entity->use = m->plat_use;
    make_trigger(m, entity, game);
    if ((q2mp_u32(entity->spawnflags) & 4u) == 0) {
      qa_q2_body_changes move = q2mp_changes();
      move.has_origin = true; move.origin = entity->pos2;
      q2mp_move(game, entity, move, false, false);
      entity->style = 1;
    }
  }
  game->show(game->ctx, entity);
}

static void secret1_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
static void secret_use_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const qa_actor_id *other,
                          const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q2mp_rogue_movers *m = ctx;
  if ((q2mp_u32(entity->flags) & 1024u) != 0) return;
  qa_q2_entity *member = entity;
  while (member != NULL) {
    qa_q2_linear_motion_move_to(qa_q2_mover_module_linear(m->hooks.movers), member, game, member->pos1, m->secret1);
    member = q2mp_ent(game, member->has_team_chain, member->team_chain);
  }
}
static void secret_die_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  qa_q2mp_rogue_movers *m = ctx;
  qa_gameplay_authority_set_health(game->host->combat, entity->actor, entity->max_health);
  q2mp_traits_hurt(game, entity->actor, false);
  qa_q2_entity *master = q2mp_ent(game, entity->has_team_master, entity->team_master);
  qa_combat_state_result master_combat;
  memset(&master_combat, 0, sizeof master_combat);
  if (master != NULL) master_combat = q2mp_cread(game, master->actor.id);
  if (((q2mp_u32(entity->flags) & 1024u) != 0) && master != NULL && master_combat.has_value && master_combat.value.can_take_damage) {
    qa_death_reaction copy = reaction;
    copy.pain.self = master->actor;
    secret_die_fn(ctx, master, game, copy);
  } else secret_use_fn(ctx, entity, game, reaction.inflictor, reaction.pain.attacker);
  (void)m;
}
static void secret1_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_rogue_movers *m = ctx;
  game->schedule(game->ctx, entity, 1, m->secret2);
}
static void secret2_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_rogue_movers *m = ctx;
  qa_q2_linear_motion_move_to(qa_q2_mover_module_linear(m->hooks.movers), entity, game, entity->pos2, m->secret3);
}
static void secret3_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_rogue_movers *m = ctx;
  if ((q2mp_u32(entity->spawnflags) & 1u) == 0) game->schedule(game->ctx, entity, entity->wait, m->secret4);
}
static void secret4_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_rogue_movers *m = ctx;
  qa_q2_linear_motion_move_to(qa_q2_mover_module_linear(m->hooks.movers), entity, game, entity->pos1, m->secret5);
}
static void secret5_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_rogue_movers *m = ctx;
  game->schedule(game->ctx, entity, 1, m->secret6);
}
static void secret6_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_rogue_movers *m = ctx;
  qa_q2_linear_motion_move_to(qa_q2_mover_module_linear(m->hooks.movers), entity, game, entity->movedir, m->secret_done);
}
static void secret_done_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_rogue_movers *m = ctx;
  if (strcmp(entity->targetname, "") == 0 || ((q2mp_u32(entity->spawnflags) & 16u) != 0)) {
    qa_gameplay_authority_set_health(game->host->combat, entity->actor, 1);
    q2mp_traits_hurt(game, entity->actor, true);
    entity->die = m->secret_die;
  }
}
static void secret_blocked_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_actor_id other) {
  (void)ctx;
  qa_body_state_result body = q2mp_bread(game, other);
  if ((q2mp_u32(entity->flags) & 1024u) == 0 && body.has_value)
    q2mp_damage(game, other, entity, entity->actor.id, &entity->actor.id, entity->damage, 0, q2mp_zero(), body.value.origin,
                q2mp_zero(), 20, false, 0, NULL);
}
static void secret_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  (void)ctx;
  if (!game->host->is_player(game->host->ctx, contact.other) || q2mp_health(game, contact.other) <= 0 ||
      entity->timestamp > q2mp_now(game)) return;
  entity->timestamp = q2mp_now(game) + 2;
  if (strcmp(entity->message, "") == 0) return;
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "centerprint"; e.has_actor = true; e.actor = contact.other; e.text = entity->message;
  game->host->emit(game->host->ctx, e);
}
static void spawn_secret(qa_q2mp_rogue_movers *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_vec3 angles = q2mp_body(game, entity).angles;
  qa_angle_vectors axes = q2mp_axes(angles);
  qa_q2_body_changes c = q2mp_changes();
  c.has_angles = true; c.angles = q2mp_zero();
  q2mp_move(game, entity, c, false, false);
  game->solid(game->ctx, entity, "brush");
  game->motion(game->ctx, entity, "push");
  qa_body_state body = q2mp_body(game, entity);
  qa_vec3 size = q2mp_sub(body.bounds.max, body.bounds.min);
  if (!(angles.y == 0 || angles.y == 90 || angles.y == 180 || angles.y == 270)) {
    game->host->diagnostic(game->host->ctx, "Secret door not at 0,90,180,270!");
    game->remove(game->ctx, entity);
    return;
  }
  qa_vec3 forward = q2mp_scale(axes.forward, (angles.y == 0 || angles.y == 180 ? size.x : size.y) *
      (((q2mp_u32(entity->spawnflags) & 64u) != 0) ? 1 : -1));
  qa_vec3 right = q2mp_scale(axes.right, (angles.y == 0 || angles.y == 180 ? size.y : size.x) *
      (((q2mp_u32(entity->spawnflags) & 32u) != 0) ? 1 : -1));
  entity->movedir = body.origin;
  entity->pos1 = q2mp_add(body.origin, ((q2mp_u32(entity->spawnflags) & 4u) != 0) ? forward : right);
  entity->pos2 = q2mp_add(entity->pos1, ((q2mp_u32(entity->spawnflags) & 4u) != 0) ? right : forward);
  if (entity->damage == 0) entity->damage = 2;
  if (entity->wait == 0) entity->wait = 5;
  entity->speed = 50; entity->accel = 50; entity->decel = 50;
  entity->touch = m->secret_touch;
  entity->blocked = m->secret_blocked;
  entity->use = m->secret_use;
  if (strcmp(entity->targetname, "") == 0 || ((q2mp_u32(entity->spawnflags) & 16u) != 0)) {
    entity->max_health = 1;
    entity->die = m->secret_die;
    q2mp_combat_create(game, entity->actor, 1, 0, true);
  }
  game->show(game->ctx, entity);
}
static void force_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2mp_rogue_movers *m = ctx;
  if (entity->wait == 0) {
    qa_q2mp_entity_event event;
    memset(&event, 0, sizeof event);
    event.kind = QA_Q2MP_ENTITY_EVENT_FORCE_WALL;
    event.start = entity->pos1; event.end = entity->pos2; event.color = entity->style;
    m->hooks.emit(m->hooks.ctx, event);
  }
  game->schedule(game->ctx, entity, 0.1, m->force_think);
}
static void force_use_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q2mp_rogue_movers *m = ctx;
  if (entity->wait == 0) {
    entity->wait = 1;
    game->cancel(game->ctx, entity);
    game->solid(game->ctx, entity, "none");
    return;
  }
  entity->wait = 0;
  game->schedule(game->ctx, entity, 0.1, m->force_think);
  game->solid(game->ctx, entity, "brush");
  (void)qa_q2_kill_box(entity, game);
  game->link(game->ctx, entity);
}
static void spawn_force_wall(qa_q2mp_rogue_movers *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  game->solid(game->ctx, entity, "brush");
  qa_body_state body = q2mp_body(game, entity);
  qa_vec3 min = q2mp_add(body.origin, body.bounds.min), max = q2mp_add(body.origin, body.bounds.max);
  qa_vec3 middle = q2mp_scale(q2mp_add(min, max), 0.5);
  if (max.x - min.x > max.y - min.y) {
    entity->pos1 = q2mp_v3(min.x, middle.y, max.z);
    entity->pos2 = q2mp_v3(max.x, middle.y, max.z);
  } else {
    entity->pos1 = q2mp_v3(middle.x, min.y, max.z);
    entity->pos2 = q2mp_v3(middle.x, max.y, max.z);
  }
  if (entity->style == 0) entity->style = 208;
  entity->wait = 1;
  entity->use = m->force_use;
  entity->visible = false;
  game->motion(game->ctx, entity, "stationary");
  if ((q2mp_u32(entity->spawnflags) & 1u) != 0) game->schedule(game->ctx, entity, 0.1, m->force_think);
  else game->solid(game->ctx, entity, "none");
  game->show(game->ctx, entity);
}

bool qa_q2mp_rogue_movers_spawn(qa_q2mp_rogue_movers *m, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2_source_callbacks_register(game->source_callbacks, qa_q2mp_rogue_movers_callbacks(m));
  if (strcmp(entity->classname, "func_plat2") == 0) spawn_platform(m, entity, game);
  else if (strcmp(entity->classname, "func_door_secret2") == 0) spawn_secret(m, entity, game);
  else if (strcmp(entity->classname, "func_force_wall") == 0) spawn_force_wall(m, entity, game);
  else return false;
  return true;
}

void qa_q2mp_rogue_movers_init(qa_q2mp_rogue_movers *m, qa_q2mp_entity_hooks hooks) {
  memset(m, 0, sizeof *m);
  m->hooks = hooks;
  m->up = (qa_q2_think){m, up_fn};
  m->down = (qa_q2_think){m, down_fn};
  m->top = (qa_q2_think){m, top_fn};
  m->bottom = (qa_q2_think){m, bottom_fn};
  m->secret1 = (qa_q2_think){m, secret1_fn};
  m->secret2 = (qa_q2_think){m, secret2_fn};
  m->secret3 = (qa_q2_think){m, secret3_fn};
  m->secret4 = (qa_q2_think){m, secret4_fn};
  m->secret5 = (qa_q2_think){m, secret5_fn};
  m->secret6 = (qa_q2_think){m, secret6_fn};
  m->secret_done = (qa_q2_think){m, secret_done_fn};
  m->force_think = (qa_q2_think){m, force_think_fn};
  m->plat_use = (qa_q2_use){m, plat_use_fn};
  m->activate = (qa_q2_use){m, activate_fn};
  m->secret_use = (qa_q2_use){m, secret_use_fn};
  m->force_use = (qa_q2_use){m, force_use_fn};
  m->plat_touch = (qa_q2_touch){m, plat_touch_fn};
  m->secret_touch = (qa_q2_touch){m, secret_touch_fn};
  m->secret_die = (qa_q2_die){m, secret_die_fn};
  m->plat_blocked = (qa_q2_blocked){m, plat_blocked_fn};
  m->secret_blocked = (qa_q2_blocked){m, secret_blocked_fn};
  m->cb_think[0] = (qa_q2_named_think){"plat2_go_up", m->up};
  m->cb_think[1] = (qa_q2_named_think){"plat2_go_down", m->down};
  m->cb_think[2] = (qa_q2_named_think){"plat2_hit_top", m->top};
  m->cb_think[3] = (qa_q2_named_think){"plat2_hit_bottom", m->bottom};
  m->cb_think[4] = (qa_q2_named_think){"fd_secret_move1", m->secret1};
  m->cb_think[5] = (qa_q2_named_think){"fd_secret_move2", m->secret2};
  m->cb_think[6] = (qa_q2_named_think){"fd_secret_move3", m->secret3};
  m->cb_think[7] = (qa_q2_named_think){"fd_secret_move4", m->secret4};
  m->cb_think[8] = (qa_q2_named_think){"fd_secret_move5", m->secret5};
  m->cb_think[9] = (qa_q2_named_think){"fd_secret_move6", m->secret6};
  m->cb_think[10] = (qa_q2_named_think){"fd_secret_done", m->secret_done};
  m->cb_think[11] = (qa_q2_named_think){"force_wall_think", m->force_think};
  m->cb_use[0] = (qa_q2_named_use){"Use_Plat2", m->plat_use};
  m->cb_use[1] = (qa_q2_named_use){"plat2_activate", m->activate};
  m->cb_use[2] = (qa_q2_named_use){"fd_secret_use", m->secret_use};
  m->cb_use[3] = (qa_q2_named_use){"force_wall_use", m->force_use};
  m->cb_touch[0] = (qa_q2_named_touch){"Touch_Plat_Center2", m->plat_touch};
  m->cb_touch[1] = (qa_q2_named_touch){"secret_touch", m->secret_touch};
  m->cb_die[0] = (qa_q2_named_die){"fd_secret_killed", m->secret_die};
  m->cb_blocked[0] = (qa_q2_named_blocked){"plat2_blocked", m->plat_blocked};
  m->cb_blocked[1] = (qa_q2_named_blocked){"secret_blocked", m->secret_blocked};
}
qa_q2_callback_definitions qa_q2mp_rogue_movers_callbacks(qa_q2mp_rogue_movers *m) {
  qa_q2_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.think = m->cb_think; defs.think_count = 12;
  defs.use = m->cb_use; defs.use_count = 4;
  defs.touch = m->cb_touch; defs.touch_count = 2;
  defs.die = m->cb_die; defs.die_count = 1;
  defs.blocked = m->cb_blocked; defs.blocked_count = 2;
  return defs;
}
