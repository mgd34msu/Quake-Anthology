/* Port of src/input/index.ts (re-export barrel). As in the
 * TypeScript barrel, binding-store, client-commands,
 * device-settings, impulse, mouse-buttons, mouse-settings,
 * pitch-drift and weapon-bindings are not re-exported here.
 * GPL-2.0-or-later. */
#ifndef QA_INPUT_H
#define QA_INPUT_H

#include "qa/input/bindings.h"
#include "qa/input/buttons.h"
#include "qa/input/gamepad.h"
#include "qa/input/haptics.h"
#include "qa/input/key_codes.h"
#include "qa/input/keys.h"
#include "qa/input/midi.h"
#include "qa/input/mouse.h"
#include "qa/input/router.h"
#include "qa/input/sdl_keys.h"
#include "qa/input/seat.h"
#include "qa/input/source_input.h"
#include "qa/input/source_joystick.h"
#include "qa/input/source_midi.h"
#include "qa/input/user_command.h"

#endif
