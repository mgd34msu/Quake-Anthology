/* Port of src/content/q1/base/grunt.ts: monsters/grunt.qc,
 * monsters/rottweiler.qc and the soldier/dog parts of fight.qc and
 * combat.qc. Copyright (C) 1996-2022 id Software LLC.
 * GPL-2.0-or-later. */
#include "qa/content/q1/base/grunt.h"

#include "base_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/core/math.h"
#include "qa/foundation/js.h"

static const qa_bounds POINT_BOUNDS = {{0, 0, 0}, {0, 0, 0}};

static double host_random(qa_q1_services *game) { return game->host.random(game->host.ctx); }
static void set_name(char *dst, size_t size, const char *src) {
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memcpy(dst, src, length);
  dst[length] = '\0';
}
static const char *prefix_of(const qa_q1_base_monster *monster) {
  return monster->has_source && monster->source.callback_prefix != NULL ? monster->source.callback_prefix : "base";
}
/* TS monster.rangeDistance() / visible(): the default target is this.enemy. */
static double range_enemy(qa_q1_base_monster *monster) {
  if (!monster->state->has_enemy) return qa_q1_base_monster_range_distance(monster, NULL);
  const qa_actor_id enemy = monster->state->enemy;
  return qa_q1_base_monster_range_distance(monster, &enemy);
}
static bool visible_enemy(qa_q1_base_monster *monster) {
  if (!monster->state->has_enemy) return false;
  const qa_actor_id enemy = monster->state->enemy;
  return qa_q1_base_monster_visible(monster, &enemy);
}

void qa_q1_grunt_drop_shells(qa_q1_base_monster *monster) {
  monster->entity->solid = "none";
  qa_q1_link(monster->game, monster->entity);
  qa_q1_backpack_contents contents;
  memset(&contents, 0, sizeof contents);
  contents.weapon = NULL;
  contents.shells = 5;
  (void)qa_q1_drop_backpack(monster->game, qa_q1_base_monster_origin(monster), &contents, NULL);
}
void qa_q1_grunt_walk_back(qa_q1_base_monster *monster, double distance) {
  qa_q1_services *game = monster->game;
  (void)game->host.walk_move(game->host.ctx, monster->entity->actor, qa_q1_get_body(game, monster->entity).angles.y + 180,
                             distance);
}
void qa_q1_army_fire(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_face(monster);
  qa_q1_sound(game, entity->actor.id, "soldier/sattck1.wav", "weapon", 1, 1);
  qa_q1_body enemy;
  if (!monster->state->has_enemy || !q1base_read_body(game, monster->state->enemy, &enemy)) return;
  const qa_vec3 origin = qa_q1_base_monster_origin(monster);
  const qa_vec3 aim = qa_q1_normalize(qa_q1_vsub(qa_q1_vsub(enemy.origin, qa_q1_vscale(enemy.velocity, 0.2)), origin));
  qa_q1_fire_bullets(game, entity->actor, aim, qa_q1_actor_vector(entity, "v_angle"), 4, 0.1, 0.1, NULL);
  qa_q1_effect(game, "muzzleflash", qa_q1_base_monster_origin(monster), &entity->actor.id, 1);
}
void qa_q1_dog_leap(qa_q1_base_monster *monster, const char *touch) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  char name[128];
  if (touch == NULL) {
    (void)snprintf(name, sizeof name, "%s:monster_jump_touch", prefix_of(monster));
    touch = name;
  }
  qa_q1_base_monster_face(monster);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, touch);
  const qa_q1_body body = qa_q1_get_body(game, entity);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = qa_q1_vadd(body.origin, (qa_vec3){0, 0, 1});
  patch.has_velocity = true;
  patch.velocity = qa_q1_vadd(qa_q1_vscale(qa_q1_make_vectors(game, body.angles).forward, 300), (qa_vec3){0, 0, 200});
  patch.has_ground = true;
  patch.ground = NULL;
  qa_q1_set_body(game, entity, &patch);
  entity->movement_flags = qa_js_to_int32(entity->movement_flags) & ~512;
}
void qa_q1_dog_bite(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (!monster->state->has_enemy) return;
  qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 10);
  if (!monster->state->has_enemy) return;
  const qa_actor_id enemy = monster->state->enemy;
  if (qa_q1_can_damage(game, enemy, entity->actor.id) && qa_q1_base_monster_distance(monster) <= 100) {
    double roll = host_random(game); /* three draws, left to right */
    roll += host_random(game);
    roll += host_random(game);
    (void)qa_q1_damage(game, enemy, entity->actor.id, &entity->actor.id, roll * 8, NULL, "direct", "", NULL);
  }
}

