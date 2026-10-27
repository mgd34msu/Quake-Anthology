/* Port of src/content/q2/missionpacks/entities/rogue.ts. GPL-2.0-or-later.
 * game.host.bodies.unlink has no qa_q2f_bodies mirror entry; the
 * teleport relies on move(link=false) + game.link as in TS order. */
#include "qa/content/q2/missionpacks/entities/rogue.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../missionpacks_internal.h"
#include "qa/content/q2/missionpacks/projectiles/common.h"
#include "qa/content/q2/missionpacks/types.h"

qa_q2mp_rogue_entities_checkpoint qa_q2mp_rogue_entities_capture(qa_q2mp_rogue_entities *entities) {
  qa_q2mp_rogue_entities_checkpoint cp = {entities->steam_id};
  return cp;
}
void qa_q2mp_rogue_entities_restore(qa_q2mp_rogue_entities *entities, qa_q2mp_rogue_entities_checkpoint state) {
  entities->steam_id = state.steam_id;
}

static void teleport_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                            const qa_actor_id *activator) {
  (void)ctx; (void)game; (void)other; (void)activator;
  entity->delay = entity->delay == 0 ? 1 : 0;
}
static void teleport_touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  qa_q2mp_rogue_entities *e = ctx;
  if (!game->host->is_player(game->host->ctx, contact.other) || entity->delay != 0) return;
  size_t n = 0;
  qa_q2f_entity *const *found = game->targets(game->ctx, entity->target, &n);
  qa_q2f_entity *destination = n > 0 ? found[0] : NULL;
  qa_q2f_entity *player = q2mp_ent(game, true, contact.other);
  if (destination == NULL) {
    game->host->diagnostic(game->host->ctx, "Teleport Destination not found!");
    return;
  }
  if (player == NULL) return;
  qa_q2f_presentation_event fx;
  memset(&fx, 0, sizeof fx);
  fx.kind = QA_Q2F_EVENT_EFFECT; fx.effect = "q2:teleport_effect"; fx.origin = q2mp_body(game, player).origin;
  fx.direction = q2mp_zero(); fx.count = 1; fx.color = 0;
  game->host->emit(game->host->ctx, fx);
  qa_body_state body = q2mp_body(game, destination);
  qa_vec3 origin = q2mp_add(body.origin, q2mp_v3(0, 0, 10));
  qa_q2f_body_changes c = q2mp_changes();
  c.has_origin = true; c.origin = origin;
  c.has_velocity = true; c.velocity = q2mp_zero();
  c.has_angles = true; c.angles = q2mp_zero();
  q2mp_move(game, player, c, true, false);
  e->hooks.teleport_player(e->hooks.ctx, player->actor.id, origin, body.angles);
  qa_q2f_presentation_event ev;
  memset(&ev, 0, sizeof ev);
  ev.kind = QA_Q2F_EVENT_ENTITY_EVENT; ev.actor = &player->actor.id; ev.event = 6;
  game->host->emit(game->host->ctx, ev);
  (void)qa_q2mp_kill_box(player, game);
  game->link(game->ctx, player);
}
static void disguise_touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  (void)ctx;
  if (!game->host->is_player(game->host->ctx, contact.other)) return;
  qa_q2f_entity *player = q2mp_ent(game, true, contact.other);
  if (player != NULL) {
    double high = player->flags >= 4294967296.0 ? 4294967296.0 : 0;
    player->flags = (double)(((q2mp_u32(entity->spawnflags) & 4u) != 0)
        ? (q2mp_u32(player->flags) & ~0x8000u) : (q2mp_u32(player->flags) | 0x8000u)) + high;
  }
}
static void disguise_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                            const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  game->solid(game->ctx, entity, entity->solid == QA_Q2F_SOLID_NONE ? QA_Q2F_SOLID_TRIGGER : QA_Q2F_SOLID_NONE);
}
static void core_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  entity->visible = !entity->visible;
  game->show(game->ctx, entity);
}
static void steam_start_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_rogue_entities *e = ctx;
  entity->use = e->steam_use;
  if (strcmp(entity->target, "") != 0) {
    size_t n = 0;
    qa_q2f_entity *const *found = game->targets(game->ctx, entity->target, &n);
    if (n > 0) { entity->enemy = found[0]->actor.id; entity->has_enemy = true; }
    else entity->has_enemy = false;
    if (!entity->has_enemy) {
      char message[256];
      (void)snprintf(message, sizeof message, "%s: %s is a bad target", entity->classname, entity->target);
      game->host->diagnostic(game->host->ctx, message);
    }
  } else {
    entity->movedir = qa_q2mp_movedir(q2mp_body(game, entity).angles);
    qa_q2f_body_changes c = q2mp_changes();
    c.has_angles = true; c.angles = q2mp_zero();
    q2mp_move(game, entity, c, false, false);
  }
  entity->count = (double)(q2mp_u32(entity->count != 0 ? entity->count : 32) & 255u);
  if (entity->speed == 0) entity->speed = 75;
  double sounds = qa_q2mp_integer_field(&entity->spawn, "sounds", 0);
  entity->style = (double)(q2mp_u32(sounds != 0 ? sounds : 8) & 255u);
  entity->wait *= 1000;
  entity->visible = false;
  game->show(game->ctx, entity);
}
static void steam_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)activator;
  qa_q2mp_rogue_entities *e = ctx;
  if (e->steam_id > 20000) e->steam_id = fmod(e->steam_id, 20000);
  e->steam_id++;
  if (entity->wait == 0) {
    qa_q2f_entity *other_entity = q2mp_ent(game, other != NULL, other ? *other : entity->actor.id);
    entity->wait = other == NULL ? 1000 : (other_entity != NULL ? other_entity->wait : 0) * 1000;
  }
  qa_body_state_result target = entity->has_enemy ? q2mp_bread(game, entity->enemy)
      : (qa_body_state_result){false, {q2mp_zero(), q2mp_zero(), q2mp_zero(), {q2mp_zero(), q2mp_zero()}, NULL}};
  qa_vec3 origin = q2mp_body(game, entity).origin;
  if (target.has_value)
    entity->movedir = q2mp_norm(q2mp_sub(
        q2mp_add(target.value.origin, q2mp_scale(q2mp_add(target.value.bounds.min, target.value.bounds.max), 0.5)), origin));
  qa_q2mp_entity_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2MP_ENTITY_EVENT_STEAM;
  event.id = entity->wait > 100 ? e->steam_id : -1;
  event.origin = origin; event.direction = entity->movedir;
  event.count = entity->count; event.color = entity->style; event.speed = trunc(entity->speed);
  event.milliseconds = entity->wait > 100 ? trunc(entity->wait) : 0;
  e->hooks.emit(e->hooks.ctx, event);
}
static void anger_use_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q2mp_rogue_entities *e = ctx;
  size_t n = 0;
  qa_q2f_entity *const *found = game->targets(game->ctx, entity->killtarget, &n);
  qa_q2f_entity *target = n > 0 ? found[0] : NULL;
  if (target == NULL || strcmp(entity->target, "") == 0) return;
  target->server_flags = (double)(q2mp_u32(target->server_flags) | 4u);
  if (!q2mp_cread(game, target->actor.id).has_value) q2mp_combat_create(game, target->actor, 300, 0, false);
  else game->host->combat.set_health(game->host->combat.ctx, target->actor, 300);
  size_t count = 0;
  qa_q2f_entity *const *monsters = game->targets(game->ctx, entity->target, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q2f_entity *monster = monsters[i];
    if (monster == entity) game->host->diagnostic(game->host->ctx, "WARNING: entity used itself.");
    else if (monster->use.fn != NULL) {
      if (q2mp_health(game, monster->actor.id) < 0) return;
      e->hooks.target_anger(e->hooks.ctx, monster, target, game);
    }
    if (!q2mp_live(game, entity->actor.id)) {
      game->host->diagnostic(game->host->ctx, "entity was removed while using targets");
      return;
    }
  }
}
static void kill_players_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, const qa_actor_id *other,
                            const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  qa_vec3 origin = q2mp_body(game, entity).origin;
  size_t players = 0;
  const qa_actor_id *list = game->host->players(game->host->ctx, &players);
  for (size_t i = 0; i < players; i++)
    q2mp_damage(game, list[i], entity, entity->actor.id, &entity->actor.id, 100000, 0, q2mp_zero(), origin, q2mp_zero(),
                21, true, 32, NULL);
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q2f_entity *target = game->entities[i];
    qa_combat_state_result combat = q2mp_cread(game, target->actor.id);
    if (!combat.has_value || combat.value.health < 1 || !combat.value.can_take_damage) continue;
    for (size_t j = 0; j < players; j++) {
      qa_q2f_entity *player = q2mp_ent(game, true, list[j]);
      if (player != NULL && qa_q2mp_sight(game, player, target->actor.id)) {
        q2mp_damage(game, target->actor.id, entity, entity->actor.id, &entity->actor.id, combat.value.health, 0,
                    q2mp_zero(), q2mp_body(game, target).origin, q2mp_zero(), 21, true, 32, NULL);
        break;
      }
    }
  }
}
static void random_angles(qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2f_body_changes c = q2mp_changes();
  c.has_angles = true;
  c.angles = q2mp_v3(floor(q2mp_rnd(game) * 360), floor(q2mp_rnd(game) * 360), floor(q2mp_rnd(game) * 360));
  q2mp_move(game, entity, c, false, false);
}
static void black_light_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_rogue_entities *e = ctx;
  random_angles(entity, game);
  game->schedule(game->ctx, entity, 0.1, e->black_light);
}
static void orb_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2mp_rogue_entities *e = ctx;
  random_angles(entity, game);
  game->schedule(game->ctx, entity, 0.1, e->orb);
}

