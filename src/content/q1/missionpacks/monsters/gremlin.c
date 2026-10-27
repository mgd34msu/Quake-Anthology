/* Port of src/content/q1/missionpacks/monsters/gremlin.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/gremlin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/missionpacks/monsters/gremlin_ai.h"
#include "qa/content/q1/missionpacks/monsters/gremlin_weapons.h"
#include "qa/content/q1/missionpacks/monsters/helpers.h"
#include "qa/content/q1/missionpacks/monsters/tables/hipgrem.h"
#include "monsters_internal.h"

static void gib_impl(qa_q1mpm_mission_monster *monster, double damage) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_game_sound(game, entity, "player/udeath.wav");
  qa_q1mpm_ext_throw_head_damage(game, entity, "h_grem", damage);
  for (int i = 0; i < 3; i++)
    qa_q1mpm_ext_throw_gib(game, qa_q1mpm_monster_origin(monster), "gib1", damage);
}
static void resume(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (monster->state.old_enemy.has && monster->game->health(monster->game, monster->state.old_enemy.id) > 0) {
    monster->state.enemy = monster->state.old_enemy;
    qa_q1mpm_references_set(monster->entity, "goalentity", monster->state.enemy);
    qa_q1mpm_monster_set_next_frame(monster, qa_q1mpm_monster_spec(monster)->run);
    monster->state.attack_finished = monster->game->time + 1;
    qa_q1mpm_monster_delay(monster, 0.1);
    return;
  }
  qa_q1mpm_monster_play(monster, monster->state.path[0] == '\0' ? "gremlin_stand1" : "gremlin_walk1");
}
static void melee_side(qa_q1mpm_mission_monster *monster, double side) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_monster_face(monster);
  if (!monster->state.enemy.has || qa_q1mpm_monster_distance(monster) > 100 ||
      !game->can_damage(game, monster->state.enemy.id, entity->actor.id))
    return;
  game->sound(game, entity, "grem/attack.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  qa_q1mpm_game_damage(game, monster->state.enemy.id, entity->actor.id, QA_Q1MPM_SOME(entity->actor.id),
                       10 + 5 * game->host->random(game->host->ctx));
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_q1mpm_ext_spawn_meat_spray(game, entity,
                                qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), qa_q1mpm_vscale(basis.forward, 16)),
                                qa_q1mpm_vscale(basis.right, side));
}
static bool clear_trace(qa_q1mpm_mission_monster *monster, qa_vec3 end) {
  qa_q1mpm_trace_request request = {qa_q1mpm_monster_origin(monster), end, QA_Q1MPM_POINT,
                                    QA_Q1MPM_SOME(monster->entity->actor.id), true, false};
  return monster->game->host->trace(monster->game->host->ctx, &request).fraction == 1;
}
static void split(qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_runtime *runtime = monster->runtime;
  if (runtime->spawned_gremlins >= runtime->authored_gremlins * 2) return;
  qa_vec3 angles = game->body(game, entity).angles;
  qa_vec3 position = qa_q1mpm_monster_origin(monster);
  bool found = false;
  for (int count = 0; count < 10; count++) {
    position = qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster),
                             qa_q1mpm_vscale(game->make_vectors(game, angles).forward, 80));
    bool proceed = true;
    qa_actor_id actors[256];
    size_t actor_count = qa_q1mpm_radius_actors(game, position, 35, actors, 256);
    for (size_t i = 0; i < actor_count; i++) {
      if (game->health(game, actors[i]) > 0 &&
          (((game->entity(game, QA_Q1MPM_SOME(actors[i])) == NULL
                 ? 0
                 : game->entity(game, QA_Q1MPM_SOME(actors[i]))->movement_flags) & 32) != 0 ||
           game->is_player(game, QA_Q1MPM_SOME(actors[i]))))
        proceed = false;
    }
    if (clear_trace(monster, position) && proceed && clear_trace(monster, qa_q1mpm_vsub(position, QA_Q1MPM_V3(40, 40, 0))) &&
        clear_trace(monster, qa_q1mpm_vadd(position, QA_Q1MPM_V3(40, 40, 0))) &&
        clear_trace(monster, qa_q1mpm_vadd(position, QA_Q1MPM_V3(0, 0, 64))) &&
        !clear_trace(monster, qa_q1mpm_vsub(position, QA_Q1MPM_V3(0, 0, 64)))) {
      found = true;
      break;
    }
    angles.y += 36;
  }
  if (!found) return;
  runtime->spawned_gremlins++;
  qa_q1mpm_actor *child = game->clone_entity(game, entity);
  qa_q1mpm_mission_monster *controller = qa_q1mpm_runtime_require(runtime, child);
  child->solid = "slidebox";
  child->movement = "step";
  snprintf(child->model, sizeof(child->model), "%s", "progs/grem.mdl");
  game->set_bounds(game, child, qa_q1mpm_hull_bounds);
  double health = fmax(100, game->health(game, entity->actor.id));
  game->host->combat_set_health(game->host->ctx, entity->actor, health / 2);
  game->host->combat_set_health(game->host->ctx, child->actor, health / 2);
  qa_q1mpm_number(controller, "stoleweapon", 0);
  qa_q1mpm_inventory_entry entries[64];
  size_t entry_count = game->host->inventory_entries(game->host->ctx, child->actor.id, entries, 64);
  for (size_t i = 0; i < entry_count; i++) {
    if (strncmp(entries[i].item, "q1:weapon/", 10) == 0) {
      entries[i].count = 0;
      game->host->inventory_configure(game->host->ctx, child->actor, &entries[i]);
    }
  }
  game->total_monsters++;
  qa_q1mpm_event event = {0};
  event.kind = "monster-total";
  event.total = game->total_monsters;
  game->host->emit(game->host->ctx, &event);
  game->set_origin(game, child, position);
  qa_q1mpm_monster_play(controller, "gremlin_spawn1");
  controller->state.enemy = QA_Q1MPM_NONE;
  qa_q1mpm_number(controller, "gorging", 0);
}
static void gorge_damage(qa_q1mpm_mission_monster *monster, qa_actor_id target, double damage) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_actor *victim = game->entity(game, QA_Q1MPM_SOME(target));
  qa_owned_actor owned;
  qa_q1mpm_combat_read combat;
  if (!game->host->actors_resolve_owned(game->host->ctx, target, &owned) ||
      !game->host->combat_read(game->host->ctx, target, &combat) ||
      ((victim == NULL ? 0 : victim->movement_flags) & 64) != 0)
    return;
  qa_q1mpm_player_state *player = game->player(game, target);
  double invulnerable_until = player != NULL && qa_q1mpm_player_powerup(player, "invulnerability") != 0
                                  ? qa_q1mpm_player_powerup(player, "invulnerability")
                                  : victim == NULL ? 0 : qa_q1mpm_actor_number(victim, "invincible_finished");
  /* TS: player?.powerups.get("invulnerability") ?? victim?.number(...) ?? 0;
   * player powerup presence is approximated by nonzero expiry. */
  if (player != NULL) invulnerable_until = qa_q1mpm_player_powerup(player, "invulnerability");
  if (invulnerable_until >= game->time) {
    if (qa_q1mpm_actor_number(entity, "invincible_sound") < game->time) {
      game->sound_owned(game, owned, "items/protect3.wav", QA_Q1MPM_CH("item"), 1, 1);
      qa_q1mpm_number(monster, "invincible_sound", game->time + 2);
    }
    return;
  }
  double teamplay = game->options.has_teamplay ? game->options.teamplay : 0;
  if (teamplay == 1 && combat.team != NULL) {
    qa_q1mpm_combat_read self;
    const char *self_team = game->host->combat_read(game->host->ctx, entity->actor.id, &self) ? self.team : NULL;
    if (self_team != NULL && strcmp(combat.team, self_team) == 0) return;
  }
  game->host->combat_set_health(game->host->ctx, owned, (float)(combat.health - damage));
}
static const char *head_for(const char *classname) {
  if (strcmp(classname, "monster_ogre") == 0) return "h_ogre";
  if (strcmp(classname, "monster_knight") == 0) return "h_knight";
  if (strcmp(classname, "monster_shambler") == 0) return "h_shams";
  if (strcmp(classname, "monster_demon1") == 0) return "h_demon";
  if (strcmp(classname, "monster_wizard") == 0) return "h_wizard";
  if (strcmp(classname, "monster_zombie") == 0) return "h_zombie";
  if (strcmp(classname, "monster_dog") == 0) return "h_dog";
  if (strcmp(classname, "monster_hell_knight") == 0) return "h_hellkn";
  if (strcmp(classname, "monster_enforcer") == 0) return "h_mega";
  if (strcmp(classname, "monster_army") == 0) return "h_guard";
  if (strcmp(classname, "monster_shalrath") == 0) return "h_shal";
  if (strcmp(classname, "monster_gremlin") == 0) return "h_grem";
  if (strcmp(classname, "monster_scourge") == 0) return "h_scourg";
  if (strcmp(classname, "monster_fish") == 0) return "gib1";
  return "h_player";
}
static void gorge(qa_q1mpm_mission_monster *monster, double side) {
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (!monster->state.enemy.has) return;
  qa_actor_id target = monster->state.enemy.id;
  game->sound(game, entity, "demon/dhit2.wav", QA_Q1MPM_CH("weapon"), 1, 1);
  gorge_damage(monster, target, 7 + 5 * game->host->random(game->host->ctx));
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_q1mpm_ext_spawn_meat_spray(game, entity,
                                qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), qa_q1mpm_vscale(basis.forward, 16)),
                                qa_q1mpm_vscale(basis.right, side));
  if (game->health(game, target) >= -200) return;
  qa_q1mpm_actor *victim = game->entity(game, QA_Q1MPM_SOME(target));
  if (victim != NULL && qa_q1mpm_actor_number(victim, "gorging") == 0) {
    qa_q1mpm_fields_set(victim, "gorging", "1");
    qa_q1mpm_game_sound(game, entity, "player/udeath.wav");
    qa_q1mpm_ext_throw_head_damage(game, victim, head_for(victim->classname), -15);
    double amount = 150 + 100 * game->host->random(game->host->ctx);
    double health = game->health(game, entity->actor.id);
    if (health > 0 && health < entity->max_health)
      game->host->combat_set_health(game->host->ctx, entity->actor,
                                    fmin(entity->max_health, health + ceil(amount)));
    split(monster);
  }
  monster->state.enemy = QA_Q1MPM_NONE;
  qa_q1mpm_number(monster, "gorging", 0);
  qa_q1mpm_monster_play(monster, "gremlin_look1");
}
static void melee_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1mpm_actor_number(monster->entity, "gorging") != 0) {
    qa_q1mpm_monster_play(monster, "gremlin_gorge1");
    return;
  }
  if (qa_q1mpm_actor_number(monster->entity, "stoleweapon") == 1)
    qa_range_error("gremlin meleeing with stolen weapon");
  if (monster->state.enemy.has && monster->game->is_player(monster->game, monster->state.enemy) &&
      monster->game->host->random(monster->game->host->ctx) < 0.4 && qa_q1mpm_gremlin_steal(monster))
    return;
  double r = monster->game->host->random(monster->game->host->ctx);
  qa_q1mpm_monster_play(monster, r < 0.3 ? "gremlin_claw1" : r < 0.6 ? "gremlin_lunge1" : "gremlin_claw1");
}
static void missile_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1mpm_actor_number(monster->entity, "stoleweapon") != 0) {
    if (qa_q1mpm_gremlin_weapon_attack(monster)) return;
    if (monster->game->host->random(monster->game->host->ctx) < 0.1 &&
        (monster->entity->movement_flags & 512) != 0) {
      qa_q1mpm_monster_play(monster, "gremlin_jump1");
      return;
    }
  }
  if ((monster->entity->movement_flags & 512) != 0) qa_q1mpm_monster_play(monster, "gremlin_jump1");
}
static void fire_lightning_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_gremlin_fire_lightning(monster);
}
static void drop_backpack_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_gremlin_drop_backpack(monster);
}
static void gib_action(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; gib_impl(monster, -35); }
static void stand1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_gremlin_stand(monster);
  qa_q1mpm_monster_delay(monster, 0.2);
}
static void jump5(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_face(monster);
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if ((entity->movement_flags & 512) == 0) { qa_q1mpm_monster_play(monster, "gremlin_run1"); return; }
  entity->touch = game->named.touch(game->named.ctx, entity, "hipnotic:Gremlin_JumpTouch");
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 1));
  patch.has_velocity = true;
  patch.velocity = qa_q1mpm_vadd(qa_q1mpm_vscale(basis.forward, 300), QA_Q1MPM_V3(0, 0, 300));
  game->set_body(game, entity, &patch);
  entity->movement_flags -= 512;
}
static void jump11(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1mpm_monster_delay(monster, 3); }
static void shot1(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; monster->entity->effects |= 2; }
static void nail1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->effects |= 2;
  qa_q1mpm_gremlin_fire_nail(monster);
}
static void laser1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->effects |= 2;
  qa_q1mpm_gremlin_fire_laser(monster);
}
static void look1(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1mpm_monster_delay(monster, 0.2); }
static void glook20(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_gremlin_drop_backpack(monster);
  qa_q1mpm_number(monster, "stoleweapon", 0);
  resume(NULL, monster);
}
static void spawn1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_delay(monster, 0.3);
  qa_q1mpm_number(monster, "gremlin:pain-disabled", 1);
}
static void spawn2(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1mpm_monster_delay(monster, 0.3); }
static void spawn6(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "gremlin:pain-disabled", 0);
}
static void flip1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_monster_face(monster);
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_q1mpm_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1mpm_vadd(qa_q1mpm_monster_origin(monster), QA_Q1MPM_V3(0, 0, 1));
  patch.has_velocity = true;
  patch.velocity = qa_q1mpm_vsub(QA_Q1MPM_V3(0, 0, 350), qa_q1mpm_vscale(basis.forward, 200));
  game->set_body(game, entity, &patch);
  entity->movement_flags &= ~512;
  qa_q1mpm_game_sound(game, entity, "grem/death.wav");
}
static void flip6(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->entity->touch = monster->game->named.touch(monster->game->named.ctx, monster->entity,
                                                       "hipnotic:Gremlin_FlipTouch");
}
static double din0 = 0, din2 = 2, din4 = 4, din8 = 8, din12 = 12, din16 = 16, din200 = 200, din_neg200 = -200, din_neg4 = -4;
static void ai_back_dist(void *ctx, qa_q1mpm_mission_monster *monster) {
  double distance = *(const double *)ctx;
  monster->game->host->walk_move(monster->game->host->ctx, monster->entity->actor,
                                  monster->game->body(monster->game, monster->entity).angles.y + 180, distance);
}
static void gremlin_run_dist(void *ctx, qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_gremlin_run(monster, *(const double *)ctx);
}
static void gremlin_walk8(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_gremlin_walk(monster, 8);
}
static void melee_side_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  melee_side(monster, *(const double *)ctx);
}
static void gorge_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  gorge(monster, *(const double *)ctx);
}
static void fire_nail_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_gremlin_fire_nail(monster);
}
static void fire_laser_action(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_gremlin_fire_laser(monster);
}
static void jump_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                       bool has_normal, qa_vec3 normal, const void *surface) {
  (void)other; (void)has_normal; (void)normal; (void)surface;
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_require((qa_q1mpm_runtime *)ctx, entity);
  if (game->health(game, entity->actor.id) <= 0) return;
  if (!game->host->check_bottom(game->host->ctx, entity->actor.id)) {
    if ((entity->movement_flags & 512) != 0) {
      entity->touch = (qa_q1mpm_callback){NULL, NULL, QA_Q1MPM_CB_TOUCH};
      qa_q1mpm_monster_set_next_frame(monster, "gremlin_jump1");
      qa_q1mpm_monster_delay(monster, 0.1);
    }
    return;
  }
  entity->touch = (qa_q1mpm_callback){NULL, NULL, QA_Q1MPM_CB_TOUCH};
  qa_q1mpm_monster_set_next_frame(monster, "gremlin_jump12");
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void flip_touch(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity, qa_actor_id other,
                       bool has_normal, qa_vec3 normal, const void *surface) {
  (void)other; (void)has_normal; (void)normal; (void)surface;
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_require((qa_q1mpm_runtime *)ctx, entity);
  if (!game->host->check_bottom(game->host->ctx, entity->actor.id)) {
    if ((entity->movement_flags & 512) != 0) {
      entity->touch = (qa_q1mpm_callback){NULL, NULL, QA_Q1MPM_CB_TOUCH};
      qa_q1mpm_monster_set_next_frame(monster, "gremlin_flip1");
      qa_q1mpm_monster_delay(monster, 0.1);
    }
    return;
  }
  entity->touch = (qa_q1mpm_callback){NULL, NULL, QA_Q1MPM_CB_TOUCH};
  qa_q1mpm_monster_set_next_frame(monster, "gremlin_flip8");
  qa_q1mpm_monster_delay(monster, 0.1);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->runtime->authored_gremlins++;
  qa_q1mpm_fields_set(monster->entity, "yaw_speed", "40");
  qa_q1mpm_monster_spawn_default(monster);
  monster->entity->max_health = 101;
  if (!monster->game->host->inventory_has(monster->game->host->ctx, monster->entity->actor.id)) {
    static const char *const ammo_names[] = {"shells", "nails", "rockets", "cells"};
    qa_item_id items[4];
    char item_text[4][32];
    qa_q1mpm_inventory_entry entries[4];
    for (int i = 0; i < 4; i++) {
      snprintf(item_text[i], sizeof(item_text[i]), "q1:ammo/%s", ammo_names[i]);
      items[i] = item_text[i];
      entries[i] = (qa_q1mpm_inventory_entry){items[i], 0, 1000000, "source-counter", "binary32"};
    }
    monster->game->host->inventory_create(monster->game->host->ctx, monster->entity->actor, entries, 4);
  }
}
static bool check_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (monster->game->time < monster->state.attack_finished) return false;
  if (qa_q1mpm_monster_distance(monster) <= 90 && qa_q1mpm_actor_number(monster->entity, "stoleweapon") == 0) {
    monster->entity->attack_state = "melee";
    return true;
  }
  if (monster->game->host->random(monster->game->host->ctx) <
      0.03 + qa_q1mpm_actor_number(monster->entity, "stoleweapon")) {
    monster->entity->attack_state = "missile";
    return true;
  }
  return false;
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker, double damage) {
  (void)ctx; (void)damage;
  if (qa_q1mpm_actor_number(monster->entity, "gremlin:pain-disabled") != 0) return;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  if (game->host->random(game->host->ctx) < 0.8) {
    qa_q1mpm_number(monster, "gorging", 0);
    monster->state.enemy = attacker;
    if (attacker.has) qa_q1mpm_monster_found(monster, attacker.id);
  }
  if (qa_q1mpm_str_eq(qa_q1mpm_callback_name(entity->touch), "hipnotic:Gremlin_JumpTouch") ||
      monster->state.pain_finished > game->time)
    return;
  monster->state.pain_finished = game->time + 1;
  double r = game->host->random(game->host->ctx);
  char path[32];
  snprintf(path, sizeof(path), "grem/pain%d.wav", r < 0.33 ? 1 : r < 0.66 ? 2 : 3);
  qa_q1mpm_game_sound(game, entity, path);
  qa_q1mpm_monster_play(monster, qa_q1mpm_actor_number(entity, "stoleweapon") != 0 ? "gremlin_gunpain1"
                                                                                   : "gremlin_pain1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, qa_q1mpm_opt_actor attacker) {
  (void)ctx;
  qa_q1mpm_game *game = monster->game;
  qa_q1mpm_actor *entity = monster->entity;
  qa_q1mpm_inventory_entry entries[64];
  size_t entry_count = game->host->inventory_entries(game->host->ctx, entity->actor.id, entries, 64);
  bool has_weapon = false;
  for (size_t i = 0; i < entry_count; i++) {
    if (strncmp(entries[i].item, "q1:weapon/", 10) == 0 && strcmp(entries[i].item, "q1:weapon/axe") != 0 &&
        strcmp(entries[i].item, "q1:weapon/shotgun") != 0 &&
        strcmp(entries[i].item, "q1:weapon/hipnotic:mjolnir") != 0 && entries[i].count > 0)
      has_weapon = true;
  }
  if (has_weapon) {
    qa_q1mpm_gremlin_drop_backpack(monster);
    qa_q1mpm_number(monster, "stoleweapon", 0);
  }
  qa_q1mpm_basis basis = game->make_vectors(game, game->body(game, entity).angles);
  qa_q1mpm_body attacker_body;
  bool has_attacker_body = attacker.has && game->host->bodies_read(game->host->ctx, attacker.id, &attacker_body);
  double facing = qa_q1mpm_dot(qa_q1mpm_normalize(qa_q1mpm_vsub(
                                    has_attacker_body ? attacker_body.origin : QA_Q1MPM_ZERO,
                                    qa_q1mpm_monster_origin(monster))),
                                basis.forward);
  double health = game->health(game, entity->actor.id);
  if (health < -35) { gib_impl(monster, health); return; }
  if (facing > 0.7 && game->host->random(game->host->ctx) < 0.5 && (entity->movement_flags & 512) != 0) {
    qa_q1mpm_monster_play(monster, "gremlin_flip1");
    return;
  }
  qa_q1mpm_monster_play(monster, "gremlin_die1");
}
static const char *const classnames[] = {"monster_gremlin"};
static const char *const spec_gibs[] = {"gib1", "gib1", "gib1"};
static const qa_q1mpm_named_action actions[] = {
    {"Gremlin_MeleeAttack", {melee_attack, NULL}},
    {"Gremlin_MissileAttack", {missile_attack, NULL}},
    {"Gremlin_FireLightningGun", {fire_lightning_action, NULL}},
    {"GremlinDropBackpack", {drop_backpack_action, NULL}},
    {"gremlin_gib", {gib_action, NULL}},
    {"hipgrem:gremlin_stand1", {stand1, NULL}},
    {"hipgrem:gremlin_jump5", {jump5, NULL}},
    {"hipgrem:gremlin_jump11", {jump11, NULL}},
    {"hipgrem:gremlin_shot1", {shot1, NULL}},
    {"hipgrem:gremlin_nail1", {nail1, NULL}},
    {"hipgrem:gremlin_laser1", {laser1, NULL}},
    {"hipgrem:gremlin_look1", {look1, NULL}},
    {"hipgrem:gremlin_look9", {resume, NULL}},
    {"hipgrem:gremlin_glook20", {glook20, NULL}},
    {"hipgrem:gremlin_spawn1", {spawn1, NULL}},
    {"hipgrem:gremlin_spawn2", {spawn2, NULL}},
    {"hipgrem:gremlin_spawn6", {spawn6, NULL}},
    {"hipgrem:gremlin_flip1", {flip1, NULL}},
    {"hipgrem:gremlin_flip6", {flip6, NULL}},
    {"ai_back(2)", {ai_back_dist, &din2}},
    {"ai_back(4)", {ai_back_dist, &din4}},
    {"gremlin_run(0)", {gremlin_run_dist, &din0}},
    {"gremlin_run(8)", {gremlin_run_dist, &din8}},
    {"gremlin_run(12)", {gremlin_run_dist, &din12}},
    {"gremlin_run(16)", {gremlin_run_dist, &din16}},
    {"gremlin_walk(8)", {gremlin_walk8, NULL}},
    {"Gremlin_Melee(0)", {melee_side_action, &din0}},
    {"Gremlin_Melee(200)", {melee_side_action, &din200}},
    {"Gremlin_Gorge(-200)", {gorge_action, &din_neg200}},
    {"Gremlin_Gorge(200)", {gorge_action, &din200}},
    {"Gremlin_FireNailGun(-4)", {fire_nail_action, &din_neg4}},
    {"Gremlin_FireNailGun(4)", {fire_nail_action, &din4}},
    {"Gremlin_FireLaserGun(-4)", {fire_laser_action, &din_neg4}},
    {"Gremlin_FireLaserGun(4)", {fire_laser_action, &din4}},
};
void qa_q1mpm_gremlin_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out) {
  *out = (qa_q1mpm_pack_monster_definition){0};
  out->spec = (qa_q1mpm_species_spec){
      "gremlin", NULL, classnames, 1, "grem", "h_grem", 100, -35, spec_gibs, 3,
      {{-16, -16, -24}, {16, 16, 32}}, "gremlin_stand1", "gremlin_walk1", "gremlin_run1",
      "grem/sight1.wav", "Gremlin_MissileAttack", true, "walk"};
  out->frames = (qa_q1mpm_frame_table){qa_q1mpm_tables_hipgrem_entries, QA_Q1MPM_TABLES_HIPGREM_COUNT};
  out->actions = actions;
  out->action_count = sizeof(actions) / sizeof(actions[0]);
  qa_q1mpm_named_handlers *callbacks = calloc(2, sizeof(*callbacks));
  if (callbacks != NULL) {
    callbacks[0].name = "Gremlin_JumpTouch";
    callbacks[0].handlers = (qa_q1mpm_handlers){runtime, NULL, jump_touch, NULL, NULL, NULL};
    callbacks[1].name = "Gremlin_FlipTouch";
    callbacks[1].handlers = (qa_q1mpm_handlers){runtime, NULL, flip_touch, NULL, NULL, NULL};
    out->callbacks = callbacks;
    out->callback_count = 2;
  }
  out->spawn = (qa_q1mpm_action){spawn, NULL};
  out->pain = pain;
  out->die = die;
  out->melee = (qa_q1mpm_action){melee_attack, NULL};
  out->check_attack = check_attack;
}
