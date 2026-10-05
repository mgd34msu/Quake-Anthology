#ifndef QA_FRONTEND_REMOTE_Q2_RESTORE_H
#define QA_FRONTEND_REMOTE_Q2_RESTORE_H
#include "remote_q2_client.h"
#include "qa/network_downloads_save.h"

bool frontend_remote_q2_download_refs(frontend_remote_q2 *, qa_download_checkpoint_refs *, qa_error *);
bool frontend_remote_q2_import_read(const frontend_remote_q2 *, frontend_remote_q2_view *, qa_error *);
#endif
