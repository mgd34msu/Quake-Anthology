/* Port of src/world/actors/callbacks.ts. GPL-2.0-or-later. */
#include "qa/world/actors/callbacks.h"

#include <stdlib.h>
#include "qa/foundation/support.h"
#include <string.h>

static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}

typedef struct binding {
  qa_owned_actor actor;
  qa_actor_callbacks callbacks;
} binding;

struct qa_actor_callback_table {
  qa_session_actor_registry *actors;
  qa_mod_operation think_op, touch_op, use_op, pain_op, die_op;
  binding *bindings;
  size_t binding_count;
  size_t binding_capacity;
  qa_actor_invocation *invocation; /* owned chain head */
  int64_t release_subscription;
};

static void on_release(void *ctx, qa_owned_actor actor) {
  qa_actor_callback_table *table = ctx;
  for (size_t i = 0; i < table->binding_count; i++) {
    if (qa_same_owned_actor(table->bindings[i].actor, actor)) {
      memmove(&table->bindings[i], &table->bindings[i + 1],
              (table->binding_count - i - 1) * sizeof *table->bindings);
      table->binding_count--;
      return;
    }
  }
}

qa_actor_callback_table *qa_actor_callback_table_create(qa_session_actor_registry *actors) {
  qa_actor_callback_table *table = calloc(1, sizeof *table);
  if (table == NULL) return NULL;
  table->actors = actors;
  (void)qa_mod_operation_init(&table->think_op, "actor.think",
                              sizeof(qa_actor_think_request), sizeof(bool));
  (void)qa_mod_operation_init(&table->touch_op, "actor.touch",
                              sizeof(qa_actor_touch_request), sizeof(bool));
  (void)qa_mod_operation_init(&table->use_op, "actor.use",
                              sizeof(qa_actor_use_request), sizeof(bool));
  (void)qa_mod_operation_init(&table->pain_op, "actor.pain",
                              sizeof(qa_actor_pain_request), sizeof(bool));
  (void)qa_mod_operation_init(&table->die_op, "actor.die",
                              sizeof(qa_actor_die_request), sizeof(bool));
  table->release_subscription = qa_session_actor_registry_on_release(actors, on_release, table);
  return table;
}
void qa_actor_callback_table_destroy(qa_actor_callback_table *table) {
  if (table == NULL) return;
  qa_session_actor_registry_off_release(table->actors, table->release_subscription);
  qa_mod_operation_destroy(&table->think_op);
  qa_mod_operation_destroy(&table->touch_op);
  qa_mod_operation_destroy(&table->use_op);
  qa_mod_operation_destroy(&table->pain_op);
  qa_mod_operation_destroy(&table->die_op);
  while (table->invocation != NULL) {
    qa_actor_invocation *parent = (qa_actor_invocation *)table->invocation->parent;
    free(table->invocation);
    table->invocation = parent;
  }
  free(table->bindings);
  free(table);
}
qa_mod_operation *qa_actor_callback_table_think_operation(qa_actor_callback_table *table) { return &table->think_op; }
qa_mod_operation *qa_actor_callback_table_touch_operation(qa_actor_callback_table *table) { return &table->touch_op; }
qa_mod_operation *qa_actor_callback_table_use_operation(qa_actor_callback_table *table) { return &table->use_op; }
qa_mod_operation *qa_actor_callback_table_pain_operation(qa_actor_callback_table *table) { return &table->pain_op; }
qa_mod_operation *qa_actor_callback_table_die_operation(qa_actor_callback_table *table) { return &table->die_op; }
const qa_actor_invocation *qa_actor_callback_table_current(qa_actor_callback_table *table) {
  return table->invocation;
}

