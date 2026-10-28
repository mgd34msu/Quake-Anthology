/* Port of src/app/bootstrap/simulation/prediction/types.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/prediction/types.h"
#include <stdlib.h>
void qa_app_movement_prediction_snapshot_destroy(qa_app_movement_prediction_snapshot *snapshot) {
  if (!snapshot) return;
  free((void *)snapshot->arsenal.ammo);
  snapshot->arsenal.ammo = NULL;
  snapshot->arsenal.ammo_count = 0;
}
