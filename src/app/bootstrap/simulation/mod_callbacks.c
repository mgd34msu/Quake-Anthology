/* Port of src/app/bootstrap/simulation/mod-callbacks.ts: SourceTime to
 * scalar seconds staging; operation registration rides landed mod
 * composition at the runtime call site. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/mod_callbacks.h"
double qa_app_sim_mod_callback_seconds(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}
