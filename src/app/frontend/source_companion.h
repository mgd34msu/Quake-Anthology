#ifndef QA_FRONTEND_SOURCE_COMPANION_H
#define QA_FRONTEND_SOURCE_COMPANION_H
#include "internal.h"
#include "qa/application_q3_arsenal_client.h"
typedef struct frontend_source_companion_view {
    qa_application_q3_arsenal_client receipt;
    qa_q3_presentation_assets *assets;
    uint64_t frame_sequence;
    int32_t source_time_ms;
    size_t packet_count;
} frontend_source_companion_view;
typedef struct frontend_source_companion_packet {
    qa_q3_refdef definition;
    const qa_q3_ref_entity *entities;
    const qa_actor_id *entity_actors;
    const bool *entity_views;
    size_t entity_count;
} frontend_source_companion_packet;
bool frontend_source_companion_read(const qa_frontend *,uint32_t physical,qa_actor_id,
    frontend_source_companion_view *,bool *present,qa_error *);
bool frontend_source_companion_current(const qa_frontend *,const frontend_source_companion_view *);
bool frontend_source_companion_packet_read(const qa_frontend *,const frontend_source_companion_view *,size_t,
    frontend_source_companion_packet *,qa_error *);
#endif
