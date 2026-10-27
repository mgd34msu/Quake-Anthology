/* Port of src/world/session/mod-client-input.ts. GPL-2.0-or-later. */
#include "qa/world/session/mod_client_input.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_errorf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  abort();
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

struct scope {
  void (*cleanup)(void *ctx);
  void *cleanup_ctx;
  bool opening;
  bool retired;
};
struct active_entry {
  qa_mod_client_application *application;
  struct scope *scope;
};
struct subscription {
  qa_mod_client_services *services;
  const qa_mod_client_input_binding *bindings;
  size_t binding_count;
  qa_mod_client_input_operations operations;
  qa_mod_client_unsubscribe unsubscribe;
  struct active_entry *active;
  size_t active_count;
  size_t active_capacity;
  struct scope **scopes;
  size_t scope_count;
  size_t scope_capacity;
};

static void drain(struct subscription *sub) {
  while (sub->scope_count != 0) {
    struct scope *scope = sub->scopes[sub->scope_count - 1];
    if (scope->opening || !scope->retired) break;
    sub->scope_count--;
    if (scope->cleanup != NULL) scope->cleanup(scope->cleanup_ctx);
    free(scope);
  }
}
static bool live_of(struct subscription *sub,
                    qa_mod_client_application *application) {
  qa_actor_id actor;
  qa_client_id client;
  return sub->services->actor(sub->services->ctx,
                              application->identity.client, &actor) &&
         qa_same_actor(actor, application->identity.actor) &&
         sub->services->for_actor(sub->services->ctx,
                                  application->identity.actor, &client) &&
         qa_same_client(client, application->identity.client);
}
static struct active_entry *find_active(
    struct subscription *sub, const qa_mod_client_application *application) {
  for (size_t i = 0; i < sub->active_count; i++)
    if (sub->active[i].application == application) return &sub->active[i];
  return NULL;
}
static void remove_active(struct subscription *sub,
                          struct active_entry *entry) {
  size_t index = (size_t)(entry - sub->active);
  memmove(&sub->active[index], &sub->active[index + 1],
          (sub->active_count - index - 1) * sizeof *sub->active);
  sub->active_count--;
}

struct run_state {
  struct subscription *sub;
  const qa_mod_client_input_binding *binding;
  qa_mod_client_application *application;
};
static void run_binding(void *ctx) {
  struct run_state *run = ctx;
  const uint8_t *calls = run->binding->calls;
  for (size_t i = 0; i < run->binding->call_count; i++) {
    if (find_active(run->sub, run->application) == NULL ||
        !live_of(run->sub, run->application))
      break;
    run->sub->operations.invoke(
        run->sub->operations.ctx,
        calls + i * run->binding->call_elem_size, run->application);
  }
}

static void on_event(void *ctx, qa_mod_client_application_event *event) {
  struct subscription *sub = ctx;
  qa_mod_client_application *application = event->application;
  if (event->is_before) {
    bool scoped = false;
    for (size_t i = 0; i < sub->binding_count; i++)
      if (str_eq(sub->bindings[i].scope, application->scope)) scoped = true;
    if (!live_of(sub, application) || !scoped) return;
    struct scope *scope = calloc(1, sizeof *scope);
    if (scope == NULL) abort();
    scope->opening = true;
    if (sub->active_count == sub->active_capacity) {
      sub->active_capacity =
          sub->active_capacity != 0 ? sub->active_capacity * 2 : 8;
      sub->active = xrealloc(sub->active,
                             sub->active_capacity * sizeof *sub->active);
    }
    sub->active[sub->active_count].application = application;
    sub->active[sub->active_count].scope = scope;
    sub->active_count++;
    if (sub->scope_count == sub->scope_capacity) {
      sub->scope_capacity =
          sub->scope_capacity != 0 ? sub->scope_capacity * 2 : 8;
      sub->scopes =
          xrealloc(sub->scopes, sub->scope_capacity * sizeof *sub->scopes);
    }
    sub->scopes[sub->scope_count++] = scope;
    sub->operations.open(sub->operations.ctx, application, &scope->cleanup,
                         &scope->cleanup_ctx);
    scope->opening = false;
    drain(sub);
  }
  struct active_entry *entry = find_active(sub, application);
  if (entry == NULL) return;
  struct scope *scope = entry->scope;
  for (size_t i = 0; i < sub->binding_count; i++) {
    const qa_mod_client_input_binding *binding = &sub->bindings[i];
    bool phase_matches =
        str_eq(binding->phase, event->is_before ? "before" : "after");
    if (!((event->is_before || str_eq(event->outcome, "completed")) &&
          str_eq(binding->scope, application->scope) && phase_matches))
      continue;
    struct run_state run = {sub, binding, application};
    if (event->is_before && str_eq(binding->phase, "before") &&
        binding->outputs != NULL) {
      if (sub->operations.output == NULL)
        qa_errorf("Source input output adapter is unavailable");
      qa_mod_client_input_output *outputs = NULL;
      size_t output_count = 0;
      sub->operations.output(sub->operations.ctx, binding->outputs,
                             binding->output_count,
                             binding->output_elem_size, application,
                             run_binding, &run, &outputs, &output_count);
      if (find_active(sub, application) != NULL &&
          live_of(sub, application))
        for (size_t j = 0; j < output_count; j++)
          event->output(event->output_ctx, outputs[j]);
      free(outputs);
    } else {
      run_binding(&run);
    }
  }
  if (!event->is_before) {
    entry = find_active(sub, application);
    if (entry != NULL) {
      remove_active(sub, entry);
      scope->retired = true;
      drain(sub);
    }
  }
}

static void do_unsubscribe(void *ctx) {
  struct subscription *sub = ctx;
  sub->unsubscribe.invoke(sub->unsubscribe.ctx);
  for (size_t i = 0; i < sub->active_count; i++)
    sub->active[i].scope->retired = true;
  sub->active_count = 0;
  drain(sub);
  free(sub->active);
  free(sub->scopes);
  free(sub);
}
static void noop_unsubscribe(void *ctx) { (void)ctx; }

qa_mod_client_unsubscribe qa_subscribe_mod_client_input(
    qa_mod_client_services *services,
    const qa_mod_client_input_binding *bindings, size_t binding_count,
    qa_mod_client_input_operations operations) {
  if (binding_count == 0) {
    qa_mod_client_unsubscribe none = {NULL, noop_unsubscribe};
    return none;
  }
  struct subscription *sub = calloc(1, sizeof *sub);
  if (sub == NULL) abort();
  sub->services = services;
  sub->bindings = bindings;
  sub->binding_count = binding_count;
  sub->operations = operations;
  sub->unsubscribe = services->subscribe_application(services->ctx, on_event,
                                                     sub);
  qa_mod_client_unsubscribe result = {sub, do_unsubscribe};
  return result;
}
