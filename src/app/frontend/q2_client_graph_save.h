#ifndef QA_FRONTEND_Q2_CLIENT_GRAPH_SAVE_H
#define QA_FRONTEND_Q2_CLIENT_GRAPH_SAVE_H
#include "network_q2_restore.h"
typedef struct frontend_q2_client_graph frontend_q2_client_graph;
bool frontend_q2_client_graph_checkpoint(qa_frontend *,const frontend_remote_q2_restore_refs *,qa_buffer *,qa_error *);
bool frontend_q2_client_graph_decode(qa_frontend *,qa_bytes,frontend_q2_client_graph **,qa_error *);
bool frontend_q2_client_graph_stage(frontend_q2_client_graph *,const frontend_remote_q2_restore_refs *,const qa_console_save_resolvers *,qa_error *);
bool frontend_q2_client_graph_prepare(frontend_q2_client_graph *,qa_error *);
bool frontend_q2_client_graph_roots(frontend_q2_client_graph *,const frontend_remote_q2_restore_refs *,qa_error *);
bool frontend_q2_client_graph_finish(frontend_q2_client_graph *,const frontend_remote_q2_restore_refs *,qa_error *);
void frontend_q2_client_graph_destroy(frontend_q2_client_graph *);
#endif
