/* Port of src/guest/pe/exports.ts. GPL-2.0-or-later. */
#include "qa/guest/pe/exports.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool symbol_equal(const qa_guest_symbol_name *entry, const qa_guest_symbol_name *requested) {
  if (entry->is_ordinal) return requested->is_ordinal && entry->ordinal == requested->ordinal;
  if (requested->is_ordinal) return false;
  bool version_equal = (entry->version == NULL && requested->version == NULL)
      || (entry->version != NULL && requested->version != NULL
          && strcmp(entry->version, requested->version) == 0);
  return strcmp(entry->name, requested->name) == 0 && version_equal;
}
static bool key_equal(const char *a, const char *b) { return strcmp(a, b) == 0; }

qa_pe_resolved_export qa_resolve_pe_export(const qa_pe_image *image, qa_guest_symbol_name symbol,
                                           qa_pe_library_lookup lookup, void *lookup_ctx) {
  typedef struct seen_entry { const qa_pe_image *image; char *key; } seen_entry;
  seen_entry *seen = NULL; size_t seen_count = 0, cap = 0;
  const qa_pe_image *current = image;
  qa_guest_symbol_name name = symbol;
  for (int depth = 0; depth < 128; depth++) {
    char key_buffer[1024];
    if (name.is_ordinal) (void)snprintf(key_buffer, sizeof key_buffer, "ordinal:%d", name.ordinal);
    else (void)snprintf(key_buffer, sizeof key_buffer, "name:%s:%s", name.name,
                         name.version != NULL ? name.version : "");
    for (size_t i = 0; i < seen_count; i++) {
      if (seen[i].image == current && key_equal(seen[i].key, key_buffer)) {
        for (size_t j = 0; j < seen_count; j++) free(seen[j].key);
        free(seen);
        qa_pe_error(QA_PE_STAGE_EXPORTS, "cyclic export forwarder");
      }
    }
    if (seen_count == cap) {
      cap = cap == 0 ? 8 : cap * 2;
      seen = realloc(seen, cap * sizeof *seen);
      if (seen == NULL) qa_pe_error(QA_PE_STAGE_EXPORTS, "out of memory");
    }
    seen[seen_count].image = current;
    seen[seen_count].key = malloc(strlen(key_buffer) + 1);
    if (seen[seen_count].key == NULL) qa_pe_error(QA_PE_STAGE_EXPORTS, "out of memory");
    strcpy(seen[seen_count].key, key_buffer);
    seen_count++;
    const qa_guest_export *exported = NULL;
    for (size_t i = 0; i < current->image.export_count; i++)
      if (symbol_equal(&current->image.exports[i].symbol, &name)) {
        exported = &current->image.exports[i];
        break;
      }
    if (exported == NULL) {
      const char *module_id = current->image.module.id != NULL ? current->image.module.id : "";
      for (size_t j = 0; j < seen_count; j++) free(seen[j].key);
      free(seen);
      qa_pe_error(QA_PE_STAGE_EXPORTS, "unresolved %s %s", module_id, key_buffer);
    }
    if (!exported->is_forward) {
      qa_pe_resolved_export result = {current, exported->address};
      for (size_t j = 0; j < seen_count; j++) free(seen[j].key);
      free(seen);
      return result;
    }
    const qa_pe_image *dependency = lookup(lookup_ctx, exported->forward_library, current);
    if (dependency == NULL) {
      const char *library = exported->forward_library;
      for (size_t j = 0; j < seen_count; j++) free(seen[j].key);
      free(seen);
      qa_pe_error(QA_PE_STAGE_EXPORTS, "unresolved forwarded library %s", library);
    }
    if (dependency->image.abi.pointer_bytes != image->image.abi.pointer_bytes) {
      for (size_t j = 0; j < seen_count; j++) free(seen[j].key);
      free(seen);
      qa_pe_error(QA_PE_STAGE_EXPORTS, "forwarded export belongs to another guest address space or ABI width");
    }
    current = dependency;
    name = exported->forward_symbol;
  }
  for (size_t j = 0; j < seen_count; j++) free(seen[j].key);
  free(seen);
  qa_pe_error(QA_PE_STAGE_EXPORTS, "export forwarder chain exceeds 128 entries");
  qa_pe_resolved_export empty = {NULL, {0}};
  return empty;
}
