/* Port of src/content/q1/missionpacks/monsters/morph.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/morph.h"

#include <string.h>

#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/overlord.h"
#include "qa/content/q1/missionpacks/monsters/tables/morph.h"
#include "monsters_internal.h"

static void setup(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_actor *owner = qa_q1_get_entity(game, (entity->has_owner ? &entity->owner : NULL));
  entity->solid = "slidebox";
  entity->movement = "step";
  qa_q1_actor_set_damageable(entity, false);
  entity->movement_flags = qa_js_bit_or(entity->movement_flags, 32);
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  if (entity->yaw_speed == 0) entity->yaw_speed = 20;
  (void)qa_q1_actor_field_set(entity, "view_ofs", "0 0 25");
  const qa_q1_monster_frame *wake_frame = qa_q1_monster_frame_table_get(&qa_q1mpm_tables_morph_frames, "morph_wake1");
  entity->frame = wake_frame == NULL ? 0 : wake_frame->frame;
  qa_q1_set_bounds(game, entity, qa_q1mpm_hull_bounds);
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, "rogue:monster_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, "rogue:monster_die");
  entity->max_health = owner == NULL ? 2000 : 200;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, entity->max_health);
  if (owner != NULL) { entity->effects = 0; entity->spawnflags = owner->spawnflags; }
  else entity->effects = qa_js_bit_or(entity->effects, 8);
  entity->skin = 2;
  qa_q1_link(game, entity);
}
static void wake(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1mpm_is_spawn_point_empty(monster->base.game, monster->base.entity)) {
    setup(monster);
    q1mpm_set_next_frame(&monster->base, "morph_wake1");
  } else {
    q1mpm_set_next_frame(&monster->base, "morph_wake");
  }
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void stab(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (!monster->base.state->has_enemy || !qa_q1_can_damage(game, monster->base.state->enemy, entity->actor.id))
    return;
  qa_q1_base_monster_face(&monster->base);
  qa_vec3 target;
  if (!qa_q1mpm_eye(game, monster->base.state->enemy, &target)) return;
  qa_vec3 origin0 = qa_q1_base_monster_origin(&monster->base);
  qa_vec3 delta = qa_q1_vsub(target, origin0);
  double distance = qa_q1_length(delta);
  qa_vec3 direction = qa_q1_normalize(delta);
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  if (distance <= 90) {
    qa_q1_sound(game, entity->actor.id, "enforcer/enfstop.wav", "weapon", 3, 1);
    (void)qa_q1_damage(game, monster->base.state->enemy, entity->actor.id, &entity->actor.id, qa_q1_host_random(game) * 10 + 20, NULL, NULL, NULL, NULL);
    qa_vec3 monster_target;
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "particles";
    event.has_origin = true;
    event.origin = qa_q1_base_monster_target(&monster->base, &monster_target) ? monster_target : target;
    event.direction = qa_q1_vscale(basis.forward, 15);
    event.color = 73;
    event.count = 28;
    qa_q1_emit(game, event);
  } else {
    entity->effects = qa_js_bit_or(entity->effects, 2);
    qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(qa_q1_vadd(origin0, qa_q1_vscale(basis.forward, 80)),
                                                qa_q1_vscale(basis.right, 4)),
                                   (qa_vec3){0, 0, 4});
    (void)qa_q1_launch_laser(game, &entity->actor.id, origin, direction);
    (void)qa_q1_launch_laser(game, &entity->actor.id, origin,
                             qa_q1_vadd(direction, qa_q1_vscale(game->basis.right, distance != 0 ? 0.04 : 0.1)));
    (void)qa_q1_launch_laser(game, &entity->actor.id, origin,
                             qa_q1_vsub(direction, qa_q1_vscale(game->basis.right, distance != 0 ? 0.04 : 0.1)));
  }
}
static void smack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (!monster->base.state->has_enemy || !qa_q1_can_damage(game, monster->base.state->enemy, entity->actor.id))
    return;
  qa_q1_base_monster_face(&monster->base);
  if (qa_q1_base_monster_distance(&monster->base) > 100) return;
  (void)qa_q1_damage(game, monster->base.state->enemy, entity->actor.id, &entity->actor.id, qa_q1_host_random(game) * 10 + 10, NULL, NULL, NULL, NULL);
  const qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_owned_actor owner;
  const bool has_owner = qa_session_actor_registry_resolve_owned(game->host.actors, monster->base.state->enemy, &owner);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, monster->base.state->enemy);
  if (has_owner && body.has_value) {
    qa_body_state next = body.value; /* TS { ...body, velocity } */
    next.velocity = qa_q1_vadd(qa_q1_vscale(basis.forward, 100), (qa_vec3){0, 0, 100});
    qa_shared_body_table_write(game->host.bodies, owner, next);
  }
}
static void fire_impl(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_base_monster_face(&monster->base);
  entity->effects = qa_js_bit_or(entity->effects, 2);
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) return;
  qa_vec3 origin0 = qa_q1_base_monster_origin(&monster->base);
  qa_vec3 origin = qa_q1_vadd(qa_q1_vadd(qa_q1_vadd(origin0, qa_q1_vscale(basis.forward, 30)),
                                              qa_q1_vscale(basis.right, 8.5)),
                                 (qa_vec3){0, 0, 16});
  qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(target, origin0));
  (void)qa_q1_launch_laser(game, &entity->actor.id, origin, direction);
  const double spread = qa_q1_base_monster_distance(&monster->base) > 400 ? 0.04 : 0.1;
  (void)qa_q1_launch_laser(game, &entity->actor.id, origin, qa_q1_vadd(direction, qa_q1_vscale(game->basis.right, spread)));
  (void)qa_q1_launch_laser(game, &entity->actor.id, origin, qa_q1_vsub(direction, qa_q1_vscale(game->basis.right, spread)));
}
static void child_impl(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1mpm_runtime *runtime = monster->runtime;
  if (entity->has_owner || qa_q1_actor_number(entity, "childrenSpawned") > 1 + qa_q1_option_skill(game))
    return;
  qa_q1_actor *destination = qa_q1mpm_overlord_destination(game);
  if (destination == NULL) return;
  qa_q1_actor *next = qa_q1_create(game, "monster_morph", NULL, NULL);
  q1mpm_set_text(&next->model, entity->model);
  next->has_owner = true; next->owner = entity->actor.id;
  next->mangle = destination->mangle;
  qa_q1_body_patch patch = {0};
  patch.has_angles = true;
  patch.angles = destination->mangle;
  qa_q1_set_body(game, next, &patch);
  qa_q1mpm_mission_monster *controller = qa_q1mpm_mission_monster_create(game, next, monster->definition, runtime);
  qa_q1mpm_runtime_set_monster(runtime, next->actor, controller);
  qa_q1_base_monster_set_enemy(&controller->base, qa_q1_base_monster_enemy(&monster->base));
  q1mpm_set_text(&controller->base.state->path, monster->base.state->path);
  qa_actor_id movetarget, goalentity;
  (void)qa_q1_actor_reference_set(next, "movetarget", q1mpm_reference(entity, "movetarget", &movetarget));
  (void)qa_q1_actor_reference_set(next, "goalentity", q1mpm_reference(entity, "goalentity", &goalentity));
  setup(controller);
  qa_q1_set_origin(game, next, qa_q1_get_body(game, destination).origin);
  (void)qa_q1mpm_drop_to_floor(monster);
  q1mpm_set_next_frame(&controller->base, "morph_wake1");
  qa_q1_base_monster_delay(&controller->base, 0.3);
}
static void teleport_child(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; child_impl(monster); }
static void die9(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; monster->base.entity->skin++; }
static void die21(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_remove(monster->base.game, monster->base.entity);
}
static void wake1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_sound_defaults(game, entity->actor.id, "guard/see1.wav");
  qa_q1_actor *owner = qa_q1_get_entity(game, (entity->has_owner ? &entity->owner : NULL));
  if (owner != NULL) {
    game->total_monsters++;
    q1mpm_set_number_text(owner, "childrenSpawned", qa_q1_actor_number(owner, "childrenSpawned") + 1);
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "monster-total";
    event.total = game->total_monsters;
    qa_q1_emit(game, event);
  }
}
static void wake15(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; monster->base.entity->skin = 1; }
static void wake31(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->solid = "slidebox";
  qa_q1_actor_set_damageable(monster->base.entity, true);
  monster->base.entity->aimed_damage = true;
  monster->base.entity->skin--;
  if (monster->base.entity->has_owner) {
    q1mpm_set_next_frame(&monster->base, "morph_run1");
    qa_q1_base_monster_delay(&monster->base, 0.1);
  }
  qa_q1_link(monster->base.game, monster->base.entity);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_js_bit_and(entity->spawnflags, 2) != 0) q1mpm_set_text(&entity->model, "progs/morph_az.mdl");
  else if (qa_js_bit_and(entity->spawnflags, 4) != 0) q1mpm_set_text(&entity->model, "progs/morph_eg.mdl");
  else if (qa_js_bit_and(entity->spawnflags, 8) != 0) q1mpm_set_text(&entity->model, "progs/morph_gr.mdl");
  else qa_range_error("monster_morph: no skin selection!");
  game->total_monsters++;
  if (entity->targetname[0] != '\0') {
    entity->use = qa_q1_callback_registry_use(game->named, entity, "rogue:monster_use");
    return;
  }
  wake(NULL, monster);
}
static void use_hook(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *activator) {
  (void)ctx; (void)activator;
  q1mpm_set_next_frame(&monster->base, "morph_wake");
  qa_q1_base_monster_delay(&monster->base, monster->base.entity->delay != 0 ? monster->base.entity->delay : 0.1);
}
static void melee_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  double r = qa_q1_host_random(monster->base.game);
  qa_q1_base_monster_play(&monster->base, r < 0.5 ? "morph_bigattack01" : r < 0.75 ? "morph_attack01" : "morph_knockback01");
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage;
  qa_q1_services *game = monster->base.game;
  if (qa_q1_option_skill(game) == 3) {
    if (qa_q1_host_random(game) > 0.5) child_impl(monster);
    return;
  }
  if (monster->base.state->pain_finished > game->time || qa_q1_host_random(game) > 0.25) return;
  double r = qa_q1_host_random(game);
  monster->base.state->pain_finished = game->time + 2;
  qa_q1_sound_defaults(game, monster->base.entity->actor.id, "guard/pain1.wav");
  q1mpm_set_next_frame(&monster->base, r > 0.6 ? "morph_painB1" : "morph_painA1");
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; (void)attacker;
  qa_q1_sound_defaults(monster->base.game, monster->base.entity->actor.id, "guard/death.wav");
  monster->base.entity->solid = "none";
  q1mpm_set_next_frame(&monster->base, "morph_die1");
  qa_q1_link(monster->base.game, monster->base.entity);
  qa_q1_base_monster_delay(&monster->base, 0.1);
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
  out->spec = (qa_q1_base_monster_species){
      "morph", NULL, classnames, 1, "morph_az", NULL, 2000, -INFINITY, NULL, 0,
      {{-16, -16, -24}, {16, 16, 32}}, "morph_stand1", "morph_walk1", "morph_run1",
      "", "morph_fire1", true, "walk"};
  out->frames = (qa_q1_monster_frame_table){qa_q1mpm_tables_morph_entries, QA_Q1MPM_TABLES_MORPH_COUNT};
  out->actions = actions;
  out->action_count = sizeof(actions) / sizeof(actions[0]);
  out->spawn = (qa_q1mpm_action){spawn, NULL};
  out->pain = pain;
  out->die = die;
  out->melee = (qa_q1mpm_action){melee_action, NULL};
  out->use = use_hook;
}
