/* Port of src/content/q1/foundation/callbacks.ts. GPL-2.0-or-later. */
#include "qa/content/q1/foundation/callbacks.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/foundation/support.h"

struct qa_q1_callback_registry {
  qa_q1_services *game;
  struct {
    char *name;
    qa_q1_callback_handlers handlers;
  } *entries;
  size_t count, capacity;
};

static char *dupstr(const char *text) {
  const size_t len = strlen(text) + 1;
  char *copy = malloc(len);
  if (copy) memcpy(copy, text, len);
  return copy;
}

qa_q1_callback_registry *qa_q1_callback_registry_create(qa_q1_services *game) {
  qa_q1_callback_registry *registry = calloc(1, sizeof *registry);
  if (registry) registry->game = game;
  return registry;
}

void qa_q1_callback_registry_destroy(qa_q1_callback_registry *registry) {
  if (!registry) return;
  for (size_t i = 0; i < registry->count; i++) free(registry->entries[i].name);
  free(registry->entries);
  free(registry);
}

static const qa_q1_callback_handlers *get(qa_q1_callback_registry *registry, const char *name) {
  for (size_t i = 0; i < registry->count; i++)
    if (strcmp(registry->entries[i].name, name) == 0) return &registry->entries[i].handlers;
  qa_range_error("Unknown Q1 saved callback: %s", name);
}

void qa_q1_callback_registry_register(qa_q1_callback_registry *registry, const char *name,
                                      qa_q1_callback_handlers handlers) {
  for (size_t i = 0; i < registry->count; i++)
    if (strcmp(registry->entries[i].name, name) == 0)
      qa_range_error("Duplicate Q1 callback: %s", name);
  if (registry->count == registry->capacity) {
    const size_t grown = registry->capacity ? registry->capacity * 2 : 16;
    void *next = realloc(registry->entries, grown * sizeof *registry->entries);
    if (!next) return; /* OOM only; TS has no counterpart */
    registry->entries = next;
    registry->capacity = grown;
  }
  char *copy = dupstr(name);
  if (!copy) return;
  registry->entries[registry->count].name = copy;
  registry->entries[registry->count].handlers = handlers;
  registry->count++;
}

const char *qa_q1_action_name(qa_q1_bound_action cb) {
  if (cb.fn == NULL) return NULL;
  if (cb.name == NULL) qa_range_error("Q1 save encountered an unnamed source callback");
  return cb.name;
}
const char *qa_q1_use_name(qa_q1_bound_use cb) {
  if (cb.fn == NULL) return NULL;
  if (cb.name == NULL) qa_range_error("Q1 save encountered an unnamed source callback");
  return cb.name;
}
const char *qa_q1_touch_name(qa_q1_bound_touch cb) {
  if (cb.fn == NULL && cb.trajectory == NULL) return NULL;
  if (cb.name == NULL) qa_range_error("Q1 save encountered an unnamed source callback");
  return cb.name;
}
const char *qa_q1_pain_name(qa_q1_bound_pain cb) {
  if (cb.fn == NULL) return NULL;
  if (cb.name == NULL) qa_range_error("Q1 save encountered an unnamed source callback");
  return cb.name;
}
const char *qa_q1_die_name(qa_q1_bound_die cb) {
  if (cb.fn == NULL) return NULL;
  if (cb.name == NULL) qa_range_error("Q1 save encountered an unnamed source callback");
  return cb.name;
}
const char *qa_q1_blocked_name(qa_q1_bound_blocked cb) {
  if (cb.fn == NULL) return NULL;
  if (cb.name == NULL) qa_range_error("Q1 save encountered an unnamed source callback");
  return cb.name;
}

void qa_q1_callback_registry_project_trajectory(qa_q1_callback_registry *registry, qa_q1_actor *entity,
                                                qa_weapon_trajectory_update update) {
  const char *name = qa_q1_touch_name(entity->touch);
  if (name != NULL) {
    const qa_q1_callback_handlers *handlers = get(registry, name);
    if (handlers->trajectory) handlers->trajectory(handlers->ctx, registry->game, entity, update);
  }
}

qa_q1_bound_action qa_q1_callback_registry_action(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name) {
  (void)entity;
  const qa_q1_callback_handlers *handlers = get(registry, name);
  if (handlers->action == NULL) qa_range_error("Q1 callback is not an action: %s", name);
  const qa_q1_bound_action bound = {name, handlers->ctx, handlers->action};
  return bound;
}
qa_q1_bound_use qa_q1_callback_registry_use(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name) {
  (void)entity;
  const qa_q1_callback_handlers *handlers = get(registry, name);
  if (handlers->use == NULL) qa_range_error("Q1 callback is not use: %s", name);
  const qa_q1_bound_use bound = {name, handlers->ctx, handlers->use};
  return bound;
}
qa_q1_bound_touch qa_q1_callback_registry_touch(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name) {
  (void)entity;
  const qa_q1_callback_handlers *handlers = get(registry, name);
  if (handlers->touch == NULL) qa_range_error("Q1 callback is not touch: %s", name);
  const qa_q1_bound_touch bound = {name, handlers->ctx, handlers->touch, handlers->trajectory};
  return bound;
}
qa_q1_bound_pain qa_q1_callback_registry_pain(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name) {
  (void)entity;
  const qa_q1_callback_handlers *handlers = get(registry, name);
  if (handlers->pain == NULL) qa_range_error("Q1 callback is not pain: %s", name);
  const qa_q1_bound_pain bound = {name, handlers->ctx, handlers->pain};
  return bound;
}
qa_q1_bound_die qa_q1_callback_registry_die(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name) {
  (void)entity;
  const qa_q1_callback_handlers *handlers = get(registry, name);
  if (handlers->die == NULL) qa_range_error("Q1 callback is not die: %s", name);
  const qa_q1_bound_die bound = {name, handlers->ctx, handlers->die};
  return bound;
}
qa_q1_bound_blocked qa_q1_callback_registry_blocked(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name) {
  (void)entity;
  const qa_q1_callback_handlers *handlers = get(registry, name);
  if (handlers->blocked == NULL) qa_range_error("Q1 callback is not blocked: %s", name);
  const qa_q1_bound_blocked bound = {name, handlers->ctx, handlers->blocked};
  return bound;
}

void qa_q1_call_action(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_action cb) {
  if (cb.fn) cb.fn(cb.ctx, game, entity);
}
void qa_q1_call_use(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_use cb,
                    const qa_actor_id *other, const qa_actor_id *activator) {
  if (cb.fn) cb.fn(cb.ctx, game, entity, other, activator);
}
void qa_q1_call_touch(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_touch cb, qa_actor_id other,
                      const qa_vec3 *normal, const qa_touch_surface *surface) {
  if (cb.fn) cb.fn(cb.ctx, game, entity, other, normal, surface);
}
void qa_q1_call_pain(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_pain cb,
                     const qa_actor_id *attacker, double damage) {
  if (cb.fn) cb.fn(cb.ctx, game, entity, attacker, damage);
}
void qa_q1_call_die(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_die cb, const qa_actor_id *attacker) {
  if (cb.fn) cb.fn(cb.ctx, game, entity, attacker);
}
void qa_q1_call_blocked(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_blocked cb, qa_actor_id other) {
  if (cb.fn) cb.fn(cb.ctx, game, entity, other);
}
