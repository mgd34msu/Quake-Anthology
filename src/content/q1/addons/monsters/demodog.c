/* Port of src/content/q1/addons/monsters/demodog.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/demodog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/demodog_frames.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/foundation/support.h"

#define FLAGS(x) ((int32_t)(x))

static const char *const demodog_classnames[] = {"monster_demodog"};
static const char *const demodog_gibs[] = {"gib3", "gib3", "gib3"};
static const qa_q1am_species demodog_spec = {
  "dog", NULL, "dog_explosive", "h_dog", "demodog_stand1", "demodog_walk1", "demodog_run1",
  "dog/dsight.wav", "demodog_leap1", "walk", demodog_classnames, 1, demodog_gibs, 3,
  25, -35, {{-32, -32, -24}, {32, 32, 64}}, true,
};
static void bite_action(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  if (!monster->state->has_enemy) return;
  monster->ops.ai(monster, QA_Q1AM_AI_CHARGE, 10);
  qa_q1am_monster_melee(monster, 100, 8, 3, true);
}
static void jump_action(void *ctx, qa_q1am_monster *monster) {
  (void)ctx;
  qa_q1am_game *game = monster->game;
  qa_q1am_actor *entity = monster->entity;
  qa_q1am_body body = game->body(game->ctx, entity);
  entity->touch = game->named_touch(game->ctx, entity, "mg3:demodog:jump_touch");
  entity->movement_flags = (double)(FLAGS(entity->movement_flags) & ~512);
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_add3(body.origin, (qa_vec3){0, 0, 1});
  patch.has_velocity = true;
  patch.velocity = qa_add3(qa_scale3(qa_q1am_game_make_vectors(game, body.angles).forward, 300),
                           (qa_vec3){0, 0, 200});
  patch.has_ground = true;
  patch.ground_null = true;
  game->set_body(game->ctx, entity, &patch);
}
static const qa_q1am_action_entry demodog_actions[] = {
  {"demodog_bite", NULL, bite_action},
  {"demodog_jump", NULL, jump_action},
};
static const qa_q1am_source demodog_source = {
  "mg3:demodog", &qa_q1am_demodog_frames, demodog_actions,
  sizeof demodog_actions / sizeof demodog_actions[0],
};

static qa_q1am_demodog *as_dog(qa_q1am_monster *m) { return m->owner; }
static void demodog_spawn(qa_q1am_monster *m) {
  qa_q1am_demodog *dog = as_dog(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  (void)snprintf(entity->classname, sizeof entity->classname, "%s", "monster_dog");
  dog->context->set_number(dog->context->ctx, entity, "aflag", 1);
  entity->wait = 0;
  game->combat_set_health(game->ctx, entity->actor, 25);
  entity->max_health = 25;
  entity->pain = game->named_pain(game->ctx, entity, "mg3:demodog:monster_pain");
  entity->die = game->named_die(game->ctx, entity, "mg3:demodog:monster_die");
  entity->path_end = game->named_action(game->ctx, entity, "mg3:demodog:monster_stand");
  qa_q1am_actor_set_field(entity, "allowPathFind", "1");
  qa_q1am_actor_set_field(entity, "combat_style", "2");
  qa_q1am_init_mg3_monster(m, dog->context, "progs/dog_explosive.mdl", 1, 2);
}
static void demodog_start(qa_q1am_monster *m) { qa_q1am_start_mg3_monster(m, as_dog(m)->context); }
static void demodog_use(qa_q1am_monster *m, bool has, qa_actor_id activator) {
  qa_actor_id resolved;
  bool resolved_has = qa_q1am_mg3_monster_activator(m->game, has, activator, &resolved);
  as_dog(m)->super_ops.use(m, resolved_has, resolved);
}
static bool demodog_try_attack(qa_q1am_monster *m) {
  qa_q1am_body target;
  if (!m->state->has_enemy || !m->game->body_read(m->game->ctx, m->state->enemy, &target)) return false;
  if ((m->game->world ? qa_q1am_actor_number(m->game->world, "enemy_range") : 0) == 0) {
    (void)snprintf(m->entity->attack_state, sizeof m->entity->attack_state, "%s", "melee");
    return true;
  }
  qa_q1am_body body = m->game->body(m->game->ctx, m->entity);
  qa_vec3 delta = qa_sub3(target.origin, body.origin);
  double height = target.bounds.max.z - target.bounds.min.z;
  if (body.origin.z + body.bounds.min.z > target.origin.z + target.bounds.min.z + height * 0.75 ||
      body.origin.z + body.bounds.max.z < target.origin.z + target.bounds.min.z + height * 0.25)
    return false;
  double distance = hypot(delta.x, delta.y);
  if (distance < 80 || distance > 150) return false;
  (void)snprintf(m->entity->attack_state, sizeof m->entity->attack_state, "%s", "missile");
  return true;
}
static void demodog_melee_attack(qa_q1am_monster *m) { m->ops.play(m, "demodog_atta1"); }
static void demodog_pain(qa_q1am_monster *m, bool has, qa_actor_id attacker, double damage) {
  (void)damage;
  qa_q1am_monster_retaliate(m, has, attacker);
  m->game->sound(m->game->ctx, m->entity, "dog/dpain1.wav", "voice", 1);
  m->ops.play(m, m->game->random(m->game->ctx) > 0.5 ? "demodog_pain1" : "demodog_painb1");
}
static void demodog_grenade(qa_q1am_demodog *dog, bool vertical) {
  qa_q1am_monster *m = &dog->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_vec3 velocity = qa_scale3((qa_vec3){100 * (game->random(game->ctx) * 2 - 1),
                                         100 * (game->random(game->ctx) * 2 - 1),
                                         200 + 100 * game->random(game->ctx)}, 1.5);
  if (vertical) {
    velocity.x = 0;
    velocity.y = 0;
  }
  velocity = qa_add3(velocity, qa_scale3(qa_q1am_game_make_vectors(game, game->body(game->ctx, entity).angles).forward, 100));
  game->sound(game->ctx, entity, "weapons/grenade.wav", "weapon", 1);
  qa_q1am_actor *grenade = game->create(game->ctx, "grenade");
  grenade->has_owner = true;
  grenade->owner = entity->actor.id;
  grenade->movement = "bounce";
  grenade->solid = "bbox";
  grenade->projectile = "grenade";
  grenade->damage = 60;
  (void)snprintf(grenade->model, sizeof grenade->model, "%s", "progs/grenade.mdl");
  grenade->angular_velocity = (qa_vec3){300, 300, 300};
  dog->context->set_vector(dog->context->ctx, grenade, "oldorigin", velocity);
  qa_q1am_body_patch patch = {0};
  patch.has_origin = true;
  patch.origin = qa_q1am_monster_origin(m);
  patch.has_velocity = true;
  patch.velocity = velocity;
  patch.has_bounds = true;
  patch.bounds = QA_Q1AM_POINT;
  patch.has_angles = true;
  patch.angles = (qa_vec3){atan2(velocity.z, hypot(velocity.x, velocity.y)) * 180 / M_PI,
                           qa_q1am_yaw_for(velocity), 0};
  game->set_body(game->ctx, grenade, &patch);
  grenade->touch = game->named_touch(game->ctx, grenade, "mg3:demodog:grenade_touch");
  game->link(game->ctx, grenade);
  game->schedule(game->ctx, grenade, 2.5 + 0.25 * (game->random(game->ctx) * 2 - 1),
                 game->named_action(game->ctx, grenade, "mg3:demodog:grenade_explode"));
}
static void demodog_die(qa_q1am_monster *m, bool has, qa_actor_id attacker) {
  if (m->counted_death) return;
  qa_q1am_demodog *dog = as_dog(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  m->state->has_enemy = has;
  m->state->enemy = attacker;
  qa_q1am_actor_set_damageable(game, entity, false);
  entity->touch.present = false;
  qa_q1am_monster_count_kill(m);
  game->combat_set_health(game->ctx, entity->actor, -50);
  demodog_grenade(dog, false);
  demodog_grenade(dog, false);
  demodog_grenade(dog, false);
  if (game->options.skill > 2 && game->random(game->ctx) > 0.7) demodog_grenade(dog, true);
  game->sound(game->ctx, entity, "player/udeath.wav", "voice", 1);
  for (int i = 0; i < 3; i++) game->throw_gib(game->ctx, game, qa_q1am_monster_origin(m), "gib3", -50);
  game->throw_head(game->ctx, game, entity, "h_dog", true, -50);
}
void qa_q1am_demodog_jump_touch(qa_q1am_demodog *dog, qa_actor_id other) {
  qa_q1am_monster *m = &dog->base;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  if (game->health(game->ctx, entity->actor.id) <= 0) return;
  if (game->can_take_damage(game->ctx, other) && m->state->attack_finished < game->time &&
      qa_length3(game->body(game->ctx, entity).velocity) > 300) {
    game->damage(game->ctx, other, entity->actor.id, true, entity->actor.id,
                 10 + 10 * game->random(game->ctx));
    if (game->is_player(game->ctx, true, other)) {
      game->damage(game->ctx, entity->actor.id, other, true, other, 200);
      return;
    }
    m->state->attack_finished = game->time + 0.5;
  }
  if (!game->check_bottom(game->ctx, entity->actor.id)) {
    if ((FLAGS(entity->movement_flags) & 512) != 0) {
      entity->touch.present = false;
      qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, "demodog_leap1");
      qa_q1am_monster_delay(m, 0.1);
    }
    return;
  }
  entity->wait = 0;
  entity->touch.present = false;
  qa_q1am_monster_set_frame(m->next_frame, sizeof m->next_frame, m->spec.run);
  qa_q1am_monster_delay(m, 0.1);
}
void qa_q1am_demodog_init(qa_q1am_demodog *dog, qa_q1am_context *context, qa_q1am_actor *entity) {
  memset(dog, 0, sizeof *dog);
  dog->context = context;
  qa_q1am_monster_init(&dog->base, context->game, entity, &demodog_spec, context->base, &demodog_source);
  dog->base.owner = dog;
  qa_q1am_mg3_install(&dog->base);
  dog->base.kind = QA_Q1AM_KIND_DEMODOG;
  dog->super_ops = dog->base.ops;
  dog->base.ops.spawn = demodog_spawn;
  dog->base.ops.start = demodog_start;
  dog->base.ops.use = demodog_use;
  dog->base.ops.try_attack = demodog_try_attack;
  dog->base.ops.melee_attack = demodog_melee_attack;
  dog->base.ops.pain = demodog_pain;
  dog->base.ops.die = demodog_die;
}

typedef struct demodog_registry {
  qa_q1am_context *context;
  qa_q1am_demodog *head, *tail;
} demodog_registry;
static qa_q1am_demodog *registry_get(demodog_registry *r, qa_owned_actor actor) {
  for (qa_q1am_demodog *d = r->head; d; d = d->next)
    if (qa_q1am_same_owned(d->base.entity->actor, actor)) return d;
  return NULL;
}
static void registry_put(demodog_registry *r, qa_q1am_demodog *d) {
  qa_q1am_demodog *old = registry_get(r, d->base.entity->actor);
  if (old == d) return;
  if (old != NULL) {
    qa_q1am_demodog **link = &r->head;
    while (*link && *link != old) link = &(*link)->next;
    *link = old->next;
    if (r->tail == old) r->tail = NULL;
    free(old);
  }
  d->next = NULL;
  if (r->tail) r->tail->next = d;
  else r->head = d;
  r->tail = d;
}
static void registry_delete(demodog_registry *r, qa_owned_actor actor) {
  qa_q1am_demodog **link = &r->head;
  while (*link) {
    if (qa_q1am_same_owned((*link)->base.entity->actor, actor)) {
      qa_q1am_demodog *dead = *link;
      *link = dead->next;
      free(dead);
      return;
    }
    link = &(*link)->next;
  }
}
static void registry_clear(demodog_registry *r) {
  while (r->head) {
    qa_q1am_demodog *dead = r->head;
    r->head = dead->next;
    free(dead);
  }
  r->tail = NULL;
}
static qa_q1am_demodog *require_dog(demodog_registry *r, qa_q1am_actor *entity) {
  qa_q1am_demodog *d = registry_get(r, entity->actor);
  if (d == NULL) qa_range_error("Missing MG3 demodog source state");
  return d;
}
static qa_q1am_monster *lookup(void *ctx, qa_q1am_actor *entity) {
  return &require_dog(ctx, entity)->base;
}
static void on_release(void *ctx, qa_owned_actor actor) { registry_delete(ctx, actor); }
static void jump_touch_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                               bool has_normal, qa_vec3 normal) {
  (void)game; (void)has_normal; (void)normal;
  qa_q1am_demodog_jump_touch(require_dog(ctx, entity), other);
}
static void explode_grenade(qa_q1am_game *game, qa_q1am_actor *entity, bool has_ignore, qa_actor_id ignore) {
  game->radius_damage(game->ctx, entity->actor.id, entity->has_owner, entity->owner, entity->damage,
                      has_ignore, ignore, NULL);
  game->effect(game->ctx, "explosion", game->body(game->ctx, entity).origin, false, (qa_actor_id){0}, 1);
  game->remove(game->ctx, entity);
}
static void grenade_explode_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)ctx;
  explode_grenade(game, entity, game->world != NULL, game->world ? game->world->actor.id : (qa_actor_id){0});
}
static void grenade_touch_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity, qa_actor_id other,
                                  bool has_normal, qa_vec3 normal) {
  (void)has_normal; (void)normal;
  demodog_registry *r = ctx;
  if (entity->has_owner && qa_same_actor(other, entity->owner)) return;
  qa_q1am_actor *target = game->entity(game->ctx, true, other);
  if ((target && target->aimed_damage) || game->is_player(game->ctx, true, other)) {
    if (target && (strcmp(target->classname, "monster_boss") == 0 ||
                   strcmp(target->classname, "monster_oldone_new") == 0)) {
      game->damage(game->ctx, other, entity->actor.id, entity->has_owner, entity->owner, entity->damage);
      explode_grenade(game, entity, true, other);
      return;
    }
    explode_grenade(game, entity, game->world != NULL, game->world ? game->world->actor.id : (qa_actor_id){0});
    return;
  }
  if (qa_length3(game->body(game->ctx, entity).velocity) == 0) entity->angular_velocity = QA_Q1AM_ZERO;
  if (qa_q1am_actor_number(entity, "attack_finished") < game->time)
    game->sound(game->ctx, entity, "weapons/bounce.wav", "weapon", 1);
  r->context->set_number(r->context->ctx, entity, "attack_finished", game->time + 0.1);
}
static void spawn_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  demodog_registry *r = ctx;
  qa_q1am_demodog *value = malloc(sizeof *value);
  if (!value) qa_range_error("Demodog allocation failed");
  qa_q1am_demodog_init(value, r->context, entity);
  registry_put(r, value);
  value->base.ops.spawn(&value->base);
}
static uint8_t *capture_all(void *ctx, size_t *out_length) {
  demodog_registry *r = ctx;
  qa_save_value *list = qa_save_value_array();
  for (qa_q1am_demodog *d = r->head; d; d = d->next) {
    qa_save_value *object = qa_save_value_object();
    qa_save_value_set(object, "slot", qa_save_value_number((double)d->base.entity->actor.id.slot));
    qa_save_value_set(object, "generation", qa_save_value_number((double)d->base.entity->actor.id.generation));
    qa_save_value_set(object, "state", qa_q1am_monster_capture(&d->base));
    qa_save_value_push(list, object);
  }
  uint8_t *bytes = qa_encode_checkpoint_value(list, out_length);
  qa_save_value_free(list);
  return bytes;
}
static void restore_all(void *ctx, const uint8_t *bytes, size_t length) {
  demodog_registry *r = ctx;
  registry_clear(r);
  qa_q1am_game *game = r->context->game;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader root;
  qa_save_reader_init(&root, decoded, "mg3:demodog");
  size_t count = qa_save_reader_list_count(&root);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader reader, slot, generation, state;
    qa_save_reader_item(&root, i, &reader);
    qa_save_reader_field(&reader, "slot", &slot);
    qa_save_reader_field(&reader, "generation", &generation);
    qa_owned_actor owner;
    bool resolved = game->actors_resolve_saved(game->ctx, (int64_t)qa_save_reader_integer_min(&slot, 0),
                                                (int64_t)qa_save_reader_integer_min(&generation, 0), &owner);
    qa_save_reader_destroy(&slot);
    qa_save_reader_destroy(&generation);
    qa_q1am_actor *entity = resolved ? game->entity(game->ctx, true, owner.id) : NULL;
    if (!resolved || entity == NULL) qa_save_reader_fail(&reader, "missing demodog actor");
    qa_q1am_demodog *value = malloc(sizeof *value);
    if (!value) qa_range_error("Demodog allocation failed");
    qa_q1am_demodog_init(value, r->context, entity);
    registry_put(r, value);
    qa_save_reader_field(&reader, "state", &state);
    qa_q1am_monster_restore(&value->base, &state);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&root);
  qa_save_value_free(decoded);
}
static void clone_one(void *ctx, qa_q1am_actor *source, qa_q1am_actor *target) {
  demodog_registry *r = ctx;
  qa_q1am_demodog *value = registry_get(r, source->actor);
  if (value == NULL) return;
  qa_q1am_demodog *copy = malloc(sizeof *copy);
  if (!copy) qa_range_error("Demodog allocation failed");
  qa_q1am_demodog_init(copy, r->context, target);
  registry_put(r, copy);
  qa_save_value *captured = qa_q1am_monster_capture(&value->base);
  qa_save_reader reader;
  qa_save_reader_init(&reader, captured, "mg3:demodog");
  qa_q1am_monster_restore(&copy->base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
}
qa_q1am_demodog *qa_q1am_register_mg3_demodog(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  demodog_registry *r = calloc(1, sizeof *r);
  if (!r) qa_range_error("Demodog allocation failed");
  r->context = context;
  qa_q1am_monster_lookup lookup_value = {r, lookup};
  game->register_monster_callbacks(game->ctx, game, "mg3:demodog", lookup_value);
  qa_q1am_handlers handlers = {0};
  handlers.ctx = r;
  handlers.touch = jump_touch_handler;
  game->named_register(game->ctx, "mg3:demodog:jump_touch", handlers);
  qa_q1am_register_mg3_monster_startup(game, "mg3:demodog", lookup_value);
  handlers.touch = NULL;
  handlers.action = grenade_explode_handler;
  game->named_register(game->ctx, "mg3:demodog:grenade_explode", handlers);
  handlers.action = NULL;
  handlers.touch = grenade_touch_handler;
  game->named_register(game->ctx, "mg3:demodog:grenade_touch", handlers);
  game->register_spawn(game->ctx, "monster_demodog", spawn_handler, r);
  game->actors_on_release(game->ctx, on_release, r);
  qa_q1am_state_extension extension = {"mg3:demodog", r, capture_all, restore_all, clone_one};
  game->register_state_extension(game->ctx, extension);
  return r->head;
}
