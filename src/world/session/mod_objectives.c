/* Port of src/world/session/mod-objectives.ts. GPL-2.0-or-later. */
#include "qa/world/session/mod_objectives.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/cleanup.h"

/* TS `throw new Error(...)`: a catchable raise (aborts only uncaught). */
static noreturn void qa_errorf(const char *format, ...) {
  char message[2048];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) abort();
  memcpy(copy, text, length);
  return copy;
}
static bool actor_opt_eq(const qa_actor_id *a, const qa_actor_id *b) {
  if (a == NULL || b == NULL) return a == b;
  return qa_same_actor(*a, *b);
}
static bool state_equal(qa_source_objective_state left,
                        qa_source_objective_state right) {
  return str_eq(left.stage, right.stage) &&
         actor_opt_eq(left.carrier, right.carrier) &&
         actor_opt_eq(left.target, right.target);
}

struct projected_entry {
  qa_objective_id id;
  qa_source_objective_state state;
  bool has_carrier;
  qa_actor_id carrier;
  bool has_target;
  qa_actor_id target;
  char *location; /* owned */
};
struct qa_mod_source_objectives {
  qa_provider_id owner;
  const qa_source_objective_declaration *declarations;
  size_t count;
  const qa_source_match_services *match;
  qa_mod_objective_storage storage;
  qa_cleanup_stack removals;
  struct projected_entry *projected;
  size_t projected_count;
  size_t projected_capacity;
  bool busy;
  bool active;
  bool closed;
  /* Scratch actor storage for read() results. */
  qa_actor_id scratch_carrier;
  qa_actor_id scratch_target;
};

static qa_source_objective_state read_declaration(
    qa_mod_source_objectives *objectives,
    const qa_source_objective_declaration *declaration) {
  qa_mod_objective_storage *storage = &objectives->storage;
  if (objectives->closed || !storage->current(storage->ctx))
    qa_errorf("Original objective source was retired");
  double raw = storage->read_scalar(storage->ctx, declaration->state_storage);
  const qa_source_objective_value *value = NULL;
  for (size_t i = 0; i < declaration->state_value_count; i++)
    if (declaration->state_values[i].value == raw)
      value = &declaration->state_values[i];
  if (value == NULL)
    qa_errorf("Objective %s has undeclared original state %s",
              declaration->id, QA_JS_NUMBER(raw));
  qa_source_objective_state state;
  memset(&state, 0, sizeof state);
  state.stage = value->stage;
  state.complete = value->complete;
  if (declaration->carrier != NULL &&
      storage->read_actor(storage->ctx, declaration->carrier,
                          &objectives->scratch_carrier))
    state.carrier = &objectives->scratch_carrier;
  if (declaration->target != NULL &&
      storage->read_actor(storage->ctx, declaration->target,
                          &objectives->scratch_target))
    state.target = &objectives->scratch_target;
  return state;
}

static void set_projected(qa_mod_source_objectives *objectives,
                          qa_objective_id id,
                          qa_source_objective_state state,
                          const char *location) {
  struct projected_entry *entry = NULL;
  for (size_t i = 0; i < objectives->projected_count; i++)
    if (str_eq(objectives->projected[i].id, id))
      entry = &objectives->projected[i];
  if (entry == NULL) {
    if (objectives->projected_count == objectives->projected_capacity) {
      objectives->projected_capacity =
          objectives->projected_capacity != 0
              ? objectives->projected_capacity * 2
              : 8;
      objectives->projected = xrealloc(
          objectives->projected,
          objectives->projected_capacity * sizeof *objectives->projected);
    }
    entry = &objectives->projected[objectives->projected_count++];
    memset(entry, 0, sizeof *entry);
    entry->id = id;
  }
  entry->state = state;
  entry->has_carrier = state.carrier != NULL;
  if (entry->has_carrier) {
    entry->carrier = *state.carrier;
    entry->state.carrier = &entry->carrier;
  }
  entry->has_target = state.target != NULL;
  if (entry->has_target) {
    entry->target = *state.target;
    entry->state.target = &entry->target;
  }
  free(entry->location);
  entry->location = xstrdup(location);
}
static struct projected_entry *find_projected(
    qa_mod_source_objectives *objectives, qa_objective_id id) {
  for (size_t i = 0; i < objectives->projected_count; i++)
    if (str_eq(objectives->projected[i].id, id))
      return &objectives->projected[i];
  return NULL;
}

