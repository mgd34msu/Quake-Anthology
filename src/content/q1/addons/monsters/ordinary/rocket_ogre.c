/* Port of src/content/q1/addons/monsters/ordinary/rocket-ogre.ts.
 * quakec_mg3/monsters/ogre.qc rocket variant. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/rocket_ogre.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/base/projectiles.h"
#include "../support.h"

static void fire_rocket(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1_base_monster_target(monster, &target) || !monster->state->has_enemy) return;
  qa_q1_body body;
  if (!qa_q1am_read_body(game, monster->state->enemy, &body)) return;
  const qa_q1_basis basis = qa_q1_base_monster_make_vectors(monster);
  double left = qa_q1_actor_number(entity, "projectiles_max");
  double right2 = qa_q1_actor_number(entity, "projectiles");
  const double offset = (left - right2) *
                        (qa_q1am_random(game) > 0.5 ? -64 : 64);
  qa_vec3 velocity = body.velocity;
  velocity.z = 0;
  const qa_vec3 lead = qa_scale3(velocity, qa_length3(qa_sub3(target, qa_q1_base_monster_origin(monster))) / 1200);
  const qa_vec3 aim = qa_add3(qa_sub3(qa_add3(qa_add3(target, (qa_vec3){0, 0, -8}), lead),
                                      qa_add3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 16})),
                              qa_scale3(basis.right, offset));
  entity->effects = QA_Q1AM_FLAGS(entity->effects) | 2;
  qa_q1_sound(game, entity->actor.id, "weapons/sgun1.wav", "weapon", 1, 1);
  qa_q1_actor *missile = qa_q1_create_missile(game, &entity->actor.id, "ogre_missile", "missile",
                                              qa_add3(qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(basis.forward, 8)), (qa_vec3){0, 0, 16}),
                                              qa_scale3(qa_q1_normalize(aim), 600));
  qa_q1am_field_number(missile, "ammo_nails", game->time + 0.2);
  missile->touch = qa_q1_callback_registry_touch(game->named, missile, "mg3:ordinary:T_OgreMissileTouch");
}
void qa_q1am_rocket_ogre_frame(qa_q1_base_monster *monster, const char *name) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (strcmp(name, "ogre_nail1") == 0 || strcmp(name, "ogre_nail5") == 0)
    qa_q1am_field_number(entity, "projectiles", qa_q1_actor_number(entity, "projectiles_max"));
  if (strcmp(name, "ogre_nail4") == 0) {
    fire_rocket(monster);
    const double remaining = qa_q1_actor_number(entity, "projectiles") - 1;
    qa_q1am_field_number(entity, "projectiles", remaining);
    if (qa_q1_actor_number(entity, "projectiles_max") != 0 && remaining > 0)
      qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "ogre_nail2");
  }
  if ((strcmp(name, "ogre_stand5") == 0 || strcmp(name, "ogre_walk3") == 0 || strcmp(name, "ogre_run1") == 0) &&
      qa_q1am_random(game) < 0.2) {
    int left = (strcmp(name, "ogre_walk3") == 0 ? 3 : 1);
    int right = (qa_q1am_random(game) < 0.5 ? 0 : 1);
    const int index = left + right;
    char path[32];
    (void)snprintf(path, sizeof path, "armagon/idle%d.wav", index);
    qa_q1_sound(game, entity->actor.id, path, "voice", 2, 1);
  }
}
static void ogre_missile_touch(void *ctx, qa_q1_services *game, qa_q1_actor *missile, qa_actor_id other,
                               const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface; /* TS (game, missile, other) */
  if (missile->has_owner && qa_same_actor(missile->owner, other)) return;
  const qa_q1_body body = qa_q1_get_body(game, missile);
  const qa_vec3 origin = body.origin;
  if (strcmp(game->host.contents(game->host.ctx, origin), "sky") == 0) { qa_q1_remove(game, missile); return; }
  const qa_actor_id *owner = qa_q1am_opt(missile->has_owner, &missile->owner);
  if (qa_q1_actor_number(missile, "ammo_nails") > game->time) {
    qa_q1_effect(game, "blood", origin, &other, 18);
    (void)qa_q1_damage(game, other, missile->actor.id, owner, 20, NULL, NULL, NULL, NULL);
    qa_q1_effect(game, "knight-spike", origin, NULL, 1);
    qa_q1_remove(game, missile);
    return;
  }
  if (qa_q1_health(game, other) != 0) {
    const qa_q1_actor *target = qa_q1_get_entity(game, &other);
    const char *classname = target == NULL ? NULL : target->classname;
    (void)qa_q1_damage(game, other, missile->actor.id, owner,
                       classname != NULL && strcmp(classname, "monster_shambler") == 0 ? 20
                       : classname != NULL && strcmp(classname, "monster_zombie") == 0 ? 60 : 40,
                       NULL, NULL, NULL, NULL);
  }
  qa_q1_radius_damage(game, missile->actor.id, owner, 40, &other, NULL, NULL);
  qa_q1_set_origin(game, missile, qa_sub3(origin, qa_scale3(qa_q1_normalize(body.velocity), 8)));
  qa_q1_sprite_explosion(game, missile);
}
void qa_q1am_register_rocket_ogre(qa_q1ad_context *context) {
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = context;
  handlers.touch = ogre_missile_touch;
  qa_q1_callback_registry_register(context->game->named, "mg3:ordinary:T_OgreMissileTouch", handlers);
}
