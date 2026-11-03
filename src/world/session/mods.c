/* Port of src/world/session/mods.ts. GPL-2.0-or-later. */
#include "qa/world/session/mods.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include "qa/foundation/support.h"
#include "qa/world/handles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/selection.h"
#include "qa/world/session/mod_presentations.h"

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
static void *xmalloc(size_t size) {
  void *ptr = malloc(size != 0 ? size : 1);
  if (ptr == NULL) abort();
  return ptr;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}
static bool same_module(qa_module_identity left, qa_module_identity right) {
  return str_eq(left.id, right.id) &&
         str_eq(left.artifact_path, right.artifact_path) &&
         str_eq(left.digest, right.digest) &&
         str_eq(left.revision, right.revision);
}

/* TS retainIdentity: copies the identity and its arrays. */
static qa_mod_identity retain_identity(qa_mod_identity identity) {
  qa_mod_identity retained = identity;
  if (identity.module_count != 0) {
    qa_module_identity *modules =
        xmalloc(identity.module_count * sizeof *modules);
    memcpy(modules, identity.modules,
           identity.module_count * sizeof *modules);
    retained.modules = modules;
  }
  if (identity.provider_count != 0) {
    qa_provider_checkpoint_ref *providers =
        xmalloc(identity.provider_count * sizeof *providers);
    memcpy(providers, identity.providers,
           identity.provider_count * sizeof *providers);
    retained.providers = providers;
  }
  return retained;
}
static void free_retained_identity(qa_mod_identity *identity) {
  free((void *)identity->modules);
  free((void *)identity->providers);
}

/* TS closeScopes: newest first, each in its own try; failures become one
 * AggregateError. Closes only: the scopes' memory belongs to their mod
 * records (freed at qa_session_mods_destroy), so a mod that kept its scope or
 * its assertCurrent closure sees the TS "closed" throw instead of freed memory. */
static void close_scope_call(void *ctx) { qa_resource_scope_close(ctx); }
static size_t close_scopes_collect(qa_resource_scope **scopes, size_t count) {
  size_t errors = 0;
  for (size_t i = count; i-- > 0;) {
    qa_caught_error caught;
    if (!qa_try(close_scope_call, scopes[i], &caught)) errors++;
  }
  return errors;
}


struct removal_node {
  qa_mod_operation *operation;
  int64_t sequence;
  bool removed;
};
static void remove_registration(void *ctx) {
  struct removal_node *node = ctx;
  if (node->removed) return;
  node->removed = true;
  qa_mod_operation_unregister(node->operation, node->sequence);
}
static void defer_removal(void *ctx) {
  struct removal_node *node = ctx;
  remove_registration(node);
  free(node);
}

qa_mod_registration_removal qa_mod_registrations_register(
    qa_mod_registrations *registrations, qa_mod_operation *operation,
    qa_mod_operation_registration contribution) {
  qa_resource_scope_assert_open(registrations->resources);
  contribution.identity.provider = registrations->instance;
  contribution.identity.order = registrations->order;
  int64_t sequence =
      qa_mod_operation_register(operation, contribution);
  struct removal_node *node = xmalloc(sizeof *node);
  node->operation = operation;
  node->sequence = sequence;
  node->removed = false;
  qa_resource_scope_defer(registrations->resources, defer_removal, node);
  qa_mod_registration_removal removal = {node, remove_registration};
  return removal;
}

static void runtime_close_ctx(void *ctx) {
  qa_mod_runtime *runtime = ctx;
  runtime->close(runtime->ctx);
}
qa_session_resource qa_mod_runtime_as_resource(qa_mod_runtime *runtime) {
  qa_session_resource resource = {runtime, runtime_close_ctx};
  return resource;
}

struct prepared_entry {
  const qa_prepared_mod *prepared;
  qa_mod_identity identity; /* retained */
  char *key;                /* owned selection key */
};
struct active_entry {
  struct prepared_entry *entry;
  bool has_presentation_owner;
  qa_presentation_owner presentation_owner;
  qa_resource_scope *resources;
  qa_mod_runtime runtime;
  char *instance; /* owned modInstanceProvider string */
};
struct qa_session_mods {
  qa_session_mods_options options;
  struct prepared_entry *prepared;
  size_t prepared_count;
  struct active_entry **active;
  size_t active_count;
  size_t active_capacity;
  qa_resource_scope **pending;
  size_t pending_count;
  size_t pending_capacity;
  /* One per initialized mod: its scope, instance id and initialization
   * closure state, freed at destroy (TS keeps them while referenced). */
  struct init_ctx **records;
  size_t record_count, record_capacity;
  bool closed;
  bool busy;
  int64_t next_order;
  /* Scratch returns. */
  qa_mod_selection *scratch_enabled;
  size_t scratch_enabled_capacity;
  qa_app_simulation_presentation *scratch_presentations;
  size_t scratch_presentations_capacity;
  qa_session_mod_appearance_group *scratch_groups;
  size_t scratch_groups_capacity;
  qa_app_simulation_presentation *scratch_group_items;
  size_t scratch_group_items_capacity;
  qa_active_mod_client_presentation *scratch_client_sources;
  size_t scratch_client_sources_capacity;
  qa_active_mod_presentation *scratch_sources;
  size_t scratch_sources_capacity;
};

