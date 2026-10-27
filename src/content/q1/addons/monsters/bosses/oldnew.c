/* Port of src/content/q1/addons/monsters/bosses/oldnew.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/oldnew.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/effects.h"
#include "qa/content/q1/addons/monsters/bosses/frames/oldnew.h"
#include "qa/content/q1/addons/monsters/bosses/oldnew_children.h"
#include "qa/content/q1/addons/monsters/bosses/oldnew_projectiles.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/content/q1/addons/monsters/bosses/sphere_points.h"
#include "qa/content/q1/addons/monsters/bosses/szombie.h"
#include "qa/foundation/support.h"

#define FLAGS(x) ((int32_t)(x))

static const char *const oldnew_classnames[] = {"monster_oldone_new"};
static const qa_q1am_species oldnew_spec = {
  "oldone", NULL, "oldone", NULL, "oldnew_idle1", "oldnew_idle1", "oldnew_walk1",
  "", NULL, "walk", oldnew_classnames, 1, NULL, 0,
  12000, -INFINITY, {{-128, -128, -24}, {128, 128, 256}}, false,
};
static void oldnew_dispatch(qa_q1am_monster *monster, const char *name) {
  qa_q1am_oldnew_action(monster->owner, name);
}
static const qa_q1am_source *oldnew_source(void) {
  static const qa_q1am_source *source;
  if (source == NULL)
    source = qa_q1am_boss_build_source("mg3:oldnew", &qa_q1am_oldnew_frames, QA_Q1AM_KIND_OLDNEW,
                                       "Oldone callback received another controller",
                                       oldnew_dispatch, NULL, 0);
  return source;
}
static qa_q1am_oldnew *as_oldnew(qa_q1am_monster *m) { return m->owner; }
static void oldnew_spawn(qa_q1am_monster *m) {
  qa_q1am_oldnew *self = as_oldnew(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = self->context;
  if (game->options.deathmatch != 0) {
    game->remove(game->ctx, entity);
    return;
  }
  entity->solid = "slidebox";
  entity->movement = "step";
  (void)snprintf(entity->model, sizeof entity->model, "%s", "progs/oldone.mdl");
  game->set_bounds(game->ctx, entity,
                   (FLAGS(entity->spawnflags) & 128) != 0
                       ? (qa_bounds){{1, 1, -24}, {1, 1, 0}} : oldnew_spec.bounds);
  game->combat_set_health(game->ctx, entity->actor, 12000);
  entity->max_health = 12000;
  qa_q1am_actor_set_damageable(game, entity, true);
  entity->aimed_damage = true;
  entity->movement_flags = 32;
  game->combat_set_team(game->ctx, entity->actor, "q1:monsters");
  context->set_vector(context->ctx, entity, "view_ofs", (qa_vec3){0, 0, 24});
  entity->pain = game->named_pain(game->ctx, entity, "mg3:oldnew:monster_pain");
  entity->die = game->named_die(game->ctx, entity, "mg3:oldnew:monster_die");
  entity->use = game->named_use(game->ctx, entity, "mg3:oldnew:monster_use");
  entity->yaw_speed = 10;
  entity->ideal_yaw = game->body(game->ctx, entity).angles.y;
  game->total_monsters++;
  qa_q1am_event event = {0};
  event.kind = "monster-total";
  event.total = game->total_monsters;
  game->emit(game->ctx, &event);
  qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, oldnew_spec.stand);
  qa_q1am_monster_delay(m, 0.1);
}
static void oldnew_wave(qa_q1am_oldnew *self, const char *field) {
  qa_q1am_monster *m = &self->base;
  const char *target = qa_q1am_actor_text(m->entity, field);
  if (target[0] == '\0') return;
  char previous[128], wanted[128];
  (void)snprintf(previous, sizeof previous, "%s", m->entity->target);
  (void)snprintf(wanted, sizeof wanted, "%s", target);
  (void)snprintf(m->entity->target, sizeof m->entity->target, "%s", wanted);
  m->game->use_targets(m->game->ctx, m->entity, m->entity->has_activator, m->entity->activator);
  (void)snprintf(m->entity->target, sizeof m->entity->target, "%s", previous);
}
static void oldnew_pain(qa_q1am_monster *m, bool has, qa_actor_id attacker, double damage) {
  (void)damage;
  qa_q1am_monster_retaliate(m, has, attacker);
  qa_q1am_oldnew *self = as_oldnew(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = self->context;
  if (qa_q1am_actor_number(entity, "boss_immune") != 0) return;
  bool trigger = false;
  if (qa_q1am_actor_number(entity, "boss_phase") == 0) {
    trigger = true;
    context->set_number(context->ctx, entity, "boss_phase", 1);
  }
  if (qa_q1am_actor_number(entity, "boss_phase") == 1 &&
      game->health(game->ctx, entity->actor.id) < entity->max_health * 0.75) {
    trigger = true;
    context->set_number(context->ctx, entity, "boss_phase", 2);
    qa_q1am_spawn_eye(context, entity);
    oldnew_wave(self, "wave1");
  }
  if (qa_q1am_actor_number(entity, "boss_phase") == 2 &&
      game->health(game->ctx, entity->actor.id) < entity->max_health * 0.5) {
    trigger = true;
    context->set_number(context->ctx, entity, "boss_phase", 3);
    oldnew_wave(self, "wave2");
  }
  if (qa_q1am_actor_number(entity, "boss_phase") == 3 &&
      game->health(game->ctx, entity->actor.id) < entity->max_health * 0.25) {
    trigger = true;
    context->set_number(context->ctx, entity, "boss_phase", 4);
    oldnew_wave(self, "wave2");
  }
  if (!trigger) return;
  context->set_number(context->ctx, entity, "boss_immune", 1);
  game->sound(game->ctx, entity, "orb/orb_pain.wav", "voice", 1);
  m->state->pain_finished = game->time + 2.1;
  m->ops.play(m, "oldnew_thrash1");
}
static void oldnew_die(qa_q1am_monster *m, bool has, qa_actor_id attacker) {
  if (m->counted_death) return;
  qa_q1am_oldnew *self = as_oldnew(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = self->context;
  m->state->has_enemy = has;
  m->state->enemy = attacker;
  qa_q1am_actor_set_damageable(game, entity, false);
  entity->touch.present = false;
  qa_q1am_monster_count_kill(m);
  context->set_number(context->ctx, entity, "boss_immune", 1);
  context->set_number(context->ctx, entity, "cnt", 0);
  m->ops.play(m, "oldnew_death1");
  qa_q1am_cleanup_oldnew(context);
  qa_q1am_spawn_sphere_manager(context, entity,
                               game->options.skill > 2 ? qa_q1am_actor_number(entity, "boss_phase") : 1);
}
void qa_q1am_oldnew_attack(qa_q1am_oldnew *oldnew) {
  qa_q1am_monster *m = &oldnew->base;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = oldnew->context;
  if (qa_q1am_actor_number(entity, "boss_immune") != 0) return;
  double phase = qa_q1am_actor_number(entity, "boss_phase");
  if ((int64_t)qa_q1am_actor_number(entity, "cnt") % 3 != 0) {
    qa_q1am_spawn_sphere_chunk_manager(context, entity, phase <= 3 ? 1 : 2);
  } else if (phase == 0) {
    qa_q1am_spawn_sphere_chunk_manager(context, entity, 1);
  } else {
    if (phase == 1) {
      if (entity->count == 0) qa_q1am_spawn_spammer(context, entity);
      else if (entity->count == 1) qa_q1am_spawn_swiper(context, entity);
    } else if (phase == 2) {
      if (entity->count == 0) qa_q1am_spawn_vortex(context, entity);
      else if (entity->count == 1) qa_q1am_spawn_swiper(context, entity);
      else if (entity->count == 2) qa_q1am_spawn_spammer(context, entity);
    } else if (phase == 3) {
      if (entity->count == 0) qa_q1am_spawn_blaster(context, entity);
      else if (entity->count >= 1 && entity->count <= 3) qa_q1am_spawn_swiper(context, entity);
    } else if (phase >= 4) {
      if (entity->count == 0) qa_q1am_spawn_spammer(context, entity);
      else if (entity->count == 1) qa_q1am_spawn_eye(context, entity);
      else if (entity->count == 2) qa_q1am_spawn_swiper(context, entity);
      else if (entity->count == 3) qa_q1am_spawn_blaster(context, entity);
    }
    entity->count++;
    if (entity->count > (phase == 1 ? 1 : phase == 2 ? 2 : 3)) entity->count = 0;
  }
  context->set_number(context->ctx, entity, "cnt", qa_q1am_actor_number(entity, "cnt") + 1);
}
static void oldnew_auto_gun(qa_q1am_oldnew *self, double side, double offset, double count) {
  qa_q1am_monster *m = &self->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  if (qa_q1am_actor_number(entity, "boss_immune") != 0 ||
      count > qa_q1am_actor_number(entity, "boss_phase") + 2 || game->options.skill == 0 ||
      count > game->options.skill + 1)
    return;
  qa_q1am_basis basis = qa_q1am_game_make_vectors(game, game->body(game->ctx, entity).angles);
  game->sound(game->ctx, entity, "weapons/spike2.wav", "weapon", 1);
  qa_q1am_auto_gun(self->context, entity,
                   qa_add3(qa_add3(qa_q1am_monster_origin(m), (qa_vec3){0, 0, 80}),
                           qa_scale3(basis.right, side * 64)),
                   offset);
}
void qa_q1am_oldnew_action(qa_q1am_oldnew *oldnew, const char *name) {
  qa_q1am_monster *m = &oldnew->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = oldnew->context;
  struct gun_case { const char *name; double side, offset, count; bool zombie; };
  static const struct gun_case gun_cases[] = {
    {"oldnew:oldnew_walk21", 1, 0.05, 1, false},
    {"oldnew:oldnew_walk22", 1, 0, 2, false},
    {"oldnew:oldnew_walk23", 1, -0.05, 3, true},
    {"oldnew:oldnew_walk24", 1, -0.1, 4, false},
    {"oldnew:oldnew_walk25", 1, -0.15, 5, false},
    {"oldnew:oldnew_walk36", -1, -0.05, 1, false},
    {"oldnew:oldnew_walk37", -1, 0, 2, false},
    {"oldnew:oldnew_walk38", -1, 0.05, 3, false},
    {"oldnew:oldnew_walk39", -1, 0.1, 4, false},
    {"oldnew:oldnew_walk40", -1, 0.15, 5, false},
  };
  for (size_t i = 0; i < sizeof gun_cases / sizeof gun_cases[0]; i++)
    if (strcmp(name, gun_cases[i].name) == 0) {
      qa_q1am_monster_face(m);
      if (gun_cases[i].zombie) qa_q1am_spawn_shub_zombie(context);
      oldnew_auto_gun(oldnew, gun_cases[i].side, gun_cases[i].offset, gun_cases[i].count);
      return;
    }
  if (strcmp(name, "oldnew:oldnew_idle1") == 0) {
    m->ops.ai(m, QA_Q1AM_AI_STAND, 0);
  } else if (strcmp(name, "oldnew:oldnew_walk1") == 0) {
    qa_q1am_monster_face(m);
    context->set_number(context->ctx, entity, "boss_immune", 0);
  } else if (strcmp(name, "oldnew:oldnew_walk2") == 0) {
    qa_q1am_monster_face(m);
  } else if (strcmp(name, "oldnew:oldnew_walk14") == 0) {
    qa_q1am_monster_face(m);
    qa_q1am_oldnew_attack(oldnew);
  } else if (strcmp(name, "oldnew:oldnew_walk45") == 0) {
    qa_q1am_monster_face(m);
    qa_q1am_spawn_shub_zombie(context);
  } else if (strcmp(name, "oldnew:oldnew_thrash4") == 0) {
    qa_q1am_pain_lightning(context, entity, (qa_vec3){0, 0, 100});
  } else if (strcmp(name, "oldnew:oldnew_thrash14") == 0) {
    context->set_number(context->ctx, entity, "boss_immune", 1);
    qa_q1am_spawn_sphere_manager(context, entity,
                                 game->options.skill > 2 ? qa_q1am_actor_number(entity, "boss_phase") : 1);
  } else if (strcmp(name, "oldnew:oldnew_thrash15") == 0) {
    context->set_number(context->ctx, entity, "boss_immune", 0);
  } else if (strcmp(name, "oldnew:oldnew_death1") == 0) {
    game->sound(game->ctx, entity, "boss2/death.wav", "voice", 1);
  } else if (strcmp(name, "oldnew:oldnew_death15") == 0) {
    context->set_number(context->ctx, entity, "cnt", qa_q1am_actor_number(entity, "cnt") + 1);
    if (qa_q1am_actor_number(entity, "cnt") != 3)
      qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "oldnew_death1");
  } else if (strcmp(name, "oldnew:oldnew_death16") == 0 ||
             strcmp(name, "oldnew:oldnew_death17") == 0 ||
             strcmp(name, "oldnew:oldnew_death18") == 0 ||
             strcmp(name, "oldnew:oldnew_death19") == 0) {
    qa_q1am_event event = {0};
    event.kind = "lightstyle";
    event.style = 0;
    event.pattern = strcmp(name, "oldnew:oldnew_death16") == 0 ? "g"
                    : strcmp(name, "oldnew:oldnew_death17") == 0 ? "c"
                    : strcmp(name, "oldnew:oldnew_death18") == 0 ? "b" : "a";
    game->emit(game->ctx, &event);
  } else if (strcmp(name, "oldnew:oldnew_death20") == 0) {
    qa_q1am_oldnew_finish(oldnew);
  } else {
    qa_range_error("Unknown oldone action %s", name);
  }
}
void qa_q1am_oldnew_finish(qa_q1am_oldnew *oldnew) {
  qa_q1am_monster *m = &oldnew->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  game->sound(game->ctx, entity, "boss2/pop2.wav", "voice", 1);
  size_t count = 0;
  qa_actor_id *players = game->players(game->ctx, &count);
  bool has_player = count > 0;
  qa_actor_id player = has_player ? players[0] : (qa_actor_id){0};
  free(players);
  if (!game->options.coop && (!has_player || game->health(game->ctx, player) <= 0)) {
    m->ops.play(m, "oldnew_idle1");
    return;
  }
  for (int i = 0; i < 100; i++) {
    qa_vec3 point = qa_q1am_sphere_point(i);
    if (point.z > 0)
      qa_q1am_throw_gib_vector(oldnew->context,
                               qa_add3(qa_add3(qa_q1am_monster_origin(m), qa_scale3(point, 64)), (qa_vec3){0, 0, 48}),
                               point);
  }
  game->remove(game->ctx, entity);
  qa_q1am_event event = {0};
  event.kind = "music";
  event.track = 3;
  event.loop_track = 3;
  oldnew->context->services_emit(oldnew->context->services_ctx, &event);
  memset(&event, 0, sizeof event);
  event.kind = "lightstyle";
  event.style = 0;
  event.pattern = "m";
  game->emit(game->ctx, &event);
  qa_q1am_oldnew_credits(oldnew->context);
}
qa_q1am_actor *qa_q1am_throw_gib_vector(qa_q1am_context *context, qa_vec3 origin, qa_vec3 direction) {
  qa_q1am_game *game = context->game;
  qa_q1am_actor *gib = game->create(game->ctx, "gib_vector");
  gib->classname[0] = '\0';
  double choice = game->random(game->ctx);
  (void)snprintf(gib->model, sizeof gib->model, "progs/gib%d.mdl", choice < 0.3 ? 1 : choice < 0.6 ? 2 : 3);
  double speed = 800 + (game->random(game->ctx) * 2 - 1) * 200;
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_velocity = true;
  patch.velocity = qa_scale3(direction, speed);
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  game->set_body(game->ctx, gib, &patch);
  gib->movement = "bounce";
  gib->solid = "none";
  gib->angular_velocity = (qa_vec3){game->random(game->ctx) * 600, game->random(game->ctx) * 600,
                                    game->random(game->ctx) * 600};
  context->set_number(context->ctx, gib, "ltime", game->time);
  game->schedule(game->ctx, gib, 10 + game->random(game->ctx) * 10,
                 game->named_action(game->ctx, gib, "SUB_Remove"));
  game->link(game->ctx, gib);
  return gib;
}
void qa_q1am_oldnew_credits(qa_q1am_context *context) {
  if (context->game->world)
    qa_q1am_actor_set_field(context->game->world, "addon.intermissiontext", "$map_dopa_endtext_final");
  context->base->level_begin(context->base->ctx, "start", false, (qa_actor_id){0});
}
void qa_q1am_oldnew_init(qa_q1am_oldnew *oldnew, qa_q1am_context *context, qa_q1am_actor *entity) {
  memset(oldnew, 0, sizeof *oldnew);
  oldnew->context = context;
  qa_q1am_monster_init(&oldnew->base, context->game, entity, &oldnew_spec, context->base, oldnew_source());
  oldnew->base.owner = oldnew;
  oldnew->base.kind = QA_Q1AM_KIND_OLDNEW;
  oldnew->super_ops = oldnew->base.ops;
  oldnew->base.ops.spawn = oldnew_spawn;
  oldnew->base.ops.pain = oldnew_pain;
  oldnew->base.ops.die = oldnew_die;
}
static qa_q1am_monster *create_oldnew(void *ctx, qa_q1am_actor *entity) {
  qa_q1am_oldnew *oldnew = malloc(sizeof *oldnew);
  if (!oldnew) qa_range_error("Oldone allocation failed");
  qa_q1am_oldnew_init(oldnew, ctx, entity);
  return &oldnew->base;
}
static void destroy_oldnew(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  free(monster->owner);
}
static void credits_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game; (void)entity;
  qa_q1am_oldnew_credits(ctx);
}
static bool before_health(void *ctx, const qa_q1am_damage_request *request) {
  qa_q1am_game *game = ((qa_q1am_context *)ctx)->game;
  qa_q1am_actor *entity = game->entity(game->ctx, true, request->target);
  return entity == NULL || strcmp(entity->classname, "monster_oldone_new") != 0 ||
         qa_q1am_actor_number(entity, "boss_immune") != 1;
}
void qa_q1am_register_oldnew(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  qa_q1am_register_oldnew_projectiles(context);
  qa_q1am_register_oldnew_children(context);
  qa_q1am_boss_create create = {context, create_oldnew, destroy_oldnew};
  qa_q1am_register_boss_controllers(context, "mg3:oldnew", "monster_oldone_new", create);
  qa_q1am_handlers handlers = {0};
  handlers.ctx = context;
  handlers.action = credits_handler;
  game->named_register(game->ctx, QA_Q1AM_OLDNEW_PREFIX "oldnew_credits", handlers);
  qa_q1am_damage_effects effects = {context, before_health, NULL};
  game->register_damage_source_effects(game->ctx, "mg3:oldone", effects);
}
