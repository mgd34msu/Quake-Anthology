/* Port of src/app/bootstrap/simulation/native-q2-rerelease-save.ts:
 * save payload carried as checkpoint-value tree (envelope codec landed
 * in persistence/value). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_q2_rerelease_save.h"
void qa_app_sim_q2_rerelease_native_save_destroy(qa_app_sim_q2_rerelease_native_save *save) {
  qa_save_value_free(save->value); save->value = NULL;
}
