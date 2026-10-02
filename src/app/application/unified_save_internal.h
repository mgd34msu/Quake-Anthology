#ifndef QA_APPLICATION_UNIFIED_SAVE_INTERNAL_H
#define QA_APPLICATION_UNIFIED_SAVE_INTERNAL_H

#include "unified_output.h"
#include "qa/source_save.h"

bool application_unified_save_source_read(qa_application *, application_unified_source *, qa_error *);
bool application_unified_save_player_read(qa_application *, qa_net_client_id, qa_net_seat_id,
    qa_unified_session_player *, qa_error *);
bool application_unified_save_magic(qa_source_save_io *, const char [4]);
bool application_unified_save_blob(qa_source_save_io *, qa_buffer *);
bool application_unified_save_document(qa_source_save_io *, qa_unified_document **, qa_unified_document_kind);
bool application_unified_save_output(qa_source_save_io *, application_unified_output *);
bool application_unified_save_client(qa_source_save_io *, qa_net_client_id);
bool application_unified_save_source(qa_source_save_io *, qa_application *,
    const application_unified_source *, application_unified_source *, bool historical);
bool application_unified_save_player(qa_source_save_io *, const qa_unified_session_player *);
bool application_unified_save_output_equal(const application_unified_output *, const application_unified_output *);

#endif
