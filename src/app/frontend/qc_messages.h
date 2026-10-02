#ifndef QA_FRONTEND_QC_MESSAGES_H
#define QA_FRONTEND_QC_MESSAGES_H
#include "qa/frontend.h"
#include "qa/application_qc_presentation.h"
typedef struct frontend_qc_messages frontend_qc_messages;
typedef struct frontend_qc_camera_receipt {
    qa_application_qc_message_source source;
    qa_actor_id recipient,view_entity;
    uint32_t source_slot,intermission;
    float angles[3];
    uint64_t view_sequence,angle_sequence;
    bool has_view,has_angles;
} frontend_qc_camera_receipt;
bool frontend_qc_messages_create(qa_frontend *,frontend_qc_messages **,qa_error *);
bool frontend_qc_messages_idle(const frontend_qc_messages *);
bool frontend_qc_messages_destroy(frontend_qc_messages **,qa_error *);
bool frontend_qc_messages_drain(frontend_qc_messages *,qa_error *);
bool frontend_qc_messages_camera_read(const frontend_qc_messages *,qa_actor_owner,qa_actor_id,
    frontend_qc_camera_receipt *,qa_error *);
bool frontend_qc_messages_camera_current(const frontend_qc_messages *,const frontend_qc_camera_receipt *);
bool frontend_qc_messages_stat_read(const frontend_qc_messages *,qa_actor_owner,qa_actor_id,
    uint32_t index,int32_t *value,bool *present,qa_error *);
bool frontend_qc_messages_checkpoint(const frontend_qc_messages *,qa_buffer *,qa_error *);
bool frontend_qc_messages_restore(qa_frontend *,qa_bytes,frontend_qc_messages **,qa_error *);
#endif
