/* Port of src/content/q1/addons/monsters/bosses/oldnew.ts.
 * quakec_mg3/monsters/mg3_oldone_new.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/bosses/oldnew.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/bosses/effects.h"
#include "qa/content/q1/addons/monsters/bosses/frames/oldnew.h"
#include "qa/content/q1/addons/monsters/bosses/oldnew_children.h"
#include "qa/content/q1/addons/monsters/bosses/oldnew_projectiles.h"
#include "qa/content/q1/addons/monsters/bosses/registry.h"
#include "qa/content/q1/addons/monsters/bosses/sphere_points.h"
#include "qa/content/q1/addons/monsters/bosses/szombie.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/base/rules.h"
#include "../support.h"

#define PREFIX "mg3:oldnew"
#define FLAGS QA_Q1AM_FLAGS

static const char *const oldnew_classnames[] = {"monster_oldone_new"};
static const qa_q1_base_monster_species oldnew_spec = {
  .species = "oldone", .classnames = oldnew_classnames, .classname_count = 1, .model = "oldone", .head = NULL,
  .health = 12000, .gib_health = -INFINITY, .gibs = NULL, .gib_count = 0,
  .bounds = {{-128, -128, -24}, {128, 128, 256}}, .stand = "oldnew_idle1", .walk = "oldnew_idle1",
  .run = "oldnew_walk1", .sight = "", .missile = NULL, .melee = false, .movement = "walk",
};

static void oldnew_spawn(qa_q1_base_monster *monster);
static void oldnew_use(qa_q1_base_monster *monster, const qa_actor_id *activator);
static void oldnew_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
static void oldnew_die(qa_q1_base_monster *monster, const qa_actor_id *attacker);
static void oldnew_destroy(qa_q1_base_monster *monster) { free(monster); }
static const qa_q1_base_monster_ops oldnew_ops = {
  QA_Q1AM_MG3_MONSTER_OPS,
  .spawn = oldnew_spawn, .use = oldnew_use, .pain = oldnew_pain, .die = oldnew_die, .destroy = oldnew_destroy,
};
bool qa_q1am_is_oldnew(const qa_q1_base_monster *monster) { return monster->ops == &oldnew_ops; }
static qa_q1am_oldnew *oldnew_of(qa_q1_base_monster *monster) { return (qa_q1am_oldnew *)monster; }

static void oldnew_frame_action(void *ctx, qa_q1_base_monster *monster) {
  if (!qa_q1am_is_oldnew(monster)) qa_range_error("Oldone callback received another controller");
  qa_q1am_oldnew_action(oldnew_of(monster), ctx);
}
static void emit_host(qa_q1_services *game, qa_q1_event event) { qa_q1_emit(game, event); }
static void lightstyle(qa_q1_services *game, const char *pattern) {
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "lightstyle";
  event.style = 0;
  event.pattern = pattern;
  emit_host(game, event);
}
static void oldnew_spawn(qa_q1_base_monster *monster) {
  qa_q1ad_context *context = oldnew_of(monster)->context;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
  entity->solid = "slidebox";
  entity->movement = "step";
  qa_q1ad_set_text(&entity->model, "progs/oldone.mdl");
  qa_q1_set_bounds(game, entity, (FLAGS(entity->spawnflags) & 128) != 0 ? (qa_bounds){{1, 1, -24}, {1, 1, 0}} : oldnew_spec.bounds);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 12000);
  entity->max_health = 12000;
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  entity->movement_flags = 32;
  qa_combat_trait_changes traits;
  memset(&traits, 0, sizeof traits);
  traits.has_team = true;
  traits.team = "q1:monsters";
  qa_gameplay_authority_set_traits(game->host.combat, entity->actor, traits);
  qa_q1ad_set_vector(context, entity, "view_ofs", (qa_vec3){0, 0, 24});
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, PREFIX ":monster_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, PREFIX ":monster_die");
  entity->use = qa_q1_callback_registry_use(game->named, entity, PREFIX ":monster_use");
  entity->yaw_speed = 10;
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  game->total_monsters++;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "monster-total";
  event.total = game->total_monsters;
  emit_host(game, event);
  qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, oldnew_spec.stand);
  qa_q1_base_monster_delay(monster, 0.1);
}
static void oldnew_use(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_actor_id resolved;
  const bool has = qa_q1am_mg3_monster_activator(monster->game, activator, &resolved);
  qa_q1_base_monster_use_super(monster, has ? &resolved : NULL);
}
/* TS private wave(field). */
static void wave(qa_q1_base_monster *monster, const char *field) {
  qa_q1_actor *entity = monster->entity;
  const char *target = qa_q1_actor_text(entity, field);
  if (target[0] == '\0') return;
  char *previous = entity->target;
  entity->target = NULL;
  qa_q1ad_set_text(&entity->target, target);
  if (entity->target == NULL) qa_range_error("out of memory");
  const qa_actor_id activator = entity->activator;
  qa_q1_use_targets(monster->game, entity, entity->has_activator ? &activator : NULL);
  free(entity->target);
  entity->target = previous;
}
static void oldnew_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)damage; /* TS pain(attacker, _damage) */
  qa_q1_base_monster_retaliate(monster, attacker);
  qa_q1ad_context *context = oldnew_of(monster)->context;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (qa_q1_actor_number(entity, "boss_immune") != 0) return;
  bool trigger = false;
  if (qa_q1_actor_number(entity, "boss_phase") == 0) { trigger = true; qa_q1ad_set_number(context, entity, "boss_phase", 1); }
  if (qa_q1_actor_number(entity, "boss_phase") == 1 && qa_q1_health(game, entity->actor.id) < entity->max_health * 0.75) {
    trigger = true;
    qa_q1ad_set_number(context, entity, "boss_phase", 2);
    (void)qa_q1am_spawn_eye(context, entity);
    wave(monster, "wave1");
  }
  if (qa_q1_actor_number(entity, "boss_phase") == 2 && qa_q1_health(game, entity->actor.id) < entity->max_health * 0.5) {
    trigger = true;
    qa_q1ad_set_number(context, entity, "boss_phase", 3);
    wave(monster, "wave2");
  }
  if (qa_q1_actor_number(entity, "boss_phase") == 3 && qa_q1_health(game, entity->actor.id) < entity->max_health * 0.25) {
    trigger = true;
    qa_q1ad_set_number(context, entity, "boss_phase", 4);
    wave(monster, "wave2");
  }
  if (!trigger) return;
  qa_q1ad_set_number(context, entity, "boss_immune", 1);
  qa_q1_sound_defaults(game, entity->actor.id, "orb/orb_pain.wav");
  monster->state->pain_finished = game->time + 2.1;
  qa_q1_base_monster_play(monster, "oldnew_thrash1");
}
static void oldnew_die(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  if (monster->counted_death) return;
  qa_q1ad_context *context = oldnew_of(monster)->context;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_set_enemy(monster, attacker);
  qa_q1_actor_set_damageable(entity, false);
  memset(&entity->touch, 0, sizeof entity->touch);
  qa_q1_base_monster_count_kill(monster);
  qa_q1ad_set_number(context, entity, "boss_immune", 1);
  qa_q1ad_set_number(context, entity, "cnt", 0);
  qa_q1_base_monster_play(monster, "oldnew_death1");
  qa_q1am_cleanup_oldnew(context);
  (void)qa_q1am_spawn_sphere_manager(context, entity, qa_q1_option_skill(game) > 2 ? qa_q1_actor_number(entity, "boss_phase") : 1);
}
void qa_q1am_oldnew_attack(qa_q1am_oldnew *oldnew) {
  qa_q1_actor *entity = oldnew->base.base.entity;
  qa_q1ad_context *context = oldnew->context;
  if (qa_q1_actor_number(entity, "boss_immune") != 0) return;
  const double phase = qa_q1_actor_number(entity, "boss_phase");
  if (fmod(qa_q1_actor_number(entity, "cnt"), 3) != 0) {
    (void)qa_q1am_spawn_sphere_chunk_manager(context, entity, phase <= 3 ? 1 : 2);
  } else if (phase == 0) {
    (void)qa_q1am_spawn_sphere_chunk_manager(context, entity, 1);
  } else {
    const double count = entity->count;
    if (phase == 1) {
      if (count == 0) (void)qa_q1am_spawn_spammer(context, entity);
      else if (count == 1) (void)qa_q1am_spawn_swiper(context, entity);
    } else if (phase == 2) {
      if (count == 0) (void)qa_q1am_spawn_vortex(context, entity);
      else if (count == 1) (void)qa_q1am_spawn_swiper(context, entity);
      else if (count == 2) (void)qa_q1am_spawn_spammer(context, entity);
    } else if (phase == 3) {
      if (count == 0) qa_q1am_spawn_blaster(context, entity);
      else if (count >= 1 && count <= 3) (void)qa_q1am_spawn_swiper(context, entity);
    } else if (phase >= 4) {
      if (count == 0) (void)qa_q1am_spawn_spammer(context, entity);
      else if (count == 1) (void)qa_q1am_spawn_eye(context, entity);
      else if (count == 2) (void)qa_q1am_spawn_swiper(context, entity);
      else if (count == 3) qa_q1am_spawn_blaster(context, entity);
    }
    entity->count++;
    if (entity->count > (phase == 1 ? 1 : phase == 2 ? 2 : 3)) entity->count = 0;
  }
  qa_q1ad_set_number(context, entity, "cnt", qa_q1_actor_number(entity, "cnt") + 1);
}
/* TS private autoGun(side, offset, count). */
static void auto_gun(qa_q1am_oldnew *oldnew, double side, double offset, double count) {
  qa_q1_base_monster *monster = &oldnew->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (qa_q1_actor_number(entity, "boss_immune") != 0 || count > qa_q1_actor_number(entity, "boss_phase") + 2 ||
      qa_q1_option_skill(game) == 0 || count > qa_q1_option_skill(game) + 1)
    return;
  const qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
  qa_q1_sound(game, entity->actor.id, "weapons/spike2.wav", "weapon", 1, 1);
  qa_q1am_auto_gun(oldnew->context, entity,
                   qa_add3(qa_add3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 80}), qa_scale3(basis.right, side * 64)),
                   offset);
}
void qa_q1am_oldnew_action(qa_q1am_oldnew *oldnew, const char *name) {
  qa_q1_base_monster *monster = &oldnew->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_context *context = oldnew->context;
  static const struct { const char *name; double side, offset, count; bool zombie; } guns[] = {
    {"oldnew:oldnew_walk21", 1, 0.05, 1, false}, {"oldnew:oldnew_walk22", 1, 0, 2, false},
    {"oldnew:oldnew_walk23", 1, -0.05, 3, true}, {"oldnew:oldnew_walk24", 1, -0.1, 4, false},
    {"oldnew:oldnew_walk25", 1, -0.15, 5, false}, {"oldnew:oldnew_walk36", -1, -0.05, 1, false},
    {"oldnew:oldnew_walk37", -1, 0, 2, false}, {"oldnew:oldnew_walk38", -1, 0.05, 3, false},
    {"oldnew:oldnew_walk39", -1, 0.1, 4, false}, {"oldnew:oldnew_walk40", -1, 0.15, 5, false},
  };
  for (size_t i = 0; i < sizeof guns / sizeof guns[0]; i++)
    if (strcmp(name, guns[i].name) == 0) {
      qa_q1_base_monster_face(monster);
      if (guns[i].zombie) (void)qa_q1am_spawn_shub_zombie(context);
      auto_gun(oldnew, guns[i].side, guns[i].offset, guns[i].count);
      return;
    }
  if (strcmp(name, "oldnew:oldnew_idle1") == 0) {
    qa_q1_base_monster_ai(monster, QA_Q1_AI_STAND, 0);
  } else if (strcmp(name, "oldnew:oldnew_walk1") == 0) {
    qa_q1_base_monster_face(monster);
    qa_q1ad_set_number(context, entity, "boss_immune", 0);
  } else if (strcmp(name, "oldnew:oldnew_walk2") == 0) {
    qa_q1_base_monster_face(monster);
  } else if (strcmp(name, "oldnew:oldnew_walk14") == 0) {
    qa_q1_base_monster_face(monster);
    qa_q1am_oldnew_attack(oldnew);
  } else if (strcmp(name, "oldnew:oldnew_walk45") == 0) {
    qa_q1_base_monster_face(monster);
    (void)qa_q1am_spawn_shub_zombie(context);
  } else if (strcmp(name, "oldnew:oldnew_thrash4") == 0) {
    qa_q1am_pain_lightning(context, entity, (qa_vec3){0, 0, 100});
  } else if (strcmp(name, "oldnew:oldnew_thrash14") == 0) {
    qa_q1ad_set_number(context, entity, "boss_immune", 1);
    (void)qa_q1am_spawn_sphere_manager(context, entity, qa_q1_option_skill(game) > 2 ? qa_q1_actor_number(entity, "boss_phase") : 1);
  } else if (strcmp(name, "oldnew:oldnew_thrash15") == 0) {
    qa_q1ad_set_number(context, entity, "boss_immune", 0);
  } else if (strcmp(name, "oldnew:oldnew_death1") == 0) {
    qa_q1_sound_defaults(game, entity->actor.id, "boss2/death.wav");
  } else if (strcmp(name, "oldnew:oldnew_death15") == 0) {
    qa_q1ad_set_number(context, entity, "cnt", qa_q1_actor_number(entity, "cnt") + 1);
    if (qa_q1_actor_number(entity, "cnt") != 3) qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "oldnew_death1");
  } else if (strcmp(name, "oldnew:oldnew_death16") == 0) {
    lightstyle(game, "g");
  } else if (strcmp(name, "oldnew:oldnew_death17") == 0) {
    lightstyle(game, "c");
  } else if (strcmp(name, "oldnew:oldnew_death18") == 0) {
    lightstyle(game, "b");
  } else if (strcmp(name, "oldnew:oldnew_death19") == 0) {
    lightstyle(game, "a");
  } else if (strcmp(name, "oldnew:oldnew_death20") == 0) {
    qa_q1am_oldnew_finish(oldnew);
  } else {
    qa_range_error("Unknown oldone action %s", name);
  }
}
void qa_q1am_oldnew_finish(qa_q1am_oldnew *oldnew) {
  qa_q1_base_monster *monster = &oldnew->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_context *context = oldnew->context;
  qa_q1_sound_defaults(game, entity->actor.id, "boss2/pop2.wav");
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  if (!game->options.coop && (count == 0 || qa_q1_health(game, players[0]) <= 0)) {
    qa_q1_base_monster_play(monster, "oldnew_idle1");
    return;
  }
  for (int i = 0; i < 100; i++) {
    const qa_vec3 point = qa_q1am_sphere_point(i);
    if (point.z > 0)
      (void)qa_q1am_throw_gib_vector(context, qa_add3(qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(point, 64)),
                                                      (qa_vec3){0, 0, 48}), point);
  }
  qa_q1_remove(game, entity);
  qa_q1ad_event music;
  memset(&music, 0, sizeof music);
  music.kind = QA_Q1AD_EVENT_MUSIC;
  music.track = 3;
  music.loop_track = 3;
  context->services.emit(context->services.ctx, &music);
  lightstyle(game, "m");
  qa_q1am_oldnew_credits(context);
}
qa_q1_actor *qa_q1am_throw_gib_vector(qa_q1ad_context *context, qa_vec3 origin, qa_vec3 direction) {
  qa_q1_services *game = context->game;
  qa_q1_actor *gib = qa_q1_create(game, "gib_vector", NULL, NULL);
  qa_q1ad_set_text(&gib->classname, "");
  const double choice = qa_q1am_random(game);
  qa_q1ad_set_text(&gib->model, choice < 0.3 ? "progs/gib1.mdl" : choice < 0.6 ? "progs/gib2.mdl" : "progs/gib3.mdl");
  const double speed = 800 + (qa_q1am_random(game) * 2 - 1) * 200;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = origin;
  patch.has_velocity = true;
  patch.velocity = qa_scale3(direction, speed);
  patch.has_bounds = true;
  patch.bounds = QA_Q1_POINT;
  qa_q1_set_body(game, gib, &patch);
  gib->movement = "bounce";
  gib->solid = "none";
  qa_vec3 angular;
  angular.x = qa_q1am_random(game) * 600;
  angular.y = qa_q1am_random(game) * 600;
  angular.z = qa_q1am_random(game) * 600;
  gib->angular_velocity = angular;
  qa_q1ad_set_number(context, gib, "ltime", game->time);
  double delay = 10 + qa_q1am_random(game) * 10;
  qa_q1_bound_action callback = qa_q1_callback_registry_action(game->named, gib, "SUB_Remove");
  qa_q1_schedule(game, gib, delay, callback);
  qa_q1_link(game, gib);
  return gib;
}
void qa_q1am_oldnew_credits(qa_q1ad_context *context) {
  if (context->game->world != NULL) qa_q1am_field_text(context->game->world, "addon.intermissiontext", "$map_dopa_endtext_final");
  /* execute_changelevel overwrites oldnew_credits' provisional 10000000-second delay with the ordinary two-second wait. */
  qa_q1_level_rules_begin(qa_q1_base_level_rules(context->base), "start", NULL);
}
bool qa_q1am_oldnew_init(qa_q1am_oldnew *oldnew, qa_q1ad_context *context, qa_q1_actor *entity) {
  memset(oldnew, 0, sizeof *oldnew);
  oldnew->context = context;
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = PREFIX;
  source.frames = &qa_q1am_oldnew_frames;
  source.actions = qa_q1am_frame_actions(&qa_q1am_oldnew_frames, oldnew_frame_action, &source.action_count);
  return qa_q1am_mg3_monster_init(&oldnew->base, context->game, entity, &oldnew_spec,
                                  qa_q1_base_as_monster_services(context->base), &source, &oldnew_ops, NULL);
}
static qa_q1_base_monster *create_oldnew(void *ctx, qa_q1_actor *entity) {
  qa_q1am_oldnew *oldnew = malloc(sizeof *oldnew);
  if (oldnew == NULL) return NULL;
  if (!qa_q1am_oldnew_init(oldnew, ctx, entity)) { free(oldnew); return NULL; }
  return &oldnew->base.base;
}
static void credits_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; (void)entity; /* TS action: () => oldnewCredits(context) */
  qa_q1am_oldnew_credits(ctx);
}
static bool oldone_before_health(void *ctx, qa_damage_request request, double damage, qa_combat_state target,
                                 const qa_combat_state *attacker) {
  (void)damage; (void)target; (void)attacker; /* TS beforeHealth(request) */
  qa_q1ad_context *context = ctx;
  const qa_q1_actor *entity = qa_q1_get_entity(context->game, &request.target);
  return entity == NULL || strcmp(entity->classname, "monster_oldone_new") != 0 ||
         qa_q1_actor_number(entity, "boss_immune") != 1;
}
void qa_q1am_register_oldnew(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1am_register_oldnew_projectiles(context);
  qa_q1am_register_oldnew_children(context);
  (void)qa_q1am_register_boss_controllers(context, PREFIX, "monster_oldone_new", create_oldnew, context);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = context;
  handlers.action = credits_action;
  qa_q1_callback_registry_register(game->named, QA_Q1AM_OLDNEW_PREFIX "oldnew_credits", handlers);
  qa_q1_damage_source_effects effects;
  memset(&effects, 0, sizeof effects);
  effects.ctx = context;
  effects.before_health = oldone_before_health;
  qa_q1_register_damage_source_effects(game, "mg3:oldone", effects);
}
