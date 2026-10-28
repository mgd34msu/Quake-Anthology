/* Port of src/app/bootstrap/simulation/arsenal/inventory-labels.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_INVENTORY_LABELS_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_INVENTORY_LABELS_H
#include "qa/contracts/gameplay.h"
/* family "q1"|"q2"|"q3"; missing label aborts with the TS Error text. */
const char *qa_app_selected_ammo_label(const char *family, qa_item_id item);
#endif
