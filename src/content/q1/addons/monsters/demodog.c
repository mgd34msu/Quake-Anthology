/* Port of src/content/q1/addons/monsters/demodog.ts.
 * quakec_mg3/monsters/mg3_demodog.qc and monsters.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/demodog.h"
#include "qa/foundation/support.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/demodog_frames.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/content/q1/base/projectiles.h"
#include "support.h"

#define PREFIX "mg3:demodog"
#define FLAGS QA_Q1AM_FLAGS

static const char *const demodog_classnames[] = {"monster_demodog"};
static const char *const demodog_gibs[] = {"gib3", "gib3", "gib3"};
static const qa_q1_base_monster_species demodog_spec = {
  .species = "dog", .classnames = demodog_classnames, .classname_count = 1, .model = "dog_explosive", .head = "h_dog",
  .health = 25, .gib_health = -35, .gibs = demodog_gibs, .gib_count = 3, .bounds = {{-32, -32, -24}, {32, 32, 64}},
  .stand = "demodog_stand1", .walk = "demodog_walk1", .run = "demodog_run1", .sight = "dog/dsight.wav",
  .missile = "demodog_leap1", .melee = true, .movement = "walk",
};
static void demodog_bite(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  if (!monster->state->has_enemy) return;
  qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 10);
  (void)qa_q1_base_monster_melee(monster, 100, 8, 3, true);
}
static void demodog_jump(void *ctx, qa_q1_base_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, PREFIX ":jump_touch");
  entity->movement_flags = FLAGS(entity->movement_flags) & ~512;
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = qa_add3(body.origin, (qa_vec3){0, 0, 1});
  patch.has_velocity = true;
  patch.velocity = qa_add3(qa_scale3(qa_q1_make_vectors(game, body.angles).forward, 300), (qa_vec3){0, 0, 200});
  patch.has_ground = true;
  patch.ground = NULL;
  qa_q1_set_body(game, entity, &patch);
}
static const qa_q1_monster_action_entry demodog_actions[] = {
  {"demodog_bite", demodog_bite, NULL}, {"demodog_jump", demodog_jump, NULL},
};

static void demodog_spawn(qa_q1_base_monster *monster);
static void demodog_start(qa_q1_base_monster *monster);
static void demodog_use(qa_q1_base_monster *monster, const qa_actor_id *activator);
static bool demodog_try_attack(qa_q1_base_monster *monster);
static void demodog_melee_attack(qa_q1_base_monster *monster) { qa_q1_base_monster_play(monster, "demodog_atta1"); }
static void demodog_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage);
static void demodog_die(qa_q1_base_monster *monster, const qa_actor_id *attacker);
static void demodog_destroy(qa_q1_base_monster *monster) { free(monster); }
static const qa_q1_base_monster_ops demodog_ops = {
  QA_Q1AM_MG3_MONSTER_OPS,
  .spawn = demodog_spawn, .start = demodog_start, .use = demodog_use, .try_attack = demodog_try_attack,
  .melee_attack = demodog_melee_attack, .pain = demodog_pain, .die = demodog_die, .destroy = demodog_destroy,
};
bool qa_q1am_is_demodog(const qa_q1_base_monster *monster) { return monster->ops == &demodog_ops; }
static qa_q1am_demodog *dog_of(qa_q1_base_monster *monster) { return (qa_q1am_demodog *)monster; }

bool qa_q1am_demodog_init(qa_q1am_demodog *dog, qa_q1ad_context *context, qa_q1_actor *entity) {
  memset(dog, 0, sizeof *dog);
  dog->context = context;
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = PREFIX;
  source.frames = &qa_q1am_demodog_frames;
  source.actions = demodog_actions;
  source.action_count = sizeof demodog_actions / sizeof demodog_actions[0];
  return qa_q1am_mg3_monster_init(&dog->base, context->game, entity, &demodog_spec,
                                  qa_q1_base_as_monster_services(context->base), &source, &demodog_ops, NULL);
}
static void demodog_spawn(qa_q1_base_monster *monster) {
  qa_q1ad_context *context = dog_of(monster)->context;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1ad_set_text(&entity->classname, "monster_dog");
  qa_q1ad_set_number(context, entity, "aflag", 1);
  entity->wait = 0;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 25);
  entity->max_health = 25;
  entity->pain = qa_q1_callback_registry_pain(game->named, entity, PREFIX ":monster_pain");
  entity->die = qa_q1_callback_registry_die(game->named, entity, PREFIX ":monster_die");
  entity->path_end = qa_q1_callback_registry_action(game->named, entity, PREFIX ":monster_stand");
  qa_q1am_field_text(entity, "allowPathFind", "1");
  qa_q1am_field_text(entity, "combat_style", "2");
  qa_q1am_init_mg3_monster(monster, context, "progs/dog_explosive.mdl", 1, 2);
}
static void demodog_start(qa_q1_base_monster *monster) { qa_q1am_start_mg3_monster(monster, dog_of(monster)->context); }
static void demodog_use(qa_q1_base_monster *monster, const qa_actor_id *activator) {
  qa_actor_id resolved;
  const bool has = qa_q1am_mg3_monster_activator(monster->game, activator, &resolved);
  qa_q1_base_monster_use_super(monster, has ? &resolved : NULL);
}
static bool demodog_try_attack(qa_q1_base_monster *monster) {
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_body target;
  if (!monster->state->has_enemy || !qa_q1am_read_body(game, monster->state->enemy, &target)) return false;
  if ((game->world == NULL ? 0 : qa_q1_actor_number(game->world, "enemy_range")) == 0) {
    entity->attack_state = "melee";
    return true;
  }
  const qa_q1_body body = qa_q1_get_body(game, entity);
  const qa_vec3 delta = qa_sub3(target.origin, body.origin);
  const double height = target.bounds.max.z - target.bounds.min.z;
  if (body.origin.z + body.bounds.min.z > target.origin.z + target.bounds.min.z + height * 0.75 ||
      body.origin.z + body.bounds.max.z < target.origin.z + target.bounds.min.z + height * 0.25)
    return false;
  const double distance = qa_js_hypot2(delta.x, delta.y);
  if (distance < 80 || distance > 150) return false;
  entity->attack_state = "missile";
  return true;
}
static void demodog_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)damage; /* TS pain(attacker, _damage) */
  qa_q1_base_monster_retaliate(monster, attacker);
  qa_q1_sound(monster->game, monster->entity->actor.id, "dog/dpain1.wav", "voice", 1, 1);
  qa_q1_base_monster_play(monster, qa_q1am_random(monster->game) > 0.5 ? "demodog_pain1" : "demodog_painb1");
}
/* TS private grenade(vertical). */
static void grenade(qa_q1am_demodog *dog, bool vertical) {
  qa_q1_base_monster *monster = &dog->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_vec3 raw;
  raw.x = 100 * (qa_q1am_random(game) * 2 - 1);
  raw.y = 100 * (qa_q1am_random(game) * 2 - 1);
  raw.z = 200 + 100 * qa_q1am_random(game);
  qa_vec3 velocity = qa_scale3(raw, 1.5);
  if (vertical) velocity = (qa_vec3){0, 0, velocity.z};
  velocity = qa_add3(velocity, qa_scale3(qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles).forward, 100));
  qa_q1_sound(game, entity->actor.id, "weapons/grenade.wav", "weapon", 1, 1);
  qa_q1_actor *shell = qa_q1_create(game, "grenade", NULL, NULL);
  shell->has_owner = true;
  shell->owner = entity->actor.id;
  shell->movement = "bounce";
  shell->solid = "bbox";
  shell->projectile = "grenade";
  shell->damage = 60;
  qa_q1ad_set_text(&shell->model, "progs/grenade.mdl");
  shell->angular_velocity = (qa_vec3){300, 300, 300};
  qa_q1ad_set_vector(dog->context, shell, "oldorigin", velocity);
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = qa_q1_base_monster_origin(monster);
  patch.has_velocity = true;
  patch.velocity = velocity;
  patch.has_bounds = true;
  patch.bounds = QA_Q1_POINT;
  patch.has_angles = true;
  patch.angles = (qa_vec3){atan2(velocity.z, qa_js_hypot2(velocity.x, velocity.y)) * 180 / 3.141592653589793,
                           qa_q1_yaw_for(velocity), 0};
  qa_q1_set_body(game, shell, &patch);
  shell->touch = qa_q1_callback_registry_touch(game->named, shell, PREFIX ":grenade_touch");
  qa_q1_link(game, shell);
  double delay = 2.5 + 0.25 * (qa_q1am_random(game) * 2 - 1);
  qa_q1_bound_action callback = qa_q1_callback_registry_action(game->named, shell, PREFIX ":grenade_explode");
  qa_q1_schedule(game, shell, delay, callback);
}
static void demodog_die(qa_q1_base_monster *monster, const qa_actor_id *attacker) {
  if (monster->counted_death) return;
  qa_q1am_demodog *dog = dog_of(monster);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_set_enemy(monster, attacker);
  qa_q1_actor_set_damageable(entity, false);
  memset(&entity->touch, 0, sizeof entity->touch);
  qa_q1_base_monster_count_kill(monster);
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, -50);
  grenade(dog, false);
  grenade(dog, false);
  grenade(dog, false);
  if (qa_q1_option_skill(game) > 2 && qa_q1am_random(game) > 0.7) grenade(dog, true);
  qa_q1_sound(game, entity->actor.id, "player/udeath.wav", "voice", 1, 1);
  for (int i = 0; i < 3; i++) (void)qa_q1_throw_gib(game, qa_q1_base_monster_origin(monster), "gib3", -50);
  qa_q1_throw_head_damage(game, entity, "h_dog", -50);
}
void qa_q1am_demodog_jump_touch(qa_q1am_demodog *dog, qa_actor_id other) {
  qa_q1_base_monster *monster = &dog->base.base;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  if (qa_q1_health(game, entity->actor.id) <= 0) return;
  if (qa_q1am_can_take_damage(game, other) && monster->state->attack_finished < game->time &&
      qa_length3(qa_q1_get_body(game, entity).velocity) > 300) {
    const qa_actor_id self = entity->actor.id;
    (void)qa_q1_damage(game, other, self, &self, 10 + 10 * qa_q1am_random(game), NULL, NULL, NULL, NULL);
    if (qa_q1_is_player(game, &other)) {
      (void)qa_q1_damage(game, self, other, &other, 200, NULL, NULL, NULL, NULL);
      return;
    }
    monster->state->attack_finished = game->time + 0.5;
  }
  if (!game->host.check_bottom(game->host.ctx, entity->actor.id)) {
    if ((FLAGS(entity->movement_flags) & 512) != 0) {
      memset(&entity->touch, 0, sizeof entity->touch);
      qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, "demodog_leap1");
      qa_q1_base_monster_delay(monster, 0.1);
    }
    return;
  }
  entity->wait = 0;
  memset(&entity->touch, 0, sizeof entity->touch);
  qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, demodog_spec.run);
  qa_q1_base_monster_delay(monster, 0.1);
}