void qa_actor_callback_table_bind(qa_actor_callback_table *table, qa_owned_actor actor,
                                  qa_actor_callbacks callbacks) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  for (size_t i = 0; i < table->binding_count; i++) {
    if (qa_same_owned_actor(table->bindings[i].actor, actor)) {
      table->bindings[i].callbacks = callbacks;
      return;
    }
  }
  if (table->binding_count == table->binding_capacity) {
    table->binding_capacity = table->binding_capacity != 0 ? table->binding_capacity * 2 : 8;
    table->bindings = xrealloc(table->bindings, table->binding_capacity * sizeof *table->bindings);
  }
  table->bindings[table->binding_count].actor = actor;
  table->bindings[table->binding_count].callbacks = callbacks;
  table->binding_count++;
}

static void push_invocation(qa_actor_callback_table *table, qa_owned_actor self, const char *kind) {
  qa_actor_invocation *value = malloc(sizeof *value);
  if (value == NULL) abort();
  value->self = self;
  value->kind = kind;
  value->parent = table->invocation;
  table->invocation = value;
}
static void pop_invocation(qa_actor_callback_table *table) {
  qa_actor_invocation *value = table->invocation;
  table->invocation = (qa_actor_invocation *)value->parent;
  free(value);
}

/* TS `try { ... } finally { this.invocation = parent; }`: a raise that a
 * caller catches must not leave the frame current. */
static void pop_invocation_cleanup(void *arg) { pop_invocation(arg); }
#define INVOCATION_BEGIN(table, self, kind) \
  qa_cleanup invocation_node; \
  push_invocation((table), (self), (kind)); \
  qa_cleanup_push(&invocation_node, pop_invocation_cleanup, (table))
#define INVOCATION_END(table) \
  qa_cleanup_pop(&invocation_node); \
  pop_invocation(table)

static const qa_actor_callbacks *find_callbacks(qa_actor_callback_table *table, qa_owned_actor self) {
  for (size_t i = 0; i < table->binding_count; i++) {
    if (qa_same_owned_actor(table->bindings[i].actor, self)) return &table->bindings[i].callbacks;
  }
  return NULL;
}

/* TS invoke(): kind selects the callback; call() runs it. */
typedef void (*invoke_call_fn)(void *ctx, const qa_actor_callbacks *callbacks);
static bool invoke_cb(qa_actor_callback_table *table, qa_owned_actor self, const char *kind,
                      bool has_callback, invoke_call_fn call, void *ctx) {
  if (!qa_session_actor_registry_is_live(table->actors, self.id)) return false;
  qa_session_actor_registry_assert_owned(table->actors, self);
  const qa_actor_callbacks *callbacks = find_callbacks(table, self);
  if (callbacks == NULL || !has_callback) return false;
  INVOCATION_BEGIN(table, self, kind);
  call(ctx, callbacks);
  INVOCATION_END(table);
  return true;
}
static bool compose(qa_actor_callback_table *table, qa_owned_actor self, qa_mod_operation *operation,
                    const void *request, qa_mod_canonical canonical) {
  if (!qa_session_actor_registry_is_live(table->actors, self.id)) return false;
  qa_session_actor_registry_assert_owned(table->actors, self);
  bool result = false;
  qa_mod_operation_dispatch(operation, request, canonical, NULL, &result);
  return result;
}

/* --- canonical bodies --- */
typedef struct canonical_state { qa_actor_callback_table *table; void *execute_ctx; } canonical_state;

static void call_think(void *ctx, const qa_actor_callbacks *callbacks) {
  const qa_actor_think_request *request = ((canonical_state *)ctx)->execute_ctx;
  callbacks->think(callbacks->ctx, request->self, request->frame);
}
static bool think_canonical(qa_actor_callback_table *table, qa_actor_think_request request) {
  const qa_actor_callbacks *callbacks = find_callbacks(table, request.self);
  canonical_state state = {table, &request};
  return invoke_cb(table, request.self, "think",
                   callbacks != NULL && callbacks->think != NULL, call_think, &state);
}
static void think_dispatch(void *ctx, const void *request, void *out) {
  *(bool *)out = think_canonical(((canonical_state *)ctx)->table,
                                  *(const qa_actor_think_request *)request);
}