static void assert_open(const qa_session_mods *mods) {
  if (mods->closed) qa_errorf("Mod session is closed");
}
static void exclusive_begin(qa_session_mods *mods) {
  assert_open(mods);
  if (mods->busy)
    qa_errorf("Mod session lifecycle operation is already active");
  mods->busy = true;
}
static void exclusive_end(qa_session_mods *mods) { mods->busy = false; }
static void exclusive_end_cleanup(void *arg) { exclusive_end(arg); }

static struct prepared_entry *find_prepared(qa_session_mods *mods,
                                            const char *key) {
  for (size_t i = 0; i < mods->prepared_count; i++)
    if (str_eq(mods->prepared[i].key, key)) return &mods->prepared[i];
  return NULL;
}

static void validate_state(struct prepared_entry *entry,
                           qa_mod_private_checkpoint state) {
  const qa_mod_identity *identity = &entry->identity;
  size_t expected_guests =
      str_eq(entry->prepared->module_checkpoint, "provider")
          ? 0
          : identity->module_count;
  bool guests_ok = state.guest_count == expected_guests;
  for (size_t i = 0; guests_ok && i < state.guest_count; i++)
    guests_ok = same_module(state.guests[i].module, identity->modules[i]);
  if (!guests_ok)
    qa_errorf("Mod guest state ownership differs: %s", entry->key);
  bool providers_ok = state.provider_count == identity->provider_count;
  for (size_t i = 0; providers_ok && i < state.provider_count; i++) {
    const qa_provider_checkpoint_ref *expected = &identity->providers[i];
    providers_ok = str_eq(state.providers[i].provider, expected->provider) &&
                   str_eq(state.providers[i].schema, expected->schema) &&
                   state.providers[i].version == expected->version;
  }
  if (!providers_ok)
    qa_errorf("Mod provider state ownership differs: %s", entry->key);
  entry->prepared->validate_state(entry->prepared->ctx, state);
}

static void validate_saved(struct prepared_entry **entries, size_t count,
                           const qa_mod_session_checkpoint *saved) {
  if (saved->version != 1 || saved->mod_count != count)
    qa_errorf("Saved mod selection differs from the enabled mods");
  for (size_t i = 0; i < count; i++) {
    if (!qa_same_mod_identity(&entries[i]->identity,
                              &saved->mods[i].identity))
      qa_errorf("Saved mod order or identity differs: %s", entries[i]->key);
    validate_state(entries[i], saved->mods[i].state);
  }
}
static void validate_travel(struct prepared_entry **entries, size_t count,
                            const qa_mod_travel_checkpoint *travel) {
  if (travel->version != 1 || travel->mod_count != count)
    qa_errorf("Travel mod selection differs from the enabled mods");
  for (size_t i = 0; i < count; i++) {
    const qa_mod_travel_entry *checkpoint = &travel->mods[i];
    if (!qa_same_mod_identity(&entries[i]->identity, &checkpoint->identity))
      qa_errorf("Travel mod order or identity differs: %s", entries[i]->key);
    if ((checkpoint->state != NULL) !=
        str_eq(entries[i]->prepared->travel, "retain"))
      qa_errorf("Mod travel state differs from its source lifecycle");
    if (checkpoint->state != NULL)
      validate_state(entries[i], *checkpoint->state);
  }
}

static void combat_assert_idle(qa_session_mods *mods) {
  if (mods->options.services != NULL &&
      mods->options.services->combat != NULL)
    qa_gameplay_authority_assert_idle(mods->options.services->combat);
}

/* Cleanup nodes freeing a pointer variable's current value on a raise. */
static void free_checkpoints(void *arg) { free(*(qa_mod_checkpoint **)arg); }
static void free_travel_entries(void *arg) { free(*(qa_mod_travel_entry **)arg); }
static void free_prepared_list(void *arg) { free(*(struct prepared_entry ***)arg); }

static void capture(qa_session_mods *mods, qa_mod_checkpoint **out_mods,
                    size_t *out_count) {
  combat_assert_idle(mods);
  qa_mod_checkpoint *captured = NULL;
  if (mods->active_count != 0)
    captured = xmalloc(mods->active_count * sizeof *captured);
  qa_cleanup node; /* a throwing checkpoint drops the partial capture */
  qa_cleanup_push(&node, free_checkpoints, &captured);
  for (size_t i = 0; i < mods->active_count; i++) {
    struct active_entry *entry = mods->active[i];
    qa_mod_private_checkpoint state =
        entry->runtime.checkpoint(entry->runtime.ctx);
    assert_open(mods);
    combat_assert_idle(mods);
    validate_state(entry->entry, state);
    captured[i].identity = entry->entry->identity;
    captured[i].state = state;
  }
  qa_cleanup_pop(&node);
  *out_mods = captured;
  *out_count = mods->active_count;
}