static void write_declaration(
    qa_mod_source_objectives *objectives,
    const qa_source_objective_declaration *declaration,
    qa_source_objective_state value) {
  qa_mod_objective_storage *storage = &objectives->storage;
  const qa_source_objective_value *original = NULL;
  for (size_t i = 0; i < declaration->state_value_count; i++)
    if (str_eq(declaration->state_values[i].stage, value.stage))
      original = &declaration->state_values[i];
  if (original == NULL)
    qa_errorf("Objective %s cannot represent source stage %s",
              declaration->id, value.stage);
  if ((declaration->carrier == NULL && value.carrier != NULL) ||
      (declaration->target == NULL && value.target != NULL))
    qa_errorf(
        "Objective %s requires its declared source actor reference "
        "storage",
        declaration->id);
  storage->write_scalar(storage->ctx, declaration->state_storage,
                        original->value);
  if (declaration->carrier != NULL)
    storage->write_actor(storage->ctx, declaration->carrier, value.carrier);
  if (declaration->target != NULL)
    storage->write_actor(storage->ctx, declaration->target, value.target);
  set_projected(objectives, declaration->id, value,
                storage->location(storage->ctx, declaration));
}

struct binding_ctx {
  qa_mod_source_objectives *objectives;
  const qa_source_objective_declaration *declaration;
};
static qa_source_objective_state binding_read(void *ctx) {
  struct binding_ctx *binding = ctx;
  return read_declaration(binding->objectives, binding->declaration);
}
static qa_mod_runtime_value actor_value(const qa_actor_id *actor) {
  qa_mod_runtime_value value;
  memset(&value, 0, sizeof value);
  value.kind = "actor";
  value.actor_value = actor;
  return value;
}
static qa_mod_runtime_value float_value(double number) {
  qa_mod_runtime_value value;
  memset(&value, 0, sizeof value);
  value.kind = "float";
  value.number_value = number;
  return value;
}
static void binding_change(void *ctx, qa_source_objective_change request) {
  struct binding_ctx *binding = ctx;
  qa_mod_source_objectives *objectives = binding->objectives;
  const qa_source_objective_declaration *declaration = binding->declaration;
  qa_mod_objective_storage *storage = &objectives->storage;
  if (objectives->closed || !storage->current(storage->ctx))
    qa_errorf("Original objective source was retired");
  const qa_source_objective_value *original = NULL;
  for (size_t i = 0; i < declaration->state_value_count; i++)
    if (str_eq(declaration->state_values[i].stage, request.stage))
      original = &declaration->state_values[i];
  if (original == NULL || declaration->change == NULL)
    qa_errorf("Objective %s has no original change for %s", declaration->id,
              request.stage);
  qa_mod_objective_input_entry inputs[5] = {
      {"self", actor_value(request.target)},
      {"other", actor_value(request.carrier)},
      {"activator", actor_value(request.target)},
      {"amount", float_value(original->value)},
      {"time", float_value(storage->seconds(storage->ctx))},
  };
  storage->invoke(storage->ctx, declaration->change, inputs, 5);
}

struct removal {
  qa_source_match_unbind unbind;
  struct binding_ctx binding;
};
static void run_removal(void *ctx) {
  struct removal *removal = ctx;
  removal->unbind.invoke(removal->unbind.ctx);
  free(removal);
}

