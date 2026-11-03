/* Port of src/content/q1/addons/monsters/bosses/final.ts.
 * quakec_mg3/monsters/boss_final.qc. Copyright (C) 1996-2026 id Software LLC. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/final.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/addons/monsters/bosses/effects.h"
#include "qa/content/q1/addons/monsters/bosses/frames/final.h"
#include "qa/content/q1/addons/monsters/bosses/oldnew.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/content/q1/addons/monsters/bosses/sphere_points.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/content/q1/missionpacks/types.h"
#include "../support.h"

#define PREFIX "mg3:final"
#define FR QA_Q1AM_FR
#define FLAGS QA_Q1AM_FLAGS

static const char *const final_classnames[] = {"monster_boss_final"};
static const qa_q1_base_monster_species final_spec = {
  .species = "boss", .classnames = final_classnames, .classname_count = 1, .model = "boss", .head = NULL,
  .health = 12000, .gib_health = -INFINITY, .gibs = NULL, .gib_count = 0,
  .bounds = {{-128, -128, -24}, {128, 128, 256}}, .stand = "boss_final_idle1", .walk = "boss_final_idle1",
  .run = "boss_final_missile1", .sight = "", .missile = "boss_final_missile1", .melee = false, .movement = "boss",
};

static void final_spawn(qa_q1_base_monster *monster);
static void final_use(qa_q1_base_monster *monster, const qa_actor_id *activator);
static void final_awake(qa_q1_base_monster *monster, const qa_actor_id *activator);
static void final_play(qa_q1_base_monster *monster, const char *name);
static void final_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
static void final_die(qa_q1_base_monster *monster, const qa_actor_id *attacker);
static void final_destroy(qa_q1_base_monster *monster) { free(monster); }
static const qa_q1_base_monster_ops final_ops = {
  .spawn = final_spawn, .use = final_use, .awake = final_awake, .play = final_play, .pain = final_pain,
  .die = final_die, .destroy = final_destroy,
};
bool qa_q1am_is_final_boss(const qa_q1_base_monster *monster) { return monster->ops == &final_ops; }
static qa_q1am_final_boss *final_of(qa_q1_base_monster *monster) { return (qa_q1am_final_boss *)monster; }

static void final_frame_action(void *ctx, qa_q1_base_monster *monster) {
  if (!qa_q1am_is_final_boss(monster)) qa_range_error("Final boss callback received another source controller");
  qa_q1am_final_boss_action(final_of(monster), ctx);
}
static void final_decide(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  if (!qa_q1am_is_final_boss(monster)) qa_range_error("Final boss decision received another source controller");
  qa_q1_base_monster_play(monster, "boss_final_decide");
}
/* TS actions: the frame actions, then actions.set("boss_final_decide", ...). */
static const qa_q1_monster_action_entry *final_actions(size_t *count) {
  static qa_q1_monster_action_entry *entries;
  static size_t entry_count;
  if (entries == NULL) {
    size_t frame_count = 0;
    const qa_q1_monster_action_entry *frame = qa_q1am_frame_actions(&qa_q1am_final_boss_frames, final_frame_action, &frame_count);
    entries = malloc((frame_count + 1) * sizeof *entries);
    if (entries == NULL) qa_range_error("out of memory");
    memcpy(entries, frame, frame_count * sizeof *entries);
    entry_count = frame_count;
    size_t slot = entry_count;
    for (size_t i = 0; i < frame_count; i++)
      if (strcmp(entries[i].name, "boss_final_decide") == 0) slot = i;
    entries[slot] = (qa_q1_monster_action_entry){"boss_final_decide", final_decide, NULL};
    if (slot == entry_count) entry_count++;
  }
  *count = entry_count;
  return entries;
}

static void later(qa_q1ad_context *context, qa_q1_actor *entity, const char *name, double delay) {
  char full[96];
  (void)snprintf(full, sizeof full, PREFIX ":%s", name);
  qa_q1_schedule(context->game, entity, delay, qa_q1_callback_registry_action(context->game->named, entity, full));
}
static double random_signed(qa_q1ad_context *context) { return FR(2 * qa_q1am_random(context->game) - 1); }
static void spin(qa_q1ad_context *context, qa_q1_actor *shot) {
  qa_vec3 angular;
  angular.x = 300 * random_signed(context);
  angular.y = 300 * random_signed(context);
  angular.z = 300 * random_signed(context);
  shot->angular_velocity = angular;
}
static qa_q1_actor *rock(qa_q1ad_context *context, const qa_actor_id *owner, qa_vec3 origin, qa_vec3 direction,
                         const char *model) {
  qa_q1_services *game = context->game;
  qa_q1_actor *shot = qa_q1_launch_spike(game, owner, origin, direction, "spike");
  qa_q1ad_set_text(&shot->classname, "rock");
  char path[64];
  (void)snprintf(path, sizeof path, "progs/rogue/%s.mdl", model);
  qa_q1ad_set_text(&shot->model, path);
  shot->touch = qa_q1_callback_registry_touch(game->named, shot, PREFIX ":T_RockTouch2");
  qa_q1_set_bounds(game, shot, QA_Q1_POINT);
  return shot;
}
static double enemy_health(qa_q1_base_monster *monster) {
  return monster->state->has_enemy ? qa_q1_health(monster->game, monster->state->enemy) : 0;
}
static qa_vec3 target_or_zero(qa_q1_base_monster *monster) {
  qa_vec3 target = QA_Q1_ZERO;
  (void)qa_q1_base_monster_target(monster, &target);
  return target;
}

