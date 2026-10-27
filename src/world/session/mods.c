/* Port of src/world/session/mods.ts. GPL-2.0-or-later. */
#include "qa/world/session/mods.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/selection.h"
#include "qa/world/session/mod_presentations.h"

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

static void close_scopes(qa_resource_scope **scopes, size_t count) {
  for (size_t i = count; i-- > 0;) qa_resource_scope_destroy(scopes[i]);
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
  bool closed;
  bool busy;
  int64_t next_order;
  /* Scratch returns. */
  qa_mod_selection *scratch_enabled;
  size_t scratch_enabled_capacity;
  qa_session_simulation_presentation *scratch_presentations;
  size_t scratch_presentations_capacity;
  qa_session_mod_appearance_group *scratch_groups;
  size_t scratch_groups_capacity;
  qa_session_simulation_presentation *scratch_group_items;
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

static void capture(qa_session_mods *mods, qa_mod_checkpoint **out_mods,
                    size_t *out_count) {
  combat_assert_idle(mods);
  qa_mod_checkpoint *captured = NULL;
  if (mods->active_count != 0)
    captured = xmalloc(mods->active_count * sizeof *captured);
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
  *out_mods = captured;
  *out_count = mods->active_count;
}

struct init_ctx {
  qa_session_mods *mods;
  qa_resource_scope *resources;
};
static void init_assert_current(void *ctx) {
  struct init_ctx *init = ctx;
  assert_open(init->mods);
  qa_resource_scope_assert_open(init->resources);
}
static void init_next_frame(void *ctx) {
  struct init_ctx *init = ctx;
  init_assert_current(init);
  init->mods->options.next_frame(init->mods->options.ctx);
  init_assert_current(init);
}

static void apply(qa_session_mods *mods, const qa_mod_selection *selections,
                  size_t selection_count,
                  const qa_mod_session_checkpoint *saved,
                  const qa_mod_travel_checkpoint *travel) {
  struct prepared_entry **entries = NULL;
  if (selection_count != 0)
    entries = xmalloc(selection_count * sizeof *entries);
  for (size_t i = 0; i < selection_count; i++) {
    char *key = qa_mod_selection_key(selections[i]);
    if (key == NULL) abort();
    entries[i] = find_prepared(mods, key);
    if (entries[i] == NULL)
      qa_errorf("Selected mod is not prepared: %s", key);
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
  struct active_entry **next = NULL;
  size_t next_count = 0;
  struct active_entry **opened = NULL;
  size_t opened_count = 0;
  qa_resource_scope **scopes = NULL;
  size_t scope_count = 0;
  if (selection_count != 0) {
    next = xmalloc(selection_count * sizeof *next);
    opened = xmalloc(selection_count * sizeof *opened);
    scopes = xmalloc(selection_count * sizeof *scopes);
  }
  for (size_t i = 0; i < selection_count; i++) {
    struct prepared_entry *entry = entries[i];
    struct active_entry *previous = NULL;
    for (size_t j = 0; j < mods->active_count; j++)
      if (str_eq(mods->active[j]->entry->key, entry->key))
        previous = mods->active[j];
    if (previous != NULL) {
      next[next_count++] = previous;
      continue;
    }
    char *name = xmalloc(strlen("mod ") + strlen(entry->key) + 1);
    strcpy(name, "mod ");
    strcat(name, entry->key);
    qa_resource_scope *resources = qa_resource_scope_create(name);
    free(name);
    if (resources == NULL) abort();
    scopes[scope_count++] = resources;
    if (mods->pending_count == mods->pending_capacity) {
      mods->pending_capacity =
          mods->pending_capacity != 0 ? mods->pending_capacity * 2 : 8;
      mods->pending = xrealloc(
          mods->pending, mods->pending_capacity * sizeof *mods->pending);
    }
    mods->pending[mods->pending_count++] = resources;
    const qa_mod_private_checkpoint *state = NULL;
    if (saved != NULL) state = &saved->mods[i].state;
    else if (travel != NULL) state = travel->mods[i].state;
    /* Working copies: services/engine with bound owner events. */
    qa_mod_host_services services_copy;
    qa_session_mod_engine_services engine_copy;
    const qa_mod_host_services *services = mods->options.services;
    bool has_owner = false;
    qa_presentation_owner owner = {NULL, 0};
    char *instance = qa_mod_instance_provider(entry->identity.selection);
    if (instance == NULL) abort();
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
      engine_copy = *services->engine;
      engine_copy.events = bound.events;
      services_copy = *services;
      services_copy.engine = &engine_copy;
      services = &services_copy;
    }
    struct init_ctx init = {mods, resources};
    qa_mod_initialization context;
    memset(&context, 0, sizeof context);
    context.restoring = state != NULL;
    context.services = services;
    context.instance = instance;
    context.resources = resources;
    context.ctx = &init;
    context.next_frame = init_next_frame;
    context.assert_current = init_assert_current;
    qa_mod_runtime runtime =
        entry->prepared->initialize(entry->prepared->ctx, &context);
    struct active_entry *active = xmalloc(sizeof *active);
    memset(active, 0, sizeof *active);
    active->entry = entry;
    active->resources = resources;
    active->runtime = runtime;
    active->instance = instance;
    active->has_presentation_owner = has_owner;
    active->presentation_owner = owner;
    if (qa_resource_scope_is_closed(resources)) {
      runtime.close(runtime.ctx);
      qa_errorf("Mod session closed during initialization");
    }
    qa_session_resource wrapped = {&active->runtime, NULL};
    wrapped.close = (void (*)(void *))runtime_close_ctx;
    wrapped.ctx = &active->runtime;
    qa_resource_scope_own(resources, wrapped);
    init_assert_current(&init);
    opened[opened_count++] = active;
    next[next_count++] = active;
    if (state != NULL) {
      runtime.restore(runtime.ctx, *state);
      init_assert_current(&init);
    }
    if (runtime.activate != NULL) runtime.activate(runtime.ctx);
    init_assert_current(&init);
  }
  for (size_t i = 0; i < opened_count; i++) {
    qa_mod_registrations registrations;
    memset(&registrations, 0, sizeof registrations);
    registrations.instance = opened[i]->instance;
    registrations.operations = mods->options.operations;
    registrations.order = mods->next_order++;
    registrations.resources = opened[i]->resources;
    opened[i]->runtime.register_(opened[i]->runtime.ctx, &registrations);
    assert_open(mods);
  }
  for (size_t i = 0; i < scope_count; i++)
    for (size_t j = 0; j < mods->pending_count; j++)
      if (mods->pending[j] == scopes[i]) {
        memmove(&mods->pending[j], &mods->pending[j + 1],
                (mods->pending_count - j - 1) * sizeof *mods->pending);
        mods->pending_count--;
        break;
      }
  /* Removed actives close after the new set is published. */
  qa_resource_scope **removed = NULL;
  size_t removed_count = 0;
  if (mods->active_count != 0)
    removed = xmalloc(mods->active_count * sizeof *removed);
  struct active_entry **removed_entries = NULL;
  if (mods->active_count != 0)
    removed_entries = xmalloc(mods->active_count * sizeof *removed_entries);
  for (size_t i = 0; i < mods->active_count; i++) {
    bool kept = false;
    for (size_t j = 0; j < next_count; j++)
      if (next[j] == mods->active[i]) kept = true;
    if (!kept) {
      removed[removed_count] = mods->active[i]->resources;
      removed_entries[removed_count] = mods->active[i];
      removed_count++;
    }
  }
  free(mods->active);
  mods->active = next;
  mods->active_count = next_count;
  mods->active_capacity = selection_count;
  close_scopes(removed, removed_count);
  for (size_t i = 0; i < removed_count; i++) {
    free(removed_entries[i]->instance);
    free(removed_entries[i]);
  }
  free(removed);
  free(removed_entries);
  free(opened);
  free(scopes);
  free(entries);
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
  exclusive_begin(mods);
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
  apply(mods, enabled_copy, enabled_count, saved, travel);
  free(enabled_copy);
  exclusive_end(mods);
  if (options->presentation != NULL &&
      options->presentation->finish_owner_restore != NULL)
    options->presentation->finish_owner_restore(
        options->presentation->ctx);
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

const qa_session_simulation_presentation *qa_session_mods_presentations(
    qa_session_mods *mods, size_t *out_count) {
  size_t count = 0;
  for (size_t i = 0; i < mods->active_count; i++) {
    qa_mod_runtime *runtime = &mods->active[i]->runtime;
    if (runtime->presentations == NULL) continue;
    size_t entry_count = 0;
    const qa_session_simulation_presentation *items =
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
    const qa_session_simulation_presentation *source =
        runtime->appearance_overrides(runtime->ctx, &entry_count);
    if (entry_count == 0) continue;
    for (size_t j = 0; j < entry_count; j++)
      if (source[j].view_weapon)
        qa_errorf("Mod body appearance cannot replace a view weapon");
    /* Group this mod's items per actor (first-appearance order,
     * items in source order), staging copies at the scratch tail. */
    if (item_count + entry_count > mods->scratch_group_items_capacity) {
      qa_session_simulation_presentation *old_base =
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
  for (size_t i = 0; i < mods->active_count; i++) {
    qa_mod_runtime *runtime = &mods->active[i]->runtime;
    if (runtime->advance != NULL) runtime->advance(runtime->ctx, frame);
    assert_open(mods);
  }
  mods->busy = false;
}

void qa_session_mods_set_enabled(qa_session_mods *mods,
                                 qa_mod_selection selection, bool enabled) {
  exclusive_begin(mods);
  size_t current_count = 0;
  const qa_mod_selection *current =
      qa_session_mods_enabled(mods, &current_count);
  qa_mod_selection *current_copy = NULL;
  if (current_count != 0) {
    current_copy = xmalloc(current_count * sizeof *current_copy);
    memcpy(current_copy, current, current_count * sizeof *current_copy);
  }
  qa_mod_selection_set set;
  selection_set_for(mods, current_copy, current_count, &set);
  free(current_copy);
  qa_mod_selection_set_set_enabled(&set, selection, enabled);
  size_t next_count = 0;
  const qa_mod_selection *next =
      qa_mod_selection_set_enabled(&set, &next_count);
  qa_mod_selection *next_copy = NULL;
  if (next_count != 0) {
    next_copy = xmalloc(next_count * sizeof *next_copy);
    memcpy(next_copy, next, next_count * sizeof *next_copy);
  }
  qa_mod_selection_set_destroy(&set);
  apply(mods, next_copy, next_count, NULL, NULL);
  free(next_copy);
  exclusive_end(mods);
}

void qa_session_mods_checkpoint(qa_session_mods *mods,
                                qa_mod_checkpoint **out_mods,
                                size_t *out_count) {
  exclusive_begin(mods);
  capture(mods, out_mods, out_count);
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
  exclusive_end(mods);
  *out_mods = entries;
  *out_count = mods->active_count;
}

void qa_session_mods_restore(qa_session_mods *mods,
                             const qa_mod_session_checkpoint *saved) {
  exclusive_begin(mods);
  struct prepared_entry **entries = NULL;
  if (mods->active_count != 0)
    entries = xmalloc(mods->active_count * sizeof *entries);
  for (size_t i = 0; i < mods->active_count; i++)
    entries[i] = mods->active[i]->entry;
  validate_saved(entries, mods->active_count, saved);
  free(entries);
  /* The TS captures a rollback checkpoint first; C restore failures
   * abort, so the capture only validates current state. */
  qa_mod_checkpoint *previous = NULL;
  size_t previous_count = 0;
  capture(mods, &previous, &previous_count);
  free(previous);
  for (size_t i = 0; i < mods->active_count; i++) {
    struct active_entry *entry = mods->active[i];
    if (mods->options.services != NULL &&
        mods->options.services->engine != NULL &&
        mods->options.presentation != NULL &&
        mods->options.presentation->refresh_owner != NULL)
      mods->options.presentation->refresh_owner(
          mods->options.presentation->ctx, entry->instance);
    entry->runtime.restore(entry->runtime.ctx, saved->mods[i].state);
    assert_open(mods);
    if (entry->runtime.activate != NULL)
      entry->runtime.activate(entry->runtime.ctx);
    assert_open(mods);
  }
  exclusive_end(mods);
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
  for (size_t i = 0; i < mods->active_count; i++) {
    free(mods->active[i]->instance);
    free(mods->active[i]);
  }
  mods->active_count = 0;
  mods->pending_count = 0;
  close_scopes(scopes, total);
  free(scopes);
}

static void mods_close_ctx(void *ctx) {
  qa_session_mods_close((qa_session_mods *)ctx);
}
qa_session_resource qa_session_mods_as_resource(qa_session_mods *mods) {
  qa_session_resource resource = {mods, mods_close_ctx};
  return resource;
}
