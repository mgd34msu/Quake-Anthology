/* Port of src/app/bootstrap/network/unified-component-consumer.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_component_consumer.h"
#include "qa/compat/qvm/mod_presentation.h"
#include <stdlib.h>
#include <string.h>
#include "qa/world/session/mod_presentations.h"
#include <stdio.h>
#include "qa/contracts/mods.h"
#include "qa/foundation/support.h"
typedef struct entry {
  struct qa_active_mod_presentation active;
  const void *mounts; bool borrowed;
  const void *writable;
  bool has_client; qa_active_mod_client_presentation client;
  qa_app_unified_component_identity metadata;
  double game_state_revision;
  qa_q3_source_game_state_record game_state; /* deep-owned */
  qa_qvm_cp_scene_command *commands; size_t command_count, command_cap;
  double command_sequence;
  bool has_frame; qa_app_unified_component_frame frame;
  /* The gamestate the current frame was accepted under (TS frameGameState). It aliases
   * game_state until an update replaces game_state, then owns the older record. */
  qa_q3_source_game_state_record frame_game_state;
  /* Owned scene contexts; scene->baseline points at baseline and is detached before freeing. */
  qa_qvm_scene_context *baseline, *scene;
  qa_qvm_presentation_context context_storage;
  qa_qvm_presentation_binding *binding_storage; size_t binding_count;
  struct qa_app_unified_component_consumers *owner;
} entry;
struct qa_app_unified_component_consumers {
  qa_app_unified_component_consumer_options options;
  entry **entries; size_t count, capacity;
  double revision; bool closed;
  struct qa_active_mod_presentation *sources; size_t sources_cap;
  qa_active_mod_client_presentation *client_sources; size_t client_sources_cap;
};
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static bool actor_equal(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token
      && a.session.token == b.session.token;
}
static void current(qa_app_unified_component_consumers *c) {
  c->options.assert_current(c->options.ctx);
  if (c->closed) qa_raise("Error", 0, 0, "Remote component collection is retired");
}
static qa_q3_source_game_state_record record_copy(const qa_q3_source_game_state_record *source) {
  qa_q3_source_game_state_record out = *source;
  out.string_offsets = xmalloc(1024 * sizeof *out.string_offsets);
  memcpy(out.string_offsets, source->string_offsets, 1024 * sizeof *out.string_offsets);
  out.string_data = xmalloc(16000);
  memcpy(out.string_data, source->string_data, 16000);
  return out;
}
static void retire(qa_app_unified_component_consumers *c, entry *e, bool activation) {
  if (activation) c->options.retire_replicated_owner(c->options.ctx, e->active.owner);
  if (e->borrowed) c->options.close_mounts(c->options.ctx, e->mounts);
}
static entry *find_entry(qa_app_unified_component_consumers *c, const char *provider) {
  for (size_t i = 0; i < c->count; i++)
    if (strcmp(c->entries[i]->active.owner.provider, provider) == 0) return c->entries[i];
  return NULL;
}
static void source_assert(void *ctx) {
  entry *e = ctx;
  current(e->owner);
  if (find_entry(e->owner, e->active.owner.provider) != e)
    qa_raise("Error", 0, 0, "Remote component activation is retired");
}
static bool viewer_is(entry *e, qa_actor_id viewer) {
  qa_actor_id current_viewer;
  return e->owner->options.viewer(e->owner->options.ctx, &current_viewer)
      && actor_equal(current_viewer, viewer);
}
static const qa_qvm_presentation_context *source_context(void *ctx, qa_actor_id viewer) {
  entry *e = ctx;
  source_assert(ctx);
  if (!viewer_is(e, viewer) || !e->has_frame || !actor_equal(e->frame.viewer, viewer))
    return NULL;
  qa_qvm_presentation_context *context = &e->context_storage;
  memset(context, 0, sizeof *context);
  context->has_client_number = true;
  context->client_number = (int64_t)e->frame.client_number;
  context->game_state = e->frame_game_state;
  context->game_state_revision = (int64_t)e->frame.game_state_revision;
  context->has_weapon_presented = true;
  context->weapon_presented = e->frame.weapon_presented;
  context->snapshot_server_time = (int64_t)e->frame.snapshot.server_time;
  context->snapshot_player_state = e->frame.snapshot.player_state;
  memcpy(context->player_slots, e->frame.snapshot.player_slots,
      sizeof context->player_slots);
  context->snapshot_player_state.stats.values = context->player_slots;
  context->snapshot_player_state.stats.count = 16;
  context->snapshot_player_state.persistent.values = context->player_slots + 16;
  context->snapshot_player_state.persistent.count = 16;
  context->snapshot_player_state.powerups.values = context->player_slots + 32;
  context->snapshot_player_state.powerups.count = 16;
  context->snapshot_player_state.ammo.values = context->player_slots + 48;
  context->snapshot_player_state.ammo.count = 16;
  context->scene = e->scene;
  return context;
}
static bool source_files(void *ctx, qa_qvm_presentation_files *out) {
  entry *e = ctx;
  source_assert(ctx);
  out->mounts = (qa_content_mounted *)e->mounts;
  out->writable = (qa_user_file_store *)e->writable;
  return true;
}
static void source_command(void *ctx, qa_actor_id viewer,
    const char *const *arguments, size_t count) {
  entry *e = ctx;
  if (!source_context(ctx, viewer))
    qa_raise("Error", 0, 0, "Remote component command viewer is retired");
  e->owner->options.command(e->owner->options.ctx, e->metadata.owner,
      e->metadata.generation, arguments, count);
}
static const qa_qvm_presentation_binding *source_bindings(void *ctx, size_t *out_count) {
  entry *e = ctx;
  source_assert(ctx);
  size_t count = e->has_frame ? e->frame.binding_count : 0;
  if (e->binding_count != count) {
    free(e->binding_storage);
    e->binding_storage = xmalloc(count * sizeof *e->binding_storage);
    e->binding_count = count;
    for (size_t i = 0; i < count; i++) {
      e->binding_storage[i].actor = e->frame.bindings[i].actor;
      e->binding_storage[i].slot = (double)e->frame.bindings[i].slot;
      e->binding_storage[i].owned = e->frame.bindings[i].owned;
    }
  }
  *out_count = count;
  return e->binding_storage;
}
static bool source_actor(void *ctx, double slot, qa_actor_id *out) {
  entry *e = ctx;
  source_assert(ctx);
  if (!e->has_frame) return false;
  for (size_t i = 0; i < e->frame.binding_count; i++)
    if ((double)e->frame.bindings[i].slot == slot) {
      *out = e->frame.bindings[i].actor;
      return true;
    }
  return false;
}
static bool source_live(void *ctx, qa_actor_id actor) {
  entry *e = ctx;
  if (e->owner->closed || find_entry(e->owner, e->active.owner.provider) != e) return false;
  qa_actor_id wanted = e->has_frame ? e->frame.viewer : actor;
  if (!viewer_is(e, wanted) || !e->has_frame) return false;
  for (size_t i = 0; i < e->frame.binding_count; i++)
    if (actor_equal(e->frame.bindings[i].actor, actor)) return true;
  return false;
}
static bool client_frame(void *ctx, qa_actor_id actor, qa_mod_client_presentation_frame *out) {
  entry *e = ctx;
  if (!source_context(ctx, actor)) return false;
  memset(out, 0, sizeof *out);
  out->kind = QA_MOD_CLIENT_PRESENTATION_QVM;
  const qa_qvm_mod_presentation_declaration *declaration = &e->active.prepared.declaration;
  const qa_qvm_presentation_base *base = declaration->is_scene
      ? &declaration->as.scene->base : &declaration->as.player_events->base;
  out->qvm_hud_mode = base->hud ? base->hud->mode : NULL;
  return true;
}
static void client_assert(void *ctx) { source_assert(ctx); }
static bool frame_state_owned(const entry *e) {
  return e->frame_game_state.string_data != NULL && e->frame_game_state.string_data != e->game_state.string_data;
}
static void scene_free(qa_qvm_scene_context *scene) {
  if (!scene) return;
  scene->baseline = NULL;
  qa_qvm_scene_context_free(scene);
  free(scene);
}
static void frame_release(entry *e) {
  if (!e->has_frame) return;
  free(e->frame.bindings);
  if (e->frame.has_scene) qa_qvm_source_snapshot_free(&e->frame.scene.snapshot);
  e->has_frame = false;
}
static void entry_free(qa_app_unified_component_consumers *c, entry *e) {
  (void)c;
  if (frame_state_owned(e)) qa_q3_source_game_state_record_destroy(&e->frame_game_state);
  qa_q3_source_game_state_record_destroy(&e->game_state);
  for (size_t i = 0; i < e->command_count; i++) free((void *)e->commands[i].arguments);
  free(e->commands);
  frame_release(e);
  scene_free(e->scene);
  scene_free(e->baseline);
  free(e->binding_storage);
  free(e);
}
qa_app_unified_component_consumers *qa_app_unified_component_consumers_create(
    qa_app_unified_component_consumer_options options) {
  qa_app_unified_component_consumers *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->options = options;
  return c;
}
void qa_app_unified_component_consumers_destroy(qa_app_unified_component_consumers *consumers) {
  if (!consumers) return;
  for (size_t i = 0; i < consumers->count; i++) entry_free(consumers, consumers->entries[i]);
  free(consumers->entries);
  free(consumers->sources);
  free(consumers->client_sources);
  free(consumers);
}
/* One allocation per command: the argument pointers followed by their text, so the single
 * free qa_qvm_scene_context_free gives `arguments` releases everything. */
