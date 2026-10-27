/* Port of src/content/composition/q3-q2-supply.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_COMPOSITION_Q3_Q2_SUPPLY_H
#define QA_CONTENT_COMPOSITION_Q3_Q2_SUPPLY_H

#include "qa/content/composition/q1_q2_supply.h"
#include "qa/contracts/pickups.h"

extern const qa_pickup_supply_profile QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE;

/** TS q3Q2SupplyLoadout(); returned storage is static. */
qa_composition_supply_loadout qa_composition_q3_q2_supply_loadout(void);

#endif
