/* Port of src/content/catalog/qvm-weapon-behaviors.ts.
 * GPL-2.0-or-later. */
#include "qa/content/catalog/qvm_weapon_behaviors.h"

#include "catalog_internal.h"

bool qa_read_qvm_weapon_behavior_document(
    const uint8_t *bytes, size_t length, qa_qvm_weapon_behavior_document *out) {
  memset(out, 0, sizeof *out);
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return false;
  qa_save_reader reader = {0};
  if (!qa_save_reader_init(&reader, value, "qvm-weapon-behaviors.json")) {
    qa_save_value_free(value);
    return false;
  }
  qa_save_reader version = {0}, profiles = {0};
  bool ok = qa_save_reader_field(&reader, "version", &version) &&
            qa_save_reader_field(&reader, "profiles", &profiles);
  if (ok) {
    (void)qa_save_reader_literal_number(&version, 1);
    out->profile_count = qa_save_reader_list_count(&profiles);
    out->profiles = profiles.value != NULL
                        ? (const qa_save_value *const *)profiles.value->items
                        : NULL;
    out->tree = value;
  } else {
    qa_save_value_free(value);
  }
  qa_save_reader_destroy(&version);
  qa_save_reader_destroy(&profiles);
  qa_save_reader_destroy(&reader);
  return ok;
}

void qa_qvm_weapon_behavior_document_free(
    qa_qvm_weapon_behavior_document *document) {
  qa_save_value_free(document->tree);
  memset(document, 0, sizeof *document);
}

bool qa_load_qvm_weapon_behavior(qa_catalog_mounted_reader mounts,
                                 qa_qvm_weapon_behavior_services services,
                                 qa_provider_id provider,
                                 const qa_save_value *declaration,
                                 char **owned_path,
                                 qa_mounted_qvm_weapon_behavior *out) {
  memset(out, 0, sizeof *out);
  *owned_path = NULL;
  if (services.resolve_artifact == NULL || services.read_profile == NULL)
    return false;
  qa_save_reader reader = {0};
  if (!qa_save_reader_init(&reader, declaration, "qvm-weapon-profile"))
    return false;
  qa_save_reader path_reader = {0}, abi_reader = {0};
  bool readers_ok = qa_save_reader_field(&reader, "artifactPath", &path_reader) &&
                    qa_save_reader_field(&reader, "abiProfile", &abi_reader);
  char *path = NULL;
  const char *abi_profile = NULL;
  if (readers_ok) {
    path = qa_catalog_normalize_resource_path(qa_save_reader_string(&path_reader));
    static const char *const choices[] = {"q3-modern", "q3-1.16n-base"};
    abi_profile = qa_save_reader_choice_string(&abi_reader, choices, 2);
  }
  qa_save_reader_destroy(&path_reader);
  qa_save_reader_destroy(&abi_reader);
  qa_save_reader_destroy(&reader);
  if (path == NULL) return false;
  *owned_path = path;
  qa_catalog_opened_resource opened = {0};
  bool found = mounts.open(mounts.ctx, path, &opened);
  if (!found)
    qa_catalog_error("Declared QVM behavior artifact is missing: %s", path);
  qa_catalog_qvm_artifact artifact = {0};
  artifact.kind = NULL;
  artifact.role = "qagame";
  artifact.abi_profile = abi_profile;
  artifact.module.id = provider;
  artifact.module.artifact_path = path;
  artifact.module.digest = opened.reference.digest;
  artifact.module.revision = opened.reference.digest;
  if (!services.resolve_artifact(services.ctx, &artifact, opened.bytes,
                                 opened.byte_length)) return false;
  if (artifact.kind == NULL || strcmp(artifact.kind, "bytecode") != 0)
    qa_catalog_error("QVM weapon behavior requires actual bytecode");
  qa_catalog_qvm_weapon_profile profile = {0};
  if (!services.read_profile(services.ctx, declaration, &artifact, &profile))
    return false;
  out->artifact = artifact;
  out->resource = opened.reference;
  out->profile = profile;
  out->declaration = declaration;
  return true;
}

void qa_mounted_qvm_weapon_behavior_list_free(
    qa_mounted_qvm_weapon_behavior_list *list) {
  free(list->values);
  for (size_t i = 0; i < list->owned_count; i++) free(list->owned_paths[i]);
  free(list->owned_paths);
  qa_qvm_weapon_behavior_document_free(&list->document);
  memset(list, 0, sizeof *list);
}

bool qa_discover_qvm_weapon_behaviors(
    qa_catalog_mounted_reader mounts,
    qa_qvm_weapon_behavior_services services, qa_provider_id provider,
    qa_mounted_qvm_weapon_behavior_list *out, bool *out_found) {
  memset(out, 0, sizeof *out);
  *out_found = false;
  qa_catalog_opened_resource opened = {0};
  bool found = mounts.open(mounts.ctx, "qvm-weapon-behaviors.json", &opened);
  if (!found) return true;
  if (!qa_read_qvm_weapon_behavior_document(opened.bytes, opened.byte_length,
                                            &out->document))
    return false;
  size_t capacity = 0;
  for (size_t i = 0; i < out->document.profile_count; i++) {
    char *owned_path = NULL;
    qa_mounted_qvm_weapon_behavior entry = {0};
    bool ok = qa_load_qvm_weapon_behavior(mounts, services, provider,
                                          out->document.profiles[i],
                                          &owned_path, &entry);
    if (owned_path != NULL) {
      char **grown = (char **)realloc(out->owned_paths,
                                      (out->owned_count + 1) * sizeof *grown);
      if (grown != NULL) {
        out->owned_paths = grown;
        out->owned_paths[out->owned_count++] = owned_path;
      } else {
        free(owned_path);
      }
    }
    if (!ok) { qa_mounted_qvm_weapon_behavior_list_free(out); return false; }
    const char *id = entry.profile.definition.id;
    for (size_t prior = 0; prior < out->count; prior++)
      if (strcmp(out->values[prior].profile.definition.id, id) == 0)
        qa_catalog_error("Duplicate QVM weapon behavior %s", id);
    if (out->count == capacity) {
      size_t next = capacity != 0 ? capacity * 2 : 4;
      qa_mounted_qvm_weapon_behavior *grown =
          (qa_mounted_qvm_weapon_behavior *)realloc(out->values,
                                                    next * sizeof *grown);
      if (grown == NULL) {
        qa_mounted_qvm_weapon_behavior_list_free(out);
        return false;
      }
      out->values = grown; capacity = next;
    }
    out->values[out->count++] = entry;
  }
  *out_found = true;
  return true;
}
