/* Port of src/content/q1/base/monster-actions.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/monster_actions.h"

#include "base_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/grunt.h"
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

#define V3(x, y, z) ((qa_vec3){(x), (y), (z)})
static const qa_vec3 ZERO3 = {0, 0, 0};

static double host_random(qa_q1_services *game) { return game->host.random(game->host.ctx); }
static void set_name(char *dst, size_t size, const char *src) {
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memcpy(dst, src, length);
  dst[length] = '\0';
}
/* TS `game.sourceBloodyNightmare?.() === true`. */
static bool bloody_nightmare(qa_q1_services *game) {
  return game->source_bloody_nightmare.fn != NULL && game->source_bloody_nightmare.fn(game->source_bloody_nightmare.ctx);
}
/* TS monster.visible() / rangeDistance(): the default target is this.enemy
 * (a NULL target in C is TS null, which is never visible). */
static bool visible_enemy(qa_q1_base_monster *monster) {
  if (!monster->state->has_enemy) return false;
  const qa_actor_id enemy = monster->state->enemy;
  return qa_q1_base_monster_visible(monster, &enemy);
}
static double range_enemy(qa_q1_base_monster *monster) {
  if (!monster->state->has_enemy) return qa_q1_base_monster_range_distance(monster, NULL);
  const qa_actor_id enemy = monster->state->enemy;
  return qa_q1_base_monster_range_distance(monster, &enemy);
}
static const char *prefix_of(qa_q1_base_monster *monster) {
  return monster->has_source && monster->source.callback_prefix != NULL
             ? monster->source.callback_prefix : "base";
}

