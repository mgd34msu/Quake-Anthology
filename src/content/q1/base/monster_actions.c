/* Port of src/content/q1/base/monster-actions.ts. GPL-2.0-or-later. */
#include "qa/content/q1/base/monster_actions.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/content/q1/base/creatures.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

#define V3(x, y, z) ((qa_vec3){(x), (y), (z)})
static const qa_vec3 ZERO3 = {0, 0, 0};

static double host_random(qa_q1b_game *game) { return game->host->random(game->host->ctx); }
static void set_name(char *dst, size_t size, const char *src) {
  size_t length = strlen(src);
  if (length >= size) length = size - 1;
  memcpy(dst, src, length);
  dst[length] = '\0';
}
static const char *prefix_of(qa_q1_base_monster *monster) {
  return monster->has_source && monster->source.callback_prefix != NULL
             ? monster->source.callback_prefix : "base";
}

static void meat(qa_q1_base_monster *monster, double side) {
  qa_q1b_basis axes = qa_q1_base_monster_make_vectors(monster);
  qa_vec3 velocity = qa_scale3(axes.right, side == 1 ? (host_random(monster->game) * 2 - 1) * 100 : side);
  (void)qa_q1_spawn_meat_spray(monster->game, monster->entity,
      qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(axes.forward, 16)), velocity);
}
static void chainsaw(qa_q1_base_monster *monster, double side) {
  if (!monster->state.enemy.has ||
      !monster->game->can_damage(monster->game, monster->state.enemy.id, monster->entity->actor.id)) return;
  qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 10);
  if (qa_q1_base_monster_distance(monster) > 100) return;
  (void)qa_q1_base_monster_melee(monster, 100, 4, 3, false);
  if (side != 0) meat(monster, side);
}
static void jump(qa_q1_base_monster *monster, bool tar) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_q1b_body body = game->body(game, entity);
  if (tar) entity->movement = "bounce";
  monster->counter = 0;
  entity->movement_flags &= ~512;
  qa_q1b_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_add3(body.origin, V3(0, 0, 1));
  patch.has_velocity = true;
  patch.velocity = qa_add3(qa_scale3(qa_q1_base_monster_make_vectors(monster).forward, 600),
                           V3(0, 0, tar ? 200 + host_random(game) * 150 : 250));
  patch.has_ground = true; patch.ground = QA_Q1B_NO_ACTOR;
  game->set_body(game, entity, &patch);
  char name[128];
  (void)snprintf(name, sizeof name, "%s:monster_jump_touch", prefix_of(monster));
  entity->touch = game->named.touch(game->named.ctx, entity, name);
  game->link(game, entity);
}
void qa_q1_monster_jump_touch(qa_q1_base_monster *monster, qa_actor_id other) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  bool tar = strcmp(monster->spec->species, "tarbaby") == 0;
  if (game->health(game, entity->actor.id) <= 0) return;
  qa_combat_state combat;
  bool damageable = game->host->combat_read(game->host->ctx, other, &combat) && combat.can_take_damage;
  if (damageable && (!tar || strcmp(game->host->classname(game->host->ctx, other), entity->classname) != 0)) {
    if (qa_length3(game->body(game, entity).velocity) > 400) {
      qa_q1b_game_damage(game, other, entity->actor.id, qa_q1b_some_actor(entity->actor.id),
                         (tar ? 10 : 40) + 10 * host_random(game));
      if (tar) qa_q1b_game_sound_ch(game, entity, "blob/hit1.wav", qa_q1b_channel_named("weapon"));
    }
  } else if (tar) {
    qa_q1b_game_sound_ch(game, entity, "blob/land1.wav", qa_q1b_channel_named("weapon"));
  }
  if (game->host->check_bottom(game->host->ctx, entity->actor.id)) {
    entity->touch = (qa_q1b_touch){0};
    if (!tar) entity->movement = "step";
    set_name(monster->next_frame, sizeof monster->next_frame, tar ? "tbaby_jump1" : "demon1_jump11");
    qa_q1_base_monster_delay(monster, 0.1);
    return;
  }
  if (game->body(game, entity).ground.has) {
    entity->touch = (qa_q1b_touch){0};
    entity->movement = "step";
    set_name(monster->next_frame, sizeof monster->next_frame, tar ? "tbaby_run1" : "demon1_jump1");
    qa_q1_base_monster_delay(monster, 0.1);
  }
}
static void wizard_fast(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  if (!monster->state.enemy.has) return;
  qa_actor_id enemy = monster->state.enemy.id;
  qa_q1b_game_sound_ch(game, entity, "wizard/wattack.wav", qa_q1b_channel_named("weapon"));
  qa_q1b_basis axes = qa_q1_base_monster_make_vectors(monster);
  static const struct { double side; double delay; } SHOTS[] = {{1, 0.8}, {-1, 0.3}};
  for (size_t i = 0; i < 2; i++) {
    qa_q1b_actor *timer = game->create(game, "wizard_fastfire");
    timer->owner = qa_q1b_some_actor(entity->actor.id);
    qa_vec3 origin = qa_add3(qa_q1_base_monster_origin(monster),
        qa_add3(V3(0, 0, 30), qa_add3(qa_scale3(axes.forward, 14), qa_scale3(axes.right, 14 * SHOTS[i].side))));
    game->set_origin(game, timer, origin);
    qa_q1_wizard_shot shot = {enemy, qa_scale3(axes.right, SHOTS[i].side)};
    qa_q1_creatures_shots_set(qa_q1_creatures_for(game), timer->actor, shot);
    game->schedule(game, timer, SHOTS[i].delay, game->named.action(game->named.ctx, timer, "base:wizard_fastfire"));
  }
}
void qa_q1_wizard_fast_fire(qa_q1b_game *game, qa_q1b_actor *timer) {
  const qa_q1_wizard_shot *found = qa_q1_creatures_shots_get(qa_q1_creatures_for(game), timer->actor);
  if (found == NULL) qa_range_error("Wizard shot has no source target");
  qa_q1_wizard_shot shot = *found;
  qa_q1b_body target;
  bool has_target = game->host->bodies_read(game->host->ctx, shot.enemy, &target);
  qa_vec3 origin = game->body(game, timer).origin;
  qa_q1b_opt_actor owner = timer->owner;
  if (owner.has && game->health(game, owner.id) > 0 && has_target) {
    qa_q1b_body body;
    if (game->host->bodies_read(game->host->ctx, owner.id, &body))
      qa_q1b_game_effect_full(game, "muzzleflash", body.origin, owner, 1);
    qa_vec3 angles = target.angles;
    if (strcmp(game->options.edition, "rerelease") == 0) angles.x = -angles.x;
    (void)game->make_vectors(game, angles);
    qa_vec3 direction = qa_q1b_normalize(qa_sub3(qa_sub3(target.origin, qa_scale3(shot.right, 13)), origin));
    qa_q1b_game_sound_ch(game, timer, "wizard/wattack.wav", qa_q1b_channel_named("weapon"));
    (void)qa_q1_launch_spike(game, owner, origin, qa_scale3(direction, 600), "wizard");
  }
  game->remove(game, timer);
}
static void hell_knight_shot(qa_q1_base_monster *monster, double offset) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target)) return;
  qa_vec3 origin0 = qa_q1_base_monster_origin(monster);
  qa_vec3 delta = qa_sub3(target, origin0);
  qa_vec3 angle = V3(atan2(delta.z, hypot(delta.x, delta.y)) * 180 / 3.141592653589793,
                     atan2(delta.y, delta.x) * 180 / 3.141592653589793 + offset * 6, 0);
  qa_vec3 forward = game->make_vectors(game, angle).forward;
  qa_q1b_body body = game->body(game, entity);
  qa_vec3 origin = qa_add3(qa_add3(origin0, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5)),
                           qa_scale3(forward, 20));
  qa_vec3 direction = qa_q1b_normalize(forward);
  qa_vec3 velocity_dir = direction;
  velocity_dir.z = -direction.z + (host_random(game) - 0.5) * 0.1;
  (void)qa_q1_launch_spike(game, qa_q1b_some_actor(entity->actor.id), origin, qa_scale3(velocity_dir, 300), "knight");
  qa_q1b_game_sound_ch(game, entity, "hknight/attack1.wav", qa_q1b_channel_named("weapon"));
}
static void boss_face(qa_q1_base_monster *monster) {
  qa_q1b_game *game = monster->game;
  if (!monster->state.enemy.has || game->health(game, monster->state.enemy.id) <= 0 ||
      host_random(game) < 0.02) {
    qa_actor_id players[64];
    size_t count = game->host->players(game->host->ctx, players, 64);
    long start = -1;
    if (monster->state.enemy.has)
      for (size_t i = 0; i < count; i++) if (qa_same_actor(players[i], monster->state.enemy.id)) { start = (long)i; break; }
    size_t limit = count < 4 ? count : 4;
    for (size_t i = 1; i <= limit; i++) {
      qa_actor_id candidate = players[(size_t)(start + (long)i) % count];
      if (game->health(game, candidate) > 0) { monster->state.enemy = qa_q1b_some_actor(candidate); break; }
    }
  }
  qa_q1_base_monster_face(monster);
}
static void boss_missile(qa_q1_base_monster *monster, qa_vec3 offset) {
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target) || !monster->state.enemy.has) return;
  qa_actor_id enemy = monster->state.enemy.id;
  qa_vec3 m_origin = qa_q1_base_monster_origin(monster);
  qa_vec3 delta = qa_sub3(target, m_origin);
  qa_vec3 angles = V3(atan2(delta.z, hypot(delta.x, delta.y)) * 180 / 3.141592653589793,
                      atan2(delta.y, delta.x) * 180 / 3.141592653589793, 0);
  qa_q1b_basis axes = game->make_vectors(game, angles);
  qa_vec3 origin = qa_add3(m_origin, qa_add3(qa_scale3(axes.forward, offset.x),
      qa_add3(qa_scale3(axes.right, offset.y), V3(0, 0, offset.z))));
  qa_q1b_body enemy_body;
  qa_vec3 velocity = game->host->bodies_read(game->host->ctx, enemy, &enemy_body) ? enemy_body.velocity : ZERO3;
  qa_vec3 destination = target;
  if (game->options.skill > 1) {
    qa_vec3 flat = velocity;
    flat.z = 0;
    destination = qa_add3(target, qa_scale3(flat, qa_length3(qa_sub3(target, origin)) / 300));
  }
  qa_q1b_actor *missile = qa_q1_create_missile_lifetime(
      game, qa_q1b_some_actor(entity->actor.id), "chthon_lavaball", "lavaball", origin,
      qa_scale3(qa_q1b_normalize(qa_sub3(destination, origin)), 300), 6);
  missile->projectile = "rocket";
  missile->angular_velocity = V3(200, 100, 300);
  qa_q1b_game_sound_ch(game, entity, "boss1/throw.wav", qa_q1b_channel_named("weapon"));
  if (game->health(game, enemy) <= 0) qa_q1_base_monster_play(monster, "boss_idle1");
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
  qa_q1b_game *game = monster->game;
  qa_q1b_actor *entity = monster->entity;
  qa_q1b_monster_state *state = &monster->state;
  for (size_t i = 0; i < sizeof KNIGHT_OFFSETS / sizeof *KNIGHT_OFFSETS; i++) {
    if (strcmp(name, KNIGHT_OFFSETS[i].name) == 0) { hell_knight_shot(monster, KNIGHT_OFFSETS[i].offset); return; }
  }
  if (starts_with(name, "boss_idle") || starts_with(name, "boss_missile")) {
    if (strcmp(name, "boss_idle1") == 0 && state->enemy.has && game->health(game, state->enemy.id) > 0) {
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
    qa_q1b_game_sound_ch(game, entity, host_random(game) > 0.5 ? "knight/sword2.wav" : "knight/sword1.wav", qa_q1b_channel_named("weapon"));
    qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 20); return;
  }
  if (strcmp(name, "enf_atk6") == 0 || strcmp(name, "enf_atk10") == 0) {
    if (!has_target) return;
    qa_q1b_basis axes = qa_q1_base_monster_make_vectors(monster);
    qa_q1b_game_effect_full(game, "muzzleflash", origin, qa_q1b_some_actor(entity->actor.id), 1);
    qa_q1b_game_sound_ch(game, entity, "enforcer/enfire.wav", qa_q1b_channel_named("weapon"));
    qa_vec3 laser_origin = qa_add3(origin, qa_add3(qa_scale3(axes.forward, 30),
        qa_add3(qa_scale3(axes.right, 8.5), V3(0, 0, 16))));
    (void)qa_q1_launch_laser(game, qa_q1b_some_actor(entity->actor.id), laser_origin, qa_sub3(target, origin));
    return;
  }
  if (strcmp(name, "enf_atk14") == 0) {
    if (game->options.skill == 3 && !state->refired && qa_q1_base_monster_visible(monster, QA_Q1B_NO_ACTOR)) {
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
    game->host->walk_move(game->host->ctx, entity->actor, entity->ideal_yaw, 12);
    if (state->enemy.has && qa_q1_base_monster_distance(monster) <= 100 &&
        game->can_damage(game, state->enemy.id, entity->actor.id)) {
      qa_q1b_game_sound_ch(game, entity, "demon/dhit2.wav", qa_q1b_channel_named("weapon"));
      qa_q1b_game_damage(game, state->enemy.id, entity->actor.id, qa_q1b_some_actor(entity->actor.id),
                         10 + 5 * host_random(game));
      meat(monster, strcmp(name, "demon1_atta5") == 0 ? 200 : -200);
    }
    return;
  }
  {
    static const char *const SWING[] = {"ogre_swing5","ogre_swing6","ogre_swing7","ogre_swing8","ogre_swing9","ogre_swing10","ogre_swing11"};
    if (is_one(name, SWING, 7)) {
      chainsaw(monster, strcmp(name, "ogre_swing6") == 0 ? 200 : strcmp(name, "ogre_swing10") == 0 ? -200 : 0);
      qa_q1b_body body = game->body(game, entity);
      qa_vec3 angles = body.angles;
      angles.y = body.angles.y + host_random(game) * 25;
      qa_q1b_body_patch patch = {0};
      patch.has_angles = true; patch.angles = angles;
      game->set_body(game, entity, &patch);
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
    if (host_random(game) < 0.2) qa_q1b_game_sound(game, entity, "hknight/idle.wav");
    qa_q1_base_monster_ai(monster, QA_Q1_AI_WALK, 2); return;
  }
  if (strcmp(name, "hknight_run1") == 0) {
    if (host_random(game) < 0.2) qa_q1b_game_sound(game, entity, "hknight/idle.wav");
    qa_q1_base_monster_ai(monster, QA_Q1_AI_RUN, 20);
    if (has_target && qa_q1_base_monster_visible(monster, QA_Q1B_NO_ACTOR) &&
        game->time >= state->attack_finished && fabs(origin.z - target.z) <= 20 &&
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
      qa_q1b_game_sound_ch(game, entity, host_random(game) > 0.5 ? "knight/sword2.wav" : "knight/sword1.wav", qa_q1b_channel_named("weapon"));
    }
    qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 23);
    qa_q1_base_monster_ai(monster, QA_Q1_AI_MELEE, 0);
    return;
  }
  if (strcmp(name, "sham_smash10") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 0);
    if (state->enemy.has && qa_q1_base_monster_distance(monster) <= 100 &&
        game->can_damage(game, state->enemy.id, entity->actor.id)) {
      (void)qa_q1_base_monster_melee(monster, 100, 40, 3, false);
      qa_q1b_game_sound(game, entity, "shambler/smack.wav");
      for (int i = 0; i < 2; i++) {
        qa_q1b_basis axes = game->basis;
        (void)qa_q1_spawn_meat_spray(game, entity, qa_add3(origin, qa_scale3(axes.forward, 16)),
                                     qa_scale3(axes.right, (host_random(game) * 2 - 1) * 100));
      }
    }
    return;
  }
  if (strcmp(name, "sham_swingl7") == 0 || strcmp(name, "sham_swingr7") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 10);
    if (state->enemy.has && qa_q1_base_monster_distance(monster) <= 100) {
      (void)qa_q1_base_monster_melee(monster, 100, 20, 3, false);
      qa_q1b_game_sound(game, entity, "shambler/smack.wav");
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
    qa_q1b_game_effect_full(game, "muzzleflash", origin, qa_q1b_some_actor(entity->actor.id), 1);
    qa_q1_base_monster_face(monster);
    qa_q1b_actor *light = game->create(game, "shambler_light");
    entity->owner = qa_q1b_some_actor(light->actor.id);
    (void)snprintf(light->model, sizeof light->model, "progs/s_light.mdl");
    qa_q1b_body_patch patch = {0};
    patch.has_origin = true; patch.origin = origin;
    patch.has_angles = true; patch.angles = game->body(game, entity).angles;
    game->set_body(game, light, &patch);
    game->link(game, light);
    game->schedule(game, light, 0.7, game->named.action(game->named.ctx, light, "SUB_Remove"));
    return;
  }
  if (strcmp(name, "sham_magic4") == 0 || strcmp(name, "sham_magic5") == 0) {
    qa_q1b_game_effect_full(game, "muzzleflash", origin, qa_q1b_some_actor(entity->actor.id), 1);
    qa_q1b_actor *light = game->entity(game, entity->owner);
    if (light != NULL) light->frame = strcmp(name, "sham_magic4") == 0 ? 1 : 2;
    return;
  }
  if (strcmp(name, "sham_magic6") == 0) {
    qa_q1b_actor *light = game->entity(game, entity->owner);
    if (light != NULL) game->remove(game, light);
    qa_q1_cast_lightning(monster);
    qa_q1b_game_sound_ch(game, entity, "shambler/sboom.wav", qa_q1b_channel_named("weapon"));
    return;
  }
  if (strcmp(name, "sham_magic9") == 0 || strcmp(name, "sham_magic10") == 0) { qa_q1_cast_lightning(monster); return; }
  if (strcmp(name, "wiz_walk1") == 0 || strcmp(name, "wiz_run1") == 0 || strcmp(name, "wiz_side1") == 0) {
    double r = host_random(game) * 5;
    if (monster->idle_until < game->time) {
      monster->idle_until = game->time + 2;
      if (r > 4.5) game->sound(game, entity, "wizard/widle1.wav", qa_q1b_channel_named("voice"), 2, 1);
      if (r < 1.5) game->sound(game, entity, "wizard/widle2.wav", qa_q1b_channel_named("voice"), 2, 1);
    }
    return;
  }
  if (strcmp(name, "wiz_fast1") == 0) { wizard_fast(monster); return; }
  if (strcmp(name, "wiz_fast10") == 0) {
    qa_q1_base_monster_attack_finished(monster, 2);
    monster->sliding = qa_q1_base_monster_range_distance(monster, QA_Q1B_NO_ACTOR) < 500 &&
                       qa_q1_base_monster_visible(monster, QA_Q1B_NO_ACTOR);
    set_name(monster->next_frame, sizeof monster->next_frame, monster->sliding ? "wiz_side1" : "wiz_run1");
    return;
  }
  if (strcmp(name, "wiz_death1") == 0) {
    qa_q1b_body_patch patch = {0};
    patch.has_velocity = true;
    patch.velocity = V3(-200 + 400 * host_random(game), -200 + 400 * host_random(game), 100 + 100 * host_random(game));
    patch.has_ground = true; patch.ground = QA_Q1B_NO_ACTOR;
    game->set_body(game, entity, &patch);
    qa_q1b_game_sound(game, entity, "wizard/wdeath.wav");
    return;
  }
  if (strcmp(name, "shal_attack9") == 0) { qa_q1_launch_vore_ball(monster); return; }
  if (strcmp(name, "tbaby_fly4") == 0) {
    monster->counter++;
    if (monster->counter == 4) qa_q1_base_monster_play(monster, "tbaby_jump5");
    return;
  }
  if (strcmp(name, "tbaby_jump5") == 0) { jump(monster, true); return; }
  if (strcmp(name, "tbaby_die1") == 0) { qa_q1b_actor_set_damageable(game, entity, false); return; }
  if (strcmp(name, "tbaby_die2") == 0) {
    game->radius_damage(game, entity->actor.id, qa_q1b_some_actor(entity->actor.id), 120, QA_Q1B_NO_ACTOR, NULL, "");
    qa_q1b_game_sound(game, entity, "blob/death1.wav");
    qa_q1b_game_effect(game, "tar-explosion",
        qa_sub3(origin, qa_scale3(qa_q1b_normalize(game->body(game, entity).velocity), 8)));
    game->remove(game, entity);
    return;
  }
  if (strcmp(name, "f_attack3") == 0 || strcmp(name, "f_attack9") == 0 || strcmp(name, "f_attack15") == 0) {
    if (state->enemy.has && qa_q1_base_monster_distance(monster) <= 60) {
      qa_q1b_game_sound(game, entity, "fish/bite.wav");
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
  if (strcmp(name, "zombie_paine1") == 0) { game->host->combat_set_health(game->host->ctx, entity->actor, 60); return; }
  if (strcmp(name, "zombie_paine11") == 0) {
    game->host->combat_set_health(game->host->ctx, entity->actor, 60);
    qa_q1_base_monster_delay(monster, 5.1); return;
  }
  if (strcmp(name, "zombie_paine12") == 0) {
    game->host->combat_set_health(game->host->ctx, entity->actor, 60);
    game->sound(game, entity, "zombie/z_idle.wav", qa_q1b_channel_named("voice"), 2, 1);
    entity->solid = "slidebox";
    if (!game->host->walk_move(game->host->ctx, entity->actor, 0, 0)) {
      set_name(monster->next_frame, sizeof monster->next_frame, "zombie_paine11");
      entity->solid = "none";
    }
    game->link(game, entity);
    return;
  }
  if (strcmp(name, "boss_death9") == 0) { qa_q1b_game_effect(game, "lava-splash", origin); return; }
  if (strcmp(name, "boss_death10") == 0) {
    qa_q1_base_monster_count_kill(monster);
    game->remove(game, entity);
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
