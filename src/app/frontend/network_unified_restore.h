#ifndef QA_FRONTEND_NETWORK_UNIFIED_RESTORE_H
#define QA_FRONTEND_NETWORK_UNIFIED_RESTORE_H
#include "network_unified_client.h"
bool frontend_network_unified_service_checkpoint(qa_frontend *,const qa_application_content_graph *,
    bool *present,qa_buffer *,qa_error *);
bool frontend_network_unified_service_stage(qa_frontend *,qa_application_content_graph *,
    const qa_console_save_resolvers *,qa_bytes,qa_error *);
bool frontend_network_unified_finish_import(qa_frontend *,qa_error *);
bool frontend_network_unified_replica_import_read(qa_frontend *,qa_net_client_id *,
    frontend_remote_unified **,bool *present,qa_error *);
bool frontend_network_unified_replica_capture_read(qa_frontend *,qa_net_client_id *,
    frontend_remote_unified **,bool *present,qa_error *);
#endif