/* A mod's initialization record. The mod's assertCurrent/nextFrame
 * closures reach it through a handle: once the SessionMods is destroyed (it
 * was closed first) a late call gets the TS "Mod session is closed". */
struct init_ctx {
  qa_session_mods *mods;
  qa_resource_scope *resources;
  char *instance;
  void *token;
};
static struct init_ctx *init_of(void *token) {
  struct init_ctx *init = qa_world_handle_get(token);
  if (init == NULL) qa_errorf("Mod session is closed");
  return init;
}
static void init_assert_current(void *ctx) {
  struct init_ctx *init = init_of(ctx);
  assert_open(init->mods);
  qa_resource_scope_assert_open(init->resources);
}
static void init_next_frame(void *ctx) {
  struct init_ctx *init = init_of(ctx);
  init_assert_current(ctx);
  init->mods->options.next_frame(init->mods->options.ctx);
  init_assert_current(ctx);
}


/* State of one apply() across its try (TS apply's try/catch/finally). */
struct apply_frame {
  qa_session_mods *mods;
  struct prepared_entry **entries;
  size_t selection_count;
  const qa_mod_session_checkpoint *saved;
  const qa_mod_travel_checkpoint *travel;
  struct active_entry **next;
  size_t next_count;
  struct active_entry **opened;
  size_t opened_count;
  qa_resource_scope **scopes;
  size_t scope_count;
};

static void apply_body(void *ctx) {
  struct apply_frame *f = ctx;
  qa_session_mods *mods = f->mods;
  for (size_t i = 0; i < f->selection_count; i++) {
    struct prepared_entry *entry = f->entries[i];
    struct active_entry *previous = NULL;
    for (size_t j = 0; j < mods->active_count; j++)
      if (str_eq(mods->active[j]->entry->key, entry->key))
        previous = mods->active[j];
    if (previous != NULL) {
      f->next[f->next_count++] = previous;
      continue;
    }
    char *name = xmalloc(strlen("mod ") + strlen(entry->key) + 1);
    strcpy(name, "mod ");
    strcat(name, entry->key);
    qa_resource_scope *resources = qa_resource_scope_create(name);
    free(name);
    if (resources == NULL) abort();
    char *instance = qa_mod_instance_provider(entry->identity.selection);
    if (instance == NULL) abort();
    /* The record owns the scope and instance id from here on. */
    struct init_ctx *init = xmalloc(sizeof *init);
    init->mods = mods;
    init->resources = resources;
    init->instance = instance;
    init->token = qa_world_handle_open(init);
    if (mods->record_count == mods->record_capacity) {
      mods->record_capacity = mods->record_capacity != 0 ? mods->record_capacity * 2 : 8;
      mods->records = xrealloc(mods->records, mods->record_capacity * sizeof *mods->records);
    }
    mods->records[mods->record_count++] = init;
    f->scopes[f->scope_count++] = resources;
    if (mods->pending_count == mods->pending_capacity) {
      mods->pending_capacity =
          mods->pending_capacity != 0 ? mods->pending_capacity * 2 : 8;
      mods->pending = xrealloc(
          mods->pending, mods->pending_capacity * sizeof *mods->pending);
    }
    mods->pending[mods->pending_count++] = resources;
    const qa_mod_private_checkpoint *state = NULL;
    if (f->saved != NULL) state = &f->saved->mods[i].state;
    else if (f->travel != NULL) state = f->travel->mods[i].state;
    /* TS { ...services, engine: { ...services.engine, events } }: the runtime keeps the
     * services it is initialized with, so the copies live as long as its resource scope. */
    const qa_mod_host_services *services = mods->options.services;
    bool has_owner = false;
    qa_presentation_owner owner = {NULL, 0};
    if (services != NULL && services->engine != NULL) {
      if (mods->options.presentation == NULL)
        qa_errorf("Component engine output requires presentation ownership");
      const qa_session_mods_presentation_option *binding =
          mods->options.presentation;
      qa_session_mod_bound_events bound = binding->bind_owner(
          binding->ctx, instance, entry->identity.source.content,
          state != NULL);
      qa_resource_scope_own(resources, bound.resource);
      has_owner = true;
      owner = bound.owner;
      qa_session_mod_engine_services *engine_copy = xmalloc(sizeof *engine_copy);
      qa_resource_scope_defer(resources, free, engine_copy);
      *engine_copy = *services->engine;
      engine_copy->events = bound.events;
      qa_mod_host_services *services_copy = xmalloc(sizeof *services_copy);
      qa_resource_scope_defer(resources, free, services_copy);
      *services_copy = *services;
      services_copy->engine = engine_copy;
      services = services_copy;
    }
    qa_mod_initialization context;
    memset(&context, 0, sizeof context);
    context.restoring = state != NULL;
    context.services = services;
    context.instance = instance;
    context.resources = resources;
    context.ctx = init->token;
    context.next_frame = init_next_frame;
    context.assert_current = init_assert_current;
    qa_mod_runtime runtime =
        entry->prepared->initialize(entry->prepared->ctx, &context);
    if (qa_resource_scope_is_closed(resources)) {
      runtime.close(runtime.ctx);
      qa_errorf("Mod session closed during initialization");
    }
    struct active_entry *active = xmalloc(sizeof *active);
    memset(active, 0, sizeof *active);
    active->entry = entry;
    active->resources = resources;
    active->runtime = runtime;
    active->instance = instance;
    active->has_presentation_owner = has_owner;
    active->presentation_owner = owner;
    /* Listed before the scope owns it: a raise below frees it with this apply. */
    f->opened[f->opened_count++] = active;
    qa_session_resource wrapped;
    wrapped.close = (void (*)(void *))runtime_close_ctx;
    wrapped.ctx = &active->runtime;
    qa_resource_scope_own(resources, wrapped);
    init_assert_current(init->token);
    f->next[f->next_count++] = active;
    if (state != NULL) {
      runtime.restore(runtime.ctx, *state);
      init_assert_current(init->token);
    }
    if (runtime.activate != NULL) runtime.activate(runtime.ctx);
    init_assert_current(init->token);
  }
  for (size_t i = 0; i < f->opened_count; i++) {
    qa_mod_registrations registrations;
    memset(&registrations, 0, sizeof registrations);
    registrations.instance = f->opened[i]->instance;
    registrations.operations = mods->options.operations;
    registrations.order = mods->next_order++;
    registrations.resources = f->opened[i]->resources;
    f->opened[i]->runtime.register_(f->opened[i]->runtime.ctx, &registrations);
    assert_open(mods);
  }
}