static void final_spawn(qa_q1_base_monster *monster) {
  qa_q1ad_context *context = final_of(monster)->context;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
  qa_q1ad_set_text(&entity->classname, "monster_boss");
  qa_q1ad_set_number(context, entity, "boss_immune", 1);
  entity->movement_flags = 32;
  qa_combat_trait_changes traits;
  memset(&traits, 0, sizeof traits);
  traits.has_team = true;
  traits.team = "q1:monsters";
  qa_gameplay_authority_set_traits(game->host.combat, entity->actor, traits);
  game->total_monsters++;
  qa_q1ad_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q1AD_EVENT_MONSTER_COUNT;
  event.count = game->total_monsters;
  context->services.emit(context->services.ctx, &event);
  qa_q1ad_set_number(context, entity, "frags", 0);
  entity->use = qa_q1_callback_registry_use(game->named, entity, PREFIX ":boss_awake");
}
static void final_use(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_actor_id resolved;
  const bool has = qa_q1am_mg3_monster_activator(monster->game, activator, &resolved);
  qa_q1_base_monster_use_super(monster, has ? &resolved : NULL);
}
static void final_awake(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  entity->solid = "slidebox";
  entity->movement = "step";
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  qa_q1ad_set_text(&entity->model, "progs/boss.mdl");
  monster->state->attack_finished = game->time + 3;
  qa_q1_set_bounds(game, entity, final_spec.bounds);
  qa_q1_base_monster_set_enemy(monster, activator);
  entity->die = qa_q1_callback_registry_die(game->named, entity, PREFIX ":monster_die");
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, PREFIX ":monster_pain");
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 12000);
  entity->max_health = 12000;
  entity->use = qa_q1_callback_registry_use(game->named, entity, PREFIX ":monster_use");
  entity->yaw_speed = 20;
  qa_q1_effect(game, "lava-splash", qa_q1_base_monster_origin(monster), NULL, 1);
  qa_q1_base_monster_play(monster, "boss_final_rise1");
}
static void final_play(qa_q1_base_monster *monster, const char *name) {
  if (strcmp(name, "boss_final_decide") == 0) {
    qa_q1_base_monster_play_super(monster, monster->entity->wait > 0 && qa_q1am_random(monster->game) < 0.3
                                               ? "boss_final_mg1" : "boss_final_missile1");
    return;
  }
  qa_q1_base_monster_play_super(monster, name);
}
static int compare_slots(const void *a, const void *b, void *ctx) {
  (void)ctx;
  const int64_t left = ((const qa_actor_id *)a)->slot, right = ((const qa_actor_id *)b)->slot;
  return left < right ? -1 : left > right ? 1 : 0;
}
void qa_q1am_final_boss_face(qa_q1am_final_boss *boss) {
  qa_q1_base_monster *monster = &boss->base;
  qa_q1_services *game = monster->game;
  if (enemy_health(monster) <= 0 || qa_q1am_random(game) < 0.02) {
    size_t count = 0;
    const qa_actor_id *host_players = game->host.players(game->host.ctx, &count);
    qa_actor_id *players = count == 0 ? NULL : malloc(count * sizeof *players);
    if (count != 0 && players == NULL) qa_range_error("out of memory");
    if (count != 0) memcpy(players, host_players, count * sizeof *players);
    qa_js_stable_sort(players, count, sizeof *players, compare_slots, NULL);
    const int64_t after = monster->state->has_enemy ? monster->state->enemy.slot : 0;
    const qa_actor_id *chosen = NULL;
    for (size_t i = 0; i < count && chosen == NULL; i++)
      if (players[i].slot > after) chosen = &players[i];
    if (chosen == NULL && count > 0) chosen = &players[0];
    qa_q1_base_monster_set_enemy(monster, chosen);
    free(players);
  }
  if (enemy_health(monster) > 0) qa_q1_base_monster_face(monster);
}
static void final_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)damage; /* TS pain(attacker, _damage) */
  qa_q1_base_monster_retaliate(monster, attacker);
  qa_q1ad_context *context = final_of(monster)->context;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (monster->state->attack_finished > game->time || monster->state->pain_finished > game->time ||
      qa_q1_actor_number(entity, "boss_immune") != 0)
    return;
  const char *event = "";
  double frags = qa_q1_actor_number(entity, "frags");
  if (frags == 0) { event = "boss_final_shocka1"; frags = 1; }
  static const struct { double step, fraction; const char *frame; } steps[] = {
    {1, 0.83, "boss_final_shocka1"}, {2, 0.66, "boss_final_shocka1"}, {3, 0.5, "boss_final_shockb1"},
    {4, 0.33, "boss_final_shocka1"}, {5, 0.16, "boss_final_shockb1"},
  };
  for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++)
    if (frags == steps[i].step && qa_q1_health(game, entity->actor.id) < FR(entity->max_health * FR(steps[i].fraction))) {
      event = steps[i].frame;
      frags++;
    }
  qa_q1ad_set_number(context, entity, "frags", frags);
  if (event[0] == '\0') return;
  qa_q1ad_set_number(context, entity, "boss_immune", 1);
  qa_q1_sound(game, entity->actor.id, "boss1/pain.wav", "weapon", 1, 1);
  monster->state->pain_finished = game->time + 3;
  qa_q1_base_monster_play(monster, event);
}
static void final_die(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_set_enemy(monster, attacker);
  qa_q1_actor_set_damageable(entity, false);
  memset(&entity->touch, 0, sizeof entity->touch);
  entity->movement_flags = FLAGS(entity->movement_flags) & ~3;
  const qa_actor_id *enemy = qa_q1_base_monster_enemy(monster);
  qa_actor_id copy;
  if (enemy != NULL) { copy = *enemy; enemy = &copy; }
  qa_q1_use_targets(monster->game, entity, enemy);
  qa_q1_base_monster_play(monster, "boss_final_death1");
}
static bool ends_with(const char *text, const char *suffix) {
  const size_t length = strlen(text), size = strlen(suffix);
  return length >= size && strcmp(text + length - size, suffix) == 0;
}
static void set_next(qa_q1_base_monster *monster, const char *frame) {
  qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, frame);
}
static qa_actor_id *activator_copy(const qa_q1_actor *entity, qa_actor_id *storage) {
  if (!entity->has_activator) return NULL;
  *storage = entity->activator;
  return storage;
}
void qa_q1am_final_boss_action(qa_q1am_final_boss *boss, const char *name) {
  qa_q1_base_monster *monster = &boss->base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_context *context = boss->context;
  if (strncmp(name, "boss_final_idle", 15) == 0) {
    qa_q1am_final_boss_face(boss);
    if (strcmp(name, "boss_final_idle31") == 0)
      qa_q1_base_monster_play(monster, monster->state->has_enemy ? "boss_final_missile1" : "boss_final_idle1");
    return;
  }
  if (strncmp(name, "boss_final_shock", 16) == 0) {
    if (ends_with(name, "10")) qa_q1am_final_boss_upgrade(boss);
    else qa_q1am_pain_lightning(context, entity, (qa_vec3){0, 0, 100});
    return;
  }
  if (strcmp(name, "boss_final_rise1") == 0) {
    qa_q1_sound(game, entity->actor.id, "boss1/out1.wav", "weapon", 1, 1);
  } else if (strcmp(name, "boss_final_rise2") == 0) {
    qa_q1_sound(game, entity->actor.id, "boss1/sight1.wav", "voice", 1, 1);
  } else if (strcmp(name, "boss_final_mg7") == 0) {
    const double shells = FR(FR((qa_q1_option_skill(game) + 3) / 6) * 30) + floor(random_signed(context) * 10 + 0.5);
    qa_q1ad_set_number(context, entity, "ammo_shells", shells);
    qa_q1ad_set_number(context, entity, "ammo_nails", shells);
    if (qa_q1_option_skill(game) > 0) monster->state->attack_finished = game->time + 5;
    qa_q1am_final_boss_face(boss);
  } else if (strcmp(name, "boss_final_mg8") == 0) {
    const double nails = qa_q1_actor_number(entity, "ammo_nails");
    const bool wide = fmod(nails, 2) == 0;
    double spread = FR(1 - FR(nails / qa_q1_actor_number(entity, "ammo_shells")));
    spread = FR(spread * spread);
    qa_q1am_final_boss_blast(boss, (qa_vec3){270, 60, 210}, wide ? spread : spread * 0.5, wide);
    if (nails != 0) set_next(monster, "boss_final_mg8");
    qa_q1ad_set_number(context, entity, "ammo_nails", nails - 1);
    qa_q1am_final_boss_face(boss);
  } else if (strcmp(name, "boss_final_missile1") == 0) {
    qa_q1ad_set_number(context, entity, "boss_immune", 0);
    qa_q1am_final_boss_face(boss);
  } else if (strcmp(name, "boss_final_missile10") == 0) {
    qa_q1am_final_boss_missile(boss, (qa_vec3){200, 100, 60});
  } else if (strcmp(name, "boss_final_missile21") == 0) {
    qa_q1am_final_boss_missile(boss, (qa_vec3){200, -100, 60});
  } else if (strcmp(name, "boss_final_death1") == 0) {
    qa_q1_sound(game, entity->actor.id, "boss1/death.wav", "voice", 1, 1);
    static const double delays[] = {1, 3, 5};
    for (size_t i = 0; i < 3; i++) {
      qa_q1_actor *timer = qa_q1_create(game, "circle_thinker", NULL, NULL);
      qa_q1ad_set_text(&timer->classname, "");
      qa_q1_set_origin(game, timer, qa_add3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 60}));
      later(context, timer, "circle_think", delays[i]);
    }
  } else if (strcmp(name, "boss_final_death9") == 0) {
    qa_q1_sound(game, entity->actor.id, "boss1/out1.wav", "body", 1, 1);
    qa_q1_effect(game, "lava-splash", qa_q1_base_monster_origin(monster), NULL, 1);
  } else if (strcmp(name, "boss_final_death10") == 0) {
    game->killed_monsters++;
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "monster-killed";
    event.has_actor = true;
    event.actor = entity->actor.id;
    event.total = game->total_monsters;
    event.found = game->killed_monsters;
    qa_q1_emit(game, event);
    qa_q1ad_set_number(context, entity, "frags", qa_q1_actor_number(entity, "frags") + 1);
    while (qa_q1_actor_number(entity, "frags") < 5) {
      qa_actor_id storage;
      qa_q1_use_targets(game, entity, activator_copy(entity, &storage));
      qa_q1ad_set_number(context, entity, "frags", qa_q1_actor_number(entity, "frags") + 1);
    }
    qa_q1_sound(game, entity->actor.id, "boss2/pop2.wav", "voice", 1, 1);
    for (int i = 0; i < 100; i++) {
      const qa_vec3 point = qa_q1am_sphere_point(i);
      if (point.z > 0)
        (void)qa_q1am_throw_gib_vector(context, qa_add3(qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(point, 64)),
                                                        (qa_vec3){0, 0, 48}), point);
    }
    qa_q1_actor *timer = qa_q1_create(game, "boss_end_timer", NULL, NULL);
    qa_q1ad_set_text(&timer->classname, "");
    qa_q1_schedule(game, timer, 8, qa_q1_callback_registry_action(game->named, timer, "mg3:bosses:boss_end"));
    qa_q1_remove(game, entity);
  } else if (strncmp(name, "boss_final_missile", 18) == 0 || strncmp(name, "boss_final_mg", 13) == 0) {
    qa_q1am_final_boss_face(boss);
  } else {
    qa_range_error("Unknown final boss action %s", name);
  }
}
void qa_q1am_final_boss_upgrade(qa_q1am_final_boss *boss) {
  qa_q1_base_monster *monster = &boss->base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_context *context = boss->context;
  entity->wait++;
  qa_q1ad_set_number(context, entity, "boss_immune", 0);
  const char *target = entity->wait == 2 ? qa_q1_actor_text(entity, "wave1")
                     : entity->wait == 3 ? qa_q1_actor_text(entity, "tele_target")
                     : entity->wait == 4 ? qa_q1_actor_text(entity, "wave2")
                     : entity->wait == 5 ? qa_q1_actor_text(entity, "wave3") : NULL;
  if (entity->wait == 3) {
    entity->frame = 0;
    qa_q1_schedule(game, entity, 0, qa_q1_callback_registry_action(game->named, entity, "SUB_Null"));
    qa_q1ad_set_number(context, entity, "boss_immune", 1);
  }
  if (target != NULL) {
    char *original = entity->target;
    entity->target = NULL;
    qa_q1ad_set_text(&entity->target, target);
    if (entity->target == NULL) qa_range_error("out of memory");
    qa_actor_id storage;
    qa_q1_use_targets(game, entity, activator_copy(entity, &storage));
    free(entity->target);
    entity->target = original;
  }
  if (entity->wait == 3) qa_q1am_boss_teleport(context, false);
  if (entity->wait == 5) {
    qa_q1_actor *spiral = qa_q1_create(game, "spiral", NULL, NULL);
    qa_q1ad_set_text(&spiral->classname, "");
    spiral->has_owner = true;
    spiral->owner = entity->actor.id;
    qa_vec3 a = target_or_zero(monster);
    qa_vec3 b = qa_q1_base_monster_origin(monster);
    qa_vec3 delta = qa_sub3(a, b);
    delta.z = 0;
    qa_q1_body_patch patch;
    memset(&patch, 0, sizeof patch);
    patch.has_origin = true;
    patch.origin = qa_add3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 60});
    patch.has_angles = true;
    patch.angles = qa_q1mp_velocity_angles(qa_q1_normalize(delta));
    qa_q1_set_body(game, spiral, &patch);
    spiral->delay = qa_q1_option_skill(game) <= 2 ? 0.2 : 0.15;
    spiral->count = 50;
    later(context, spiral, "spiral_single", spiral->delay);
  }
}
qa_vec3 qa_q1am_final_boss_muzzle(qa_q1am_final_boss *boss, qa_vec3 offset, qa_vec3 angles) {
  qa_q1_base_monster *monster = &boss->base;
  const qa_q1_basis basis = qa_q1_make_vectors(monster->game, angles);
  return qa_add3(qa_add3(qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(basis.forward, offset.x)),
                         qa_scale3(basis.right, offset.y)),
                 (qa_vec3){0, 0, offset.z});
}
static void lavaball(qa_q1_services *game, qa_q1_actor *shot, qa_vec3 velocity) {
  qa_q1ad_set_text(&shot->model, "progs/lavaball.mdl");
  shot->projectile = "rocket";
  shot->touch = qa_q1_callback_registry_touch(game->named, shot, "projectile_touch");
  shot->angular_velocity = (qa_vec3){200, 100, 300};
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_bounds = true;
  patch.bounds = QA_Q1_POINT;
  patch.has_velocity = true;
  patch.velocity = velocity;
  qa_q1_set_body(game, shot, &patch);
}
void qa_q1am_final_boss_missile(qa_q1am_final_boss *boss, qa_vec3 offset) {
  qa_q1_base_monster *monster = &boss->base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if ((FLAGS(entity->spawnflags) & 2) != 0) {
    (void)qa_q1am_random(game);
    qa_q1am_final_boss_line(boss, offset);
    return;
  }
  const qa_vec3 target = target_or_zero(monster);
  const qa_vec3 origin = qa_q1am_final_boss_muzzle(boss, offset, qa_q1mp_velocity_angles(qa_sub3(target, qa_q1_base_monster_origin(monster))));
  const qa_vec3 direction = qa_q1_normalize(qa_sub3(target, origin));
  qa_q1_actor *shot = qa_q1_launch_spike(game, &entity->actor.id, origin, direction, "spike");
  lavaball(game, shot, qa_scale3(direction, 300));
  qa_q1_sound(game, entity->actor.id, "boss1/throw.wav", "weapon", 1, 1);
  if (enemy_health(monster) <= 0) qa_q1_base_monster_play(monster, "boss_final_idle1");
}
void qa_q1am_final_boss_melee_check(qa_q1am_final_boss *boss) {
  qa_q1_base_monster *monster = &boss->base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (qa_q1_base_monster_distance(monster) > 300 || !monster->state->has_enemy ||
      !qa_q1_can_damage(game, monster->state->enemy, entity->actor.id))
    return;
  const double first = qa_q1am_random(game), second = qa_q1am_random(game), third = qa_q1am_random(game);
  const qa_actor_id self = entity->actor.id;
  (void)qa_q1_damage(game, monster->state->enemy, self, &self, (first + second + third) * 40, NULL, NULL, NULL, NULL);
  qa_q1_sound(game, self, "shambler/smack.wav", "voice", 1, 1);
  for (int i = 0; i < 2; i++) {
    const qa_vec3 origin = qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(game->basis.forward, 100));
    (void)qa_q1_spawn_meat_spray(game, entity, origin, qa_scale3(game->basis.right, random_signed(boss->context) * 100));
  }
}
void qa_q1am_final_boss_missile_spawner(qa_q1am_final_boss *boss, qa_vec3 offset) {
  qa_q1_base_monster *monster = &boss->base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_vec3 a = target_or_zero(monster);
  qa_vec3 b = qa_q1_base_monster_origin(monster);
  const qa_vec3 origin = qa_q1am_final_boss_muzzle(
      boss, offset, qa_q1mp_velocity_angles(qa_sub3(a, b)));
  qa_q1_actor *shot = qa_q1_launch_spike(game, &entity->actor.id, origin, QA_Q1_ZERO, "spike");
  lavaball(game, shot, QA_Q1_ZERO);
  qa_q1_sound(game, entity->actor.id, "boss1/throw.wav", "weapon", 1, 1);
  if (!monster->state->has_enemy || qa_q1_health(game, monster->state->enemy) <= 0)
    qa_q1_base_monster_play(monster, "boss_final_idle1");
}
void qa_q1am_final_boss_blast(qa_q1am_final_boss *boss, qa_vec3 offset, double spread, bool effect) {
  qa_q1_base_monster *monster = &boss->base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_context *context = boss->context;
  const double speed = qa_q1_option_skill(game) > 2 ? 500 : qa_q1_option_skill(game) > 0 ? 450 : 400;
  const double width = FR(0.15 + FR(0.2 * fabs(spread)));
  const double count = floor(qa_q1am_random(game) * 6 + 0.5);
  const qa_vec3 target = target_or_zero(monster);
  const qa_vec3 angles = qa_q1mp_velocity_angles(qa_sub3(target, qa_q1_base_monster_origin(monster)));
  const qa_vec3 origin = qa_q1am_final_boss_muzzle(boss, offset, angles);
  const qa_q1_basis basis = qa_q1_make_vectors(game, angles);
  const double time = FR(qa_length3(qa_sub3(target, origin)) / speed);
  qa_vec3 velocity = QA_Q1_ZERO;
  qa_q1_body enemy_body;
  if (monster->state->has_enemy && qa_q1am_read_body(game, monster->state->enemy, &enemy_body)) velocity = enemy_body.velocity;
  velocity.z = 0;
  const qa_vec3 direction = qa_q1_normalize(qa_sub3(qa_add3(target, qa_scale3(velocity, time / 4)), origin));
  if (effect) qa_q1_effect(game, "explosion", origin, NULL, 1);
  for (double remaining = count; remaining > 0; remaining--) {
    const double right = random_signed(context) * width; /* TS evaluates right, then up */
    const double up = random_signed(context) * width;
    const qa_vec3 aim = qa_q1_normalize(qa_add3(qa_add3(direction, qa_scale3(basis.right, right)), qa_scale3(basis.up, up)));
    qa_q1_actor *shot = rock(context, &entity->actor.id, qa_add3(origin, qa_scale3(aim, 8)), aim, "sphere");
    qa_q1am_set_velocity_body(game, shot, qa_scale3(aim, speed + random_signed(context) * 100));
    spin(context, shot);
    if (fmod(remaining, 2) == 0) shot->effects = FLAGS(shot->effects) | 64;
  }
}
static void boss_radius_damage(qa_q1ad_context *context, qa_q1_actor *source, double damage);
void qa_q1am_final_boss_line(qa_q1am_final_boss *boss, qa_vec3 offset) {
  qa_q1_base_monster *monster = &boss->base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_context *context = boss->context;
  const double count = qa_q1_option_skill(game) == 0 ? 4 : qa_q1_option_skill(game) == 1 ? 8 : qa_q1_option_skill(game) == 3 ? 15 : 11;
  const qa_vec3 angles = qa_q1_get_body(game, entity).angles;
  const qa_vec3 origin = qa_q1am_final_boss_muzzle(boss, offset, angles);
  const qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_vec3 delta = qa_sub3(target_or_zero(monster), origin);
  delta.z = 0;
  const qa_vec3 direction = qa_q1_normalize(delta);
  qa_q1_effect(game, "explosion", origin, NULL, 1);
  const double start = 4 / count * (offset.y < 0 ? 1 : -1), increment = 2 / count * (offset.y < 0 ? -1 : 1);
  for (double i = 0; i < count; i++) {
    boss_radius_damage(context, entity, 10 * qa_q1am_random(game) + 10);
    qa_vec3 aim = qa_q1_normalize(qa_add3(qa_add3(direction, qa_scale3(basis.right, start)),
                                          qa_scale3(qa_scale3(basis.right, increment), i)));
    aim.z = 0;
    aim = qa_q1_normalize(aim);
    qa_q1_actor *shot = rock(context, &entity->actor.id, qa_add3(origin, qa_scale3(aim, 8)), aim, "sphere");
    qa_vec3 velocity = qa_scale3(aim, 300 + random_signed(context) * 100);
    if (qa_q1am_random(game) > 0.5) velocity.z = (qa_q1am_random(game) + 1) * 8;
    qa_q1am_set_velocity_body(game, shot, velocity);
    spin(context, shot);
    if (fmod(i, 2) == 0) {
      qa_q1ad_set_number(context, shot, "frags", 1);
      shot->effects = FLAGS(shot->effects) | 64;
    }
  }
}
static void boss_radius_damage(qa_q1ad_context *context, qa_q1_actor *source, double damage) {
  qa_q1_services *game = context->game;
  const qa_vec3 origin = qa_q1_get_body(game, source).origin;
  size_t count = 0;
  const qa_actor_observation *live = qa_session_actor_registry_observations(game->host.actors, &count);
  qa_actor_id *targets = count == 0 ? NULL : malloc(count * sizeof *targets);
  if (count != 0 && targets == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) targets[i] = live[i].id; /* snapshot: damage may release actors */
  const qa_actor_id self = source->actor.id;
  for (size_t i = count; i-- > 0;) {
    const qa_actor_id target = targets[i];
    qa_q1_body body;
    const bool has_body = qa_q1am_read_body(game, target, &body);
    if (qa_same_actor(target, self) || strcmp(game->host.classname(game->host.ctx, target), "monster_lava_man") == 0 ||
        !qa_q1am_can_take_damage(game, target) || !has_body)
      continue;
    const double distance = qa_length3(qa_sub3(origin, qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5))));
    if (distance > damage + 40) continue;
    const double points = damage - 0.5 * distance;
    if (points > 0 && qa_q1_can_damage(game, target, self))
      (void)qa_q1_damage(game, target, self, &self, points, NULL, NULL, NULL, NULL);
  }
  free(targets);
}
static qa_q1_actor *first_with_classname(qa_q1_services *game, const char *classname) {
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, classname) == 0) return game->entities[i];
  return NULL;
}
void qa_q1am_boss_teleport(qa_q1ad_context *context, bool variant) {
  qa_q1_services *game = context->game;
  if (variant) {
    qa_q1_actor *boss = first_with_classname(game, "monster_boss");
    qa_q1_actor *point = first_with_classname(game, "info_boss_teleport_boss");
    if (boss != NULL && point != NULL) {
      qa_q1_set_origin(game, boss, qa_q1_get_body(game, point).origin);
      qa_q1_effect(game, "lava-splash", qa_q1_get_body(game, boss).origin, NULL, 1);
      later(context, boss, "boss_final_rise1", 0.1);
    }
  }
  const char *wanted = variant ? "info_boss_teleport_second" : "info_boss_teleport_first";
  size_t point_count = 0;
  qa_q1_actor **points = game->entity_count == 0 ? NULL : malloc(game->entity_count * sizeof *points);
  if (game->entity_count != 0 && points == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, wanted) == 0) points[point_count++] = game->entities[i];
  for (size_t i = 0; i < point_count; i++) points[i]->wait = 0;
  size_t player_count = 0;
  const qa_actor_id *host_players = game->host.players(game->host.ctx, &player_count);
  qa_actor_id *players = player_count == 0 ? NULL : malloc(player_count * sizeof *players);
  if (player_count != 0 && players == NULL) qa_range_error("out of memory");
  if (player_count != 0) memcpy(players, host_players, player_count * sizeof *players);
  for (size_t p = 0; p < player_count; p++) {
    const qa_actor_id player = players[p];
    qa_q1_actor *point = NULL;
    for (size_t i = 0; i < point_count && point == NULL; i++)
      if (points[i]->wait == 0) point = points[i];
    if (point == NULL) {
      qa_q1ad_event event;
      memset(&event, 0, sizeof event);
      event.kind = QA_Q1AD_EVENT_DEVELOPER_MESSAGE;
      event.text = "ERROR: Could not find valid teleport destination!\n";
      context->services.emit(context->services.ctx, &event);
      break;
    }
    point->wait = 1;
    const qa_vec3 origin = qa_q1_get_body(game, point).origin, angles = qa_q1_actor_vector(point, "mangle");
    qa_q1_actor *entity = qa_q1_get_entity(game, &player);
    qa_q1_player_state *state = qa_q1_player(game, player);
    if (entity != NULL) entity->movement_flags = FLAGS(entity->movement_flags) & ~512;
    if (state != NULL) state->view_angles = angles;
    qa_owned_actor owned;
    const bool has_owned = qa_session_actor_registry_resolve_owned(game->host.actors, player, &owned);
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, player);
    if (has_owned && body.has_value) {
      qa_body_state next = body.value;
      next.origin = origin;
      next.angles = angles;
      next.ground = NULL;
      next.velocity = (qa_vec3){0, 0, 300};
      qa_shared_body_table_write(game->host.bodies, owned, next);
      qa_shared_body_table_link(game->host.bodies, owned, NULL);
    }
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "camera";
    event.has_player = true;
    event.player = player;
    event.has_origin = true;
    event.origin = origin;
    event.angles = angles;
    qa_q1_emit(game, event);
    (void)qa_q1_spawn_teleport_fog(game, qa_add3(origin, qa_scale3(qa_q1_make_vectors(game, angles).forward, 32)));
  }
  free(players);
  free(points);
}
bool qa_q1am_final_boss_init(qa_q1am_final_boss *boss, qa_q1ad_context *context, qa_q1_actor *entity) {
  memset(boss, 0, sizeof *boss);
  boss->context = context;
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = PREFIX;
  source.frames = &qa_q1am_final_boss_frames;
  source.actions = final_actions(&source.action_count);
  if (!qa_q1_base_monster_init(&boss->base, context->game, entity, &final_spec,
                               qa_q1_base_as_monster_services(context->base), &source))
    return false;
  boss->base.ops = &final_ops;
  return true;
}
static qa_q1_base_monster *create_final(void *ctx, qa_q1_actor *entity) {
  qa_q1am_final_boss *boss = malloc(sizeof *boss);
  if (boss == NULL) return NULL;
  if (!qa_q1am_final_boss_init(boss, ctx, entity)) { free(boss); return NULL; }
  return &boss->base;
}

