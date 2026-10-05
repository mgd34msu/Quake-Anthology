#ifndef QA_APPLICATION_UNIFIED_Q3_EVENTS_H
#define QA_APPLICATION_UNIFIED_Q3_EVENTS_H

#include "internal.h"

typedef enum application_unified_q3_text_kind {
    APPLICATION_Q3_SOURCE_PRINT, APPLICATION_Q3_SOURCE_LOG,
    APPLICATION_Q3_SOURCE_COMMAND, APPLICATION_Q3_SOURCE_DROP
} application_unified_q3_text_kind;
bool application_unified_q3_text(application_provider *, application_unified_q3_text_kind,
    int32_t client, const char *, qa_error *);
bool application_unified_q3_configstring(application_provider *, uint32_t, const char *, qa_error *);
bool application_unified_q3_console(application_provider *, bool execute_now, const char *, qa_error *);
bool application_unified_q3_participant(void *, qa_actor_id, int32_t, int32_t,
    qa_vec3, int32_t, qa_error *);
bool application_unified_q3_attack_providers(void *, qa_actor_id, qa_item_id,
    qa_actor_owner *, qa_actor_owner *, qa_error *);

struct application_q3_component_publication;
struct application_q3_scene_player_event;
bool application_unified_q3_component_player(qa_application *,
    const struct application_q3_component_publication *,const struct application_q3_scene_player_event *,qa_error *);
bool application_unified_q3_component_command(qa_application *,
    const struct application_q3_component_publication *, qa_actor_id recipient,
    const char *text, int32_t source_time_ms, qa_error *);

#endif