bool qa_q1_grunt_action(qa_q1_base_monster *monster, const char *name) {
  if (!strcmp(name, "army_atk5")) {
    qa_q1_base_monster_face(monster);
    qa_q1_army_fire(monster);
    return true;
  }
  if (!strcmp(name, "army_atk7")) {
    qa_q1_base_monster_face(monster);
    if (qa_q1_option_skill(monster->game) == 3 && !monster->state->refired && visible_enemy(monster)) {
      monster->state->refired = true;
      set_name(monster->next_frame, sizeof monster->next_frame, "army_atk1");
    }
    return true;
  }
  if (!strcmp(name, "army_die3")) { qa_q1_grunt_drop_shells(monster); return true; }
  if (!strcmp(name, "army_cdie2")) { qa_q1_grunt_walk_back(monster, 5); return true; }
  if (!strcmp(name, "army_cdie3")) {
    qa_q1_grunt_drop_shells(monster);
    qa_q1_grunt_walk_back(monster, 4);
    return true;
  }
  if (!strcmp(name, "army_cdie4")) { qa_q1_grunt_walk_back(monster, 13); return true; }
  if (!strcmp(name, "army_cdie5")) { qa_q1_grunt_walk_back(monster, 3); return true; }
  if (!strcmp(name, "army_cdie6")) { qa_q1_grunt_walk_back(monster, 4); return true; }
  if (!strcmp(name, "dog_atta4")) { qa_q1_dog_bite(monster); return true; }
  if (!strcmp(name, "dog_leap2")) { qa_q1_dog_leap(monster, NULL); return true; }
  return false;
}

