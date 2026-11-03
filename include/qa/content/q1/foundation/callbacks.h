/* Port of src/content/q1/foundation/callbacks.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_CALLBACKS_H
#define QA_CONTENT_Q1_FOUNDATION_CALLBACKS_H

#include "qa/content/q1/foundation/entity.h"

typedef struct qa_q1_callback_handlers {
  void *ctx;
  qa_q1_trajectory_fn trajectory;
  qa_q1_action_fn action;
  qa_q1_use_fn use;
  qa_q1_touch_fn touch;
  qa_q1_pain_fn pain;
  qa_q1_die_fn die;
  qa_q1_blocked_fn blocked;
} qa_q1_callback_handlers;

typedef struct qa_q1_state_extension {
  void *ctx;
  const char *id;
  /* capture returns owned bytes via out params (malloc'd); false on OOM. */
  bool (*capture)(void *ctx, uint8_t **out, size_t *out_len);
  void (*restore)(void *ctx, const uint8_t *bytes, size_t len);
  void (*clone)(void *ctx, qa_q1_actor *source, qa_q1_actor *target); /* optional */
  /* TS restoreAbsent is attached with qa_q1_set_state_extension_restore_absent
   * (entity_services.h), keeping this struct's positional initializers valid. */
} qa_q1_state_extension;

/* TS callbackName for each slot kind: NULL slot name with no fn -> NULL;
 * fn without name -> qa_range_error (exact TS message). */
const char *qa_q1_action_name(qa_q1_bound_action cb);
const char *qa_q1_use_name(qa_q1_bound_use cb);
const char *qa_q1_touch_name(qa_q1_bound_touch cb);
const char *qa_q1_pain_name(qa_q1_bound_pain cb);
const char *qa_q1_die_name(qa_q1_bound_die cb);
const char *qa_q1_blocked_name(qa_q1_bound_blocked cb);

typedef struct qa_q1_callback_registry qa_q1_callback_registry;
qa_q1_callback_registry *qa_q1_callback_registry_create(qa_q1_services *game);
void qa_q1_callback_registry_destroy(qa_q1_callback_registry *registry);
void qa_q1_callback_registry_register(qa_q1_callback_registry *registry, const char *name, qa_q1_callback_handlers handlers);
/* TS handlers.has(name) (the registry's private Map): whether name is registered. */
bool qa_q1_callback_registry_has(const qa_q1_callback_registry *registry, const char *name);
void qa_q1_callback_registry_project_trajectory(qa_q1_callback_registry *registry, qa_q1_actor *entity, qa_weapon_trajectory_update update);
qa_q1_bound_action qa_q1_callback_registry_action(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name);
qa_q1_bound_use qa_q1_callback_registry_use(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name);
qa_q1_bound_touch qa_q1_callback_registry_touch(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name);
qa_q1_bound_pain qa_q1_callback_registry_pain(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name);
qa_q1_bound_die qa_q1_callback_registry_die(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name);
qa_q1_bound_blocked qa_q1_callback_registry_blocked(qa_q1_callback_registry *registry, qa_q1_actor *entity, const char *name);

/* Invoke bound slots (no-op when fn is NULL, TS `?.`). */
void qa_q1_call_action(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_action cb);
void qa_q1_call_use(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_use cb, const qa_actor_id *other, const qa_actor_id *activator);
void qa_q1_call_touch(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_touch cb, qa_actor_id other, const qa_vec3 *normal, const qa_touch_surface *surface);
void qa_q1_call_pain(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_pain cb, const qa_actor_id *attacker, double damage);
void qa_q1_call_die(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_die cb, const qa_actor_id *attacker);
void qa_q1_call_blocked(qa_q1_services *game, qa_q1_actor *entity, qa_q1_bound_blocked cb, qa_actor_id other);

#endif
