#ifndef APPLICATION_NATIVE_Q2_SOURCE_ACTORS_H
#define APPLICATION_NATIVE_Q2_SOURCE_ACTORS_H
#include "native_q2_callbacks.h"
struct application_native_q2_source_actors;
bool application_native_q2_source_actors_declared(const struct application_native_q2 *);
bool application_native_q2_source_actors_admit(struct application_native_q2 *,uint32_t,qa_actor_id,qa_error *);
bool application_native_q2_source_actors_refresh(struct application_native_q2 *,qa_error *);
bool application_native_q2_source_actors_touch(struct application_native_q2 *,
    const qa_touch_contact *,bool *,qa_error *);
bool application_native_q2_source_actors_reaction(struct application_native_q2 *,
    const qa_damage_outcome *,bool *,qa_error *);
void application_native_q2_source_actors_released(struct application_native_q2 *,qa_actor_id);
bool application_native_q2_source_actors_idle(const struct application_native_q2_source_actors *);
bool application_native_q2_source_actors_returned(const struct application_native_q2_source_actors *);
bool application_native_q2_source_actors_suspend(struct application_native_q2 *,qa_error *);
bool application_native_q2_source_actors_close(struct application_native_q2 *,qa_error *);
bool application_native_q2_source_actors_damage_capture(struct application_native_q2 *,qa_buffer *,qa_error *);
bool application_native_q2_source_actors_combat_binding(struct application_native_q2 *,qa_actor_id,uint64_t,
    qa_combat_binding *,qa_error *);
#endif
