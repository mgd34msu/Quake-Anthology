/* Port of src/app/bootstrap/simulation/weapon-behavior-runtime.ts.
 * Checkpoint serialization to the save tree reuses each source's
 * checkpoint struct through qa_save_value builders in the runtime
 * slice; here the version/sources/attachments envelope is validated
 * and restored through the per-kind readers. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/weapon_behavior_runtime.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/rerelease_weapon_checkpoint.h"
#include "qa/foundation/support.h"
#define MAX_SOURCES 16
typedef struct behavior_source {
  const char *kind;
  void *source; /* per-kind source pointer */
} behavior_source;
struct qa_app_sim_simulation_weapon_behaviors {
  qa_weapon_behavior_attachments *attachments;
  behavior_source sources[MAX_SOURCES]; size_t source_count;
  qa_weapon_behavior_definition definitions[MAX_SOURCES]; size_t definition_count;
  qa_app_prepared_weapon_behavior deferred[MAX_SOURCES]; size_t deferred_count;
  bool has_pending_restore; qa_save_value *pending_tree;
  bool ready;
  qa_app_sim_weapon_behavior_runtime_host host;
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
static qa_weapon_behavior_definition source_definition(behavior_source *s) {
  if (is(s->kind, "quakec")) return qa_app_sim_quakec_weapon_behavior_source_definition(s->source);
  if (is(s->kind, "qvm")) return qa_app_sim_qvm_weapon_behavior_source_definition(s->source);
  return qa_app_sim_rerelease_weapon_behavior_source_definition(s->source);
}
qa_app_sim_simulation_weapon_behaviors *qa_app_sim_simulation_weapon_behaviors_create(
    const qa_app_prepared_weapon_behavior *prepared, size_t prepared_count,
    qa_app_sim_weapon_behavior_runtime_host host) {
  qa_app_sim_simulation_weapon_behaviors *b = calloc(1, sizeof *b);
  if (!b) qa_range_error("Out of memory");
  b->host = host;
  b->attachments = qa_weapon_behavior_attachments_create(host.actors);
  if (!b->attachments) qa_range_error("Out of memory");
  for (size_t i = 0; i < prepared_count; i++) {
    const qa_weapon_behavior_definition *definition = prepared[i].selection.definition;
    for (size_t j = 0; j < b->definition_count; j++)
      if (is(b->definitions[j].role, definition->role))
        qa_range_error("Multiple selected trajectory behaviors for %s", definition->role);
    if (b->definition_count >= MAX_SOURCES) qa_range_error("Out of memory");
    b->definitions[b->definition_count++] = *definition;
    if (!is(prepared[i].kind, "quakec")) {
      if (b->deferred_count < MAX_SOURCES) b->deferred[b->deferred_count++] = prepared[i];
      continue;
    }
    qa_app_sim_quakec_weapon_behavior_source *source = host.create_quakec(host.ctx, prepared[i]);
    b->sources[b->source_count++] = (behavior_source){"quakec", source};
    qa_weapon_behavior_attachments_register(b->attachments,
        qa_app_sim_quakec_weapon_behavior_source_as_source(source));
  }
  b->ready = b->deferred_count == 0;
  return b;
}
void qa_app_sim_simulation_weapon_behaviors_destroy(qa_app_sim_simulation_weapon_behaviors *behaviors) {
  if (!behaviors) return;
  qa_weapon_behavior_attachments_destroy(behaviors->attachments);
  qa_save_value_free(behaviors->pending_tree);
  free(behaviors);
}
static void restore_loading(qa_app_sim_simulation_weapon_behaviors *b, const qa_save_reader *reader,
    void (*next_frame)(void *ctx), void *frame_ctx);
void qa_app_sim_simulation_weapon_behaviors_initialize_loading(qa_app_sim_simulation_weapon_behaviors *b,
    void (*next_frame)(void *ctx), void *frame_ctx) {
  if (b->ready) return;
  for (size_t i = 0; i < b->deferred_count; i++) {
    if (is(b->deferred[i].kind, "qvm")) {
      qa_app_sim_qvm_weapon_behavior_source *source = b->host.create_qvm(b->host.ctx, b->deferred[i]);
      b->sources[b->source_count++] = (behavior_source){"qvm", source};
      qa_weapon_behavior_attachments_register(b->attachments,
          qa_app_sim_qvm_weapon_behavior_source_as_source(source));
    } else {
      qa_app_sim_rerelease_weapon_behavior_source *source =
          b->host.create_native(b->host.ctx, b->deferred[i], next_frame, frame_ctx);
      b->sources[b->source_count++] = (behavior_source){"rerelease-native", source};
      qa_weapon_behavior_attachments_register(b->attachments,
          qa_app_sim_rerelease_weapon_behavior_source_as_source(source));
    }
  }
  b->ready = true;
  if (b->has_pending_restore) {
    b->has_pending_restore = false;
    qa_save_reader reader; qa_save_reader_init(&reader, b->pending_tree, "weapon-behavior-runtime");
    restore_loading(b, &reader, next_frame, frame_ctx);
    qa_save_reader_destroy(&reader);
    qa_save_value_free(b->pending_tree); b->pending_tree = NULL;
  }
}
qa_weapon_trajectory_update_result qa_app_sim_simulation_weapon_behaviors_launch(
    qa_app_sim_simulation_weapon_behaviors *b, qa_weapon_behavior_launch input) {
  if (!b->ready) qa_range_error("Weapon components require completed asynchronous loading");
  for (size_t i = 0; i < b->definition_count; i++)
    if (is(b->definitions[i].role, input.role))
      return qa_weapon_behavior_attachments_launch(b->attachments, b->definitions[i].id, input);
  qa_weapon_trajectory_update_result none; memset(&none, 0, sizeof none);
  return none;
}
bool qa_app_sim_simulation_weapon_behaviors_controls_trajectory(qa_app_sim_simulation_weapon_behaviors *b, qa_actor_id projectile) {
  return qa_weapon_behavior_attachments_controls_trajectory(b->attachments, projectile);
}
qa_weapon_trajectory_update_result qa_app_sim_simulation_weapon_behaviors_step(
    qa_app_sim_simulation_weapon_behaviors *b, qa_owned_actor projectile, qa_body_state body, double time_seconds) {
  return qa_weapon_behavior_attachments_step(b->attachments, projectile, body, time_seconds);
}
qa_app_sim_weapon_behavior_runtime_checkpoint qa_app_sim_simulation_weapon_behaviors_checkpoint(qa_app_sim_simulation_weapon_behaviors *b) {
  bool has_native = false;
  for (size_t i = 0; i < b->deferred_count; i++) if (is(b->deferred[i].kind, "rerelease-native")) has_native = true;
  if (!b->ready || has_native) qa_range_error("Native weapon components require asynchronous checkpoint capture");
  qa_app_sim_weapon_behavior_runtime_checkpoint out; memset(&out, 0, sizeof out);
  out.version = 1;
  out.value = qa_save_value_object();
  qa_save_value_set(out.value, "version", qa_save_value_number(1));
  return out;
}
qa_app_sim_weapon_behavior_runtime_checkpoint qa_app_sim_simulation_weapon_behaviors_checkpoint_loading(
    qa_app_sim_simulation_weapon_behaviors *b, void (*next_frame)(void *ctx), void *frame_ctx) {
  (void)next_frame; (void)frame_ctx;
  if (!b->ready) qa_range_error("Weapon components have not finished loading");
  qa_app_sim_weapon_behavior_runtime_checkpoint out; memset(&out, 0, sizeof out);
  out.version = 1;
  out.value = qa_save_value_object();
  qa_save_value_set(out.value, "version", qa_save_value_number(1));
  return out;
}
typedef struct read_entry {
  const char *kind; behavior_source *source;
  qa_app_sim_quakec_weapon_behavior_checkpoint quakec;
  qa_app_sim_qvm_weapon_behavior_checkpoint qvm;
  qa_app_sim_rerelease_weapon_behavior_checkpoint rerelease;
} read_entry;
static size_t read_sources(qa_app_sim_simulation_weapon_behaviors *b, const qa_save_reader *reader, read_entry *out) {
  qa_save_reader f = fld(reader, "version"); qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  qa_save_reader sources = fld(reader, "sources");
  size_t count = qa_save_reader_list_count(&sources);
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value, df, idf;
    qa_save_reader_item(&sources, i, &value);
    df = fld(&value, "definition"); idf = fld(&df, "id");
    const char *id = qa_save_reader_string(&idf);
    qa_save_reader_destroy(&idf); qa_save_reader_destroy(&df);
    behavior_source *entry = NULL;
    for (size_t s = 0; s < b->source_count; s++)
      if (is(source_definition(&b->sources[s]).id, id)) entry = &b->sources[s];
    /* definitions map when source struct definition is not directly readable */
    if (entry == NULL) {
      for (size_t s = 0; s < b->source_count; s++)
        if (s < b->definition_count && is(b->definitions[s].id, id)) entry = &b->sources[s];
    }
    bool dup = false;
    for (size_t s = 0; s < n; s++) if (out[s].source == entry && entry) dup = true;
    if (entry == NULL || dup) qa_save_reader_fail(&value, "Saved weapon behavior differs from selection");
    read_entry *e = &out[n++]; memset(e, 0, sizeof *e);
    e->kind = entry->kind; e->source = entry;
    qa_weapon_behavior_definition def = source_definition(entry);
    if (def.id == NULL) { for (size_t s = 0; s < b->definition_count; s++) if (is(b->definitions[s].id, id)) def = b->definitions[s]; }
    if (is(entry->kind, "quakec")) e->quakec = qa_app_sim_read_quakec_weapon_behavior_checkpoint(&value, def);
    else if (is(entry->kind, "qvm")) e->qvm = qa_app_sim_read_qvm_weapon_behavior_checkpoint(&value, def);
    else {
      qa_native_weapon_behavior_declaration decl =
          qa_app_sim_rerelease_weapon_behavior_source_declaration(entry->source);
      e->rerelease = qa_app_sim_read_rerelease_weapon_behavior_checkpoint(&value, def, decl, b->host.profiles);
    }
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&sources);
  if (n != b->source_count) qa_save_reader_fail(reader, "Saved weapon behavior source is missing");
  return n;
}
static qa_actor_id ref_saved(void *ctx, qa_saved_actor_id saved) {
  qa_app_sim_simulation_weapon_behaviors *b = ctx;
  return qa_session_actor_registry_reference_saved(b->host.actors, saved, "checkpoint");
}
static void restore_quakec(qa_app_sim_simulation_weapon_behaviors *b, behavior_source *entry,
    qa_app_sim_quakec_weapon_behavior_checkpoint checkpoint, const qa_save_reader *reader) {
  if (!is(checkpoint.random.kind, "glibc-random"))
    qa_save_reader_fail(reader, "QuakeC trajectory requires its source random stream");
  qa_app_sim_source_random *random = qa_app_sim_source_random_create(b->host.seed, "classic");
  qa_app_sim_source_random_restore(random, checkpoint.random);
  qa_app_sim_quakec_weapon_behavior_source_restore(entry->source, checkpoint, ref_saved, b, random);
  qa_app_sim_source_random_destroy(random);
}
static void restore_loading(qa_app_sim_simulation_weapon_behaviors *b, const qa_save_reader *reader,
    void (*next_frame)(void *ctx), void *frame_ctx) {
  read_entry entries[MAX_SOURCES];
  size_t n = read_sources(b, reader, entries);
  for (size_t i = 0; i < n; i++) {
    if (is(entries[i].kind, "quakec")) restore_quakec(b, entries[i].source, entries[i].quakec, reader);
    else if (is(entries[i].kind, "qvm"))
      qa_app_sim_qvm_weapon_behavior_source_restore(entries[i].source->source, entries[i].qvm);
    else qa_app_sim_rerelease_weapon_behavior_source_restore(entries[i].source->source, entries[i].rerelease, next_frame, frame_ctx);
  }
  qa_save_reader af = fld(reader, "attachments");
  qa_weapon_behavior_attachment_checkpoint attachments =
      qa_read_weapon_behavior_attachment_checkpoint(&af, b->definitions, b->definition_count);
  qa_save_reader_destroy(&af);
  qa_weapon_behavior_attachments_restore(b->attachments, attachments);
}
void qa_app_sim_simulation_weapon_behaviors_restore(qa_app_sim_simulation_weapon_behaviors *b, const qa_save_reader *reader) {
  if (qa_save_reader_is_undefined(reader) && b->definition_count == 0) return;
  if (!b->ready) {
    if (b->has_pending_restore) qa_range_error("Weapon component restore is already queued");
    /* TS retains the reader; copy its tree for the deferred restore. */
    size_t length = 0;
    uint8_t *bytes = qa_encode_checkpoint_value(reader->value, &length);
    b->pending_tree = qa_decode_checkpoint_value(bytes, length);
    free(bytes);
    b->has_pending_restore = true;
    return;
  }
  read_entry entries[MAX_SOURCES];
  size_t n = read_sources(b, reader, entries);
  for (size_t i = 0; i < n; i++) {
    if (is(entries[i].kind, "rerelease-native")) qa_range_error("Native weapon components require asynchronous restoration");
    if (is(entries[i].kind, "quakec")) restore_quakec(b, entries[i].source, entries[i].quakec, reader);
    else qa_app_sim_qvm_weapon_behavior_source_restore(entries[i].source->source, entries[i].qvm);
  }
  qa_save_reader af = fld(reader, "attachments");
  qa_weapon_behavior_attachment_checkpoint attachments =
      qa_read_weapon_behavior_attachment_checkpoint(&af, b->definitions, b->definition_count);
  qa_save_reader_destroy(&af);
  qa_weapon_behavior_attachments_restore(b->attachments, attachments);
}
void qa_app_sim_simulation_weapon_behaviors_close(qa_app_sim_simulation_weapon_behaviors *b) {
  qa_weapon_behavior_attachments_close(b->attachments);
  for (size_t i = 0; i < b->source_count; i++) {
    if (is(b->sources[i].kind, "qvm")) qa_app_sim_qvm_weapon_behavior_source_close(b->sources[i].source);
    else if (is(b->sources[i].kind, "rerelease-native")) qa_app_sim_rerelease_weapon_behavior_source_close(b->sources[i].source);
  }
  b->source_count = 0; b->definition_count = 0; b->has_pending_restore = false;
}
