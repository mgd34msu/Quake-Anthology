/* Port of src/content/q2/missionpacks/monsters/widow/death.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/widow/death.h"

#include <stdio.h>
#include <stdlib.h>

#include "../mp_internal.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/missionpacks/monsters/widow/common.h"
#include "qa/foundation/js.h"

static const char *const MEAT = "models/objects/gibs/sm_meat/tris.md2";
static const char *const METAL = "models/objects/gibs/sm_metal/tris.md2";
static void free_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  game->remove(game->ctx, entity);
}
static void gib_die_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  game->remove(game->ctx, entity);
}
static void gib_touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  (void)ctx; (void)contact;
  game->solid(game->ctx, entity, "none");
  entity->touch = (qa_q2_touch){NULL, NULL};
  entity->angular_velocity = q2mpm_zero();
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_angles = true;
  change.angles = q2mpm_v3(0, q2mpm_body(game, entity).angles.y, 0);
  q2mpm_move_body(game, entity, change, false, false);
  game->motion(game->ctx, entity, entity->motion);
  const char *noise = entity->noise ? entity->noise : "";
  if (noise[0] != '\0') game->sound(game->ctx, entity->actor, noise, 2, 1, 1);
}
static qa_vec3 clip_velocity(qa_vec3 velocity) {
  return q2mpm_v3(qa_js_max(-300, qa_js_min(300, velocity.x)), qa_js_max(-300, qa_js_min(300, velocity.y)),
                  qa_js_max(200, qa_js_min(500, velocity.z)));
}
qa_q2_entity *qa_q2mpm_widow_gib(qa_q2_entity *self, qa_q2_game_services *game, const char *model, double damage,
                                  bool organic, const qa_vec3 *origin, bool sized, const char *hit_sound,
                                  bool fade) {
  double x5;
  double y5;
  double z5;
  qa_q2_entity *gib = game->create(game->ctx, "gib", NULL, 0);
  qa_body_state body = q2mpm_body(game, self);
  qa_vec3 half = qa_scale3_double(qa_sub3_double(body.bounds.max, body.bounds.min), 0.5);
  qa_vec3 center = qa_add3_double(body.origin, qa_add3_double(body.bounds.min, qa_add3_double(half, q2mpm_v3(-1, -1, -1))));
  qa_vec3 point = origin ? *origin : qa_add3_double(center, (x5 = (q2mpm_rnd(game) * 2 - 1) * half.x, y5 = (q2mpm_rnd(game) * 2 - 1) * half.y, z5 = (q2mpm_rnd(game) * 2 - 1) * half.z, q2mpm_v3(x5,
                                                                      y5, z5)));
  gib->effects = 2;
  gib->flags = q2mpm_or(gib->flags, 2048);
  gib->render_flags = q2mpm_or(gib->render_flags, 32768);
  gib->die = (qa_q2_die){NULL, gib_die_fn};
  double lifetime = (fade ? (sized ? 20 : 5) : (sized ? 60 : 25)) + q2mpm_rnd(game) * (sized ? 15 : 10);
  double x4 = damage * (q2mpm_rnd(game) * 2 - 1);
  double y4 = damage * (q2mpm_rnd(game) * 2 - 1);
  double z4 = damage * (q2mpm_rnd(game) * 2 - 1) + 200;
  qa_vec3 raw = {x4, y4, z4};
  qa_vec3 velocity = clip_velocity(qa_add3_double(body.velocity, qa_scale3_double(raw, organic ? 0.5 : 1)));
  gib->model = model;
  qa_bounds bounds = {{0, 0, 0}, {0, 0, 0}};
  if (sized) {
    gib->noise = hit_sound;
    double x3 = q2mpm_rnd(game) * 400;
    double y3 = q2mpm_rnd(game) * 400;
    double z3 = q2mpm_rnd(game) * 200;
    gib->angular_velocity = q2mpm_v3(x3, y3, z3);
    velocity = clip_velocity(q2mpm_v3(velocity.x * 2, velocity.y * 2, fabs(velocity.z)));
    velocity.z = qa_js_max(350 + q2mpm_rnd(game) * 100, velocity.z);
    gib->gravity = 0.25;
    gib->touch = (qa_q2_touch){NULL, gib_touch_fn};
    gib->has_owner = true;
    gib->owner = self->actor.id;
    double size = strcmp(model, "models/monsters/blackwidow2/gib2/tris.md2") == 0 ? 10 : 5;
    bounds.min = q2mpm_v3(-size, -size, 0);
    bounds.max = q2mpm_v3(size, size, size);
  } else {
    velocity.x *= 2;
    velocity.y *= 2;
    double x2 = q2mpm_rnd(game) * 600;
    double y2 = q2mpm_rnd(game) * 600;
    double z2 = q2mpm_rnd(game) * 600;
    gib->angular_velocity = q2mpm_v3(x2, y2, z2);
    if (organic) {
      bool has_touch = false;
      qa_q2_touch touch = qa_q2_touch_resolve(game->source_callbacks, "gib_touch", &has_touch);
      gib->touch = has_touch ? touch : (qa_q2_touch){NULL, NULL};
    } else {
      gib->touch = (qa_q2_touch){NULL, NULL};
    }
  }
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = point;
  change.has_velocity = true; change.velocity = velocity;
  change.has_bounds = true; change.bounds = bounds;
  q2mpm_move_body(game, gib, change, true, false);
  qa_combat_state combat;
  memset(&combat, 0, sizeof combat);
  combat.armor.powered.kind = "none";
  combat.can_take_damage = true;
  qa_gameplay_authority_create_state(game->host->combat, gib->actor, combat, NULL, NULL);
  game->solid(game->ctx, gib, sized ? "box" : "none");
  game->motion(game->ctx, gib, organic ? "toss" : "bounce");
  qa_q2_think free_think = {NULL, free_fn};
  game->schedule(game->ctx, gib, lifetime, free_think);
  game->link(game->ctx, gib);
  game->show(game->ctx, gib);
  return gib;
}
qa_q2_entity *qa_q2mpm_widow_gib_default(qa_q2_entity *self, qa_q2_game_services *game, const char *model, double damage,
                                          bool organic) {
  return qa_q2mpm_widow_gib(self, game, model, damage, organic, NULL, false, "", true);
}
void qa_q2mpm_widow_effect(qa_q2_game_services *game, qa_vec3 origin, const char *effect, double count) {
  q2mpm_emit_effect(game, effect, origin, q2mpm_zero(), count, 0);
}
void qa_q2mpm_widow_effect_default(qa_q2_game_services *game, qa_vec3 origin) {
  qa_q2mpm_widow_effect(game, origin, "q2:explosion1", 1);
}
static void small_gibs(qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 point) {
  for (int i = 0; i < 2; i++) (void)qa_q2mpm_widow_gib(self, game, MEAT, 300, true, &point, false, "", false);
  (void)qa_q2mpm_widow_gib(self, game, METAL, 300, false, &point, false, "", false);
  (void)qa_q2mpm_widow_gib(self, game, METAL, 100, false, &point, false, "", false);
}
static void more_gibs(qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 point) {
  if (game->options.mode == QA_Q2_MODE_COOP) { small_gibs(self, game, point); return; }
  (void)qa_q2mpm_widow_gib(self, game, MEAT, 300, true, &point, false, "", false);
  for (int i = 0; i < 2; i++) (void)qa_q2mpm_widow_gib(self, game, METAL, 300, false, &point, false, "", false);
  for (int i = 0; i < 3; i++) (void)qa_q2mpm_widow_gib(self, game, METAL, 100, false, &point, false, "", false);
}
static void legs_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
static void legs_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  qa_q2_think again = {NULL, legs_think_fn};
  if (entity->frame == 17) {
    qa_vec3 point = qa_q2mpm_widow_project(entity, game, q2mpm_v3(11.77, -7.24, 23.31));
    qa_q2mpm_widow_effect_default(game, point);
    small_gibs(entity, game, point);
  }
  if (entity->frame < 23) {
    entity->frame++;
    game->show(game->ctx, entity);
    game->schedule(game->ctx, entity, 0.1, again);
    return;
  }
  if (entity->wait == 0) entity->wait = q2mpm_now(game) + 1;
  if (q2mpm_now(game) > entity->wait) {
    static const qa_vec3 offsets[2] = {{-65.6, -8.44, 28.59}, {-1.04, -51.18, 7.04}};
    static const double pieces[2] = {2, 3};
    for (int part = 0; part < 2; part++) {
      qa_vec3 point = qa_q2mpm_widow_project(entity, game, offsets[part]);
      qa_q2mpm_widow_effect_default(game, point);
      small_gibs(entity, game, point);
      for (double i = 1; i <= pieces[part]; i++) {
        char model[64];
        snprintf(model, sizeof model, "models/monsters/blackwidow/gib%d/tris.md2", (int)i);
        char *copy = malloc(strlen(model) + 1);
        if (!copy) qa_raise("Error", 0, 0, "Out of memory in widow legs");
        strcpy(copy, model);
        (void)qa_q2mpm_widow_gib(entity, game, copy, 80 + trunc(q2mpm_rnd(game) * 20), false, &point, true, "", true);
      }
    }
    game->remove(game->ctx, entity);
    return;
  }
  if (q2mpm_now(game) > entity->wait - 0.5 && entity->count == 0) {
    entity->count = 1;
    qa_q2mpm_widow_effect_default(game, qa_q2mpm_widow_project(entity, game, q2mpm_v3(31, -88.7, 10.96)));
    qa_q2mpm_widow_effect_default(game, qa_q2mpm_widow_project(entity, game, q2mpm_v3(-12.67, -4.39, 15.68)));
  }
  game->schedule(game->ctx, entity, 0.1, again);
}
void qa_q2mpm_spawn_widow_legs(qa_q2_entity *self, qa_q2_game_services *game) {
  qa_q2_entity *legs = game->create(game->ctx, "widowlegs", NULL, 0);
  qa_body_state body = q2mpm_body(game, self);
  legs->model = "models/monsters/legs/tris.md2";
  legs->render_flags = 32768;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = body.origin;
  change.has_angles = true; change.angles = body.angles;
  q2mpm_move_body(game, legs, change, true, false);
  game->solid(game->ctx, legs, "none");
  game->motion(game->ctx, legs, "stationary");
  qa_q2_think think = {NULL, legs_think_fn};
  game->schedule(game->ctx, legs, 0.1, think);
  game->link(game->ctx, legs);
  game->show(game->ctx, legs);
}
qa_q2_callback_definitions qa_q2mpm_widow_debris_callbacks(void) {
  static qa_q2_named_think thinks[8];
  static qa_q2_named_touch touches[8];
  static qa_q2_named_die dies[8];
  static qa_q2_callback_definitions definitions;
  static bool ready;
  if (!ready) {
    qa_q2_callback_definitions gibs = qa_q2m_gib_callbacks();
    size_t think_count = gibs.think_count < 6 ? gibs.think_count : 6;
    for (size_t i = 0; i < think_count; i++) thinks[i] = gibs.think[i];
    thinks[think_count] = (qa_q2_named_think){"q2:rogue/widow_gib_free", {NULL, free_fn}};
    thinks[think_count + 1] = (qa_q2_named_think){"q2:rogue/widowlegs_think", {NULL, legs_think_fn}};
    size_t touch_count = gibs.touch_count < 7 ? gibs.touch_count : 7;
    for (size_t i = 0; i < touch_count; i++) touches[i] = gibs.touch[i];
    touches[touch_count] = (qa_q2_named_touch){"q2:rogue/widow_gib_touch", {NULL, gib_touch_fn}};
    size_t die_count = gibs.die_count < 7 ? gibs.die_count : 7;
    for (size_t i = 0; i < die_count; i++) dies[i] = gibs.die[i];
    dies[die_count] = (qa_q2_named_die){"q2:rogue/widow_gib_die", {NULL, gib_die_fn}};
    memset(&definitions, 0, sizeof definitions);
    definitions.think = thinks;
    definitions.think_count = think_count + 2;
    definitions.touch = touches;
    definitions.touch_count = touch_count + 1;
    definitions.die = dies;
    definitions.die_count = die_count + 1;
    ready = true;
  }
  return definitions;
}
void qa_q2mpm_widow_explosion(qa_q2m_context *context, qa_vec3 offset) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_vec3 point = qa_q2mpm_widow_project(entity, game, offset);
  qa_q2mpm_widow_effect_default(game, point);
  (void)qa_q2mpm_widow_gib(entity, game, MEAT, 300, true, &point, false, "", false);
  (void)qa_q2mpm_widow_gib(entity, game, METAL, 100, false, &point, false, "", false);
  for (int i = 0; i < 2; i++) (void)qa_q2mpm_widow_gib(entity, game, METAL, 300, false, &point, false, "", false);
}
void qa_q2mpm_widow_explosion_leg(qa_q2m_context *context) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  static const qa_vec3 offsets[2] = {{-31.89, -47.86, 67.02}, {-44.9, -82.14, 54.72}};
  static const double pieces[2] = {2, 1};
  static const double damages[2] = {200, 300};
  for (int i = 0; i < 2; i++) {
    qa_vec3 point = qa_q2mpm_widow_project(entity, game, offsets[i]);
    qa_q2mpm_widow_effect(game, point, pieces[i] == 2 ? "q2:explosion1_big" : "q2:explosion1", 1);
    char model[64];
    snprintf(model, sizeof model, "models/monsters/blackwidow2/gib%d/tris.md2", (int)pieces[i]);
    char *copy = malloc(strlen(model) + 1);
    if (!copy) qa_raise("Error", 0, 0, "Out of memory in widow explosion leg");
    strcpy(copy, model);
    (void)qa_q2mpm_widow_gib(entity, game, copy, damages[i], false, &point, true, "misc/fhit3.wav", false);
    (void)qa_q2mpm_widow_gib(entity, game, MEAT, 300, true, &point, false, "", false);
    (void)qa_q2mpm_widow_gib(entity, game, METAL, 100, false, &point, false, "", false);
  }
}
void qa_q2mpm_widow_explosion_leg_handler(void *ctx, qa_q2m_context *context) {
  (void)ctx;
  qa_q2mpm_widow_explosion_leg(context);
}
typedef struct explode_bundle {
  qa_q2m_monsters *monsters;
} explode_bundle;
static void explode_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  explode_bundle *bundle = ctx;
  qa_q2m_context *context = qa_q2m_monsters_context(bundle->monsters, entity->actor.id);
  if (!context) qa_raise("Error", 0, 0, "Widow explosion lost its source controller");
  qa_vec3 origin = q2mpm_body(game, entity).origin;
  qa_vec3 point = origin;
  point.z += 24 + (double)(qa_js_to_int32(qa_js_to_uint32(floor(q2mpm_rnd(game) * 2147483648.0)) & 15u));
  if (entity->count < 8) point.z += 24 + (double)(qa_js_to_int32(qa_js_to_uint32(floor(q2mpm_rnd(game) * 2147483648.0)) & 31u));
  int count = (int)entity->count;
  switch (count) {
    case 0: point = qa_add3_double(point, q2mpm_v3(-24, -24, 0)); break;
    case 1: point = qa_add3_double(point, q2mpm_v3(24, 24, 0)); small_gibs(entity, game, point); break;
    case 2: point = qa_add3_double(point, q2mpm_v3(24, -24, 0)); break;
    case 3: point = qa_add3_double(point, q2mpm_v3(-24, 24, 0)); more_gibs(entity, game, point); break;
    case 4: point = qa_add3_double(point, q2mpm_v3(-48, -48, 0)); break;
    case 5: {
      point = qa_add3_double(point, q2mpm_v3(48, 48, 0));
      qa_vec3 arm = qa_q2mpm_widow_project(entity, game, q2mpm_v3(65.76, 17.52, 7.56));
      qa_q2mpm_widow_effect(game, arm, "q2:explosion1_big", 1);
      for (int i = 0; i < 2; i++) (void)qa_q2mpm_widow_gib(entity, game, METAL, 100, false, &arm, false, "", false);
      break;
    }
    case 6: {
      point = qa_add3_double(point, q2mpm_v3(-48, 48, 0));
      qa_vec3 arm = qa_q2mpm_widow_project(entity, game, q2mpm_v3(65.76, 17.52, 7.56));
      (void)qa_q2mpm_widow_gib(entity, game, "models/monsters/blackwidow2/gib4/tris.md2", 200, false, &arm, true,
                               "misc/fhit3.wav", false);
      (void)qa_q2mpm_widow_gib(entity, game, MEAT, 300, true, &arm, false, "", false);
      break;
    }
    case 7: point = qa_add3_double(point, q2mpm_v3(48, -48, 0)); small_gibs(entity, game, point); break;
    case 8: point = qa_add3_double(origin, q2mpm_v3(18, 18, 48)); more_gibs(entity, game, point); break;
    case 9: point = qa_add3_double(origin, q2mpm_v3(-18, 18, 48)); break;
    case 10: point = qa_add3_double(origin, q2mpm_v3(18, -18, 48)); break;
    case 11: point = qa_add3_double(origin, q2mpm_v3(-18, -18, 48)); break;
    case 12: {
      entity->sound = "";
      (void)qa_q2mpm_widow_gib_default(entity, game, MEAT, 400, true);
      for (int i = 0; i < 2; i++) (void)qa_q2mpm_widow_gib_default(entity, game, METAL, 100, false);
      for (int i = 0; i < 2; i++) (void)qa_q2mpm_widow_gib_default(entity, game, METAL, 400, false);
      context->state->dead = true;
      q2mpm_set_move(context, "widow2_move_dead");
      qa_q2m_monsters_resume_monster(bundle->monsters, entity, game);
      return;
    }
    default: break;
  }
  entity->count++;
  const char *effect = entity->count >= 9 && entity->count <= 12 ? "q2:explosion1_big"
      : fmod(entity->count, 2) == 1 ? "q2:explosion1" : "q2:explosion1_np";
  qa_q2mpm_widow_effect(game, point, effect, 1);
  qa_q2_think again = {bundle, explode_fn};
  game->schedule(game->ctx, entity, 0.1, again);
}
qa_q2_think qa_q2mpm_create_widow_explode(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters) {
  explode_bundle *bundle = qa_q2mpm_arena_alloc(arena, sizeof *bundle);
  if (!bundle) qa_raise("Error", 0, 0, "Out of memory creating widow explode");
  bundle->monsters = monsters;
  qa_q2_think think = {bundle, explode_fn};
  return think;
}
