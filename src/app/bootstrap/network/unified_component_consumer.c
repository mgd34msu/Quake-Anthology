/* Port of src/app/bootstrap/network/unified-component-consumer.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_component_consumer.h"
#include "qa/compat/qvm/mod_presentation.h"
#include <stdlib.h>
#include <string.h>
#include "qa/world/session/mod_presentations.h"
#include <stdio.h>
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
  qa_q3_source_game_state_record frame_game_state; /* shallow alias of game_state */
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
  if (c->closed) qa_range_error("Remote component collection is retired");
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
    qa_range_error("Remote component activation is retired");
}
static bool viewer_is(entry *e, qa_actor_id viewer) {
  qa_actor_id current_viewer;
  return e->owner->options.viewer(e->owner->options.ctx, &current_viewer)
      && actor_equal(current_viewer, viewer);
}
static qa_session_qvm_presentation_context source_context(void *ctx, qa_actor_id viewer) {
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
    qa_range_error("Remote component command viewer is retired");
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
static void entry_free(qa_app_unified_component_consumers *c, entry *e) {
  (void)c;
  qa_q3_source_game_state_record_destroy(&e->game_state);
  for (size_t i = 0; i < e->command_count; i++) {
    for (size_t j = 0; j < e->commands[i].argument_count; j++)
      free((void *)e->commands[i].arguments[j]);
    free((void *)e->commands[i].arguments);
  }
  free(e->commands);
  if (e->has_frame) {
    free(e->frame.bindings);
    free(e->frame.scene.snapshot.entities);
  }
  if (e->scene) {
    /* scene aliases baseline via scene->baseline; free both once. */
    qa_qvm_scene_context_free(e->scene);
    free(e->scene);
  } else if (e->baseline) {
    qa_qvm_scene_context_free(e->baseline);
    free(e->baseline);
  }
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
static void commands_append(entry *e, const qa_qvm_cp_scene_command *commands, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (e->command_count == e->command_cap) {
      e->command_cap = e->command_cap ? e->command_cap * 2 : 16;
      e->commands = realloc(e->commands, e->command_cap * sizeof *e->commands);
      if (!e->commands) qa_range_error("out of memory");
    }
    qa_qvm_cp_scene_command *copy = &e->commands[e->command_count++];
    copy->sequence = commands[i].sequence;
    copy->argument_count = commands[i].argument_count;
    const char **args = xmalloc(copy->argument_count * sizeof *args);
    for (size_t j = 0; j < copy->argument_count; j++) {
      size_t n = strlen(commands[i].arguments[j]) + 1;
      char *text = xmalloc(n);
      memcpy(text, commands[i].arguments[j], n);
      args[j] = text;
    }
    copy->arguments = (const char *const *)args;
  }
  while (e->command_count > 64) {
    for (size_t j = 0; j < e->commands[0].argument_count; j++)
      free((void *)e->commands[0].arguments[j]);
    free((void *)e->commands[0].arguments);
    memmove(e->commands, e->commands + 1, (e->command_count - 1) * sizeof *e->commands);
    e->command_count--;
  }
}
void qa_app_unified_component_consumers_update(qa_app_unified_component_consumers *c,
    const qa_app_unified_component_update *update) {
  current(c);
  if (update->revision != c->revision + 1)
    qa_range_error("Remote component reliable revision is not consecutive");
  size_t count = update->source_count;
  entry **next = xmalloc(count * sizeof *next);
  entry **created = xmalloc(count * sizeof *created);
  size_t created_count = 0;
  /* TS wraps admission in try/catch closing borrowed mounts of
   * created entries; C errors abort, so creation is direct. */
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_component_state *metadata = &update->sources[i];
    entry *e = find_entry(c, metadata->identity.owner.provider);
    bool same = e && qa_same_presentation_owner(&e->active.owner, metadata->identity.owner)
        && e->metadata.generation == metadata->identity.generation;
    if (same) {
      if (!qa_same_mod_identity(&e->active.identity, &metadata->identity.identity)
          || strcmp(e->metadata.abi, metadata->identity.abi) != 0
          || strcmp(e->metadata.runtime, metadata->identity.runtime) != 0)
        qa_range_error("Remote component activation changed its admitted identity");
    } else {
      const qa_prepared_mod *prepared = NULL;
      for (size_t j = 0; j < c->options.prepared_mod_count; j++)
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
        qa_range_error("Remote component differs from its locally qualified presentation");
      if (!metadata->has_game_state)
        qa_range_error("Remote component activation has no original gamestate");
      const void *installed = c->options.installed_mounts(c->options.ctx,
          metadata->identity.identity.source.content);
      current(c);
      const void *writable = NULL;
      char selection_text[256];
      snprintf(selection_text, sizeof selection_text, "%s:%s",
          metadata->identity.identity.selection.product,
          metadata->identity.identity.selection.id);
      const void *mounts = c->options.borrow_mounts(c->options.ctx, selection_text,
          metadata->identity.identity.source.content, installed, &writable);
      e = calloc(1, sizeof *e);
      if (!e) qa_range_error("out of memory");
      e->owner = c;
      e->mounts = mounts;
      e->writable = writable;
      e->borrowed = mounts != installed;
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
      created[created_count++] = e;
    }
    if (metadata->command_base != e->command_sequence)
      qa_range_error("Remote component reliable command history has a gap");
    for (size_t j = 0; j < metadata->command_count; j++)
      if ((double)metadata->commands[j].sequence != metadata->command_base + (double)j + 1)
        qa_range_error("Remote component reliable command history has a gap");
    if (metadata->game_state_revision < e->game_state_revision
        || (!metadata->has_game_state
            && metadata->game_state_revision != e->game_state_revision))
      qa_range_error("Remote component configstring history has a gap");
    next[i] = e;
  }
  current(c);
  for (size_t i = 0; i < c->count; i++) {
    entry *old = c->entries[i];
    entry *replacement = NULL;
    for (size_t j = 0; j < count; j++)
      if (strcmp(next[j]->active.owner.provider, old->active.owner.provider) == 0)
        replacement = next[j];
    if (replacement != old) {
      bool activation = replacement
          && !qa_same_presentation_owner(&replacement->active.owner, old->active.owner);
      retire(c, old, activation);
      entry_free(c, old);
    }
  }
  free(c->entries);
  c->entries = next;
  c->count = count;
  c->capacity = count;
  c->revision = update->revision;
  free(created);
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_component_state *metadata = &update->sources[i];
    entry *e = find_entry(c, metadata->identity.owner.provider);
    if (!e) qa_range_error("Remote component admission disappeared");
    c->options.admit_replicated_owner(c->options.ctx, metadata->identity.owner,
        metadata->identity.identity.source.content);
    e->metadata = metadata->identity;
    e->game_state_revision = metadata->game_state_revision;
    if (metadata->has_game_state) {
      qa_q3_source_game_state_record_destroy(&e->game_state);
      e->game_state = record_copy(&metadata->game_state);
    }
    commands_append(e, metadata->commands, metadata->command_count);
    e->command_sequence = metadata->command_count
        ? (double)metadata->commands[metadata->command_count - 1].sequence
        : metadata->command_base;
  }
}
bool qa_app_unified_component_consumers_accept(qa_app_unified_component_consumers *c,
    const qa_app_unified_component_frames *frames, qa_actor_id viewer) {
  current(c);
  if (frames->revision < c->revision) return false;
  if (frames->revision != c->revision || frames->source_count != c->count)
    qa_range_error("Remote component frame lacks reliable admission");
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
      qa_range_error("Remote component frame differs from its source or recipient admission");
    if (frame->has_scene
        && ((double)frame->scene.snapshot.server_command_sequence != e->command_sequence
            || (double)frame->scene.snapshot.server_time != frame->snapshot.server_time))
      qa_range_error("Remote component snapshot lost its reliable source history");
  }
  for (size_t i = 0; i < frames->source_count; i++) {
    const qa_app_unified_component_frame *source = &frames->sources[i];
    entry *e = find_entry(c, source->owner.provider);
    if (!e) qa_range_error("Remote component frame owner disappeared");
    qa_app_unified_component_frame frame = *source;
    frame.bindings = xmalloc(source->binding_count * sizeof *frame.bindings);
    if (source->binding_count)
      memcpy(frame.bindings, source->bindings, source->binding_count * sizeof *frame.bindings);
    if (source->has_scene) {
      frame.scene.snapshot.entities = xmalloc(
          source->scene.snapshot.entity_count * sizeof *frame.scene.snapshot.entities);
      if (source->scene.snapshot.entity_count)
        memcpy(frame.scene.snapshot.entities, source->scene.snapshot.entities,
            source->scene.snapshot.entity_count * sizeof *frame.scene.snapshot.entities);
    }
    if (e->has_frame) {
      free(e->frame.bindings);
      free(e->frame.scene.snapshot.entities);
    }
    e->frame = frame;
    e->has_frame = true;
    e->frame_game_state = e->game_state;
    if (frame.has_scene) {
      qa_qvm_scene_context *scene = calloc(1, sizeof *scene);
      if (!scene) qa_range_error("out of memory");
      scene->revision = (int64_t)frame.scene.revision;
      scene->game_state = e->game_state;
      scene->game_state_revision = (int64_t)frame.game_state_revision;
      scene->snapshot = frame.scene.snapshot;
      /* Scene context owns its arrays (qa_qvm_scene_context_free). */
      scene->actors = xmalloc(frame.binding_count * sizeof *scene->actors);
      if (frame.binding_count)
        memcpy(scene->actors, frame.bindings, frame.binding_count * sizeof *scene->actors);
      scene->actor_count = frame.binding_count;
      scene->commands = xmalloc(e->command_count * sizeof *scene->commands);
      for (size_t ci = 0; ci < e->command_count; ci++) {
        scene->commands[ci].sequence = e->commands[ci].sequence;
        scene->commands[ci].argument_count = e->commands[ci].argument_count;
        const char **args = xmalloc(e->commands[ci].argument_count * sizeof *args);
        for (size_t cj = 0; cj < e->commands[ci].argument_count; cj++) {
          size_t n = strlen(e->commands[ci].arguments[cj]) + 1;
          char *text = xmalloc(n);
          memcpy(text, e->commands[ci].arguments[cj], n);
          args[cj] = text;
        }
        scene->commands[ci].arguments = (const char *const *)args;
      }
      scene->command_count = e->command_count;
      if (!e->baseline) e->baseline = scene;
      scene->baseline = e->baseline;
      if (e->scene && e->scene != e->baseline) {
        qa_qvm_scene_context_free(e->scene);
        free(e->scene);
      }
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
  /* TS collects retirement failures into an AggregateError; the C
   * retire callbacks abort on failure. */
  for (size_t i = 0; i < consumers->count; i++) retire(consumers, consumers->entries[i], false);
  consumers->count = 0;
}