bool qa_q2mp_rogue_entities_spawn(qa_q2mp_rogue_entities *e, qa_q2f_entity *entity, qa_q2f_game *game) {
  const char *classname = entity->classname;
  if (strcmp(classname, "info_teleport_destination") == 0) {
    /* TS: break (no-op) */
  } else if (strcmp(classname, "trigger_teleport") == 0) {
    if (entity->wait == 0) entity->wait = 0.2;
    entity->delay = strcmp(entity->targetname, "") != 0 && ((q2mp_u32(entity->spawnflags) & 8u) == 0) ? 1 : 0;
    entity->use = strcmp(entity->targetname, "") == 0 ? (qa_q2f_use){NULL, NULL} : e->teleport_use;
    entity->touch = e->teleport_touch;
    entity->movedir = qa_q2mp_movedir(q2mp_body(game, entity).angles);
    qa_q2f_body_changes c = q2mp_changes();
    c.has_angles = true; c.angles = q2mp_zero();
    q2mp_move(game, entity, c, false, false);
    game->solid(game->ctx, entity, QA_Q2F_SOLID_TRIGGER);
    game->motion(game->ctx, entity, QA_Q2F_MOTION_STATIONARY);
  } else if (strcmp(classname, "trigger_disguise") == 0) {
    entity->touch = e->disguise_touch;
    entity->use = e->disguise_use;
    entity->visible = false;
    game->solid(game->ctx, entity, ((q2mp_u32(entity->spawnflags) & 2u) != 0) ? QA_Q2F_SOLID_TRIGGER : QA_Q2F_SOLID_NONE);
    game->motion(game->ctx, entity, QA_Q2F_MOTION_STATIONARY);
    game->show(game->ctx, entity);
  } else if (strcmp(classname, "target_steam") == 0) {
    if (strcmp(entity->target, "") != 0) game->schedule(game->ctx, entity, 1, e->steam_start);
    else steam_start_fn(e, entity, game);
  } else if (strcmp(classname, "target_anger") == 0) {
    if (strcmp(entity->target, "") == 0 || strcmp(entity->killtarget, "") == 0) {
      char message[128];
      (void)snprintf(message, sizeof message, "target_anger without %s!",
                     strcmp(entity->target, "") == 0 ? "target" : "killtarget");
      game->host->diagnostic(game->host->ctx, message);
      game->remove(game->ctx, entity);
    } else {
      entity->use = e->anger_use;
      entity->visible = false;
      game->show(game->ctx, entity);
    }
  } else if (strcmp(classname, "target_killplayers") == 0) {
    entity->use = e->kill_players;
    entity->visible = false;
    game->show(game->ctx, entity);
  } else if (strcmp(classname, "target_blacklight") == 0 || strcmp(classname, "target_orb") == 0) {
    if (q2mp_dm(game)) {
      game->remove(game->ctx, entity);
    } else {
      bool orb = strcmp(classname, "target_orb") == 0;
      entity->model = "models/items/spawngro2/tris.md2";
      entity->effects = (double)(q2mp_u32(entity->effects) | (orb ? 0x10000000u : (0x80000000u | 0x4000000u)));
      entity->frame = orb ? 2 : 1;
      qa_q2f_body_changes c = q2mp_changes();
      c.has_bounds = true; c.bounds = (qa_bounds){q2mp_zero(), q2mp_zero()};
      q2mp_move(game, entity, c, false, false);
      game->schedule(game->ctx, entity, 0.1, orb ? e->orb : e->black_light);
      game->show(game->ctx, entity);
    }
  } else if (strcmp(classname, "misc_nuke_core") == 0) {
    entity->model = "models/objects/core/tris.md2";
    entity->use = e->core_use;
    game->show(game->ctx, entity);
  } else return false;
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2mp_rogue_entities_callbacks(e));
  return true;
}

