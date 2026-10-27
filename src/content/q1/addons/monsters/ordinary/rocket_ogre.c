/* Port of src/content/q1/addons/monsters/ordinary/rocket-ogre.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ordinary/rocket_ogre.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static qa_q1am_frame frames_storage[8];
static qa_q1am_operation ops_storage[8][8];
static qa_q1am_frame_table table_storage;
static bool table_built;
static const char *const frame_names[] = {
  "ogre_stand5", "ogre_walk3", "ogre_run1", "ogre_nail1",
  "ogre_nail4", "ogre_nail5", "ogre_die3", "ogre_bdie3",
};
const qa_q1am_frame_table *qa_q1am_rocket_ogre_frames(qa_q1am_game *game) {
  if (table_built) return &table_storage;
  for (size_t i = 0; i < 8; i++) {
    const qa_q1am_frame *frame = game->base_frame(game->ctx, frame_names[i]);
    if (frame == NULL) qa_range_error("Missing ogre frame %s", frame_names[i]);
    size_t kept = 0;
    for (size_t o = 0; o < frame->operation_count; o++) {
      qa_q1am_op_kind kind = frame->operations[o].kind;
      if (kind == QA_Q1AM_OP_SOUND || kind == QA_Q1AM_OP_ACTION) continue;
      if (kept >= 8) qa_range_error("Missing ogre frame %s", frame_names[i]);
      ops_storage[i][kept++] = frame->operations[o];
    }
    frames_storage[i].name = frame_names[i];
    frames_storage[i].frame = frame->frame;
    frames_storage[i].next = frame->next;
    frames_storage[i].operations = ops_storage[i];
    frames_storage[i].operation_count = kept;
  }
  table_storage.frames = frames_storage;
  table_storage.count = 8;
  table_built = true;
  return &table_storage;
}
static void fire_rocket(qa_q1am_monster *monster) {
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  qa_vec3 target;
  if (!qa_q1am_monster_target(monster, &target) || !monster->state->has_enemy) return;
  qa_q1am_body body;
  if (!game->body_read(game->ctx, monster->state->enemy, &body)) return;
  qa_q1am_basis basis = monster->ops.make_vectors(monster);
  double offset = (qa_q1am_actor_number(entity, "projectiles_max") -
                   qa_q1am_actor_number(entity, "projectiles")) *
                  (game->random(game->ctx) > 0.5 ? -64 : 64);
  qa_vec3 flat_velocity = body.velocity;
  flat_velocity.z = 0;
  qa_vec3 lead = qa_scale3(flat_velocity,
                           qa_length3(qa_sub3(target, qa_q1am_monster_origin(monster))) / 1200);
  qa_vec3 aim = qa_add3(qa_sub3(qa_add3(qa_add3(target, (qa_vec3){0, 0, -8}), lead),
                                qa_add3(qa_q1am_monster_origin(monster), (qa_vec3){0, 0, 16})),
                        qa_scale3(basis.right, offset));
  entity->effects = (double)((int32_t)entity->effects | 2);
  game->sound(game->ctx, entity, "weapons/sgun1.wav", "weapon", 1);
  qa_q1am_actor *missile = game->create_missile(
      game->ctx, game, true, entity->actor.id, "ogre_missile", "missile",
      qa_add3(qa_add3(qa_q1am_monster_origin(monster), qa_scale3(basis.forward, 8)), (qa_vec3){0, 0, 16}),
      qa_scale3(qa_q1am_normalize(aim), 600));
  char text[64];
  (void)snprintf(text, sizeof text, "%.17g", game->time + 0.2);
  qa_q1am_actor_set_field(missile, "ammo_nails", text);
  missile->touch = game->named_touch(game->ctx, missile, "mg3:ordinary:T_OgreMissileTouch");
}
void qa_q1am_rocket_ogre_frame(qa_q1am_monster *monster, const char *name) {
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  if (strcmp(name, "ogre_nail1") == 0 || strcmp(name, "ogre_nail5") == 0) {
    char text[64];
    (void)snprintf(text, sizeof text, "%.17g", qa_q1am_actor_number(entity, "projectiles_max"));
    qa_q1am_actor_set_field(entity, "projectiles", text);
  }
  if (strcmp(name, "ogre_nail4") == 0) {
    fire_rocket(monster);
    double remaining = qa_q1am_actor_number(entity, "projectiles") - 1;
    char text[64];
    (void)snprintf(text, sizeof text, "%.17g", remaining);
    qa_q1am_actor_set_field(entity, "projectiles", text);
    if (qa_q1am_actor_number(entity, "projectiles_max") != 0 && remaining > 0)
      qa_q1am_monster_set_frame(monster->next_frame, sizeof monster->next_frame, "ogre_nail2");
  }
  if ((strcmp(name, "ogre_stand5") == 0 || strcmp(name, "ogre_walk3") == 0 ||
       strcmp(name, "ogre_run1") == 0) &&
      game->random(game->ctx) < 0.2) {
    double index = (strcmp(name, "ogre_walk3") == 0 ? 3 : 1) + (game->random(game->ctx) < 0.5 ? 0 : 1);
    char path[64];
    (void)snprintf(path, sizeof path, "armagon/idle%d.wav", (int)index);
    game->sound(game->ctx, entity, path, "voice", 2);
  }
}
static void missile_touch(void *ctx, qa_q1am_game *game, qa_q1am_actor *missile, qa_actor_id other,
                          bool has_normal, qa_vec3 normal) {
  (void)ctx; (void)has_normal; (void)normal;
  if (missile->has_owner && qa_same_actor(missile->owner, other)) return;
  qa_q1am_body body = game->body(game->ctx, missile);
  qa_vec3 origin = body.origin;
  if (strcmp(game->contents(game->ctx, origin), "sky") == 0) {
    game->remove(game->ctx, missile);
    return;
  }
  if (qa_q1am_actor_number(missile, "ammo_nails") > game->time) {
    game->effect(game->ctx, "blood", origin, true, other, 18);
    game->damage(game->ctx, other, missile->actor.id, missile->has_owner, missile->owner, 20);
    game->effect(game->ctx, "knight-spike", origin, false, (qa_actor_id){0}, 1);
    game->remove(game->ctx, missile);
    return;
  }
  if (game->health(game->ctx, other) != 0) {
    qa_q1am_actor *target = game->entity(game->ctx, true, other);
    const char *classname = target ? target->classname : NULL;
    game->damage(game->ctx, other, missile->actor.id, missile->has_owner, missile->owner,
                 classname && strcmp(classname, "monster_shambler") == 0 ? 20
                 : classname && strcmp(classname, "monster_zombie") == 0 ? 60 : 40);
  }
  game->radius_damage(game->ctx, missile->actor.id, missile->has_owner, missile->owner, 40, true, other, NULL);
  game->set_origin(game->ctx, missile, qa_sub3(origin, qa_scale3(qa_q1am_normalize(body.velocity), 8)));
  game->sprite_explosion(game->ctx, game, missile);
}
void qa_q1am_register_rocket_ogre(qa_q1am_context *context) {
  qa_q1am_handlers handlers = {0};
  handlers.touch = missile_touch;
  context->game->named_register(context->game->ctx, "mg3:ordinary:T_OgreMissileTouch", handlers);
}
