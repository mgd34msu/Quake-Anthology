/*
 * Port of src/content/mods/selection.ts: ModSelectionSet. The set
 * borrows description rows (refresh copies the row array; a
 * kept-but-uninstalled enabled mod gets an owned replacement row
 * with availability unavailable / "This enabled mod is no longer
 * installed"). TS plain-Error throws abort with the identical
 * message; allocation failure aborts the same way. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_SELECTION_H
#define QA_CONTENT_MODS_SELECTION_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/mods.h"

typedef struct qa_mod_selection_set {
  qa_mod_description *descriptions; /* owned array, borrowed rows */
  size_t description_count;
  qa_mod_selection *selected; /* owned array */
  size_t selected_count;
} qa_mod_selection_set;

/* TS constructor(descriptions, initial = []): refresh + restore. */
bool qa_mod_selection_set_init(qa_mod_selection_set *set,
                               const qa_mod_description *descriptions,
                               size_t description_count,
                               const qa_mod_selection *initial,
                               size_t initial_count);
void qa_mod_selection_set_destroy(qa_mod_selection_set *set);

/* TS entries() / enabled(): borrowed arrays + counts. */
const qa_mod_description *qa_mod_selection_set_entries(
    const qa_mod_selection_set *set, size_t *out_count);
const qa_mod_selection *qa_mod_selection_set_enabled(
    const qa_mod_selection_set *set, size_t *out_count);
bool qa_mod_selection_set_has(const qa_mod_selection_set *set,
                              qa_mod_selection selection);
void qa_mod_selection_set_refresh(qa_mod_selection_set *set,
                                  const qa_mod_description *descriptions,
                                  size_t description_count);
void qa_mod_selection_set_restore(qa_mod_selection_set *set,
                                  const qa_mod_selection *selections,
                                  size_t selection_count);
void qa_mod_selection_set_set_enabled(qa_mod_selection_set *set,
                                      qa_mod_selection selection, bool enabled);
void qa_mod_selection_set_validate(const qa_mod_selection_set *set);

#endif
