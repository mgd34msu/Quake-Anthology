#ifndef QA_APPLICATION_NATIVE_Q2_COMBAT_KEX_H
#define QA_APPLICATION_NATIVE_Q2_COMBAT_KEX_H
#include "guest_native_q2_combat_state.h"
struct application_q2_kex_damage;
struct application_q2_kex_restore;
bool application_q2_kex_damage_prepare(application_q2_combat_profile *,
    struct application_q2_kex_damage **, qa_error *);
bool application_q2_kex_damage_bind(struct application_q2_kex_damage *, qa_error *);
bool application_q2_kex_damage_activate(struct application_q2_kex_damage *, qa_error *);
bool application_q2_kex_damage_track(struct application_q2_kex_damage *,
    const application_q2_combat_actor *, qa_error *);
void application_q2_kex_damage_released(struct application_q2_kex_damage *, qa_actor_id);
bool application_q2_kex_damage_suspend(struct application_q2_kex_damage *, qa_error *);
bool application_q2_kex_damage_close(struct application_q2_kex_damage *, qa_error *);
bool application_q2_kex_damage_capture(struct application_q2_kex_damage *, qa_buffer *, qa_error *);
bool application_q2_kex_damage_restore_prepare(struct application_q2_kex_damage *, qa_bytes,
    struct application_q2_kex_restore **, qa_error *);
void application_q2_kex_damage_restore_commit(struct application_q2_kex_damage *,
    struct application_q2_kex_restore *);
void application_q2_kex_damage_restore_abort(struct application_q2_kex_restore *);
/* Borrowed only while the synchronous original source frame is active. */
const qa_damage_request *application_native_q2_combat_request(struct application_native_q2 *, qa_actor_id);
bool application_native_q2_combat_inline_armor(struct application_native_q2 *,
    qa_native_address target, int32_t amount, uint32_t flags, qa_native_address point,
    qa_native_address normal, bool *handled, int32_t *saved, qa_error *);
#endif
