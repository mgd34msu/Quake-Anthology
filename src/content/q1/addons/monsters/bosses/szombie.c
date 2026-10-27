/* Port of src/content/q1/addons/monsters/bosses/szombie.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/szombie.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/frames/szombie.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/foundation/support.h"

#define FLAGS(x) ((int32_t)(x))

static const char *const szombie_classnames[] = {"monster_szombie"};
static const qa_q1am_species szombie_spec = {
  "zombie", NULL, "zombie", NULL, "szombie_stand1", "szombie_walk1", "szombie_run1",
  "", NULL, "walk", szombie_classnames, 1, NULL, 0,
  60, -INFINITY, {{-16, -16, -24}, {16, 16, 32}}, true,
};
static void szombie_dispatch(qa_q1am_monster *monster, const char *name) {
  qa_q1am_shub_zombie_action(monster->owner, name);
}
static const qa_q1am_source *szombie_source(void) {
  static const qa_q1am_source *source;
  if (source == NULL)
    source = qa_q1am_boss_build_source("mg3:szombie", &qa_q1am_szombie_frames, QA_Q1AM_KIND_SZOMBIE,
                                       "Shub zombie callback received another controller",
                                       szombie_dispatch, NULL, 0);
  return source;
}
static qa_q1am_shub_zombie *as_szombie(qa_q1am_monster *m) { return m->owner; }
static void szombie_spawn(qa_q1am_monster *m) {
  qa_q1am_shub_zombie *self = as_szombie(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_context *context = self->context;
  game->combat_set_health(game->ctx, entity->actor, 60);
  entity->max_health = 60;
  (void)snprintf(entity->classname, sizeof entity->classname, "%s", "monster_szombie");
  m->in_pain = 2;
  entity->solid = "none";
  entity->movement = "step";
  (void)snprintf(entity->model, sizeof entity->model, "%s", "progs/zombie.mdl");
  game->set_bounds(game->ctx, entity, szombie_spec.bounds);
  qa_q1am_actor *owner = game->entity(game->ctx, entity->has_owner, entity->owner);
  game->set_origin(game->ctx, entity, owner ? game->body(game->ctx, owner).origin : QA_Q1AM_ZERO);
  qa_q1am_actor_set_damageable(game, entity, true);
  entity->aimed_damage = true;
  entity->ideal_yaw = game->body(game->ctx, entity).angles.y;
  context->set_vector(context->ctx, entity, "view_ofs", (qa_vec3){0, 0, 25});
  game->combat_set_team(game->ctx, entity->actor, "q1:monsters");
  entity->movement_flags = (double)(FLAGS(entity->movement_flags) | 32);
  if (entity->yaw_speed == 0) entity->yaw_speed = 20;
  context->set_number(context->ctx, entity, "combat_style", 2);
  entity->pain = game->named_pain(game->ctx, entity, "mg3:szombie:monster_pain");
  entity->die = game->named_die(game->ctx, entity, "mg3:szombie:monster_die");
  game->total_monsters++;
  qa_q1am_event event = {0};
  event.kind = "monster-total";
  event.total = game->total_monsters;
  game->emit(game->ctx, &event);
  size_t count = 0;
  qa_actor_id *players = game->players(game->ctx, &count);
  m->state->has_enemy = count > 0;
  if (count > 0) m->state->enemy = players[0];
  free(players);
  entity->frame = 174;
  qa_q1am_monster_set_frame(m->current_frame, sizeof m->current_frame, "szombie_paine10");
  qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "szombie_paine11");
  qa_q1am_monster_delay(m, 2);
}
static void szombie_play(qa_q1am_monster *m, const char *name) {
  char renamed[96];
  if (strncmp(name, "zombie_", 7) == 0) {
    (void)snprintf(renamed, sizeof renamed, "szombie_%s", name + 7);
    name = renamed;
    as_szombie(m)->super_ops.play(m, name);
    return;
  }
  as_szombie(m)->super_ops.play(m, name);
}
static void szombie_melee_attack(qa_q1am_monster *m) {
  double r = m->game->random(m->game->ctx);
  m->ops.play(m, r < 0.3 ? "szombie_atta1" : r < 0.6 ? "szombie_attb1" : "szombie_attc1");
}
static void szombie_die(qa_q1am_monster *m, bool has, qa_actor_id attacker) {
  if (m->counted_death) return;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  m->state->has_enemy = has;
  m->state->enemy = attacker;
  qa_q1am_actor_set_damageable(game, entity, false);
  entity->touch.present = false;
  qa_q1am_monster_count_kill(m);
  game->sound(game->ctx, entity, "zombie/z_gib.wav", "voice", 1);
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (int i = 0; i < 3; i++)
    game->throw_gib(game->ctx, game, qa_q1am_monster_origin(m), models[i],
                    game->health(game->ctx, entity->actor.id));
  game->remove(game->ctx, entity);
}
static void szombie_grenade(qa_q1am_shub_zombie *zombie, qa_vec3 offset) {
  qa_q1am_monster *m = &zombie->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  game->sound(game->ctx, entity, "zombie/z_shot1.wav", "weapon", 1);
  qa_q1am_actor *missile = game->create(game->ctx, "szombie_grenade");
  missile->classname[0] = '\0';
  missile->has_owner = true;
  missile->owner = entity->actor.id;
  missile->movement = "bounce";
  missile->solid = "bbox";
  qa_q1am_basis basis = game->basis;
  qa_vec3 origin = qa_add3(qa_add3(qa_add3(qa_q1am_monster_origin(m), qa_scale3(basis.forward, offset.x)),
                                   qa_scale3(basis.right, offset.y)),
                           qa_scale3(basis.up, offset.z - 24));
  qa_q1am_game_make_vectors(game, game->body(game->ctx, entity).angles);
  qa_vec3 target;
  qa_vec3 velocity = qa_scale3(qa_q1am_normalize(qa_sub3(
      qa_q1am_monster_target(m, &target) ? target : QA_Q1AM_ZERO, origin)), 600);
  velocity.z = 200;
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_velocity = true;
  patch.velocity = velocity;
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  game->set_body(game->ctx, missile, &patch);
  missile->angular_velocity = (qa_vec3){3000, 1000, 2000};
  (void)snprintf(missile->model, sizeof missile->model, "%s", "progs/zom_gib.mdl");
  missile->touch = game->named_touch(game->ctx, missile, "mg3:szombie:grenade_touch");
  game->schedule(game->ctx, missile, 2.5, game->named_action(game->ctx, missile, "SUB_Remove"));
  game->link(game->ctx, missile);
}
#define SZ_AI(mode, dist) do { m->ops.ai(m, mode, dist); return; } while (0)
void qa_q1am_shub_zombie_action(qa_q1am_shub_zombie *zombie, const char *name) {
  qa_q1am_monster *m = &zombie->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  struct action_case { const char *name; qa_q1am_ai mode; double distance; };
  static const struct action_case ai_cases[] = {
    {"szombie:szombie_stand1", QA_Q1AM_AI_STAND, 0},
    {"szombie:szombie_walk1", QA_Q1AM_AI_WALK, 0},
    {"szombie:szombie_walk2", QA_Q1AM_AI_WALK, 2},
    {"szombie:szombie_walk3", QA_Q1AM_AI_WALK, 3},
    {"szombie:szombie_walk5", QA_Q1AM_AI_WALK, 1},
    {"szombie:szombie_run2", QA_Q1AM_AI_RUN, 1},
    {"szombie:szombie_run3", QA_Q1AM_AI_RUN, 0},
    {"szombie:szombie_run5", QA_Q1AM_AI_RUN, 2},
    {"szombie:szombie_run6", QA_Q1AM_AI_RUN, 3},
    {"szombie:szombie_run7", QA_Q1AM_AI_RUN, 4},
    {"szombie:szombie_run15", QA_Q1AM_AI_RUN, 6},
    {"szombie:szombie_run16", QA_Q1AM_AI_RUN, 7},
    {"szombie:szombie_paina2", QA_Q1AM_AI_PAINFORWARD, 3},
    {"szombie:szombie_paina3", QA_Q1AM_AI_PAINFORWARD, 1},
    {"szombie:szombie_paina4", QA_Q1AM_AI_PAIN, 1},
    {"szombie:szombie_paina5", QA_Q1AM_AI_PAIN, 3},
    {"szombie:szombie_painb2", QA_Q1AM_AI_PAIN, 2},
    {"szombie:szombie_painb3", QA_Q1AM_AI_PAIN, 8},
    {"szombie:szombie_painb4", QA_Q1AM_AI_PAIN, 6},
    {"szombie:szombie_paine3", QA_Q1AM_AI_PAIN, 5},
    {"szombie:szombie_paine25", QA_Q1AM_AI_PAINFORWARD, 5},
  };
  for (size_t i = 0; i < sizeof ai_cases / sizeof ai_cases[0]; i++)
    if (strcmp(name, ai_cases[i].name) == 0) SZ_AI(ai_cases[i].mode, ai_cases[i].distance);
  if (strcmp(name, "szombie:szombie_walk19") == 0) {
    m->ops.ai(m, QA_Q1AM_AI_WALK, 0);
    if (game->random(game->ctx) < 0.2) game->sound(game->ctx, entity, "zombie/z_idle.wav", "voice", 2);
  } else if (strcmp(name, "szombie:szombie_run1") == 0) {
    m->ops.ai(m, QA_Q1AM_AI_RUN, 1);
    m->in_pain = 0;
  } else if (strcmp(name, "szombie:szombie_run18") == 0) {
    m->ops.ai(m, QA_Q1AM_AI_RUN, 8);
    if (game->random(game->ctx) < 0.2) game->sound(game->ctx, entity, "zombie/z_idle.wav", "voice", 2);
    if (game->random(game->ctx) > 0.8) game->sound(game->ctx, entity, "zombie/z_idle1.wav", "voice", 2);
  } else if (strcmp(name, "szombie:szombie_atta1") == 0) {
    qa_q1am_monster_face(m);
  } else if (strcmp(name, "szombie:szombie_atta13") == 0) {
    qa_q1am_monster_face(m);
    szombie_grenade(zombie, (qa_vec3){-10, -22, 30});
  } else if (strcmp(name, "szombie:szombie_attb14") == 0) {
    qa_q1am_monster_face(m);
    szombie_grenade(zombie, (qa_vec3){-10, -24, 29});
  } else if (strcmp(name, "szombie:szombie_attc12") == 0) {
    qa_q1am_monster_face(m);
    szombie_grenade(zombie, (qa_vec3){-12, -19, 29});
  } else if (strcmp(name, "szombie:szombie_paina1") == 0) {
    game->sound(game->ctx, entity, "zombie/z_pain.wav", "voice", 1);
  } else if (strcmp(name, "szombie:szombie_painb1") == 0) {
    game->sound(game->ctx, entity, "zombie/z_pain1.wav", "voice", 1);
  } else if (strcmp(name, "szombie:szombie_painb9") == 0) {
    game->sound(game->ctx, entity, "zombie/z_fall.wav", "body", 1);
  } else if (strcmp(name, "szombie:szombie_paine1") == 0) {
    game->sound(game->ctx, entity, "zombie/z_pain.wav", "voice", 1);
    game->combat_set_health(game->ctx, entity->actor, 60);
  } else if (strcmp(name, "szombie:szombie_paine10") == 0) {
    game->sound(game->ctx, entity, "zombie/z_fall.wav", "body", 1);
    entity->solid = "none";
    game->link(game->ctx, entity);
  } else if (strcmp(name, "szombie:szombie_paine11") == 0) {
    qa_q1am_monster_delay(m, entity->next_think - game->time + 5);
    game->combat_set_health(game->ctx, entity->actor, 60);
  } else if (strcmp(name, "szombie:szombie_paine12") == 0) {
    game->combat_set_health(game->ctx, entity->actor, 60);
    game->sound(game->ctx, entity, "zombie/z_idle.wav", "voice", 2);
    entity->solid = "slidebox";
    if (!game->walk_move(game->ctx, entity->actor, 0, 0)) {
      qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "szombie_paine11");
      entity->solid = "none";
    }
    game->link(game->ctx, entity);
  } else {
    qa_range_error("Unknown Shub zombie action %s", name);
  }
}
void qa_q1am_shub_zombie_init(qa_q1am_shub_zombie *zombie, qa_q1am_context *context, qa_q1am_actor *entity) {
  memset(zombie, 0, sizeof *zombie);
  zombie->context = context;
  qa_q1am_monster_init(&zombie->base, context->game, entity, &szombie_spec, context->base, szombie_source());
  zombie->base.owner = zombie;
  zombie->base.kind = QA_Q1AM_KIND_SZOMBIE;
  zombie->super_ops = zombie->base.ops;
  zombie->base.ops.spawn = szombie_spawn;
  zombie->base.ops.play = szombie_play;
  zombie->base.ops.melee_attack = szombie_melee_attack;
  zombie->base.ops.die = szombie_die;
}
qa_q1am_actor *qa_q1am_spawn_shub_zombie(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  size_t total = game->entity_count(game->ctx);
  size_t zombies = 0, spawn_count = 0, eligible = 0;
  for (size_t i = 0; i < total; i++) {
    qa_q1am_actor *entity = game->entity_at(game->ctx, i);
    if (!entity) continue;
    if (strcmp(entity->classname, "monster_szombie") == 0) zombies++;
    if (strcmp(entity->classname, "info_szombie_spawn") == 0) {
      spawn_count++;
      if (entity->wait != 0 && entity->wait < game->time) eligible++;
    }
  }
  if (zombies > 32) return NULL;
  if (eligible == 0) return NULL;
  size_t wanted = (size_t)floor(game->random(game->ctx) * ((double)eligible - 1) + 0.5);
  qa_q1am_actor *point = NULL;
  /* Native selection indexes the original (unfiltered) list. */
  for (size_t i = 0, seen = 0; i < total; i++) {
    qa_q1am_actor *entity = game->entity_at(game->ctx, i);
    if (entity && strcmp(entity->classname, "info_szombie_spawn") == 0 && seen++ == wanted) point = entity;
  }
  (void)spawn_count;
  if (point == NULL) qa_range_error("Missing source zombie spawn");
  point->wait = game->time + 8;
  qa_q1am_actor *zombie = game->create(game->ctx, "monster_szombie");
  zombie->has_owner = true;
  zombie->owner = point->actor.id;
  game->spawn_entity(game->ctx, zombie);
  return zombie;
}
qa_q1am_actor *qa_q1am_spawn_homing_flame(qa_q1am_context *context, qa_q1am_actor *source) {
  qa_q1am_game *game = context->game;
  qa_q1am_actor *flame = game->create(game->ctx, "homing_flame");
  flame->classname[0] = '\0';
  flame->solid = "bbox";
  flame->movement = "flymissile";
  (void)snprintf(flame->model, sizeof flame->model, "%s", "progs/flame2.mdl");
  flame->effects = 64;
  qa_actor_id enemy;
  bool has_enemy = (source->has_monster && source->monster.has_enemy)
                       ? (enemy = source->monster.enemy, true)
                       : qa_q1am_actor_ref(source, "enemy", &enemy);
  bool use_enemy = has_enemy && game->is_player(game->ctx, true, enemy);
  if (!use_enemy) {
    size_t count = 0;
    qa_actor_id *players = game->players(game->ctx, &count);
    has_enemy = count > 0;
    if (has_enemy) enemy = players[0];
    free(players);
  }
  qa_q1am_actor_set_ref(flame, "enemy", has_enemy, enemy);
  qa_vec3 origin = qa_add3(game->body(game->ctx, source).origin, (qa_vec3){0, 0, 4});
  flame->speed = 400;
  qa_q1am_body target_body;
  qa_vec3 target = has_enemy && game->body_read(game->ctx, enemy, &target_body) ? target_body.origin : QA_Q1AM_ZERO;
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_velocity = true;
  patch.velocity = qa_scale3(qa_q1am_normalize(qa_sub3(target, origin)), 400);
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  game->set_body(game->ctx, flame, &patch);
  flame->projectile = "spike";
  flame->touch = game->named_touch(game->ctx, flame, "projectile_touch");
  context->set_number(context->ctx, flame, "waitmin", game->time);
  game->schedule(game->ctx, flame, 0.1, game->named_action(game->ctx, flame, "mg3:szombie:homing_flame_think"));
  game->link(game->ctx, flame);
  game->remove(game->ctx, source);
  return flame;
}
static qa_q1am_monster *create_szombie(void *ctx, qa_q1am_actor *entity) {
  qa_q1am_shub_zombie *zombie = malloc(sizeof *zombie);
  if (!zombie) qa_range_error("Shub zombie allocation failed");
  qa_q1am_shub_zombie_init(zombie, ctx, entity);
  return &zombie->base;
}
static void destroy_szombie(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  free(monster->owner);
}
static void spawn_point_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)ctx; (void)game;
  entity->wait = -1;
}
static void grenade_touch_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                                  bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal; (void)normal;
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  const char *classname = game->classname_of(game->ctx, other);
  if (strcmp(classname, "monster_oldone_new") == 0 || strcmp(classname, "oldnew_child") == 0) {
    game->remove(game->ctx, entity);
    return;
  }
  if (game->can_take_damage(game->ctx, other)) {
    game->damage(game->ctx, other, entity->actor.id, entity->has_owner, entity->owner, 10);
    game->sound(game->ctx, entity, "zombie/z_hit.wav", "weapon", 1);
    game->remove(game->ctx, entity);
    return;
  }
  game->sound(game->ctx, entity, "zombie/z_miss.wav", "weapon", 1);
  qa_q1am_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = QA_Q1AM_ZERO;
  game->set_body(game->ctx, entity, &patch);
  entity->angular_velocity = QA_Q1AM_ZERO;
  entity->touch = game->named_touch(game->ctx, entity, "SUB_Remove");
}
static void homing_flame_think(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  qa_q1am_context *context = ctx;
  qa_vec3 origin = game->body(game->ctx, entity).origin;
  if (qa_q1am_actor_number(entity, "waitmin") + 3 < game->time) {
    qa_q1am_event event = {0};
    event.kind = "colored-explosion";
    event.origin = origin;
    event.color_start = 244;
    event.color_length = 3;
    context->services_emit(context->services_ctx, &event);
    game->radius_damage(game->ctx, entity->actor.id, true, entity->actor.id, 100,
                        game->world != NULL, game->world ? game->world->actor.id : (qa_actor_id){0}, NULL);
    game->remove(game->ctx, entity);
    return;
  }
  entity->speed = fmax(0, entity->speed - 10);
  qa_actor_id enemy;
  bool has_enemy = qa_q1am_actor_ref(entity, "enemy", &enemy);
  qa_q1am_body target_body;
  qa_vec3 target = has_enemy && game->body_read(game->ctx, enemy, &target_body) ? target_body.origin : QA_Q1AM_ZERO;
  qa_vec3 direction = qa_q1am_normalize(qa_sub3(target, origin));
  qa_q1am_body_patch patch = {0};
  patch.has_velocity = true;
  patch.velocity = qa_scale3(qa_add3(qa_scale3(direction, 0.3),
                                     qa_scale3(qa_q1am_normalize(game->body(game->ctx, entity).velocity), 0.7)),
                             entity->speed);
  game->set_body(game->ctx, entity, &patch);
  game->schedule(game->ctx, entity, 0.1, game->named_action(game->ctx, entity, "mg3:szombie:homing_flame_think"));
}
void qa_q1am_register_shub_zombie(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  qa_q1am_boss_create create = {context, create_szombie, destroy_szombie};
  qa_q1am_register_boss_controllers(context, "mg3:szombie", "monster_szombie", create);
  game->register_spawn(game->ctx, "info_szombie_spawn", spawn_point_handler, NULL);
  qa_q1am_handlers handlers = {0};
  handlers.touch = grenade_touch_handler;
  game->named_register(game->ctx, "mg3:szombie:grenade_touch", handlers);
  handlers.touch = NULL;
  handlers.ctx = context;
  handlers.action = homing_flame_think;
  game->named_register(game->ctx, "mg3:szombie:homing_flame_think", handlers);
}