/* TS finally { for (const scope of scopes) this.pending.delete(scope) }. */
static void apply_unpend(struct apply_frame *f) {
  qa_session_mods *mods = f->mods;
  for (size_t i = 0; i < f->scope_count; i++)
    for (size_t j = 0; j < mods->pending_count; j++)
      if (mods->pending[j] == f->scopes[i]) {
        memmove(&mods->pending[j], &mods->pending[j + 1],
                (mods->pending_count - j - 1) * sizeof *mods->pending);
        mods->pending_count--;
        break;
      }
}

static void apply(qa_session_mods *mods, const qa_mod_selection *selections,
                  size_t selection_count,
                  const qa_mod_session_checkpoint *saved,
                  const qa_mod_travel_checkpoint *travel) {
  struct prepared_entry **entries = NULL;
  if (selection_count != 0)
    entries = xmalloc(selection_count * sizeof *entries);
  qa_cleanup entries_node; /* the validation below throws */
  qa_cleanup_push(&entries_node, free_prepared_list, &entries);
  for (size_t i = 0; i < selection_count; i++) {
    char *key = qa_mod_selection_key(selections[i]);
    if (key == NULL) abort();
    entries[i] = find_prepared(mods, key);
    if (entries[i] == NULL) {
      char message[2048];
      (void)snprintf(message, sizeof message, "Selected mod is not prepared: %s", key);
      free(key);
      qa_errorf("%s", message);
    }
    free(key);
  }
  struct prepared_entry *hud = NULL, *view = NULL;
  for (size_t i = 0; i < selection_count; i++) {
    const qa_prepared_mod *prepared = entries[i]->prepared;
    if (!prepared->has_client_presentation) continue;
    if (str_eq(prepared->client_presentation.hud, "replace")) {
      if (hud != NULL)
        qa_errorf("Component HUD replacement conflict: %s and %s", hud->key,
                  entries[i]->key);
      hud = entries[i];
    }
    if (prepared->client_presentation.view) {
      if (view != NULL)
        qa_errorf("Component camera control conflict: %s and %s", view->key,
                  entries[i]->key);
      view = entries[i];
    }
  }
  if (saved != NULL) validate_saved(entries, selection_count, saved);
  if (travel != NULL) validate_travel(entries, selection_count, travel);
  qa_cleanup_pop(&entries_node);
  struct apply_frame f;
  memset(&f, 0, sizeof f);
  f.mods = mods;
  f.entries = entries;
  f.selection_count = selection_count;
  f.saved = saved;
  f.travel = travel;
  if (selection_count != 0) {
    f.next = xmalloc(selection_count * sizeof *f.next);
    f.opened = xmalloc(selection_count * sizeof *f.opened);
    f.scopes = xmalloc(selection_count * sizeof *f.scopes);
  }
  qa_caught_error error;
  bool ok = qa_try(apply_body, &f, &error);
  apply_unpend(&f);
  if (!ok) {
    /* catch (error) { closeScopes(scopes) ... throw } */
    size_t cleanup_errors = close_scopes_collect(f.scopes, f.scope_count);
    for (size_t i = 0; i < f.opened_count; i++) free(f.opened[i]);
    free(f.next);
    free(f.opened);
    free(f.scopes);
    free(entries);
    if (cleanup_errors > 0)
      qa_raise("AggregateError", 0, 0, "Mod preparation and cleanup failed");
    qa_rethrow(&error);
  }
  /* Removed actives close after the new set is published. */
  struct active_entry **removed_entries = NULL;
  qa_resource_scope **removed = NULL;
  size_t removed_count = 0;
  if (mods->active_count != 0) {
    removed = xmalloc(mods->active_count * sizeof *removed);
    removed_entries = xmalloc(mods->active_count * sizeof *removed_entries);
  }
  for (size_t i = 0; i < mods->active_count; i++) {
    bool kept = false;
    for (size_t j = 0; j < f.next_count; j++)
      if (f.next[j] == mods->active[i]) kept = true;
    if (!kept) {
      removed[removed_count] = mods->active[i]->resources;
      removed_entries[removed_count] = mods->active[i];
      removed_count++;
    }
  }
  free(mods->active);
  mods->active = f.next;
  mods->active_count = f.next_count;
  mods->active_capacity = selection_count;
  free(f.opened);
  free(f.scopes);
  free(entries);
  size_t cleanup_errors = close_scopes_collect(removed, removed_count);
  for (size_t i = 0; i < removed_count; i++) free(removed_entries[i]);
  free(removed);
  free(removed_entries);
  if (cleanup_errors > 0) qa_raise("AggregateError", 0, 0, "Mod cleanup failed");
}