static void call_touch(void *ctx, const qa_actor_callbacks *callbacks) {
  const qa_actor_touch_request *request = ((canonical_state *)ctx)->execute_ctx;
  callbacks->touch(callbacks->ctx, request->contact);
}
static bool touch_canonical(qa_actor_callback_table *table, qa_actor_touch_request request) {
  if (!qa_session_actor_registry_is_live(table->actors, request.contact.other) &&
      !(request.contact.source_trace != NULL && request.contact.source_trace->inverted))
    return false;
  const qa_actor_callbacks *callbacks = find_callbacks(table, request.contact.self);
  canonical_state state = {table, &request};
  return invoke_cb(table, request.contact.self, "touch",
                   callbacks != NULL && callbacks->touch != NULL, call_touch, &state);
}
static void touch_dispatch(void *ctx, const void *request, void *out) {
  *(bool *)out = touch_canonical(((canonical_state *)ctx)->table,
                                  *(const qa_actor_touch_request *)request);
}

static void call_use(void *ctx, const qa_actor_callbacks *callbacks) {
  const qa_actor_use_request *request = ((canonical_state *)ctx)->execute_ctx;
  callbacks->use(callbacks->ctx, request->self,
                 request->has_other ? &request->other : NULL,
                 request->has_activator ? &request->activator : NULL);
}
static bool use_canonical(qa_actor_callback_table *table, qa_actor_use_request request) {
  const qa_actor_callbacks *callbacks = find_callbacks(table, request.self);
  canonical_state state = {table, &request};
  return invoke_cb(table, request.self, "use",
                   callbacks != NULL && callbacks->use != NULL, call_use, &state);
}
static void use_dispatch(void *ctx, const void *request, void *out) {
  *(bool *)out = use_canonical(((canonical_state *)ctx)->table,
                                *(const qa_actor_use_request *)request);
}

static void call_pain(void *ctx, const qa_actor_callbacks *callbacks) {
  const qa_actor_pain_request *request = ((canonical_state *)ctx)->execute_ctx;
  callbacks->pain(callbacks->ctx, request->reaction);
}
static bool pain_canonical(qa_actor_callback_table *table, qa_actor_pain_request request) {
  const qa_actor_callbacks *callbacks = find_callbacks(table, request.reaction.self);
  canonical_state state = {table, &request};
  return invoke_cb(table, request.reaction.self, "pain",
                   callbacks != NULL && callbacks->pain != NULL, call_pain, &state);
}
static void pain_dispatch(void *ctx, const void *request, void *out) {
  *(bool *)out = pain_canonical(((canonical_state *)ctx)->table,
                                 *(const qa_actor_pain_request *)request);
}

static void call_die(void *ctx, const qa_actor_callbacks *callbacks) {
  const qa_actor_die_request *request = ((canonical_state *)ctx)->execute_ctx;
  callbacks->die(callbacks->ctx, request->reaction);
}
static bool die_canonical(qa_actor_callback_table *table, qa_actor_die_request request) {
  const qa_actor_callbacks *callbacks = find_callbacks(table, request.reaction.pain.self);
  canonical_state state = {table, &request};
  return invoke_cb(table, request.reaction.pain.self, "die",
                   callbacks != NULL && callbacks->die != NULL, call_die, &state);
}
static void die_dispatch(void *ctx, const void *request, void *out) {
  *(bool *)out = die_canonical(((canonical_state *)ctx)->table,
                                *(const qa_actor_die_request *)request);
}