qa_mod_source_objectives *qa_mod_source_objectives_create(
    qa_provider_id owner,
    const qa_source_objective_declaration *declarations, size_t count,
    const qa_source_match_services *match,
    qa_mod_objective_storage storage) {
  if (count != 0 && match == NULL)
    qa_errorf("Declared objectives require destination match services");
  for (size_t i = 0; i < count; i++)
    for (size_t j = i + 1; j < count; j++)
      if (str_eq(declarations[i].id, declarations[j].id))
        qa_errorf("Duplicate source objective channel");
  for (size_t i = 0; i < count; i++) {
    const qa_source_objective_declaration *declaration = &declarations[i];
    bool invalid = declaration->state_value_count == 0;
    for (size_t j = 0; j < declaration->state_value_count; j++) {
      const qa_source_objective_value *value =
          &declaration->state_values[j];
      if (!isfinite(value->value) || value->stage == NULL ||
          value->stage[0] == '\0')
        invalid = true;
      for (size_t k = j + 1; k < declaration->state_value_count; k++)
        if (declaration->state_values[k].value == value->value ||
            str_eq(declaration->state_values[k].stage, value->stage))
          invalid = true;
    }
    if (invalid)
      qa_errorf(
          "Source objective requires distinct original values and shared "
          "stages");
  }
  qa_mod_source_objectives *objectives = calloc(1, sizeof *objectives);
  if (objectives == NULL) return NULL;
  objectives->owner = owner;
  objectives->declarations = declarations;
  objectives->count = count;
  objectives->match = match;
  objectives->storage = storage;
  qa_cleanup_stack_init(&objectives->removals);
  return objectives;
}
void qa_mod_source_objectives_destroy(qa_mod_source_objectives *objectives) {
  if (objectives == NULL) return;
  qa_mod_source_objectives_close(objectives);
  qa_cleanup_stack_destroy(&objectives->removals);
  for (size_t i = 0; i < objectives->projected_count; i++)
    free(objectives->projected[i].location);
  free(objectives->projected);
  free(objectives);
}

static void activate_body(void *ctx) {
  qa_mod_source_objectives *objectives = ctx;
  for (size_t i = 0; i < objectives->count; i++) {
    const qa_source_objective_declaration *declaration =
        &objectives->declarations[i];
    if (!declaration->is_owned) continue;
    if (objectives->match == NULL)
      qa_errorf("Original objective match service is unavailable");
    (void)read_declaration(objectives, declaration);
    struct removal *removal = malloc(sizeof *removal);
    if (removal == NULL) abort();
    qa_cleanup node; /* a throwing bindObjective drops the removal */
    qa_cleanup_push(&node, free, removal);
    removal->binding.objectives = objectives;
    removal->binding.declaration = declaration;
    qa_source_objective_binding binding;
    memset(&binding, 0, sizeof binding);
    binding.ctx = &removal->binding;
    binding.owner = objectives->owner;
    binding.id = declaration->id;
    binding.campaign_gate = declaration->campaign_gate;
    binding.bot_goal = declaration->bot_goal;
    binding.read = binding_read;
    binding.change = binding_change;
    const qa_source_match_services *match = objectives->match;
    removal->unbind = match->bind_objective(match->ctx, binding);
    qa_cleanup_pop(&node);
    if (qa_cleanup_stack_add(&objectives->removals, run_removal, removal) != 0)
      abort();
  }
  objectives->active = true;
  qa_mod_source_objectives_refresh(objectives);
}
void qa_mod_source_objectives_activate(qa_mod_source_objectives *objectives) {
  if (objectives->active) return;
  qa_mod_objective_storage *storage = &objectives->storage;
  if (objectives->closed || !storage->current(storage->ctx))
    qa_errorf("Original objective source is unavailable");
  /* try { ... } catch (error) { removals.runReverse(); active = false; throw } */
  qa_caught_error error;
  if (!qa_try(activate_body, objectives, &error)) {
    qa_cleanup_stack_run_reverse(&objectives->removals);
    objectives->active = false;
    qa_rethrow(&error);
  }
}
static void end_busy(void *arg) { ((qa_mod_source_objectives *)arg)->busy = false; }
void qa_mod_source_objectives_refresh(qa_mod_source_objectives *objectives) {
  qa_mod_objective_storage *storage = &objectives->storage;
  if (objectives->closed || objectives->busy ||
      !storage->current(storage->ctx))
    return;
  objectives->busy = true;
  qa_cleanup node; /* finally { busy = false } */
  qa_cleanup_push(&node, end_busy, objectives);
  for (size_t i = 0; i < objectives->count; i++) {
    const qa_source_objective_declaration *declaration =
        &objectives->declarations[i];
    if (declaration->is_owned) continue;
    qa_source_objective_state_result value = {false, {NULL, false, NULL, NULL}};
    if (objectives->match != NULL)
      value = objectives->match->objective(objectives->match->ctx,
                                            declaration->id);
    if (!value.has_value)
      qa_errorf("Borrowed objective %s requires its enabled source owner",
                declaration->id);
    write_declaration(objectives, declaration, value.value);
  }
  qa_cleanup_pop(&node);
  objectives->busy = false;
}

