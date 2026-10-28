/* Port of src/app/bootstrap/network/unified-native-consumer.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/unified_native_consumer.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct entry {
  qa_active_mod_client_presentation active;
  const char *edition; bool view;
  qa_app_unified_native_identity identity;
  bool has_hud; qa_app_unified_native_hud_state hud;
  bool has_configs; qa_native_q2_config_string *configs; size_t config_count;
  bool has_frame; qa_actor_id frame_viewer;
  qa_mod_client_presentation_frame frame;
  qa_session_native_q2_hud_frame native_frame; /* storage behind frame */
  struct qa_app_unified_native_consumers *owner;
} entry;
struct qa_app_unified_native_consumers {
  qa_app_unified_native_consumer_options options;
  entry **entries; size_t count;
  double revision; bool legacy, closed;
  qa_active_mod_client_presentation *sources; size_t sources_cap;
};
struct qa_app_unified_native_commit {
  qa_app_unified_native_consumers *consumers;
  int action; /* 0 noop-clear, 1 update, 2 frames, 3 legacy */
  entry **next; size_t next_count;
  double revision;
  struct { entry *entry; const qa_app_unified_native_state *metadata;
           bool has_configs; const qa_native_q2_config_string *configs; size_t config_count; }
      *pending_updates; size_t pending_update_count;
  struct { entry *entry; qa_actor_id viewer; qa_mod_client_presentation_frame value; }
      *pending_frames; size_t pending_frame_count;
  entry *legacy_entry;
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
static void current(qa_app_unified_native_consumers *c) {
  c->options.assert_current(c->options.ctx);
  if (c->closed) qa_range_error("Remote native component collection is retired");
}
static entry *find_entry(qa_app_unified_native_consumers *c, const char *provider) {
  for (size_t i = 0; i < c->count; i++)
    if (strcmp(c->entries[i]->active.owner.provider, provider) == 0) return c->entries[i];
  return NULL;
}
static void source_assert(void *ctx) {
  entry *e = ctx;
  current(e->owner);
  if (find_entry(e->owner, e->active.owner.provider) != e)
    qa_range_error("Remote native component is retired");
}
static bool source_frame(void *ctx, qa_actor_id actor, qa_mod_client_presentation_frame *out) {
  entry *e = ctx;
  source_assert(ctx);
  qa_actor_id viewer;
  bool has_viewer = e->owner->options.viewer(e->owner->options.ctx, &viewer);
  if (!has_viewer || !actor_equal(viewer, actor) || !e->has_frame
      || !actor_equal(e->frame_viewer, actor)) return false;
  *out = e->frame;
  return true;
}
static void entry_free(entry *e) {
  free(e->hud.configstrings);
  free(e->hud.inventory);
  free(e->configs);
  (void)0; /* native_view is by-value in the landed frame */
  free(e);
}
static entry *create_entry(qa_app_unified_native_consumers *c,
    const qa_app_unified_native_state *metadata, bool camera_only) {
  const qa_prepared_mod *prepared = NULL;
  for (size_t i = 0; i < c->options.prepared_mod_count; i++)
    if (qa_same_mod_identity(&c->options.prepared_mods[i].identity,
            &metadata->identity.identity)) prepared = &c->options.prepared_mods[i];
  char *instance = qa_mod_instance_provider(metadata->identity.identity.selection);
  bool qualified = prepared && prepared->has_client_presentation && instance
      && strcmp(instance, metadata->identity.owner.provider) == 0;
  free(instance);
  if (!qualified)
    qa_range_error("Remote native presentation differs from its locally qualified component");
  const char *admission_hud = prepared->client_presentation.hud;
  const char *mode = strcmp(admission_hud, "none") == 0 ? NULL
      : strcmp(admission_hud, "replace") == 0 ? "replace-status" : "layout-overlay";
  const char *metadata_mode = metadata->has_hud ? metadata->hud.mode : NULL;
  if (!camera_only && ((mode == NULL) != (metadata_mode == NULL)
          || (mode && strcmp(mode, metadata_mode) != 0)))
    qa_range_error("Remote native HUD differs from its admitted mode");
  const char *family = NULL, *edition = NULL;
  c->options.product_expectation(c->options.ctx,
      metadata->identity.identity.source.content, &family, &edition);
  if (strcmp(family, "q2") != 0
      || (strcmp(edition, "classic") != 0 && strcmp(edition, "rerelease") != 0))
    qa_range_error("Remote native presentation has no Q2 source edition");
  entry *e = calloc(1, sizeof *e);
  if (!e) qa_range_error("out of memory");
  e->owner = c;
  e->edition = edition;
  e->view = prepared->client_presentation.view;
  e->identity = metadata->identity;
  e->has_hud = metadata->has_hud;
  e->hud = metadata->hud;
  e->active.owner = metadata->identity.owner;
  e->active.identity = metadata->identity.identity;
  e->active.source.ctx = e;
  e->active.source.generation = metadata->identity.generation;
  e->active.source.frame = source_frame;
  e->active.source.assert_current = source_assert;
  return e;
}
qa_app_unified_native_consumers *qa_app_unified_native_consumers_create(
    qa_app_unified_native_consumer_options options) {
  qa_app_unified_native_consumers *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->options = options;
  return c;
}
void qa_app_unified_native_consumers_destroy(qa_app_unified_native_consumers *consumers) {
  if (!consumers) return;
  for (size_t i = 0; i < consumers->count; i++) entry_free(consumers->entries[i]);
  free(consumers->entries);
  free(consumers->sources);
  free(consumers);
}
qa_app_unified_native_commit *qa_app_unified_native_consumers_prepare_update(
    qa_app_unified_native_consumers *c, const qa_app_unified_component_update *update) {
  current(c);
  if (update->revision != c->revision + 1)
    qa_range_error("Remote native reliable revision is not consecutive");
  qa_app_unified_native_commit *commit = calloc(1, sizeof *commit);
  if (!commit) qa_range_error("out of memory");
  commit->consumers = c;
  commit->action = 1;
  commit->revision = update->revision;
  size_t count = update->native_count;
  commit->next = xmalloc(count * sizeof *commit->next);
  commit->next_count = count;
  commit->pending_updates = xmalloc(count * sizeof *commit->pending_updates);
  commit->pending_update_count = count;
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_native_state *metadata = &update->native[i];
    entry *previous = find_entry(c, metadata->identity.owner.provider);
    bool same = previous
        && qa_same_presentation_owner(&previous->active.owner, metadata->identity.owner)
        && previous->active.source.generation == metadata->identity.generation;
    entry *e = same ? previous : create_entry(c, metadata, false);
    const char *old_mode = e->has_hud ? e->hud.mode : NULL;
    const char *new_mode = metadata->has_hud ? metadata->hud.mode : NULL;
    if (!qa_same_mod_identity(&e->active.identity, &metadata->identity.identity)
        || ((old_mode == NULL) != (new_mode == NULL)
            || (old_mode && strcmp(old_mode, new_mode) != 0)))
      qa_range_error("Remote native activation changed its admitted identity or HUD");
    bool has_configs = false;
    const qa_native_q2_config_string *configs = NULL;
    size_t config_count = 0;
    if (metadata->has_hud) {
      if (metadata->hud.has_configstrings) {
        has_configs = true;
        configs = metadata->hud.configstrings;
        config_count = metadata->hud.configstring_count;
      } else {
        has_configs = e->has_configs;
        configs = e->configs;
        config_count = e->config_count;
      }
    }
    if (metadata->has_hud && (!has_configs
            || strcmp(metadata->hud.protocol.kind,
                strcmp(e->edition, "classic") == 0 ? "q2-classic" : "q2-rerelease") != 0))
      qa_range_error("Remote native HUD has no source configstrings or differs from its source edition");
    commit->next[i] = e;
    commit->pending_updates[i].entry = e;
    commit->pending_updates[i].metadata = metadata;
    commit->pending_updates[i].has_configs = has_configs;
    commit->pending_updates[i].configs = configs;
    commit->pending_updates[i].config_count = config_count;
  }
  return commit;
}
static void read_frame(entry *e, const qa_app_unified_native_frame *frame,
    qa_actor_id viewer, qa_mod_client_presentation_frame *out) {
  if (!qa_same_presentation_owner(&e->active.owner, frame->owner)
      || e->active.source.generation != frame->generation
      || !actor_equal(frame->viewer, viewer))
    qa_range_error("Remote native frame differs from its source or recipient admission");
  if ((frame->has_view) != e->view
      || (frame->has_view && strcmp(frame->view.edition, e->edition) != 0))
    qa_range_error("Remote native camera differs from its admitted source");
  memset(out, 0, sizeof *out);
  out->kind = QA_MOD_CLIENT_PRESENTATION_NATIVE;
  out->has_native_view = frame->has_view;
  if (frame->has_view) out->native_view = frame->view;
  if ((!e->has_hud) != (!frame->has_hud))
    qa_range_error("Remote native frame differs from its admitted HUD");
  if (!e->has_hud) { out->has_native_hud = false; return; }
  if (!e->has_configs
      || frame->hud.stat_count != (strcmp(e->edition, "classic") == 0 ? 32u : 64u))
    qa_range_error("Remote native HUD differs from its source ABI");
  out->has_native_hud = true;
  out->native_hud_mode = e->hud.mode;
  qa_session_native_q2_hud_frame *native = &e->native_frame;
  memset(native, 0, sizeof *native);
  native->protocol = e->hud.protocol;
  native->configstrings = (const void *)e->configs;
  native->stats.values = frame->hud.stats;
  native->stats.count = frame->hud.stat_count;
  native->server_frame = frame->hud.server_frame;
  native->time_milliseconds = frame->hud.time_milliseconds;
  native->has_frame_time_milliseconds = frame->hud.has_frame_time;
  native->frame_time_milliseconds = frame->hud.frame_time_milliseconds;
  native->layout = e->hud.layout;
  native->inventory.values = e->hud.inventory;
  native->inventory.count = e->hud.inventory_count;
  native->player_number = e->hud.player_number;
  out->native_hud_frame = *native;
}
qa_app_unified_native_commit *qa_app_unified_native_consumers_prepare(
    qa_app_unified_native_consumers *c, const qa_app_unified_component_frames *frames,
    qa_actor_id viewer, const qa_app_unified_native_camera *legacy) {
  current(c);
  if (frames->revision < c->revision) return NULL;
  if (frames->revision != c->revision)
    qa_range_error("Remote native frame lacks reliable admission");
  if (legacy && !frames->has_native) {
    qa_app_unified_native_state metadata;
    memset(&metadata, 0, sizeof metadata);
    metadata.identity.owner = *(const qa_presentation_owner *)legacy->owner;
    metadata.identity.identity = *(const qa_mod_identity *)legacy->identity;
    metadata.identity.generation = legacy->generation;
    entry *previous = find_entry(c, metadata.identity.owner.provider);
    entry *e = previous
        && qa_same_presentation_owner(&previous->active.owner, metadata.identity.owner)
        && previous->active.source.generation == metadata.identity.generation
        ? previous : create_entry(c, &metadata, true);
    if (!qa_same_mod_identity(&e->active.identity, &metadata.identity.identity))
      qa_range_error("Remote native activation changed identity");
    qa_app_unified_native_frame frame;
    memset(&frame, 0, sizeof frame);
    frame.owner = metadata.identity.owner;
    frame.generation = legacy->generation;
    frame.viewer = viewer;
    frame.has_view = true;
    frame.view = *(const qa_native_mod_camera_view *)legacy->view;
    qa_mod_client_presentation_frame value;
    memset(&value, 0, sizeof value);
    read_frame(e, &frame, viewer, &value);
    qa_app_unified_native_commit *commit = calloc(1, sizeof *commit);
    if (!commit) qa_range_error("out of memory");
    commit->consumers = c;
    commit->action = 3;
    commit->legacy_entry = e;
    commit->pending_frames = xmalloc(sizeof *commit->pending_frames);
    commit->pending_frame_count = 1;
    commit->pending_frames[0].entry = e;
    commit->pending_frames[0].viewer = viewer;
    commit->pending_frames[0].value = value;
    return commit;
  }
  qa_app_unified_native_commit *commit = calloc(1, sizeof *commit);
  if (!commit) qa_range_error("out of memory");
  commit->consumers = c;
  if (c->legacy && !frames->has_native) {
    commit->action = 0;
    return commit;
  }
  size_t count = frames->has_native ? frames->native_count : 0;
  if (count != c->count)
    qa_range_error("Remote native frame differs from admitted owners");
  commit->action = 2;
  commit->pending_frames = xmalloc(count * sizeof *commit->pending_frames);
  commit->pending_frame_count = count;
  size_t views = 0, replaces = 0;
  for (size_t i = 0; i < count; i++) {
    const qa_app_unified_native_frame *frame = &frames->native[i];
    entry *e = find_entry(c, frame->owner.provider);
    if (!e) qa_range_error("Remote native frame has an unadmitted owner");
    qa_mod_client_presentation_frame value;
    memset(&value, 0, sizeof value);
    read_frame(e, frame, viewer, &value);
    if (value.has_native_view) views++;
    if (value.has_native_hud && value.native_hud_mode
        && strcmp(value.native_hud_mode, "replace-status") == 0) replaces++;
    commit->pending_frames[i].entry = e;
    commit->pending_frames[i].viewer = frame->viewer;
    commit->pending_frames[i].value = value;
  }
  if (views > 1 || replaces > 1)
    qa_range_error("Remote native presentation has conflicting exclusive owners");
  return commit;
}
void qa_app_unified_native_commit_run(qa_app_unified_native_commit *commit) {
  qa_app_unified_native_consumers *c = commit->consumers;
  if (commit->action == 0) {
    for (size_t i = 0; i < c->count; i++) entry_free(c->entries[i]);
    c->count = 0;
    c->legacy = false;
    return;
  }
  if (commit->action == 3) {
    entry *e = commit->legacy_entry;
    entry **next = xmalloc(sizeof *next);
    next[0] = e;
    for (size_t i = 0; i < c->count; i++)
      if (c->entries[i] != e) entry_free(c->entries[i]);
    free(c->entries);
    c->entries = next;
    c->count = 1;
    c->legacy = true;
    e->has_frame = true;
    e->frame_viewer = commit->pending_frames[0].viewer;
    (void)0; /* native_view is by-value in the landed frame */
    e->frame = commit->pending_frames[0].value;
    return;
  }
  if (commit->action == 1) {
    current(c);
    for (size_t i = 0; i < c->count; i++) {
      entry *old = c->entries[i];
      entry *replacement = NULL;
      for (size_t j = 0; j < commit->next_count; j++)
        if (strcmp(commit->next[j]->active.owner.provider, old->active.owner.provider) == 0)
          replacement = commit->next[j];
      if (replacement != old
          && (!replacement
              || !qa_same_presentation_owner(&replacement->active.owner, old->active.owner))) {
        c->options.retire_replicated_owner(c->options.ctx, old->active.owner);
      }
      if (replacement != old) entry_free(old);
    }
    free(c->entries);
    c->entries = commit->next;
    commit->next = NULL;
    c->count = commit->next_count;
    c->revision = commit->revision;
    c->legacy = false;
    for (size_t i = 0; i < commit->pending_update_count; i++) {
      entry *e = commit->pending_updates[i].entry;
      const qa_app_unified_native_state *metadata = commit->pending_updates[i].metadata;
      e->identity = metadata->identity;
      free(e->hud.configstrings);
      free(e->hud.inventory);
      e->has_hud = metadata->has_hud;
      e->hud = metadata->hud;
      e->hud.configstrings = NULL;
      e->hud.inventory = NULL;
      if (metadata->has_hud) {
        e->hud.configstrings = xmalloc(
            metadata->hud.configstring_count * sizeof *e->hud.configstrings);
        if (metadata->hud.configstring_count)
          memcpy(e->hud.configstrings, metadata->hud.configstrings,
              metadata->hud.configstring_count * sizeof *e->hud.configstrings);
        e->hud.inventory = xmalloc(metadata->hud.inventory_count * sizeof *e->hud.inventory);
        if (metadata->hud.inventory_count)
          memcpy(e->hud.inventory, metadata->hud.inventory,
              metadata->hud.inventory_count * sizeof *e->hud.inventory);
      }
      free(e->configs);
      e->has_configs = commit->pending_updates[i].has_configs;
      e->config_count = commit->pending_updates[i].config_count;
      e->configs = xmalloc(e->config_count * sizeof *e->configs);
      if (e->config_count)
        memcpy(e->configs, commit->pending_updates[i].configs,
            e->config_count * sizeof *e->configs);
      c->options.admit_replicated_owner(c->options.ctx, metadata->identity.owner,
          metadata->identity.identity.source.content);
    }
    return;
  }
  for (size_t i = 0; i < commit->pending_frame_count; i++) {
    entry *e = commit->pending_frames[i].entry;
    e->has_frame = true;
    e->frame_viewer = commit->pending_frames[i].viewer;
    (void)0; /* native_view is by-value in the landed frame */
    e->frame = commit->pending_frames[i].value;
  }
}
void qa_app_unified_native_commit_free(qa_app_unified_native_commit *commit) {
  if (!commit) return;
  free(commit->next);
  free(commit->pending_updates);

  free(commit->pending_frames);
  free(commit);
}
void qa_app_unified_native_consumers_retire(qa_app_unified_native_consumers *consumers,
    qa_presentation_owner owner) {
  entry *e = find_entry(consumers, owner.provider);
  if (e && qa_same_presentation_owner(&e->active.owner, owner)) {
    entry **next = xmalloc(consumers->count ? consumers->count * sizeof *next : sizeof *next);
    size_t count = 0;
    for (size_t i = 0; i < consumers->count; i++)
      if (consumers->entries[i] != e) next[count++] = consumers->entries[i];
    free(consumers->entries);
    consumers->entries = next;
    consumers->count = count;
    entry_free(e);
  }
}
const qa_active_mod_client_presentation *qa_app_unified_native_consumers_sources(
    qa_app_unified_native_consumers *consumers, size_t *out_count) {
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
void qa_app_unified_native_consumers_close(qa_app_unified_native_consumers *consumers) {
  consumers->closed = true;
  for (size_t i = 0; i < consumers->count; i++) entry_free(consumers->entries[i]);
  consumers->count = 0;
}
