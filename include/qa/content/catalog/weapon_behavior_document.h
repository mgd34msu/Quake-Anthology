/*
 * Port of src/content/catalog/weapon-behavior-document.ts. The
 * behaviors list borrows from an owned parsed tree kept in the
 * document; free with qa_weapon_behavior_document_free.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_WEAPON_BEHAVIOR_DOCUMENT_H
#define QA_CONTENT_CATALOG_WEAPON_BEHAVIOR_DOCUMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/persistence/value.h"

typedef struct qa_weapon_behavior_document {
  int version; /* TS literal 1 */
  bool has_artifact_path;
  char *artifact_path; /* owned */
  const qa_save_value *const *behaviors; /* borrowed from tree */
  size_t behavior_count;
  qa_save_value *tree; /* owned */
} qa_weapon_behavior_document;

/* TS readWeaponBehaviorDocument. False only on OOM; invalid
 * documents abort with the exact TS Error messages. */
bool qa_read_weapon_behavior_document(const uint8_t *bytes, size_t length,
                                      qa_weapon_behavior_document *out);
void qa_weapon_behavior_document_free(qa_weapon_behavior_document *document);

/* TS weaponBehaviorEntryId (borrowed from entry). */
const char *qa_weapon_behavior_entry_id(const qa_save_value *entry);

#endif
