#ifndef QA_FRONTEND_CLASSIC_CLIENT_GRAPH_SAVE_H
#define QA_FRONTEND_CLASSIC_CLIENT_GRAPH_SAVE_H
#include "network_q1_restore.h"
typedef struct frontend_classic_client_graph frontend_classic_client_graph;
bool frontend_classic_client_graph_capture_numbers(qa_frontend *,frontend_scene_namespace *,qa_error *);
bool frontend_classic_client_graph_checkpoint(qa_frontend *,const frontend_remote_q1_restore_refs *,qa_buffer *,qa_error *);
bool frontend_classic_client_graph_decode(qa_frontend *,qa_bytes,frontend_classic_client_graph **,qa_error *);
bool frontend_classic_client_graph_stage(frontend_classic_client_graph *,const frontend_remote_q1_restore_refs *,
    const qa_console_save_resolvers *,qa_error *);
bool frontend_classic_client_graph_prepare(frontend_classic_client_graph *,qa_error *);
bool frontend_classic_client_graph_roots(frontend_classic_client_graph *,const frontend_remote_q1_restore_refs *,qa_error *);
bool frontend_classic_client_graph_finish(frontend_classic_client_graph *,const frontend_remote_q1_restore_refs *,qa_error *);
void frontend_classic_client_graph_destroy(frontend_classic_client_graph *);
#endif