void qa_q2mp_rogue_entities_init(qa_q2mp_rogue_entities *e, qa_q2mp_entity_hooks hooks) {
  memset(e, 0, sizeof *e);
  e->hooks = hooks;
  e->steam_start = (qa_q2f_think){e, steam_start_fn};
  e->black_light = (qa_q2f_think){e, black_light_fn};
  e->orb = (qa_q2f_think){e, orb_fn};
  e->teleport_touch = (qa_q2f_touch){e, teleport_touch_fn};
  e->disguise_touch = (qa_q2f_touch){e, disguise_touch_fn};
  e->teleport_use = (qa_q2f_use){e, teleport_use_fn};
  e->disguise_use = (qa_q2f_use){e, disguise_use_fn};
  e->steam_use = (qa_q2f_use){e, steam_use_fn};
  e->anger_use = (qa_q2f_use){e, anger_use_fn};
  e->kill_players = (qa_q2f_use){e, kill_players_fn};
  e->core_use = (qa_q2f_use){e, core_use_fn};
  e->cb_think[0] = (qa_q2f_named_think){"target_steam_start", e->steam_start};
  e->cb_think[1] = (qa_q2f_named_think){"blacklight_think", e->black_light};
  e->cb_think[2] = (qa_q2f_named_think){"orb_think", e->orb};
  e->cb_touch[0] = (qa_q2f_named_touch){"trigger_teleport_touch", e->teleport_touch};
  e->cb_touch[1] = (qa_q2f_named_touch){"trigger_disguise_touch", e->disguise_touch};
  e->cb_use[0] = (qa_q2f_named_use){"trigger_teleport_use", e->teleport_use};
  e->cb_use[1] = (qa_q2f_named_use){"trigger_disguise_use", e->disguise_use};
  e->cb_use[2] = (qa_q2f_named_use){"use_target_steam", e->steam_use};
  e->cb_use[3] = (qa_q2f_named_use){"target_anger_use", e->anger_use};
  e->cb_use[4] = (qa_q2f_named_use){"target_killplayers_use", e->kill_players};
  e->cb_use[5] = (qa_q2f_named_use){"misc_nuke_core_use", e->core_use};
}
qa_q2f_callback_definitions qa_q2mp_rogue_entities_callbacks(qa_q2mp_rogue_entities *e) {
  qa_q2f_callback_definitions defs;
  memset(&defs, 0, sizeof defs);
  defs.think = e->cb_think; defs.think_count = 3;
  defs.touch = e->cb_touch; defs.touch_count = 2;
  defs.use = e->cb_use; defs.use_count = 6;
  return defs;
}
