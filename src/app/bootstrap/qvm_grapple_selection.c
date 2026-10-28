/* Port of src/app/bootstrap/qvm-grapple-selection.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/qvm_grapple_selection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qvm/grapple_profile.h"
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
#include "qa/content/q3/equipment/threewave_grapple_profile.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
void qa_app_prepared_qvm_grapple_destroy(qa_app_prepared_qvm_grapple *prepared) {
  if (prepared == NULL) return;
  qa_resolved_qvm_artifact_destroy(&prepared->artifact);
  qa_content_mounted_destroy(prepared->mounts);
  free(prepared->module_id);
  memset(prepared, 0, sizeof *prepared);
}
static bool from_mounts(qa_content_id content, qa_content_mounted *mounts,
    const qa_app_qvm_grapple_host *host, qa_app_prepared_qvm_grapple *out) {
  memset(out, 0, sizeof *out);
  qa_content_opened_resource resource;
  if (!qa_content_mounted_open(mounts, "vm/qagame.qvm", NULL, NULL, &resource)) return false;
  const char *digest = resource.reference->digest;
  if (strcmp(digest, qa_lrctf_grapple_digest) != 0 && strcmp(digest, qa_threewave_grapple_digest) != 0) {
    free(resource.bytes);
    return false;
  }
  size_t id_size = strlen("q3:grapple/") + strlen(content) + 1;
  out->module_id = malloc(id_size);
  if (out->module_id == NULL) qa_range_error("Out of memory");
  snprintf(out->module_id, id_size, "q3:grapple/%s", content);
  qa_module_identity module = {out->module_id, resource.reference->requested_path, digest, digest};
  qa_qvm_resolve_artifact_options options = {0};
  options.module = module;
  options.role = "qagame";
  options.bytes = resource.bytes;
  options.length = resource.byte_length;
  bool resolved = qa_qvm_resolve_artifact(options, &out->artifact);
  free(resource.bytes);
  if (!resolved) qa_range_error("Out of memory");
  if (out->artifact.is_typescript) {
    qa_app_prepared_qvm_grapple_destroy(out);
    return false;
  }
  if (!host->profile_for(host->ctx, &out->artifact, &out->profile)) {
    qa_app_prepared_qvm_grapple_destroy(out);
    return false;
  }
  out->selection.is_enabled = true;
  out->selection.mechanic = "q3-qvm";
  out->selection.edition = "classic";
  out->selection.binding = "offhand";
  out->selection.source.provider = out->module_id;
  out->selection.source.content = content;
  out->selection.profile = &out->profile;
  out->mounts = mounts;
  return true;
}
bool qa_app_application_qvm_grapple_selection(const qa_content_catalog *catalog, qa_content_id content,
    const qa_app_qvm_grapple_host *host, qa_app_qvm_grapple_selection *selection_out,
    qa_qvm_grapple_definition *profile_out, char **module_id_out) {
  *module_id_out = NULL;
  if (qa_content_catalog_product(catalog, content)->expectation.family != QA_GAME_FAMILY_Q3) return false;
  qa_content_mounted *mounts = host->open_plan(host->ctx, catalog, content);
  qa_app_prepared_qvm_grapple prepared;
  bool found = from_mounts(content, mounts, host, &prepared);
  if (!found) {
    qa_content_mounted_destroy(mounts);
    return false;
  }
  *selection_out = prepared.selection;
  *profile_out = prepared.profile;
  *module_id_out = prepared.module_id;
  selection_out->profile = profile_out;
  selection_out->source.provider = *module_id_out;
  qa_resolved_qvm_artifact_destroy(&prepared.artifact);
  qa_content_mounted_destroy(prepared.mounts);
  return true;
}
static bool values_equal(const qa_save_value *a, const qa_save_value *b) {
  if (a->kind != b->kind) return false;
  switch (a->kind) {
    case QA_SAVE_BOOLEAN: return a->boolean == b->boolean;
    case QA_SAVE_NUMBER: return a->number == b->number;
    case QA_SAVE_STRING: case QA_SAVE_BIGINT:
      return (a->text == NULL) == (b->text == NULL) && (a->text == NULL || strcmp(a->text, b->text) == 0);
    case QA_SAVE_ARRAY:
      if (a->item_count != b->item_count) return false;
      for (size_t i = 0; i < a->item_count; i++) if (!values_equal(a->items[i], b->items[i])) return false;
      return true;
    case QA_SAVE_OBJECT:
      if (a->entry_count != b->entry_count) return false;
      for (size_t i = 0; i < a->entry_count; i++) {
        const qa_save_value *other = qa_save_value_get(b, a->entries[i].key);
        if (other == NULL || !values_equal(a->entries[i].value, other)) return false;
      }
      return true;
    case QA_SAVE_BYTES:
      return a->byte_length == b->byte_length
          && (a->byte_length == 0 || memcmp(a->bytes, b->bytes, a->byte_length) == 0);
    default: return true;
  }
}
bool qa_app_prepare_application_qvm_grapple(qa_app_qvm_grapple_selection selection,
    qa_app_for_content_fn for_content, void *for_content_ctx,
    const qa_app_qvm_grapple_host *host, qa_app_prepared_qvm_grapple *out) {
  qa_content_mounted *mounts = for_content(for_content_ctx, selection.source.content);
  qa_app_prepared_qvm_grapple mounted;
  bool found = from_mounts(selection.source.content, mounts, host, &mounted);
  bool matches = found && strcmp(mounted.selection.source.provider, selection.source.provider) == 0;
  if (matches) {
    qa_save_value *installed = qa_qvm_grapple_profile_declaration(&mounted.profile);
    qa_save_value *wanted = qa_qvm_grapple_profile_declaration(selection.profile);
    matches = installed != NULL && wanted != NULL && values_equal(installed, wanted);
    qa_save_value_free(installed);
    qa_save_value_free(wanted);
  }
  if (!matches) {
    if (found) qa_app_prepared_qvm_grapple_destroy(&mounted);
    else qa_content_mounted_destroy(mounts);
    qa_range_error("Selected grapple differs from its installed source executable or declaration");
  }
  *out = mounted;
  out->selection = selection;
  return true;
}