static void selection_set_for(qa_session_mods *mods,
                              const qa_mod_selection *enabled,
                              size_t enabled_count,
                              qa_mod_selection_set *out) {
  qa_mod_description *descriptions = NULL;
  if (mods->prepared_count != 0) {
    descriptions = xmalloc(mods->prepared_count * sizeof *descriptions);
    for (size_t i = 0; i < mods->prepared_count; i++)
      descriptions[i] = mods->prepared[i].prepared->description;
  }
  if (!qa_mod_selection_set_init(out, descriptions, mods->prepared_count,
                                 enabled, enabled_count))
    abort();
  free(descriptions);
}

struct open_call {
  qa_session_mods *mods;
  const qa_mod_selection *enabled;
  size_t enabled_count;
  const qa_mod_session_checkpoint *saved;
  const qa_mod_travel_checkpoint *travel;
};
/* await owner.exclusive(apply); options.presentation?.finishOwnerRestore() */
static void open_body(void *ctx) {
  struct open_call *call = ctx;
  qa_session_mods *mods = call->mods;
  exclusive_begin(mods);
  qa_cleanup node; /* exclusive: finally { busy = false } */
  qa_cleanup_push(&node, exclusive_end_cleanup, mods);
  apply(mods, call->enabled, call->enabled_count, call->saved, call->travel);
  qa_cleanup_pop(&node);
  exclusive_end(mods);
  if (mods->options.presentation != NULL &&
      mods->options.presentation->finish_owner_restore != NULL)
    mods->options.presentation->finish_owner_restore(
        mods->options.presentation->ctx);
}
static void mods_close_call(void *ctx) { qa_session_mods_close(ctx); }

