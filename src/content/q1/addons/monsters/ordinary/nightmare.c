/* Port of src/content/q1/addons/monsters/ordinary/nightmare.ts.
 * quakec_mg3 monsters/{shambler,ogre,shalrath}.qc skill and Bloody Nightmare behavior.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/nightmare.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "qa/content/q1/base/monster_actions.h"
#include "qa/content/q1/base/projectiles.h"
#include "../support.h"

/* TS game.sourceBloodyNightmare?.() === true. */
static bool bloody(qa_q1_services *game) {
  return game->source_bloody_nightmare.fn != NULL && game->source_bloody_nightmare.fn(game->source_bloody_nightmare.ctx);
}
/* nightmare_attack_check: after a run of melee the shambler answers with lightning, from skill 3 and sooner in Bloody Nightmare. */
static bool nightmare_attack_check(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  const bool is_bloody = bloody(game);
  const double cap = 3 + floor(qa_q1am_random(game) * 2 + 0.5) - (is_bloody ? 1 : 0);
  if (qa_q1_option_skill(game) <= 2) return false;
  monster->entity->count++;
  return monster->entity->count > cap;
}
/* TS afterMelee(name); entry ctx = the captured name. */
static void after_melee(void *ctx, qa_q1_base_monster *monster) {
  const char *name = ctx;
  if (nightmare_attack_check(monster)) {
    qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "sham_magic1");
    return;
  }
  if (strcmp(name, "sham_smash12") != 0) qa_q1_monster_action(monster, name);
}
static void sham_magic6(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  if (qa_q1_option_skill(monster->game) > 2) monster->entity->count = 0;
  qa_q1_monster_action(monster, "sham_magic6");
}
static void ogre_nail1(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (qa_q1_actor_number(entity, "aflag") == 0) {
    const bool is_bloody = bloody(game);
    qa_q1am_field_text(entity, "projectiles_max", is_bloody && qa_q1am_random(game) > 0.3 ? "2" : "0");
  }
  qa_q1am_field_number(entity, "projectiles", qa_q1_actor_number(entity, "projectiles_max"));
}
static void ogre_nail4(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  qa_q1_actor *entity = monster->entity;
  qa_q1_monster_action(monster, "ogre_nail4");
  const double remaining = qa_q1_actor_number(entity, "projectiles") - 1;
  qa_q1am_field_number(entity, "projectiles", remaining);
  if (qa_q1_actor_number(entity, "projectiles_max") != 0 && remaining > 0)
    qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "ogre_nail2");
}
static void ogre_nail5(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  qa_q1am_field_number(monster->entity, "projectiles", qa_q1_actor_number(monster->entity, "projectiles_max"));
}
static void launch_shal_missile(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx; /* TS shal_attack9: monster => launchShalMissile(monster) */
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (!monster->state->has_enemy) return;
  const qa_actor_id enemy = monster->state->enemy;
  qa_q1_body target;
  if (!qa_q1am_read_body(game, enemy, &target)) return;
  const qa_vec3 aim = qa_add3(target.origin, (qa_vec3){0, 0, 10});
  const qa_vec3 dir = qa_q1_normalize(qa_sub3(aim, qa_q1_base_monster_origin(monster)));
  const double flytime = qa_js_max(0.1, qa_length3(qa_sub3(target.origin, qa_q1_base_monster_origin(monster))) * 0.002);
  entity->effects = QA_Q1AM_FLAGS(entity->effects) | 2;
  qa_q1_sound(game, entity->actor.id, "shalrath/attack2.wav", "weapon", 1, 1);
  qa_q1_actor *missile = qa_q1_create_missile_lifetime(game, &entity->actor.id, "shal_missile", "v_spike",
                                                       qa_add3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 10}),
                                                       qa_scale3(dir, 400), 5);
  missile->angular_velocity = (qa_vec3){300, 300, 300};
  qa_q1am_reference(missile, "enemy", &enemy);
  if (bloody(game)) {
    qa_q1am_field_number(missile, "ltime", game->time + flytime);
    qa_q1_schedule(game, missile, 0.1, qa_q1_callback_registry_action(game->named, missile, "mg3:ordinary:shal_home"));
  } else {
    qa_q1_schedule(game, missile, flytime, qa_q1_callback_registry_action(game->named, missile, "mg3:ordinary:shal_home"));
  }
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, "base:vore_touch");
}
#define NAMED(N) (void *)(uintptr_t)(N)
const qa_q1_monster_action_entry qa_q1am_mg3_nightmare_actions[] = {
  {"sham_smash12", after_melee, NAMED("sham_smash12")}, {"sham_swingl9", after_melee, NAMED("sham_swingl9")},
  {"sham_swingr9", after_melee, NAMED("sham_swingr9")}, {"sham_magic6", sham_magic6, NULL},
  {"ogre_nail1", ogre_nail1, NULL}, {"ogre_nail4", ogre_nail4, NULL}, {"ogre_nail5", ogre_nail5, NULL},
  {"shal_attack9", launch_shal_missile, NULL},
};
#undef NAMED
const size_t qa_q1am_mg3_nightmare_action_count = sizeof qa_q1am_mg3_nightmare_actions / sizeof qa_q1am_mg3_nightmare_actions[0];

static void shal_home(void *ctx, qa_q1_services *game, qa_q1_actor *missile) {
  (void)ctx; /* TS (_game, missile) => shalHome(context, missile) over context.game */
  qa_actor_id target;
  const bool has_target = qa_q1_actor_reference_get(missile, "enemy", &target);
  qa_q1_body enemy;
  if (!has_target || !qa_q1am_read_body(game, target, &enemy) || qa_q1_health(game, target) < 1) {
    qa_q1_remove(game, missile);
    return;
  }
  const qa_vec3 here = qa_q1_get_body(game, missile).origin;
  qa_vec3 dir = qa_q1_normalize(qa_sub3(qa_add3(enemy.origin, (qa_vec3){0, 0, 10}), here));
  if (bloody(game) && game->time < qa_q1_actor_number(missile, "ltime")) {
    qa_q1_schedule(game, missile, 0.1, qa_q1_callback_registry_action(game->named, missile, "mg3:ordinary:shal_home"));
    return;
  }
  const double speed = qa_q1_option_skill(game) == 3 ? 285 : 250, reach = 32;
  qa_q1_actor *nearest = NULL;
  double nearest_distance = reach;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *other = game->entities[i];
    if (strcmp(other->classname, "shal_missile") != 0 || other == missile) continue;
    const double distance = qa_length3(qa_sub3(qa_q1_get_body(game, other).origin, here));
    if (distance < nearest_distance) { nearest_distance = distance; nearest = other; }
  }
  const double ratio = nearest_distance / reach, push = 0.25 - ratio * ratio * 0.25;
  if (nearest != NULL)
    dir = qa_add3(qa_scale3(dir, 1 - push), qa_scale3(qa_q1_normalize(qa_sub3(here, qa_q1_get_body(game, nearest).origin)), push));
  qa_q1am_set_velocity_body(game, missile, qa_scale3(dir, speed));
  qa_q1_schedule(game, missile, 0.1, qa_q1_callback_registry_action(game->named, missile, "mg3:ordinary:shal_home"));
}
void qa_q1am_register_mg3_nightmare(qa_q1ad_context *context) {
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = context;
  handlers.action = shal_home;
  qa_q1_callback_registry_register(context->game->named, "mg3:ordinary:shal_home", handlers);
}
