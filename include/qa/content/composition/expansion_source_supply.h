/*
 * Port of src/content/composition/expansion-source-supply.ts: expansion
 * pickups reuse the selected composition's explicit base supply groups,
 * retaining original quantities. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_EXPANSION_SOURCE_SUPPLY_H
#define QA_CONTENT_COMPOSITION_EXPANSION_SOURCE_SUPPLY_H

#include <stdbool.h>

#include "qa/content/composition/expansion_supply.h"
#include "qa/contracts/pickups.h"

/** TS expansionSourceSupply(): false is OOM only; *out is zeroed on entry. */
bool qa_composition_expansion_source_supply(const qa_pickup_supply_profile *profile,
                                            qa_composition_owned_profile *out);

#endif
