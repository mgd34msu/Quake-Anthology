/* Port of src/world/gameplay/weapon-behaviors.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/weapon_behaviors.h"

#include "qa/foundation/support.h"
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/execution.h"
#include "qa/persistence/save_image.h"

static noreturn void qa_error(const char *message) {
  qa_raise("Error", 0, 0, "%s", message);
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static void *xcalloc(size_t size) {
  void *value = calloc(1, size != 0 ? size : 1);
  if (value == NULL) abort();
  return value;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}

typedef struct attachment_entry {
  qa_owned_actor owner;
  qa_weapon_behavior_instance instance;
} attachment_entry;

struct qa_weapon_behavior_attachments {
  qa_session_actor_registry *registry;
  qa_weapon_behavior_source *sources;
  size_t source_count, source_capacity;
  attachment_entry *entries;
  size_t entry_count, entry_capacity;
  int64_t release_subscription;
};

static void detach_entry(qa_weapon_behavior_attachments *attachments, size_t index) {
  qa_weapon_behavior_instance instance = attachments->entries[index].instance;
  memmove(&attachments->entries[index], &attachments->entries[index + 1],
          (attachments->entry_count - index - 1) * sizeof *attachments->entries);
  attachments->entry_count--;
  instance.close(instance.ctx);
}
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_weapon_behavior_attachments_detach(ctx, actor.id);
}

qa_weapon_behavior_attachments *qa_weapon_behavior_attachments_create(
    qa_session_actor_registry *registry) {
  qa_weapon_behavior_attachments *attachments = calloc(1, sizeof *attachments);
  if (attachments == NULL) return NULL;
  attachments->registry = registry;
  attachments->release_subscription =
      qa_session_actor_registry_on_release(registry, on_release, attachments);
  return attachments;
}
void qa_weapon_behavior_attachments_destroy(qa_weapon_behavior_attachments *attachments) {
  if (attachments == NULL) return;
  qa_session_actor_registry_off_release(attachments->registry,
                                        attachments->release_subscription);
  free(attachments->sources);
  free(attachments->entries);
  free(attachments);
}

static qa_weapon_behavior_source *find_source(qa_weapon_behavior_attachments *attachments,
                                              const char *id) {
  for (size_t i = 0; i < attachments->source_count; i++) {
    if (str_eq(attachments->sources[i].definition.id, id)) return &attachments->sources[i];
  }
  return NULL;
}
static attachment_entry *find_entry(qa_weapon_behavior_attachments *attachments,
                                    qa_actor_id actor) {
  for (size_t i = 0; i < attachments->entry_count; i++) {
    if (qa_same_actor(attachments->entries[i].owner.id, actor))
      return &attachments->entries[i];
  }
  return NULL;
}

void qa_weapon_behavior_attachments_register(qa_weapon_behavior_attachments *attachments,
                                             qa_weapon_behavior_source source) {
  if (find_source(attachments, source.definition.id) != NULL) {
    char message[2048];
    (void)snprintf(message, sizeof message, "Weapon behavior already registered: %s",
                   source.definition.id);
    qa_error(message);
  }
  if (attachments->source_count == attachments->source_capacity) {
    attachments->source_capacity =
        attachments->source_capacity != 0 ? attachments->source_capacity * 2 : 8;
    attachments->sources = xrealloc(attachments->sources,
                                    attachments->source_capacity * sizeof *attachments->sources);
  }
  attachments->sources[attachments->source_count++] = source;
}

bool qa_weapon_behavior_attachments_attach(qa_weapon_behavior_attachments *attachments,
                                           const char *selection,
                                           qa_weapon_behavior_launch launch) {
  qa_session_actor_registry_assert_owned(attachments->registry, launch.projectile);
  if (!qa_session_actor_registry_is_live(attachments->registry, launch.shooter))
    qa_error("Weapon behavior shooter is retired");
  qa_weapon_behavior_source *source = find_source(attachments, selection);
  if (source == NULL) {
    char message[2048];
    (void)snprintf(message, sizeof message, "Weapon behavior is unavailable: %s", selection);
    qa_error(message);
  }
  if (!str_eq(source->definition.role, launch.role))
    qa_error("Weapon behavior does not match the fired projectile role");
  if (find_entry(attachments, launch.projectile.id) != NULL)
    qa_error("Projectile already has a selected trajectory behavior");
  qa_weapon_behavior_instance_result instance =
      source->attach(source->ctx, launch);
  if (!instance.has_value) return false;
  if (attachments->entry_count == attachments->entry_capacity) {
    attachments->entry_capacity =
        attachments->entry_capacity != 0 ? attachments->entry_capacity * 2 : 8;
    attachments->entries = xrealloc(attachments->entries,
                                    attachments->entry_capacity * sizeof *attachments->entries);
  }
  attachments->entries[attachments->entry_count].owner = launch.projectile;
  attachments->entries[attachments->entry_count].instance = instance.value;
  attachments->entry_count++;
  return true;
}

qa_weapon_trajectory_update_result qa_weapon_behavior_attachments_launch(
    qa_weapon_behavior_attachments *attachments, const char *selection,
    qa_weapon_behavior_launch input) {
  qa_weapon_trajectory_update_result result;
  result.has_value = false;
  memset(&result.value, 0, sizeof result.value);
  if (!qa_weapon_behavior_attachments_attach(attachments, selection, input)) return result;
  attachment_entry *entry = find_entry(attachments, input.projectile.id);
  if (entry == NULL) qa_error("Attached projectile has no behavior instance");
  result.has_value = true;
  result.value = entry->instance.initial;
  return result;
}

bool qa_weapon_behavior_attachments_controls_trajectory(qa_weapon_behavior_attachments *attachments,
                                                        qa_actor_id projectile) {
  return find_entry(attachments, projectile) != NULL;
}

qa_weapon_trajectory_update_result qa_weapon_behavior_attachments_step(
    qa_weapon_behavior_attachments *attachments, qa_owned_actor projectile, qa_body_state body,
    double time_seconds) {
  qa_weapon_trajectory_update_result result;
  result.has_value = false;
  memset(&result.value, 0, sizeof result.value);
  attachment_entry *entry = find_entry(attachments, projectile.id);
  if (entry == NULL) return result;
  qa_session_actor_registry_assert_owned(attachments->registry, projectile);
  if (!qa_same_owned_actor(entry->owner, projectile))
    qa_error("Weapon behavior actor ownership changed");
  return entry->instance.step(entry->instance.ctx, body, time_seconds);
}

void qa_weapon_behavior_attachments_detach(qa_weapon_behavior_attachments *attachments,
                                           qa_actor_id actor) {
  for (size_t i = 0; i < attachments->entry_count; i++) {
    if (qa_same_actor(attachments->entries[i].owner.id, actor)) {
      detach_entry(attachments, i);
      return;
    }
  }
}

qa_weapon_behavior_attachment_checkpoint qa_weapon_behavior_attachments_checkpoint(
    qa_weapon_behavior_attachments *attachments) {
  qa_weapon_behavior_attachment *rows =
      xcalloc((attachments->entry_count != 0 ? attachments->entry_count : 1) * sizeof *rows);
  for (size_t i = 0; i < attachments->entry_count; i++) {
    rows[i].projectile.slot = (double)attachments->entries[i].owner.id.slot;
    rows[i].projectile.generation = (double)attachments->entries[i].owner.id.generation;
    rows[i].owner = attachments->entries[i].owner.owner;
    rows[i].definition = attachments->entries[i].instance.definition;
  }
  qa_weapon_behavior_attachment_checkpoint checkpoint;
  checkpoint.version = 1;
  checkpoint.attachments = rows;
  checkpoint.attachment_count = attachments->entry_count;
  return checkpoint;
}
void qa_free_weapon_behavior_attachment_checkpoint(
    qa_weapon_behavior_attachment_checkpoint checkpoint) {
  free((void *)checkpoint.attachments);
}

typedef struct resume_row { qa_owned_actor owner; qa_weapon_behavior_source *source; } resume_row;
typedef struct resume_job {
  qa_weapon_behavior_attachments *attachments;
  const resume_row *rows;
  size_t count;
} resume_job;
static void resume_all(void *arg) {
  resume_job *job = arg;
  qa_weapon_behavior_attachments *attachments = job->attachments;
  for (size_t i = 0; i < job->count; i++) {
    qa_weapon_behavior_instance instance =
        job->rows[i].source->resume(job->rows[i].source->ctx, job->rows[i].owner.id);
    if (attachments->entry_count == attachments->entry_capacity) {
      attachments->entry_capacity =
          attachments->entry_capacity != 0 ? attachments->entry_capacity * 2 : 8;
      attachments->entries = xrealloc(attachments->entries,
                                      attachments->entry_capacity * sizeof *attachments->entries);
    }
    attachments->entries[attachments->entry_count].owner = job->rows[i].owner;
    attachments->entries[attachments->entry_count].instance = instance;
    attachments->entry_count++;
  }
}

void qa_weapon_behavior_attachments_restore(qa_weapon_behavior_attachments *attachments,
                                            qa_weapon_behavior_attachment_checkpoint checkpoint) {
  if (checkpoint.version != 1 || attachments->entry_count != 0)
    qa_error("Weapon behavior attachments require an empty matching checkpoint owner");
  resume_row *rows = xcalloc((checkpoint.attachment_count != 0 ? checkpoint.attachment_count : 1) *
                          sizeof *rows);
  for (size_t i = 0; i < checkpoint.attachment_count; i++) {
    const qa_weapon_behavior_attachment *entry = &checkpoint.attachments[i];
    qa_owned_actor owner;
    bool resolved =
        qa_session_actor_registry_resolve_saved(attachments->registry, entry->projectile, &owner);
    qa_weapon_behavior_source *source = find_source(attachments, entry->definition.id);
    bool seen = false;
    if (resolved) {
      for (size_t j = 0; j < i; j++) {
        if (qa_same_actor(rows[j].owner.id, owner.id)) seen = true;
      }
    }
    if (!resolved || !str_eq(owner.owner, entry->owner) || source == NULL ||
        !qa_same_weapon_behavior(source->definition, entry->definition) || seen) {
      free(rows);
      qa_error("Saved weapon behavior owner or source differs");
    }
    rows[i].owner = owner;
    rows[i].source = source;
  }
  /* try { resume each } catch { detach every attached actor; rethrow } */
  resume_job job = {attachments, rows, checkpoint.attachment_count};
  qa_caught_error caught;
  bool resumed = qa_try(resume_all, &job, &caught);
  free(rows);
  if (!resumed) {
    while (attachments->entry_count != 0)
      qa_weapon_behavior_attachments_detach(attachments, attachments->entries[0].owner.id);
    qa_rethrow(&caught);
  }
}

