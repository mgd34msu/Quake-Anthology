/* Port of src/content/catalog/weapon-behavior-document.ts.
 * GPL-2.0-or-later. */
#include "qa/content/catalog/weapon_behavior_document.h"

#include "catalog_internal.h"

static bool is_record(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_OBJECT;
}

bool qa_read_weapon_behavior_document(const uint8_t *bytes, size_t length,
                                      qa_weapon_behavior_document *out) {
  memset(out, 0, sizeof *out);
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return false;
  const qa_save_value *behaviors =
      is_record(value) ? qa_save_value_get(value, "behaviors") : NULL;
  const qa_save_value *version =
      is_record(value) ? qa_save_value_get(value, "version") : NULL;
  if (!is_record(value) || version == NULL || version->kind != QA_SAVE_NUMBER ||
      version->number != 1 || behaviors == NULL ||
      behaviors->kind != QA_SAVE_ARRAY) {
    qa_save_value_free(value);
    qa_catalog_error("Invalid weapon behavior declaration");
  }
  const qa_save_value *artifact = qa_save_value_get(value, "artifactPath");
  if (artifact != NULL && artifact->kind != QA_SAVE_UNDEFINED &&
      artifact->kind != QA_SAVE_STRING) {
    qa_save_value_free(value);
    qa_catalog_error("Invalid weapon behavior artifact path");
  }
  out->version = 1;
  out->tree = value;
  out->behaviors = (const qa_save_value *const *)behaviors->items;
  out->behavior_count = behaviors->item_count;
  if (artifact != NULL && artifact->kind == QA_SAVE_STRING) {
    out->artifact_path =
        qa_catalog_normalize_resource_path(artifact->text != NULL ? artifact->text : "");
    if (out->artifact_path == NULL) {
      qa_weapon_behavior_document_free(out);
      return false;
    }
    out->has_artifact_path = true;
  }
  return true;
}

void qa_weapon_behavior_document_free(qa_weapon_behavior_document *document) {
  free(document->artifact_path);
  qa_save_value_free(document->tree);
  document->artifact_path = NULL;
  document->tree = NULL;
  document->behaviors = NULL;
  document->behavior_count = 0;
}

const char *qa_weapon_behavior_entry_id(const qa_save_value *entry) {
  const qa_save_value *id = is_record(entry) ? qa_save_value_get(entry, "id") : NULL;
  if (!is_record(entry) || id == NULL || id->kind != QA_SAVE_STRING)
    qa_catalog_error("Invalid weapon behavior entry");
  return id->text != NULL ? id->text : "";
}
