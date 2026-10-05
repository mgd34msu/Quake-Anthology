#ifndef QA_FRONTEND_QC_MESSAGES_H
#define QA_FRONTEND_QC_MESSAGES_H
#include "qa/frontend.h"
#include "qa/application_qc_presentation.h"
#include "../application/unified_player.h"
#include "qa/unified_frame_metadata.h"
typedef struct frontend_qc_messages frontend_qc_messages;
typedef struct frontend_qc_camera_receipt {
    qa_application_qc_message_source source;
    qa_actor_id recipient,view_entity;
    uint32_t source_slot,intermission;
    float angles[3];
    uint64_t view_sequence,angle_sequence;
    bool has_view,has_angles;
} frontend_qc_camera_receipt;
typedef struct frontend_qc_unified_player_receipt {
    const frontend_qc_messages *owner;
    qa_application *application;
    application_unified_source source;
    qa_net_client_id client;
    qa_unified_session_player player;
    frontend_qc_camera_receipt camera;
    application_unified_player_camera player_camera;
    qa_application_qc_client_presentation declared_vitals, declared_view;
    frontend_qc_camera_receipt declared_receipt;
    qa_application_camera_view declared_camera;
    application_unified_player_external external;
} frontend_qc_unified_player_receipt;
/* The output owner retains this receipt through synchronous player/output
 * capture. present=false means no actual QC decoder or declared output. */
bool frontend_qc_messages_unified_player_read(const frontend_qc_messages *,qa_application *,
    const application_unified_source *,qa_net_client_id,const qa_unified_session_player *,
    frontend_qc_unified_player_receipt *,bool *present,qa_error *);
bool frontend_qc_messages_create(qa_frontend *,frontend_qc_messages **,qa_error *);
bool frontend_qc_messages_idle(const frontend_qc_messages *);
bool frontend_qc_messages_destroy(frontend_qc_messages **,qa_error *);
bool frontend_qc_messages_drain(frontend_qc_messages *,qa_error *);
bool frontend_qc_messages_camera_read(const frontend_qc_messages *,qa_actor_owner,qa_actor_id,
    frontend_qc_camera_receipt *,qa_error *);
bool frontend_qc_messages_camera_current(const frontend_qc_messages *,const frontend_qc_camera_receipt *);
/* First admitted declared output in the actual configured Source order.
 * These reads borrow existing decoded messages and raw QC fields only. */
bool frontend_qc_messages_client_vitals(const frontend_qc_messages *,qa_actor_id,
    qa_application_qc_client_presentation *,bool *found,qa_error *);
bool frontend_qc_messages_client_camera(const frontend_qc_messages *,qa_actor_id,
    qa_application_camera_view *,bool *found,qa_error *);
bool frontend_qc_messages_stat_read(const frontend_qc_messages *,qa_actor_owner,qa_actor_id,
    uint32_t index,int32_t *value,bool *present,qa_error *);
bool frontend_qc_messages_q1_world_read(const frontend_qc_messages *,uint32_t physical_seat,
    qa_unified_q1_world_state *,bool *present,qa_error *);
bool frontend_qc_messages_checkpoint(const frontend_qc_messages *,qa_buffer *,qa_error *);
bool frontend_qc_messages_restore(qa_frontend *,qa_bytes,frontend_qc_messages **,qa_error *);
#endif