qa_session_mods *qa_session_mods_open(
    const qa_session_mods_options *options,
    const qa_mod_session_checkpoint *saved,
    const qa_mod_travel_checkpoint *travel) {
  if (saved != NULL && travel != NULL)
    qa_errorf(
        "Mod save restoration and world travel are distinct lifecycle "
        "operations");
  qa_session_mods *mods = calloc(1, sizeof *mods);
  if (mods == NULL) return NULL;
  mods->options = *options;
  mods->prepared_count = options->prepared_count;
  if (mods->prepared_count != 0) {
    mods->prepared =
        xmalloc(mods->prepared_count * sizeof *mods->prepared);
    memset(mods->prepared, 0,
           mods->prepared_count * sizeof *mods->prepared);
  }
  for (size_t i = 0; i < mods->prepared_count; i++) {
    const qa_prepared_mod *prepared = &options->prepared[i];
    char *key = qa_mod_selection_key(prepared->description.selection);
    if (key == NULL) abort();
    qa_mod_identity identity = retain_identity(prepared->identity);
    for (size_t j = 0; j < i; j++)
      if (str_eq(mods->prepared[j].key, key))
        qa_errorf("Duplicate prepared mod: %s", key);
    char *identity_key = qa_mod_selection_key(identity.selection);
    if (identity_key == NULL) abort();
    bool differs = !str_eq(key, identity_key) ||
                   !str_eq(identity.source.provider,
                           prepared->description.source.provider) ||
                   !str_eq(identity.source.content,
                           prepared->description.source.content);
    free(identity_key);
    if (differs)
      qa_errorf("Prepared mod identity differs from its declaration: %s",
                key);
    mods->prepared[i].prepared = prepared;
    mods->prepared[i].identity = identity;
    mods->prepared[i].key = key;
  }
  qa_mod_selection_set set;
  selection_set_for(mods, options->enabled, options->enabled_count, &set);
  size_t enabled_count = 0;
  const qa_mod_selection *enabled =
      qa_mod_selection_set_enabled(&set, &enabled_count);
  qa_mod_selection *enabled_copy = NULL;
  if (enabled_count != 0) {
    enabled_copy = xmalloc(enabled_count * sizeof *enabled_copy);
    memcpy(enabled_copy, enabled, enabled_count * sizeof *enabled_copy);
  }
  qa_mod_selection_set_destroy(&set);
  struct open_call call = {mods, enabled_copy, enabled_count, saved, travel};
  qa_caught_error error;
  bool ok = qa_try(open_body, &call, &error);
  free(enabled_copy);
  if (!ok) {
    /* catch (error) { try { owner.close() } catch (cleanup) { throw
     * AggregateError } throw error }: the owner never escapes, so C frees it. */
    qa_caught_error cleanup;
    bool closed = qa_try(mods_close_call, mods, &cleanup);
    qa_session_mods_destroy(mods);
    if (!closed)
      qa_raise("AggregateError", 0, 0, "Mod initialization and cleanup failed");
    qa_rethrow(&error);
  }
  return mods;
}

void qa_session_mods_destroy(qa_session_mods *mods) {
  if (mods == NULL) return;
  qa_session_mods_close(mods);
  for (size_t i = 0; i < mods->prepared_count; i++) {
    /* Keys/identities exist only for fully constructed entries. */
    free(mods->prepared[i].key);
    free_retained_identity(&mods->prepared[i].identity);
  }
  free(mods->prepared);
  for (size_t i = 0; i < mods->record_count; i++) {
    qa_world_handle_close(mods->records[i]->token);
    qa_resource_scope_destroy(mods->records[i]->resources);
    free(mods->records[i]->instance);
    free(mods->records[i]);
  }
  free(mods->records);
  free(mods->active);
  free(mods->pending);
  free(mods->scratch_enabled);
  free(mods->scratch_presentations);
  free(mods->scratch_groups);
  free(mods->scratch_group_items);
  free(mods->scratch_client_sources);
  free(mods->scratch_sources);
  free(mods);
}

const qa_mod_selection *qa_session_mods_enabled(qa_session_mods *mods,
                                                size_t *out_count) {
  if (mods->active_count > mods->scratch_enabled_capacity) {
    mods->scratch_enabled_capacity = mods->active_count;
    mods->scratch_enabled = xrealloc(
        mods->scratch_enabled,
        mods->scratch_enabled_capacity * sizeof *mods->scratch_enabled);
  }
  for (size_t i = 0; i < mods->active_count; i++)
    mods->scratch_enabled[i] = mods->active[i]->entry->identity.selection;
  *out_count = mods->active_count;
  return mods->scratch_enabled;
}

bool qa_session_mods_owns(qa_session_mods *mods, qa_provider_id provider) {
  for (size_t i = 0; i < mods->active_count; i++)
    if (str_eq(mods->active[i]->instance, provider)) return true;
  return false;
}

const qa_app_simulation_presentation *qa_session_mods_presentations(
    qa_session_mods *mods, size_t *out_count) {
  size_t count = 0;
  for (size_t i = 0; i < mods->active_count; i++) {
    qa_mod_runtime *runtime = &mods->active[i]->runtime;
    if (runtime->presentations == NULL) continue;
    size_t entry_count = 0;
    const qa_app_simulation_presentation *items =
        runtime->presentations(runtime->ctx, &entry_count);
    if (count + entry_count > mods->scratch_presentations_capacity) {
      mods->scratch_presentations_capacity = count + entry_count;
      mods->scratch_presentations =
          xrealloc(mods->scratch_presentations,
                   mods->scratch_presentations_capacity *
                       sizeof *mods->scratch_presentations);
    }
    if (entry_count != 0)
      memcpy(&mods->scratch_presentations[count], items,
             entry_count * sizeof *items);
    count += entry_count;
  }
  *out_count = count;
  return mods->scratch_presentations;
}

