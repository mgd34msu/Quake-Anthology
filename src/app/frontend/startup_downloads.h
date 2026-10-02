#ifndef QA_FRONTEND_STARTUP_DOWNLOADS_H
#define QA_FRONTEND_STARTUP_DOWNLOADS_H
#include "internal.h"
typedef struct frontend_startup_downloads frontend_startup_downloads;
bool frontend_startup_downloads_create(frontend_seat *,qa_ui_id,qa_ui_id,frontend_startup_downloads **,qa_error *);
bool frontend_startup_downloads_idle(const frontend_startup_downloads *);
bool frontend_startup_downloads_destroy(frontend_startup_downloads **,qa_error *);
bool frontend_startup_downloads_open(frontend_startup_downloads *,qa_error *);
bool frontend_startup_downloads_checkpoint(const frontend_startup_downloads *,qa_buffer *,qa_error *);
bool frontend_startup_downloads_restore(frontend_startup_downloads *,qa_bytes,qa_error *);
#endif
