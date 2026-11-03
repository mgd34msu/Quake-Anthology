/* Port of src/content/q1/addons/monsters/bosses/szombie.ts.
 * quakec_mg3/monsters/mg3_shub_zombie.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/szombie.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/frames/szombie.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/content/q1/base/projectiles.h"
#include "../support.h"

#define PREFIX "mg3:szombie"
#define FLAGS QA_Q1AM_FLAGS

static const char *const szombie_classnames[] = {"monster_szombie"};
static const qa_q1_base_monster_species szombie_spec = {
  .species = "zombie", .classnames = szombie_classnames, .classname_count = 1, .model = "zombie", .head = NULL,
  .health = 60, .gib_health = -INFINITY, .gibs = NULL, .gib_count = 0, .bounds = {{-16, -16, -24}, {16, 16, 32}},
  .stand = "szombie_stand1", .walk = "szombie_walk1", .run = "szombie_run1", .sight = "", .missile = NULL,
  .melee = true, .movement = "walk",
};

static void szombie_spawn(qa_q1_base_monster *monster);
static void szombie_play(qa_q1_base_monster *monster, const char *name);
static void szombie_melee_attack(qa_q1_base_monster *monster);
static void szombie_die(qa_q1_base_monster *monster, const qa_actor_id *attacker);
static void szombie_destroy(qa_q1_base_monster *monster) { free(monster); }
static const qa_q1_base_monster_ops szombie_ops = {
  QA_Q1AM_MG3_MONSTER_OPS,
  .spawn = szombie_spawn, .play = szombie_play, .melee_attack = szombie_melee_attack, .die = szombie_die,
  .destroy = szombie_destroy,
};
bool qa_q1am_is_shub_zombie(const qa_q1_base_monster *monster) { return monster->ops == &szombie_ops; }
static qa_q1am_shub_zombie *szombie_of(qa_q1_base_monster *monster) { return (qa_q1am_shub_zombie *)monster; }

static void szombie_frame_action(void *ctx, qa_q1_base_monster *monster) {
  if (!qa_q1am_is_shub_zombie(monster)) qa_range_error("Shub zombie callback received another controller");
  qa_q1am_shub_zombie_action(szombie_of(monster), ctx);
}
static void szombie_spawn(qa_q1_base_monster *monster) {
  qa_q1ad_context *context = szombie_of(monster)->context;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 60);
  entity->max_health = 60;
  qa_q1ad_set_text(&entity->classname, "monster_szombie");
  monster->in_pain = 2;
  entity->solid = "none";
  entity->movement = "step";
  qa_q1ad_set_text(&entity->model, "progs/zombie.mdl");
  qa_q1_set_bounds(game, entity, szombie_spec.bounds);
  qa_q1_actor *owner = qa_q1_get_entity(game, qa_q1am_opt(entity->has_owner, &entity->owner));
  qa_q1_set_origin(game, entity, owner == NULL ? QA_Q1_ZERO : qa_q1_get_body(game, owner).origin);
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  qa_q1ad_set_vector(context, entity, "view_ofs", (qa_vec3){0, 0, 25});
  qa_combat_trait_changes traits;
  memset(&traits, 0, sizeof traits);
  traits.has_team = true;
  traits.team = "q1:monsters";
  qa_gameplay_authority_set_traits(game->host.combat, entity->actor, traits);
  entity->movement_flags = FLAGS(entity->movement_flags) | 32;
  if (entity->yaw_speed == 0 || isnan(entity->yaw_speed)) entity->yaw_speed = 20; /* TS ||= */
  qa_q1ad_set_number(context, entity, "combat_style", 2);
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, PREFIX ":monster_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, PREFIX ":monster_die");
  game->total_monsters++;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "monster-total";
  event.total = game->total_monsters;
  qa_q1_emit(game, event);
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  qa_actor_id first;
  if (count > 0) first = players[0];
  qa_q1_base_monster_set_enemy(monster, count > 0 ? &first : NULL);
  entity->frame = 174;
  qa_q1am_copy_name(monster->current_frame, sizeof monster->current_frame, "szombie_paine10");
  qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "szombie_paine11");
  qa_q1_base_monster_delay(monster, 2);
}
static void szombie_play(qa_q1_base_monster *monster, const char *name) {
  if (strncmp(name, "zombie_", 7) == 0) {
    char renamed[80];
    (void)snprintf(renamed, sizeof renamed, "szombie_%s", name + 7);
    qa_q1_base_monster_play_super(monster, renamed);
    return;
  }
  qa_q1_base_monster_play_super(monster, name);
}
static void szombie_melee_attack(qa_q1_base_monster *monster) {
  const double r = qa_q1am_random(monster->game);
  qa_q1_base_monster_play(monster, r < 0.3 ? "szombie_atta1" : r < 0.6 ? "szombie_attb1" : "szombie_attc1");
}
static void szombie_die(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  qa_vec3 origin;
  const char * model;
  double damage;
  if (monster->counted_death) return;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_set_enemy(monster, attacker);
  qa_q1_actor_set_damageable(entity, false);
  memset(&entity->touch, 0, sizeof entity->touch);
  qa_q1_base_monster_count_kill(monster);
  qa_q1_sound_defaults(game, entity->actor.id, "zombie/z_gib.wav");
  static const char *const models[] = {"gib1", "gib2", "gib3"};
  for (size_t i = 0; i < 3; i++)
    (void)(origin = qa_q1_base_monster_origin(monster), model = models[i], damage = qa_q1_health(game, entity->actor.id), qa_q1_throw_gib(game, origin, model, damage));
  qa_q1_remove(game, entity);
}
/* TS private grenade(offset). */
static void grenade(qa_q1am_shub_zombie *zombie, qa_vec3 offset) {
  qa_q1_base_monster *monster = &zombie->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_sound(game, entity->actor.id, "zombie/z_shot1.wav", "weapon", 1, 1);
  qa_q1_actor *missile = qa_q1_create(game, "szombie_grenade", NULL, NULL);
  qa_q1ad_set_text(&missile->classname, "");
  missile->has_owner = true;
  missile->owner = entity->actor.id;
  missile->movement = "bounce";
  missile->solid = "bbox";
  const qa_q1_basis basis = game->basis;
  const qa_vec3 origin = qa_add3(qa_add3(qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(basis.forward, offset.x)),
                                         qa_scale3(basis.right, offset.y)), qa_scale3(basis.up, offset.z - 24));
  (void)qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_vec3 target = QA_Q1_ZERO;
  (void)qa_q1_base_monster_target(monster, &target);
  qa_vec3 velocity = qa_scale3(qa_q1_normalize(qa_sub3(target, origin)), 600);
  velocity.z = 200;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_velocity = true;
  patch.velocity = velocity;
  patch.has_bounds = true;
  patch.bounds = QA_Q1_POINT;
  qa_q1_set_body(game, missile, &patch);
  missile->angular_velocity = (qa_vec3){3000, 1000, 2000};
  qa_q1ad_set_text(&missile->model, "progs/zom_gib.mdl");
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, PREFIX ":grenade_touch");
  qa_q1_schedule(game, missile, 2.5, qa_q1_callback_registry_action(game->named, missile, "SUB_Remove"));
  qa_q1_link(game, missile);
}
static void idle(qa_q1_services *game, qa_q1_actor *entity, const char *path) {
  qa_q1_sound(game, entity->actor.id, path, "voice", 2, 1);
}
void qa_q1am_shub_zombie_action(qa_q1am_shub_zombie *zombie, const char *name) {
  qa_q1_base_monster *monster = &zombie->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  static const struct { const char *name; qa_q1_monster_ai mode; double distance; } moves[] = {
    {"szombie:szombie_stand1", QA_Q1_AI_STAND, 0}, {"szombie:szombie_walk1", QA_Q1_AI_WALK, 0},
    {"szombie:szombie_walk2", QA_Q1_AI_WALK, 2}, {"szombie:szombie_walk3", QA_Q1_AI_WALK, 3},
    {"szombie:szombie_walk5", QA_Q1_AI_WALK, 1}, {"szombie:szombie_run2", QA_Q1_AI_RUN, 1},
    {"szombie:szombie_run3", QA_Q1_AI_RUN, 0}, {"szombie:szombie_run5", QA_Q1_AI_RUN, 2},
    {"szombie:szombie_run6", QA_Q1_AI_RUN, 3}, {"szombie:szombie_run7", QA_Q1_AI_RUN, 4},
    {"szombie:szombie_run15", QA_Q1_AI_RUN, 6}, {"szombie:szombie_run16", QA_Q1_AI_RUN, 7},
    {"szombie:szombie_paina2", QA_Q1_AI_PAINFORWARD, 3}, {"szombie:szombie_paina3", QA_Q1_AI_PAINFORWARD, 1},
    {"szombie:szombie_paina4", QA_Q1_AI_PAIN, 1}, {"szombie:szombie_paina5", QA_Q1_AI_PAIN, 3},
    {"szombie:szombie_painb2", QA_Q1_AI_PAIN, 2}, {"szombie:szombie_painb3", QA_Q1_AI_PAIN, 8},
    {"szombie:szombie_painb4", QA_Q1_AI_PAIN, 6}, {"szombie:szombie_paine3", QA_Q1_AI_PAIN, 5},
    {"szombie:szombie_paine25", QA_Q1_AI_PAINFORWARD, 5},
  };
  for (size_t i = 0; i < sizeof moves / sizeof moves[0]; i++)
    if (strcmp(name, moves[i].name) == 0) {
      qa_q1_base_monster_ai(monster, moves[i].mode, moves[i].distance);
      return;
    }
  if (strcmp(name, "szombie:szombie_walk19") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_WALK, 0);
    if (qa_q1am_random(game) < 0.2) idle(game, entity, "zombie/z_idle.wav");
  } else if (strcmp(name, "szombie:szombie_run1") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_RUN, 1);
    monster->in_pain = 0;
  } else if (strcmp(name, "szombie:szombie_run18") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_RUN, 8);
    if (qa_q1am_random(game) < 0.2) idle(game, entity, "zombie/z_idle.wav");
    if (qa_q1am_random(game) > 0.8) idle(game, entity, "zombie/z_idle1.wav");
  } else if (strcmp(name, "szombie:szombie_atta1") == 0) {
    qa_q1_base_monster_face(monster);
  } else if (strcmp(name, "szombie:szombie_atta13") == 0) {
    qa_q1_base_monster_face(monster);
    grenade(zombie, (qa_vec3){-10, -22, 30});
  } else if (strcmp(name, "szombie:szombie_attb14") == 0) {
    qa_q1_base_monster_face(monster);
    grenade(zombie, (qa_vec3){-10, -24, 29});
  } else if (strcmp(name, "szombie:szombie_attc12") == 0) {
    qa_q1_base_monster_face(monster);
    grenade(zombie, (qa_vec3){-12, -19, 29});
  } else if (strcmp(name, "szombie:szombie_paina1") == 0) {
    qa_q1_sound_defaults(game, entity->actor.id, "zombie/z_pain.wav");
  } else if (strcmp(name, "szombie:szombie_painb1") == 0) {
    qa_q1_sound_defaults(game, entity->actor.id, "zombie/z_pain1.wav");
  } else if (strcmp(name, "szombie:szombie_painb9") == 0) {
    qa_q1_sound(game, entity->actor.id, "zombie/z_fall.wav", "body", 1, 1);
  } else if (strcmp(name, "szombie:szombie_paine1") == 0) {
    qa_q1_sound_defaults(game, entity->actor.id, "zombie/z_pain.wav");
    qa_gameplay_authority_set_health(game->host.combat, entity->actor, 60);
  } else if (strcmp(name, "szombie:szombie_paine10") == 0) {
    qa_q1_sound(game, entity->actor.id, "zombie/z_fall.wav", "body", 1, 1);
    entity->solid = "none";
    qa_q1_link(game, entity);
  } else if (strcmp(name, "szombie:szombie_paine11") == 0) {
    qa_q1_base_monster_delay(monster, entity->next_think - game->time + 5);
    qa_gameplay_authority_set_health(game->host.combat, entity->actor, 60);
  } else if (strcmp(name, "szombie:szombie_paine12") == 0) {
    qa_gameplay_authority_set_health(game->host.combat, entity->actor, 60);
    idle(game, entity, "zombie/z_idle.wav");
    entity->solid = "slidebox";
    if (!qa_q1am_walk_move(game, entity->actor, 0, 0)) {
      qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "szombie_paine11");
      entity->solid = "none";
    }
    qa_q1_link(game, entity);
  } else {
    qa_range_error("Unknown Shub zombie action %s", name);
  }
}
qa_q1_actor *qa_q1am_spawn_shub_zombie(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  size_t zombies = 0, spawn_count = 0, count = 0;
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, "monster_szombie") == 0) zombies++;
  if (zombies > 32) return NULL;
  qa_q1_actor **spawns = game->entity_count == 0 ? NULL : malloc(game->entity_count * sizeof *spawns);
  if (game->entity_count != 0 && spawns == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, "info_szombie_spawn") == 0) spawns[spawn_count++] = game->entities[i];
  for (size_t i = 0; i < spawn_count; i++)
    if (spawns[i]->wait != 0 && spawns[i]->wait < game->time) count++;
  if (count == 0) { free(spawns); return NULL; }
  /* Native selection counts eligible points, then indexes the original list without filtering. */
  const double index = floor(qa_q1am_random(game) * ((double)count - 1) + 0.5);
  if (!(index >= 0 && index < (double)spawn_count)) { free(spawns); qa_range_error("Missing source zombie spawn"); }
  qa_q1_actor *point = spawns[(size_t)index];
  free(spawns);
  point->wait = game->time + 8;
  qa_q1_actor *zombie = qa_q1_create(game, "monster_szombie", NULL, NULL);
  zombie->has_owner = true;
  zombie->owner = point->actor.id;
  qa_q1_spawn_entity(game, zombie);
  return zombie;
}
bool qa_q1am_shub_zombie_init(qa_q1am_shub_zombie *zombie, qa_q1ad_context *context, qa_q1_actor *entity) {
  memset(zombie, 0, sizeof *zombie);
  zombie->context = context;
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = PREFIX;
  source.frames = &qa_q1am_szombie_frames;
  source.actions = qa_q1am_frame_actions(&qa_q1am_szombie_frames, szombie_frame_action, &source.action_count);
  return qa_q1am_mg3_monster_init(&zombie->base, context->game, entity, &szombie_spec,
                                  qa_q1_base_as_monster_services(context->base), &source, &szombie_ops, NULL);
}
static qa_q1_base_monster *create_szombie(void *ctx, qa_q1_actor *entity) {
  qa_q1am_shub_zombie *zombie = malloc(sizeof *zombie);
  if (zombie == NULL) return NULL;
  if (!qa_q1am_shub_zombie_init(zombie, ctx, entity)) { free(zombie); return NULL; }
  return &zombie->base.base;
}
static void spawn_point(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; (void)game; /* TS (_game, entity) */
  entity->wait = -1;
}
static void grenade_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                          const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (_game, entity, other) */
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  const char *classname = game->host.classname(game->host.ctx, other);
  if (strcmp(classname, "monster_oldone_new") == 0 || strcmp(classname, "oldnew_child") == 0) {
    qa_q1_remove(game, entity);
    return;
  }
  if (qa_q1am_can_take_damage(game, other)) {
    (void)qa_q1_damage(game, other, entity->actor.id, qa_q1am_opt(entity->has_owner, &entity->owner), 10, NULL, NULL, NULL, NULL);
    qa_q1_sound(game, entity->actor.id, "zombie/z_hit.wav", "weapon", 1, 1);
    qa_q1_remove(game, entity);
    return;
  }
  qa_q1_sound(game, entity->actor.id, "zombie/z_miss.wav", "weapon", 1, 1);
  qa_q1am_set_velocity_body(game, entity, QA_Q1_ZERO);
  entity->angular_velocity = QA_Q1_ZERO;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "SUB_Remove");
}
void qa_q1am_register_shub_zombie(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  (void)qa_q1am_register_boss_controllers(context, PREFIX, "monster_szombie", create_szombie, context);
  qa_q1_register_spawn(game, "info_szombie_spawn", spawn_point, context);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = context;
  handlers.touch = grenade_touch;
  qa_q1_callback_registry_register(game->named, PREFIX ":grenade_touch", handlers);
}
