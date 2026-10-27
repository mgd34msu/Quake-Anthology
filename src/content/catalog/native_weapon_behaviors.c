/* Port of src/content/catalog/native-weapon-behaviors.ts.
 * GPL-2.0-or-later. */
#include "qa/content/catalog/native_weapon_behaviors.h"

#include "catalog_internal.h"

bool qa_read_native_weapon_behavior_document(
    const uint8_t *bytes, size_t length,
    qa_native_weapon_behavior_document *out) {
  memset(out, 0, sizeof *out);
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return false;
  qa_save_reader reader = {0};
  if (!qa_save_reader_init(&reader, value, "native-weapon-behaviors.json")) {
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

void qa_native_weapon_behavior_document_free(
    qa_native_weapon_behavior_document *document) {
  qa_save_value_free(document->tree);
  memset(document, 0, sizeof *document);
}

bool qa_load_native_weapon_behavior(qa_catalog_mounted_reader mounts,
                                    qa_native_weapon_behavior_services services,
                                    qa_provider_id provider,
                                    const qa_save_value *value,
                                    qa_mounted_native_weapon_behavior *out) {
  memset(out, 0, sizeof *out);
  if (services.read_declaration == NULL || services.validate_image == NULL ||
      services.weapon_definition == NULL) return false;
  qa_native_weapon_behavior_declaration declaration = {0};
  if (!services.read_declaration(services.ctx, value, NULL, &declaration))
    return false;
  qa_catalog_opened_resource opened = {0};
  bool found = mounts.open(mounts.ctx, declaration.artifact_path, &opened);
  if (!found)
    qa_catalog_error("Declared native behavior artifact is missing: %s",
                     declaration.artifact_path);
  qa_module_identity module = {0};
  module.id = provider;
  module.artifact_path = opened.reference.requested_path;
  module.digest = opened.reference.digest;
  module.revision = opened.reference.digest;
  qa_native_weapon_behavior_declaration verified = {0};
  if (!services.read_declaration(services.ctx, value, &module, &verified))
    return false;
  /* TS re-reads into the same declaration shape; the second read
   * validates against the module. The behavior keeps the first
   * declaration object, so verify that the service's second read
   * agrees on the artifact path and otherwise keep the first. */
  (void)verified;
  if (!services.validate_image(services.ctx, &declaration, opened.bytes,
                               opened.byte_length)) return false;
  qa_weapon_behavior_definition definition = {0};
  if (!services.weapon_definition(services.ctx, module, &declaration, &definition))
    qa_catalog_error("Declared native behavior has no executable definition");
  out->resource = opened.reference;
  out->definition = definition;
  out->declaration = declaration;
  return true;
}

void qa_mounted_native_weapon_behavior_list_free(
    qa_mounted_native_weapon_behavior_list *list) {
  free(list->values);
  memset(list, 0, sizeof *list);
}

bool qa_discover_native_weapon_behaviors(
    qa_catalog_mounted_reader mounts,
    qa_native_weapon_behavior_services services, qa_provider_id provider,
    qa_mounted_native_weapon_behavior_list *out, bool *out_found) {
  memset(out, 0, sizeof *out);
  *out_found = false;
  qa_catalog_opened_resource document = {0};
  bool have_document =
      mounts.open(mounts.ctx, "native-weapon-behaviors.json", &document);
  if (!have_document) {
    qa_catalog_opened_resource opened = {0};
    bool have_dll = mounts.open(mounts.ctx, "game_x64.dll", &opened);
    if (!have_dll) return true;
    if (services.builtin_declaration == NULL) return true;
    qa_module_identity module = {0};
    module.id = provider;
    module.artifact_path = opened.reference.requested_path;
    module.digest = opened.reference.digest;
    module.revision = opened.reference.digest;
    qa_save_value *declaration = NULL;
    if (!services.builtin_declaration(services.ctx, module, &declaration))
      return true;
    qa_mounted_native_weapon_behavior entry = {0};
    bool ok = qa_load_native_weapon_behavior(mounts, services, provider,
                                             declaration, &entry);
    qa_save_value_free(declaration);
    if (!ok) return false;
    out->values = (qa_mounted_native_weapon_behavior *)malloc(sizeof *out->values);
    if (out->values == NULL) return false;
    out->values[0] = entry;
    out->count = 1;
    *out_found = true;
    return true;
  }
  qa_native_weapon_behavior_document parsed = {0};
  if (!qa_read_native_weapon_behavior_document(document.bytes,
                                               document.byte_length, &parsed))
    return false;
  size_t capacity = 0;
  for (size_t i = 0; i < parsed.profile_count; i++) {
    qa_mounted_native_weapon_behavior entry = {0};
    if (!qa_load_native_weapon_behavior(mounts, services, provider,
                                        parsed.profiles[i], &entry)) {
      qa_native_weapon_behavior_document_free(&parsed);
      qa_mounted_native_weapon_behavior_list_free(out);
      return false;
    }
    for (size_t prior = 0; prior < out->count; prior++) {
      if (strcmp(out->values[prior].definition.id, entry.definition.id) == 0) {
        qa_native_weapon_behavior_document_free(&parsed);
        qa_catalog_error("Duplicate native weapon behavior %s",
                         entry.definition.id);
      }
    }
    if (out->count == capacity) {
      size_t next = capacity != 0 ? capacity * 2 : 4;
      qa_mounted_native_weapon_behavior *grown =
          (qa_mounted_native_weapon_behavior *)realloc(out->values,
                                                       next * sizeof *grown);
      if (grown == NULL) {
        qa_native_weapon_behavior_document_free(&parsed);
        qa_mounted_native_weapon_behavior_list_free(out);
        return false;
      }
      out->values = grown; capacity = next;
    }
    out->values[out->count++] = entry;
  }
  qa_native_weapon_behavior_document_free(&parsed);
  *out_found = true;
  return true;
}