static void meat(qa_q1_base_monster *monster, double side) {
  qa_q1_basis axes = qa_q1_base_monster_make_vectors(monster);
  qa_vec3 velocity = qa_scale3(axes.right, side == 1 ? (host_random(monster->game) * 2 - 1) * 100 : side);
  (void)qa_q1_spawn_meat_spray(monster->game, monster->entity,
      qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(axes.forward, 16)), velocity);
}
static void chainsaw(qa_q1_base_monster *monster, double side) {
  if (!monster->state->has_enemy ||
      !qa_q1_can_damage(monster->game, monster->state->enemy, monster->entity->actor.id)) return;
  qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 10);
  if (qa_q1_base_monster_distance(monster) > 100) return;
  (void)qa_q1_base_monster_melee(monster, 100, 4, 3, false);
  if (side != 0) meat(monster, side);
}
static void jump(qa_q1_base_monster *monster, bool tar) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_body body = qa_q1_get_body(game, entity);
  if (tar) entity->movement = "bounce";
  monster->counter = 0;
  entity->movement_flags = qa_js_to_int32(entity->movement_flags) & (~512);
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_add3(body.origin, V3(0, 0, 1));
  patch.has_velocity = true;
  qa_vec3 a = qa_scale3(qa_q1_base_monster_make_vectors(monster).forward, 600);
  qa_vec3 b = V3(0, 0, tar ? 200 + host_random(game) * 150 : 250);
  patch.velocity = qa_add3(a, b);
  patch.has_ground = true; patch.ground = NULL;
  qa_q1_set_body(game, entity, &patch);
  char name[128];
  (void)snprintf(name, sizeof name, "%s:monster_jump_touch", prefix_of(monster));
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, name);
  qa_q1_link(game, entity);
}
void qa_q1_monster_jump_touch(qa_q1_base_monster *monster, qa_actor_id other) {
  if (strcmp(monster->spec->species, "dog") == 0) {
    qa_q1_dog_jump_touch(monster, other);
    return;
  }
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  bool tar = strcmp(monster->spec->species, "tarbaby") == 0;
  if (qa_q1_health(game, entity->actor.id) <= 0) return;
  qa_combat_state combat;
  bool damageable = q1base_read_combat(game, other, &combat) && combat.can_take_damage;
  /* MG1 and MG3 Demon_JumpTouch only hurts once attack_finished has passed. */
  const bool gated = ((monster->has_source && monster->source.jump_attack_gate) || game->source_item_start.fn != NULL) && !tar;
  if (damageable && (!tar || strcmp(game->host.classname(game->host.ctx, other), entity->classname) != 0) &&
      (!gated || monster->state->attack_finished < game->time)) {
    if (qa_length3(qa_q1_get_body(game, entity).velocity) > 400) {
      (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, (tar ? 10 : 40) + 10 * host_random(game), NULL, "direct", "", NULL);
      if (tar) qa_q1_sound(game, entity->actor.id, "blob/hit1.wav", "weapon", 1, 1);
      if (gated) monster->state->attack_finished = game->time + 0.5;
    }
  } else if (tar) {
    qa_q1_sound(game, entity->actor.id, "blob/land1.wav", "weapon", 1, 1);
  }
  if (game->host.check_bottom(game->host.ctx, entity->actor.id)) {
    entity->touch = (qa_q1_bound_touch){0};
    if (!tar) entity->movement = "step";
    set_name(monster->next_frame, sizeof monster->next_frame, tar ? "tbaby_jump1" : "demon1_jump11");
    qa_q1_base_monster_delay(monster, 0.1);
    return;
  }
  if (qa_q1_get_body(game, entity).has_ground) {
    entity->touch = (qa_q1_bound_touch){0};
    entity->movement = "step";
    set_name(monster->next_frame, sizeof monster->next_frame, tar ? "tbaby_run1" : "demon1_jump1");
    qa_q1_base_monster_delay(monster, 0.1);
  }
}
static void wizard_fast(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (!monster->state->has_enemy) return;
  qa_actor_id enemy = monster->state->enemy;
  qa_q1_sound(game, entity->actor.id, "wizard/wattack.wav", "weapon", 1, 1);
  qa_q1_basis axes = qa_q1_base_monster_make_vectors(monster);
  static const struct { double side; double delay; } SHOTS[] = {{1, 0.8}, {-1, 0.3}};
  for (size_t i = 0; i < 2; i++) {
    qa_q1_actor *timer = qa_q1_create(game, "wizard_fastfire", NULL, NULL);
    timer->has_owner = true; timer->owner = entity->actor.id;
    qa_vec3 origin = qa_add3(qa_q1_base_monster_origin(monster),
        qa_add3(V3(0, 0, 30), qa_add3(qa_scale3(axes.forward, 14), qa_scale3(axes.right, 14 * SHOTS[i].side))));
    qa_q1_set_origin(game, timer, origin);
    qa_q1_wizard_shot shot = {enemy, qa_scale3(axes.right, SHOTS[i].side)};
    qa_q1_creatures_shots_set(qa_q1_creatures_for(game), timer->actor, shot);
    qa_q1_schedule(game, timer, SHOTS[i].delay, qa_q1_callback_registry_action(game->named, timer, "base:wizard_fastfire"));
  }
}
void qa_q1_wizard_fast_fire(qa_q1_services *game, qa_q1_actor *timer) {
  const qa_q1_wizard_shot *found = qa_q1_creatures_shots_get(qa_q1_creatures_for(game), timer->actor);
  if (found == NULL) qa_range_error("Wizard shot has no source target");
  qa_q1_wizard_shot shot = *found;
  qa_q1_body target;
  bool has_target = q1base_read_body(game, shot.enemy, &target);
  qa_vec3 origin = qa_q1_get_body(game, timer).origin;
  const qa_actor_id *owner = timer->has_owner ? &timer->owner : NULL;
  if ((owner != NULL) && qa_q1_health(game, (*owner)) > 0 && has_target) {
    qa_q1_body body;
    if (q1base_read_body(game, (*owner), &body))
      qa_q1_effect(game, "muzzleflash", body.origin, owner, 1);
    qa_vec3 angles = target.angles;
    if (strcmp(game->options.edition, "rerelease") == 0) angles.x = -angles.x;
    (void)qa_q1_make_vectors(game, angles);
    qa_vec3 direction = qa_q1_normalize(qa_sub3(qa_sub3(target.origin, qa_scale3(shot.right, 13)), origin));
    qa_q1_sound(game, timer->actor.id, "wizard/wattack.wav", "weapon", 1, 1);
    (void)qa_q1_launch_spike(game, owner, origin, qa_scale3(direction, 600), "wizard");
    if (bloody_nightmare(game)) {
      const qa_q1_basis basis = game->basis;
      for (double remaining = floor(host_random(game) * 2 + 0.5); remaining > 0; remaining--) {
        double side = 0.05 + host_random(game) * 0.05;
        if (host_random(game) > 0.5) side = -side;
        double lift = 0.01 + host_random(game) * 0.01;
        if (host_random(game) > 0.5) lift = -lift;
        const qa_vec3 spread = qa_q1_normalize(qa_add3(qa_add3(qa_scale3(direction, 0.9), qa_scale3(basis.right, side)),
                                                       qa_scale3(basis.up, lift)));
        const double speed = 600 - (5 * remaining + host_random(game) * 50);
        (void)qa_q1_launch_spike(game, owner, origin, qa_scale3(spread, speed), "wizard2");
      }
    }
  }
  qa_q1_remove(game, timer);
}
static void hell_knight_shot(qa_q1_base_monster *monster, double offset) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  qa_vec3 origin0 = qa_q1_base_monster_origin(monster);
  qa_vec3 delta = qa_sub3(target, origin0);
  qa_vec3 angle = V3(atan2(delta.z, hypot(delta.x, delta.y)) * 180 / 3.141592653589793,
                     atan2(delta.y, delta.x) * 180 / 3.141592653589793 + offset * 6, 0);
  qa_vec3 forward = qa_q1_make_vectors(game, angle).forward;
  qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 centre = qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5);
  qa_vec3 muzzle = centre;
  if (qa_q1_actor_number(entity, "infected") != 0) muzzle.z = body.bounds.min.z + 28;
  qa_vec3 origin = qa_add3(qa_add3(qa_q1_base_monster_origin(monster), muzzle), qa_scale3(forward, 20));
  qa_vec3 direction = qa_q1_normalize(forward);
  qa_vec3 velocity_dir = direction;
  velocity_dir.z = -direction.z + (host_random(game) - 0.5) * 0.1;
  (void)qa_q1_launch_spike(game, &entity->actor.id, origin,
                           qa_scale3(velocity_dir, bloody_nightmare(game) ? 500 : 300), "knight");
  qa_q1_sound(game, entity->actor.id, "hknight/attack1.wav", "weapon", 1, 1);
}
static void boss_face(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  if (!monster->state->has_enemy || qa_q1_health(game, monster->state->enemy) <= 0 ||
      host_random(game) < 0.02) {
    size_t count = 0;
    const qa_actor_id *players = q1base_players(game, &count);
    long start = -1;
    if (monster->state->has_enemy)
      for (size_t i = 0; i < count; i++) if (qa_same_actor(players[i], monster->state->enemy)) { start = (long)i; break; }
    size_t limit = count < 4 ? count : 4;
    for (size_t i = 1; i <= limit; i++) {
      qa_actor_id candidate = players[(size_t)(start + (long)i) % count];
      if (qa_q1_health(game, candidate) > 0) { monster->state->has_enemy = true; monster->state->enemy = candidate; break; }
    }
  }
  qa_q1_base_monster_face(monster);
}
static void boss_missile(qa_q1_base_monster *monster, qa_vec3 offset) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target) || !monster->state->has_enemy) return;
  qa_actor_id enemy = monster->state->enemy;
  qa_vec3 m_origin = qa_q1_base_monster_origin(monster);
  qa_vec3 delta = qa_sub3(target, m_origin);
  qa_vec3 angles = V3(atan2(delta.z, hypot(delta.x, delta.y)) * 180 / 3.141592653589793,
                      atan2(delta.y, delta.x) * 180 / 3.141592653589793, 0);
  qa_q1_basis axes = qa_q1_make_vectors(game, angles);
  qa_vec3 origin = qa_add3(m_origin, qa_add3(qa_scale3(axes.forward, offset.x),
      qa_add3(qa_scale3(axes.right, offset.y), V3(0, 0, offset.z))));
  qa_q1_body enemy_body;
  qa_vec3 velocity = q1base_read_body(game, enemy, &enemy_body) ? enemy_body.velocity : ZERO3;
  qa_vec3 destination = target;
  if (qa_q1_option_skill(game) > 1) {
    qa_vec3 flat = velocity;
    flat.z = 0;
    destination = qa_add3(target, qa_scale3(flat, qa_length3(qa_sub3(target, origin)) / 300));
  }
  qa_q1_actor *missile = qa_q1_create_missile_lifetime(game, &entity->actor.id, "chthon_lavaball", "lavaball", origin, qa_scale3(qa_q1_normalize(qa_sub3(destination, origin)), 300), 6);
  missile->projectile = "rocket";
  missile->angular_velocity = V3(200, 100, 300);
  qa_q1_sound(game, entity->actor.id, "boss1/throw.wav", "weapon", 1, 1);
  if (qa_q1_health(game, enemy) <= 0) qa_q1_base_monster_play(monster, "boss_idle1");
}
typedef struct knight_offset { const char *name; double offset; } knight_offset;
static const knight_offset KNIGHT_OFFSETS[] = {
  {"hknight_magica7", -2}, {"hknight_magica8", -1}, {"hknight_magica9", 0},
  {"hknight_magica10", 1}, {"hknight_magica11", 2}, {"hknight_magica12", 3},
  {"hknight_magicb7", -2}, {"hknight_magicb8", -1}, {"hknight_magicb9", 0},
  {"hknight_magicb10", 1}, {"hknight_magicb11", 2}, {"hknight_magicb12", 3},
  {"hknight_magicc6", -2}, {"hknight_magicc7", -1}, {"hknight_magicc8", 0},
  {"hknight_magicc9", 1}, {"hknight_magicc10", 2}, {"hknight_magicc11", 3},
};
static bool starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}
static bool is_one(const char *name, const char *const *names, size_t count) {
  for (size_t i = 0; i < count; i++) if (strcmp(name, names[i]) == 0) return true;
  return false;
}

