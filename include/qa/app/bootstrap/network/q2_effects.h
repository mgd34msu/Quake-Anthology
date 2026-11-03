/* Port of src/app/bootstrap/network/q2-effects.ts. The decoded wire
 * effect is the content Q2PresentationEvent of kind "effect" and the
 * decoded trail the Q2WeaponEvent of kind "beam". GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_EFFECTS_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_EFFECTS_H
#include <stdbool.h>
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/network/q2/temp_entities.h"
/* TS q2BeamFromWire: false = TS null. On success *out is a zeroed "beam"
 * event (actor NULL = TS null; effect a static string). Missing endpoints
 * abort with the TS message. */
bool qa_app_q2_beam_from_wire(const qa_q2_temp_entity *value, qa_q2w_event *out);
/* TS q2EffectToWire over an "effect" event: false = TS null. On success
 * out->raw is NULL with length 0. */
bool qa_app_q2_effect_to_wire(const qa_q2_presentation_event *event, qa_q2_temp_entity *out);
/* TS q2EffectFromWire: false = TS null. On success *out is a zeroed
 * "effect" event (kind and effect static strings). */
bool qa_app_q2_effect_from_wire(const qa_q2_temp_entity *value, qa_q2_presentation_event *out);
#endif
