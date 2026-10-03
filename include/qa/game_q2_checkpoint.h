#ifndef QA_GAME_Q2_CHECKPOINT_H
#define QA_GAME_Q2_CHECKPOINT_H
#include "qa/game_q2.h"
#include "qa/targets.h"

/* qa.native-q2-continuation. Native continuation uses explicit
 * fields, string values and saved actor generations. Shared stores retain
 * body, combat, inventory, pickup, target and scheduler authority. */
bool qa_q2_game_capture(qa_q2_game *, qa_buffer *, qa_error *);
/* Requires an empty candidate and restored shared actors. The application
 * discards its entire candidate on failure. Shared stores restore afterward. */
bool qa_q2_game_restore(qa_q2_game *, qa_bytes, qa_error *);
/* Reconnect inventory actions, power cells, pickup observations and targets
 * after all shared stores are restored. Required before candidate publication. */
bool qa_q2_game_restore_finish(qa_q2_game *, qa_error *);
bool qa_q2_game_think_binding(qa_q2_game *, qa_actor_id, uint32_t,
                              qa_think_fn *, void **context, qa_error *);
bool qa_q2_game_target_binding(qa_q2_game *, qa_actor_id, qa_target_binding *, qa_error *);
bool qa_q2_game_inventory_group(qa_q2_game *, qa_actor_id, uint64_t saved_serial,
                                 const qa_inventory_source_group *, qa_inventory_items *, qa_error *);
bool qa_q2_game_pickup_observer(qa_q2_game *, qa_actor_id, qa_actor_owner,
                                 uint64_t saved_serial, qa_pickup_observer *, qa_error *);
#endif
