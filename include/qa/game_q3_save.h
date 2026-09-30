#ifndef QA_GAME_Q3_SAVE_H
#define QA_GAME_Q3_SAVE_H

#include "qa/game_q3.h"
#include "qa/targets.h"

/* Portable native and authored continuation. Shared stores retain one owner.
 * Restore requires an isolated candidate with restored shared actors. A failed
 * restore invalidates that candidate; the save coordinator must discard it. */
bool qa_q3_game_capture(qa_q3_game *, qa_buffer *, qa_error *);
bool qa_q3_game_restore(qa_q3_game *, qa_bytes, qa_error *);
/* Finish after exact shared-store import. Validates captured private lease
 * presence and actual authored bindings without registering shared state.
 * Source frames remain blocked until finish succeeds. */
bool qa_q3_game_reconnect(qa_q3_game *, qa_error *);
bool qa_q3_game_target_binding(qa_q3_game *, qa_actor_id, qa_target_binding *, qa_error *);
/* Candidate binding reconstruction validates the decoded shared declarations
 * and adopts only private lease metadata. It never registers shared stores. */
bool qa_q3_game_inventory_group(qa_q3_game *, qa_actor_id, uint64_t saved_serial,
    const qa_inventory_source_group *, qa_inventory_items *, qa_error *);
bool qa_q3_game_pickup_observer(qa_q3_game *, qa_actor_id, uint64_t saved_serial,
    qa_pickup_observer *, qa_error *);
bool qa_q3_game_damage_admission(qa_q3_game *, qa_actor_id, qa_combat_admission *, qa_error *);

#endif
