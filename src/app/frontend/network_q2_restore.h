#ifndef QA_FRONTEND_NETWORK_Q2_RESTORE_H
#define QA_FRONTEND_NETWORK_Q2_RESTORE_H
#include "network_q2_client.h"
typedef struct frontend_network_q2_client_recipe {
    qa_net_address remote;
    qa_net_protocol_id protocol;
    uint16_t qport;
    uint32_t physical_seat;
} frontend_network_q2_client_recipe;
bool frontend_network_q2_checkpoint(qa_frontend *,const frontend_remote_q2_restore_refs *,
    bool *present,frontend_network_q2_client_state *,qa_error *);
/* Candidate runtime already carries its genuine decoded connection identity.
 * Graph recipes and observer actor references belong to the enclosing import. */
bool frontend_network_q2_prepare_import(qa_frontend *,qa_network_runtime *,
    const frontend_network_q2_client_recipe *,const frontend_network_q2_client_restore *,qa_error *);
bool frontend_network_q2_import_read(const qa_frontend *,frontend_remote_q2_source_view *,qa_error *);
bool frontend_network_q2_finish_import(qa_frontend *,const frontend_remote_q2_restore_refs *,qa_error *);
#endif
