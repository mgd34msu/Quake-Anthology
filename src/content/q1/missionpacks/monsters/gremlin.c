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
#include "qa/foundation/js.h"

static void gib_impl(qa_q1mpm_mission_monster *monster, double damage) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_sound_defaults(game, entity->actor.id, "player/udeath.wav");
  qa_q1_throw_head_damage(game, entity, "h_grem", damage);
  for (int i = 0; i < 3; i++) (void)qa_q1_throw_gib(game, qa_q1_base_monster_origin(&monster->base), "gib1", damage);
}
static void resume(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (monster->base.state->has_old_enemy && qa_q1_health(monster->base.game, monster->base.state->old_enemy) > 0) {
    qa_q1_base_monster_set_enemy(&monster->base, &monster->base.state->old_enemy);
    (void)qa_q1_actor_reference_set(monster->base.entity, "goalentity", qa_q1_base_monster_enemy(&monster->base));
    q1mpm_set_next_frame(&monster->base, monster->base.spec->run);
    monster->base.state->attack_finished = monster->base.game->time + 1;
    qa_q1_base_monster_delay(&monster->base, 0.1);
    return;
  }
  qa_q1_base_monster_play(&monster->base, q1mpm_str_eq(monster->base.state->path, "") ? "gremlin_stand1" : "gremlin_walk1");
}
static void melee_side(qa_q1mpm_mission_monster *monster, double side) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_base_monster_face(&monster->base);
  if (!monster->base.state->has_enemy || qa_q1_base_monster_distance(&monster->base) > 100 ||
      !qa_q1_can_damage(game, monster->base.state->enemy, entity->actor.id))
    return;
  qa_q1_sound(game, entity->actor.id, "grem/attack.wav", "weapon", 1, 1);
  (void)qa_q1_damage(game, monster->base.state->enemy, entity->actor.id, &entity->actor.id, 10 + 5 * qa_q1_host_random(game), NULL, NULL, NULL, NULL);
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  (void)qa_q1_spawn_meat_spray(game, entity,
                               qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), qa_q1_vscale(basis.forward, 16)),
                               qa_q1_vscale(basis.right, side));
}
static bool clear_trace(qa_q1mpm_mission_monster *monster, qa_vec3 end) {
  return q1mpm_trace(monster->base.game, qa_q1_base_monster_origin(&monster->base), end, QA_Q1_POINT,
                     &monster->base.entity->actor.id, true).fraction == 1;
}
static void split(qa_q1mpm_mission_monster *monster) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1mpm_runtime *runtime = monster->runtime;
  if (runtime->spawned_gremlins >= runtime->authored_gremlins * 2) return;
  qa_vec3 angles = qa_q1_get_body(game, entity).angles;
  qa_vec3 position = qa_q1_base_monster_origin(&monster->base);
  bool found = false;
  for (int count = 0; count < 10; count++) {
    const qa_vec3 origin = qa_q1_base_monster_origin(&monster->base);
    position = qa_q1_vadd(origin, qa_q1_vscale(qa_q1_make_vectors(game, angles).forward, 80));
    bool proceed = true;
    size_t actor_count = 0;
    qa_actor_id *actors = qa_q1mpm_radius_actors(game, position, 35, &actor_count);
    for (size_t i = 0; i < actor_count; i++) {
      qa_q1_actor *other = qa_q1_get_entity(game, &actors[i]);
      if (qa_q1_health(game, actors[i]) > 0 &&
          (qa_js_bit_and(other == NULL ? 0 : other->movement_flags, 32) != 0 || qa_q1_is_player(game, &actors[i])))
        proceed = false;
    }
    free(actors);
    if (clear_trace(monster, position) && proceed && clear_trace(monster, qa_q1_vsub(position, (qa_vec3){40, 40, 0})) &&
        clear_trace(monster, qa_q1_vadd(position, (qa_vec3){40, 40, 0})) &&
        clear_trace(monster, qa_q1_vadd(position, (qa_vec3){0, 0, 64})) &&
        !clear_trace(monster, qa_q1_vsub(position, (qa_vec3){0, 0, 64}))) {
      found = true;
      break;
    }
    angles.y += 36;
  }
  if (!found) return;
  runtime->spawned_gremlins++;
  qa_q1_actor *child = qa_q1_clone_entity(game, entity);
  qa_q1mpm_mission_monster *controller = qa_q1mpm_runtime_require(runtime, child);
  child->solid = "slidebox";
  child->movement = "step";
  q1mpm_set_text(&child->model, "progs/grem.mdl");
  qa_q1_set_bounds(game, child, qa_q1mpm_hull_bounds);
  double health = qa_js_max(100, qa_q1_health(game, entity->actor.id));
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, health / 2);
  qa_gameplay_authority_set_health(game->host.combat, child->actor, health / 2);
  qa_q1mpm_number(controller, "stoleweapon", 0);
  /* The table's entries list is scratch: copy before configuring. */
  size_t entry_count = 0;
  const qa_inventory_entry *scratch = qa_shared_inventory_table_entries(game->host.inventory, child->actor.id, &entry_count);
  qa_inventory_entry *entries = malloc((entry_count == 0 ? 1 : entry_count) * sizeof *entries);
  if (entries == NULL) qa_range_error("out of memory");
  memcpy(entries, scratch, entry_count * sizeof *entries);
  for (size_t i = 0; i < entry_count; i++) {
    if (strncmp(entries[i].item, "q1:weapon/", 10) != 0) continue;
    qa_inventory_entry entry = entries[i]; /* TS { ...entry, count: 0 } */
    entry.count = 0;
    qa_shared_inventory_table_configure(game->host.inventory, child->actor, entry, NULL, NULL);
  }
  free(entries);
  game->total_monsters++;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "monster-total";
  event.total = game->total_monsters;
  qa_q1_emit(game, event);
  qa_q1_set_origin(game, child, position);
  qa_q1_base_monster_play(&controller->base, "gremlin_spawn1");
  qa_q1_base_monster_set_enemy(&controller->base, NULL);
  qa_q1mpm_number(controller, "gorging", 0);
}
static void gorge_damage(qa_q1mpm_mission_monster *monster, qa_actor_id target, double damage) {
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_actor *victim = qa_q1_get_entity(game, &target);
  qa_owned_actor owned;
  const bool has_owned = qa_session_actor_registry_resolve_owned(game->host.actors, target, &owned);
  const qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, target);
  if (!has_owned || !combat.has_value || qa_js_bit_and(victim == NULL ? 0 : victim->movement_flags, 64) != 0) return;
  /* TS player?.powerups.get("invulnerability") ?? victim?.number("invincible_finished") ?? 0. */
  double invulnerable_until = 0;
  const qa_q1_player_state *player = qa_q1_player(game, target);
  if (player == NULL || !qa_q1_player_powerup_has(player, "invulnerability", &invulnerable_until))
    invulnerable_until = victim == NULL ? 0 : qa_q1_actor_number(victim, "invincible_finished");
  if (invulnerable_until >= game->time) {
    if (qa_q1_actor_number(entity, "invincible_sound") < game->time) {
      qa_q1_sound(game, owned.id, "items/protect3.wav", "item", 1, 1);
      qa_q1mpm_number(monster, "invincible_sound", game->time + 2);
    }
    return;
  }
  if (qa_q1_option_teamplay(game) == 1 && combat.value.team != NULL) {
    const qa_combat_state_result self = qa_gameplay_authority_read(game->host.combat, entity->actor.id);
    if (self.has_value && self.value.team != NULL && strcmp(combat.value.team, self.value.team) == 0) return;
  }
  qa_gameplay_authority_set_health(game->host.combat, owned, (float)(combat.value.health - damage));
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
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (!monster->base.state->has_enemy) return;
  qa_actor_id target = monster->base.state->enemy;
  qa_q1_sound(game, entity->actor.id, "demon/dhit2.wav", "weapon", 1, 1);
  gorge_damage(monster, target, 7 + 5 * qa_q1_host_random(game));
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  (void)qa_q1_spawn_meat_spray(game, entity,
                               qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), qa_q1_vscale(basis.forward, 16)),
                               qa_q1_vscale(basis.right, side));
  if (qa_q1_health(game, target) >= -200) return;
  qa_q1_actor *victim = qa_q1_get_entity(game, &target);
  if (victim != NULL && qa_q1_actor_number(victim, "gorging") == 0) {
    (void)qa_q1_actor_field_set(victim, "gorging", "1");
    qa_q1_sound_defaults(game, entity->actor.id, "player/udeath.wav");
    qa_q1_throw_head_damage(game, victim, head_for(victim->classname), -15);
    /* hipgrem.qc calls T_Heal from a think where other is world, so it refuses; the roll is still drawn. */
    (void)qa_q1_host_random(game);
    split(monster);
  }
  qa_q1_base_monster_set_enemy(&monster->base, NULL);
  qa_q1mpm_number(monster, "gorging", 0);
  qa_q1_base_monster_play(&monster->base, "gremlin_look1");
}
static void melee_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1_actor_number(monster->base.entity, "gorging") != 0) {
    qa_q1_base_monster_play(&monster->base, "gremlin_gorge1");
    return;
  }
  if (qa_q1_actor_number(monster->base.entity, "stoleweapon") == 1)
    qa_range_error("gremlin meleeing with stolen weapon");
  if (monster->base.state->has_enemy && qa_q1_is_player(monster->base.game, qa_q1_base_monster_enemy(&monster->base)) &&
      qa_q1_host_random(monster->base.game) < 0.4 && qa_q1mpm_gremlin_steal(monster))
    return;
  double r = qa_q1_host_random(monster->base.game);
  qa_q1_base_monster_play(&monster->base, r < 0.3 ? "gremlin_claw1" : r < 0.6 ? "gremlin_lunge1" : "gremlin_claw1");
}
static void missile_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (qa_q1_actor_number(monster->base.entity, "stoleweapon") != 0) {
    if (qa_q1mpm_gremlin_weapon_attack(monster)) return;
    if (qa_q1_host_random(monster->base.game) < 0.1 &&
        qa_js_bit_and(monster->base.entity->movement_flags, 512) != 0) {
      qa_q1_base_monster_play(&monster->base, "gremlin_jump1");
      return;
    }
  }
  if (qa_js_bit_and(monster->base.entity->movement_flags, 512) != 0) qa_q1_base_monster_play(&monster->base, "gremlin_jump1");
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
  qa_q1_base_monster_delay(&monster->base, 0.2);
}
static void jump5(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_face(&monster->base);
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_js_bit_and(entity->movement_flags, 512) == 0) { qa_q1_base_monster_play(&monster->base, "gremlin_run1"); return; }
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "hipnotic:Gremlin_JumpTouch");
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 1});
  patch.has_velocity = true;
  patch.velocity = qa_q1_vadd(qa_q1_vscale(basis.forward, 300), (qa_vec3){0, 0, 300});
  qa_q1_set_body(game, entity, &patch);
  entity->movement_flags -= 512;
}
static void jump11(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1_base_monster_delay(&monster->base, 3); }
static void shot1(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; monster->base.entity->effects = qa_js_bit_or(monster->base.entity->effects, 2); }
static void nail1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->effects = qa_js_bit_or(monster->base.entity->effects, 2);
  qa_q1mpm_gremlin_fire_nail(monster);
}
static void laser1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->effects = qa_js_bit_or(monster->base.entity->effects, 2);
  qa_q1mpm_gremlin_fire_laser(monster);
}
static void look1(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1_base_monster_delay(&monster->base, 0.2); }
static void glook20(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_gremlin_drop_backpack(monster);
  qa_q1mpm_number(monster, "stoleweapon", 0);
  resume(NULL, monster);
}
static void spawn1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_delay(&monster->base, 0.3);
  qa_q1mpm_number(monster, "gremlin:pain-disabled", 1);
}
static void spawn2(void *ctx, qa_q1mpm_mission_monster *monster) { (void)ctx; qa_q1_base_monster_delay(&monster->base, 0.3); }
static void spawn6(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1mpm_number(monster, "gremlin:pain-disabled", 0);
}
static void flip1(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_base_monster_face(&monster->base);
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 1});
  patch.has_velocity = true;
  patch.velocity = qa_q1_vsub((qa_vec3){0, 0, 350}, qa_q1_vscale(basis.forward, 200));
  qa_q1_set_body(game, entity, &patch);
  entity->movement_flags = qa_js_bit_and(entity->movement_flags, qa_js_bit_not(512));
  qa_q1_sound_defaults(game, entity->actor.id, "grem/death.wav");
}
static void flip6(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->base.entity->touch = qa_q1_callback_registry_touch(monster->base.game->named, monster->base.entity,
                                                       "hipnotic:Gremlin_FlipTouch");
}
static const double DIN0 = 0, DIN2 = 2, DIN4 = 4, DIN8 = 8, DIN12 = 12, DIN16 = 16, DIN200 = 200, DIN_NEG200 = -200;
static void ai_back_dist(void *ctx, qa_q1mpm_mission_monster *monster) {
  const double distance = *(const double *)ctx;
  (void)monster->base.game->host.walk_move(monster->base.game->host.ctx, monster->base.entity->actor,
                                           qa_q1_get_body(monster->base.game, monster->base.entity).angles.y + 180, distance);
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
static void jump_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)other; (void)normal; (void)surface;
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_require((qa_q1mpm_runtime *)ctx, entity);
  if (qa_q1_health(game, entity->actor.id) <= 0) return;
  if (!game->host.check_bottom(game->host.ctx, entity->actor.id)) {
    if (qa_js_bit_and(entity->movement_flags, 512) != 0) {
      entity->touch = (qa_q1_bound_touch){0};
      q1mpm_set_next_frame(&monster->base, "gremlin_jump1");
      qa_q1_base_monster_delay(&monster->base, 0.1);
    }
    return;
  }
  entity->touch = (qa_q1_bound_touch){0};
  q1mpm_set_next_frame(&monster->base, "gremlin_jump12");
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void flip_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)other; (void)normal; (void)surface;
  qa_q1mpm_mission_monster *monster = qa_q1mpm_runtime_require((qa_q1mpm_runtime *)ctx, entity);
  if (!game->host.check_bottom(game->host.ctx, entity->actor.id)) {
    if (qa_js_bit_and(entity->movement_flags, 512) != 0) {
      entity->touch = (qa_q1_bound_touch){0};
      q1mpm_set_next_frame(&monster->base, "gremlin_flip1");
      qa_q1_base_monster_delay(&monster->base, 0.1);
    }
    return;
  }
  entity->touch = (qa_q1_bound_touch){0};
  q1mpm_set_next_frame(&monster->base, "gremlin_flip8");
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  monster->runtime->authored_gremlins++;
  (void)qa_q1_actor_field_set(monster->base.entity, "yaw_speed", "40");
  qa_q1mpm_monster_spawn_default(monster);
  monster->base.entity->max_health = 101;
  qa_shared_inventory_table *inventory = monster->base.game->host.inventory;
  if (!qa_shared_inventory_table_has(inventory, monster->base.entity->actor.id)) {
    static const char *const items[] = {"q1:ammo/shells", "q1:ammo/nails", "q1:ammo/rockets", "q1:ammo/cells"};
    qa_inventory_entry entries[4];
    for (int i = 0; i < 4; i++) {
      memset(&entries[i], 0, sizeof entries[i]);
      entries[i].item = items[i];
      entries[i].count = 0;
      entries[i].capacity = 1000000;
      entries[i].has_count_policy = true;
      entries[i].count_policy = (qa_inventory_count_policy){.kind = "source-counter", .arithmetic = "binary32"};
    }
    qa_shared_inventory_table_create_entries(inventory, monster->base.entity->actor, entries, 4);
  }
}
static bool check_attack(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if (monster->base.game->time < monster->base.state->attack_finished) return false;
  if (qa_q1_base_monster_distance(&monster->base) <= 90 && qa_q1_actor_number(monster->base.entity, "stoleweapon") == 0) {
    monster->base.entity->attack_state = "melee";
    return true;
  }
  double left = qa_q1_host_random(monster->base.game);
  double right = 0.03 + qa_q1_actor_number(monster->base.entity, "stoleweapon");
  if (left < right) {
    monster->base.entity->attack_state = "missile";
    return true;
  }
  return false;
}
static void pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)damage;
  if (qa_q1_actor_number(monster->base.entity, "gremlin:pain-disabled") != 0) return;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_q1_host_random(game) < 0.8) {
    qa_q1mpm_number(monster, "gorging", 0);
    qa_q1_base_monster_set_enemy(&monster->base, attacker);
    if (attacker != NULL) qa_q1_base_monster_found(&monster->base, *attacker);
  }
  if (q1mpm_str_eq(qa_q1_touch_name(entity->touch), "hipnotic:Gremlin_JumpTouch") ||
      monster->base.state->pain_finished > game->time)
    return;
  monster->base.state->pain_finished = game->time + 1;
  double r = qa_q1_host_random(game);
  char path[32];
  snprintf(path, sizeof(path), "grem/pain%d.wav", r < 0.33 ? 1 : r < 0.66 ? 2 : 3);
  qa_q1_sound_defaults(game, entity->actor.id, path);
  qa_q1_base_monster_play(&monster->base, qa_q1_actor_number(entity, "stoleweapon") != 0 ? "gremlin_gunpain1"
                                                                                   : "gremlin_pain1");
}
static void die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host.inventory, entity->actor.id, &entry_count);
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
  qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_q1_body attacker_body;
  const bool has_attacker_body = attacker != NULL && q1mpm_read_body(game, *attacker, &attacker_body);
  double facing = qa_q1_dot(qa_q1_normalize(qa_q1_vsub(
                                    has_attacker_body ? attacker_body.origin : QA_Q1_ZERO,
                                    qa_q1_base_monster_origin(&monster->base))),
                                basis.forward);
  double health = qa_q1_health(game, entity->actor.id);
  if (health < -35) { gib_impl(monster, health); return; }
  if (facing > 0.7 && qa_q1_host_random(game) < 0.5 && qa_js_bit_and(entity->movement_flags, 512) != 0) {
    qa_q1_base_monster_play(&monster->base, "gremlin_flip1");
    return;
  }
  qa_q1_base_monster_play(&monster->base, "gremlin_die1");
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
    {"ai_back(2)", {ai_back_dist, (void *)&DIN2}},
    {"ai_back(4)", {ai_back_dist, (void *)&DIN4}},
    {"gremlin_run(0)", {gremlin_run_dist, (void *)&DIN0}},
    {"gremlin_run(8)", {gremlin_run_dist, (void *)&DIN8}},
    {"gremlin_run(12)", {gremlin_run_dist, (void *)&DIN12}},
    {"gremlin_run(16)", {gremlin_run_dist, (void *)&DIN16}},
    {"gremlin_walk(8)", {gremlin_walk8, NULL}},
    {"Gremlin_Melee(0)", {melee_side_action, (void *)&DIN0}},
    {"Gremlin_Melee(200)", {melee_side_action, (void *)&DIN200}},
    {"Gremlin_Gorge(-200)", {gorge_action, (void *)&DIN_NEG200}},
    {"Gremlin_Gorge(200)", {gorge_action, (void *)&DIN200}},
    {"Gremlin_FireNailGun(-4)", {fire_nail_action, NULL}},
    {"Gremlin_FireLaserGun(-4)", {fire_laser_action, NULL}},
    {"Gremlin_FireNailGun(4)", {fire_nail_action, NULL}},
    {"Gremlin_FireLaserGun(4)", {fire_laser_action, NULL}},
};
void qa_q1mpm_gremlin_definition(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out) {
  *out = (qa_q1mpm_pack_monster_definition){0};
  out->spec = (qa_q1_base_monster_species){
      "gremlin", NULL, classnames, 1, "grem", "h_grem", 100, -35, spec_gibs, 3,
      {{-16, -16, -24}, {16, 16, 32}}, "gremlin_stand1", "gremlin_walk1", "gremlin_run1",
      "grem/sight1.wav", "Gremlin_MissileAttack", true, "walk"};
  out->frames = (qa_q1_monster_frame_table){qa_q1mpm_tables_hipgrem_entries, QA_Q1MPM_TABLES_HIPGREM_COUNT};
  out->actions = actions;
  out->action_count = sizeof(actions) / sizeof(actions[0]);
  qa_q1mpm_named_handlers *callbacks = qa_q1mpm_runtime_calloc(runtime, 2, sizeof *callbacks);
  callbacks[0].name = "Gremlin_JumpTouch";
  callbacks[0].handlers = (qa_q1_callback_handlers){.ctx = runtime, .touch = jump_touch};
  callbacks[1].name = "Gremlin_FlipTouch";
  callbacks[1].handlers = (qa_q1_callback_handlers){.ctx = runtime, .touch = flip_touch};
  out->callbacks = callbacks;
  out->callback_count = 2;
  out->spawn = (qa_q1mpm_action){spawn, NULL};
  out->pain = pain;
  out->die = die;
  out->melee = (qa_q1mpm_action){melee_attack, NULL};
  out->check_attack = check_attack;
}