bool qa_q1_army_check_attack(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (!monster->state->has_enemy) return false;
  const qa_actor_id enemy = monster->state->enemy;
  qa_vec3 start, end;
  if (!qa_q1_base_monster_eye(monster, entity->actor.id, &start)) return false;
  if (!qa_q1_base_monster_eye(monster, enemy, &end)) return false;
  const double distance = range_enemy(monster);
  if (game->time < monster->state->attack_finished || distance >= 1000) return false;
  qa_q1_trace_request request;
  memset(&request, 0, sizeof request);
  request.start = start;
  request.end = end;
  request.bounds = POINT_BOUNDS;
  request.has_ignore = true;
  request.ignore = entity->actor.id;
  request.monsters = true;
  const qa_q1_trace trace = qa_q1_host_trace(game, request);
  if (!trace.has_actor || !qa_same_actor(trace.actor, enemy) || (trace.in_open && trace.in_water)) return false;
  if (host_random(game) >= (distance < 120 ? 0.9 : distance < 500 ? 0.4 : 0.05)) return false;
  qa_q1_base_monster_play(monster, "army_atk1");
  qa_q1_base_monster_attack_finished(monster, 1 + host_random(game));
  /* The rerelease SoldierCheckAttack rolls for a change of strafe side after committing. */
  if (host_random(game) < 0.3) monster->lefty = !monster->lefty;
  return true;
}
bool qa_q1_dog_check_attack(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (!monster->state->has_enemy) return false;
  const qa_actor_id enemy = monster->state->enemy;
  if (range_enemy(monster) < 120) {
    entity->attack_state = "melee";
    return true;
  }
  const qa_q1_body body = qa_q1_get_body(game, entity);
  qa_q1_body target;
  if (!q1base_read_body(game, enemy, &target)) return false;
  const double height = target.bounds.max.z - target.bounds.min.z;
  const bool vertical = body.origin.z + body.bounds.min.z <= target.origin.z + target.bounds.min.z + height * 0.75 &&
                        body.origin.z + body.bounds.max.z >= target.origin.z + target.bounds.min.z + height * 0.25;
  const double horizontal = hypot(target.origin.x - body.origin.x, target.origin.y - body.origin.y);
  if (!vertical || horizontal < 80 || horizontal > 150) return false;
  entity->attack_state = "missile";
  return true;
}
void qa_q1_army_pain(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_monster *state = monster->state;
  if (state->pain_finished > game->time) return;
  const double r = host_random(game);
  state->pain_finished = game->time + (r < 0.2 ? 0.6 : 1.1);
  qa_q1_base_monster_play(monster, r < 0.2 ? "army_pain1" : r < 0.6 ? "army_painb1" : "army_painc1");
  qa_q1_sound_defaults(game, monster->entity->actor.id, r < 0.2 ? "soldier/pain1.wav" : "soldier/pain2.wav");
}
void qa_q1_dog_pain(qa_q1_base_monster *monster) {
  qa_q1_sound_defaults(monster->game, monster->entity->actor.id, "dog/dpain1.wav");
  qa_q1_base_monster_play(monster, host_random(monster->game) > 0.5 ? "dog_pain1" : "dog_painb1");
}
void qa_q1_dog_jump_touch(qa_q1_base_monster *monster, qa_actor_id other) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (qa_q1_health(game, entity->actor.id) <= 0) return;
  const bool gated = (monster->has_source && monster->source.jump_attack_gate) || game->source_item_start.fn != NULL;
  qa_combat_state combat;
  if (q1base_read_combat(game, other, &combat) && combat.can_take_damage &&
      (!gated || monster->state->attack_finished < game->time) && qa_length3(qa_q1_get_body(game, entity).velocity) > 300) {
    (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, 10 + 10 * host_random(game), NULL, "direct", "",
                       NULL);
    if (gated) monster->state->attack_finished = game->time + 0.5;
  }
  if (game->host.check_bottom(game->host.ctx, entity->actor.id)) {
    entity->touch = (qa_q1_bound_touch){0};
    set_name(monster->next_frame, sizeof monster->next_frame, "dog_run1");
    qa_q1_base_monster_delay(monster, 0.1);
    return;
  }
  if (qa_q1_get_body(game, entity).has_ground) {
    entity->touch = (qa_q1_bound_touch){0};
    set_name(monster->next_frame, sizeof monster->next_frame, "dog_leap1");
    qa_q1_base_monster_delay(monster, 0.1);
  }
}

typedef struct legacy_start { const char *mode; double first; const char *prefix; } legacy_start;
static const legacy_start ARMY_STARTS[] = {
    {"stand", 0, "army_stand"}, {"walk", 90, "army_walk"},  {"run", 73, "army_run"},
    {"attack", 81, "army_atk"}, {"pain", 40, "army_pain"},  {"pain", 46, "army_painb"},
    {"pain", 60, "army_painc"}, {"death", 8, "army_die"},   {"death", 18, "army_cdie"},
};
static const legacy_start DOG_STARTS[] = {
    {"stand", 69, "dog_stand"}, {"walk", 78, "dog_walk"},  {"run", 48, "dog_run"},
    {"attack", 0, "dog_atta"},  {"leap", 60, "dog_leap"},  {"pain", 26, "dog_pain"},
    {"pain", 32, "dog_painb"},  {"death", 8, "dog_die"},   {"death", 17, "dog_dieb"},
};
bool qa_q1_legacy_grunt_frame(const char *species, const char *mode, double first_frame, double frame_index,
                              char *out, size_t out_size) {
  const legacy_start *starts = NULL;
  size_t count = 0;
  if (species != NULL && !strcmp(species, "army")) { starts = ARMY_STARTS; count = sizeof ARMY_STARTS / sizeof *ARMY_STARTS; }
  else if (species != NULL && !strcmp(species, "dog")) { starts = DOG_STARTS; count = sizeof DOG_STARTS / sizeof *DOG_STARTS; }
  for (size_t i = 0; i < count; i++) {
    if (mode == NULL || strcmp(starts[i].mode, mode) != 0 || starts[i].first != first_frame) continue;
    char number[40];
    qa_net_js_number(frame_index + 1, number, sizeof number);
    (void)snprintf(out, out_size, "%s%s", starts[i].prefix, number);
    return true;
  }
  return false;
}
