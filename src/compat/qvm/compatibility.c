/* Port of src/compat/qvm/compatibility.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/compatibility.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/mounts/paths.h"
#include "qa/foundation/support.h"

static void reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("out of memory");
}

static char *lower_copy(const char *text) {
  char *copy = malloc(strlen(text) + 1);
  if (copy == NULL) qa_range_error("out of memory");
  for (size_t i = 0; text[i] != '\0'; i++) copy[i] = (char)tolower((unsigned char)text[i]);
  copy[strlen(text)] = '\0';
  return copy;
}

qa_qvm_compatibility_declaration qa_qvm_parse_compatibility_declaration(
    const qa_save_value *value, qa_qvm_module_path_digest module, qa_qvm_role role) {
  qa_save_reader reader, version, modules;
  if (!qa_save_reader_init(&reader, value, "qvm-compatibility.json"))
    qa_range_error("out of memory");
  reader_field(&reader, "version", &version);
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  reader_field(&reader, "modules", &modules);
  size_t count = qa_save_reader_list_count(&modules);
  qa_qvm_compatibility_declaration selected = {"q3-modern", NULL, NULL};
  char **seen = calloc(count == 0 ? 1 : count, sizeof(char *));
  if (seen == NULL) qa_range_error("out of memory");
  size_t seen_count = 0;
  char *module_lower = lower_copy(module.artifact_path);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, role_reader, path_reader, digest_reader, profile_reader;
    if (!qa_save_reader_item(&modules, i, &entry)) qa_range_error("out of memory");
    reader_field(&entry, "role", &role_reader);
    const char *role_choices[] = {"qagame", "cgame", "ui"};
    const char *selected_role = qa_save_reader_choice_string(&role_reader, role_choices, 3);
    qa_save_reader_destroy(&role_reader);
    reader_field(&entry, "artifactPath", &path_reader);
    const char *raw_path = qa_save_reader_string(&path_reader);
    char *path = qa_normalize_resource_path(raw_path);
    if (path == NULL) qa_range_error("out of memory");
    qa_save_reader_destroy(&path_reader);
    char *path_lower = lower_copy(path);
    size_t key_length = strlen(selected_role) + 1 + strlen(path_lower) + 1;
    char *key = malloc(key_length);
    if (key == NULL) qa_range_error("out of memory");
    strcpy(key, selected_role);
    strcat(key, "/");
    strcat(key, path_lower);
    bool duplicate = false;
    for (size_t s = 0; s < seen_count; s++) duplicate = duplicate || strcmp(seen[s], key) == 0;
    if (duplicate) qa_save_reader_fail(&entry, "duplicate module compatibility declaration");
    seen[seen_count++] = key;
    reader_field(&entry, "artifactDigest", &digest_reader);
    const char *digest = qa_save_reader_string(&digest_reader);
    bool digest_ok = strncmp(digest, "sha256:", 7) == 0 && strlen(digest) == 71;
    for (size_t d = 7; digest_ok && digest[d] != '\0'; d++)
      digest_ok = (digest[d] >= '0' && digest[d] <= '9') || (digest[d] >= 'a' && digest[d] <= 'f');
    if (!digest_ok) qa_save_reader_fail(&entry, "expected exact sha256 artifact digest");
    qa_save_reader_destroy(&digest_reader);
    reader_field(&entry, "profile", &profile_reader);
    const char *profiles[] = {"q3-modern", "q3-1.16n-base"};
    const char *profile = qa_save_reader_choice_string(&profile_reader, profiles, 2);
    qa_save_reader_destroy(&profile_reader);
    qa_save_reader primary, equipment;
    reader_field(&entry, "primary", &primary);
    reader_field(&entry, "equipmentPresentation", &equipment);
    bool has_primary = !qa_save_reader_is_undefined(&primary);
    bool has_equipment = !qa_save_reader_is_undefined(&equipment);
    if (has_primary && strcmp(selected_role, "qagame") != 0)
      qa_save_reader_fail(&primary, "primary player interfaces belong to qagame");
    if (has_equipment && strcmp(selected_role, "cgame") != 0)
      qa_save_reader_fail(&equipment, "equipment presentation belongs to cgame");
    if (strcmp(selected_role, role) == 0 && strcmp(path_lower, module_lower) == 0) {
      if (strcmp(digest, module.digest) != 0)
        qa_save_reader_fail(&entry, "compatibility declaration belongs to different artifact bytes");
      selected.profile = profile;
      selected.primary = has_primary ? primary.value : NULL;
      selected.equipment_presentation = has_equipment ? equipment.value : NULL;
    }
    qa_save_reader_destroy(&primary);
    qa_save_reader_destroy(&equipment);
    free(path);
    free(path_lower);
    qa_save_reader_destroy(&entry);
  }
  free(module_lower);
  for (size_t s = 0; s < seen_count; s++) free(seen[s]);
  free(seen);
  qa_save_reader_destroy(&modules);
  qa_save_reader_destroy(&reader);
  return selected;
}

qa_qvm_abi_profile qa_qvm_parse_compatibility(const qa_save_value *value,
                                              qa_qvm_module_path_digest module, qa_qvm_role role) {
  return qa_qvm_parse_compatibility_declaration(value, module, role).profile;
}

bool qa_qvm_read_compatibility_declaration(qa_content_mounted *mounts,
                                           qa_qvm_module_path_digest module, qa_qvm_role role,
                                           qa_qvm_compatibility_reading *out) {
  out->declaration.profile = "q3-modern";
  out->declaration.primary = NULL;
  out->declaration.equipment_presentation = NULL;
  out->resource = NULL;
  out->parsed = NULL;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(mounts, "qvm-compatibility.json", NULL, NULL, &opened)) return true;
  qa_save_value *value = qa_parse_json_value(opened.bytes, opened.byte_length, false);
  out->resource = opened.reference;
  free(opened.bytes);
  if (value == NULL) return false;
  out->parsed = value;
  out->declaration = qa_qvm_parse_compatibility_declaration(value, module, role);
  return true;
}

void qa_qvm_compatibility_reading_destroy(qa_qvm_compatibility_reading *reading) {
  if (reading == NULL) return;
  qa_save_value_free(reading->parsed);
  reading->parsed = NULL;
}

bool qa_qvm_read_compatibility(qa_content_mounted *mounts, qa_qvm_module_path_digest module,
                               qa_qvm_role role, qa_qvm_abi_profile *out) {
  qa_qvm_compatibility_reading reading;
  if (!qa_qvm_read_compatibility_declaration(mounts, module, role, &reading)) return false;
  *out = reading.declaration.profile;
  qa_qvm_compatibility_reading_destroy(&reading);
  return true;
}
