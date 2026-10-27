/* Port of src/content/q1/missionpacks/monsters/morph.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/morph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/overlord.h"
#include "qa/content/q1/missionpacks/monsters/tables/morph.h"
#include "monsters_internal.h"

static void setup(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_actor *owner = game->entity(game, entity->owner);
  entity->solid = "slidebox";
  entity->movement = "step";
  qa_q1mpm_actor_set_damageable(game, entity, false);
  entity->movement_flags |= 32;
  entity->ideal_yaw = game->body(game, entity).angles.y;
  if (entity->yaw_speed == 0) entity->yaw_speed = 20;
  qa_q1mpm_fields_set(entity, "view_ofs", "0 0 25");
  const qa_q1mpm_frame_entry *wake_frame =
      qa_q1mpm_frame_table_get(&qa_q1mpm_tables_morph_frames, "morph_wake1");
  entity->frame = wake_frame == NULL ? 0 : wake_frame->frame;
  game->set_bounds(game, entity, qa_q1mpm_hull_bounds);
  entity->pain = game->named.pain(game->named.ctx, entity, "rogue:monster_pain");
  entity->die = game->named.die(game->named.ctx, entity, "rogue:monster_die");
  entity->max_health = owner == NULL ? 2000 : 200;
  game->host->combat_set_health(game->host->ctx, entity->actor, entity->max_health);
  if (owner != NULL) { entity->effects = 0; entity->spawnflags = owner->spawnflags; }
  else entity->effects |= 8;
  entity->skin = 2;
  game->link(game, entity);
}
static void wake(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1mpm_is_spawn_point_empty(monster->game, monster->entity)) {
    setup(monster);
    qa_q1mpm_monster_set_next_frame(monster, "morph_wake1");
  } else {
    qa_q1mpm_monster_set_next_frame(monster, "morph_wake");
  }
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void stab(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (!monster->state.enemy.has || !game->can_damage(game, monster->state.enemy.id, entity->actor.id))
    return;
  qa_q1mpm_monster_face(monster);
  qa_vec3 target;
  if (!qa_q1mpm_eye(game, monster->state.enemy.id, &target)) return;
  qa_vec3 origin0 = qa_q1mpm_monster_origin(monster);
  qa_vec3 delta = qa_q1mpm_vsub(target, origin0);
  double distance = qa_q1mpm_length(delta);
  qa_vec3 direction = qa_q1mpm_normalize(delta);
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  if (distance <= 90) {
    game->sound(game, entity, "enforcer/enfstop.wav", QA_Q1MPM_CH("weapon"), 3, 1);
    qa_q1mpm_game_damage(game, monster->state.enemy.id, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id),
                         game->host->random(game->host->ctx) * 10 + 20);
    qa_vec3 monster_target;
    qa_q1mpm_event event = {0};
    event.kind = "particles";
    event.has_origin = true;
    event.origin = qa_q1mpm_monster_target(monster, &monster_target) ? monster_target : target;
    event.direction = qa_q1mpm_vscale(basis.forward, 150);
    event.color = 73;
    event.count_value = 14;
    game->host->emit(game->host->ctx, &event);
  } else {
    entity->effects |= 2;
    qa_vec3 origin = qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_vadd(origin0, qa_q1mpm_vscale(basis.forward, 80)),
                                                qa_q1mpm_vscale(basis.right, 4)),
                                   QA_Q1MPM_V3(0, 0, 4));
    qa_q1mpm_ext_launch_laser(game, QA_Q1MPM_SOME(entity->actor.id), origin, direction);
    qa_q1mpm_ext_launch_laser(game, QA_Q1MPM_SOME(entity->actor.id), origin,
                              qa_q1mpm_vadd(direction, qa_q1mpm_vscale(game->basis.right, distance != 0 ? 0.04 : 0.1)));
    qa_q1mpm_ext_launch_laser(game, QA_Q1MPM_SOME(entity->actor.id), origin,
                              qa_q1mpm_vsub(direction, qa_q1mpm_vscale(game->basis.right, distance != 0 ? 0.04 : 0.1)));
  }
}
static void smack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (!monster->state.enemy.has || !game->can_damage(game, monster->state.enemy.id, entity->actor.id))
    return;
  qa_q1mpm_monster_face(monster);
  if (qa_q1mpm_monster_distance(monster) > 100) return;
  qa_q1mpm_game_damage(game, monster->state.enemy.id, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id),
                       game->host->random(game->host->ctx) * 10 + 10);
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_owned_actor owner;
  qa_q1mpm_body body;
  if (game->host->actors_resolve_owned(game->host->ctx, monster->state.enemy.id, &owner) &&
      game->host->bodies_read(game->host->ctx, monster->state.enemy.id, &body)) {
    body.velocity = qa_q1mpm_vadd(qa_q1mpm_vscale(basis.forward, 100), QA_Q1MPM_V3(0, 0, 100));
    game->host->bodies_write(game->host->ctx, owner, &body);
  }
}
static void fire_impl(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_monster_face(monster);
  entity->effects |= 2;
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_vec3 target;
  if (!qa_q1mpm_monster_target(monster, &target)) return;
  qa_vec3 origin0 = qa_q1mpm_monster_origin(monster);
  qa_vec3 origin = qa_q1mpm_vadd(qa_q1mpm_vadd(qa_q1mpm_vadd(origin0, qa_q1mpm_vscale(basis.forward, 30)),
                                              qa_q1mpm_vscale(basis.right, 8.5)),
                                 QA_Q1MPM_V3(0, 0, 16));
  qa_vec3 direction = qa_q1mpm_normalize(qa_q1mpm_vsub(target, origin0));
  qa_q1mpm_ext_launch_laser(game, QA_Q1MPM_SOME(entity->actor.id), origin, direction);
  double spread = qa_q1mpm_monster_distance(monster) > 400 ? 0.04 : 0.1;
  qa_q1mpm_ext_launch_laser(game, QA_Q1MPM_SOME(entity->actor.id), origin,
                            qa_q1mpm_vadd(direction, qa_q1mpm_vscale(game->basis.right, spread)));
  qa_q1mpm_ext_launch_laser(game, QA_Q1MPM_SOME(entity->actor.id), origin,
                            qa_q1mpm_vsub(direction, qa_q1mpm_vscale(game->basis.right, spread)));
}
static void child_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_runtime *runtime = monster->runtime;
  if (entity->owner.has || qa_q1mpm_actor_number(entity, "childrenSpawned") > 1 + game->options.skill)
    return;
  qa_q1mpm_actor *destination = qa_q1mpm_overlord_destination(game);
  if (destination == NULL) return;
  qa_q1mpm_actor *next = game->create(game, "monster_morph");
  snprintf(next->model, sizeof(next->model), "%s", entity->model);
  next->owner = QA_Q1MPM_SOME(entity->actor.id);
  next->mangle = destination->mangle;
  qa_q1mpm_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = destination->mangle;
  game->set_body(game, next, &patch);
  qa_q1mpm_mission_monster *controller = calloc(1, sizeof(*controller));
  if (controller == NULL) return;
  qa_q1mpm_mission_monster_init(controller, game, next, monster->definition, runtime);
  if (!qa_q1mpm_runtime_set_monster(runtime, next->actor, controller)) {
    qa_q1mpm_mission_monster_destroy(controller);
    free(controller);
    return;
  }
  controller->state.enemy = monster->state.enemy;
  snprintf(controller->state.path, sizeof(controller->state.path), "%s", monster->state.path);
  qa_q1mpm_references_set(next, "movetarget", qa_q1mpm_references_get(entity, "movetarget"));
  qa_q1mpm_references_set(next, "goalentity", qa_q1mpm_references_get(entity, "goalentity"));
  setup(controller);
  game->set_origin(game, next, game->body(game, destination).origin);
  qa_q1mpm_drop_to_floor(monster);
  qa_q1mpm_monster_set_next_frame(controller, "morph_wake1");
  qa_q1mpm_monster_delay(controller, 0.3);
}
static void teleport_child(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; child_impl(monster); }
static void die9(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; monster->entity->skin++; }
static void die21(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->game->remove(monster->game, monster->entity);
}
static void wake1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_game_sound(game, entity, "guard/see1.wav");
  qa_q1mpm_actor *owner = game->entity(game, entity->owner);
  if (owner != NULL) {
    game->total_monsters++;
    char buf[32];
    qa_q1mpm_format_number(buf, sizeof(buf), qa_q1mpm_actor_number(owner, "childrenSpawned") + 1);
    qa_q1mpm_fields_set(owner, "childrenSpawned", buf);
    qa_q1mpm_event event = {0};
    event.kind = "monster-total";
    event.total = game->total_monsters;
    game->host->emit(game->host->ctx, &event);
  }
}
static void wake15(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; monster->entity->skin = 1; }
static void wake31(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->solid = "slidebox";
  qa_q1mpm_actor_set_damageable(monster->game, monster->entity, true);
  monster->entity->aimed_damage = true;
  monster->entity->skin--;
  if (monster->entity->owner.has) {
    qa_q1mpm_monster_set_next_frame(monster, "morph_run1");
    qa_q1mpm_monster_delay(monster, 0.1);
  }
  monster->game->link(monster->game, monster->entity);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if ((entity->spawnflags & 2) != 0) snprintf(entity->model, sizeof(entity->model), "%s", "progs/morph_az.mdl");
  else if ((entity->spawnflags & 4) != 0) snprintf(entity->model, sizeof(entity->model), "%s", "progs/morph_eg.mdl");
  else if ((entity->spawnflags & 8) != 0) snprintf(entity->model, sizeof(entity->model), "%s", "progs/morph_gr.mdl");
  else qa_range_error("monster_morph: no skin selection!");
  game->total_monsters++;
  if (entity->targetname[0] != '\0') {
    entity->use = game->named.use(game->named.ctx, entity, "rogue:monster_use");
    return;
  }
  wake(NULL, monster);
}
static void use_hook(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor activator) {
  (void)ctx; (void)activator;
  qa_q1mpm_monster_set_next_frame(monster, "morph_wake");
  qa_q1mpm_monster_delay(monster, monster->entity->delay != 0 ? monster->entity->delay : 0.1);
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  double r = monster->game->host->random(monster->game->host->ctx);
  qa_q1mpm_monster_play(monster, r < 0.5 ? "morph_bigattack01" : r < 0.75 ? "morph_attack01" : "morph_knockback01");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  qa_q1mpm_game *game = monster->game;
  if (game->options.skill == 3) {
    if (game->host->random(game->host->ctx) > 0.5) child_impl(monster);
    return;
  }
  if (monster->state.pain_finished > game->time || game->host->random(game->host->ctx) > 0.25) return;
  double r = game->host->random(game->host->ctx);
  monster->state.pain_finished = game->time + 2;
  qa_q1mpm_game_sound(game, monster->entity, "guard/pain1.wav");
  qa_q1mpm_monster_set_next_frame(monster, r > 0.6 ? "morph_painB1" : "morph_painA1");
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx; (void)attacker;
  qa_q1mpm_game_sound(monster->game, monster->entity, "guard/death.wav");
  monster->entity->solid = "none";
  qa_q1mpm_monster_set_next_frame(monster, "morph_die1");
  monster->game->link(monster->game, monster->entity);
  qa_q1mpm_monster_delay(monster, 0.1);
}
static const char *const classnames[] = {"monster_morph"};
static const qa_q1mpm_named_action actions[] = {
    {"morph_stab2", {stab, NULL}},
    {"morph_smack", {smack, NULL}},
    {"morph_fire", {fire_impl, NULL}},
    {"morph_teleport", {teleport_child, NULL}},
    {"morph_wake", {wake, NULL}},
    {"morph:morph_die9", {die9, NULL}},
    {"morph:morph_die21", {die21, NULL}},
    {"morph:morph_wake1", {wake1, NULL}},
    {"morph:morph_wake15", {wake15, NULL}},
    {"morph:morph_wake31", {wake31, NULL}},
};
void qa_q1mpm_morph_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out) {
  (void)runtime;
  *out = (qa_q1mpm_pack_monster_definition){0};
  out->spec = (qa_q1mpm_species_spec){
      "morph", NULL, classnames, 1, "morph_az", NULL, 2000, -INFINITY, NULL, 0,
      {{-16, -16, -24}, {16, 16, 32}}, "morph_stand1", "morph_walk1", "morph_run1",
      "", "morph_fire1", true, "walk"};
  out->frames = (qa_q1mpm_frame_table){qa_q1mpm_tables_morph_entries, QA_Q1MPM_TABLES_MORPH_COUNT};
  out->actions = actions;
  out->action_count = sizeof(actions) / sizeof(actions[0]);
  out->spawn = (qa_q1mpm_action){spawn, NULL};
  out->pain = pain;
  out->die = die;
  out->melee = (qa_q1mpm_action){melee_action, NULL};
  out->use = use_hook;
}