const qa_session_mod_appearance_group *qa_session_mods_appearance_overrides(
    qa_session_mods *mods, size_t *out_count) {
  size_t group_count = 0;
  size_t item_count = 0;
  for (size_t i = 0; i < mods->active_count; i++) {
    qa_mod_runtime *runtime = &mods->active[i]->runtime;
    if (runtime->appearance_overrides == NULL) continue;
    size_t entry_count = 0;
    const qa_app_simulation_presentation *source =
        runtime->appearance_overrides(runtime->ctx, &entry_count);
    if (entry_count == 0) continue;
    for (size_t j = 0; j < entry_count; j++)
      if (source[j].view_weapon)
        qa_errorf("Mod body appearance cannot replace a view weapon");
    /* Group this mod's items per actor (first-appearance order,
     * items in source order), staging copies at the scratch tail. */
    if (item_count + entry_count > mods->scratch_group_items_capacity) {
      qa_app_simulation_presentation *old_base =
          mods->scratch_group_items;
      mods->scratch_group_items_capacity = item_count + entry_count;
      mods->scratch_group_items =
          xrealloc(mods->scratch_group_items,
                   mods->scratch_group_items_capacity *
                       sizeof *mods->scratch_group_items);
      if (mods->scratch_group_items != old_base && old_base != NULL)
        for (size_t k = 0; k < group_count; k++)
          if (mods->scratch_groups[k].presentations != NULL)
            mods->scratch_groups[k].presentations =
                mods->scratch_group_items +
                (mods->scratch_groups[k].presentations - old_base);
    }
    for (size_t j = 0; j < entry_count; j++) {
      bool seen = false;
      for (size_t k = 0; k < j; k++)
        if (qa_same_actor(source[k].actor, source[j].actor)) seen = true;
      if (seen) continue;
      size_t run_start = item_count;
      for (size_t k = j; k < entry_count; k++)
        if (qa_same_actor(source[k].actor, source[j].actor))
          mods->scratch_group_items[item_count++] = source[k];
      /* Later mods replace an earlier group for the same actor. */
      size_t group = group_count;
      for (size_t k = 0; k < group_count; k++)
        if (qa_same_actor(mods->scratch_groups[k].actor, source[j].actor))
          group = k;
      if (group == group_count) {
        if (group_count == mods->scratch_groups_capacity) {
          mods->scratch_groups_capacity =
              mods->scratch_groups_capacity != 0
                  ? mods->scratch_groups_capacity * 2
                  : 8;
          mods->scratch_groups = xrealloc(
              mods->scratch_groups, mods->scratch_groups_capacity *
                                        sizeof *mods->scratch_groups);
        }
        group_count++;
      }
      mods->scratch_groups[group].actor = source[j].actor;
      mods->scratch_groups[group].presentations =
          &mods->scratch_group_items[run_start];
      mods->scratch_groups[group].count = item_count - run_start;
    }
  }
  *out_count = group_count;
  return mods->scratch_groups;
}

void qa_session_mods_advance(qa_session_mods *mods,
                             qa_frame_context frame) {
  assert_open(mods);
  if (mods->busy)
    qa_errorf("Cannot advance mods during a lifecycle operation");
  mods->busy = true;
  qa_cleanup node; /* finally { busy = false } */
  qa_cleanup_push(&node, exclusive_end_cleanup, mods);
  for (size_t i = 0; i < mods->active_count; i++) {
    qa_mod_runtime *runtime = &mods->active[i]->runtime;
    if (runtime->advance != NULL) runtime->advance(runtime->ctx, frame);
    assert_open(mods);
  }
  qa_cleanup_pop(&node);
  mods->busy = false;
}

void qa_session_mods_checkpoint(qa_session_mods *mods,
                                qa_mod_checkpoint **out_mods,
                                size_t *out_count) {
  exclusive_begin(mods);
  qa_cleanup node; /* exclusive: finally { busy = false } */
  qa_cleanup_push(&node, exclusive_end_cleanup, mods);
  capture(mods, out_mods, out_count);
  qa_cleanup_pop(&node);
  exclusive_end(mods);
}

const qa_active_mod_client_presentation *
qa_session_mods_client_presentation_sources(qa_session_mods *mods,
                                            size_t *out_count) {
  assert_open(mods);
  if (mods->busy)
    qa_errorf(
        "Cannot read component client output during a lifecycle "
        "operation");
  size_t count = 0;
  for (size_t i = 0; i < mods->active_count; i++) {
    struct active_entry *entry = mods->active[i];
    if (!entry->entry->prepared->has_client_presentation) continue;
    qa_mod_client_presentation_source source;
    bool has_source =
        entry->runtime.client_presentation != NULL &&
        entry->runtime.client_presentation(entry->runtime.ctx, &source);
    if (!has_source || !entry->has_presentation_owner)
      qa_errorf("Declared client presentation has no source owner");
    if (count == mods->scratch_client_sources_capacity) {
      mods->scratch_client_sources_capacity =
          mods->scratch_client_sources_capacity != 0
              ? mods->scratch_client_sources_capacity * 2
              : 4;
      mods->scratch_client_sources =
          xrealloc(mods->scratch_client_sources,
                   mods->scratch_client_sources_capacity *
                       sizeof *mods->scratch_client_sources);
    }
    mods->scratch_client_sources[count].owner = entry->presentation_owner;
    mods->scratch_client_sources[count].identity = entry->entry->identity;
    mods->scratch_client_sources[count].source = source;
    count++;
  }
  *out_count = count;
  return mods->scratch_client_sources;
}