void qa_mod_source_objectives_flush(qa_mod_source_objectives *objectives) {
  qa_mod_objective_storage *storage = &objectives->storage;
  if (objectives->closed || objectives->busy ||
      !storage->current(storage->ctx))
    return;
  objectives->busy = true;
  qa_cleanup node; /* finally { busy = false } */
  qa_cleanup_push(&node, end_busy, objectives);
  for (size_t i = 0; i < objectives->count; i++) {
    const qa_source_objective_declaration *declaration =
        &objectives->declarations[i];
    if (declaration->is_owned) continue;
    struct projected_entry *before =
        find_projected(objectives, declaration->id);
    if (before == NULL) continue;
    if (!str_eq(before->location,
                storage->location(storage->ctx, declaration))) {
      qa_source_objective_state_result current = {
          false, {NULL, false, NULL, NULL}};
      if (objectives->match != NULL)
        current = objectives->match->objective(objectives->match->ctx,
                                                declaration->id);
      if (!current.has_value)
        qa_errorf("Borrowed objective %s requires its enabled source owner",
                  declaration->id);
      write_declaration(objectives, declaration, current.value);
      continue;
    }
    qa_source_objective_state before_state = before->state;
    qa_source_objective_state value =
        read_declaration(objectives, declaration);
    if (state_equal(before_state, value)) continue;
    if (!declaration->writable)
      qa_errorf("Original source wrote read-only objective %s",
                declaration->id);
    qa_source_objective_change request = {value.stage, value.carrier,
                                          value.target};
    qa_source_objective_state_result accepted = {
        false, {NULL, false, NULL, NULL}};
    if (objectives->match != NULL)
      accepted = objectives->match->change_objective(
          objectives->match->ctx, declaration->id, request);
    if (objectives->closed || !storage->current(storage->ctx)) {
      qa_cleanup_pop(&node);
      objectives->busy = false;
      return;
    }
    if (!accepted.has_value)
      qa_errorf("Objective %s owner retired during its original change",
                declaration->id);
    write_declaration(objectives, declaration, accepted.value);
  }
  qa_cleanup_pop(&node);
  objectives->busy = false;
}

void qa_mod_source_objectives_restored(qa_mod_source_objectives *objectives) {
  for (size_t i = 0; i < objectives->projected_count; i++)
    free(objectives->projected[i].location);
  objectives->projected_count = 0;
  if (objectives->active) qa_mod_source_objectives_refresh(objectives);
}

void qa_mod_source_objectives_close(qa_mod_source_objectives *objectives) {
  if (objectives->closed) return;
  objectives->closed = true;
  qa_cleanup_stack_run_reverse(&objectives->removals);
  for (size_t i = 0; i < objectives->projected_count; i++)
    free(objectives->projected[i].location);
  objectives->projected_count = 0;
}
