#ifndef APPLICATION_NATIVE_Q2_SOURCE_COMBAT_STATE_H
#define APPLICATION_NATIVE_Q2_SOURCE_COMBAT_STATE_H
#include "native_q2_armor.h"
#include "qa/game_q2.h"
struct application_native_q2;
typedef struct application_native_q2_source_combat_state application_native_q2_source_combat_state;
bool application_native_q2_source_combat_state_create(struct application_native_q2 *,
    application_native_q2_source_combat_state **,qa_error *);
bool application_native_q2_source_combat_state_destroy(application_native_q2_source_combat_state **,qa_error *);
bool application_native_q2_source_combat_state_read(application_native_q2_source_combat_state *,
    qa_actor_id,qa_combat_state *,qa_error *);
bool application_native_q2_source_combat_projection_read(application_native_q2_source_combat_state *,
    qa_actor_id,qa_combat_state *,qa_error *);
bool application_native_q2_source_combat_projection_health(application_native_q2_source_combat_state *,
    qa_actor_id,qa_native_address *,size_t *,qa_error *);
bool application_native_q2_source_combat_projection_armor_observe(application_native_q2_source_combat_state *,
    qa_actor_id,application_native_q2_armor_changed_fn,void *,application_native_q2_armor_watch **,qa_error *);
bool application_native_q2_source_combat_health_write(application_native_q2_source_combat_state *,
    qa_actor_id,float,qa_error *);
bool application_native_q2_source_combat_armor_validate(application_native_q2_source_combat_state *,
    qa_actor_id,const qa_armor *,qa_error *);
bool application_native_q2_source_combat_armor_write(application_native_q2_source_combat_state *,
    qa_actor_id,const qa_armor *,qa_error *);
bool application_native_q2_source_combat_armor_normalize(application_native_q2_source_combat_state *,
    qa_actor_id,const qa_armor *,qa_armor *,qa_error *);
bool application_native_q2_source_combat_cause_read(application_native_q2_source_combat_state *,
    const qa_native_value *,uint32_t,qa_damage_cause *,qa_error *);
bool application_native_q2_source_combat_cause_lower(application_native_q2_source_combat_state *,
    const qa_damage_cause *,uint8_t[3],qa_native_value *,qa_error *);
#endif
