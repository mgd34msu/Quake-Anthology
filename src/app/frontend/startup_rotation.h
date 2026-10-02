#ifndef QA_FRONTEND_STARTUP_ROTATION_H
#define QA_FRONTEND_STARTUP_ROTATION_H
#include "internal.h"
typedef struct frontend_startup_rotation frontend_startup_rotation;
bool frontend_startup_rotation_create(frontend_seat *,qa_ui_id,frontend_startup_rotation **,qa_error *);
bool frontend_startup_rotation_idle(const frontend_startup_rotation *);
bool frontend_startup_rotation_destroy(frontend_startup_rotation **,qa_error *);
bool frontend_startup_rotation_open(frontend_startup_rotation *,qa_error *);
bool frontend_startup_rotation_checkpoint(const frontend_startup_rotation *,qa_buffer *,qa_error *);
bool frontend_startup_rotation_restore(frontend_startup_rotation *,qa_bytes,qa_error *);
#endif
