/*
 * Port of src/input/binding-store.ts: binding data without a
 * device, focus, or interactive seat. The store owns deep copies
 * of its bindings (the TS Map retains the objects); rebinding an
 * input keeps its first insertion position, as JS Map.set does.
 * GPL-2.0-or-later.
 */
#ifndef QA_INPUT_BINDING_STORE_H
#define QA_INPUT_BINDING_STORE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/ui.h"

/* Malloc'd physical-input key; caller frees. NULL only on OOM. */
char *qa_physical_input_key(const qa_physical_input *input);

typedef struct qa_binding_store qa_binding_store;

/* NULL only on allocation failure. Initial bindings are bound in
 * order, as the TS constructor does. */
qa_binding_store *qa_binding_store_create(const qa_input_binding *bindings,
                                          size_t count);
void qa_binding_store_destroy(qa_binding_store *store);
/* Borrowed store-owned array in insertion order. */
const qa_input_binding *qa_binding_store_bindings(const qa_binding_store *store,
                                                  size_t *out_count);
/* Borrowed target, or NULL for the TS null. */
const qa_input_binding_target *qa_binding_store_binding(
    const qa_binding_store *store, const qa_physical_input *input);
/* False only on allocation failure. */
bool qa_binding_store_bind(qa_binding_store *store, const qa_input_binding *binding);
void qa_binding_store_unbind(qa_binding_store *store, const qa_physical_input *input);
void qa_binding_store_unbind_all(qa_binding_store *store);

#endif
