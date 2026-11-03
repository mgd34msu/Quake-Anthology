/* Port of src/world/session/mod-client-applications.ts.
 * GPL-2.0-or-later. */
#include "qa/world/session/mod_client_applications.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/world/handles.h"
#include "qa/world/session/mod_client_command.h"

/* TS `throw new Error(...)`: catchable (a guest mod's late output call must
 * not end the process); aborts only when nothing catches it. */
static noreturn void qa_errorf(const char *format, ...) {
  char message[1024];
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

struct listener_entry {
  int64_t id;
  qa_mod_client_application_listener_fn fn;
  void *ctx;
};
struct pending_entry {
  qa_mod_client_application *application;
  int64_t *listener_ids;
  size_t listener_count;
  size_t listener_capacity;
};
struct qa_mod_client_applications {
  qa_mod_client_applications_live_fn live;
  void *live_ctx;
  struct listener_entry *listeners;
  size_t listener_count;
  size_t listener_capacity;
  int64_t next_listener_id;
  struct pending_entry *pending;
  size_t pending_count;
  size_t pending_capacity;
  int64_t ordinal;
  qa_mod_client_application *current;
  qa_mod_client_encode_aim_fn encode_aim;
  void *encode_aim_ctx;
};

static bool live_of(qa_mod_client_applications *apps,
                    qa_mod_client_identity identity) {
  return apps->live(apps->live_ctx, identity);
}
static struct listener_entry *find_listener(
    qa_mod_client_applications *apps, int64_t id) {
  for (size_t i = 0; i < apps->listener_count; i++)
    if (apps->listeners[i].id == id) return &apps->listeners[i];
  return NULL;
}
static struct pending_entry *find_pending(
    qa_mod_client_applications *apps,
    const qa_mod_client_application *application) {
  for (size_t i = 0; i < apps->pending_count; i++)
    if (apps->pending[i].application == application)
      return &apps->pending[i];
  return NULL;
}
static void remove_pending(qa_mod_client_applications *apps,
                           struct pending_entry *entry) {
  size_t index = (size_t)(entry - apps->pending);
  free(entry->listener_ids);
  memmove(&apps->pending[index], &apps->pending[index + 1],
          (apps->pending_count - index - 1) * sizeof *apps->pending);
  apps->pending_count--;
}

/* Applies one output to the application's working state (TS outputs
 * map closure). Fatal on the TS validation failures. */
static void apply_output(qa_mod_client_applications *apps,
                         qa_mod_client_application *application,
                         qa_mod_client_input_output output) {
  size_t changes = qa_mod_client_output_change_count(output);
  for (size_t i = 0; i < changes; i++) {
    qa_mod_client_input_output change =
        qa_mod_client_output_change_at(output, i);
    if (str_eq(change.input, "view-angles")) {
      qa_vec3 value = change.vector_value;
      if (!isfinite(value.x) || !isfinite(value.y) || !isfinite(value.z))
        qa_range_error("Source input aim must be finite");
      qa_user_command next =
          apps->encode_aim != NULL
              ? apps->encode_aim(apps->encode_aim_ctx, value,
                                 application->command)
              : qa_mod_client_command_aim(application->command, value,
                                          application->absolute_aim);
      if (next.kind != application->command.kind)
        qa_errorf("Input output changed command dialect");
      application->command = next;
      application->absolute_aim = value;
    } else {
      application->command = qa_mod_client_command_scalar(
          application->command, change.input, change.number_value);
      if (str_eq(change.input, "impulse")) {
        application->controls_impulse = change.number_value;
        if (application->has_arsenal) {
          application->arsenal.has_impulse = true;
          application->arsenal.impulse = change.number_value;
        }
      }
    }
  }
}

struct output_state {
  qa_mod_client_applications *apps;
  qa_mod_client_application *application;
  int64_t listener_id;
  bool open;
  bool failed; /* TS failure.value */
  qa_caught_error failure;
};
struct output_call {
  struct output_state *state;
  qa_mod_client_input_output value;
};
static void gated_output_body(void *arg) {
  struct output_call *call = arg;
  struct output_state *state = call->state;
  qa_mod_client_input_output value = call->value;
  qa_mod_client_applications *apps = state->apps;
  if (!state->open ||
      find_listener(apps, state->listener_id) == NULL ||
      apps->current != state->application ||
      find_pending(apps, state->application) == NULL ||
      !live_of(apps, state->application->identity))
    qa_errorf("Input output is outside its live before callback");
  apply_output(apps, state->application, value);
}
/* The output closure records its throw before rethrowing; the before
 * callback rethrows it even when the listener caught it. */
/* ctx is a closure handle: a listener that keeps the output closure and
 * calls it after its callback returned gets the TS throw (open is false). */
static void gated_output(void *ctx, qa_mod_client_input_output value) {
  struct output_state *state = qa_world_handle_get(ctx);
  if (state == NULL) qa_errorf("Input output is outside its live before callback");
  struct output_call call = {state, value};
  if (!qa_try(gated_output_body, &call, &state->failure)) {
    state->failed = true;
    qa_rethrow(&state->failure);
  }
}
struct listener_call {
  qa_mod_client_application_listener_fn fn;
  void *ctx;
  qa_mod_client_application_event *event;
  struct output_state *state;
};
static void run_listener(void *arg) {
  struct listener_call *call = arg;
  call->fn(call->ctx, call->event);
  if (call->event->is_before && call->state->failed) qa_rethrow(&call->state->failure);
}
struct publish_scope {
  qa_mod_client_applications *apps;
  qa_mod_client_application *previous;
  int64_t *ids;
};
/* finally { this.current = previous } (and the id snapshot). */
static void publish_release(void *arg) {
  struct publish_scope *scope = arg;
  scope->apps->current = scope->previous;
  free(scope->ids);
}

static void publish(qa_mod_client_applications *apps,
                    qa_mod_client_application *application, bool is_before,
                    const char *outcome, const int64_t *listener_ids,
                    size_t listener_count) {
  qa_mod_client_application *previous = apps->current;
  apps->current = application;
  /* Snapshot the id list: callbacks may subscribe/unsubscribe. */
  int64_t *ids = NULL;
  if (listener_count != 0) {
    ids = malloc(listener_count * sizeof *ids);
    if (ids == NULL) abort();
    memcpy(ids, listener_ids, listener_count * sizeof *ids);
  }
  struct publish_scope scope = {apps, previous, ids};
  qa_cleanup node;
  qa_cleanup_push(&node, publish_release, &scope);
  size_t errors = 0;
  qa_caught_error first;
  memset(&first, 0, sizeof first);
  for (size_t i = 0; i < listener_count; i++) {
    struct listener_entry *listener = find_listener(apps, ids[i]);
    if (listener == NULL) continue;
    qa_mod_client_application_listener_fn fn = listener->fn;
    void *ctx = listener->ctx;
    if (is_before) {
      struct pending_entry *entered = find_pending(apps, application);
      if (entered == NULL || !live_of(apps, application->identity)) break;
      if (entered->listener_count == entered->listener_capacity) {
        entered->listener_capacity =
            entered->listener_capacity != 0 ? entered->listener_capacity * 2
                                            : 4;
        entered->listener_ids = xrealloc(
            entered->listener_ids,
            entered->listener_capacity * sizeof *entered->listener_ids);
      }
      entered->listener_ids[entered->listener_count++] = ids[i];
    }
    struct output_state state;
    memset(&state, 0, sizeof state);
    state.apps = apps;
    state.application = application;
    state.listener_id = ids[i];
    state.open = true;
    qa_mod_client_application_event event;
    memset(&event, 0, sizeof event);
    event.is_before = is_before;
    event.application = application;
    event.outcome = outcome;
    event.output = gated_output;
    event.output_ctx = qa_world_handle_open(&state);
    if (!is_before && str_eq(outcome, "completed") &&
        !live_of(apps, application->identity))
      event.outcome = "actor-removed";
    struct listener_call call = {fn, ctx, &event, &state};
    qa_caught_error caught;
    bool ok = qa_try(run_listener, &call, &caught);
    qa_world_handle_close(event.output_ctx);
    state.open = false;
    if (!ok) {
      if (is_before) qa_rethrow(&caught); /* the scope cleanup restores current */
      if (errors++ == 0) first = caught;
    }
  }
  qa_cleanup_pop(&node);
  publish_release(&scope);
  if (errors == 1) qa_rethrow(&first);
  if (errors > 1) qa_raise("AggregateError", 0, 0, "Client input completion callbacks failed");
}

qa_mod_client_applications *qa_mod_client_applications_create(
    qa_mod_client_applications_live_fn live, void *live_ctx) {
  qa_mod_client_applications *apps = calloc(1, sizeof *apps);
  if (apps == NULL) return NULL;
  apps->live = live;
  apps->live_ctx = live_ctx;
  return apps;
}
void qa_mod_client_applications_destroy(
    qa_mod_client_applications *applications) {
  if (applications == NULL) return;
  qa_mod_client_applications_close(applications);
  free(applications->listeners);
  free(applications->pending);
  free(applications);
}
bool qa_mod_client_applications_active(
    const qa_mod_client_applications *applications) {
  return applications->listener_count != 0;
}

int64_t qa_mod_client_applications_subscribe(
    qa_mod_client_applications *applications,
    qa_mod_client_application_listener_fn listener, void *listener_ctx) {
  if (applications->listener_count == applications->listener_capacity) {
    applications->listener_capacity =
        applications->listener_capacity != 0
            ? applications->listener_capacity * 2
            : 8;
    applications->listeners = xrealloc(
        applications->listeners,
        applications->listener_capacity * sizeof *applications->listeners);
  }
  int64_t id = ++applications->next_listener_id;
  applications->listeners[applications->listener_count].id = id;
  applications->listeners[applications->listener_count].fn = listener;
  applications->listeners[applications->listener_count].ctx = listener_ctx;
  applications->listener_count++;
  return id;
}
void qa_mod_client_applications_unsubscribe(
    qa_mod_client_applications *applications, int64_t subscription) {
  for (size_t i = 0; i < applications->listener_count; i++)
    if (applications->listeners[i].id == subscription) {
      memmove(&applications->listeners[i],
              &applications->listeners[i + 1],
              (applications->listener_count - i - 1) *
                  sizeof *applications->listeners);
      applications->listener_count--;
      return;
    }
}

struct before_publish {
  qa_mod_client_applications *apps;
  qa_mod_client_application *application;
  const int64_t *ids;
  size_t count;
};
static void run_before_publish(void *arg) {
  struct before_publish *job = arg;
  publish(job->apps, job->application, true, NULL, job->ids, job->count);
}
struct failed_finish {
  qa_mod_client_applications *apps;
  qa_mod_client_application *application;
};
static void run_failed_finish(void *arg) {
  struct failed_finish *job = arg;
  qa_mod_client_applications_finish(job->apps, job->application, true);
}
static void run_release_finish(void *arg) {
  struct failed_finish *job = arg;
  qa_mod_client_applications_finish(job->apps, job->application, false);
}

qa_mod_client_application *qa_mod_client_applications_begin(
    qa_mod_client_applications *applications,
    qa_mod_client_application_input input,
    qa_mod_client_encode_aim_fn encode_aim, void *encode_aim_ctx) {
  if (!qa_mod_client_applications_active(applications) ||
      !live_of(applications, input.identity))
    return NULL;
  if (applications->ordinal == QA_MAX_SAFE_INTEGER)
    qa_range_error("Client application ordinal exhausted");
  qa_mod_client_application *parent = applications->current;
  if (input.has_parent_invocation) {
    bool found = applications->current != NULL &&
                 applications->current->invocation ==
                     input.parent_invocation;
    for (size_t i = 0; i < applications->pending_count; i++) {
      qa_mod_client_application *pending =
          applications->pending[i].application;
      if (pending->invocation == input.parent_invocation) {
        found = true;
        if (parent == NULL || pending->invocation > parent->invocation)
          parent = pending;
        break;
      }
    }
    if (!found) qa_errorf("Input application parent is no longer active");
  }
  qa_mod_client_application *application = calloc(1, sizeof *application);
  if (application == NULL) abort();
  application->identity = input.identity;
  application->scope = input.scope;
  application->command = input.command;
  application->angle_space = input.angle_space;
  application->absolute_aim = input.absolute_aim;
  application->frame = input.frame;
  application->accepted = input.accepted;
  /* Arsenal inheritance mirrors the TS ternary chain. */
  const qa_arsenal_intent *arsenal = NULL;
  if (input.has_arsenal) {
    arsenal = input.arsenal;
  } else if (parent != NULL &&
             qa_same_actor(parent->identity.actor, input.identity.actor)) {
    arsenal = parent->has_arsenal ? &parent->arsenal : NULL;
  } else if (input.accepted != NULL) {
    arsenal = input.accepted->input.arsenal;
  }
  if (arsenal != NULL) {
    application->has_arsenal = true;
    application->arsenal = *arsenal;
  }
  /* controls?.impulse ?? arsenal?.impulse ?? command impulse: an arsenal
   * intent without an impulse falls through to the command. */
  application->controls_impulse =
      input.has_controls
          ? input.controls_impulse
          : application->has_arsenal && application->arsenal.has_impulse
                ? application->arsenal.impulse
                : qa_mod_client_command_impulse(input.command);
  application->invocation = ++applications->ordinal;
  application->has_parent_invocation = parent != NULL;
  application->parent_invocation = parent != NULL ? parent->invocation : 0;
  if (applications->pending_count == applications->pending_capacity) {
    applications->pending_capacity =
        applications->pending_capacity != 0
            ? applications->pending_capacity * 2
            : 8;
    applications->pending = xrealloc(
        applications->pending,
        applications->pending_capacity * sizeof *applications->pending);
  }
  struct pending_entry *entry =
      &applications->pending[applications->pending_count++];
  memset(entry, 0, sizeof *entry);
  entry->application = application;
  qa_mod_client_encode_aim_fn saved_encode = applications->encode_aim;
  void *saved_encode_ctx = applications->encode_aim_ctx;
  applications->encode_aim = encode_aim;
  applications->encode_aim_ctx = encode_aim_ctx;
  /* Snapshot listener ids for the before publication. */
  size_t count = applications->listener_count;
  int64_t *ids = NULL;
  if (count != 0) {
    ids = malloc(count * sizeof *ids);
    if (ids == NULL) abort();
    for (size_t i = 0; i < count; i++) ids[i] = applications->listeners[i].id;
  }
  struct before_publish job = {applications, application, ids, count};
  qa_caught_error caught;
  bool ok = qa_try(run_before_publish, &job, &caught);
  free(ids);
  applications->encode_aim = saved_encode;
  applications->encode_aim_ctx = saved_encode_ctx;
  if (!ok) {
    /* catch (error) { try { finish(application, true) } catch (cleanup) {
     * throw AggregateError } throw error } */
    struct failed_finish cleanup_job = {applications, application};
    qa_caught_error cleanup;
    if (!qa_try(run_failed_finish, &cleanup_job, &cleanup))
      qa_raise("AggregateError", 0, 0, "Input callback and cleanup failed");
    qa_rethrow(&caught);
  }
  return application;
}

void qa_mod_client_applications_finish(
    qa_mod_client_applications *applications,
    qa_mod_client_application *application, bool failed) {
  if (application == NULL) return;
  struct pending_entry *entry = find_pending(applications, application);
  if (entry == NULL) return;
  size_t count = entry->listener_count;
  int64_t *ids = entry->listener_ids;
  entry->listener_ids = NULL;
  entry->listener_count = 0;
  remove_pending(applications, entry);
  const char *outcome =
      failed ? "failed"
             : live_of(applications, application->identity)
                   ? "completed"
                   : "actor-removed";
  publish(applications, application, false, outcome, ids, count);
  free(ids);
}

void qa_mod_client_applications_release(
    qa_mod_client_applications *applications, qa_actor_id actor) {
  /* A reversed snapshot of the pending applications; each finish runs in
   * its own try. One failure rethrows; several become one AggregateError. */
  size_t count = 0;
  qa_mod_client_application **targets =
      malloc((applications->pending_count != 0 ? applications->pending_count : 1) *
             sizeof *targets);
  if (targets == NULL) abort();
  for (size_t i = applications->pending_count; i-- > 0;) {
    qa_mod_client_application *candidate = applications->pending[i].application;
    if (qa_same_actor(candidate->identity.actor, actor)) targets[count++] = candidate;
  }
  size_t errors = 0;
  qa_caught_error first;
  memset(&first, 0, sizeof first);
  for (size_t i = 0; i < count; i++) {
    struct failed_finish job = {applications, targets[i]};
    qa_caught_error caught;
    if (!qa_try(run_release_finish, &job, &caught) && errors++ == 0) first = caught;
  }
  free(targets);
  if (errors == 1) qa_rethrow(&first);
  if (errors > 1) qa_raise("AggregateError", 0, 0, "Client input release callbacks failed");
}

int64_t qa_mod_client_applications_checkpoint(
    qa_mod_client_applications *applications) {
  if (applications->pending_count != 0)
    qa_errorf("Cannot save during client input application");
  return applications->ordinal;
}
void qa_mod_client_applications_restore(
    qa_mod_client_applications *applications, int64_t ordinal) {
  if (ordinal < 0 || ordinal > QA_MAX_SAFE_INTEGER || applications->pending_count != 0)
    qa_errorf("Invalid client application continuation");
  applications->ordinal = ordinal;
}
void qa_mod_client_applications_close(
    qa_mod_client_applications *applications) {
  applications->listener_count = 0;
  for (size_t i = 0; i < applications->pending_count; i++)
    free(applications->pending[i].listener_ids);
  applications->pending_count = 0;
  applications->current = NULL;
}
void qa_mod_client_application_dispose(
    qa_mod_client_application *application) {
  free(application);
}
