/* Port of src/content/q1/addons/monsters/infected/index.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/infected/index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/infected/species.h"
#include "qa/content/q1/addons/monsters/startup.h"
#include "qa/foundation/support.h"

typedef struct infected_registry {
  qa_q1am_context *context;
  qa_q1am_infected *head, *tail;
} infected_registry;
static qa_q1am_infected *registry_get(infected_registry *r, qa_owned_actor actor) {
  for (qa_q1am_infected *m = r->head; m; m = m->next)
    if (qa_q1am_same_owned(m->base.entity->actor, actor)) return m;
  return NULL;
}
static void registry_put(infected_registry *r, qa_q1am_infected *m) {
  qa_q1am_infected *old = registry_get(r, m->base.entity->actor);
  if (old == m) return;
  if (old != NULL) {
    qa_q1am_infected **link = &r->head;
    while (*link && *link != old) link = &(*link)->next;
    *link = old->next;
    if (r->tail == old) r->tail = NULL;
    free(old);
  }
  m->next = NULL;
  if (r->tail) r->tail->next = m;
  else r->head = m;
  r->tail = m;
}
static void registry_delete(infected_registry *r, qa_owned_actor actor) {
  qa_q1am_infected **link = &r->head;
  while (*link) {
    if (qa_q1am_same_owned((*link)->base.entity->actor, actor)) {
      qa_q1am_infected *dead = *link;
      *link = dead->next;
      free(dead);
      return;
    }
    link = &(*link)->next;
  }
}
static void registry_clear(infected_registry *r) {
  while (r->head) {
    qa_q1am_infected *dead = r->head;
    r->head = dead->next;
    free(dead);
  }
  r->tail = NULL;
}
static qa_q1am_infected *replace_one(void *ctx, qa_q1am_actor *entity) {
  infected_registry *r = ctx;
  qa_q1am_infected *value = malloc(sizeof *value);
  if (!value) qa_range_error("Infected allocation failed");
  qa_q1am_infected_replace replace = {r, replace_one};
  qa_q1am_infected_init(value, r->context, entity, replace);
  registry_put(r, value);
  return value;
}
static qa_q1am_infected *require_infected(infected_registry *r, qa_q1am_actor *entity) {
  qa_q1am_infected *m = registry_get(r, entity->actor);
  if (m == NULL) qa_range_error("Missing MG3 infected source state");
  return m;
}
static qa_q1am_monster *lookup(void *ctx, qa_q1am_actor *entity) {
  return &require_infected(ctx, entity)->base;
}
static void on_release(void *ctx, qa_owned_actor actor) { registry_delete(ctx, actor); }
static void resurrect_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  qa_q1am_infected_resurrect(require_infected(ctx, entity));
}
static bool kill_count_rule(void *ctx, qa_q1am_monster *value) {
  infected_registry *r = ctx;
  if (registry_get(r, value->entity->actor) == NULL ||
      qa_q1am_actor_number(value->entity, "infected") != 1)
    return true;
  r->context->set_number(r->context->ctx, value->entity, "infected", 0);
  return false;
}
typedef struct spawn_ctx { infected_registry *registry; const char *kind; } spawn_ctx;
static void spawn_handler(void *ctx, qa_q1am_game *game, qa_q1am_actor *entity) {
  (void)game;
  spawn_ctx *state = ctx;
  qa_q1am_actor_set_field(entity, "infected.kind", state->kind);
  qa_q1am_infected *value = replace_one(state->registry, entity);
  value->base.ops.spawn(&value->base);
}
static uint8_t *capture_all(void *ctx, size_t *out_length) {
  infected_registry *r = ctx;
  qa_save_value *list = qa_save_value_array();
  for (qa_q1am_infected *m = r->head; m; m = m->next) {
    qa_save_value *object = qa_save_value_object();
    qa_save_value_set(object, "slot", qa_save_value_number((double)m->base.entity->actor.id.slot));
    qa_save_value_set(object, "generation", qa_save_value_number((double)m->base.entity->actor.id.generation));
    qa_save_value_set(object, "state", qa_q1am_monster_capture(&m->base));
    qa_save_value_push(list, object);
  }
  uint8_t *bytes = qa_encode_checkpoint_value(list, out_length);
  qa_save_value_free(list);
  return bytes;
}
static void restore_all(void *ctx, const uint8_t *bytes, size_t length) {
  infected_registry *r = ctx;
  registry_clear(r);
  qa_q1am_game *game = r->context->game;
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  qa_save_reader root;
  qa_save_reader_init(&root, decoded, QA_Q1AM_INFECTED_PREFIX);
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
    if (entity == NULL) qa_save_reader_fail(&reader, "missing infected actor");
    qa_q1am_infected *value = replace_one(r, entity);
    qa_save_reader_field(&reader, "state", &state);
    qa_q1am_monster_restore(&value->base, &state);
    qa_save_reader_destroy(&state);
    qa_save_reader_destroy(&reader);
  }
  qa_save_reader_destroy(&root);
  qa_save_value_free(decoded);
}
static void clone_one(void *ctx, qa_q1am_actor *source, qa_q1am_actor *target) {
  infected_registry *r = ctx;
  qa_q1am_infected *value = registry_get(r, source->actor);
  if (value == NULL) return;
  qa_q1am_infected *copy = replace_one(r, target);
  qa_save_value *captured = qa_q1am_monster_capture(&value->base);
  qa_save_reader reader;
  qa_save_reader_init(&reader, captured, QA_Q1AM_INFECTED_PREFIX);
  qa_q1am_monster_restore(&copy->base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
}
qa_q1am_infected *qa_q1am_register_mg3_infected(qa_q1am_context *context) {
  qa_q1am_game *game = context->game;
  infected_registry *r = calloc(1, sizeof *r);
  if (!r) qa_range_error("Infected allocation failed");
  r->context = context;
  qa_q1am_monster_lookup lookup_value = {r, lookup};
  game->register_monster_callbacks(game->ctx, game, QA_Q1AM_INFECTED_PREFIX, lookup_value);
  qa_q1am_register_mg3_monster_startup(game, QA_Q1AM_INFECTED_PREFIX, lookup_value);
  qa_q1am_handlers handlers = {0};
  handlers.ctx = r;
  handlers.action = resurrect_handler;
  game->named_register(game->ctx, QA_Q1AM_INFECTED_PREFIX ":resurrect", handlers);
  context->base->register_kill_count_rule(context->base->ctx, QA_Q1AM_INFECTED_PREFIX, kill_count_rule, r);
  for (size_t i = 0; i < qa_q1am_infected_classname_count; i++) {
    spawn_ctx *state = malloc(sizeof *state);
    if (!state) qa_range_error("Infected allocation failed");
    state->registry = r;
    state->kind = qa_q1am_infected_classnames[i].kind;
    game->register_spawn(game->ctx, qa_q1am_infected_classnames[i].classname, spawn_handler, state);
  }
  game->actors_on_release(game->ctx, on_release, r);
  qa_q1am_state_extension extension = {QA_Q1AM_INFECTED_PREFIX, r, capture_all, restore_all, clone_one};
  game->register_state_extension(game->ctx, extension);
  return r->head;
}