void qa_q1_monster_action(qa_q1_base_monster *monster, const char *name) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_monster *state = monster->state;
  if (qa_q1_grunt_action(monster, name)) return;
  for (size_t i = 0; i < sizeof KNIGHT_OFFSETS / sizeof *KNIGHT_OFFSETS; i++) {
    if (strcmp(name, KNIGHT_OFFSETS[i].name) == 0) { hell_knight_shot(monster, KNIGHT_OFFSETS[i].offset); return; }
  }
  if (starts_with(name, "boss_idle") || starts_with(name, "boss_missile")) {
    if (strcmp(name, "boss_idle1") == 0 && state->has_enemy && qa_q1_health(game, state->enemy) > 0) {
      qa_q1_base_monster_play(monster, "boss_missile1"); return;
    }
    if (strcmp(name, "boss_missile9") == 0) { boss_missile(monster, V3(100, 100, 200)); return; }
    if (strcmp(name, "boss_missile20") == 0) { boss_missile(monster, V3(100, -100, 200)); return; }
    boss_face(monster); return;
  }
  qa_vec3 origin = qa_q1_base_monster_origin(monster);
  qa_vec3 target;
  bool has_target = qa_q1_base_monster_target(monster, &target);
  if (strcmp(name, "knight_runatk1") == 0) {
    qa_q1_sound(game, entity->actor.id, host_random(game) > 0.5 ? "knight/sword2.wav" : "knight/sword1.wav", "weapon", 1, 1);
    qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 20); return;
  }
  if (strcmp(name, "enf_atk6") == 0 || strcmp(name, "enf_atk10") == 0) {
    if (!has_target) return;
    qa_q1_basis axes = qa_q1_base_monster_make_vectors(monster);
    qa_q1_effect(game, "muzzleflash", origin, &entity->actor.id, 1);
    qa_q1_sound(game, entity->actor.id, "enforcer/enfire.wav", "weapon", 1, 1);
    qa_vec3 laser_origin = qa_add3(origin, qa_add3(qa_scale3(axes.forward, 30),
        qa_add3(qa_scale3(axes.right, 8.5), V3(0, 0, 16))));
    (void)qa_q1_launch_laser(game, &entity->actor.id, laser_origin, qa_sub3(target, origin));
    if (bloody_nightmare(game)) {
      const double side = qa_q1_actor_number(entity, "ammo_shells") != 0 ? 0.05 : -0.05;
      (void)qa_q1_launch_laser_alt(game, &entity->actor.id, laser_origin,
                                   qa_add3(qa_q1_normalize(qa_sub3(target, origin)), qa_scale3(axes.right, side)));
      q1base_set_number_field(entity, "ammo_shells", 1 - qa_q1_actor_number(entity, "ammo_shells"));
    }
    return;
  }
  if (strcmp(name, "enf_atk14") == 0) {
    if (qa_q1_option_skill(game) == 3 && !state->refired && visible_enemy(monster)) {
      state->refired = true;
      set_name(monster->next_frame, sizeof monster->next_frame, "enf_atk1");
    }
    return;
  }
  if (strcmp(name, "enf_die3") == 0 || strcmp(name, "enf_fdie3") == 0) {
    qa_q1_backpack_contents contents = {.cells = 5};
    (void)qa_q1_drop_backpack(game, origin, &contents, NULL); return;
  }
  if (strcmp(name, "demon1_jump4") == 0) { jump(monster, false); return; }
  if (strcmp(name, "demon1_jump10") == 0) { qa_q1_base_monster_delay(monster, 3); return; }
  if (strcmp(name, "demon1_atta5") == 0 || strcmp(name, "demon1_atta11") == 0) {
    qa_q1_base_monster_face(monster);
    game->host.walk_move(game->host.ctx, entity->actor, entity->ideal_yaw, 12);
    if (state->has_enemy && qa_q1_base_monster_distance(monster) <= 100 &&
        qa_q1_can_damage(game, state->enemy, entity->actor.id)) {
      qa_q1_sound(game, entity->actor.id, "demon/dhit2.wav", "weapon", 1, 1);
      (void)qa_q1_damage(game, state->enemy, entity->actor.id, &entity->actor.id, 10 + 5 * host_random(game), NULL, "direct", "", NULL);
      meat(monster, strcmp(name, "demon1_atta5") == 0 ? 200 : -200);
    }
    return;
  }
  {
    static const char *const SWING[] = {"ogre_swing5","ogre_swing6","ogre_swing7","ogre_swing8","ogre_swing9","ogre_swing10","ogre_swing11"};
    if (is_one(name, SWING, 7)) {
      chainsaw(monster, strcmp(name, "ogre_swing6") == 0 ? 200 : strcmp(name, "ogre_swing10") == 0 ? -200 : 0);
      qa_q1_body body = qa_q1_get_body(game, entity);
      qa_vec3 angles = body.angles;
      angles.y = body.angles.y + host_random(game) * 25;
      qa_q1_body_patch patch = {0};
      patch.has_angles = true; patch.angles = angles;
      qa_q1_set_body(game, entity, &patch);
      return;
    }
  }
  {
    static const char *const SMASH[] = {"ogre_smash6","ogre_smash7","ogre_smash8","ogre_smash9"};
    if (is_one(name, SMASH, 4)) { chainsaw(monster, 0); return; }
  }
  if (strcmp(name, "ogre_smash10") == 0) { chainsaw(monster, 1); return; }
  if (strcmp(name, "ogre_smash11") == 0) { chainsaw(monster, 0); qa_q1_base_monster_delay(monster, 0.1 + host_random(game) * 0.2); return; }
  if (strcmp(name, "ogre_nail4") == 0) { qa_q1_launch_ogre_grenade(monster); return; }
  if (strcmp(name, "ogre_die3") == 0 || strcmp(name, "ogre_bdie3") == 0) {
    qa_q1_backpack_contents contents = {.rockets = 2};
    (void)qa_q1_drop_backpack(game, origin, &contents, NULL); return;
  }
  if (strcmp(name, "hknight_walk1") == 0) {
    if (host_random(game) < 0.2) qa_q1_sound_defaults(game, entity->actor.id, "hknight/idle.wav");
    qa_q1_base_monster_ai(monster, QA_Q1_AI_WALK, 2); return;
  }
  if (strcmp(name, "hknight_run1") == 0) {
    if (host_random(game) < 0.2) qa_q1_sound_defaults(game, entity->actor.id, "hknight/idle.wav");
    qa_q1_base_monster_ai(monster, QA_Q1_AI_RUN, 20);
    /* TS reads monster.target and monster.origin after ai("run") moved and
     * may have retargeted the monster. */
    qa_vec3 run_target;
    const bool run_has_target = qa_q1_base_monster_target(monster, &run_target);
    if (run_has_target && visible_enemy(monster) &&
        game->time >= state->attack_finished &&
        fabs(qa_q1_base_monster_origin(monster).z - run_target.z) <= 20 &&
        qa_q1_base_monster_distance(monster) >= 80) {
      qa_q1_base_monster_attack_finished(monster, 2);
      qa_q1_base_monster_play(monster, "hknight_char_a1");
    }
    return;
  }
  if (strcmp(name, "hknight_char_b1") == 0) {
    if (game->time > state->attack_finished) {
      qa_q1_base_monster_attack_finished(monster, 3);
      qa_q1_base_monster_play(monster, "hknight_run1");
    } else {
      qa_q1_sound(game, entity->actor.id, host_random(game) > 0.5 ? "knight/sword2.wav" : "knight/sword1.wav", "weapon", 1, 1);
    }
    qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 23);
    qa_q1_base_monster_ai(monster, QA_Q1_AI_MELEE, 0);
    return;
  }
  if (strcmp(name, "sham_smash10") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 0);
    if (state->has_enemy && qa_q1_base_monster_distance(monster) <= 100 &&
        qa_q1_can_damage(game, state->enemy, entity->actor.id)) {
      (void)qa_q1_base_monster_melee(monster, 100, 40, 3, false);
      qa_q1_sound_defaults(game, entity->actor.id, "shambler/smack.wav");
      for (int i = 0; i < 2; i++) {
        qa_q1_basis axes = game->basis;
        (void)qa_q1_spawn_meat_spray(game, entity, qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(axes.forward, 16)),
                                     qa_scale3(axes.right, (host_random(game) * 2 - 1) * 100));
      }
    }
    return;
  }
  if (strcmp(name, "sham_swingl7") == 0 || strcmp(name, "sham_swingr7") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 10);
    if (state->has_enemy && qa_q1_base_monster_distance(monster) <= 100) {
      (void)qa_q1_base_monster_melee(monster, 100, 20, 3, false);
      qa_q1_sound_defaults(game, entity->actor.id, "shambler/smack.wav");
      meat(monster, strcmp(name, "sham_swingl7") == 0 ? 250 : -250);
    }
    return;
  }
  if (strcmp(name, "sham_swingl9") == 0) {
    if (host_random(game) < 0.5) set_name(monster->next_frame, sizeof monster->next_frame, "sham_swingr1");
    return;
  }
  if (strcmp(name, "sham_swingr9") == 0) {
    if (host_random(game) < 0.5) set_name(monster->next_frame, sizeof monster->next_frame, "sham_swingl1");
    return;
  }
  if (strcmp(name, "sham_magic3") == 0) {
    qa_q1_base_monster_delay(monster, 0.3);
    qa_q1_effect(game, "muzzleflash", origin, &entity->actor.id, 1);
    qa_q1_base_monster_face(monster);
    qa_q1_actor *light = qa_q1_create(game, "shambler_light", NULL, NULL);
    entity->has_owner = true; entity->owner = light->actor.id;
    { char model_text[128]; (void)snprintf(model_text, sizeof model_text, "progs/s_light.mdl"); q1base_set_text(&light->model, model_text); }
    qa_q1_body_patch patch = {0};
    patch.has_origin = true; patch.origin = qa_q1_base_monster_origin(monster);
    patch.has_angles = true; patch.angles = qa_q1_get_body(game, entity).angles;
    qa_q1_set_body(game, light, &patch);
    qa_q1_link(game, light);
    qa_q1_schedule(game, light, 0.7, qa_q1_callback_registry_action(game->named, light, "SUB_Remove"));
    return;
  }
  if (strcmp(name, "sham_magic4") == 0 || strcmp(name, "sham_magic5") == 0) {
    qa_q1_effect(game, "muzzleflash", origin, &entity->actor.id, 1);
    qa_q1_actor *light = qa_q1_get_entity(game, (entity->has_owner ? &entity->owner : NULL));
    if (light != NULL) light->frame = strcmp(name, "sham_magic4") == 0 ? 1 : 2;
    return;
  }
  if (strcmp(name, "sham_magic6") == 0) {
    qa_q1_actor *light = qa_q1_get_entity(game, (entity->has_owner ? &entity->owner : NULL));
    if (light != NULL) qa_q1_remove(game, light);
    qa_q1_cast_lightning(monster);
    qa_q1_sound(game, entity->actor.id, "shambler/sboom.wav", "weapon", 1, 1);
    return;
  }
  if (strcmp(name, "sham_magic9") == 0 || strcmp(name, "sham_magic10") == 0) { qa_q1_cast_lightning(monster); return; }
  if (strcmp(name, "wiz_walk1") == 0 || strcmp(name, "wiz_run1") == 0 || strcmp(name, "wiz_side1") == 0) {
    double r = host_random(game) * 5;
    if (monster->idle_until < game->time) {
      monster->idle_until = game->time + 2;
      if (r > 4.5) qa_q1_sound(game, (entity)->actor.id, "wizard/widle1.wav", "voice", 2, 1);
      if (r < 1.5) qa_q1_sound(game, (entity)->actor.id, "wizard/widle2.wav", "voice", 2, 1);
    }
    return;
  }
  if (strcmp(name, "wiz_fast1") == 0) { wizard_fast(monster); return; }
  if (strcmp(name, "wiz_fast10") == 0) {
    qa_q1_base_monster_attack_finished(monster, 2);
    monster->sliding = range_enemy(monster) < 500 && visible_enemy(monster);
    set_name(monster->next_frame, sizeof monster->next_frame, monster->sliding ? "wiz_side1" : "wiz_run1");
    return;
  }
  if (strcmp(name, "wiz_death1") == 0) {
    qa_q1_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity.x = -200 + 400 * host_random(game); /* TS literal order */
    patch.velocity.y = -200 + 400 * host_random(game);
    patch.velocity.z = 100 + 100 * host_random(game);
    patch.has_ground = true; patch.ground = NULL;
    qa_q1_set_body(game, entity, &patch);
    qa_q1_sound_defaults(game, entity->actor.id, "wizard/wdeath.wav");
    return;
  }
  if (strcmp(name, "shal_attack9") == 0) { qa_q1_launch_vore_ball(monster); return; }
  if (strcmp(name, "tbaby_fly4") == 0) {
    monster->counter++;
    if (monster->counter == 4) qa_q1_base_monster_play(monster, "tbaby_jump5");
    return;
  }
  if (strcmp(name, "tbaby_jump5") == 0) { jump(monster, true); return; }
  if (strcmp(name, "tbaby_die1") == 0) { qa_q1_actor_set_damageable(entity, false); return; }
  if (strcmp(name, "tbaby_die2") == 0) {
    qa_q1_radius_damage(game, entity->actor.id, &entity->actor.id, 120, NULL, NULL, "");
    qa_q1_sound_defaults(game, entity->actor.id, "blob/death1.wav");
    qa_q1_effect(game, "tar-explosion", qa_sub3(qa_q1_base_monster_origin(monster), qa_scale3(qa_q1_normalize(qa_q1_get_body(game, entity).velocity), 8)), NULL, 1);
    qa_q1_remove(game, entity);
    return;
  }
  if (strcmp(name, "f_attack3") == 0 || strcmp(name, "f_attack9") == 0 || strcmp(name, "f_attack15") == 0) {
    if (state->has_enemy && qa_q1_base_monster_distance(monster) <= 60) {
      qa_q1_sound_defaults(game, entity->actor.id, "fish/bite.wav");
      (void)qa_q1_base_monster_melee(monster, 60, 3, 2, false);
    }
    return;
  }
  {
    static const char *const CRUC[] = {"zombie_cruc2","zombie_cruc3","zombie_cruc4","zombie_cruc5","zombie_cruc6"};
    if (is_one(name, CRUC, 5)) { qa_q1_base_monster_delay(monster, 0.1 + host_random(game) * 0.1); return; }
  }
  if (strcmp(name, "zombie_run1") == 0) { monster->in_pain = 0; return; }
  if (strcmp(name, "zombie_atta13") == 0) { qa_q1_launch_zombie_grenade(monster, V3(-10, -22, 30)); return; }
  if (strcmp(name, "zombie_attb14") == 0) { qa_q1_launch_zombie_grenade(monster, V3(-10, -24, 29)); return; }
  if (strcmp(name, "zombie_attc12") == 0) { qa_q1_launch_zombie_grenade(monster, V3(-12, -19, 29)); return; }
  if (strcmp(name, "zombie_paine1") == 0) { q1base_set_health(game, entity->actor, 60); return; }
  if (strcmp(name, "zombie_paine11") == 0) {
    q1base_set_health(game, entity->actor, 60);
    qa_q1_base_monster_delay(monster, 5.1); return;
  }
  if (strcmp(name, "zombie_paine12") == 0) {
    q1base_set_health(game, entity->actor, 60);
    qa_q1_sound(game, (entity)->actor.id, "zombie/z_idle.wav", "voice", 2, 1);
    entity->solid = "slidebox";
    if (!game->host.walk_move(game->host.ctx, entity->actor, 0, 0)) {
      set_name(monster->next_frame, sizeof monster->next_frame, "zombie_paine11");
      entity->solid = "none";
    }
    qa_q1_link(game, entity);
    return;
  }
  if (strcmp(name, "boss_death9") == 0) { qa_q1_effect(game, "lava-splash", origin, NULL, 1); return; }
  if (strcmp(name, "boss_death10") == 0) {
    qa_q1_base_monster_count_kill(monster);
    qa_q1_remove(game, entity);
    return;
  }
  if (strcmp(name, "old_thrash15") == 0) {
    monster->counter++;
    if (monster->counter != 3) set_name(monster->next_frame, sizeof monster->next_frame, "old_thrash1");
    return;
  }
  if (strcmp(name, "old_thrash20") == 0) {
    monster->services.finish_finale(monster->services.ctx, monster);
    return;
  }
  qa_range_error("Q1 frame action is not implemented: %s", name);
}