static const char *const *arguments_copy(const char *const *arguments, size_t count) {
  size_t size = count * sizeof(char *);
  for (size_t i = 0; i < count; i++) size += strlen(arguments[i]) + 1;
  char **out = xmalloc(size);
  char *text = (char *)(out + count);
  for (size_t i = 0; i < count; i++) {
    size_t n = strlen(arguments[i]) + 1;
    memcpy(text, arguments[i], n);
    out[i] = text;
    text += n;
  }
  return (const char *const *)out;
}
static void commands_append(entry *e, const qa_qvm_cp_scene_command *commands, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (e->command_count == e->command_cap) {
      size_t cap = e->command_cap ? e->command_cap * 2 : 16;
      qa_qvm_cp_scene_command *grown = realloc(e->commands, cap * sizeof *grown);
      if (!grown) qa_range_error("out of memory");
      e->commands = grown;
      e->command_cap = cap;
    }
    qa_qvm_cp_scene_command *copy = &e->commands[e->command_count];
    copy->sequence = commands[i].sequence;
    copy->argument_count = commands[i].argument_count;
    copy->arguments = arguments_copy(commands[i].arguments, commands[i].argument_count);
    e->command_count++;
  }
  /* TS [...commands, ...metadata.commands].slice(-64). */
  while (e->command_count > 64) {
    free((void *)e->commands[0].arguments);
    memmove(e->commands, e->commands + 1, (e->command_count - 1) * sizeof *e->commands);
    e->command_count--;
  }
}
typedef struct update_scope {
  qa_app_unified_component_consumers *c;
  entry **next, **created; size_t created_count;
} update_scope;
/* TS catch: close the borrowed mounts of entries this update created, then rethrow. */
static void update_scope_free(void *arg) {
  update_scope *scope = arg;
  for (size_t i = 0; i < scope->created_count; i++) {
    entry *e = scope->created[i];
    if (e->borrowed) scope->c->options.close_mounts(scope->c->options.ctx, e->mounts);
    entry_free(scope->c, e);
  }
  free(scope->next);
  free(scope->created);
}
static void cleanup_text(void *arg) { free(*(char **)arg); }
void qa_app_unified_component_consumers_update(qa_app_unified_component_consumers *c,
    const qa_app_unified_component_update *update) {
  current(c);
  if (update->revision != c->revision + 1)
    qa_raise("Error", 0, 0, "Remote component reliable revision is not consecutive");
  size_t count = update->source_count;
  update_scope scope = {c, calloc(count ? count : 1, sizeof(entry *)), calloc(count ? count : 1, sizeof(entry *)), 0};
  if (!scope.next || !scope.created) { free(scope.next); free(scope.created); qa_range_error("out of memory"); }
  qa_cleanup guard;
  qa_cleanup_push(&guard, update_scope_free, &scope);
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_component_state *metadata = &update->sources[i];
    entry *e = find_entry(c, metadata->identity.owner.provider);
    bool same = e && qa_same_presentation_owner(&e->active.owner, metadata->identity.owner)
        && e->metadata.generation == metadata->identity.generation;
    if (same) {
      if (!qa_same_mod_identity(&e->active.identity, &metadata->identity.identity)
          || strcmp(e->metadata.abi, metadata->identity.abi) != 0
          || strcmp(e->metadata.runtime, metadata->identity.runtime) != 0)
        qa_raise("Error", 0, 0, "Remote component activation changed its admitted identity");
    } else {
      const qa_prepared_mod *prepared = NULL;
      for (size_t j = 0; j < c->options.prepared_mod_count && !prepared; j++)
        if (qa_same_mod_identity(&c->options.prepared_mods[j].identity,
                &metadata->identity.identity)) prepared = &c->options.prepared_mods[j];
      bool qualified = prepared && prepared->has_presentation;
      if (qualified) {
        const qa_qvm_mod_presentation_declaration *declaration =
            &prepared->presentation.declaration;
        const char *runtime = declaration->is_scene
            ? declaration->as.scene->runtime : declaration->as.player_events->runtime;
        const qa_qvm_presentation_base *base = declaration->is_scene
            ? &declaration->as.scene->base : &declaration->as.player_events->base;
        qualified = strcmp(runtime, metadata->identity.runtime) == 0
            && strcmp(base->gameplay.abi_profile, metadata->identity.abi) == 0
            && strcmp(prepared->presentation.source.id, metadata->identity.owner.provider) == 0;
      }
      if (!qualified)
        qa_raise("Error", 0, 0, "Remote component differs from its locally qualified presentation");
      if (!metadata->has_game_state)
        qa_raise("Error", 0, 0, "Remote component activation has no original gamestate");
      const void *installed = c->options.installed_mounts(c->options.ctx,
          metadata->identity.identity.source.content);
      current(c);
      const void *writable = NULL;
      char *selection = qa_mod_selection_key(metadata->identity.identity.selection);
      if (!selection) qa_range_error("out of memory");
      qa_cleanup selection_guard;
      qa_cleanup_push(&selection_guard, cleanup_text, &selection);
      const void *mounts = c->options.borrow_mounts(c->options.ctx, selection,
          metadata->identity.identity.source.content, installed, &writable);
      qa_cleanup_pop(&selection_guard);
      free(selection);
      e = calloc(1, sizeof *e);
      if (!e) {
        if (mounts != installed) c->options.close_mounts(c->options.ctx, mounts);
        qa_range_error("out of memory");
      }
      e->owner = c;
      e->mounts = mounts;
      e->writable = writable;
      e->borrowed = mounts != installed;
      scope.created[scope.created_count++] = e;
      e->active.owner = metadata->identity.owner;
      e->active.identity = metadata->identity.identity;
      e->active.prepared = prepared->presentation;
      e->active.source.ctx = e;
      e->active.source.module = prepared->presentation.source;
      e->active.source.abi_profile = metadata->identity.abi;
      e->active.source.generation = metadata->identity.generation;
      e->active.source.context = source_context;
      e->active.source.files = source_files;
      e->active.source.client_command = source_command;
      e->active.source.bindings = source_bindings;
      e->active.source.actor = source_actor;
      e->active.source.live = source_live;
      e->active.source.assert_current = source_assert;
      e->metadata = metadata->identity;
      e->game_state = record_copy(&metadata->game_state);
      e->frame_game_state = e->game_state;
      e->game_state_revision = metadata->game_state_revision;
      e->command_sequence = metadata->command_base;
      const qa_qvm_mod_presentation_declaration *declaration =
          &prepared->presentation.declaration;
      const qa_qvm_presentation_base *base = declaration->is_scene
          ? &declaration->as.scene->base : &declaration->as.player_events->base;
      if (base->hud) {
        e->has_client = true;
        e->client.owner = metadata->identity.owner;
        e->client.identity = metadata->identity.identity;
        e->client.source.ctx = e;
        e->client.source.generation = metadata->identity.generation;
        e->client.source.frame = client_frame;
        e->client.source.assert_current = client_assert;
      }
    }
    bool gap = metadata->command_base != e->command_sequence;
    for (size_t j = 0; j < metadata->command_count && !gap; j++)
      if ((double)metadata->commands[j].sequence != metadata->command_base + (double)j + 1) gap = true;
    if (gap) qa_raise("Error", 0, 0, "Remote component reliable command history has a gap");
    if (metadata->game_state_revision < e->game_state_revision
        || (!metadata->has_game_state && metadata->game_state_revision != e->game_state_revision))
      qa_raise("Error", 0, 0, "Remote component configstring history has a gap");
    scope.next[i] = e;
  }
  current(c);
  qa_cleanup_pop(&guard);
  for (size_t i = 0; i < c->count; i++) {
    entry *old = c->entries[i];
    entry *replacement = NULL;
    for (size_t j = 0; j < count; j++)
      if (strcmp(scope.next[j]->active.owner.provider, old->active.owner.provider) == 0)
        replacement = scope.next[j];
    if (replacement != old) {
      bool activation = replacement
          && !qa_same_presentation_owner(&replacement->active.owner, old->active.owner);
      retire(c, old, activation);
      entry_free(c, old);
    }
  }
  free(c->entries);
  c->entries = scope.next;
  c->count = count;
  c->capacity = count;
  c->revision = update->revision;
  free(scope.created);
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_component_state *metadata = &update->sources[i];
    entry *e = find_entry(c, metadata->identity.owner.provider);
    if (!e) qa_raise("Error", 0, 0, "Remote component admission disappeared");
    c->options.admit_replicated_owner(c->options.ctx, metadata->identity.owner,
        metadata->identity.identity.source.content);
    e->metadata = metadata->identity;
    e->game_state_revision = metadata->game_state_revision;
    if (metadata->has_game_state) {
      /* An accepted frame keeps the record it was published under (TS frameGameState); a
       * record no frame refers to is released. */
      bool frame_holds_current = e->has_frame && !frame_state_owned(e);
      if (!frame_holds_current) qa_q3_source_game_state_record_destroy(&e->game_state);
      e->game_state = record_copy(&metadata->game_state);
      if (!e->has_frame) e->frame_game_state = e->game_state;
    }
    commands_append(e, metadata->commands, metadata->command_count);
    e->command_sequence = metadata->command_count
        ? (double)metadata->commands[metadata->command_count - 1].sequence
        : metadata->command_base;
  }
}
static qa_qvm_source_snapshot snapshot_copy(const qa_qvm_source_snapshot *source) {
  qa_qvm_source_snapshot out = *source;
  qa_qvm_entity_state_fields *entities = xmalloc(source->entity_count * sizeof *entities);
  if (source->entity_count) memcpy(entities, source->entities, source->entity_count * sizeof *entities);
  out.entities = entities;
  out.area_mask = NULL;
  if (source->area_mask != NULL) {
    uint8_t *mask = xmalloc(source->area_mask_length);
    memcpy(mask, source->area_mask, source->area_mask_length);
    out.area_mask = mask;
  }
  double *slots = xmalloc(64 * sizeof *slots);
  memset(slots, 0, 64 * sizeof *slots);
  const qa_q3_player_state *state = &source->player_state;
  if (state->stats.values) memcpy(slots, state->stats.values, 16 * sizeof *slots);
  if (state->persistent.values) memcpy(slots + 16, state->persistent.values, 16 * sizeof *slots);
  if (state->powerups.values) memcpy(slots + 32, state->powerups.values, 16 * sizeof *slots);
  if (state->ammo.values) memcpy(slots + 48, state->ammo.values, 16 * sizeof *slots);
  out.player_state.stats.values = slots;
  out.player_state.persistent.values = slots + 16;
  out.player_state.powerups.values = slots + 32;
  out.player_state.ammo.values = slots + 48;
  return out;
}
/* TS {...frame.scene, gameState, gameStateRevision, actors: bindings, commands}; owned. */
static qa_qvm_scene_context *scene_make(const entry *e, const qa_app_unified_component_frame *frame) {
  qa_qvm_scene_context *scene = calloc(1, sizeof *scene);
  if (!scene) qa_range_error("out of memory");
  scene->revision = (int64_t)frame->scene.revision;
  scene->game_state = record_copy(&e->game_state);
  scene->game_state_revision = (int64_t)frame->game_state_revision;
  scene->snapshot = snapshot_copy(&frame->scene.snapshot);
  scene->actors = xmalloc(frame->binding_count * sizeof *scene->actors);
  if (frame->binding_count) memcpy(scene->actors, frame->bindings, frame->binding_count * sizeof *scene->actors);
  scene->actor_count = frame->binding_count;
  scene->commands = xmalloc(e->command_count * sizeof *scene->commands);
  for (size_t i = 0; i < e->command_count; i++) {
    scene->commands[i].sequence = e->commands[i].sequence;
    scene->commands[i].argument_count = e->commands[i].argument_count;
    scene->commands[i].arguments = arguments_copy(e->commands[i].arguments, e->commands[i].argument_count);
  }
  scene->command_count = e->command_count;
  return scene;
}
bool qa_app_unified_component_consumers_accept(qa_app_unified_component_consumers *c,
    const qa_app_unified_component_frames *frames, qa_actor_id viewer) {
  current(c);
  if (frames->revision < c->revision) return false;
  if (frames->revision != c->revision || frames->source_count != c->count)
    qa_raise("Error", 0, 0, "Remote component frame lacks reliable admission");
  for (size_t i = 0; i < frames->source_count; i++) {
    const qa_app_unified_component_frame *frame = &frames->sources[i];
    entry *e = find_entry(c, frame->owner.provider);
    bool viewer_bound = false;
    if (e) for (size_t j = 0; j < frame->binding_count; j++)
      if (actor_equal(frame->bindings[j].actor, viewer)
          && (double)frame->bindings[j].slot == frame->client_number
          && !frame->bindings[j].owned) viewer_bound = true;
    if (!e || !qa_same_presentation_owner(&e->active.owner, frame->owner)
        || e->metadata.generation != frame->generation
        || strcmp(e->metadata.abi, frame->abi) != 0
        || e->game_state_revision != frame->game_state_revision
        || !actor_equal(frame->viewer, viewer) || !viewer_bound
        || (strcmp(e->metadata.runtime, "qvm-scene") == 0) != frame->has_scene)
      qa_raise("Error", 0, 0, "Remote component frame differs from its source or recipient admission");
    if (frame->has_scene
        && ((double)frame->scene.snapshot.server_command_sequence != e->command_sequence
            || (double)frame->scene.snapshot.server_time != frame->snapshot.server_time))
      qa_raise("Error", 0, 0, "Remote component snapshot lost its reliable source history");
  }
  for (size_t i = 0; i < frames->source_count; i++) {
    const qa_app_unified_component_frame *source = &frames->sources[i];
    entry *e = find_entry(c, source->owner.provider);
    if (!e) qa_raise("Error", 0, 0, "Remote component frame owner disappeared");
    /* The decoded frame is freed after this call: the entry keeps its own copy. */
    qa_app_unified_component_frame frame = *source;
    frame.owner = e->active.owner;
    frame.bindings = xmalloc(source->binding_count * sizeof *frame.bindings);
    if (source->binding_count) memcpy(frame.bindings, source->bindings, source->binding_count * sizeof *frame.bindings);
    if (source->has_scene) frame.scene.snapshot = snapshot_copy(&source->scene.snapshot);
    frame_release(e);
    e->frame = frame;
    e->has_frame = true;
    qa_q3_player_state *state = &e->frame.snapshot.player_state;
    state->stats.values = e->frame.snapshot.player_slots;
    state->persistent.values = e->frame.snapshot.player_slots + 16;
    state->powerups.values = e->frame.snapshot.player_slots + 32;
    state->ammo.values = e->frame.snapshot.player_slots + 48;
    if (frame_state_owned(e)) qa_q3_source_game_state_record_destroy(&e->frame_game_state);
    e->frame_game_state = e->game_state;
    if (frame.has_scene) {
      /* entry.baseline ??= scene; entry.scene = {...scene, baseline}. */
      if (!e->baseline) e->baseline = scene_make(e, &e->frame);
      qa_qvm_scene_context *scene = scene_make(e, &e->frame);
      scene->baseline = e->baseline;
      scene_free(e->scene);
      e->scene = scene;
    }
  }
  return true;
}
const struct qa_active_mod_presentation *qa_app_unified_component_consumers_sources(
    qa_app_unified_component_consumers *consumers, size_t *out_count) {
  current(consumers);
  if (consumers->sources_cap < consumers->count) {
    consumers->sources = realloc(consumers->sources,
        consumers->count * sizeof *consumers->sources);
    if (!consumers->sources) qa_range_error("out of memory");
    consumers->sources_cap = consumers->count;
  }
  size_t count = 0;
  for (size_t i = 0; i < consumers->count; i++)
    if (consumers->entries[i]->has_frame) consumers->sources[count++] = consumers->entries[i]->active;
  *out_count = count;
  return consumers->sources;
}
const qa_active_mod_client_presentation *qa_app_unified_component_consumers_client_sources(
    qa_app_unified_component_consumers *consumers, size_t *out_count) {
  current(consumers);
  if (consumers->client_sources_cap < consumers->count) {
    consumers->client_sources = realloc(consumers->client_sources,
        consumers->count * sizeof *consumers->client_sources);
    if (!consumers->client_sources) qa_range_error("out of memory");
    consumers->client_sources_cap = consumers->count;
  }
  size_t count = 0;
  for (size_t i = 0; i < consumers->count; i++)
    if (consumers->entries[i]->has_frame && consumers->entries[i]->has_client)
      consumers->client_sources[count++] = consumers->entries[i]->client;
  *out_count = count;
  return consumers->client_sources;
}
void qa_app_unified_component_consumers_close(qa_app_unified_component_consumers *consumers) {
  if (consumers->closed) return;
  consumers->closed = true;
  /* TS this.entries.clear(), then retire each entry (closing borrowed mounts). */
  size_t count = consumers->count;
  consumers->count = 0;
  for (size_t i = 0; i < count; i++) {
    retire(consumers, consumers->entries[i], false);
    entry_free(consumers, consumers->entries[i]);
  }
}