/* ---- registerMg3Demodog ---- */
struct qa_q1am_demodog_registry {
  qa_q1ad_context *context;
  struct { qa_owned_actor actor; qa_q1am_demodog *monster; } *entries; size_t count, capacity;
  qa_q1am_demodog **retired; size_t retired_count, retired_capacity;
  void *monster_callbacks;
  int64_t release_subscription;
  qa_q1_state_extension extension;
  qa_q1am_demodog_registry *next;
};
static qa_q1am_demodog_registry *all_registries;
static void *grow(void *items, size_t *capacity, size_t size) {
  const size_t next = *capacity != 0 ? *capacity * 2 : 8;
  void *grown = realloc(items, next * size);
  if (grown == NULL) qa_range_error("out of memory");
  *capacity = next;
  return grown;
}
static void retire(qa_q1am_demodog_registry *r, qa_q1am_demodog *monster) {
  if (r->retired_count == r->retired_capacity) r->retired = grow(r->retired, &r->retired_capacity, sizeof *r->retired);
  r->retired[r->retired_count++] = monster;
}
qa_q1am_demodog *qa_q1am_demodog_registry_get(const qa_q1am_demodog_registry *monsters, qa_owned_actor actor) {
  for (size_t i = 0; i < monsters->count; i++)
    if (qa_same_owned_actor(monsters->entries[i].actor, actor)) return monsters->entries[i].monster;
  return NULL;
}
static void registry_set(qa_q1am_demodog_registry *r, qa_owned_actor actor, qa_q1am_demodog *monster) {
  for (size_t i = 0; i < r->count; i++)
    if (qa_same_owned_actor(r->entries[i].actor, actor)) {
      if (r->entries[i].monster != monster) retire(r, r->entries[i].monster);
      r->entries[i].monster = monster;
      return;
    }
  if (r->count == r->capacity) r->entries = grow(r->entries, &r->capacity, sizeof *r->entries);
  r->entries[r->count].actor = actor;
  r->entries[r->count].monster = monster;
  r->count++;
}
static qa_q1am_demodog *create(qa_q1am_demodog_registry *r, qa_q1_actor *entity) {
  qa_q1am_demodog *dog = malloc(sizeof *dog);
  if (dog == NULL || !qa_q1am_demodog_init(dog, r->context, entity)) qa_range_error("out of memory");
  return dog;
}
/* TS monster(entity). */
static qa_q1am_demodog *require(qa_q1am_demodog_registry *r, qa_q1_actor *entity) {
  qa_q1am_demodog *value = qa_q1am_demodog_registry_get(r, entity->actor);
  if (value == NULL) qa_range_error("Missing MG3 demodog source state");
  return value;
}
static qa_q1_base_monster *lookup(void *ctx, qa_q1_actor *entity) { return &require(ctx, entity)->base.base; }
static void jump_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)game; (void)normal; (void)surface; /* TS (_game, entity, other) */
  qa_q1am_demodog_jump_touch(require(ctx, entity), other);
}
static void explode(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *ignore) {
  qa_q1_radius_damage(game, entity->actor.id, qa_q1am_opt(entity->has_owner, &entity->owner), entity->damage, ignore, NULL, NULL);
  qa_q1_effect(game, "explosion", qa_q1_get_body(game, entity).origin, NULL, 1);
  qa_q1_remove(game, entity);
}
static void grenade_explode(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  explode(game, entity, game->world == NULL ? NULL : &game->world->actor.id);
}
static void grenade_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                          const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS (_game, entity, other) */
  qa_q1am_demodog_registry *r = ctx;
  if (entity->has_owner && qa_same_actor(other, entity->owner)) return;
  const qa_q1_actor *target = qa_q1_get_entity(game, &other);
  if ((target != NULL && target->aimed_damage) || qa_q1_is_player(game, &other)) {
    if (target != NULL && (strcmp(target->classname, "monster_boss") == 0 || strcmp(target->classname, "monster_oldone_new") == 0)) {
      (void)qa_q1_damage(game, other, entity->actor.id, qa_q1am_opt(entity->has_owner, &entity->owner), entity->damage,
                         NULL, NULL, NULL, NULL);
      explode(game, entity, &other);
      return;
    }
    explode(game, entity, game->world == NULL ? NULL : &game->world->actor.id);
    return;
  }
  if (qa_length3(qa_q1_get_body(game, entity).velocity) == 0) entity->angular_velocity = QA_Q1_ZERO;
  if (qa_q1_actor_number(entity, "attack_finished") < game->time)
    qa_q1_sound(game, entity->actor.id, "weapons/bounce.wav", "weapon", 1, 1);
  qa_q1ad_set_number(r->context, entity, "attack_finished", game->time + 0.1);
}
static void spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game; /* TS (_game, entity) */
  qa_q1am_demodog_registry *r = ctx;
  qa_q1am_demodog *value = create(r, entity);
  registry_set(r, entity->actor, value);
  qa_q1_base_monster_spawn(&value->base.base);
}
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q1am_demodog_registry *r = ctx;
  for (size_t i = 0; i < r->count; i++)
    if (qa_same_owned_actor(r->entries[i].actor, actor)) {
      retire(r, r->entries[i].monster);
      memmove(&r->entries[i], &r->entries[i + 1], (r->count - i - 1) * sizeof *r->entries);
      r->count--;
      return;
    }
}
static void must(bool ok) { if (!ok) qa_range_error("out of memory"); }
static void set_child(qa_save_value *object, const char *key, qa_save_value *child) {
  must(child != NULL && qa_save_value_set(object, key, child));
}
static bool capture(void *ctx, uint8_t **out, size_t *out_length) {
  qa_q1am_demodog_registry *r = ctx;
  qa_save_value *list = qa_save_value_array();
  if (list == NULL) return false;
  for (size_t i = 0; i < r->count; i++) {
    const qa_q1_base_monster *value = &r->entries[i].monster->base.base;
    qa_save_value *object = qa_save_value_object();
    must(object != NULL);
    set_child(object, "slot", qa_save_value_number((double)value->entity->actor.id.slot));
    set_child(object, "generation", qa_save_value_number((double)value->entity->actor.id.generation));
    set_child(object, "state", qa_q1_base_monster_capture(value));
    must(qa_save_value_push(list, object));
  }
  *out = qa_encode_checkpoint_value(list, out_length);
  qa_save_value_free(list);
  return *out != NULL;
}
static double field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader value;
  must(qa_save_reader_field(reader, name, &value));
  const double result = qa_save_reader_integer_min(&value, 0);
  qa_save_reader_destroy(&value);
  return result;
}
static void q1_free_decoded_save(void *value) { qa_save_value_free(value); }
static void restore(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q1am_demodog_registry *r = ctx;
  qa_q1_services *game = r->context->game;
  for (size_t i = 0; i < r->count; i++) retire(r, r->entries[i].monster);
  r->count = 0;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_cleanup decoded_cleanup; /* a malformed save raises from the readers */
  qa_cleanup_push(&decoded_cleanup, q1_free_decoded_save, decoded);
  qa_save_reader root;
  must(qa_save_reader_init(&root, decoded, PREFIX));
  const size_t count = qa_save_reader_list_count(&root);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader reader, state;
    must(qa_save_reader_item(&root, i, &reader));
    qa_saved_actor_id saved;
    saved.slot = field_integer(&reader, "slot");
    saved.generation = field_integer(&reader, "generation");
    qa_owned_actor owner;
    const bool resolved = qa_session_actor_registry_resolve_saved(game->host.actors, saved, &owner);
    qa_q1_actor *entity = resolved ? qa_q1_get_entity(game, &owner.id) : NULL;
    if (!resolved || entity == NULL) qa_save_reader_fail(&reader, "missing demodog actor");
    qa_q1am_demodog *value = create(r, entity);
    must(qa_save_reader_field(&reader, "state", &state));
    qa_q1_base_monster_restore(&value->base.base, &state);
    qa_save_reader_destroy(&state);
    registry_set(r, owner, value);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&root);
  qa_cleanup_pop(&decoded_cleanup);
  qa_save_value_free(decoded);
}
static void clone(void *ctx, qa_q1_actor *source, qa_q1_actor *target) {
  qa_q1am_demodog_registry *r = ctx;
  qa_q1am_demodog *value = qa_q1am_demodog_registry_get(r, source->actor);
  if (value == NULL) return;
  qa_q1am_demodog *copy = create(r, target);
  qa_save_value *captured = qa_q1_base_monster_capture(&value->base.base);
  must(captured != NULL);
  qa_save_reader reader;
  must(qa_save_reader_init(&reader, captured, PREFIX));
  qa_q1_base_monster_restore(&copy->base.base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
  registry_set(r, target->actor, copy);
}
qa_q1am_demodog_registry *qa_q1am_register_mg3_demodog(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1am_demodog_registry *r = calloc(1, sizeof *r);
  if (r == NULL) qa_range_error("out of memory");
  r->context = context;
  r->next = all_registries;
  all_registries = r;
  r->monster_callbacks = qa_q1_register_monster_callbacks(game, PREFIX, lookup, r);
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = r;
  handlers.touch = jump_touch;
  qa_q1_callback_registry_register(game->named, PREFIX ":jump_touch", handlers);
  qa_q1am_register_mg3_monster_startup(game, PREFIX, lookup, r);
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = r;
  handlers.action = grenade_explode;
  qa_q1_callback_registry_register(game->named, PREFIX ":grenade_explode", handlers);
  memset(&handlers, 0, sizeof handlers);
  handlers.ctx = r;
  handlers.touch = grenade_touch;
  qa_q1_callback_registry_register(game->named, PREFIX ":grenade_touch", handlers);
  qa_q1_register_spawn(game, "monster_demodog", spawn, r);
  r->release_subscription = qa_session_actor_registry_on_release(game->host.actors, on_release, r);
  r->extension.ctx = r;
  r->extension.id = PREFIX;
  r->extension.capture = capture;
  r->extension.restore = restore;
  r->extension.clone = clone;
  qa_q1_register_state_extension(game, &r->extension);
  return r;
}
void qa_q1am_release_mg3_demodog(qa_q1_services *game) {
  for (qa_q1am_demodog_registry **link = &all_registries; *link != NULL;) {
    qa_q1am_demodog_registry *r = *link;
    if (r->context->game != game) { link = &r->next; continue; }
    *link = r->next;
    qa_session_actor_registry_off_release(game->host.actors, r->release_subscription);
    for (size_t i = 0; i < r->count; i++) qa_q1_base_monster_destroy(&r->entries[i].monster->base.base);
    for (size_t i = 0; i < r->retired_count; i++) qa_q1_base_monster_destroy(&r->retired[i]->base.base);
    free(r->entries);
    free(r->retired);
    free(r->monster_callbacks);
    free(r);
  }
}