/* The TS registerFinalBoss closure environment (context, bosses, and the
 * classname each teleport-point spawn closure captures). */
typedef struct final_registration {
  qa_q1ad_context *context;
  qa_q1am_boss_registry *bosses;
  struct teleport_spawn { struct final_registration *owner; const char *classname; } teleports[3];
  struct final_registration *next;
} final_registration;
static final_registration *all_registrations;

static bool final_boss_entity(final_registration *state, qa_actor_id target, qa_q1_actor **out) {
  qa_q1_actor *entity = qa_q1_get_entity(state->context->game, &target);
  *out = entity;
  return entity != NULL && qa_q1am_boss_registry_get(state->bosses, entity->actor) != NULL;
}
static bool before_health(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                          const qa_combat_state *attacker) {
  (void)damage; (void)target; (void)attacker; /* TS beforeHealth(request) */
  final_registration *state = ctx;
  qa_q1_services *game = state->context->game;
  qa_q1_actor *entity;
  return !final_boss_entity(state, request.target, &entity) || (FLAGS(entity->spawnflags) & 2) == 0 ||
         (qa_q1_actor_number(entity, "boss_immune") != 1 &&
          (request.attack.attacker == NULL ||
           strcmp(game->host.classname(game->host.ctx, *request.attack.attacker), "monster_lava_man") != 0));
}
static double after_armor(void *ctx, qa_damage_request request, double take, qa_combat_state target,
                          const qa_combat_state *attacker) {
  (void)target; (void)attacker; /* TS afterArmor(request, take) */
  final_registration *state = ctx;
  qa_q1_services *game = state->context->game;
  qa_q1_actor *entity;
  const bool boss = final_boss_entity(state, request.target, &entity);
  const qa_q1_player_state *player = request.attack.attacker == NULL ? NULL : qa_q1_player(game, *request.attack.attacker);
  const char *weapon = player == NULL ? NULL : player->weapon;
  return boss && (FLAGS(entity->spawnflags) & 2) != 0 && !(weapon != NULL && strcmp(weapon, "lightning") == 0) &&
                 !(weapon != NULL && strcmp(weapon, "mg3:laser") == 0)
             ? FR(take * FR(0.8)) : take;
}
static void melee_check(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS (_game, entity) */
  final_registration *state = ctx;
  qa_q1am_final_boss_melee_check(final_of(qa_q1am_require_boss(state->bosses, entity)));
}
static void rise(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS (_game, entity) */
  final_registration *state = ctx;
  qa_q1_base_monster_play(qa_q1am_require_boss(state->bosses, entity), "boss_final_rise1");
}
static void teleport_point_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const struct teleport_spawn *spawn = ctx;
  qa_q1ad_set_vector(spawn->owner->context, entity, "mangle", qa_q1_get_body(game, entity).angles);
  qa_q1ad_set_text(&entity->model, "");
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_angles = true;
  patch.angles = QA_Q1_ZERO;
  patch.has_origin = true;
  qa_vec3 a = qa_q1_get_body(game, entity).origin;
  qa_vec3 b = (qa_vec3){0, 0, strcmp(spawn->classname, "info_boss_teleport_boss") == 0 ? 0 : 27};
  patch.origin = qa_add3(a, b);
  qa_q1_set_body(game, entity, &patch);
}
static void teleport_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)game; (void)other; (void)activator; /* TS (_game, entity) */
  final_registration *state = ctx;
  qa_q1am_boss_teleport(state->context, (FLAGS(entity->spawnflags) & 1) != 0);
}
static void trigger_teleport_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; /* the TS closure reads only the game */
  entity->use = qa_q1_callback_registry_use(game->named, entity, PREFIX ":trigger_boss_teleport_use");
}
static void rock_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)surface; /* TS (_game, entity, other, normal) */
  if (entity->has_owner && qa_same_actor(entity->owner, other)) return;
  const qa_q1_actor *target = qa_q1_get_entity(game, &other);
  const char *classname = game->host.classname(game->host.ctx, other);
  if (strcmp(classname, "monster_orb") == 0 || strcmp(classname, "monster_lava_man") == 0) {
    qa_q1_remove(game, entity);
    return;
  }
  if (strcmp(classname, entity->classname) == 0 || (target != NULL && strcmp(target->solid, "trigger") == 0)) return;
  if (strcmp(game->host.contents(game->host.ctx, qa_q1_get_body(game, entity).origin), "sky") == 0) {
    qa_q1_remove(game, entity);
    return;
  }
  if (qa_q1am_can_take_damage(game, other)) {
    const qa_vec3 velocity = qa_q1_normalize(qa_q1_get_body(game, entity).velocity);
    const double up = qa_q1am_random(game) - 0.5; /* TS evaluates up, then right */
    const double right = qa_q1am_random(game) - 0.5;
    const qa_vec3 direction = qa_q1_normalize(qa_add3(qa_add3(velocity, qa_scale3(game->basis.up, up)),
                                                      qa_scale3(game->basis.right, right)));
    const qa_vec3 spray = qa_scale3(qa_scale3(qa_add3(direction, qa_scale3(normal != NULL ? *normal : QA_Q1_ZERO, 2)), 200), 0.2);
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "particles";
    event.has_origin = true;
    event.origin = qa_add3(qa_q1_get_body(game, entity).origin, qa_scale3(spray, 0.01));
    event.direction = qa_scale3(spray, 0.1);
    event.color = 73;
    event.count = 36;
    qa_q1_emit(game, event);
    (void)qa_q1_damage(game, other, entity->actor.id, qa_q1am_opt(entity->has_owner, &entity->owner), 18,
                       NULL, NULL, NULL, NULL);
  } else if (qa_q1_actor_number(entity, "frags") != 0) {
    qa_q1_effect(game, "knight-spike", qa_q1_get_body(game, entity).origin, NULL, 1);
  }
  qa_q1_remove(game, entity);
}
static void rock_death(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  final_registration *state = ctx;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_vec3 a;
    qa_vec3 b;
    qa_q1_actor *other = game->entities[i];
    if (strcmp(other->classname, "rock") == 0 &&
        qa_length3((a = qa_q1_get_body(game, other).origin, b = qa_q1_get_body(game, entity).origin, qa_sub3(a, b))) <= 96)
      later(state->context, other, "Rock_Death", 0.1);
  }
  qa_q1_effect(game, "explosion", qa_q1_get_body(game, entity).origin, NULL, 1);
  qa_q1_remove(game, entity);
}
static void spiral_single(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  final_registration *state = ctx;
  qa_q1ad_context *context = state->context;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_angles = true;
  patch.angles = qa_add3(qa_q1_get_body(game, entity).angles, (qa_vec3){0, 5, 0});
  qa_q1_set_body(game, entity, &patch);
  (void)qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  if (qa_q1_option_skill(game) < 2 && fmod(entity->count, 10) < 5) {
    later(context, entity, "spiral_single", entity->delay);
    entity->count--;
    return;
  }
  for (int i = 3; i >= 0; i--) {
    const qa_vec3 direction = qa_q1_make_vectors(game, qa_add3(qa_q1_get_body(game, entity).angles, (qa_vec3){0, 90.0 * i, 0})).forward;
    qa_q1_actor *shot = rock(context, qa_q1am_opt(entity->has_owner, &entity->owner),
                             qa_add3(qa_q1_get_body(game, entity).origin, qa_scale3(direction, 100)), direction, "plasma");
    qa_q1am_set_velocity_body(game, shot, qa_scale3(direction, 100));
    spin(context, shot);
    qa_q1_schedule(game, shot, 30, qa_q1_callback_registry_action(game->named, shot, "SUB_Remove"));
  }
  entity->count--;
  if (!entity->has_owner || qa_q1_health(game, entity->owner) <= 0) {
    qa_q1_remove(game, entity);
    return;
  }
  later(context, entity, "spiral_single", entity->delay);
}
static void circle_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  final_registration *state = ctx;
  qa_q1ad_context *context = state->context;
  for (int remaining = 72; remaining > 0; remaining--) {
    const qa_vec3 direction = qa_q1_make_vectors(game, (qa_vec3){0, (72 - remaining) * 5.0, 0}).forward;
    qa_q1_actor *shot = rock(context, &entity->actor.id, qa_q1_get_body(game, entity).origin, direction, "sphere");
    qa_q1am_set_velocity_body(game, shot, qa_scale3(direction, 250 + random_signed(context) * 100));
    spin(context, shot);
    qa_q1_schedule(game, shot, 20, qa_q1_callback_registry_action(game->named, shot, "SUB_Remove"));
    if (remaining % 4 == 0) shot->effects = FLAGS(shot->effects) | 8;
  }
  qa_q1_remove(game, entity);
}
static void boss_end(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_base *base = context->base;
  size_t count = 0;
  const qa_actor_id *host_players = game->host.players(game->host.ctx, &count);
  if (!game->options.coop && (count == 0 || qa_q1_health(game, host_players[0]) <= 0)) return;
  qa_actor_id *players = count == 0 ? NULL : malloc(count * sizeof *players);
  if (count != 0 && players == NULL) qa_range_error("out of memory");
  if (count != 0) memcpy(players, host_players, count * sizeof *players);
  for (size_t p = 0; p < count; p++) {
    qa_q1_player_state *player = qa_q1_player(game, players[p]);
    if (player == NULL) continue;
    size_t entry_count = 0;
    const qa_inventory_entry *live = qa_shared_inventory_table_entries(game->host.inventory, players[p], &entry_count);
    qa_inventory_entry *entries = entry_count == 0 ? NULL : malloc(entry_count * sizeof *entries);
    if (entry_count != 0 && entries == NULL) qa_range_error("out of memory");
    if (entry_count != 0) memcpy(entries, live, entry_count * sizeof *entries);
    for (size_t i = 0; i < entry_count; i++) {
      const char *item = entries[i].item;
      if (strcmp(item, "q1:ammo/shells") == 0 || strcmp(item, "q1:ammo/nails") == 0 ||
          strcmp(item, "q1:ammo/cells") == 0 || strcmp(item, "q1:ammo/rockets") == 0) {
        qa_inventory_entry entry = entries[i];
        entry.count = strcmp(item, "q1:ammo/shells") == 0 ? 25 : 0;
        qa_shared_inventory_table_configure(game->host.inventory, player->actor, entry, NULL, NULL);
      }
    }
    free(entries);
    qa_armor_state armor;
    memset(&armor, 0, sizeof armor);
    armor.regular.kind = QA_REGULAR_ARMOR_NONE;
    armor.powered.kind = "none";
    qa_gameplay_authority_set_armor(game->host.combat, player->actor, armor);
    player->weapon = "shotgun";
  }
  const qa_q1_campaign_binding campaign = qa_q1_base_campaign(base);
  const int32_t flags = qa_js_to_int32(campaign.read_flags(campaign.ctx));
  const char *map = "start";
  if ((flags & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0) {
    if ((flags & QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME) != 0) {
      map = "boss2";
    } else {
      map = "map1";
      campaign.write_flags(campaign.ctx, QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE | QA_Q1AD_BLOODY_NIGHTMARE_DISCOVERED |
                                             QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME);
      if (game->world != NULL) qa_q1am_field_text(game->world, "mg3.finalNewGameTravel", "1");
      static const char *const parms[] = {"parm10", "parm11", "parm12", "parm13", "parm14"};
      size_t now = 0;
      const qa_actor_id *current = game->host.players(game->host.ctx, &now);
      for (size_t p = 0; p < now; p++)
        for (size_t i = 0; i < 5; i++) qa_q1ad_set_player_number(context, current[p], parms[i], 0);
    }
  }
  free(players);
  if (game->world != NULL) qa_q1am_field_text(game->world, "addon.intermissiontext", "$mg3_qc_boss_finale");
  qa_q1_level_rules_begin(qa_q1_base_level_rules(base), map, NULL);
}
static void boss_end_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; (void)entity; /* TS action: () => bossEnd(context) */
  final_registration *state = ctx;
  boss_end(state->context);
}
static void register_named(qa_q1_services *game, const char *name, qa_q1_callback_handlers handlers) {
  qa_q1_callback_registry_register(game->named, name, handlers);
}
void qa_q1am_register_final_boss(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  final_registration *state = calloc(1, sizeof *state);
  if (state == NULL) qa_range_error("out of memory");
  state->context = context;
  state->bosses = qa_q1am_register_boss_controllers(context, PREFIX, "monster_boss_final", create_final, context);
  state->next = all_registrations;
  all_registrations = state;
  qa_q1_damage_source_effects effects;
  memset(&effects, 0, sizeof effects);
  effects.ctx = state;
  effects.before_health = before_health;
  effects.after_armor = after_armor;
  qa_q1_register_damage_source_effects(game, "mg3:final", effects);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = state;
  handlers.action = melee_check;
  register_named(game, PREFIX ":boss_final_melee_check", handlers);
  handlers.action = rise;
  register_named(game, PREFIX ":boss_final_rise1", handlers);
  static const char *const teleport_classnames[3] = {"info_boss_teleport_first", "info_boss_teleport_second",
                                                     "info_boss_teleport_boss"};
  for (size_t i = 0; i < 3; i++) {
    state->teleports[i].owner = state;
    state->teleports[i].classname = teleport_classnames[i];
    qa_q1_register_spawn(game, teleport_classnames[i], teleport_point_spawn, &state->teleports[i]);
  }
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = state;
  handlers.use = teleport_use;
  register_named(game, PREFIX ":trigger_boss_teleport_use", handlers);
  qa_q1_register_spawn(game, "trigger_boss_teleport", trigger_teleport_spawn, state);
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = state;
  handlers.touch = rock_touch;
  register_named(game, PREFIX ":T_RockTouch2", handlers);
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = state;
  handlers.action = rock_death;
  register_named(game, PREFIX ":Rock_Death", handlers);
  handlers.action = spiral_single;
  register_named(game, PREFIX ":spiral_single", handlers);
  handlers.action = circle_think;
  register_named(game, PREFIX ":circle_think", handlers);
  handlers.action = boss_end_action;
  register_named(game, "mg3:bosses:boss_end", handlers);
}
void qa_q1am_release_final_boss(qa_q1_services *game) {
  for (final_registration **link = &all_registrations; *link != NULL;) {
    final_registration *state = *link;
    if (state->context->game != game) { link = &state->next; continue; }
    *link = state->next;
    free(state);
  }
}