bool qa_actor_callback_table_think(qa_actor_callback_table *table, qa_owned_actor self,
                                   qa_frame_context frame) {
  qa_actor_think_request request = {self, frame};
  if (!qa_mod_operation_active(&table->think_op)) return think_canonical(table, request);
  canonical_state state = {table, NULL};
  qa_mod_canonical canonical = {&state, think_dispatch};
  return compose(table, self, &table->think_op, &request, canonical);
}
bool qa_actor_callback_table_touch(qa_actor_callback_table *table, qa_touch_contact contact) {
  qa_actor_touch_request request = {contact};
  if (!qa_mod_operation_active(&table->touch_op)) return touch_canonical(table, request);
  canonical_state state = {table, NULL};
  qa_mod_canonical canonical = {&state, touch_dispatch};
  return compose(table, contact.self, &table->touch_op, &request, canonical);
}
bool qa_actor_callback_table_use(qa_actor_callback_table *table, qa_owned_actor self,
                                 const qa_actor_id *other, const qa_actor_id *activator) {
  qa_actor_use_request request;
  memset(&request, 0, sizeof request);
  request.self = self;
  request.has_other = other != NULL;
  if (other != NULL) request.other = *other;
  request.has_activator = activator != NULL;
  if (activator != NULL) request.activator = *activator;
  if (!qa_mod_operation_active(&table->use_op)) return use_canonical(table, request);
  canonical_state state = {table, NULL};
  qa_mod_canonical canonical = {&state, use_dispatch};
  return compose(table, self, &table->use_op, &request, canonical);
}
bool qa_actor_callback_table_pain(qa_actor_callback_table *table, qa_pain_reaction reaction) {
  qa_actor_pain_request request = {reaction};
  if (!qa_mod_operation_active(&table->pain_op)) return pain_canonical(table, request);
  canonical_state state = {table, NULL};
  qa_mod_canonical canonical = {&state, pain_dispatch};
  return compose(table, reaction.self, &table->pain_op, &request, canonical);
}
bool qa_actor_callback_table_die(qa_actor_callback_table *table, qa_death_reaction reaction) {
  qa_actor_die_request request = {reaction};
  if (!qa_mod_operation_active(&table->die_op)) return die_canonical(table, request);
  canonical_state state = {table, NULL};
  qa_mod_canonical canonical = {&state, die_dispatch};
  return compose(table, reaction.pain.self, &table->die_op, &request, canonical);
}

/* --- source variants (TS invokeSource inlined per kind) --- */
typedef struct pain_source {
  qa_actor_callback_table *table;
  void (*execute)(void *ctx, qa_pain_reaction reaction);
  void *ctx;
} pain_source;
static void pain_source_run(void *ctx, const void *request, void *out) {
  pain_source *state = ctx;
  const qa_actor_pain_request *value = request;
  qa_pain_reaction reaction = value->reaction;
  typedef struct { pain_source *state; qa_pain_reaction reaction; } exec_state;
  exec_state exec = {state, reaction};
  /* inline invokeSource */
  if (!qa_session_actor_registry_is_live(state->table->actors, reaction.self.id)) {
    *(bool *)out = false;
    return;
  }
  qa_session_actor_registry_assert_owned(state->table->actors, reaction.self);
  INVOCATION_BEGIN(state->table, reaction.self, "pain");
  exec.state->execute(exec.state->ctx, exec.reaction);
  INVOCATION_END(state->table);
  *(bool *)out = true;
}
bool qa_actor_callback_table_source_pain(qa_actor_callback_table *table,
                                         qa_pain_reaction reaction,
                                         void (*execute)(void *ctx, qa_pain_reaction reaction),
                                         void *ctx) {
  pain_source state = {table, execute, ctx};
  qa_actor_pain_request request = {reaction};
  if (!qa_mod_operation_active(&table->pain_op)) {
    bool result = false;
    pain_source_run(&state, &request, &result);
    return result;
  }
  qa_mod_canonical canonical = {&state, pain_source_run};
  return compose(table, reaction.self, &table->pain_op, &request, canonical);
}

