#ifndef APPLICATION_NATIVE_Q2_SOURCE_DAMAGE_H
#define APPLICATION_NATIVE_Q2_SOURCE_DAMAGE_H
#include "native_q2_source_combat_state.h"
struct application_native_q2_source_damage;
bool application_native_q2_source_damage_create(struct application_native_q2 *,
    application_native_q2_source_combat_state *,uint32_t,
    struct application_native_q2_source_damage **,qa_error *);
bool application_native_q2_source_damage_activate(struct application_native_q2_source_damage *,qa_error *);
bool application_native_q2_source_damage_admit(struct application_native_q2_source_damage *,qa_actor_id,qa_error *);
bool application_native_q2_source_damage_reaction(struct application_native_q2_source_damage *,qa_actor_id,
    const qa_damage_result *,qa_attack *,float *,bool *,qa_error *);
bool application_native_q2_source_damage_reaction_attack(struct application_native_q2_source_damage *,
    qa_actor_id,qa_actor_id,qa_actor_id,qa_native_address,qa_attack *,qa_error *);
void application_native_q2_source_damage_released(struct application_native_q2_source_damage *,qa_actor_id);
bool application_native_q2_source_damage_idle(const struct application_native_q2_source_damage *);
bool application_native_q2_source_damage_returned(const struct application_native_q2_source_damage *);
bool application_native_q2_source_damage_suspend(struct application_native_q2_source_damage *,qa_error *);
bool application_native_q2_source_damage_destroy(struct application_native_q2_source_damage **,qa_error *);
bool application_native_q2_source_damage_capture(struct application_native_q2_source_damage *,qa_buffer *,qa_error *);
bool application_native_q2_source_damage_restore(struct application_native_q2_source_damage *,qa_bytes,qa_error *);
bool application_native_q2_source_damage_saved_binding(struct application_native_q2_source_damage *,
    qa_actor_id,uint64_t,qa_combat_binding *,qa_error *);
bool application_native_q2_source_damage_finish_restore(struct application_native_q2_source_damage *,qa_error *);
#endif
