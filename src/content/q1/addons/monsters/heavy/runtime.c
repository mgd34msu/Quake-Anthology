/* Port of src/content/q1/addons/monsters/heavy/runtime.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/foundation/support.h"

static qa_q1am_heavy_monster *as_heavy(qa_q1am_monster *m) { return m->owner; }
static void wrapped_action(void *ctx, qa_q1am_monster *monster) {
  const qa_q1am_heavy_action_entry *entry = ctx;
  entry->fn(as_heavy(monster));
}
static void heavy_spawn(qa_q1am_monster *m) {
  qa_q1am_heavy_monster *self = as_heavy(m);
  self->definition->spawn(self);
}
static void heavy_start(qa_q1am_monster *m) {
  qa_q1am_start_mg3_monster(m, as_heavy(m)->runtime->context);
}
static void heavy_use(qa_q1am_monster *m, bool has, qa_actor_id activator) {
  qa_actor_id resolved;
  bool resolved_has = qa_q1am_mg3_monster_activator(m->game, has, activator, &resolved);
  as_heavy(m)->super_ops.use(m, resolved_has, resolved);
}
static const qa_q1am_heavy_action_entry *find_action(const qa_q1am_heavy_definition *definition, const char *name) {
  for (size_t i = 0; i < definition->action_count; i++)
    if (strcmp(definition->actions[i].name, name) == 0) return &definition->actions[i];
  return NULL;
}
static void heavy_play(qa_q1am_monster *m, const char *name) {
  qa_q1am_heavy_monster *self = as_heavy(m);
  const qa_q1am_heavy_action_entry *action = find_action(self->definition, name);
  if (qa_q1am_frame_table_get(self->definition->frames, name) == NULL && action != NULL) {
    action->fn(self);
    return;
  }
  self->super_ops.play(m, name);
}
static void heavy_sight_sound(qa_q1am_monster *m) {
  qa_q1am_heavy_monster *self = as_heavy(m);
  if (self->definition->sight == NULL) self->super_ops.sight_sound(m);
  else self->definition->sight(self);
}
static void heavy_melee_attack(qa_q1am_monster *m) {
  qa_q1am_heavy_monster *self = as_heavy(m);
  if (self->definition->melee) self->definition->melee(self);
}
static bool heavy_try_attack(qa_q1am_monster *m) {
  qa_q1am_heavy_monster *self = as_heavy(m);
  if (self->definition->attack) return self->definition->attack(self);
  return m->ops.check_attack(m);
}
static void heavy_pain(qa_q1am_monster *m, bool has, qa_actor_id attacker, double damage) {
  qa_q1am_monster_retaliate(m, has, attacker);
  qa_q1am_heavy_monster *self = as_heavy(m);
  self->definition->pain(self, has, attacker, damage);
}
static void heavy_die(qa_q1am_monster *m, bool has, qa_actor_id attacker) {
  if (m->counted_death) return;
  qa_q1am_heavy_monster *self = as_heavy(m);
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  m->state->has_enemy = has;
  m->state->enemy = attacker;
  if (game->health(game->ctx, entity->actor.id) < -99)
    game->combat_set_health(game->ctx, entity->actor, -99);
  qa_q1am_actor_set_damageable(game, entity, false);
  entity->touch.present = false;
  qa_q1am_monster_count_kill(m);
  self->definition->die(self);
}
void qa_q1am_heavy_monster_set_number(qa_q1am_heavy_monster *monster, const char *key, double value) {
  monster->runtime->context->set_number(monster->runtime->context->ctx, monster->base.entity, key, value);
}
void qa_q1am_heavy_monster_install_callbacks(qa_q1am_heavy_monster *monster) {
  qa_q1am_game *game = monster->base.game;
  qa_q1am_actor *entity = monster->base.entity;
  entity->pain = game->named_pain(game->ctx, entity, QA_Q1AM_HEAVY_PREFIX ":monster_pain");
  entity->die = game->named_die(game->ctx, entity, QA_Q1AM_HEAVY_PREFIX ":monster_die");
  entity->path_end = game->named_action(game->ctx, entity, QA_Q1AM_HEAVY_PREFIX ":monster_stand");
}
void qa_q1am_heavy_monster_initialize(qa_q1am_heavy_monster *monster, double size) {
  qa_q1am_heavy_monster_install_callbacks(monster);
  char model[192];
  (void)snprintf(model, sizeof model, "progs/%s.mdl", monster->base.spec.model);
  qa_q1am_init_mg3_monster(&monster->base, monster->runtime->context, model, 1, size);
}
void qa_q1am_heavy_monster_init(qa_q1am_heavy_monster *monster, qa_q1am_mg3_heavy *runtime,
                                qa_q1am_actor *entity, const qa_q1am_heavy_definition *definition) {
  memset(monster, 0, sizeof *monster);
  monster->runtime = runtime;
  monster->definition = definition;
  monster->wrapped_actions = calloc(definition->action_count ? definition->action_count : 1,
                                    sizeof *monster->wrapped_actions);
  if (!monster->wrapped_actions) qa_range_error("Heavy monster allocation failed");
  for (size_t i = 0; i < definition->action_count; i++) {
    monster->wrapped_actions[i].name = definition->actions[i].name;
    monster->wrapped_actions[i].ctx = (void *)&definition->actions[i];
    monster->wrapped_actions[i].fn = wrapped_action;
  }
  monster->source.callback_prefix = QA_Q1AM_HEAVY_PREFIX;
  monster->source.frames = definition->frames;
  monster->source.actions = monster->wrapped_actions;
  monster->source.action_count = definition->action_count;
  qa_q1am_monster_init(&monster->base, runtime->context->game, entity, &definition->spec,
                       runtime->context->base, &monster->source);
  monster->base.owner = monster;
  qa_q1am_mg3_install(&monster->base);
  monster->base.kind = QA_Q1AM_KIND_HEAVY;
  monster->super_ops = monster->base.ops;
  monster->base.ops.spawn = heavy_spawn;
  monster->base.ops.start = heavy_start;
  monster->base.ops.use = heavy_use;
  monster->base.ops.play = heavy_play;
  monster->base.ops.sight_sound = heavy_sight_sound;
  monster->base.ops.melee_attack = heavy_melee_attack;
  monster->base.ops.try_attack = heavy_try_attack;
  monster->base.ops.pain = heavy_pain;
  monster->base.ops.die = heavy_die;
}

static qa_q1am_heavy_monster *registry_get(qa_q1am_mg3_heavy *r, qa_owned_actor actor) {
  for (qa_q1am_heavy_monster *m = r->head; m; m = m->next)
    if (qa_q1am_same_owned(m->base.entity->actor, actor)) return m;
  return NULL;
}
static void registry_put(qa_q1am_mg3_heavy *r, qa_q1am_heavy_monster *m) {
  qa_q1am_heavy_monster *old = registry_get(r, m->base.entity->actor);
  if (old == m) return;
  if (old != NULL) {
    qa_q1am_heavy_monster **link = &r->head;
    while (*link && *link != old) link = &(*link)->next;
    *link = old->next;
    if (r->tail == old) r->tail = NULL;
    free(old->wrapped_actions);
    free(old);
  }
  m->next = NULL;
  if (r->tail) r->tail->next = m;
  else r->head = m;
  r->tail = m;
}
static void registry_delete(qa_q1am_mg3_heavy *r, qa_owned_actor actor) {
  qa_q1am_heavy_monster **link = &r->head;
  while (*link) {
    if (qa_q1am_same_owned((*link)->base.entity->actor, actor)) {
      qa_q1am_heavy_monster *dead = *link;
      *link = dead->next;
      free(dead->wrapped_actions);
      free(dead);
      return;
    }
    link = &(*link)->next;
  }
}
static void registry_clear(qa_q1am_mg3_heavy *r) {
  while (r->head) {
    qa_q1am_heavy_monster *dead = r->head;
    r->head = dead->next;
    free(dead->wrapped_actions);
    free(dead);
  }
  r->tail = NULL;
}
qa_q1am_heavy_monster *qa_q1am_mg3_heavy_require(qa_q1am_mg3_heavy *runtime, qa_q1am_actor *entity) {
  qa_q1am_heavy_monster *m = registry_get(runtime, entity->actor);
  if (m == NULL) qa_range_error("Missing MG3 heavy source controller for %s", entity->classname);
  return m;
}
static qa_q1am_monster *lookup(void *ctx, qa_q1am_actor *entity) {
  return &qa_q1am_mg3_heavy_require(ctx, entity)->base;
}
static void on_release(void *ctx, qa_owned_actor actor) { registry_delete(ctx, actor); }
static qa_q1am_heavy_monster *create_heavy(qa_q1am_mg3_heavy *r, qa_q1am_actor *entity,
                                           const qa_q1am_heavy_definition *definition) {
  qa_q1am_heavy_monster *m = malloc(sizeof *m);
  if (!m) qa_range_error("Heavy monster allocation failed");
  qa_q1am_heavy_monster_init(m, r, entity, definition);
  registry_put(r, m);
  return m;
}
static void source_die_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  qa_q1am_heavy_monster *monster = qa_q1am_mg3_heavy_require(ctx, entity);
  monster->definition->die(monster);
}
typedef struct spawn_ctx { qa_q1am_mg3_heavy *runtime; const qa_q1am_heavy_definition *definition; } spawn_ctx;
static void spawn_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  spawn_ctx *state = ctx;
  qa_q1am_heavy_monster *m = create_heavy(state->runtime, entity, state->definition);
  m->base.ops.spawn(&m->base);
}
static uint8_t *capture_all(void *ctx, size_t *out_length) {
  qa_q1am_mg3_heavy *r = ctx;
  qa_save_value *root = qa_save_value_object();
  qa_save_value_set(root, "runeKnightMeleeCycle", qa_save_value_number(r->rune_knight_melee_cycle));
  qa_save_value *list = qa_save_value_array();
  for (qa_q1am_heavy_monster *m = r->head; m; m = m->next) {
    qa_save_value *object = qa_save_value_object();
    qa_save_value *actor = qa_save_value_object();
    qa_save_value_set(actor, "slot", qa_save_value_number((double)m->base.entity->actor.id.slot));
    qa_save_value_set(actor, "generation", qa_save_value_number((double)m->base.entity->actor.id.generation));
    qa_save_value_set(object, "actor", actor);
    qa_save_value_set(object, "definition",
                      qa_save_value_string(m->base.spec.classname_count > 0
                                               ? m->base.spec.classnames[0] : m->base.entity->classname));
    qa_save_value_set(object, "state", qa_q1am_monster_capture(&m->base));
    qa_save_value_push(list, object);
  }
  qa_save_value_set(root, "monsters", list);
  uint8_t *bytes = qa_encode_checkpoint_value(root, out_length);
  qa_save_value_free(root);
  return bytes;
}
static const qa_q1am_heavy_definition *definition_for(qa_q1am_mg3_heavy *r, const char *classname) {
  for (size_t i = 0; i < r->definition_count; i++)
    for (size_t c = 0; c < r->definitions[i]->spec.classname_count; c++)
      if (strcmp(r->definitions[i]->spec.classnames[c], classname) == 0) return r->definitions[i];
  return NULL;
}
static void restore_all(void *ctx, const uint8_t *bytes, size_t length) {
  qa_q1am_mg3_heavy *r = ctx;
  qa_q1am_game *game = r->context->game;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader root, cycle, list;
  qa_save_reader_init(&root, decoded, QA_Q1AM_HEAVY_PREFIX);
  qa_save_reader_field(&root, "runeKnightMeleeCycle", &cycle);
  r->rune_knight_melee_cycle = qa_save_reader_number(&cycle);
  qa_save_reader_destroy(&cycle);
  registry_clear(r);
  qa_save_reader_field(&root, "monsters", &list);
  size_t count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader reader, saved, slot, generation, definition_reader, state;
    qa_save_reader_item(&list, i, &reader);
    qa_save_reader_field(&reader, "actor", &saved);
    qa_save_reader_field(&saved, "slot", &slot);
    qa_save_reader_field(&saved, "generation", &generation);
    qa_owned_actor owner;
    bool resolved = game->actors_resolve_saved(game->ctx, (int64_t)qa_save_reader_integer_min(&slot, 0),
                                                (int64_t)qa_save_reader_integer_min(&generation, 0), &owner);
    qa_save_reader_destroy(&slot);
    qa_save_reader_destroy(&generation);
    qa_save_reader_destroy(&saved);
    qa_q1am_actor *entity = resolved ? game->entity(game->ctx, true, owner.id) : NULL;
    if (entity == NULL) qa_save_reader_fail(&reader, "missing heavy monster actor");
    qa_save_reader_field(&reader, "definition", &definition_reader);
    const qa_q1am_heavy_definition *definition = definition_for(r, qa_save_reader_string(&definition_reader));
    qa_save_reader_destroy(&definition_reader);
    if (definition == NULL) qa_save_reader_fail(&reader, "unknown heavy monster definition");
    qa_q1am_heavy_monster *m = create_heavy(r, entity, definition);
    qa_save_reader_field(&reader, "state", &state);
    qa_q1am_monster_restore(&m->base, &state);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&list);
  qa_save_reader_destroy(&root);
  qa_save_value_free(decoded);
}
static void clone_one(void *ctx, qa_q1am_actor *source, qa_q1am_actor *target) {
  qa_q1am_mg3_heavy *r = ctx;
  qa_q1am_heavy_monster *monster = registry_get(r, source->actor);
  if (monster == NULL) return;
  qa_q1am_heavy_monster *copy = create_heavy(r, target, monster->definition);
  qa_save_value *captured = qa_q1am_monster_capture(&monster->base);
  qa_save_reader reader;
  qa_save_reader_init(&reader, captured, QA_Q1AM_HEAVY_PREFIX);
  qa_q1am_monster_restore(&copy->base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
}
void qa_q1am_mg3_heavy_init(qa_q1am_mg3_heavy *runtime, qa_q1am_context *context) {
  memset(runtime, 0, sizeof *runtime);
  runtime->context = context;
  qa_q1am_game *game = context->game;
  qa_q1am_monster_lookup lookup_value = {runtime, lookup};
  game->register_monster_callbacks(game->ctx, game, QA_Q1AM_HEAVY_PREFIX, lookup_value);
  qa_q1am_register_mg3_monster_startup(game, QA_Q1AM_HEAVY_PREFIX, lookup_value);
  qa_q1am_handlers handlers = {0};
  handlers.ctx = runtime;
  handlers.action = source_die_handler;
  game->named_register(game->ctx, QA_Q1AM_HEAVY_PREFIX ":source_die", handlers);
  game->actors_on_release(game->ctx, on_release, runtime);
  qa_q1am_state_extension extension = {QA_Q1AM_HEAVY_PREFIX, runtime, capture_all, restore_all, clone_one};
  game->register_state_extension(game->ctx, extension);
}
void qa_q1am_mg3_heavy_register(qa_q1am_mg3_heavy *runtime, const qa_q1am_heavy_definition *definition) {
  qa_q1am_game *game = runtime->context->game;
  for (size_t i = 0; i < definition->callback_count; i++) {
    char name[220];
    (void)snprintf(name, sizeof name, "%s:%s", QA_Q1AM_HEAVY_PREFIX, definition->callbacks[i].name);
    game->named_register(game->ctx, name, definition->callbacks[i].handlers);
  }
  if (runtime->definition_count < 16) runtime->definitions[runtime->definition_count++] = definition;
  for (size_t c = 0; c < definition->spec.classname_count; c++) {
    spawn_ctx *state = malloc(sizeof *state);
    if (!state) qa_range_error("Heavy monster allocation failed");
    state->runtime = runtime;
    state->definition = definition;
    game->register_spawn(game->ctx, definition->spec.classnames[c], spawn_handler, state);
  }
}
