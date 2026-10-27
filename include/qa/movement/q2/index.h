/*
 * Port of src/movement/q2/index.ts: Quake II ClientThink/Pmove and
 * rerelease game/cgame integration, plus the re-export barrel (the
 * umbrella header is include/qa/movement/q2.h). Copyright
 * id Software / ZeniMax. GPL-2.0-or-later.
 * C accommodations:
 *  - Movement results carry malloc'd contact arrays (caller frees);
 *    effects stay with the result that carries them. Applying
 *    contacts consumes the input result's arrays: the returned
 *    result owns a fresh effects array and keeps the contacts array
 *    (or frees it on the actor-removed path, which carries none).
 *  - The TS try/catch that reports input-application failure is
 *    unreachable in C: movement errors abort via qa_range_error, so
 *    end(state, failed) is only called on the success path.
 *  - The rerelease provider borrows its movement context (the TS
 *    closure capture); the classic provider owns no context.
 */
#ifndef QA_MOVEMENT_Q2_INDEX_H
#define QA_MOVEMENT_Q2_INDEX_H

#include "qa/contracts/identity.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/scene.h"
#include "qa/movement/q2/rerelease.h"

/** TS moveQ2Classic. */
qa_q2_movement_result qa_move_q2_classic(qa_q2_movement_input input,
                                         qa_movement_services services);
/** TS moveQ2Rerelease. */
qa_q2_rerelease_movement_result qa_move_q2_rerelease(
    qa_q2_rerelease_movement_input input, qa_movement_services services,
    qa_q2_rerelease_movement_context *context);

/** TS createQ2ClassicMovementProvider. */
qa_movement_provider qa_create_q2_classic_movement_provider(qa_provider_id id);
/** TS createQ2RereleaseMovementProvider (context borrowed). */
qa_movement_provider qa_create_q2_rerelease_movement_provider(
    qa_provider_id id, qa_q2_rerelease_movement_context *context);

/** TS q2PlayerShape. */
qa_trace_shape qa_q2_player_shape(void);

/** TS applyQ2MovementContacts (both overloads, unified contracts). */
qa_movement_result qa_apply_q2_movement_contacts(qa_movement_input input,
                                                 qa_movement_services services,
                                                 qa_movement_result result);

#endif
