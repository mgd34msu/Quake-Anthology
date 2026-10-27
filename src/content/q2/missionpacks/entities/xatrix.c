/* Port of src/content/q2/missionpacks/entities/xatrix.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/entities/xatrix.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../missionpacks_internal.h"
#include "qa/content/q2/missionpacks/types.h"
#include "qa/foundation/support.h"

static void sparks(qa_q2f_entity *entity, qa_q2f_game *game, double count) {
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_EFFECT; e.effect = "q2:welding_sparks"; e.origin = q2mp_body(game, entity).origin;
  e.direction = q2mp_zero(); e.count = count; e.color = 0xe0 + floor(q2mp_rnd(game) * 8);
  game->host->emit(game->host->ctx, e);
}
static void alarm_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_xatrix_entities *x = ctx;
  if ((q2mp_u32(entity->spawnflags) & 1u) != 0) { game->cancel(game->ctx, entity); return; }
  q2mp_sound(game, entity, "misc/alarm.wav", true, 10, true, 1, true, 3);
  game->schedule(game->ctx, entity, 1, x->alarm);
}
static void light_killed_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  sparks(entity, game, 30);
  entity->effects = (double)(q2mp_u32(entity->effects) & ~0x800000u);
  entity->use = (qa_q2f_use){NULL, NULL};
  game->show(game->ctx, entity);
  game->schedule(game->ctx, entity, 0.1, qa_q2f_free_entity_think());
}
static void light_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q2mp_xatrix_entities *x = ctx;
  if ((q2mp_u32(entity->spawnflags) & 1u) != 0) {
    entity->spawnflags = (double)(q2mp_u32(entity->spawnflags) & ~1u);
    entity->effects = (double)(q2mp_u32(entity->effects) | 0x800000u);
    if ((q2mp_u32(entity->spawnflags) & 2u) != 0) game->schedule(game->ctx, entity, 0.1, x->alarm);
  } else {
    entity->spawnflags = (double)(q2mp_u32(entity->spawnflags) | 1u);
    entity->effects = (double)(q2mp_u32(entity->effects) & ~0x800000u);
  }
  game->show(game->ctx, entity);
}
static void repair_fx_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_xatrix_entities *x = ctx;
  qa_combat_state_result combat = q2mp_cread(game, entity->actor.id);
  if (!combat.has_value) qa_q2f_error("Source repair object lost health");
  if (combat.value.health <= 100) game->host->combat.set_health(game->host->combat.ctx, entity->actor, combat.value.health + 1);
  else sparks(entity, game, 10);
  game->schedule(game->ctx, entity, entity->delay, x->repair_fx);
}
static void repair_dead_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_xatrix_entities *x = ctx;
  game->use_targets(game->ctx, entity, &entity->actor.id, false, false);
  if (q2mp_live(game, entity->actor.id)) game->schedule(game->ctx, entity, 0.1, x->repair_fx);
}
static void repair_sparks_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_xatrix_entities *x = ctx;
  if (q2mp_health(game, entity->actor.id) < 0) { game->schedule(game->ctx, entity, 0.1, x->repair_dead); return; }
  sparks(entity, game, 10);
  game->schedule(game->ctx, entity, entity->delay, x->repair_sparks);
}
static void ambience_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_xatrix_entities *x = ctx;
  q2mp_sound(game, entity, "world/amb4.wav", true, 2, true, 1, true, 0);
  game->schedule(game->ctx, entity, 2.7, x->ambience);
}
static void missile_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                           const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q2mp_xatrix_entities *x = ctx;
  size_t n = 0;
  qa_q2f_entity *const *found = game->targets(game->ctx, entity->target, &n);
  qa_q2f_entity *target = n > 0 ? found[0] : NULL;
  if (target == NULL) {
    char message[256];
    (void)snprintf(message, sizeof message, "misc_viper_missile missing target %s", entity->target);
    game->host->diagnostic(game->host->ctx, message);
    return;
  }
  entity->enemy = target->actor.id; entity->has_enemy = true;
  qa_vec3 origin = q2mp_body(game, entity).origin;
  qa_vec3 direction = q2mp_norm(q2mp_sub(q2mp_body(game, target).origin, origin));
  (void)qa_q2w_ballistics_fire_rocket(x->hooks.weapons, entity->actor, game, origin, direction, entity->damage, 500,
                                      entity->damage + 20, entity->damage);
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_MONSTER_MUZZLEFLASH; e.actor = &entity->actor.id; e.flash = 57;
  e.origin = origin; e.direction = direction;
  game->host->emit(game->host->ctx, e);
  game->schedule(game->ctx, entity, 0.1, qa_q2f_free_entity_think());
}
static void nuke_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q2f_entity *target = game->entities[i];
    if (target == entity) continue;
    if (game->host->is_player(game->host->ctx, target->actor.id))
      q2mp_damage(game, target->actor.id, entity, entity->actor.id, &entity->actor.id, 100000, 1, q2mp_zero(),
                  q2mp_body(game, target).origin, q2mp_zero(), 39, false, 0, NULL);
    else if (game->host->is_monster(game->host->ctx, target->actor.id)) game->remove(game->ctx, target);
  }
  entity->use = (qa_q2f_use){NULL, NULL};
}
static void mal_think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_xatrix_entities *x = ctx;
  if (x->hooks.target_laser_think.fn != NULL) x->hooks.target_laser_think.fn(x->hooks.target_laser_think.ctx, entity, game);
  entity->spawnflags = (double)(q2mp_u32(entity->spawnflags) | 0x80000000u);
  game->schedule(game->ctx, entity, entity->wait + 0.1, x->mal_think);
}
static void mal_on(qa_q2mp_xatrix_entities *x, qa_q2f_entity *entity, qa_q2f_game *game) {
  if (!entity->has_activator) { entity->activator = entity->actor.id; entity->has_activator = true; }
  entity->spawnflags = (double)(q2mp_u32(entity->spawnflags) | 0x80000001u);
  entity->visible = true;
  game->show(game->ctx, entity);
  game->schedule(game->ctx, entity, entity->wait + entity->delay, x->mal_think);
}
static void mal_off(qa_q2mp_xatrix_entities *x, qa_q2f_entity *entity, qa_q2f_game *game) {
  entity->spawnflags = (double)(q2mp_u32(entity->spawnflags) & ~1u);
  entity->visible = false;
  game->cancel(game->ctx, entity);
  game->show(game->ctx, entity);
  if (x->hooks.beam != NULL) {
    qa_vec3 origin = q2mp_body(game, entity).origin;
    x->hooks.beam(x->hooks.ctx, entity->actor.id, origin, origin, entity->frame, entity->skin, false);
  }
}
static void mal_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                       const qa_actor_id *activator) {
  (void)other;
  qa_q2mp_xatrix_entities *x = ctx;
  if (activator != NULL) { entity->activator = *activator; entity->has_activator = true; }
  else entity->has_activator = false;
  if ((q2mp_u32(entity->spawnflags) & 1u) != 0) mal_off(x, entity, game);
  else mal_on(x, entity, game);
}

bool qa_q2mp_xatrix_entities_spawn(qa_q2mp_xatrix_entities *x, qa_q2f_entity *entity, qa_q2f_game *game) {
  const char *classname = entity->classname;
  if (strcmp(classname, "rotating_light") == 0) {
    double health = qa_q2f_number_field(&entity->spawn, "health", 0);
    if (health == 0) health = 10;
    entity->model = "models/objects/light/tris.md2";
    if (entity->speed == 0) entity->speed = 32;
    entity->max_health = health;
    entity->frame = 0;
    entity->use = x->light_use;
    entity->die = x->light_killed;
    entity->effects = (double)(((q2mp_u32(entity->spawnflags) & 1u) != 0)
        ? (q2mp_u32(entity->effects) & ~0x800000u) : (q2mp_u32(entity->effects) | 0x800000u));
    q2mp_combat_create(game, entity->actor, health, 0, true);
    game->solid(game->ctx, entity, QA_Q2F_SOLID_BOX);
    game->motion(game->ctx, entity, QA_Q2F_MOTION_STOP);
    game->show(game->ctx, entity);
  } else if (strcmp(classname, "func_object_repair") == 0) {
    entity->classname = "object_repair";
    if (entity->delay == 0) entity->delay = 1;
    q2mp_combat_create(game, entity->actor, 100, 0, false);
    qa_q2f_body_changes c = q2mp_changes();
    c.has_bounds = true; c.bounds = (qa_bounds){q2mp_v3(-8, -8, 8), q2mp_v3(8, 8, 8)};
    q2mp_move(game, entity, c, false, false);
    game->solid(game->ctx, entity, QA_Q2F_SOLID_BOX);
    game->motion(game->ctx, entity, QA_Q2F_MOTION_STATIONARY);
    game->schedule(game->ctx, entity, 1, x->repair_sparks);
  } else if (strcmp(classname, "misc_viper_missile") == 0) {
    if (entity->damage == 0) entity->damage = 250;
    entity->model = "models/objects/bomb/tris.md2";
    entity->visible = false;
    entity->use = x->missile_use;
    qa_q2f_body_changes c = q2mp_changes();
    c.has_bounds = true; c.bounds = (qa_bounds){q2mp_v3(-8, -8, -8), q2mp_v3(8, 8, 8)};
    q2mp_move(game, entity, c, false, false);
    game->solid(game->ctx, entity, QA_Q2F_SOLID_NONE);
    game->motion(game->ctx, entity, QA_Q2F_MOTION_STATIONARY);
    game->show(game->ctx, entity);
  } else if (strcmp(classname, "misc_amb4") == 0) {
    game->schedule(game->ctx, entity, 1, x->ambience);
  } else if (strcmp(classname, "misc_nuke") == 0) {
    entity->use = x->nuke_use;
  } else if (strcmp(classname, "misc_crashviper") == 0 || strcmp(classname, "misc_transport") == 0) {
    if (strcmp(entity->target, "") == 0) {
      char message[160];
      (void)snprintf(message, sizeof message, "%s without a target", classname);
      game->host->diagnostic(game->host->ctx, message);
      game->remove(game->ctx, entity);
    } else {
      bool transport = strcmp(classname, "misc_transport") == 0;
      qa_vec3 angles = q2mp_body(game, entity).angles;
      x->hooks.movers.spawn_train(x->hooks.movers.ctx, entity, game);
      entity->blocked = (qa_q2f_blocked){NULL, NULL};
      entity->model = transport ? "models/objects/ship/tris.md2" : "models/ships/bigviper/tris.md2";
      qa_q2f_body_changes c = q2mp_changes();
      c.has_angles = true; c.angles = angles;
      q2mp_move(game, entity, c, false, false);
      if (transport) entity->spawnflags = (double)(q2mp_u32(entity->spawnflags) | 1u);
      game->show(game->ctx, entity);
    }
  } else if (strcmp(classname, "target_mal_laser") == 0) {
    entity->render_flags = (double)(q2mp_u32(entity->render_flags) | 0xa0u);
    entity->frame = ((q2mp_u32(entity->spawnflags) & 64u) != 0) ? 16 : 4;
    uint32_t flags = q2mp_u32(entity->spawnflags);
    entity->skin = (double)((flags & 2u) != 0 ? 0xf2f2f0f0u : (flags & 4u) != 0 ? 0xd0d1d2d3u : (flags & 8u) != 0 ? 0xf3f3f1f1u
                            : (flags & 16u) != 0 ? 0xdcdddedfu : (flags & 32u) != 0 ? 0xe0e1e2e3u : 0u);
    entity->movedir = qa_q2mp_movedir(q2mp_body(game, entity).angles);
    if (entity->delay == 0) entity->delay = 0.1;
    if (entity->wait == 0) entity->wait = 0.1;
    if (entity->damage == 0) entity->damage = 5;
    entity->use = x->mal_use;
    qa_q2f_body_changes c = q2mp_changes();
    c.has_angles = true; c.angles = q2mp_zero();
    c.has_bounds = true; c.bounds = (qa_bounds){q2mp_v3(-8, -8, -8), q2mp_v3(8, 8, 8)};
    q2mp_move(game, entity, c, false, false);
    game->solid(game->ctx, entity, QA_Q2F_SOLID_NONE);
    game->motion(game->ctx, entity, QA_Q2F_MOTION_STATIONARY);
    if ((q2mp_u32(entity->spawnflags) & 1u) != 0) mal_on(x, entity, game);
    else mal_off(x, entity, game);
  } else return false;
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2mp_xatrix_entities_callbacks(x));
  return true;
}

void qa_q2mp_xatrix_entities_init(qa_q2mp_xatrix_entities *x, qa_q2mp_entity_hooks hooks) {
  memset(x, 0, sizeof *x);
  x->hooks = hooks;
  x->alarm = (qa_q2f_think){x, alarm_fn};
  x->repair_sparks = (qa_q2f_think){x, repair_sparks_fn};
  x->repair_dead = (qa_q2f_think){x, repair_dead_fn};
  x->repair_fx = (qa_q2f_think){x, repair_fx_fn};
  x->ambience = (qa_q2f_think){x, ambience_fn};
  x->mal_think = (qa_q2f_think){x, mal_think_fn};
  x->light_killed = (qa_q2f_die){x, light_killed_fn};
  x->light_use = (qa_q2f_use){x, light_use_fn};
  x->missile_use = (qa_q2f_use){x, missile_use_fn};
  x->nuke_use = (qa_q2f_use){x, nuke_use_fn};
  x->mal_use = (qa_q2f_use){x, mal_use_fn};
  x->cb_think[0] = (qa_q2f_named_think){"rotating_light_alarm", x->alarm};
  x->cb_think[1] = (qa_q2f_named_think){"object_repair_sparks", x->repair_sparks};
  x->cb_think[2] = (qa_q2f_named_think){"object_repair_dead", x->repair_dead};
  x->cb_think[3] = (qa_q2f_named_think){"object_repair_fx", x->repair_fx};
  x->cb_think[4] = (qa_q2f_named_think){"amb4_think", x->ambience};
  x->cb_think[5] = (qa_q2f_named_think){"mal_laser_think", x->mal_think};
  x->cb_think[6] = (qa_q2f_named_think){"target_laser_think", hooks.target_laser_think};
  x->cb_die[0] = (qa_q2f_named_die){"rotating_light_killed", x->light_killed};
  x->cb_use[0] = (qa_q2f_named_use){"rotating_light_use", x->light_use};
  x->cb_use[1] = (qa_q2f_named_use){"misc_viper_missile_use", x->missile_use};
  x->cb_use[2] = (qa_q2f_named_use){"use_nuke", x->nuke_use};
  x->cb_use[3] = (qa_q2f_named_use){"target_mal_laser_use", x->mal_use};
}
qa_q2f_callback_definitions qa_q2mp_xatrix_entities_callbacks(qa_q2mp_xatrix_entities *x) {
  qa_q2f_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.think = x->cb_think; defs.think_count = 7;
  defs.die = x->cb_die; defs.die_count = 1;
  defs.use = x->cb_use; defs.use_count = 4;
  return defs;
}
