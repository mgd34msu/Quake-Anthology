#ifndef QA_GAME_Q1_CHECKPOINT_H
#define QA_GAME_Q1_CHECKPOINT_H

#include "qa/game_q1.h"
#include "qa/q1_save.h"
#include "qa/qc.h"

typedef struct qa_q1_restore qa_q1_restore;

/* Original ED_Write fields from the actual compiled Source; record positions
 * retain the physical edict namespace, including free rows. */
bool qa_q1_game_original_capture(qa_q1_game *, const qa_qc_program *,
    const qa_movement_state *, qa_q1_save_data *, qa_error *);
bool qa_q1_game_original_restore(qa_q1_game *, const qa_qc_program *,
    const qa_q1_save_data *, qa_movement_state *, qa_error *);

/* The owned byte stream contains native continuation only. Shared actor,
 * body, inventory, combat, campaign and scheduler state belongs to its owner.
 * Strings are deduplicated by value; actor references use the registry's
 * checkpoint identity domain. No native address is persisted. */
bool qa_q1_game_capture(qa_q1_game *, qa_buffer *, qa_error *);
/* Prepare requires a safe session and an empty native provider after shared
 * actors have been restored. It does not run source callbacks. Interned string
 * additions may survive abort; gameplay state remains unpublished. */
bool qa_q1_game_restore_prepare(qa_q1_game *, qa_bytes, qa_q1_restore **, qa_error *);
/* The coordinator publishes private state before importing shared stores.
 * Source turns remain blocked until finish validates the imported bindings. */
bool qa_q1_game_restore_prepare_source(qa_q1_game *, qa_bytes,
    const qa_q1_game *current_unit, qa_q1_restore **, qa_error *);
bool qa_q1_game_restore_finish(qa_q1_game *, qa_error *);
bool qa_q1_game_restore_validate(const qa_q1_restore *, qa_error *);
bool qa_q1_game_restore_commit(qa_q1_restore *, qa_error *);
void qa_q1_game_restore_abort(qa_q1_restore *);
/* Resolve scheduler callbacks after native continuation is published. The
 * shared save owner retains due time, sequence, boundary and execution owner. */
bool qa_q1_game_think_binding(qa_q1_game *, qa_actor_id, uint32_t callback_id, qa_think_fn *,
                              void **context, qa_error *);
/* Shared target restoration binds only native actor claims. The borrowed
 * callback context is this game, which must outlive every published binding.
 * An absent native claim returns false without an error. */
bool qa_q1_game_target_binding(qa_q1_game *, qa_actor_id, qa_target_binding *, qa_error *);
bool qa_q1_game_pickup_observer(qa_q1_game *, qa_actor_id, qa_actor_owner,
                                uint64_t saved_serial, qa_pickup_observer *, qa_error *);

#endif