const qa_active_mod_presentation *qa_session_mods_presentation_sources(
    qa_session_mods *mods, size_t *out_count) {
  assert_open(mods);
  if (mods->busy)
    qa_errorf("Cannot read mod presentation during a lifecycle operation");
  size_t count = 0;
  for (size_t i = 0; i < mods->active_count; i++) {
    struct active_entry *entry = mods->active[i];
    if (!entry->entry->prepared->has_presentation) continue;
    qa_mod_qvm_presentation_source source;
    bool has_source =
        entry->runtime.qvm_presentation != NULL &&
        entry->runtime.qvm_presentation(entry->runtime.ctx, &source);
    if (!has_source)
      qa_errorf("Declared QVM presentation has no live source context");
    if (!entry->has_presentation_owner)
      qa_errorf("Component presentation has no active presentation owner");
    if (count == mods->scratch_sources_capacity) {
      mods->scratch_sources_capacity =
          mods->scratch_sources_capacity != 0
              ? mods->scratch_sources_capacity * 2
              : 4;
      mods->scratch_sources =
          xrealloc(mods->scratch_sources, mods->scratch_sources_capacity *
                                              sizeof *mods->scratch_sources);
    }
    mods->scratch_sources[count].owner = entry->presentation_owner;
    mods->scratch_sources[count].identity = entry->entry->identity;
    mods->scratch_sources[count].prepared =
        entry->entry->prepared->presentation;
    mods->scratch_sources[count].source = source;
    count++;
  }
  *out_count = count;
  return mods->scratch_sources;
}

void qa_session_mods_checkpoint_for_travel(qa_session_mods *mods,
                                           qa_mod_travel_entry **out_mods,
                                           size_t *out_count) {
  exclusive_begin(mods);
  qa_cleanup busy_node; /* exclusive: finally { busy = false } */
  qa_cleanup_push(&busy_node, exclusive_end_cleanup, mods);
  combat_assert_idle(mods);
  qa_mod_travel_entry *entries = NULL;
  /* Retained states live in the same allocation after the entries,
   * so one free() of the entries array releases both. */
  qa_mod_private_checkpoint *states = NULL;
  if (mods->active_count != 0) {
    entries = xmalloc(mods->active_count * sizeof *entries +
                      mods->active_count * sizeof *states);
    states = (qa_mod_private_checkpoint *)(entries + mods->active_count);
  }
  qa_cleanup entries_node;
  qa_cleanup_push(&entries_node, free_travel_entries, &entries);
  for (size_t i = 0; i < mods->active_count; i++) {
    struct active_entry *entry = mods->active[i];
    entries[i].identity = entry->entry->identity;
    entries[i].state = NULL;
    if (str_eq(entry->entry->prepared->travel, "retain")) {
      states[i] = entry->runtime.checkpoint(entry->runtime.ctx);
      assert_open(mods);
      combat_assert_idle(mods);
      validate_state(entry->entry, states[i]);
      entries[i].state = &states[i];
    }
  }
  qa_cleanup_pop(&entries_node);
  qa_cleanup_pop(&busy_node);
  exclusive_end(mods);
  *out_mods = entries;
  *out_count = mods->active_count;
}

void qa_session_mods_close(qa_session_mods *mods) {
  if (mods->closed) return;
  mods->closed = true;
  size_t count = mods->active_count + mods->pending_count;
  qa_resource_scope **scopes = NULL;
  if (count != 0) scopes = xmalloc(count * sizeof *scopes);
  size_t total = 0;
  for (size_t i = 0; i < mods->active_count; i++)
    scopes[total++] = mods->active[i]->resources;
  for (size_t i = 0; i < mods->pending_count; i++)
    scopes[total++] = mods->pending[i];
  /* The scopes own each runtime's close (its ctx is &active->runtime), so they close
   * before the active entries are freed. */
  size_t active_count = mods->active_count;
  mods->active_count = 0;
  mods->pending_count = 0;
  size_t errors = close_scopes_collect(scopes, total);
  free(scopes);
  for (size_t i = 0; i < active_count; i++) free(mods->active[i]);
  if (errors > 0) qa_raise("AggregateError", 0, 0, "Mod cleanup failed");
}

static void mods_close_ctx(void *ctx) {
  qa_session_mods_close((qa_session_mods *)ctx);
}
qa_session_resource qa_session_mods_as_resource(qa_session_mods *mods) {
  qa_session_resource resource = {mods, mods_close_ctx};
  return resource;
}
