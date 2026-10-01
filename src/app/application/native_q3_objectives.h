#ifndef QA_APPLICATION_NATIVE_Q3_OBJECTIVES_H
#define QA_APPLICATION_NATIVE_Q3_OBJECTIVES_H

#include "internal.h"

/* Ordinary source construction only. Restore imports the existing mode and
 * source rows, then reconnects their exact physical actor identities. */
bool application_native_q3_objective_adopt(application_provider *, qa_modes *, qa_mode_id,
    qa_actor_id, const qa_mode_object_spec *, qa_error *);
bool application_native_q3_objectives_initialize(application_provider *, qa_error *);
bool application_native_q3_objectives_reconnect(application_provider *, qa_error *);
bool application_native_q3_objective_bound(void *application, qa_mode_id, qa_actor_id,
    bool *native_source, qa_error *);
bool application_native_q3_objective_view(void *application, qa_mode_id, qa_actor_id,
    qa_mode_object_view *, qa_error *);
bool application_native_q3_obelisk_settings(void *, qa_q3_obelisk_settings *, qa_error *);
bool application_native_q3_obelisk_admitted(void *, qa_actor_id trigger,
    qa_actor_id model, int32_t source_team, qa_error *);
bool application_native_q3_obelisk_touch(void *, qa_actor_id trigger,
    qa_actor_id player, qa_error *);
bool application_native_q3_obelisk_die(void *, qa_actor_id trigger,
    qa_actor_id attacker, qa_q3_obelisk_die_stage, qa_error *);
bool application_native_q3_obelisk_pain(void *, qa_actor_id trigger,
    qa_actor_id attacker, int32_t source_score, qa_error *);

bool application_native_q3_objective_pickup(void *, qa_actor_id item, qa_actor_id player,
    uint32_t item_index, bool *accepted, qa_error *);
bool application_native_q3_objective_admitted(void *, qa_actor_id item,
    uint32_t item_index, bool finished, qa_error *);
bool application_native_q3_objective_dropped(void *, qa_actor_id item,
    uint32_t item_index, qa_error *);
bool application_native_q3_objective_drop(void *, qa_actor_id player, qa_error *);
bool application_native_q3_source_flags_cleared(void *, qa_actor_id player, qa_error *);
bool application_native_q3_objective_expired(void *, qa_actor_id item,
    uint32_t item_index, qa_error *);
bool application_native_q3_objective_nodrop(void *, qa_actor_id item,
    uint32_t item_index, qa_error *);
bool application_native_q3_return_flag(application_provider *, int32_t source_team,
    qa_error *);

#endif
