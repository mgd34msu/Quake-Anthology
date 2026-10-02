#ifndef QA_FRONTEND_NETWORK_Q1_RESTORE_H
#define QA_FRONTEND_NETWORK_Q1_RESTORE_H
#include "network_q1_client.h"
typedef struct frontend_network_q1_client_recipe {
    qa_net_address remote,connected_remote;
    qa_net_protocol_id protocol;
    qa_product_id profile;
    uint32_t physical_seat;
    uint16_t qport;
    qa_network_q1_client_policy policy;
    qa_net_client_id client;
    qa_net_seat_id seat;
    uint64_t epoch;
    qa_sha256_digest composition;
    bool retired;
} frontend_network_q1_client_recipe;
bool frontend_network_q1_checkpoint(qa_frontend *,const frontend_remote_q1_restore_refs *,bool *present,
    frontend_network_q1_client_recipe *,frontend_network_q1_client_state *,qa_error *);
bool frontend_network_q1_stage_recipe(qa_frontend *,const frontend_network_q1_client_recipe *,
    const frontend_remote_q1_restore_refs *,const qa_console_save_resolvers *,
    const frontend_network_q1_client_state *,qa_error *);
bool frontend_network_q1_finish_import(qa_frontend *,const frontend_remote_q1_restore_refs *,qa_error *);
#endif
