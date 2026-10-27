/*
 * Port of src/movement/client-outputs.ts: crouch requests and movement
 * mode/type projection from mod client outputs. TS bitwise command
 * edits apply ToInt32 (qa_int32), as in the source.
 * GPL-2.0-or-later.
 */
#ifndef QA_MOVEMENT_CLIENT_OUTPUTS_H
#define QA_MOVEMENT_CLIENT_OUTPUTS_H

#include <stdbool.h>

#include "qa/contracts/mod_client_outputs.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/protocol.h"

/*
 * TS clientStanceCommand. has_crouched false is the TS undefined
 * (command returned unchanged). NetQuake aborts with the TS message.
 */
qa_user_command qa_client_stance_command(qa_user_command command, bool has_crouched, bool crouched);

/** TS clientMovementType: original enumerations differ per family. */
double qa_client_movement_type(qa_movement_state_kind kind, qa_mod_client_movement_mode mode);

/*
 * TS clientMovementMode: false is the TS undefined (dead, no outputs,
 * or outputs without a mode); otherwise *out_mode holds the mode.
 */
bool qa_client_movement_mode(const qa_mod_client_movement_outputs *outputs, double health,
                             qa_mod_client_movement_mode *out_mode);

#endif
