/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/travel/types.h"
static const char *const NAMES[QA_BMV_COUNT] = {
    "svMaxStep", "svMaxBarrier", "svGravity", "rocketLauncherIndex", "bfgIndex",
    "grappleIndex", "missileEntityType", "offhandGrapple", "grappleOnCommand", "grappleOffCommand"};
const char *qa_bot_movement_variable_name_string(qa_bot_movement_variable_name name) {
  return (name >= 0 && name < QA_BMV_COUNT) ? NAMES[name] : "svMaxStep";
}