void qa_weapon_behavior_attachments_close(qa_weapon_behavior_attachments *attachments) {
  while (attachments->entry_count != 0)
    qa_weapon_behavior_attachments_detach(attachments, attachments->entries[0].owner.id);
  attachments->source_count = 0;
}

/* --- save readers --- */
static const char *callback_kind_name(qa_weapon_behavior_callback_kind kind) {
  switch (kind) {
    case QA_WEAPON_CALLBACK_QUAKEC: return "quakec";
    case QA_WEAPON_CALLBACK_QVM: return "qvm";
    case QA_WEAPON_CALLBACK_NATIVE_ARTIFACT: return "native-artifact";
  }
  return "";
}
static void read_module_checked(const qa_save_reader *source, qa_module_identity wanted) {
  qa_module_identity found = qa_read_module(source);
  if (!str_eq(found.id, wanted.id) || !str_eq(found.artifact_path, wanted.artifact_path) ||
      !str_eq(found.digest, wanted.digest) || !str_eq(found.revision, wanted.revision))
    qa_save_reader_fail(source, "weapon behavior module differs from the loaded artifact");
}
static void read_callback(const qa_save_reader *source, bool has_wanted,
                          qa_weapon_behavior_callback wanted) {
  if (!has_wanted) {
    if (!qa_save_reader_is_null(source))
      qa_save_reader_fail(source, "unexpected activation callback");
    return;
  }
  qa_save_reader kind, module;
  if (!qa_save_reader_field(source, "kind", &kind)) abort();
  (void)qa_save_reader_literal_string(&kind, callback_kind_name(wanted.kind));
  qa_save_reader_destroy(&kind);
  if (!qa_save_reader_field(source, "module", &module)) abort();
  read_module_checked(&module, wanted.module);
  qa_save_reader_destroy(&module);
  switch (wanted.kind) {
    case QA_WEAPON_CALLBACK_QUAKEC: {
      qa_save_reader field;
      if (!qa_save_reader_field(source, "functionIndex", &field)) abort();
      (void)qa_save_reader_literal_number(&field, wanted.function_index);
      qa_save_reader_destroy(&field);
      break;
    }
    case QA_WEAPON_CALLBACK_QVM: {
      qa_save_reader field;
      if (!qa_save_reader_field(source, "instructionIndex", &field)) abort();
      (void)qa_save_reader_literal_number(&field, wanted.instruction_index);
      qa_save_reader_destroy(&field);
      break;
    }
    case QA_WEAPON_CALLBACK_NATIVE_ARTIFACT: {
      qa_save_reader field, abi_reader;
      if (!qa_save_reader_field(source, "imageOffset", &field)) abort();
      if (qa_save_reader_bigint(&field) != wanted.image_offset)
        qa_save_reader_fail(source, "native weapon entry differs from the loaded artifact");
      qa_save_reader_destroy(&field);
      if (!qa_save_reader_field(source, "abi", &abi_reader)) abort();
      qa_native_call_abi abi = qa_read_native_call_abi(&abi_reader);
      if (!str_eq(abi.kind, wanted.abi.kind) || !str_eq(abi.call, wanted.abi.call) ||
          !str_eq(abi.image, wanted.abi.image) || abi.pointer_bytes != wanted.abi.pointer_bytes)
        qa_save_reader_fail(source, "native weapon callback ABI differs from the loaded artifact");
      qa_save_reader_destroy(&abi_reader);
      break;
    }
  }
}
qa_weapon_behavior_definition qa_read_weapon_behavior_definition(
    const qa_save_reader *reader, qa_weapon_behavior_definition expected) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, "id", &field)) abort();
  (void)qa_save_reader_literal_string(&field, expected.id);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "title", &field)) abort();
  (void)qa_save_reader_string(&field);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "role", &field)) abort();
  (void)qa_save_reader_literal_string(&field, expected.role);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "aspect", &field)) abort();
  (void)qa_save_reader_literal_string(&field, expected.aspect);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "module", &field)) abort();
  read_module_checked(&field, expected.module);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "fire", &field)) abort();
  read_callback(&field, true, expected.fire);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(reader, "activate", &field)) abort();
  read_callback(&field, expected.has_activate, expected.activate);
  qa_save_reader_destroy(&field);
  return expected;
}
qa_weapon_behavior_attachment_checkpoint qa_read_weapon_behavior_attachment_checkpoint(
    const qa_save_reader *reader, const qa_weapon_behavior_definition *definitions,
    size_t definition_count) {
  qa_save_reader version, list;
  if (!qa_save_reader_field(reader, "version", &version)) abort();
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  if (!qa_save_reader_field(reader, "attachments", &list)) abort();
  size_t count = qa_save_reader_list_count(&list);
  qa_weapon_behavior_attachment *rows = xcalloc((count != 0 ? count : 1) * sizeof *rows);
  /* A SaveFormatError leaves the partial list to the GC in TS. */
  qa_cleanup node;
  qa_cleanup_push(&node, free, rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, definition_reader, id_reader, projectile, owner;
    if (!qa_save_reader_item(&list, i, &entry)) abort();
    if (!qa_save_reader_field(&entry, "definition", &definition_reader)) abort();
    if (!qa_save_reader_field(&definition_reader, "id", &id_reader)) abort();
    const char *id = qa_save_reader_string(&id_reader);
    const qa_weapon_behavior_definition *expected = NULL;
    for (size_t d = 0; d < definition_count; d++) {
      if (str_eq(definitions[d].id, id)) expected = &definitions[d];
    }
    if (expected == NULL)
      qa_save_reader_fail(&definition_reader, "saved weapon behavior is not selected");
    if (!qa_save_reader_field(&entry, "projectile", &projectile)) abort();
    rows[i].projectile = qa_read_saved_actor(&projectile);
    qa_save_reader_destroy(&projectile);
    if (!qa_save_reader_field(&entry, "owner", &owner)) abort();
    rows[i].owner = qa_namespaced(&owner);
    qa_save_reader_destroy(&owner);
    rows[i].definition = qa_read_weapon_behavior_definition(&definition_reader, *expected);
    qa_save_reader_destroy(&id_reader);
    qa_save_reader_destroy(&definition_reader);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  qa_cleanup_pop(&node);
  qa_weapon_behavior_attachment_checkpoint checkpoint;
  checkpoint.version = 1;
  checkpoint.attachments = rows;
  checkpoint.attachment_count = count;
  return checkpoint;
}