typedef struct die_source {
  qa_actor_callback_table *table;
  void (*execute)(void *ctx, qa_death_reaction reaction);
  void *ctx;
} die_source;
static void die_source_run(void *ctx, const void *request, void *out) {
  die_source *state = ctx;
  const qa_actor_die_request *value = request;
  if (!qa_session_actor_registry_is_live(state->table->actors, value->reaction.pain.self.id)) {
    *(bool *)out = false;
    return;
  }
  qa_session_actor_registry_assert_owned(state->table->actors, value->reaction.pain.self);
  INVOCATION_BEGIN(state->table, value->reaction.pain.self, "die");
  state->execute(state->ctx, value->reaction);
  INVOCATION_END(state->table);
  *(bool *)out = true;
}
bool qa_actor_callback_table_source_die(qa_actor_callback_table *table,
                                        qa_death_reaction reaction,
                                        void (*execute)(void *ctx, qa_death_reaction reaction),
                                        void *ctx) {
  die_source state = {table, execute, ctx};
  qa_actor_die_request request = {reaction};
  if (!qa_mod_operation_active(&table->die_op)) {
    bool result = false;
    die_source_run(&state, &request, &result);
    return result;
  }
  qa_mod_canonical canonical = {&state, die_source_run};
  return compose(table, reaction.pain.self, &table->die_op, &request, canonical);
}

typedef struct use_source {
  qa_actor_callback_table *table;
  void (*execute)(void *ctx, qa_owned_actor self, const qa_actor_id *other,
                  const qa_actor_id *activator);
  void *ctx;
} use_source;
static void use_source_run(void *ctx, const void *request, void *out) {
  use_source *state = ctx;
  const qa_actor_use_request *value = request;
  if (!qa_session_actor_registry_is_live(state->table->actors, value->self.id)) {
    *(bool *)out = false;
    return;
  }
  qa_session_actor_registry_assert_owned(state->table->actors, value->self);
  INVOCATION_BEGIN(state->table, value->self, "use");
  state->execute(state->ctx, value->self, value->has_other ? &value->other : NULL,
                 value->has_activator ? &value->activator : NULL);
  INVOCATION_END(state->table);
  *(bool *)out = true;
}
bool qa_actor_callback_table_source_use(qa_actor_callback_table *table, qa_owned_actor self,
                                        const qa_actor_id *other, const qa_actor_id *activator,
                                        void (*execute)(void *ctx, qa_owned_actor self,
                                                        const qa_actor_id *other,
                                                        const qa_actor_id *activator),
                                        void *ctx) {
  use_source state = {table, execute, ctx};
  qa_actor_use_request request;
  memset(&request, 0, sizeof request);
  request.self = self;
  request.has_other = other != NULL;
  if (other != NULL) request.other = *other;
  request.has_activator = activator != NULL;
  if (activator != NULL) request.activator = *activator;
  if (!qa_mod_operation_active(&table->use_op)) {
    bool result = false;
    use_source_run(&state, &request, &result);
    return result;
  }
  qa_mod_canonical canonical = {&state, use_source_run};
  return compose(table, self, &table->use_op, &request, canonical);
}

typedef struct touch_source {
  qa_actor_callback_table *table;
  void (*execute)(void *ctx, qa_touch_contact contact);
  void *ctx;
} touch_source;
static void touch_source_run(void *ctx, const void *request, void *out) {
  touch_source *state = ctx;
  const qa_actor_touch_request *value = request;
  if (!qa_session_actor_registry_is_live(state->table->actors, value->contact.other) &&
      !(value->contact.source_trace != NULL && value->contact.source_trace->inverted)) {
    *(bool *)out = false;
    return;
  }
  if (!qa_session_actor_registry_is_live(state->table->actors, value->contact.self.id)) {
    *(bool *)out = false;
    return;
  }
  qa_session_actor_registry_assert_owned(state->table->actors, value->contact.self);
  INVOCATION_BEGIN(state->table, value->contact.self, "touch");
  state->execute(state->ctx, value->contact);
  INVOCATION_END(state->table);
  *(bool *)out = true;
}
bool qa_actor_callback_table_source_touch(qa_actor_callback_table *table,
                                          qa_touch_contact contact,
                                          void (*execute)(void *ctx, qa_touch_contact contact),
                                          void *ctx) {
  touch_source state = {table, execute, ctx};
  qa_actor_touch_request request = {contact};
  if (!qa_mod_operation_active(&table->touch_op)) {
    bool result = false;
    touch_source_run(&state, &request, &result);
    return result;
  }
  qa_mod_canonical canonical = {&state, touch_source_run};
  return compose(table, contact.self, &table->touch_op, &request, canonical);
}
